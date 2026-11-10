#include "video.h"
#include "videomode.h"
#include "trxcanvas.h"
#include <string.h>

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
void Video::place_menu(int hdmi, int sys)
{
    int fw = 0, fh = 0;
    video_active_size(hdmi, sys, &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        video_active_size(VID_HDMI_1080P, sys, &fw, &fh);
    }
    TrxCanvas c = trx_canvas_for(fw, fh);
    px_->menu_canvas(c.w, c.h);
    px_->menu_place(c.x0, c.y0, c.w * c.factor, c.h * c.factor);
}

// T194: the alternative's height follows the System Mode's family (the VIC frame is 65 x 263 in NTSC and PAL-60: 240 lines of the window, else 270)
void Video::alt_height(int sys)
{
    if (pic_ && alt_ && alt_->h60) {
        d_.h = (sys == VID_SYS_NTSC60 || sys == VID_SYS_PAL60) ? alt_->h60 : alt_->h;
        vw_ = d_.w;
        vh_ = d_.h;
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
