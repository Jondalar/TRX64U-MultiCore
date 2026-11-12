// The picture window (T97 5.10, T104 library `fbwin`): a core whose frame writer crops a window out of the raw video stream and whose
// chipset tells where the picture is. The gateware offers the decoded display window read-only (the Amiga: DIWSTRT / DIWSTOP / DIWHIGH as
// the register writes set them) and the crop window as four registers (where it starts, how big it is); the frame writer puts it at the top
// left of the buffer. The rule (Alex, 2026-10-21): like MiSTer, the picture is the full frame (the default window: its height fills
// the screen, a 4:3 screen is pillarboxed) centred on the middle of the display window; where that does not fit inside the stream the window
// shrinks symmetrically around the middle (equal borders, the layer is cut to the same size by set_view).
// A window that makes no sense changes nothing: the picture stays where it is.
//
// Units: the source is the stream of the chipset (x in hires pixels after hblank, y in lines after vblank); the crop window is in words
// of 8 pixels horizontally and in lines (two rows each) vertically. Pure: CoreIo and the Video of the context.
#ifndef COREAPP_FBWIN_H
#define COREAPP_FBWIN_H

#include "ctx.h"

struct FbWinSpec {
    uint16_t reg_feat;                    // FEATURES
    uint8_t feat_bit;                     // its bit: the gateware has the registers (older gateware has not: then nothing here runs)
    uint16_t reg_ctrl;                    // the control byte ...
    uint8_t ctrl_video_off;               // ... and its VIDEO_OFF bit (the frame writer takes no new frame)
    uint16_t reg_diw_h, reg_diw_v;        // the display window, 32 bit little endian each: {start, stop} 16 bit halves
    uint16_t reg_skip_x, reg_skip_y, reg_cols, reg_lines;
    // how the display window's coordinates meet the source stream (measured on the stripes picture, cores/amiga/sim/run_fbwin.sh)
    int16_t x_per_unit;                   // 2: source hires pixels per DIW unit (a lores pixel)
    int16_t x_origin;                     // 182: source x = x_per_unit * diw_x - x_origin
    int16_t y_origin;                     // 27: source line = diw_y - y_origin
    int16_t src_w, src_h;                 // 753, 286: what the stream holds after the blanking
    // the default window the gateware powers up with: also the full frame, the largest window
    uint16_t def_skip_x, def_skip_y, def_cols, def_lines;
    uint16_t interval_ms;                 // 100: how often the display window is looked at
    uint16_t settle_ms;                   // 60: from VIDEO_OFF until the last frame in flight is written
    uint16_t frames_ms;                   // 1000: how long the new position may take to fill every buffer before the move counts as done
    uint16_t min_apply_ms;                // 2000: no two moves closer than this (a screen that changes every second must not make the picture jump)
};

struct FbWin {
    uint16_t skip_x, skip_y, cols, lines;
    bool operator==(const FbWin &o) const
    {
        return skip_x == o.skip_x && skip_y == o.skip_y && cols == o.cols && lines == o.lines;
    }
};

enum { FBW_IDLE = 0, FBW_SETTLE, FBW_FILL };

struct FbWinState {
    bool init, off;                       // off: the gateware has no registers, or the core has no record
    FbWin applied, pending;               // what the layer shows / what the last looks agreed on
    uint8_t pending_n;
    uint8_t phase;
    uint8_t ctrl;                         // CONTROL as it was before VIDEO_OFF
    uint32_t last_look, last_apply, t0;
    uint8_t seq, nseq;                    // FILL: the frame counter, and how many frames came since the registers were written
};

// The frame (def_cols x def_lines) centred on the middle of the display window {hs, he} x {vs, ve} (DIW units); smaller, still centred, where it does not fit the stream.
// False: the display window makes no sense; *out is the default and the service ignores it.
bool fbwin_compute(const FbWinSpec &s, uint32_t layer_w, uint32_t layer_lines, uint32_t hs, uint32_t he, uint32_t vs, uint32_t ve, FbWin *out);

// The service's side: looks at the display window now and then and moves the crop window when it moved. The move is: VIDEO_OFF, settle, the
// registers, VIDEO_ON, every buffer rewritten with the new position; the layer is cut (set_view) only when the size changed. Always returns false (nothing
// to do at once).
bool fbwin_poll(CoreCtx *c);

#endif
