// T256: the overlay service with a fake layer (S1, S7, S9, S10). The service is pure (overlay.cc); the layer and the board are recorded.
#include "overlay.h"
#include "libapi.h"
#include "check.h"
#include <string.h>
#include <stdlib.h>
#include <vector>

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
    void *alloc(uint32_t n) { live++; return malloc(n); }
    void release(void *p) { frees++; live--; free(p); }
    bool grab_still(uint16_t *dst, int w, int h) { grabs++; if (grab_ok) { for (int i = 0; i < w * h; i++) dst[i] = 0x1234; } return grab_ok; }
    void layer_to_ui() { to_ui++; }
    void layer_to_core() { to_core++; }
    void menu_hide() { hide++; }
    void menu_show() { show++; }
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
    // ---- the TOML side (D8): a place is inside the canvas, a row that is not is refused
    {
        OvRect r;
        CHECK(ovl_place(UiElementRow{ "paused", OVA_TOP_RIGHT, -14, 14, 126, 30, false }, &r));
        CHECK_EQ(r.x, 820);
        CHECK_EQ(r.y, 14);
        CHECK(ovl_place(UiElementRow{ "bar", OVA_BOTTOM_MID, 0, -20, 400, 40, false }, &r));
        CHECK_EQ(r.x, 280);
        CHECK_EQ(r.y, 480);
        CHECK(!ovl_place(UiElementRow{ "x", OVA_TOP_LEFT, 900, 0, 100, 10, false }, &r));
        CHECK(!ovl_place(UiElementRow{ "x", OVA_CENTER, 0, 0, 961, 10, false }, &r));
        CHECK(ovl_row_check(UiElementRow{ "x", OVA_TOP_LEFT, 900, 0, 100, 10, false }) != 0);
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

    // ---- S4 without a capture: the elements stand over the keyed screen, as the tag did
    {
        Rig r(true);
        r.host.grab_ok = false;
        SvcSurface tag = sf(g_tag, 126, 30);
        r.svc.show(OVL_OWNER_BASE, "paused", &tag);
        r.svc.paused(true);
        r.svc.frame();
        check_plan(r.layer.plan, OVH_UI, true, 255, false, true, 255);
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
    {
        Rig r(false);                                                                       // a stand without a capture keeps the core's picture, no element
        r.host.grab_ok = false;
        SvcSurface bar = sf(g_bar, 400, 40);
        r.svc.show(0, "bar", &bar);
        r.svc.paused(true);
        r.svc.frame();
        check_plan(r.layer.plan, OVH_CORE, false, 255, false, false, 255);
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
        CHECK_EQ(r.svc.show(2, "bar", &bar), OVL_E_TAKEN);
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
        CHECK_EQ(r.svc.show(0, "bar", &bar), OVL_E_NOSERVICE);
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

    // ---- the limits of the calls: a refusal with its reason
    {
        Rig r(true);
        SvcSurface bad = sf(g_bar, 400, 41);
        CHECK_EQ(r.svc.show(0, "bar", &bad), OVL_E_SIZE);
        bad = sf(g_bar, 400, 40);
        bad.bytes -= 2;
        CHECK_EQ(r.svc.show(0, "bar", &bad), OVL_E_BYTES);
        SvcSurface ok = sf(g_bar, 400, 40);
        CHECK_EQ(r.svc.show(0, "nothing", &ok), OVL_E_NAME);
        CHECK_EQ(r.svc.update(0, 3), OVL_E_HANDLE);
        CHECK_EQ(r.svc.remove(0, 99), OVL_E_HANDLE);
        CHECK(strlen(OverlaySvc::error(OVL_E_SIZE)) > 10);
    }

    // ---- S10: a library hides the menu and shows it again: the requests reach the board's main loop at the next frame, in the order of the calls (the last wins)
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
    DONE("overlay");
}
