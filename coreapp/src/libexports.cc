// The tables of libexports.h. Each member is the function of the application that its header names, with the core's context; the differences from those functions are
// the ones the headers state (pointers where the application takes references, no context, the size-first rule, a missing message buffer).
#include "libexports.h"
#include "c64cart_api.h"
#include "cbm1541_api.h"
#include "uci_api.h"
#include "sid_api.h"
#include "tempdisk_api.h"
#include "cbmprg_api.h"
#include "c64_cart.h"                                                // a header of coreapp/cores, found through the include path
#include "cbm1541.h"
#include "d1541_drive.h"
#include "uci.h"
#include "sidfile.h"
#include "tempdisk.h"
#include "cbm_prg.h"                                                 // app/loaders
#include "writeback.h"
#include "ctx.h"
#include "appsvc.h"
#include "desc.h"
#include "board.h"
#include <stddef.h>
#include <stdint.h>
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
    }
}

// What the cartridge definition of the firmware (cart_def.require / prohibit) says is not kept by c64_cart.cc: a .crt starts with the port to itself and an application's own start passes its needs
// to run_image once and keeps nothing. Both members therefore answer 0 for every cartridge.
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

// ---- cbm1541 (cbm1541_api.h) -------------------------------------------------------------------------------------------------------------------------

// A member of task class "any" never makes the drive (that runs CbmDrive::attach, which writes the drive's registers, on a task that does not hold the core lock): it answers from the drive as
// it is (drive_made) or from the window (drive_window). The members of class lock make the drive through cbm1541_drive (drive_a).
static CbmDrive *drive_made(void)
{
    CbmDrive *d = g_c ? (CbmDrive *)g_c->drive : 0;
    return d && d->attached() ? d : 0;
}
static CbmDrive *drive_a(void) { return g_c ? cbm1541_drive(g_c) : 0; }
static bool drive_window(void) { return g_c && g_c->io && g_c->io->drv_present(); }

static const char *drv_slot_spec(void) { return cbm1541_slot_spec(g_c); }
static int drv_slot_mount(const char *spec, char *msg, int msglen) { REASON(msg, msglen); return cbm1541_slot_mount(g_c, spec, msg, msglen); }
static int drv_slot_eject(char *msg, int msglen) { REASON(msg, msglen); return cbm1541_slot_eject(g_c, msg, msglen); }

static bool drv_info(int slot, Cbm1541Info *info)
{
    CbmDrive *d = slot == 0 && info && info->size >= sizeof(info->size) ? drive_made() : 0;
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

static uint32_t drv_image_size(int slot) { return slot == 0 && drive_window() ? 0x10000u : 0; }
static void drv_image_read(int slot, uint32_t off, uint8_t *dst, uint32_t n)
{
    if (slot != 0 || !drive_window() || off > 0x10000u || n > 0x10000u - off) {
        memset(dst, 0, n);
        return;
    }
    g_c->io->ddr_abs_read(DRIVE_A_AREA + off, dst, n);
}
static void drv_image_write(int slot, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (slot == 0 && drive_window() && off <= 0x10000u && n <= 0x10000u - off) {
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

// The reset of drive A when the core has made one, as the C64's reset does it (the check of "Resets when C64 resets" is the caller's); it never makes a drive: -1 and no reason without one.
static int drv_reset(char *msg, int msglen)
{
    CbmDrive *d = g_c ? (CbmDrive *)g_c->drive : 0;
    if (!d) {
        return -1;
    }
    REASON(msg, msglen);
    return d->reset(msg, msglen);
}

static const Cbm1541Api g_drv = {
    sizeof(Cbm1541Api), drv_slot_spec, drv_slot_mount, drv_slot_eject, drv_info, drv_image_size, drv_image_read, drv_image_write, drv_set_options, drv_create,
    drv_service, drv_media_flush, drv_media_pending, drv_reset,
};

// ---- uci (uci_api.h) ---------------------------------------------------------------------------------------------------------------------------------

// The callbacks of the core's library arrive as UciHooks; uci.cc takes a UciHost with the context in front, so each one is adapted. A member past the caller's `size` is null.
static const UciHooks *g_hooks;
static int g_hooks_owner = -1;                                        // the slot of the image whose code was running when the table was given; -1: none
static bool g_uci_started;                                            // uci_start ran: it takes its host once, and the host below stays for good
static char g_memory_file[96];                                        // a copy: the hooks' own string lies in the library's image
static LibLoader *g_ld;
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

// The table lives in the image of the library that gave it: when that image goes (an unload, a failed load), the adapters stop calling through it and a library loaded later may
// give its own table. uci_start itself is a one-time call; the host it was given stays, and the adapters read g_hooks at every call.
static void hooks_image_gone(int handle, uint32_t image_addr, uint32_t image_size)
{
    if (!g_hooks) {
        return;
    }
    uint32_t at = (uint32_t)(uintptr_t)g_hooks;                       // the CPU's view of the address: the pointer itself on the board, its low word in the tests (LibHost::addr_of)
    bool inside = at >= image_addr && at - image_addr < image_size;
    if (inside || (g_hooks_owner >= 0 && handle == g_hooks_owner)) {
        g_hooks = 0;
        g_hooks_owner = -1;
    }
}

static void uci_api_start(const UciHooks *hooks)
{
    static UciHost host;
    if (!hooks || g_hooks) {                                          // no hooks, or a table is in use already
        return;
    }
    g_hooks = hooks;
    g_hooks_owner = g_ld ? g_ld->current() : -1;
    host.reu = uh_reu;
    host.drive_info = uh_drive_info;
    host.drive_do = uh_drive_do;
    host.sid_map = uh_sid_map;
    g_memory_file[0] = 0;
    if (HOOK(memory_file)) {
        snprintf(g_memory_file, sizeof(g_memory_file), "%s", g_hooks->memory_file);
    }
    host.memory_file = g_memory_file[0] ? g_memory_file : 0;                                           // null: uci.cc's own default name
    host.ef_clear = uh_ef_clear;
    host.ramdrive = uh_ramdrive;
    host.palette = uh_palette;
    if (!g_uci_started) {
        g_uci_started = true;
        uci_start(g_c, &host);
    }
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

// ---- cbmprg (cbmprg_api.h) ---------------------------------------------------------------------------------------------------------------------------

// The machine of the caller as the parameters of cbm_prg.cc: the members its `size` covers are read, the ones past it are 0 (members are only appended inside a major). A size that does
// not reach max_size, the last member of the first layout, is no machine. The memory is the running core's, through its io.
static bool prg_params(const CbmPrgMachine *m, CbmPrgParams *p)
{
    if (!m || !g_c || !g_c->io || m->size < offsetof(CbmPrgMachine, max_size) + sizeof(m->max_size)) {
        return false;
    }
    CbmPrgMachine all;
    memset(&all, 0, sizeof(all));
    memcpy(&all, m, m->size < sizeof(all) ? m->size : sizeof(all));
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < CBM_BASIC_STARTS_MAX; i++) {
        p->basic_starts[i] = all.basic_starts[i];
    }
    p->nbasic_starts = all.nbasic_starts;
    p->txttab = all.txttab;
    p->txttab_min = all.txttab_min;
    p->txttab_max = all.txttab_max;
    p->vartab = all.vartab;
    p->arytab = all.arytab;
    p->strend = all.strend;
    p->kbd_buffer = all.kbd_buffer;
    p->kbd_count = all.kbd_count;
    p->ready_ms = all.ready_ms;
    p->p00_header = all.p00_header;
    p->min_size = all.min_size;
    p->max_size = all.max_size;
    p->mem_ddr = all.mem_ddr;
    return true;
}

static int prg_place(const uint8_t *data, uint32_t n, const CbmPrgMachine *m, bool is_p00, CbmPrgPlaced *out, char *msg, int msglen)
{
    REASON(msg, msglen);
    msg[0] = 0;
    CbmPrgParams p;
    if (!data || !out) {
        snprintf(msg, msglen, "No program given.");
        return 1;
    }
    if (!prg_params(m, &p)) {
        memset(out, 0, sizeof(*out));
        snprintf(msg, msglen, "No machine given.");
        return 1;
    }
    CbmPrgResult r;
    int rc = cbm_prg_place(g_c->io, data, n, p, is_p00, &r, msg, msglen);
    out->load = r.load;
    out->end = r.end;
    out->basic = r.basic;
    out->start = r.start;
    return rc;
}
static void prg_type_start(const CbmPrgMachine *m, const CbmPrgPlaced *r)
{
    CbmPrgParams p;
    if (r && prg_params(m, &p)) {
        CbmPrgResult res;
        res.load = r->load;
        res.end = r->end;
        res.basic = r->basic;
        res.start = r->start;
        cbm_prg_type_start(g_c->io, p, res);
    }
}
static int prg_script_check(const char *text, char *msg, int msglen)
{
    REASON(msg, msglen);
    msg[0] = 0;
    return cbm_prg_script_check(text ? text : "", msg, msglen);
}
static int prg_script_step(const CbmPrgMachine *m, const char *script, int *pos)
{
    CbmPrgParams p;
    return script && pos && prg_params(m, &p) ? cbm_prg_script_step(g_c->io, p, script, pos) : 0;
}
static uint16_t prg_relink(uint8_t *prog, uint32_t n, uint16_t from, uint16_t to) { return cbm_prg_relink(prog, n, from, to); }
static bool prg_key_for(uint8_t petscii, uint8_t *idx, bool *shift)
{
    CbmKey k;
    if (!idx || !shift || !cbm_key_for(petscii, &k)) {
        return false;
    }
    *idx = k.idx;
    *shift = k.shift;
    return true;
}
static void prg_mem_write(const CbmPrgMachine *m, uint16_t addr, const void *data, uint32_t n)
{
    CbmPrgParams p;
    if (data && prg_params(m, &p)) {
        cbm_prg_mem_write(g_c->io, p, addr, data, n);
    }
}
static void prg_mem_read(const CbmPrgMachine *m, uint16_t addr, void *data, uint32_t n)
{
    CbmPrgParams p;
    if (!data) {
        return;
    }
    if (!prg_params(m, &p)) {
        memset(data, 0, n);
        return;
    }
    cbm_prg_mem_read(g_c->io, p, addr, data, n);
}

static const CbmPrgApi g_prg = {
    sizeof(CbmPrgApi), prg_place, prg_type_start, prg_script_check, prg_script_step, prg_relink, prg_key_for, prg_mem_write, prg_mem_read,
};

void libexports_register(LibLoader &ld, CoreCtx *c)
{
    g_c = c;
    g_ld = &ld;
    g_hooks = 0;
    g_hooks_owner = -1;
    g_uci_started = false;
    ld.set_discard_notify(hooks_image_gone);
    ld.add_export("c64cart", 1, &g_cart);
    ld.add_export("cbm1541", 1, &g_drv);
    ld.add_export("uci", 1, &g_uci);
    ld.add_export("sid", 1, &g_sid);
    ld.add_export("tempdisk", 1, &g_temp);
    ld.add_export(CBMPRG_API_NAME, CBMPRG_API_MAJOR, &g_prg);
}
