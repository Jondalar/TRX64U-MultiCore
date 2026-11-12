#include "mediaglue.h"
#include "file_device.h"
#include "filemanager.h"
#include "file_system.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool internal_drive(const char *name)
{
    return !strcasecmp(name, "Flash");
}

static bool sniff_read(void *ctx, uint32_t lba, uint8_t *buf)
{
    return ((BlockDevice *)ctx)->read(buf, lba, 1) == RES_OK;
}

bool media_free_bytes(const char *name, uint64_t *bytes)
{
    FileDevice *d = FileDevice::find(name);
    FileSystem *fs = d ? d->get_file_info()->fs : 0;
    uint32_t freec = 0, cs = 0;
    if (!fs || fs->get_free(&freec, &cs) != FR_OK) {
        return false;
    }
    *bytes = (uint64_t)freec * cs;
    return true;
}

uint32_t media_free_heap(void)
{
    return (uint32_t)xPortGetFreeHeapSize();
}

bool media_present(const char *name)
{
    FileDevice *d = FileDevice::find(name);
    return d && d->is_ready();
}

bool media_info(const char *name, MediaInfo *mi)
{
    memset(mi, 0, sizeof(*mi));
    snprintf(mi->name, sizeof(mi->name), "%s", name);
    FileDevice *d = FileDevice::find(name);
    if (!d) {
        return false;
    }
    mi->found = true;
    mi->formattable = !internal_drive(name);
    BlockDevice *b = d->get_block_device();
    mi->ready = d->is_ready() && !(b->status() & (STA_NODISK | STA_NOINIT));
    if (!mi->ready) {
        return true;
    }
    uint32_t ss = 512, n = 0;
    b->ioctl(GET_SECTOR_SIZE, &ss);
    if (b->ioctl(GET_SECTOR_COUNT, &n) != RES_OK) {
        n = 0;
    }
    mi->sector_size = ss;
    mi->sectors = n;
    media_sniff(sniff_read, b, n, ss, &mi->sn);
    if (mi->formattable) {
        FmtGeom g;
        mi->refuse = fmt_check(b, fmt_work_size(media_free_heap()), &g);
    }
    return true;
}

// ---- the ops of the format: the host lets go of the volume, and takes the medium up again ----------------------------------------------
struct GlueCtx {
    FileDevice *dev;
};

static void say(char *why, int n, const char *s)
{
    if (why && n > 0) {
        snprintf(why, n, "%s", s);
    }
}

static bool op_detach(void *c)
{
    FileDevice *d = ((GlueCtx *)c)->dev;
    FileManager::getFileManager()->invalidate(d);      // what the USB poll does on a removal: open files of this medium are closed, mount points dropped
    d->release_volume();                               // f_unmount, the drive slot is free (detach_disk alone leaves both)
    d->detach_disk();
    return true;
}

static bool op_attach(void *c, const FmtGeom *g, bool verify, char *why, int whylen)
{
    FileDevice *d = ((GlueCtx *)c)->dev;
    d->release_volume();                               // whatever is still mounted from before (a format that stopped early): unmount before the disk goes
    d->detach_disk();                                  // harmless without a disk; a second attach_disk on a live one would complain
    if (!d->is_ready()) {
        say(why, whylen, "the medium is gone");
        return false;
    }
    d->attach_disk(512);
    int r = d->probe();                                // Disk::Init, and f_mount of the one partition
    FileManager::getFileManager()->sendEventToObservers(eNodeUpdated, "/", d->get_name());
    if (!verify) {
        return r >= 0;
    }
    FileSystem *fs = d->get_file_info()->fs;
    if (r != 1 || !fs) {
        say(why, whylen, "the host does not mount the volume that was written");
        return false;
    }
    uint32_t freec = 0, cs = 0;
    if (fs->get_free(&freec, &cs) != FR_OK) {
        say(why, whylen, "the free space of the new volume cannot be read");
        return false;
    }
    if (cs != g->cluster_bytes || freec != g->clusters - g->sys_clusters) {
        say(why, whylen, "the mounted volume is not the one that was written (cluster size or free space differ)");
        return false;
    }
    return true;
}

FmtResult media_format(const char *name, FmtProgressCb cb, void *ctx, char *why, int whylen)
{
    FileDevice *d = FileDevice::find(name);
    if (!d || internal_drive(name)) {
        say(why, whylen, "that is not a medium that can be formatted");
        return FMT_REFUSED;
    }
    uint32_t wl = fmt_work_size(media_free_heap());
    uint8_t *work = wl ? (uint8_t *)malloc(wl) : 0;
    GlueCtx gc = { d };
    FmtOps ops = { &gc, op_detach, op_attach };
    FmtResult r = fmt_exfat(d->get_block_device(), &ops, work, work ? wl : 0, cb, ctx, why, whylen);
    free(work);
    return r;
}
