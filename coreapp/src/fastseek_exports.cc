// The table of the fast seek library (fastseek.cc): the table of fastseek_api.h. This file is part of the library: libtab.cc (from the TOML B of the app) calls fastseek_bind once, when the
// loader is made, and the bind gives the table to the file manager's CoreFs (fm_fs.cc), which has no other way to the library.
#include "fastseek_api.h"
#include "fastseek.h"
#include "fm_fs.h"
struct CoreCtx;
class LibLoader;

#if FF_USE_FASTSEEK

static void *fs_make(void) { return new FastSeek(); }
static void fs_access(void *s, FIL *fp, uint64_t off, uint32_t len, bool write, const char *name) { static_cast<FastSeek *>(s)->access(fp, off, len, write, name); }
static void fs_destroy(void *s) { delete static_cast<FastSeek *>(s); }

static const FastSeekApi g_fastseek = { sizeof(FastSeekApi), fs_make, fs_access, fs_destroy, fastseek_maps_live, fastseek_maps_peak };

const FastSeekApi *fastseek_table(void)
{
    return &g_fastseek;
}

void fastseek_bind(CoreCtx *, LibLoader &)
{
    fm_fs_set_fastseek(&g_fastseek);
}

#else

void fastseek_bind(CoreCtx *, LibLoader &)
{
}

#endif
