// The "DDT Look Ma, No VIC" descriptor (T168): no ROMs, no settings, no program; the core starts by itself and draws on the host's HDMI. One task, Reset.
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "ddt_regs.h"
#include "cores.h"

static int ddt_init(CoreCtx *c, char *, int)
{
    c->io->wr(DDT_CTRL, DDT_CTRL_HOLD);
    c->io->mode_reset(true);
    return 0;
}

static int ddt_release(CoreCtx *c, char *, int)
{
    c->io->wr(DDT_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

static const Task tasks[] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
};

static const MemMap ddt_mem = { 0, 0, false, "This DDT core has no memory the application reaches (the picture is computed, not kept in a window)" };

static void ddt_register(OpRegistry &r)
{
    r.contributor("ddt_novic");
    MEM_OPS(r, ddt_mem, 0xFFFF);
}

extern const CoreDesc ddt_novic_desc;
const CoreDesc ddt_novic_desc = {
    { 'D', 'D', 'T', 'N' },
    "DDT Look Ma, No VIC",
    "DDT Look Ma, No VIC",
    "NOVIC",
    "ddt_novic",
    "DDT Look Ma, No VIC Settings",
    1,
    0, 0,
    0, 0,
    tasks, 1,
    0, 0,
    0, 0,
    // 352 x 200 at scale 3 is 1056 x 600, centred on 1920 x 1080; the buffers are the reset values of FB_BASE / FB_STRIDE
    { 352, 200, 3, 432, 240, DDT_FB_ADDR, DDT_FB_STRIDE_BYTES, 4, DDT_FB_LATEST, DDT_FRAME_SEQ, DDT_FB_BASE, DDT_FB_STRIDE },
    { DDT_STATUS, DDT_ST_HELD, 200 },
    ddt_init, ddt_release, 0, 20,
    0, 0, false,
    0, 0,
    ddt_register,
    0, 0,
};
