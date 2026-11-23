// LVGL on the TRX64 Ultimate pixel layer. See trx_lv_port.h.
//
// Registers (pix_layer.v; 32-bit, byte 3 commits, trx_io.h):
//   0x101A6080 PX_CTRL   b0 enable, b3:1 format (1 RGB565), b5:4 scale S
//   0x101A6084 PX_FRONT  the shown buffer;  6088 PX_BACK: shown from the next frame start
//   6090 PX_SIZE {h, w}, 6094 PX_STRIDE, 6098 PX_POS {y0, x0}, 609C PX_GALPHA, 60A0 PX_KEY, 60A8 PX_OSIZE {h, w}
// Two pix_layers take them (T228). The MultiCore host's (host/pix_layer.v, and its copies in the large cores) sizes the picture by PX_OSIZE and does
// not read b5:4. TRX64-Ultimate's (designs/c64_basic/pix_layer.v, the bitstream of trx64u.cor) has no PX_OSIZE: it shows each source pixel as S x S
// output pixels (b5:4, 0 counting as 1). So the port writes both, the menu's output size and its whole factor over the canvas (px_ctrl).

#include "trx_lv_port.h"
#include "trx_io.h"
#include "trxcanvas.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PX_CTRL        (TRX_IO_BASE + 0x6080)
#define PX_FRONT       (TRX_IO_BASE + 0x6084)
#define PX_BACK        (TRX_IO_BASE + 0x6088)
#define PX_SIZE        (TRX_IO_BASE + 0x6090)
#define PX_STRIDE      (TRX_IO_BASE + 0x6094)
#define PX_POS         (TRX_IO_BASE + 0x6098)
#define PX_GALPHA      (TRX_IO_BASE + 0x609C)
#define PX_KEY         (TRX_IO_BASE + 0x60A0)
#define PX_OSIZE       (TRX_IO_BASE + 0x60A8)
#define PX_CTRL_FMT    0x02 // RGB565, nearest sampling (b6 smoothing off)

#define BUF_BYTES      (TRX_LV_W * 2 * TRX_LV_H)   // the buffers hold the largest canvas (960 x 540); every other canvas has fewer pixels (T164 D10a)
#define FLIP_TIMEOUT   pdMS_TO_TICKS(100)
#define MAX_SLEEP_MS   50

static SemaphoreHandle_t lock;      // the layer's registers and the LVGL state below
static SemaphoreHandle_t flip_sem;
static bool irq_attached;
static lv_display_t *disp;
static void *raw[2];                 // malloc results
static uint16_t *buf[2];             // 16-byte aligned
static bool layer_on;                // LVGL switched the layer on
static volatile bool taken;          // trx_lv_yield_layer() since trx_lv_open()
static volatile bool suspended;      // T93 D9: trx_lv_suspend(): the layer belongs to a core's picture for now
static int cv_w = TRX_LV_W, cv_h = TRX_LV_H;   // T164 D10a: the canvas LVGL draws (trx_lv_set_canvas); the stride of the buffers is its width
static bool place_set;               // T107 5.9: trx_lv_set_place() was called; the menu has its own position and output size
static uint32_t place_pos, place_osize;
static uint8_t alpha_reg = 255;      // PX_GALPHA as last asked for; written with the first picture and when it changes; 255 whenever the layer was given up
static uint32_t key_reg;             // PX_KEY as last asked for (b16 on, b15:0 the RGB565 colour); written with the first picture and when it changes

bool trx_lv_available(void)
{
    uint8_t need = TRX_CAPS_VIDEO | TRX_CAPS_PX565;
    if ((trx_io_caps() & need) != need) {
        return false;
    }
    // Created here, in the init path, before any task can yield the layer.
    if (!lock) {
        lock = xSemaphoreCreateMutex();
        flip_sem = xSemaphoreCreateBinary();
    }
    return lock && flip_sem;
}

static uint32_t tick_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static uint8_t flip_irq(uint8_t act)
{
    BaseType_t woken = pdFALSE;
    ioWrite8(TRX_IRQ_CAUSE, act);
    xSemaphoreGiveFromISR(flip_sem, &woken);
    return woken == pdTRUE;
}

// The rvlite core has no data cache, but its stores are posted: a load from the
// last address written waits for them before the register write that follows.
static inline void stores_done(const uint16_t *last)
{
    __asm__ volatile ("fence" ::: "memory");
    (void)*(volatile const uint16_t *)last;
}

// PX_CTRL, on or off: b5:4 is the whole factor of the menu's output size over its canvas (trx_canvas_factor; TRX_LV_SCALE until trx_lv_set_place),
// the scale of TRX64-Ultimate's layer; the MultiCore layer does not read it and takes PX_OSIZE. With the lock held.
static uint32_t px_ctrl(bool on)
{
    int ow = place_set ? int(place_osize & 0xFFFF) : cv_w * TRX_LV_SCALE;
    int oh = place_set ? int(place_osize >> 16) : cv_h * TRX_LV_SCALE;
    return (uint32_t(trx_canvas_factor(ow, oh, cv_w, cv_h)) << 4) | PX_CTRL_FMT | (on ? 1u : 0u);
}

// First picture: shown opaque over everything, the whole output. With the lock held.
static void layer_up(const uint16_t *fb)
{
    trx_io_write32(PX_CTRL, px_ctrl(false));
    trx_io_write32(PX_SIZE, (uint32_t(cv_h) << 16) | (uint32_t)cv_w);
    trx_io_write32(PX_STRIDE, cv_w * 2);
    if (place_set) {
        trx_io_write32(PX_POS, place_pos);
        trx_io_write32(PX_OSIZE, place_osize);
    } else {
        trx_io_write32(PX_POS, (uint32_t((1080 - cv_h * TRX_LV_SCALE) / 2) << 16) | (uint32_t)((1920 - cv_w * TRX_LV_SCALE) / 2));
        trx_io_write32(PX_OSIZE, (uint32_t(cv_h * TRX_LV_SCALE) << 16) | (uint32_t)(cv_w * TRX_LV_SCALE));   // the MultiCore pix_layer scales to this size
    }
    trx_io_write32(PX_GALPHA, alpha_reg);
    trx_io_write32(PX_KEY, key_reg);
    trx_io_write32(PX_FRONT, (uint32_t)fb);
    trx_io_write32(PX_CTRL, px_ctrl(true));
    layer_on = true;
}

// Called by LVGL from trx_lv_handler(), so with the lock held.
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t *fb = (uint16_t *)px_map;
    stores_done(fb + area->y2 * cv_w + area->x2);
    if (taken || !lv_display_flush_is_last(d)) {
        lv_display_flush_ready(d);
        return;
    }
    if (!layer_on) {
        layer_up(fb);
    } else {
        xSemaphoreTake(flip_sem, 0); // drop a stale give
        ioWrite8(TRX_IRQ_CAUSE, TRX_IRQ_FLIP);
        trx_io_write32(PX_BACK, (uint32_t)fb);
        if (xSemaphoreTake(flip_sem, FLIP_TIMEOUT) != pdTRUE) {
            printf("[TRX UI] pixel layer: no flip\n");
        }
    }
    lv_display_flush_ready(d);
}

static void free_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        free(raw[i]);
        raw[i] = NULL;
        buf[i] = NULL;
    }
}

bool trx_lv_open(void)
{
    if (!trx_lv_available() || disp) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        raw[i] = malloc(BUF_BYTES + 15);
        if (!raw[i]) {
            printf("[TRX UI] pixel layer: no memory for frame buffer %d (%d bytes)\n", i, BUF_BYTES);
            free_buffers();
            return false;
        }
        buf[i] = (uint16_t *)(((uint32_t)raw[i] + 15) & ~15UL);
    }
    if (!irq_attached) {
        trx_irq_attach(TRX_IRQ_FLIP, flip_irq);
        irq_attached = true;
    }
    trx_irq_enable(TRX_IRQ_FLIP, true);

    xSemaphoreTake(lock, portMAX_DELAY);
    taken = false;
    suspended = false;
    layer_on = false;
    lv_init();
    lv_tick_set_cb(tick_ms);
    disp = lv_display_create(cv_w, cv_h);
    if (disp) {
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(disp, buf[0], buf[1], BUF_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(disp, flush_cb);
    } else {
        lv_deinit();
    }
    xSemaphoreGive(lock);
    if (!disp) {
        free_buffers();
        return false;
    }
    printf("[TRX UI] LVGL %d.%d.%d on the pixel layer: buffers at %p, %p, heap free %u\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, buf[0], buf[1],
           (unsigned)xPortGetFreeHeapSize());
    return true;
}

void trx_lv_backdrop(void (*paint)(uint16_t *fb))
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (disp) {
        paint(buf[0]);
        memcpy(buf[1], buf[0], BUF_BYTES);
        lv_obj_set_style_bg_opa(lv_display_get_layer_bottom(disp), LV_OPA_TRANSP, 0);
    }
    xSemaphoreGive(lock);
}

void trx_lv_buffers(uint16_t **a, uint16_t **b)
{
    *a = buf[0];
    *b = buf[1];
}

bool trx_lv_show_picture(const uint16_t *fb)
{
    bool ok = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (disp && !taken && !layer_on && (fb == buf[0] || fb == buf[1])) {
        stores_done(fb + cv_w * cv_h - 1);
        layer_up(fb);
        ok = true;
    }
    xSemaphoreGive(lock);
    return ok;
}

int trx_lv_handler(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t ms = taken ? 0 : (suspended ? MAX_SLEEP_MS : lv_timer_handler());
    bool gone = taken;
    xSemaphoreGive(lock);
    if (gone) {
        return -1;
    }
    return (ms > MAX_SLEEP_MS) ? MAX_SLEEP_MS : int(ms);
}

void trx_lv_refresh_now(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!taken && !suspended) {
        lv_refr_now(NULL);
    }
    xSemaphoreGive(lock);
}

bool trx_lv_set_alpha(uint8_t alpha)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = !taken;
    if (ok) {
        alpha_reg = alpha;
        if (layer_on) {
            trx_io_write32(PX_GALPHA, alpha);
        }
    }
    xSemaphoreGive(lock);
    return ok;
}

// The colour key (pix_layer.v PX_KEY: b16 on, b15:0 the RGB565 colour whose pixels are transparent). The layer's registers are write only, so
// the value is kept here; it takes effect at once when the layer is up, else with the first picture.
void trx_lv_set_key(bool on, uint16_t rgb565)
{
    uint32_t v = on ? (0x10000u | rgb565) : 0;
    if (!lock) {
        key_reg = v;
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (v != key_reg) {
        key_reg = v;
        if (layer_on && !taken && !suspended) {
            trx_io_write32(PX_KEY, key_reg);
        }
    }
    xSemaphoreGive(lock);
}

void trx_lv_close(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (layer_on && !taken) {
        trx_io_write32(PX_CTRL, px_ctrl(false));
        trx_io_write32(PX_GALPHA, 255);
    }
    alpha_reg = 255;
    layer_on = false;
    if (disp) {
        lv_display_delete(disp);
        disp = NULL;
        lv_deinit();
    }
    xSemaphoreGive(lock);
    free_buffers();
}

void trx_lv_yield_layer(void)
{
    if (!lock) {
        return; // LVGL never ran: nothing of ours on the layer
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (disp && !taken) {
        if (layer_on) {
            trx_io_write32(PX_CTRL, px_ctrl(false));
            trx_io_write32(PX_GALPHA, 255);
            layer_on = false;
        }
        alpha_reg = 255;
        taken = true;
        printf("[TRX UI] pixel layer handed over\n");
    }
    xSemaphoreGive(lock);
}

// T93 D9 (Freeze menu on the one pixel layer): a core application shows its core's picture on the same layer. suspend()
// switches the layer off and keeps LVGL, its views and its buffers; the caller writes the core's layer registers. resume()
// is the other way: the next render writes the registers of the menu again (as for a first picture) and the whole screen
// is redrawn. The layer registers are write-only, so nothing here reads them back (T93 R2).
void trx_lv_suspend(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (disp && !taken && !suspended) {
        if (layer_on) {
            trx_io_write32(PX_CTRL, px_ctrl(false));
            trx_io_write32(PX_GALPHA, 255);
            layer_on = false;
        }
        alpha_reg = 255;
        suspended = true;
    }
    xSemaphoreGive(lock);
}

void trx_lv_resume(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (disp && !taken && suspended) {
        suspended = false;
        layer_on = false;
        lv_obj_invalidate(lv_screen_active());
    }
    xSemaphoreGive(lock);
}

// T164 D10a: the canvas of the menu (960 x 540, or the mode's own size below 960 wide). The display is laid out for it again and the buffers get its
// stride; the layer goes off and the next flush (the whole screen is drawn again) writes size, stride and the first picture as for a new opening.
// Before trx_lv_open() it only sets the size the display is created with. The caller builds its views again and sets its buffer mode (one buffer).
void trx_lv_set_canvas(int w, int h)
{
    if (w <= 0 || h <= 0 || w > TRX_LV_W || w * h > TRX_LV_W * TRX_LV_H) {          // the layer's line buffer holds 960 pixels; the frame buffers 960 x 540
        return;
    }
    if (!lock) {
        cv_w = w;
        cv_h = h;
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (w != cv_w || h != cv_h) {
        cv_w = w;
        cv_h = h;
        if (layer_on && !taken && !suspended) {
            trx_io_write32(PX_CTRL, px_ctrl(false));
        }
        layer_on = false;
        if (disp) {
            lv_display_set_resolution(disp, w, h);
            lv_display_set_buffers(disp, buf[0], buf[1], BUF_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
        }
    }
    xSemaphoreGive(lock);
}

// T107 5.9: the menu's place in the output. The registers are written with the layer off when it is up (a layer whose geometry changes under a
// shown frame tears), and not at all while the layer belongs to a core's picture (suspended): the next opening writes them in layer_up().
void trx_lv_set_place(int x, int y, int w, int h)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t pos = ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF);
    uint32_t osize = ((uint32_t)(h & 0xFFFF) << 16) | (uint32_t)(w & 0xFFFF);
    bool same = place_set && pos == place_pos && osize == place_osize;
    place_set = true;
    place_pos = pos;
    place_osize = osize;
    if (!same && layer_on && !taken && !suspended) {
        trx_io_write32(PX_CTRL, px_ctrl(false));
        trx_io_write32(PX_POS, place_pos);
        trx_io_write32(PX_OSIZE, place_osize);
        trx_io_write32(PX_CTRL, px_ctrl(true));
    }
    xSemaphoreGive(lock);
}
