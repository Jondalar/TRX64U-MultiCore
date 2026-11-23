// The tables of libexports.h. Each member is the function of the application that its header names, with the core's context; the differences from those functions are
// the ones the headers state (pointers where the application takes references, no context, the size-first rule, a missing message buffer).
#include "libexports.h"
#include "c64cart_api.h"
#include "cbm1541_api.h"
#include "uci_api.h"
#include "sid_api.h"
#include "tempdisk_api.h"
#include <c64_cart.h>                                                // a header of coreapp/cores, found through the include path
#include "cbm1541.h"
#include "d1541_drive.h"
#include "uci.h"
#include "sidfile.h"
#include "tempdisk.h"
#include "writeback.h"
#include "ctx.h"
#include "desc.h"
#include "board.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef LIBEXPORTS_SLOT_REGS_VAR                                       // the native test: the two registers are an array of the test
extern uint8_t LIBEXPORTS_SLOT_REGS_VAR[];
#define LIBEXPORTS_SLOT_REGS ((volatile uint8_t *)LIBEXPORTS_SLOT_REGS_VAR)
#else
#include "iomap.h"
#define LIBEXPORTS_SLOT_REGS ((volatile uint8_t *)CMD_IF_BASE)       // SLOT_BASE at +0, SLOT_ENABLE at +1 (the command interface's registers)
#endif

static CoreCtx *g_c;

// a call that may be made with no place for the reason gets one of its own
#define REASON(msg, msglen) char loc_[160]; if (!(msg) || (msglen) < 1) { (msg) = loc_; (msglen) = (int)sizeof(loc_); }

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

static uint32_t cart_ram_size(void) { return g_c && g_c->desc && desc_has_capability(g_c->desc, "cartridge") ? CART_RAM_BYTES : 0; }
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
    }
}

// What the cartridge definition of the firmware (cart_def.require / prohibit) says is not kept by c64_cart.cc: a .crt starts with the port to itself and an application's own start passes its needs
// to run_image once and keeps nothing. Both members therefore answer 0 for every cartridge.
static uint16_t cart_require(void) { return 0; }
static uint16_t cart_prohibit(void) { return 0; }
// The flash responder (flash_configure) registers the cartridge's file with the write-back service and nothing else does; a type change and a removal unregister it.
static bool cart_writable(void) { return c64_cart_active() && wb_active(); }

static void cart_eeprom_get(uint8_t out[2048]) { g_c->io->eeprom_read(EE_BYTES, out, EE_BYTES); }
static void cart_eeprom_put(const uint8_t data[2048])
{
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

// ---- cbm1541 (cbm1541_api.h) -------------------------------------------------------------------------------------------------------------------------

static CbmDrive *drive_a(void) { return g_c ? cbm1541_drive(g_c) : 0; }

static const char *drv_slot_spec(void) { return cbm1541_slot_spec(g_c); }
static int drv_slot_mount(const char *spec, char *msg, int msglen) { REASON(msg, msglen); return cbm1541_slot_mount(g_c, spec, msg, msglen); }
static int drv_slot_eject(char *msg, int msglen) { REASON(msg, msglen); return cbm1541_slot_eject(g_c, msg, msglen); }

static bool drv_info(int slot, Cbm1541Info *info)
{
    CbmDrive *d = slot == 0 && info && info->size >= sizeof(info->size) ? drive_a() : 0;
    if (!d) {
        return false;
    }
    Cbm1541Info all;
    memset(&all, 0, sizeof(all));
    all.size = sizeof(all);
    all.type = d->type();                                             // DT_* and CBM1541_TYPE_* are the same numbers
    all.powered = d->powered() ? 1 : 0;
    all.unit = (uint8_t)d->bus_id();
    all.mode = (uint8_t)d->mount_mode();                              // MM_* and CBM1541_MODE_* are the same numbers
    all.image_path = d->image_path();
    all.image_name = d->image_name();
    all.last_dir = d->last_dir();
    all.swap_delay_ms = d->swap_delay_ms();
    uint32_t have = info->size;                                       // the caller's sizeof: the members it knows are filled, size stays its own
    memcpy(info, &all, have < sizeof(all) ? have : sizeof(all));
    info->size = have;
    return true;
}

static uint32_t drv_image_size(int slot) { return slot == 0 && drive_a() ? 0x10000u : 0; }
static void drv_image_read(int slot, uint32_t off, uint8_t *dst, uint32_t n)
{
    if (slot != 0 || !drive_a() || off > 0x10000u || n > 0x10000u - off) {
        memset(dst, 0, n);
        return;
    }
    g_c->io->ddr_abs_read(DRIVE_A_AREA + off, dst, n);
}
static void drv_image_write(int slot, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (slot == 0 && drive_a() && off <= 0x10000u && n <= 0x10000u - off) {
        g_c->io->ddr_abs_write(DRIVE_A_AREA + off, src, n);
    }
}

static bool drv_set_options(const Cbm1541Options *opt)
{
    CbmDrive *d = opt ? drive_a() : 0;
    if (!d) {
        return false;
    }
    d->set_twist(opt->track_twist);
    d->set_align(opt->align_tracks != 0);
    d->set_reset_with_c64(opt->reset_with_c64 != 0);
    d->set_freeze(opt->freeze_in_menu != 0);
    d->set_geos(opt->geos_protection);
    return true;
}

static int drv_create(const char *path, int format, const char *name, char *msg, int msglen)
{
    REASON(msg, msglen);
    CbmDrive *d = drive_a();
    if (!d) {
        snprintf(msg, msglen, "No drive: the core has none.");
        return 1;
    }
    return d->create(path, format, name, msg, msglen);
}
static bool drv_service(void) { return cbm1541_service(g_c); }
static int drv_media_flush(char *msg, int msglen) { REASON(msg, msglen); return cbm1541_media_flush(g_c, msg, msglen); }
static bool drv_media_pending(void) { return cbm1541_media_pending(g_c); }

static const Cbm1541Api g_drv = {
    sizeof(Cbm1541Api), drv_slot_spec, drv_slot_mount, drv_slot_eject, drv_info, drv_image_size, drv_image_read, drv_image_write, drv_set_options, drv_create,
    drv_service, drv_media_flush, drv_media_pending,
};

// ---- uci (uci_api.h) ---------------------------------------------------------------------------------------------------------------------------------

// The callbacks of the core's library arrive as UciHooks; uci.cc takes a UciHost with the context in front, so each one is adapted. A member past the caller's `size` is null.
static const UciHooks *g_hooks;
#define HOOK(m) (g_hooks && g_hooks->size >= offsetof(UciHooks, m) + sizeof(g_hooks->m) && g_hooks->m)

static int uh_reu(CoreCtx *, uint32_t *size, uint32_t *offset, char *img, int imgn, char *busy, int busyn)
{
    return HOOK(reu) ? g_hooks->reu(size, offset, img, imgn, busy, busyn) : -2;
}
static bool uh_drive_info(CoreCtx *, int idx, uint8_t *type, uint8_t *addr, bool *power)
{
    return HOOK(drive_info) && g_hooks->drive_info(idx, type, addr, power);
}
static int uh_drive_do(CoreCtx *, int idx, int what, const char *path, char *msg, int msglen)
{
    if (HOOK(drive_do)) {
        return g_hooks->drive_do(idx, what, path, msg, msglen);
    }
    snprintf(msg, msglen, "This machine has no drive.");
    return 1;
}
static int uh_sid_map(CoreCtx *, uint8_t *data) { return HOOK(sid_map) ? g_hooks->sid_map(data) : 0; }
static bool uh_ef_clear(CoreCtx *, uint8_t bank, bool high) { return HOOK(ef_clear) && g_hooks->ef_clear(bank, high); }
static void uh_ramdrive(CoreCtx *, int n, int *type, uint32_t *size, uint32_t *base)
{
    if (HOOK(ramdrive)) {
        g_hooks->ramdrive(n, type, size, base);
    } else {
        *type = 0;
        *size = 0;
        *base = 0;
    }
}
static void uh_palette(CoreCtx *, int what, uint8_t idx, uint8_t (*rgb)[3])
{
    if (HOOK(palette)) {
        g_hooks->palette(what, idx, rgb);
    }
}

static void uci_api_start(const UciHooks *hooks)
{
    static UciHost host;
    if (!hooks || g_hooks) {                                          // no hooks, or the interface was started already (uci_start itself does nothing the second time)
        return;
    }
    g_hooks = hooks;
    host.reu = uh_reu;
    host.drive_info = uh_drive_info;
    host.drive_do = uh_drive_do;
    host.sid_map = uh_sid_map;
    host.memory_file = HOOK(memory_file) ? g_hooks->memory_file : 0;                                   // null: uci.cc's own default name
    host.ef_clear = uh_ef_clear;
    host.ramdrive = uh_ramdrive;
    host.palette = uh_palette;
    uci_start(g_c, &host);
}

// The rule of the slot calls (uci_api.h): ignored unless the running core has "uci" and the board did not strike it.
static bool uci_present(void) { return g_c && g_c->desc && desc_has_capability(g_c->desc, "uci") && (!g_c->board || (g_c->board->probes & BC_UCI)); }
static uint8_t uci_slot_enable(void) { return uci_present() ? LIBEXPORTS_SLOT_REGS[1] : 0; }
static void uci_set_slot_enable(uint8_t v)
{
    if (uci_present()) {
        LIBEXPORTS_SLOT_REGS[1] = v;
    }
}
static void uci_set_slot_base(uint8_t v)
{
    if (uci_present()) {
        LIBEXPORTS_SLOT_REGS[0] = v;
    }
}

static const UciApi g_uci = { sizeof(UciApi), uci_api_start, uci_slot_enable, uci_set_slot_enable, uci_set_slot_base };

// ---- sid (sid_api.h) ---------------------------------------------------------------------------------------------------------------------------------

static bool sid_t_parse(const uint8_t *head, uint32_t n, const char *name, bool ext_mus, struct SidTune *t) { return sid_parse(head, n, name, ext_mus, t); }
static int sid_t_info_text(const struct SidTune *t, char *out, int outlen) { return sid_info_text(*t, out, outlen); }
static int sid_t_plan(struct SidTune *t, uint32_t fsize, uint16_t load_le, int song, const char **why)
{
    const char *none;                                                 // sid_plan writes *why always; the header allows null
    return sid_plan(t, fsize, load_le, song, why ? why : &none);
}
static int sid_t_requests(const struct SidTune *t, struct SidReq out[3]) { return sid_requests(*t, out); }
static bool sid_t_mus_stereo(const struct SidTune *t, uint32_t load_end, uint8_t (*rd)(void *, uint32_t), void *user, uint32_t *first_end)
{
    return sid_mus_stereo(*t, load_end, rd, user, first_end);
}
static void sid_t_mus_header(struct SidTune *t, bool stereo) { sid_mus_header(t, stereo); }

static const SidApi g_sid = { sizeof(SidApi), sid_t_parse, sid_t_info_text, sid_t_plan, sid_t_requests, sid_t_mus_stereo, sid_t_mus_header };

// ---- tempdisk (tempdisk_api.h) -------------------------------------------------------------------------------------------------------------------------

static void temp_set(bool auto_cleanup, bool subfolders) { tempdisk_set(auto_cleanup, subfolders); }

static const TempDiskApi g_temp = { sizeof(TempDiskApi), temp_set };

void libexports_register(LibLoader &ld, CoreCtx *c)
{
    g_c = c;
    g_hooks = 0;
    ld.add_export("c64cart", 1, &g_cart);
    ld.add_export("cbm1541", 1, &g_drv);
    ld.add_export("uci", 1, &g_uci);
    ld.add_export("sid", 1, &g_sid);
    ld.add_export("tempdisk", 1, &g_temp);
}
