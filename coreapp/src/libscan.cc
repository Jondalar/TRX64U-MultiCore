#include "libscan.h"
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

// ---- the order ------------------------------------------------------------------------------------------------------------------

static bool is_lib(const LibMeta &m) { return m.valid && m.kind == U2A3_KIND_LIB; }

// Does file m give what a need of this name asks for: its own name, or a name it exports?
static bool gives(const LibMeta &m, const char *name)
{
    if (!strcmp(m.name, name)) {
        return true;
    }
    for (int e = 0; e < m.nexports; e++) {
        if (!strcmp(m.exp_name[e], name)) {
            return true;
        }
    }
    return false;
}

// Does file i need something that a file still to be loaded provides?
static bool waits(const LibMeta *m, int n, int i, const bool *placed)
{
    for (int k = 0; k < m[i].nneeds; k++) {
        if (!strcmp(m[i].need[k], "base")) {
            continue;
        }
        for (int j = 0; j < n; j++) {
            if (is_lib(m[j]) && !placed[j] && gives(m[j], m[i].need[k])) {
                return true;
            }
        }
    }
    return false;
}

int lib_dep_order(const LibMeta *m, int n, int *order, int *cycle)
{
    bool placed[LIB_SCAN_MAX];
    memset(placed, 0, sizeof(placed));
    int no = 0;
    // The first library in the given order whose providers are all in front goes next, so the order among independent files stays theirs.
    for (bool progress = true; progress;) {
        progress = false;
        for (int i = 0; i < n; i++) {
            if (!placed[i] && is_lib(m[i]) && !waits(m, n, i, placed)) {
                placed[i] = true;
                order[no++] = i;
                progress = true;
                break;
            }
        }
    }
    // What is left is a cycle or lies behind one. i reaches j when i needs what j provides; a library that reaches itself is in a cycle.
    bool reach[LIB_SCAN_MAX][LIB_SCAN_MAX];
    memset(reach, 0, sizeof(reach));
    for (int i = 0; i < n; i++) {
        cycle[i] = -1;
        if (!is_lib(m[i]) || placed[i]) {
            continue;
        }
        for (int k = 0; k < m[i].nneeds; k++) {
            for (int j = 0; j < n; j++) {
                if (is_lib(m[j]) && !placed[j] && gives(m[j], m[i].need[k]) && strcmp(m[i].need[k], "base")) {
                    reach[i][j] = true;
                }
            }
        }
    }
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                reach[i][j] = reach[i][j] || (reach[i][k] && reach[k][j]);
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (reach[i][i]) {
            cycle[i] = i;
            for (int j = 0; j < i; j++) {
                if (reach[i][j] && reach[j][i]) {
                    cycle[i] = cycle[j];
                    break;
                }
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (!placed[i] && is_lib(m[i]) && cycle[i] < 0) {      // behind a cycle
            order[no++] = i;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!is_lib(m[i])) {
            order[no++] = i;
        }
    }
    return no;
}

// ---- the folder -----------------------------------------------------------------------------------------------------------------

namespace {

struct Cand {
    char path[256];
    const char *file;                    // the part of path after the last slash
    uint32_t base;                       // a library inside a .cor: where it starts in the file (path), and how long it is; len 0: the whole file
    uint32_t len;
    LibMeta meta;
};

class FileSource : public LibSource {
public:
    FileSource(CoreFile *f, uint32_t base = 0, uint32_t len = 0) : f_(f), base_(base), len_(len) { }
    uint32_t size() { return len_ ? len_ : fs_size32(f_->size()); }
    bool read(uint32_t off, void *dst, uint32_t n) { return (!len_ || ((uint64_t)off + n <= len_)) && f_->read((uint64_t)base_ + off, dst, n); }

private:
    CoreFile *f_;
    uint32_t base_, len_;
};

bool has_ext(const char *name)
{
    size_t l = strlen(name), e = strlen(LIB_EXT);
    return l > e && !strcasecmp(name + l - e, LIB_EXT);
}

// Terminated inside its field, or the file is not one for the sorter (the loader says why).
bool name_in_field(const char *f)
{
    return memchr(f, 0, U2A3_NAME_MAX) != 0;
}

void read_meta(CoreFs *fs, Cand *c)
{
    LibMeta &m = c->meta;
    memset(&m, 0, sizeof(m));
    CoreFile *f = fs->open(c->path, false);
    if (!f) {
        return;
    }
    U2a3Header h;
    if (f->size() >= (uint64_t)c->base + sizeof(h) && (!c->len || c->len >= sizeof(h)) && f->read(c->base, &h, sizeof(h)) && h.magic == U2A3_MAGIC && h.header_size == U2A3_HEADER_SIZE &&
        h.format_version == U2A3_FORMAT_VERSION && name_in_field(h.name) && h.name[0] && h.needs_count <= U2A3_NEEDS_MAX) {
        bool ok = true;
        for (int i = 0; i < h.needs_count && ok; i++) {
            U2a3Need nd;
            ok = (!c->len || (uint64_t)h.needs_offset + (uint64_t)(i + 1) * U2A3_NEED_SIZE <= c->len) &&
                 f->read((uint64_t)c->base + h.needs_offset + (uint64_t)i * U2A3_NEED_SIZE, &nd, sizeof(nd)) && name_in_field(nd.name);
            if (ok) {
                memcpy(m.need[i], nd.name, U2A3_NAME_MAX);
            }
        }
        U2a3ExportName ex[U2A3_EXPORTS_MAX];
        int nex = 0;
        if (ok && (h.flags & U2A3_FLAG_EXPORTS)) {
            FileSource src(f, c->base, c->len);
            nex = lib_read_exports(&src, h, ex);
            ok = nex >= 0;                                  // a malformed list is the loader's refusal to word
            for (int e = 0; ok && e < nex; e++) {
                memcpy(m.exp_name[e], ex[e].name, U2A3_NAME_MAX);
            }
        }
        if (ok) {
            m.nexports = nex;
            m.valid = true;
            m.kind = h.kind;
            m.version = h.version;
            m.nneeds = h.needs_count;
            memcpy(m.name, h.name, U2A3_NAME_MAX);
        }
    }
    fs->close(f);
}

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

void refuse(LibScanResult *r, LibLog *log, const Cand &c, const char *reason, char *first)
{
    char who[48];
    if (c.meta.valid) {
        snprintf(who, sizeof(who), "%s v%u.%u", c.meta.name, (unsigned)(c.meta.version >> 16), (unsigned)(c.meta.version & 0xFFFF));
    } else {
        snprintf(who, sizeof(who), "%s", c.file);
    }
    say(log, "[lib] %s : refused: %s", who, reason);
    if (!r->refused) {
        snprintf(first, 24, "%s", c.meta.valid ? c.meta.name : c.file);
    }
    r->refused++;
    if (r->nkept < LIB_REFUSALS_KEPT) {
        // a sentence of the loader names the library; one about a file that is not one has to name the file
        snprintf(r->kept[r->nkept++], LIB_ERR_MAX, "%s%s%s", c.meta.valid ? "" : c.file, c.meta.valid ? "" : ": ", reason);
    }
}

}

namespace {

// The loading proper, for candidates that are files (a folder) or records inside a bundle: dependency order, a refusal each, the summary.
void load_cands(LibLoader *ld, CoreFs *fs, Cand *cand, int n, const char *folder, LibLog *log, LibScanResult *r, void (*before_first)(void *), void *ctx)
{
    char first[24] = "";                                     // the name of the first refused file, for the one-line summary
    LibMeta metas[LIB_SCAN_MAX];
    int order[LIB_SCAN_MAX], cycle[LIB_SCAN_MAX];
    for (int i = 0; i < n; i++) {
        read_meta(fs, &cand[i]);
        metas[i] = cand[i].meta;
    }
    int no = lib_dep_order(metas, n, order, cycle);

    // a cycle member is not in order[]: its refusal is told here, in the order of the files
    for (int i = 0; i < n; i++) {
        if (cycle[i] >= 0) {
            char members[120] = "";
            for (int j = 0; j < n; j++) {
                if (cycle[j] == cycle[i]) {
                    strncat(members, members[0] ? ", " : "", sizeof(members) - strlen(members) - 1);
                    strncat(members, metas[j].name, sizeof(members) - strlen(members) - 1);
                }
            }
            char why[160];
            snprintf(why, sizeof(why), "Library \"%s\" is part of a dependency cycle (%s) and is not loaded.", metas[i].name, members);
            refuse(r, log, cand[i], why, first);
        }
    }
    for (int k = 0; k < no; k++) {
        Cand &c = cand[order[k]];
        if (c.meta.valid && c.meta.kind != U2A3_KIND_LIB) {
            char why[120];
            snprintf(why, sizeof(why), "\"%s\" is an application, not a library; only libraries are loaded from %s.", c.meta.name, folder);
            refuse(r, log, c, why, first);
            continue;
        }
        if (before_first) {
            before_first(ctx);
            before_first = 0;
        }
        CoreFile *f = fs->open(c.path, false);
        if (!f) {
            refuse(r, log, c, "the file cannot be opened.", first);
            continue;
        }
        FileSource src(f, c.base, c.len);
        char err[LIB_ERR_MAX] = "";
        int h = -1;
        int rc = ld->load(&src, &h, err, sizeof(err));
        fs->close(f);
        if (rc == LIB_OK) {
            const LibInfo *i = ld->info(h);
            say(log, "[lib] %s v%u.%u at 0x%08x : ok", i->name, (unsigned)(i->version >> 16), (unsigned)(i->version & 0xFFFF),
                (unsigned)i->image_addr);
            r->loaded++;
        } else {
            refuse(r, log, c, err, first);
        }
    }
    if (r->refused) {
        snprintf(r->summary, sizeof(r->summary), "%d loaded, %d refused: %s", r->loaded, r->refused, first);
    } else if (r->loaded) {
        snprintf(r->summary, sizeof(r->summary), "%d loaded", r->loaded);
    }
    r->unserved = ld->unserved_count();
    if (r->unserved) {
        size_t l = strlen(r->summary);
        snprintf(r->summary + l, sizeof(r->summary) - l, "%s%d with unserved parts", r->loaded || r->refused ? ", " : "", r->unserved);
    }
}

}

void lib_load_folder(LibLoader *ld, CoreFs *fs, const char *const *roots, const char *folder, LibLog *log, LibScanResult *r)
{
    lib_load_folder(ld, fs, roots, folder, log, r, 0, 0);
}

void lib_load_folder(LibLoader *ld, CoreFs *fs, const char *const *roots, const char *folder, LibLog *log, LibScanResult *r,
                     void (*before_first)(void *), void *ctx)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->summary, sizeof(r->summary), "none");
    Cand *cand = new Cand[LIB_SCAN_MAX];
    FsEntry *ents = new FsEntry[LIB_LIST_MAX];
    int n = 0;
    for (int ri = 0; roots[ri]; ri++) {
        char dir[64];
        snprintf(dir, sizeof(dir), "%s/%s", roots[ri], folder);
        int ne = fs->list(dir, ents, LIB_LIST_MAX);
        if (ne < 0) {
            continue;                                       // no such folder: nothing to say
        }
        r->roots_found++;
        for (int e = 0; e < ne; e++) {
            if (ents[e].dir || !has_ext(ents[e].name)) {
                continue;
            }
            r->files++;
            if (n >= LIB_SCAN_MAX || strlen(dir) + 1 + strlen(ents[e].name) >= sizeof(cand[0].path)) {      // a path that does not fit is not loaded, never cut into another file's
                r->ignored++;
                continue;
            }
            snprintf(cand[n].path, sizeof(cand[n].path), "%s/%s", dir, ents[e].name);
            cand[n].base = cand[n].len = 0;
            n++;
        }
    }
    // by path, so the order does not depend on how the file system lists a folder
    for (int i = 1; i < n; i++) {
        Cand t = cand[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(cand[j].path, t.path) > 0) {
            cand[j + 1] = cand[j];
            j--;
        }
        cand[j + 1] = t;
    }
    for (int i = 0; i < n; i++) {
        cand[i].file = strrchr(cand[i].path, '/') + 1;
    }
    if (!r->roots_found) {
        say(log, "[lib] no library folder (%s on %s ...): nothing to load", folder, roots[0] ? roots[0] : "");
    } else if (!n) {
        say(log, "[lib] the folder %s holds no %s file", folder, LIB_EXT);
    }
    if (r->ignored) {
        say(log, "[lib] %d more file(s) than the %d this base looks at are left alone", r->ignored, LIB_SCAN_MAX);
    }

    load_cands(ld, fs, cand, n, folder, log, r, before_first, ctx);
    delete[] cand;
    delete[] ents;
}

// ---- the bundle ---------------------------------------------------------------------------------------------------------------
// Record 7 of a .cor (tools/cor_pack.py pack_libs): "TRXL", u32 version 1, u32 count, then for each library u32 length, the .u2a, zeros to a
// multiple of 4. Every field is checked against the record before a library is touched; a record that is not one loads nothing and is told.
void lib_load_cor(LibLoader *ld, CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibLog *log, LibScanResult *r,
                  void (*before_first)(void *), void *ctx)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->summary, sizeof(r->summary), "none");
    if (!rec_size) {
        return;
    }
    CoreFile *f = fs->open(cor_path, false);
    uint8_t h[12];
    if (!f || rec_size < sizeof(h) || !f->read(rec_off, h, sizeof(h)) || memcmp(h, "TRXL", 4) != 0) {
        if (f) {
            fs->close(f);
        }
        say(log, "[lib] the library record of %s cannot be read: nothing of it is loaded", cor_path);
        r->refused = 1;
        snprintf(r->kept[r->nkept++], LIB_ERR_MAX, "The library record of the core bundle cannot be read.");
        snprintf(r->summary, sizeof(r->summary), "bundle: record unreadable");
        return;
    }
    uint32_t ver = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
    uint32_t cnt = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
    Cand *cand = new Cand[LIB_SCAN_MAX];
    int n = 0;
    uint32_t p = 12;
    bool ok = ver == 1 && cnt > 0 && cnt <= LIB_SCAN_MAX;
    for (uint32_t i = 0; ok && i < cnt; i++) {
        uint8_t lb[4];
        ok = (uint64_t)p + 4 <= rec_size && f->read(rec_off + p, lb, 4);
        if (!ok) {
            break;
        }
        uint32_t len = lb[0] | lb[1] << 8 | lb[2] << 16 | (uint32_t)lb[3] << 24;
        p += 4;
        ok = len >= sizeof(U2a3Header) && (uint64_t)p + len <= rec_size;
        if (!ok) {
            break;
        }
        snprintf(cand[n].path, sizeof(cand[n].path), "%s", cor_path);
        cand[n].file = "(core bundle)";
        cand[n].base = rec_off + p;
        cand[n].len = len;
        n++;
        p += len + ((4 - (len & 3)) & 3);
    }
    fs->close(f);                                               // the loader opens the file itself for each library
    ok = ok && p == rec_size;
    if (!ok) {
        say(log, "[lib] the library record of %s is damaged (version %u, %u entries): nothing of it is loaded", cor_path, (unsigned)ver, (unsigned)cnt);
        r->refused = 1;
        snprintf(r->kept[r->nkept++], LIB_ERR_MAX, "The library record of the core bundle is damaged.");
        snprintf(r->summary, sizeof(r->summary), "bundle: record damaged");
    } else {
        for (int i = 0; i < n; i++) {
            cand[i].file = "(core bundle)";
            read_meta(fs, &cand[i]);
        }
        load_cands(ld, fs, cand, n, "the core bundle", log, r, before_first, ctx);
    }
    delete[] cand;
}
