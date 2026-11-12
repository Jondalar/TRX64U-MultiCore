/*
 * trx_lens.c - memory through a lens (trx_lens.h; T21-monitor.md §3.4.1).
 *
 * The PLA is worked out from $01 (state bus) and the cartridge's GAME/EXROM
 * lines; RAM, the ROM block and the state bus cost no bus cycle. The
 * cartridge is asked through the lens unit (freeze_resp.v LK): a look for an
 * emulated one (the decode a bus access would use, banking registers as they
 * are), one bus cycle with the select forced for one on the physical port.
 */
#include "trx_lens.h"

static lens_state_t S;

static void unpack(uint8_t *dst, const uint32_t *w, int nbytes)
{
    for (int i = 0; i < nbytes; i++) {
        dst[i] = (uint8_t)(w[i >> 2] >> (8 * (i & 3)));
    }
}

void lens_snapshot(const lens_hw_t *hw)
{
    uint32_t w[12];
    hw->sb_block(2, 0, w, 12);
    unpack(S.vic, w, 48);
    hw->sb_block(3, 0, w, 5);
    unpack(S.cia1, w, 16);
    hw->sb_block(4, 0, w, 5);
    unpack(S.cia2, w, 16);
    S.pa_out = (uint8_t)w[4];
    S.ddra = (uint8_t)(w[4] >> 16);
    uint32_t p;
    hw->sb_block(1, 2, &p, 1);
    S.ddr = (uint8_t)p;
    S.latch = (uint8_t)(p >> 8);
    S.port = (uint8_t)(p >> 16);
    uint32_t g;
    hw->sb_block(0, 0, &g, 1);
    S.game = (g >> 1) & 1;
    S.exrom = (g >> 2) & 1;
    S.cart = (g >> 3) & 1;
    uint32_t c;
    hw->sb_block(7, 0, &c, 1);
    S.cart_reg0 = (uint8_t)c;
    S.cart_reg1 = (uint8_t)(c >> 8);
}

const lens_state_t *lens_state(void) { return &S; }

enum { L_CPU, L_RAM, L_ROM, L_IO, L_CART, L_DRIVE, L_BAD };

static int eq(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

static int lens_id(const char *l)
{
    if (eq(l, "cpu")) {
        return L_CPU;
    }
    if (eq(l, "ram")) {
        return L_RAM;
    }
    if (eq(l, "rom")) {
        return L_ROM;
    }
    if (eq(l, "io")) {
        return L_IO;
    }
    if (eq(l, "cart")) {
        return L_CART;
    }
    if (eq(l, "drive8")) {
        return L_DRIVE;
    }
    return L_BAD;
}

int lens_valid(const char *l) { return lens_id(l) != L_BAD; }

/* The PLA from $01 (as the CPU reads it) and the cartridge's GAME/EXROM */
int lens_pla(uint16_t a)
{
    int lo = S.port & 1, hi = (S.port >> 1) & 1, ch = (S.port >> 2) & 1;
    int game = S.game, exrom = S.exrom;
    if (!game && exrom) { /* Ultimax */
        if (a < 0x1000) {
            return R_RAM;
        }
        if ((a >= 0x8000) && (a < 0xA000)) {
            return R_ROML;
        }
        if ((a >= 0xD000) && (a < 0xE000)) {
            return R_IO;
        }
        if (a >= 0xE000) {
            return R_ROMH;
        }
        return R_OPEN;
    }
    if ((a >= 0x8000) && (a < 0xA000)) {
        return (hi && lo && !exrom) ? R_ROML : R_RAM;
    }
    if ((a >= 0xA000) && (a < 0xC000)) {
        if (hi && lo && game) {
            return R_BASIC;
        }
        return (hi && !game && !exrom) ? R_ROMH : R_RAM;
    }
    if ((a >= 0xD000) && (a < 0xE000)) {
        if (!(lo || hi)) {
            return R_RAM;
        }
        return ch ? R_IO : R_CHAR;
    }
    if (a >= 0xE000) {
        return hi ? R_KERNAL : R_RAM;
    }
    return R_RAM;
}

static uint8_t ram8(const lens_hw_t *hw, uint16_t a)
{
    uint8_t v;
    hw->ram_read(a, &v, 1);
    return v;
}

/* One lens-unit request, 0 when it has no answer */
static uint32_t lk_ask(const lens_hw_t *hw, uint16_t a, int cls, int real)
{
    uint32_t r = hw->lk(a, cls, real);
    return (r & LK_DONE) ? r : 0;
}

/* ROML (cls 0) or ROMH (cls 1) as the cartridge answers it now. -1: not
 * readable now (a real cartridge and the machine runs). */
static int cart_byte(const lens_hw_t *hw, uint16_t a, int cls)
{
    if (!S.cart) {
        return 0xFF;
    }
    if (!hw->lk) {
        return -1;
    }
    uint32_t r = lk_ask(hw, a, cls, 0);
    if (!r) {
        return -1;
    }
    if (r & LK_EXT) {
        r = lk_ask(hw, a, cls, 1);
        return (r && !(r & LK_REFUSED)) ? (int)LK_DATA(r) : -1;
    }
    return LK_DATA(r);
}

/* $D000-$DFFF: VIC, CIA 1/2 from the state bus; IO1/IO2 through the look for
 * an emulated cartridge and the REU. The colour RAM is read through the session's window
 * where one runs (a debugger stop is one, T12 §14.8). SID and the real port's I/O are bus
 * cycles only. */
static int io_peek(const lens_hw_t *hw, uint16_t a, int sidefx)
{
    if (sidefx) {
        /* not held (the machine runs): what the state bus has, as without sidefx */
        if (hw->lk) {
            uint32_t r = lk_ask(hw, a, 2, 1);
            if (r && !(r & LK_REFUSED)) {
                return LK_DATA(r);
            }
        } else if (hw->bus_read) {
            int v = hw->bus_read(a);
            if (v >= 0) {
                return v;
            }
        }
    }
    if ((a >= 0xD800) && (a < 0xDC00) && hw->session_read) {
        int v = hw->session_read(a);
        if (v >= 0) {
            return v;
        }
    }
    if (a < 0xD400) {
        return (a & 0x3F) < 0x30 ? S.vic[a & 0x3F] : 0xFF;
    }
    if ((a >= 0xDC00) && (a < 0xDD00)) {
        return S.cia1[a & 15];
    }
    if ((a >= 0xDD00) && (a < 0xDE00)) {
        return S.cia2[a & 15];
    }
    if ((a >= 0xDE00) && hw->lk) {
        uint32_t r = lk_ask(hw, a, a < 0xDF00 ? 2 : 3, 0);
        return (r && !(r & LK_EXT)) ? (int)LK_DATA(r) : -1;
    }
    return -1;
}

int lens_peek(const lens_hw_t *hw, const char *lens, uint16_t a, int sidefx)
{
    switch (lens_id(lens)) {
    case L_DRIVE:
        return hw->drive_peek(a);
    case L_RAM:
        return ram8(hw, a);
    case L_ROM:
        if ((a >= 0xA000) && (a < 0xC000)) {
            return hw->rom((uint16_t)(0x4000 + (a - 0xA000)));
        }
        if (a >= 0xE000) {
            return hw->rom((uint16_t)(0x2000 + (a - 0xE000)));
        }
        if ((a >= 0xD000) && (a < 0xE000)) {
            return hw->rom(a & 0x0FFF);
        }
        return ram8(hw, a);
    case L_IO:
        return ((a >= 0xD000) && (a < 0xE000)) ? io_peek(hw, a, sidefx) : ram8(hw, a);
    case L_CART:
        if ((a >= 0x8000) && (a < 0xA000)) {
            return cart_byte(hw, a, 0);
        }
        if (((a >= 0xA000) && (a < 0xC000)) || (a >= 0xE000)) {
            return cart_byte(hw, a, 1);
        }
        return ram8(hw, a);
    default:
        break;
    }
    switch (lens_pla(a)) {
    case R_BASIC: return hw->rom((uint16_t)(0x4000 + (a - 0xA000)));
    case R_KERNAL: return hw->rom((uint16_t)(0x2000 + (a - 0xE000)));
    case R_CHAR: return hw->rom(a & 0x0FFF);
    case R_IO: return io_peek(hw, a, sidefx);
    case R_ROML: return cart_byte(hw, a, 0);
    case R_ROMH: return cart_byte(hw, a, 1);
    case R_OPEN: return 0xFF;
    default: return ram8(hw, a);
    }
}

int lens_read(const lens_hw_t *hw, const char *lens, uint16_t a, uint8_t *dst, uint32_t len, int sidefx)
{
    int bad = 0;
    if (lens_id(lens) == L_BAD) {
        return (int)len;
    }
    int id = lens_id(lens);
    if (id == L_CPU || id == L_IO || id == L_CART) { /* the others read no state bus */
        lens_snapshot(hw);
    }
    if (id == L_RAM) {
        uint32_t n = 0;
        while (n < len) {
            uint32_t chunk = 0x10000u - (uint16_t)(a + n);
            if (chunk > len - n) {
                chunk = len - n;
            }
            hw->ram_read((uint16_t)(a + n), dst + n, (int)chunk);
            n += chunk;
        }
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        int v = lens_peek(hw, lens, (uint16_t)(a + i), sidefx);
        if (v < 0) {
            bad++;
            v = 0;
        }
        dst[i] = (uint8_t)v;
    }
    return bad;
}
