// The overlay service of the base (T256, T239.3 D23, D29): the one place that decides who holds the pixel layer, with which colour key and alpha, and what a library's elements and
// the still look like on it. Pure logic: the layer (LVGL on the port) and the board (the still capture, the menu keys) are the two interfaces below, so a test drives it with a fake layer.
//
// The state table (ovl_plan) is T256 section 2 S1; the calls are the SDK group svc_overlay.h.
#ifndef COREAPP_OVERLAY_H
#define COREAPP_OVERLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "svc_overlay.h"

// The canvas the elements' places are given in: the menu's canvas at its largest (trx_lv_port.h TRX_LV_W x TRX_LV_H).
#define OVL_CANVAS_W 960
#define OVL_CANVAS_H 540
// The maxima. A core's TOML B sizes them at link time (the [sizes] table, tools/app_build.py, P3-2); these are the defaults of the shared app.
#ifndef OVL_ELEMENTS_MAX
#define OVL_ELEMENTS_MAX 16                // elements of the merged UI TOML (the base's own and the core's)
#endif
#ifndef OVL_TEXT_MAX
#define OVL_TEXT_MAX 16                    // text calls that wait for the next frame
#endif
#define OVL_TEXT_LEN 48                    // a string of a text call, with its end
#define OVL_NAME_MAX 15                    // characters of an element's name

// ---- the element rows of a UI TOML ([[element]], descrec.h section 25) ----------------------------------------------------------------------------------------------
enum {
    OVA_TOP_LEFT = 0, OVA_TOP_MID, OVA_TOP_RIGHT, OVA_LEFT_MID, OVA_CENTER, OVA_RIGHT_MID, OVA_BOTTOM_LEFT, OVA_BOTTOM_MID, OVA_BOTTOM_RIGHT, OVA_COUNT
};
struct UiElementRow {
    const char *name;                      // a-z 0-9 _, 1 to OVL_NAME_MAX characters
    uint8_t anchor;                        // OVA_*: the point of the canvas the offset counts from, and the point of the element that lies on it
    int16_t dx, dy;                        // the offset from the anchor, canvas pixels (right and down are positive)
    uint16_t w, h;                         // the size, canvas pixels: the surface's size
    bool still;                            // the still (at most one element of a table)
};
struct UiElements {
    UiElementRow rows[OVL_ELEMENTS_MAX];
    int n;
};
struct OvRect {
    int x, y, w, h;
};

const char *ovl_anchor_name(int anchor);   // "top_left", ...; null for a number outside OVA_*
int ovl_anchor_of(const char *name);       // the number of the name, -1 when it is none
bool ovl_name_ok(const char *name);
// The place of a row on the canvas. False when the row has no size, or its rectangle is not inside the canvas (the TOML check refuses such a row).
bool ovl_place(const UiElementRow &r, OvRect *out);
// The row's refusal as a sentence, or null when it is fine. The compiler (tools/descrec.py _element_check) holds the same rules.
const char *ovl_row_check(const UiElementRow &r);
// base + core: `out` is the base's table with the core's rows (a row of the name of one the base has replaces it, the others are added). A row that does not fit is not applied; the
// sentence names it in why. Returns how many were refused. `base` and `core` may be null.
int ovl_merge(const UiElements *base, const UiElements *core, UiElements *out, char *why, int whylen);

// ---- the state table (S1) ---------------------------------------------------------------------------------------------------------------------------------------------
enum { OVH_CORE = 0, OVH_UI = 1 };         // who drives the pixel layer: the core's picture (the host's own picture, or the core's frame buffer), or the base's LVGL
struct OvState {
    bool menu;                             // the open menu, a message, the picture question: LVGL's full screen is up
    bool stopped;                          // the machine is held: for the menu, or paused
    bool paused;                           // Pause is set
    bool own_picture;                      // a C64-type core: the picture comes through the host's own video path, the layer is off or the menu's
    bool still;                            // a still surface exists (the base's capture of the stopped machine, or a library's)
    bool live_key;                         // the page of the open menu lets the picture through (Timewarp)
    bool elems;                            // at least one element that is not the still is shown by a library or the base
    uint8_t alpha;                         // the layer's alpha for the open menu: 255 minus the person's Overlay Transparency
};
struct OvPlan {
    uint8_t holder;                        // OVH_*
    bool key;                              // the layer's colour key on (TRX_LIVE_KEY_565)
    uint8_t alpha;                         // the layer's global alpha
    bool scene;                            // the menu is closed and LVGL shows only the still and the elements (no menu widget stands)
    bool draw_still;                       // the still is the bottom of LVGL's picture
    bool draw_elems;                       // the elements are on it
    uint8_t menu_opa;                      // the opacity LVGL draws the menu with over the still (255: opaque); only with draw_still
};
OvPlan ovl_plan(const OvState &s);
inline bool ovl_plan_same(const OvPlan &a, const OvPlan &b)
{
    return a.holder == b.holder && a.scene == b.scene && a.key == b.key && a.alpha == b.alpha && a.draw_still == b.draw_still && a.draw_elems == b.draw_elems && a.menu_opa == b.menu_opa;
}

// ---- what the layer shows -----------------------------------------------------------------------------------------------------------------------------------------------
struct OvDraw {
    const uint16_t *px;                    // the surface (RGB565, stride w)
    int x, y, w, h;                        // where, on the canvas
    uint32_t serial;                       // changes with every show and update
};
struct OvScene {
    bool has_still;
    OvDraw still;
    OvDraw el[OVL_ELEMENTS_MAX];           // in the order of the TOML
    int n;
};

// The pixel layer, as the service drives it. The board's implementation is LVGL on the port (ui.cc); a test's is a recorder.
class OverlayLayer {
public:
    virtual ~OverlayLayer() { }
    // Takes the plan and the scene: the holder, the key, the alpha, and what LVGL draws. Idempotent. The UI task only.
    virtual void apply(const OvPlan &p, const OvScene &s) = 0;
    // Writes text into a surface with the base's fonts (the UI task only).
    virtual void text(const SvcSurface &sf, int x, int y, const char *s, uint16_t colour) = 0;
};

// The board, as the service asks it.
class OverlayHost {
public:
    virtual ~OverlayHost() { }
    virtual void *alloc(uint32_t n) = 0;
    virtual void release(void *p) = 0;
    // The picture of the machine that stopped, fitted to w x h pixels (RGB565): from the host's still capture on a C64-type core, from the core's frame buffer on the others. False: none.
    virtual bool grab_still(uint16_t *dst, int w, int h) = 0;
    // The layer goes to LVGL (the core's picture off) / back to the core (its picture on): the board's Video::hide() and show(). Called around apply() on a change of the holder.
    virtual void layer_to_ui() { }
    virtual void layer_to_core() { }
    // The menu hotkey's close and open, for the main loop (the person's close of the menu, the menu key).
    virtual void menu_hide() = 0;
    virtual void menu_show() = 0;
    // A short critical section around the service's tables (calls come from any task).
    virtual void enter() { }
    virtual void leave() { }
    // The layer is touched by the UI task only (LVGL has no lock around the menu's own drawing). A call from another task that takes a surface away (remove, a second show, a released
    // library) waits until the UI task's next frame has let go of it: ui_task() says whether the caller is the UI task (then it settles itself), now_ms() and wait_ms() bound the wait.
    virtual bool ui_task() { return true; }
    virtual uint32_t now_ms() { return 0; }
    virtual void wait_ms(uint32_t ms) { (void)ms; }
    virtual void log(const char *line) { (void)line; }
};

#define OVL_OWNER_BASE (-1)                // the base's own elements (the PAUSED tag); a library's owner is its slot, 0 and up
#define OVL_OWNER_NONE (-100)

class OverlaySvc {
public:
    OverlaySvc(OverlayLayer *layer, OverlayHost *host);
    ~OverlaySvc();

    // The core is up: its merged table (copied) and the kind of picture. From here the service drives the layer. clear() is the core's end: every element goes and the layer takes
    // the state of no core (the core's picture, key off, alpha 255) at once.
    void set_table(const UiElements *t, bool own_picture);
    void clear();
    bool enabled() const { return enabled_; }

    // The base's state, from the UI task. Each takes effect at once (the layer is told before the call returns).
    void menu(bool up);
    void frozen(bool on);                  // the machine is held for the menu
    void paused(bool on);
    void live_key(bool on);
    void look(uint8_t alpha);              // 255 minus the person's Overlay Transparency
    // One pass of the main loop: the text calls, the redraws, the menu calls of libraries. UI task.
    void frame();

    // The SDK calls. `owner` is the library's slot, or OVL_OWNER_BASE.
    int show(int owner, const char *name, const SvcSurface *sf);
    int update(int owner, int el);
    int remove(int owner, int el);
    void text(int owner, const SvcSurface *sf, int x, int y, const char *s, uint16_t colour);
    void menu_hide();
    void menu_show();
    // A library is released: its elements and waiting text calls go, the layer is told at once.
    void drop_owner(int owner);
    static const char *error(int code);

    bool holds_layer() const { return enabled_ && plan_.holder == OVH_UI; }   // LVGL is the layer's holder: the main loop has to poll it
    const OvPlan &plan() const { return plan_; }
    const OvScene &scene() const { return scene_; }
    OvState state() const;
    int shown() const;                     // the elements shown now (the still too)
    int owner_of(const char *name) const;  // OVL_OWNER_NONE when nobody shows it, -2 when there is no such element

private:
    struct Slot {
        UiElementRow row;
        OvRect rect;
        bool placed;
        int owner;
        const uint16_t *px;
        uint32_t serial;
    };
    struct TextCall {
        int owner;
        SvcSurface sf;
        int x, y;
        uint16_t colour;
        char s[OVL_TEXT_LEN];
    };
    void settle();
    void sync_ui();                        // a surface was taken away: the UI task has settled before this returns (or after 200 ms, with a line in the log)
    void build_scene(OvScene *sc, bool *elems, bool *lib_still) const;
    void take_still();
    void drop_still();
    int find(const char *name) const;
    OverlayLayer *layer_;
    OverlayHost *host_;
    bool enabled_, own_picture_;
    Slot slot_[OVL_ELEMENTS_MAX];
    int n_;
    int still_slot_;                       // the slot of the still, -1: the table has none
    uint16_t *base_still_;                 // the base's capture of the stopped machine, w x h of the still's row
    bool base_still_ok_;
    bool menu_up_, frozen_, paused_, live_key_;
    bool was_standing_;
    uint8_t alpha_;
    TextCall text_[OVL_TEXT_MAX];
    int ntext_;
    int menu_req_;                         // 0 none, 1 hide, 2 show: the last call of a library
    uint32_t serial_;
    volatile uint32_t frames_;             // counts the settles of the UI task: sync_ui waits for it to move
    OvPlan plan_;
    OvScene scene_;
    bool applied_;
};

#endif
