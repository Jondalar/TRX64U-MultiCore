// The application's life after identification (T93 D4 steps 3-8, the Freeze menu, the menu loop and the start of the service layer's tasks). Board code; the sequences
// it calls (boot.cc, freeze.cc, menuctl.cc, video.cc, settings.cc) are tested natively against models of the core.
#include "listgrow.h"
#include "FreeRTOS.h"
#include "run.h"
#include "diag.h"
#include "online.h"
#include "cbm1541.h"
#include "ui.h"
#include "monitor.h"
#include "netfs.h"
#include "trx_ring.h"
#include "trx64u_ops.h"
#include "hw.h"
#include "fm_fs.h"
#include "d1541_fs.h"
#include "boot.h"
#include "video.h"
#include "videomode.h"
#include "corereport.h"
#include "start.h"
#include "mediumwait.h"
#include "corefolder.h"
#include "settings.h"
#include "picture.h"
#include "menubuild.h"
#include "hostwifi.h"
#include "wifinet.h"
#include "volume.h"
#include "hotkeys.h"
#include "mediamounts.h"
#include "home.h"
#include "freeze.h"
#include "uci.h"
#include "menuctl.h"
#include "romfind.h"
#include "ops_glue.h"
#include "exec.h"
#include "service.h"
#include "writeback.h"
#include "portdiag.h"
extern "C" uint32_t get_fattime(void);
extern "C" bool rtc_time_valid(void);
#include "libboard.h"
#include "restsrv.h"
#include "rest.h"
#include "uiapp.h"
#include "bundle.h"
#include "instfiles.h"
#include "apphost.h"
#include "appsvc.h"
#include "engines.h"
#include "mousesink.h"
#include "baseset.h"
#include "hookhost.h"
#include "hooklib.h"
#include "product.h"
#include "rawkeys.h"
#include "trx_io.h"
#include "trx_lv_port.h"
#include "keyboard_usb.h"
#include "usb_hid.h"
#include "usb_xinput.h"
#include "pad.h"
#include "joystick_output.h"
#include "keyboard_c64.h"
#include "network_interface.h"
#include "u64.h"
#include "gitinfo.h"
#include "startlog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

namespace {

// ---- keys (the host app's way: USB first, then what the matrix timer scan queued) -------------------------------------------
class MatrixHost : public GenericHost {
public:
    bool is_accessible(void) { return true; }
};
MatrixHost matrix_host;
Keyboard_C64 *matrix_kb;

int getch_nowait(void)
{
    int k = system_usb_keyboard.getch();
    if (k < 0 && matrix_kb) {
        k = matrix_kb->getch_nowait();
    }
    return k;
}

// ---- the screen -------------------------------------------------------------------------------------------------------------
class BoardUi : public BootUi {
public:
    void message(const char *title, const char *text, bool bad)
    {
        ui_message(title, text, bad);
        char what[200];
        snprintf(what, sizeof(what), "%s: %s", title, text);
        rest_starting(what);                      // a remote client sees the same text while the start waits (a ROM missing); no effect after the start
    }
    void clear() { ui_message_clear(); }
    void tick() { ui_poll(); }
    bool leave_asked()
    {
        bool leave = false;
        int k;
        while ((k = getch_nowait()) > 0) {
            leave |= boot_leave_key(k);
        }
        return leave;
    }
    bool return_to_host(char *msg, int n) { return hw_return_to_host(msg, n); }
};

class LogStdout : public LibLog {
public:
    void line(const char *t) { printf("%s\n", t); }
};

class TrxPx : public PxPort {
public:
    void w32(uint32_t reg, uint32_t v) { trx_io_write32(TRX_IO_BASE + reg, v); }
    void menu_place(int x, int y, int w, int h) { trx_lv_set_place(x, y, w, h); }
    void menu_canvas(int w, int h) { ui_set_canvas(w, h); }
};

CoreCtx ctx;
Video *video;
TrxPx px;

// T180 D4: a write-back of a medium runs with the menu up: the picture says so (the menu draws itself again after the key that started it)
static void saving_note(CoreCtx *c, bool on)
{
    if (!c->menu_open) {
        return;
    }
    if (on) {
        ui_message("Saving media", "Saving media...", false);
        ui_render_now();
    } else if (c->loading_depth > 0) {                // T226: a write-back inside a load: the load's note stands again
        ui_message("Cartridge", "Loading cartridge...", false);
        ui_render_now();
    } else {
        ui_message_clear();
    }
}

// T226 S1: a cartridge / ROM image is read in with the menu up (seconds on the SNES): the picture says so until the core runs or the load failed (the menu
// shows its answer after the operation, as after a write-back).
static void loading_note(CoreCtx *c, bool on)
{
    if (!c->menu_open) {
        return;
    }
    if (on) {
        ui_message("Cartridge", "Loading cartridge...", false);
        ui_render_now();
    } else {
        ui_message_clear();
    }
}

class BoardFreeze : public FreezeOps {
public:
    void stop(bool on) { ctx.io->stop(on); }
    uint8_t status() { return freeze_status(ctx.io, ctx.desc->freeze); }
    void layer_to_menu() { video->hide(); ui_show(); }
    void layer_to_core() { ui_hide(); video->show(); }
    bool wait_keys_idle(uint32_t timeout_ms)
    {
        uint32_t t0 = ctx.io->now_ms();
        if (matrix_kb) {
            matrix_kb->wait_free();                   // returns when no key is down (or after its own 2 s limit)
            matrix_kb->clear_buffer();
        }
        while (getch_nowait() >= 0) {
        }
        return ctx.io->now_ms() - t0 < timeout_ms;
    }
    uint32_t now_ms() { return ctx.io->now_ms(); }
    void delay_ms(uint32_t ms) { vTaskDelay(ms >= 5 ? pdMS_TO_TICKS(ms) : 1); }
    void log(const char *t) { printf("%s\n", t); }
};

// T97 5.12: the interrupt guard. Three times (10-21, 10-22, 10-23) the core poll task ran on with mstatus.MIE 0 and no critical section
// open: the tick stopped, the task spun on taskYIELD() and the core lost its sector service. Nobody may sleep or yield inside a critical
// section, so MIE 0 here is always that defect: put interrupts back and say which trap returned last (mepc, mcause), so the next
// occurrence names its source. The counters are plain symbols for a JTAG read.
extern "C" {
volatile uint32_t irq_guard_hits;
volatile uint32_t irq_guard_last[4];                      // mstatus, mepc, mcause, tick of the last hit
}

static void irq_guard(void)
{
    uint32_t st;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(st));
    if (st & 8) {
        return;
    }
    uint32_t epc, cause;
    __asm__ volatile ("csrr %0, mepc" : "=r"(epc));
    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    portENABLE_INTERRUPTS();
    uint32_t n = ++irq_guard_hits;
    irq_guard_last[0] = st;
    irq_guard_last[1] = epc;
    irq_guard_last[2] = cause;
    irq_guard_last[3] = (uint32_t)xTaskGetTickCount();
    if (n <= 8 || !(n & (n - 1))) {
        printf("irq guard: MIE was 0 in '%s' (mstatus %08x, mepc %08x, mcause %08x), hit %u\n",
               pcTaskGetName(NULL), (unsigned)st, (unsigned)epc, (unsigned)cause, (unsigned)n);
    }
}

// The service layer's view of FreeRTOS (exec.h): the executor and the poll tasks run on it. Binary semaphores - the build has no counting
// ones - so a give is remembered once; the executor and the waiting callers are written for that. Time is the tick (200 Hz), the same
// clock the sleeps count in, so a period is a whole number of ticks.
class BoardOs : public OsPort {
public:
    void *mutex_new() { return xSemaphoreCreateMutex(); }
    void lock(void *m) { xSemaphoreTake((SemaphoreHandle_t)m, portMAX_DELAY); }
    void unlock(void *m) { xSemaphoreGive((SemaphoreHandle_t)m); }
    void *sem_new() { return xSemaphoreCreateBinary(); }
    void give(void *s) { xSemaphoreGive((SemaphoreHandle_t)s); }
    bool take(void *s, uint32_t ms)
    {
        return xSemaphoreTake((SemaphoreHandle_t)s, ms == OS_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ms)) == pdTRUE;
    }
    bool task_new(const char *name, void (*fn)(void *), void *arg, int above_ui, uint32_t stack_words)
    {
        Entry *e = new Entry();
        e->fn = fn;
        e->arg = arg;
        return xTaskCreate(&BoardOs::entry, name, stack_words, e, tskIDLE_PRIORITY + 1 + above_ui, NULL) == pdPASS;
    }
    void *self() { return (void *)xTaskGetCurrentTaskHandle(); }
    uint32_t now_ms() { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }
    void sleep_ms(uint32_t ms)
    {
        irq_guard();
        if (!ms) {
            taskYIELD();
        } else {
            vTaskDelay(pdMS_TO_TICKS(ms) + 1);
        }
    }

private:
    struct Entry {
        void (*fn)(void *);
        void *arg;
    };
    static void entry(void *p)
    {
        Entry *e = (Entry *)p;
        e->fn(e->arg);
        delete e;
        vTaskDelete(NULL);
    }
};

char rom_names[200];

// The mouse sink's accumulators are shared by the input task and the poll task (mousesink.h): a critical section, as the reference's was.
class BoardMouseCrit : public MouseCrit {
public:
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
};

// The 1351 backend's pots (mousesink.h): the driver's own port-1 output, as it drove it before the library took the reports.
class BoardMousePots : public MousePots {
public:
    void position(int16_t x, int16_t y)
    {
        portENTER_CRITICAL();
        usb_hid_set_mouse1_position(x, y);
        portEXIT_CRITICAL();
        JoystickOutput::instance().paceMouse();
    }
    void buttons(uint8_t joy) { usb_hid_set_joy1_output(joy); }
};

// The two addresses the Network line and the REST server look at (T184 D5): the cable's, when it has link and a usable address, and the WiFi's, when the driver
// says it is usable (not a cached lease). Status > Network reads the same interfaces (netops.cc st_eth_ip, st_wifi_ip).
static void net_addresses(char *lan, int ln, char *wifi, int wn)
{
    lan[0] = wifi[0] = 0;
    for (int i = 0; i < 4; i++) {
        NetworkInterface *ni = NetworkInterface::getInterface(i);
        if (ni && !ni->is_wireless() && ni->is_link_up()) {
            ni->getIpAddrString(lan, ln);
            if (!net_address_usable(lan)) {
                lan[0] = 0;
            }
            break;
        }
    }
    WifiNetStatus w;
    wifi_net_status(&w);
    snprintf(wifi, wn, "%s", w.ip);
}

bool ip_string(char *buf, int len)
{
    char lan[24], wifi[24];
    net_addresses(lan, sizeof(lan), wifi, sizeof(wifi));
    snprintf(buf, len, "%s", lan[0] ? lan : wifi);
    return buf[0] != 0;
}

class BoardMenuHost : public MenuHost {
public:
    void info(int which, char *out, int n)
    {
        out[0] = 0;
        switch (which) {
        case 0:
            snprintf(out, n, "%s%s%s (%c%c%c%c)", ctx.desc->cor_name, ctx.core_version[0] ? " " : "", ctx.core_version, ctx.desc->id[0],
                     ctx.desc->id[1], ctx.desc->id[2], ctx.desc->id[3]);
            break;
        case 1:
            if (APP_VERSION_STRING[0] && APP_BUILD_DATE[0]) {                       // "built" only when the image has a date
                snprintf(out, n, "%s built %s", APP_VERSION_STRING, APP_BUILD_DATE);
            } else if (APP_VERSION_STRING[0]) {
                snprintf(out, n, "%s", APP_VERSION_STRING);
            } else if (APP_BUILD_DATE[0]) {
                snprintf(out, n, "built %s", APP_BUILD_DATE);
            }
            break;
        case 2: {
            // The header asks for this line on every render, so every key press asked four interfaces and the WiFi driver for their addresses. The addresses
            // are read at most once a second (a link that came up shows on the next render after that second).
            static char lan[24], wifi[24];
            static uint32_t net_at;
            static bool net_known;
            uint32_t now = ctx.io->now_ms();
            if (!net_known || now - net_at >= 1000) {
                net_addresses(lan, sizeof(lan), wifi, sizeof(wifi));
                net_at = now;
                net_known = true;
            }
            net_chip_text(out, n, lan, wifi, NET_NONE_TEXT);                       // "LAN x" / "WiFi x", the cable first
            break;
        }
        case 3: {
            // The volumes on the stick. Listing "/" goes through the USB stack, and the header asks for this
            // line on every render (ui.cc), so every key paid for a directory listing - why the core
            // application answered a key later than the host's (Alex, 2026-10-16). Listed at most every 2 s,
            // the period relist() uses for the browser; a stick plugged in shows up within that.
            static char media[96];
            static uint32_t media_at;
            static bool media_known;
            uint32_t now = ctx.io->now_ms();
            if (!media_known || now - media_at >= 2000) {
                FsEntry r[12];
                int c = ctx.fs->list("/", r, 12);
                media[0] = 0;
                for (int i = 0; i < c; i++) {
                    strncat(media, i ? " " : "", sizeof(media) - strlen(media) - 1);
                    strncat(media, r[i].name, sizeof(media) - strlen(media) - 1);
                }
                if (c <= 0) {
                    snprintf(media, sizeof(media), "none");
                }
                media_at = now;
                media_known = true;
            }
            snprintf(out, n, "%s", media);
            break;
        }
        case 4:
            snprintf(out, n, "%s", rom_names[0] ? rom_names : ((ctx.desc->nroms || ctx.desc->romset) ? "(not loaded)" : "none needed"));
            break;
        case 5:
            snprintf(out, n, "%s", lib_board_summary());
            break;
        }
    }
    bool return_to_host(char *msg, int n) { return hw_return_to_host(msg, n); }
};

}

static void collect_rom_names(void)
{
    rom_names[0] = 0;
    if (ctx.desc->romset && ctx.eng) {
        snprintf(rom_names, sizeof(rom_names), "%s", ctx.eng->rs.loaded);       // the member of the ROM set that went into the window
    }
    if (!ctx.desc->nroms) {
        return;
    }
    RomResult res[ROMS_MAX];
    char sum[240];
    char own[96];
    rom_scan(ctx.fs, ctx.desc->folder, ctx.desc->roms, ctx.desc->nroms, res, sum, sizeof(sum), ctx.board ? ctx.board->rom_dir : 0,
             rom_own_dir(ctx.cor_path, own, sizeof(own)) ? own : 0);
    for (int i = 0; i < ctx.desc->nroms; i++) {
        if (res[i].status == ROM_FOUND) {
            strncat(rom_names, rom_names[0] ? ", " : "", sizeof(rom_names) - strlen(rom_names) - 1);
            strncat(rom_names, res[i].hit.name, sizeof(rom_names) - strlen(rom_names) - 1);
        }
    }
}

#define CS_STEPS 4
static void cs_step(int step, const char *label)
{
    STARTLOG("phase %s", label);
    ui_splash_step(step, label);
    rest_starting(label);
}

void coreapp_wait_named(const char *what)
{
    cs_step(0, what);
}

void coreapp_held(const char *why, const BoardProfile *board)
{
    ui_splash_close();                            // the picture and the REST gate were opened before the description (coreapp_open_early)
    rest_starting("The core is held");
    if (!matrix_kb && held_offers_return(board)) {
        matrix_kb = new Keyboard_C64(&matrix_host, &U64II_KEYB_ROW, &U64II_KEYB_COL, &U64II_KEYB_JOY);
    }
    char text[720], err[100] = "";                 // the reason (up to 511 bytes, coreapp.cc) and the RETURN line after it
    held_text(why, board, 0, text, sizeof(text));
    ui_message("This core cannot be started", text, true);
    for (;;) {
        int k;
        while ((k = getch_nowait()) > 0) {
            if (held_key(board, k, hw_return_to_host, err, sizeof(err)) == HELD_RETURN_FAILED) {
                held_text(why, board, err, text, sizeof(text));
                ui_message("This core cannot be started", text, true);
            }
        }
        ui_poll();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// What the application's own settings (baseset.h) put into the board: the menu's look, the product, the host keyboard's joystick registers.
static const BaseSetPort board_set_port = { ui_set_look, ui_set_name_squeeze, isCommodoreBoard, hw_keyboard_joystick };

// T239.3 (advisor 2026-11-23): the base's own groups for a core described by its record, whose code is a library (ops_glue.h ops_record_groups). A built-in
// descriptor registers the groups it uses in its register_ops (c64.cc: cbm1541, online, diag); a record core gets them by its data.
static void record_groups(OpRegistry &r, const CoreDesc *d)
{
    diag_register(r);                                         // the Diagnostics tab (diag.cc): the log, the EDID, the heap, the media and USB states
    online_register(r);                                       // the Assembly64 / CommoServe search, a central library (D12)
    if (desc_has_capability(d, "drive_a")) {                  // drive A is the 1541 in every core that names it (cbm1541.cc): drive.put_rom and the drive's rows
        cbm1541_register(r);
    }
}

static bool early_open;

void coreapp_open_early(const Handover *h)
{
    if (early_open) {
        return;
    }
    early_open = true;
    // T101 S9: the picture comes first. LVGL and the pixel layer are opened here, the splash is the first frame, and every step
    // after it that waits for something (the bundle's medium, the stick, the file system, a library, a ROM) is named on it.
    // T239.3 P2-2 (M1): this runs before the description of a core that has none compiled in is made of its bundle, which waits for the medium of the .cor.
    STARTLOG("phase core app run: pixel layer");
    ui_open(0);
    {
        // T210 P1: the splash is placed for the running mode from its first frame (layer_up writes the place of the first picture). T228: the running mode
        // is the gateware's row, not always the hand-over's (trx64u's bitstream starts in 1080p50), and 1080p without the gateware (video_menu_start).
        int sys = -1, hdmi = -1;
        video_unpack(h && handover_valid(h) ? h->video : 0, &sys, &hdmi);
        bool gw = video_gateware_present();
        video_menu_start(gw, gw ? video_gateware_row() : -1, &sys, &hdmi);
        printf("[core] menu frame: %s %s (%s)\n", video_sys_name(sys), video_hdmi_name(hdmi), gw ? "the gateware's row" : "no gateware: the fixed 1080p frame");
        video_place_menu(&px, hdmi, sys);
    }
    {
        char ver[64];
        snprintf(ver, sizeof(ver), "%s  %s", APP_VERSION_STRING, APP_BUILD_DATE);
        // the name of the core is the bundle's, from the hand-over (the descriptor does not exist yet); a start without a hand-over has none to show
        const char *name = h && handover_valid(h) ? (h->core_name[0] ? h->core_name : handover_core_id(h)) : "";
        ui_splash_open(name, ver, CS_STEPS);
    }
    // The port opens now, not after the start: the start can wait as long as it takes (the bundle's medium, a ROM) and FTP answers meanwhile, so the API must
    // too (board 2026-11-09). Until rest_started below it answers 503 with the step (rest.h); REST and WS reach the service only after that.
    rest_starting("Starting");
    restsrv_start(&ctx, ip_string);               // REST and WS JSON-RPC over the same registry and executor (T104.1 D3, Q8: in the base)
}

void coreapp_run(const CoreDesc *desc, const Handover *h, const BoardProfile *board)
{
    ops_record_groups = record_groups;                        // before anything makes the registry (ops_of)
    ui_live_key_changed = hooks_menu_video;                   // the core hears each change of the picture behind the menu (core_hooks.h menu_video)
    static BoardUi board_ui;
    static BoardFreeze board_freeze;
    static BoardMenuHost board_host;
    memset(&ctx, 0, sizeof(ctx));
    baseset_attach(&ctx, &board_set_port);        // the USB driver reads the mouse settings through it, the settings' operations act through the port
    // The picture and the REST gate came first (coreapp_open_early): the core app task opens them before the description waits for the bundle's medium.
    coreapp_open_early(h);                        // a no-op after the core app task; a caller that did not open them gets them here
    ui_set_desc(desc);
    {
        char ver[64];
        snprintf(ver, sizeof(ver), "%s  %s", APP_VERSION_STRING, APP_BUILD_DATE);
        ui_splash_open(desc->cor_name, ver, CS_STEPS);     // the early splash named the bundle from the hand-over; the descriptor's name replaces it (still at step 0)
    }
    static CoreDesc picture_desc;                 // the descriptor plus the application's own Picture item (T107)
    static Setting picture_items[SETTINGS_MAX];
    PictureEnv picture_env;
    {
        int hsys = -1, hhdmi = -1;
        video_unpack(h && handover_valid(h) ? h->video : 0, &hsys, &hhdmi);
        picture_env_read(&picture_env, hsys);          // T164 D11a: the screen's EDID and the gateware's status decide what the items list
    }
    menu_build(desc, &picture_desc, picture_items, &picture_env);       // T187: Picture, Volume, Persist, Home, Hotkeys - the one chain the benches build too
    // T104.2 D3: the descriptor names what the machine may have, the board strikes what it lacks. The libraries are refused against the
    // effective list (T104.1 Q10), so a library that needs a drive the board has no answer for is told so by name.
    static const char *eff_caps[16];
    if (desc->board_caps && desc->ncaps <= 16) {
        char struck[80];
        picture_desc.ncaps = board_effective_caps(*board, desc->caps, desc->ncaps, eff_caps, 16, struck, sizeof(struck));
        picture_desc.caps = eff_caps;
        printf("[core] capabilities:");
        for (int i = 0; i < picture_desc.ncaps; i++) {
            printf(" %s", eff_caps[i]);
        }
        printf("%s%s\n", struck[0] ? "; struck by the board: " : "", struck);
    }
    ctx.desc = &picture_desc;
    ctx.board = board;
    ctx.io = hw_io();
    ctx.saving = saving_note;
    ctx.loading = loading_note;
    ctx.mount_changed = mounts_engine_changed;     // T180 D2: a mount or unmount in the core's own media engine is stored (mediamounts.h)
    ctx.partial = hw_partial();
    // a .d64 / .d81 is a folder for everything that goes through CoreFs (the browser, Run PRG, Mount & Run of a PRG inside an image; d1541_fs.h)
    static DiskFs disk_fs(fm_fs());
    static NetFs net_fs(&disk_fs);                      // the FTP hosts and the Assembly64 results are folders of the same file system
    net_fs.bind(&ctx);
    netfs_set(&net_fs);
    ctx.fs = &net_fs;
    ListGrow::free_heap() = []() -> uint32_t { return (uint32_t)xPortGetFreeHeapSize(); };   // the Library and the folder pick size their lists against it (listgrow.h)
    core_report_edid_reader() = video_read_edid;       // R8: the bug report names the screen's EDID
    hostwifi_load(ctx.fs);                              // T184 D3: the WiFi settings are the host's (F8); read once, never written
    start_paths(&ctx, h, board);
    char file_config[HANDOVER_CONFIG_MAX + 2] = "";     // T223 D4: a large core's choice comes in a file beside the page
    bool file_safe = false;
    {                                                   // T148 D1, D5, D13: the core's own folder, made at the first start (after its medium is up); an old .cfg beside the .cor moves in
        class BoardMediumEnv : public MediumEnv {
        public:
            uint32_t now_ms() { return ctx.io->now_ms(); }
            void delay_ms(uint32_t ms) { vTaskDelay(ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1); }
        } medium_env;
        start_core_folder(&ctx, &medium_env);
    }
    if (board && board->kind == BOARD_MONOLITH && !(h && handover_valid(h) && h->path[0])) {      // T230 P-2, D6: monolith only (a host start always fills the page, a page-less start there is a dev start): no page, no medium of a .cor to wait for: the file may lie on a stick that is still coming up
        uint64_t sfsz;
        bool sfdir;
        if (!ctx.fs->stat("/SD/" STARTFILE_NAME, &sfsz, &sfdir)) {
            lib_board_wait_stick(&ctx);
        }
    }
    start_file_take(&ctx, h, file_config, sizeof(file_config), &file_safe);    // after start_core_folder: it waits for the core's medium, the file lives on it
    start_migrate_cfg(&ctx, board);
    config_start(&ctx, h && handover_valid(h) && handover_config(h)[0] ? handover_config(h) : file_config);                              // T198 D7: the host named a config in the hand-over page: the settings are read from it
    // The machine's stop register lives in the host (C64_CARTREGS_BASE + 1, hw.cc), so it keeps whatever the
    // application before us left in it: a core application that was replaced with its Freeze menu open leaves the
    // machine stopped, and the next one releases its core into a stop nobody can see. Clearing it here is the
    // state the host's power-up has (spec 5.15). Found on 2026-10-15: the NES ran, was released, and the screen
    // stayed black because a test hook had opened the menu in the application before.
    ctx.io->stop(false);
    static BoardMouseCrit mouse_crit;
    mouse_sink_set_crit(&mouse_crit);
    // The USB mouse reaches the core through the library and nowhere else (T104.1 5.6): while this is registered the driver's own handling
    // of a mouse report (port-1 output, cursor keys, menu navigation) does not run, so our menus are keys only. A core without a mouse
    // record takes the report and drops it.
    static BoardMousePots mouse_pots;
    mouse_sink_set_pots(&mouse_pots);
    usb_hid_set_mouse_sink(mouse_sink_usb_report);
    keyboard_usb_matrix_filter = hk_usb_matrix_filter;   // T181: the USB keyboard's hotkey combination does not reach the core (hotkeys.h)
    hk_usb_set_usage_map(usb_usage_matrix_index);
    usb_hid_set_keyboard_sink(rawkeys_usb_report);       // T141: a core with a rawkeys record gets the USB keyboard as its own key events
    // T156: a USB gamepad reaches the core through the pad service and nowhere else; a core without a pad record drops the reports. The service
    // shares the mouse sink's critical section (the USB task and the poll task).
    static const UsbPadSink pad_sink = { pad_attach, pad_report, pad_detach };
    pad_set_crit(&mouse_crit);
    usb_hid_set_pad_sink(&pad_sink);
    usb_xinput_set_sink(&pad_sink, desc_has_capability(desc, "pad"));      // T196 D3, D4: an XInput pad only in a core that names the capability `pad`
    matrix_kb = new Keyboard_C64(&matrix_host, &U64II_KEYB_ROW, &U64II_KEYB_COL, &U64II_KEYB_JOY);

    cs_step(0, "Reading the settings");           // step 3: the views are built when the menu opens, none at the first open
    ctx.settings = new Settings(ctx.desc->settings, ctx.desc->nsettings, desc->store, ctx.fs, ctx_settings_path(&ctx), 0);
    bool restore_down = false;                           // T228 R21 (FW components/config.cc:233): RESTORE held at the start = safe mode for the configuration: the defaults, the stored file is not read
    {
        uint8_t km[8];
        bool rs = false;
        restore_down = ctx.io && ctx.io->keys_read(km, &rs) && rs;
        if (restore_down) {
            ctx.io->delay_ms(20);
            restore_down = ctx.io->keys_read(km, &rs) && rs;       // twice: a brush is not a hold
        }
    }
    if (restore_down) {
        printf("[core] RESTORE held: safe mode, the settings file is not read this start\n");
        snprintf(ctx.notice, sizeof(ctx.notice), "Safe mode (RESTORE held): the defaults run for this start. The stored settings are kept.");
        ctx.settings->set_safe(true);
        ctx.notice_pending = true;
    } else {
        ctx.settings->load();
    }
    {                                                   // the "Splash" setting (T153, Alex 2026-10-29): Off takes the start screen away as soon as the settings are known
        int ix = ctx.settings->find("Splash");
        if (ix >= 0 && ctx.settings->value(ix) == 0) {
            ui_splash_close();
            ui_render_now();
        }
    }
    if (file_safe || (h && handover_valid(h) && handover_safe(h))) {      // T224 D2: "Boot safe graphics mode": the three video values of this start, stored nowhere
        int n = picture_safe(&ctx);
        printf("[core] safe graphics mode: %d video settings overridden for this start\n", n);
        if (!ctx.notice_pending) {
            snprintf(ctx.notice, sizeof(ctx.notice), "Safe graphics mode: Picture Auto, Scale Integer, Smoothing Off for this start. Your settings are kept.");
            ctx.notice_pending = true;
        }
    }
    printf("[core] settings %s: %s\n", desc->store, ctx.cfg_path[0] ? ctx.cfg_path : "(no .cfg path, in memory only)");
    cs_step(1, "Setting the picture");
    video = new Video(desc->video, &px, ctx.io);
    video->set_alt(desc->alt_video);                  // T194: the C128's second picture
    ctx.video = video;
    // ADR-0057: the application that runs owns the picture. The host handed us the mode it was using so
    // the first frames are visible on the same screen; now the person's choice (the Picture setting), else
    // the mode that shows THIS core's picture largest, is applied and the scale and position follow it.
    // The host puts its own mode back when we hand control over.
    {
        int sys = -1, hdmi = -1;
        video_unpack(h && handover_valid(h) ? h->video : 0, &sys, &hdmi);
        picture_start(&ctx, sys, hdmi);
    }
    Freeze *freeze = new Freeze(desc->freeze, &board_freeze);
    MenuCtl *menu = new MenuCtl(&ctx, &board_host);
    bool menu_open = false;

    // The libraries (T104.1 D12): every .u2a of <stick>/trx/lib, after the descriptor and before the core is released, so what they register is
    // there for the menu, the executor and the poll. A refusal (Q10: a capability this core lacks, a dependency, a cycle) is told here, on the
    // screen and in the log; the core still starts. So does a start that skips them all (libguard.h). Waits for a key, at most 6 s, so that it can be read and an unattended start is not held.
    static BundleExtras bundle;
    {
        LibBootReport lr;
        cs_step(2, "Libraries on the stick");
        bundle_read(&ctx, &bundle);                // the bundle's description (record 6) and where its libraries (record 7) lie
        if (bundle.why[0]) {
            printf("[core] %s\n", bundle.why);
        }
        // gate fix 4 (T228 R10): a pad the core's record declares ([pad]) is a pad as the capability is: the XInput claim follows the descriptor the menu will be built from. This is the
        // first place that knows the record; a pad enumerated before it (the call near the sink registration decided on the C descriptor alone) is not claimed.
        usb_xinput_set_sink(&pad_sink, desc_has_capability(desc, "pad") || (bundle.desc && bundle.desc->has_pad));
        lib_board_load(&ctx, &lr, &bundle);
        if (lr.screen[0]) {                       // a refusal, or why no library was loaded at all (the Commodore key, a start that did not finish)
            board_ui.message(lr.title ? lr.title : "Library not loaded", lr.screen, true);
            uint32_t t0 = ctx.io->now_ms();
            while (ctx.io->now_ms() - t0 < 6000 && getch_nowait() < 0) {
                board_ui.tick();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            board_ui.clear();
        }
    }

    // The menu merge (T104.1 D8): the description of the bundle, the libraries and the applications, laid onto the descriptor. From here on the
    // descriptor the menu, the settings and the browser read is the merged one; the settings are read again because they may have grown.
    // The applications of the core's folder (T104.3, appindex.h) are read here, header and menu record only: nothing of them is loaded.
    static CoreDesc merged_desc;
    static MenuMerge merge;
    static AppIndex app_index;
    static LogStdout say_out;
    const CoreDesc *merge_base = ctx.desc;
    // A core whose record names the library that is its code (CoreDesc::hooks_lib, core_hooks.h): its table of hooks is taken here, after the libraries are loaded and before the first
    // reset (boot_start below), and the descriptor of every merge gets the library's hooks and mount slots (hooks_bind) before the menu is built from it. What goes wrong is told, the core runs on.
    auto hooks_told = [&](const char *what) {
        printf("[core] %s\n", what);
        if (!ctx.notice_pending) {
            snprintf(ctx.notice, sizeof(ctx.notice), "%s", what);
            ctx.notice_pending = true;
        }
    };
    if (desc->hooks_lib) {
        char why[200];
        if (hooks_attach_lib(lib_board_loader(), desc->hooks_lib, why, sizeof(why))) {
            hooks_told(why);
        }
        static CoreDesc bound_desc;                          // bound once: every merge copies a descriptor whose hooks and mount slots are in place (init and release are never null for the tasks that run meanwhile)
        bound_desc = *merge_base;
        if (const char *bad = hooks_bind(&bound_desc)) {
            hooks_told(bad);
        }
        merge_base = &bound_desc;
    }
    auto do_merge = [&]() {
        app_index.set_room(lib_board_loader() ? lib_board_loader()->room() : 0xFFFFFFFFu, lib_board_loader());   // T197 P4: the loader window of this moment
        int refused = bundle_merge(&ctx, merge_base, &bundle, lib_board_loader(), &say_out, &merged_desc, &merge, &app_index);
        lib_board_menu_note(refused);
        ctx.desc = &merged_desc;
        ctx.merge = &merge;
    };
    {
        static char app_folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&ctx, app_folders, APP_FOLDERS_MAX);
        app_index.set_multicore(!ctx.board || ctx.board->can_return_host);                  // the predicate of the state host.multicore (diag.cc)
        appsvc_init(&ctx);                                                                 // appsvc_c64() reads the context (it is set again below, harmless)
        app_index.set_cart_port(appsvc_c64() != 0);                                        // the cartridge tools only where the core gives that service (CART_APP_NEED)
        app_index.set_prg_apps(ops_of(&ctx)->find_op(PRG_APP_OP) != 0);                    // T228 R21 #14: a core that registered the operation gets "Run with App"
        app_index.scan(ctx.fs, app_folders, nf, &say_out);
        printf("[app] %d application(s) in %s\n", app_index.count(), nf ? app_folders[0] : "(no folder)");
        do_merge();
        if (merge.ncontributed_settings || merged_desc.nsettings != desc->nsettings) {
            delete ctx.settings;
            ctx.settings = new Settings(ctx.desc->settings, ctx.desc->nsettings, desc->store, ctx.fs, ctx_settings_path(&ctx), 0);
            if (restore_down) {
                ctx.settings->set_safe(true);                // safe mode (above): the defaults; the stored file is neither read nor written
            } else {
                ctx.settings->load();
            }
            if (file_safe || (h && handover_valid(h) && handover_safe(h))) {
                picture_safe(&ctx);                          // T224 D2: the overrides of this start belong to the new object too
            }
        }
        menu->rebuild();
    }

    ctx.install_files = core_files_install;
    core_files_install(&ctx);                     // record 8 of the bundle: the files the core installs on the card, when missing (instfiles.h)
    hk_apply(&ctx);                               // T181: the map into the host's mask registers (and the USB filter) before the core runs
    cs_step(3, desc->nroms || desc->romset ? "ROM files, then the core" : "Starting the core");
    // The libraries' groups before the first reset and the start hook (api.md 9.7.10, 9.7.5.1): a library's init and start have api->io, the operations, the
    // machine's groups and the jobs. The application host fills them when it installs. A dialog is served by the main loop only, so init and start ask none
    // (api.md 4.5.5). The executor starts before the start hook (a call from the hook, which holds the core lock, runs on its task: exec.cc), the polls after it.
    static BoardOs board_os;
    static CoreLock core_lock(&board_os);
    // The application host (T104.3): app.open from a menu entry, the application's task, its screens and its dialogs. The loader exists whatever the libraries did.
    static AppHost app_host(&ctx, lib_board_loader(), &app_index, &board_os, &core_lock, menu, &board_host);
    appsvc_init(&ctx);
    app_host.install();
    app_host.set_end_hook(appsvc_end);
    app_host.set_machine_groups(appsvc_trx(), appsvc_net(), appsvc_c64(), appsvc_flash_read, appsvc_handover, appsvc_c64_api());
    if (!desc->hooks_lib) {
        hooks_attach(lib_board_loader() ? lib_board_loader()->svc_lib() : 0);    // the table "corehooks" of a library of the core (core_hooks.h), if one exports it; a record that names its library took its table above
    }
    BootResult r = boot_start(&ctx, &board_ui);
    STARTLOG("phase boot_start returned (%s)", r == BOOT_RUNNING ? "running" : "waiting for a program");
    lib_board_started();                          // the start finished: the boot-attempt counter (libguard.h) goes back to 0
    collect_rom_names();

    // The service layer (T104.1 D2, D6, Q9): from here on every operation runs on the executor, in order, and the core's poll on a task of
    // its own above this one's priority; the two share only the core lock. Before this point (the start sequence) an operation runs on
    // the task that calls it: this one, or a library's job.
    // the guard's control (T97 5.12): a yield with interrupts off must count one hit and come back with them on, or the app stops here
    portDISABLE_INTERRUPTS();
    board_os.sleep_ms(0);
    printf("irq guard: self-test %s\n", irq_guard_hits == 1 ? "ok" : "FAILED");
    if (ops_of(&ctx)->error()[0]) {
        printf("[core] a registration was refused: %s\n", ops_error(&ctx));
    }
    static Executor executor(&board_os, &core_lock, ops_of(&ctx), &ctx);
    if (!executor.start("Core ops", 0, 6 * configMINIMAL_STACK_SIZE)) {
        printf("[core] the executor task did not start; operations run on the menu's task\n");
    } else {
        ctx.exec = &executor;
        ctx.lock = &core_lock;
    }
    wb_attach(&ctx, hw_wb_port());                           // T154: "Save Changed Cartridge", the interrupt of the cartridge's flash, the writes of the file
    ring_autostart = cfg_value(&ctx, "Timewarp Ring", 1) != 0;
    bool hook_started = false;
    if (hooks_present()) {                                   // a library of the core starts its own engine (core_hooks.h start): the ring's buffers and its init, the monitor's engine
        char why[120];
        core_lock.lock();
        if (hooks_start(&ctx, why, sizeof(why), &hook_started)) {
            printf("[core] the core's start hook failed: %s\n", why);
            snprintf(ctx.notice, sizeof(ctx.notice), "A part of the core did not start: %s", why);
            ctx.notice_pending = true;
        }
        core_lock.unlock();
    }
    if (!hook_started) {
        trx_ring_engine_init();                              // the frame ring (ring.cc): buffers and its poll task; the log starts 6 s later when the setting says so
        appsvc_trx_ring_update();                            // api->trx->ring: the table was made when the application host was installed, before this start (api.md 4.7.2)
    }
    static PollTask wb_task(&board_os, &core_lock, ops_of(&ctx), &ctx, "writeback.poll", WB_POLL_MS, false);   // the first poll: after the start hook (core_hooks.h start)
    if (!wb_task.start("Save cartridge", 0, 8 * configMINIMAL_STACK_SIZE)) {
        printf("[core] the write-back task did not start; a changed cartridge is written at the menu's open only\n");
    }
    rest_report = diag_report_rest;                           // T162: GET /v1/report
    static PollTask poll(&board_os, &core_lock, ops_of(&ctx), &ctx, desc->service_op, desc->service_period_ms, desc->poll_when_frozen);
    if (desc->service_op && !poll.armed()) {
        printf("[core] %s is not polled: it is not registered, or its name is %d bytes or longer\n", desc->service_op, POLL_OP_MAX - 1);
    }
    if (desc->service_op && !poll.start("Core poll", 1, 6 * configMINIMAL_STACK_SIZE)) {
        printf("[core] the poll task did not start; %s is not served\n", desc->service_op);
    }
    // The polls the description and the libraries declared (T104.1 D6, Q9): each on a task of its own above the menu's, under the core lock; one runs
    // while the machine is frozen for the menu only when its declaration says when_frozen (libapi.h LibPollDecl). Those of the libraries loaded from here on are added by the loader
    // when they register, and end when they are released (T239.3 S3).
    static LibPolls lib_polls(&board_os, &core_lock, ops_of(&ctx), &ctx, 6 * configMINIMAL_STACK_SIZE);
    for (int i = 0; i < merge.npolls; i++) {
        char why[120];
        if (!lib_polls.add(merge.polls[i].owner, merge.polls[i].op, merge.polls[i].period_ms, merge.polls[i].when_frozen, why, sizeof(why))) {
            printf("[core] the poll of %s did not start; %s is not served (%s)\n", merge.polls[i].owner, merge.polls[i].op, why);
        }
    }
    if (lib_board_loader()) {
        lib_board_loader()->set_poll_sink(&lib_polls);
    }
    // T175: the diag core's runner asks the person through the application host's dialogs, on a task of its own (portdiag.h)
    struct DiagHost : PdHost {
        BoardOs *os;
        AppDlg *dlg;
        bool spawn(void (*f)(void *), void *a) override
        {
            return os->task_new("Port diag", f, a, 0, 16 * configMINIMAL_STACK_SIZE);
        }
        int ask(const char *title, const char *text, int timeout_s) override
        {
            int b = dlg->buttons_timed(title, text, SVC_BUTTON_YES | SVC_BUTTON_NO, timeout_s);
            return b == SVC_BUTTON_YES ? 1 : b == SVC_BUTTON_NO ? 0 : b == DLG_TIMEOUT ? -2 : -1;
        }
        int ask_live(const char *title, const char *text, void (*live)(void *), void *ud) override
        {
            int b = dlg->buttons_live(title, text, SVC_BUTTON_YES | SVC_BUTTON_NO, live, ud);
            return b == SVC_BUTTON_YES ? 1 : b == SVC_BUTTON_NO ? 0 : -1;
        }
        int choose(const char *title, const char *text, const char *const *items, int n, int timeout_s) override
        {
            int b = dlg->choose_timed(title, text, items, n, timeout_s);
            return b == DLG_TIMEOUT ? -2 : b;
        }
        void joystick_menu(bool on) override
        {
            if (matrix_kb) {
                matrix_kb->set_keyboard_only(!on);
            }
        }
        void notice(const char *title, const char *text) override { dlg->message(title, text); }
        bool pick(const char *start, char *out, int outlen) override { return dlg->pick_file(start, ".toml", out, outlen); }
        bool stamp(char out[16]) override
        {
            if (!rtc_time_valid()) {
                return false;
            }
            uint32_t ft = get_fattime();
            snprintf(out, 16, "%04u%02u%02u-%02u%02u%02u", (unsigned)(1980 + (ft >> 25)), (unsigned)((ft >> 21) & 15), (unsigned)((ft >> 16) & 31),
                     (unsigned)((ft >> 11) & 31), (unsigned)((ft >> 5) & 63), (unsigned)((ft & 31) * 2));
            return true;
        }
    };
    static DiagHost diag_host;
    diag_host.os = &board_os;
    diag_host.dlg = &app_host.dialog();
    portdiag_attach(&diag_host);
    uint32_t t_req = 0;

    auto open_menu = [&]() {
        t_req = ctx.io->now_ms();
        ui_paused_tag(false);                     // the tag of a paused machine goes, the menu draws itself
        if (ctx.started) {
            ctx.frozen = true;                    // no poll starts from here on ...
            core_lock.lock();                     // ... and one that is in the core is finished (it holds the lock)
            core_lock.unlock();
            freeze->enter();                      // C64_STOP, PAUSED, the layer to the menu
            rawkeys_menu(&ctx, true);             // T141: the keys down on USB are released in the machine; USB keys go to the menu now
        } else {
            ui_show();                            // the core is not running yet: nothing to freeze
        }
        while (getch_nowait() >= 0) {
        }
        ui_message_clear();
        ui_menu_visible(true);
        if (app_host.rescan_if_changed(&say_out)) {              // a stick that came after the start: its applications join the menu
            do_merge();
            menu->rebuild();
            printf("[app] %d application(s) now\n", app_index.count());
        }
        ctx.menu_open = true;                     // before the first frame: the top bar's word (Frozen) is drawn from it
        menu->open();
        if (ctx.started) {
            menu->held_open();                 // T154: the machine is held; a changed cartridge is asked about (Ask) or written (Auto) first
        }
        ui_menu_render(*menu);
        ui_poll();
        menu_open = true;
        uiapp_changed();
        printf("[core] menu open %u ms after the request\n", (unsigned)(ctx.io->now_ms() - t_req));
    };
    auto close_menu = [&]() {
        t_req = ctx.io->now_ms();
        rawkeys_menu(&ctx, false);                // T141: the keys still held are ignored until they are up
        const bool hooked = hooks_has_menu_closed();
        if (!hooked) {
            mon_release();                        // the monitor's hold on the machine, its breakpoints and observers go with the menu
        }
        menu->toast_clear();                      // T160: the volume line does not stay over the running core
        ui_toast_render(*menu);
        ui_menu_visible(false);
        if (hooked) {
            hooks_menu_closed();                  // the core's library gives back its debugger's hold and lands what the menu chose (core_hooks.h menu_closed)
        } else if (trx_ring_pending()) {
            trx_ring_unfreeze();                  // Timewarp: the frame chosen in the menu is where the machine runs on
        }
        if (freeze->frozen()) {
            freeze->leave(ctx.paused);            // the picture back, the closing key out of the matrix, then C64_STOP = 0
        } else {
            ui_hide();
            video->show();
        }
        ctx.frozen = false;
        ctx.menu_open = false;
        menu_open = false;
        ui_paused_tag(ctx.paused);
        uiapp_changed();
        printf("[core] menu closed, machine running %u ms after the close key\n", (unsigned)(ctx.io->now_ms() - t_req));
    };

    // The keys go to the question an application asks, else to the application's screens, else to the core's menu. RUN/STOP on the application's first
    // screen closes the application, not the menu (apphost.h). T192.1: this is the key path of the loop; a core with the service reaches the picture question and the dialogs through the service (uiapp.cc); this path is the menu's and the non-service cores'.
    bool close_req = false;
    auto give_key = [&](int key) {
        if (picture_confirm_pending()) {
            if (key == KEY_LEFT || key == KEY_ESCAPE) {
                picture_confirm_revert();     // T187 D5: LEFT / ESC send the previous mode back at once
            } else {
                picture_confirm_key();        // Q7: any other key keeps the mode and is spent on that
            }
            return;
        }
        if (app_host.dialog_open()) {
            app_host.dialog_key(key);
            return;
        }
        MenuCtl *act = app_host.session() ? app_host.session() : menu;
        if (act->key(key) == MenuCtl::R_CLOSE) {
            if (act == menu) {
                close_req = !ctx.desc->menu_first;      // T175: nothing is behind the menu of a core without a picture
            } else {
                char why[160];
                if (!app_host.close_session(why, sizeof(why))) {
                    act->say(why, true);
                }
            }
        }
    };
    // An intent of the menu (the key table has translated the key): the controller applies it; R_CLOSE closes the menu, or the application's session, exactly as a key does.
    auto give_intent = [&](const MenuIntent &in) {
        MenuCtl *act = app_host.session() ? app_host.session() : menu;
        if (act->apply(in) == MenuCtl::R_CLOSE) {
            if (act == menu) {
                close_req = !ctx.desc->menu_first;
            } else {
                char why[160];
                if (!app_host.close_session(why, sizeof(why))) {
                    act->say(why, true);
                }
            }
        }
    };
    {
        // the UI service (uicore): the adapter lends itself the loop's own hooks through plain function pointers
        static auto *s_open = &open_menu;
        static auto *s_give = &give_key;
        static auto *s_intent = &give_intent;
        static auto *s_close = &close_menu;
        static bool *s_menu_open = &menu_open;
        static AppHost *s_apps = &app_host;
        static MenuCtl *s_menu = menu;
        UiAppEnv env;
        env.user = 0;
        env.menu_open = [](void *) { return *s_menu_open; };
        env.open_menu = [](void *) { (*s_open)(); };
        env.give_key = [](void *, int key) { (*s_give)(key); };
        env.give_intent = [](void *, const MenuIntent *in) { (*s_intent)(*in); };
        env.picture_pending = [](void *) { return picture_confirm_pending(); };
        env.app_dialog = [](void *) { return s_apps->dialog_open(); };
        env.active = [](void *) { return s_apps->session() ? s_apps->session() : s_menu; };
        env.leave = [](void *) { (*s_close)(); };
        env.dialog_key_intent = [](void *, int key, DlgIntent *out) { *out = AppDlg::resolve(key); };
        env.dialog_apply = [](void *, const DlgIntent *in) { s_apps->dialog_apply(*in); };
        env.dialog_view = [](void *) { return &s_apps->dialog_view(); };
        env.picture_left_s = [](void *) { return picture_confirm_left_s(); };
        uiapp_init(&ctx, &env, uiapp_board_platform());
    }
    rest_started();                               // the token gate (rest_uic) and the executor are there: the start gate opens (rest.h)

    if (r == BOOT_RUNNING) {
        ui_splash_close();
        ui_message_clear();
        ui_poll();
        ui_hide();
        video->show();
        printf("[core] %s released\n", desc->cor_name);
        if (desc->menu_first) {
            uiapp_local_open();                   // T175: a core with no machine and no picture; the menu is its screen
        }
    } else {
        ui_splash_close();
        uiapp_local_open();                       // the NES: the browser is the first thing, there is nothing to run yet
    }

    // Board test hook, empty in every build that ships (spec 5.15): runs one file through the descriptor's first
    // action - the same call the browser's "Run" makes - and then reports the core's frame counter for three
    // seconds. It answers "is the core producing frames" without a keyboard and without anyone at the board.
#ifndef COREAPP_AUTORUN_PATH
#define COREAPP_AUTORUN_PATH ""
#endif
    if (COREAPP_AUTORUN_PATH[0] && ctx.desc->nfiletypes && ctx.desc->filetypes[0].nactions) {
        const FileAction &fa = ctx.desc->filetypes[0].actions[0];
        OpResult res;
        int rc = ops_run_bound(&ctx, fa.op, fa.bind, fa.nbind, COREAPP_AUTORUN_PATH, &res);
        printf("[core] autorun %s: rc %d %s\n", COREAPP_AUTORUN_PATH, rc, res.msg);
        if (!rc) {
            ui_menu_visible(false);
            ui_hide();
            video->show();
            menu_open = false;
            ctx.menu_open = false;
        }
        for (int i = 0; i < 12; i++) {
            ctx.io->delay_ms(250);
            printf("[core] seq %02x latest %02x front %08x shown %d\n", ctx.io->rd(desc->video.reg_seq),
                   ctx.io->rd(desc->video.reg_latest), (unsigned)video->front(), video->visible() ? 1 : 0);
            video->poll();
        }
    }

    int confirm_shown = -1;                           // the seconds the Q7 question showed last
    // T164 G12, Q7: the screen's hot-plug and the confirm-or-revert wait of a changed mode, on every pass whatever the menu does
    auto picture_tick = [&]() {
        int ev = picture_service(&ctx);
        if (picture_confirm_pending()) {
            int left = picture_confirm_left_s();
            if ((ev & PS_CONFIRM_ASK) || left != confirm_shown) {
                if (!menu_open) {
                    ui_show();                        // Q7: a change made over REST or the web with the menu closed asks on the screen too: the layer comes up over the running picture
                }
                char now_t[48], prev_t[48];
                picture_confirm_names(now_t, sizeof(now_t), prev_t, sizeof(prev_t));
                ui_picture_question(left, now_t, prev_t);
                ui_poll();
                confirm_shown = left;
            }
        }
        if (ev & PS_CONFIRM_END) {
            confirm_shown = -1;
            ui_message_clear();
            if (const char *res = picture_take_result()) {
                menu->say(res);                       // T187 D4/D5: the result is a chip in the top bar, not a card
            }
            if (menu_open) {
                ui_menu_visible(true);
            } else if (ctx.paused) {
                ui_paused_tag(true);                  // the machine stands: the layer goes back to the keyed screen with its tag
            } else {
                ui_hide();                            // the layer is the core's again
            }
        }
        if (ev & PS_LIST_CHANGED) {
            menu->rebuild();
        }
        if (menu_open && (ev & (PS_CONFIRM_END | PS_LIST_CHANGED))) {
            ui_menu_render(app_host.session() ? *app_host.session() : *menu);
            ui_render_now();
        }
    };
    // T181: the hotkeys. hotkey_poll reads the physical keys (CoreIo::keys_read) and the USB keyboard's, and says which actions fired (HkDetect); hotkey_act performs them
    // with the operations the menu uses (volume and picture are settings, reset is machine.cold_reset); nothing is drawn (D4), the console says what happened.
    static HkLoop hk_loop;
    auto hotkey_poll = [&]() -> uint32_t { return hk_loop.poll(&ctx); };
    auto hotkey_act = [&](uint32_t f) {
        char line[64];
        // T181 D4 (Alex, board 2026-11-06): nothing is drawn over the running core - the action is done and only told on the console.
        if (hk_loop.perform(&ctx, f, line, sizeof(line))) {
            printf("[core] hotkey: %s\n", line);
        }
    };
    bool ui_changed = false, redraw_next = false;
    for (;;) {
        picture_tick();
        if (!menu_open) {
            video->poll();
            uint32_t hf = hotkey_poll();
            if (hf & (1u << HKA_MENU)) {
                uiapp_local_open();                   // the menu hotkey is a local producer of the open intent (T192.3); a core without the service opens it directly
                continue;
            }
            if (hf) {
                hotkey_act(hf);
                if (ctx.want_screen[0] && ctx.started) {  // T228 R1: a hotkey of the core asked for a screen of the menu (Timewarp, Monitor): it opens there
                    uiapp_local_open();
                    continue;
                }
            }
            bool want = hw_menu_button_pressed() | uci_take_freeze_request();   // the command interface's FREEZE (control_target.cc: C64_PUSH_BUTTON) is the menu button pressed
            int k;
            while ((k = getch_nowait()) >= 0) {
                if (picture_confirm_pending()) {
                    uiapp_local_key(k);               // Q7: the key that keeps the mode (the answer "yes" of the service's question) is not the one that opens the menu
                    continue;
                }
                want |= (k == KEY_F10 && !rawkeys_active()) || k == KEY_SCRLOCK || k == KEY_MENU;   // T141 D5: F10 is the Amiga's
            }
            if (want && ctx.started) {
                uiapp_local_open();
                continue;
            }
            app_host.service(false);
            vTaskDelay(1);
        } else {
            uci_take_freeze_request();                // the menu is open already: the request is answered
            if (!app_host.session() && !menu->learning() && (hotkey_poll() & (1u << HKA_MENU))) {   // T207: not while a key is being learned
                uiapp_local_leave();                  // T181: the menu hotkey closes it again (the leave intent; the loop closes below)
                if (!close_req) {
                    continue;
                }
                close_menu();
                continue;
            }
            // Every key that is already queued first, then one render: a held key's repeats collapse into one
            // move instead of one move per pass through this loop, which is what made the menu feel slow. The
            // host application's menu loop is built the same way (hostui.cc:1712-1731).
            int k, nk = 0;
            close_req = false;
            while (!close_req && nk < 24 && (k = getch_nowait()) > 0) {
                nk++;
                uiapp_local_key(k);                   // T192.1-S7: the board keyboard enters through the same key intent a remote client's `key` uses
            }
            if (!nk && hw_menu_button_pressed()) {
                nk++;
                uiapp_local_key(KEY_MENU);
            }
            int nkeys = nk;                                // the log below is about keys; a redraw an application asked for is not one
            if (app_host.service(true)) {
                nk++;
            }
            if (close_req) {
                close_menu();
                continue;
            }
            // The controller's own clock (MenuCtl::pump): a wizard's next press, a new file written one chunk per pass, the monitor's stops, the toast that ran out, a live register, the
            // Library read again every two seconds. On a core that has the UI service this runs inside it (uiapp.cc tick); here, on a core without, the loop calls it.
            if (!uiapp_service() && menu->pump(nk == 0)) {
                nk++;
            }
            if (redraw_next) {
                redraw_next = false;
                nk++;
            }
            ui_changed = nk > 0;
            if (nk) {
                uint32_t t0 = ctx.io->now_ms();
                ui_menu_render(app_host.session() ? *app_host.session() : *menu);
                ui_dialog_render(app_host.dialog());
                ui_toast_render(app_host.session() ? *app_host.session() : *menu);
                uint32_t t1 = ctx.io->now_ms();
                ui_render_now();                         // draw now, not at LVGL's next refresh tick
                if (nkeys) {
                    printf("[core] menu: %d key(s), update %u ms, render+flush %u ms (rows rewritten %u, selection moves %u)\n", nkeys,
                           (unsigned)(t1 - t0), (unsigned)(ctx.io->now_ms() - t1), (unsigned)ui_inplace_moves(), (unsigned)ui_cheap_moves());
                }
            }
            if (ui_poll() < 0) {
                ui_menu_render(app_host.session() ? *app_host.session() : *menu);
                ui_dialog_render(app_host.dialog());
                ui_toast_render(app_host.session() ? *app_host.session() : *menu);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (uiapp_pass(ui_changed)) {
            redraw_next = true;                           // a remote intent changed the menu: the next pass draws it
        }
        ui_changed = false;
        if (close_req) {                                  // ... or closed it (a remote `back`/RUN/STOP): the same close as the local key's
            close_req = false;
            if (menu_open) {
                close_menu();
            }
        }
        if (!ctx.desc->hooks_lib) {                   // a core whose code is a library brings its own monitor (TRX64-Ultimate's, D12): the port is its
            mon_serve(&ctx);                          // the RPC on TCP 4312, whatever the menu does (monsrv.h)
        }
    }
}
