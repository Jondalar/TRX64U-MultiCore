#include "appindex.h"
#include "corefolder.h"
#include "libmenu.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const k_roots[] = { "/sd", "/usb0", "/usb1", "/usb2", "/usb3", 0 };

static void say(LibLog *log, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(LibLog *log, const char *fmt, ...)
{
    if (!log) {
        return;
    }
    char b[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    log->line(b);
}

int AppIndex::folders(const CoreCtx *c, char out[][APP_PATH_MAX], int max)
{
    int n = 0;
    if (c->cor_path[0]) {
        if (n < max && corefolder_sub(c->cor_path, APP_FOLDER, out[n], APP_PATH_MAX)) {
            n++;
        }
        // T180 D9: the folder every core reads, "<dir>/apps" beside the .cor ("/SD/cores/apps" for "/SD/cores/trx64u.cor"), after the core's own
        char d[APP_PATH_MAX];
        char *slash;
        if (n < max && corefolder_dir(c->cor_path, d, sizeof(d)) && (slash = strrchr(d, '/')) &&
            (size_t)(slash - d) + 2 + strlen(APP_SHARED_FOLDER) < APP_PATH_MAX) {
            strcpy(slash + 1, APP_SHARED_FOLDER);
            strcpy(out[n++], d);
        }
        return n;
    }
    char core[24];
    int i = 0;
    for (const char *p = c->desc->cor_name; *p && i < (int)sizeof(core) - 1; p++, i++) {
        core[i] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    }
    core[i] = 0;
    for (int r = 0; k_roots[r] && n < max; r++) {
        snprintf(out[n++], APP_PATH_MAX, "%s/cores/%s/%s", k_roots[r], core, APP_FOLDER);
    }
    for (int r = 0; k_roots[r] && n < max; r++) {
        snprintf(out[n++], APP_PATH_MAX, "%s/cores/%s", k_roots[r], APP_SHARED_FOLDER);
    }
    return n;
}

static bool is_u2a(const char *name)
{
    int l = (int)strlen(name);
    return l > 4 && !strcasecmp(name + l - 4, LIB_EXT);
}

uint32_t AppIndex::signature(CoreFs *fs, char folders[][APP_PATH_MAX], int nf)
{
    uint32_t h = 2166136261u;
    FsEntry *e = (FsEntry *)malloc(sizeof(FsEntry) * LIB_LIST_MAX);        // whole FAT long names (T180 #38): 17 KB for this pass, not for good
    if (!e) {
        return h;
    }
    for (int f = 0; f < nf; f++) {
        int n = fs->list(folders[f], e, LIB_LIST_MAX);
        for (int i = 0; i < n; i++) {
            if (e[i].dir || !is_u2a(e[i].name)) {
                continue;
            }
            for (const char *p = folders[f]; *p; p++) {
                h = (h ^ (uint8_t)*p) * 16777619u;
            }
            for (const char *p = e[i].name; *p; p++) {
                h = (h ^ (uint8_t)*p) * 16777619u;
            }
            h = (h ^ (uint32_t)e[i].size) * 16777619u;
        }
    }
    free(e);
    return h;
}

void AppIndex::clear()
{
    for (int i = 0; i < n_; i++) {
        delete app_[i].menu;
        delete[] app_[i].menu_bytes;
        delete[] app_[i].nodes;
    }
    memset(app_, 0, sizeof(app_));
    n_ = 0;
    if (has_extra_) {
        delete extra_.menu;
        delete[] extra_.menu_bytes;
        delete[] extra_.nodes;
        memset(&extra_, 0, sizeof(extra_));
        has_extra_ = false;
    }
}

// The nodes of a record, told apart: those anchored "APP/..." belong to the application's own screens, the rest to the core's menu.
static bool is_screen_node(const LibNodeDecl &nd)
{
    return nd.anchor && !strncmp(nd.anchor, APP_AREA "/", sizeof(APP_AREA));
}

// One file: its header and its menu record into `a`. False (and a line in the log) when it is not an application this base can use.
bool AppIndex::read_file(CoreFs *fs, const char *path, AppEntry &a, LibLog *log)
{
    CoreFile *cf = fs->open(path, false);
    if (!cf) {
        say(log, "[app] %s: the file cannot be opened", path);
        return false;
    }
    CoreFileSource src(cf);
    U2a3Header h;
    uint32_t fsize = src.size();
    bool ok = fsize >= sizeof(h) && src.read(0, &h, sizeof(h)) && h.magic == U2A3_MAGIC && h.header_size == U2A3_HEADER_SIZE &&
              h.format_version == U2A3_FORMAT_VERSION && h.name[0];
    if (!ok) {
        fs->close(cf);
        say(log, "[app] %s: not an application file of format %d", path, U2A3_FORMAT_VERSION);
        return false;
    }
    h.name[U2A3_NAME_MAX - 1] = 0;
    if (h.kind != U2A3_KIND_APP) {
        fs->close(cf);
        if (h.kind == U2A3_KIND_LIB) {                              // base 4.0 (T239.3 D58): a library is linked into the core's app through its TOML B, never loaded
            say(log, "[app] %s: \"%s\" is a library file; base 4.0 loads no library file", path, h.name);
        } else {
            say(log, "[app] %s: \"%s\" is not an application", path, h.name);
        }
        return false;
    }
    memset(&a, 0, sizeof(a));
    snprintf(a.path, sizeof(a.path), "%s", path);
    memcpy(a.name, h.name, sizeof(a.name));
    a.version = h.version;
    a.residence = h.residence;
    a.fsize = fsize;
    a.image_bytes = h.payload_size + h.bss_size + h.align;
    for (int k = 0; k < h.needs_count && k < U2A3_NEEDS_MAX; k++) {
        U2a3Need nd;
        if (src.read(h.needs_offset + k * U2A3_NEED_SIZE, &nd, sizeof(nd))) {
            nd.name[U2A3_NAME_MAX - 1] = 0;
            if (!strcmp(nd.name, PRG_APP_NEED)) {
                a.standalone_only = true;
            } else if (!strcmp(nd.name, CART_APP_NEED)) {
                a.cart_only = true;
            } else if (strcmp(nd.name, "base")) {
                a.needs[a.nneeds++] = nd;
            }
        }
    }
    if (h.flags & U2A3_FLAG_MENU) {
        char why[160];
        if (libmenu_read(&src, fsize, h.reserved, &a.menu, &a.menu_bytes, why, sizeof(why), true) != 0) {
            say(log, "[app] %s: %s", a.name, why);
            fs->close(cf);
            memset(&a, 0, sizeof(a));
            return false;
        }
    }
    fs->close(cf);
    if (a.menu) {
        DescRecord &d = *a.menu;
        int nn = d.rec.nnodes;
        a.nodes = new LibNodeDecl[nn > 0 ? nn : 1];
        int nh = 0, ns = 0;
        for (int k = 0; k < nn; k++) {                              // host nodes first, the screens' behind them, each in its declared order
            if (!is_screen_node(d.nodes[k])) {
                a.nodes[nh++] = d.nodes[k];
            }
        }
        for (int k = 0; k < nn; k++) {
            if (is_screen_node(d.nodes[k])) {
                a.nodes[nh + ns++] = d.nodes[k];
            }
        }
        a.host = d.rec;
        a.host.nodes = a.nodes;
        a.host.nnodes = nh;
        memset(&a.screens, 0, sizeof(a.screens));
        a.screens.struct_size = sizeof(a.screens);
        a.screens.nodes = a.nodes + nh;
        a.screens.nnodes = ns;
        a.has_screens = d.has_route;
        if (!a.has_screens && ns) {
            say(log, "[app] %s: nodes anchored in " APP_AREA " and no route of its own; they are not shown", a.name);
        }
    }
    return true;
}

static void free_entry(AppEntry &a)
{
    delete a.menu;
    delete[] a.menu_bytes;
    delete[] a.nodes;
    memset(&a, 0, sizeof(a));
}

void AppIndex::scan(CoreFs *fs, char folders[][APP_PATH_MAX], int nf, LibLog *log)
{
    clear();
    sig_ = signature(fs, folders, nf);
    FsEntry *e = (FsEntry *)malloc(sizeof(FsEntry) * LIB_LIST_MAX);        // 17 KB for this scan, not for good
    if (!e) {
        return;
    }
    for (int f = 0; f < nf; f++) {
        int n = fs->list(folders[f], e, LIB_LIST_MAX);
        for (int i = 0; i < n && n_ < APPS_MAX; i++) {
            if (e[i].dir || !is_u2a(e[i].name)) {
                continue;
            }
            char path[APP_PATH_MAX];
            if (strlen(folders[f]) + 1 + strlen(e[i].name) >= sizeof(path)) {       // a path that does not fit is not read, never cut into another file's
                say(log, "[app] %s/%s: the path is too long", folders[f], e[i].name);
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", folders[f], e[i].name);
            AppEntry tmp;
            if (!read_file(fs, path, tmp, log)) {
                continue;
            }
            if (find(tmp.name)) {
                say(log, "[app] %s: \"%s\" is on the medium twice; the first one counts", path, tmp.name);
                free_entry(tmp);
                continue;
            }
            app_[n_++] = tmp;
        }
    }
    prg_dir_[0] = 0;                                     // the apps folder of the flash is found when a file's actions are listed (prg_action)
    free(e);
}

// T228 R21 #14, FW user_file_interaction.cc:55-76: a file whose extension has a <ext>.prg in the flash's apps folder gets "Run with App", looked up when the
// file's actions are listed. The extension is cut to three characters (FW's FileInfo extension), the folder is "<flash root>/apps", the root the filesystem calls
// "flash". Files of 16 MB - 3 bytes and more and folders get none.
const FileAction *AppIndex::prg_action(CoreFs *fs, const char *name, uint64_t size, bool dir)
{
    if (!prg_on_ || dir || size > 16u * 1024 * 1024 - 4) {
        return 0;
    }
    const char *dot = strrchr(name, '.');
    char stem[4];
    int n = 0;
    for (const char *p = dot ? dot + 1 : ""; *p && n < 3; p++) {
        stem[n++] = (char)tolower((unsigned char)*p);
    }
    stem[n] = 0;
    if (!n) {
        return 0;
    }
    if (!prg_dir_[0]) {
        FsEntry *e = (FsEntry *)malloc(sizeof(FsEntry) * LIB_LIST_MAX);
        if (!e) {
            return 0;
        }
        int nr = fs->list("/", e, LIB_LIST_MAX);
        for (int i = 0; i < nr && !prg_dir_[0]; i++) {
            if (!strcasecmp(e[i].name, "flash") && strlen(e[i].name) < 20) {
                snprintf(prg_dir_, sizeof(prg_dir_), "/%s/apps", e[i].name);
            }
        }
        free(e);
        if (!prg_dir_[0]) {
            return 0;
        }
    }
    char pp[64];
    uint64_t sz = 0;
    bool d = false;
    snprintf(pp, sizeof(pp), "%s/%s.prg", prg_dir_, stem);
    if (!fs->stat(pp, &sz, &d) || d) {
        for (char *c = pp + strlen(prg_dir_) + 1; *c && *c != '.'; c++) {              // a filesystem that tells upper and lower case apart
            *c = (char)toupper((unsigned char)*c);
        }
        if (!fs->stat(pp, &sz, &d) || d) {
            return 0;
        }
    }
    static OpBind bind[1];
    bind[0].arg = "path";
    bind[0].kind = BIND_FILE;
    bind[0].i = 0;
    bind[0].s = 0;
    static FileAction act;
    memset(&act, 0, sizeof(act));
    act.label = "Run with App";
    act.op = PRG_APP_OP;
    act.bind = bind;
    act.nbind = 1;
    act.closes_menu = true;
    return &act;
}

// An application file that is not in the apps folder: a .u2a the person opened in the Library. It is an entry of its own until the next scan.
const AppEntry *AppIndex::adhoc(CoreFs *fs, const char *path, LibLog *log)
{
    if (strlen(path) >= APP_PATH_MAX) {                  // the entry keeps the path in APP_PATH_MAX bytes: a path cut is another file
        return 0;
    }
    AppEntry tmp;
    if (!read_file(fs, path, tmp, log)) {
        return 0;
    }
    if (has_extra_) {
        free_entry(extra_);
        has_extra_ = false;
    }
    const AppEntry *same = find(tmp.name);
    if (same) {
        free_entry(tmp);
        return same;                                     // the one of the folder is the application
    }
    extra_ = tmp;
    has_extra_ = true;
    return &extra_;
}

const AppEntry *AppIndex::find(const char *name) const
{
    for (int i = 0; name && i < n_; i++) {
        if (!strcasecmp(app_[i].name, name)) {
            return &app_[i];
        }
    }
    if (has_extra_ && name && !strcasecmp(extra_.name, name)) {
        return &extra_;
    }
    return 0;
}

bool AppIndex::need_bytes(const AppEntry &a, const LibLoader *ld, uint64_t *need) const
{
    (void)ld;
    for (int k = 0; k < a.nneeds; k++) {
        if (!LibLoader::find_export(a.needs[k].name, a.needs[k].min_version, 0)) {
            return false;
        }
    }
    *need = (uint64_t)a.image_bytes + APP_WORK_BYTES;
    return true;
}

bool AppIndex::available(const AppEntry &a, const LibLoader *ld) const
{
    uint64_t n;
    return need_bytes(a, ld, &n);
}

bool AppIndex::fits(const AppEntry &a, uint32_t room, const LibLoader *ld) const
{
    uint64_t need;
    return need_bytes(a, ld, &need) && need <= room;
}

bool AppIndex::prepare_open(CoreFs *fs, LibLoader *ld, const AppEntry &a, char *err, int errlen) const
{
    (void)fs;
    uint64_t need;
    if (need_bytes(a, ld, &need) && need > ld->room()) {
        // Rule: needs is rounded UP, free is rounded DOWN, so the two figures can never read equal on a refusal (need > room gives ceil(need) > floor(room)).
        snprintf(err, errlen, "Not enough memory for %s (needs %u KB, %u KB free).", a.name, (unsigned)((need + 1023) / 1024), (unsigned)(ld->room() / 1024));
        return false;
    }
    return needs_linked(a, err, errlen);
}

bool AppIndex::needs_linked(const AppEntry &a, char *err, int errlen) const
{
    for (int k = 0; k < a.nneeds; k++) {
        if (!LibLoader::find_export(a.needs[k].name, a.needs[k].min_version, 0)) {
            snprintf(err, errlen, "%s needs \"%s\" %u.%u, which this core's app is not linked with.", a.name, a.needs[k].name, (unsigned)(a.needs[k].min_version >> 16),
                     (unsigned)(a.needs[k].min_version & 0xFFFF));
            return false;
        }
    }
    return true;
}

int AppIndex::contributors(Contributor *out, int max) const
{
    int k = 0;
    for (int i = 0; i < n_ && k < max; i++) {
        if (app_[i].menu && offered(app_[i]) && available(app_[i], room_ld_)) {
            out[k].name = app_[i].name;
            out[k].rank = CR_APP;
            out[k].rec = &app_[i].host;
            k++;
        }
    }
    return k;
}
