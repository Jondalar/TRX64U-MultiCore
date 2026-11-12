// The menu canvas of a mode (T164 D10a). Pure, no LVGL: the host app, the core app and the benches choose the canvas with the same function.
//
// A mode 960 or more wide gets the 960 x 540 canvas at the whole factor max(1, floor(vact / 540)), centred (D10). A mode narrower than 960
// (576p/480p: 720/640 wide, 800 x 600) gets a canvas of its own size at factor 1: the compact layout (trx_compact()) is drawn for it, nothing is
// cut and nothing is scaled. The buffers of the pixel layer hold 960 x 540 pixels; every canvas has at most that many (800 x 600 = 480,000,
// 720 x 576 = 414,720, 640 x 480 = 307,200).
#ifndef TRX_CANVAS_H
#define TRX_CANVAS_H

#define TRX_CANVAS_FULL_W 960
#define TRX_CANVAS_FULL_H 540
#define TRX_CANVAS_MAX_PX (TRX_CANVAS_FULL_W * TRX_CANVAS_FULL_H)

struct TrxCanvas {
    int w, h;                        // the canvas the menu is drawn on
    int factor;                      // whole output pixels per canvas pixel
    int x0, y0;                      // top left in the mode's active area (centred; 0 when the canvas fills or exceeds it)
};

// fw x fh: the active area of the mode.
static inline TrxCanvas trx_canvas_for(int fw, int fh)
{
    TrxCanvas c;
    if (fw < TRX_CANVAS_FULL_W && fw > 0 && fh > 0) {
        c.w = fw;
        c.h = fh;
        c.factor = 1;
    } else {
        c.w = TRX_CANVAS_FULL_W;
        c.h = TRX_CANVAS_FULL_H;
        c.factor = fh / TRX_CANVAS_FULL_H < 1 ? 1 : fh / TRX_CANVAS_FULL_H;
    }
    int ow = c.w * c.factor, oh = c.h * c.factor;
    c.x0 = fw > ow ? (fw - ow) / 2 : 0;
    c.y0 = fh > oh ? (fh - oh) / 2 : 0;
    return c;
}

#endif
