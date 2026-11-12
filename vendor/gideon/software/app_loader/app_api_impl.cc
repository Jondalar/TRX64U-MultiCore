/*
 * app_api_impl.cc
 *
 * Builds the tables of the app ABI (app_api.h) handed to loaded apps. The
 * functions are thin wrappers over FreeRTOS and firmware APIs; UI functions act
 * on the UserInterface that launched the app.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "app_api.h"
#include "app_api_net.h"
#include "c1541.h"
#include "c64_crt.h"
#include "filemanager.h"
#include "screen.h"
#include "tree_browser.h"
#include "ui_elements.h"
#include "userinterface.h"
#include "versions.h"
#include "gitinfo.h"
#include "flash.h"
#include "itu.h"
#include "acia.h"
#include "config.h"
#include "filetype_prg.h"
#include "pattern.h"
#include "subsys.h"
#include "overlay.h"
#include "trx_video.h"
#if U64 && !defined(RISCV)
#include "esp32.h"
#endif

/* Set by AppLoader::run_app() before spawning the app task; cleared after
 * the task joins. Apps calling UI functions while s_current_ui is NULL
 * get a no-op (printf fallback). */
static UserInterface *s_current_ui = NULL;
static Keyboard *s_ctrl_kb = NULL; /* the keyboard ui->key_ctrl_tap turned CTRL-alone reporting on for */

extern "C" void app_api_set_ui(UserInterface *ui) { s_current_ui = ui; }

/* Memory — tracked allocations                                        */

/* Every app malloc adds a node to s_alloc_head. Every app free removes
 * one. When the app task exits, the loader calls app_alloc_cleanup()
 * which walks the remaining list and frees leaks. This guards firmware
 * uptime against apps that forget to free.
 *
 * The mutex is only strictly needed if multiple app-side tasks ever
 * allocate concurrently. The loader runs a single app task, but the
 * cleanup call comes from the loader task — so the mutex is kept for
 * safety. */

typedef struct alloc_node_s
{
    struct alloc_node_s *next;
    void *ptr;
    size_t size; /* kept for debugging; also lets realloc copy correctly */
} alloc_node_t;

static alloc_node_t *s_alloc_head = NULL;
static SemaphoreHandle_t s_alloc_mtx = NULL;

static void alloc_lock()
{
    if (s_alloc_mtx)
        xSemaphoreTake(s_alloc_mtx, portMAX_DELAY);
}
static void alloc_unlock()
{
    if (s_alloc_mtx)
        xSemaphoreGive(s_alloc_mtx);
}

static void alloc_track(void *ptr, size_t size)
{
    if (!ptr)
        return;
    alloc_node_t *n = (alloc_node_t *)pvPortMalloc(sizeof(*n));
    if (!n) {
        /* Out of memory for the tracker node itself. Free the user block
         * too so we don't leak an un-tracked allocation. Caller gets NULL. */
        vPortFree(ptr);
        return;
    }
    n->ptr = ptr;
    n->size = size;
    alloc_lock();
    n->next = s_alloc_head;
    s_alloc_head = n;
    alloc_unlock();
}

static size_t alloc_untrack(void *ptr)
{
    if (!ptr)
        return 0;
    size_t size = 0;
    alloc_lock();
    alloc_node_t **prev = &s_alloc_head;
    for (alloc_node_t *n = s_alloc_head; n; prev = &n->next, n = n->next) {
        if (n->ptr == ptr) {
            *prev = n->next;
            size = n->size;
            alloc_unlock();
            vPortFree(n);
            return size;
        }
    }
    alloc_unlock();
    /* Not found — app is freeing a pointer we didn't hand out (or
     * double-free). Still free the caller's ptr through vPortFree;
     * at worst we get a heap assertion which is the correct behaviour. */
    printf("AppLoader: warning — free of untracked ptr %p\n", ptr);
    return 0;
}

extern "C" void app_alloc_cleanup(void)
{
    /* Walk the list, freeing every live app allocation. Called by the
     * loader after the app task has joined. */
    alloc_lock();
    alloc_node_t *n = s_alloc_head;
    s_alloc_head = NULL;
    alloc_unlock();

    int count = 0;
    size_t total = 0;
    while (n) {
        alloc_node_t *next = n->next;
        total += n->size;
        vPortFree(n->ptr);
        vPortFree(n);
        n = next;
        count++;
    }
    if (count) {
        printf("AppLoader: auto-freed %d leaked allocation(s), %u bytes\n", count, (unsigned int)total);
    }
}

extern "C" void app_alloc_init(void)
{
    /* Created once; reused across runs. */
    if (!s_alloc_mtx)
        s_alloc_mtx = xSemaphoreCreateMutex();
}

static void *api_malloc(size_t size)
{
    void *p = pvPortMalloc(size);
    alloc_track(p, size);
    return p;
}

static void api_free(void *ptr)
{
    if (!ptr)
        return;
    alloc_untrack(ptr);
    vPortFree(ptr);
}

static void *api_realloc(void *ptr, size_t size)
{
    if (!ptr) {
        void *p = pvPortMalloc(size);
        alloc_track(p, size);
        return p;
    }
    if (size == 0) {
        alloc_untrack(ptr);
        vPortFree(ptr);
        return NULL;
    }
    size_t old_size = 0;
    /* Peek old size under lock (don't untrack yet — we want the old
     * pointer usable during copy even if untrack/pvPortFree races). */
    alloc_lock();
    for (alloc_node_t *n = s_alloc_head; n; n = n->next) {
        if (n->ptr == ptr) {
            old_size = n->size;
            break;
        }
    }
    alloc_unlock();

    void *newp = pvPortMalloc(size);
    if (!newp)
        return NULL;
    size_t copy = (old_size && old_size < size) ? old_size : size;
    memcpy(newp, ptr, copy);
    alloc_untrack(ptr);
    vPortFree(ptr);
    alloc_track(newp, size);
    return newp;
}

/* TTY                                                                 */

/* Firmware provides printf / vsprintf via software/system/small_printf.cc.
 * It does NOT provide vprintf, so buffer the output ourselves then hand it
 * to printf. Keeps app output flowing through the same sink (UART/syslog)
 * as the loader's own diagnostics. */
extern "C" int printf(const char *fmt, ...);
extern "C" int vsprintf(char *dest, const char *fmt, va_list ap);

static int api_tty_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    printf("%s", buf);
    return r;
}

static int api_tty_getc(void)
{
    if (!s_current_ui || !s_current_ui->keyboard)
        return -1;
    for (;;) {
        int c = s_current_ui->keyboard->getch();
        if (c >= 0) {
            /* Firmware's own UIObject::poll does this; apps without
             * keymapper translation would see F-key scancodes instead
             * of the PageUp / Help / Tasks mappings. */
            return s_current_ui->keymapper(c, e_keymap_default);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Non-blocking variant of tty_getc. The keyboard reports -2 when its host
 * is gone; without a UI the app has no host either. */
static int api_ui_getch(void)
{
    if (!s_current_ui || !s_current_ui->keyboard) {
        return -2;
    }
    int c = s_current_ui->keyboard->getch();
    if (c == -2) {
        return -2;
    }
    if (c < 0) {
        return -1;
    }
    return s_current_ui->keymapper(c, e_keymap_default);
}

/* UI dialogs                                                          */

static void api_ui_popup(const char *title, const char *msg)
{
    if (s_current_ui) {
        /* UI popup ignores title; prepend it into the message for now. */
        if (title && *title) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s\n%s", title, msg ? msg : "");
            s_current_ui->popup(buf, BUTTON_OK);
        } else {
            s_current_ui->popup(msg ? msg : "", BUTTON_OK);
        }
    } else {
        printf("[APP POPUP] %s: %s\n", title ? title : "", msg ? msg : "");
    }
}

static int api_ui_confirm(const char *msg)
{
    if (s_current_ui) {
        int r = s_current_ui->popup(msg ? msg : "", BUTTON_YES | BUTTON_NO);
        return (r == BUTTON_YES) ? 1 : 0;
    }
    return 0;
}

static int api_ui_popup_buttons(const char *msg, uint8_t buttons)
{
    if (s_current_ui) {
        return s_current_ui->popup(msg ? msg : "", buttons);
    }
    return 0;
}

static int api_ui_color(int which)
{
    if (!s_current_ui) {
        return -1;
    }
    switch (which) {
    case 0:
        return s_current_ui->color_fg;
    case 1:
        return s_current_ui->color_bg;
    case 2:
        return s_current_ui->color_sel;
    case 3:
        return s_current_ui->color_sel_bg;
    case 4:
        return s_current_ui->color_status;
    case 5:
        return s_current_ui->color_inactive;
    case 6:
        return s_current_ui->color_border;
    default:
        return -1;
    }
}

static int api_ui_menu(const char *title, const char **items, int n)
{
    if (s_current_ui && items && n > 0) {
        /* UserInterface::choice returns the 0-based index (already
         * -1-adjusted) on a valid selection, or MENU_CLOSE (-1) on
         * cancel. Pass through directly. */
        return s_current_ui->choice(title ? title : "", items, n);
    }
    return -1;
}

static int api_ui_menu_keys(const char *title, const char **items, int n, const char *keys)
{
    if (!s_current_ui || !items || !keys || n <= 0)
        return -1;
    /* UIPopup::poll returns (1 << i) where i is the selected button
     * index — NOT the character code. Decode by locating the set bit. */
    int r = s_current_ui->popup(title ? title : "", n, items, keys);
    if (r <= 0)
        return -1;
    for (int i = 0; i < n; i++) {
        if (r == (1 << i))
            return i;
    }
    return -1;
}

static void *api_ui_progress_open(const char *title, int total)
{
    if (s_current_ui) {
        s_current_ui->show_progress(title ? title : "", total);
        return (void *)1; /* opaque handle: anything non-null */
    }
    return NULL;
}

static void api_ui_progress_set(void *h, int value, const char *msg)
{
    if (h && s_current_ui) {
        s_current_ui->update_progress(msg, value);
    }
}

static void api_ui_progress_close(void *h)
{
    if (h && s_current_ui) {
        s_current_ui->hide_progress();
    }
}

/* Custom screen: app gets a Window carved out of the UI's Screen.
 * Handle returned to the app is the Window* itself. */

static void *api_ui_screen_open(const char *title, int w, int h)
{
    if (!s_current_ui || !s_current_ui->screen)
        return NULL;
    Screen *parent = s_current_ui->screen;
    parent->clear();

    /* Title at row 0, window body starting row 2. */
    if (title && *title) {
        /* Use raw parent screen for title so it stays above the window. */
        parent->move_cursor(0, 0);
        parent->output(title);
    }

    int sx = parent->get_size_x();
    int sy = parent->get_size_y();
    if (w <= 0 || w > sx)
        w = sx;
    if (h <= 0 || h > sy - 2)
        h = sy - 2;

    Window *win = new Window(parent, 0, 2, w, h);
    win->draw_border();
    return (void *)win;
}

/* Same window as the firmware cartridge tools use: no title, no border. */
// The freeze buffers hold $0400-$0FFF and the colour RAM while the menu uses
// the C64's screen for itself, so this is what a tool must read to see the
// machine rather than the menu.
static int api_c64_dma_read_frozen(uint16_t addr, void *dst, int len)
{
    C64 *machine = C64::getMachine();
    if (!machine || !dst || (len <= 0)) {
        return 0;
    }
    machine->dma_transfer_frozen(addr, (uint8_t *)dst, len, 0);
    return len;
}

// Which file a medium in the machine came from, so a tool can carry it along.
static int api_media_source(int role, char *buf, int len)
{
    if (!buf || (len <= 0)) {
        return 0;
    }
    buf[0] = 0;
    if (role == 0) {
        const char *source = C64_CRT::get_source();
        if (!source || !*source) {
            return 0;
        }
        strncpy(buf, source, len - 1);
        buf[len - 1] = 0;
        return (int)strlen(buf);
    }

    C1541 *drive = (role == 1) ? c1541_A : ((role == 2) ? c1541_B : NULL);
    if (!drive) {
        return 0;
    }
    mstring path, name;
    drive->get_last_mounted_file(path, name);
    if (!name.length()) {
        return 0;
    }
    mstring full(path);
    if (full.length() && (full.c_str()[full.length() - 1] != '/')) {
        full += "/";
    }
    full += name;
    strncpy(buf, full.c_str(), len - 1);
    buf[len - 1] = 0;
    return (int)strlen(buf);
}

static void *api_ui_screen_open_full(void)
{
    if (!s_current_ui || !s_current_ui->screen) {
        return NULL;
    }
    Screen *screen = s_current_ui->screen;
    Window *win = new Window(screen, 0, 2, 40, screen->get_size_y() - 2);
    win->clear();
    return (void *)win;
}

extern Overlay *overlay; /* ultimate.cc */

/* ui->screen_open_wide: 80 columns on the overlay. With the menu on the overlay (Overlay on HDMI) that is
 * its own screen; with the menu on the C64's screen (Freeze) the overlay is shown over the freeze's layers
 * (trx_video_wide_begin takes them away) and the window is on the overlay's screen, the keys stay the UI's
 * (T21 §8.11). s_wide_win is the window that holds the wide mode, s_wide_freeze that the layers are taken. */
static Window *s_wide_win = NULL;
static bool s_wide_freeze = false;

/* The wide mode ends (idempotent): the overlay is 40 x 25 again, and in Freeze the layers are back and
 * the overlay hidden as before. */
static void wide_end(void)
{
    if (overlay) {
        overlay->end_wide();
    }
    if (s_wide_freeze) {
        s_wide_freeze = false;
        if (overlay) {
            overlay->hide_in_freeze();
        }
        trx_video_wide_end();
    }
}

static void *api_ui_screen_open_wide(int rows)
{
    if (!s_current_ui || !s_current_ui->screen || !overlay) {
        return NULL;
    }
    bool freeze = (s_current_ui->screen != overlay->getScreen());
    if (rows < 12) {
        rows = 12;
    } else if (rows > 50) {
        rows = 50;
    }
    if (s_wide_win) {
        if (overlay->is_wide()) {
            return NULL; /* one at a time */
        }
        wide_end(); /* in Freeze: the layers it took */
        delete s_wide_win; /* the overlay was given back under it */
        s_wide_win = NULL;
    }
    if (freeze && !trx_video_wide_begin()) {
        return NULL; /* no freeze with the TRX layers up (Gideon's bitstream, no HDMI): the app uses 40 columns */
    }
    if (!overlay->begin_wide(80, rows)) {
        if (freeze) {
            trx_video_wide_end();
        }
        return NULL;
    }
    if (freeze) {
        s_wide_freeze = true;
        overlay->show_in_freeze();
    }
    Screen *screen = overlay->getScreen();
    s_wide_win = new Window(screen, 0, 0, 80, rows);
    s_wide_win->clear();
    return (void *)s_wide_win;
}

/* The wide mode ends: here when the app closes the window, in the loader when the app returned
 * (app_api_overlay_restore), and in Overlay::release_ownership whoever gives the overlay back. */
extern "C" void app_api_overlay_restore(void)
{
    if (s_ctrl_kb) { /* the app returned: CTRL alone is nobody's key any more */
        s_ctrl_kb->set_report_ctrl(false);
        s_ctrl_kb = NULL;
    }
    wide_end();
    if (s_wide_win) {
        delete s_wide_win; /* the app returned without screen_close */
        s_wide_win = NULL;
    }
}

static void api_ui_screen_puts(void *h, int x, int y, const char *s, int color)
{
    Window *win = (Window *)h;
    if (!win || !s)
        return;
    if (color >= 0)
        win->set_color(color);
    win->move_cursor(x, y);
    win->output(s);
}

/* Window control (thin wrappers around screen.h Window methods). */

static void api_ui_win_clear(void *h)
{
    Window *w = (Window *)h;
    if (w)
        w->clear();
}
static void api_ui_win_set_color(void *h, int c)
{
    Window *w = (Window *)h;
    if (w)
        w->set_color(c);
}
static void api_ui_win_set_background(void *h, int c)
{
    Window *w = (Window *)h;
    if (w)
        w->set_background(c);
}
static void api_ui_win_reverse_mode(void *h, int on)
{
    Window *w = (Window *)h;
    if (w)
        w->reverse_mode(on);
}
static void api_ui_win_move_cursor(void *h, int x, int y)
{
    Window *w = (Window *)h;
    if (w)
        w->move_cursor(x, y);
}
static void api_ui_win_putc(void *h, char c)
{
    Window *w = (Window *)h;
    if (w)
        w->output(c);
}
static void api_ui_win_puts(void *h, const char *s)
{
    Window *w = (Window *)h;
    if (w && s)
        w->output(s);
}
static void api_ui_win_repeat(void *h, char c, int n)
{
    Window *w = (Window *)h;
    if (w)
        w->repeat(c, n);
}
static void api_ui_win_set_char_at(void *h, int x, int y, char c)
{
    Window *w = (Window *)h;
    if (w)
        w->set_char(x, y, c);
}
static void api_ui_win_draw_border(void *h)
{
    Window *w = (Window *)h;
    if (w)
        w->draw_border();
}
static void api_ui_win_reset_border(void *h)
{
    Window *w = (Window *)h;
    if (w)
        w->reset_border();
}
static int api_ui_win_get_size_x(void *h)
{
    Window *w = (Window *)h;
    return w ? w->get_size_x() : 0;
}
static int api_ui_win_get_size_y(void *h)
{
    Window *w = (Window *)h;
    return w ? w->get_size_y() : 0;
}

/* ui->getch_monitor: getch through e_keymap_monitor, the keymap the firmware's own monitor reads with: the
 * navigation style does not turn w a s d into cursor keys. The function keys are the interface's (F1 PageUp
 * in the Ultimate interface, F3 in the Commodore one), as in getch. */
static int api_ui_getch_monitor(void)
{
    if (!s_current_ui || !s_current_ui->keyboard) {
        return -2;
    }
    int c = s_current_ui->keyboard->getch();
    if (c == -2) {
        return -2;
    }
    if (c < 0) {
        return -1;
    }
    return s_current_ui->keymapper(c, e_keymap_monitor);
}

/* ui->key_ctrl_tap: the C64 keyboard of the current UI reports CTRL alone as KEY_CTRL_TAP until the app returns
 * (app_api_overlay_restore turns it off) */
static void api_ui_key_ctrl_tap(int on)
{
    if (s_ctrl_kb && (!on || !s_current_ui || (s_ctrl_kb != s_current_ui->keyboard))) {
        s_ctrl_kb->set_report_ctrl(false);
        s_ctrl_kb = NULL;
    }
    if (on && s_current_ui && s_current_ui->keyboard) {
        s_ctrl_kb = s_current_ui->keyboard;
        s_ctrl_kb->set_report_ctrl(true);
    }
}

/* ui->close_menu: run_app (app_loader.cc) asks for it once the app has returned */
static volatile bool s_close_menu = false;
static void api_ui_close_menu(void) { s_close_menu = true; }
extern "C" bool app_api_take_close_menu(void)
{
    bool r = s_close_menu;
    s_close_menu = false;
    return r;
}

static void api_ui_screen_close(void *h)
{
    Window *win = (Window *)h;
    if (win && (win == s_wide_win)) {
        /* 40 x 25 again; the clear below is what every other window's close does, so the end state is
         * the same (the loader draws the menu again, or the menu closes over a blank overlay) */
        s_wide_win = NULL;
        delete win;
        wide_end();
        if (s_current_ui && s_current_ui->screen) {
            s_current_ui->screen->clear();
        }
        return;
    }
    if (win) {
        delete win;
        /* Re-prompt the UI task to redraw its menu underneath. */
        if (s_current_ui && s_current_ui->screen) {
            s_current_ui->screen->clear();
        }
    }
}

/* Files — stdio-style wrappers                                        */

static void *api_fopen(const char *path, const char *mode) { return fopen(path, mode); }

static int api_fread(void *buf, int size, int n, void *f) { return (int)fread(buf, size, n, (FILE *)f); }

static int api_fwrite(const void *buf, int size, int n, void *f) { return (int)fwrite(buf, size, n, (FILE *)f); }

static int api_fseek(void *f, long offs, int whence) { return fseek((FILE *)f, offs, whence); }

static long api_ftell(void *f) { return ftell((FILE *)f); }

static int api_fclose(void *f) { return fclose((FILE *)f); }

static int api_file_exists(const char *path, const char *name)
{
    if (!path || !name) {
        return 0;
    }
    FileInfo info(128);
    return (FileManager::getFileManager()->fstat(path, name, info) == FR_OK) ? 1 : 0;
}

/* C64 bus — direct memory-mapped IO                                   */

#include "c64.h"
#include "c64_subsys.h"
#include "iomap.h"
#include "u64.h"

static uint8_t api_c64_peek(uint16_t addr) { return *((volatile uint8_t *)(C64_MEMORY_BASE + addr)); }

static void api_c64_poke(uint16_t addr, uint8_t val) { *((volatile uint8_t *)(C64_MEMORY_BASE + addr)) = val; }

static int api_c64_dma_read(uint16_t addr, void *dst, int len)
{
    /* Byte-by-byte through the memory-mapped C64 bus. */
    uint8_t *d = (uint8_t *)dst;
    for (int i = 0; i < len; i++) {
        d[i] = *((volatile uint8_t *)(C64_MEMORY_BASE + (uint32_t)(addr + i)));
    }
    return len;
}

static int api_c64_dma_write(uint16_t addr, const void *src, int len)
{
    const uint8_t *s = (const uint8_t *)src;
    for (int i = 0; i < len; i++) {
        *((volatile uint8_t *)(C64_MEMORY_BASE + (uint32_t)(addr + i))) = s[i];
    }
    return len;
}

/* Cart-reader bus helpers                                             */

/* Save state for setup/restore. One set is enough — apps single-task. */
static struct
{
    uint8_t bus_bridge, bus_internal, bus_external, serve_control;
    uint8_t cpu_ddr, cpu_port;
    uint8_t dma_memonly;
    bool valid;
} s_cart_saved = {0, 0, 0, 0, 0, 0, 0, false};

/* Saves the bus registers, C64_DMA_MEMONLY and the CPU port, then maps the
 * cartridge: DMA sees the C64 memory map, serving continues while the CPU is
 * stopped, only the external bus is enabled and the CPU port selects BASIC,
 * KERNAL and I/O. C64_MODE is left alone. */
static void api_c64_cart_setup_bus(void)
{
    s_cart_saved.bus_bridge = C64_BUS_BRIDGE;
    s_cart_saved.bus_internal = C64_BUS_INTERNAL;
    s_cart_saved.bus_external = C64_BUS_EXTERNAL;
    s_cart_saved.serve_control = C64_SERVE_CONTROL;
    s_cart_saved.cpu_ddr = C64_PEEK(0x00);
    s_cart_saved.cpu_port = C64_PEEK(0x01);
    s_cart_saved.dma_memonly = C64_DMA_MEMONLY;
    s_cart_saved.valid = true;

    C64_DMA_MEMONLY = 0; /* DMA has to see the C64 memory map (I/O, cart), not raw RAM */
    C64_SERVE_CONTROL = s_cart_saved.serve_control | SERVE_WHILE_STOPPED;
    C64_BUS_BRIDGE = 0x03; /* Dynamic + Writes */
    C64_BUS_INTERNAL = 0;
    C64_BUS_EXTERNAL = 15;
    C64_POKE(0x00, 0x2F); /* DDR: standard */
    C64_POKE(0x01, 0x37); /* LORAM=HIRAM=CHAREN=1 */
}

/* Puts back everything api_c64_cart_setup_bus() saved. Does nothing without a
 * preceding setup. */
static void api_c64_cart_restore_bus(void)
{
    if (!s_cart_saved.valid) {
        return;
    }
    C64_POKE(0x00, s_cart_saved.cpu_ddr);
    C64_POKE(0x01, s_cart_saved.cpu_port);
    C64_BUS_BRIDGE = s_cart_saved.bus_bridge;
    C64_BUS_INTERNAL = s_cart_saved.bus_internal;
    C64_BUS_EXTERNAL = s_cart_saved.bus_external;
    C64_SERVE_CONTROL = s_cart_saved.serve_control;
    C64_DMA_MEMONLY = s_cart_saved.dma_memonly;
    s_cart_saved.valid = false;
}

static void api_c64_cart_mode(uint8_t mode) { C64_MODE = mode; }

static uint8_t api_c64_cart_detect(void) { return U64_CART_DETECT & 0x03; }

static int api_c64_cart_preference(void)
{
    C64 *machine = C64::getMachine();
    return machine ? machine->get_cfg_value(CFG_C64_CART_PREF) : -1;
}

/* Input helpers and misc                                              */

static int api_ui_string_input(const char *title, char *buf, int maxlen)
{
    if (s_current_ui && buf && maxlen > 0) {
        return s_current_ui->string_box(title ? title : "", buf, maxlen);
    }
    return -1;
}

static int api_ui_path_input(char *buf, int maxlen)
{
    if (s_current_ui && buf && maxlen > 0) {
        // buf is the start path when the app puts one in; empty means "where the last pick went"
        mstring picked;
        if (!pick_path(s_current_ui, picked, buf[0] ? buf : NULL)) {
            return -1;
        }
        strncpy(buf, picked.c_str(), maxlen - 1);
        buf[maxlen - 1] = '\0';
        return 1;
    }
    return -1;
}

/* Which key code Fn produces right now. The interface the machine presents
 * decides that, and only the firmware knows which one is in force. */
static int api_mkdir(const char *path)
{
    if (!path) {
        return U2P_ERR_ARG;
    }
    FRESULT fres = FileManager::getFileManager()->create_dir(path);
    if ((fres == FR_OK) || (fres == FR_EXIST)) {
        return U2P_OK;
    }
    return (fres == FR_NO_PATH) ? U2P_ERR_NOT_FOUND : U2P_ERR_IO;
}

static int api_dir(const char *path, int (*cb)(void *ctx, const char *name, uint32_t size, int is_dir), void *ctx)
{
    if (!path || !cb) {
        return U2P_ERR_ARG;
    }
    FileManager *fm = FileManager::getFileManager();
    Directory *dir = NULL;
    FRESULT fres = fm->open_directory(path, &dir);
    if ((fres != FR_OK) || !dir) {
        return ((fres == FR_NO_PATH) || (fres == FR_NO_FILE)) ? U2P_ERR_NOT_FOUND : U2P_ERR_IO;
    }
    FileInfo info(128);
    while (dir->get_entry(info) == FR_OK) {
        if ((info.attrib & (AM_VOL | AM_HID)) || !strcmp(info.lfname, ".") || !strcmp(info.lfname, "..")) {
            continue;
        }
        int is_dir = (info.attrib & AM_DIR) ? 1 : 0;
        if (cb(ctx, info.lfname, is_dir ? 0 : info.size32(), is_dir)) {
            break;
        }
    }
    delete dir;
    return U2P_OK;
}

static int api_rmdir(const char *path)
{
    if (!path) {
        return U2P_ERR_ARG;
    }
    FileInfo info(128);
    FileManager *fm = FileManager::getFileManager();
    if ((fm->fstat(path, info) != FR_OK) || !(info.attrib & AM_DIR)) {
        return U2P_ERR_NOT_FOUND;
    }
    FRESULT fres = fm->delete_file(path); // a directory goes only when it is empty
    return (fres == FR_OK) ? U2P_OK : U2P_ERR_IO;
}

static int api_flash_read(uint32_t addr, void *buf, int len)
{
    Flash *flash = get_flash();
    if (!flash) {
        return U2P_ERR_UNSUPPORTED;
    }
    if (!buf || (len < 0)) {
        return U2P_ERR_ARG;
    }
    uint32_t size = (uint32_t)flash->get_number_of_pages() * (uint32_t)flash->get_page_size();
    if ((addr > size) || ((uint32_t)len > size - addr)) {
        return U2P_ERR_ARG;
    }
    flash->read_linear_addr((int)addr, len, buf);
    return len;
}

// The same steps FileTypeUpdate::execute takes before it jumps into an updater.
static void api_machine_handover(void)
{
    if (s_current_ui && s_current_ui->host) {
        s_current_ui->host->release_ownership();
    }
#if U64 && !defined(RISCV)
    esp32.Quit();
#endif
    acia.deinit();
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
}

static int api_ui_fkey(int n)
{
    if (!s_current_ui) {
        return -1;
    }
    int key;
    switch (n) {
    case 1: key = KEY_F1; break;
    case 2: key = KEY_F2; break;
    case 3: key = KEY_F3; break;
    case 4: key = KEY_F4; break;
    case 5: key = KEY_F5; break;
    case 6: key = KEY_F6; break;
    case 7: key = KEY_F7; break;
    case 8: key = KEY_F8; break;
    default: return -1;
    }
    return s_current_ui->keymapper(key, e_keymap_default);
}

/* pdMS_TO_TICKS rounds down: at 200 Hz a 2 ms wait was vTaskDelay(0), a bare yield that never lets a
 * task of lower priority run. ms > 0 sleeps at least ms: whole ticks rounded up, and one more because
 * vTaskDelay(n) ends on the nth tick edge from now, and the first one comes at any time (app_api.h). */
static void api_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        vTaskDelay(0);
        return;
    }
    vTaskDelay((TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS) + 1);
}

static uint32_t api_tick_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

/* Result codes of the firmware as ABI errors                          */

static int from_ssret(SubsysResultCode_e r)
{
    switch (r) {
    case SSRET_OK:
        return U2P_OK;
    case SSRET_INVALID_PARAMETER:
        return U2P_ERR_ARG;
    case SSRET_CANNOT_OPEN_FILE:
        return U2P_ERR_NOT_FOUND;
    case SSRET_DISK_ERROR:
    case SSRET_FILE_SEEK_FAILED:
    case SSRET_FILE_READ_FAILED:
        return U2P_ERR_IO;
    case SSRET_ERROR_IN_FILE_FORMAT:
    case SSRET_ROM_IMAGE_TOO_LARGE:
        return U2P_ERR_FORMAT;
    case SSRET_NO_LOCK:
        return U2P_ERR_BUSY;
    case SSRET_SUBSYS_NOT_PRESENT:
    case SSRET_NOT_IMPLEMENTED:
        return U2P_ERR_UNSUPPORTED;
    default:
        return U2P_ERR_FAILED;
    }
}

/* Media                                                               */

// Drive letter to subsystem and drive, NULL when this machine has no such drive.
static C1541 *drive_of(int drive, int &subsys)
{
    if (drive == U2P_DRIVE_A) {
        subsys = SUBSYSID_DRIVE_A;
        return c1541_A;
    }
    if (drive == U2P_DRIVE_B) {
        subsys = SUBSYSID_DRIVE_B;
        return c1541_B;
    }
    return NULL;
}

// The same command PUT /v1/drives/{drive}:mount builds (route_drives.cc, api_mount).
static int api_media_mount(int drive, const char *path, int mode)
{
    int subsys;
    if (!path || !*path) {
        return U2P_ERR_ARG;
    }
    if (!drive_of(drive, subsys)) {
        return U2P_ERR_NOT_FOUND;
    }
    char ext[4];
    get_extension(path, ext);
    int command, ftype;
    if (!strcasecmp(ext, "d64")) {
        command = MENU_1541_MOUNT_D64, ftype = 1541;
    } else if (!strcasecmp(ext, "d71")) {
        command = MENU_1541_MOUNT_D64, ftype = 1571;
    } else if (!strcasecmp(ext, "d81")) {
        command = MENU_1541_MOUNT_D64, ftype = 1581;
    } else if (!strcasecmp(ext, "g64")) {
        command = MENU_1541_MOUNT_G64, ftype = 1541;
    } else if (!strcasecmp(ext, "g71")) {
        command = MENU_1541_MOUNT_G64, ftype = 1571;
    } else {
        return U2P_ERR_FORMAT;
    }
    switch (mode) {
    case U2P_MOUNT_READWRITE:
        break;
    case U2P_MOUNT_READONLY:
        command |= MENU_1541_READ_ONLY;
        break;
    case U2P_MOUNT_UNLINKED:
        command |= MENU_1541_UNLINKED;
        break;
    default:
        return U2P_ERR_ARG;
    }
    SubsysCommand *cmd = new SubsysCommand(NULL, subsys, command, ftype, "", path);
    return from_ssret(cmd->execute().status);
}

static int api_media_unmount(int drive)
{
    int subsys;
    if (!drive_of(drive, subsys)) {
        return U2P_ERR_NOT_FOUND;
    }
    SubsysCommand *cmd = new SubsysCommand(NULL, subsys, MENU_1541_REMOVE, 0, "", "");
    return from_ssret(cmd->execute().status);
}

// Starting a program or a cartridge, and a reset, release the menu's hold on the machine. With
// the app still drawing into that menu this would pull the screen from under it, so the request
// waits here until the app has returned; app_api_run_deferred() then carries it out.
enum deferred_e { DEFERRED_NONE, DEFERRED_PRG, DEFERRED_CRT, DEFERRED_RESET };
static deferred_e s_deferred = DEFERRED_NONE;
static mstring s_deferred_path;

static int api_media_run(const char *path)
{
    if (!path || !*path) {
        return U2P_ERR_ARG;
    }
    char ext[4];
    get_extension(path, ext);
    deferred_e kind;
    if (!strcasecmp(ext, "prg")) {
        kind = DEFERRED_PRG;
    } else if (!strcasecmp(ext, "crt")) {
        kind = DEFERRED_CRT;
    } else {
        return U2P_ERR_FORMAT;
    }
    File *f = NULL;
    FileManager *fm = FileManager::getFileManager();
    if ((fm->fopen(path, FA_READ, &f) != FR_OK) || !f) {
        return U2P_ERR_NOT_FOUND;
    }
    fm->fclose(f);
    s_deferred = kind;
    s_deferred_path = path;
    return U2P_OK;
}

static int api_media_reset(void)
{
    s_deferred = DEFERRED_RESET;
    s_deferred_path = "";
    return U2P_OK;
}

extern "C" void app_api_clear_deferred(void)
{
    s_deferred = DEFERRED_NONE;
    (void)app_api_take_close_menu();
}

// Called by the loader once the app has returned. Returns true when the machine was taken away
// from the menu, which then must not be redrawn.
extern "C" bool app_api_run_deferred(void)
{
    deferred_e kind = s_deferred;
    s_deferred = DEFERRED_NONE;
    SubsysResultCode_e r = SSRET_OK;
    switch (kind) {
    case DEFERRED_PRG:
        r = FileTypePRG::start_prg(s_deferred_path.c_str(), true).status;
        break;
    case DEFERRED_CRT: {
        cart_def def;
        crt_autosave_before_reset(); // the changes of the cartridge this one replaces
        r = C64_CRT::load_crt("", s_deferred_path.c_str(), &def, C64::get_cartridge_rom_addr());
        if (r == SSRET_OK) {
            SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
            r = cmd->execute().status;
        }
        break;
    }
    case DEFERRED_RESET: {
        SubsysCommand *cmd = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0);
        r = cmd->execute().status;
        break;
    }
    default:
        return false;
    }
    printf("AppLoader: deferred %s %s: %s\n", (kind == DEFERRED_RESET) ? "reset" : "start",
           s_deferred_path.c_str(), SubsysCommand::error_string(r));
    return r == SSRET_OK;
}

/* Configuration                                                       */

static ConfigItem *find_config_item(const char *category, const char *item, ConfigStore **store)
{
    if (!category || !item) {
        return NULL;
    }
    IndexedList<ConfigStore *> *stores = ConfigManager::getConfigManager()->getStores();
    for (int i = 0; i < stores->get_elements(); i++) {
        ConfigStore *st = (*stores)[i];
        if (strcasecmp(st->get_store_name(), category)) {
            continue;
        }
        for (int n = 0; n < st->items.get_elements(); n++) {
            ConfigItem *it = st->items[n];
            if (!strcasecmp(it->definition->item_text, item)) {
                *store = st;
                return it;
            }
        }
    }
    return NULL;
}

static int api_config_get(const char *category, const char *item, char *buf, int len)
{
    ConfigStore *st;
    if (!buf || (len <= 0)) {
        return U2P_ERR_ARG;
    }
    ConfigItem *it = find_config_item(category, item, &st);
    if (!it) {
        return U2P_ERR_NOT_FOUND;
    }
    switch (it->definition->type) {
    case CFG_TYPE_VALUE:
        snprintf(buf, len, "%d", it->getValue());
        break;
    case CFG_TYPE_ENUM:
        snprintf(buf, len, "%s", it->definition->items[it->getValue()]);
        break;
    case CFG_TYPE_STRING:
    case CFG_TYPE_STRFUNC:
        snprintf(buf, len, "%s", it->getString());
        break;
    default: // passwords and actions have no value to hand out
        return U2P_ERR_UNSUPPORTED;
    }
    return (int)strlen(buf);
}

static int api_config_set(const char *category, const char *item, const char *value, int flags)
{
    ConfigStore *st;
    if (!value) {
        return U2P_ERR_ARG;
    }
    ConfigItem *it = find_config_item(category, item, &st);
    if (!it) {
        return U2P_ERR_NOT_FOUND;
    }
    // The rules of set_item() in route_configs.cc, so an app and PUT /v1/configs take the same values.
    const t_cfg_definition *def = it->definition;
    if (def->type == CFG_TYPE_VALUE) {
        char *end;
        int v = strtol(value, &end, 0);
        if ((end == value) || *end || (v < def->min) || (v > def->max)) {
            return U2P_ERR_ARG;
        }
        it->setValue(v);
    } else if ((def->type == CFG_TYPE_STRING) || (def->type == CFG_TYPE_STRFUNC) || (def->type == CFG_TYPE_STRPASS)) {
        it->setString(value);
    } else if (def->type == CFG_TYPE_ENUM) {
        int n;
        for (n = def->min; n <= def->max; n++) {
            if (!strcasecmp(value, def->items[n])) {
                break;
            }
        }
        if (n > def->max) {
            return U2P_ERR_ARG;
        }
        it->setValue(n);
    } else {
        return U2P_ERR_UNSUPPORTED;
    }
    st->at_close_config();
    if (flags & U2P_CFG_PERSIST) {
        st->write();
    }
    return U2P_OK;
}

/* Answer of a REST launch                                             */

static char *s_reply = NULL;
static int s_reply_cap = 0;
static int s_reply_len = 0;

extern "C" void app_api_set_reply(char *buf, int cap)
{
    s_reply = buf;
    s_reply_cap = cap;
    s_reply_len = 0;
    if (buf && (cap > 0)) {
        buf[0] = 0;
    }
}

extern "C" int app_api_reply(const char *text)
{
    if (!text) {
        return 0;
    }
    if (!s_reply) {
        printf("[APP REPLY] %s\n", text);
        return (int)strlen(text);
    }
    int room = s_reply_cap - 1 - s_reply_len;
    int n = (int)strlen(text);
    if (n > room) {
        n = room;
    }
    memcpy(s_reply + s_reply_len, text, n);
    s_reply_len += n;
    s_reply[s_reply_len] = 0;
    return n;
}

/* ABI tables                                                          */

static const u2p_sys_api_t s_sys = {
    sizeof(u2p_sys_api_t),
    api_tty_printf,
    api_delay_ms,
    api_tick_ms,
    api_flash_read,
    api_machine_handover,
};

static const u2p_mem_api_t s_mem = {
    sizeof(u2p_mem_api_t),
    api_malloc,
    api_free,
    api_realloc,
};

static const u2p_ui_api_t s_ui = {
    sizeof(u2p_ui_api_t),
    api_ui_popup,
    api_ui_confirm,
    api_ui_popup_buttons,
    api_ui_menu,
    api_ui_menu_keys,
    api_ui_string_input,
    api_ui_path_input,
    api_tty_getc,
    api_ui_getch,
    api_ui_fkey,
    api_ui_color,
    api_ui_progress_open,
    api_ui_progress_set,
    api_ui_progress_close,
    api_ui_screen_open,
    api_ui_screen_open_full,
    api_ui_screen_close,
    api_ui_screen_puts,
    api_ui_win_clear,
    api_ui_win_set_color,
    api_ui_win_set_background,
    api_ui_win_reverse_mode,
    api_ui_win_move_cursor,
    api_ui_win_putc,
    api_ui_win_puts,
    api_ui_win_repeat,
    api_ui_win_set_char_at,
    api_ui_win_draw_border,
    api_ui_win_reset_border,
    api_ui_win_get_size_x,
    api_ui_win_get_size_y,
    api_ui_close_menu,
    api_ui_screen_open_wide,
    api_ui_getch_monitor,
    api_ui_key_ctrl_tap,
};

static const u2p_fs_api_t s_fs = {
    sizeof(u2p_fs_api_t),
    api_fopen,
    api_fread,
    api_fwrite,
    api_fseek,
    api_ftell,
    api_fclose,
    api_file_exists,
    api_mkdir,
    api_dir,
    api_rmdir,
};

static const u2p_c64_api_t s_c64 = {
    sizeof(u2p_c64_api_t),
    api_c64_peek,
    api_c64_poke,
    api_c64_dma_read,
    api_c64_dma_write,
    api_c64_dma_read_frozen,
    api_c64_cart_setup_bus,
    api_c64_cart_restore_bus,
    api_c64_cart_mode,
    api_c64_cart_detect,
    api_c64_cart_preference,
};

static const u2p_media_api_t s_media = {
    sizeof(u2p_media_api_t),
    api_media_source,
    api_media_mount,
    api_media_unmount,
    api_media_run,
    api_media_reset,
};

static const u2p_config_api_t s_config = {
    sizeof(u2p_config_api_t),
    api_config_get,
    api_config_set,
};

/* Not const: `trx` is filled in per launch (app_api_refresh), it depends on the bitstream. */
extern "C" u2p_api_t g_app_api;
u2p_api_t g_app_api = {
    U2P_ABI_MAJOR,
    U2P_ABI_MINOR,
    sizeof(u2p_api_t),
    APPL_VERSION_ASCII,
    &s_sys,
    &s_mem,
    &s_ui,
    &s_fs,
    &s_c64,
    &s_media,
    &s_config,
    NULL, // diag: nothing in this build
    &g_app_net,
    NULL, // trx: app_api_refresh()
    APP_VERSION_HASH,
};

extern "C" void app_api_refresh(void)
{
    g_app_api.trx = app_trx_table();
}
