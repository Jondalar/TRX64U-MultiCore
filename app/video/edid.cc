#include "edid.h"
#include <string.h>

// The 9 rows of host/hdmi_modes.vh: active area, raster (active + front porch + sync + back porch) and pixel clock (hm_rate). The
// consistency with the generated file is a test (tests/coreapp/test_edid.cc).
static const EdidRow k_rows[EDID_ROWS] = {
    /* 0  576p 50   */ { 720, 576, 864, 625, 27067117u, { 17, 18, 0 } },
    /* 1  720p 50   */ { 1280, 720, 1980, 750, 74434572u, { 19, 0, 0 } },
    /* 2  1080p 50  */ { 1920, 1080, 2640, 1125, 148869143u, { 31, 0, 0 } },
    /* 3  (no row)  */ { 0, 0, 0, 0, 0, { 0, 0, 0 } },
    /* 4  (no row)  */ { 0, 0, 0, 0, 0, { 0, 0, 0 } },
    /* 5  (no row)  */ { 0, 0, 0, 0, 0, { 0, 0, 0 } },
    /* 6  480p 60   */ { 640, 480, 800, 525, 25125345u, { 1, 2, 3 } },
    /* 7  720p 60   */ { 1280, 720, 1650, 750, 74035605u, { 4, 0, 0 } },
    /* 8  1080p 60  */ { 1920, 1080, 2200, 1125, 148071211u, { 16, 0, 0 } },
    /* 9  800x600   */ { 800, 600, 1052, 650, 41051794u, { 0, 0, 0 } },
    /* 10 1024x768  */ { 1024, 768, 1344, 806, 64995343u, { 0, 0, 0 } },
    /* 11 1280x1024 */ { 1280, 1024, 1688, 1052, 106548910u, { 0, 0, 0 } },
};

const EdidRow *edid_row(int m)
{
    return (m >= 0 && m < EDID_ROWS && k_rows[m].w) ? &k_rows[m] : 0;
}

static bool within_pct(long long a, long long b, int pct)
{
    long long d = a > b ? a - b : b - a;
    return d * 100 <= b * pct;
}

static long long frame_mhz(long long clk_hz, long long htot, long long vtot)
{
    return htot > 0 && vtot > 0 ? clk_hz * 1000 / (htot * vtot) : 0;
}

// A detailed timing names a row when the active area is the row's and the frame rate is within 1 Hz of it (the 50 and the 60 Hz rows of
// one size differ by 10 Hz, 720p and 1080p raster 20 % apart). The raster itself is not held to 1 %: the VESA 800 x 600 mode is 1056 x 628
// at 40 MHz, our row 1052 x 650 at 41.05 MHz (3.5 % in the frame height), and the screen takes both; the pixel clock only has to be
// within 5 %.
static uint16_t row_of_timing(int hact, int htot, int vact, int vtot, long clk_hz)
{
    uint16_t m = 0;
    long long fr = frame_mhz(clk_hz, htot, vtot);
    for (int r = 0; r < EDID_ROWS; r++) {
        const EdidRow &w = k_rows[r];
        long long d = fr - frame_mhz(w.pixel_hz, w.htotal, w.vtotal);
        if (w.w && hact == w.w && vact == w.h && (d < 0 ? -d : d) <= 1000 && within_pct(clk_hz, (long long)w.pixel_hz, 5)) {
            m |= (uint16_t)(1u << r);
        }
    }
    return m;
}

// A VESA mode at 60 Hz named by an established or standard timing: the screen takes the mode by its active size. Only the three PC rows
// (G11): 640 x 480 is the television row 6, which a DVI monitor gets only when its detailed timing says so.
static uint16_t row_of_vesa60(int w, int h)
{
    uint16_t m = 0;
    for (int r = 9; r < EDID_ROWS; r++) {
        if (k_rows[r].w == w && k_rows[r].h == h) {
            m |= (uint16_t)(1u << r);
        }
    }
    return m;
}

static int std_height(int w, int aspect)
{
    switch (aspect) {
    case 0:  return w * 10 / 16;
    case 1:  return w * 3 / 4;
    case 2:  return w * 4 / 5;
    default: return w * 9 / 16;
    }
}

struct Limits {
    bool present;
    int vmin, vmax, hmin, hmax;      // Hz and kHz
    int clk_mhz;                     // 0: none
};

static void descriptor(const uint8_t *d, uint16_t *mask, Limits *lim)
{
    if (d[0] || d[1]) {              // a detailed timing
        if (d[17] & 0x80) {
            return;                  // interlaced: the host puts out progressive frames only
        }
        long clk = ((long)d[0] | ((long)d[1] << 8)) * 10000L;
        int hact = d[2] | ((d[4] & 0xF0) << 4), hbl = d[3] | ((d[4] & 0x0F) << 8);
        int vact = d[5] | ((d[7] & 0xF0) << 4), vbl = d[6] | ((d[7] & 0x0F) << 8);
        *mask |= row_of_timing(hact, hact + hbl, vact, vact + vbl, clk);
        return;
    }
    if (d[2] != 0) {
        return;
    }
    if (d[3] == 0xFD) {              // range limits
        int vo = d[4] & 3, ho = (d[4] >> 2) & 3;
        lim->present = true;
        lim->vmin = d[5] + (vo == 3 ? 255 : 0);
        lim->vmax = d[6] + (vo >= 2 ? 255 : 0);
        lim->hmin = d[7] + (ho == 3 ? 255 : 0);
        lim->hmax = d[8] + (ho >= 2 ? 255 : 0);
        lim->clk_mhz = (d[9] == 0 || d[9] == 0xFF) ? 0 : d[9] * 10;
    } else if (d[3] == 0xFA) {       // six more standard timings
        for (int i = 0; i < 6; i++) {
            const uint8_t *s = d + 5 + 2 * i;
            if (s[0] == 0x01 && s[1] == 0x01) {
                continue;
            }
            int w = (s[0] + 31) * 8, f = (s[1] & 0x3F) + 60;
            if (f == 60) {
                *mask |= row_of_vesa60(w, std_height(w, s[1] >> 6));
            }
        }
    }
}

// A VIC to the row that carries it, -1 for one we do not drive.
static int row_of_vic(int vic)
{
    for (int r = 0; r < EDID_ROWS; r++) {
        for (int i = 0; i < 3; i++) {
            if (k_rows[r].w && k_rows[r].vic[i] == vic) {
                return r;
            }
        }
    }
    return -1;
}

static void cea_blocks(const uint8_t *ext, uint16_t *svd, uint16_t *native, uint16_t *mask, Limits *lim, int *tmds)
{
    int dtd = ext[2];
    if (dtd < 4 || dtd > 127) {
        return;                      // no collection, or one that does not fit in the block
    }
    int p = 4;
    while (p < dtd) {
        int tag = ext[p] >> 5, n = ext[p] & 0x1F;
        if (p + 1 + n > dtd) {
            break;                   // a length that walks out of the collection: stop believing it
        }
        const uint8_t *b = ext + p + 1;
        if (tag == 2) {              // short video descriptors; 1 .. 64 may carry the native flag in bit 7, the codes above 64 are plain
            for (int i = 0; i < n; i++) {
                int v = b[i], nat = 0;
                if (v >= 129 && v <= 192) {
                    v &= 0x7F;
                    nat = 1;
                }
                int r = row_of_vic(v);
                if (r >= 0) {
                    *svd |= (uint16_t)(1u << r);
                    if (nat) {
                        *native |= (uint16_t)(1u << r);
                    }
                }
            }
        } else if (tag == 3 && n >= 7 && b[0] == 0x03 && b[1] == 0x0C && b[2] == 0x00) {   // HDMI vendor block, byte 6: max TMDS clock in 5 MHz
            *tmds = b[6] * 5;
        }
        p += 1 + n;
    }
    for (int q = dtd; q + 18 <= 127; q += 18) {
        descriptor(ext + q, mask, lim);
    }
}

static bool block_ok(const uint8_t *b)
{
    unsigned s = 0;
    for (int i = 0; i < 128; i++) {
        s += b[i];
    }
    return (s & 0xFF) == 0;
}

bool edid_parse(const uint8_t *edid, int len, EdidInfo *out)
{
    static const uint8_t header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memset(out, 0, sizeof(*out));
    if (!edid || len < 128 || memcmp(edid, header, 8) != 0 || !block_ok(edid)) {
        return false;
    }
    int blocks = 1 + edid[126];
    if (blocks * 128 > len) {
        blocks = len / 128;
    }
    for (int i = 1; i < blocks; i++) {
        if (!block_ok(edid + 128 * i)) {
            return false;
        }
    }
    uint16_t mask = 0, svd = 0, native = 0;
    Limits lim;
    memset(&lim, 0, sizeof(lim));
    int tmds = 0;

    // established timings: 800 x 600 and 1024 x 768 at 60 Hz
    if (edid[35] & 0x01) mask |= row_of_vesa60(800, 600);
    if (edid[36] & 0x08) mask |= row_of_vesa60(1024, 768);
    // standard timings
    for (int i = 0; i < 8; i++) {
        const uint8_t *s = edid + 38 + 2 * i;
        if (s[0] == 0x01 && s[1] == 0x01) {
            continue;
        }
        int w = (s[0] + 31) * 8;
        if ((s[1] & 0x3F) == 0) {
            mask |= row_of_vesa60(w, std_height(w, s[1] >> 6));
        }
    }
    for (int q = 54; q <= 108; q += 18) {
        descriptor(edid + q, &mask, &lim);
    }
    for (int i = 1; i < blocks; i++) {
        const uint8_t *ext = edid + 128 * i;
        if (ext[0] == 0x02) {
            out->has_cea = true;
            cea_blocks(ext, &svd, &native, &mask, &lim, &tmds);
        }
    }

    // The vetoes: what the screen's range limits and its HDMI vendor block rule out. A mode the CEA video data block names is the
    // screen's own statement about that timing and is not vetoed by the range limits (CEA-861 timings are outside their scope); the TMDS
    // clock limit holds for every row.
    for (int r = 0; r < EDID_ROWS; r++) {
        uint16_t b = (uint16_t)(1u << r);
        if (!k_rows[r].w) {
            continue;
        }
        const EdidRow &w = k_rows[r];
        if (tmds && (long)w.pixel_hz > (long)tmds * 1010000L) {
            mask &= (uint16_t)~b;
            svd &= (uint16_t)~b;
        }
        if (lim.present && !(svd & b)) {
            long hz = (long)w.pixel_hz / w.htotal;                         // line rate
            long vhz = (hz * 10 / w.vtotal + 5) / 10;                      // frame rate, rounded
            if (vhz < lim.vmin || vhz > lim.vmax || hz < lim.hmin * 980L || hz > lim.hmax * 1020L ||
                (lim.clk_mhz && (long)w.pixel_hz > lim.clk_mhz * 1010000L)) {
                mask &= (uint16_t)~b;
            }
        }
    }
    out->valid = true;
    out->mask = (uint16_t)((mask | svd) & EDID_ROW_MASK);
    out->native = (uint16_t)(native & out->mask);
    out->max_tmds_mhz = tmds;
    return true;
}

uint16_t video_modes_offered(const uint8_t *edid, int len)
{
    EdidInfo i;
    if (!edid_parse(edid, len, &i)) {
        return EDID_LIST_NONE;
    }
    if (i.mask) {
        return i.mask;
    }
    return i.has_cea ? EDID_LIST_NONE : (uint16_t)(1u << 10);
}

int video_modes_best(uint16_t mask, int sys)
{
    static const uint8_t order[2][6] = { { 2, 1, 0, 0, 0, 0 }, { 8, 7, 6, 10, 11, 9 } };
    static const int count[2] = { 3, 6 };
    mask &= EDID_ROW_MASK;
    int f = sys == VID_SYS_NTSC60 ? 1 : 0;
    for (int k = 0; k < 2; k++) {
        int fam = k == 0 ? f : 1 - f;
        for (int i = 0; i < count[fam]; i++) {
            if (mask & (1u << order[fam][i])) {
                return order[fam][i];
            }
        }
    }
    return -1;
}

int video_modes_hdmi_list(uint16_t mask, int sys, int *hdmi)
{
    mask &= EDID_ROW_MASK;
    int f = sys == VID_SYS_NTSC60 ? 1 : 0;
    uint16_t own = f ? 0x0FC0u : 0x0007u;               // the rows of the machine's family
    int n = 0;
    for (int h = 0; h < VID_HDMI_COUNT; h++) {
        int r = f * 6 + h;
        int o = (1 - f) * 6 + h;
        bool in = (mask & (1u << r)) != 0;
        if (!in && !(mask & own)) {
            in = (mask & (1u << o)) != 0;                // nothing in the machine's family: the other one
        }
        if (!in && h >= VID_HDMI_800x600 && f == 0) {
            in = (mask & (1u << o)) != 0;                // a PC mode has no 50 Hz row
        }
        if (in) {
            hdmi[n++] = h;
        }
    }
    return n;
}

bool video_modes_resolve(uint16_t mask, int sys, int hdmi, int *row)
{
    int l[VID_HDMI_COUNT];
    int n = video_modes_hdmi_list(mask, sys, l);
    for (int i = 0; i < n; i++) {
        if (l[i] == hdmi) {
            int r = video_row_of(hdmi, sys);
            if (!((mask & EDID_ROW_MASK) & (1u << r))) {
                r = video_row_of(hdmi, sys == VID_SYS_NTSC60 ? VID_SYS_PAL50 : VID_SYS_NTSC60);
            }
            *row = r;
            return true;
        }
    }
    return false;
}

int video_modes_default(const uint8_t *edid, int len, int sys)
{
    EdidInfo i;
    int f = sys == VID_SYS_NTSC60 ? 1 : 0;
    if (!edid_parse(edid, len, &i) || (!i.mask && i.has_cea)) {
        return f * 6 + VID_HDMI_480_576;
    }
    int m = video_modes_best(i.mask ? i.mask : (uint16_t)(1u << 10), sys);
    return m >= 0 ? m : f * 6 + VID_HDMI_480_576;
}

// The older question, kept for the applications that still hand over a single mode for a fixed family: the best row of the SAME family
// `pal` says, and a PC row only when the family has no television row. Never a row of the other family: a 60 Hz-only screen asked in PAL
// answers 576p/480p, not 1080p50 (T105).
int video_mode_from_edid(const uint8_t *edid, int len, bool pal)
{
    EdidInfo i;
    if (!edid_parse(edid, len, &i) || (!i.mask && i.has_cea)) {
        return VID_HDMI_480_576;
    }
    uint16_t m = i.mask ? i.mask : (uint16_t)(1u << 10);
    static const uint8_t tv[2][3] = { { 2, 1, 0 }, { 8, 7, 6 } };
    const uint8_t *t = tv[pal ? 0 : 1];
    for (int k = 0; k < 3; k++) {
        if (m & (1u << t[k])) {
            return video_row_hdmi(t[k]);
        }
    }
    static const uint8_t pc[3] = { 10, 11, 9 };
    for (int k = 0; k < 3; k++) {
        if (m & (1u << pc[k])) {
            return video_row_hdmi(pc[k]);
        }
    }
    return VID_HDMI_480_576;
}
