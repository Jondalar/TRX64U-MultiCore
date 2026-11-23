// The table of the folder disk library (folderhd.cc): the table "folderhd" of folderhd_api.h. This file is part of the library: libtab.cc (from the TOML B of the app) calls folderhd_bind once,
// when the loader is made, and the bind gives the table to the sector engine (sectors.cc), which has no other way to the library.
#include "folderhd_api.h"
#include "folderhd.h"
#include "sectors.h"
struct CoreCtx;
class LibLoader;

static uint32_t fh_max_cylinders(uint64_t max_bytes)
{
    uint64_t cyl = max_bytes / ((uint64_t)FFS_CYL_BLOCKS * SEC_BYTES);
    return cyl > FH_MAX_CYL ? FH_MAX_CYL : (uint32_t)cyl;
}

static uint64_t fh_offer(CoreFs *fs, const char *folder, uint32_t max_cylinders, uint64_t at_least)
{
    return folderhd_offer_bytes(fs, folder, max_cylinders, at_least);
}

static bool fh_parse_gb(const char *text, uint64_t def, uint64_t *out)
{
    return folderhd_parse_gb(text, def, out);
}

static CoreFile *fh_mount(CoreFs *fs, CoreIo *io, int unit, const char *folder, uint64_t want_bytes, uint32_t max_cylinders, char *msg, int msglen)
{
    FolderDisk::Cfg cfg = { fs, io, unit, folder, want_bytes, max_cylinders };
    return FolderDisk::mount(cfg, msg, msglen);
}

static bool fh_dirty(CoreFile *d) { return static_cast<FolderDisk *>(d)->dirty(); }
static bool fh_idle_slice(CoreFile *d) { return static_cast<FolderDisk *>(d)->idle_slice(); }
static void fh_unmount(CoreFile *d) { delete static_cast<FolderDisk *>(d); }

static void fh_info(CoreFile *d, FolderHdInfo *out)
{
    FolderDisk *f = static_cast<FolderDisk *>(d);
    out->host_writable = f->host_writable();
    out->cylinders = f->stats().cylinders;
    out->nodes = f->stats().nodes;
    out->report = f->report();
}

static const FolderHdApi g_folderhd = { sizeof(FolderHdApi), fh_max_cylinders, fh_offer, fh_parse_gb, fh_mount, fh_dirty, fh_idle_slice, fh_unmount, fh_info };

const FolderHdApi *folderhd_table(void)
{
    return &g_folderhd;
}

void folderhd_bind(CoreCtx *, LibLoader &)
{
    sec_set_folderhd(&g_folderhd);
}
