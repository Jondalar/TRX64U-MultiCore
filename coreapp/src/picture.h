// The "Picture" setting every core application has (T107, ADR-0057): the person chooses the HDMI mode, and the
// picture's scale and position follow. It is added to the core's own settings when the application starts, so no
// descriptor declares it and a core added tomorrow has it with no line of its own.
#ifndef COREAPP_PICTURE_H
#define COREAPP_PICTURE_H

#include "desc.h"
#include "ctx.h"
#include "edid.h"
#include "picmodes.h"

// The operation behind the Picture item (the base contributor registers it, picture_register).
#define PICTURE_OP "picture.apply"
#define SCALE_OP   "scale.apply"           // T107 D8
#define SMOOTH_OP  "smooth.apply"          // T164 Q3

// T164 G11-G14: what the application knows about the screen and the gateware when it builds the items.
struct PictureEnv {
    uint16_t mask;            // the rows the screen takes (video_modes_offered(), EDID_ROW_MASK when nobody asked)
    uint16_t native;          // T187 D5: the rows its short video descriptors flag as native (0: none known)
    bool gateware;            // the host's status register answered (video_gateware_present()): Scale and Smoothing, the exact mode, the 1 s status wait
    int sys;                  // the machine's 50/60 Hz family (VID_SYS_*), -1: what video_current says
    bool vic_scale;           // T222: the gateware scales the VIC's picture with a scale register (video_gateware_vic_scale()): a core with no frame buffer gets a Scale item
};

// out = *d with the Picture item after d's own settings; for every frame-buffer core on a host whose gateware scales (env->gateware), Scale and
// Smoothing follow it; a core whose picture is the VIC (no frame buffer) on a gateware with the VIC scaler (env->vic_scale, T222) gets Scale alone (0x4F01). `store` holds the merged array (SETTINGS_MAX items) and has to outlive `out`. Returns null, or the reason when the core has no
// room for the items or declares a key itself. The menu, the boot sequence and the settings read the merged copy (CoreCtx::desc points at it), never
// the table's original. The Picture list is Auto and the resolutions of video_modes_hdmi_list() for the mask and the machine's family (D11a, Q6);
// env null: every row, no gateware.
const char *picture_extend(const CoreDesc *d, CoreDesc *out, Setting *store, const PictureEnv *env = 0);
// The board's answer, for run.cc: reads the EDID and probes the gateware.
void picture_env_read(PictureEnv *env, int sys);

// T187 D5: what the mode list of the Picture page shows (PictureModes, picmodes.h; the host fills the same struct): row 0 Auto, row n the n-th resolution
// of the Picture list (the order of the item's values).
void picture_modes(CoreCtx *c, PictureModes *out);

// Index of the Picture item in c->desc->settings, -1 when the descriptor is not an extended one.
int picture_index(const CoreDesc *d);

// The item's apply (WHEN_LIVE). value 0 is Auto: the mode the host is set to (host.cfg [Video] Mode, read at picture_start; D10c) when the screen takes it,
// else the mode the host handed over, else the best the screen offers (D11c; without the gateware the core's own wish); value n is the n-th resolution of the list. Sets the mode when it differs from the one the picture is in, then lets the scale
// and the position follow, and starts the confirm-or-revert wait of Q7 (picture_service).
void picture_apply(CoreCtx *c, int value);

// Index of the Scale / Smoothing items (a frame-buffer core on a gateware host has them), -1 otherwise; their applies.
int scale_index(const CoreDesc *d);
int smooth_index(const CoreDesc *d);
void scale_apply(CoreCtx *c, int value);
void smooth_apply(CoreCtx *c, int value);

// Start-up: the application is in `hdmi_now` (what the host handed over, -1: nothing) in system `sys` (-1: nothing).
// Takes the mode the stored choice says (Auto when there is none), through the same path as a change at run time.
// Returns the mode the picture is in.
int picture_start(CoreCtx *c, int sys, int hdmi_now);

// T140 D24: the System Mode (0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50, the order of t_video_mode) of a core whose clock follows it: the HDMI mode stays, the
// host's PLL and scan registers go to `sys` through the same call a change of the Picture makes, video_current and the Video follow. The caller writes the
// core's own format bits (CoreIo::video_format) after it. No change, no call.
void picture_set_system(CoreCtx *c, int sys);

void picture_register(OpRegistry &r);       // Picture, Scale and Smoothing

// T164 G12, Q7: called on every pass of the application's loop, menu open or not. Every ~500 ms it looks at the hot-plug line and the screen's
// signature; a screen it has not seen is read again, the list is rebuilt, and when the running row is not in the new mask the best row is applied
// once. Every mode change (menu, REST, web) is held for PICTURE_CONFIRM_MS: picture_confirm_key() keeps it, the time running out puts the
// previous mode and the previous Picture value back. Returns PS_* bits.
#define PICTURE_POLL_MS    500
#define PICTURE_CONFIRM_MS 8000
#define PS_LIST_CHANGED 1       // the Picture list or the mode changed: the menu is built again
#define PS_CONFIRM_ASK  2       // a confirm wait began: show the question
#define PS_CONFIRM_END  4       // it ended (kept or reverted): take the question away
int picture_service(CoreCtx *c);
bool picture_confirm_pending();
int picture_confirm_left_s();   // whole seconds left
void picture_confirm_key();     // any key keeps the mode
void picture_confirm_names(char *now, int nn, char *prev, int pn);   // T187 D5: "1280 x 720, 50.0 Hz" (the mode that came up) and the mode that comes back
const char *picture_take_result(void);   // T187 D4: the chip text once the question ended (kept / back to ...), null when there is none; read once
void picture_confirm_revert();  // the previous mode comes back now (the answer "no" of the service)


#endif
