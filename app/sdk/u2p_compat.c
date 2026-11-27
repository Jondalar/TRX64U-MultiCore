/*
 * u2p_compat.c - the tables of ABI 2 (u2p_app.h) over the service tables of ABI 3 (svctab.h).
 *
 * PROVENANCE: u2p_vsnprintf is trex_crt's ui_vsnprintf (vendor/gideon/apps/trex_crt/src/ui_shim.c, GPL-3.0), unchanged but for the name.
 */
#include "u2p_app.h"

static const LibApi *g_root;
static u2p_api_t g_api;
static u2p_sys_api_t g_sys;
static u2p_mem_api_t g_mem;
static u2p_ui_api_t g_ui;
static u2p_fs_api_t g_fs;
static u2p_media_api_t g_media;

/* ---- formatting ---- */

typedef struct {
    char *buf;
    int size;
    int pos;
} fmt_out_t;

static void fmt_char(fmt_out_t *o, char c)
{
    if (o->pos < o->size - 1) {
        o->buf[o->pos] = c;
    }
    o->pos++;
}

int u2p_vsnprintf(char *buf, int size, const char *fmt, __builtin_va_list ap)
{
    fmt_out_t o = { buf, size, 0 };
    char digits[12];

    while (*fmt) {
        if (*fmt != '%') {
            fmt_char(&o, *fmt++);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0;
        int precision = -1;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt++ - '0');
            }
        }
        while (*fmt == 'l') {
            fmt++;
        }

        const char *str = digits;
        int len = 0;
        bool negative = false;

        switch (*fmt) {
        case 'd':
        case 'u':
        case 'x':
        case 'X': {
            unsigned int base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            const char *hex = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            unsigned int value;
            if (*fmt == 'd') {
                int v = __builtin_va_arg(ap, int);
                negative = (v < 0);
                value = negative ? (unsigned int)(-(v + 1)) + 1 : (unsigned int)v;
            } else {
                value = __builtin_va_arg(ap, unsigned int);
            }
            char rev[12];
            int n = 0;
            do {
                rev[n++] = hex[value % base];
                value /= base;
            } while (value);
            if (negative) {
                digits[len++] = '-';
            }
            while (n) {
                digits[len++] = rev[--n];
            }
            break;
        }
        case 's':
            str = __builtin_va_arg(ap, const char *);
            if (!str) {
                str = "(null)";
            }
            while (str[len] && (precision < 0 || len < precision)) {
                len++;
            }
            break;
        case 'c':
            digits[0] = (char)__builtin_va_arg(ap, int);
            len = 1;
            break;
        case 0:
            continue;
        default:
            fmt_char(&o, '%');
            digits[0] = *fmt;
            len = 1;
            break;
        }
        fmt++;

        if (negative && pad == '0') {
            fmt_char(&o, '-');
            str++;
            len--;
            width--;
        }
        for (int i = len; i < width; i++) {
            fmt_char(&o, pad);
        }
        for (int i = 0; i < len; i++) {
            fmt_char(&o, str[i]);
        }
    }

    if (size > 0) {
        o.buf[(o.pos < size) ? o.pos : size - 1] = 0;
    }
    return o.pos;
}

int u2p_snprintf(char *buf, int size, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = u2p_vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

/* ---- sys, mem ---- */

static int c_log(const char *fmt, ...)
{
    char b[256];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = u2p_vsnprintf(b, sizeof(b), fmt, ap);
    __builtin_va_end(ap);
    if (g_root->sys && g_root->sys->log) {
        g_root->sys->log(b);
    }
    return n;
}
static void c_delay(uint32_t ms) { g_root->sys->sleep_ms(ms); }
static uint32_t c_tick(void) { return g_root->sys->ms(); }
static int c_flash_read(uint32_t addr, void *buf, int len)
{
    return U2P_HAS(g_root->sys, flash_read) ? g_root->sys->flash_read(addr, buf, len) : U2P_ERR_UNSUPPORTED;
}
static void c_handover(void)
{
    if (U2P_HAS(g_root->sys, machine_handover)) {
        g_root->sys->machine_handover();
    }
}
static void *c_malloc(size_t n) { return g_root->mem->malloc((uint32_t)n); }
static void c_free(void *p) { g_root->mem->free(p); }
static void *c_realloc(void *p, size_t n) { return g_root->mem->realloc(p, (uint32_t)n); }

/* ---- the dialogs ---- */

static void c_popup(const char *title, const char *msg)
{
    if (g_root->dlg) {
        g_root->dlg->message(title, msg);
    }
}
static int c_buttons(const char *msg, uint8_t buttons)
{
    return g_root->dlg ? g_root->dlg->buttons("", msg, buttons) : 0;
}
static int c_confirm(const char *msg)
{
    return g_root->dlg ? g_root->dlg->confirm("", msg) : 0;
}
static int c_menu(const char *title, const char **items, int n)
{
    return g_root->dlg ? g_root->dlg->choose(title, items, n, 0) : -1;
}
static int c_menu_keys(const char *title, const char **items, int n, const char *keys)
{
    return g_root->dlg ? g_root->dlg->choose_keys(title, items, n, keys) : -1;
}
static int c_string_input(const char *title, char *buf, int maxlen)
{
    if (!g_root->dlg || !g_root->dlg->text(title, buf, maxlen + 1)) {         /* maxlen counts characters; the buffer holds one more for the NUL */
        return -1;
    }
    return (int)strlen(buf);
}
static int c_path_input(char *buf, int maxlen)
{
    if (!g_root->dlg) {
        return -1;
    }
    char start[200];
    int n = 0;
    while (buf[n] && n < (int)sizeof(start) - 1) {
        start[n] = buf[n];
        n++;
    }
    start[n] = 0;
    char out[200];
    if (!g_root->dlg->pick_folder(start[0] ? start : "/", out, sizeof(out))) {
        return -1;
    }
    n = 0;
    while (out[n] && n < maxlen - 1) {
        buf[n] = out[n];
        n++;
    }
    buf[n] = 0;
    return n;
}
static int c_pick_file(const char *start, const char *ext, char *out, int outlen)
{
    if (!g_root->dlg || !g_root->dlg->pick_file(start, ext, out, outlen)) {
        return -1;
    }
    return (int)strlen(out);
}
static void *c_progress_open(const char *title, int total)
{
    return g_root->dlg ? g_root->dlg->progress_open(title, total) : 0;
}
static void c_progress_set(void *h, int value, const char *msg)
{
    if (g_root->dlg && h) {
        g_root->dlg->progress_set(h, value, msg);
    }
}
static void c_progress_close(void *h)
{
    if (g_root->dlg && h) {
        g_root->dlg->progress_close(h);
    }
}

/* ---- files: a stdio of a kind over the all-or-nothing reads of SvcFs ---- */

typedef struct {
    struct SvcFile *f;
    uint64_t pos;
} cfile_t;

static void *c_fopen(const char *path, const char *mode)
{
    bool rw = false, create = false, append = false;
    for (const char *m = mode; *m; m++) {
        if (*m == 'w') {
            create = true;
            rw = true;
        } else if (*m == 'a') {
            create = true;
            rw = true;
            append = true;
        } else if (*m == '+') {
            rw = true;
        }
    }
    const SvcFs *fs = g_root->fs;
    if (!fs) {
        return 0;
    }
    uint64_t sz;
    bool dir;
    bool there = fs->stat(path, &sz, &dir) && !dir;
    if (create && (!there || !append)) {
        if (!fs->save(path, "", 0)) {                  /* a new file, or the old one made empty */
            return 0;
        }
    }
    struct SvcFile *f = fs->open(path, rw);
    if (!f) {
        return 0;
    }
    cfile_t *c = (cfile_t *)g_root->mem->malloc(sizeof(cfile_t));
    if (!c) {
        fs->close(f);
        return 0;
    }
    c->f = f;
    c->pos = append ? fs->fsize(f) : 0;
    return c;
}

static int c_fread(void *buf, int size, int n, void *fp)
{
    cfile_t *c = (cfile_t *)fp;
    if (!c || size <= 0 || n <= 0) {
        return 0;
    }
    uint64_t total = (uint64_t)size * (uint64_t)n;
    uint64_t fsz = g_root->fs->fsize(c->f);
    if (c->pos >= fsz) {
        return 0;
    }
    if (total > fsz - c->pos) {
        total = fsz - c->pos;
    }
    uint32_t whole = (uint32_t)total;                  /* a read of an application is far below 4 GiB */
    whole -= whole % (uint32_t)size;
    if (!whole || !g_root->fs->read(c->f, c->pos, buf, whole)) {
        return 0;
    }
    c->pos += whole;
    return (int)(whole / (uint32_t)size);
}

static int c_fwrite(const void *buf, int size, int n, void *fp)
{
    cfile_t *c = (cfile_t *)fp;
    if (!c || size <= 0 || n <= 0) {
        return 0;
    }
    uint64_t total = (uint64_t)size * (uint64_t)n;
    if (!g_root->fs->write(c->f, c->pos, buf, (uint32_t)total)) {
        return 0;
    }
    c->pos += total;
    return n;
}

static int c_fseek(void *fp, long offs, int whence)
{
    cfile_t *c = (cfile_t *)fp;
    if (!c) {
        return -1;
    }
    int64_t base = whence == 1 ? (int64_t)c->pos : whence == 2 ? (int64_t)g_root->fs->fsize(c->f) : 0;
    int64_t to = base + offs;
    if (to < 0) {
        return -1;
    }
    c->pos = (uint64_t)to;
    return 0;
}

static long c_ftell(void *fp) { return fp ? (long)((cfile_t *)fp)->pos : -1; }

static int c_fclose(void *fp)
{
    cfile_t *c = (cfile_t *)fp;
    if (!c) {
        return -1;
    }
    g_root->fs->close(c->f);
    g_root->mem->free(c);
    return 0;
}

static int c_exists(const char *path, const char *name)
{
    char full[256];
    int n = 0;
    while (path[n] && n < 250) {
        full[n] = path[n];
        n++;
    }
    if (n > 0 && full[n - 1] != '/') {
        full[n++] = '/';
    }
    for (int i = 0; name[i] && n < 255; i++) {
        full[n++] = name[i];
    }
    full[n] = 0;
    uint64_t sz;
    bool dir;
    return g_root->fs->stat(full, &sz, &dir) ? 1 : 0;
}

static int c_mkdir(const char *path) { return g_root->fs->mkdir(path) ? U2P_OK : U2P_ERR_IO; }

static int c_dir(const char *path, int (*cb)(void *ctx, const char *name, uint32_t size, int is_dir), void *ctx)
{
    static SvcFsEntry e[64];
    int n = g_root->fs->list(path, e, 64);
    if (n < 0) {
        return U2P_ERR_NOT_FOUND;
    }
    for (int i = 0; i < n; i++) {
        if (cb(ctx, e[i].name, (uint32_t)e[i].size, e[i].dir)) {
            break;
        }
    }
    return U2P_OK;
}

/* media.reset: the operation "machine.warm_reset" through the root's registry. The OpResult is the base's (ops.h: code, more, msg 240, ndata, data 256 = 508 bytes); a C
   application has only its name, so the call gets room for it. */
static int c_reset(void)
{
    static uint32_t res[256];                            /* 1024 bytes, 4-aligned: more than the OpResult needs */
    if (!g_root->ops || !g_root->ops->call) {
        return U2P_ERR_UNSUPPORTED;
    }
    return g_root->ops->call("machine.warm_reset", 0, 0, (struct OpResult *)res) == 0 ? U2P_OK : U2P_ERR_IO;
}

const u2p_api_t *u2p_compat_init(const LibApi *api)
{
    g_root = api;
    g_sys.size = sizeof(g_sys);
    g_sys.log = c_log;
    g_sys.delay_ms = c_delay;
    g_sys.tick_ms = c_tick;
    g_sys.flash_read = c_flash_read;
    g_sys.machine_handover = c_handover;
    g_mem.size = sizeof(g_mem);
    g_mem.malloc = c_malloc;
    g_mem.free = c_free;
    g_mem.realloc = c_realloc;
    g_ui.size = sizeof(g_ui);
    g_ui.popup = c_popup;
    g_ui.confirm = c_confirm;
    g_ui.popup_buttons = c_buttons;
    g_ui.menu = c_menu;
    g_ui.menu_keys = c_menu_keys;
    g_ui.string_input = c_string_input;
    g_ui.path_input = c_path_input;
    g_ui.pick_file = c_pick_file;
    g_ui.progress_open = c_progress_open;
    g_ui.progress_set = c_progress_set;
    g_ui.progress_close = c_progress_close;
    g_fs.size = sizeof(g_fs);
    g_fs.fopen = c_fopen;
    g_fs.fread = c_fread;
    g_fs.fwrite = c_fwrite;
    g_fs.fseek = c_fseek;
    g_fs.ftell = c_ftell;
    g_fs.fclose = c_fclose;
    g_fs.exists = c_exists;
    g_fs.mkdir = c_mkdir;
    g_fs.dir = c_dir;
    g_media.size = sizeof(g_media);
    g_media.reset = c_reset;
    g_api.size = sizeof(g_api);
    g_api.sys = &g_sys;
    g_api.mem = &g_mem;
    g_api.ui = &g_ui;
    g_api.fs = &g_fs;
    g_api.media = &g_media;
    g_api.net = api->net;
    g_api.job = api->job;
    g_api.root = api;
    g_api.firmware_git = "";
    g_api.firmware_version = "T104.3";
    return &g_api;
}

/* ---- the SDK's u2p_ui.h dialogs ---- */

static const u2p_api_t *s_api;
void u2p_ui_init(const u2p_api_t *api) { s_api = api; }
void u2p_ui_popup(const char *title, const char *msg) { s_api->ui->popup(title, msg); }
int u2p_ui_popup_buttons(const char *msg, uint8_t buttons) { return s_api->ui->popup_buttons(msg, buttons); }
int u2p_ui_confirm(const char *msg) { return s_api->ui->confirm(msg); }
int u2p_ui_menu(const char *title, const char **items, int n) { return s_api->ui->menu(title, items, n); }
int u2p_ui_menu_keys(const char *title, const char **items, int n, const char *keys) { return s_api->ui->menu_keys(title, items, n, keys); }
int u2p_ui_string_input(const char *title, char *buf, int maxlen) { return s_api->ui->string_input(title, buf, maxlen); }
int u2p_ui_path_input(char *buf, int maxlen) { return s_api->ui->path_input(buf, maxlen); }
