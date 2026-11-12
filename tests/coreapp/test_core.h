// A made-up core "TEST" (T93 S11): ROMs, two settings, one media slot, one task, one file type. Everything the shared code does
// for the real cores works for it without a line changed in coreapp/src.
#ifndef TESTS_COREAPP_TEST_CORE_H
#define TESTS_COREAPP_TEST_CORE_H

#include "ctx.h"

namespace testcore {
static int g_loaded = 0, g_ran = 0, g_mounted = 0, g_hold = 0, g_release = 0;
static int g_set_a = -1, g_set_b = -1;
static int op_put(CoreCtx *, const OpArgs &, OpOut &) { g_loaded++; return OPE_OK; }
static const ArgSpec blob_arg[] = { ARG_BLOB("data") };
static const RomSpec roms[] = { { "Test ROM", { "TEST.ROM", "T.BIN", 0 }, { 4096, 8192, 0 }, true, 0, "test.put" } };
static int op_a(CoreCtx *, const OpArgs &a, OpOut &) { g_set_a = a.i(0); return OPE_OK; }
static int op_b(CoreCtx *, const OpArgs &a, OpOut &) { g_set_b = a.i(0); return OPE_OK; }
static const ArgSpec a_arg[] = { ARG_INT("value", 0, 1) };
static const ArgSpec b_arg[] = { ARG_INT("value", 0, 2) };
static const char *const av[] = { "Off", "On" };
static const char *const bv[] = { "Slow", "Normal", "Fast" };
static const Setting settings[] = { { "Sound", av, 2, 1, WHEN_LIVE, "test.set_a" }, { "Speed", bv, 3, 1, WHEN_RESET, "test.set_b" } };
static int op_task(CoreCtx *, const OpArgs &, OpOut &) { g_ran++; return OPE_OK; }
static const Task tasks[] = { { "Do it", "test.task", 0, 0, true } };
static int op_mount(CoreCtx *, const OpArgs &, OpOut &) { g_mounted++; return OPE_OK; }
static const ArgSpec mount_args[] = { ARG_PATH("path") };
static const OpBind mount_bind[] = { B_FILE("path") };
static const FileAction acts[] = { { "Mount", "test.mount", mount_bind, 1, false } };
static const FileType fts[] = { { ".tst", acts, 1 } };
static const char *what(CoreCtx *) { return 0; }
static const MediaSlot media[] = { { "Slot", "test.slot", 0, 0, 0 } };
static void contribute(OpRegistry &r)
{
    r.contributor("test");
    r.add_op("test.put", blob_arg, 1, op_put);
    r.add_op("test.set_a", a_arg, 1, op_a);
    r.add_op("test.set_b", b_arg, 1, op_b);
    r.add_op("test.task", 0, 0, op_task);
    r.add_op("test.mount", mount_args, 1, op_mount);
    r.add_state("test.slot", what);
}
static int init(CoreCtx *c, char *, int) { g_hold++; c->io->mode_reset(true); return 0; }
static int release(CoreCtx *c, char *, int) { g_release++; c->io->mode_reset(false); return 0; }
static const CoreDesc desc = {
    { 'T', 'E', 'S', 'T' }, "Test Machine", "Test Machine", "TEST", "test", "Test Settings", 1,
    roms, 1, settings, 2, tasks, 1, fts, 1, media, 1,
    { 320, 200, 2, 100, 100, 0x01000000u + 0x800000u, 0x100000u, 3, 0xC0, 0xC1, 0xC4, 0xC8 },
    { 0xA8, 0x02, 200 },
    init, release, 0, 5, 0, 0, false, 0, 0, contribute,
};
}

#endif
