// The video service (T93 D9): puts a core's picture on the host's pixel layer and flips to the newest complete frame.
// The layer's registers are write-only (T84 notes, T93 R2), so this class keeps the set it wrote and writes the whole set
// again when the layer comes back from the menu; it never reads a layer register. Pure: PxPort is the board's
// trx_io_write32 or a test's recorder.
#ifndef COREAPP_VIDEO_H
#define COREAPP_VIDEO_H

#include "desc.h"

// The layer's registers, offsets into page 6 of the core window (pix_layer.v)
#define PX_CTRL    0x6080
#define PX_OSIZE   0x60A8                 // T107 D8: {height 16, width 16} output pixels (the A1200 build's layer)
#define PX_FRONT   0x6084
#define PX_BACK    0x6088
#define PX_SIZE    0x6090
#define PX_STRIDE  0x6094
#define PX_POS     0x6098
#define PX_GALPHA  0x609C
#define PX_KEY     0x60A0

class PxPort {
public:
    virtual ~PxPort() { }
    virtual void w32(uint32_t reg, uint32_t v) = 0;     // 32 bit little endian, byte 3 commits
    // The two registers of the layer that read (PX_STATUS 0x60A4 and STILL_STATUS 0x6068); every other one is write only. 0 for a port that cannot read (a test's).
    virtual uint32_t r32(uint32_t reg) { (void)reg; return 0; }
    // T107 5.9: where the menu's own canvas goes in the output (position and output size in output pixels). The menu and the core's picture take
    // turns on the one layer, and the layer's OSIZE stays what the picture last wrote, so the menu needs its own; the board hands it to the
    // LVGL port, which writes it with every first picture and moves a menu that is up. A test records it.
    virtual void menu_place(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
    // T164 D10a: the size of the menu's canvas (960 x 540, or the mode's own size below 960 wide), told before menu_place(). The board hands it to the
    // LVGL port and to the view, which lay the menu out again when it changed.
    virtual void menu_canvas(int w, int h) { (void)w; (void)h; }
};


// T210 P1: the menu canvas and its place for a mode; also called before the splash opens (the mode video_menu_start names).
void video_place_menu(PxPort *px, int hdmi, int sys);

// T228: the mode whose frame the layer drives when the application starts, from what the gateware's status says. *sys / *hdmi come in as the hand-over's
// mode. With the gateware (gw: 0x4F04 answers) they become the row it runs (gw_row, 0x4F07): a bitstream of its own (trx64u's, a large core's) starts in
// its power-up row, whatever the host handed over; the hand-over's mode stays only where the gateware names no row (gw_row < 0). Without the gateware
// the frame is 1920 x 1080 in every mode (video_frame_size): 1080p.
void video_menu_start(bool gw, int gw_row, int *sys, int *hdmi);

class Video {
public:
    Video(const VideoDesc &d, PxPort *px, CoreIo *io) : d_(d), prim_h_max_(d.h), px_(px), io_(io), mode_(-1), pref_scale_(d.scale), bias_x_(0), bias_y_(0), last_seq_(0), vw_(d.w), vh_(d.h), front_(0), on_(false), ready_(false), hidden_(false) { init_bias(); }

    // Clears the frame buffers (black) and tells the core where they are. The layer itself is not touched: show() gives it
    // to the core's picture, once the menu or the start message that may still hold it has let go.
    void init();
    // The core's picture goes away / comes back, with exactly the registers init() wrote and the buffer last shown.
    void hide();
    // T97 5.10: the layer shows only the top left w x h of the picture, centred where the whole picture would be (the core fills the
    // buffers with a smaller window than the descriptor's: everything outside it is not on the screen). 0 x 0 or the descriptor's size: all of it.
    void set_view(uint32_t w, uint32_t h);
    // T113 D11: the core's own picture changes its height while it runs (the C128's VDC: 400 rows, 480 woven in interlace; the descriptor holds the largest). The layer shows
    // h rows and the layout (Picture, HDMI mode) is made for that height; no effect on the alternative picture, h = 0 or the same height: nothing.
    void set_rows(uint32_t h);
    uint32_t view_w() const { return vw_; }
    uint32_t view_h() const { return vh_; }
    void show();
    // Flips to the newest complete frame when there is one. Cheap: one register read when nothing is new.
    // Returns true when it flipped.
    bool poll();

    // ADR-0057: which HDMI mode shows this core's picture largest. The picture is already at the pixel
    // layer's biggest whole-number scale, so the only lever left is the size of the frame around it -
    // the NES fills 62 % of 1080p's height and 93 % of 720p's, the Atari is better off at 1080p. Pure,
    // so the bench reads it; the board applies it at start-up.
    int wanted_hdmi() const;                       // -1: no wish (own_picture())

    // T107: the mode the picture is in. `hdmi` is a VID_HDMI_* value, `sys` the 50/60 Hz family it runs
    // in; the scale and the position follow (video_layout). The host's frame is 1920 x 1080 in every mode, so
    // they come out the same for all of them (T107 5.6). A layer whose geometry changed is rewritten at once. wanted_hdmi() keeps answering from the descriptor's own scale, not from the last mode's.
    // With the gateware the menu's canvas and place follow the mode, for a core with its own picture too (T228: trx64u's frame is the gateware's row).
    int set_mode(int hdmi, int sys);
    int mode() const { return mode_; }
    // T164 G14: the gateware (the host's hdmi_out, or a bitstream's own mode request: U16's on trx64u; probed at 0x4F04) takes the mode and scales to any
    // output size, for every core whose picture is a frame buffer. Without it (an old host) the frame is 1920 x 1080 and the scale at most 3. Set before the first set_mode.
    void set_gateware(bool on) { gw_ = on; }
    bool gateware() const { return gw_; }
    // T107 D8: Integer / Fit / Fill (VID_SCALE_*), with the gateware; the layout is redone at once. Without it ignored.
    void set_scale_mode(int m);
    int scale_mode() const { return scale_mode_; }
    void set_vic_scale(int m);   // a C64-type core: the Scale (VID_SCALE_*) the gateware's VIC scaler runs at, for picture_rect; -1: not told
    // T164 Q3: bilinear smoothing of Fit and Fill, with the gateware
    void set_smoothing(bool on);
    bool smoothing() const { return smooth_; }
    uint32_t out_w() const { return ow_; }
    uint32_t out_h() const { return oh_; }
    uint8_t scale() const { return d_.scale; }
    int32_t x0() const { return d_.x0; }
    int32_t y0() const { return d_.y0; }

    // T104.2 D5: w == 0 says the core draws its own picture (the C64's goes through Gideon's video path, the pixel layer is only the
    // menu's). Then init() writes no core register and no frame buffer, show() only switches the layer off, poll() never flips and
    // wanted_hdmi() has no wish (-1): the screen keeps the mode it is in.
    bool own_picture() const { return d_.w == 0; }

    bool ready() const { return ready_; }          // init() has run
    // T256 D10: the picture of the machine that stopped, for the still of the overlay service. The picture is fitted to fw x fh RGB565 pixels by nearest sampling (0 x 0: to w x h), and dst
    // takes the window of w x h pixels of it that starts at ox, oy. A C64-type core (own_picture) has the host's still capture: still_start() points it at two slots of the base's own heap
    // once, at the core's start; grab() reads the last complete frame (4 bit VIC colours, 400 x 270 or 400 x 240 in the 60 Hz modes) and paints it with the palette the person's setting
    // put on the screen (set_still_palette; Colodore when no source is set). Any other core: the newest frame buffer of its own picture (32 bit pixels are cut to RGB565). False: no picture.
    // The rows of dst are `stride` pixels apart (0: w). `pal` is a library's source of the colours, asked first (T256 D11); null: the base's own, then Colodore. Both grab paths keep static buffers: the
    // caller makes only one grab at a time (the overlay service's grab_lock).
    typedef bool (*StillPalette)(uint8_t rgb[16][3]);
    void still_start();
    bool grab(uint16_t *dst, int w, int h, int fw = 0, int fh = 0, int ox = 0, int oy = 0, int stride = 0, StillPalette pal = nullptr);
    // The 16 colours of the screen's palette (R, G, B) as the person set them; true when the source answered. One source for the application, set when a core's palette code starts.
    static void set_still_palette(StillPalette f);
    // T256 D10: where the running picture of a frame-buffer core stands on the menu's canvas (the canvas video_place_menu names for the mode), in canvas pixels, the rectangle of the whole shown
    // picture: it may reach beyond the canvas (a Fit picture on a mode whose canvas is smaller than its active area). A C64-type core (its picture is the host's scaler's) has one when the gateware has the VIC scaler and set_vic_scale told the Scale in force: the 400 x 270 (400 x 240 at 60 Hz) window laid out by that Scale in the mode's active area. False otherwise, and for any other core before init().
    bool picture_rect(int *x, int *y, int *w, int *h) const;
    // T113: a core with two pictures (the C128: VIC-IIe through the host's scaler, VDC on this layer) keeps the layer off while the other one is
    // shown. show() then writes the layer set with the layer off, so every caller that gives the layer back (menu close, Freeze) keeps it dark.
    void set_hidden(bool h) { hidden_ = h; }
    bool hidden() const { return hidden_; }
    // T194: a core with a second picture on the same layer (CoreDesc::alt_video; the C128's VIC-IIe next to the VDC). select_picture(1) puts the alternative
    // on the layer (n = 0: the descriptor's own picture again): its source size, the layer's format and stride, and, for the 60 Hz family of the System
    // Mode (sys 1 and 2), its h60 height; the layout is redone with the same Picture mode, HDMI mode and Smoothing, and a layer that is on is rewritten.
    void set_alt(const VideoDesc *a) { alt_ = a; }
    void select_picture(int n);
    int picture() const { return pic_; }
    void set_machine_sys(int sys);                 // the System Mode the core's VIC runs (the capture's family), apart from the HDMI row's
    uint32_t front() const { return front_; }
    bool visible() const { return on_; }
    // fmt: the layer's format, 1 RGB565 or 2 32 bit (T136)
    static uint32_t ctrl_value(uint8_t scale, bool on, uint8_t fmt = 1) { return ((uint32_t)scale << 4) | ((uint32_t)fmt << 1) | (on ? 1 : 0); }
    // PX_CTRL as the layer is written: the scale bits b5:4 only matter to a layer without PX_OSIZE (an old host; TRX64-Ultimate's, which carries no frame-buffer
    // picture: trx64u's is its own), the gateware's layer sizes by PX_OSIZE; b6 is bilinear smoothing (T164 Q3)
    uint32_t ctrl(bool on) const { return ctrl_value(d_.scale, on, fmt()) | (gw_ && smooth_ ? 0x40u : 0u); }
    uint8_t fmt() const { return d_.pxfmt ? d_.pxfmt : d_.pixel_bytes() == 4 ? 2 : 1; }

private:
    void write_set(bool on);
    void place_menu(int hdmi, int sys);
    VideoDesc d_;
    uint32_t prim_h_max_;                          // the descriptor's height: set_rows() goes up to it
    PxPort *px_;
    CoreIo *io_;
    void init_bias();
    int mode_;
    int vic_mode_ = -1;
    int scale_mode_ = 0, sys_ = 0;                 // VID_SCALE_*; the 50/60 Hz family of the last set_mode
    bool gw_ = false, smooth_ = false;
    uint32_t ow_ = 0, oh_ = 0;                     // gateware: the picture's output size
    uint8_t pref_scale_;                           // the descriptor's scale: what the core would like at best
    int bias_x_, bias_y_;                          // the descriptor's offset from the centre of 1920 x 1080, in source pixels
    uint8_t last_seq_;
    uint32_t vw_, vh_;                             // the part of the picture the layer shows (set_view)
    uint32_t front_;
    bool on_;
    bool ready_;
    bool hidden_;
    const VideoDesc *alt_ = nullptr;               // T194: the second picture
    VideoDesc prim_ = VideoDesc();                 // the descriptor's own picture while the alternative is selected, with its bias
    int pic_ = 0, prim_bx_ = 0, prim_by_ = 0;
    void alt_height(int sys);
    uint8_t *still_raw_ = nullptr, *still_slots_ = nullptr;   // T256: the capture's two slots of 64 KB, 16-byte aligned (raw: what malloc gave)
    int msys_ = -1;                                // the machine's System Mode (its VIC's 50 / 60 Hz family); -1 = not told, sys_ stands in
};

#endif
