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

#define TRX_LV_W 960
#define TRX_LV_H 540

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

// UI task: global alpha of the layer (255 opaque .. 0 invisible), written now.
// False when the layer was taken away.
bool trx_lv_set_alpha(uint8_t alpha);

// UI task: layer off (if still ours), display deleted, lv_deinit, buffers freed.
void trx_lv_close(void);

// Any task: the layer is no longer LVGL's. Switches it off if LVGL had it on and
// returns once the UI task cannot touch the layer's registers any more. Cheap
// and immediate when LVGL is not running.
void trx_lv_yield_layer(void);

#endif
