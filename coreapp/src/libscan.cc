#include "libscan.h"
#include "libmenu.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

const char *const lib_roots[] = { "/usb0", "/usb1", "/usb2", "/usb3", 0 };

// ---- the wait for the stick ---------------------------------------------------------------------------------------------------------

static bool root_lists(CoreFs *fs)
{
    FsEntry probe[1];
    for (int r = 0; lib_roots[r]; r++) {
        if (fs->list(lib_roots[r], probe, 1) >= 0) {
            return true;
        }
    }
    return false;
}

// "/" names the devices the USB stack has installed; lib_roots[r] is "/usbN". An installed device whose medium is not attached yet lists "/usbN" as an
// error until it is.
static bool root_installed(CoreFs *fs)
{
    FsEntry top[16];
    int n = fs->list("/", top, 16);
    for (int i = 0; i < n; i++) {
        for (int r = 0; lib_roots[r]; r++) {
            if (!strcasecmp(top[i].name, lib_roots[r] + 1)) {
                return true;
            }
        }
    }
    return false;
}

void lib_wait_for_stick(CoreFs *fs, LibMountEnv *env, bool roms_in_flash, LibWaitResult *out)
{
    memset(out, 0, sizeof(*out));
    if (root_lists(fs)) {
        out->present = true;
        snprintf(out->why, sizeof(out->why), "a stick root lists already");
        return;
    }
    const char *reason;
    if (root_installed(fs)) {
        reason = "the USB stack installed a stick, its medium is not attached yet";
    } else if (roms_in_flash) {
        snprintf(out->why, sizeof(out->why), "the ROMs come from /flash and no stick is installed: not waiting");
        return;
    } else if (!env->usb_storage_attached()) {
        snprintf(out->why, sizeof(out->why), "no USB mass-storage device attached: not waiting");
        return;
    } else {
        reason = "a USB mass-storage device is enumerating";
    }
    out->waited = true;
    uint32_t t0 = env->now_ms();
    for (;;) {
        out->ms = env->now_ms() - t0;
        if (root_lists(fs)) {
            out->present = true;
            break;
        }
        if (out->ms >= LIB_MOUNT_WAIT_MS) {
            break;
        }
        env->delay_ms(100);
    }
    snprintf(out->why, sizeof(out->why), "%s: stick %s after %u ms", reason, out->present ? "present" : "not seen", (unsigned)out->ms);
}

// ---- the linked libraries and their menu records (record 7 version 2) -----------------------------------------------------------

namespace {

uint32_t le32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

// One menu record of record 7 inside the .cor, for libmenu_read.
class SliceSource : public LibSource {
public:
    SliceSource(CoreFile *f, uint32_t base, uint32_t len) : f_(f), base_(base), len_(len) { }
    uint32_t size() { return len_; }
    bool read(uint32_t off, void *dst, uint32_t n) { return (uint64_t)off + n <= len_ && f_->read((uint64_t)base_ + off, dst, n); }

private:
    CoreFile *f_;
    uint32_t base_, len_;
};

void say(LibLog *log, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void say(LibLog *log, const char *fmt, ...)
{
    char line[LIB_ERR_MAX + 96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    log->line(line);
}

void refuse(LibScanResult *r, const char *who, const char *sentence, char *first)
{
    if (!r->refused) {
        snprintf(first, 24, "%s", who);
    }
    r->refused++;
    if (r->nkept < LIB_REFUSALS_KEPT) {
        snprintf(r->kept[r->nkept++], LIB_ERR_MAX, "%s", sentence);
    }
}

struct LinkRun {
    LibLoader *ld;
    CoreFs *fs;
    const char *cor_path;
    LibCorMenus *menus;                  // null: the bundle carries none
    LibLog *log;
    LibScanResult *r;
    void (*before_first)(void *);
    void *ctx;
    char first[24];
};

// libtab_link asks for the menu record of a library before it registers it: the guard arms before the first library's code runs.
bool menu_of(void *user, const char *name, DescRecord **rec, uint8_t **bytes)
{
    LinkRun *x = (LinkRun *)user;
    if (x->before_first) {
        x->before_first(x->ctx);
        x->before_first = 0;
    }
    *rec = 0;
    *bytes = 0;
    LibCorMenus *m = x->menus;
    for (int i = 0; m && i < m->n; i++) {
        if (m->taken[i] || strcmp(m->name[i], name)) {
            continue;
        }
        m->taken[i] = true;
        CoreFile *f = x->fs->open(x->cor_path, false);
        if (!f) {
            say(x->log, "[lib] %s (linked) : its menu record cannot be read (%s does not open); it registers without one", name, x->cor_path);
            return false;
        }
        SliceSource src(f, m->off[i], m->len[i]);
        char why[160];
        int rc = libmenu_read(&src, m->len[i], 0, rec, bytes, why, sizeof(why));
        x->fs->close(f);
        if (rc != 0) {
            say(x->log, "[lib] %s (linked) : its menu record is refused (%s); it registers without one", name, why);
            return false;
        }
        return true;
    }
    return false;
}

void link_done(void *user, const char *name, int rc, const char *err)
{
    LinkRun *x = (LinkRun *)user;
    if (rc == LIB_OK) {
        say(x->log, "[lib] %s (linked) : ok", name);
        x->r->loaded++;
        return;
    }
    say(x->log, "[lib] %s (linked) : refused: %s", name, err);
    refuse(x->r, name, err, x->first);
}

}

int lib_cor_menus(CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibCorMenus *out, char *why, int whylen)
{
    memset(out, 0, sizeof(*out));
    CoreFile *f = fs->open(cor_path, false);
    uint8_t h[12];
    if (!f || rec_size < sizeof(h) || !f->read(rec_off, h, sizeof(h)) || memcmp(h, "TRXL", 4) != 0) {
        if (f) {
            fs->close(f);
        }
        snprintf(why, whylen, "The library record of the core bundle cannot be read.");
        return -1;
    }
    uint32_t ver = le32(h + 4), cnt = le32(h + 8);
    if (ver == 1) {
        fs->close(f);
        snprintf(why, whylen, "The core bundle carries library files (record 7 version 1): base 4.0 loads no library file; the bundle has to be packed again.");
        return -1;
    }
    bool ok = ver == 2 && cnt > 0 && cnt <= LIB_COR_MENUS_MAX;
    uint32_t p = 12;
    for (uint32_t i = 0; ok && i < cnt; i++) {
        uint8_t e[U2A3_NAME_MAX + 4];
        ok = (uint64_t)p + sizeof(e) <= rec_size && f->read(rec_off + p, e, sizeof(e)) && memchr(e, 0, U2A3_NAME_MAX) && e[0];
        if (!ok) {
            break;
        }
        uint32_t len = le32(e + U2A3_NAME_MAX);
        p += sizeof(e);
        ok = len > 0 && (uint64_t)p + len <= rec_size;
        for (int k = 0; ok && k < out->n; k++) {
            ok = strcmp(out->name[k], (const char *)e) != 0;          // one record per library
        }
        if (!ok) {
            break;
        }
        memcpy(out->name[out->n], e, U2A3_NAME_MAX);
        out->off[out->n] = rec_off + p;
        out->len[out->n] = len;
        out->n++;
        p += len + ((4 - (len & 3)) & 3);
    }
    fs->close(f);
    if (!ok || p != rec_size) {
        memset(out, 0, sizeof(*out));
        snprintf(why, whylen, "The library record of the core bundle is damaged (version %u, %u entries).", (unsigned)ver, (unsigned)cnt);
        return -1;
    }
    return 0;
}

void lib_link_cor(LibLoader *ld, CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibLinkAll link_all, LibLog *log, LibScanResult *r,
                  void (*before_first)(void *), void *ctx)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->summary, sizeof(r->summary), "none");
    LibCorMenus *menus = 0;
    LinkRun x;
    memset(&x, 0, sizeof(x));
    if (rec_size) {
        menus = new LibCorMenus;
        char why[LIB_ERR_MAX];
        if (lib_cor_menus(fs, cor_path, rec_off, rec_size, menus, why, sizeof(why)) != 0) {
            say(log, "[lib] %s: %s", cor_path, why);              // the libraries still register, without the menu records of the bundle
            refuse(r, "bundle", why, x.first);
            delete menus;
            menus = 0;
        }
    }
    x.ld = ld;
    x.fs = fs;
    x.cor_path = cor_path;
    x.menus = menus;
    x.log = log;
    x.r = r;
    x.before_first = before_first;
    x.ctx = ctx;
    if (link_all) {
        link_all(*ld, menu_of, link_done, &x);
    }
    for (int i = 0; menus && i < menus->n; i++) {
        if (!menus->taken[i]) {                                    // cor_pack packs a record only for a library of the TOML B it was given: another app's bundle
            char why[LIB_ERR_MAX];
            snprintf(why, sizeof(why), "The core bundle carries a menu record for \"%s\", which this app does not link with `register`.", menus->name[i]);
            say(log, "[lib] %s", why);
            refuse(r, menus->name[i], why, x.first);
        }
    }
    delete menus;
    if (r->refused) {
        snprintf(r->summary, sizeof(r->summary), "%d loaded, %d refused: %s", r->loaded, r->refused, x.first);
    } else if (r->loaded) {
        snprintf(r->summary, sizeof(r->summary), "%d loaded", r->loaded);
    }
}
