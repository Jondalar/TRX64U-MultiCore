#include "writeback.h"
#include "corefolder.h"
#include "mediamounts.h"
#include <stdio.h>
#include <string.h>

// One medium at a time, one set of state: the cartridge in the port, or a console's battery RAM. The change source runs in the interrupt (wb_mark),
// everything else in tasks; the map, the timestamps and the "in flight" copy are touched under port->enter()/leave().
namespace {

struct WbState {
    CoreCtx *ctx;
    WbPort *port;
    int mode;
    bool have;
    WbMedium m;
    volatile uint32_t pend[WB_BLOCKS_MAX / 32];
    uint32_t flight[WB_BLOCKS_MAX / 32];
    volatile bool aux_pend;
    bool aux_flight;
    volatile bool any;
    volatile bool flush_req;
    volatile uint32_t first_ms, last_ms;
    bool told;                              // "cannot be written" was said for the changes up to now
    bool retry;                             // after a failure: not again before retry_at
    uint32_t retry_at;
};
WbState g;

uint32_t now_ms(void) { return g.port ? g.port->ticks_ms() : 0; }
void enter(void) { if (g.port) g.port->enter(); }
void leave(void) { if (g.port) g.port->leave(); }

void note_change(void)
{
    uint32_t now = now_ms();
    if (!g.any) {
        g.first_ms = now;
    }
    g.last_ms = now;
    g.any = true;
    g.told = false;
}

void clear_all(void)
{
    enter();
    memset((void *)g.pend, 0, sizeof(g.pend));
    memset(g.flight, 0, sizeof(g.flight));
    g.aux_pend = g.aux_flight = false;
    g.any = false;
    g.flush_req = false;
    g.told = false;
    g.retry = false;
    leave();
}

// The map of changes moves to "in flight": what this write is about. A failed write puts it back.
void begin(WbDirty *d)
{
    enter();
    for (int i = 0; i < WB_BLOCKS_MAX / 32; i++) {
        g.flight[i] |= g.pend[i];
        g.pend[i] = 0;
        d->blocks[i] = g.flight[i];
    }
    g.aux_flight = g.aux_flight || g.aux_pend;
    g.aux_pend = false;
    d->aux = g.aux_flight;
    g.any = false;
    g.flush_req = false;
    leave();
}

void end(bool ok)
{
    enter();
    if (!ok) {
        bool any = g.aux_flight;
        for (int i = 0; i < WB_BLOCKS_MAX / 32; i++) {
            g.pend[i] |= g.flight[i];
            any = any || g.flight[i];
        }
        g.aux_pend = g.aux_pend || g.aux_flight;
        if (any && !g.any) {
            g.first_ms = g.last_ms = now_ms();
        }
        g.any = g.any || any;
    }
    memset(g.flight, 0, sizeof(g.flight));
    g.aux_flight = false;
    leave();
}

void tell(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
    printf("[writeback] %s\n", text);
}

// Can the file be written at all? A write protected card, a read only mount, the internal flash (the board refuses it) say no here.
bool target_writable(CoreCtx *c)
{
    CoreFile *f = c->fs->open(g.m.path, true);
    if (!f) {
        uint64_t sz;
        bool dir;
        return g.m.may_create && !c->fs->stat(g.m.path, &sz, &dir);   // not there yet: the write itself finds out whether the folder takes it
    }
    bool ok = f->writable();
    c->fs->close(f);
    return ok;
}

bool exists(CoreFs *fs, const char *path)
{
    uint64_t sz;
    bool dir;
    return fs->stat(path, &sz, &dir);
}

// `path` with `ext` behind it. A FAT name holds 255 characters: beside a file whose name is that long the name part is cut so that the whole stays one
// (NAME.tmp and NAME.bak of the same file stay two names: the extensions differ).
void side_path(char *out, size_t n, const char *path, const char *ext)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t pre = (size_t)(base - path), lb = strlen(base), le = strlen(ext);
    size_t keep = lb + le > FS_NAME_MAX ? FS_NAME_MAX - le : lb;
    if (pre + keep + le + 1 > n) {
        out[0] = 0;                                    // does not fit: no name, the callers' open / save of "" fails
        return;
    }
    memcpy(out, path, pre + keep);
    memcpy(out + pre + keep, ext, le + 1);
}

// NAME.tmp complete, NAME.bak taken once, NAME.tmp becomes NAME. The file as the user had it stays recoverable until the first write is replaced
// by a later one; a failure on the way leaves either NAME or NAME.tmp whole.
int replace_file(CoreCtx *c, const char *tmp, char *msg, int msglen)
{
    CoreFs *fs = c->fs;
    char bak[FS_PATH_MAX + 8];
    side_path(bak, sizeof(bak), g.m.path, ".bak");
    bool ok;
    if (!exists(fs, g.m.path)) {
        ok = true;                                     // a first save: nothing to keep
    } else if (exists(fs, bak)) {
        ok = fs->remove(g.m.path) || !exists(fs, g.m.path);
    } else {
        ok = fs->rename(g.m.path, bak);
    }
    if (!ok) {
        fs->remove(tmp);
        snprintf(msg, msglen, "%s: the old file could not be moved aside", g.m.name);
        return 1;
    }
    if (!fs->rename(tmp, g.m.path)) {
        snprintf(msg, msglen, "%s: saved, but renaming %s.tmp failed - the new state is in that file", g.m.name, g.m.name);
        return 1;
    }
    return 0;
}

// The whole write. The caller has decided that the file is to be written and that it can be.
int write_now(CoreCtx *c, char *msg, int msglen)
{
    CoreFs *fs = c->fs;
    char tmp[FS_PATH_MAX + 8];
    side_path(tmp, sizeof(tmp), g.m.path, ".tmp");
    CoreFile *old = fs->open(g.m.path, false);
    if (!old && !g.m.may_create) {
        snprintf(msg, msglen, "%s: cannot be opened (the medium is gone?)", g.m.name);
        return 1;
    }
    if (!fs->save(tmp, "", 0)) {
        if (old) {
            fs->close(old);
        }
        snprintf(msg, msglen, "%s: cannot write %s.tmp", g.m.name, g.m.name);
        return 1;
    }
    CoreFile *out = fs->open(tmp, true);
    if (!out || !out->writable()) {
        if (out) {
            fs->close(out);
        }
        if (old) {
            fs->close(old);
        }
        fs->remove(tmp);
        snprintf(msg, msglen, "%s: the medium cannot be written", g.m.name);
        return 1;
    }
    WbDirty d;
    memset(&d, 0, sizeof(d));
    if (g.m.hold) {
        g.m.hold(g.m.user, true);                  // the machine sees its flash busy while the image is read: one state
    }
    begin(&d);
    int rc = g.m.write(c, g.m.user, old, out, d, msg, msglen);
    if (g.m.hold) {
        g.m.hold(g.m.user, false);
    }
    bool synced = rc == 0 && out->sync();
    fs->close(out);
    if (old) {
        fs->close(old);
    }
    if (rc || !synced) {
        fs->remove(tmp);
        end(false);
        if (!rc) {
            snprintf(msg, msglen, "%s: writing failed (medium full or gone)", g.m.name);
        }
        return 1;
    }
    rc = replace_file(c, tmp, msg, msglen);
    end(rc == 0);
    if (rc == 0) {
        g.retry = false;
        printf("[writeback] %s written\n", g.m.path);
        snprintf(msg, msglen, "%s saved", g.m.name);
    }
    return rc;
}

// The decision for a change that is due or asked for, with the checks that tell the user: returns 0 (nothing to do, or written), 1 (failed).
int try_write(CoreCtx *c, char *msg, int msglen)
{
    msg[0] = 0;
    if (!g.have || !g.any) {
        return 0;
    }
    if (!target_writable(c)) {
        if (!g.told) {
            g.told = true;
            snprintf(msg, msglen, "%s changed, but its file cannot be written (write protected or read only). The change stays in memory until the cartridge is removed.",
                     g.m.name);
            tell(c, msg);
        }
        return 0;
    }
    int rc = write_now(c, msg, msglen);
    if (rc) {
        g.retry = true;
        g.retry_at = now_ms() + WB_RETRY_MS;
        tell(c, msg);
    }
    return rc;
}

}  // namespace

void wb_attach(CoreCtx *c, WbPort *port)
{
    g.ctx = c;
    g.port = port;
    int i = c && c->settings ? c->settings->find("Save Changed Cartridge") : -1;
    g.mode = i >= 0 ? c->settings->value(i) : WB_ASK;
}

WbPort *wb_port(void) { return g.port; }

void wb_set_mode(int mode)
{
    g.mode = mode < WB_OFF ? WB_OFF : mode > WB_AUTO ? WB_AUTO : mode;
}

int wb_mode(void) { return g.mode; }

void wb_register(const WbMedium &m)
{
    clear_all();
    g.m = m;
    g.have = true;
}

void wb_unregister(void)
{
    clear_all();
    g.have = false;
}

bool wb_active(void) { return g.have; }
const char *wb_path(void) { return g.have ? g.m.path : ""; }

void wb_mark(uint32_t off, uint32_t len)
{
    if (!g.have || !len) {
        return;
    }
    uint32_t b0 = off >> WB_BLOCK_SHIFT, b1 = (off + len - 1) >> WB_BLOCK_SHIFT;
    for (uint32_t b = b0; b <= b1 && b < WB_BLOCKS_MAX; b++) {
        g.pend[b >> 5] |= 1u << (b & 31);
    }
    note_change();
}

void wb_mark_aux(void)
{
    if (!g.have) {
        return;
    }
    g.aux_pend = true;
    note_change();
}

bool wb_pending(void) { return g.have && g.any; }

bool wb_due(void)
{
    if (!wb_pending()) {
        return false;
    }
    uint32_t now = now_ms();
    return g.flush_req || (uint32_t)(now - g.last_ms) >= WB_QUIET_MS || (uint32_t)(now - g.first_ms) >= WB_MAX_AGE_MS;
}

void wb_request_flush(void) { g.flush_req = true; }

void wb_forget(void)
{
    WbDirty d;
    begin(&d);
    end(true);
    g.told = false;
}

int wb_flush(CoreCtx *c, WbWhy why, char *msg, int msglen)
{
    (void)why;
    msg[0] = 0;
    if (g.mode != WB_AUTO || !wb_pending()) {
        return 0;
    }
    return try_write(c, msg, msglen);
}

bool wb_menu_open(CoreCtx *c, char *question, int qlen, char *note, int nlen)
{
    question[0] = 0;
    note[0] = 0;
    if (g.mode == WB_OFF || !wb_pending()) {
        return false;
    }
    if (!target_writable(c)) {
        if (!g.told) {
            g.told = true;
            snprintf(note, nlen, "%s changed, but its file cannot be written (write protected or read only). The change stays in memory until the cartridge is removed.",
                     g.m.name);
        }
        return false;
    }
    if (g.mode == WB_ASK) {
        snprintf(question, qlen, "%s changed. Save it?", g.m.name);
        return true;
    }
    char m[160];
    if (write_now(c, m, sizeof(m))) {
        g.retry = true;
        g.retry_at = now_ms() + WB_RETRY_MS;
        snprintf(note, nlen, "%s", m);
    }
    return false;
}

int wb_answer(CoreCtx *c, bool yes, char *msg, int msglen)
{
    msg[0] = 0;
    if (!yes) {
        wb_forget();
        snprintf(msg, msglen, "The change is not saved");
        return 0;
    }
    if (!wb_pending()) {
        return 0;
    }
    return write_now(c, msg, msglen);
}

int wb_poll(CoreCtx *c)
{
    if (g.mode != WB_AUTO || !wb_due()) {
        return 0;
    }
    if (g.retry && (int32_t)(now_ms() - g.retry_at) < 0) {
        return 0;
    }
    char m[160];
    return try_write(c, m, sizeof(m));
}

// ---- the operations -------------------------------------------------------------------------------------------------------------------------
static int op_mode(CoreCtx *, const OpArgs &a, OpOut &)
{
    wb_set_mode(a.i(0));
    return OPE_OK;
}

static int op_poll(CoreCtx *c, const OpArgs &, OpOut &)
{
    wb_poll(c);
    mounts_pass(c);                                            // the slots that a library notified (SvcCtx::mount_notify)
    return OPE_OK;
}

static const ArgSpec mode_args[] = { ARG_INT("value", 0, 2) };

// T192.3 leak 4: the question at the menu's open is the module's flow, not the controller's. writeback.open runs when the menu has opened with the machine held (the
// controller's flow table, menuflows.cc): Auto writes now, a file that cannot be written is told once (the answer's text, a note for the status line); Ask with a change
// puts the question into the state writeback.ask, which the controller shows as its confirmation. writeback.answer(value 1 = yes, 0 = no) is the one way out of it; the
// answer's text is the status line, a failure is a failed operation. A question that was left unanswered (the person went to another page) is asked again at the next open.
static char g_ask[120];
static const char *st_ask(CoreCtx *) { return g_ask[0] ? g_ask : 0; }

static int op_open(CoreCtx *c, const OpArgs &, OpOut &o)
{
    g_ask[0] = 0;
    if (wb_menu_open(c, g_ask, sizeof(g_ask), o.msg, o.msglen)) {
        return OPE_OK;
    }
    g_ask[0] = 0;
    return OPE_OK;
}

static int op_answer(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    g_ask[0] = 0;
    return wb_answer(c, a.i(0) != 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// The person left the question unanswered: it is not up any more, and the change is still pending (the next open asks again).
static int op_leave(CoreCtx *, const OpArgs &, OpOut &)
{
    g_ask[0] = 0;
    return OPE_OK;
}
static const ArgSpec answer_args[] = { ARG_INT("value", 0, 1) };

void wb_register_ops(OpRegistry &r)
{
    r.add_op("writeback.mode", mode_args, 1, op_mode);
    r.add_op("writeback.poll", 0, 0, op_poll);
    r.add_op("writeback.open", 0, 0, op_open);
    r.add_op("writeback.answer", answer_args, 1, op_answer);
    r.add_op("writeback.leave", 0, 0, op_leave);
    r.add_state("writeback.ask", st_ask);
}

// ---- a raw image -----------------------------------------------------------------------------------------------------------------------------
int wb_raw_write(CoreCtx *, void *user, CoreFile *, CoreFile *out, const WbDirty &, char *msg, int msglen)
{
    const WbRaw *r = (const WbRaw *)user;
    static uint8_t buf[4096];
    uint32_t pos = 0;
    while (pos < r->size) {
        uint32_t n = r->size - pos < sizeof(buf) ? r->size - pos : (uint32_t)sizeof(buf);
        if (r->read(r->user, pos, buf, n) || !out->write(pos, buf, n)) {
            snprintf(msg, msglen, "the save file could not be written");
            return 1;
        }
        pos += n;
    }
    return 0;
}

bool wb_save_path(CoreCtx *c, const char *game_path, char *out, int outlen)
{
    const char *slash = strrchr(game_path, '/');
    const char *name = slash ? slash + 1 : game_path;
    char file[64];
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (stem <= 0 || stem + 5 > (int)sizeof(file)) {
        return false;
    }
    memcpy(file, name, stem);
    strcpy(file + stem, ".sav");
#if WB_SAVES_IN_CORE_FOLDER
    if (c && c->cor_path[0] && corefolder_save_path(c->cor_path, file, out, outlen)) {
        return true;
    }
#else
    (void)c;
#endif
    int dirlen = slash ? (int)(slash - game_path) + 1 : 0;
    if (dirlen + (int)strlen(file) + 1 > outlen) {
        return false;
    }
    memcpy(out, game_path, dirlen);
    strcpy(out + dirlen, file);
    return true;
}

int wb_raw_load(CoreCtx *c, const char *path, void *buf, uint32_t size)
{
    int n = c->fs->load(path, buf, size);
    return n < 0 ? 0 : n;
}
