// LVGL on the TRX64 Ultimate pixel layer (TRX64-Ultimate docs/specs/T24-lvgl-ui.md;
// hardware: ARCH-lean-state-access.md §7.5, §15.3 page 6, pix_layer.v).
//
// One display, 960 x 540 RGB565 at scale 2 (the whole 1920 x 1080 output), two
// full frame buffers in the app's heap, LVGL in direct mode: a flush hands the
// finished buffer to PX_BACK and waits for the flip-done interrupt (IRQ_CAUSE b7).
//
// Threading: LVGL is called from one task only (the UI task, which calls
// trx_lv_open/trx_lv_handler/trx_lv_close). The layer has one owner at a time;
// trx_lv_yield_layer() takes it away from LVGL for anyone else (the T12 freeze
// renderer), from any task.
//
// Only the U64-II application builds this. Without the "TRX2" IO block, or with
// a bitstream whose layer is not RGB565 (CAPS b6, b7), trx_lv_available() is
// false and nothing here touches the hardware.
#ifndef TRX_LV_PORT_H
#define TRX_LV_PORT_H

#include <stdint.h>

// The layout resolution is a build choice (-DUI_RES=n, host-app/target/u64ii/riscv/hostapp/Makefile; T101 5.x):
//   0  960 x 540 at scale 2: the whole 1920 x 1080 output (today's default)
//   1  960 x 540 at scale 1: every UI pixel is one output pixel, the picture centred (a quarter of the screen)
// pix_layer.v takes W <= 960 (its line buffer is 2 x 1024 pixels), so a canvas wider than 960 does not exist at scale 1.
#ifndef UI_RES
#define UI_RES 0
#endif
#define TRX_LV_W 960
#define TRX_LV_H 540
#if UI_RES == 0
#define TRX_LV_SCALE 2
#elif UI_RES == 1
#define TRX_LV_SCALE 1
#else
#error "UI_RES: 0 = 960x540 at scale 2, 1 = 960x540 at scale 1"
#endif
// the top left of the layer in the 1920 x 1080 output (PX_POS): centred
#define TRX_LV_X0 ((1920 - TRX_LV_W * TRX_LV_SCALE) / 2)
#define TRX_LV_Y0 ((1080 - TRX_LV_H * TRX_LV_SCALE) / 2)

// True on a bitstream with the RGB565 pixel layer.
bool trx_lv_available(void);

// UI task: lv_init, the tick, the two buffers and the display. The layer stays
// off until the first flush; it is then shown opaque (global alpha 255, key
// off). False when the layer is not there or the buffers cannot be had
// (everything undone).
bool trx_lv_open(void);

// UI task, after trx_lv_open() and before the first trx_lv_handler(): paint()
// fills a frame buffer (TRX_LV_W x TRX_LV_H RGB565, stride TRX_LV_W pixels) and
// the result goes into both buffers; the display's bottom layer turns
// transparent. RGB565 has no alpha, so LVGL never clears the buffers: wherever
// the active screen is transparent (bg_opa 0), its objects are drawn over these
// pixels.
void trx_lv_backdrop(void (*paint)(uint16_t *fb));

// UI task: one lv_timer_handler() under the port lock. Returns the ms until
// LVGL wants to run again (capped), or -1 when the layer was taken away
// (trx_lv_yield_layer): the caller then shuts the UI down with trx_lv_close().
int trx_lv_handler(void);

// T146: the two frame buffers (both NULL while the port is not open). The one LVGL does not render into is free for the
// caller (a file read, the encoder) until LVGL needs it.
void trx_lv_buffers(uint16_t **a, uint16_t **b);

// T146, UI task, after trx_lv_open() and before the first flush: fb (one of the two buffers, filled by the caller) is the
// first picture, the layer on, opaque. LVGL then has to render into the other buffer (lv_display_set_buffers); its first
// flush flips to it. False when the layer is not ours or already on.
bool trx_lv_show_picture(const uint16_t *fb);

// UI task: renders what is pending at once (lv_refr_now), instead of waiting for
// LVGL's next refresh tick. For the moment after a key, where the delay is the
// one the user feels. Does nothing when the layer was taken away or suspended.
void trx_lv_refresh_now(void);

// UI task: global alpha of the layer (255 opaque .. 0 invisible), written now when the layer is up, else with the first picture.
// The layer going off (close, yield, suspend) sets it back to 255.
// False when the layer was taken away.
bool trx_lv_set_alpha(uint8_t alpha);

// UI task: the layer's colour key. While `on`, pixels of exactly this RGB565 colour are transparent and show what is under the layer (the core's
// own picture). Off is the default; the key is written now when the layer is up, else with the first picture.
void trx_lv_set_key(bool on, uint16_t rgb565);

// T107 5.9: where the menu goes in the output, x/y of its top left and w/h of its size in output pixels (a whole multiple of the canvas,
// trx_canvas_for). Kept; written with the first picture of every menu opening (layer_up) and at once to a menu that is up (the layer goes
// off for it and back on). Until it is called the layer is TRX_LV_SCALE times its source, centred in 1920 x 1080 (TRX_LV_X0 / TRX_LV_Y0).
// T228: not every bitstream's pix_layer has PX_OSIZE. The MultiCore host's sizes by it and does not read PX_CTRL b5:4; TRX64-Ultimate's
// (trx64u.cor) has none and scales by b5:4. So the size goes to PX_OSIZE and its whole factor over the canvas (trx_canvas_factor) to b5:4.
void trx_lv_set_place(int x, int y, int w, int h);

// T164 D10a: the canvas LVGL draws on, w x h pixels (at most TRX_LV_W wide and TRX_LV_W x TRX_LV_H pixels in all: 800 x 600 fits). 960 x 540 until it is called. It is the mode's own size
// below 960 wide (app/video/trxcanvas.h). The layer is off until the next flush, which draws the whole screen; the caller builds its views again.
void trx_lv_set_canvas(int w, int h);

// UI task: layer off (if still ours), display deleted, lv_deinit, buffers freed.
void trx_lv_close(void);

// Any task: the layer is no longer LVGL's. Switches it off if LVGL had it on and
// returns once the UI task cannot touch the layer's registers any more. Cheap
// and immediate when LVGL is not running.
void trx_lv_yield_layer(void);

// T93: the layer is the core's for a while / LVGL's again. suspend() = layer off, LVGL idles (trx_lv_handler returns without
// rendering); resume() = the next trx_lv_handler() redraws the whole screen and switches the layer on. Both from the UI task.
void trx_lv_suspend(void);
void trx_lv_resume(void);

#endif
