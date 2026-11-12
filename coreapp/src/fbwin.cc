#include "fbwin.h"
#include "engines.h"
#include "video.h"
#include <stdio.h>

namespace {

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int mini(int a, int b)
{
    return a < b ? a : b;
}

FbWin default_window(const FbWinSpec &s)
{
    FbWin w;
    w.skip_x = s.def_skip_x; w.skip_y = s.def_skip_y; w.cols = s.def_cols; w.lines = s.def_lines;
    return w;
}

uint32_t rd32(CoreIo *io, uint32_t reg)
{
    return (uint32_t)io->rd(reg) | ((uint32_t)io->rd(reg + 1) << 8) | ((uint32_t)io->rd(reg + 2) << 16) | ((uint32_t)io->rd(reg + 3) << 24);
}

void write_window(CoreIo *io, const FbWinSpec &s, const FbWin &w)
{
    io->wr(s.reg_skip_x, (uint8_t)w.skip_x);
    io->wr(s.reg_skip_x + 1, (uint8_t)(w.skip_x >> 8));
    io->wr(s.reg_skip_y, (uint8_t)w.skip_y);
    io->wr(s.reg_cols, (uint8_t)w.cols);
    io->wr(s.reg_lines, (uint8_t)w.lines);
    io->wr(s.reg_lines + 1, (uint8_t)(w.lines >> 8));
}

}

bool fbwin_compute(const FbWinSpec &s, uint32_t layer_w, uint32_t layer_lines, uint32_t hs, uint32_t he, uint32_t vs, uint32_t ve, FbWin *out)
{
    (void)layer_w; (void)layer_lines;
    *out = default_window(s);
    if (he <= hs || ve <= vs) {                       // not written yet, or a stop before the start: no display window to speak of
        return false;
    }
    int xs = s.x_per_unit * (int)hs - s.x_origin, xe = s.x_per_unit * (int)he - s.x_origin;
    int ys = (int)vs - s.y_origin, ye = (int)ve - s.y_origin;
    xs = clampi(xs, 0, 511);                          // the stream has nothing before its blanking ends and the register holds 9 bits
    xe = xe > s.src_w ? s.src_w : xe;
    ys = clampi(ys, 0, 255);
    ye = ye > s.src_h ? s.src_h : ye;
    if (xe - xs < 8 || ye - ys < 1) {
        return false;
    }
    // The full frame (the default window) centred on the middle of the display window. Where it does not fit inside the stream (the default
    // window itself may reach past src_w/src_h by its skip), the window shrinks symmetrically around that middle: a letterbox with equal
    // borders on both sides, whole words horizontally.
    int fw = (int)s.def_cols * 8, fl = (int)s.def_lines;
    int right = s.src_w > (int)s.def_skip_x + fw ? s.src_w : (int)s.def_skip_x + fw;
    int bottom = s.src_h > (int)s.def_skip_y + fl ? s.src_h : (int)s.def_skip_y + fl;
    int mx = (xs + xe) / 2, my = (ys + ye) / 2;
    int hw = mini(fw / 2, mini(mx, right - mx)), hl = mini(fl / 2, mini(my, bottom - my));
    int cols = hw / 4;                                // 2 * hw pixels, rounded down to whole words
    if (cols < 1 || hl < 1) {
        return false;
    }
    out->cols = (uint16_t)cols;
    out->lines = (uint16_t)(2 * hl);
    out->skip_x = (uint16_t)(mx - cols * 4);
    out->skip_y = (uint16_t)(my - hl);
    return true;
}

// How many looks in a row must see the same window before the picture moves: about a second at interval_ms 100. On the board
// (2026-10-21) two agreeing looks were not enough: AGA writes DIWSTRT/DIWSTOP and then DIWHIGH, a look between them sees
// another window, and two random agreements flipped the picture between two sizes every half second.
static const uint8_t FBW_STABLE_LOOKS = 10;

static void look(CoreCtx *c, FbWinState *st, const FbWinSpec &s)
{
    CoreIo *io = c->io;
    uint32_t now = io->now_ms();
    if (now - st->last_look < s.interval_ms) {
        return;
    }
    st->last_look = now;
    uint32_t h1 = rd32(io, s.reg_diw_h), v1 = rd32(io, s.reg_diw_v);
    uint32_t h2 = rd32(io, s.reg_diw_h), v2 = rd32(io, s.reg_diw_v);
    if (h1 != h2 || v1 != v2) {                        // it changed while it was read
        return;
    }
    FbWin w;
    bool okw = fbwin_compute(s, c->desc->video.w, c->desc->video.h / 2, h1 & 0xFFFF, h1 >> 16, v1 & 0xFFFF, v1 >> 16, &w);
    if (!okw) {                                        // no display window to speak of: the picture stays where it is (a fall back to the default flipped it on the board)
        st->pending_n = 0;
        return;
    }
    if (w == st->applied) {
        st->pending_n = 0;
        return;
    }
    if (st->pending_n && w == st->pending) {
        if (st->pending_n < 255) st->pending_n++;
    } else {
        st->pending = w;
        st->pending_n = 1;
    }
    if (st->pending_n >= FBW_STABLE_LOOKS && now - st->last_apply >= s.min_apply_ms) {   // it held for a second: not a split screen or a half-written window
        st->ctrl = io->rd(s.reg_ctrl);
        io->wr(s.reg_ctrl, (uint8_t)(st->ctrl | s.ctrl_video_off));
        st->t0 = now;
        st->phase = FBW_SETTLE;
        printf("[core] picture window: DIW h %03X-%03X v %03X-%03X -> at %u/%u, %u x %u\n", (unsigned)(h1 & 0xFFFF), (unsigned)(h1 >> 16),
               (unsigned)(v1 & 0xFFFF), (unsigned)(v1 >> 16), (unsigned)w.skip_x, (unsigned)w.skip_y, (unsigned)w.cols, (unsigned)w.lines);
    }
}

bool fbwin_poll(CoreCtx *c)
{
    if (!c->desc->fbwin || !c->eng) {
        return false;
    }
    const FbWinSpec &s = *c->desc->fbwin;
    FbWinState *st = &c->eng->fw;
    CoreIo *io = c->io;
    if (st->off) {
        return false;
    }
    if (!st->init) {
        st->init = true;
        if (!(io->rd(s.reg_feat) & s.feat_bit)) {      // a gateware from before the registers: the picture stays as it was
            st->off = true;
            return false;
        }
        st->applied = default_window(s);
        st->last_apply = io->now_ms() - s.min_apply_ms;
        st->last_look = io->now_ms();
        return false;
    }
    switch (st->phase) {
    case FBW_IDLE:
        look(c, st, s);
        return false;
    case FBW_SETTLE:
        if (io->now_ms() - st->t0 < s.settle_ms) {
            return false;
        }
        write_window(io, s, st->pending);               // no frame is being written: the next one has the new geometry
        io->wr(s.reg_ctrl, st->ctrl);                    // VIDEO_ON
        st->seq = io->rd(c->desc->video.reg_seq);
        st->nseq = 0;
        st->t0 = io->now_ms();
        st->phase = FBW_FILL;
        return false;
    default: {
        uint8_t q = io->rd(c->desc->video.reg_seq);
        if (q != st->seq) {
            st->seq = q;
            st->nseq++;
        }
        // every buffer has the new position once as many frames came as there are buffers (they rotate)
        if (st->nseq >= c->desc->video.nbuf || io->now_ms() - st->t0 >= s.frames_ms) {
            // a new size: the layer shows that much, centred on the screen (set_view; the layer is off for an instant). A move that only changes
            // the place needs nothing here.
            if (c->video && (st->pending.cols != st->applied.cols || st->pending.lines != st->applied.lines)) {
                c->video->set_view(st->pending.cols * 8, st->pending.lines * 2);
            }
            st->applied = st->pending;
            st->pending_n = 0;
            st->last_apply = io->now_ms();
            st->phase = FBW_IDLE;
        }
        return false;
    }
    }
}
