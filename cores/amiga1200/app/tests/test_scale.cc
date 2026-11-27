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

// T213.3 (T239.5 D13): built as the A1200's app (a12_scale): the A1200's record is the frame-buffer core of the cases (T107 D7/D8, T136); the NES
// and the Atari (its own app, a8_scale) drop out
#define SC_REC amiga1200_desc
const CoreDesc *const core_table[] = { &amiga1200_desc, &testcore::desc, 0 };

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

// T234: the picture rows of a core whose gateware has Frame follow (the Amiga cores): the item, its place on F5 Video, the write, the stored value
static void follow_item()
{
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    for (const CoreDesc *d : { &SC_REC }) {
        Setting items[SETTINGS_MAX + 4]; CoreDesc ext;
        PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50, false, false, true };
        CHECK(!picture_extend(d, &ext, items, &env));
        CHECK_EQ(ext.nsettings, d->nsettings + 4);                                // Picture, Scale, Smoothing, Frame follow
        int fi = follow_index(&ext);
        CHECK_EQ(fi, d->nsettings + 3);
        if (fi < 0) { return; }
        const Setting &f = ext.settings[fi];
        CHECK_STR(f.key, "Frame follow"); CHECK_EQ(f.nvalues, 2); CHECK_STR(f.values[0], "Off"); CHECK_STR(f.values[1], "On");
        CHECK_EQ(f.def, 0); CHECK(f.when == WHEN_LIVE); CHECK_STR(f.apply, "follow.apply");
        const MenuGroupRoute *rt = menu_route_of(f);                              // D5: with the other picture rows, F5 Video
        CHECK(rt != 0);
        if (rt) { CHECK_STR(rt->area, "F5"); CHECK_STR(rt->tab, "video"); }
        menu_place_groups(menu_place_base(), items, ext.nsettings);                // D32: the group of a base row comes from the base UI TOML, not from picture_extend
        CHECK_EQ(ext.settings[picture_index(&ext)].group != 0 && !strcmp(ext.settings[picture_index(&ext)].group, f.group), 1);
        OpRegistry reg; picture_register(reg);
        CHECK(reg.find_op(FOLLOW_OP) != 0);
        // the gateware without the register (an older host, every other core): no item, the three rows as before
        env.follow = false;
        CHECK(!picture_extend(d, &ext, items, &env));
        CHECK_EQ(ext.nsettings, d->nsettings + 3);
        CHECK_EQ(follow_index(&ext), -1);
    }
#endif
    // a core that draws its own picture has no frame buffer: no item even where the gateware has the register
    // a descriptor may not declare the key itself
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    { CoreDesc bad = SC_REC; Setting s[SETTINGS_MAX + 4]; CoreDesc ext;
      static const char *const v[2] = { "Off", "On" };
      for (int i = 0; i < bad.nsettings; i++) { s[i] = SC_REC.settings[i]; }
      s[bad.nsettings] = Setting(); s[bad.nsettings].key = "Frame follow"; s[bad.nsettings].values = v; s[bad.nsettings].nvalues = 2;
      bad.settings = s; bad.nsettings++;
      Setting items[SETTINGS_MAX + 4];
      PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50, false, false, true };
      CHECK(picture_extend(&bad, &ext, items, &env) != 0);
    }
    // the apply writes the register, the stored value is read back after a restart and applied by picture_start before the mode
    { Setting items[SETTINGS_MAX + 4]; CoreDesc ext; FakeIo io; FakeFs fs; PxRec px;
      PictureEnv env = { EDID_ROW_MASK, 0, true, VID_SYS_PAL50, false, false, true };
      CHECK(!picture_extend(&SC_REC, &ext, items, &env));
      int fi = follow_index(&ext);
      { Settings st(ext.settings, ext.nsettings, SC_REC.store, &fs, "/usb0/cores/x.cfg", 0);
        st.load();
        CHECK_EQ(st.value(fi), 0);                                               // a start with no stored settings: Off
        CHECK(st.set(fi, 1));
        CHECK(st.save());
      }
      Settings st2(ext.settings, ext.nsettings, SC_REC.store, &fs, "/usb0/cores/x.cfg", 0);   // after a restart
      st2.load();
      CHECK_EQ(st2.value(fi), 1);
      CoreCtx c; memset(&c, 0, sizeof(c)); c.desc = &ext; c.io = &io; c.fs = &fs; c.settings = &st2;
      Video vid(SC_REC.video, &px, &io); c.video = &vid;
      g_gw_row = 2; g_gw_ok = true; g_follow_written = -1;
      picture_start(&c, VID_SYS_PAL50, VID_HDMI_1080P);
      CHECK_EQ(g_follow_written, 1);
      follow_apply(&c, 0);
      CHECK_EQ(g_follow_written, 0);
    }
#endif
}

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
    const CoreDesc *others[] = { 0 };                         // the shared unit has no frame-buffer core
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
    // a core that draws its own picture has no frame buffer to scale
    // T222: a core whose picture is the VIC through a gateware with the scale register: Picture and Scale (default Fit), no Smoothing; Scale writes 0x4F01
    // Smoothing (Q3): the scaler's register follows the item, written with every layer set so that it survives the menu
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    { Rig r(&SC_REC);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.vid->init(); r.vid->show();
      CHECK_EQ(r.px.last(PX_CTRL) & 0x40u, 0u);
      smooth_apply(&r.c, 1);
      CHECK(r.vid->smoothing());
      CHECK_EQ(r.px.last(PX_CTRL) & 0x40u, 0x40u);
      r.vid->hide(); r.px.w.clear(); r.vid->show();
      CHECK_EQ(r.px.last(PX_CTRL) & 0x40u, 0x40u);
      smooth_apply(&r.c, 0);
      CHECK_EQ(r.px.last(PX_CTRL) & 0x40u, 0u);
    }
#endif
    // the mode goes to the gateware when it answers, to the firmware otherwise
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    { Rig r(&SC_REC);
      g_fw.clear(); g_gw.clear();
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      CHECK(g_fw.empty() && g_gw.empty());                       // Auto: the A1200 wants what the screen has (1440 x 1080 at 1080p)
      picture_apply(&r.c, pv(VID_HDMI_720P));                    // 1280 x 720 at 50 Hz
      CHECK(g_fw.empty() && g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
      CHECK_EQ(r.vid->out_w(), 720u);                            // Integer at 720p: x1
      scale_apply(&r.c, VID_SCALE_FIT);
      CHECK_EQ(r.vid->scale_mode(), VID_SCALE_FIT);
      CHECK_EQ(r.vid->out_w(), 960u);
      CHECK_EQ(r.vid->out_h(), 720u);
      scale_apply(&r.c, VID_SCALE_FILL);
      CHECK(r.vid->out_w() == 1280u && r.vid->out_h() == 720u);
      r.vid->init(); r.vid->show();
      CHECK_EQ(r.px.last(PX_OSIZE), (720u << 16) | 1280u);
      CHECK_EQ(r.px.last(PX_POS), 0u);
      scale_apply(&r.c, VID_SCALE_INTEGER);
      CHECK_EQ(r.px.last(PX_OSIZE), (540u << 16) | 720u);        // rewritten at once, the layer is on
      CHECK_EQ(r.px.last(PX_POS), (90u << 16) | 280u);
      // the viewed part keeps the ratio: a 360 x 270 window of the 720 x 540 picture at x1 is 360 x 270 output pixels, centred
      r.vid->set_view(360, 270);
      CHECK_EQ(r.px.last(PX_OSIZE), (270u << 16) | 360u);
      CHECK_EQ(r.px.last(PX_POS), (225u << 16) | 460u);
      // the same mode again is not applied twice
      picture_apply(&r.c, pv(VID_HDMI_720P));
      CHECK_EQ(g_gw.size(), 1u);
    }
    // T107 5.10, T235: every Picture value of the A1200 through the item's own path (Settings, regset_apply_setting, the operation, picture_apply): the byte the
    // application writes to the gateware's request register (0x4F00) is the row the value names - the list is every row of the table (9 rows), the machine's rate
    // first within a resolution, and the value says the family itself (no silent fallback any more)
    for (int sys = VID_SYS_PAL50; sys <= VID_SYS_NTSC60; sys++) {
        int rows[10], n = 0;
        for (int h = 0; h < VID_HDMI_COUNT; h++) {
            for (int k = 0; k < 2; k++) {
                int row = (k == 0 ? sys : 1 - sys) * 6 + h;
                if ((EDID_ROW_MASK >> row) & 1u) rows[n++] = row;
            }
        }
        CHECK_EQ(n, 9);
        for (int v = 1; v <= n; v++) {
            Rig r(&SC_REC);
            g_fw.clear(); g_gw.clear();
            picture_start(&r.c, sys, VID_HDMI_1080P);                // the gateware starts in 1080p; a 60 Hz host asks for the 60 Hz family at once
            size_t before = g_gw.size();
            int idx = picture_index(&r.ext);
            CHECK_EQ(r.ext.settings[idx].nvalues, 10);
            CHECK(r.st->set(idx, v));
            regset_apply_setting(&r.c, idx);
            int want = rows[v - 1];
            if (want == sys * 6 + VID_HDMI_1080P) {
                CHECK_EQ(g_gw.size(), before);                       // the mode that is up: nothing is written
            } else {
                CHECK_EQ(g_gw.size(), before + 1);
                if (g_gw.size() == before + 1) {
                    CHECK_EQ(g_gw.back().first, want / 6);
                    CHECK_EQ(g_gw.back().second, want % 6);
                    CHECK_EQ(video_mode_index(g_gw.back().second, g_gw.back().first), want);
                }
            }
            char lbl[48];                                            // the label the menu shows for the value is the mode it applies, with its rate
            snprintf(lbl, sizeof(lbl), "%s (%d Hz)", want % 6 == VID_HDMI_480_576 ? (want / 6 ? "480p" : "576p") : video_hdmi_name(want % 6), want / 6 ? 60 : 50);
            CHECK_STR(r.ext.settings[idx].values[v], lbl);
        }
    }
#endif
    // the gateware starts in 1080p at 50 Hz: a host that was handed 60 Hz (or anything else) does not make it so
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    { Rig r(&SC_REC);
      g_fw.clear(); g_gw.clear();
      picture_start(&r.c, VID_SYS_NTSC60, VID_HDMI_1080P);
      CHECK(g_fw.empty() && g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_1080P));
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
    }
#endif
}

// T107 5.9 (Alex, 2026-10-30): the menu canvas (960 x 540, laid out for 540 lines) at a whole-number factor max(1, active height / 540), centred in the
// mode's active area, laid out again with every mode change (also while the menu is up and the video service shows nothing), independent of Scale.
static void menu_places()
{
    struct P { int value, factor, x, y, cw, ch; };       // D10a: the canvas too (960 x 540, or the mode's own size below 960 wide)
    static const P t[] = {                                       // the Picture item's value is the mode + 1
        { pv(VID_HDMI_1080P),     2,   0,   0, 960, 540 },                 // 1920 x 1080: x2 fills it
        { pv(VID_HDMI_720P),      1, 160,  90, 960, 540 },                 // 1280 x 720: x1, never 1.33
        { pv(VID_HDMI_800x600),   1,   0,   0, 800, 600 },                 // D10a: narrower than 960: the canvas is the frame, the compact layout, nothing cut
        { pv(VID_HDMI_1024x768),  1,  32, 114, 960, 540 },
        { pv(VID_HDMI_1280x1024), 1, 160, 242, 960, 540 },
        { pv(VID_HDMI_480_576),   1,   0,   0, 720, 576 },                 // 576p under PAL: 720 x 576, the canvas is the frame
        { pv(VID_HDMI_1080P),     2,   0,   0, 960, 540 },                 // and back: the menu is not left at the small size
    };
#ifdef SC_REC                // the Amiga's record: the two Amiga apps (T239.5 D13: the Amiga 500's cases are its unit amiga_scale)
    Rig r(&SC_REC);
    g_gw.clear();
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    CHECK(!r.px.place.empty());                                  // at start-up, before the menu was ever up
    CHECK(!r.px.place.empty() && r.px.place.back().x == 0 && r.px.place.back().w == 1920 && r.px.place.back().h == 1080);
    // the menu is up: the video service's layer is off (hide), and a mode change from the menu's Picture item still moves the menu
    r.vid->init(); r.vid->hide();
    for (const P &p : t) {
        size_t before = r.px.place.size();
        picture_apply(&r.c, p.value);
        size_t wr = r.px.w.size();
        CHECK(r.px.place.size() > before);
        if (r.px.place.size() > before) {
            const PxRec::Pl &g = r.px.place.back();
            if (g.x != p.x || g.y != p.y || g.w != p.cw * p.factor || g.h != p.ch * p.factor) {
                printf("menu place for picture value %d: got %d,%d %dx%d\n", p.value, g.x, g.y, g.w, g.h);
            }
            CHECK_EQ(g.x, p.x); CHECK_EQ(g.y, p.y);
            CHECK_EQ(g.w, p.cw * p.factor); CHECK_EQ(g.h, p.ch * p.factor);        // a whole factor of the canvas
            CHECK(!r.px.canvas.empty() && r.px.canvas.back().first == p.cw && r.px.canvas.back().second == p.ch);   // the canvas was told before the place
        }
        CHECK_EQ(r.px.w.size(), wr);                             // the menu's place is not a write of the video service's layer set
    }
    // the picture's Scale and size do not move the menu: Fill at 720p is 1280 x 720 for the picture, Integer 720 x 540
    picture_apply(&r.c, pv(VID_HDMI_720P));
    for (int sc : { VID_SCALE_INTEGER, VID_SCALE_FIT, VID_SCALE_FILL }) {
        scale_apply(&r.c, sc);
        CHECK(!r.px.place.empty());
        if (r.px.place.empty()) { break; }
        const PxRec::Pl &g = r.px.place.back();
        CHECK(g.x == 160 && g.y == 90 && g.w == 960 && g.h == 540);
    }
#endif
    // a core on a host without the gateware keeps the layer the way it was: no place at all
}

int main()
{
    layouts();
    menu_places();
    items_and_apply();
    follow_item();
    DONE("scale");
}
