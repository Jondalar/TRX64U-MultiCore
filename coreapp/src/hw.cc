#include "hw.h"
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "iomap.h"
#include "u64.h"
#include "trx_io.h"
#include <string.h>
#include <stdio.h>
#include "trx_lv_port.h"
#include "dma_uart.h"
#include "esp32.h"
#include "wifi_cmd.h"
#include "board.h"
#include "writeback.h"
#include "handover.h"
#include "walltime.h"
#include "product.h"
#include "joystick_output.h"
#include "drvtab.h"
#define C64_DMA_MEMONLY_REG (*(volatile uint8_t *)(C64_IO_BASE + 0x3))
#define C64_MODE_REG(v)  DRV_cart_regs_mode_WR(v)
#define C64_STOP_REG(v)  DRV_cart_regs_stop_WR(v)
#define C64_MODE_RESET   DRV_cart_regs_mode_reset
#define C64_MODE_UNRESET DRV_cart_regs_mode_unreset
#define DDR_WINDOW     0x01000000u

extern "C" uint32_t get_fattime(void) __attribute__((weak));


namespace {

class HwIo : public CoreIo {
public:
    uint8_t rd(uint32_t reg) { return ioRead8(TRX_IO_BASE + reg); }
    void wr(uint32_t reg, uint8_t v) { ioWrite8(TRX_IO_BASE + reg, v); }
    bool read32(uint32_t addr, uint32_t *v)
    {
        uint32_t w = 0;
        for (int i = 0; i < 4; i++) {
            w |= (uint32_t)(*(volatile uint8_t *)(uintptr_t)(addr + (uint32_t)i)) << (8 * i);
        }
        *v = w;
        return true;
    }
    // The windows take one byte per access (the DMA window's handshake) and the DDR2 window is posted: a read-back
    // of the last address written makes the stores done before the next register write.
    void ddr_write(uint32_t off, const void *p, uint32_t n)
    {
        memcpy((void *)(DDR_WINDOW + off), p, n);
        __asm__ volatile ("fence" ::: "memory");
        if (n) {
            (void)*(volatile uint8_t *)(DDR_WINDOW + off + n - 1);
        }
    }
    void ddr_read(uint32_t off, void *p, uint32_t n) { memcpy(p, (const void *)(DDR_WINDOW + off), n); }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        volatile uint8_t *d = (volatile uint8_t *)(U64_ROMS_BASE + off);
        const uint8_t *s = (const uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        volatile uint8_t *d = (volatile uint8_t *)(U64_ROMS_BASE + off);
        uint8_t *s = (uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) {
            s[i] = d[i];
        }
    }
    void ram_write(uint32_t addr, const void *p, uint32_t n)
    {
        volatile uint8_t *d = (volatile uint8_t *)(C64_MEMORY_BASE + addr);
        const uint8_t *s = (const uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    void ram_read(uint32_t addr, void *p, uint32_t n)
    {
        volatile uint8_t *d = (volatile uint8_t *)(C64_MEMORY_BASE + addr);
        uint8_t *s = (uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) {
            s[i] = d[i];
        }
    }
    void ram_memonly(bool on) { C64_DMA_MEMONLY_REG = on ? 1 : 0; }
    uint8_t cart_rd(uint32_t reg) { return *DRV_PTR(uint8_t, DRV_cart_regs_ADDR + reg); }
    void cart_wr(uint32_t reg, uint8_t v) { *DRV_PTR(uint8_t, DRV_cart_regs_ADDR + reg) = v; }
    void bus_wr(uint32_t off, uint8_t v) { *(volatile uint8_t *)(C64_IO_BASE + off) = v; }
    uint8_t bus_rd(uint32_t off) { return *(volatile uint8_t *)(C64_IO_BASE + off); }
    void eeprom_read(uint32_t off, void *p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) {
            ((uint8_t *)p)[i] = *(volatile uint8_t *)(EEPROM_BASE + off + i);
        }
    }
    void eeprom_write(uint32_t off, const void *p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) {
            *(volatile uint8_t *)(EEPROM_BASE + off + i) = ((const uint8_t *)p)[i];
        }
    }
    // The built-in drive A (T104.2 D9.1): the block drive_a of the driver table (drvtab.h) is the same address on the monolith and on a MultiCore core with drive_l2; the capability
    // word is Gideon's own test for the drive (a read of the window cannot tell a register that is 0 from nothing).
    bool drv_present() { return (getFpgaCapabilities() & CAPAB_DRIVE_1541_1) != 0; }
    uint8_t drv_rd(uint32_t off) { return *DRV_PTR(uint8_t, DRV_drive_a_ADDR + off); }
    void drv_wr(uint32_t off, uint8_t v) { *DRV_PTR(uint8_t, DRV_drive_a_ADDR + off) = v; }
    // Absolute DDR2, the drive's own areas only (linker.x): 0x00EA8000 .. 0x00F00000. Anything else is ignored (reads give zeros).
    static bool drive_area(uint32_t addr, uint32_t n)
    {
        return addr >= 0x00EA8000u && addr < 0x00F00000u && n <= 0x00F00000u - addr;
    }
    void ddr_abs_write(uint32_t addr, const void *p, uint32_t n)
    {
        if (!drive_area(addr, n)) {
            return;
        }
        memcpy((void *)addr, p, n);
        __asm__ volatile ("fence" ::: "memory");
        if (n) {
            (void)*(volatile uint8_t *)(addr + n - 1);
        }
    }
    void ddr_abs_read(uint32_t addr, void *p, uint32_t n)
    {
        if (!drive_area(addr, n)) {
            memset(p, 0, n);
            return;
        }
        memcpy(p, (const void *)addr, n);
    }
    // Gideon's `new uint8_t[]` is a DDR2 address the drive reads (c1541.cc, GcrImage): the CPU address is the hardware address.
    uint8_t *drv_buffer(uint32_t bytes, uint32_t *phys)
    {
        uint8_t *b = new uint8_t[bytes];
        if (b && phys) {
            *phys = (uint32_t)b;
        }
        return b;
    }
    uint8_t bling_flags() { return BLING_RX_FLAGS; }
    void bling_lock_off(bool on) { BLING_RX_FLAGS = on ? 0x01 : 0x00; }
    void case_led_select(uint8_t v) { U64_CASELED_SELECT = v; }
    void master_volume(uint8_t v) { *(volatile uint8_t *)(U64_AUDIO_MIXER + 0x50) = v; }
    void master_tone(uint8_t v) { *(volatile uint8_t *)(U64_AUDIO_MIXER + 0x51) = v; }
    void video_format(uint8_t v) { C64_VIDEOFORMAT = v; }
    // T181: the 11 bytes of the hotkey mask (hotkeys.h) at U64_AUDIO_MIXER + 0x60
    void hotkey_regs(const uint8_t r[11])
    {
        for (int i = 0; i < 11; i++) {
            *(volatile uint8_t *)(U64_AUDIO_MIXER + 0x60 + i) = r[i];
        }
    }
    // T206: MATRIX_KEYB bytes 0-7 (host-app/software/system/u2p.h): byte = column, bit = row
    void matrix_keys(const uint8_t m[8])
    {
        for (int i = 0; i < 8; i++) {
            *(volatile uint8_t *)(U2P_IO_BASE + 0x0300 + i) = m[i];
        }
    }
    // T228 R10: JoystickOutput owns C64_JOY1_SWOUT / C64_JOY2_SWOUT (each apply() writes both, from the mouse's buttons too), so the pad is one more of its
    // sources, not a write of its own; the registers take the lines active low.
    void joy_lines(int port, uint8_t pressed)
    {
        JoystickOutput::instance().setPadPort(port, (uint8_t)(~pressed & 0x1F));
    }
    // T181: the matrix as the menu's scan reads it (Keyboard_C64::scan_keyboard), whole in one critical section so that the matrix timer's scan cannot
    // move the column in between; a column is settled when two reads agree.
    bool keys_read(uint8_t m[8], bool *restore)
    {
        taskENTER_CRITICAL();
        uint8_t col = 0xFE;
        for (int y = 0; y < 8; y++) {
            U64II_KEYB_COL = 0xFF;
            U64II_KEYB_COL = col;
            uint8_t row;
            do {
                row = U64II_KEYB_ROW;
                U64II_KEYB_COL = col;
            } while (row != U64II_KEYB_ROW);
            m[y] = (uint8_t)~row;
            col = (uint8_t)((col << 1) | 1);
        }
        U64II_KEYB_COL = 0;
        taskEXIT_CRITICAL();
        *restore = U64_RESTORE_REG == 1;
        return true;
    }
    // byte stores at C64_PALETTE + 4n + {0,1,2} = R, G, B (T59 section 1); entries 0-15
    void palette(uint8_t n, uint8_t r, uint8_t g, uint8_t b)
    {
        volatile uint8_t *e = (volatile uint8_t *)(C64_PALETTE + 4u * (n & 15));
        e[0] = r;
        e[1] = g;
        e[2] = b;
    }
    void stop(bool on) { C64_STOP_REG(on ? DRV_cart_regs_stop_stopped : DRV_cart_regs_stop_run); }
    void mode_reset(bool on) { C64_MODE_REG(on ? C64_MODE_RESET : C64_MODE_UNRESET); }
    uint32_t now_ms()
    {
        static uint16_t last;
        static uint32_t hi;
        uint16_t now = getMsTimer();
        if (now < last) {
            hi += 0x10000;
        }
        last = now;
        return hi + now;
    }
    // The board's clock through FatFs' timestamp hook (the Ultimate firmware stamps its files with it): bits 31:25 year since 1980, 24:21 month,
    // 20:16 day, 15:11 hour, 10:5 minute, 4:0 seconds / 2. The symbol is weak, so a firmware without it only costs the clock (the caller makes one
    // up); a clock that was never set reads 1980. The application sets it from the hand-over page at start (walltime.h, T191).
    bool wall_time(uint8_t t[6])
    {
        if (!get_fattime) {
            return false;
        }
        return fattime_to_wall(get_fattime(), t);
    }
    void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) + 1); }
    void delay_us(uint32_t us)
    {
        while (us >= 10) {                               // wait_10us takes up to 126 units
            uint32_t n = us / 10 > 120 ? 120 : us / 10;
            wait_10us((uint8_t)n);
            us -= n * 10;
        }
    }
};

}

// T154: the write-back service's view of the board. The interrupt is the TRX block's (trx_io.cc, ITU high 2), the clock the tick counter, which
// is readable from the interrupt as well.
namespace {
class HwWbPort : public WbPort {
public:
    uint32_t ticks_ms() { return (uint32_t)((uint64_t)xTaskGetTickCountFromISR() * 1000u / configTICK_RATE_HZ); }
    void enter() { portENTER_CRITICAL(); }
    void leave() { portEXIT_CRITICAL(); }
    void irq_attach(uint8_t causes, uint8_t (*handler)(uint8_t)) { trx_irq_attach(causes, handler); }
    void irq_enable(uint8_t causes, bool on) { trx_irq_enable(causes, on); }
};
}

WbPort *hw_wb_port(void)
{
    static HwWbPort port;
    return &port;
}

CoreIo *hw_io(void)
{
    static HwIo io;
    return &io;
}

// The host's ICAP loader is the block icap of the driver table (drvtab.h). An app linked against a TOML A without that block has no ICAP access at all: no partial port
// (hw_partial is null), no way back to the host, and the board probe's ICAP byte reads 0, which board_detect() takes for a standalone installation (board.cc).
#ifdef DRV_HAS_icap
static bool icap_loader(void) { return DRV_icap_id_RD() == DRV_icap_id_loader; }

namespace {

// The loader of layer 1 (host-app coreboot.cc core_swap, the same registers in the same order): decouple request at +20 (status bit 1 is the
// effective decouple), enable at +14 (a rising edge clears the word counter at +18), data bytes at +10 - four make one word, the loader
// reverses the bits itself (top.v icap_swap), so the bytes go out as the file has them.
class HwPartialPort : public PartialPort {
public:
    bool present() { return icap_loader(); }
    bool decouple(bool on)
    {
        DRV_icap_decouple_WR(on ? DRV_icap_decouple_request_MASK : 0);
        if (!on) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10) + 1);              // the core's DDR2 request is served (T79.1: about 1 ms)
        if (!(DRV_icap_decouple_RD() & DRV_icap_decouple_effective_MASK)) {
            DRV_icap_decouple_WR(0);
            return false;
        }
        return true;
    }
    void begin() { DRV_icap_enable_WR(DRV_icap_enable_on_on); }
    void write(const uint8_t *p, uint32_t n)
    {
        volatile uint8_t *dp = DRV_PTR(uint8_t, DRV_icap_data_ADDR);
        const uint32_t *w = (const uint32_t *)p;        // the buffer is word aligned (partial.cc)
        for (uint32_t words = n >> 2; words; words--, w++) {
            uint32_t a = *w;
            *dp = (uint8_t)a; *dp = (uint8_t)(a >> 8); *dp = (uint8_t)(a >> 16); *dp = (uint8_t)(a >> 24);
        }
    }
    void end() { DRV_icap_enable_WR(DRV_icap_enable_on_off); }
    uint32_t words() { return DRV_icap_count0_RD() | (DRV_icap_count1_RD() << 8) | (DRV_icap_count2_RD() << 16) | ((uint32_t)DRV_icap_count3_RD() << 24); }
};

}

PartialPort *hw_partial(void)
{
    static HwPartialPort port;
    return &port;
}
#else
PartialPort *hw_partial(void) { return 0; }                // no ICAP block in the linked TOML A: nothing to reload a partial with (ctx.h)
#endif

namespace {

// The reads board_detect() is made of (T104.2 D2, D3). Every one is a read; nothing here writes.
class HwBoardProbe : public BoardProbe {
public:
    uint8_t icap_id()
    {
#ifdef DRV_HAS_icap
        return DRV_icap_id_RD();
#else
        return 0;                                    // no ICAP block linked: board_detect() takes the profile of a standalone installation
#endif
    }
    // A probe asks for a capability of a block; an app linked against a TOML A without that block has no such probe (T239.3 V-4): the drive bit is read only with the block drive_a, the
    // command interface bit only with the block cmd_if. The bits of the capabilities without a block in TOML A (the cartridge, the ring, the IEC version) are still read.
    uint32_t fpga_caps()
    {
        uint32_t c = getFpgaCapabilities();
#ifndef DRV_HAS_drive_a
        c &= ~BOARD_FPGA_DRIVE_1541_1;
#endif
#ifndef DRV_HAS_cmd_if
        c &= ~BOARD_FPGA_COMMAND_INTF;
#endif
        return c;
    }
    uint8_t trx_caps() { return DRV_ident_caps_RD(); }
    uint8_t iec_version() { return ioRead8(IEC_BASE); }
    bool handover_valid()
    {
        Handover h;
        memcpy(&h, (const void *)HANDOVER_ADDR, sizeof(h));
        return ::handover_valid(&h);
    }
};

}

BoardProbe *hw_board_probe(void)
{
    static HwBoardProbe p;
    return &p;
}

void hw_read_id(uint8_t id[4])
{
    for (int i = 0; i < 4; i++) {
        id[i] = *DRV_PTR(uint8_t, DRV_ident_id_ADDR + i);
    }
}

// ---- the ESP32 link: Gideon's driver (T93 5.13, 5.16; T184) ------------------------------------------------------
// The rocker is not a pin. The ESP32 sends it as a packet whose first byte is the event code (EVENT_FREEZE 0x81, EVENT_MENU 0x82 in rpc_calls.h); the host
// lifts that first byte out of the stream (rx_dma.vhd) and holds Menu as 200 ms of ITU button 1, Freeze as button 0 (T195: bit 5; bit 7 is the right board button).
//
// Since T184 the link is Gideon's WiFi driver as a whole: `esp32` (io/wifi/esp32.cc) owns the one DmaUART on the module's UART and `wifi` (io/wifi/wifi.cc)
// runs the command task and the lwIP interface; both start from their InitFunction (executeAll in coreapp.cc), with wifi_command_init()'s settings
// (5 Mbit/s, CTS/RTS, SLIP, wifi_rx_isr). The old receive callback that refused every packet is gone: the driver's callback routes a reply to the task that
// waits for it and everything else to the WiFi task. Run mode / ModuleCtrl are not used (the control pins are not armed, esp_ctl.v).
// A request: hw_esp32_rpc() below, the same buffer and notify mechanism as wifi_cmd.cc's BUFARGS / TRANSMIT.

// Board test hook, 0 in every build that ships: 1 posts a Menu event from the loopback after the given ms (no ESP32 involved).
#ifndef ROCKER_INJECT_MENU_MS
#define ROCKER_INJECT_MENU_MS 0
#endif

#if ROCKER_INJECT_MENU_MS
// With loopback on, rxd comes from our own transmitter and txd is held high, so the ESP32 neither hears nor says anything (uart_dma.vhd:269-271). The packet is
// the event byte alone: rx_dma lifts it into the button and nothing is left for the driver's callback to take.
static bool rocker_fire(uint8_t ev, uint8_t button, uint32_t *ms)
{
    esp32.uart->EnableLoopback(true);
    esp32.uart->SendSlipPacket(&ev, 1);
    uint16_t t0 = getMsTimer();
    bool up = false;
    while ((uint16_t)(getMsTimer() - t0) < 600) {
        if (ioRead8(ITU_BUTTON_REG) & button) {
            up = true;
            break;
        }
        vTaskDelay(1);
    }
    *ms = (uint16_t)(getMsTimer() - t0);
    esp32.uart->EnableLoopback(false);
    return up;
}
#endif

// The `.cfw` run of a core's library (T104.2 S13): the updater that is started next brings the WiFi UART up itself, so ours is stopped first - the WiFi task and its
// interface gone, no interrupt, no loopback, the receiver and transmitter reset - and the ESP32 is not left with a SLIP receiver that nobody pops.
void hw_rocker_quiesce(void)
{
    esp32.StopApp();
    esp32.uart->EnableIRQ(false);
    esp32.uart->EnableLoopback(false);
    esp32.uart->SoftReset();
}

#define HW_RMII_RX_ENABLE (*(volatile uint8_t *)(RMII_BASE + 0x08))        // rmii_interface.h RMII_RX_ENABLE
#define HW_WIFI_FLOWCTRL  (*(volatile uint8_t *)(WIFI_UART_BASE + 0x03))   // dma_uart.h flowctrl; bit 7 = soft_reset (uart_dma.vhd c_uart_flowctrl)

void hw_net_rx_stop(void)
{
    HW_RMII_RX_ENABLE = 0;                          // received Ethernet frames would land in the heap the next image was copied over
    HW_WIFI_FLOWCTRL = HW_WIFI_FLOWCTRL | 0x80;     // ESP32 frames too: soft_reset drops the armed address (rx_addr_valid, rx_dma flush); other bits as read
}

// The board report of the link: who answers, and how long the module needed. A task of its own: a module that does not answer costs 2.5 s, and the
// boot of the core application must not wait for that (the old probe waited at most 500 ms in line).
static void rocker_report_task(void *)
{
    uint32_t t0 = getMsTimer();
    uint8_t r[48];
    int n = hw_esp32_rpc(CMD_IDENTIFY, 0, 0, r, sizeof(r), 2500);
    uint32_t ms = (uint16_t)(getMsTimer() - t0);
    char ver[40] = "no answer";
    if (n >= 9) {                                  // rpc_identify_resp: the header, major and minor as 16 bit, then the name
        char nm[17];                               // the board's printf has no string precision
        int k = 0;
        for (; k < 16 && 8 + k < n && r[8 + k]; k++) {
            nm[k] = (char)r[8 + k];
        }
        nm[k] = 0;
        snprintf(ver, sizeof(ver), "V%d.%d %s", r[4] | (r[5] << 8), r[6] | (r[7] << 8), nm);
    }
    printf("[core] ESP32 link: %s (%s, %d ms)\n", n >= 9 ? "ok" : "FAILED", ver, (int)ms);
    vTaskDelete(NULL);
}

void hw_rocker_init(void)
{
    // The driver started in executeAll(); nothing here waits for the module.
    xTaskCreate(rocker_report_task, "esp32 link", 4 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
#if ROCKER_INJECT_MENU_MS
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(ROCKER_INJECT_MENU_MS));
        uint32_t ms = 0;
        bool ok = rocker_fire(0x82, ITU_BUTTON1, &ms);
        printf("[core] test hook: Menu event posted, button 1 %s after %d ms\n", ok ? "up" : "DID NOT COME UP", (int)ms);
        vTaskDelete(NULL);
    }, "rocker hook", 2 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
#endif
}

// One request to the ESP32 control module and its answer (T104.2 UI: power cycle, power off, power-on mode, Wi-Fi): rpc_header_t (command, thread, sequence as 16 bit)
// and the arguments in a driver buffer. The thread field is a slot of the driver's reply table (wifi_reply.h, T184 D4), claimed here and answered by wifi_rx_isr
// only with the sequence it was claimed with; wifi_transmit() waits and drops an answer of another command. The reply is the whole answer packet, its first byte
// the command. timeout_ms <= 0 sends and does not wait (power off); no answer in time gives -1.
int hw_esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)
{
    if (nargs < 0 || nargs > 100) {
        return -2;
    }
    command_buf_t *buf;
    if (esp32.uart->GetBuffer(&buf, 1000) != pdTRUE) {
        return -3;
    }
    rpc_header_t *h = (rpc_header_t *)buf->data;
    h->command = cmd;
    h->sequence = 0;
    h->thread = 0xFF;                                  // nobody waits for it
    for (int i = 0; i < nargs; i++) {
        buf->data[sizeof(rpc_header_t) + i] = args[i];
    }
    buf->size = sizeof(rpc_header_t) + nargs;
    if (timeout_ms <= 0) {
        return esp32.uart->TransmitPacket(buf) == pdTRUE ? 0 : -3;
    }
    if (!wifi_reply_register(h)) {
        esp32.uart->FreeBuffer(buf);
        return -3;
    }
    command_buf_t *rb = wifi_transmit(buf, timeout_ms);   // from here on the receive buffer
    if (!rb) {
        return -1;
    }
    int n = rb->size < maxreply ? rb->size : maxreply;
    for (int i = 0; i < n; i++) {
        reply[i] = rb->data[i];
    }
    esp32.uart->FreeBuffer(rb);
    return n;
}

bool hw_menu_button_pressed(void)
{
    static uint8_t prev;
    uint8_t b = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    bool hit = ((b & ~prev) & ITU_BUTTON1) != 0;
    prev = b;
    return hit;
}

#ifdef DRV_HAS_icap
void hw_release_expansion_port(void)
{
    if (!icap_loader()) {
        return;
    }
    DRV_icap_release_WR(DRV_icap_release_port_MASK);
    printf("[core] expansion port released\n");
}

bool hw_return_to_host(char *msg, int n)
{
    if (!icap_loader()) {
        snprintf(msg, n, "no ICAP loader in this bitstream (ID byte is not 0x%02X)", DRV_icap_id_loader);
        return false;
    }
    // dummy word, sync word, NOP, write WBSTAR := 0, write CMD := IPROG, NOP, NOP - the bytes in bitstream (file) order
    static const uint8_t seq[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x99, 0x55, 0x66, 0x20, 0x00, 0x00, 0x00,
        0x30, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F,
        0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    };
    printf("[core] returning to the host: ICAP IPROG, WBSTAR 0\n");
    handover_menu_request((void *)HANDOVER_ADDR);     // T135 D3: the host shows its menu and does not start the default again; DDR2 keeps the page across the IPROG
    trx_lv_yield_layer();
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    portDISABLE_INTERRUPTS();
    DRV_icap_decouple_WR(0);
    DRV_icap_enable_WR(DRV_icap_enable_on_on);
    for (unsigned i = 0; i < sizeof(seq); i++) {
        DRV_icap_data_WR(seq[i]);
    }
    DRV_icap_enable_WR(DRV_icap_enable_on_off);
    for (;;) {                                        // the FPGA reconfigures; nothing here runs on
    }
    return true;
}
#else
void hw_release_expansion_port(void) { }               // no ICAP block in the linked TOML A: the app is standalone by its link, nothing to release

bool hw_return_to_host(char *msg, int n)
{
    snprintf(msg, n, "this app is linked without an ICAP loader: it is standalone");
    return false;
}
#endif

extern uint8_t keyb_joy_select;
extern uint8_t wasd_to_joy;
void hw_keyboard_joystick(uint8_t port, uint8_t wasd)
{
    U64II_KEYB_JOY = keyb_joy_select = port;
    MATRIX_WASD_TO_JOY = wasd_to_joy = wasd;
}
