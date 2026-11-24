// The table of the C64 cartridge library (c64_cart.cc): its table "c64cart" (T239.3 D12, base 3.3), answered by api->lib->find under its export name. This file is part of the library: libtab.cc (from the TOML B of the app) calls c64cart_bind once, when the loader is made.
// The bodies call the code of the library with the core's context that is given here once, so no member of a table takes one.
#include "libexports.h"
#include "libexp_util.h"
#include "c64cart_api.h"
#include "c64_cart.h"
#include "c64_regs.h"
#include "writeback.h"
#include "appsvc.h"
#include "desc.h"
#include "ctx.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static CoreCtx *g_c;

// ---- c64cart (c64cart_api.h) -------------------------------------------------------------------------------------------------------------------------

#define CART_RAM_ABS   0x00EF0000u                    // __cart_ram_start of the linker script: the 64 KB of the port's RAM window
#define CART_RAM_BYTES 0x10000u
#define EE_BYTES       2048u                          // the GMod2's 93C86: byte 0 of the window is the dirty flag, the data starts at +2048

static bool cart_active(void) { return c64_cart_active(); }
static const char *cart_name(void) { return c64_cart_name(); }
static const char *cart_path(void) { return c64_cart_path(); }
static int cart_crt_type(void) { return c64_cart_crt_type(); }
static uint32_t cart_image_bytes(void) { return c64_cart_image_bytes(); }
static void cart_image_read(uint32_t off, uint8_t *dst, uint32_t n) { c64_cart_image_read(g_c, off, dst, n); }
static void cart_image_write(uint32_t off, const uint8_t *src, uint32_t n) { c64_cart_image_write(g_c, off, src, n); }
static int cart_for_state(const char *path, char *msg, int msglen) { REASON(msg, msglen); return c64_cart_for_state(g_c, path, msg, msglen); }

// The port of the C64 family: the capability "cartridge" alone is also listed by cores with a cartridge of their own (the Atari, the NES, the SNES, the Neo Geo), whose windows are not these;
// the core that has the port is the one the base gives its "c64" table (appsvc_c64).
static bool cart_port(void) { return g_c && g_c->desc && appsvc_c64() && desc_has_capability(g_c->desc, "cartridge"); }
static uint32_t cart_ram_size(void) { return cart_port() ? CART_RAM_BYTES : 0; }
static void cart_ram_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (off > cart_ram_size() || n > cart_ram_size() - off) {
        memset(dst, 0, n);
        return;
    }
    g_c->io->ddr_abs_read(CART_RAM_ABS + off, dst, n);
}
static void cart_ram_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    if (off <= cart_ram_size() && n <= cart_ram_size() - off) {
        g_c->io->ddr_abs_write(CART_RAM_ABS + off, src, n);
        g_c->io->wr(C64_CART_CMD, C64_CART_CMD_INVAL);                // cmd.inval of the cartridge page: the cached lines of the RAM go after every write, no caller does it (D50)
    }
}

// What the cartridge definition of the firmware (cart_def.require / prohibit) says is not kept by c64_cart.cc: a .crt starts with the port to itself and run_image takes only a name and an image. Both members therefore answer 0 for every cartridge.
static uint16_t cart_require(void) { return 0; }
static uint16_t cart_prohibit(void) { return 0; }
// The flash responder (flash_configure) registers the cartridge's file with the write-back service and nothing else does; a type change and a removal unregister it.
static bool cart_writable(void) { return c64_cart_active() && wb_active(); }

static void cart_eeprom_get(uint8_t out[2048])
{
    if (cart_port()) {
        g_c->io->eeprom_read(EE_BYTES, out, EE_BYTES);
    } else {
        memset(out, 0, EE_BYTES);                                     // a core without the window reads 0
    }
}
static void cart_eeprom_put(const uint8_t data[2048])
{
    if (!cart_port()) {
        return;
    }
    uint8_t clean = 1;
    g_c->io->eeprom_write(EE_BYTES, data, EE_BYTES);
    g_c->io->eeprom_write(0, &clean, 1);                              // the dirty flag cleared: only what the C64 writes from here on is a change (c64_cart.cc load_eeprom)
}

static int cart_run(const char *path, char *msg, int msglen) { REASON(msg, msglen); return c64_cart_run(g_c, path, msg, msglen); }
static int cart_remove(char *msg, int msglen) { REASON(msg, msglen); return c64_cart_remove(g_c, msg, msglen); }
static int cart_save(const char *path, char *msg, int msglen) { REASON(msg, msglen); return c64_cart_save(g_c, path, msg, msglen); }
static bool cart_ef_clear(uint8_t bank, bool high) { return c64_cart_ef_clear(g_c, bank, high); }
static int cart_run_image(const char *name, const uint8_t *img, char *msg, int msglen) { REASON(msg, msglen); return c64_cart_run_image(g_c, name, img, msg, msglen); }
static void cart_player_end(void) { c64_cart_player_end(g_c); }
static bool cart_player_active(void) { return c64_cart_player_active(); }

static const C64CartApi g_cart = {
    sizeof(C64CartApi), cart_active, cart_name, cart_path, cart_crt_type, cart_image_bytes, cart_image_read, cart_image_write, cart_for_state,
    cart_ram_size, cart_ram_read, cart_ram_write, cart_require, cart_prohibit, cart_writable, cart_eeprom_get, cart_eeprom_put,
    cart_run, cart_remove, cart_save, cart_ef_clear, cart_run_image, cart_player_end, cart_player_active,
};

void c64cart_bind(CoreCtx *c, LibLoader &ld)
{
    g_c = c;
    ld.add_export("c64cart", 1, &g_cart);
}
