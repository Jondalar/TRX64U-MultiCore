/*
 * cart_base.c
 *
 *  Shared functionality for the cartridge dumper, writer and inspector.
 *  Port of the firmware CartBase (software/io/c64/cart_base.cc).
 */

#include "cart_base.h"

// Scratch buffer for one 8K bank read back from the bus
static uint8_t s_bank_buf[0x2000];

// =========================================================================
//  The cores, the slot block and the session (T57)
// =========================================================================

int g_core = CORE_GIDEON;
bool g_cart_touched;

static bool s_bus_open;     // between cart_setup_bus and cart_restore_bus
static bool s_touch_on;     // the session is open: accesses count
static bool s_wr_open;      // ROM_WR_EN is raised by slot_wr_open
static uint8_t s_wr0;       // SLOT_STATUS b18 when the session began
static const char *s_status_msg = "";

// The registers of the IO block (the host harness puts a model behind them)
#ifdef HOST_TEST
extern uint8_t mock_trx_rd8(uint32_t off);
extern void mock_trx_wr8(uint32_t off, uint8_t v);
#define TRX_RD8(off)    mock_trx_rd8(off)
#define TRX_WR8(off, v) mock_trx_wr8(off, v)
#else
#define TRX_RD8(off)    (g_api->trx->regs[off])
#define TRX_WR8(off, v) (g_api->trx->regs[off] = (v))
#endif

// 32-bit registers over the byte bus (ARCH-lean-state-access 10): a read takes byte 0 first, then 1-3; a write
// of byte 3 commits bytes 0-2, so it comes last
static uint32_t trx_rd32(uint32_t off)
{
    uint32_t v = TRX_RD8(off);
    v |= (uint32_t)TRX_RD8(off + 1) << 8;
    v |= (uint32_t)TRX_RD8(off + 2) << 16;
    v |= (uint32_t)TRX_RD8(off + 3) << 24;
    return v;
}

static void trx_wr32(uint32_t off, uint32_t v)
{
    TRX_WR8(off, (uint8_t)v);
    TRX_WR8(off + 1, (uint8_t)(v >> 8));
    TRX_WR8(off + 2, (uint8_t)(v >> 16));
    TRX_WR8(off + 3, (uint8_t)(v >> 24));
}

// api->trx is there only when the firmware found "TRX2" and the debug page; Gideon's block reads 0, and nothing
// at 0x101A0000 is touched on his core
void core_detect(void)
{
    g_core = CORE_GIDEON;
    if (g_api->trx && g_api->trx->regs && g_api->trx->regs[0] == 'T' && g_api->trx->regs[1] == 'R' &&
        g_api->trx->regs[2] == 'X' && g_api->trx->regs[3] == '2') {
        g_core = CORE_TRX;
    }
    LOG("[CartBase] core: %s\n", g_core == CORE_TRX ? "TRX64 Ultimate" : "Gideon");
}

// Read until two reads agree: the register is read as four bytes, and another task using the block's byte bus in
// between would tear it
uint32_t slot_status(void)
{
    uint32_t a = trx_rd32(SLOT_STATUS_OFF);
    for (int n = 0; n < 4; n++) {
        uint32_t b = trx_rd32(SLOT_STATUS_OFF);
        if (a == b) {
            break;
        }
        a = b;
    }
    return a;
}

static void wr_en_set(bool on)
{
    uint32_t c = trx_rd32(SLOT_CTRL_OFF);
    trx_wr32(SLOT_CTRL_OFF, on ? (c | 2u) : (c & ~2u));
}

bool slot_wr_open(void)
{
    if (!is_trx()) {
        return true;
    }
    wr_en_set(true);
    for (int n = 0; n < 100; n++) {
        if (slot_status() & SLOT_ST_WR_EN) {
            s_wr_open = true;
            return true;
        }
    }
    wr_en_set(s_wr0 != 0);
    LOG("[CartBase] ROM_WR_EN did not rise\n");
    return false;
}

void slot_wr_close(void)
{
    if (!is_trx() || !s_wr_open) {
        return;
    }
    s_wr_open = false;
    for (int n = 0; n < 3; n++) {
        wr_en_set(s_wr0 != 0);
        if (((slot_status() & SLOT_ST_WR_EN) != 0) == (s_wr0 != 0)) {
            return;
        }
    }
    LOG("[CartBase] ROM_WR_EN did not return to %d\n", s_wr0);
    cart_notify("Write lock did not clear!\n"
                "Power-cycle before playing.");
}

void cart_notify(const char *msg)
{
    LOG("[CartBase] %s\n", msg);
    u2p_ui_popup_buttons(msg, BUTTON_OK);
}

void cart_notify_no_cartridge(void)
{
    if (is_trx()) {
        cart_notify("No cartridge on the pins.\n"
                    "A running program can switch\n"
                    "it off: reset, then freeze at\n"
                    "its start.");
    }
}

const char *cart_session_status(void) { return s_status_msg; }

static void session_fail(const char *status)
{
    s_status_msg = status;
    LOG("[CartBase] session: %s\n", status);
}

// T57 5.1. Gideon's core keeps the order it always had; ours adds the port checks (U9, U4, U3/U2) in front and the
// wait for the masks (U5) behind the bus set-up.
bool cart_session_open(bool gideon_normal)
{
    g_cart_touched = false;
    s_touch_on = false;
    s_wr_open = false;
    s_status_msg = "";
    if (!is_trx()) {
        cart_setup_bus();
        if (gideon_normal) {
            g_api->c64->cart_mode(MODE_NORMAL); // Release cart GAME/EXROM pins (freeze sets Ultimax)
            // The mode write only reaches the C64 bus with the next bus cycle; one
            // discarded read through the aperture is that cycle.
            (void)g_api->c64->peek(0x8000);
            LOG("[CartDumper] C64_MODE = MODE_NORMAL (was Ultimax from freeze)\n");
        }
        return true;
    }

    uint32_t s0 = slot_status();
    s_wr0 = (s0 & SLOT_ST_WR_EN) ? 1 : 0;
    if (!(s0 & SLOT_ST_BLOCK)) {
        cart_notify("This bitstream has no cartridge\n"
                    "port support. Update the\n"
                    "TRX64 bitstream.");
        session_fail("No port support in bitstream");
        return false;
    }
    if (s0 & SLOT_ST_FAULT) {
        char msg[64];
        ui_snprintf(msg, sizeof(msg), "Port fault (cause %d).\nReset the C64.", (int)((s0 >> 20) & 15));
        cart_notify(msg);
        session_fail("Port fault: reset the C64");
        return false;
    }
    // Armed at the last reset: the masks then in effect were external ROM, internal ROM off (Auto with a cartridge on
    // the pins, External, Manual). Internal, or a cartridge plugged in later, leaves cart_present unlatched.
    bool latched = (s0 & SLOT_ST_ARMED) && (s0 & SLOT_ST_EXT_ROM) && !(s0 & SLOT_ST_INT_ROM);
    if (!latched) {
        if ((s0 & SLOT_ST_PINS) == SLOT_ST_PINS) {
            cart_notify_no_cartridge();
            session_fail("No cartridge detected.");
        } else {
            cart_notify("The port is not enabled.\n"
                        "Set Cartridge Preference to\n"
                        "Auto or External, reset the\n"
                        "C64 and freeze again.");
            session_fail("Port not armed: reset needed");
        }
        return false;
    }

    cart_setup_bus();
    g_api->c64->cart_mode(MODE_NORMAL); // the freeze's Ultimax keeps ROML/ROMH off the port
    (void)g_api->c64->peek(0x8000);     // the mode reaches the bus with the next cycle

    uint32_t want = SLOT_ST_ARMED | SLOT_ST_EXT_IO | SLOT_ST_EXT_ROM;
    bool ok = false;
    uint32_t t0 = g_api->sys->tick_ms();
    for (int n = 0; n < 1000; n++) {
        uint32_t s = slot_status();
        if ((s & want) == want && !(s & SLOT_ST_INT_ROM)) {
            ok = true;
            break;
        }
        if ((g_api->sys->tick_ms() - t0) > 10 && n >= 4) {
            break;
        }
    }
    if (!ok) {
        session_fail("Port did not arm (SLOT_STATUS).");
        return false;
    }
    s_touch_on = true;
    return true;
}

void cart_session_close(void)
{
    slot_wr_close();
    cart_restore_bus();
    s_touch_on = false;
    // Bank registers and the EasyFlash mode are write-only: a running program cannot be put back into its bank, so
    // the cartridge restarts through the C64's reset. The reset is carried out after the app returned.
    if (is_trx() && g_cart_touched && U2P_HAS(g_api->media, reset)) {
        LOG("[CartBase] Resetting C64 (the cartridge was touched)\n");
        g_api->media->reset();
    }
}

uint8_t cart_peek(uint16_t addr)
{
    if (s_touch_on && addr >= 0xDE00 && addr < 0xE000) {
        g_cart_touched = true;
    }
    return g_api->c64->peek(addr);
}

void cart_poke(uint16_t addr, uint8_t val)
{
    if (s_touch_on && addr >= 0x8000) {
        g_cart_touched = true;
    }
    g_api->c64->poke(addr, val);
}

int cart_read(uint16_t addr, void *dst, int len)
{
    if (s_touch_on && addr < 0xE000 && (uint32_t)addr + (uint32_t)len > 0xDE00u) {
        g_cart_touched = true;
    }
    return g_api->c64->dma_read(addr, dst, len);
}

// =========================================================================
//  Bus management
// =========================================================================

void cart_setup_bus(void)
{
    g_api->c64->cart_setup_bus();
    s_bus_open = true;
    LOG("[CartBase] Bus setup: bridge=03 int=0 ext=15 $01=37\n");
}

void cart_restore_bus(void)
{
    if (s_bus_open) {
        g_api->c64->cart_restore_bus();
        s_bus_open = false;
    }
}

// When the U64 boots with an external cartridge and Cartridge Preference Auto or
// External, the cartridge init is skipped, and on the v3.15 core DMA then no longer
// reaches the cartridge in normal mode: dumping and flashing read raw RAM. Only a
// boot with Cartridge Preference Internal (or Manual) gives access. Our core serves the
// cartridge in the pause whatever the preference says (the session checked the port).
bool cart_check_dma_access(void)
{
    if (is_trx()) {
        return true;
    }
    int pref = g_api->c64->cart_preference();
    if (pref < 0) {
        pref = 1; // no machine: same default as the firmware
    }
    bool ext_cart = ((g_api->c64->cart_detect() & 3) != 3);
    if (!ext_cart || ((pref != 0) && (pref != 2))) {
        return true;
    }

    LOG("[CartBase] No cartridge access: Cartridge Preference is %s\n", pref ? "External" : "Auto");
    u2p_ui_popup_buttons("No access to the cartridge.\n"
                            "Set 'Cartridge Preference'\n"
                            "to 'Internal' and reboot.",
                            BUTTON_OK);
    return false;
}

// =========================================================================
//  Bank switching
// =========================================================================

void cart_switch_bank(int bank, int crt_hw_type)
{
    switch (crt_hw_type) {
    case 15: // C64 Game System — bank encoded in address ($DE00+N)
        C64_POKE(0xDE00 + (bank & 0x3F), 0);
        break;
    case 8: // Super Games — write to $DF00, bits 1:0 = bank
        C64_POKE(0xDF00, (uint8_t)(bank & 0x03));
        break;
    case 32: // EasyFlash — bank to $DE00, Ultimax mode via $DE02
    case 90: // EasyFlash 3 (all slots): the same, the slot is $DE01
        C64_POKE(0xDE00, (uint8_t)bank);
        C64_POKE(0xDE02, 0x05);
        break;
    default: // Ocean, Magic Desk, GMOD2, MegaByter, MegaCart
        C64_POKE(0xDE00, (uint8_t)bank);
        break;
    }
}

// =========================================================================
//  Cart presence detection
// =========================================================================

bool cart_detect_present(cart_base_t *cb)
{
    uint8_t detect = g_api->c64->cart_detect();
    cb->det_game = detect & 1;
    cb->det_exrom = (detect >> 1) & 1;

    if (is_trx() && (slot_status() & SLOT_ST_PINS) != (uint32_t)(detect & 3)) {
        LOG("[CartBase] CART_DETECT %d and SLOT_STATUS pins %d differ\n", detect & 3, (int)(slot_status() & 3));
    }
    LOG("[CartBase] GAME=%d EXROM=%d → %s\n", cb->det_game, cb->det_exrom,
        (cb->det_exrom == 0 && cb->det_game == 1)   ? "8K"
        : (cb->det_exrom == 0 && cb->det_game == 0) ? "16K"
        : (cb->det_exrom == 1 && cb->det_game == 0) ? "Ultimax"
                                                    : "No cart");

    return (detect & 3) != 3;
}

// Forward declaration (defined below in EEPROM section)
static uint16_t eeprom_read_word(int address);

// =========================================================================
//  EasyFlash detection — $DF00 RAM test
// =========================================================================

bool cart_detect_easyflash(void)
{
    // EasyFlash has 256 bytes of RAM at $DF00, always visible.
    // No other cart has RAM at $DF00. Write a pattern and read back.
    uint8_t old = C64_PEEK(0xDF00);
    C64_POKE(0xDF00, 0xA5);
    for (volatile int d = 0; d < 10; d++) {
    } // settle
    uint8_t back = C64_PEEK(0xDF00);
    C64_POKE(0xDF00, old); // restore

    LOG("[CartBase] $DF00 RAM test: wrote $A5, read $%02X → %s\n", back, (back == 0xA5) ? "EasyFlash!" : "no RAM");
    if (back != 0xA5 || g_core != CORE_TRX) {
        return (back == 0xA5);
    }

    // Our board: a cart that drives nothing at $DF00 leaves the bus as the last cycle left it, and the last cycle
    // is our own write, so the read above returns $A5 on a MegaByter, GMod2 or ROM cart too. Two neighbouring
    // cells with different values, read in the order an echo of the last cycle cannot satisfy, then swapped.
    uint8_t old1 = C64_PEEK(0xDF01);
    C64_POKE(0xDF00, 0xA5);
    C64_POKE(0xDF01, 0x5A);
    uint8_t a0 = C64_PEEK(0xDF00); // an echo shows $5A here
    uint8_t a1 = C64_PEEK(0xDF01);
    C64_POKE(0xDF00, 0x5A);
    C64_POKE(0xDF01, 0xA5);
    uint8_t b0 = C64_PEEK(0xDF00);
    uint8_t b1 = C64_PEEK(0xDF01);
    C64_POKE(0xDF00, old);
    C64_POKE(0xDF01, old1);
    bool ram = (a0 == 0xA5 && a1 == 0x5A && b0 == 0x5A && b1 == 0xA5);
    LOG("[CartBase] $DF00/$DF01 echo test: %02X %02X / %02X %02X → %s\n", a0, a1, b0, b1,
        ram ? "RAM" : "open bus");
    return ram;
}

// =========================================================================
//  EasyFlash 3 — slot register and EF-Directory
//
//  In EasyFlash mode the EF3 CPLD answers a $DE01 read with "00000" & slot
//  (ef3-vhdl cart_easyflash.vhdl). An EF1 leaves IO1 undriven and takes a
//  $DE01 write as a bank number. The EF-Directory in slot 0, bank $10, ROML
//  holds the signature, 16 slot names, 8 KERNAL names and a checksum word,
//  all names PETSCII (EF3BootImage directory.s, EasyProg slots.h).
// =========================================================================

// "EF-Directory V1:" in PETSCII, as the EF3 menu and EasyProg compare it
static const uint8_t ef3_dir_signature[EF3_DIR_SIGN_LEN] = { 0xC5, 0xC6, 0x2D, 0xC4, 0x49, 0x52, 0x45, 0x43,
                                                             0x54, 0x4F, 0x52, 0x59, 0x20, 0xD6, 0x31, 0x3A };

bool cart_detect_ef3(cart_base_t *cb)
{
    cb->is_ef3 = false;

    // A read with any of the upper 5 bits set cannot be the slot register: no write at all then
    uint8_t start = C64_PEEK(EF3_SLOT_REG);
    if (start & 0xF8) {
        LOG("[CartBase] EF3 test: $DE01 reads $%02X → not EF3\n", start);
        return false;
    }

    // An undriven bus can read like a slot number too. Write two different slots and read each back; in between
    // the $DF00 RAM drives a value with the upper bits set, so a bus that keeps its last value fails the test.
    static const uint8_t probe[2] = { 5, 2 };
    uint8_t ram_old = C64_PEEK(0xDF00);
    bool match = true;
    for (int i = 0; i < 2 && match; i++) {
        C64_POKE(EF3_SLOT_REG, probe[i]);
        C64_POKE(0xDF00, (uint8_t)(0xF8 | (~probe[i] & 7)));
        (void)C64_PEEK(0xDF00);
        uint8_t back = C64_PEEK(EF3_SLOT_REG);
        LOG("[CartBase] EF3 test: $DE01=%d, read $%02X\n", probe[i], back);
        match = (back == probe[i]);
    }
    C64_POKE(0xDF00, ram_old);

    if (!match) {
        C64_POKE(0xDE00, 0x00); // an EF1 selected banks 5 and 2
        LOG("[CartBase] Not EF3\n");
        return false;
    }

    C64_POKE(EF3_SLOT_REG, start);
    cb->is_ef3 = true;
    cb->ef3_start_slot = start;
    cb->ef3_slot = 0;
    cb->ef3_name[0] = 0;
    LOG("[CartBase] EasyFlash 3, slot %d active\n", start);
    return true;
}

void cart_ef3_select_slot(const cart_base_t *cb)
{
    if (cb->is_ef3) {
        // all slots (the dumper): the scan shows the system area, slot 0
        C64_POKE(EF3_SLOT_REG, cb->ef3_whole ? 0 : cb->ef3_slot ? cb->ef3_slot : cb->ef3_start_slot);
    }
}

void cart_ef3_restore_slot(const cart_base_t *cb)
{
    if (cb->is_ef3) {
        C64_POKE(EF3_SLOT_REG, cb->ef3_start_slot);
        LOG("[CartBase] EF3 slot %d restored\n", cb->ef3_start_slot);
    }
}

bool cart_ef3_read_dir(const cart_base_t *cb, uint8_t *dir)
{
    C64_POKE(0xDE02, 0x05); // Ultimax: ROML at $8000
    C64_POKE(EF3_SLOT_REG, 0);
    C64_POKE(0xDE00, EF3_DIR_BANK);
    C64_READ(0x8000, dir, EF3_DIR_SIZE);
    cart_ef3_select_slot(cb);
    C64_POKE(0xDE00, 0x00);

    bool valid = (memcmp(dir, ef3_dir_signature, EF3_DIR_SIGN_LEN) == 0);
    LOG("[CartBase] EF-Directory: %s\n", valid ? "valid" : "no signature");
    return valid;
}

// The system area of an EasyFlash 3 holds the EF-Directory in bank $10. Seeing its signature identifies the
// cartridge without writing to $DE01, which is what the Cart Inspector needs: on a cartridge that is not an EF3
// the slot register is a bank register. Slot 0 must be the active slot, so this only answers for a cartridge
// still showing its menu; a game slot is recognised by its EAPI instead.
bool cart_ef3_dir_visible(void)
{
    uint8_t sign[EF3_DIR_SIGN_LEN];
    C64_POKE(0xDE00, EF3_DIR_BANK);
    C64_READ(0x8000, sign, sizeof(sign));
    C64_POKE(0xDE00, 0x00);

    bool valid = (memcmp(sign, ef3_dir_signature, EF3_DIR_SIGN_LEN) == 0);
    LOG("[CartBase] EF-Directory in bank $%02X: %s\n", EF3_DIR_BANK, valid ? "found" : "absent");
    return valid;
}

void cart_ef3_slot_name(const uint8_t *dir, int slot, char *name)
{
    cart_petscii_to_ascii(name, dir + EF3_DIR_SIGN_LEN + slot * EF3_NAME_LEN, EF3_NAME_LEN);
}

int cart_ef3_pick_slot(cart_base_t *cb, int extra)
{
    uint8_t dir[EF3_DIR_SIZE];
    bool valid = cart_ef3_read_dir(cb, dir);

    // Slot 0 is the system area and not offered as a slot
    char items[EF3_NUM_SLOTS - 1][24];
    const char *list[EF3_NUM_SLOTS + 1];
    int n = EF3_NUM_SLOTS - 1;
    for (int slot = 1; slot < EF3_NUM_SLOTS; slot++) {
        char name[EF3_NAME_LEN + 1];
        cart_ef3_slot_name(dir, slot, name);
        if (!valid) {
            ui_snprintf(items[slot - 1], sizeof(items[0]), "Slot %d", slot);
        } else {
            // EasyProg shows an empty name as '-'
            ui_snprintf(items[slot - 1], sizeof(items[0]), "%d: %s", slot, name[0] ? name : "-");
        }
        list[slot - 1] = items[slot - 1];
    }
    const int whole_at = (extra & EF3_PICK_WHOLE) ? n : -1;
    if (extra & EF3_PICK_WHOLE)
        list[n++] = "Full EF3 (all slots, type 90)";
    const int erase_at = (extra & EF3_PICK_ERASE) ? n : -1;
    if (extra & EF3_PICK_ERASE)
        list[n++] = "ERASE WHOLE CART...";

    int choice = u2p_ui_menu("EasyFlash 3 slot", list, n);
    if (choice < 0 || choice >= n) {
        return EF3_PICKED_NONE;
    }
    if (choice == whole_at) {
        cb->ef3_whole = true;
        cb->ef3_slot = 0;
        cb->ef3_name[0] = 0;
        cart_ef3_select_slot(cb);
        LOG("[CartBase] EF3: full EF3 picked\n");
        return EF3_PICKED_WHOLE;
    }
    if (choice == erase_at)
        return EF3_PICKED_ERASE;
    cb->ef3_whole = false;
    cb->ef3_slot = (uint8_t)(choice + 1);
    if (valid) {
        cart_ef3_slot_name(dir, cb->ef3_slot, cb->ef3_name);
    } else {
        cb->ef3_name[0] = 0;
    }
    cart_ef3_select_slot(cb);
    LOG("[CartBase] EF3 slot %d picked: '%s'\n", cb->ef3_slot, cb->ef3_name);
    return EF3_PICKED_SLOT;
}

void cart_petscii_to_ascii(char *dst, const uint8_t *src, int len)
{
    int n = 0;
    for (; n < len && src[n]; n++) {
        uint8_t c = src[n];
        if (c >= 0x41 && c <= 0x5A) {
            c += 0x20; // lower case
        } else if (c >= 0x61 && c <= 0x7A) {
            c -= 0x20; // upper case (shifted letters)
        } else if (c >= 0xC1 && c <= 0xDA) {
            c -= 0x80; // upper case
        } else if ((c < 0x20 || c > 0x40) && c != 0x5B && c != 0x5D) {
            c = '?';
        }
        dst[n] = (char)c;
    }
    dst[n] = 0;
}

void cart_ascii_to_petscii(uint8_t *dst, const char *src, int len)
{
    int n = 0;
    for (; n < len && src[n]; n++) {
        uint8_t c = (uint8_t)src[n];
        if (c >= 'a' && c <= 'z') {
            c -= 0x20;
        } else if (c >= 'A' && c <= 'Z') {
            c += 0x80;
        }
        dst[n] = c;
    }
    for (; n < len; n++) {
        dst[n] = 0;
    }
}

// =========================================================================
//  GMod2 detection
// =========================================================================

bool cart_detect_gmod2(void)
{
    // GMod2 detection: two-stage approach.
    //
    // Stage 1: EEPROM presence test (M93C86 serial read via $DE00).
    //   GMod2 is the only cart type with a serial EEPROM.
    //   Read word 0 twice — consistent = EEPROM present.
    //
    // Stage 2 (fallback): If EEPROM reads all-zero or all-FF (empty/absent),
    //   check if "high" banks (64, 128) show identical data.
    //   On GMod2, $DE00 bits 6-7 are control signals (not banking),
    //   so banks 64/128 read the same.
    //   On a real large cart (Ocean/GMod3), they'd have different content.

    // --- Stage 1: EEPROM ---
    uint16_t w0 = eeprom_read_word(0);
    uint16_t w1 = eeprom_read_word(1);
    uint16_t w0_verify = eeprom_read_word(0);
    C64_POKE(0xDE00, 0x00);

    LOG("[CartBase] GMod2 EEPROM test: word0=$%04X word1=$%04X verify=$%04X\n", w0, w1, w0_verify);

    if (w0 == w0_verify && w0 != w1) {
        LOG("[CartBase] GMod2 confirmed (EEPROM present)\n");
        return true;
    }

    if (w0 == w0_verify && w0 != 0xFFFF && w0 != 0x0000) {
        LOG("[CartBase] GMod2 likely (EEPROM uniform data)\n");
        return true;
    }

    // --- Stage 2: High-bank duplicate test ---
    LOG("[CartBase] GMod2 EEPROM unclear, trying high-bank test...\n");

    uint8_t ref[256], b64[256], b128[256];

    cart_switch_bank(0, 0);
    C64_READ(0x8000, ref, 256);

    C64_POKE(0xDE00, 64); // bit 6 = EEPROM CS on GMod2, bank 64 on Ocean
    C64_READ(0x8000, b64, 256);

    C64_POKE(0xDE00, 128); // bit 7 = flash WE on GMod2, bank 128 on Ocean
    C64_READ(0x8000, b128, 256);

    C64_POKE(0xDE00, 0x00);

    bool b64_same = (memcmp(b64, b128, 256) == 0);
    bool b64_is_b0 = (memcmp(ref, b64, 256) == 0);

    LOG("[CartBase] High-bank test: b64==b128: %s, b64==b0: %s\n", b64_same ? "YES" : "NO",
        b64_is_b0 ? "YES" : "NO");

    if (b64_same && !b64_is_b0) {
        // Banks 64 and 128 are identical but different from bank 0.
        // → $DE00 bits 6-7 are control signals, not banking.
        // → Upper bits disturb the bus equally → same garbage.
        // → This is GMod2 behavior.
        LOG("[CartBase] GMod2 confirmed (high banks identical, control bits)\n");
        return true;
    }

    LOG("[CartBase] Not GMod2\n");
    return false;
}

// =========================================================================
//  GMod2 EEPROM read — M93C86 serial protocol via $DE00
//
//  $DE00 bit layout for EEPROM access:
//    Bit 7 (read):  Data Out (DO)
//    Bit 6 (write): Chip Select (CS)
//    Bit 5 (write): Clock (CLK)
//    Bit 4 (write): Data In (DI)
//
//  M93C86 in 16-bit word mode (ORG=1):
//    READ command: opcode "110" + 10-bit address → 16 data bits out
// =========================================================================

#define EEPROM_CS  0x40
#define EEPROM_CLK 0x20
#define EEPROM_DI  0x10
#define EEPROM_DO  0x80

static void eeprom_clock_bit(int di_bit)
{
    uint8_t val = EEPROM_CS | (di_bit ? EEPROM_DI : 0);
    C64_POKE(0xDE00, val);
    C64_POKE(0xDE00, val | EEPROM_CLK);
    C64_POKE(0xDE00, val);
}

static uint16_t eeprom_read_word(int address)
{
    C64_POKE(0xDE00, EEPROM_CS);

    // Send READ: opcode "110" + 10-bit address = 13 bits (MSB first)
    uint16_t cmd = (0x6 << 10) | (address & 0x3FF);
    for (int bit = 12; bit >= 0; bit--) {
        eeprom_clock_bit((cmd >> bit) & 1);
    }

    // Read 16 data bits (MSB first)
    uint16_t data = 0;
    for (int bit = 15; bit >= 0; bit--) {
        C64_POKE(0xDE00, EEPROM_CS);
        C64_POKE(0xDE00, EEPROM_CS | EEPROM_CLK);
        if (C64_PEEK(0xDE00) & EEPROM_DO) {
            data |= (1 << bit);
        }
    }

    C64_POKE(0xDE00, 0x00);
    return data;
}

bool cart_read_gmod2_eeprom(uint8_t *buf)
{
    // Verify EEPROM is present by reading word 0 twice
    uint16_t w0 = eeprom_read_word(0);
    uint16_t w0_verify = eeprom_read_word(0);
    if (w0 != w0_verify) {
        LOG("[CartBase] EEPROM: inconsistent reads — no EEPROM\n");
        C64_POKE(0xDE00, 0x00);
        return false;
    }

    LOG("[CartBase] Reading 2KB EEPROM (word0=$%04X)...\n", w0);
    for (int addr = 0; addr < 1024; addr++) {
        uint16_t w = eeprom_read_word(addr);
        buf[addr * 2] = (uint8_t)(w >> 8);
        buf[addr * 2 + 1] = (uint8_t)(w & 0xFF);
    }
    LOG("[CartBase] EEPROM read complete\n");

    C64_POKE(0xDE00, 0x00);
    return true;
}

// =========================================================================
//  GMod2 EEPROM write — M93C86 EWEN, EWDS and WRITE
//
//  Every command starts with CS rising while CLK is low (eeprom_read_word
//  and the deselect before each command ensure that). A WRITE starts when
//  CS goes low; with CS high again DO reads 0 while the word is written and
//  1 when ready (up to about 10 ms on the real chip; VICE and TRX64 show
//  busy for one read).
// =========================================================================

#define EEPROM_READY_TIMEOUT_MS 100

uint16_t cart_gmod2_eeprom_read_word(int address) { return eeprom_read_word(address); }

// Clocks count bits in, MSB first
static void eeprom_send(uint32_t bits, int count)
{
    for (int bit = count - 1; bit >= 0; bit--) {
        eeprom_clock_bit((bits >> bit) & 1);
    }
}

void cart_gmod2_eeprom_write_enable(bool enable)
{
    C64_POKE(0xDE00, 0x00);
    C64_POKE(0xDE00, EEPROM_CS);
    // Start bit, opcode 00, address 11xxxxxxxx (EWEN) or 00xxxxxxxx (EWDS)
    eeprom_send((1UL << 12) | (enable ? (3UL << 8) : 0), 13);
    C64_POKE(0xDE00, 0x00);
    LOG("[CartBase] EEPROM %s\n", enable ? "EWEN" : "EWDS");
}

bool cart_gmod2_eeprom_write_word(int address, uint16_t data)
{
    C64_POKE(0xDE00, 0x00);
    C64_POKE(0xDE00, EEPROM_CS);
    // Start bit, opcode 01, 10-bit address, 16 data bits
    eeprom_send((1UL << 28) | (1UL << 26) | ((uint32_t)(address & 0x3FF) << 16) | data, 29);
    C64_POKE(0xDE00, 0x00);
    C64_POKE(0xDE00, EEPROM_CS);

    bool ready = false;
    uint32_t start = g_api->sys->tick_ms();
    for (int reads = 1; !ready; reads++) {
        ready = (C64_PEEK(0xDE00) & EEPROM_DO) != 0;
        if (!ready && reads >= 2 && (g_api->sys->tick_ms() - start) > EEPROM_READY_TIMEOUT_MS) {
            break;
        }
    }
    C64_POKE(0xDE00, 0x00);
    if (!ready) {
        LOG("[CartBase] EEPROM write of word %d timed out\n", address);
    }
    return ready;
}

// =========================================================================
//  Bank count via wrap-around
// =========================================================================

static uint32_t hash_bank(void)
{
    // FNV-1a over full 8K of ROML
    C64_READ(0x8000, s_bank_buf, 0x2000);
    uint32_t h = 0x811C9DC5;
    for (int i = 0; i < 0x2000; i++) {
        h ^= s_bank_buf[i];
        h *= 0x01000193;
    }
    return h;
}

int cart_count_banks(void)
{
    // Hash bank 0 and several probe banks for wrap detection.
    // Strategy: for each candidate size S, check if bank S == bank 0.
    // If not, check if bank S == bank S*2 (higher bit doesn't matter).
    // This catches GMod2-like carts where control bits make bank 64+
    // look different from bank 0, but bank 128==256==512 (bit 7+8 same effect).

    cart_switch_bank(0, 0);
    for (volatile int d = 0; d < 100; d++) {
    }
    uint32_t ref = hash_bank();

    static const int boundaries[] = { 8, 16, 32, 64, 128, 256, 512 };
    uint32_t hashes[7];
    LOG("[CartBase] Bank 0 hash: %08X\n", ref);

    // Collect all hashes first
    for (int b = 0; b < 7; b++) {
        cart_switch_bank(boundaries[b], 0);
        for (volatile int d = 0; d < 100; d++) {
        }
        hashes[b] = hash_bank();
        LOG("[CartBase] Bank %d hash: %08X%s\n", boundaries[b], hashes[b], (hashes[b] == ref) ? " == bank0" : "");
    }
    cart_switch_bank(0, 0);

    // Find wrap: first boundary where bank N == bank 0
    int result = 512;
    for (int b = 0; b < 7; b++) {
        if (hashes[b] == ref) {
            result = boundaries[b];
            break;
        }
    }

    // If no direct wrap found, check from the top:
    // if bank 256 == bank 512, the top bit doesn't matter → max 256 banks
    // if bank 128 == bank 256, next bit also doesn't matter → max 128 banks
    // etc. But stop when hashes diverge.
    if (result == 512) {
        for (int b = 5; b >= 0; b--) { // 256 down to 8
            if (hashes[b] == hashes[b + 1]) {
                result = boundaries[b];
            } else {
                break;
            }
        }
    }

    LOG("[CartBase] Bank wrap at %d\n", result);
    return result;
}

// =========================================================================
//  Bank scanning
// =========================================================================

// True if the 8K at addr reads all $FF. Reads in 256-byte steps and stops at
// the first step with data, like the byte loop in the firmware.
static bool chip_empty(uint16_t addr)
{
    uint8_t buf[256];
    for (int offs = 0; offs < 0x2000; offs += 256) {
        C64_READ((uint16_t)(addr + offs), buf, 256);
        for (int i = 0; i < 256; i++) {
            if (buf[i] != 0xFF) {
                return false;
            }
        }
    }
    return true;
}

void cart_scan_all_banks(uint8_t *status_lo, uint8_t *status_hi, int max_banks, bool has_romh, uint16_t romh_addr)
{
    for (int bank = 0; bank < max_banks; bank++) {
        cart_switch_bank(bank, 0);

        // Check ROML ($8000-$9FFF)
        bool roml_empty = chip_empty(0x8000);
        status_lo[bank] = roml_empty ? BANK_EMPTY : BANK_FILLED;

        if (has_romh) {
            bool romh_empty = chip_empty(romh_addr);
            status_hi[bank] = romh_empty ? BANK_EMPTY : BANK_FILLED;
        }

        if (bank < 4 || (bank & 15) == 0) {
            if (has_romh) {
                LOG("[CartBase] Bank %3d: LO=%s HI=%s\n", bank, status_lo[bank] == BANK_EMPTY ? "empty" : "data",
                    status_hi[bank] == BANK_EMPTY ? "empty" : "data");
            } else {
                LOG("[CartBase] Bank %3d: %s\n", bank, status_lo[bank] == BANK_EMPTY ? "empty" : "data");
            }
        }
    }
}

// =========================================================================
//  Endian helpers (big-endian CRT format)
// =========================================================================

void cart_put_word(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

void cart_put_dword(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

uint16_t cart_get_word(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }

uint32_t cart_get_dword(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// =========================================================================
//  Bank map drawing (shared by Dumper + Writer UIs)
// =========================================================================

int cart_bank_color(uint8_t status)
{
    switch (status) {
    case BANK_UNKNOWN:
        return 11; // dark gray
    case BANK_EMPTY:
        return 12; // medium gray
    case BANK_FILLED:
        return 7; // yellow
    case BANK_ERASING:
        return 8; // orange
    case BANK_WRITING:
        return 3; // cyan
    case BANK_VERIFIED:
        return 5; // green
    case BANK_ERROR:
        return 2; // red
    default:
        return 1; // white
    }
}

// Draw a single 16-column hex grid for one chip
// label: "ROML" or "ROMH" (NULL = no label, used for 8K carts)
// Layout: "ROML 00: X X X ..." or "     10: X X X ..."


