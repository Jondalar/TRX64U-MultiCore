// The page "Cores and ROMs" (host_core_cm.h): the code of hostui.cc's cm_* functions, moved with its texts unchanged.
#include "host_core_cm.h"
#include "host_core_nav_rw.h"
#include "host_core_flow_fileops.h"
#include "mediaui.h"
#include "keyboard.h"
#include <stdio.h>
#include <string.h>

#define CM_LINES 200

namespace {

struct CmLine {
    char t[HC_CM_LINE];
    uint8_t tone;
};
CmReport g_rep;
CmLine g_lines[CM_LINES];
int g_nlines;
char g_msg[160];
uint8_t g_msg_tone;

void cm_add(int tone, const char *fmt, const char *a = "", const char *b = "", const char *c = "")
{
    if (g_nlines >= CM_LINES) {
        return;
    }
    snprintf(g_lines[g_nlines].t, HC_CM_LINE, fmt, a, b, c);
    g_lines[g_nlines++].tone = (uint8_t)tone;
}

void lines_build(void)
{
    g_nlines = 0;
    char sz[24], t[HC_CM_LINE];
    if (hc_nav().cm_mode >= HCM_ASK1) {
        ui_size_text(sz, sizeof(sz), g_rep.delete_bytes);
        snprintf(t, sizeof(t), "These %d entries go (%s). Nothing else is touched.", g_rep.ndelete, sz);
        cm_add(HCT_WARN, "%s", t);
        for (int i = 0; i < g_rep.norph; i++) {
            if (g_rep.orph[i].deletable) {
                ui_size_text(sz, sizeof(sz), g_rep.orph[i].size);
                snprintf(t, sizeof(t), "%s%s   %s", g_rep.orph[i].path, g_rep.orph[i].dir ? "/" : "", g_rep.orph[i].dir ? "folder" : sz);
                cm_add(HCT_INK, "%s", t);
            }
        }
        return;
    }
    cm_add(HCT_HOST, "Cores (%s)", g_rep.ncores ? "folder beside the .cor, its ROM files, its saves" : "none found in cores/ of any medium");
    for (int i = 0; i < g_rep.ncores; i++) {
        const CmCore &c = g_rep.core[i];
        char rom[100];
        if (!c.folder) {
            snprintf(rom, sizeof(rom), "no folder yet (made at the first start)");
            snprintf(t, sizeof(t), "%-5s %-22s %s", c.medium, c.file, rom);
            cm_add(HCT_MUTED, "%s", t);
            continue;
        }
        if (c.need == 0) {
            snprintf(rom, sizeof(rom), "ROMs: none needed   saves %d", c.saves);
        } else if (c.have == c.need) {
            snprintf(rom, sizeof(rom), "ROMs %d/%d   saves %d", c.have, c.need, c.saves);
        } else {
            snprintf(rom, sizeof(rom), "ROMs %d/%d, missing %s   saves %d", c.have, c.need, c.missing, c.saves);
        }
        snprintf(t, sizeof(t), "%-5s %-22s %s", c.medium, c.file, rom);
        cm_add(c.have == c.need ? HCT_OK : HCT_WARN, "%s", t);
    }
    int kept = 0;
    for (int i = 0; i < g_rep.norph; i++) {
        kept += !g_rep.orph[i].deletable;
    }
    ui_size_text(sz, sizeof(sz), g_rep.delete_bytes);
    if (g_rep.norph) {
        snprintf(t, sizeof(t), "Belong to no core (%d entries go with DEL, %s; %d are kept)", g_rep.ndelete, sz, kept);
        cm_add(HCT_HOST, "%s", t);
        for (int i = 0; i < g_rep.norph; i++) {
            const CmOrphan &o = g_rep.orph[i];
            char s2[24] = "";
            if (!o.dir) {
                ui_size_text(s2, sizeof(s2), o.size);
            }
            snprintf(t, sizeof(t), "%s %s%s  %s  (%s)", o.deletable ? "goes" : "kept", o.path, o.dir ? "/" : "", s2, o.why);
            cm_add(o.deletable ? HCT_INK : HCT_MUTED, "%s", t);
        }
    } else {
        cm_add(HCT_HOST, "Belong to no core: nothing");
    }
    if (g_rep.ndups) {
        cm_add(HCT_HOST, "There twice or more (the same bytes)");
        for (int i = 0; i < g_rep.ndups; i++) {
            const CmDup &d = g_rep.dup[i];
            ui_size_text(sz, sizeof(sz), d.size);
            snprintf(t, sizeof(t), "%s  %s  in %d places: %s", d.name, sz, d.n, d.where[0]);
            cm_add(HCT_INK, "%s", t);
            for (int k = 1; k < 3 && k < d.n; k++) {
                snprintf(t, sizeof(t), "    and %s%s", d.where[k], k == 2 && d.n > 3 ? " ..." : "");
                cm_add(HCT_MUTED, "%s", t);
            }
        }
    }
    if (g_rep.truncated) {
        cm_add(HCT_WARN, "More than this page holds: the lists are cut.");
    }
}

}  // namespace

int hc_cm_lines(void) { return g_nlines; }
const char *hc_cm_line(int i, int *tone)
{
    if (i < 0 || i >= g_nlines) {
        if (tone) {
            *tone = HCT_INK;
        }
        return "";
    }
    if (tone) {
        *tone = g_lines[i].tone;
    }
    return g_lines[i].t;
}
int hc_cm_ndelete(void) { return g_rep.ndelete; }
const char *hc_cm_msg(int *tone)
{
    if (tone) {
        *tone = g_msg_tone;
    }
    return g_msg;
}

void hc_cm_clamp_top(void)
{
    int maxtop = g_nlines > hc_nav_cores_rows() ? g_nlines - hc_nav_cores_rows() : 0;
    if (hc_nav().cm_top > maxtop) {
        hc_nav_rw().cm_top = maxtop;
    }
}

const char *hc_cm_foot(void)
{
    switch (hc_nav().cm_mode) {
    case HCM_ASK1: return hc_foot(HCF_CM_ASK1);
    case HCM_ASK2: return hc_foot(HCF_CM_ASK2);
    default: return g_rep.ndelete ? hc_foot(HCF_CM_LIST) : hc_foot(HCF_CM_EMPTY);
    }
}

void hc_cm_head(char *h, int cap, int *tone)
{
    *tone = HCT_MUTED;
    if (hc_nav().cm_mode == HCM_ASK1) {
        snprintf(h, cap, "Delete these %d entries? Press Y. RETURN or any other key is No.", g_rep.ndelete);
        *tone = HCT_WARN;
    } else if (hc_nav().cm_mode == HCM_ASK2) {
        snprintf(h, cap, "Really delete %d entries? This cannot be undone. Press Y once more; any other key is No.", g_rep.ndelete);
        *tone = HCT_BAD;
    } else {
        snprintf(h, cap, "%s", "Every core keeps its settings, ROM files and saves in a folder of its own beside its .cor.");
    }
}

void hc_cm_open(const HcCmEnv &e)
{
    cm_scan(*e.cm, *e.fo, &g_rep);
    hc_nav_rw().cm_mode = HCM_LIST;
    hc_nav_rw().cm_top = 0;
    g_msg[0] = 0;
    lines_build();
    hc_note_clear();
}

void hc_cm_close(void)
{
    hc_nav_rw().cm_mode = HCM_OFF;
    hc_note_clear();
}

// The question the page asks (T192: the service shows it and answers it with the page's own keys): its text as the page draws it.
void hc_cm_ask_sync(const HcCmEnv &e)
{
    char h[HC_CM_LINE];
    if (!e.ask) {
        return;
    }
    if (hc_nav().cm_mode == HCM_ASK1) {
        snprintf(h, sizeof(h), "Delete these %d entries? Press Y. RETURN or any other key is No.", g_rep.ndelete);
        e.ask(e.ctx, h, 'y', KEY_ESCAPE);
    } else if (hc_nav().cm_mode == HCM_ASK2) {
        snprintf(h, sizeof(h), "Really delete %d entries? This cannot be undone. Press Y once more; any other key is No.", g_rep.ndelete);
        e.ask(e.ctx, h, 'y', KEY_ESCAPE);
    } else {
        e.ask(e.ctx, "", 0, 0);
    }
}

static void key_inner(const HcCmEnv &e, int act)
{
    HcNav &n = hc_nav_rw();
    int maxtop = g_nlines > hc_nav_cores_rows() ? g_nlines - hc_nav_cores_rows() : 0;
    if (act == HCA_CURSOR_UP) {
        n.cm_top = n.cm_top > 0 ? n.cm_top - 1 : 0;
        return;
    }
    if (act == HCA_CURSOR_DOWN) {
        n.cm_top = n.cm_top < maxtop ? n.cm_top + 1 : maxtop;
        return;
    }
    if (n.cm_mode == HCM_LIST) {
        if (act == HCA_BACK) {
            hc_cm_close();
        } else if (act == HCA_CORES_DELETE && g_rep.ndelete > 0) {
            n.cm_mode = HCM_ASK1;
            n.cm_top = 0;
            lines_build();
        } else if (act == HCA_CORES_DELETE) {
            g_msg_tone = HCT_MUTED;
            snprintf(g_msg, sizeof(g_msg), "Nothing belongs to no core: nothing to delete.");
        }
        return;
    }
    bool yes = act == HCA_CORES_YES;
    if (!yes) {                                          // RETURN and everything else: No
        n.cm_mode = HCM_LIST;
        n.cm_top = 0;
        lines_build();
        g_msg_tone = HCT_MUTED;
        snprintf(g_msg, sizeof(g_msg), "Cancelled. Nothing was deleted.");
        return;
    }
    if (n.cm_mode == HCM_ASK1) {
        n.cm_mode = HCM_ASK2;
        return;
    }
    CmDelete dl;
    char dmsg[HC_CM_LINE];
    int dtone;
    hc_fo_cores_delete(*e.flow, g_rep, &dl, dmsg, sizeof(dmsg), &dtone);
    cm_scan(*e.cm, *e.fo, &g_rep);
    n.cm_mode = HCM_LIST;
    n.cm_top = 0;
    lines_build();
    g_msg_tone = (uint8_t)dtone;
    snprintf(g_msg, sizeof(g_msg), "%s", dmsg);
}

void hc_cm_key(const HcCmEnv &e, int act)
{
    key_inner(e, act);
    hc_cm_ask_sync(e);
}
