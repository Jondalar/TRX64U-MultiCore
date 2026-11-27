// T164 WP16 (S10, S11, S12, Q7, G11-G14 on the application side): the Picture item of a core lists every row of the table at its own rate (T235; the screen's
// EDID only marks the rows it names), follows the screen when it changes, and picks the mode in the order core choice > host default > best fit; a mode chosen from the open menu is kept by any key or taken back
// after 8 s. The host is faked: the gateware's request and status (video_apply_gateware, video_gateware_row), the hot-plug line and the EDID that
// the screen answers. The EDIDs are synthetic (a CEA block with the VICs that name the rows), the rows are those of host/hdmi_modes.vh:
// 0 576p50, 1 720p50, 2 1080p50, 6 480p60, 7 720p60, 8 1080p60, 9 800 x 600, 10 1024 x 768, 11 1280 x 1024 (60 Hz).
#include <string>
#include <utility>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "boot.h"
#include "regset.h"
#include "picture.h"
#include "menuctl.h"
#include "keyboard.h"
#include "videomode.h"

// ---- the faked host ------------------------------------------------------------------------------------------------------------------
static std::vector<std::pair<int, int>> g_gw;       // every request the gateware got
static int g_gw_row = 2;                            // the status register's row (the gateware starts in 1080p50)
static bool g_gw_ok = true;
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
void video_apply(int, int) { }

static bool g_attached = true;                      // the hot-plug line
static bool g_answers = true;                       // the screen answers on the DDC lines
static uint32_t g_sig = 1;                          // its signature
static uint8_t g_edid[256];
static int g_edid_len = 0;
static int g_sig_reads = 0, g_edid_reads = 0;
bool video_edid_sig(uint32_t *sig)
{
    g_sig_reads++;
    if (!g_attached) {
        return false;
    }
    *sig = g_sig;
    return true;
}
bool video_read_edid(uint8_t *buf, int max, int *len)
{
    g_edid_reads++;
    *len = 0;
    if (!g_attached || !g_answers) {
        return false;
    }
    memcpy(buf, g_edid, g_edid_len < max ? g_edid_len : max);
    *len = g_edid_len;
    return true;
}

extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, 0 };

// ---- synthetic EDIDs -------------------------------------------------------------------------------------------------------------------
static void fix_sum(uint8_t *b)
{
    unsigned t = 0;
    for (int i = 0; i < 127; i++) {
        t += b[i];
    }
    b[127] = (uint8_t)(0x100 - (t & 0xFF));
}

// A television: the CEA video data block names `vics`. VIC 31 1080p50, 19 720p50, 17 576p50, 16 1080p60, 4 720p60, 3 480p60.
static void screen_tv(const uint8_t *vics, int n, uint32_t sig)
{
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memset(g_edid, 0, sizeof(g_edid));
    memcpy(g_edid, hdr, 8);
    for (int i = 0; i < 8; i++) {
        g_edid[38 + 2 * i] = 0x01;
        g_edid[39 + 2 * i] = 0x01;
    }
    g_edid[126] = 1;
    fix_sum(g_edid);
    uint8_t *x = g_edid + 128;
    x[0] = 0x02;
    x[1] = 0x03;
    int p = 4;
    x[p++] = (uint8_t)((2 << 5) | n);
    for (int i = 0; i < n; i++) {
        x[p++] = vics[i];
    }
    x[2] = (uint8_t)p;
    fix_sum(x);
    g_edid_len = 256;
    g_sig = sig;
    g_attached = true;
    g_answers = true;
}
static void tv50(uint32_t sig = 1) { static const uint8_t v[] = { 31, 19, 17, 16 }; screen_tv(v, 4, sig); }     // rows 2 1 0 8
static void tv50_no1080(uint32_t sig = 2) { static const uint8_t v[] = { 19, 17 }; screen_tv(v, 2, sig); }       // rows 1 0
static void tv60only(uint32_t sig = 3) { static const uint8_t v[] = { 16, 4, 3 }; screen_tv(v, 3, sig); }        // rows 8 7 6
// T235 §5: exactly the six television rows, no PC mode - the same mask as no EDID (EDID_LIST_NONE), yet a screen that named its modes
static void tv_six(uint32_t sig = 6) { static const uint8_t v[] = { 31, 19, 17, 16, 4, 3 }; screen_tv(v, 6, sig); }   // rows 2 1 0 8 7 6
// a PC monitor with no CEA block: 1024 x 768 at 60 Hz in the detailed timing (row 10 only)
static void screen_pc(uint32_t sig = 4)
{
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memset(g_edid, 0, sizeof(g_edid));
    memcpy(g_edid, hdr, 8);
    for (int i = 0; i < 8; i++) {
        g_edid[38 + 2 * i] = 0x01;
        g_edid[39 + 2 * i] = 0x01;
    }
    uint8_t *d = g_edid + 54;
    int hact = 1024, htot = 1344, vact = 768, vtot = 806, hbl = htot - hact, vbl = vtot - vact;
    long c = 65000000L / 10000;
    d[0] = (uint8_t)(c & 0xFF); d[1] = (uint8_t)(c >> 8);
    d[2] = (uint8_t)(hact & 0xFF); d[3] = (uint8_t)(hbl & 0xFF); d[4] = (uint8_t)(((hact >> 8) << 4) | (hbl >> 8));
    d[5] = (uint8_t)(vact & 0xFF); d[6] = (uint8_t)(vbl & 0xFF); d[7] = (uint8_t)(((vact >> 8) << 4) | (vbl >> 8));
    fix_sum(g_edid);
    g_edid_len = 128;
    g_sig = sig;
    g_attached = true;
    g_answers = true;
}

// a television that also names 1024 x 768 at 60 Hz in the detailed timing of its base block: television rows 2 1 0 8 and the PC row 10
static void screen_tvpc(uint32_t sig = 5)
{
    tv50(sig);
    uint8_t *d = g_edid + 54;
    int hact = 1024, htot = 1344, vact = 768, vtot = 806, hbl = htot - hact, vbl = vtot - vact;
    long c = 65000000L / 10000;
    d[0] = (uint8_t)(c & 0xFF); d[1] = (uint8_t)(c >> 8);
    d[2] = (uint8_t)(hact & 0xFF); d[3] = (uint8_t)(hbl & 0xFF); d[4] = (uint8_t)(((hact >> 8) << 4) | (hbl >> 8));
    d[5] = (uint8_t)(vact & 0xFF); d[6] = (uint8_t)(vbl & 0xFF); d[7] = (uint8_t)(((vact >> 8) << 4) | (vbl >> 8));
    fix_sum(g_edid);
}

// the labels of the Picture item, "Auto" first; the note the first one carries once the picture runs ("Auto (1280 x 720 from host)", D10c) is cut: these tests are about the list
static std::string list_of(const CoreDesc &d)
{
    int pi = picture_index(&d);
    std::string s;
    for (int v = 0; v < d.settings[pi].nvalues; v++) {
        std::string l = d.settings[pi].values[v];
        if (v == 0 && l.find(" (") != std::string::npos) {
            l.erase(l.find(" ("));
        }
        s += l;
        s += v + 1 < d.settings[pi].nvalues ? "|" : "";
    }
    return s;
}
// T235: the value texts of the full list (the machine's rate first within a resolution)
#define L576 "576p (50 Hz)"
#define L480 "480p (60 Hz)"
#define L720 "1280 x 720 (50 Hz)"
#define L720_60 "1280 x 720 (60 Hz)"
#define L1080 "1920 x 1080 (50 Hz)"
#define L1080_60 "1920 x 1080 (60 Hz)"
#define L800 "800 x 600 (60 Hz)"
#define L1024 "1024 x 768 (60 Hz)"
#define L1280 "1280 x 1024 (60 Hz)"
#define FULL_PAL "Auto|" L576 "|" L480 "|" L720 "|" L720_60 "|" L1080 "|" L1080_60 "|" L800 "|" L1024 "|" L1280
#define FULL_NTSC "Auto|" L480 "|" L576 "|" L720_60 "|" L720 "|" L1080_60 "|" L1080 "|" L800 "|" L1024 "|" L1280
// the plain resolution names the host writes into host.cfg
#define H576 "576p / 480p"
#define H720 "1280 x 720"
#define H1080 "1920 x 1080"
// the rows the screen names, as the page marks them: a string of the table rows (0-11) in ascending order, "-" for none
static std::string named_rows(const CoreDesc &d)
{
    CoreCtx c; memset(&c, 0, sizeof(c));
    c.desc = &d;
    PictureModes pm;
    picture_modes(&c, &pm);
    std::string s;
    for (int i = 1; i < pm.n; i++) {
        if (pm.row[i].named) {
            s += (s.empty() ? "" : ",") + std::to_string(pm.row[i].mode);
        }
    }
    return s.empty() ? "-" : s;
}

struct Rig {
    FakeIo io; FakeFs fs; PxRec px; CoreDesc ext; Setting items[SETTINGS_MAX + 4]; CoreCtx c; Settings *st; Video *vid;
    const char *why;
    // The screen is whatever the fakes hold when the rig is made: its mask is what the application read (picture_env_read does the same on the board).
    Rig(const CoreDesc *d, FakeFs *shared = 0, int sys = VID_SYS_PAL50)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        g_gw_row = 2;
        g_gw_ok = true;
        g_gw.clear();
        PictureEnv env;
        picture_env_read(&env, sys);
        why = picture_extend(d, &ext, items, &env);
        c.desc = &ext; c.io = &io; c.fs = &fs;
        st = new Settings(ext.settings, ext.nsettings, d->store, &fs, "/usb0/cores/x.cfg", 0);
        st->load();
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        c.menu_open = false;
    }
    int pic() const { return picture_index(&ext); }
};

// ---- S10 / T235 S1, S2, S4: every row is offered, the screen's rows are marked -------------------------------------------------------------
// S10 on the Atari: every frame-buffer core, not only one - the same list as the NES's (the shared unit's s10_lists)
static void s10_atari()
{
    // every frame-buffer core, not only one: the same list in the Atari
    tv50_no1080();
    { Rig r(&atari800_desc);
      CHECK_STR(list_of(r.ext).c_str(), FULL_PAL);
      CHECK_STR(named_rows(r.ext).c_str(), "0,1");
    }
}
static void s12_precedence()
{
    tv50();                                                                              // rows 0 1 2 8
    // the host handed over 720p; the core has no choice: Auto is the host's default
    { Rig r(&atari800_desc);
      g_gw_row = 1;
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P);
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
      CHECK_EQ(g_gw.size(), 0u);
    }
    // the core's own Picture (1080p, stored in its .cfg) wins over the host's 720p
    { Rig a(&atari800_desc);
      CHECK(a.st->set(a.pic(), 5));
      Rig r(&atari800_desc, &a.fs);
      g_gw_row = 1;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P), VID_HDMI_1080P);
      CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_1080P));
    }
    // the core's own choice is not named by the screen (T235): it is still the choice
    { Rig a(&atari800_desc);
      CHECK(a.st->set(a.pic(), 5));
      tv50_no1080();
      Rig r(&atari800_desc, &a.fs);
      g_gw_row = 0;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_480_576), VID_HDMI_1080P);
      CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_1080P));
    }
    // at run time: Auto after a choice goes to the host's default, not to what was up
    tv50();
    { Rig r(&atari800_desc);
      g_gw_row = 1;
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P);
      picture_apply(&r.c, 5);                                                            // 1080p50
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
      picture_apply(&r.c, 0);
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
      CHECK_EQ(g_gw_row, 1);
    }
}

// ---- S11: the screen changes ---------------------------------------------------------------------------------------------------------------
static void s11_follow()
{
    tv50();
    Rig r(&atari800_desc);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);                                  // running 1080p50
    CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
    CHECK(r.st->set(r.pic(), 5));                                                        // Picture = 1920 x 1080 (50 Hz) (stored)
    g_gw.clear();
    // nothing changed: polls are cheap (the signature only) and apply nothing
    g_edid_reads = 0;
    int ev = picture_service(&r.c);
    CHECK_EQ(ev, 0);
    r.io.advance(100000);                                                                // 100 ms later: not yet time
    int sr = g_sig_reads;
    picture_service(&r.c);
    CHECK_EQ(g_sig_reads, sr);
    r.io.advance(450000);
    picture_service(&r.c);
    CHECK_EQ(g_sig_reads, sr + 1);                                                       // ~500 ms: one look at the line
    CHECK_EQ(g_edid_reads, 0);                                                           // the same screen is not read again
    CHECK_EQ(g_gw.size(), 0u);
    // hot-plug goes low: nothing happens
    g_attached = false;
    r.io.advance(500000);
    CHECK_EQ(picture_service(&r.c), 0);
    CHECK_EQ(g_gw.size(), 0u);
    // ... and high with another screen that lacks the running mode: within one poll the best fitting mode, once
    tv50_no1080(10);
    r.io.advance(500000);
    ev = picture_service(&r.c);
    CHECK(ev & PS_LIST_CHANGED);
    CHECK_EQ(g_gw.size(), 1u);
    CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
    CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
    CHECK_EQ(g_edid_reads, 1);
    CHECK_STR(list_of(r.ext).c_str(), FULL_PAL);                                      // the list is the same; the marks follow
    CHECK_STR(named_rows(r.ext).c_str(), "0,1");
    CHECK(!picture_confirm_pending());                                                   // a screen that took the mode away needs no question
    CHECK_EQ(r.st->value(r.pic()), 0);                                                   // the stored 1080p50 is not named by it: the item reads Auto for this run ...
    CHECK(r.fs.text("/usb0/cores/x.cfg").find(L1080) != std::string::npos);              // ... and the file keeps the choice
    // the same EDID again (any number of polls, a replug of the same screen): nothing is applied, nothing read
    for (int i = 0; i < 3; i++) {
        r.io.advance(500000);
        picture_service(&r.c);
    }
    g_attached = false; r.io.advance(500000); picture_service(&r.c);
    tv50_no1080(10); r.io.advance(500000); picture_service(&r.c);
    CHECK_EQ(g_gw.size(), 1u);
    CHECK_EQ(g_edid_reads, 1);
    // a screen that keeps the running mode: the list changes, the mode does not
    tv50(11);
    r.io.advance(500000);
    ev = picture_service(&r.c);
    CHECK(ev & PS_LIST_CHANGED);
    CHECK_EQ(g_gw.size(), 1u);
    CHECK_STR(list_of(r.ext).c_str(), FULL_PAL);
    CHECK_STR(named_rows(r.ext).c_str(), "0,1,2,8");
    // a screen that is attached but silent on the DDC lines is asked again at the next poll and nothing is applied meanwhile
    tv60only(12);
    g_answers = false;
    r.io.advance(500000);
    CHECK_EQ(picture_service(&r.c), 0);
    CHECK_EQ(g_gw.size(), 1u);
    int reads = g_edid_reads;
    g_answers = true;
    r.io.advance(500000);
    picture_service(&r.c);
    CHECK_EQ(g_edid_reads, reads + 1);
    CHECK(g_gw.size() == 2 && g_gw[1] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_1080P));   // 720p50 is gone, 60 Hz only: 1080p60, silently
}

// the list is fixed (T235), so a new screen moves no stored choice; the machine's family does (the machine's rate comes first): the choice keeps its label
static void s11_remap()
{
    tv50();
    Rig r(&atari800_desc);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    CHECK(r.st->set(r.pic(), 5));                                                        // 1920 x 1080 (50 Hz)
    std::string cfg = r.fs.text("/usb0/cores/x.cfg");
    static const uint8_t v[] = { 31, 17 };                                               // the new screen: 1080p50 and 576p only
    screen_tv(v, 2, 20);
    r.io.advance(500000);
    picture_service(&r.c);
    CHECK_STR(list_of(r.ext).c_str(), FULL_PAL);
    CHECK_EQ(r.st->value(r.pic()), 5);
    CHECK_STR(r.st->label(r.pic()), L1080);
    CHECK(r.fs.text("/usb0/cores/x.cfg") == cfg);                                       // following the screen writes no file
    CHECK_EQ(g_gw.size(), 0u);
    // the machine's family changes (a core whose clock follows its System Mode): the list is built again, the choice stays the same row
    picture_set_system(&r.c, VID_SYS_NTSC60);
    CHECK_STR(list_of(r.ext).c_str(), FULL_NTSC);
    CHECK_STR(r.st->label(r.pic()), L1080);
    CHECK_EQ(r.st->value(r.pic()), 6);
}

// ---- Q7: confirm or revert -------------------------------------------------------------------------------------------------------------------
static void q7_confirm()
{
    tv50();
    // from the open menu: asks, and any key keeps
    { Rig r(&atari800_desc);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.c.menu_open = true;
      g_gw.clear();
      r.io.advance(1000000);
      picture_apply(&r.c, 3);                                                            // 720p50
      CHECK_EQ(g_gw.size(), 1u);
      CHECK(picture_confirm_pending());
      int ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_ASK);
      CHECK_EQ(picture_confirm_left_s(), 8);
      r.io.advance(5000000);
      ev = picture_service(&r.c);
      CHECK(!(ev & PS_CONFIRM_END));
      CHECK(picture_confirm_pending());
      CHECK_EQ(picture_confirm_left_s(), 3);
      picture_confirm_key();
      CHECK(!picture_confirm_pending());
      ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_END);
      r.io.advance(30000000);                                                            // long after: it stays
      picture_service(&r.c);
      CHECK_EQ(g_gw.size(), 1u);
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
    }
    // nobody answers: 8 s later the previous mode and the previous item value are back
    { Rig r(&atari800_desc);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.c.menu_open = true;
      r.io.advance(1000000);
      CHECK(r.st->set(r.pic(), 5));                                                      // the item says 1080p (it is what is up)
      picture_apply(&r.c, 5);
      CHECK(!picture_confirm_pending());                                                 // 1080p is up already: nothing changed, nothing to ask
      CHECK(r.st->set(r.pic(), 3));
      g_gw.clear();
      picture_apply(&r.c, 3);                                                            // 720p50
      CHECK(picture_confirm_pending());
      picture_service(&r.c);
      r.io.advance(7900000);
      int ev = picture_service(&r.c);
      CHECK(!(ev & PS_CONFIRM_END));
      CHECK_EQ(g_gw.size(), 1u);
      r.io.advance(200000);
      ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_END);
      CHECK(ev & PS_LIST_CHANGED);
      CHECK(!picture_confirm_pending());
      CHECK_EQ(g_gw.size(), 2u);
      CHECK(g_gw.size() == 2 && g_gw[1] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_1080P));
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
      { const char *chip = picture_take_result();                                          // T187 D4/D5: the chip says which mode came back
        CHECK(chip && std::string(chip).rfind("Picture: back to 1920 x 1080", 0) == 0);
        CHECK(!picture_take_result()); }                                                 // read once
      CHECK_EQ(g_gw_row, 2);
      CHECK_EQ(r.st->value(r.pic()), 5);                                                 // the item says what is up again, and the .cfg too
      CHECK(r.fs.text("/usb0/cores/x.cfg").find(L1080) != std::string::npos);
    }
    // a change with the menu closed (REST, the web page) asks too (Q7 amended): the question is raised, no key goes back after 8 s
    { Rig r(&atari800_desc);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.c.menu_open = false;
      r.io.advance(1000000);
      g_gw.clear();
      picture_apply(&r.c, 3);
      CHECK(picture_confirm_pending());
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
      int ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_ASK);
      CHECK_EQ(picture_confirm_left_s(), 8);
      r.io.advance(7900000);
      ev = picture_service(&r.c);
      CHECK(!(ev & PS_CONFIRM_END));
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
      r.io.advance(200000);
      ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_END);
      CHECK(!picture_confirm_pending());
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
    }
    // a mode that did not come up is not a change: the previous one stays, no question
    { Rig r(&atari800_desc);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.c.menu_open = true;
      g_gw_ok = false;
      picture_apply(&r.c, 3);
      CHECK(!picture_confirm_pending());
      CHECK_EQ(r.vid->mode(), VID_HDMI_1080P);
      int sys = -1, hdmi = -1;
      CHECK(video_unpack(video_current, &sys, &hdmi) && hdmi == VID_HDMI_1080P);
    }
    // a screen that changes while the question is up answers it: the mode is gone, nothing to take back to
    { Rig r(&atari800_desc);
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
      r.c.menu_open = true;
      picture_apply(&r.c, 3);
      CHECK(picture_confirm_pending());
      tv60only(30);
      r.io.advance(500000);
      picture_service(&r.c);
      CHECK(!picture_confirm_pending());
    }
}

// ---- the gateware path at start: the status row decides, not a guess --------------------------------------------------------------------------
static void start_from_status()
{
    tv50();
    // a DFX core's gateware is in the mode the host put it in: no request when Auto wants that mode
    { Rig r(&atari800_desc);
      g_gw_row = 1;                                                                      // 720p50 is up
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P);
      CHECK_EQ(g_gw.size(), 0u);
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P);
    }
    // a large core's own copy starts in 1080p50 whatever the host was handed: the host's default (720p) is asked for
    { Rig r(&atari800_desc);
      g_gw_row = 2;
      picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P);
      CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
    }
}

// ---- D10c: Auto is the host's own choice (host.cfg), on the gateware path too ---------------------------------------------------------------
static void d10c_host_auto()
{
    auto host = [](Rig &r, const char *mode) { r.fs.put_text("/Flash/trx/host.cfg", std::string("[Video]\nMode=") + mode + "\nSystem=PAL 50 Hz\n"); };
    tv50();                                                                              // rows 0 1 2 8
    // host 720p, core Auto: 720p; the hand-over carries nothing (-1), only the file says it
    { Rig r(&atari800_desc); host(r, H720);
      g_gw_row = 2;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, -1), VID_HDMI_720P);
      CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_720P));
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P); }
    // the file wins over a hand-over that says another mode
    { Rig r(&atari800_desc); host(r, H720);
      g_gw_row = 2;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_720P); }
    // host 720p, the core's own choice 1080p: 1080p; Auto at run time then goes to the host's 720p
    { Rig a(&atari800_desc);
      CHECK(a.st->set(a.pic(), 5));
      Rig r(&atari800_desc, &a.fs); host(r, H720);
      g_gw_row = 1;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_720P), VID_HDMI_1080P);
      picture_apply(&r.c, 0);
      CHECK_EQ(r.vid->mode(), VID_HDMI_720P); }
    // the host is set to "From the screen" or has no file: Auto is the mode the host had up, as before D10c
    { Rig r(&atari800_desc); host(r, "From the screen");
      g_gw_row = 2;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P);
      CHECK_EQ(g_gw.size(), 0u); }
    { Rig r(&atari800_desc);
      g_gw_row = 2;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P), VID_HDMI_1080P); }
    // the host's choice is not a mode this screen takes (1080p gone): the mode the host handed over
    tv50_no1080();                                                                       // rows 1 0
    { Rig r(&atari800_desc); host(r, H1080);
      g_gw_row = 0;
      CHECK_EQ(picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_480_576), VID_HDMI_480_576); }
    tv50();
}

// T187 D5: the open Picture page follows a hot-plug. The loop calls MenuCtl::rebuild() on PS_LIST_CHANGED (run.cc): the rows are made again from the new list,
// the cursor keeps its mode by name, and a mode that is gone puts it on the one in force.
static void d5_page_rebuild()
{
    struct Host : MenuHost {
        void info(int, char *out, int) { out[0] = 0; }
        bool return_to_host(char *, int) { return false; }
    } host;
    g_attached = true;
    tv50();
    Rig r(&atari800_desc);
    picture_start(&r.c, VID_SYS_PAL50, VID_HDMI_1080P);
    CHECK(r.st->set(r.pic(), 5));                                                        // 1080p50 stored and up
    MenuCtl m(&r.c, &host);
    m.open();
    m.key(KEY_F5); m.key('2');                                                           // T205 D1: the core, Video: the second tab of F5
    int row = -1;
    for (int i = 0; i < m.rows(); i++) { char l[100], rr[100]; bool a; m.row(i, l, 100, rr, 100, &a); if (!strcmp(l, "Picture")) row = i; }
    CHECK(row >= 0);
    while (m.selected() < row) m.key(KEY_DOWN);
    m.key(KEY_RETURN);
    CHECK(std::string(m.title()) == "Picture");
    CHECK_EQ(m.rows(), 10);                                                              // Auto and the nine rows of the table
    CHECK_EQ(m.selected(), 5);                                                           // the cursor starts on the mode in force
    char l[100], rr[100]; bool a;
    m.row(1, l, 100, rr, 100, &a);                                                       // 576p50: named by the screen, the mark stands left of the rate
    CHECK(std::string(l) == "576p" && std::string(rr).find("*") != std::string::npos && std::string(rr).find("50.") != std::string::npos);
    m.row(2, l, 100, rr, 100, &a);                                                       // 480p60: not named, no mark, still a row
    CHECK(std::string(l) == "480p" && std::string(rr).find("*") == std::string::npos && (std::string(rr).find("59.") != std::string::npos || std::string(rr).find("60.") != std::string::npos));
    m.row(3, l, 100, rr, 100, &a);
    m.row(4, l, 100, rr, 100, &a);
    CHECK(std::string(l) == "1280 x 720" && (std::string(rr).find("59.") != std::string::npos || std::string(rr).find("60.") != std::string::npos));
    m.key(KEY_UP);                                                                       // 1280 x 720 (60 Hz), the row above 1080p50
    m.key(KEY_UP);                                                                       // 1280 x 720 (50 Hz)
    CHECK_EQ(m.selected(), 3);
    // a screen that names nothing of 1080p: the list stays whole, the cursor keeps its row (50 and 60 Hz share the name), the marks move
    g_attached = false;
    r.io.advance(500000);
    picture_service(&r.c);
    tv50_no1080(10);
    r.io.advance(500000);
    int ev = picture_service(&r.c);
    CHECK(ev & PS_LIST_CHANGED);
    m.rebuild();
    CHECK(std::string(m.title()) == "Picture");
    CHECK_EQ(m.rows(), 10);
    CHECK_EQ(m.selected(), 3);
    m.row(3, l, 100, rr, 100, &a);
    CHECK(std::string(l) == "1280 x 720" && std::string(rr).find("50.") != std::string::npos);
    m.key(KEY_DOWN); m.key(KEY_DOWN);                                                    // 1080p50: no longer named
    m.row(m.selected(), l, 100, rr, 100, &a);
    CHECK(std::string(l) == "1920 x 1080" && std::string(rr).find("*") == std::string::npos);
}

// T235 S3: an unnamed mode is picked from the page, applied with the 8-second question; LEFT or ESC (the revert) puts the previous one back at once
static void t235_unnamed_pick()
{
    tv60only();                                                                          // the photo's screen: no 50 Hz row named
    { Rig r(&atari800_desc);
      g_gw_row = 8;
      picture_start(&r.c, VID_SYS_NTSC60, VID_HDMI_1080P);                               // up in 1080p60 (a named row)
      r.c.menu_open = true;
      r.io.advance(1000000);
      g_gw.clear();
      picture_apply(&r.c, 6);                                                            // 1080p50 (a 60 Hz machine lists its own rate first): not named
      CHECK(g_gw.size() == 1 && g_gw[0] == std::make_pair((int)VID_SYS_PAL50, (int)VID_HDMI_1080P));
      CHECK(picture_confirm_pending());
      char now[48], prev[48];
      picture_confirm_names(now, sizeof(now), prev, sizeof(prev));
      char want[48];
      picmodes_mode_text(EDID_ROW_MASK, VID_SYS_PAL50, VID_HDMI_1080P, want, sizeof(want));
      CHECK_STR(now, want);                                                              // the question names the rate of the row, not the screen's other family
      CHECK(std::string(now).find("1920 x 1080, 50.") == 0);
      picmodes_mode_text(EDID_ROW_MASK, VID_SYS_NTSC60, VID_HDMI_1080P, want, sizeof(want));
      CHECK_STR(prev, want);
      CHECK(std::string(prev).find("1920 x 1080, 59.") == 0 || std::string(prev).find("1920 x 1080, 60.") == 0);
      picture_service(&r.c);
      picture_confirm_revert();                                                          // LEFT / ESC
      int ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_END);
      CHECK(g_gw.size() == 2 && g_gw[1] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_1080P));
    }
    { Rig r(&atari800_desc);                                                             // no key: the 8 s run out and the screen's mode is back
      g_gw_row = 8;
      picture_start(&r.c, VID_SYS_NTSC60, VID_HDMI_1080P);
      r.c.menu_open = true;
      r.io.advance(1000000);
      g_gw.clear();
      picture_apply(&r.c, 6);
      picture_service(&r.c);
      r.io.advance(8100000);
      int ev = picture_service(&r.c);
      CHECK(ev & PS_CONFIRM_END);
      CHECK(g_gw.size() == 2 && g_gw[1] == std::make_pair((int)VID_SYS_NTSC60, (int)VID_HDMI_1080P));
    }
}


int main()
{
    s10_atari();
    s12_precedence();
    s11_follow();
    s11_remap();
    q7_confirm();
    start_from_status();
    d10c_host_auto();
    d5_page_rebuild();
    t235_unnamed_pick();
    DONE("pictfollow");
}
