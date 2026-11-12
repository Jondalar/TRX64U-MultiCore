/*
 * cart_inspector.c
 *
 *  Cart Inspector — hex viewer for examining physical cartridges using DMA
 *  access to the C64 bus. Port of the firmware CartInspector
 *  (software/io/c64/cart_inspector.cc).
 */

#include "cart_inspector.h"

#define LINES_VISIBLE  21
#define BYTES_PER_LINE 8

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

static void draw_header(cart_inspector_t *t)
{
    uint8_t cpu_port = C64_PEEK(0x01);
    uint8_t det_game = t->base.det_game;
    uint8_t det_exrom = t->base.det_exrom;

    const char *cart_type;
    if (det_game == 0 && det_exrom == 0)
        cart_type = "16K";
    else if (det_game == 1 && det_exrom == 0)
        cart_type = "8K ";
    else if (det_game == 0 && det_exrom == 1)
        cart_type = "UMX";
    else
        cart_type = "Off";

    char buf[41];
    int len;
    if (t->ef3) {
        // $DE01 reads back the slot on an EF3: read it for every header, so a write to $DE01 with F3 shows up
        len = ui_snprintf(buf, sizeof(buf), "EasyFlash 3 S%d G=%d E=%d $01=%02X B=%02X", C64_PEEK(EF3_SLOT_REG) & 7,
                          det_game, det_exrom, cpu_port, t->bank_reg);
    } else {
        len = ui_snprintf(buf, sizeof(buf), "Cart:G=%d E=%d (%s) $01=%02X B=%02X", det_game, det_exrom, cart_type,
                          cpu_port, t->bank_reg);
    }

    window_move_cursor(t->window, 0, 0);
    window_set_color(t->window, g_api->ui->color(0));
    window_reverse_mode(t->window, 0);
    window_output(t->window, buf);
    while (len < 40) {
        window_output_char(t->window, ' ');
        len++;
    }
}

static void draw_line(cart_inspector_t *t, int y, uint16_t addr)
{
    char buf[41];
    char asc[9];

    int pos = ui_snprintf(buf, sizeof(buf), "%04X: ", addr);
    for (int i = 0; i < BYTES_PER_LINE; i++) {
        uint8_t b = C64_PEEK((uint16_t)(addr + i));
        pos += ui_snprintf(buf + pos, (int)sizeof(buf) - pos, "%02X ", b);
        asc[i] = petscii_printable(b);
    }
    asc[BYTES_PER_LINE] = 0;

    window_move_cursor(t->window, 0, y);
    window_set_color(t->window, g_api->ui->color(0));
    window_reverse_mode(t->window, 0);
    window_output(t->window, buf);
    window_output(t->window, asc);
}

static void draw_hex_view(cart_inspector_t *t)
{
    uint16_t addr = t->view_addr;
    for (int y = 0; y < LINES_VISIBLE; y++) {
        draw_line(t, 1 + y, addr);
        addr += BYTES_PER_LINE;
    }
}

static void draw_footer(cart_inspector_t *t)
{
    int y = window_get_size_y(t->window) - 1;
    window_move_cursor(t->window, 0, y);
    window_set_color(t->window, g_api->ui->color(0));
    window_reverse_mode(t->window, 0);
    window_output(t->window, "F1:Go F3:Wr F5:$01 F7:$DE UD LR R/S");
}

void cart_inspector_redraw(cart_inspector_t *t)
{
    draw_header(t);
    draw_hex_view(t);
    draw_footer(t);
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
        t->view_addr = (uint16_t)(addr & 0xFFF8);
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
        window_move_cursor(t->window, 0, 1);
        window_output(t->window, cart_session_status());
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
