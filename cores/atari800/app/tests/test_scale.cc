// T107 D7/D8, T164 G14: a core on a host whose gateware owns the picture (the status register answers, video_gateware_present): the real frame of
// each mode, the three scalings (Integer without the cap of 3, Fit, Fill), the Scale and Smoothing items the application adds next to Picture for
// every frame-buffer core, and the mode going to the gateware's register instead of the host's firmware calls. Every number in the tables is
// worked out by hand.
#include <string>
#include <utility>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "boot.h"
#include "desc_check.h"
#include "picture.h"
#include "videomode.h"
#include "regset.h"
#include "menutree.h"

// the host: the firmware path (video_apply) and the gateware's request (video_apply_gateware), which moves the status row when it succeeds
static std::vector<std::pair<int, int>> g_fw, g_gw;
static int g_gw_row = 2;                         // the gateware starts in 1080p50
static bool g_gw_ok = true;
void video_apply(int sys, int hdmi) { g_fw.push_back({ sys, hdmi }); }
bool video_apply_gateware(int sys, int hdmi)
{
    g_gw.push_back({ sys, hdmi });
    if (g_gw_ok) {
        g_gw_row = video_mode_index(hdmi, sys);
    }
    return g_gw_ok;
}
bool video_gateware_present(void) { return true; }
int video_gateware_row(void) { return g_gw_row; }

// T239.5 D13: built as the Atari's app (a8_scale): its frame buffer gets the picture items too; the cases on the Amiga's record are the shared unit's
extern const CoreDesc atari800_desc;            // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, &testcore::desc, 0 };

struct L { int w, h, hdmi, sys, mode, bx, by, ow, oh, x0, y0; };

// T235: the Picture value of a resolution on a PAL machine (the list is every row of the table, the machine's rate first within a resolution)
static int pv(int hdmi) { static const int v[VID_HDMI_COUNT] = { 1, 3, 5, 7, 8, 9 }; return v[hdmi]; }

static void layouts()
{
    static const L t[] = {
        // the A1200: 720 x 540
        { 720, 540, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 1440, 1080, 240, 0 },
        { 720, 540, VID_HDMI_720P,    VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 720, 540, 280, 90 },
        { 720, 540, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_FIT,     0, 0, 1440, 1080, 240, 0 },
        { 720, 540, VID_HDMI_720P,    VID_SYS_PAL50,  VID_SCALE_FIT,     0, 0, 960, 720, 160, 0 },
        { 720, 540, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_FILL,    0, 0, 1920, 1080, 0, 0 },
        { 720, 540, VID_HDMI_480_576, VID_SYS_PAL50,  VID_SCALE_FILL,    0, 0, 720, 576, 0, 0 },
        { 720, 540, VID_HDMI_480_576, VID_SYS_PAL50,  VID_SCALE_FIT,     0, 0, 720, 540, 0, 18 },
        { 720, 540, VID_HDMI_480_576, VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 720, 540, 0, 18 },
        { 720, 540, VID_HDMI_480_576, VID_SYS_NTSC60, VID_SCALE_FILL,    0, 0, 640, 480, 0, 0 },
        { 720, 540, VID_HDMI_480_576, VID_SYS_NTSC60, VID_SCALE_INTEGER, 0, 0, 720, 540, 0, 0 },   // too big even at x1: 0, 0, cut by the frame
        { 720, 540, VID_HDMI_800x600, VID_SYS_PAL50,  VID_SCALE_FIT,     0, 0, 800, 600, 0, 0 },
        { 720, 540, VID_HDMI_1024x768, VID_SYS_NTSC60, VID_SCALE_FILL,   0, 0, 1024, 768, 0, 0 },
        { 720, 540, VID_HDMI_1024x768, VID_SYS_NTSC60, VID_SCALE_FIT,    0, 0, 1024, 768, 0, 0 },
        { 720, 540, VID_HDMI_1280x1024, VID_SYS_PAL50, VID_SCALE_FIT,    0, 0, 1280, 960, 0, 32 },
        { 720, 540, VID_HDMI_1280x1024, VID_SYS_PAL50, VID_SCALE_INTEGER, 0, 0, 720, 540, 280, 242 },
        // no cap of 3: 100 x 100 fits 10 times
        { 100, 100, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 1000, 1000, 460, 40 },
        { 320, 256, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 1280, 1024, 320, 28 },      // Amiga lores: x4 (issue #8)
        // the offset of the core scales with the picture (the Atari's 14 pixels)
        { 384, 288, VID_HDMI_1080P,   VID_SYS_PAL50,  VID_SCALE_INTEGER, 14, 0, 1152, 864, 426, 108 },
        // too big for the frame: x1 at 0, 0
        { 2000, 1100, VID_HDMI_1080P, VID_SYS_PAL50,  VID_SCALE_INTEGER, 0, 0, 2000, 1100, 0, 0 },
    };
    for (const L &l : t) {
        int ow = -1, oh = -1, x = -1, y = -1;
        video_layout_scaled(l.w, l.h, l.hdmi, l.sys, l.mode, l.bx, l.by, &ow, &oh, &x, &y);
        if (ow != l.ow || oh != l.oh || x != l.x0 || y != l.y0) {
            printf("layout %dx%d mode %d sys %d scale %d: got %dx%d at %d,%d\n", l.w, l.h, l.hdmi, l.sys, l.mode, ow, oh, x, y);
        }
        CHECK_EQ(ow, l.ow);
        CHECK_EQ(oh, l.oh);
        CHECK_EQ(x, l.x0);
        CHECK_EQ(y, l.y0);
    }
    int w, h;
    video_active_size(VID_HDMI_480_576, VID_SYS_PAL50, &w, &h);   CHECK(w == 720 && h == 576);
    video_active_size(VID_HDMI_480_576, VID_SYS_NTSC60, &w, &h);  CHECK(w == 640 && h == 480);
    video_active_size(VID_HDMI_1080P, VID_SYS_NTSC60, &w, &h);    CHECK(w == 1920 && h == 1080);
    CHECK_EQ(video_mode_index(VID_HDMI_1080P, VID_SYS_PAL50), 2);
    CHECK_EQ(video_mode_index(VID_HDMI_480_576, VID_SYS_NTSC60), 6);
    CHECK_EQ(video_mode_index(VID_HDMI_1280x1024, VID_SYS_NTSC60), 11);
    CHECK_STR(video_scale_name(VID_SCALE_FIT), "Fit");
}

int g_follow_written = -1;
bool video_set_follow_gateware(int on) { g_follow_written = on; return true; }              // T234: the board's write to 0x4F02
int g_vic_scale_written = -1;
bool video_set_scale_gateware(int scale) { g_vic_scale_written = scale; return true; }   // T222: the board's write to 0x4F01


struct Rig {
    FakeIo io; FakeFs fs; PxRec px; CoreDesc ext; Setting items[SETTINGS_MAX + 4]; CoreCtx c; Settings *st; Video *vid;
    const char *why;
    Rig(const CoreDesc *d, bool gateware = true, uint16_t mask = EDID_ROW_MASK, int sys = VID_SYS_PAL50)
    {
        memset(&c, 0, sizeof(c));
        g_gw_row = 2;
        g_gw_ok = true;
        PictureEnv env = { mask, 0, gateware, sys };
        why = picture_extend(d, &ext, items, &env);
        c.desc = &ext; c.io = &io; c.fs = &fs;
        st = new Settings(ext.settings, ext.nsettings, d->store, &fs, "/usb0/cores/x.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
    }
};

static void items_and_apply()
{
    // T164 G14: Picture, Scale and Smoothing for every frame-buffer core on a gateware host; a host without it (an old firmware) has Picture alone
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    { Rig r(&SC_REC);
      CHECK(!r.why);
      CHECK_EQ(r.ext.nsettings, SC_REC.nsettings + 3);
      CHECK_EQ(picture_index(&r.ext), SC_REC.nsettings);
      CHECK_EQ(scale_index(&r.ext), SC_REC.nsettings + 1);
      CHECK_EQ(smooth_index(&r.ext), SC_REC.nsettings + 2);
      { const Setting &m = r.ext.settings[smooth_index(&r.ext)];
        CHECK_STR(m.key, "Smoothing"); CHECK_EQ(m.nvalues, 2); CHECK_STR(m.values[0], "Off"); CHECK_STR(m.values[1], "On"); CHECK_EQ(m.def, 0); }
      CHECK(scale_index(&r.ext) >= 0);
      if (scale_index(&r.ext) >= 0) {
          const Setting &s = r.ext.settings[scale_index(&r.ext)];
          CHECK_STR(s.key, "Scale");
          CHECK_EQ(s.nvalues, 3);
          CHECK_STR(s.values[0], "Integer");
          CHECK_STR(s.values[2], "Fill");
          CHECK_EQ(s.def, 0);
          CHECK(s.when == WHEN_LIVE);
      }
      CHECK(!desc_check(&SC_REC));
      OpRegistry reg; picture_register(reg);
      CHECK(reg.find_op(SCALE_OP) != 0);
      CHECK(reg.find_op(SMOOTH_OP) != 0);
    }
#endif
    // every frame-buffer core has them, not only the A1200 (the A500, the Atari and the NES too) ...
#ifdef SC_REC
    const CoreDesc *others[] = { &SC_REC, 0 };
#else
    const CoreDesc *others[] = { &atari800_desc, 0 };
#endif
    for (const CoreDesc *const *p = others; *p; p++) {
        Rig r(*p);
        CHECK_EQ(r.ext.nsettings, (*p)->nsettings + 3);
        CHECK_EQ(scale_index(&r.ext), (*p)->nsettings + 1);
        CHECK_EQ(smooth_index(&r.ext), (*p)->nsettings + 2);
        // ... and none of them on a host whose gateware does not answer
        Rig o(*p, false);
        CHECK_EQ(o.ext.nsettings, (*p)->nsettings + 1);
        CHECK_EQ(scale_index(&o.ext), -1);
        CHECK_EQ(smooth_index(&o.ext), -1);
    }
}


int main()
{
    layouts();
    items_and_apply();
    DONE("scale");
}
