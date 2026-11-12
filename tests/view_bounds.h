// T164 D10a: "nothing is cut" as a check, for the two render benches. Walks the objects of the active screen after a layout and reports every
// visible object that lies (a) not inside the canvas (b) a label that is not inside the object it sits in (the parent cuts it; 2 px at the sides and
// 3 px below are the line box of a font that is taller than its row, no text is lost in them).
// A caller may also name key labels (view_bounds' `keys`, a null-terminated list of texts): a label whose text is the beginning of such a key followed by "..."
// (its long mode cut it) counts as cut as well, the label of a key/value panel is never to be shortened.
// Objects with a zero size and labels without text are skipped. The canvas is the display's, not a constant: the same walk serves every mode.
#ifndef TEST_VIEW_BOUNDS_H
#define TEST_VIEW_BOUNDS_H

#include "lvgl.h"
#include <stdio.h>
#include <string.h>

struct ViewBounds {
    int objects, outside, cut;
    char first[3][160];
    int nfirst;
};

static void vb_note(ViewBounds &r, const char *kind, lv_obj_t *o, const lv_area_t &a)
{
    if (r.nfirst < 3) {
        const char *txt = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
        snprintf(r.first[r.nfirst++], sizeof(r.first[0]), "%s [%d,%d - %d,%d] \"%.40s\"", kind, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2, txt);
    }
}

static bool vb_is_key(lv_obj_t *c, const char *const *keys)
{
    if (!keys || !lv_obj_check_type(c, &lv_label_class)) {
        return false;
    }
    // LVGL's dots mode writes the "..." into the label's text (lv_label_get_text returns "Appli..."): a key cut to dots is a key's beginning and "..."
    const char *t = lv_label_get_text(c);
    size_t n = strlen(t);
    if (n < 3 || strcmp(t + n - 3, "...")) {
        return false;
    }
    for (; *keys; keys++) {
        if (strlen(*keys) > n - 3 && !strncmp(t, *keys, n - 3)) {
            return true;
        }
    }
    return false;
}

static void vb_walk(lv_obj_t *o, int cw, int ch, ViewBounds &r, const char *const *keys)
{
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        lv_area_t a, pa;
        lv_obj_get_coords(c, &a);
        lv_obj_get_coords(o, &pa);
        // an object that lies wholly outside its clipping parent is not drawn (a list's rows below the pane: the selection scrolls them in, the render cases
        // check that the selected row is inside the view); the canvas is judged by what is drawn
        if (!lv_obj_has_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE) && (a.x2 < pa.x1 || a.x1 > pa.x2 || a.y2 < pa.y1 || a.y1 > pa.y2)) {   // no lv_area_intersect: it is in LVGL's private header
            continue;
        }
        bool empty_label = lv_obj_check_type(c, &lv_label_class) && !lv_label_get_text(c)[0];      // no text, nothing to cut
        if (lv_area_get_width(&a) > 0 && lv_area_get_height(&a) > 0 && !empty_label) {
            r.objects++;
            if (a.x1 < 0 || a.y1 < 0 || a.x2 >= cw || a.y2 >= ch) {
                r.outside++;
                vb_note(r, "outside", c, a);
            } else if (lv_obj_check_type(c, &lv_label_class) && (a.x2 > pa.x2 + 2 || a.y2 > pa.y2 + 3 || a.x1 < pa.x1 - 2 || a.y1 < pa.y1 - 3)) {
                r.cut++;
                vb_note(r, "cut by its parent", c, a);
            } else if (vb_is_key(c, keys)) {
                r.cut++;
                vb_note(r, "key cut to dots", c, a);
            }
        }
        vb_walk(c, cw, ch, r, keys);
    }
}

// The canvas is cw x ch. Returns the number of offenders; `what` names the screen in the log line.
static int view_bounds(const char *what, int cw, int ch, ViewBounds *out = 0, const char *const *keys = 0)
{
    ViewBounds r = {};
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    vb_walk(scr, cw, ch, r, keys);
    printf("BOUNDS %-34s canvas %dx%d: %d objects, %d outside, %d cut\n", what, cw, ch, r.objects, r.outside, r.cut);
    for (int i = 0; i < r.nfirst; i++) {
        printf("  %s\n", r.first[i]);
    }
    if (out) {
        *out = r;
    }
    return r.outside + r.cut;
}

#endif
