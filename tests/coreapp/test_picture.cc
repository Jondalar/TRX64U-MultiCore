// T107: the application owns the picture and the person chooses it. The "Picture" setting every core gets without
// declaring it, the layout that follows a mode (scale and position), and the start-up path that has to use the same
// layout: a core that switches to 720p on its own must not keep a position computed for 1920 x 1080.
#include <string>
#include <utility>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "boot.h"
#include "menuctl.h"
#include "menutree.h"
#include "keyboard.h"
#include "desc_check.h"
#include "picture.h"
#include "picmodes.h"
#include "videomode.h"

// videomode.cc is built without the board (VIDEOMODE_NO_BOARD); this bench brings the one function it needs.
static std::vector<std::pair<int, int>> g_applied;
void video_apply(int sys, int hdmi) { g_applied.push_back({ sys, hdmi }); }

const CoreDesc *const core_table[] = { 0 };   // the shared unit: the frame and the made-up core (T239.5 D13, D61)

struct Lay { int w, h, hdmi, sys, bx, by, scale, x0, y0; };

static void layout_table()
{
    // every row is worked out by hand from the one frame the host puts out, 1920 x 1080, in every mode (T107 5.6): the mode
    // column is there to show that it changes nothing
    static const Lay t[] = {
        // NES 256 x 224
        { 256, 224, VID_HDMI_480_576, VID_SYS_PAL50,  0, 0, 3, 576, 204 },
        { 256, 224, VID_HDMI_480_576, VID_SYS_NTSC60, 0, 0, 3, 576, 204 },
        { 256, 224, VID_HDMI_720P,    VID_SYS_PAL50,  0, 0, 3, 576, 204 },
        { 256, 224, VID_HDMI_1080P,   VID_SYS_PAL50,  0, 0, 3, 576, 204 },
        { 256, 224, VID_HDMI_800x600,   VID_SYS_PAL50, 0, 0, 3, 576, 204 },
        { 256, 224, VID_HDMI_1280x1024, VID_SYS_PAL50, 0, 0, 3, 576, 204 },
        // Atari 384 x 288, shown 14 source pixels right of the centre
        { 384, 288, VID_HDMI_480_576, VID_SYS_PAL50,  14, 0, 3, 426, 108 },
        { 384, 288, VID_HDMI_480_576, VID_SYS_NTSC60, 14, 0, 3, 426, 108 },
        { 384, 288, VID_HDMI_720P,    VID_SYS_PAL50,  14, 0, 3, 426, 108 },
        { 384, 288, VID_HDMI_1080P,   VID_SYS_PAL50,  14, 0, 3, 426, 108 },
        // the cap: a 100 x 100 picture would fit 10 times in 1080p and still goes out at scale 3
        { 100, 100, VID_HDMI_1080P,   VID_SYS_PAL50,  0, 0, 3, 810, 390 },
        // too big for the frame even at scale 1: scale 1 at 0, 0, not off the frame
        { 2000, 1100, VID_HDMI_1080P, VID_SYS_PAL50,  0, 0, 1, 0, 0 },
        { 2000, 600, VID_HDMI_480_576, VID_SYS_PAL50, 0, 0, 1, 0, 240 },
        // an offset never pushes the picture out of the frame
        { 1900, 100, VID_HDMI_1080P,  VID_SYS_PAL50,  50, 0, 1, 20, 490 },
        { 1900, 100, VID_HDMI_1080P,  VID_SYS_PAL50, -50, 0, 1, 0, 490 },
    };
    for (const Lay &l : t) {
        int s = -1, x = -1, y = -1;
        video_layout(l.w, l.h, l.hdmi, l.sys, l.bx, l.by, &s, &x, &y);
        if (s != l.scale || x != l.x0 || y != l.y0) {
            printf("layout %dx%d mode %d sys %d: got %d at %d,%d\n", l.w, l.h, l.hdmi, l.sys, s, x, y);
        }
        CHECK_EQ(s, l.scale);
        CHECK_EQ(x, l.x0);
        CHECK_EQ(y, l.y0);
        int mw, mh;
        video_frame_size(l.hdmi, l.sys, &mw, &mh);
        CHECK(x >= 0 && y >= 0);
        if (l.w * s <= mw) CHECK(x + l.w * s <= mw);
        if (l.h * s <= mh) CHECK(y + l.h * s <= mh);
    }
}

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; CoreDesc ext; Setting items[SETTINGS_MAX + 4]; CoreCtx c; Settings *st; Video *vid;
    const char *why;
    Rig(const CoreDesc *d, FakeFs *shared = 0, const char *cfg = "/usb0/cores/x.cfg")
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        why = picture_extend(d, &ext, items);
        c.desc = &ext; c.io = &io; c.fs = &fs;
        st = new Settings(ext.settings, ext.nsettings, d->store, &fs, cfg, 0);
        st->load();
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
    }
};

static void extension()
{
    const CoreDesc *all[] = { &testcore::desc, 0 };
    for (const CoreDesc *const *p = all; *p; p++) {
        const CoreDesc *d = *p;
        CoreDesc ext; Setting items[SETTINGS_MAX];
        CHECK(!picture_extend(d, &ext, items));
        CHECK_EQ(ext.nsettings, d->nsettings + 1);
        for (int i = 0; i < d->nsettings; i++) CHECK(!memcmp(&ext.settings[i], &d->settings[i], sizeof(Setting)));
        const Setting &s = ext.settings[ext.nsettings - 1];
        CHECK_STR(s.key, "Picture");
        CHECK_EQ(picture_index(&ext), ext.nsettings - 1);
        CHECK_EQ(picture_index(d), -1);                    // the table's descriptor is untouched
        CHECK_EQ(s.nvalues, 1 + VID_HDMI_COUNT);
        CHECK_STR(s.values[0], "Auto");
        for (int m = 0; m < VID_HDMI_COUNT; m++) CHECK_STR(s.values[1 + m], video_hdmi_name(m));
        CHECK_EQ(s.def, 0);
        CHECK_EQ(s.when, WHEN_LIVE);
        CHECK_STR(s.apply, PICTURE_OP);
        // it reaches the menu: one more entry in the Settings tab, with its values
        MenuTree t;
        menu_build(&ext, &t);
        static char buf[8192];
        menu_dump(&ext, &t, buf, sizeof(buf));
        CHECK(strstr(buf, "setting Picture = Auto | 576p / 480p | 1280 x 720 | 1920 x 1080") != 0);
        CHECK(strstr(buf, "(default Auto, live)") != 0);
    }

    // room: a core with SETTINGS_MAX - 1 settings of its own gets the last place; a core with SETTINGS_MAX is refused with a reason, never overflowed
    static Setting own[SETTINGS_MAX + 1];
    for (int i = 0; i < SETTINGS_MAX + 1; i++) { own[i] = testcore::settings[0]; }
    static char keys[SETTINGS_MAX + 1][8];
    for (int i = 0; i < SETTINGS_MAX + 1; i++) { snprintf(keys[i], 8, "K%d", i); own[i].key = keys[i]; }
    CoreDesc d = testcore::desc, ext; Setting items[SETTINGS_MAX + 4];
    d.settings = own; d.nsettings = SETTINGS_MAX - 1;
    CHECK(!desc_check(&d));
    CHECK(!picture_extend(&d, &ext, items));
    CHECK_EQ(ext.nsettings, SETTINGS_MAX);
    d.nsettings = SETTINGS_MAX;
    const char *why = desc_check(&d);
    CHECK(why && strstr(why, "Picture") && strstr(why, "SETTINGS_MAX"));
    CHECK(picture_extend(&d, &ext, items) != 0);
    // the key is the application's
    static Setting mine[1] = { { "picture", testcore::av, 2, 0, WHEN_LIVE, "test.set_a" } };
    d = testcore::desc; d.settings = mine; d.nsettings = 1;
    why = desc_check(&d);
    CHECK(why && strstr(why, "Picture"));
    CHECK(picture_extend(&d, &ext, items) != 0);
}

static bool pos_is(Rig &r, int scale, int x0, int y0)
{
    return r.vid->scale() == scale && r.vid->x0() == x0 && r.vid->y0() == y0;
}

static int pic(Rig &r, int hdmi) { (void)r; return 1 + hdmi; }


static void runtime_change()
{
}


// T187 D5: the shared page logic (picmodes.cc) - the info line wording of the spec, the tags, the cursor rule that never applies on a move
static void picmodes_shared()
{
    PictureModes pm;
    memset(&pm, 0, sizeof(pm));
    char b[200];
    pm.n = 5;
    picmodes_info(&pm, b, sizeof(b));
    CHECK_STR(b, "Your screen offers 4 modes. Nothing changes until you pick one.");
    pm.n = 2;
    picmodes_info(&pm, b, sizeof(b));
    CHECK_STR(b, "Your screen offers 1 mode. Nothing changes until you pick one.");
    pm.fallback = true;
    picmodes_info(&pm, b, sizeof(b));
    CHECK_STR(b, "This screen names no 50 Hz modes; 60 Hz modes are listed.");
    pm.sys60 = true;
    picmodes_info(&pm, b, sizeof(b));
    CHECK_STR(b, "This screen names no 60 Hz modes; 50 Hz modes are listed.");
    pm.no_edid = true;
    picmodes_info(&pm, b, sizeof(b));
    CHECK_STR(b, "The screen did not name its modes; the standard TV modes are listed.");
    pm.n = 3; pm.cur = 1;
    pm.row[1].now = true; pm.row[1].native = true; pm.row[2].native = true; pm.row[1].refresh_dhz = 500;
    picmodes_tag_text(&pm, 1, b, sizeof(b));
    CHECK_STR(b, "now  native");
    picmodes_tag_text(&pm, 2, b, sizeof(b));
    CHECK_STR(b, "native");
    picmodes_tag_text(&pm, 0, b, sizeof(b));
    CHECK_STR(b, "");
    picmodes_refresh_text(&pm, 1, b, sizeof(b));
    CHECK_STR(b, "50.0 Hz");
    picmodes_refresh_text(&pm, 0, b, sizeof(b));
    CHECK_STR(b, "");
    int sel = 1;                                               // browsing moves the cursor and asks for nothing; RETURN on the mode in force closes, on another applies
    CHECK(picmodes_key(&sel, 3, 1, PMK_DOWN) == PMA_REDRAW && sel == 2);
    CHECK(picmodes_key(&sel, 3, 1, PMK_DOWN) == PMA_REDRAW && sel == 2);
    CHECK(picmodes_key(&sel, 3, 1, PMK_OK) == PMA_APPLY);
    CHECK(picmodes_key(&sel, 3, 2, PMK_OK) == PMA_CLOSE);
    CHECK(picmodes_key(&sel, 3, 1, PMK_BACK) == PMA_CLOSE);
    sel = 1;
    CHECK(picmodes_key(&sel, 2, 0, PMK_OK) == PMA_CLOSE);          // one mode: Auto and the mode are one picture, RETURN on either closes (D5 edge case 1)
    sel = 0;
    CHECK(picmodes_key(&sel, 2, 0, PMK_OK) == PMA_CLOSE);
    sel = 2;
    CHECK(picmodes_key(&sel, 3, 1, PMK_OTHER) == PMA_NONE);
    PictureModes q;                                            // a PC mode has only a 60 Hz row: listed beside the 50 Hz television rows it is no fallback; a television row of the other family is
    memset(&q, 0, sizeof(q));
    picmodes_set_row(&q, 1, "576p / 480p", VID_HDMI_480_576, (uint16_t)((1u << 0) | (1u << 9)), 0, VID_SYS_PAL50, -1, true);
    picmodes_set_row(&q, 2, "800 x 600", VID_HDMI_800x600, (uint16_t)((1u << 0) | (1u << 9)), 0, VID_SYS_PAL50, -1, true);
    CHECK(!q.fallback);
    picmodes_set_row(&q, 3, "1280 x 720", VID_HDMI_720P, (uint16_t)(1u << 7), 0, VID_SYS_PAL50, -1, true);
    CHECK(q.fallback);
    // T235: the full list - a row of the table with its own rate, the mark of the rows the screen names, the foot line that explains it
    {
        PictureModes f;
        memset(&f, 0, sizeof(f));
        f.n = 3;
        picmodes_set_row_at(&f, 0, "Auto", 8, false, 0, 8, true);
        picmodes_set_row_at(&f, 1, "1280 x 720", 1, true, 0, 8, false);                 // 720p50: named
        picmodes_set_row_at(&f, 2, "1280 x 720", 7, false, 1u << 7, 8, false);         // 720p60: not named
        CHECK(f.full && !f.fallback);
        CHECK(f.row[0].now && !f.row[0].named && f.row[0].mode == -1);
        CHECK(f.row[1].named && f.row[1].mode == 1 && !f.row[1].now);
        CHECK(!f.row[2].named && f.row[2].mode == 7);
        picmodes_refresh_text(&f, 1, b, sizeof(b));
        CHECK_STR(b, "50.1 Hz");                                                        // the raster's own rate (hdmi_modes.vh row 1)
        picmodes_refresh_text(&f, 2, b, sizeof(b));
        CHECK_STR(b, "59.8 Hz");
        picmodes_tag_text(&f, 1, b, sizeof(b));
        CHECK_STR(b, "*");
        picmodes_tag_text(&f, 2, b, sizeof(b));
        CHECK_STR(b, "native");
        f.row[1].now = true;
        picmodes_tag_text(&f, 1, b, sizeof(b));
        CHECK_STR(b, "now  *");
        picmodes_info(&f, b, sizeof(b));
        CHECK(strstr(b, "* = the screen names this mode") != 0);
        f.no_edid = true;
        picmodes_info(&f, b, sizeof(b));
        CHECK(strstr(b, "did not name its modes") != 0 && !strstr(b, "* ="));
        picmodes_set_row_at(&f, 1, "1280 x 720", 1, true, 0, 1, false);                // the mark needs the cursor key too: the same name at 50 and 60 Hz is two rows
        PictureModes g = f;
        g.n = 3;
        PicPage pp;
        picpage_open(&pp, &f);
        pp.sel = 2;
        picpage_refresh(&pp, &g, true);
        CHECK_EQ(pp.sel, 2);                                                            // the cursor keeps the 60 Hz row, not the first row of that name
    }
    picmodes_confirm(8, "1280 x 720, 50.0 Hz", "1920 x 1080, 50.0 Hz", b, sizeof(b));
    CHECK_STR(b, "1280 x 720, 50.0 Hz\nIn 8 s 1920 x 1080, 50.0 Hz comes back.\n\nRETURN keeps it      LEFT or ESC goes back now");
    picmodes_kept_text("1280 x 720", b, sizeof(b));                // the D4 chip after the question: kept, or the mode that came back
    CHECK_STR(b, "Picture 1280 x 720");
    picmodes_back_text("1920 x 1080", b, sizeof(b));
    CHECK(strstr(b, "Picture") && strstr(b, "back to 1920 x 1080"));
    // T187 D9: one "now" mark, on the chosen row. Auto chosen: Auto's row, the row of the mode it resolved to has none; a fixed mode: its row only; the cursor opens on it
    for (int cur = 0; cur < 3; cur++) {
        PictureModes d;
        memset(&d, 0, sizeof(d));
        d.n = 3; d.cur = cur;
        const int hd[3] = { VID_HDMI_720P, VID_HDMI_1080P, VID_HDMI_720P };           // Auto resolved to 720p (the same as row 2)
        for (int i = 0; i < 3; i++) {
            picmodes_set_row(&d, i, "x", hd[i], 0xFFFF, 0, VID_SYS_PAL50, hd[cur], cur == 0);
        }
        int nm = 0;
        for (int i = 0; i < 3; i++) {
            nm += d.row[i].now;
            CHECK(d.row[i].now == (i == cur));
        }
        CHECK_EQ(nm, 1);
        PicPage dp;
        picpage_open(&dp, &d);
        CHECK_EQ(dp.sel, cur);
    }
    CHECK(strstr(picmodes_confirm_keys(), "RETURN keeps it") && !strstr(picmodes_confirm_keys(), "mode"));
    // hot-plug (PS_LIST_CHANGED): the list is rebuilt; the cursor keeps its mode by name, and goes to the mode in force when that mode is gone
    PictureModes a;
    memset(&a, 0, sizeof(a));
    a.n = 4; a.cur = 1;
    snprintf(a.row[0].name, sizeof(a.row[0].name), "Auto");
    snprintf(a.row[1].name, sizeof(a.row[1].name), "576p / 480p");
    snprintf(a.row[2].name, sizeof(a.row[2].name), "1280 x 720");
    snprintf(a.row[3].name, sizeof(a.row[3].name), "1920 x 1080");
    PicPage pg;
    picpage_open(&pg, &a);
    CHECK_EQ(pg.sel, 1);                                           // starts on the mode in force
    CHECK(picpage_key(&pg, PMK_DOWN) == PMA_REDRAW && pg.sel == 2);
    CHECK(picpage_key(&pg, PMK_DOWN) == PMA_REDRAW && pg.sel == 3);
    PictureModes b2 = a;                                           // the screen now names fewer modes: 1920 x 1080 moves to row 2
    b2.n = 3;
    snprintf(b2.row[2].name, sizeof(b2.row[2].name), "1920 x 1080");
    picpage_refresh(&pg, &b2, true);
    CHECK_EQ(pg.sel, 2);
    PictureModes b3 = a;                                           // the mode under the cursor is gone: the cursor goes to the mode in force
    b3.n = 2;
    picpage_refresh(&pg, &b3, true);
    CHECK_EQ(pg.sel, 1);
    picpage_open(&pg, &a);
    CHECK(picpage_key(&pg, PMK_OK) == PMA_CLOSE);                  // RETURN on the mode in force applies nothing
    CHECK(picpage_key(&pg, PMK_BACK) == PMA_CLOSE);
}

// T104.2 D5: a core that draws its own picture has no wish. Auto leaves the screen in the mode it is in; a chosen mode is still applied.

// T107 5.x (2026-10-20): the host's output is always 1920 x 1080 (host/overlay.v:8, host/vic_scaler.v), whatever mode the
// application asked for, so the layout is the 1080p layout in EVERY mode and system. Apple II 280 x 192 is 560 x 384 at scale 2
// in its descriptor; NES and Atari are the table rows above.
static void fixed_frame()
{
    static const struct { int w, h, bx, by; } pics[] = { { 256, 224, 0, 0 }, { 384, 288, 14, 0 }, { 280, 192, 0, 0 } };
    static const int modes[] = { VID_HDMI_480_576, VID_HDMI_720P, VID_HDMI_800x600, VID_HDMI_1024x768, VID_HDMI_1280x1024 };
    for (const auto &p : pics) {
        int rs, rx, ry;
        video_layout(p.w, p.h, VID_HDMI_1080P, VID_SYS_PAL50, p.bx, p.by, &rs, &rx, &ry);
        for (int sys = 0; sys < 2; sys++) {
            for (int m : modes) {
                int s = -1, x = -1, y = -1;
                video_layout(p.w, p.h, m, sys, p.bx, p.by, &s, &x, &y);
                if (s != rs || x != rx || y != ry) printf("fixed frame %dx%d mode %d sys %d: got %d at %d,%d, 1080p has %d at %d,%d\n", p.w, p.h, m, sys, s, x, y, rs, rx, ry);
                CHECK_EQ(s, rs);
                CHECK_EQ(x, rx);
                CHECK_EQ(y, ry);
            }
        }
        int fw, fh;
        video_frame_size(VID_HDMI_480_576, VID_SYS_PAL50, &fw, &fh);
        CHECK(fw == 1920 && fh == 1080);
        video_frame_size(VID_HDMI_480_576, VID_SYS_NTSC60, &fw, &fh);
        CHECK(fw == 1920 && fh == 1080);
    }
    // through the application: a Picture choice of 576p / 720p moves nothing on the layer
}

int main()
{
    layout_table();
    fixed_frame();
    extension();
    picmodes_shared();
    DONE("test_picture");
}
