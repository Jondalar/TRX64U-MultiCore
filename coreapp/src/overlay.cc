// The overlay service of the base (T256). See overlay.h for the state table and svc_overlay.h for the calls.
#include "overlay.h"
#include <stdio.h>
#include <string.h>

// ---- the rows of a UI TOML --------------------------------------------------------------------------------------------------------------------------------------------
static const char *const anchor_names[OVA_COUNT] = { "top_left", "top_mid", "top_right", "left_mid", "center", "right_mid", "bottom_left", "bottom_mid", "bottom_right" };

const char *ovl_anchor_name(int a)
{
    return a >= 0 && a < OVA_COUNT ? anchor_names[a] : 0;
}

int ovl_anchor_of(const char *name)
{
    for (int i = 0; name && i < OVA_COUNT; i++) {
        if (!strcmp(name, anchor_names[i])) {
            return i;
        }
    }
    return -1;
}

bool ovl_name_ok(const char *s)
{
    if (!s || !s[0] || strlen(s) > OVL_NAME_MAX) {
        return false;
    }
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) {
            return false;
        }
    }
    return true;
}

bool ovl_place(const UiElementRow &r, OvRect *out)
{
    if (r.anchor >= OVA_COUNT || r.w < 1 || r.h < 1 || r.w > OVL_CANVAS_W || r.h > OVL_CANVAS_H) {
        return false;
    }
    const int col = r.anchor % 3, row = r.anchor / 3;                  // 0 left, 1 mid, 2 right; 0 top, 1 mid, 2 bottom
    int x = col == 0 ? 0 : col == 1 ? (OVL_CANVAS_W - r.w) / 2 : OVL_CANVAS_W - r.w;
    int y = row == 0 ? 0 : row == 1 ? (OVL_CANVAS_H - r.h) / 2 : OVL_CANVAS_H - r.h;
    x += r.dx;
    y += r.dy;
    if (x < 0 || y < 0 || x + r.w > OVL_CANVAS_W || y + r.h > OVL_CANVAS_H) {
        return false;
    }
    if (out) {
        out->x = x;
        out->y = y;
        out->w = r.w;
        out->h = r.h;
    }
    return true;
}

const char *ovl_row_check(const UiElementRow &r)
{
    if (!ovl_name_ok(r.name)) {
        return "the name is a-z, 0-9 and _, 1 to 15 characters";
    }
    if (r.anchor >= OVA_COUNT) {
        return "the anchor is none of top_left, top_mid, top_right, left_mid, center, right_mid, bottom_left, bottom_mid, bottom_right";
    }
    if (r.w < 1 || r.h < 1) {
        return "the size is 1 pixel or more in each direction";
    }
    if (!ovl_place(r, 0)) {
        return "the place is not inside the canvas of 960 x 540";
    }
    return 0;
}

int ovl_merge(const UiElements *base, const UiElements *core, UiElements *out, char *why, int whylen)
{
    memset(out, 0, sizeof(*out));
    if (whylen > 0) {
        why[0] = 0;
    }
    if (base) {
        *out = *base;
    }
    int refused = 0;
    auto note = [&](const char *name, const char *what) {
        refused++;
        size_t l = whylen > 0 ? strlen(why) : 0;
        if (whylen > 0 && l + 1 < (size_t)whylen) {
            snprintf(why + l, (size_t)whylen - l, "%selement \"%s\": %s", l ? "; " : "", name, what);
        }
    };
    for (int i = 0; core && i < core->n; i++) {
        const UiElementRow &c = core->rows[i];
        const char *bad = ovl_row_check(c);
        if (bad) {
            note(c.name ? c.name : "?", bad);
            continue;
        }
        int at = -1;
        for (int k = 0; k < out->n; k++) {
            if (!strcmp(out->rows[k].name, c.name)) {
                at = k;
            }
        }
        if (c.still) {
            for (int k = 0; k < out->n; k++) {
                if (out->rows[k].still && k != at) {
                    at = -2;
                }
            }
        }
        if (at == -2) {
            note(c.name, "the table has a still already");
        } else if (at >= 0) {
            out->rows[at] = c;
        } else if (out->n < OVL_ELEMENTS_MAX) {
            out->rows[out->n++] = c;
        } else {
            note(c.name, "no room for another element");
        }
    }
    return refused;
}

// ---- the state table --------------------------------------------------------------------------------------------------------------------------------------------------
// S1. The machine STANDS when a C64-type core is stopped for any reason (the menu's hold or Pause), and any other core when Pause is set (T256 D1, D10): only then has the layer
// nothing of the running picture to keep, and only then the base takes a still.
OvPlan ovl_plan(const OvState &s)
{
    OvPlan p;
    p.holder = OVH_CORE;
    p.key = false;
    p.alpha = 255;
    p.draw_still = false;
    p.draw_elems = false;
    p.menu_opa = 255;
    const bool standing = s.own_picture ? s.stopped : s.paused;
    const bool still = s.still && standing;
    if (s.menu) {                                       // the open menu covers the elements (D4); the still lies under it
        p.holder = OVH_UI;
        if (still) {
            p.draw_still = true;
            p.menu_opa = s.alpha;                       // the menu is blended over the still in software (D10); the layer itself is opaque
        } else {
            p.alpha = s.alpha;                          // the layer's global alpha blends the menu with the machine's picture behind it
            p.key = s.live_key;
        }
        return p;
    }
    if (still) {                                        // the machine stands, the menu is closed: the still, and the elements over it
        p.holder = OVH_UI;
        p.draw_still = true;
        p.draw_elems = s.elems;
    } else if (s.elems && s.own_picture) {              // over the running picture of a C64-type core (and a stopped one whose capture failed): the key lets it through
        p.holder = OVH_UI;
        p.key = true;
        p.draw_elems = true;
    }
    return p;                                           // else the core's picture: a layer core that runs shows no element, one that stands without a still keeps its picture
}

// ---- the service ------------------------------------------------------------------------------------------------------------------------------------------------------
OverlaySvc::OverlaySvc(OverlayLayer *layer, OverlayHost *host)
    : layer_(layer), host_(host), enabled_(false), own_picture_(false), n_(0), still_slot_(-1), base_still_(0), base_still_ok_(false), menu_up_(false), frozen_(false), paused_(false),
      live_key_(false), was_standing_(false), alpha_(255), ntext_(0), menu_req_(0), serial_(0), frames_(0), applied_(false)
{
    memset(slot_, 0, sizeof(slot_));
    memset(&plan_, 0, sizeof(plan_));
    plan_.alpha = 255;
    plan_.menu_opa = 255;
    memset(&scene_, 0, sizeof(scene_));
}

OverlaySvc::~OverlaySvc()
{
    drop_still();
}

OvState OverlaySvc::state() const
{
    OvState s;
    bool elems = false, lib_still = false;
    OvScene tmp;
    build_scene(&tmp, &elems, &lib_still);
    s.menu = menu_up_;
    s.stopped = frozen_ || paused_;
    s.paused = paused_;
    s.own_picture = own_picture_;
    s.still = lib_still || base_still_ok_;
    s.live_key = live_key_;
    s.elems = elems;
    s.alpha = alpha_;
    return s;
}

int OverlaySvc::find(const char *name) const
{
    for (int i = 0; name && i < n_; i++) {
        if (slot_[i].placed && !strcmp(slot_[i].row.name, name)) {
            return i;
        }
    }
    return -1;
}

// What the layer draws: the shown elements in the order of the table. The still is the library's surface when one shows it, else the base's capture.
void OverlaySvc::build_scene(OvScene *sc, bool *elems, bool *lib_still) const
{
    memset(sc, 0, sizeof(*sc));
    *elems = false;
    *lib_still = false;
    for (int i = 0; i < n_; i++) {
        const Slot &s = slot_[i];
        if (s.row.still) {
            if (s.owner != OVL_OWNER_NONE && s.px) {
                sc->has_still = true;
                sc->still = OvDraw{ s.px, s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.serial };
                *lib_still = true;
            } else if (base_still_ok_ && base_still_ && s.placed) {
                sc->has_still = true;
                sc->still = OvDraw{ base_still_, s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.serial };
            }
        } else if (s.owner != OVL_OWNER_NONE && s.px && s.placed) {
            sc->el[sc->n++] = OvDraw{ s.px, s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.serial };
            *elems = true;
        }
    }
}

void OverlaySvc::set_table(const UiElements *t, bool own_picture)
{
    host_->enter();
    for (int i = 0; i < n_; i++) {
        slot_[i].owner = OVL_OWNER_NONE;
        slot_[i].px = 0;
    }
    n_ = 0;
    still_slot_ = -1;
    for (int i = 0; t && i < t->n && i < OVL_ELEMENTS_MAX; i++) {
        Slot &s = slot_[n_];
        memset(&s, 0, sizeof(s));
        s.row = t->rows[i];
        s.placed = !ovl_row_check(s.row) && ovl_place(s.row, &s.rect);
        s.owner = OVL_OWNER_NONE;
        if (s.row.still && s.placed && still_slot_ < 0) {
            still_slot_ = n_;
        }
        n_++;
    }
    own_picture_ = own_picture;
    enabled_ = true;
    ntext_ = 0;
    menu_req_ = 0;
    host_->leave();
    was_standing_ = false;
    settle();
}

void OverlaySvc::clear()
{
    host_->enter();
    for (int i = 0; i < n_; i++) {
        slot_[i].owner = OVL_OWNER_NONE;
        slot_[i].px = 0;
    }
    n_ = 0;
    still_slot_ = -1;
    ntext_ = 0;
    menu_req_ = 0;
    menu_up_ = frozen_ = paused_ = live_key_ = false;
    host_->leave();
    // the layer takes the state of no core at once: the core's picture, the key off, the alpha 255, nothing drawn
    OvPlan off;
    off.holder = OVH_CORE;
    off.key = false;
    off.alpha = 255;
    off.draw_still = off.draw_elems = false;
    off.menu_opa = 255;
    OvScene none;
    memset(&none, 0, sizeof(none));
    if (enabled_) {
        layer_->apply(off, none);
    }
    plan_ = off;
    scene_ = none;
    applied_ = true;
    enabled_ = false;
    was_standing_ = false;
    drop_still();
}

// The kind of standing decides when the base takes a still; it is taken when the machine stops, dropped (after the layer let go of it) when it runs.
void OverlaySvc::take_still()
{
    drop_still();
    if (still_slot_ < 0) {
        return;
    }
    const OvRect &r = slot_[still_slot_].rect;
    base_still_ = (uint16_t *)host_->alloc((uint32_t)r.w * (uint32_t)r.h * 2u);
    if (!base_still_) {
        host_->log("[overlay] no memory for the still");
        return;
    }
    base_still_ok_ = host_->grab_still(base_still_, r.w, r.h);
    if (!base_still_ok_) {
        host_->log("[overlay] the stopped machine's picture could not be taken: no still");
        host_->release(base_still_);
        base_still_ = 0;
    }
}

void OverlaySvc::drop_still()
{
    base_still_ok_ = false;
    if (base_still_) {
        host_->release(base_still_);
        base_still_ = 0;
    }
}

void OverlaySvc::settle()
{
    if (!enabled_) {
        return;
    }
    const bool standing = own_picture_ ? (frozen_ || paused_) : paused_;
    bool released = false;
    if (standing && !was_standing_) {
        take_still();
    } else if (!standing && was_standing_) {
        released = true;                                // the layer lets go of the still first, then the memory goes
    }
    was_standing_ = standing;
    host_->enter();
    OvScene sc;
    bool elems = false, lib_still = false;
    build_scene(&sc, &elems, &lib_still);
    if (released) {
        sc.has_still = false;
        lib_still = false;
    }
    OvState st;
    st.menu = menu_up_;
    st.stopped = frozen_ || paused_;
    st.paused = paused_;
    st.own_picture = own_picture_;
    st.still = !released && (lib_still || base_still_ok_);
    st.live_key = live_key_;
    st.elems = elems;
    st.alpha = alpha_;
    OvPlan p = ovl_plan(st);
    if (!p.draw_still) {
        sc.has_still = false;
    }
    if (!p.draw_elems) {
        sc.n = 0;
    }
    host_->leave();
    bool same = applied_ && ovl_plan_same(p, plan_) && sc.has_still == scene_.has_still && sc.n == scene_.n;
    for (int i = 0; same && i < sc.n; i++) {
        same = sc.el[i].px == scene_.el[i].px && sc.el[i].serial == scene_.el[i].serial && sc.el[i].x == scene_.el[i].x && sc.el[i].y == scene_.el[i].y;
    }
    if (same && sc.has_still) {
        same = sc.still.px == scene_.still.px && sc.still.serial == scene_.still.serial;
    }
    if (!same) {
        layer_->apply(p, sc);
        plan_ = p;
        scene_ = sc;
        applied_ = true;
    }
    if (released) {
        drop_still();
    }
    frames_++;
}

void OverlaySvc::sync_ui()
{
    if (!enabled_) {
        return;
    }
    if (host_->ui_task()) {
        settle();
        return;
    }
    const uint32_t f0 = frames_, t0 = host_->now_ms();
    while (frames_ == f0) {
        if ((uint32_t)(host_->now_ms() - t0) > 200) {
            host_->log("[overlay] the UI task did not take a surface away within 200 ms");
            return;
        }
        host_->wait_ms(2);
    }
}

void OverlaySvc::menu(bool up)
{
    menu_up_ = up;
    settle();
}

void OverlaySvc::frozen(bool on)
{
    frozen_ = on;
    settle();
}

void OverlaySvc::paused(bool on)
{
    paused_ = on;
    settle();
}

void OverlaySvc::live_key(bool on)
{
    live_key_ = on;
    settle();
}

void OverlaySvc::look(uint8_t alpha)
{
    alpha_ = alpha;
    settle();
}

void OverlaySvc::frame()
{
    if (!enabled_) {
        return;
    }
    TextCall calls[OVL_TEXT_MAX];
    int n, req;
    host_->enter();
    n = ntext_;
    memcpy(calls, text_, sizeof(TextCall) * (size_t)n);
    ntext_ = 0;
    req = menu_req_;
    menu_req_ = 0;
    host_->leave();
    for (int i = 0; i < n; i++) {
        layer_->text(calls[i].sf, calls[i].x, calls[i].y, calls[i].s, calls[i].colour);
        host_->enter();
        for (int k = 0; k < n_; k++) {
            if (slot_[k].px == calls[i].sf.px) {
                slot_[k].serial = ++serial_;            // the pixels of a shown element changed: it is drawn again
            }
        }
        host_->leave();
    }
    settle();
    if (req == 1) {
        host_->menu_hide();
    } else if (req == 2) {
        host_->menu_show();
    }
}

// ---- the SDK calls ----------------------------------------------------------------------------------------------------------------------------------------------------
const char *OverlaySvc::error(int code)
{
    switch (code) {
    case OVL_E_NONE: return "done";
    case OVL_E_NAME: return "the core's UI TOML has no element of that name";
    case OVL_E_SIZE: return "the surface's width and height are not the element's size in the TOML";
    case OVL_E_BYTES: return "the surface has no buffer, or its size in bytes is not width x height x 2";
    case OVL_E_TAKEN: return "another library shows that element";
    case OVL_E_HANDLE: return "not a handle of an element of this library";
    case OVL_E_NOSERVICE: return "the overlay service is not up";
    default: return "unknown code";
    }
}

int OverlaySvc::show(int owner, const char *name, const SvcSurface *sf)
{
    if (!enabled_) {
        return OVL_E_NOSERVICE;
    }
    host_->enter();
    int rc;
    bool replaced = false;
    const int i = find(name);
    if (i < 0) {
        rc = OVL_E_NAME;
    } else if (!sf || !sf->px || sf->bytes != (uint32_t)sf->w * sf->h * 2u) {
        rc = OVL_E_BYTES;
    } else if (sf->w != slot_[i].row.w || sf->h != slot_[i].row.h) {
        rc = OVL_E_SIZE;
    } else if (slot_[i].owner != OVL_OWNER_NONE && slot_[i].owner != owner) {
        rc = OVL_E_TAKEN;
    } else {
        replaced = slot_[i].px && slot_[i].px != sf->px;
        slot_[i].owner = owner;
        slot_[i].px = sf->px;
        slot_[i].serial = ++serial_;
        rc = i + 1;
    }
    host_->leave();
    if (replaced) {
        sync_ui();                                      // the old surface is the library's again when this returns
    }
    return rc;
}

int OverlaySvc::update(int owner, int el)
{
    if (!enabled_) {
        return OVL_E_NOSERVICE;
    }
    host_->enter();
    int rc = OVL_E_HANDLE;
    if (el >= 1 && el <= n_ && slot_[el - 1].owner == owner && slot_[el - 1].px) {
        slot_[el - 1].serial = ++serial_;
        rc = 0;
    }
    host_->leave();
    return rc;
}

int OverlaySvc::remove(int owner, int el)
{
    if (!enabled_) {
        return OVL_E_NOSERVICE;
    }
    host_->enter();
    int rc = OVL_E_HANDLE;
    if (el >= 1 && el <= n_ && slot_[el - 1].owner == owner && slot_[el - 1].px) {
        slot_[el - 1].owner = OVL_OWNER_NONE;
        slot_[el - 1].px = 0;
        rc = 0;
    }
    host_->leave();
    if (rc == 0) {
        sync_ui();                                      // the surface is the library's again when this returns
    }
    return rc;
}

void OverlaySvc::text(int owner, const SvcSurface *sf, int x, int y, const char *s, uint16_t colour)
{
    if (!enabled_ || !sf || !sf->px || !s || sf->bytes != (uint32_t)sf->w * sf->h * 2u) {
        return;
    }
    host_->enter();
    if (ntext_ < OVL_TEXT_MAX) {
        TextCall &t = text_[ntext_++];
        t.owner = owner;
        t.sf = *sf;
        t.x = x;
        t.y = y;
        t.colour = colour;
        strncpy(t.s, s, OVL_TEXT_LEN - 1);
        t.s[OVL_TEXT_LEN - 1] = 0;
    }
    host_->leave();
}

void OverlaySvc::menu_hide()
{
    host_->enter();
    menu_req_ = 1;
    host_->leave();
}

void OverlaySvc::menu_show()
{
    host_->enter();
    menu_req_ = 2;
    host_->leave();
}

void OverlaySvc::drop_owner(int owner)
{
    if (!enabled_) {
        return;
    }
    host_->enter();
    for (int i = 0; i < n_; i++) {
        if (slot_[i].owner == owner) {
            slot_[i].owner = OVL_OWNER_NONE;
            slot_[i].px = 0;
        }
    }
    int k = 0;
    for (int i = 0; i < ntext_; i++) {
        if (text_[i].owner != owner) {
            text_[k++] = text_[i];
        }
    }
    ntext_ = k;
    host_->leave();
    sync_ui();                                          // the layer lets go of the surfaces before the library's memory goes
}

int OverlaySvc::shown() const
{
    int n = 0;
    for (int i = 0; i < n_; i++) {
        n += slot_[i].owner != OVL_OWNER_NONE && slot_[i].px;
    }
    return n;
}

int OverlaySvc::owner_of(const char *name) const
{
    const int i = find(name);
    return i < 0 ? -2 : slot_[i].owner;
}
