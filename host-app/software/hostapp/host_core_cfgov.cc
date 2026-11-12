#include "host_core_cfgov.h"
#include "host_core.h"
#include "host_core_flow_wifi.h"
#include "host_core_flow.h"
#include "listwin.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {
CmFs *g_cm;
FoFs *g_fo;
HcCfgOv g_ov;
CmEntry g_ents[64];

struct Cached {
    char path[200];
    int n;
    uint32_t gen;
};
Cached g_cache[BOOT_MAX + 4];
int g_ncache;
uint32_t g_gen = 1;

// One listing of both folders of a core into `l` (S2, S10: the same configlist.cc the core app uses).
bool read_list(const char *cor_path, char *dir, int dcap, ConfigList *l)
{
    configlist_clear(l);
    if (!g_cm || !config_core_dir(cor_path, dir, dcap)) {
        return false;
    }
    for (int pass = 0; pass < 2; pass++) {
        char d[220];
        if (!config_dir_path(dir, pass == 1, d, sizeof(d))) {
            continue;
        }
        int n = g_cm->list(d, g_ents, (int)(sizeof(g_ents) / sizeof(g_ents[0])));
        if (pass == 0 && n > 0 && g_fo) {
            // an interrupted save of the core application (X.bak without X.cfg, leftover X.tmp) is put right before the list is made
            struct G { static const char *get(void *, int i, bool *dd) { *dd = g_ents[i].dir; return g_ents[i].name; } };
            static ConfigFix fix;
            configlist_fix_plan(&fix, n, G::get, 0);
            bool changed = false;
            for (int i = 0; i < fix.n; i++) {
                char from[300], to[300];
                snprintf(from, sizeof(from), "%s/%s", d, fix.act[i].file);
                snprintf(to, sizeof(to), "%s/%s", d, fix.act[i].to);
                changed |= fix.act[i].recover ? g_fo->rename(from, to) : g_fo->remove(from);
            }
            if (changed) {
                n = g_cm->list(d, g_ents, (int)(sizeof(g_ents) / sizeof(g_ents[0])));
            }
        }
        for (int i = 0; i < n; i++) {
            configlist_add(l, g_ents[i].name, g_ents[i].dir, pass == 1);
        }
    }
    configlist_sort(l);
    return true;
}

void setmsg(const char *m, int tone)
{
    snprintf(g_ov.msg, sizeof(g_ov.msg), "%s", m);
    g_ov.msgtone = tone;
}

void window(void)
{
    listwin_fix(g_ov.cur, g_ov.l.n + 1, HC_CFGOV_ROWS, &g_ov.top);       // T209 D3: the same 2-row margin as the other lists
}

void file_of(int k, char *out, int cap)
{
    config_file_path(g_ov.dir, g_ov.l.name[k], g_ov.l.legacy[k], out, cap);
}

// Every boot.cfg entry of this core whose default is `from` gets `to` ("" clears it).
void follow_default(const char *from, const char *to)
{
    const BootCfg &c = hc_cfg();
    for (int i = 0; i < c.n; i++) {
        char d[200];
        if (c.e[i].defcfg[0] && !strcmp(c.e[i].defcfg, from) && config_core_dir(c.e[i].path, d, sizeof(d)) && !strcmp(d, g_ov.dir)) {
            hc_entry_defcfg(i, to);
        }
    }
}
}

void hc_cfgov_io(CmFs *cm, FoFs *fo)
{
    g_cm = cm;
    g_fo = fo;
    g_ncache = 0;
    g_gen++;
    memset(&g_ov, 0, sizeof(g_ov));
}

void hc_cfgov_forget(void) { g_gen++; }

int hc_cfgov_count(const char *cor_path)
{
    for (int i = 0; i < g_ncache; i++) {
        if (g_cache[i].gen == g_gen && !strcmp(g_cache[i].path, cor_path)) {
            return g_cache[i].n;
        }
    }
    static ConfigList l;
    char dir[200];
    int n = read_list(cor_path, dir, sizeof(dir), &l) ? l.n : 0;
    int at = -1;
    for (int i = 0; i < g_ncache; i++) {
        if (!strcmp(g_cache[i].path, cor_path)) {
            at = i;
        }
    }
    if (at < 0) {
        if (g_ncache >= (int)(sizeof(g_cache) / sizeof(g_cache[0]))) {
            g_ncache = 0;
        }
        at = g_ncache++;
    }
    snprintf(g_cache[at].path, sizeof(g_cache[at].path), "%s", cor_path);
    g_cache[at].n = n;
    g_cache[at].gen = g_gen;
    return n;
}

bool hc_cfgov_open(const char *cor_path, const char *title, int entry, bool browse)
{
    HcCfgOv o;
    memset(&o, 0, sizeof(o));
    if (!read_list(cor_path, o.dir, sizeof(o.dir), &o.l) || o.l.n == 0) {
        return false;
    }
    o.open = true;
    o.entry = entry;
    o.browse = browse;
    snprintf(o.path, sizeof(o.path), "%s", cor_path);
    snprintf(o.title, sizeof(o.title), "%s", title);
    for (int n = (int)strlen(o.title); n > 0 && o.title[n - 1] == ' '; n--) {
        o.title[n - 1] = 0;                             // a padded entry name left a wide gap before " - start with" (UX review, ue2emu)
    }
    if (entry >= 0 && entry < hc_cfg().n) {
        snprintf(o.def, sizeof(o.def), "%s", hc_cfg().e[entry].defcfg);
    }
    int d = o.def[0] ? configlist_find(&o.l, o.def) : -1;
    o.cur = d >= 0 ? d + 1 : 0;                         // D14: the cursor opens on the default config, else on "Plain start"
    g_ov = o;
    window();
    return true;
}

void hc_cfgov_close(void) { g_ov.open = false; g_ov.ask = 0; }
const HcCfgOv &hc_cfgov(void) { return g_ov; }
bool hc_cfgov_is_open(void) { return g_ov.open; }

void hc_cfgov_disarm(void)
{
    if (g_ov.ask) {
        g_ov.ask = 0;
        g_ov.msg[0] = 0;
    }
}

void hc_cfgov_move(int dir)
{
    hc_cfgov_disarm();
    g_ov.msg[0] = 0;
    int c = g_ov.cur + dir;
    if (c >= 0 && c <= g_ov.l.n) {
        g_ov.cur = c;
    }
    window();
}

const char *hc_cfgov_cur_name(void) { return g_ov.cur > 0 ? g_ov.l.name[g_ov.cur - 1] : ""; }

bool hc_cfgov_default(void)
{
    hc_cfgov_disarm();
    int e = g_ov.entry;
    if (e < 0) {
        setmsg("Add the core to the boot menu first (F3, A), then it can have a default.", HCT_WARN);
        return false;
    }
    const char *nm = hc_cfgov_cur_name();
    bool same = nm[0] && !strcmp(g_ov.def, nm);
    const char *to = same ? "" : nm;
    int r = hc_entry_defcfg(e, to);
    if (r == HCR_REFUSED) {
        setmsg("That name cannot be a default.", HCT_WARN);
        return false;
    }
    snprintf(g_ov.def, sizeof(g_ov.def), "%s", to);
    if (r == HCR_SAVE_FAILED) {
        setmsg("Could not write boot.cfg.", HCT_BAD);
    } else if (!to[0]) {
        setmsg(nm[0] ? "No default config any more." : "No default config: the countdown starts plain.", HCT_OK);
    } else {
        setmsg("Default config set. The countdown and direct boot use it.", HCT_OK);
    }
    return true;
}

int hc_cfgov_delete(void)
{
    if (g_ov.cur == 0) {
        setmsg("\"Plain start\" is not a config.", HCT_MUTED);
        return 0;
    }
    if (!g_ov.ask) {
        g_ov.ask = 1;                                    // S9: the second DEL deletes
        char m[96];
        snprintf(m, sizeof(m), "DEL again deletes \"%s\". Any other key keeps it.", hc_cfgov_cur_name());
        setmsg(m, HCT_WARN);
        return 1;
    }
    g_ov.ask = 0;
    int k = g_ov.cur - 1;
    char f[260], name[CONFIG_NAME_MAX + 1];
    snprintf(name, sizeof(name), "%s", g_ov.l.name[k]);
    file_of(k, f, sizeof(f));
    if (!g_fo || !g_fo->remove(f)) {
        setmsg("The file could not be deleted.", HCT_BAD);
        return 0;
    }
    if (!g_ov.l.legacy[k]) {
        char lf[260];                                    // a legacy media/<name>.mnt of that name would show up as the config again
        if (config_file_path(g_ov.dir, name, true, lf, sizeof(lf))) {
            g_fo->remove(lf);                            // absent or not removable: the list shows what is left
        }
    }
    follow_default(name, "");
    snprintf(g_ov.def, sizeof(g_ov.def), "%s", g_ov.entry >= 0 && g_ov.entry < hc_cfg().n ? hc_cfg().e[g_ov.entry].defcfg : "");
    read_list(g_ov.path, g_ov.dir, sizeof(g_ov.dir), &g_ov.l);
    hc_cfgov_forget();
    if (g_ov.cur > g_ov.l.n) {
        g_ov.cur = g_ov.l.n;
    }
    window();
    char m[96];
    snprintf(m, sizeof(m), "Deleted \"%s\".", name);
    setmsg(m, HCT_OK);
    return 2;
}

bool hc_cfgov_rename(const char *to)
{
    hc_cfgov_disarm();
    if (g_ov.cur == 0) {
        return false;
    }
    int k = g_ov.cur - 1;
    const char *why = config_name_why(to);
    if (why[0]) {
        setmsg(why, HCT_WARN);
        return false;
    }
    char old[CONFIG_NAME_MAX + 1];
    snprintf(old, sizeof(old), "%s", g_ov.l.name[k]);
    if (!strcmp(old, to)) {
        return true;
    }
    int clash = configlist_find(&g_ov.l, to);
    if (clash >= 0 && strcasecmp(old, to)) {
        setmsg("A config of that name exists.", HCT_WARN);
        return false;
    }
    char from[260], dst[260];
    file_of(k, from, sizeof(from));
    config_file_path(g_ov.dir, to, g_ov.l.legacy[k], dst, sizeof(dst));
    if (!g_fo || !g_fo->rename(from, dst)) {
        setmsg("The file could not be renamed.", HCT_BAD);
        return false;
    }
    if (!g_ov.l.legacy[k]) {
        char lf[260];                                    // the legacy .mnt of the old name would show up as that config again
        if (config_file_path(g_ov.dir, old, true, lf, sizeof(lf))) {
            g_fo->remove(lf);
        }
    }
    follow_default(old, to);
    snprintf(g_ov.def, sizeof(g_ov.def), "%s", g_ov.entry >= 0 && g_ov.entry < hc_cfg().n ? hc_cfg().e[g_ov.entry].defcfg : "");
    read_list(g_ov.path, g_ov.dir, sizeof(g_ov.dir), &g_ov.l);
    hc_cfgov_forget();
    int at = configlist_find(&g_ov.l, to);
    g_ov.cur = at >= 0 ? at + 1 : 0;
    window();
    char m[96];
    snprintf(m, sizeof(m), "Renamed to \"%s\".", to);
    setmsg(m, HCT_OK);
    return true;
}

bool hc_cfgov_exists(const char *cor_path, const char *name)
{
    static ConfigList l;
    char d[200];
    return read_list(cor_path, d, sizeof(d), &l) && configlist_find(&l, name) >= 0;
}

bool hc_cfgov_cycle(const char *cor_path, const char *cur, int dir, char *out, int cap)
{
    static ConfigList l;
    char d[200];
    out[0] = 0;
    if (!read_list(cor_path, d, sizeof(d), &l) || l.n == 0) {
        return false;
    }
    int at = cur && cur[0] ? configlist_find(&l, cur) + 1 : 0;      // 0 = none; a default whose file is gone reads as none
    at = (at + dir + l.n + 1) % (l.n + 1);
    if (at > 0) {
        snprintf(out, (size_t)cap, "%s", l.name[at - 1]);
    }
    return true;
}

void hc_cfgov_badge(int n, char *out, int cap)
{
    if (n <= 0) {
        out[0] = 0;
    } else {
        snprintf(out, (size_t)cap, n == 1 ? "%d config" : "%d configs", n);
    }
}

void hc_cfgov_pick(HcWifiPick *p)
{
    memset(p, 0, sizeof(*p));
    p->cfgov = true;
    snprintf(p->title, sizeof(p->title), "%s - start with", g_ov.title);
    int total = g_ov.l.n + 1;
    const char *slash = strrchr(g_ov.path, '/');
    char who[64] = "";                                   // from the File Browser the sub text names the file that RETURN starts
    if (g_ov.browse) {
        const char *nm = slash ? slash + 1 : g_ov.path;      // cut by hand: the RISC-V image's printf ignores a precision
        size_t k = strlen(nm) < 40 ? strlen(nm) : 40;
        memcpy(who, nm, k);
        memcpy(who + k, " - ", 4);
    }
    if (total > HC_CFGOV_ROWS) {
        snprintf(p->sub, sizeof(p->sub), "%s%d configs   %d-%d of %d", who, g_ov.l.n, g_ov.top + 1,
                 g_ov.top + HC_CFGOV_ROWS < total ? g_ov.top + HC_CFGOV_ROWS : total, total);
    } else {
        snprintf(p->sub, sizeof(p->sub), g_ov.l.n == 1 ? "%s%d config" : "%s%d configs", who, g_ov.l.n);
    }
    p->subtone = HCT_MUTED;
    int n = 0;
    for (int r = g_ov.top; r < total && n < HC_CFGOV_ROWS; r++, n++) {
        HcWifiItem &it = p->item[n];
        if (r == 0) {
            snprintf(it.a, sizeof(it.a), "Plain start");
        } else {
            snprintf(it.a, sizeof(it.a), "%s", g_ov.l.name[r - 1]);
            if (g_ov.def[0] && !strcmp(g_ov.def, g_ov.l.name[r - 1])) {
                snprintf(it.c, sizeof(it.c), "auto");
                it.ctone = HCT_HOST;
            }
        }
    }
    p->n = n;
    p->sel = g_ov.cur - g_ov.top;
    p->first = 0;
    snprintf(p->msg, sizeof(p->msg), "%s", g_ov.msg);
    p->msgtone = (uint8_t)g_ov.msgtone;
    p->keys = "";                                        // hc_ui_cfgov_draw sets the key bar from the foot table (HCF_CFG / HCF_CFG_PLAIN)
}
