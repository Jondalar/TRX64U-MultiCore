// The table of the UCI library (uci.cc, uci_slot.cc): its table "uci" (T239.3 D12, base 3.3), answered by api->lib->find under its export name. This file is part of the library: libtab.cc (from the TOML B of the app) calls uci_bind once, when the loader is made.
// The bodies call the code of the library with the core's context that is given here once, so no member of a table takes one.
#include "libexports.h"
#include "libexp_util.h"
#include "uci_api.h"
#include "uci.h"
#include "uci_slot.h"
#include "ctx.h"
#include "libload.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static CoreCtx *g_c;

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

// The rule of the slot calls (uci_api.h): ignored unless the running core has "uci" and the board did not strike it. The registers are the UCI driver's (uci_slot.cc).
static uint8_t uci_slot_enable(void) { return uci_slot_present(g_c) ? uci_slot_read_enable() : 0; }
static void uci_set_slot_enable(uint8_t v)
{
    if (uci_slot_present(g_c)) {
        uci_slot_write_enable(v);
    }
}
static void uci_set_slot_base(uint8_t v)
{
    if (uci_slot_present(g_c)) {
        uci_slot_write_base(v);
    }
}

static const UciApi g_uci = { sizeof(UciApi), uci_api_start, uci_slot_enable, uci_set_slot_enable, uci_set_slot_base };

void uci_bind(CoreCtx *c, LibLoader &ld)
{
    g_c = c;
    g_ld = &ld;
    g_hooks = 0;
    g_hooks_owner = -1;
    g_uci_started = false;
    ld.set_discard_notify(hooks_image_gone);
    ld.add_export("uci", 1, &g_uci);
}
