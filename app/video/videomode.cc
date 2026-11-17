#include "videomode.h"
#include <stdio.h>
#include <string.h>

uint32_t video_current;

static const char *const k_hdmi[VID_HDMI_COUNT] = {
    "576p / 480p", "1280 x 720", "1920 x 1080", "800 x 600", "1024 x 768", "1280 x 1024",
};

const char *video_hdmi_name(int hdmi)
{
    return (hdmi >= 0 && hdmi < VID_HDMI_COUNT) ? k_hdmi[hdmi] : "?";
}

const char *video_sys_name(int sys)
{
    return sys == VID_SYS_NTSC60 ? "NTSC 60 Hz" : sys == VID_SYS_PAL60 ? "PAL 60 Hz" : sys == VID_SYS_NTSC50 ? "NTSC 50 Hz" : "PAL 50 Hz";
}

// 576p and 480p share one setting; 576 is the taller of the two, so a picture that fits 576 may still
// be cut on a 480-line screen. The fit below therefore measures against 480.
static const struct { int w, h; } k_size[VID_HDMI_COUNT] = {
    { 720, 480 }, { 1280, 720 }, { 1920, 1080 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 },
};

void video_mode_size(int hdmi, int *w, int *h)
{
    bool ok = hdmi >= 0 && hdmi < VID_HDMI_COUNT;
    if (w) {
        *w = ok ? k_size[hdmi].w : 0;
    }
    if (h) {
        *h = ok ? k_size[hdmi].h : 0;
    }
}

// The frame a picture is PLACED in. The host's output is always 1920 x 1080: host/vic_scaler.v has the 1080p timing wired in and
// host/overlay.v:8 ignores the HDMI timing registers, so video_apply() changes nothing the pixel layer can see, and the layer's
// position is in pixels of that frame (host/pix_layer.v). hdmi and sys are therefore not read (T107 5.6, Alex: "No, simply 1080p always").
void video_frame_size(int hdmi, int sys, int *w, int *h)
{
    (void)hdmi;
    (void)sys;
    video_mode_size(VID_HDMI_1080P, w, h);
}

void video_layout(int w, int h, int hdmi, int sys, int bias_x, int bias_y, int *scale, int *x0, int *y0)
{
    int mw = 0, mh = 0;
    video_frame_size(hdmi, sys, &mw, &mh);
    if (mw <= 0 || mh <= 0) {
        video_frame_size(VID_HDMI_1080P, sys, &mw, &mh);
    }
    int s = 1;
    for (int t = 3; t > 1; t--) {
        if (w * t <= mw && h * t <= mh) {
            s = t;
            break;
        }
    }
    // centred, then moved by the core's own offset (source pixels, so it grows with the scale), then kept
    // inside the frame: the offset never pushes the picture out of it, and a picture too large for the
    // frame even at scale 1 sits at 0, 0
    int rx = mw - w * s, ry = mh - h * s;
    int px = rx > 0 ? rx / 2 + bias_x * s : 0;
    int py = ry > 0 ? ry / 2 + bias_y * s : 0;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > rx && rx > 0) px = rx;
    if (py > ry && ry > 0) py = ry;
    if (scale) {
        *scale = s;
    }
    if (x0) {
        *x0 = px;
    }
    if (y0) {
        *y0 = py;
    }
}

const char *video_scale_name(int scale)
{
    static const char *const n[VID_SCALE_COUNT] = { "Integer", "Fit", "Fill" };
    return (scale >= 0 && scale < VID_SCALE_COUNT) ? n[scale] : "?";
}

void video_active_size(int hdmi, int sys, int *w, int *h)
{
    video_mode_size(hdmi, w, h);
    if (hdmi == VID_HDMI_480_576 && sys != VID_SYS_NTSC60) {
        if (h) {
            *h = 576;
        }
    } else if (hdmi == VID_HDMI_480_576 && w) {
        *w = 640;
    }
}

int video_mode_index(int hdmi, int sys)
{
    return (sys == VID_SYS_NTSC60 ? 6 : 0) + hdmi;
}

void video_layout_scaled(int w, int h, int hdmi, int sys, int scale_mode, int bias_x, int bias_y, int *ow, int *oh, int *x0, int *y0)
{
    int fw = 0, fh = 0;
    video_active_size(hdmi, sys, &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        video_active_size(VID_HDMI_1080P, sys, &fw, &fh);
    }
    int w_out = w, h_out = h;
    if (w > 0 && h > 0) {
        if (scale_mode == VID_SCALE_FILL) {
            w_out = fw;
            h_out = fh;
        } else if (scale_mode == VID_SCALE_FIT) {
            if ((long)fw * h <= (long)fh * w) {
                w_out = fw;
                h_out = (int)((long)fw * h / w);
            } else {
                h_out = fh;
                w_out = (int)((long)fh * w / h);
            }
        } else {
            int s = fw / w < fh / h ? fw / w : fh / h;
            if (s < 1) {
                s = 1;
            }
            w_out = w * s;
            h_out = h * s;
        }
    }
    int rx = fw - w_out, ry = fh - h_out;
    int bx = w > 0 ? bias_x * w_out / w : 0, by = h > 0 ? bias_y * h_out / h : 0;
    int px = rx > 0 ? rx / 2 + bx : 0;
    int py = ry > 0 ? ry / 2 + by : 0;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > rx && rx > 0) px = rx;
    if (py > ry && ry > 0) py = ry;
    if (ow) *ow = w_out;
    if (oh) *oh = h_out;
    if (x0) *x0 = px;
    if (y0) *y0 = py;
}

int video_mode_fits(int w, int h, bool allow_pc_modes)
{
    if (w <= 0 || h <= 0) {
        return VID_HDMI_1080P;
    }
    int best = VID_HDMI_1080P, best_fill = -1, best_px = 0;
    for (int m = 0; m < VID_HDMI_COUNT; m++) {
        if (!allow_pc_modes && m != VID_HDMI_480_576 && m != VID_HDMI_720P && m != VID_HDMI_1080P) {
            continue;                                  // the three CEA modes a television also takes
        }
        if (k_size[m].w < w || k_size[m].h < h) {
            continue;                                  // the picture would be cut
        }
        int fill = h * 1000 / k_size[m].h;             // per mille of the frame's height
        int px = k_size[m].w * k_size[m].h;
        if (fill > best_fill || (fill == best_fill && px > best_px)) {
            best = m;
            best_fill = fill;
            best_px = px;
        }
    }
    return best_fill < 0 ? VID_HDMI_1080P : best;
}

uint32_t video_pack(int sys, int hdmi)
{
    if (sys < 0 || hdmi < 0 || hdmi >= VID_HDMI_COUNT) {
        return 0;
    }
    return 0x80000000u | ((uint32_t)(sys & 0xFF) << 8) | (uint32_t)(hdmi & 0xFF);
}

bool video_unpack(uint32_t v, int *sys, int *hdmi)
{
    if (!(v & 0x80000000u)) {
        return false;
    }
    int h = (int)(v & 0xFF);
    if (h >= VID_HDMI_COUNT) {
        return false;
    }
    *sys = (int)((v >> 8) & 0xFF);
    *hdmi = h;
    return true;
}

bool video_gw_probe_io(const GwIo *io, GwStatus *st)
{
    *st = GwStatus();
    st->present = io->rd(GW_MAGIC_AT) == GW_MAGIC;
    if (!st->present) {
        return false;
    }
    st->version = io->rd(GW_VERSION_AT);
    st->flags = io->rd(GW_FLAGS_AT);
    st->mode = io->rd(GW_MODE_AT);
    st->seen = io->rd(GW_SEEN_AT);
    st->done = io->rd(GW_DONE_AT);
    return st->version >= 1;
}

bool video_gw_up(const GwStatus &st, int row)
{
    if (!st.present || (st.seen == st.done && (st.flags & GW_F_REFUSED))) {
        return false;
    }
    return (st.flags & GW_F_PIX_LOCKED) && (st.flags & GW_F_RUNNING) && st.mode == row;
}

bool video_gw_has_scale(const GwStatus &st)
{
    // T228: the C64-type flag alone decides. The MultiCore bitstreams tie c64_type to 0 wherever their mode_req is version 1 (host/top.v and every
    // cores/*/mono/host/top.v); TRX64-Ultimate's U16 mode_req answers version 1 with the flag set, has the scale register (0x4F01 / 0x4F0A) and holds the
    // same seven rows and the System Mode's family as T222's version 2.
    return st.present && st.version >= 1 && (st.flags & GW_F_C64_TYPE) != 0;
}

bool video_gw_has_smooth(const GwStatus &st)
{
    return video_gw_has_scale(st) && st.version >= 3;
}

bool video_gw_has_follow(const GwStatus &st)
{
    return st.present && st.version >= 4;
}

int video_gw_row_for(const GwStatus &st, int sys, int hdmi)
{
    if (video_gw_has_scale(st) && sys == VID_SYS_PAL60) {
        sys = VID_SYS_NTSC60;                      // T222: the gateware runs the 60 Hz family for PAL-60, and reports that row back (0x4F07)
    }
    if (video_gw_has_scale(st) && hdmi > VID_HDMI_800x600) {
        hdmi = VID_HDMI_800x600;
    }
    if (video_gw_has_scale(st) && hdmi == VID_HDMI_800x600 && sys != VID_SYS_NTSC60) {
        hdmi = VID_HDMI_480_576;
    }
    return video_mode_index(hdmi, sys);
}

bool video_gw_set_scale_io(const GwIo *io, int scale, int timeout_ms)
{
    if (scale < 0 || scale >= VID_SCALE_COUNT) {
        return false;
    }
    io->wr(GW_SCALE, (uint8_t)scale);
    for (int t = 0;; t += 10) {
        if (io->rd(GW_SCALE_AT) == (uint8_t)scale) {
            return true;
        }
        if (t >= timeout_ms) {
            return false;
        }
        io->wait_ms(10);
    }
}

bool video_gw_set_follow_io(const GwIo *io, int on, int timeout_ms)
{
    if (on != 0 && on != 1) {
        return false;
    }
    io->wr(GW_FOLLOW, (uint8_t)on);
    int t = 0;
    while (io->rd(GW_FOLLOW_AT) != (uint8_t)on) {
        if (t >= timeout_ms) {
            return false;
        }
        io->wait_ms(10);
        t += 10;
    }
    io->wait_ms(20);                                   // the gateware starts the re-tune within a few clocks; the status of before it is not an answer
    t += 20;
    for (;; t += 10) {
        GwStatus st;
        video_gw_probe_io(io, &st);
        if (st.present && (st.flags & GW_F_PIX_LOCKED) && (st.flags & GW_F_RUNNING)) {
            return true;
        }
        if (t >= timeout_ms) {
            return false;
        }
        io->wait_ms(10);
    }
}

bool video_gw_set_smooth_io(const GwIo *io, int on, int timeout_ms)
{
    if (on != 0 && on != 1) {
        return false;
    }
    io->wr(GW_SMOOTH, (uint8_t)on);
    for (int t = 0;; t += 10) {
        if (io->rd(GW_SMOOTH) == (uint8_t)on) {
            return true;
        }
        if (t >= timeout_ms) {
            return false;
        }
        io->wait_ms(10);
    }
}

bool video_gw_apply_io(const GwIo *io, int sys, int hdmi, int timeout_ms)
{
    GwStatus st0;
    video_gw_probe_io(io, &st0);
    int row = video_gw_row_for(st0, sys, hdmi);
    io->wr(GW_REQ, (uint8_t)row);
    GwStatus st;
    for (int t = 0;; t += 10) {
        video_gw_probe_io(io, &st);
        if (st.present && st.seen == st.done && (st.flags & GW_F_REFUSED)) {
            return false;                              // the gateware says no: nothing changed, waiting does not help
        }
        if (video_gw_up(st, row)) {
            return true;
        }
        if (t >= timeout_ms) {
            return false;
        }
        io->wait_ms(10);
    }
}

#ifndef VIDEOMODE_NO_BOARD
#include "u64.h"
#include "itu.h"
#include "i2c_drv.h"
#include "hdmi_scan.h"

extern "C" void SetVideoPll(t_video_mode mode, int ppm);
extern "C" void ResetHdmiPll(void);

// The read Gideon's configuration does (u64_config.cc:2833-2882), without the parts that write files.
bool video_read_edid(uint8_t *buf, int max, int *len)
{
    *len = 0;
    if ((U64_HDMI_REG & U64_HDMI_HPD_CURRENT) == 0) {
        printf("[video] no hot-plug: nothing is attached, or it does not say so\n");
        return false;
    }
    if (!i2c || max < 128) {
        return false;
    }
    U64_HDMI_REG = U64_HDMI_DDC_ENABLE;
    i2c->i2c_lock("video EDID");
    i2c->set_channel(I2C_CHANNEL_HDMI);
    bool ok = i2c->i2c_read_block(0xA0, 0x00, buf, 128) == 0;
    if (ok) {
        *len = 128;
        if (buf[126] && max >= 256 && i2c->i2c_read_block(0xA0, 0x80, buf + 128, 128) == 0) {
            *len = 256;
        }
    } else {
        printf("[video] the screen did not answer on the DDC lines\n");
    }
    U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
    i2c->i2c_unlock();
    return ok;
}

void video_apply(int sys, int hdmi)
{
    SetVideoPll((t_video_mode)sys, 0);
    SetVideoMode1080p((t_video_mode)sys, (t_hdmi_mode)hdmi);
    ResetHdmiPll();
    U64_HDMI_ENABLE = 1;
}

static void gw_wr(uint32_t off, uint8_t v) { *(volatile uint8_t *)(VID_IO_BASE + off) = v; }
static uint8_t gw_rd(uint32_t off) { return *(volatile uint8_t *)(VID_IO_BASE + off); }
static void gw_wait(int ms) { wait_ms(ms); }
static const GwIo k_gw_io = { gw_wr, gw_rd, gw_wait };

bool video_gateware_present(void)
{
    static bool seen = false;                      // an old host never grows the gateware: only a yes is kept
    if (!seen) {
        GwStatus st;
        seen = video_gw_probe_io(&k_gw_io, &st);
    }
    return seen;
}

int video_gateware_row(void)
{
    GwStatus st;
    if (!video_gw_probe_io(&k_gw_io, &st)) {
        return -1;
    }
    return st.mode < 12 ? st.mode : -1;
}

bool video_gateware_vic_scale(void)
{
    static bool seen = false;                      // the gateware does not change under a running application: only a yes is kept
    if (!seen) {
        GwStatus st;
        video_gw_probe_io(&k_gw_io, &st);
        seen = video_gw_has_scale(st);
    }
    return seen;
}

bool video_set_scale_gateware(int scale)
{
    return video_gw_set_scale_io(&k_gw_io, scale, GW_SCALE_TIMEOUT_MS);
}

bool video_gateware_vic_smooth(void)
{
    static bool seen = false;                      // as video_gateware_vic_scale: only a yes is kept
    if (!seen) {
        GwStatus st;
        video_gw_probe_io(&k_gw_io, &st);
        seen = video_gw_has_smooth(st);
    }
    return seen;
}

bool video_set_smooth_gateware(int on)
{
    return video_gw_set_smooth_io(&k_gw_io, on, GW_SCALE_TIMEOUT_MS);
}

bool video_gateware_follow(void)
{
    static bool seen = false;                      // as video_gateware_vic_scale: only a yes is kept
    if (!seen) {
        GwStatus st;
        video_gw_probe_io(&k_gw_io, &st);
        seen = video_gw_has_follow(st);
    }
    return seen;
}

bool video_set_follow_gateware(int on)
{
    return video_gw_set_follow_io(&k_gw_io, on, GW_APPLY_TIMEOUT_MS);
}

bool video_apply_gateware(int sys, int hdmi)
{
    bool ok = video_gw_apply_io(&k_gw_io, sys, hdmi, GW_APPLY_TIMEOUT_MS);   // the synthesizer, the GTP and mmcm_pix follow the request
    if (!ok) {
        printf("[video] the gateware did not bring %s %s up\n", video_sys_name(sys), video_hdmi_name(hdmi));
    }
    U64_HDMI_ENABLE = 1;
    return ok;
}

bool video_edid_sig(uint32_t *sig)
{
    if ((U64_HDMI_REG & U64_HDMI_HPD_CURRENT) == 0 || !i2c) {
        return false;
    }
    uint8_t b[2] = { 0, 0 };
    U64_HDMI_REG = U64_HDMI_DDC_ENABLE;
    i2c->i2c_lock("video EDID sig");
    i2c->set_channel(I2C_CHANNEL_HDMI);
    bool ok = i2c->i2c_read_block(0xA0, 0x7E, b, 2) == 0;     // extension count, checksum
    U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
    i2c->i2c_unlock();
    *sig = ok ? (0x80000000u | ((uint32_t)b[0] << 8) | b[1]) : 0x80000000u;   // attached, but it did not answer: its own value
    return true;
}

void video_start(int stored_sys, int stored_hdmi, int *used_sys, int *used_hdmi)
{
    int sys = (stored_sys >= 0) ? stored_sys : VID_SYS_PAL50;
    int hdmi;
    if (stored_hdmi >= 0 && stored_hdmi < VID_HDMI_COUNT) {
        hdmi = stored_hdmi;
        printf("[video] %s, %s (chosen)\n", video_sys_name(sys), video_hdmi_name(hdmi));
    } else {
        static uint8_t edid[256];
        int len = 0;
        video_read_edid(edid, sizeof(edid), &len);
        hdmi = video_mode_from_edid(edid, len, sys == VID_SYS_PAL50);
        printf("[video] %s, %s (from the screen, %d bytes of EDID)\n", video_sys_name(sys),
               video_hdmi_name(hdmi), len);
    }
    // The host app asks twice (before the flash disk is mounted, then with boot.cfg's choice). The same answer twice is not
    // applied twice: the PLL and the HDMI encoder are reset by video_apply, and the screen loses and regains its sync (T101 S9).
    if (video_current != video_pack(sys, hdmi)) {
        video_apply(sys, hdmi);
    } else {
        printf("[video] this mode is up already, not applied again\n");
    }
    video_current = video_pack(sys, hdmi);
    if (used_sys) {
        *used_sys = sys;
    }
    if (used_hdmi) {
        *used_hdmi = hdmi;
    }
}
#endif
