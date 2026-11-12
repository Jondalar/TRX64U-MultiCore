// LVGL on the TRX64 Ultimate pixel layer. See trx_lv_port.h.
//
// Registers (pix_layer.v; 32-bit, byte 3 commits, trx_io.h):
//   0x101A6080 PX_CTRL   b0 enable, b3:1 format (1 RGB565), b5:4 scale
//   0x101A6084 PX_FRONT  the shown buffer;  6088 PX_BACK: shown from the next frame start
//   6090 PX_SIZE {h, w}, 6094 PX_STRIDE, 6098 PX_POS {y0, x0}, 609C PX_GALPHA, 60A0 PX_KEY

#include "trx_lv_port.h"
#include "trx_io.h"
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
#define PX_CTRL_OFF    0x22 // RGB565, scale 2, disabled
#define PX_CTRL_ON     0x23

#define STRIDE         (TRX_LV_W * 2)
#define BUF_BYTES      (STRIDE * TRX_LV_H)
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

// Called by LVGL from trx_lv_handler(), so with the lock held.
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t *fb = (uint16_t *)px_map;
    stores_done(fb + area->y2 * TRX_LV_W + area->x2);
    if (taken || !lv_display_flush_is_last(d)) {
        lv_display_flush_ready(d);
        return;
    }
    if (!layer_on) {
        // First picture: shown opaque over everything, the whole output.
        trx_io_write32(PX_CTRL, PX_CTRL_OFF);
        trx_io_write32(PX_SIZE, (uint32_t(TRX_LV_H) << 16) | TRX_LV_W);
        trx_io_write32(PX_STRIDE, STRIDE);
        trx_io_write32(PX_POS, 0);
        trx_io_write32(PX_GALPHA, 255);
        trx_io_write32(PX_KEY, 0);
        trx_io_write32(PX_FRONT, (uint32_t)fb);
        trx_io_write32(PX_CTRL, PX_CTRL_ON);
        layer_on = true;
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
    layer_on = false;
    lv_init();
    lv_tick_set_cb(tick_ms);
    disp = lv_display_create(TRX_LV_W, TRX_LV_H);
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

int trx_lv_handler(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t ms = taken ? 0 : lv_timer_handler();
    bool gone = taken;
    xSemaphoreGive(lock);
    if (gone) {
        return -1;
    }
    return (ms > MAX_SLEEP_MS) ? MAX_SLEEP_MS : int(ms);
}

bool trx_lv_set_alpha(uint8_t alpha)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = !taken;
    if (ok && layer_on) {
        trx_io_write32(PX_GALPHA, alpha);
    }
    xSemaphoreGive(lock);
    return ok;
}

void trx_lv_close(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (layer_on && !taken) {
        trx_io_write32(PX_CTRL, PX_CTRL_OFF);
        trx_io_write32(PX_GALPHA, 255);
    }
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
            trx_io_write32(PX_CTRL, PX_CTRL_OFF);
            trx_io_write32(PX_GALPHA, 255);
            layer_on = false;
        }
        taken = true;
        printf("[TRX UI] pixel layer handed over\n");
    }
    xSemaphoreGive(lock);
}
