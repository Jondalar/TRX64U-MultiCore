// The table of the 1541 drive library (cbm1541.cc, app/lib/cbm1541): its table "cbm1541" (T239.3 D12, base 3.3), its export (base 4.0: declared in the TOML B, `exports`, reached through libslot.h). This file is part of the library: libtab.cc (from the TOML B of the app) calls cbm1541_bind once, when the loader is made.
// The bodies call the code of the library with the core's context that is given here once, so no member of a table takes one.
#include "libexports.h"
#include "libexp_util.h"
#include "cbm1541_api.h"
#include "cbm1541.h"
#include "d1541_drive.h"
#include "ctx.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static CoreCtx *g_c;

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

void cbm1541_bind(CoreCtx *c, LibLoader &)
{
    g_c = c;
}

const void *cbm1541_table(void) { return &g_drv; }
