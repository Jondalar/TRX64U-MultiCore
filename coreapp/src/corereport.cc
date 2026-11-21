#include "corereport.h"
#include "bugreport.h"
#include "ops_glue.h"
#include "ops.h"
#include "board.h"
#include "listgrow.h"
#include "edid.h"
#include <stdio.h>
#include <string.h>

#include "gitinfo.h"
#include "../../host-app/software/components/u64_str.h"   // u64_dec: the board's printf has no 64 bit conversion (T251 D1)

// D3: the registers the descriptor names, in its order. Nothing else of the register window is read.
static void put_regs(BrOut *o, CoreCtx *c)
{
    const CoreDesc *d = c->desc;
    if (!d->report_regs || d->nreport_regs <= 0) {
        br_put(o, "This core declares no register list.\n");
        return;
    }
    for (int i = 0; i < d->nreport_regs && i < REPORT_REGS_MAX; i++) {
        const ReportReg &r = d->report_regs[i];
        uint32_t v = 0;
        for (int b = r.bytes - 1; b >= 0; b--) {
            v = (v << 8) | c->io->rd((uint32_t)r.reg + (uint32_t)b);
        }
        br_put(o, "%-40s $%03X = $%0*X\n", r.name, (unsigned)r.reg, r.bytes * 2, (unsigned)v);
    }
}

static void put_settings(BrOut *o, CoreCtx *c)
{
    Settings *s = c->settings;
    if (!s) {
        br_put(o, "(the settings are not loaded)\n");
        return;
    }
    static const char *const layer[] = { "default", "host", "core" };
    for (int i = 0; i < s->count(); i++) {
        const Setting &it = s->item(i);
        const char *key = it.label ? it.label : it.key;
        if (it.kind == 2 || br_secret_name(it.key)) {         // typed as stars: a password, whatever its name; or a key whose name says so
            br_put(o, "%s: %s\n", key, BR_WITHHELD);
        } else if (it.kind == 1 || it.kind == 3) {
            br_kv(o, key, "%s", s->text(i));
        } else {
            br_kv(o, key, "%s (%s)", s->label(i), layer[s->layer(i)]);
        }
    }
    if (!s->count()) {
        br_put(o, "(none)\n");
    }
}

static void put_media(BrOut *o, CoreCtx *c)
{
    const CoreDesc *d = c->desc;
    if (!d->nmedia) {
        br_put(o, "(this core has no media slots)\n");
        return;
    }
    OpRegistry *reg = ops_of(c);
    for (int i = 0; i < d->nmedia; i++) {
        const char *st = 0;
        char copy[160];
        copy[0] = 0;
        if (reg && d->media[i].state && d->media[i].state[0]) {
            uint32_t rev;
            st = reg->state(c, d->media[i].state, &rev);
            if (st) {
                snprintf(copy, sizeof(copy), "%s", st);
            }
        }
        if (!copy[0]) {
            br_kv(o, d->media[i].name, "empty");
            continue;
        }
        // D4 (Alex): the name and the size in bytes, no hash. The state text is the name; the size comes from the path the mount took.
        const char *path = ops_find_path(c, copy);
        uint64_t size = 0;
        bool dir = false;
        if (path && c->fs && c->fs->stat(path, &size, &dir) && !dir) {
            char sz[21];
            br_kv(o, d->media[i].name, "%s, %s bytes (%s)", copy, u64_dec(size, sz), path);     // T251 D1
        } else {
            br_kv(o, d->media[i].name, "%s, size not known", copy);
        }
    }
}

// R8: the heap that is free now - the number of the Diagnostics "Heap" row (xPortGetFreeHeapSize), read through the seam the board wires (ListGrow::free_heap).
static void put_heap(BrOut *o)
{
    if (!ListGrow::free_heap()) {
        br_put(o, "Heap free: not known (the board did not wire it)\n");
        return;
    }
    uint32_t h = ListGrow::free_heap()();
    br_put(o, "Heap free: %u bytes (%u KB)\n", (unsigned)h, (unsigned)(h / 1024));
}

bool (*&core_report_edid_reader())(uint8_t *, int, int *)
{
    static bool (*f)(uint8_t *, int, int *) = 0;
    return f;
}

// R8: the screen's raw EDID as hex and the rows the application decodes from it (EdidInfo, video_modes_offered), or plainly that there is none.
static void put_edid(BrOut *o)
{
    static uint8_t ed[256];
    int len = 0;
    if (!core_report_edid_reader() || !core_report_edid_reader()(ed, (int)sizeof(ed), &len) || len <= 0) {
        br_put(o, "The screen answered no EDID (a TV on a converter, or no screen).\n");
        return;
    }
    br_put(o, "%d bytes\n", len);
    for (int i = 0; i < len; i += 16) {
        br_put(o, "%03X:", i);
        for (int k = i; k < i + 16 && k < len; k++) {
            br_put(o, " %02X", ed[k]);
        }
        br_put(o, "\n");
    }
    EdidInfo info;
    if (!edid_parse(ed, len, &info)) {
        br_put(o, "Decoded: not valid (header or checksum wrong); the application offers the television rows\n");
    } else {
        br_kv(o, "Decoded", "valid, %s CEA block, HDMI limit %d MHz", info.has_cea ? "with" : "no", info.max_tmds_mhz);
    }
    uint16_t offered = video_modes_offered(ed, len);
    for (int m = 0; m < EDID_ROWS; m++) {
        const EdidRow *r = edid_row(m);
        if (r && ((offered >> m) & 1u)) {
            br_put(o, "Mode: %u x %u at %d Hz%s\n", (unsigned)r->w, (unsigned)r->h, video_row_sys(m) ? 60 : 50,
                   info.valid && ((info.native >> m) & 1u) ? " (native)" : "");
        }
    }
}

int core_report_build(CoreCtx *c, uint32_t fattime, bool time_valid, const char *log, int log_len, char *out, int cap)
{
    BrOut o;
    br_init(&o, out, cap);
    const CoreDesc *d = c->desc;
    char dt[32];
    br_date(dt, sizeof(dt), fattime, time_valid);
    br_put(&o, "TRX64 bug report (core application)\n");
    br_kv(&o, "Date", "%s", dt);

    br_section(&o, "Versions");
    br_kv(&o, "Core", "%s (%s)", d->cor_name, d->title);
    br_kv(&o, "Core id", "%s", d->core_id ? d->core_id : "none named");
    br_kv(&o, "Core version", "%s", c->core_version[0] ? c->core_version : "none named");
    br_kv(&o, "Core file", "%s", c->cor_path[0] ? c->cor_path : "none (started without a hand-over)");
    br_kv(&o, "Host bitstream", "%s%s%08X", c->host_version, c->host_version[0] ? ", key " : "", (unsigned)c->host_key);
    br_kv(&o, "Core application", "%s built %s", APP_VERSION_STRING, APP_BUILD_DATE);
    br_kv(&o, "Board", "%s", c->board ? board_name(*c->board) : "MultiCore host");
    br_kv(&o, "Started", "%s", c->started ? "yes" : "no");
    br_kv(&o, "Paused", "%s", c->paused ? "yes" : "no");

    br_section(&o, "Configuration");
    br_kv(&o, "Settings file", "%s", c->cfg_path[0] ? c->cfg_path : "none (settings live in memory)");
    put_settings(&o, c);

    br_section(&o, "Media (the slot's file name and its size in bytes; no hash)");
    put_media(&o, c);

    br_section(&o, "Core state (registers named by the descriptor)");
    put_regs(&o, c);

    br_section(&o, "Heap");
    put_heap(&o);

    br_section(&o, "Screen (EDID)");
    put_edid(&o);

    br_section(&o, "Log (last lines the application printed)");
    br_log(&o, log, log_len);
    br_finish(&o);
    return o.n;
}

static char g_rdir[24] = CORE_REPORT_DIR;

const char *core_report_dir(void)
{
    return g_rdir;
}

// D5: /sd/report when there is an SD card, else report/ on the first USB medium that takes a folder.
static bool pick_dir(CoreCtx *c)
{
    static const char *const dirs[] = { CORE_REPORT_DIR, "/usb0/report", "/usb1/report", "/usb2/report", "/usb3/report" };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (c->fs->mkdir(dirs[i])) {
            snprintf(g_rdir, sizeof(g_rdir), "%s", dirs[i]);
            return true;
        }
    }
    return false;
}

bool core_report_save(CoreCtx *c, const char *text, int len, uint32_t fattime, bool time_valid, char *name, int namelen, char *why, int whylen)
{
    why[0] = 0;
    char stamp[BR_STAMP_LEN];
    br_stamp(stamp, fattime, time_valid);
    if (!pick_dir(c)) {
        snprintf(why, whylen, "no SD card or USB stick");
        return false;
    }
    // two reports in one second (or with the clock not set) must not replace each other
    for (int k = time_valid ? 0 : 1; k < 100; k++) {
        if (!time_valid) {
            snprintf(name, namelen, "nodate-%d.txt", k);
        } else if (k) {
            snprintf(name, namelen, "%s-%d.txt", stamp, k);
        } else {
            snprintf(name, namelen, "%s.txt", stamp);
        }
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", g_rdir, name);
        uint64_t sz;
        bool dir;
        if (c->fs->stat(path, &sz, &dir)) {
            continue;
        }
        if (!c->fs->save(path, text, (uint32_t)len)) {
            snprintf(why, whylen, "the medium did not take the file");
            return false;
        }
        return true;
    }
    snprintf(why, whylen, "no free name");
    return false;
}
