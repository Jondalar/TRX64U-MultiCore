// The test library of the overlay service (T256 S3, S5, S10): it calls the SDK group api->overlay and api->ctx the way a library of TRX64-Ultimate's does, so the ue2emu features
// (tests/ui/features*/overlay_*.feature) prove the service without that library. It has no menu: the features call its operations over REST (POST /v1/ops/ovtest.NAME).
//   ovtest.show     paints the element "ovtest" (a red box with a white frame and the text OVT) and shows it
//   ovtest.remove   takes the element away
//   ovtest.pause    Pause on (api->ctx->set_paused), ovtest.resume Pause off
//   ovtest.hide     api->overlay->menu_hide, ovtest.show_menu api->overlay->menu_show
// The element's name, place and size are the UI TOML's (ovtest.toml, [[element]] "ovtest": 160 x 40 above the bottom edge); this library only holds the surface.
// Build: tests/ui/ovtest/Makefile (make RV_PREFIX=riscv32-unknown-elf-).
#include "libapi.h"
#include "libfmt.h"

#define EL_W 160
#define EL_H 40
#define COL_RED   0xF800
#define COL_WHITE 0xFFFF

static const LibApi *g_api;
static uint16_t g_px[EL_W * EL_H];
static int g_el;                                      // the handle of the element, 0: none

static void say(OpOut &o, const char *s)
{
    int i = 0;
    while (s[i] && i < o.msglen - 1) {
        o.msg[i] = s[i];
        i++;
    }
    o.msg[i] = 0;
}

static bool have_service(OpOut &o)
{
    if (!LIB_API_COVERS(g_api, overlay) || !g_api->overlay) {
        say(o, "no overlay service");
        return false;
    }
    return true;
}

static int op_show(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (!have_service(o)) {
        return OPE_FAILED;
    }
    for (int y = 0; y < EL_H; y++) {
        for (int x = 0; x < EL_W; x++) {
            const bool frame = x < 2 || y < 2 || x >= EL_W - 2 || y >= EL_H - 2;
            g_px[y * EL_W + x] = frame ? COL_WHITE : COL_RED;
        }
    }
    const SvcSurface sf = { g_px, (uint32_t)sizeof(g_px), EL_W, EL_H };
    g_api->overlay->text(&sf, 8, 12, "OVT", COL_WHITE);
    const int h = g_api->overlay->show("ovtest", &sf);
    if (h < 0) {
        say(o, g_api->overlay->error(h));
        return OPE_FAILED;
    }
    g_el = h;
    say(o, "shown");
    return OPE_OK;
}

static int op_remove(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (!have_service(o)) {
        return OPE_FAILED;
    }
    const int r = g_el ? g_api->overlay->remove(g_el) : 0;
    g_el = 0;
    if (r < 0) {
        say(o, g_api->overlay->error(r));
        return OPE_FAILED;
    }
    say(o, "removed");
    return OPE_OK;
}

#define VC_W 376
#define VC_H 283
#define VC_OFF    0x00D00000u                   // VC20_FB_ADDR (0x01D00000) less the start of the core's DDR2 window (0x01000000)
#define VC_STRIDE 0x00100000u                   // VC20_FB_STRIDE_BYTES
#define VC_NBUF   4

static int op_paint(CoreCtx *, const OpArgs &, OpOut &o)
{
    static const uint16_t bars[8] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x8410 };
    static uint16_t line[VC_W];
    for (int x = 0; x < VC_W; x++) {
        line[x] = bars[x * 8 / VC_W];
    }
    for (int b = 0; b < VC_NBUF; b++) {
        for (int y = 0; y < VC_H; y++) {
            g_api->io->ddr_write(VC_OFF + (uint32_t)b * VC_STRIDE + (uint32_t)y * VC_W * 2u, line, sizeof(line));
        }
    }
    say(o, "painted");
    return OPE_OK;
}

static int op_pause(CoreCtx *, const OpArgs &, OpOut &o)
{
    g_api->ctx->set_paused(true);
    say(o, "paused");
    return OPE_OK;
}

static int op_resume(CoreCtx *, const OpArgs &, OpOut &o)
{
    g_api->ctx->set_paused(false);
    say(o, "resumed");
    return OPE_OK;
}

static int op_hide(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (!have_service(o)) {
        return OPE_FAILED;
    }
    g_api->overlay->menu_hide();
    say(o, "hide asked");
    return OPE_OK;
}

static int op_show_menu(CoreCtx *, const OpArgs &, OpOut &o)
{
    if (!have_service(o)) {
        return OPE_FAILED;
    }
    g_api->overlay->menu_show();
    say(o, "show asked");
    return OPE_OK;
}

static const char *status_text(CoreCtx *) { return g_el ? "ovtest shown" : "ovtest idle"; }

static const LibOpDecl ops[] = {
    { "ovtest.show", 0, 0, op_show },
    { "ovtest.remove", 0, 0, op_remove },
    { "ovtest.paint", 0, 0, op_paint },
    { "ovtest.pause", 0, 0, op_pause },
    { "ovtest.resume", 0, 0, op_resume },
    { "ovtest.hide", 0, 0, op_hide },
    { "ovtest.show_menu", 0, 0, op_show_menu },
};
static const LibStateDecl states[] = { { "ovtest.status", status_text } };

#ifdef __riscv
#define LIB_ENTRY_ATTR __attribute__((visibility("default"), section(".text.lib_register")))
#else
#define LIB_ENTRY_ATTR
#endif
extern "C" LIB_ENTRY_ATTR int lib_register(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != U2A_ABI_MAJOR) {
        return 1;
    }
    g_api = api;
    out->ops = ops;
    out->nops = (int)(sizeof(ops) / sizeof(ops[0]));
    out->states = states;
    out->nstates = 1;
    return 0;
}
