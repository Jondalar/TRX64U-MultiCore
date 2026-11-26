// T194 S3: the C128's two pictures on the one pixel layer. The VDC's 640 x 400 buffers (format 1) and the VIC-IIe's capture (format 3, 400 x 270 at 50 Hz,
// 400 x 240 in the 60 Hz family of the System Mode, rows of 208 bytes, no buffer named): Video::select_picture switches, Picture (Integer / Fit / Fill), the HDMI
// mode and Smoothing reach both. Every number in the tables is worked out by hand from video_layout_scaled (Integer: the largest whole factor; Fit: the frame
// filled in one direction; Fill: the frame).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "cores.h"
#include "c128_regs.h"

extern const CoreDesc c128_desc;                // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)

struct P { int hdmi, sys, mode; uint32_t ow, oh, x0, y0; };

int main()
{
    CHECK(c128_desc.alt_video != nullptr);
    const VideoDesc &vic = *c128_desc.alt_video;
    CHECK_EQ(vic.pxfmt, 3);
    CHECK_EQ(vic.w, 400);
    CHECK_EQ(vic.h, 270);
    CHECK_EQ(vic.h60, 240);
    CHECK_EQ(vic.stride_bytes(), 208);                    // 13 words of 16 bytes
    CHECK_EQ(c128_desc.video.stride_bytes(), 1280);       // the VDC: 640 x 2 bytes, as before

    FakeIo io;
    PxRec px;
    Video v(c128_desc.video, &px, &io);
    v.set_alt(c128_desc.alt_video);
    v.set_gateware(true);
    v.init();
    CHECK_EQ(v.picture(), 0);

    // the VDC first (T113 D11: the descriptor is the woven 640 x 480): 1080p Integer = 1280 x 960 at (320, 60), format 1, scale bits 2
    v.set_mode(VID_HDMI_1080P, VID_SYS_PAL50);
    v.show();
    CHECK_EQ(px.last(PX_CTRL), 0x23u);
    CHECK_EQ(px.last(PX_SIZE), (480u << 16) | 640u);
    CHECK_EQ(px.last(PX_STRIDE), 1280u);
    CHECK_EQ(px.last(PX_OSIZE), (960u << 16) | 1280u);
    CHECK_EQ(px.last(PX_POS), (60u << 16) | 320u);
    // a picture that is not interlaced: the layer is cut to 400 rows (1280 x 800 at y 140), and back; the same height again writes nothing
    v.set_rows(400);
    CHECK_EQ(px.last(PX_SIZE), (400u << 16) | 640u);
    CHECK_EQ(px.last(PX_OSIZE), (800u << 16) | 1280u);
    CHECK_EQ(px.last(PX_POS), (140u << 16) | 320u);
    int nsz = px.count(PX_SIZE);
    v.set_rows(400);
    CHECK_EQ(px.count(PX_SIZE), nsz);
    v.set_rows(481);                                      // more than the descriptor holds: refused
    CHECK_EQ(px.last(PX_SIZE), (400u << 16) | 640u);
    v.set_rows(480);
    CHECK_EQ(px.last(PX_SIZE), (480u << 16) | 640u);
    CHECK_EQ(px.last(PX_POS), (60u << 16) | 320u);
    int fronts = px.count(PX_FRONT);
    CHECK(fronts >= 1);

    // the VIC: the layer is on, so the switch rewrites the whole set: off, the geometry, on (format 3: b3:1 = 3, 0x17 = scale bits 1, format 3, on)
    v.select_picture(1);
    CHECK_EQ(v.picture(), 1);
    CHECK_EQ(px.last(PX_CTRL), 0x17u);
    CHECK_EQ(px.last(PX_SIZE), (270u << 16) | 400u);
    CHECK_EQ(px.last(PX_STRIDE), 208u);
    CHECK_EQ(px.last(PX_OSIZE), (1080u << 16) | 1600u);
    CHECK_EQ(px.last(PX_POS), (0u << 16) | 160u);
    CHECK_EQ(px.count(PX_FRONT), fronts);                 // no buffer is named for a capture
    // no flip while it is selected, however the (VDC) frame register moves
    io.regs[C128_FB_LATEST & 0xFFF] = 0x80 | 1;
    io.regs[C128_FRAME_SEQ & 0xFFF] = 7;
    CHECK(!v.poll());

    static const P t[] = {
        // 1080p, PAL (50 Hz family, 400 x 270)
        { VID_HDMI_1080P, VID_SYS_PAL50,  VID_SCALE_INTEGER, 1600, 1080, 160, 0 },
        { VID_HDMI_1080P, VID_SYS_PAL50,  VID_SCALE_FIT,     1600, 1080, 160, 0 },
        { VID_HDMI_1080P, VID_SYS_PAL50,  VID_SCALE_FILL,    1920, 1080, 0, 0 },
        { VID_HDMI_1080P, VID_SYS_NTSC50, VID_SCALE_INTEGER, 1600, 1080, 160, 0 },
        // 1080p, the 60 Hz family (NTSC and PAL-60: 400 x 240): Integer 4x = 1600 x 960, Fit 4.5x = 1800 x 1080
        { VID_HDMI_1080P, VID_SYS_NTSC60, VID_SCALE_INTEGER, 1600, 960, 160, 60 },
        { VID_HDMI_1080P, VID_SYS_NTSC60, VID_SCALE_FIT,     1800, 1080, 60, 0 },
        { VID_HDMI_1080P, VID_SYS_NTSC60, VID_SCALE_FILL,    1920, 1080, 0, 0 },
        { VID_HDMI_1080P, VID_SYS_PAL60,  VID_SCALE_FIT,     1800, 1080, 60, 0 },
        // 720p
        { VID_HDMI_720P,  VID_SYS_PAL50,  VID_SCALE_INTEGER, 800, 540, 240, 90 },
        { VID_HDMI_720P,  VID_SYS_PAL50,  VID_SCALE_FIT,     1066, 720, 107, 0 },
        { VID_HDMI_720P,  VID_SYS_PAL50,  VID_SCALE_FILL,    1280, 720, 0, 0 },
        { VID_HDMI_720P,  VID_SYS_NTSC60, VID_SCALE_INTEGER, 1200, 720, 40, 0 },
        { VID_HDMI_720P,  VID_SYS_NTSC60, VID_SCALE_FIT,     1200, 720, 40, 0 },
        { VID_HDMI_720P,  VID_SYS_NTSC60, VID_SCALE_FILL,    1280, 720, 0, 0 },
    };
    for (const P &p : t) {
        v.set_scale_mode(p.mode);
        v.set_mode(p.hdmi, p.sys);
        bool h60 = p.sys == VID_SYS_NTSC60 || p.sys == VID_SYS_PAL60;
        uint32_t sh = h60 ? 240u : 270u;
        if (px.last(PX_OSIZE) != ((p.oh << 16) | p.ow) || px.last(PX_POS) != ((p.y0 << 16) | p.x0)) {
            printf("mode %d sys %d scale %d: got %ux%u at %u,%u\n", p.hdmi, p.sys, p.mode, px.last(PX_OSIZE) & 0xFFFF, px.last(PX_OSIZE) >> 16,
                   px.last(PX_POS) & 0xFFFF, px.last(PX_POS) >> 16);
        }
        CHECK_EQ(px.last(PX_OSIZE), (p.oh << 16) | p.ow);
        CHECK_EQ(px.last(PX_POS), (p.y0 << 16) | p.x0);
        CHECK_EQ(px.last(PX_SIZE), (sh << 16) | 400u);
        CHECK_EQ(px.last(PX_STRIDE), 208u);
        CHECK_EQ(px.last(PX_CTRL), 0x17u);
    }
    // Board 2026-11-16: a 50 Hz machine (System Mode PAL) on a 1080p 60 Hz row still captures 270 rows, a 60 Hz machine on a 50 Hz row 240: the capture's height
    // is the machine's, the row's family only picks the frame (before: the row's family chose, a PAL machine showed 240 of its 270 lines, picture too low, bottom cut)
    v.set_scale_mode(VID_SCALE_FILL);
    v.set_machine_sys(VID_SYS_PAL50);
    v.set_mode(VID_HDMI_1080P, VID_SYS_NTSC60);
    CHECK_EQ(px.last(PX_SIZE), (270u << 16) | 400u);
    CHECK_EQ(px.last(PX_OSIZE), (1080u << 16) | 1920u);
    CHECK_EQ(px.last(PX_POS), 0u);
    v.set_scale_mode(VID_SCALE_INTEGER);
    CHECK_EQ(px.last(PX_OSIZE), (1080u << 16) | 1600u);          // 4 x 270, centred: 160, 0
    CHECK_EQ(px.last(PX_POS), 160u);
    v.set_machine_sys(VID_SYS_NTSC60);
    v.set_mode(VID_HDMI_1080P, VID_SYS_PAL50);
    CHECK_EQ(px.last(PX_SIZE), (240u << 16) | 400u);
    CHECK_EQ(px.last(PX_OSIZE), (960u << 16) | 1600u);           // 4 x 240, centred: 160, 60
    CHECK_EQ(px.last(PX_POS), (60u << 16) | 160u);
    v.set_machine_sys(VID_SYS_PAL50);                            // the family changes while shown: the height follows at once
    CHECK_EQ(px.last(PX_SIZE), (270u << 16) | 400u);
    v.set_machine_sys(-1);                                       // not told: the row's family stands in, as the table above
    v.set_scale_mode(VID_SCALE_FILL);
    // Smoothing reaches it: b6 of PX_CTRL
    v.set_scale_mode(VID_SCALE_FILL);
    v.set_mode(VID_HDMI_1080P, VID_SYS_PAL50);
    v.set_smoothing(true);
    CHECK_EQ(px.last(PX_CTRL), 0x57u);
    CHECK_EQ(px.last(PX_OSIZE), (1080u << 16) | 1920u);
    v.set_smoothing(false);
    CHECK_EQ(px.last(PX_CTRL), 0x17u);

    // hide and show (the menu, Freeze) keep the selected picture
    v.hide();
    CHECK_EQ(px.last(PX_CTRL), 0x16u);
    v.show();
    CHECK_EQ(px.last(PX_CTRL), 0x17u);
    CHECK_EQ(px.last(PX_SIZE), (270u << 16) | 400u);

    // back to 80 columns: the VDC's own set, with the Picture mode now in force (Fill: 1920 x 1080), and the flip works again
    v.select_picture(0);
    CHECK_EQ(v.picture(), 0);
    CHECK_EQ(px.last(PX_CTRL), 0x23u);
    CHECK_EQ(px.last(PX_SIZE), (480u << 16) | 640u);
    CHECK_EQ(px.last(PX_STRIDE), 1280u);
    CHECK_EQ(px.last(PX_OSIZE), (1080u << 16) | 1920u);
    CHECK_EQ(px.last(PX_POS), 0u);
    CHECK(px.count(PX_FRONT) > fronts);
    CHECK(v.poll());
    CHECK_EQ(px.last(PX_BACK), 0x01D00000u + 1 * 0x100000u);
    // and again to the VIC and back at Integer: nothing of the VIC's geometry stays in the VDC's
    v.set_scale_mode(VID_SCALE_INTEGER);
    v.set_mode(VID_HDMI_1080P, VID_SYS_NTSC60);
    CHECK_EQ(px.last(PX_OSIZE), (960u << 16) | 1280u);
    CHECK_EQ(px.last(PX_POS), (60u << 16) | 320u);
    v.select_picture(1);
    CHECK_EQ(px.last(PX_OSIZE), (960u << 16) | 1600u);
    v.select_picture(0);
    CHECK_EQ(px.last(PX_OSIZE), (960u << 16) | 1280u);
    CHECK_EQ(px.last(PX_POS), (60u << 16) | 320u);
    // wanted_hdmi() answers from the descriptor's own picture while the VIC is selected
    int w0 = v.wanted_hdmi();
    v.select_picture(1);
    CHECK_EQ(v.wanted_hdmi(), w0);
    DONE("test_c128pic");
}
