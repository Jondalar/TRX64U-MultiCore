// The menu canvas of a mode (T164 D10a). Pure, no LVGL: the host app, the core app and the benches choose the canvas with the same function.
//
// A mode 960 or more wide gets the 960 x 540 canvas at the whole factor max(1, floor(vact / 540)), centred (D10). A mode narrower than 960
// (576p/480p: 720/640 wide, 800 x 600) gets a canvas of its own size at factor 1: the compact layout (trx_compact()) is drawn for it, nothing is
// scaled. 576p is the exception (T259 D16): its canvas is 720 x 540, centred, 18 black lines above and below. A PAL TV shows about 550 of the 576
// lines (overscan; Alex's Sony on the A/V port, 2026-11-27: about 275 of a field's 288), so a 576-line canvas loses its top and bottom bars there;
// 540 lines are 270 a field and fit, as the 960 x 540 canvas does. The buffers of the pixel layer hold 960 x 540 pixels; every canvas has at most
// that many (800 x 600 = 480,000, 720 x 540 = 388,800, 640 x 480 = 307,200).
#ifndef TRX_CANVAS_H
#define TRX_CANVAS_H

#define TRX_CANVAS_FULL_W 960
#define TRX_CANVAS_FULL_H 540
#define TRX_CANVAS_MAX_PX (TRX_CANVAS_FULL_W * TRX_CANVAS_FULL_H)
#define TRX_CANVAS_PAL_H 576            // 576p's active height: its canvas is TRX_CANVAS_FULL_H lines (T259 D16)

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
        c.h = fh == TRX_CANVAS_PAL_H ? TRX_CANVAS_FULL_H : fh;
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

// T228: the whole factor S of an output size ow x oh over a canvas cw x ch, 1..3, for a pix_layer that scales by PX_CTRL b5:4 instead of sizing by
// PX_OSIZE: TRX64-Ultimate's (the bitstream of trx64u.cor) shows each canvas pixel as S x S output pixels, 0 counting as 1. The menu's output size is a
// whole multiple of its canvas (trx_canvas_for), so S times the canvas is that size; any other size gets the largest S that fits in it.
static inline int trx_canvas_factor(int ow, int oh, int cw, int ch)
{
    int s = (cw > 0 && ch > 0) ? (ow / cw < oh / ch ? ow / cw : oh / ch) : 1;
    return s < 1 ? 1 : (s > 3 ? 3 : s);
}

#endif
