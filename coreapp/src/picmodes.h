// T187 D5 (GitHub #50): the Picture page both applications show - the host's F8 Picture row and V key, the core's Video > Picture row. The rows, their texts, the
// cursor and the keys live here, once; each application only draws the rows and applies the mode (picture.apply in the core, hv_user_set in the host).
#ifndef COREAPP_PICMODES_H
#define COREAPP_PICMODES_H

#include <stdint.h>
#include "videomode.h"

// Row 0 is Auto, row n the n-th resolution of the list (the order of the Picture item's values, so the row index is the item's value). `refresh_dhz` is the pixel
// clock over the raster of the row the gateware is asked for (tenths of Hz, 0: unknown); `now`: the mode the picture is in (Auto's row too when Auto is the choice);
// `native`: the screen flags it as its native one.
struct PictureModeRow {
    char name[48];
    uint32_t refresh_dhz;
    bool now, native;
    int mode;                 // T235: the table row (0-11) of a core's row, -1 for Auto and for the host's rows (they have a resolution only)
    bool named;               // T235: the screen's EDID names this row (the mark beside the rate)
};
#define PIC_ROWS_MAX 9        // T235: the rows of the mode table that exist (hdmi_modes.vh: 3 at 50 Hz, 6 at 60 Hz)
struct PictureModes {
    int n;                    // rows, Auto included
    int cur;                  // the row of the choice in force (0: Auto)
    bool fallback;            // a listed row is of the other 50/60 Hz family than the machine's (the screen names none in the machine's)
    bool sys60;               // the machine's family is 60 Hz (for the wording of the fallback line)
    bool no_edid;             // the screen named nothing: the television rows are listed
    bool full;                // T235: every row of the table at its own rate is listed, the rows the screen names carry a mark (a core on the gateware path)
    PictureModeRow row[1 + PIC_ROWS_MAX];
};

// Refresh of an EDID row in tenths of Hz (pixel clock over the raster), 0 when the row is unknown.
uint32_t picmodes_row_dhz(int row);
// Fills row i: `hdmi` the resolution it asks for (-1: unknown), `msys` the machine's family, `now_hdmi` the resolution in force (-1: none), `cur_auto` the choice is Auto.
void picmodes_set_row(PictureModes *pm, int i, const char *name, int hdmi, uint16_t mask, uint16_t native, int msys, int now_hdmi, bool cur_auto);

// T235: row i of the full list: `row` the table row it asks for (-1: unknown), `named` the screen names it, `now_row` the row in force (-1: none). Sets pm->full.
void picmodes_set_row_at(PictureModes *pm, int i, const char *name, int row, bool named, uint16_t native, int now_row, bool cur_auto);
// "1280 x 720, 50.0 Hz" for the resolution as the screen's list asks for it in family `sys` (the name alone when the raster is unknown, "the previous mode" for none).
void picmodes_mode_text(uint16_t mask, int sys, int hdmi, char *out, int n);

// Text of a row's columns: "50.0 Hz" (empty when unknown) and the tag at its end ("now", "native", "now  native", empty).
void picmodes_refresh_text(const PictureModes *pm, int i, char *out, int n);
void picmodes_tag_text(const PictureModes *pm, int i, char *out, int n);
// The one line above the rows.
void picmodes_info(const PictureModes *pm, char *out, int n);
// The confirm question's body: the mode that came up, the seconds, the mode that comes back, the keys.
void picmodes_confirm(int left_s, const char *now, const char *prev, char *out, int n);
// The keys line of the question (the key bar under the question shows the same, not the page's keys).
const char *picmodes_confirm_keys(void);
// The chip text after the question ended: kept, or the mode that came back.
void picmodes_kept_text(const char *name, char *out, int n);
void picmodes_back_text(const char *prev, char *out, int n);

// The cursor and keys. The application maps its keys to PMK_*.
enum { PMK_UP, PMK_DOWN, PMK_HOME, PMK_END, PMK_OK, PMK_BACK, PMK_OTHER };
enum PmAct { PMA_NONE = 0, PMA_REDRAW, PMA_APPLY, PMA_CLOSE };
// The cursor rule on its own (the core's menu keeps its cursor in the pick stage it shares with other pages).
PmAct picmodes_key(int *sel, int n, int cur, int pmk);
struct PicPage {
    PictureModes pm;
    int sel;
};
void picpage_open(PicPage *p, const PictureModes *pm);                 // the cursor on the choice in force
void picpage_refresh(PicPage *p, const PictureModes *pm, bool keep);   // after a change or a hot-plug; keep: the cursor keeps its mode by name, else the one in force
// PMA_APPLY: apply row p->sel. OK on the choice in force closes (nothing to apply); BACK closes; browsing applies nothing.
PmAct picpage_key(PicPage *p, int pmk);

#endif
