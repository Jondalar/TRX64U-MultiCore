// T164 WP17 (S10, S11, S12, Q7): the host application's picture - hostvideo.cc against a fake screen (EDID, hot-plug), a fake mode-request
// gateware (0x4F00 .. 0x4F09 as host/mode_req.v answers) and a fake host.cfg. Every decision has a control in run_hostvideo.sh.
#include "hostvideo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- the fake world ------------------------------------------------------------------------------------------------------------
static uint32_t g_t;
static bool g_hpd = true;
static uint8_t g_edid[256];
static int g_len;                       // 0: nobody answers
static int g_reads;
static bool g_gw;                       // the host answers the mode request
static bool g_gw_silent;                // ... but never processes one
static unsigned g_refuse;               // rows (bit) the gateware refuses
static int g_writes, g_last_row = -1;
static uint8_t g_wr, g_pr, g_mode = 2;
static uint32_t g_req_at;
static bool g_refused;
static int g_legacy, g_legacy_sys, g_legacy_hdmi;
static int g_saved_hdmi = -1, g_saved_sys = 0, g_stores;

void hv_saved_load(int *h, int *s) { *h = g_saved_hdmi; *s = g_saved_sys; }
void hv_saved_store(int h, int s) { g_saved_hdmi = h; g_saved_sys = s; g_stores++; }

static bool f_hpd(void) { return g_hpd; }
static bool f_edid(uint8_t *b, int max, int *len)
{
    g_reads++;
    if (!g_len) {
        *len = 0;
        return false;
    }
    memcpy(b, g_edid, g_len < max ? g_len : max);
    *len = g_len;
    return true;
}
static bool f_present(void) { return g_gw; }
static void f_write(int row)
{
    g_writes++;
    g_last_row = row;
    g_wr++;
    g_req_at = g_t;
    bool ok = row >= 0 && row < 12 && !(g_refuse & (1u << row)) && !(row >= 3 && row <= 5);
    g_refused = !ok;
    if (ok) {
        g_mode = (uint8_t)row;
    }
}
static bool f_status(HvGwStatus *s)
{
    if (!g_gw) {
        return false;
    }
    if (!g_gw_silent && g_t - g_req_at >= 5) {
        g_pr = g_wr;
    }
    s->written = g_wr;
    s->processed = g_pr;
    s->mode = g_mode;
    s->flags = (uint8_t)((g_refused && g_pr == g_wr ? HV_GW_REFUSED : 0) | (g_t - g_req_at >= 100 ? 7 : 0));
    return true;
}
static void f_legacy(int sys, int hdmi) { g_legacy++; g_legacy_sys = sys; g_legacy_hdmi = hdmi; }
static void f_enable(void) {}
static uint32_t f_ms(void) { return g_t; }
static void f_pause(int ms) { g_t += ms; }
static int g_av_mode = -1, g_av_std = -1, g_av_writes = 0;
static void f_av(int mode, int std) { g_av_mode = mode; g_av_std = std; g_av_writes++; }
static const HostVideoIo io = { f_hpd, f_edid, f_present, f_write, f_status, f_legacy, f_enable, f_ms, f_pause, f_av };
const HostVideoIo *hostvideo_board_io(void) { return &io; }

// ---- EDID builders (the same shapes as tests/coreapp/test_edid.cc) --------------------------------------------------------------
static void sum(uint8_t *b)
{
    unsigned t = 0;
    for (int i = 0; i < 127; i++) {
        t += b[i];
    }
    b[127] = (uint8_t)(0x100 - (t & 0xFF));
}
static void base(uint8_t *e, int ext)
{
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memset(e, 0, 256);
    memcpy(e, hdr, 8);
    for (int i = 0; i < 8; i++) {
        e[38 + 2 * i] = 0x01;
        e[39 + 2 * i] = 0x01;
    }
    e[126] = (uint8_t)ext;
}
static void cea(uint8_t *x, const uint8_t *vics, int n)
{
    memset(x, 0, 128);
    x[0] = 0x02;
    x[1] = 0x03;
    int p = 4;
    x[p++] = (uint8_t)((2 << 5) | n);
    for (int i = 0; i < n; i++) {
        x[p++] = vics[i];
    }
    x[2] = (uint8_t)p;
    sum(x);
}
static void tv(const uint8_t *vics, int n)
{
    base(g_edid, 1);
    sum(g_edid);
    cea(g_edid + 128, vics, n);
    g_len = 256;
}
static void tv50() { static const uint8_t v[] = { 31, 19, 17 }; tv(v, 3); }        // 1080p50, 720p50, 576p
static void tv60() { static const uint8_t v[] = { 16, 4, 2 }; tv(v, 3); }          // 1080p60, 720p60, 480p
static void tv60_1080() { static const uint8_t v[] = { 16 }; tv(v, 1); }           // 1080p60 only
static void pc(int w, int aspect)                                                   // a monitor without a CEA block: one standard timing at 60 Hz
{
    base(g_edid, 0);
    g_edid[38] = (uint8_t)(w / 8 - 31);
    g_edid[39] = (uint8_t)(aspect << 6);
    sum(g_edid);
    g_len = 128;
}
static void tvpc1280()                                                              // a 60 Hz television that also names 1280 x 1024
{
    static const uint8_t v[] = { 16, 4, 2 };
    base(g_edid, 1);
    g_edid[38] = (uint8_t)(1280 / 8 - 31);
    g_edid[39] = (uint8_t)(2 << 6);
    sum(g_edid);
    cea(g_edid + 128, v, 3);
    g_len = 256;
}

static void world(void)
{
    g_t = 1000;
    g_hpd = true;
    g_len = 0;
    g_reads = 0;
    g_gw = false;
    g_gw_silent = false;
    g_refuse = 0;
    g_writes = 0;
    g_last_row = -1;
    g_wr = g_pr = 0;
    g_mode = 2;
    g_req_at = 0;
    g_refused = false;
    g_legacy = 0;
    g_saved_hdmi = -1;
    g_saved_sys = 0;
    g_stores = 0;
    video_current = 0;
    hv_init(&io);
}

static void gw_on(void) { g_gw = true; hv_init(&io); }
static void poll(void) { g_t += HV_POLL_MS; }

// ---- S10: only what the screen takes is offered --------------------------------------------------------------------------------
static void s10(void)
{
    world();
    tv50();
    int s, h;
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(s == VID_SYS_PAL50 && h == VID_HDMI_1080P);                 // the best row the screen names
    int l[VID_HDMI_COUNT];
    CHECK(hv_list(hv_state()->mask, VID_SYS_PAL50, l) == 3 && l[0] == 0 && l[1] == 1 && l[2] == 2);
    char why[64];
    CHECK(!hv_user_set(VID_HDMI_1024x768, why, sizeof(why)));         // not named: refused, nothing applied
    CHECK(hv_state()->hdmi == VID_HDMI_1080P && g_legacy == 1 && g_stores == 0);
    CHECK(strstr(why, "does not list"));
    CHECK(hv_step(hv_state()->mask, VID_SYS_PAL50, 2, 1) == 0);       // V wraps inside the list
    CHECK(hv_step(hv_state()->mask, VID_SYS_PAL50, 0, 1) == 1);

    // a 60 Hz-only television asked as PAL: the other family silently (Q6), only resolutions
    world();
    tv60();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(s == VID_SYS_NTSC60 && h == VID_HDMI_1080P);
    CHECK(hv_list(hv_state()->mask, VID_SYS_PAL50, l) == 3);
    CHECK(hv_offered(hv_state()->mask, VID_SYS_PAL50, VID_HDMI_720P) && !hv_offered(hv_state()->mask, VID_SYS_PAL50, VID_HDMI_800x600));

    // a DVI monitor without a CEA block: its one named PC row, and nothing else
    world();
    pc(1024, 1);
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_1024x768);
    CHECK(hv_list(hv_state()->mask, VID_SYS_PAL50, l) == 1 && l[0] == VID_HDMI_1024x768);
    CHECK(hv_step(hv_state()->mask, VID_SYS_PAL50, -1, 1) == VID_HDMI_1024x768);

    // no EDID (Q9): the six television rows listed for the blind V key, 576p/480p up
    world();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_480_576 && s == VID_SYS_PAL50);
    CHECK(hv_list(hv_state()->mask, VID_SYS_PAL50, l) == 3 && l[2] == VID_HDMI_1080P);
    CHECK(!hv_state()->have_edid);
    CHECK(hv_user_set(VID_HDMI_720P, why, sizeof(why)) && hv_state()->hdmi == VID_HDMI_720P);
    CHECK(!hv_user_set(VID_HDMI_1280x1024, why, sizeof(why)));
}

// ---- S11: the screen changes ---------------------------------------------------------------------------------------------------
static void s11(void)
{
    world();
    gw_on();
    tv50();
    int s, h;
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(hv_gateware() && h == VID_HDMI_1080P && s == VID_SYS_PAL50);
    CHECK(g_writes == 1 && g_last_row == 2 && g_legacy == 0);          // the gateware path: row 2 written, the old path unused
    CHECK(g_mode == 2);
    poll();
    CHECK(hv_tick() == HV_NONE && g_writes == 1);                       // the same screen: nothing
    g_hpd = false;                                                      // unplugged: nothing is read
    poll();
    int r0 = g_reads;
    CHECK(hv_tick() == HV_NONE && g_writes == 1 && g_reads == r0);
    g_hpd = true;
    tv60();                                                             // plugged in again: an EDID that does not name 1080p50
    poll();
    CHECK(hv_tick() == HV_FOLLOWED);
    CHECK(g_writes == 2 && g_last_row == 8 && hv_state()->sys == VID_SYS_NTSC60 && hv_state()->hdmi == VID_HDMI_1080P);
    CHECK(video_current == video_pack(VID_SYS_NTSC60, VID_HDMI_1080P)); // what the hand-over carries
    int reads = g_reads;
    CHECK(hv_tick() == HV_NONE && g_reads == reads);                    // inside the 500 ms: the line is not even looked at
    poll();
    CHECK(hv_tick() == HV_NONE && g_writes == 2);                       // the same EDID again applies nothing
    // a screen that names a mode the gateware then refuses: tried once, not on every poll
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    g_refuse = 1u << 8;
    tv60_1080();
    poll();
    HvEvent e = hv_tick();
    CHECK(e == HV_NONE && g_writes == 2 && hv_state()->hdmi == VID_HDMI_1080P && hv_state()->sys == VID_SYS_PAL50);   // refused: the mode stays
    poll();
    CHECK(hv_tick() == HV_NONE && g_writes == 2);                       // ... and the same EDID is not tried again
    poll();
    CHECK(hv_tick() == HV_NONE && g_writes == 2);
}

// ---- S12: default and override -------------------------------------------------------------------------------------------------
static void s12(void)
{
    int s, h;
    // the stored default wins when the screen names it
    world();
    tv50();
    g_saved_hdmi = VID_HDMI_720P;
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_720P && s == VID_SYS_PAL50 && video_current == video_pack(VID_SYS_PAL50, VID_HDMI_720P));
    // ... else the best fitting row, and the default is kept
    world();
    tv50();
    g_saved_hdmi = VID_HDMI_1280x1024;
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_1080P);
    CHECK(hv_state()->def_hdmi == VID_HDMI_1280x1024 && g_saved_hdmi == VID_HDMI_1280x1024 && g_stores == 0);
    // ... and a screen that names it brings it back
    tvpc1280();
    poll();
    CHECK(hv_tick() == HV_FOLLOWED && hv_state()->hdmi == VID_HDMI_1280x1024);
    // the start before the flash disk is mounted does not read the default
    world();
    tv50();
    g_saved_hdmi = VID_HDMI_720P;
    hv_load_default(-1, -1);
    hv_start(false, &s, &h);
    CHECK(h == VID_HDMI_1080P);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_720P);
    hv_start(true, &s, &h);
    CHECK(g_legacy == 2);                                              // the same answer twice is not applied twice

    // boot.cfg's picture is read once: moved into host.cfg when host.cfg holds none
    world();
    CHECK(hv_load_default(VID_SYS_NTSC60, VID_HDMI_720P));
    CHECK(g_saved_hdmi == VID_HDMI_720P && g_saved_sys == VID_SYS_NTSC60 && g_stores == 1);
    CHECK(hv_state()->def_hdmi == VID_HDMI_720P && hv_state()->def_sys == VID_SYS_NTSC60);
    CHECK(!hv_load_default(-1, -1) && g_stores == 1);                  // boot.cfg cleared: nothing moves again
    world();
    g_saved_hdmi = VID_HDMI_1080P;
    CHECK(!hv_load_default(VID_SYS_PAL50, VID_HDMI_720P) && g_stores == 0);   // host.cfg has a choice: boot.cfg is not read
    CHECK(hv_state()->def_hdmi == VID_HDMI_1080P);
    world();
    CHECK(!hv_load_default(VID_SYS_PAL50, 17) && g_stores == 0 && hv_state()->def_hdmi == -1);   // a garbage value moves nothing
}

// ---- Q7: confirm or revert; the gateware ---------------------------------------------------------------------------------------
static void q7(void)
{
    int s, h;
    char why[64];
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    int w0 = g_writes;
    // a key keeps it
    CHECK(hv_user_set(VID_HDMI_720P, why, sizeof(why)));
    CHECK(hv_state()->pending && hv_state()->hdmi == VID_HDMI_720P && g_stores == 0 && g_last_row == 1 && g_writes == w0 + 1);
    CHECK(hv_seconds_left() == 8);
    g_t += 7900;
    CHECK(hv_tick() == HV_CONFIRM_TICK && hv_state()->pending);       // 7.9 s: still asking
    CHECK(hv_keep() && !hv_state()->pending && g_saved_hdmi == VID_HDMI_720P && g_stores == 1 && hv_state()->hdmi == VID_HDMI_720P);
    CHECK(!hv_keep());                                                // nothing to keep any more
    g_t += 20000;
    CHECK(hv_tick() != HV_REVERTED && hv_state()->hdmi == VID_HDMI_720P);
    // no key: back after 8 s, nothing stored
    int st0 = g_stores;
    CHECK(hv_user_set(VID_HDMI_480_576, why, sizeof(why)));
    g_t += 8000;
    CHECK(hv_tick() == HV_REVERTED);
    CHECK(!hv_state()->pending && hv_state()->hdmi == VID_HDMI_720P && g_last_row == 1 && g_stores == st0 && g_saved_hdmi == VID_HDMI_720P);
    // a second change during the question: the timer restarts, the way back is still the confirmed mode
    CHECK(hv_user_set(VID_HDMI_480_576, why, sizeof(why)));
    g_t += 5000;
    CHECK(hv_user_set(VID_HDMI_1080P, why, sizeof(why)));
    CHECK(hv_seconds_left() == 8);
    g_t += 8000;
    CHECK(hv_tick() == HV_REVERTED && hv_state()->hdmi == VID_HDMI_720P && g_stores == st0);
    // from the screen as the choice: the best row, stored as -1
    CHECK(hv_user_set(-1, why, sizeof(why)) && hv_state()->hdmi == VID_HDMI_1080P);
    CHECK(hv_keep() && g_saved_hdmi == -1);

    // a refused request changes nothing and asks nothing
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    g_refuse = 1u << 1;
    CHECK(!hv_user_set(VID_HDMI_720P, why, sizeof(why)) && !hv_state()->pending && hv_state()->hdmi == VID_HDMI_1080P && g_stores == 0);
    CHECK(strstr(why, "refused"));
    // a gateware that never answers
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    g_gw_silent = true;
    uint32_t t0 = g_t;
    hv_start(true, &s, &h);
    CHECK(g_t - t0 <= 600);                                           // gave up after the answer window, did not hang
    CHECK(hv_state()->hdmi == -1);                                    // nothing is claimed to be up

    // no gateware (the host as it is today): the old path, no question, stored at once, the register never written
    world();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(!hv_gateware() && g_legacy == 1 && g_legacy_hdmi == VID_HDMI_1080P && g_writes == 0);
    CHECK(hv_user_set(VID_HDMI_720P, why, sizeof(why)));
    CHECK(!hv_state()->pending && g_stores == 1 && g_saved_hdmi == VID_HDMI_720P && g_legacy == 2 && g_writes == 0);
    g_t += 60000;
    CHECK(hv_tick() != HV_REVERTED && hv_state()->hdmi == VID_HDMI_720P);
}

// ---- the picture survives a reboot (T164 picsave, GitHub #24) ---------------------------------------------------------------------
// A power-on as the board does it: hostapp.cc's first start without the stored choice, then the host UI's start with it. hostui.cc calls
// hv_load_default only when boot.cfg could be read, so a board without /Flash/boot.cfg must get its choice from hv_start itself.
static void power_on(int *s, int *h)
{
    video_current = 0;
    g_wr = g_pr = 0;
    g_t += 100000;
    hv_init(&io);
    hv_start(false, 0, 0);                                            // video_init: before the flash disk is mounted
    hv_start(true, s, h);                                             // hostui_run: the stored choice, no hv_load_default before it
}
static void picsave(void)
{
    int s, h;
    char why[64];
    // V (and the web page, and the F8 row: one hv_user_set) + any key, then a reboot: the choice is up again
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(h == VID_HDMI_1080P);
    CHECK(hv_user_set(VID_HDMI_720P, why, sizeof(why)) && hv_state()->pending);
    CHECK(hv_keep() && g_saved_hdmi == VID_HDMI_720P);
    power_on(&s, &h);
    CHECK(h == VID_HDMI_720P && hv_state()->hdmi == VID_HDMI_720P && hv_state()->def_hdmi == VID_HDMI_720P);
    CHECK(video_current == video_pack(s, h));
    // V twice (1080p -> 576p -> 720p, each one a question, the last one kept) -> reboot -> 720p
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    int next = hv_step(hv_state()->mask, hv_state()->def_sys, hv_state()->hdmi, 1);
    CHECK(next == VID_HDMI_480_576 && hv_user_set(next, why, sizeof(why)));
    next = hv_step(hv_state()->mask, hv_state()->def_sys, hv_state()->hdmi, 1);
    CHECK(next == VID_HDMI_720P && hv_user_set(next, why, sizeof(why)));
    CHECK(hv_keep());
    power_on(&s, &h);
    CHECK(h == VID_HDMI_720P);
    // a second boot with the same file: still 720p, and the start stores nothing over it
    int stores = g_stores;
    power_on(&s, &h);
    CHECK(h == VID_HDMI_720P && g_stores == stores);
    // no key: the change goes back and nothing was stored, the reboot comes up as before
    world();
    gw_on();
    tv50();
    hv_load_default(-1, -1);
    hv_start(true, &s, &h);
    CHECK(hv_user_set(VID_HDMI_720P, why, sizeof(why)));
    g_t += HV_CONFIRM_MS + 10;
    CHECK(hv_tick() == HV_REVERTED && g_saved_hdmi == -1);
    power_on(&s, &h);
    CHECK(h == VID_HDMI_1080P);
}

int main()
{
    s10();
    s11();
    s12();
    q7();
    picsave();
    // T265 P4: the A/V port goes out as mode 1 (RGB) / 0 (off) with std 0 (the core's raster)
    hv_av_apply(1);
    CHECK(g_av_writes == 1 && g_av_mode == 1 && g_av_std == 0);
    hv_av_apply(0);
    CHECK(g_av_writes == 2 && g_av_mode == 0 && g_av_std == 0);
    printf("hostvideo: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
