// T93 D9: the video service - the layer set for a core's picture, the flip to the newest complete frame, hide and show with
// exactly the registers that were written (the layer's registers cannot be read back, T93 R2).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "trxcanvas.h"
#include "cores.h"
#ifdef TEST_APP_AMIGA                    // T239.5 D13: the Amiga 500's cases run as its app (run.sh amigaapp, unit amiga_video); the shared unit video keeps the others
#include "amiga_regs.h"
extern const CoreDesc amiga_desc;
#endif



int main()
{
#ifndef TEST_APP_AMIGA                   // the Amiga 500's app runs its buffers and its format below (unit amiga_video)
    CHECK_EQ(Video::ctrl_value(2, true), 0x23);           // the Amiga application's PX_CTRL_ON for scale 2, RGB565
    CHECK_EQ(Video::ctrl_value(2, false), 0x22);
    CHECK_EQ(Video::ctrl_value(3, true), 0x33);
#endif
    // T104 5.3: the Amiga's three buffers are gateware parameters. No FB_BASE / FB_STRIDE register exists (register 0 is the ID block), so
    // nothing is written there, and a buffer the descriptor does not have is never shown (a reported 3 would address the byte past the window).
    {
#ifdef TEST_APP_AMIGA
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
#endif
    }
#ifdef TEST_APP_AMIGA
    // T136 S3: the A500 keeps format 1 and its buffers in the window (the A1200's 32-bit format: its unit a12_base, test_a12base.cc)
    {
        CHECK_EQ(Video::ctrl_value(2, true, 2), 0x25);
        CHECK_EQ(amiga_desc.video.pixel_bytes(), 2u);
        PxRec opx;
        FakeIo oio;
        Video ov(amiga_desc.video, &opx, &oio);
        ov.init();
        ov.show();
        CHECK_EQ(opx.last(PX_CTRL), 0x23);
        CHECK_EQ(opx.last(PX_STRIDE), 720u * 2u);
    }
#endif
    DONE("test_video");
}
