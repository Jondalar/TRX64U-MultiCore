// T93 D9: the video service - the layer set for a core's picture, the flip to the newest complete frame, hide and show with
// exactly the registers that were written (the layer's registers cannot be read back, T93 R2).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "trxcanvas.h"
#include "cores.h"
#include "atari800_regs.h"
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)



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
    // ADR-0057: the mode a core's own numbers ask for. The Atari draws 384 x 288 at scale 3, which needs
    // 1080p's 864 lines (the NES's 720p is its app's unit nes_video). It has no mode written down anywhere:
    // it comes out of the picture, which is why a core added tomorrow needs no entry in a table.
    {
        PxRec px2;
        FakeIo io2;
        Video a8_v(atari800_desc.video, &px2, &io2);
        CHECK_EQ(a8_v.wanted_hdmi(), VID_HDMI_1080P);
    }
    // T104 5.3: the Amiga's three buffers are gateware parameters. No FB_BASE / FB_STRIDE register exists (register 0 is the ID block), so
    // nothing is written there, and a buffer the descriptor does not have is never shown (a reported 3 would address the byte past the window).
    {
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
    DONE("test_video");
}
