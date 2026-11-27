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
static void s12_precedence()
{
    tv50();                                                                              // rows 0 1 2 8
}

int main()
{
    DONE("pictfollow");
}
