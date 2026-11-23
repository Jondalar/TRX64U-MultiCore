#include "libboard.h"
#include "libexports.h"
#include "mediamounts.h"
#include "textgrid.h"
#include "menuflows.h"
#include "ops_glue.h"
#include "romfind.h"
#include "libguard.h"
#include "keyboard_c64.h"
#include "u64.h"
#include "usb_base.h"
#include "usb_hub.h"
#include "gitinfo.h"
#include "regset.h"
#include "rest.h"
#include "hw.h"
#include "itu.h"
#include "product.h"
#include "picture.h"
#include "i2c_drv.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

// The NEORV32 instruction cache is direct-mapped, 256 blocks of 8 bytes (the fork's fpga/cpu_unit/vhdl_source/neorv32_wrapper.vhd (not vendored)), and
// the only way to invalidate it is fence.i, which traps on a core built without Zifencei (that wrapper has it only with the JTAG debugger).
// Gideon's app loader therefore executes 2 KB of NOPs from firmware text, which refills every block with something that is not the heap
// (app_loader.cc, app_icache_sweep). Same here, same reason: the heap block a library lands in may have held other code before. The
// section name is ours so that --gc-sections keeps it apart from his.
extern "C" void lib_icache_sweep(void);
__asm__(".pushsection .text.lib_icache_sweep, \"ax\", @progbits\n"
        ".balign 4\n"
        ".global lib_icache_sweep\n"
        "lib_icache_sweep:\n"
        ".rept 512\n"
        "nop\n"
        ".endr\n"
        "ret\n"
        ".popsection\n");

namespace {

extern "C" void SetVideoPll(t_video_mode mode, int ppm);
extern "C" void ResetHdmiPll(void);

class BoardLibHost : public DescLibHost {
public:
    explicit BoardLibHost(const CoreDesc *d) : DescLibHost(d) { memset(ts_, 0, sizeof(ts_)); }
    // FreeRTOS' heap (heap_5) hands out 16-byte aligned blocks; the loader asks for size + alignment and aligns inside, so a 4096 alignment works too
    void *alloc(uint32_t n) { return pvPortMalloc(n); }
    void release(void *p) { vPortFree(p); }
    uint32_t addr_of(const void *p) { return (uint32_t)(uintptr_t)p; }          // no MMU: the CPU sees the byte where it is
    void sync_code(void *, uint32_t) { lib_icache_sweep(); }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec)
    {
        lib_register_fn fn = (lib_register_fn)(uintptr_t)e.entry_addr;           // image + lib_register's offset, relocated by being placed
        return fn(api, rec);
    }
    int call_main(const Entry &e, const LibApi *api, AppCtx *ctx)
    {
        app_main_fn fn = (app_main_fn)(uintptr_t)e.entry_addr;                   // image + app_main's offset (T104.3 D2)
        return fn(api, ctx);
    }
    uint32_t room() { return (uint32_t)xPortGetFreeHeapSize(); }
    uint32_t now_ms() { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }
    void delay_ms(uint32_t ms) { vTaskDelay(ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1); }
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
    // base 3.3 (svc_sync.h, svc_sys.h, svc_board.h). A mutex is a FreeRTOS one with priority inheritance; the lock waits the ticks of timeout_ms rounded up to the 5 ms tick.
    void *mutex_new() { return (void *)xSemaphoreCreateMutex(); }
    bool mutex_lock(void *m, uint32_t timeout_ms)
    {
        TickType_t t = timeout_ms == 0xFFFFFFFFu ? portMAX_DELAY : (timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms) + 1);
        return m && xSemaphoreTake((SemaphoreHandle_t)m, t) == pdTRUE;
    }
    void mutex_unlock(void *m) { if (m) { xSemaphoreGive((SemaphoreHandle_t)m); } }
    void mutex_free(void *m) { if (m) { vSemaphoreDelete((SemaphoreHandle_t)m); } }
    uint16_t ms_timer() { return getMsTimer(); }
    const char *app_version() { return APP_VERSION_STRING; }
    const char *app_git() { return APP_VERSION_HASH; }
    void yield() { taskYIELD(); }
    uint32_t fpga_caps() { return getFpgaCapabilities(); }
    bool is_commodore() { return isCommodoreBoard(); }
    int esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms) { return hw_esp32_rpc(cmd, args, nargs, reply, maxreply, timeout_ms); }
    void video_set_system(CoreCtx *c, int sys) { if (c) { picture_set_system(c, sys); } }
    void video_set_pll(int sys, int ppm)
    {
        SetVideoPll((t_video_mode)sys, ppm);
        ResetHdmiPll();
    }
    // The SID sockets' expander (0x40 on the 1.8 V channel), under the bus lock the keyboard scan shares.
    int sid_i2c_open()
    {
        if (!i2c) {
            return 0;
        }
        if (!i2c->i2c_lock("SID Configuration")) {
            return -1;
        }
        i2c->set_channel(I2C_CHANNEL_1V8);
        return 1;
    }
    void sid_i2c_write(uint8_t reg, uint8_t v) { i2c->i2c_write_byte(0x40, reg, v); }
    void sid_i2c_close() { i2c->i2c_unlock(); }
    // The owner of the handler a task runs (libload.h): kept by task, as the build has no thread-local storage. A task that is in no handler has no entry.
    // A table that is full drops the new entry: the task's allocations then go to the application that is open, as before the scope existed.
    int task_scope()
    {
        void *t = (void *)xTaskGetCurrentTaskHandle();
        int v = -1;
        portENTER_CRITICAL();
        for (int i = 0; i < SCOPE_TASKS; i++) {
            if (ts_[i].task == t) {
                v = ts_[i].slot;
                break;
            }
        }
        portEXIT_CRITICAL();
        return v;
    }
    void set_task_scope(int slot)
    {
        void *t = (void *)xTaskGetCurrentTaskHandle();
        portENTER_CRITICAL();
        int at = -1, free_at = -1;
        for (int i = 0; i < SCOPE_TASKS; i++) {
            if (ts_[i].task == t) {
                at = i;
                break;
            }
            if (!ts_[i].task && free_at < 0) {
                free_at = i;
            }
        }
        if (slot < 0) {
            if (at >= 0) {
                ts_[at].task = 0;
            }
        } else if (at >= 0) {
            ts_[at].slot = slot;
        } else if (free_at >= 0) {
            ts_[free_at].task = t;
            ts_[free_at].slot = slot;
        }
        portEXIT_CRITICAL();
    }

private:
    enum { SCOPE_TASKS = 24 };
    struct { void *task; int slot; } ts_[SCOPE_TASKS];
};

class ConsoleLog : public LibLog {
public:
    void line(const char *t) { printf("%s\n", t); }
};

void lib_log(const char *text)
{
    printf("[lib] %s\n", text);
}

// Does the USB tree hold a mass-storage interface (class 8), installed or still enumerating? Read only; it descends through hubs (class 9, at most
// three levels, the hardware has one). A device that leaves while this reads can only make the answer stale: the pointers are checked at every step.
bool usb_has_storage(UsbDevice *d, int depth)
{
    if (!d || depth > 3) {
        return false;
    }
    int n = d->num_interfaces < USB_NUM_INTERFACES ? d->num_interfaces : USB_NUM_INTERFACES;
    for (int i = 0; i < n; i++) {
        UsbInterface *intf = d->interfaces[i];
        if (!intf) {
            continue;
        }
        uint8_t cls = intf->getInterfaceDescriptor()->interface_class;
        if (cls == 8) {
            return true;
        }
        if (cls == 9 && intf->getDriver()) {
            UsbHubDriver *hub = static_cast<UsbHubDriver *>(intf->getDriver());
            for (int c = 0; c < 7; c++) {
                if (usb_has_storage(hub->getChild(c), depth + 1)) {
                    return true;
                }
            }
        }
    }
    return false;
}

class BoardMountEnv : public LibMountEnv {
public:
    explicit BoardMountEnv(CoreIo *io) : io_(io) { }
    bool usb_storage_attached() { return usb_has_storage(usb2.rootDevice, 0); }
    uint32_t now_ms() { return io_->now_ms(); }
    void delay_ms(uint32_t ms) { io_->delay_ms(ms); }

private:
    CoreIo *io_;
};

// Do the ROMs this core needs come from /flash? (Then a stick is no part of this start, and the wait for one is not either.)
bool roms_from_flash(CoreCtx *c)
{
    const CoreDesc *d = c->desc;
    if (!d->nroms || d->nroms > ROMS_MAX) {
        return false;
    }
    RomResult res[ROMS_MAX];
    char sum[240];
    char own[96];
    if (rom_scan(c->fs, d->folder, d->roms, d->nroms, res, sum, sizeof(sum), c->board ? c->board->rom_dir : 0,
                 rom_own_dir(c->cor_path, own, sizeof(own)) ? own : 0) != ROM_FOUND) {
        return false;
    }
    for (int i = 0; i < d->nroms; i++) {
        if (res[i].status != ROM_FOUND || !fs_path_is_flash(res[i].hit.dir)) {
            return false;
        }
    }
    return true;
}

// Is the Commodore key down on the C64's own keyboard? The matrix, scanned as Keyboard_C64::scan_keyboard does (row/column registers of the
// U64II_KEYB block), for the one key whose modifier flag is 2. Three samples 20 ms apart must all see it: a brush against the key or a
// bounce is not a hold. A joystick in port 2 pulls the same CIA lines and can read as keys; it does not read as this one.
bool commodore_key_sample()
{
    volatile uint8_t *row_reg = &U64II_KEYB_ROW, *col_reg = &U64II_KEYB_COL;
    bool held = false;
    *col_reg = 0;
    if (*row_reg != 0xFF) {
        uint8_t col = 0xFE;
        for (int y = 0; y < 8; y++) {
            uint8_t row;
            *col_reg = 0xFF;
            *col_reg = col;
            *col_reg = col;
            do {
                row = *row_reg;
                *col_reg = col;
            } while (row != *row_reg);
            for (int x = 0; x < 8; x++, row >>= 1) {
                if ((row & 1) == 0 && (Keyboard_C64::matrixModifierFlag(y, x) & 2)) {
                    held = true;
                }
            }
            col = (col << 1) | 1;
        }
    }
    *col_reg = 0xFF;
    return held;
}

bool commodore_key_held(CoreIo *io)
{
    for (int i = 0; i < 3; i++) {
        if (i) {
            io->delay_ms(20);
        }
        if (!commodore_key_sample()) {
            return false;
        }
    }
    return true;
}

LibGuard *guard;
char summary[96] = "none";
bool done;

}

void lib_board_started(void)
{
    if (guard && !guard->finished()) {
        printf("[lib] the boot-attempt counter could not be cleared: the next start skips the libraries\n");
    }
}

static int menu_refused;

void lib_board_menu_note(int refused)
{
    menu_refused = refused;
}

const char *lib_board_summary(void)
{
    static char full[144];
    if (!menu_refused) {
        return summary;
    }
    snprintf(full, sizeof(full), "%s; menu: %d refused (log)", summary, menu_refused);
    return full;
}

static LibLoader *g_loader;

LibLoader *lib_board_loader(void)
{
    return g_loader;
}

namespace {
// The stack has not even found the hub at t+300 ms (ue2emu: hub at t+620, stick at t+2000), so "is a mass-storage device attached" is always no
// here and lib_wait_for_stick would never wait: this env says yes, the wait is then bounded by LIB_MOUNT_WAIT_MS and ends at the first root that lists.
class StartFileEnv : public BoardMountEnv {
public:
    explicit StartFileEnv(CoreIo *io) : BoardMountEnv(io) { }
    bool usb_storage_attached() { return true; }
};
}

void lib_board_wait_stick(CoreCtx *c)
{
    StartFileEnv env(c->io);
    LibWaitResult w;
    lib_wait_for_stick(c->fs, &env, false, &w);
    printf("[core] start file: stick wait: %s\n", w.why);
}

void lib_board_load(CoreCtx *c, LibBootReport *rep, const BundleExtras *bundle)
{
    memset(rep, 0, sizeof(*rep));
    if (done) {
        return;
    }
    done = true;
    // The host and the loader come first and always: the applications of the core's folder (apphost.h) load through the same loader whether or not the
    // libraries are loaded this time (a skipped start loads none of them, T104.1 D12).
    static BoardLibHost host(c->desc);
    static LibLoader loader(&host, ops_of(c));                  // lives as long as the libraries do: for ever
    g_loader = &loader;
    loader.add_services();                                      // base 3.3 (libsvc.cc): the groups sync, settings, ctx, io and board
    loader.set_media_source(mounts_unit_source);                // ctx.media_source for the window drives and the sector units
    loader.set_windows(bundle ? bundle->desc : 0);              // io.window: the register windows of the core's description record (section 19)
    loader.set_board_io(c->io);                                 // sys.delay_us and sys.wall_time work in lib_register, before AppHost::install gives the loader its context
    libexports_register(loader, c);                             // base 3.3 (libexports.cc): the tables c64cart, cbm1541, uci, sid and tempdisk
    if (ops_of(c)->find_op("monitor.open")) {
        loader.add_export("textgrid", 1, monitor_textgrid(c));  // the core has the built-in monitor: its screen is the grid the terminal layout names
    }
    loader.set_settings_apply(regset_apply_setting);
    loader.set_gate_probe([]() -> bool { return rest_uic != 0; });
    loader.set_log(lib_log);
    menu_set_export_finder(LibLoader::find_export);              // the terminal layout finds its text grid by the name of its `grid` parameter
    // The rescue from a crashing library (libguard.h): decided before anything of the stick is touched, and before the wait for it.
    static LibGuard g(c->fs, c->board && c->board->boot_cfg[0] ? c->board->boot_cfg : FS_FLASH_BOOT_MULTICORE);
    guard = &g;
    LibGuardPlan plan;
    g.begin(commodore_key_held(c->io), &plan);
    if (plan.why[0]) {
        printf("[lib] %s\n", plan.why);
        snprintf(rep->screen, sizeof(rep->screen), "%s", plan.why);
        rep->title = plan.skip ? "Libraries skipped" : "Crash guard";
    }
    if (plan.skip) {
        rep->skipped = true;
        snprintf(summary, sizeof(summary), "skipped: %s", plan.skip == LIBSKIP_KEY ? "C= held" : "last start did not finish");
        return;
    }
    static ConsoleLog log;
    static LibScanResult res;
    // A USB stick is still being mounted when the core starts: on the board (2026-10-18) the first scan found no /usb1 while the ROMs came from
    // /usb1 a moment later. The start waits for one stick root only when one is known to be coming (lib_wait_for_stick); without a stick
    // it must not cost every start LIB_MOUNT_WAIT_MS.
    {
        BoardMountEnv env(c->io);
        LibWaitResult w;
        lib_wait_for_stick(c->fs, &env, roms_from_flash(c), &w);
        printf("[lib] stick wait: %s\n", w.why);
    }
    uint32_t free_before = (uint32_t)xPortGetFreeHeapSize();
    struct Arm { LibGuard *g; LibGuardPlan *p; LibBootReport *rep; bool done; };
    Arm arm = { &g, &plan, rep, false };
    void (*arm_cb)(void *) = [](void *x) {
        Arm *a = (Arm *)x;
        if (a->done) {
            return;                                                   // the guard arms before the first library of the bundle or the stick, once
        }
        a->done = true;
        a->g->arm(a->p);
        if (!a->p->armed && a->p->why[0]) {                       // could not write: told like a refusal of the guard
            printf("[lib] %s\n", a->p->why);
            snprintf(a->rep->screen, sizeof(a->rep->screen), "%s", a->p->why);
            a->rep->title = "Crash guard";
        }
    };
    // The bundle's own libraries first (T104.1 D12, Q5): the copy the core was tested with wins over a stick's of the same name.
    LibScanResult cres;
    memset(&cres, 0, sizeof(cres));
    if (bundle && bundle->probed && bundle->ci.size[COR_PART_LIBS]) {
        lib_load_cor(&loader, c->fs, c->cor_path, bundle->ci.off[COR_PART_LIBS], bundle->ci.size[COR_PART_LIBS], &log, &cres, arm_cb, &arm);
    }
    lib_load_folder(&loader, c->fs, lib_roots, LIB_FOLDER, &log, &res, arm_cb, &arm);
    if (cres.loaded || cres.refused) {
        // one result for the screen and the Home line: the bundle's refusals first
        LibScanResult all = res;
        all.loaded += cres.loaded;
        all.refused += cres.refused;
        all.nkept = 0;
        for (int i = 0; i < cres.nkept && all.nkept < LIB_REFUSALS_KEPT; i++) {
            memcpy(all.kept[all.nkept++], cres.kept[i], LIB_ERR_MAX);
        }
        for (int i = 0; i < res.nkept && all.nkept < LIB_REFUSALS_KEPT; i++) {
            memcpy(all.kept[all.nkept++], res.kept[i], LIB_ERR_MAX);
        }
        if (all.refused) {
            snprintf(all.summary, sizeof(all.summary), "%d loaded, %d refused", all.loaded, all.refused);
        } else {
            snprintf(all.summary, sizeof(all.summary), "%d loaded", all.loaded);
        }
        res = all;
    }
    // images: image + bss of what stays; accounts: everything the heap gave the loaded images and has not got back (their blocks with headers, files, menu
    // records, the images); the heap free drops by about that much plus the block headers of the heap itself, and rises by it when the images go (S14)
    printf("[lib] %d loaded, %d refused; images %u bytes, accounts %u bytes, heap free %u -> %u\n", res.loaded, res.refused, (unsigned)loader.resident_bytes(),
           (unsigned)loader.held_total(), (unsigned)free_before, (unsigned)xPortGetFreeHeapSize());
    // The board test of the library build path (docs/specs/T104.1 5.5): app/lib/testlib registers test.echo and test.version, and the only way
    // to see its RISC-V code run before a menu or the REST door can call it is to call it here. Present only when that file is on the stick.
    if (ops_of(c)->find_op("test.echo")) {
        OpResult out;
        OpIn in = in_str("text", "ping");
        int rc = ops_call(c, "test.echo", &in, 1, &out);
        const char *ver = ops_state(c, "test.version");
        printf("[lib] self-test: test.echo(\"ping\") -> rc %d \"%s\"; test.version -> \"%s\"\n", rc, out.msg, ver ? ver : "(none)");
    }
    snprintf(summary, sizeof(summary), "%s", res.summary);
    rep->loaded = res.loaded;
    rep->refused = res.refused;
    if (res.refused) {
        rep->title = "Library not loaded";
        snprintf(rep->screen, sizeof(rep->screen), "%s%s", res.nkept ? res.kept[0] : "A library was refused.",
                 res.refused > 1 ? " (more in the log)" : "");
    }
}
