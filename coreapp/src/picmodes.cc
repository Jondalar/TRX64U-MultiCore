#include "picmodes.h"
#include "edid.h"
#include <stdio.h>
#include <string.h>

uint32_t picmodes_row_dhz(int row)
{
    const EdidRow *r = edid_row(row);
    if (!r || !r->htotal || !r->vtotal) {
        return 0;
    }
    uint64_t tot = (uint64_t)r->htotal * r->vtotal;
    return (uint32_t)(((uint64_t)r->pixel_hz * 10 + tot / 2) / tot);
}

void picmodes_set_row(PictureModes *pm, int i, const char *name, int hdmi, uint16_t mask, uint16_t native, int msys, int now_hdmi, bool cur_auto)
{
    PictureModeRow &r = pm->row[i];
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.refresh_dhz = 0;
    r.native = false;
    int row;
    if (hdmi >= 0 && video_modes_resolve(mask, msys, hdmi, &row)) {
        r.refresh_dhz = picmodes_row_dhz(row);
        r.native = (native >> row) & 1;
        if (i > 0 && hdmi < VID_HDMI_800x600 && video_row_sys(row) != (msys == VID_SYS_NTSC60 ? 1 : 0)) {      // a PC mode has only a 60 Hz row: that is no fallback
            pm->fallback = true;
        }
    }
    r.now = i == 0 ? cur_auto : (!cur_auto && hdmi >= 0 && hdmi == now_hdmi);       // T187 D9: one mark, on the row that is chosen (Auto's resolved mode keeps none)
    pm->sys60 = msys == VID_SYS_NTSC60;
}

void picmodes_mode_text(uint16_t mask, int sys, int hdmi, char *out, int n)
{
    int row;
    if (hdmi < 0 || hdmi >= VID_HDMI_COUNT) {
        snprintf(out, n, "the previous mode");
        return;
    }
    uint32_t dhz = 0;
    if (video_modes_resolve(mask, sys, hdmi, &row)) {
        dhz = picmodes_row_dhz(row);
    }
    if (dhz) {
        snprintf(out, n, "%s, %u.%u Hz", video_hdmi_name(hdmi), (unsigned)(dhz / 10), (unsigned)(dhz % 10));
    } else {
        snprintf(out, n, "%s", video_hdmi_name(hdmi));
    }
}

void picmodes_refresh_text(const PictureModes *pm, int i, char *out, int n)
{
    uint32_t d = pm->row[i].refresh_dhz;
    if (d) {
        snprintf(out, n, "%u.%u Hz", (unsigned)(d / 10), (unsigned)(d % 10));
    } else if (n > 0) {
        out[0] = 0;
    }
}

void picmodes_tag_text(const PictureModes *pm, int i, char *out, int n)
{
    const PictureModeRow &r = pm->row[i];
    snprintf(out, n, "%s%s%s", r.now ? "now" : "", r.now && r.native ? "  " : "", r.native ? "native" : "");
}

static bool picmodes_mixed(const PictureModes *pm)             // T187 F6: rows of the other family than the core's (a PC mode has only a 60 Hz row) stand in the list
{
    for (int i = 1; i < pm->n; i++) {
        uint32_t d = pm->row[i].refresh_dhz;
        if (d && (pm->sys60 ? d < 550 : d >= 550)) {
            return true;
        }
    }
    return false;
}

void picmodes_info(const PictureModes *pm, char *out, int n)
{
    if (pm->no_edid) {
        snprintf(out, n, "The screen did not name its modes; the standard TV modes are listed.");
    } else if (pm->fallback) {
        snprintf(out, n, "This screen names no %d Hz modes; %d Hz modes are listed.", pm->sys60 ? 60 : 50, pm->sys60 ? 50 : 60);
    } else if (pm->n > 1 && picmodes_mixed(pm)) {
        snprintf(out, n, "Your screen offers %d modes. The %d Hz ones are listed too: the core runs at %d Hz.", pm->n - 1, pm->sys60 ? 50 : 60, pm->sys60 ? 60 : 50);
    } else if (pm->n - 1 == 1) {
        snprintf(out, n, "Your screen offers 1 mode. Nothing changes until you pick one.");
    } else {
        snprintf(out, n, "Your screen offers %d modes. Nothing changes until you pick one.", pm->n - 1);
    }
}

const char *picmodes_confirm_keys(void)
{
    return "RETURN keeps it      LEFT or ESC goes back now";
}

void picmodes_confirm(int left_s, const char *now, const char *prev, char *out, int n)
{
    snprintf(out, n, "%s\nIn %d s %s comes back.\n\n%s", now, left_s, prev, picmodes_confirm_keys());
}

void picmodes_kept_text(const char *name, char *out, int n)
{
    snprintf(out, n, "Picture %s", name);
}

void picmodes_back_text(const char *prev, char *out, int n)
{
    snprintf(out, n, "Picture: back to %s", prev);
}

void picpage_open(PicPage *p, const PictureModes *pm)
{
    p->pm = *pm;
    p->sel = pm->cur < pm->n ? pm->cur : 0;
}

void picpage_refresh(PicPage *p, const PictureModes *pm, bool keep)
{
    char name[48] = "";
    if (keep && p->sel < p->pm.n) {
        snprintf(name, sizeof(name), "%s", p->pm.row[p->sel].name);
    }
    p->pm = *pm;
    if (keep) {
        p->sel = pm->cur < pm->n ? pm->cur : 0;
        for (int i = 0; i < pm->n; i++) {
            if (!strcmp(pm->row[i].name, name)) {
                p->sel = i;
                break;
            }
        }
    } else if (p->sel >= pm->n) {
        p->sel = pm->n - 1;
    }
}

PmAct picmodes_key(int *sel, int n, int cur, int k)
{
    switch (k) {
    case PMK_UP:
        if (*sel > 0) {
            (*sel)--;
        }
        return PMA_REDRAW;
    case PMK_DOWN:
        if (*sel < n - 1) {
            (*sel)++;
        }
        return PMA_REDRAW;
    case PMK_HOME:
        *sel = 0;
        return PMA_REDRAW;
    case PMK_END:
        *sel = n - 1;
        return PMA_REDRAW;
    case PMK_OK:
        return *sel == cur || n <= 2 ? PMA_CLOSE : PMA_APPLY;       // D5 edge case 1: one mode - Auto and the mode are the same picture, RETURN on either closes
    case PMK_BACK:
        return PMA_CLOSE;
    }
    return PMA_NONE;
}

PmAct picpage_key(PicPage *p, int k)
{
    return picmodes_key(&p->sel, p->pm.n, p->pm.cur, k);
}
