// T93 D9: the video service - the layer set for a core's picture, the flip to the newest complete frame, hide and show with
// exactly the registers that were written (the layer's registers cannot be read back, T93 R2).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "trxcanvas.h"
#include "cores.h"

#include "cores.h"
extern const CoreDesc c64scpu_desc;      // cores.h lists the shared app's cores; the C64SCPU is its own app (cores/c64scpu/app/lib)

// T228: the menu on a monolith-type bitstream (TRX64-Ultimate's). Its pix_layer has no PX_OSIZE and scales by PX_CTRL b5:4, so the LVGL port writes the whole
// factor of the menu's output size over its canvas there (trx_canvas_factor); its frame is the gateware's row (U16), from the start and after every mode.
static void monolith_menu()
{
    CHECK_EQ(trx_canvas_factor(1920, 1080, 960, 540), 2);         // 1080p: 960 x 540 at x2
    CHECK_EQ(trx_canvas_factor(960, 540, 960, 540), 1);           // 720p (and 1024 x 768, 1280 x 1024): x1
    {
        const TrxCanvas c = trx_canvas_for(720, 576);                 // 576p: the compact canvas 720 x 540, 18 lines down (T259 D17: PAL overscan)
        CHECK(c.w == 720 && c.h == 540 && c.factor == 1 && c.x0 == 0 && c.y0 == 18);
        CHECK_EQ(trx_canvas_factor(720, 576, c.w, c.h), 1);          // the frame over its canvas: x1
    }
    CHECK_EQ(trx_canvas_factor(2880, 1620, 960, 540), 3);
    CHECK_EQ(trx_canvas_factor(3840, 2160, 960, 540), 3);         // b5:4 holds 3 at most
    CHECK_EQ(trx_canvas_factor(1920, 540, 960, 540), 1);          // not the same multiple in both axes: the largest that fits
    CHECK_EQ(trx_canvas_factor(0, 0, 960, 540), 1);               // never 0 (the layer reads 0 as 1 anyway)
    for (int hdmi = 0; hdmi < VID_HDMI_COUNT; hdmi++) {
        for (int sys : { VID_SYS_PAL50, VID_SYS_NTSC60 }) {
            int fw = 0, fh = 0;
            video_active_size(hdmi, sys, &fw, &fh);
            TrxCanvas c = trx_canvas_for(fw, fh);
            CHECK_EQ(trx_canvas_factor(c.w * c.factor, c.h * c.factor, c.w, c.h), c.factor);   // b5:4 says what PX_OSIZE says, in every mode
        }
    }
    // the frame at the start: the gateware's row whatever the hand-over says (U16 starts in row 2); the hand-over's mode where it names no row;
    // without the gateware (ue2emu's TRX64 host, a bitstream with no mode request) the fixed 1920 x 1080
    int sys = VID_SYS_PAL50, hdmi = VID_HDMI_720P;
    video_menu_start(true, 2, &sys, &hdmi);
    CHECK(sys == VID_SYS_PAL50 && hdmi == VID_HDMI_1080P);
    video_menu_start(true, 6, &sys, &hdmi);
    CHECK(sys == VID_SYS_NTSC60 && hdmi == VID_HDMI_480_576);
    sys = VID_SYS_PAL50; hdmi = VID_HDMI_720P;
    video_menu_start(true, -1, &sys, &hdmi);
    CHECK(sys == VID_SYS_PAL50 && hdmi == VID_HDMI_720P);
    video_menu_start(false, -1, &sys, &hdmi);
    CHECK_EQ(hdmi, VID_HDMI_1080P);
    sys = -1; hdmi = -1;
    video_menu_start(false, -1, &sys, &hdmi);
    CHECK_EQ(hdmi, VID_HDMI_1080P);
    // a core with its own picture (w == 0, as c64scpu's): with the gateware the menu's canvas and place follow every mode, as a frame-buffer core's do
    struct M { int hdmi, sys, cw, ch, x, y, w, h; };
    static const M ms[] = {
        { VID_HDMI_1080P,   VID_SYS_PAL50,  960, 540,   0,  0, 1920, 1080 },
        { VID_HDMI_720P,    VID_SYS_PAL50,  960, 540, 160, 90,  960,  540 },
        { VID_HDMI_480_576, VID_SYS_PAL50,  720, 540,   0, 18,  720,  540 },
        { VID_HDMI_480_576, VID_SYS_NTSC60, 640, 480,   0,  0,  640,  480 },
        { VID_HDMI_800x600, VID_SYS_NTSC60, 800, 600,   0,  0,  800,  600 },
        { VID_HDMI_1080P,   VID_SYS_NTSC60, 960, 540,   0,  0, 1920, 1080 },
    };
    FakeIo io;
    PxRec px;
    Video v(c64scpu_desc.video, &px, &io);
    CHECK(v.own_picture());
    v.set_gateware(true);
    for (const M &m : ms) {
        CHECK_EQ(v.set_mode(m.hdmi, m.sys), m.hdmi);
        CHECK(!px.place.empty() && !px.canvas.empty());
        if (px.place.empty() || px.canvas.empty()) {
            break;
        }
        const PxRec::Pl &p = px.place.back();
        if (p.x != m.x || p.y != m.y || p.w != m.w || p.h != m.h) printf("own picture, mode %d sys %d: place %d,%d %dx%d\n", m.hdmi, m.sys, p.x, p.y, p.w, p.h);
        CHECK(p.x == m.x && p.y == m.y && p.w == m.w && p.h == m.h);
        CHECK(px.canvas.back().first == m.cw && px.canvas.back().second == m.ch);
    }
    CHECK(px.w.empty());                                            // the layer itself is not touched: it is the menu's (the LVGL port's)
    // without the gateware the frame does not follow the mode: the menu stays where the start put it
    PxRec npx;
    Video nv(c64scpu_desc.video, &npx, &io);
    nv.set_mode(VID_HDMI_720P, VID_SYS_PAL50);
    CHECK(npx.place.empty() && npx.canvas.empty());
}

int main()
{
    monolith_menu();
    CHECK_EQ(Video::ctrl_value(2, true), 0x23);           // the Amiga application's PX_CTRL_ON for scale 2, RGB565
    CHECK_EQ(Video::ctrl_value(2, false), 0x22);
    CHECK_EQ(Video::ctrl_value(3, true), 0x33);
    // T104.2 D5, S5: w == 0 says the core draws its own picture (the C64 family's goes through Gideon's video path). The service writes no register
    // of the core - they would be the ID bytes - and never puts a picture on the layer.
    {
        const VideoDesc &c = c64scpu_desc.video;
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
    }
    DONE("test_video");
}
