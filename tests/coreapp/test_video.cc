// T93 D9: the video service - the layer set for a core's picture, the flip to the newest complete frame, hide and show with
// exactly the registers that were written (the layer's registers cannot be read back, T93 R2).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "cores.h"
#include "atari800_regs.h"
#include "amiga_regs.h"

int main()
{
    const VideoDesc &d = atari800_desc.video;
    // the numbers of T88 D5: 384 x 288 at scale 3 = 1152 x 864, centred on 1920 x 1080 and shifted by 14 hires pixels
    CHECK_EQ(d.w * d.scale, 1152);
    CHECK_EQ(d.h * d.scale, 864);
    CHECK_EQ(d.x0, (1920 - 1152) / 2 + 14 * 3);
    CHECK_EQ(d.y0, (1080 - 864) / 2);
    CHECK(d.x0 + (int)(d.w * d.scale) <= 1920);
    CHECK_EQ(Video::ctrl_value(2, true), 0x23);           // the Amiga application's PX_CTRL_ON for scale 2, RGB565
    CHECK_EQ(Video::ctrl_value(2, false), 0x22);
    CHECK_EQ(Video::ctrl_value(3, true), 0x33);

    FakeIo io;
    PxRec px;
    Video v(d, &px, &io);
    v.init();
    CHECK(px.w.empty());                                  // init() leaves the layer alone
    CHECK(!v.visible());
    uint32_t zeros = 0;
    for (int b = 0; b < 3; b++) for (uint32_t i = 0; i < A8_FB_BYTES; i += 4097) zeros |= io.ddr[A8_W_FB + b * 0x100000u + i];
    CHECK_EQ(zeros, 0);

    v.show();
    CHECK(v.visible());
    CHECK_EQ(px.last(PX_SIZE), (288u << 16) | 384u);
    CHECK_EQ(px.last(PX_STRIDE), 768);
    CHECK_EQ(px.last(PX_POS), (108u << 16) | 426u);
    CHECK_EQ(px.last(PX_FRONT), 0x01D00000u);
    CHECK_EQ(px.last(PX_GALPHA), 255);
    CHECK_EQ(px.last(PX_CTRL), 0x33);
    // the geometry is never written under a shown layer: CTRL goes off before SIZE/STRIDE/POS, on last
    int first_off = -1, size_at = -1, on_at = -1;
    for (size_t i = 0; i < px.w.size(); i++) {
        if (px.w[i].reg == PX_CTRL && px.w[i].v == 0x32 && first_off < 0) first_off = (int)i;
        if (px.w[i].reg == PX_SIZE) size_at = (int)i;
        if (px.w[i].reg == PX_CTRL && px.w[i].v == 0x33) on_at = (int)i;
    }
    CHECK(first_off >= 0 && first_off < size_at && size_at < on_at);

    // flip: nothing new, nothing written
    size_t n0 = px.w.size();
    CHECK(!v.poll());
    CHECK_EQ(px.w.size(), n0);
    // a new frame in buffer 2
    io.regs[A8_FB_LATEST] = 0x80 | 2;
    io.regs[A8_FRAME_SEQ] = 1;
    CHECK(v.poll());
    CHECK_EQ(px.last(PX_BACK), 0x01D00000u + 2 * 0x100000u);
    CHECK(!v.poll());                                     // the same sequence number again: no second flip
    // a new sequence number but the core says nothing valid
    io.regs[A8_FB_LATEST] = 0x01;
    io.regs[A8_FRAME_SEQ] = 2;
    CHECK(!v.poll());
    CHECK_EQ(px.count(PX_BACK), 1);
    // buffer 3
    io.regs[A8_FB_LATEST] = 0x83;
    io.regs[A8_FRAME_SEQ] = 3;
    CHECK(v.poll());
    CHECK_EQ(px.last(PX_BACK), 0x01D00000u + 3 * 0x100000u);

    // hide: the layer goes off and nothing else is written; frames that arrive while hidden are followed but not shown
    n0 = px.w.size();
    v.hide();
    CHECK(!v.visible());
    CHECK_EQ(px.w.size(), n0 + 1);
    CHECK_EQ(px.last(PX_CTRL), 0x32);
    io.regs[A8_FB_LATEST] = 0x81;
    io.regs[A8_FRAME_SEQ] = 4;
    int backs = px.count(PX_BACK);
    CHECK(v.poll());
    CHECK_EQ(px.count(PX_BACK), backs);                   // no PX_BACK while the menu has the layer
    // show: the whole set again, with the buffer that was newest
    v.show();
    CHECK_EQ(px.last(PX_FRONT), 0x01D00000u + 1 * 0x100000u);
    CHECK_EQ(px.last(PX_SIZE), (288u << 16) | 384u);
    CHECK_EQ(px.last(PX_POS), (108u << 16) | 426u);
    CHECK_EQ(px.last(PX_CTRL), 0x33);
    // ADR-0057: the mode a core's own numbers ask for. The NES draws 256 x 224 at scale 3 and fills
    // 93 % of 720p's height where 1080p leaves 38 % of it black; the Atari draws 384 x 288 at scale 3,
    // which needs 1080p's 864 lines. Neither has a mode written down anywhere: both come out of the
    // picture, which is why a core added tomorrow needs no entry in a table.
    {
        PxRec px2;
        FakeIo io2;
        Video nes_v(nes_desc.video, &px2, &io2);
        Video a8_v(atari800_desc.video, &px2, &io2);
        CHECK_EQ(nes_v.wanted_hdmi(), VID_HDMI_720P);
        CHECK_EQ(a8_v.wanted_hdmi(), VID_HDMI_1080P);
    }
    // T104.2 D5, S5: w == 0 says the core draws its own picture (the C64's goes through Gideon's video path). The service writes no register
    // of the core - they would be the ID bytes - and never puts a picture on the layer.
    {
        const VideoDesc &c = c64_desc.video;
        CHECK_EQ(c.w, 0);
        FakeIo cio;
        PxRec cpx;
        Video cv(c, &cpx, &cio);
        CHECK(cv.own_picture());
        CHECK_EQ(cv.wanted_hdmi(), -1);                       // no wish: the screen keeps its mode
        cv.init();
        CHECK(cv.ready());
        CHECK(cio.log.empty());                               // no FB_BASE, no FB_STRIDE at register offsets 0..7
        CHECK(cpx.w.empty());
        cv.show();
        cv.hide();
        cv.show();
        CHECK(!cv.visible());
        CHECK(!cv.poll());
        cio.regs[0] = 0x80; cio.regs[1] = 0x81;                // whatever the registers at the "sequence" offsets say, there is no flip
        CHECK(!cv.poll());
        CHECK_EQ(cv.set_mode(VID_HDMI_720P, VID_SYS_PAL50), VID_HDMI_720P);
        CHECK_EQ(cv.mode(), VID_HDMI_720P);
        CHECK(cio.log.empty());
        CHECK(cpx.w.size() >= 3);
        for (auto &w : cpx.w) {
            CHECK_EQ(w.reg, PX_CTRL);                         // the layer's control and nothing else
            CHECK_EQ(w.v & 1, 0);                             // and never on
        }
        // the control: the same service with a picture of its own writes FB_BASE and turns the layer on
        VideoDesc e = c;
        e.w = 320; e.h = 200; e.scale = 2; e.x0 = 100; e.y0 = 100;
        e.fb_base = 0x01000000u + 0x800000u; e.fb_stride = 0x100000u; e.nbuf = 3;
        e.reg_latest = 0xC0; e.reg_seq = 0xC1; e.reg_fb_base = 0xC4; e.reg_fb_stride = 0xC8;
        FakeIo eio;
        PxRec epx;
        Video ev(e, &epx, &eio);
        CHECK(!ev.own_picture());
        ev.init();
        CHECK(!eio.log.empty());                              // the register writes are there ...
        CHECK_EQ(eio.regs[0xC4 + 2], 0x80);                   // ... FB_BASE = 0x01800000, little endian
        CHECK_EQ(eio.regs[0xC4 + 3], 0x01);
        CHECK_EQ(eio.regs[0xC8 + 2], 0x10);                   // FB_STRIDE = 0x100000
        ev.show();
        CHECK_EQ(epx.last(PX_CTRL) & 1, 1);
        CHECK(ev.wanted_hdmi() >= 0);
    }

    // T104 5.3: the Amiga's three buffers are gateware parameters. No FB_BASE / FB_STRIDE register exists (register 0 is the ID block), so
    // nothing is written there, and a buffer the descriptor does not have is never shown (a reported 3 would address the byte past the window).
    {
        const VideoDesc &a = amiga_desc.video;
        CHECK_EQ(a.nbuf, 3);
        CHECK_EQ(a.reg_fb_base, 0);
        FakeIo aio;
        PxRec apx;
        Video av(a, &apx, &aio);
        av.init();
        for (auto &w : aio.log) {
            CHECK(w.reg > 7);                                 // not one write into the ID bytes
        }
        CHECK(aio.log.empty());                               // the init of this core writes no register at all
        av.show();
        CHECK_EQ(apx.last(PX_FRONT), 0x01D00000u);
        CHECK_EQ(apx.last(PX_SIZE), (540u << 16) | 720u);
        CHECK_EQ(apx.last(PX_POS), 240u);
        aio.regs[AM_FB_LATEST] = 0x80 | 2;
        aio.regs[AM_FRAME_SEQ] = 1;
        CHECK(av.poll());
        CHECK_EQ(apx.last(PX_BACK), 0x01D00000u + 2 * 0x100000u);
        aio.regs[AM_FB_LATEST] = 0x80 | 3;                    // there is no buffer 3
        aio.regs[AM_FRAME_SEQ] = 2;
        size_t n = apx.w.size();
        CHECK(!av.poll());
        CHECK_EQ(apx.w.size(), n);
        CHECK_EQ(av.front(), 0x01D00000u + 2 * 0x100000u);    // the picture stays where it was
        // the four buffers of the Atari still reach buffer 3
        FakeIo bio;
        PxRec bpx;
        Video bv(atari800_desc.video, &bpx, &bio);
        bv.init();
        bv.show();
        bio.regs[A8_FB_LATEST] = 0x80 | 3;
        bio.regs[A8_FRAME_SEQ] = 1;
        CHECK(bv.poll());
    }
    // T136 S3: the A1200 shows 32-bit pixels (the layer's format 2, its own pixel layer) from buffers at 0x4000000, 2 MB apart; the A500
    // keeps format 1 and its buffers in the window
    {
        CHECK_EQ(Video::ctrl_value(2, true, 2), 0x25);
        CHECK_EQ(amiga_desc.video.pixel_bytes(), 2u);
        const VideoDesc &a = amiga1200_desc.video;
        CHECK_EQ(a.pixel_bytes(), 4u);
        CHECK_EQ(a.fb_base, 0x04000000u);
        CHECK_EQ(a.fb_stride, 0x200000u);
        CHECK(a.fb_stride >= 720u * 540u * 4u);
        FakeIo aio;
        PxRec apx;
        Video av(a, &apx, &aio);
        av.init();                                            // outside the application's window: nothing to clear, nothing written
        CHECK(aio.log.empty());
        av.show();
        CHECK_EQ(apx.last(PX_CTRL), 0x25);
        CHECK_EQ(apx.last(PX_STRIDE), 720u * 4u);
        CHECK_EQ(apx.last(PX_FRONT), 0x04000000u);
        aio.regs[AM_FB_LATEST] = 0x80 | 2;
        aio.regs[AM_FRAME_SEQ] = 1;
        CHECK(av.poll());
        CHECK_EQ(apx.last(PX_BACK), 0x04000000u + 2 * 0x200000u);
        PxRec opx;
        FakeIo oio;
        Video ov(amiga_desc.video, &opx, &oio);
        ov.init();
        ov.show();
        CHECK_EQ(opx.last(PX_CTRL), 0x23);
        CHECK_EQ(opx.last(PX_STRIDE), 720u * 2u);
    }
    DONE("test_video");
}
