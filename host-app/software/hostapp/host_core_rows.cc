// The rows of every page, as data (host_core_rows.h). The code of hostui.cc's fill_row, update_menu / edit / browse / net / settings, the manifest panel, the chips and the
// WiFi page's value lines, moved with its texts unchanged; what stayed in hostui.cc is the drawing.
#include "host_core_rows.h"
#include "host_core_nav_rw.h"
#include "host_core_flow_fileops.h"
#include "host_core_flow_wifi.h"
#include "hostdecl.h"
#include "hostvideo.h"
#include "flashlock.h"
#include "ftpstate.h"
#include "mediaui.h"
#include "mediaglue.h"
#include "discover.h"
#include "cfw.h"
#include "txu.h"
#include "cor.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "host_core_cm.h"

#ifndef HOSTAPP_FLASH_LOCK
#define HOSTAPP_FLASH_LOCK 0
#endif

namespace {

HcStatus g_st;
HcRowsPlatform g_pl;
bool g_compact;
char g_rep_last[48];                              // T162: the file the last "Bug report" wrote, shown on the row

void cell(HcCell *c, const char *text, int tone = HCT_NONE)
{
    c->set = true;
    c->tone = (uint8_t)tone;
    snprintf(c->text, sizeof(c->text), "%s", text);
}

const char *join(char *out, int n, const char *dir, const char *name)
{
    snprintf(out, n, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", name);
    return out;
}

CorSource *open_src(const char *path)
{
    StartFs *fs = hc_fs();
    return fs ? fs->open(path) : 0;
}

}  // namespace

HcStatus &hc_status(void) { return g_st; }
void hc_rows_init(const HcRowsPlatform *p)
{
    memset(&g_pl, 0, sizeof(g_pl));
    if (p) {
        g_pl = *p;
    }
}
void hc_rows_set_compact(bool compact) { g_compact = compact; }

const char *hc_item_title(const char *page, const char *id)
{
    const HostDecl &d = host_decl();
    for (int i = 0; i < d.nitems; i++) {
        if (!strcmp(d.items[i].page, page) && !strcmp(d.items[i].id, id)) {
            return d.items[i].title;
        }
    }
    return "";
}

const char *hc_page_title(const char *id)
{
    const HPage *pg = hd_page(host_decl(), id);
    return pg ? pg->title : "";
}

const char *hc_nav_title(int i)
{
    static const char *names[5];
    if (i < 0 || i >= 5) {
        return "";
    }
    if (!names[i]) {
        const HPage *pg = hd_page_of_view(host_decl(), i);
        names[i] = pg ? pg->title : "";
    }
    return names[i];
}

// ---- the entries ---------------------------------------------------------------------------------------------------------------------------------
// The words of the badges (T101 D7; the mock says "hash ok", "contract N != host M"). "hash ok" is the full check done when the entry was added, with the file unchanged since
// (size, mtime, header: T101 S10).
static void badge_text(EState st, bool full, int origin, unsigned contract, char *t, int n, uint8_t *tone)
{
    *tone = HCT_WARN;
    switch (st) {
    case ES_OK:
        if (full && origin == COR_ORIGIN_FOREIGN) {                      // T137 D1: not one of ours: the badge says so
            snprintf(t, n, "foreign");
        } else if (full && origin == COR_ORIGIN_BACKUP) {
            snprintf(t, n, "backup");
        } else {
            snprintf(t, n, full ? "ready" : "hash ok");
        }
        *tone = full && origin == COR_ORIGIN_FOREIGN ? HCT_WARN : HCT_OK;
        break;
    case ES_FWBAD: snprintf(t, n, "unusable"); break;
    case ES_CHANGED: snprintf(t, n, "changed"); break;
    case ES_MISSING: snprintf(t, n, "missing"); break;
    case ES_CONTRACT: snprintf(t, n, "contract %u != %d", contract, COR_HOST_CONTRACT); break;
    case ES_HOST: snprintf(t, n, "other host"); break;
    case ES_BAD: snprintf(t, n, "damaged"); *tone = HCT_BAD; break;
    default: snprintf(t, n, "?"); *tone = HCT_MUTED; break;
    }
}

bool hc_row_entry(int i, bool menu_style, HcEntryRow *r)
{
    const BootCfg &cfg = hc_cfg();
    if (i < 0 || i >= cfg.n) {
        return false;
    }
    EState st = hc_est(i);
    bool ok = st == ES_OK || st == ES_CHANGED;
    const char *ver = ok ? hc_einfo(i).version : "";
    bool full = hc_efull(i);
    snprintf(r->name, sizeof(r->name), "%s", cfg.e[i].name);
    snprintf(r->tile, sizeof(r->tile), "%.4s", full ? "FW" : cfg.e[i].name);
    if (menu_style && full && ok) {
        snprintf(r->sub, sizeof(r->sub), "%s  full firmware", cfg.e[i].path);
    } else if (menu_style && ver[0]) {
        snprintf(r->sub, sizeof(r->sub), "%s  v%s", cfg.e[i].path, ver);
    } else {
        snprintf(r->sub, sizeof(r->sub), "%s", cfg.e[i].path);
    }
    badge_text(st, full, hc_einfo(i).origin, (unsigned)hc_einfo(i).contract, r->badge, sizeof(r->badge), &r->badge_tone);
    r->def = i == cfg.def;
    r->full = full;
    r->ok_state = ok;
    return true;
}

void hc_pos_text(char *b, int n, int first, int total, int rows)
{
    if (total <= rows) {
        b[0] = 0;
        return;
    }
    int last = first + rows < total ? first + rows : total;
    snprintf(b, n, "%d-%d of %d", first + 1, last, total);
}

void hc_menu_texts(int first, int total, HcMenuTexts *t)
{
    snprintf(t->sub, sizeof(t->sub), "Pick a core. The default starts when the countdown ends.");
    t->hint_on = total == 0;
    snprintf(t->hint, sizeof(t->hint), "%s", HC_NO_CORES_HINT);
    hc_pos_text(t->pos, sizeof(t->pos), first, total, hc_nav_menu_rows());
}

void hc_countdown_texts(HcCountdownTexts *t)
{
    const BootCfg &cfg = hc_cfg();
    // T135 D12: with direct boot on the line says nothing useful; it comes back when direct boot goes off.
#ifndef TRX_CD_ALWAYS
    t->visible = !cfg.direct;
#else
    t->visible = true;
#endif
    if (!hc_cd_running()) {
        snprintf(t->text, sizeof(t->text), "%s", hc_cd_dead() ? "Countdown stopped" : (cfg.countdown && cfg.def >= 0 ? "Waiting for the default core's medium" : "No countdown"));
    } else {
        int left = (int)(hc_cd_left_ms(hc_now_ms()) + 999) / 1000;
        if (left < 0) {
            left = 0;
        }
        snprintf(t->text, sizeof(t->text), "Starting %s in %d s", cfg.e[cfg.def].name, left);
    }
    t->pct = 0;
    if (hc_cd_running() && cfg.countdown) {
        int32_t left = hc_cd_left_ms(hc_now_ms());
        t->pct = left <= 0 ? 0 : (int)((int64_t)left * 100 / (cfg.countdown * 1000));
    }
}

void hc_edit_texts(int first, HcEditTexts *t)
{
    const BootCfg &cfg = hc_cfg();
    if (cfg.countdown) {
        snprintf(t->countdown, sizeof(t->countdown), "%d s", cfg.countdown);
    } else {
        snprintf(t->countdown, sizeof(t->countdown), "off");
    }
    snprintf(t->direct, sizeof(t->direct), "%s", cfg.direct ? "on (C= or fire at power-on: menu)" : "off (menu, then the countdown)");
    snprintf(t->def, sizeof(t->def), "%s", cfg.def >= 0 ? cfg.e[cfg.def].name : "(none)");
    hc_pos_text(t->pos, sizeof(t->pos), first, cfg.n, hc_nav_menu_rows());
}

// ---- the File Browser ----------------------------------------------------------------------------------------------------------------------------
bool hc_browse_sel_file(char *path, int n)
{
    const HcNav &v = hc_nav();
    if (v.bsel < 0 || v.bsel >= v.bn || v.bl[v.bsel].dir) {
        return false;
    }
    join(path, n, v.bpath, v.bl[v.bsel].name);
    return true;
}

// T111 Q1: the File Browser lists every file; only the types the boot menu takes (.cor, .cfw, a large .cor) can be added or started.
bool hc_browse_sel_bundle(char *path, int n)
{
    return hc_browse_sel_file(path, n) && discover_is_bundle(hc_nav().bl[hc_nav().bsel].name);
}

// T137: a large .cor that is a backup of this board's firmware (R offers it)
bool hc_is_backup_cor(const char *path)
{
    size_t l = strlen(path);
    if (l < 4 || strcasecmp(path + l - 4, ".cor")) {
        return false;
    }
    static CorInfo ci;
    CorSource *s = open_src(path);
    if (!s) {
        return false;
    }
    bool r = cor_probe(*s, ci) == COR_ERR_LARGE && ci.origin == COR_ORIGIN_BACKUP;
    delete s;
    return r;
}

// T152 D8: the files the inspection takes
bool hc_esp_pickable(const char *name)
{
    size_t l = strlen(name);
    return l >= 4 && (!strcasecmp(name + l - 4, ".cfw") || !strcasecmp(name + l - 4, ".ue2"));
}

void hc_browse_title(char *out, int n)
{
    const HcNav &v = hc_nav();
    if (v.br_mode == HCB_PICK) {
        snprintf(out, n, "%s to: %s", hc_fo_pick_move() ? "Move" : "Copy", v.bpath);
    } else {
        snprintf(out, n, "%s", v.bpath);
    }
}

void hc_browse_empty_text(char *out, int n)
{
    const HcNav &v = hc_nav();
    snprintf(out, n, "%s", v.bn ? "" : (v.bup ? "(empty)" : "No media found"));
}

bool hc_browse_row(int k, HcBrowseRow *r)
{
    const HcNav &v = hc_nav();
    if (k < 0 || k >= v.bn) {
        return false;
    }
    r->mark[0] = v.bl[k].dir ? '>' : '*';
    r->mark[1] = 0;
    snprintf(r->name, sizeof(r->name), "%s", v.bl[k].name);
    r->size[0] = 0;
    if (!v.bl[k].dir) {
        ui_size_text(r->size, sizeof(r->size), v.bl[k].size64);
    }
    return true;
}

const char *hc_browse_foot(void)
{
    const HcNav &v = hc_nav();
    char path[200];
    if (hc_fo_replace_pending()) {
        return hc_foot(HCF_BR_ASK);
    }
    if (v.br_mode == HCB_INSPECT) {
        return hc_foot(HCF_BR_INSPECT);
    }
    if (v.br_mode == HCB_PICK) {
        return hc_fo_pick_move() ? hc_foot(HCF_BR_PICK_MOVE) : hc_foot(HCF_BR_PICK_COPY);
    }
    if (hc_browse_at_media()) {
        return hc_foot(HCF_BR_MEDIA);
    }
    if (hc_browse_sel_bundle(path, sizeof(path))) {
        return cfw_is_name(path) ? hc_foot(HCF_BR_CFW) : (hc_is_backup_cor(path) ? hc_foot(HCF_BR_BACKUP) : hc_foot(HCF_BROWSE));
    }
    if (v.bsel >= 0 && v.bsel < v.bn && v.bl[v.bsel].dir) {
        return strcmp(v.bl[v.bsel].name, "..") ? hc_foot(HCF_BR_DIR) : hc_foot(HCF_BR_PLAIN);
    }
    return v.bsel >= 0 && v.bsel < v.bn ? (txu_is_name(v.bl[v.bsel].name) ? hc_foot(HCF_BR_TXU) : hc_foot(HCF_BR_FILE)) : hc_foot(HCF_BR_PLAIN);
}

// the panel's probe cache: keyed by the row and the generation of the list
namespace {
CorInfo g_bci;
int g_bci_for = -2;
uint32_t g_bci_gen;
int g_bci_res;
CfwInfo g_bcw;
int g_bcw_res;
int g_bcw_origin;
MediaInfo g_mi;
int g_mi_for = -2;
uint32_t g_mi_gen;

bool bci_valid(void) { return g_bci_for == hc_nav().bsel && g_bci_gen == hc_nav().gen; }
bool mi_valid(void) { return g_mi_for == hc_nav().bsel && g_mi_gen == hc_nav().gen; }
}  // namespace

const CorInfo &hc_browse_ci(void) { return g_bci; }
int hc_browse_ci_res(void) { return g_bci_res; }
const CfwInfo &hc_browse_cw(void) { return g_bcw; }
int hc_browse_cw_res(void) { return g_bcw_res; }

bool hc_browse_probed_cor(const CorInfo **ci)
{
    if (bci_valid() && g_bci_res == COR_OK) {
        if (ci) {
            *ci = &g_bci;
        }
        return true;
    }
    return false;
}

// T115: the manifest of a .cfw: what the parser found
static void probe_full(const char *path)
{
    if (bci_valid()) {
        return;
    }
    CorSource *s = open_src(path);
    g_bci_res = COR_ERR_IO;                                            // this row is no .cor: the .cor reading of the last row does not stay valid for it
    g_bcw_res = s ? cfw_probe(*s, CFW_IDCODE_XC7A50T, g_bcw) : CFW_ERR_IO;
    g_bcw_origin = COR_ORIGIN_TRX;
    if (s && g_bcw_res == CFW_OK && g_bcw.from_cor) {                 // T137 D1: who made this .cor
        static CorInfo oc;
        if (cor_probe(*s, oc) == COR_ERR_LARGE) {
            g_bcw_origin = oc.origin;
        }
    }
    delete s;
    g_bci_for = hc_nav().bsel;
    g_bci_gen = hc_nav().gen;
}

static void probe_cor(const char *path)
{
    if (bci_valid()) {
        return;
    }
    CorSource *s = open_src(path);
    g_bcw_res = CFW_ERR_IO;
    g_bci_res = s ? cor_probe(*s, g_bci) : COR_ERR_IO;
    delete s;
    g_bci_for = hc_nav().bsel;
    g_bci_gen = hc_nav().gen;
}

void hc_browse_probe(HcBrowseProbe *p)
{
    char path[200];
    memset(p, 0, sizeof(*p));
    if (!hc_browse_sel_bundle(path, sizeof(path))) {
        return;
    }
    p->full = hc_is_full(path);
    if (p->full) {
        probe_full(path);
        p->res = g_bcw_res;
    } else {
        probe_cor(path);
        p->res = g_bci_res;
    }
    p->ci = &g_bci;
    p->cw = &g_bcw;
    p->probed = true;
}

const char *hc_panel_key(int mode, int i)
{
    static const char *const manifest[7] = { "Version", "Contract", "FPGA", "Core", "Application", "SHA-256", "Status" };
    static const char *const media[7] = { "Size", "Contents", "Boot menu", "Access", "Format", "", "" };
    return mode ? media[i] : manifest[i];
}

// T111: boot menu rows whose file lives on the medium `name` (a path starts with /<name>/).
int hc_boot_rows_on(const char *name)
{
    const BootCfg &cfg = hc_cfg();
    int n = 0, l = (int)strlen(name);
    for (int i = 0; i < cfg.n; i++) {
        const char *p = cfg.e[i].path;
        if (p[0] == '/' && !strncasecmp(p + 1, name, l) && p[1 + l] == '/') {
            n++;
        }
    }
    return n;
}

// T152: the side panel of a .txu in the File Browser: the header read (a few bytes), no part is read. Lower-case hex by hand (the firmware's printf prints the padded forms in capitals).
static void txu_panel(const char *name, const char *size_text, char *out, int n)
{
    char p[200];
    join(p, sizeof(p), hc_nav().bpath, name);
    CorSource *fs = open_src(p);
    static TxuInfo ti;
    txu_result_t r = fs ? txu_probe(*fs, &ti) : TXU_ERR_IO;
    delete fs;
    if (r != TXU_OK) {
        snprintf(out, n, "%s\nNot a usable update: %s.", size_text, txu_result_str(r));
        return;
    }
    char key[12] = "";
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        key[i] = hx[(ti.host_key >> (28 - 4 * i)) & 15];
    }
    key[8] = 0;
    const char *what = ti.host() && ti.app() ? "host and host app" : (ti.host() ? "host" : "host app");
    if (ti.host()) {
        snprintf(out, n, "%s\nMultiCore update %s: %s, host key %s.\nPress U to install it.", size_text, ti.version, what, key);
    } else {
        snprintf(out, n, "%s\nMultiCore update %s: %s.\nPress U to install it.", size_text, ti.version, what);
    }
}

// The media level (T111 Q1): what the medium under the selection is, and what the format would do to it.
static void panel_media(HcPanel *p)
{
    const HcNav &v = hc_nav();
    p->keys_mode = 1;
    for (int i = 0; i < 7; i++) {
        p->kv_vis[i] = !(i > 4 || v.bn == 0);
    }
    p->msg_vis = v.bn != 0;
    p->err_vis = v.bn == 0;
    if (!v.bn) {
        cell(&p->title, "");
        cell(&p->err, "No media found.", HCT_MUTED);
        return;
    }
    if (!mi_valid()) {
        media_info(v.bl[v.bsel].name, &g_mi);
        g_mi_for = v.bsel;
        g_mi_gen = v.gen;
    }
    const MediaInfo &m = g_mi;
    char t[96];
    cell(&p->title, v.bl[v.bsel].name, HCT_INK);
    if (!m.found || !m.ready) {
        cell(&p->val[0], "");
        cell(&p->val[1], !m.found ? "not a medium" : "no medium");
        cell(&p->val[2], "");
        cell(&p->val[3], "");
        cell(&p->val[4], m.found && !m.formattable ? "never (internal drive)" : "needs a medium");
        cell(&p->msg, "RETURN opens it", HCT_MUTED);
        return;
    }
    ui_size_text(t, sizeof(t), (uint64_t)m.sectors * m.sector_size);
    cell(&p->val[0], t);
    ui_fs_text(t, sizeof(t), &m.sn);
    cell(&p->val[1], t);
    ui_rows_text(t, sizeof(t), hc_boot_rows_on(v.bl[v.bsel].name));
    cell(&p->val[2], t);
    cell(&p->val[3], "readable and writable");
    if (!m.formattable) {
        cell(&p->val[4], "never (internal drive)");
        cell(&p->msg, "RETURN opens it", HCT_MUTED);
    } else if (m.refuse) {
        cell(&p->val[4], m.refuse);
        cell(&p->msg, "RETURN opens it", HCT_MUTED);
    } else {
        cell(&p->val[4], "one exFAT partition");
        cell(&p->msg, "F formats this medium as exFAT\nRETURN opens it", HCT_OK);
    }
}

static void panel_full(HcPanel *p, const char *path)
{
    char t[96];
    probe_full(path);
    bool usable = g_bcw_res == CFW_OK;
    for (int i = 0; i < 7; i++) {
        p->kv_vis[i] = usable;
    }
    p->msg_vis = usable;
    p->err_vis = !usable;
    if (!usable) {
        cell(&p->title, "Not a usable firmware image", HCT_BAD);
        cell(&p->err, cfw_result_str((cfw_result_t)g_bcw_res), HCT_MUTED);
        return;
    }
    cell(&p->title, g_bcw.name, HCT_INK);
    cell(&p->val[0], g_bcw_origin == COR_ORIGIN_FOREIGN ? "packaged firmware" : (g_bcw_origin == COR_ORIGIN_BACKUP ? "backup of this board" : "full firmware"));
    cell(&p->val[1], "none, it replaces the host");
    cell(&p->val[2], g_bcw.part);
    snprintf(t, sizeof(t), "%u B bitstream", (unsigned)g_bcw.bit_len);
    cell(&p->val[3], t);
    snprintf(t, sizeof(t), "%u B at 0x%X", (unsigned)g_bcw.app_len, (unsigned)g_bcw.app_load);
    cell(&p->val[4], t);
    cell(&p->val[5], g_bcw.map == CFW_MAP_IDENTITY ? "map identity" : "map gideon_bl4");
    int m = hc_in_menu(path);
    cell(&p->val[6], m >= 0 ? "in the boot menu" : "not in the menu");
    if (cfw_is_name(path)) {                                      // T137: a .cfw/.ue2 can be made into a .cor
        cell(&p->msg, m >= 0 ? "A refreshes the entry\nS starts once    back: power cycle\nP packages it as a .cor in the cores folder" : "A adds it to the boot menu\nS starts once    back: power cycle\nP packages it as a .cor in the cores folder", HCT_OK);
    } else {
        cell(&p->msg, m >= 0 ? "A refreshes the entry\nS starts once    back: power cycle" : "A adds it to the boot menu\nS starts once    back: power cycle", HCT_OK);
    }
}

void hc_browse_panel(HcPanel *p)
{
    memset(p, 0, sizeof(*p));
    const HcNav &v = hc_nav();
    char path[200];
    if (hc_browse_at_media()) {
        panel_media(p);
        return;
    }
    p->keys_mode = 0;
    if (v.br_mode == HCB_INSPECT && v.bsel >= 0 && v.bsel < v.bn && !v.bl[v.bsel].dir && hc_esp_pickable(v.bl[v.bsel].name)) {      // T152 D8: the pick says what RETURN does
        char sz[24], t[200];
        p->msg_vis = false;
        p->err_vis = true;
        ui_size_text(sz, sizeof(sz), v.bl[v.bsel].size64);
        snprintf(t, sizeof(t), "%s\nRETURN looks into this file for an ESP32 image set and its version. The file is only read, nothing is written.", sz);
        cell(&p->title, v.bl[v.bsel].name, HCT_INK);
        cell(&p->err, t, HCT_MUTED);
        return;
    }
    bool sel_cor = hc_browse_sel_bundle(path, sizeof(path));
    bool usable = false, contract_bad = false, host_bad = false;
    if (!sel_cor) {                                   // a folder, a file the boot menu does not take, or nothing
        p->msg_vis = false;
        p->err_vis = true;
        if (v.bsel >= 0 && v.bsel < v.bn && !v.bl[v.bsel].dir) {
            char sz[24], t[200];
            ui_size_text(sz, sizeof(sz), v.bl[v.bsel].size64);
            if (txu_is_name(v.bl[v.bsel].name)) {                   // T152: a MultiCore update says what it is and how it is installed
                txu_panel(v.bl[v.bsel].name, sz, t, sizeof(t));
            } else {
                snprintf(t, sizeof(t), "%s\nNot a boot menu file: the boot menu takes .cor bundles and .cfw firmware.", sz);
            }
            cell(&p->title, v.bl[v.bsel].name, HCT_INK);
            cell(&p->err, t, HCT_MUTED);
        } else {
            cell(&p->title, "", HCT_INK);
            cell(&p->err, v.bn ? "Select a .cor bundle or a .cfw firmware to see its manifest." : "", HCT_MUTED);
        }
        return;
    }
    if (hc_is_full(path)) {
        panel_full(p, path);
        return;
    }
    probe_cor(path);
    // a bundle for another contract or another host still shows its manifest, so the reason is on the screen; adding and starting refuse it (T84 D5a, D5b)
    usable = g_bci_res == COR_OK || g_bci_res == COR_ERR_CONTRACT || g_bci_res == COR_ERR_HOST;
    contract_bad = g_bci_res == COR_ERR_CONTRACT;
    host_bad = g_bci_res == COR_ERR_HOST;
    for (int i = 0; i < 7; i++) {
        p->kv_vis[i] = usable;
    }
    p->msg_vis = usable;
    p->err_vis = !usable;
    if (!usable) {
        cell(&p->title, "Not a usable bundle", HCT_BAD);
        cell(&p->err, cor_result_str((cor_result_t)g_bci_res), HCT_MUTED);
        return;
    }
    char t[96];
    cell(&p->title, g_bci.name, HCT_INK);
    cell(&p->val[0], g_bci.version);
    if (cor_host_key) {
        snprintf(t, sizeof(t), "%u (host %d), host %08x", (unsigned)g_bci.contract, COR_HOST_CONTRACT, (unsigned)g_bci.host_key);
    } else {
        snprintf(t, sizeof(t), "%u (host %d)", (unsigned)g_bci.contract, COR_HOST_CONTRACT);
    }
    cell(&p->val[1], t);
    cell(&p->val[2], g_bci.fpga);
    snprintf(t, sizeof(t), "%u B", (unsigned)g_bci.size[COR_PART_PARTIAL]);
    cell(&p->val[3], t);
    snprintf(t, sizeof(t), "%u B at 0x%X", (unsigned)g_bci.size[COR_PART_APP], (unsigned)g_bci.app_load);
    cell(&p->val[4], t);
    cell(&p->val[5], "checked once, when added");
    int m = hc_in_menu(path);
    cell(&p->val[6], m >= 0 ? "in the boot menu" : "not in the menu");
    if (contract_bad) {
        snprintf(t, sizeof(t), "Built for host contract %u; this host has %d. Cannot be added.", (unsigned)g_bci.contract, COR_HOST_CONTRACT);
        cell(&p->msg, t, HCT_WARN);
    } else if (host_bad) {
        snprintf(t, sizeof(t), "Routed against host %08x; this host is %08x. Cannot be added.", (unsigned)g_bci.host_key, (unsigned)cor_host_key);
        cell(&p->msg, t, HCT_WARN);
    } else {
        cell(&p->msg, m >= 0 ? "A refreshes the entry\nS starts once" : "A adds it to the boot menu\nS starts once", HCT_OK);
    }
}

// ---- the network page and the WiFi page ------------------------------------------------------------------------------------------------------------
static int wifi_state_tone(const WifiNetStatus &w)
{
    switch (w.state) {
    case WN_LINK_UP: return HCT_OK;
    case WN_LINK_DOWN: return HCT_WARN;
    case WN_NO_MODULE: return HCT_BAD;
    default: return HCT_MUTED;
    }
}

// The action that was under way has ended: its result is the page's note. Both pages that show the WiFi (F7, the WiFi page of F8) call this.
void hc_wifi_op_poll(void)
{
    char v[160];
    static int last_wstate = -1;
    if ((int)g_st.wifi.state != last_wstate) {                       // a note about the module is stale once its state changed
        last_wstate = (int)g_st.wifi.state;
        hc_nnote_clear();
    }
    if (hc_wifi_op_run() && !g_st.wifi.busy) {
        hc_wifi_op_done();
        if (g_st.wifi.last == 0) {
            snprintf(v, sizeof(v), "%s: done", hc_wifi_op_text());
        } else {
            snprintf(v, sizeof(v), "%s: failed (%d)%s%s", hc_wifi_op_text(), g_st.wifi.last, g_st.wifi.why[0] ? ". " : "", g_st.wifi.why);
        }
        if (g_pl.net_note) {
            g_pl.net_note(v, g_st.wifi.last == 0 ? HCT_OK : HCT_BAD);
        } else {
            hc_nnote_set(v, g_st.wifi.last == 0 ? HCT_OK : HCT_BAD);
        }
    }
}

// State, network and IP address of the WiFi, on three labels (F7 and the WiFi page of F8 show the same).
void hc_wifi_texts3(char (&st)[160], const char *&net, const char *&ip)
{
    const WifiNetStatus &w = g_st.wifi;
    if (w.busy) {
        snprintf(st, sizeof(st), "%s ...", hc_wifi_op_run() ? hc_wifi_op_text() : "Busy");
    } else {
        snprintf(st, sizeof(st), "%s", wifi_state_word(w));                // the words of the radio row on the WiFi page (R6)
    }
    net = w.ssid[0] ? w.ssid : "(none)";
    ip = w.ip[0] ? w.ip : (w.state == WN_LINK_UP ? "waiting for an address" : "(none)");
}

static void fill3(HcCell *st, HcCell *net, HcCell *ipl)
{
    char v[160];
    const char *n, *ip;
    hc_wifi_texts3(v, n, ip);
    const WifiNetStatus &w = g_st.wifi;
    cell(st, v, w.busy ? HCT_WARN : wifi_state_tone(w));
    cell(net, n, w.ssid[0] ? HCT_INK : HCT_MUTED);
    cell(ipl, ip, w.ip[0] ? HCT_INK : HCT_MUTED);
}

void hc_net_texts(HcNetTexts *t)
{
    char v[160];
    memset(t, 0, sizeof(*t));
    snprintf(v, sizeof(v), g_st.enet ? "link up, %s" : "no link or no address yet%s", g_st.enet ? g_st.eip : "");
    cell(&t->eth, v, g_st.enet ? HCT_OK : HCT_WARN);
    char fv[64];
    ftp_state_text(ftp_state_now(), g_st.net, fv, sizeof(fv));
    cell(&t->ftp, fv);
    cell(&t->media, g_st.media[0] ? g_st.media : "(none)");
    char rv[96] = "";
    if (g_pl.rest_text) {
        g_pl.rest_text(g_st.net, rv, sizeof(rv));
    }
    cell(&t->rest, rv);
    hc_wifi_op_poll();
    fill3(&t->wst, &t->wnet, &t->wip);
    bool mod = g_st.wifi.state != WN_NO_MODULE && g_st.wifi.mac[0];
    snprintf(v, sizeof(v), "%s%s%s", mod ? g_st.wifi.mac : "(none)", mod && g_st.wifi.module[0] ? "    " : "", mod ? g_st.wifi.module : "");
    cell(&t->wmac, v);
    const HcNav &nav = hc_nav();
    cell(&t->note, nav.nnote[0] ? nav.nnote : "The WiFi is set under F8 (Settings, WiFi).", nav.nnote[0] ? nav.nnote_tone : HCT_MUTED);
}

const char *hc_wifi_net_set_name(int i)
{
    static const char *const id[NS_N] = { "radio", "dhcp", "ip", "mask", "gw", "dns" };
    return hc_item_title("settings/wifi", id[i]);
}

void hc_wifi_texts(HcWifiTexts *t)
{
    memset(t, 0, sizeof(*t));
    hc_wifi_op_poll();
    fill3(&t->a, &t->b, &t->c);
    hc_nav_clamp_nsel(NS_N);
    const WifiNetStatus &w = g_st.wifi;
    for (int i = 0; i < NS_N; i++) {
        const char *s = "";
        bool dim = false;
        if (i == NS_RADIO) {                                          // live from the driver, not from the store
            s = hc_wifi_radio_text(w);
            dim = w.state == WN_NO_MODULE;
        } else if (!g_st.wcfg_ok) {
            s = "(no WiFi store)";
            dim = true;
        } else if (i == NS_DHCP) {
            s = g_st.wcfg.dhcp ? "Enabled" : "Disabled";
        } else {
            s = i == NS_IP ? g_st.wcfg.ip : i == NS_MASK ? g_st.wcfg.netmask : i == NS_GW ? g_st.wcfg.gateway : g_st.wcfg.dns;
            dim = g_st.wcfg.dhcp;                                     // used with DHCP off only (Gideon greys them)
        }
        cell(&t->val[i], s, dim ? HCT_MUTED : HCT_INK);
        t->name_dim[i] = dim && i != NS_DHCP && i != NS_RADIO;
    }
    cell(&t->note, hc_nav().nnote, hc_nav().nnote_tone);
}

// ---- the settings page ----------------------------------------------------------------------------------------------------------------------------
const char *hc_set_name(int kind)
{
    static const char *const id[] = { "direct", "auto", "secs", "hdmi", "protect", "wifi", "ftp", "esp", "cores", "report" };
    return kind >= 0 && kind < 10 ? hc_item_title("settings", id[kind]) : "";
}

int hc_set_kinds(int *k)
{
    const BootCfg &cfg = hc_cfg();
    int n = 0;
    k[n++] = SK_DIRECT;
    if (!cfg.direct) {
        k[n++] = SK_AUTO;
        if (cfg.countdown > 0) {
            k[n++] = SK_SECS;
        }
    }
    k[n++] = SK_HDMI;
    if (HOSTAPP_FLASH_LOCK) {
        k[n++] = SK_PROTECT;                     // T152 D9: only in a build with HOSTAPP_FLASH_LOCK
    }
    k[n++] = SK_WIFI;
    k[n++] = SK_FTP;
    k[n++] = SK_ESP;
    k[n++] = SK_CORES;
    k[n++] = SK_REPORT;
    return n;
}

void hc_set_report_name(const char *name) { snprintf(g_rep_last, sizeof(g_rep_last), "%s", name); }

void hc_set_row_text(int kind, const char *&name, char (&v)[64], const char *&help, bool with_help)
{
    const BootCfg &cfg = hc_cfg();
    name = "";
    help = "";
    v[0] = 0;
    switch (kind) {
    case SK_DIRECT:
        name = hc_set_name(SK_DIRECT);
        snprintf(v, sizeof(v), "%s", cfg.direct ? "on" : "off");
        help = cfg.direct ? "The default core starts at power-on without the menu. C= or fire on port 1 at power-on shows the menu."
                          : "The menu comes up first; the autostart below decides whether it starts the default.";
        break;
    case SK_AUTO:
        name = hc_set_name(SK_AUTO);
        snprintf(v, sizeof(v), "%s", cfg.countdown > 0 ? "on" : "off");
        help = "On: the menu starts the default core by itself after the delay. Any key stops the countdown.";
        break;
    case SK_SECS:
        name = hc_set_name(SK_SECS);
        snprintf(v, sizeof(v), "%d s", cfg.countdown);
        help = "1 to 60 seconds. LEFT/RIGHT change it by one.";
        break;
    case SK_HDMI: {
        name = hc_set_name(SK_HDMI);
        const HvState *hs = hv_state();
        int up = hc_picture_hdmi();
        if (hs->def_hdmi < 0) {
            snprintf(v, sizeof(v), "%s: %s", g_compact ? "screen" : "from the screen", up < 0 ? "?" : video_hdmi_name(up));   // the compact value column is the rest of a narrow row
        } else if (hv_offered(hs->mask, hs->def_sys, hs->def_hdmi)) {
            snprintf(v, sizeof(v), "%s", video_hdmi_name(hs->def_hdmi));
        } else {
            snprintf(v, sizeof(v), "%s (not offered, %s up)", video_hdmi_name(hs->def_hdmi), up < 0 ? "?" : video_hdmi_name(up));
        }
        help = "Only what the screen names is offered. A change asks for a key within 8 s, else it goes back. V steps the mode in every view.";
        break;
    }
    case SK_PROTECT:
        name = hc_set_name(SK_PROTECT);
        snprintf(v, sizeof(v), "%s", flashprot_enabled() ? "on" : "off");
        if (with_help) {
            static char prot_help[240];
            char st[120];
            flashprot_status_text(st, sizeof(st));
            snprintf(prot_help, sizeof(prot_help), "%s %s", flashprot_enabled() ? "On: a large core gets the machine with the host and its app (flash 0x000000-0x3FFFFF) locked until power-off."
                                                                                 : "Off: a large core can write the whole flash, the host and its app included.", st);
            help = prot_help;
        }
        break;
    case SK_WIFI:
        name = hc_set_name(SK_WIFI);
        snprintf(v, sizeof(v), "open");
        help = "Use DHCP or static addresses, the access point list, enter an access point, connect to the last one, disconnect, forget. The cores show the WiFi and switch it on and off.";
        break;
    case SK_FTP: {
        name = hc_set_name(SK_FTP);
        ftp_state_text(ftp_state_now(), g_st.net, v, sizeof(v));          // the words of the Network page's FTP row
        help = "Switches the FTP file service. Stored in host.cfg; the listener follows at once, a connection that is already open ends with its session.";
        break;
    }
    case SK_ESP:
        name = hc_set_name(SK_ESP);
        snprintf(v, sizeof(v), "open");
        help = "Looks into a firmware file for its ESP32 image set and version. RETURN opens the File Browser; pick the file there. Reads the file only, writes nothing.";
        break;
    case SK_CORES:
        name = hc_set_name(SK_CORES);
        snprintf(v, sizeof(v), "open");
        help = "Each core's folder, its ROM files and saves; what belongs to no core and what is there twice. Deletes the files that belong to no core.";
        break;
    case SK_REPORT:
        name = hc_set_name(SK_REPORT);
        snprintf(v, sizeof(v), "%s", g_rep_last[0] ? g_rep_last : "write to SD/USB");
        help = "RETURN writes report/<date-time>.txt on the SD card, else on a USB stick: versions, settings, the cores in the menu and the last log lines. No passwords or keys. The same text is at /v1/report.";
        break;
    }
}

bool hc_set_toggle_state(int kind)
{
    switch (kind) {
    case SK_DIRECT: return hc_cfg().direct;
    case SK_AUTO: return hc_cfg().countdown > 0;
    case SK_PROTECT: return flashprot_enabled();
    case SK_FTP: return ftp_state_now().on;
    }
    return false;
}

const char *hc_set_key_name(int kind)
{
    static const char *const n[] = { "settings.direct", "settings.auto", "settings.secs", "settings.hdmi", "settings.protect", "settings.wifi", "settings.ftp",
                                     "settings.esp", "settings.cores", "settings.report" };
    return kind >= 0 && kind < 10 ? n[kind] : "";
}

int hc_set_kind_of(const char *key)
{
    for (int k = 0; k < 10; k++) {
        if (!strcmp(key, hc_set_key_name(k))) {
            return k;
        }
    }
    return -1;
}

const char *hc_wifi_field_name(int row)
{
    static const char *const names[NS_N] = { "wifi.radio", "wifi.dhcp", "wifi.ip", "wifi.mask", "wifi.gw", "wifi.dns" };
    return row >= 0 && row < NS_N ? names[row] : "";
}

int hc_wifi_row_of(const char *key)
{
    for (int i = 0; i < NS_N; i++) {
        if (!strcmp(key, hc_wifi_field_name(i))) {
            return i;
        }
    }
    return -1;
}

// ---- the header chips ---------------------------------------------------------------------------------------------------------------------------
const char *hc_chip_net_text(void) { return g_st.chip_txt; }

void hc_chips_sig(char *out, int n)
{
    snprintf(out, n, "%d%d%d%d%s", g_st.usb, g_st.sd, g_st.net, g_st.lan, g_st.ip);
}

int hc_chips(HcChip *out, int cap)
{
    int n = 0;
    net_chip_text(g_st.chip_txt, sizeof(g_st.chip_txt), g_st.net && g_st.lan ? g_st.ip : "", g_st.net && !g_st.lan ? g_st.ip : "", NET_NONE_TEXT);        // D5: "LAN x" / "WiFi x"; the cable first
    if (n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "%s", g_st.chip_txt);
        out[n++].ok = g_st.net;
    }
    if (n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "USB");
        out[n++].ok = g_st.usb;
    }
    if (n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "SD");
        out[n++].ok = g_st.sd;
    }
    if (n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "contract %d", COR_HOST_CONTRACT);
        out[n++].ok = false;
    }
    // the host key of the running bitstream (T84 D5b) and, after it, the app's own build
    if (cor_host_key && n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "host %08x", (unsigned)cor_host_key);
        out[n++].ok = false;
    }
    if (n < cap) {
        snprintf(out[n].text, sizeof(out[n].text), "app %s", g_pl.app_version ? g_pl.app_version : "");
        out[n++].ok = false;
    }
    return n;
}

// ---- the dump ------------------------------------------------------------------------------------------------------------------------------------------
// Every page of the table prints what it shows now, one line per row, from the producers above. A bench reads the table against the declaration's page list and the
// render bench against what the screen drew.
namespace {

struct Out {
    char *p;
    int cap, n;
    void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        if (n >= cap - 1) {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        int k = vsnprintf(p + n, cap - n, fmt, ap);
        va_end(ap);
        n += k < cap - n ? k : cap - n - 1;
    }
};

const char *tone_name(int t)
{
    static const char *const n[] = { "-", "ink", "muted", "ok", "warn", "bad", "host" };
    return t >= 0 && t < 7 ? n[t] : "?";
}

void cellrow(Out &o, const char *id, const HcCell &c)
{
    o.add("  %s|%s|%s%s\n", id, c.text, tone_name(c.tone), c.set ? "" : "|(not written)");
}

int dump_menu(char *out, int cap)
{
    Out o = { out, cap, 0 };
    const BootCfg &cfg = hc_cfg();
    HcMenuTexts t;
    hc_menu_texts(hc_nav().mfirst, cfg.n, &t);
    o.add("page menu|%s|%s\n", hc_page_title("menu"), hc_foot(HCF_MENU));
    o.add("  sub|%s\n  hint|%s|%s\n  pos|%s\n", t.sub, t.hint, t.hint_on ? "shown" : "hidden", t.pos);
    HcCountdownTexts cd;
    hc_countdown_texts(&cd);
    o.add("  countdown|%s|%s|%d\n", cd.text, cd.visible ? "shown" : "hidden", cd.pct);
    for (int i = 0; i < cfg.n; i++) {
        HcEntryRow r;
        if (hc_row_entry(i, true, &r)) {
            o.add("  row %d|%s|%s|%s|%s|%s|%s|%s\n", i, r.name, r.sub, r.badge, tone_name(r.badge_tone), r.def ? "default" : "", r.tile, i == hc_nav().sel ? "SEL" : "");
        }
    }
    return o.n;
}

int dump_edit(char *out, int cap)
{
    Out o = { out, cap, 0 };
    const BootCfg &cfg = hc_cfg();
    HcEditTexts t;
    hc_edit_texts(hc_nav().efirst, &t);
    o.add("page edit|%s|%s\n", hc_page_title("edit"), hc_foot(HCF_EDIT));
    o.add("  countdown|%s\n  direct|%s\n  default|%s\n  pos|%s\n", t.countdown, t.direct, t.def, t.pos);
    for (int i = 0; i < cfg.n; i++) {
        HcEntryRow r;
        if (hc_row_entry(i, false, &r)) {
            o.add("  row %d|%s|%s|%s|%s|%s|%s\n", i, r.name, r.sub, r.badge, tone_name(r.badge_tone), r.def ? "default" : "", i == hc_nav().sel ? "SEL" : "");
        }
    }
    return o.n;
}

int dump_browse(char *out, int cap)
{
    Out o = { out, cap, 0 };
    const HcNav &v = hc_nav();
    char t[200];
    hc_browse_title(t, sizeof(t));
    o.add("page browse|%s|%s\n", hc_page_title("browse"), hc_browse_foot());
    o.add("  path|%s\n", t);
    hc_browse_empty_text(t, sizeof(t));
    o.add("  empty|%s\n", t);
    for (int k = 0; k < v.bn; k++) {
        HcBrowseRow r;
        if (hc_browse_row(k, &r)) {
            o.add("  row %d|%s|%s|%s|%s\n", k, r.mark, r.name, r.size, k == v.bsel ? "SEL" : "");
        }
    }
    static HcPanel p;
    hc_browse_panel(&p);
    o.add("  panel|keys %d\n", p.keys_mode);
    cellrow(o, "title", p.title);
    for (int i = 0; i < 7; i++) {
        if (p.kv_vis[i]) {
            o.add("  kv %d|%s|%s|%s\n", i, hc_panel_key(p.keys_mode, i), p.val[i].text, p.val[i].set ? "" : "(not written)");
        }
    }
    if (p.msg_vis) {
        cellrow(o, "msg", p.msg);
    }
    if (p.err_vis) {
        cellrow(o, "err", p.err);
    }
    return o.n;
}

int dump_net(char *out, int cap)
{
    Out o = { out, cap, 0 };
    static HcNetTexts t;
    hc_net_texts(&t);
    o.add("page net|%s|%s\n", hc_page_title("net"), hc_foot(HCF_NET));
    cellrow(o, hc_item_title("net", "ethernet"), t.eth);
    cellrow(o, hc_item_title("net", "ftp"), t.ftp);
    cellrow(o, hc_item_title("net", "media"), t.media);
    cellrow(o, "REST", t.rest);
    cellrow(o, hc_item_title("net", "wifi_state"), t.wst);
    cellrow(o, hc_item_title("net", "wifi_net"), t.wnet);
    cellrow(o, hc_item_title("net", "wifi_addr"), t.wip);
    cellrow(o, hc_item_title("net", "wifi_mac"), t.wmac);
    cellrow(o, "note", t.note);
    return o.n;
}

int dump_settings(char *out, int cap)
{
    Out o = { out, cap, 0 };
    o.add("page settings|%s|%s\n", hc_page_title("settings"), hc_foot(HCF_SET));
    int k[HC_SET_SLOTS], n = hc_set_kinds(k);
    for (int i = 0; i < n; i++) {
        char v[64];
        const char *name, *help;
        hc_set_row_text(k[i], name, v, help, true);
        o.add("  %s|%s|%s|%s|%s\n", hc_set_key_name(k[i]), name, v, help, i == hc_nav().ssel ? "SEL" : "");
    }
    return o.n;
}

int dump_wifi(char *out, int cap)
{
    Out o = { out, cap, 0 };
    static HcWifiTexts t;
    hc_wifi_texts(&t);
    o.add("page settings/wifi|%s|%s\n", hc_page_title("settings/wifi"), hc_foot(HCF_WM));
    cellrow(o, hc_item_title("settings/wifi", "state"), t.a);
    cellrow(o, hc_item_title("settings/wifi", "net"), t.b);
    cellrow(o, hc_item_title("settings/wifi", "addr"), t.c);
    for (int i = 0; i < NS_N; i++) {
        o.add("  %s|%s|%s|%s|%s%s\n", hc_wifi_field_name(i), hc_wifi_net_set_name(i), t.val[i].text, tone_name(t.val[i].tone), t.name_dim[i] ? "dim " : "", i == hc_nav().nsel ? "SEL" : "");
    }
    cellrow(o, "note", t.note);
    return o.n;
}

int dump_cores(char *out, int cap)
{
    Out o = { out, cap, 0 };
    char h[HC_CM_LINE];
    int ht;
    hc_cm_head(h, sizeof(h), &ht);
    o.add("page settings/cores|%s|%s\n", hc_page_title("settings/cores"), hc_cm_foot());
    o.add("  head|%s|%s\n", h, tone_name(ht));
    for (int i = 0; i < hc_cm_lines(); i++) {
        int tone;
        const char *l = hc_cm_line(i, &tone);
        o.add("  line %d|%s|%s\n", i, l, tone_name(tone));
    }
    int mt;
    const char *m = hc_cm_msg(&mt);
    o.add("  msg|%s|%s\n", m, tone_name(mt));
    return o.n;
}

int dump_colors(char *out, int cap)
{
    Out o = { out, cap, 0 };
    o.add("page colors|%s|\n", hc_page_title("colors"));
    o.add("  text|%s\n  pattern|%c\n", HC_COLORS_TEXT, hc_nav().cpat ? hc_nav().cpat : '-');
    return o.n;
}

}  // namespace

const HcPageDef HC_PAGES[] = {
    { "menu", dump_menu },
    { "browse", dump_browse },
    { "edit", dump_edit },
    { "net", dump_net },
    { "settings", dump_settings },
    { "settings/wifi", dump_wifi },
    { "settings/cores", dump_cores },
    { "colors", dump_colors },
};
const int HC_NPAGES = (int)(sizeof(HC_PAGES) / sizeof(HC_PAGES[0]));

int hc_rows_dump(char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < HC_NPAGES && n < cap - 1; i++) {
        n += HC_PAGES[i].dump(out + n, cap - n);
    }
    return n;
}
