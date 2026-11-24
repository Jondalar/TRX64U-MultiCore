#include "video.h"
#include "videomode.h"
#include "edid.h"
#include "trxcanvas.h"
#include <string.h>
#include <stdlib.h>

#define DDR_WINDOW_BYTES 0x1000000u

void Video::write_set(bool on)
{
    if (own_picture()) {                           // nothing of ours to put on the layer: it is off, the core's picture is not on it
        px_->w32(PX_CTRL, ctrl(false));
        on_ = false;
        return;
    }
    px_->w32(PX_CTRL, ctrl(false));       // never change the geometry under a shown layer
    px_->w32(PX_KEY, 0);
    if (!d_.pxfmt) {
        px_->w32(PX_FRONT, front_);                // a capture is shown from the gateware's own slots (format 3): no buffer to name
    }
    px_->w32(PX_SIZE, (vh_ << 16) | vw_);
    px_->w32(PX_STRIDE, d_.stride_bytes());      // the rows are still the picture's
    int32_t px = d_.x0 + (int32_t)((d_.w - vw_) / 2) * d_.scale, py = d_.y0 + (int32_t)((d_.h - vh_) / 2) * d_.scale;
    if (gw_) {                                     // any output size: the shown part of the picture keeps the ratio of the whole one
        px = d_.x0 + (int32_t)((d_.w - vw_) / 2 * ow_ / d_.w);
        py = d_.y0 + (int32_t)((d_.h - vh_) / 2 * oh_ / d_.h);
        px_->w32(PX_OSIZE, ((vh_ * oh_ / d_.h) << 16) | (vw_ * ow_ / d_.w));
    }
    px_->w32(PX_POS, ((uint32_t)(py & 0xFFFF) << 16) | (uint32_t)(px & 0xFFFF));
    px_->w32(PX_GALPHA, 255);
    if (on) {
        px_->w32(PX_CTRL, ctrl(true));
    }
    on_ = on;
}

void Video::set_view(uint32_t w, uint32_t h)
{
    if (own_picture()) {
        return;
    }
    vw_ = (w == 0 || w > d_.w) ? d_.w : w;
    vh_ = (h == 0 || h > d_.h) ? d_.h : h;
    if (on_) {
        write_set(true);                           // the geometry changes with the layer off, then it comes back
    }
}

void Video::set_rows(uint32_t h)
{
    if (own_picture() || pic_ || h == 0 || h > prim_h_max_ || h == d_.h) {
        return;
    }
    d_.h = h;
    vh_ = h;
    if (mode_ >= 0) {
        set_mode(mode_, sys_);                     // the layout for the new height
    }
    if (on_) {
        write_set(true);
    }
}

int Video::wanted_hdmi() const
{
    if (own_picture()) {                           // own picture: no wish
        return -1;
    }
    const VideoDesc &p = pic_ ? prim_ : d_;        // the descriptor's own picture, also while the alternative is on
    return video_mode_fits(p.w * pref_scale_, p.h * pref_scale_, false);
}

// The descriptor's x0/y0 are for 1920 x 1080. What they hold beyond the centre is the core's own
// offset (the Atari's 14 hires pixels); keep it in source pixels so it can follow into any mode.
void Video::init_bias()
{
    int fw = 0, fh = 0;
    video_mode_size(VID_HDMI_1080P, &fw, &fh);
    int s = d_.scale ? d_.scale : 1;
    bias_x_ = (d_.x0 - (fw - (int)d_.w * s) / 2) / s;
    bias_y_ = (d_.y0 - (fh - (int)d_.h * s) / 2) / s;
    ow_ = d_.w * (uint32_t)s;                      // gateware: until set_mode says otherwise, the descriptor's own size
    oh_ = d_.h * (uint32_t)s;
}

void Video::set_smoothing(bool on)
{
    smooth_ = on;
    if (gw_ && !own_picture() && on_) {
        write_set(true);                           // b6 of PX_CTRL: the set is written again with the layer off in between
    }
}

void Video::set_vic_scale(int m)
{
    vic_mode_ = (m >= 0 && m < VID_SCALE_COUNT) ? m : -1;
}

void Video::set_scale_mode(int m)
{
    if (!gw_ || m < 0 || m >= VID_SCALE_COUNT || m == scale_mode_) {
        return;
    }
    scale_mode_ = m;
    if (mode_ >= 0) {
        set_mode(mode_, sys_);
    }
}

// The menu canvas (trxcanvas.h, T164 D10a) is shown at a whole-number factor only, never fractional and never stretched (Alex, 2026-10-30):
// 960 x 540 at max(1, active height / 540) in a mode 960 or more wide, the mode's own size at 1 below that (the compact layout). Centred in the
// mode's active area, the rest black. The canvas is told to the port first (the display is laid out for it), then its place in the output.
// The canvas the menu has in a mode, and its place in the mode's active area (what video_place_menu tells the port).
// T222: the row the gateware with the VIC scaler really shows (video_gw_row_for): 1024 x 768 and 1280 x 1024 as 800 x 600, 800 x 600 at 50 Hz as 576p; PAL-60 runs the 60 Hz row
static void vic_row(int *hdmi, int *sys)
{
    if (*hdmi > VID_HDMI_800x600) {
        *hdmi = VID_HDMI_800x600;
    }
    if (*hdmi == VID_HDMI_800x600 && *sys != VID_SYS_NTSC60) {
        *hdmi = VID_HDMI_480_576;
    }
    if (*sys == VID_SYS_PAL60) {
        *sys = VID_SYS_NTSC60;
    }
}

static TrxCanvas menu_canvas_of(int hdmi, int sys)
{
    if (hdmi >= 0 && video_gateware_vic_scale()) {
        vic_row(&hdmi, &sys);
    }
    int fw = 0, fh = 0;
    video_active_size(hdmi, sys, &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        video_active_size(VID_HDMI_1080P, sys, &fw, &fh);
    }
    return trx_canvas_for(fw, fh);
}

void video_place_menu(PxPort *px, int hdmi, int sys)
{
    const TrxCanvas c = menu_canvas_of(hdmi, sys);
    px->menu_canvas(c.w, c.h);
    px->menu_place(c.x0, c.y0, c.w * c.factor, c.h * c.factor);
}

void video_menu_start(bool gw, int gw_row, int *sys, int *hdmi)
{
    if (gw && gw_row >= 0) {
        *sys = video_row_sys(gw_row);
        *hdmi = video_row_hdmi(gw_row);
    } else if (!gw) {
        *hdmi = VID_HDMI_1080P;
    }
}

void Video::place_menu(int hdmi, int sys)
{
    video_place_menu(px_, hdmi, sys);
}

// T194: the alternative's height follows the System Mode's family (the VIC frame is 65 x 263 in NTSC and PAL-60: 240 lines of the window, else 270)
void Video::alt_height(int sys)
{
    if (msys_ >= 0) {
        sys = msys_;                               // the capture's rows are the VIC's, not the HDMI row's: a 50 Hz machine on a 60 Hz row still has 270
    }
    if (pic_ && alt_ && alt_->h60) {
        d_.h = (sys == VID_SYS_NTSC60 || sys == VID_SYS_PAL60) ? alt_->h60 : alt_->h;
        vw_ = d_.w;
        vh_ = d_.h;
    }
}

void Video::set_machine_sys(int sys)
{
    if (sys == msys_) {
        return;
    }
    msys_ = sys;
    if (pic_ && mode_ >= 0 && !own_picture()) {
        set_mode(mode_, sys_);                     // the height of the capture changed with the family
    }
}

void Video::select_picture(int n)
{
    n = n ? 1 : 0;
    if (!alt_ || n == pic_ || own_picture()) {
        return;
    }
    if (pic_ == 0) {
        prim_ = d_;                                // the descriptor's own picture with the layout it has now
        prim_bx_ = bias_x_;
        prim_by_ = bias_y_;
    }
    pic_ = n;
    if (n) {
        d_ = *alt_;
        bias_x_ = bias_y_ = 0;                     // the alternative's x0 / y0 are the centre of 1920 x 1080
        ow_ = d_.w * (uint32_t)(d_.scale ? d_.scale : 1);
        oh_ = d_.h * (uint32_t)(d_.scale ? d_.scale : 1);
        alt_height(sys_);
    } else {
        d_ = prim_;
        bias_x_ = prim_bx_;
        bias_y_ = prim_by_;
        ow_ = d_.w * (uint32_t)(d_.scale ? d_.scale : 1);
        oh_ = d_.h * (uint32_t)(d_.scale ? d_.scale : 1);
        vw_ = d_.w;
        vh_ = d_.h;
    }
    if (mode_ >= 0) {
        set_mode(mode_, sys_);
    }
    if (on_) {
        write_set(true);
    }
}

int Video::set_mode(int hdmi, int sys)
{
    if (own_picture()) {                           // own picture: the mode is kept, there is no layout to move
        mode_ = hdmi;
        if (gw_) {                                 // T222, T228: the gateware's frame is the mode's (c64scpu's VIC scaler, trx64u's U16): the menu canvas follows it.
            sys_ = sys;                            // Without the gateware the frame is fixed and the menu stays where video_menu_start put it
            place_menu(hdmi, sys);
        }
        return mode_;
    }
    if (gw_) {
        int ow = 0, oh = 0, x0 = 0, y0 = 0;
        alt_height(sys);
        video_layout_scaled((int)d_.w, (int)d_.h, hdmi, sys, scale_mode_, bias_x_, bias_y_, &ow, &oh, &x0, &y0);
        bool moved = (uint32_t)ow != ow_ || (uint32_t)oh != oh_ || x0 != d_.x0 || y0 != d_.y0;
        ow_ = (uint32_t)ow;
        oh_ = (uint32_t)oh;
        d_.x0 = x0;
        d_.y0 = y0;
        mode_ = hdmi;
        sys_ = sys;
        place_menu(hdmi, sys);                     // also while the menu is up: its layer is not the picture's
        if (moved && on_) {
            write_set(true);
        }
        return mode_;
    }
    int scale = d_.scale, x0 = d_.x0, y0 = d_.y0;
    video_layout((int)d_.w, (int)d_.h, hdmi, sys, bias_x_, bias_y_, &scale, &x0, &y0);
    bool moved = scale != (int)d_.scale || x0 != d_.x0 || y0 != d_.y0;
    d_.scale = (uint8_t)scale;
    d_.x0 = (int32_t)x0;
    d_.y0 = (int32_t)y0;
    mode_ = hdmi;
    if (moved && on_) {
        write_set(true);                           // the geometry changes with the layer off, then it comes back
    }
    return mode_;
}

void Video::init()
{
    if (own_picture()) {                           // no buffers, no FB_BASE: those register offsets would be the ID bytes
        on_ = false;
        ready_ = true;
        return;
    }
    // black buffers: the first frames show black, not what the DDR2 held before
    static uint8_t zero[4096];
    uint32_t bytes = d_.w * d_.h * d_.pixel_bytes();
    for (int b = 0; b < d_.nbuf; b++) {
        uint32_t off = d_.fb_base - 0x01000000u + (uint32_t)b * d_.fb_stride;
        if (off + bytes > DDR_WINDOW_BYTES) {
            continue;                              // the app's window ends at 0x02000000; the core still writes there (T93 5.7)
        }
        for (uint32_t o = 0; o < bytes; o += sizeof(zero)) {
            io_->ddr_write(off + o, zero, bytes - o < sizeof(zero) ? bytes - o : sizeof(zero));
        }
    }
    // A core whose buffers are gateware parameters (the Amiga: FB_BUFS, FB_BYTE0) has no FB_BASE / FB_STRIDE register. Register 0 would be the ID
    // block, so a descriptor says "none" with 0 and nothing is written (T104 5.3).
    for (int i = 0; i < 4 && d_.reg_fb_base; i++) {
        io_->wr(d_.reg_fb_base + i, (uint8_t)(d_.fb_base >> (8 * i)));
    }
    for (int i = 0; i < 4 && d_.reg_fb_stride; i++) {
        io_->wr(d_.reg_fb_stride + i, (uint8_t)(d_.fb_stride >> (8 * i)));
    }
    front_ = d_.fb_base;
    last_seq_ = io_->rd(d_.reg_seq);
    on_ = false;                                   // the layer is the caller's to give: show()
    ready_ = true;
}

void Video::hide()
{
    px_->w32(PX_CTRL, ctrl(false));
    on_ = false;
}

void Video::show()
{
    write_set(!hidden_);
}

bool Video::poll()
{
    if (own_picture()) {                           // own picture: nothing to flip
        return false;
    }
    if (pic_) {                                    // the alternative has no buffers to flip
        return false;
    }
    uint8_t seq = io_->rd(d_.reg_seq);
    if (seq == last_seq_) {
        return false;
    }
    last_seq_ = seq;
    uint8_t l = io_->rd(d_.reg_latest);
    if (!(l & 0x80)) {
        return false;
    }
    // The buffer the core names has to be one of the descriptor's nbuf: on the three-buffer Amiga a reported buffer 3 would address the
    // byte past the window (T104 5.3). The core never names one, so a value like that is a fault of the read, not a frame: the picture stays.
    if ((uint32_t)(l & 3) >= d_.nbuf) {
        return false;
    }
    front_ = d_.fb_base + (uint32_t)(l & 3) * d_.fb_stride;
    if (on_) {
        px_->w32(PX_BACK, front_);
    }
    return true;
}


// ---- T256: the picture of the machine that stopped -------------------------------------------------------------------------------------------------------------------------
// The host's still capture (host/pix_layer.v): the VIC's pixels of the window the scaler shows (400 x 270, 400 x 240 in the 60 Hz modes), two to a byte, the first in the low nibble, into
// slot 0 or 1 at STILL_BASE (+ 64 KB). STILL_STATUS: b0 a frame was captured, b1 the slot of the last complete frame, b2 that frame is 60 Hz, b31:8 the frame count.
#define STILL_CTRL    0x6060
#define STILL_BASE    0x6064
#define STILL_STATUS  0x6068
#define STILL_SLOT    0x10000
#define STILL_W       400

void Video::still_start()
{
    if (!own_picture() || still_slots_ || !px_) {
        return;
    }
    still_raw_ = (uint8_t *)malloc(2 * STILL_SLOT + 16);
    if (!still_raw_) {
        return;
    }
    still_slots_ = (uint8_t *)(((uintptr_t)still_raw_ + 15) & ~(uintptr_t)15);
    px_->w32(STILL_BASE, (uint32_t)(uintptr_t)still_slots_);
    px_->w32(STILL_CTRL, 1);
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// The whole shown picture as write_set puts it on the output (the position and the output size it writes), in output pixels of the active area; the menu's canvas lies in the same area
// (menu_canvas_of), so the rectangle on the canvas is the output rectangle minus the canvas's corner, over the whole factor the canvas is shown with.
bool Video::picture_rect(int *x, int *y, int *w, int *h) const
{
    int ox, oy, ow, oh;
    int canvas_mode = mode_ >= 0 && gw_ ? mode_ : VID_HDMI_1080P;
    if (own_picture()) {
        // A C64-type core on the gateware with the VIC scaler: the window of 400 x 270 VIC pixels (400 x 240 in the 60 Hz family) in the mode's active area, laid out by the Scale the person set
        // (set_vic_scale, the register at 0x4F01) as video_layout_scaled lays out any picture; the row is the one the gateware runs.
        if (vic_mode_ < 0 || !gw_ || mode_ < 0) {
            return false;
        }
        int hdmi = mode_, sys = sys_;
        vic_row(&hdmi, &sys);
        const int fam = msys_ >= 0 ? msys_ : sys_;
        const int rows = (fam == VID_SYS_NTSC60 || fam == VID_SYS_PAL60) ? 240 : 270;
        video_layout_scaled(STILL_W, rows, hdmi, sys, vic_mode_, 0, 0, &ow, &oh, &ox, &oy);
        canvas_mode = mode_;
    } else if (!ready_ || !vw_ || !vh_ || !d_.w || !d_.h) {
        return false;
    } else if (gw_) {
        ox = d_.x0 + (int32_t)((d_.w - vw_) / 2 * ow_ / d_.w);
        oy = d_.y0 + (int32_t)((d_.h - vh_) / 2 * oh_ / d_.h);
        ow = (int)(vw_ * ow_ / d_.w);
        oh = (int)(vh_ * oh_ / d_.h);
    } else {
        const int s = d_.scale ? d_.scale : 1;
        ox = d_.x0 + (int32_t)((d_.w - vw_) / 2) * s;
        oy = d_.y0 + (int32_t)((d_.h - vh_) / 2) * s;
        ow = (int)vw_ * s;
        oh = (int)vh_ * s;
    }
    const TrxCanvas c = menu_canvas_of(canvas_mode, sys_);     // without the gateware the frame is 1080p and the menu stays where video_menu_start put it
    const int f = c.factor < 1 ? 1 : c.factor;
    *x = floor_div(ox - c.x0, f);
    *y = floor_div(oy - c.y0, f);
    *w = (ow + f / 2) / f;
    *h = (oh + f / 2) / f;
    return *w > 0 && *h > 0;
}

static const uint16_t g_colodore[16] = {         // the C64 palette of the capture as RGB565 when no source says the person's (Colodore: 000000 FFFFFF 813338 75CEC8 8E3C97 56AC4D 2E2C9B EDF171 8E5029 553800 C46C71 4A4A4A 7B7B7B A9FF9F 706DEB B2B2B2)
    0x0000, 0xFFFF, 0x8187, 0x7679, 0x89F2, 0x5569, 0x2973, 0xEF8E,
    0x8A85, 0x51C0, 0xC36E, 0x4A49, 0x7BCF, 0xAFF3, 0x737D, 0xB596
};

static Video::StillPalette g_still_palette = nullptr;

void Video::set_still_palette(StillPalette f)
{
    g_still_palette = f;
}

// The colours of the capture's 4 bit values on the screen: a library's source, else the person's palette (the one the scaler shows), else Colodore.
static void still_colours(uint16_t out[16], Video::StillPalette lib)
{
    uint8_t rgb[16][3];
    if ((lib && lib(rgb)) || (g_still_palette && g_still_palette(rgb))) {
        for (int i = 0; i < 16; i++) {
            out[i] = (uint16_t)(((rgb[i][0] & 0xF8) << 8) | ((rgb[i][1] & 0xFC) << 3) | (rgb[i][2] >> 3));
        }
        return;
    }
    memcpy(out, g_colodore, sizeof(g_colodore));
}

// The column of the source for each output column, made once per grab: the division is not made per pixel. The widest canvas is 960 (trx_canvas_for; OVL_CANVAS_W), a grab is never wider.
#define GRAB_MAX_W 960
static uint16_t g_grab_col[GRAB_MAX_W];

static void grab_columns(int w, int ox, int fw, int span)
{
    for (int x = 0; x < w; x++) {
        g_grab_col[x] = (uint16_t)((uint32_t)(ox + x) * (uint32_t)span / (uint32_t)fw);
    }
}

bool Video::grab(uint16_t *dst, int w, int h, int fw, int fh, int ox, int oy, int stride, StillPalette pal)
{
    if (!dst || w < 1 || h < 1 || w > GRAB_MAX_W) {
        return false;
    }
    if (stride < w) {
        stride = w;
    }
    if (fw < 1 || fh < 1) {
        fw = w;
        fh = h;
        ox = oy = 0;
    }
    if (own_picture()) {
        if (!still_slots_ || !px_) {
            return false;
        }
        static uint8_t cap[STILL_W * 270 / 2];
        uint16_t col[16];
        still_colours(col, pal);
        for (int tries = 0; tries < 3; tries++) {
            const uint32_t st = px_->r32(STILL_STATUS);
            if (!(st & 1)) {
                return false;
            }
            const int rows = (st & 4) ? 240 : 270;
            memcpy(cap, still_slots_ + ((st & 2) ? STILL_SLOT : 0), (size_t)(STILL_W * rows / 2));
            if ((px_->r32(STILL_STATUS) >> 8) != (st >> 8)) {
                continue;                                  // the capture moved on while the slot was read: again
            }
            grab_columns(w, ox, fw, STILL_W);
            for (int y = 0; y < h; y++) {
                uint16_t *row = dst + (size_t)y * stride;
                if (y > 0 && (oy + y) * rows / fh == (oy + y - 1) * rows / fh) {
                    memcpy(row, row - stride, (size_t)w * 2);   // the same VIC line: a copy of the line above
                    continue;
                }
                const uint8_t *src = cap + (size_t)((oy + y) * rows / fh) * (STILL_W / 2);
                for (int x = 0; x < w; x++) {
                    const int vx = g_grab_col[x];
                    const uint8_t b = src[vx >> 1];
                    row[x] = col[(vx & 1) ? (b >> 4) : (b & 15)];
                }
            }
            return true;
        }
        return false;
    }
    const uint32_t pb = d_.pixel_bytes();
    if (!ready_ || !front_ || !vw_ || !vh_ || d_.pxfmt == 3 || !io_ || vw_ * pb > 8192) {
        return false;
    }
    static uint8_t line[8192];
    uint32_t last = 0xFFFFFFFFu;
    grab_columns(w, ox, fw, (int)vw_);
    for (int y = 0; y < h; y++) {
        const uint32_t sy = (uint32_t)(oy + y) * vh_ / (uint32_t)fh;
        uint16_t *row = dst + (size_t)y * stride;
        if (sy == last) {
            memcpy(row, row - stride, (size_t)w * 2);       // the same source line as the row above (y > 0 here: last starts at an impossible value): the same pixels
            continue;
        }
        io_->ddr_abs_read(front_ + sy * d_.w * pb, line, vw_ * pb);
        last = sy;
        if (pb == 2) {
            for (int x = 0; x < w; x++) {
                const uint32_t sx = g_grab_col[x];
                row[x] = (uint16_t)(line[2 * sx] | (line[2 * sx + 1] << 8));
            }
        } else {
            for (int x = 0; x < w; x++) {
                const uint32_t sx = g_grab_col[x];
                const uint32_t v = (uint32_t)line[4 * sx] | ((uint32_t)line[4 * sx + 1] << 8) | ((uint32_t)line[4 * sx + 2] << 16);   // {x, R, G, B} little endian: B, G, R
                row[x] = (uint16_t)(((v & 0xF80000) >> 8) | ((v & 0x00FC00) >> 5) | ((v & 0x0000F8) >> 3));
            }
        }
    }
    return true;
}
