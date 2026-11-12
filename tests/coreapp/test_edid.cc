// T164 S10 (WP15): only what the screen takes is offered. Synthetic EDIDs - a television with a CEA block, a PC monitor without one, a screen
// that does 60 Hz only, range-limit and TMDS vetoes, a corrupt checksum, no EDID - go through app/video/edid.cc and the row mask, the
// best row (Q6) and the list without an EDID (Q9) are checked exactly. The row table is compared with host/hdmi_modes.vh.
// The fixtures of the Sony and the Samsung come with the board capture (S18).
#include "check.h"
#include "edid.h"
#include <stdio.h>
#include <string.h>

// ---- builders ------------------------------------------------------------------------------------------------------------------
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
        e[38 + 2 * i] = 0x01;                      // unused standard timings
        e[39 + 2 * i] = 0x01;
    }
    e[126] = (uint8_t)ext;
}

// detailed timing: pixel clock in Hz, active, total (so blanking = total - active), interlaced
static void dtd(uint8_t *d, long clk_hz, int hact, int htot, int vact, int vtot, bool interlaced = false)
{
    int hbl = htot - hact, vbl = vtot - vact;
    long c = clk_hz / 10000;
    d[0] = (uint8_t)(c & 0xFF);
    d[1] = (uint8_t)(c >> 8);
    d[2] = (uint8_t)(hact & 0xFF);
    d[3] = (uint8_t)(hbl & 0xFF);
    d[4] = (uint8_t)(((hact >> 8) << 4) | (hbl >> 8));
    d[5] = (uint8_t)(vact & 0xFF);
    d[6] = (uint8_t)(vbl & 0xFF);
    d[7] = (uint8_t)(((vact >> 8) << 4) | (vbl >> 8));
    d[17] = interlaced ? 0x80 : 0x00;
}

static void limits(uint8_t *d, int vmin, int vmax, int hmin, int hmax, int clk_mhz)
{
    memset(d, 0, 18);
    d[3] = 0xFD;
    d[5] = (uint8_t)vmin;
    d[6] = (uint8_t)vmax;
    d[7] = (uint8_t)hmin;
    d[8] = (uint8_t)hmax;
    d[9] = (uint8_t)(clk_mhz / 10);
}

static void stdt(uint8_t *e, int slot, int w, int aspect, int hz)
{
    e[38 + 2 * slot] = (uint8_t)(w / 8 - 31);
    e[39 + 2 * slot] = (uint8_t)((aspect << 6) | (hz - 60));
}

// A CEA extension: video data block with the VICs (as written, the native bit included), optionally an HDMI vendor block with a TMDS limit.
static void cea(uint8_t *x, const uint8_t *vics, int n, int tmds_mhz = 0)
{
    memset(x, 0, 128);
    x[0] = 0x02;
    x[1] = 0x03;
    int p = 4;
    x[p++] = (uint8_t)((2 << 5) | n);
    for (int i = 0; i < n; i++) {
        x[p++] = vics[i];
    }
    if (tmds_mhz) {
        x[p++] = (uint8_t)((3 << 5) | 7);
        x[p++] = 0x03; x[p++] = 0x0C; x[p++] = 0x00;
        x[p++] = 0x10; x[p++] = 0x00; x[p++] = 0x00;
        x[p++] = (uint8_t)(tmds_mhz / 5);
    }
    x[2] = (uint8_t)p;
    sum(x);
}

#define BIT(r) (1u << (r))
static const unsigned PC3 = BIT(9) | BIT(10) | BIT(11);

static unsigned offered(const uint8_t *e, int len) { return video_modes_offered(e, len); }

// ---- the row table against the generated host table ------------------------------------------------------------------------------
static int hm_numbers(const char *line, long *v, int max)
{
    int n = 0;
    const char *p = strchr(line, '{');
    if (!p) {
        return 0;
    }
    while ((p = strstr(p, "'d")) != 0 && n < max) {
        v[n++] = strtol(p + 2, 0, 10);
        p += 2;
    }
    return n;
}

static void table_vs_gateware()
{
    FILE *f = fopen(COREAPP_DIR "/../host/hdmi_modes.vh", "r");
    CHECK(f != 0);
    if (!f) {
        return;
    }
    char line[512];
    int seen_row = 0, seen_rate = 0;
    while (fgets(line, sizeof(line), f)) {
        int m = -1;
        if (strstr(line, "hm_row = {") && sscanf(line, " 4'd%d:", &m) == 1) {
            long v[16];
            CHECK_EQ(hm_numbers(line, v, 16), 10);       // vic gear hact hfp hsync hbp vact vfp vsync vbp
            const EdidRow *r = edid_row(m);
            CHECK(r != 0);
            if (r) {
                CHECK_EQ(r->w, v[2]);
                CHECK_EQ(r->htotal, v[2] + v[3] + v[4] + v[5]);
                CHECK_EQ(r->h, v[6]);
                CHECK_EQ(r->vtotal, v[6] + v[7] + v[8] + v[9]);
                bool has = v[0] == 0;                    // the PC rows have no VIC
                for (int i = 0; i < 3; i++) {
                    has = has || r->vic[i] == v[0];
                }
                CHECK(has);
            }
            seen_row++;
        } else if (strstr(line, "hm_rate = 32") && sscanf(line, " 4'd%d:", &m) == 1) {
            const char *p = strstr(line, "32'd");
            const EdidRow *r = edid_row(m);
            CHECK(r != 0 && p != 0);
            if (r && p) {
                CHECK_EQ((long long)r->pixel_hz, strtoll(p + 4, 0, 10));
            }
            seen_rate++;
        }
    }
    fclose(f);
    CHECK_EQ(seen_row, 9);
    CHECK_EQ(seen_rate, 9);
    for (int m = 0; m < EDID_ROWS; m++) {
        CHECK_EQ(edid_row(m) != 0, (EDID_ROW_MASK >> m & 1) != 0);       // exactly the rows the gateware has (Q5: no 3, 4, 5)
    }
}

int main()
{
    uint8_t e[256];

    table_vs_gateware();

    // --- a television: CEA block with 1080p50, 720p50, 576p (native), 1080p60
    {
        const uint8_t v[] = { 31, 19, 0x91, 16 };      // 0x91 = VIC 17 with the native flag
        base(e, 1);
        cea(e + 128, v, 4);
        sum(e);
        EdidInfo i;
        CHECK(edid_parse(e, 256, &i));
        CHECK(i.valid && i.has_cea);
        CHECK_EQ(i.mask, BIT(2) | BIT(1) | BIT(0) | BIT(8));
        CHECK_EQ(i.native, BIT(0));                    // the native flag is read, and only on the row it belongs to
        CHECK_EQ(offered(e, 256), BIT(2) | BIT(1) | BIT(0) | BIT(8));
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_PAL50), 2);       // the machine's family first: 1080p50
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_NTSC60), 8);
        CHECK_EQ(video_modes_default(e, 256, VID_SYS_PAL50), 2);
        CHECK_EQ(video_mode_from_edid(e, 256, true), VID_HDMI_1080P);
        // the 128-byte read of a screen whose first block says 1 extension: only the base is there, no CEA, nothing named
        CHECK_EQ(offered(e, 128), BIT(10));
    }

    // --- Q6: a screen with 60 Hz only, asked by a PAL machine, takes the other family; asked by the older single-family question, never
    {
        const uint8_t v[] = { 16, 4, 3 };
        base(e, 1);
        cea(e + 128, v, 3);
        sum(e);
        CHECK_EQ(offered(e, 256), BIT(8) | BIT(7) | BIT(6));
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_PAL50), 8);       // nothing at 50 Hz: 1080p60, silently
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_NTSC60), 8);
        CHECK_EQ(video_mode_from_edid(e, 256, true), VID_HDMI_480_576);       // T105: never 1080p50 on this screen
        CHECK_EQ(video_mode_from_edid(e, 256, false), VID_HDMI_1080P);
    }
    // a PAL television with 50 Hz rows and a PC row: the machine's family beats the PC row, and a 50 Hz-only list in NTSC takes the 50 Hz row
    {
        const uint8_t v[] = { 19, 17 };
        base(e, 1);
        cea(e + 128, v, 2);
        sum(e);
        CHECK_EQ(offered(e, 256), BIT(1) | BIT(0));
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_PAL50), 1);
        CHECK_EQ(video_modes_best(offered(e, 256), VID_SYS_NTSC60), 1);
        CHECK_EQ(video_modes_best(BIT(0) | BIT(10), VID_SYS_PAL50), 0);       // 576p50 over 1024 x 768
        CHECK_EQ(video_modes_best(BIT(6) | BIT(10), VID_SYS_NTSC60), 6);      // 480p over the PC row, 1080p > 720p > 576p/480p > PC
        CHECK_EQ(video_modes_best(BIT(11) | BIT(10) | BIT(9), VID_SYS_PAL50), 10);
        CHECK_EQ(video_modes_best(0, VID_SYS_PAL50), -1);
    }

    // --- a PC monitor with no CEA block: 1024 x 768 in the detailed timing, 800 x 600 established, 1280 x 1024 standard (5:4 at 60 Hz)
    {
        base(e, 0);
        dtd(e + 54, 65000000, 1024, 1344, 768, 806);
        e[35] = 0x01;                                  // 800 x 600 at 60 Hz
        stdt(e, 0, 1280, 2, 60);
        sum(e);
        EdidInfo i;
        CHECK(edid_parse(e, 128, &i) && !i.has_cea);
        CHECK_EQ(offered(e, 128), PC3);
        CHECK_EQ(offered(e, 256), PC3);                // a 256-byte read with a zero second block and no extension count
        CHECK_EQ(video_modes_best(offered(e, 128), VID_SYS_PAL50), 10);      // no family-0 row: the PC rows, 1024 x 768 first
        CHECK_EQ(video_mode_from_edid(e, 128, true), VID_HDMI_1024x768);
        CHECK_EQ(video_mode_from_edid(e, 128, false), VID_HDMI_1024x768);
        CHECK((offered(e, 128) & (BIT(3) | BIT(4) | BIT(5))) == 0);          // Q5: the 50 Hz PC rows are never offered
    }
    // established timings alone: 800 x 600 and 1024 x 768 at 60 Hz
    {
        base(e, 0);
        e[35] = 0x01;
        e[36] = 0x08;
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(9) | BIT(10));
    }
    // the same monitor with the standard timing at 75 Hz: not named at 60, not offered; and 1080i is not progressive
    {
        base(e, 0);
        stdt(e, 0, 1280, 2, 75);
        stdt(e, 1, 1024, 1, 75);
        dtd(e + 54, 148500000, 1920, 2200, 1080, 1125, true);
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10));            // valid EDID, no CEA, nothing named: the DVI baseline
    }
    // DTD of a real 1080p60 monitor and the 800 x 600 of VESA (1056 x 628 at 40 MHz); a 720p50 timing is row 1 and not row 7
    {
        base(e, 0);
        dtd(e + 54, 148500000, 1920, 2200, 1080, 1125);
        dtd(e + 72, 40000000, 800, 1056, 600, 628);
        dtd(e + 90, 74250000, 1280, 1980, 720, 750);
        dtd(e + 108, 108000000, 1280, 1688, 1024, 1066);
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(8) | BIT(9) | BIT(1) | BIT(11));
        CHECK_EQ(video_modes_best(offered(e, 128), VID_SYS_PAL50), 1);       // PAL: the 50 Hz 720p beats the other family's 1080p
        CHECK_EQ(video_modes_best(offered(e, 128), VID_SYS_NTSC60), 8);
    }
    // the second-descriptor 0xFA block carries six more standard timings
    {
        base(e, 0);
        uint8_t *d = e + 54;
        memset(d, 0, 18);
        d[3] = 0xFA;
        for (int i = 0; i < 6; i++) { d[5 + 2 * i] = 0x01; d[6 + 2 * i] = 0x01; }
        d[5] = (uint8_t)(1024 / 8 - 31); d[6] = (uint8_t)(1 << 6);
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10));
    }

    // --- range limits veto what the screen cannot take (the CEA video data block's own rows are not vetoed by them)
    {
        base(e, 0);
        stdt(e, 0, 1280, 2, 60);                       // 1280 x 1024: 63.1 kHz, 106.5 MHz
        stdt(e, 1, 1024, 1, 60);                       // 1024 x 768: 48.4 kHz, 65 MHz
        limits(e + 72, 56, 76, 30, 50, 140);           // up to 50 kHz
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10));            // by the line rate
        limits(e + 72, 56, 76, 30, 100, 80);           // 80 MHz
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10));            // by the pixel clock
        limits(e + 72, 56, 76, 30, 100, 140);
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10) | BIT(11));
        limits(e + 72, 70, 76, 30, 100, 140);          // 70 .. 76 Hz: 60 Hz is out
        sum(e);
        CHECK_EQ(offered(e, 128), BIT(10));            // nothing left after the vetoes: the DVI baseline (1024 x 768 at 60 Hz) stands in
    }
    // the HDMI vendor block's TMDS clock: 75 MHz keeps the 720p rows, drops 1080p even though they are in the video data block
    {
        const uint8_t v[] = { 31, 19, 16, 4 };
        base(e, 1);
        cea(e + 128, v, 4, 75);
        sum(e);
        EdidInfo i;
        CHECK(edid_parse(e, 256, &i));
        CHECK_EQ(i.max_tmds_mhz, 75);
        CHECK_EQ(i.mask, BIT(1) | BIT(7));
        cea(e + 128, v, 4, 165);
        sum(e);
        CHECK_EQ(offered(e, 256), BIT(2) | BIT(1) | BIT(8) | BIT(7));
    }

    // --- Q9: no EDID, a corrupt one, a short read
    {
        const uint8_t v[] = { 16 };
        base(e, 1);
        cea(e + 128, v, 1);
        sum(e);
        CHECK_EQ(offered(e, 256), BIT(8));
        e[40] ^= 0x04;                                 // base block: checksum no longer holds
        CHECK_EQ(offered(e, 256), EDID_LIST_NONE);
        CHECK_EQ(video_modes_default(e, 256, VID_SYS_PAL50), 0);              // 576p
        CHECK_EQ(video_modes_default(e, 256, VID_SYS_NTSC60), 6);             // 480p
        CHECK_EQ(video_mode_from_edid(e, 256, true), VID_HDMI_480_576);
        e[40] ^= 0x04;
        e[128 + 20] ^= 0x01;                           // extension: same
        CHECK_EQ(offered(e, 256), EDID_LIST_NONE);
        e[128 + 20] ^= 0x01;
        CHECK_EQ(offered(e, 256), BIT(8));
        CHECK_EQ(offered(e, 100), EDID_LIST_NONE);     // less than a block
        e[0] = 0x55;
        CHECK_EQ(offered(e, 256), EDID_LIST_NONE);     // not an EDID
    }
    CHECK_EQ(offered(0, 0), EDID_LIST_NONE);
    CHECK_EQ(EDID_LIST_NONE, BIT(0) | BIT(1) | BIT(2) | BIT(6) | BIT(7) | BIT(8));    // the six television rows, no PC row
    CHECK_EQ(video_modes_default(0, 0, VID_SYS_PAL50), 0);
    CHECK_EQ(video_modes_default(0, 0, VID_SYS_NTSC60), 6);
    CHECK_EQ(video_modes_best(EDID_LIST_NONE, VID_SYS_PAL50), 2);             // the blind V key has the whole list to cycle
    // a CEA block that names nothing we drive: the television list
    {
        const uint8_t v[] = { 5, 20 };                 // 1080i
        base(e, 1);
        cea(e + 128, v, 2);
        sum(e);
        CHECK_EQ(offered(e, 256), EDID_LIST_NONE);
        CHECK_EQ(video_modes_default(e, 256, VID_SYS_PAL50), 0);
    }
    // a collection whose length walks out of its block is not believed
    {
        const uint8_t v[] = { 31 };
        base(e, 1);
        cea(e + 128, v, 1);
        e[128 + 4] = (uint8_t)((2 << 5) | 31);
        e[128 + 2] = 6;
        sum(e + 128);
        sum(e);
        CHECK_EQ(offered(e, 256), EDID_LIST_NONE);
    }
    // the row numbering the gateware uses
    CHECK_EQ(video_row_of(VID_HDMI_1080P, VID_SYS_PAL50), 2);
    CHECK_EQ(video_row_of(VID_HDMI_1024x768, VID_SYS_NTSC60), 10);
    CHECK_EQ(video_row_hdmi(10), VID_HDMI_1024x768);
    CHECK_EQ(video_row_sys(7), VID_SYS_NTSC60);

    DONE("test_edid");
}
