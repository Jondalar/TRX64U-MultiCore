// T256: the overlay service with a fake layer (S1, S7, S9, S10). The service is pure (overlay.cc); the layer and the board are recorded.
#include <string>
#include <vector>
#include <stdint.h>
#include <stdlib.h>
#define private public                          // the test reads the still capture's slots through the pointer Video holds: the register takes 32 bits, a host pointer has more
#include "video.h"
#undef private
#include "overlay.h"
#include "libapi.h"
#include "fakes.h"
#include "videomode.h"
#include "trxcanvas.h"
#include "check.h"
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <mutex>

struct FakeLayer : OverlayLayer {
    int applies = 0, texts = 0;
    OvPlan plan = {};
    OvScene scene = {};
    std::vector<std::string> said;
    void apply(const OvPlan &p, const OvScene &s) { applies++; plan = p; scene = s; }
    void text(const SvcSurface &sf, int x, int y, const char *s, uint16_t colour)
    {
        texts++;
        said.push_back(s);
        if (x >= 0 && y >= 0 && x < sf.w && y < sf.h) {
            sf.px[y * sf.w + x] = colour;                      // one pixel, so a test can see the text reached the surface
        }
    }
};
struct FakeHost : OverlayHost {
    int grabs = 0, frees = 0, to_ui = 0, to_core = 0, hide = 0, show = 0;
    bool grab_ok = true;
    int live = 0;
    int cw = OVL_CANVAS_W, ch = OVL_CANVAS_H;
    void canvas(int *w, int *h) { *w = cw; *h = ch; }
    void *alloc(uint32_t n) { live++; return malloc(n); }
    void release(void *p) { frees++; live--; free(p); }
    int g_w = 0, g_h = 0, g_fw = 0, g_fh = 0, g_ox = 0, g_oy = 0;
    bool has_pic = false;
    OvRect pic = {};
    bool picture_rect(int, int, OvRect *r) { *r = pic; return has_pic; }
    int g_stride = 0, pal_calls = 0, locks = 0, depth = 0, bad_depth = 0;
    OvPalette g_pal = 0;
    bool pattern = false;                                                                  // the pixel says where in the fitted picture it came from
    std::mutex mx;                                                                         // the stand-in of the board's grab mutex
    void grab_lock() { mx.lock(); locks++; if (++depth != 1) bad_depth++; }
    void grab_unlock() { depth--; mx.unlock(); }
    static uint16_t mark(int px, int py) { return (uint16_t)(((py & 0xFF) << 8) | (px & 0xFF)); }
    bool grab_still(uint16_t *dst, int w, int h, int fw, int fh, int ox, int oy, int stride = 0, OvPalette pal = 0)
    {
        grabs++;
        if (depth != 1) bad_depth++;                                                       // every grab is made inside the lock
        if (stride < w) stride = w;
        g_w = w; g_h = h; g_fw = fw; g_fh = fh; g_ox = ox; g_oy = oy; g_stride = stride; g_pal = pal;
        if (pal) { uint8_t rgb[16][3]; pal_calls += pal(rgb) ? 1 : 0; }
        if (grab_ok) { for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) dst[y * stride + x] = pattern ? mark(ox + x, oy + y) : 0x1234; }
        return grab_ok;
    }
    void layer_to_ui() { to_ui++; }
    void layer_to_core() { to_core++; }
    void menu_hide() { hide++; }
    void menu_show() { show++; }
};


// A layer that records its writes, and answers the still capture's status (the host's capture of the VIC, which a C64-type core has).
struct StillPx : PxPort {
    uint32_t base = 0, status = 0;
    void w32(uint32_t reg, uint32_t v) { if (reg == 0x6064) base = v; }
    uint32_t r32(uint32_t reg) { return reg == 0x6068 ? status : 0; }
};
static bool g_vic_gw = false;
bool video_gateware_vic_scale(void) { return g_vic_gw; }                                 // the test says whether the gateware has the VIC scaler (the stub in video_stub.cc is weak)
static bool g_pal_ok;
static bool palette_source(uint8_t rgb[16][3])
{
    if (!g_pal_ok) {
        return false;
    }
    memset(rgb, 0, 16 * 3);
    rgb[2][0] = 0xF8; rgb[2][1] = 0x00; rgb[2][2] = 0x00;                   // the person's red
    rgb[5][0] = 0x00; rgb[5][1] = 0xFC; rgb[5][2] = 0x00;                   // and green
    return true;
}
static bool lib_palette(uint8_t rgb[16][3])
{
    memset(rgb, 0, 16 * 3);
    rgb[2][2] = 0xF8;                                                       // the library's blue
    return true;
}
// A frame buffer that answers by address: each byte is a function of its address, so a wrong column or row shows in the pixel.
struct PatternIo : FakeIo {
    void ddr_abs_read(uint32_t addr, void *p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++) ((uint8_t *)p)[i] = (uint8_t)((addr + i) * 2654435761u >> 13);
    }
};
// What Video::grab computed before the column table: one division per pixel, the formulas of that code. Colodore (no palette source), the 4 bit capture as the test fills it.
static uint16_t ref_colodore(int i)
{
    static const uint16_t c[16] = { 0x0000, 0xFFFF, 0x8187, 0x7679, 0x89F2, 0x5569, 0x2973, 0xEF8E, 0x8A85, 0x51C0, 0xC36E, 0x4A49, 0x7BCF, 0xAFF3, 0x737D, 0xB596 };
    return c[i & 15];
}
static uint8_t g_cap_ref[400 * 270 / 2];
static void ref_own(uint16_t *dst, int w, int h, int fw, int fh, int ox, int oy, int stride, int rows)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *src = g_cap_ref + (size_t)((oy + y) * rows / fh) * (400 / 2);
        for (int x = 0; x < w; x++) {
            const int vx = (ox + x) * 400 / fw;
            const uint8_t b = src[vx >> 1];
            dst[(size_t)y * stride + x] = ref_colodore((vx & 1) ? (b >> 4) : (b & 15));
        }
    }
}
static void ref_fb(uint16_t *dst, int w, int h, int fw, int fh, int ox, int oy, int stride, uint32_t vw, uint32_t vh, uint32_t base, uint32_t dw, uint32_t pb)
{
    PatternIo io;
    uint8_t line[8192];
    for (int y = 0; y < h; y++) {
        const uint32_t sy = (uint32_t)(oy + y) * vh / (uint32_t)fh;
        io.ddr_abs_read(base + sy * dw * pb, line, vw * pb);
        for (int x = 0; x < w; x++) {
            const uint32_t sx = (uint32_t)(ox + x) * vw / (uint32_t)fw;
            if (pb == 2) {
                dst[(size_t)y * stride + x] = (uint16_t)(line[2 * sx] | (line[2 * sx + 1] << 8));
            } else {
                const uint32_t v = (uint32_t)line[4 * sx] | ((uint32_t)line[4 * sx + 1] << 8) | ((uint32_t)line[4 * sx + 2] << 16);
                dst[(size_t)y * stride + x] = (uint16_t)(((v & 0xF80000) >> 8) | ((v & 0x00FC00) >> 5) | ((v & 0x0000F8) >> 3));
            }
        }
    }
}
struct Geo { int w, h, fw, fh, ox, oy, stride; };
static const Geo g_geo[] = {
    { 400, 270, 400, 270, 0, 0, 0 },          { 960, 540, 1066, 720, 53, 90, 960 },   { 800, 540, 800, 540, 0, 0, 1000 },  { 333, 201, 1200, 600, 80, 17, 340 },
    { 640, 480, 640, 480, 0, 0, 0 },          { 1, 1, 400, 270, 399, 269, 0 },        { 500, 130, 777, 131, 11, 0, 501 },  { 960, 100, 960, 100, 0, 0, 0 },
};

struct MenuLog : OvMenuPort {
    int leaves = 0, closes = 0, opens = 0;
    bool ask_close = false;
    bool leave_intent() { leaves++; return ask_close; }
    void close_menu() { closes++; }
    void open_intent() { opens++; }
};

static UiElements table()
{
    UiElements t;
    memset(&t, 0, sizeof(t));
    t.rows[0] = UiElementRow{ "still", OVA_TOP_LEFT, 0, 0, 960, 540, true };
    t.rows[1] = UiElementRow{ "paused", OVA_TOP_RIGHT, -14, 14, 126, 30, false };
    t.rows[2] = UiElementRow{ "bar", OVA_BOTTOM_MID, 0, -20, 400, 40, false };
    t.rows[3] = UiElementRow{ "led", OVA_BOTTOM_LEFT, 10, -10, 16, 8, false };
    t.n = 4;
    return t;
}

struct Rig {
    FakeLayer layer;
    FakeHost host;
    OverlaySvc svc;
    UiElements t;
    Rig(bool own) : svc(&layer, &host) { t = table(); svc.look(255); svc.set_table(&t, own); }
};

static uint16_t g_bar[400 * 40], g_led[16 * 8], g_tag[126 * 30], g_still[960 * 540];
static SvcSurface sf(uint16_t *px, int w, int h) { return SvcSurface{ px, (uint32_t)w * h * 2, (uint16_t)w, (uint16_t)h }; }

static void check_plan(const OvPlan &p, int holder, bool key, int alpha, bool still, bool elems, int menu_opa)
{
    CHECK_EQ(p.holder, holder);
    CHECK_EQ(p.key, key);
    CHECK_EQ(p.alpha, alpha);
    CHECK_EQ(p.draw_still, still);
    CHECK_EQ(p.draw_elems, elems);
    CHECK_EQ(p.menu_opa, menu_opa);
}

int main()
{
    // ---- the TOML side (D8): a place on the canvas of 960 x 540, and on the canvas of the video mode
    {
        OvRect r;
        CHECK(ovl_place(UiElementRow{ "paused", OVA_TOP_RIGHT, -14, 14, 126, 30, false }, &r));
        CHECK_EQ(r.x, 820);
        CHECK_EQ(r.y, 14);
        CHECK(ovl_place(UiElementRow{ "bar", OVA_BOTTOM_MID, 0, -20, 400, 40, false }, &r));
        CHECK_EQ(r.x, 280);
        CHECK_EQ(r.y, 480);
        const OvRect q = ovl_place_on(UiElementRow{ "paused", OVA_TOP_RIGHT, -14, 14, 126, 30, false }, 640, 480);    // the canvas of 480p: the anchor counts from its right edge
        CHECK_EQ(q.x, 500);
        CHECK_EQ(q.y, 14);
        CHECK(ovl_row_check(UiElementRow{ "bar", OVA_TOP_LEFT, 0, 0, 100, 10, false }) == 0);
        UiElements base = table(), core, out;
        memset(&core, 0, sizeof(core));
        core.rows[0] = UiElementRow{ "paused", OVA_TOP_LEFT, 10, 10, 126, 30, false };    // the core places the tag elsewhere
        core.rows[1] = UiElementRow{ "gauge", OVA_CENTER, 0, 0, 100, 20, false };         // and adds one
        core.n = 2;
        char why[100];
        CHECK_EQ(ovl_merge(&base, &core, &out, why, sizeof(why)), 0);
        CHECK_EQ(out.n, 5);
        CHECK_EQ(out.rows[1].anchor, OVA_TOP_LEFT);
        CHECK_STR(out.rows[4].name, "gauge");
    }

    // ---- S1: a C64-type core, the holder, the key and the alpha for every state
    {
        Rig r(true);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);                // no menu, running, nothing shown: the core's picture
        r.svc.look(191);                                                                   // the person's Overlay Transparency 25 %
        r.svc.menu(true);                                                                  // the open menu on the running machine (no stop)
        check_plan(r.layer.plan, OVH_UI, false, 191, false, false, 255);
        CHECK_EQ(r.host.to_ui, 1);
        r.svc.live_key(true);                                                              // a page that lets the picture through
        check_plan(r.layer.plan, OVH_UI, true, 191, false, false, 255);
        r.svc.live_key(false);
        r.svc.frozen(true);                                                                // the menu's stop: the base takes the still
        CHECK_EQ(r.host.grabs, 1);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 191);                    // the layer opaque, the menu blended over the still in software
        CHECK(r.layer.scene.has_still);
        CHECK_EQ(r.layer.scene.still.w, 960);
        CHECK_EQ(r.layer.scene.still.px[0], 0x1234);
        r.svc.live_key(true);                                                              // Timewarp's page over the still: the key stays off, the page is transparent
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 191);
        r.svc.live_key(false);
        r.svc.menu(false);                                                                 // the menu closed with the machine still stopped (a stop without Pause)
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 255);
        r.svc.menu(true);
        r.svc.frozen(false);                                                               // the machine runs again: no still
        check_plan(r.layer.plan, OVH_UI, false, 191, false, false, 255);
        CHECK_EQ(r.host.frees, 1);
        r.svc.menu(false);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.host.to_core, 1);
        CHECK_EQ(r.host.live, 0);
    }

    // ---- S3: an element over the running picture of a C64-type core; S9: gone while the menu is open, back when it closes
    {
        Rig r(true);
        SvcSurface bar = sf(g_bar, 400, 40);
        const int h = r.svc.show(0, "bar", &bar);
        CHECK(h > 0);
        r.svc.frame();                                                                     // a call takes effect at the UI task's next frame
        check_plan(r.layer.plan, OVH_UI, true, 255, false, true, 255);                    // the key lets the picture through, the element is opaque (D9)
        CHECK_EQ(r.layer.scene.n, 1);
        CHECK_EQ(r.layer.scene.el[0].x, 280);
        r.svc.look(127);
        r.svc.menu(true);                                                                  // S9: the open menu covers it
        check_plan(r.layer.plan, OVH_UI, false, 127, false, false, 255);
        CHECK_EQ(r.layer.scene.n, 0);
        r.svc.menu(false);
        check_plan(r.layer.plan, OVH_UI, true, 255, false, true, 255);
        CHECK_EQ(r.layer.scene.n, 1);
        CHECK_EQ(r.svc.update(0, h), 0);
        r.svc.frame();
        CHECK_EQ(r.svc.remove(0, h), 0);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.host.to_core, 1);
    }

    // ---- S4: a paused C64-type machine: the still and the tag; S9: the still lies under the menu, the tag does not
    {
        Rig r(true);
        SvcSurface tag = sf(g_tag, 126, 30);
        const int h = r.svc.show(OVL_OWNER_BASE, "paused", &tag);
        r.svc.text(OVL_OWNER_BASE, &tag, 12, 6, "PAUSED", 0xFFFF);
        r.svc.paused(true);
        r.svc.frame();
        CHECK_EQ(r.layer.texts, 1);
        CHECK_STR(r.layer.said[0].c_str(), "PAUSED");
        CHECK_EQ(g_tag[6 * 126 + 12], 0xFFFF);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, true, 255);
        CHECK(r.layer.scene.has_still);
        CHECK_EQ(r.layer.scene.n, 1);
        r.svc.look(191);
        r.svc.menu(true);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 191);
        CHECK(r.layer.scene.has_still);
        CHECK_EQ(r.layer.scene.n, 0);
        r.svc.menu(false);
        CHECK_EQ(r.layer.scene.n, 1);
        r.svc.paused(false);
        CHECK_EQ(r.svc.remove(OVL_OWNER_BASE, h), 0);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.host.live, 0);
    }

    // ---- S5: a layer core keeps its picture while it runs (no element), shows still and element only while it stands (Pause)
    {
        Rig r(false);
        SvcSurface bar = sf(g_bar, 400, 40);
        r.svc.show(0, "bar", &bar);
        r.svc.frame();
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);                // D1: a running layer core shows no element
        CHECK_EQ(r.layer.scene.n, 0);
        r.svc.menu(true);
        r.svc.frozen(true);                                                                // the menu's hold: a layer core takes no still (only Pause does)
        CHECK_EQ(r.host.grabs, 0);
        check_plan(r.layer.plan, OVH_UI, false, 255, false, false, 255);
        r.svc.frozen(false);
        r.svc.menu(false);
        r.svc.paused(true);
        CHECK_EQ(r.host.grabs, 1);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, true, 255);                    // the picture stays behind the element
        r.svc.paused(false);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.host.live, 0);
    }
    // ---- the still of a library (Timewarp): its surface replaces the base's, under the open menu too; several libraries in the order of the TOML
    {
        Rig r(true);
        SvcSurface st = sf(g_still, 960, 540), bar = sf(g_bar, 400, 40), led = sf(g_led, 16, 8);
        r.svc.frozen(true);
        CHECK_EQ(r.layer.scene.still.px[0], 0x1234);                                       // the base's capture
        const int hs = r.svc.show(1, "still", &st);
        CHECK(hs > 0);
        r.svc.frame();
        CHECK(r.layer.scene.still.px == g_still);                                          // the library's frame
        r.svc.menu(true);
        CHECK(r.layer.scene.still.px == g_still);
        r.svc.menu(false);
        CHECK(r.svc.show(2, "led", &led) > 0);                                             // another library
        CHECK(r.svc.show(1, "bar", &bar) > 0);
        r.svc.frame();
        CHECK_EQ(r.layer.scene.n, 2);
        CHECK_EQ(r.layer.scene.el[0].w, 400);                                              // bar before led: the order of the TOML
        CHECK_EQ(r.layer.scene.el[1].w, 16);
        // S7: library 1 is released: its elements are gone at once, the other's stay; the base's capture is the still again
        r.svc.drop_owner(1);
        CHECK_EQ(r.layer.scene.n, 1);
        CHECK_EQ(r.layer.scene.el[0].w, 16);
        CHECK_EQ(r.layer.scene.still.px[0], 0x1234);
        CHECK_EQ(r.svc.owner_of("bar"), OVL_OWNER_NONE);
        CHECK_EQ(r.svc.owner_of("led"), 2);
        r.svc.drop_owner(2);
        CHECK_EQ(r.layer.scene.n, 0);
        r.svc.menu(false);
        r.svc.frozen(false);
    }

    // ---- S7: the end of the core: everything goes and the layer takes its state at once
    {
        Rig r(true);
        SvcSurface bar = sf(g_bar, 400, 40);
        r.svc.show(0, "bar", &bar);
        r.svc.menu(true);
        r.svc.look(191);
        r.svc.clear();
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.layer.scene.n, 0);
        CHECK(!r.svc.enabled());
    }

    // ---- S1 on a layer core: Pause, then the menu opens: the still stays under the menu, the menu is blended over it (D4, D10)
    {
        Rig r(false);
        r.svc.look(191);
        r.svc.paused(true);
        CHECK_EQ(r.host.grabs, 1);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 255);
        r.svc.menu(true);                                                                   // the menu opens: no call tells the service that Pause ended
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 191);
        CHECK(r.layer.scene.has_still);
        CHECK_EQ(r.host.grabs, 1);
        r.svc.menu(false);
        check_plan(r.layer.plan, OVH_UI, false, 255, true, false, 255);
        r.svc.paused(false);
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
        CHECK_EQ(r.host.live, 0);
    }

    // ---- the video mode: the elements and the base's still follow the canvas of the moment
    {
        Rig r(true);
        SvcSurface tag = sf(g_tag, 126, 30);
        CHECK(r.svc.show(OVL_OWNER_BASE, "paused", &tag) > 0);
        r.host.cw = 640;
        r.host.ch = 480;
        r.svc.paused(true);                                                                 // the machine stops: the still is the picture fitted to the canvas
        CHECK_EQ(r.layer.scene.still.w, 640);
        CHECK_EQ(r.layer.scene.still.h, 480);
        CHECK_EQ(r.layer.scene.still.x, 0);
        CHECK_EQ(r.layer.scene.el[0].x, 500);                                               // right edge of 640, 14 pixels in
        r.host.cw = 800;
        r.host.ch = 600;
        r.svc.frame();                                                                      // the mode changed while it stands: the element moves at the next frame
        CHECK_EQ(r.layer.scene.el[0].x, 660);
        r.svc.paused(false);
        CHECK_EQ(r.svc.remove(OVL_OWNER_BASE, 2), 0);
    }

    // ---- a surface that is taken away leaves no text call behind: the library may free it when remove() returns
    {
        Rig r(true);
        SvcSurface bar = sf(g_bar, 400, 40);
        const int h = r.svc.show(0, "bar", &bar);
        r.svc.text(0, &bar, 2, 2, "TIME", 0xFFFF);
        CHECK_EQ(r.svc.remove(0, h), 0);
        r.svc.frame();
        CHECK_EQ(r.layer.texts, 0);
        r.svc.text(0, &bar, 2, 2, "TIME", 0xFFFF);
        r.svc.frame();
        CHECK_EQ(r.layer.texts, 1);                                                         // a surface that is not taken away is written as before
    }

    // ---- S7: a library built before the group has a smaller LibApi: it never reads the member, the macro says so to one that does
    {
        LibApi api;
        memset(&api, 0, sizeof(api));
        api.struct_size = sizeof(api);
        CHECK(LIB_API_COVERS(&api, overlay));
        api.struct_size = offsetof(LibApi, overlay);                                       // a base of 3.3 before the overlay service
        CHECK(!LIB_API_COVERS(&api, overlay));
        CHECK(LIB_API_COVERS(&api, board));
        CHECK_EQ(offsetof(LibApi, overlay), offsetof(LibApi, board) + sizeof(void *));     // appended after the last group: no member before it moved
    }

    // ---- S10: a library hides the menu and shows it again: the requests reach the board's main loop at the next frame, (one request waits for the frame: the last call wins)
    {
        Rig r(true);
        r.svc.menu_hide();
        r.svc.frame();
        CHECK_EQ(r.host.hide, 1);
        CHECK_EQ(r.host.show, 0);
        r.svc.menu_show();
        r.svc.frame();
        CHECK_EQ(r.host.show, 1);
        r.svc.menu_hide();
        r.svc.menu_show();
        r.svc.frame();
        CHECK_EQ(r.host.hide, 1);
        CHECK_EQ(r.host.show, 2);
        r.svc.frame();
        CHECK_EQ(r.host.show, 2);                                                           // a request is taken once
    }
    // ---- (3) the still of a C64-type core carries the person's palette; Colodore only when no source says one
    {
        FakeIo io;
        StillPx px;
        VideoDesc d = VideoDesc();                                                         // w 0: the picture is the host's own (own_picture)
        Video v(d, &px, &io);
        CHECK(v.own_picture());
        v.still_start();
        CHECK(px.base != 0);
        uint8_t *slot = v.still_slots_;
        memset(slot, 0, 0x10000);
        slot[0] = 0x52;                                                                    // pixel 0 = colour 2 (low nibble), pixel 1 = colour 5
        px.status = 1 | (1u << 8);                                                         // a frame was captured, slot 0, 50 Hz (270 rows)
        uint16_t out[2];
        g_pal_ok = false;
        Video::set_still_palette(0);
        CHECK(v.grab(out, 2, 1, 800, 270, 0, 0));                                          // the first two pixels of a picture fitted to 800 x 270: sampled from VIC pixels 0 and 0
        CHECK_EQ(out[0], 0x8187);                                                          // Colodore red (813338), nothing set
        Video::set_still_palette(palette_source);
        CHECK(v.grab(out, 2, 1, 400, 270, 0, 0));                                          // 1 : 1 across: pixel 0 colour 2, pixel 1 colour 5
        CHECK_EQ(out[0], 0x8187);                                                          // the source answers false: Colodore again
        g_pal_ok = true;
        CHECK(v.grab(out, 2, 1, 400, 270, 0, 0));
        CHECK_EQ(out[0], 0xF800);                                                          // the person's palette, not Colodore
        CHECK_EQ(out[1], 0x07E0);
        uint16_t part[1];
        CHECK(v.grab(part, 1, 1, 400, 270, 1, 0));                                         // a window that starts at pixel 1 of the fitted picture
        CHECK_EQ(part[0], 0x07E0);
        uint16_t two[2 + 3];                                                               // D11: a stride of 5 pixels, and the library's source first
        for (int i = 0; i < 5; i++) two[i] = 0xAAAA;
        CHECK(v.grab(two, 1, 1, 400, 270, 0, 0, 5, lib_palette));
        CHECK_EQ(two[0], 0x001F);                                                          // colour 2 in the library's blue, the person's red is not asked first
        CHECK_EQ(two[1], 0xAAAA);                                                          // the window is one pixel wide: the rest of the row is not written
        uint16_t rows[2 * 4];
        for (int i = 0; i < 8; i++) rows[i] = 0xAAAA;
        CHECK(v.grab(rows, 2, 2, 800, 540, 0, 0, 4, 0));                                   // the second row is the same VIC line: copied from the line above, at the stride
        CHECK(rows[0] == 0xF800 && rows[1] == 0xF800 && rows[2] == 0xAAAA && rows[3] == 0xAAAA);
        CHECK(rows[4] == 0xF800 && rows[5] == 0xF800 && rows[6] == 0xAAAA && rows[7] == 0xAAAA);
        Video::set_still_palette(0);
    }

    // ---- T256 fix round: Video::grab makes the column of each output pixel once per grab; the pixels are those of the formula per pixel, on both paths
    {
        FakeIo io;
        StillPx px;
        VideoDesc d = VideoDesc();
        Video v(d, &px, &io);
        v.still_start();
        Video::set_still_palette(0);
        static uint16_t out[1000 * 540 + 64], ref[1000 * 540 + 64];
        for (int rows : { 270, 240 }) {
            for (size_t i = 0; i < sizeof(g_cap_ref); i++) g_cap_ref[i] = (uint8_t)(i * 2654435761u >> 11);
            memcpy(v.still_slots_, g_cap_ref, sizeof(g_cap_ref));
            px.status = 1 | (rows == 240 ? 4u : 0u) | (1u << 8);                           // a frame in slot 0, 50 Hz (270 rows) or 60 Hz (240)
            for (const Geo &g : g_geo) {
                const int stride = g.stride < g.w ? g.w : g.stride;
                for (size_t i = 0; i < sizeof(out) / 2; i++) out[i] = ref[i] = 0xAAAA;
                CHECK(v.grab(out, g.w, g.h, g.fw, g.fh, g.ox, g.oy, g.stride));
                ref_own(ref, g.w, g.h, g.fw, g.fh, g.ox, g.oy, stride, rows);
                CHECK(memcmp(out, ref, sizeof(out)) == 0);                                 // every pixel, and the ones between the rows' ends stay as they were
            }
        }
        CHECK(!v.grab(out, 961, 1, 961, 1));                                               // wider than any canvas: no picture
        VideoDesc fd = VideoDesc();
        for (uint32_t bpp : { 2u, 4u }) {
            fd.w = 320; fd.h = 200; fd.scale = 3; fd.bpp = bpp; fd.nbuf = 1; fd.fb_base = 0x100000; fd.fb_stride = 0x100000;
            PatternIo pio;
            StillPx fpx;
            Video f(fd, &fpx, &pio);
            f.set_gateware(true);
            f.init();
            const Geo fg[] = { { 320, 200, 320, 200, 0, 0, 0 }, { 960, 540, 1066, 720, 53, 90, 960 }, { 333, 201, 640, 400, 100, 40, 340 }, { 1, 1, 320, 200, 319, 199, 0 }, { 500, 130, 777, 131, 11, 0, 501 } };
            for (const Geo &g : fg) {
                const int stride = g.stride < g.w ? g.w : g.stride;
                for (size_t i = 0; i < sizeof(out) / 2; i++) out[i] = ref[i] = 0xAAAA;
                CHECK(f.grab(out, g.w, g.h, g.fw, g.fh, g.ox, g.oy, g.stride));
                ref_fb(ref, g.w, g.h, g.fw, g.fh, g.ox, g.oy, stride, f.view_w(), 200, fd.fb_base, fd.w, bpp == 4 ? 4 : 2);
                CHECK(memcmp(out, ref, sizeof(out)) == 0);
            }
        }
    }

    // ---- (3) a frame-buffer core: the rectangle of its running picture on the canvas the menu has in that mode
    {
        FakeIo io;
        StillPx px;
        VideoDesc d = VideoDesc();
        d.w = 320; d.h = 200; d.scale = 3; d.bpp = 2; d.nbuf = 1;
        Video v(d, &px, &io);
        v.set_gateware(true);
        v.init();
        for (int hdmi : { VID_HDMI_1080P, VID_HDMI_720P }) {
            v.set_mode(hdmi, VID_SYS_PAL50);
            int fw = 0, fh = 0;
            video_active_size(hdmi, VID_SYS_PAL50, &fw, &fh);
            const TrxCanvas c = trx_canvas_for(fw, fh);
            int x, y, w, h;
            CHECK(v.picture_rect(&x, &y, &w, &h));
            const int ox = v.x0() + (int)((d.w - 320) / 2), oy = v.y0();                   // the whole picture is shown: the layer's position is x0, y0 and its size the output's
            CHECK_EQ(x * c.factor + c.x0, ox);                                             // the rectangle is the output rectangle seen from the canvas
            CHECK_EQ(y * c.factor + c.y0, oy);
            CHECK_EQ(w * c.factor, (int)v.out_w());
            CHECK_EQ(h * c.factor, (int)v.out_h());
        }
        Video c64(VideoDesc(), &px, &io);
        c64.init();
        int x, y, w, h;
        CHECK(!c64.picture_rect(&x, &y, &w, &h));                                           // a C64-type core on no gateware: the host's scaler shows the picture, there is no rectangle
    }

    // ---- (3) a C64-type core on the gateware with the VIC scaler: the window of 400 x 270 VIC pixels laid out by the Scale in force, in the 1280 x 720 row (its canvas is 960 x 540 at 160, 90 of it)
    {
        FakeIo io;
        StillPx px;
        Video c64(VideoDesc(), &px, &io);
        c64.set_gateware(true);
        g_vic_gw = true;
        int x, y, w, h;
        c64.set_mode(VID_HDMI_720P, VID_SYS_PAL50);
        CHECK(!c64.picture_rect(&x, &y, &w, &h));                                           // the Scale is not told yet
        c64.set_vic_scale(VID_SCALE_FIT);
        CHECK(c64.picture_rect(&x, &y, &w, &h));
        CHECK(x == -53 && y == -90 && w == 1066 && h == 720);                               // Fit: the height of the row, the aspect of 400 x 270 kept; it reaches beyond the canvas
        c64.set_vic_scale(VID_SCALE_INTEGER);
        CHECK(c64.picture_rect(&x, &y, &w, &h));
        CHECK(x == 80 && y == 0 && w == 800 && h == 540);                                   // Integer: twice, centred
        c64.set_vic_scale(VID_SCALE_FILL);
        CHECK(c64.picture_rect(&x, &y, &w, &h));
        CHECK(x == -160 && y == -90 && w == 1280 && h == 720);
        c64.set_machine_sys(VID_SYS_NTSC60);                                               // the 60 Hz family has 240 lines of the window: three times
        c64.set_vic_scale(VID_SCALE_INTEGER);
        c64.set_mode(VID_HDMI_720P, VID_SYS_NTSC60);
        CHECK(c64.picture_rect(&x, &y, &w, &h));
        CHECK(w == 1200 && h == 720);
        g_vic_gw = false;
    }

    // ---- (3) the base's still takes the running picture's rectangle on a frame-buffer core, the still element's on any other
    {
        Rig r(false);
        r.host.has_pic = true;
        r.host.pic = OvRect{ 100, 50, 640, 400 };                                          // inside the canvas
        r.svc.paused(true);
        CHECK_EQ(r.host.g_w, 640);
        CHECK_EQ(r.host.g_h, 400);
        CHECK_EQ(r.host.g_fw, 640);
        CHECK_EQ(r.host.g_ox, 0);
        CHECK_EQ(r.layer.scene.still.x, 100);
        CHECK_EQ(r.layer.scene.still.y, 50);
        CHECK_EQ(r.layer.scene.still.w, 640);
        r.svc.paused(false);
        r.host.pic = OvRect{ -80, 20, 1200, 600 };                                         // a Fit picture that reaches beyond the canvas of 960 x 540: the part on the canvas
        r.svc.paused(true);
        CHECK_EQ(r.host.g_w, 960);
        CHECK_EQ(r.host.g_h, 520);
        CHECK_EQ(r.host.g_fw, 1200);
        CHECK_EQ(r.host.g_fh, 600);
        CHECK_EQ(r.host.g_ox, 80);
        CHECK_EQ(r.host.g_oy, 0);
        CHECK_EQ(r.layer.scene.still.x, 0);
        CHECK_EQ(r.layer.scene.still.y, 20);
        r.svc.paused(false);
        r.host.has_pic = false;                                                             // no rectangle (a C64-type core): the still element's, fitted
        r.svc.paused(true);
        CHECK_EQ(r.host.g_w, 960);
        CHECK_EQ(r.host.g_fw, 0);
        CHECK_EQ(r.layer.scene.still.x, 0);
        r.svc.paused(false);
    }

    // ---- (2) the element maximum is OVL_ELEMENTS_MAX, which a core's [sizes] sets at link time: a table of that many rows merges, one more is refused
    {
        UiElements base, core, out;
        memset(&base, 0, sizeof(base));
        memset(&core, 0, sizeof(core));
        static char names[OVL_ELEMENTS_MAX + 2][8];
        for (int i = 0; i < OVL_ELEMENTS_MAX + 1; i++) {
            snprintf(names[i], sizeof(names[i]), "e%d", i);
        }
        for (int i = 0; i < OVL_ELEMENTS_MAX; i++) {
            base.rows[i] = UiElementRow{ names[i], OVA_TOP_LEFT, 0, 0, 10, 10, false };
        }
        base.n = OVL_ELEMENTS_MAX;
        core.rows[0] = UiElementRow{ names[OVL_ELEMENTS_MAX], OVA_TOP_LEFT, 0, 0, 10, 10, false };
        core.n = 1;
        char why[120];
        CHECK_EQ(ovl_merge(&base, &core, &out, why, sizeof(why)), 1);                       // the table is full: the core's row is told and not applied
        CHECK_EQ(out.n, OVL_ELEMENTS_MAX);
        core.rows[0] = UiElementRow{ names[3], OVA_CENTER, 0, 0, 10, 10, false };           // a row of a base name replaces it and needs no room
        CHECK_EQ(ovl_merge(&base, &core, &out, why, sizeof(why)), 0);
        CHECK_EQ(out.n, OVL_ELEMENTS_MAX);
    }

    // ---- (4) menu_hide and menu_show change the menu state as D7 says: hide is the person's close, show is the menu key
    {
        MenuLog m;
        CHECK_EQ(ovl_menu_request(OVR_HIDE, true, true, &m), OVM_LEFT);                     // the menu is open: the leave intent
        CHECK_EQ(m.leaves, 1);
        CHECK_EQ(m.closes, 0);                                                              // the intent did not ask for the close
        m.ask_close = true;                                                                 // a menu that is left by closing (nothing behind it to stay on)
        CHECK_EQ(ovl_menu_request(OVR_HIDE, true, true, &m), OVM_LEFT);
        CHECK_EQ(m.leaves, 2);
        CHECK_EQ(m.closes, 1);
        CHECK_EQ(ovl_menu_request(OVR_SHOW, false, true, &m), OVM_OPENED);                  // closed: the open intent
        CHECK_EQ(m.opens, 1);
        CHECK_EQ(ovl_menu_request(OVR_SHOW, true, true, &m), OVM_NOTHING);                  // a request made in the state it wants does nothing
        CHECK_EQ(ovl_menu_request(OVR_HIDE, false, true, &m), OVM_NOTHING);
        CHECK_EQ(ovl_menu_request(OVR_SHOW, false, false, &m), OVM_NOTHING);                // the core has not started: nothing to open the menu on
        CHECK_EQ(ovl_menu_request(OVR_NONE, true, true, &m), OVM_NOTHING);
        CHECK(m.leaves == 2 && m.closes == 1 && m.opens == 1);
    }
    // ---- D11: frame() paints the part of the picture under an element, at the picture's place and scale; the pixels the picture does not cover stay
    {
        Rig r(true);
        r.host.pattern = true;
        r.host.has_pic = true;
        r.host.pic = OvRect{ 300, 100, 200, 420 };                                         // the bar (280, 480, 400 x 40) overlaps it in x 300..500, y 480..520
        static uint16_t surf[400 * 40];
        for (int i = 0; i < 400 * 40; i++) surf[i] = 0xAAAA;
        SvcSurface bar = sf(surf, 400, 40);
        CHECK_EQ(r.svc.paint_frame(0, "bar", &bar), 0);                                    // no show before
        CHECK_EQ(r.host.g_w, 200);
        CHECK_EQ(r.host.g_h, 40);
        CHECK_EQ(r.host.g_fw, 200);
        CHECK_EQ(r.host.g_fh, 420);
        CHECK_EQ(r.host.g_ox, 0);
        CHECK_EQ(r.host.g_oy, 380);
        CHECK_EQ(r.host.g_stride, 400);
        bool ok = true;
        for (int y = 0; y < 40; y++) {
            for (int x = 0; x < 400; x++) {
                const bool in = x >= 20 && x < 220;
                const uint16_t want = in ? FakeHost::mark(x - 20, 380 + y) : 0xAAAA;
                ok = ok && surf[y * 400 + x] == want;
            }
        }
        CHECK(ok);
        CHECK_EQ(r.host.bad_depth, 0);
        CHECK_EQ(r.host.depth, 0);
        r.host.has_pic = false;                                                            // no rectangle: the picture is fitted to the still element (960 x 540); the bar shows the part under it
        CHECK_EQ(r.svc.paint_frame(0, "bar", &bar), 0);
        CHECK_EQ(r.host.g_w, 400);
        CHECK_EQ(r.host.g_fw, 960);
        CHECK_EQ(r.host.g_fh, 540);
        CHECK_EQ(r.host.g_ox, 280);
        CHECK_EQ(r.host.g_oy, 480);
        CHECK_EQ(surf[0], FakeHost::mark(280, 480));
        CHECK_EQ(surf[39 * 400 + 399], FakeHost::mark(679, 519));
        CHECK_EQ(r.svc.paint_frame(0, "bar", &bar) , 0);
        CHECK_EQ(r.host.bad_depth, 0);
    }

    // ---- D11: the base's still and frame() use the library's palette source; null gives the base's back; drop_owner drops it
    {
        Rig r(true);
        SvcSurface bar = sf(g_bar, 400, 40);
        CHECK_EQ(r.svc.set_palette(1, lib_palette), 0);
        r.svc.frozen(true);                                                                // the base's still
        CHECK(r.host.g_pal == lib_palette);
        CHECK_EQ(r.host.pal_calls, 1);
        CHECK_EQ(r.svc.paint_frame(1, "bar", &bar), 0);                                    // frame()
        CHECK(r.host.g_pal == lib_palette);
        CHECK_EQ(r.host.pal_calls, 2);
        CHECK_EQ(r.svc.set_palette(1, 0), 0);                                              // null: the base's source again
        CHECK_EQ(r.svc.paint_frame(1, "bar", &bar), 0);
        CHECK(r.host.g_pal == 0);
        CHECK_EQ(r.svc.set_palette(2, lib_palette), 0);
        CHECK_EQ(r.svc.set_palette(3, lib_palette), 0);                                    // the last call counts
        r.svc.drop_owner(2);                                                               // not the owner: stays
        CHECK_EQ(r.svc.paint_frame(1, "bar", &bar), 0);
        CHECK(r.host.g_pal == lib_palette);
        const int calls = r.host.pal_calls, locks = r.host.locks;
        r.svc.drop_owner(3);
        CHECK(r.host.locks > locks);                                                       // drop_owner waits for a grab in flight
        CHECK_EQ(r.svc.paint_frame(1, "bar", &bar), 0);
        CHECK(r.host.g_pal == 0);
        CHECK_EQ(r.host.pal_calls, calls);                                                 // the released source was not called again
        CHECK_EQ(r.host.bad_depth, 0);
        r.svc.frozen(false);
    }

    // ---- D11: the library's still goes while the machine stands: the base takes its own still again, from the frame drawn last
    for (int how = 0; how < 2; how++) {
        Rig r(true);
        SvcSurface st = sf(g_still, 960, 540);
        r.svc.frozen(true);
        CHECK_EQ(r.host.grabs, 1);
        const int hs = r.svc.show(1, "still", &st);
        r.svc.frame();
        CHECK(r.layer.scene.still.px == g_still);
        r.host.pattern = true;                                                             // the machine's picture changed meanwhile (a scrub)
        if (how == 0) {
            CHECK_EQ(r.svc.remove(1, hs), 0);
        } else {
            r.svc.drop_owner(1);
        }
        CHECK_EQ(r.host.grabs, 2);                                                         // taken again
        CHECK(r.layer.scene.has_still);
        CHECK(r.layer.scene.still.px != g_still);
        CHECK_EQ(r.layer.scene.still.px[0], FakeHost::mark(0, 0));                         // the new frame, not the one of the stop
        CHECK_EQ(r.host.live, 1);                                                          // the old capture is freed
        r.svc.frozen(false);
        CHECK_EQ(r.host.live, 0);
    }
    {
        Rig r(true);                                                                       // a library's still removed while the machine runs: nothing taken
        SvcSurface st = sf(g_still, 960, 540);
        const int hs = r.svc.show(1, "still", &st);
        CHECK_EQ(r.svc.remove(1, hs), 0);
        CHECK_EQ(r.host.grabs, 0);
        r.svc.frozen(true);
        CHECK_EQ(r.host.grabs, 1);
        r.svc.frozen(false);
    }
    // ---- T256 fix round: the menu closes - a library's still goes while it closes: no still of the base's for a machine that runs again; one when the machine stays paused; one while the menu stays open
    {
        Rig r(true);                                                                       // the menu stays open: the retake of D11
        SvcSurface st = sf(g_still, 960, 540);
        r.svc.menu(true);
        r.svc.frozen(true);
        CHECK_EQ(r.host.grabs, 1);
        const int hs = r.svc.show(1, "still", &st);
        r.svc.frame();
        CHECK_EQ(r.svc.remove(1, hs), 0);
        CHECK_EQ(r.host.grabs, 2);
        r.svc.frozen(false);
        r.svc.menu(false);
    }
    for (int stays_paused = 0; stays_paused < 2; stays_paused++) {
        Rig r(true);
        SvcSurface st = sf(g_still, 960, 540);
        r.svc.menu(true);
        r.svc.frozen(true);
        const int hs = r.svc.show(1, "still", &st);
        r.svc.frame();
        const int g0 = r.host.grabs;
        r.svc.closing();                                                                   // the close key: the core's menu_closed hook takes its still away
        CHECK_EQ(r.svc.remove(1, hs), 0);
        CHECK_EQ(r.host.grabs, g0);                                                        // no grab while the menu closes
        r.svc.paused(stays_paused != 0);
        r.svc.frozen(false);
        CHECK_EQ(r.host.grabs, g0);
        r.svc.menu(false);
        CHECK_EQ(r.host.grabs, stays_paused ? g0 + 1 : g0);                                // Pause: the still of the paused machine is the newest frame; a running machine: none
        r.svc.paused(false);
        CHECK_EQ(r.host.live, 0);
    }

    // ---- T256 fix round: canvas() is the size the places use now
    {
        Rig r(true);
        int w = 1, h = 2;
        CHECK_EQ(r.svc.canvas(&w, &h), 0);
        CHECK(w == 960 && h == 540);
        r.host.cw = 640; r.host.ch = 480;                                                  // a smaller mode
        CHECK_EQ(r.svc.canvas(&w, &h), 0);
        CHECK(w == 640 && h == 480);
        r.svc.clear();                                                                     // no core: nothing written
        w = 7; h = 8;
        CHECK_EQ(r.svc.canvas(&w, &h), OVL_E_NOSERVICE);
        CHECK(w == 7 && h == 8);
    }
    DONE("overlay");
}
