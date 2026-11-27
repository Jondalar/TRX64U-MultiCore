// T97 5.10 `fbwin`: the picture is the full frame (90 words x 270 lines) centred on the middle of the chipset's display window, and shrinks
// symmetrically (letterbox, equal borders) where that does not fit the stream; the layer is cut with set_view only when the size changes. The compute step is pure (the mapping is the one the gateware bench cores/amiga/sim/run_fbwin.sh measured: the
// standard window 0x81..0x1C1 x 0x2C..0x12C is source x 76..716, lines 17..273); the service looks at the display window now and then and, when
// ten looks agree, moves the crop window with the frame writer off and waits until every buffer has it. The pixel layer is only touched when the size changes.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "video.h"

static const FbWinSpec &spec() { return *amiga1200_desc.fbwin; }

static FbWin compute(uint32_t hs, uint32_t he, uint32_t vs, uint32_t ve, bool *ok = 0)
{
    FbWin w;
    bool r = fbwin_compute(spec(), 720, 270, hs, he, vs, ve, &w);
    if (ok) *ok = r;
    return w;
}

static void set_diw(AmigaIo &io, uint32_t hs, uint32_t he, uint32_t vs, uint32_t ve)
{
    uint32_t h = hs | (he << 16), v = vs | (ve << 16);
    for (int i = 0; i < 4; i++) {
        io.regs[AM_DIW_H + i] = (uint8_t)(h >> (8 * i));
        io.regs[AM_DIW_V + i] = (uint8_t)(v >> (8 * i));
    }
}

static int count_size(const AmigaRig &r, size_t from = 0)
{
    int n = 0;
    for (size_t i = from; i < r.px.w.size(); i++) n += r.px.w[i].reg == PX_SIZE;
    return n;
}

static int count_reg(const AmigaIo &io, uint32_t reg, size_t from = 0)
{
    int n = 0;
    for (size_t i = from; i < io.log.size(); i++) n += io.log[i].reg == reg;
    return n;
}

// looks at the display window, a frame every poll, for a while
static void run(AmigaRig &r, int polls, int ms_per_poll = 20, bool frames = true)
{
    for (int n = 0; n < polls; n++) {
        r.io.delay_ms(ms_per_poll);
        if (frames) r.io.regs[AM_FRAME_SEQ]++;
        fbwin_poll(&r.c);
    }
}

int main()
{
    // ---- the compute step
    {
        bool ok;
        FbWin w = compute(0x81, 0x1C1, 0x2C, 0x12C, &ok);                    // the standard screen: the middle is where the default frame is
        CHECK(ok);
        CHECK_EQ(w.skip_x, 36); CHECK_EQ(w.skip_y, 10); CHECK_EQ(w.cols, 90); CHECK_EQ(w.lines, 270);

        w = compute(0xA1, 0x1E1, 0x30, 0x130, &ok);                          // x 140..753 (the end clamped), lines 21..277
        CHECK(ok);
        CHECK_EQ(w.cols, 77); CHECK_EQ(w.skip_x, 138);                       // middle 446, room to the right 756 - 446 = 310: 77 words (616 px) centred
        CHECK_EQ(w.skip_y, 14); CHECK_EQ(w.lines, 270);

        w = compute(0x91, 0x1A1, 0x2C, 0x12C, &ok);                          // x 108..652: the middle 380 - 360 = 20, inside the stream
        CHECK(ok);
        CHECK_EQ(w.skip_x, 20); CHECK_EQ(w.skip_y, 10);
        w = compute(0x81, 0x181, 0x2C, 0x12C, &ok);                          // 76..588: middle 332 - 360 < 0: clamped to 0
        CHECK(ok);
        CHECK_EQ(w.skip_x, 0); CHECK_EQ(w.cols, 83);                         // shrinks instead of clamping: 664 px, 76 left and 76 right of the DIW
        CHECK_EQ(w.skip_x + w.cols * 4, 332u);                               // centred on the DIW middle

        w = compute(0x50, 0x1F0, 0x20, 0x150, &ok);                          // bigger than the picture: the middle of it
        CHECK(ok);
        CHECK_EQ(w.cols, 90); CHECK_EQ(w.lines, 270);
        CHECK_EQ(w.skip_x, 16); CHECK_EQ(w.skip_y, 10);                      // x 0..753 after the clamps: 376 - 360; y 5..286: 145 - 135

        w = compute(0x81, 0x1C1, 0x25, 0xED, &ok);                           // AmigaVision AGA (board, 2026-10-21): source lines 10..210, middle 110
        CHECK(ok);
        CHECK_EQ(w.skip_y, 0); CHECK_EQ(w.lines, 220);                       // letterbox: 10 lines above and 10 below the DIW's 200, not 10 / 60
        CHECK_EQ(10 - (int)w.skip_y, (int)(w.skip_y + w.lines) - 210);
        CHECK_EQ(w.skip_x, 36); CHECK_EQ(w.cols, 90);

        w = compute(0x1C1, 0x81, 0x2C, 0x12C, &ok);                          // the stop before the start: false, the default in *out
        CHECK(!ok);
        CHECK_EQ(w.skip_x, 36); CHECK_EQ(w.skip_y, 10); CHECK_EQ(w.cols, 90); CHECK_EQ(w.lines, 270);
        w = compute(0x81, 0x1C1, 0x12C, 0x2C, &ok);
        CHECK(!ok);
        w = compute(0x1F0, 0x1EF, 0x2C, 0x12C, &ok);                        // stop before start far right: the clamps alone would leave a window
        CHECK(!ok);
        w = compute(0, 0, 0, 0, &ok);                                        // nothing written yet
        CHECK(!ok);
        w = compute(0x81, 0x84, 0x2C, 0x12C, &ok);                           // 6 pixels wide: nothing to show
        CHECK(!ok);
    }

    // ---- the service
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        r.io.regs[AM_FEAT] = AM_FEAT_FBWIN | 1;
        engines_of(&r.c);
        const VideoDesc &vd = amiga1200_desc.video;
        Video vid(vd, &r.px, &r.io);
        r.c.video = &vid;
        vid.init();
        vid.show();
        set_diw(r.io, 0x91, 0x1A1, 0x2C, 0x12C);                             // not the standard screen: its middle is 16 pixels left of the default frame
        r.io.regs[AM_CTRL] = 0x00;
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 720u);
        int size0 = count_size(r);

        r.io.delay_ms(1000);
        fbwin_poll(&r.c);                                                    // initialises: the window the gateware powers up with is where it is
        run(r, 5, 10, false);                                                // polled faster than it looks: nothing
        CHECK_EQ(count_reg(r.io, AM_CTRL), 0);
        for (int i = 0; i < 9; i++) { r.io.delay_ms(100); fbwin_poll(&r.c); }   // nine looks are not enough
        CHECK_EQ(count_reg(r.io, AM_CTRL), 0);
        r.io.delay_ms(100); fbwin_poll(&r.c);                                // the tenth agrees too: VIDEO_OFF
        CHECK_EQ(count_reg(r.io, AM_CTRL), 1);
        CHECK_EQ(r.io.regs[AM_CTRL], AM_VIDEO_OFF);
        CHECK_EQ(count_reg(r.io, AM_FB_COLS), 0);
        r.io.delay_ms(10); fbwin_poll(&r.c);
        CHECK_EQ(count_reg(r.io, AM_FB_COLS), 0);                           // the last frame in flight is not written yet
        r.io.delay_ms(60); fbwin_poll(&r.c);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 20); CHECK_EQ(r.io.regs[AM_FB_SKIP_X + 1], 0); CHECK_EQ(r.io.regs[AM_FB_SKIP_Y], 10);
        CHECK_EQ(r.io.regs[AM_FB_COLS], 90); CHECK_EQ(r.io.regs[AM_FB_LINES], 270 & 255); CHECK_EQ(r.io.regs[AM_FB_LINES + 1], 270 >> 8);
        CHECK_EQ(r.io.regs[AM_CTRL], 0);                                    // the frame writer is on again
        // order: VIDEO_OFF before the first window register, VIDEO_ON after the last
        size_t off_at = 0, first_w = 0, last_w = 0, on_at = 0;
        for (size_t i = 0; i < r.io.log.size(); i++) {
            const AmigaIo::Wr &w = r.io.log[i];
            if (w.reg == AM_CTRL && w.v == AM_VIDEO_OFF) off_at = i + 1;
            if (w.reg >= AM_FB_SKIP_X && w.reg <= AM_FB_LINES + 1) { if (!first_w) first_w = i + 1; last_w = i + 1; }
            if (w.reg == AM_CTRL && w.v == 0) on_at = i + 1;
        }
        CHECK(off_at && first_w && off_at < first_w);
        CHECK(on_at > last_w);
        // the layer is never touched: the size stays, through the move and after it
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 720u);
        size_t p_before = r.px.w.size();
        for (int i = 0; i < 3; i++) { r.io.regs[AM_FRAME_SEQ]++; fbwin_poll(&r.c); }
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 720u);
        CHECK_EQ(r.px.last(PX_STRIDE), 720u * 4u);                           // the rows of the 32-bit picture (T136)
        CHECK_EQ(r.px.w.size(), p_before);
        CHECK_EQ(vid.view_w(), 720u);
        CHECK_EQ(count_size(r), size0);                                       // a move of the position alone: no set_view, no layer-off flash

        // a window that moves again at once waits for min_apply_ms
        set_diw(r.io, 0xA1, 0x1E1, 0x30, 0x130);
        for (int i = 0; i < 4; i++) { r.io.delay_ms(100); fbwin_poll(&r.c); }
        CHECK_EQ(r.io.regs[AM_CTRL], 0);                                    // it has not held a second yet, and the last move was 400 ms ago
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 20);
        run(r, 60, 20);                                                      // held 1.6 s: stable, but the last move was not 2 s ago
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 20);
        run(r, 60, 20);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 138); CHECK_EQ(r.io.regs[AM_FB_SKIP_Y], 14); CHECK_EQ(r.io.regs[AM_FB_COLS], 77);
        CHECK_EQ(r.io.regs[AM_CTRL], 0);
        // a new size: set_view exactly once, centred, with the new size (77 words = 616 px, 270 lines = 540 rows)
        CHECK_EQ(count_size(r), size0 + 1);
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 616u);
        CHECK_EQ(vid.view_w(), 616u); CHECK_EQ(vid.view_h(), 540u);

        // the AmigaVision screen: a second size change (lines 220), again exactly one set_view
        int size1 = count_size(r);
        set_diw(r.io, 0x81, 0x1C1, 0x25, 0xED);
        run(r, 200, 20);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 36); CHECK_EQ(r.io.regs[AM_FB_SKIP_Y], 0); CHECK_EQ(r.io.regs[AM_FB_COLS], 90);
        CHECK_EQ(r.io.regs[AM_FB_LINES], 220); CHECK_EQ(r.io.regs[AM_FB_LINES + 1], 0);
        CHECK_EQ(count_size(r), size1 + 1);
        CHECK_EQ(r.px.last(PX_SIZE), (440u << 16) | 720u);
        // the same size, another place: no set_view
        int size2 = count_size(r);
        set_diw(r.io, 0x91, 0x1A1, 0x25, 0xED);                              // x middle 380: the same size 90 x 220, another skip_x
        run(r, 200, 20);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 20); CHECK_EQ(r.io.regs[AM_FB_LINES], 220);
        CHECK_EQ(count_size(r), size2);                                       // position only
        // back to the standard screen: the full size is restored
        int size3 = count_size(r);
        set_diw(r.io, 0x81, 0x1C1, 0x2C, 0x12C);
        run(r, 200, 20);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 36); CHECK_EQ(r.io.regs[AM_FB_SKIP_Y], 10); CHECK_EQ(r.io.regs[AM_FB_COLS], 90);
        CHECK_EQ(r.io.regs[AM_FB_LINES], 270 & 255); CHECK_EQ(r.io.regs[AM_FB_LINES + 1], 270 >> 8);
        CHECK_EQ(count_size(r), size3 + 1);
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 720u);
        CHECK_EQ(vid.view_w(), 720u); CHECK_EQ(vid.view_h(), 540u);
        set_diw(r.io, 0xA1, 0x1E1, 0x30, 0x130);                              // the screen of the tests below, as before
        run(r, 200, 20);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 138);
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 616u);
        // unchanged: nothing more is written
        size_t n0 = r.io.log.size(); size_t p0 = r.px.w.size();
        run(r, 20, 100);
        CHECK_EQ(r.io.log.size(), n0);
        CHECK_EQ(r.px.w.size(), p0);

        // a split screen (the window changes between looks) is not followed
        size_t n1 = r.io.log.size();
        for (int i = 0; i < 40; i++) {
            set_diw(r.io, (i & 1) ? 0x81 : 0xA1, (i & 1) ? 0x181 : 0x1E1, 0x2C, 0x12C);
            r.io.delay_ms(100);
            fbwin_poll(&r.c);
        }
        CHECK_EQ(count_reg(r.io, AM_CTRL, n1), 0);
        CHECK_EQ(count_reg(r.io, AM_FB_COLS, n1), 0);

        // the board case (2026-10-21): a look sees one of two windows at random (DIWSTRT/STOP and DIWHIGH written apart), runs of a few
        // equal looks happen; the picture must not follow either
        size_t n2 = r.io.log.size();
        uint32_t x = 12345;
        for (int i = 0; i < 400; i++) {
            x = x * 1103515245u + 12345u;
            bool a = (x >> 16) & 1;
            set_diw(r.io, a ? 0x81 : 0xA1, a ? 0x181 : 0x1E1, 0x2C, 0x12C);
            r.io.delay_ms(100);
            fbwin_poll(&r.c);
        }
        CHECK_EQ(count_reg(r.io, AM_CTRL, n2), 0);   // 40 s of random looks

        // a window that makes no sense for a long time (not written, or stop before start): the picture stays where it is
        size_t n3 = r.io.log.size(); size_t p3 = r.px.w.size();
        set_diw(r.io, 0, 0, 0, 0);
        for (int i = 0; i < 100; i++) { r.io.delay_ms(100); fbwin_poll(&r.c); }
        set_diw(r.io, 0x1C1, 0x81, 0x2C, 0x12C);
        for (int i = 0; i < 100; i++) { r.io.delay_ms(100); fbwin_poll(&r.c); }
        CHECK_EQ(r.io.log.size(), n3);
        CHECK_EQ(r.px.w.size(), p3);
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 138);
    }

    // ---- no frames come (the machine is stopped): the move is done after frames_ms anyway
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        r.io.regs[AM_FEAT] = AM_FEAT_FBWIN | 1;
        engines_of(&r.c);
        Video vid(amiga1200_desc.video, &r.px, &r.io);
        r.c.video = &vid;
        vid.init(); vid.show();
        set_diw(r.io, 0x91, 0x1A1, 0x2C, 0x12C);
        run(r, 15, 100, false);                                              // it moved after a second, the frames have not come
        CHECK_EQ(r.io.regs[AM_FB_SKIP_X], 20);
        CHECK(r.c.eng->fw.phase == FBW_FILL);
        run(r, 10, 100, false);
        CHECK(r.c.eng->fw.phase == FBW_IDLE);
        CHECK(r.c.eng->fw.applied.skip_x == 20);
        CHECK_EQ(r.px.last(PX_SIZE), (540u << 16) | 720u);
    }

    // ---- a gateware without the registers: the picture stays as it was
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        r.io.regs[AM_FEAT] = 1;
        engines_of(&r.c);
        set_diw(r.io, 0x91, 0x1A1, 0x2C, 0x12C);                             // a window that would move the picture
        run(r, 20, 100);
        CHECK_EQ(r.io.log.size(), 0u);
    }
    DONE("fbwin");
}
