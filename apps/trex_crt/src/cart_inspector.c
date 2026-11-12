/*
 * cart_inspector.c
 *
 *  Cart Inspector — hex viewer for examining physical cartridges using DMA
 *  access to the C64 bus. Port of the firmware CartInspector
 *  (software/io/c64/cart_inspector.cc).
 */

#include "cart_inspector.h"
#include "trex_ui.h"

#define LINES_VISIBLE  16
#define BYTES_PER_LINE 16

// Window layout (23 rows, y=0..22):
//   y=0:      Header (GAME/EXROM, $01, bank)
//   y=1..21:  Hex view (21 lines x 8 bytes)
//   y=22:     Footer (key help)

static char petscii_printable(uint8_t c)
{
    if (c >= 0x20 && c <= 0x7E)
        return (char)c;
    if (c >= 0xC0 && c <= 0xFE)
        return (char)(c - 0x60);
    return '.';
}

// What the Inspector screen shows (D9.1): the memory as hex and ASCII, one line to a row (a row of $FF is dim), and the cartridge's state at the side
static void publish(cart_inspector_t *t)
{
    uint8_t cpu_port = C64_PEEK(0x01);
    uint8_t det_game = t->base.det_game;
    uint8_t det_exrom = t->base.det_exrom;

    const char *cart_type;
    if (det_game == 0 && det_exrom == 0)
        cart_type = "16K cartridge";
    else if (det_game == 1 && det_exrom == 0)
        cart_type = "8K cartridge";
    else if (det_game == 0 && det_exrom == 1)
        cart_type = "Ultimax";
    else
        cart_type = "Off";
    if (t->ef3) {
        UI_SET(i_type, "EasyFlash 3");
        UI_SET(i_slot, "%d", C64_PEEK(EF3_SLOT_REG) & 7);   // $DE01 reads back the slot on an EF3: read it for every refresh
    } else {
        UI_SET(i_type, "%s", cart_type);
        UI_SET(i_slot, "-");
    }
    UI_SET(i_bank, "$%02X", t->bank_reg);
    UI_SET(i_cpu, "$%02X", cpu_port);
    UI_SET(i_de00, "$%02X", t->bank_reg);
    UI_SET(i_pins, "%d / %d", det_game, det_exrom);
    UI_SET(chip, "BANK %02X", t->bank_reg);
    UI_SET(type, "%s", t->ef3 ? "EasyFlash 3" : cart_type);

    static char buf[sizeof(g_ui.grid)];
    int pos = u2p_snprintf(buf, (int)sizeof(buf), "~ ADDRESS   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   ASCII\n");
    uint16_t addr = t->view_addr;
    for (int y = 0; y < LINES_VISIBLE; y++) {
        uint8_t row[BYTES_PER_LINE];
        C64_READ(addr, row, BYTES_PER_LINE);
        bool blank = true;
        char asc[BYTES_PER_LINE + 1];
        for (int k = 0; k < BYTES_PER_LINE; k++) {
            blank = blank && row[k] == 0xFF;
            asc[k] = petscii_printable(row[k]);
        }
        asc[BYTES_PER_LINE] = 0;
        pos += u2p_snprintf(buf + pos, (int)sizeof(buf) - pos, "%s $%04X   ", blank ? "~" : "", addr);
        for (int k = 0; k < BYTES_PER_LINE; k++)
            pos += u2p_snprintf(buf + pos, (int)sizeof(buf) - pos, "%02X ", row[k]);
        pos += u2p_snprintf(buf + pos, (int)sizeof(buf) - pos, "  %s\n", asc);
        addr += BYTES_PER_LINE;
    }
    UI_SET(grid, "%s", buf);
}

static void draw_header(cart_inspector_t *t) { publish(t); }
static void draw_hex_view(cart_inspector_t *t) { (void)t; }
static void draw_footer(cart_inspector_t *t) { (void)t; }

void cart_inspector_redraw(cart_inspector_t *t)
{
    publish(t);
}

// The hex boxes leave the screen they found; the SDK dialogs call this when
// they close. Open the window again, which clears it, and draw everything.
static void rebuild(void *ctx)
{
    cart_inspector_t *t = (cart_inspector_t *)ctx;
    if (t->window)
        window_close(t->window);
    t->window = window_open();
    cart_inspector_redraw(t);
}

static void cmd_goto(cart_inspector_t *t)
{
    int addr = ui_hex_input("Go to address (hex):", 4);
    if (addr >= 0) {
        t->view_addr = (uint16_t)(addr & 0xFFF0);
        cart_inspector_redraw(t);
    }
}

static void cmd_write(cart_inspector_t *t)
{
    int addr = ui_hex_input("Write addr (hex):", 4);
    if (addr < 0)
        return;
    int val = ui_hex_input("Write value (hex):", 2);
    if (val < 0)
        return;
    C64_POKE((uint16_t)addr, (uint8_t)val);
    cart_inspector_redraw(t);
}

// Logs the first bytes at $8000, $A000 and $E000 (8 at start-up, 4 after a $01 change)
static void log_key_addresses(int count)
{
    static const uint16_t addrs[3] = { 0x8000, 0xA000, 0xE000 };
    for (int a = 0; a < 3; a++) {
        uint8_t p[8];
        C64_READ(addrs[a], p, count);
        if (count == 8) {
            LOG("[CartInsp] $%04X: %02X %02X %02X %02X %02X %02X %02X %02X\n", addrs[a], p[0], p[1], p[2], p[3], p[4],
                p[5], p[6], p[7]);
        } else {
            LOG("[CartInsp] $%04X: %02X %02X %02X %02X\n", addrs[a], p[0], p[1], p[2], p[3]);
        }
    }
}

static void cmd_set_01(cart_inspector_t *t)
{
    int val = ui_hex_input("Set $01 (hex):", 2);
    if (val >= 0) {
        C64_POKE(0x01, (uint8_t)val);
        LOG("[CartInsp] $01 = $%02X\n", val);
        log_key_addresses(4);
        cart_inspector_redraw(t);
    }
}

static void cmd_set_bank(cart_inspector_t *t)
{
    int val = ui_hex_input("Set $DE00 (hex):", 2);
    if (val >= 0) {
        t->bank_reg = (uint8_t)val;
        cart_switch_bank(t->bank_reg, 0);
        LOG("[CartInsp] Bank = $%02X (%d)\n", val, val);
        cart_inspector_redraw(t);
    }
}

static void change_bank(cart_inspector_t *t, int delta)
{
    t->bank_reg += delta;
    cart_switch_bank(t->bank_reg, 0);
    LOG("[CartInsp] Bank = $%02X (%d)\n", t->bank_reg, t->bank_reg);
    cart_inspector_redraw(t);
}

// True if the EAPI header of bank 0 ROMH ($1800: "eapi" and a 0-terminated 16-byte driver name) is visible at addr
// and names the EasyFlash 3 driver. Reads only: an EasyFlash in 16K mode shows it at $B800, in Ultimax at $F800.
static bool eapi_names_ef3(uint16_t addr)
{
    static const uint8_t signature[4] = { 0x65, 0x61, 0x70, 0x69 };
    uint8_t ef3_name[11];
    uint8_t header[4 + 16];
    C64_READ(addr, header, sizeof(header));
    cart_ascii_to_petscii(ef3_name, "MX29LV640EB", sizeof(ef3_name));

    bool eapi = (memcmp(header, signature, sizeof(signature)) == 0);
    char name[17];
    cart_petscii_to_ascii(name, header + 4, 16);
    LOG("[CartInsp] $%04X: %s%s\n", addr, eapi ? "EAPI " : "no EAPI", eapi ? name : "");
    return eapi && memcmp(header + 4, ef3_name, sizeof(ef3_name)) == 0;
}

// =========================================================================
//  init / deinit / poll
// =========================================================================

void cart_inspector_init(cart_inspector_t *t)
{
    memset(t, 0, sizeof(*t));
    t->view_addr = 0x8000;

    t->window = window_open();
    window_clear(t->window);
    u2p_ui_set_redraw(rebuild, t);

    // Use shared bus setup from CartBase (on our core also the port checks and the Normal mode)
    if (!cart_session_open(false)) {
        t->failed = true; // nothing is read without the port set up: the message, then any key leaves
        UI_SET(status, "%s", cart_session_status());
        UI_SET(grid, "~%s", cart_session_status());
        return;
    }

    // The firmware printed and displayed GAME/EXROM without ever reading
    // them (uninitialised members); read the real lines instead.
    cart_detect_present(&t->base);
    LOG("[CartInsp] GAME=%d EXROM=%d\n", t->base.det_game, t->base.det_exrom);

    // Start at bank 0 — do NOT read $DE00 (reads can change banking state)
    t->bank_reg = 0;
    cart_switch_bank(0, 0);

    // EasyFlash 3, in both states it can be found in and without writing to $DE01, which is a bank register on
    // every other cartridge: showing its system area it has the EF-Directory in bank $10, and in a game slot the
    // EAPI of bank 0 names the MX29LV640EB driver. The menu has no EAPI, so that alone would miss it.
    t->ef3 = cart_ef3_dir_visible() || eapi_names_ef3(0xB800) || eapi_names_ef3(0xF800);

    // Log first bytes at key addresses
    log_key_addresses(8);

    cart_inspector_redraw(t);
}

void cart_inspector_deinit(cart_inspector_t *t)
{
    u2p_ui_set_redraw(NULL, NULL);
    cart_session_close();
    window_close(t->window);
    t->window = NULL;
}

int cart_inspector_poll(cart_inspector_t *t, int c)
{
    if (t->failed)
        return MENU_CLOSE;
    switch (c) {
    case KEY_DOWN:
        t->view_addr += BYTES_PER_LINE;
        draw_header(t);
        draw_hex_view(t);
        break;
    case KEY_UP:
        t->view_addr -= BYTES_PER_LINE;
        draw_header(t);
        draw_hex_view(t);
        break;
    case KEY_PAGEDOWN:
        t->view_addr += BYTES_PER_LINE * LINES_VISIBLE;
        draw_header(t);
        break;
    case KEY_PAGEUP:
        t->view_addr -= BYTES_PER_LINE * LINES_VISIBLE;
        draw_header(t);
        break;
    case KEY_HOME:
        t->view_addr = 0x8000;
        draw_header(t);
        break;
    case KEY_RIGHT:
        change_bank(t, 1);
        break;
    case KEY_LEFT:
        change_bank(t, -1);
        break;
    case KEY_F1:
        cmd_goto(t);
        break;
    case KEY_F3:
        cmd_write(t);
        break;
    case KEY_F5:
        cmd_set_01(t);
        break;
    case KEY_F7:
        cmd_set_bank(t);
        break;
    case KEY_ESCAPE:
    case KEY_BACK:
    case KEY_BREAK:
        return MENU_CLOSE;
    default:
        break;
    }
    return 0;
}
