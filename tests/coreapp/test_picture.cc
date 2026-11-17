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

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, 0 };

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
    const CoreDesc *all[] = { &atari800_desc, &nes_desc, &testcore::desc, 0 };
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

static void nes_startup()
{
    // the hand-over says 1080p PAL; nothing stored: Auto, which for the NES is 720p, and the position moves with it
    g_applied.clear();
    Rig r(&nes_desc);
    CHECK(!r.why);
    CHECK_EQ(nes_desc.video.x0, 576);                          // the descriptor is for 1080p ...
    int mode = picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    CHECK_EQ(mode, VID_HDMI_720P);
    CHECK_EQ(g_applied.size(), 1u);
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
    CHECK(pos_is(r, 3, 576, 204));                             // ... and it stays there: the host's frame is 1080p in every mode (T107 5.6)
    CHECK(r.vid->x0() + 256 * r.vid->scale() <= 1920);
    CHECK(r.vid->y0() + 224 * r.vid->scale() <= 1080);
    r.vid->init();
    r.vid->show();
    CHECK_EQ(r.px.last(PX_POS), (204u << 16) | 576u);
    CHECK_EQ(r.px.last(PX_CTRL), 0x33);
    int sys = -1, hdmi = -1;
    CHECK(video_unpack(video_current, &sys, &hdmi) && sys == VID_SYS_PAL50 && hdmi == VID_HDMI_720P);

    // the boot sequence applies every setting once more: same mode, nothing is set again, nothing moves
    size_t w0 = r.px.w.size();
    boot_apply_settings(&r.c, true);
    CHECK_EQ(g_applied.size(), 1u);
    CHECK_EQ(r.px.w.size(), w0);

    // a stored choice wins over Auto and survives a restart: 1080p chosen, the host is in 1080p, nothing to set, x0 stays 576
    CHECK(r.st->set(picture_index(&r.ext), pic(r, VID_HDMI_1080P)));
    g_applied.clear();
    Rig r2(&nes_desc, &r.fs);
    CHECK_EQ(r2.st->value(picture_index(&r2.ext)), pic(r2, VID_HDMI_1080P));
    CHECK_EQ(picture_start(&r2.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P);
    CHECK_EQ(g_applied.size(), 0u);
    CHECK(pos_is(r2, 3, 576, 204));
    // the same choice with the host in another mode: the application takes the chosen one
    CHECK_EQ(picture_start(&r2.c, VID_SYS_NTSC60, VID_HDMI_720P), VID_HDMI_1080P);
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_1080P));

    // no hand-over at all (JTAG, developer): PAL, and the mode is still applied
    g_applied.clear();
    Rig r3(&nes_desc);
    CHECK_EQ(picture_start(&r3.c, -1, -1), VID_HDMI_720P);
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
}

static void runtime_change()
{
    // the layer is shown; the person picks another mode: the layer goes off, the geometry changes, it comes back
    g_applied.clear();
    Rig r(&nes_desc);
    picture_start(&r.c, VID_SYS_NTSC60, VID_HDMI_1080P);
    r.vid->init();
    r.vid->show();
    r.px.w.clear();
    g_applied.clear();
    picture_apply(&r.c, pic(r, VID_HDMI_480_576));
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_480_576));   // the system it was in
    CHECK(pos_is(r, 3, 576, 204));                             // the layout is the host's 1080p frame in every mode: nothing moves
    CHECK_EQ(r.px.w.size(), 0u);                               // so the shown layer is not touched either
    // the same choice again: nothing happens
    size_t w0 = r.px.w.size();
    picture_apply(&r.c, pic(r, VID_HDMI_480_576));
    CHECK_EQ(g_applied.size(), 1u);
    CHECK_EQ(r.px.w.size(), w0);
    // back to 1080p: the descriptor's own numbers again
    picture_apply(&r.c, pic(r, VID_HDMI_1080P));
    CHECK(pos_is(r, 3, 576, 204));
    // Auto goes back to what the core's picture asks for
    picture_apply(&r.c, 0);
    CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
    CHECK(pos_is(r, 3, 576, 204));
    // a layer that is not shown is not touched, but shows the new geometry when it comes
    Rig h(&nes_desc);
    picture_start(&h.c, VID_SYS_PAL50, VID_HDMI_1080P);
    h.vid->init();
    h.px.w.clear();
    picture_apply(&h.c, pic(h, VID_HDMI_1080P));
    CHECK_EQ(h.px.w.size(), 0u);
    h.vid->show();
    CHECK_EQ(h.px.last(PX_POS), (204u << 16) | 576u);

    // the Atari: its 14-pixel offset is the same in every mode, and Auto stays 1080p after a smaller mode was chosen
    Rig a(&atari800_desc);
    CHECK_EQ(picture_start(&a.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P);
    CHECK(pos_is(a, 3, 426, 108));
    picture_apply(&a.c, pic(a, VID_HDMI_720P));
    CHECK(pos_is(a, 3, 426, 108));
    picture_apply(&a.c, 0);
    CHECK_EQ(a.vid->mode(), VID_HDMI_1080P);
    CHECK(pos_is(a, 3, 426, 108));

    // before the window exists there is nothing to move
    CoreCtx none; memset(&none, 0, sizeof(none));
    g_applied.clear();
    picture_apply(&none, pic(a, VID_HDMI_720P));
    CHECK_EQ(g_applied.size(), 0u);
}

static void through_the_menu()
{
    // the person's way: F5, Video, the Picture row, a key (T187 D1, T205 D1: Video is a tab of F5) - applied live and written to the .cfg beside the .cor
    struct Host : MenuHost {
        void info(int, char *out, int) { out[0] = 0; }
        bool return_to_host(char *, int) { return false; }
    } host;
    Rig r(&nes_desc, 0, "/usb0/cores/nes.cfg");
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    r.vid->init();
    r.vid->show();
    MenuCtl m(&r.c, &host);
    m.open();
    m.key(KEY_F5); m.key('2');
    int row = -1;
    for (int i = 0; i < m.rows(); i++) { char l[100], rr[100]; bool a; m.row(i, l, 100, rr, 100, &a); if (!strcmp(l, "Picture")) row = i; }
    CHECK(row >= 0);
    while (m.selected() < row) m.key(KEY_DOWN);
    g_applied.clear();
    m.key(KEY_RIGHT);                                          // T187 D5: the closed row is no stepper
    CHECK(g_applied.empty());
    m.key(KEY_RETURN);                                         // the page of the modes opens; browsing applies nothing
    CHECK(std::string(m.title()) == "Picture");
    CHECK_EQ(m.selected(), 0);
    m.key(KEY_DOWN);
    CHECK(g_applied.empty());
    m.key(KEY_UP); m.key(KEY_DOWN);
    CHECK(g_applied.empty());
    m.key(KEY_RETURN);                                         // Auto -> 576p / 480p
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_480_576));
    CHECK_STR(m.toast_text(), "Picture 576p / 480p");                  // T187 D4: the result is the chip in the top bar, named by the row
    CHECK(pos_is(r, 3, 576, 204));
    CHECK(r.fs.text("/usb0/cores/nes.cfg").find("Picture") != std::string::npos);
    CHECK(r.fs.text("/usb0/cores/nes.cfg").find("576p / 480p") != std::string::npos);
    char l[100], rr[100]; bool a;
    m.row(1, l, 100, rr, 100, &a);                             // the page: the applied mode carries the "now" tag
    CHECK(std::string(l).find("576p") != std::string::npos && std::string(rr).find("now") != std::string::npos);
    m.key(KEY_LEFT);                                           // back to the Video list; its row names the mode in force
    m.row(row, l, 100, rr, 100, &a);
    CHECK(std::string(rr).find("576p") != std::string::npos);
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
static void own_picture()
{
    g_applied.clear();
    video_current = 0;                                                               // nobody has set a mode yet
    Rig r(&c64_desc);
    CHECK(!r.why);
    CHECK(r.vid->own_picture());
    CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P), VID_HDMI_720P);      // Auto: the host's mode stays
    CHECK_EQ(g_applied.size(), 0u);
    CHECK_EQ(picture_start(&r.c, -1, -1), -1);                                       // nobody knows the mode: nothing is guessed, nothing set
    CHECK_EQ(g_applied.size(), 0u);
    picture_apply(&r.c, 0);                                                          // Auto at run time: still nothing
    CHECK_EQ(g_applied.size(), 0u);
    picture_apply(&r.c, pic(r, VID_HDMI_1080P));                                     // a choice is applied, with no layout of ours to move
    CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_1080P));
    CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
    picture_apply(&r.c, 0);                                                          // back to Auto: the mode it is in now stays
    CHECK_EQ(g_applied.size(), 1u);
    CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
    for (auto &w : r.px.w) CHECK_EQ(w.v & 1, 0);                                      // the pixel layer was never switched on for it
    // the normal cores are as before: Auto asks for what the picture fits in
    Rig n(&nes_desc);
    CHECK_EQ(picture_start(&n.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P);
}

// T164 D10c: the picture value "Auto" is the mode the HOST is set to (host.cfg [Video] Mode), not the mode that shows this core's picture largest. Without
// the gateware (this bench) the NES alone would ask for 720p; every case below says which of the two wins.
static void host_auto()
{
    const char *HC = "/Flash/trx/host.cfg";
    auto host = [](Rig &r, const char *mode) { r.fs.put_text("/Flash/trx/host.cfg", std::string("[Video]\nMode=") + mode + "\nSystem=PAL 50 Hz\n"); };
    // host 1080p, core Auto: 1080p, where the NES alone takes 720p
    { Rig r(&nes_desc); host(r, "1920 x 1080"); g_applied.clear();
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P);
      CHECK_EQ(g_applied.size(), 0u);
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P); }
    // host 576p, the hand-over says 720p (the host fell back): the host's own choice wins over the hand-over
    { Rig r(&nes_desc); host(r, "576p / 480p"); g_applied.clear();
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P), VID_HDMI_480_576);
      CHECK(g_applied.size() == 1 && g_applied[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_480_576)); }
    // the core's own choice overrides it (stored in its .cfg, 720p against the host's 1080p) ...
    { Rig a(&nes_desc); CHECK(a.st->set(picture_index(&a.ext), pic(a, VID_HDMI_800x600)));
      Rig r(&nes_desc, &a.fs); host(r, "1920 x 1080"); g_applied.clear();
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_800x600);
      // ... and Auto at run time goes back to the host's mode, not to the NES's wish
      picture_apply(&r.c, 0);
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P); }
    // an old core .cfg (written before D10c: "Picture=Auto") loads and now means the host's mode
    { Rig r(&nes_desc); host(r, "1920 x 1080");
      r.fs.put_text("/usb0/cores/old.cfg", std::string("[") + nes_desc.store + "]\nPicture=Auto\n");
      Settings st(r.ext.settings, r.ext.nsettings, nes_desc.store, &r.fs, "/usb0/cores/old.cfg", 0);
      st.load();
      r.c.settings = &st;
      CHECK_EQ(st.value(picture_index(&r.ext)), 0);
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P);
      r.c.settings = r.st; }
    // the host is set to "From the screen": Auto is as before (720p for the NES); so with no file, a file with no section and a Mode in another section
    { Rig r(&nes_desc); host(r, "From the screen");
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P); }
    { Rig r(&nes_desc);
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P); }
    { Rig r(&nes_desc); r.fs.put_text(HC, "Mode=1920 x 1080\n");
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P); }
    { Rig r(&nes_desc); r.fs.put_text(HC, "[Other]\nMode=1920 x 1080\n");
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P); }
    // the Video section is found after another one
    { Rig r(&nes_desc); r.fs.put_text(HC, "[Other]\nMode=576p / 480p\n[Video]\nMode=1920 x 1080\n");
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P); }
    // a core that draws its own picture has no wish and keeps the screen's mode, whatever the host file says
    { g_applied.clear(); Rig r(&c64_desc); host(r, "576p / 480p");
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P), VID_HDMI_720P);
      CHECK_EQ(g_applied.size(), 0u); }
    // T164 D10c amended (Alex 2026-11-04): the first value of the Picture item says which mode Auto is and whose it is
    auto auto_of = [](Rig &r) { return std::string(r.ext.settings[picture_index(&r.ext)].values[0]); };
    { Rig r(&nes_desc);
      CHECK_STR(auto_of(r).c_str(), "Auto");                                              // before the picture starts the text is plain
      host(r, "1920 x 1080"); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      CHECK_STR(auto_of(r).c_str(), "Auto (1920 x 1080 from host)"); }
    { Rig r(&nes_desc); host(r, "576p / 480p"); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P);
      CHECK_STR(auto_of(r).c_str(), "Auto (576p / 480p from host)"); }
    { Rig r(&nes_desc); host(r, "From the screen"); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      CHECK_STR(auto_of(r).c_str(), "Auto (1280 x 720 from screen)"); }                  // the host stored none: what the NES picked from the screen
    { Rig r(&nes_desc); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      CHECK_STR(auto_of(r).c_str(), "Auto (1280 x 720 from screen)"); }
    // the .cfg keeps the plain label: the note is for the display and goes stale with the host's mode
    { Rig r(&nes_desc); host(r, "1920 x 1080"); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      int pi = picture_index(&r.ext);
      CHECK_STR(auto_of(r).c_str(), "Auto (1920 x 1080 from host)");
      CHECK(r.st->set(pi, 2));
      CHECK(r.st->set(pi, 0));
      std::string t = r.fs.text("/usb0/cores/x.cfg");
      CHECK(t.find("Picture=Auto\n") != std::string::npos);
      CHECK(t.find("from host") == std::string::npos && t.find("(") == std::string::npos); }
    // the label a .cfg stored (any release) still finds the first value: plain "Auto", and the note of another host mode
    { Rig r(&nes_desc); host(r, "1920 x 1080"); picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      const char *lbl[] = { "Auto", "Auto (1280 x 720 from host)", "auto (1920 x 1080 from screen)" };
      for (const char *l : lbl) {
          Settings st(r.ext.settings, r.ext.nsettings, nes_desc.store, &r.fs, "/usb0/cores/lbl.cfg", 0);
          int pi = picture_index(&r.ext);
          r.fs.put_text("/usb0/cores/lbl.cfg", std::string("[") + nes_desc.store + "]\nPicture=" + l + "\n");
          st.load();
          CHECK_EQ(st.value(pi), 0);
          CHECK_EQ((int)st.layer(pi), (int)Settings::L_CORE);                              // the label was found: the stored Auto is the core's own choice, not the default's
      } }
}

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
    static const CoreDesc *const cores[] = { &nes_desc, &atari800_desc };
    for (const CoreDesc *d : cores) {
        for (int m : modes) {
            Rig r(d);
            picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
            Rig ref(d);
            ref.vid->set_mode(VID_HDMI_1080P, VID_SYS_PAL50);
            picture_apply(&r.c, 1 + m);
            CHECK(pos_is(r, ref.vid->scale(), ref.vid->x0(), ref.vid->y0()));
        }
    }
    CHECK(nes_desc.video.x0 == 576);
    // set_mode puts the layout's numbers into the working geometry, not the descriptor's: a descriptor that says scale 2 gets
    // the scale the frame allows, and the offset it implies (its own x0/y0 are only the bias, init_bias)
    CoreDesc odd = nes_desc;
    odd.video.scale = 2;
    odd.video.x0 = 512;
    odd.video.y0 = 160;
    FakeIo io; PxRec px;
    Video v(odd.video, &px, &io);
    v.set_mode(VID_HDMI_720P, VID_SYS_PAL50);
    int es, ex, ey;
    video_layout(256, 224, VID_HDMI_1080P, VID_SYS_PAL50, (512 - (1920 - 256 * 2) / 2) / 2, (160 - (1080 - 224 * 2) / 2) / 2, &es, &ex, &ey);
    CHECK_EQ(v.scale(), es);
    CHECK_EQ(v.x0(), ex);
    CHECK_EQ(v.y0(), ey);
    CHECK(es == 3 && (ex != 512 || ey != 160));
    // wanted_hdmi() answers from the descriptor's own scale, not from the one set_mode() worked out
    CoreDesc small = atari800_desc;
    small.video.scale = 1;
    Video sv(small.video, &px, &io);
    int before = sv.wanted_hdmi();
    sv.set_mode(VID_HDMI_1080P, VID_SYS_PAL50);
    CHECK_EQ(sv.scale(), 3);
    CHECK_EQ(sv.wanted_hdmi(), before);
    CHECK_EQ(before, VID_HDMI_480_576);
    // the start-up path goes through the same layout
    Rig o(&odd);
    picture_start(&o.c, VID_SYS_PAL50, VID_HDMI_1080P);
    CHECK(pos_is(o, es, ex, ey));
}

int main()
{
    layout_table();
    fixed_frame();
    extension();
    nes_startup();
    runtime_change();
    own_picture();
    host_auto();
    through_the_menu();
    picmodes_shared();
    DONE("test_picture");
}
