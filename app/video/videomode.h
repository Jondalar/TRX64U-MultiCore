// Which picture the two applications put out, and how it is chosen (T105).
//
// Both applications used to force PAL 50 Hz on 1920 x 1080 and offered no way out. 1080p50 is a CEA
// mode televisions usually take and PC monitors over HDMI often do not list at all, and a board whose
// screen stays black cannot be told anything: our file browser shows only `.cor`, so the updater
// cannot even be started to install a cure. A tester outside this house lost an evening to it on
// 2026-10-16.
//
// So: the mode is read from the screen when nobody has chosen one (EDID over the HDMI DDC lines),
// a choice is kept in Flash:/boot.cfg and handed to the core application, and one key cycles the
// resolution blind for the case where the picture is already gone.
#ifndef HOST_VIDEOMODE_H
#define HOST_VIDEOMODE_H

#include <stdint.h>

// t_video_mode / t_hdmi_mode without dragging u64.h into the tests: the numbers are its enums.
#define VID_SYS_PAL50    0
#define VID_SYS_NTSC60   1
#define VID_SYS_PAL60    2      // t_video_mode 2 and 3 (T140 D24): the C128 offers all four System Modes
#define VID_SYS_NTSC50   3
#define VID_HDMI_480_576 0
#define VID_HDMI_720P    1
#define VID_HDMI_1080P   2
#define VID_HDMI_800x600 3
#define VID_HDMI_1024x768 4
#define VID_HDMI_1280x1024 5
#define VID_HDMI_COUNT   6

// What the host application has the picture set to, packed as video_pack(); the hand-over carries it
// to the core application so a core comes up in the mode the user can see (T105).
extern uint32_t video_current;

const char *video_hdmi_name(int hdmi);
const char *video_sys_name(int sys);

// What the screen says it can do. `edid` is what came off the DDC lines, `len` its length (128 or
// more); `pal` picks between the 50 Hz and the 60 Hz families. Returns a VID_HDMI_* value. Lives in edid.cc (T164 G11), which parses
// the EDID into the mask of table rows (edid.h); this is the single-family answer, the list and the best row are in edid.h.
//
//   rows named in the machine's family -> the best: 1080p, else 720p, else 576p/480p, else a PC row (1024x768 first)
//   EDID but no extension, no row named -> a DVI/PC monitor: 1024x768, which every one of them has
//   no EDID at all, a corrupt one, or a CEA block naming none of our rows -> 576p/480p, the mode a television is least likely to refuse
int video_mode_from_edid(const uint8_t *edid, int len, bool pal);

// 32 bits for the hand-over's `video` field, and back. 0 means nobody chose: the core application
// then asks the screen itself.
uint32_t video_pack(int sys, int hdmi);
bool video_unpack(uint32_t v, int *sys, int *hdmi);

// The width and height a mode puts out, 0 for one this board does not drive.
void video_mode_size(int hdmi, int *w, int *h);

// Which mode shows a picture of `w` x `h` largest (ADR-0057). The pixel layer scales by whole numbers
// only and at most 3 (`host/pix_layer.v`: "b5:4 scale S (0 and 1: 1; 2; 3)"), so the picture is as big
// as it will ever be and the question is which output frame wastes least around it. The mode whose
// height the picture fills best wins; a tie goes to the one with more pixels, because the same share of
// a bigger frame is a sharper picture. A picture that fits nowhere keeps 1080p.
//
// Measured 2026-10-16: the NES draws 768 x 672 and goes from 62 % of a 1080p frame's height to 93 % at
// 720p; the Atari draws 1152 x 864 and stays at 1080p, where it already fills 80 % - at 720p it would
// have to drop a scale step for the same share and fewer pixels.
int video_mode_fits(int w, int h, bool allow_pc_modes);

// The frame a picture is placed in: always 1920 x 1080, whatever hdmi and sys say. The host's output is fixed
// (host/vic_scaler.v, host/overlay.v:8) and the pixel layer's position is in pixels of that frame (T107 5.6).
void video_frame_size(int hdmi, int sys, int *w, int *h);

// Where a picture of `w` x `h` source pixels sits in the host's frame (1920 x 1080 whatever `hdmi` is, see video_frame_size), and how far it is scaled.
// The pixel layer scales by whole numbers and at most 3 (`host/pix_layer.v`: "b5:4 scale S (0 and 1:
// 1; 2; 3)"), so this takes the largest step whose result still fits the frame and centres it.
// `bias_x`/`bias_y` are the core's own offset from the centre in SOURCE pixels (the Atari shows its
// 384 columns 14 pixels right of centre); they scale with the picture and never push it out of the
// frame. A picture too large even at scale 1 keeps scale 1 and sits at 0, 0 rather than off the frame.
// Pure. The layout is the same in every mode (T107 5.6).
void video_layout(int w, int h, int hdmi, int sys, int bias_x, int bias_y, int *scale, int *x0, int *y0);

// T107 D7/D8 (cores whose gateware owns the picture: VideoDesc::hdmi_modes, the A1200's large core): the output really has the mode's size,
// and the layer scales to any output size (pix_layer.v PX_OSIZE). Scale: Integer = the largest whole multiple that fits (no cap; 1 when
// even that does not fit), Fit = the largest size that keeps the aspect, Fill = the whole frame.
#define VID_SCALE_INTEGER 0
#define VID_SCALE_FIT     1
#define VID_SCALE_FILL    2
#define VID_SCALE_COUNT   3
const char *video_scale_name(int scale);
// The active area the gateware drives in this mode and system (host/hdmi_modes.vh): 576p is 720 x 576 under PAL, 480p is 640 x 480.
void video_active_size(int hdmi, int sys, int *w, int *h);
// The mode number of the gateware's request register: family (0 = 50 Hz, 1 = 60 Hz) * 6 + hdmi.
int video_mode_index(int hdmi, int sys);
// Where a picture of w x h source pixels goes in that frame: output size ow x oh and position, `bias` as for video_layout (source pixels, they
// scale with the picture). Pure.
void video_layout_scaled(int w, int h, int hdmi, int sys, int scale_mode, int bias_x, int bias_y, int *ow, int *oh, int *x0, int *y0);
// T164 WP16 (G2, G14): the gateware's request (0x4F00) and status (0x4F04..) registers in the overlay window, which every core has through the host.
// The status tells the gateware from an old host (magic), and when a requested mode is up. The access goes through GwIo so a test can play the
// register file; the board's wrapper below supplies the real one. Offsets are bytes of the overlay window.
#define GW_REQ        0x4F00
#define GW_MAGIC_AT   0x4F04
#define GW_VERSION_AT 0x4F05
#define GW_FLAGS_AT   0x4F06
#define GW_MODE_AT    0x4F07
#define GW_SEEN_AT    0x4F08               // requests written
#define GW_DONE_AT    0x4F09               // requests processed; equal to GW_SEEN_AT = the refused bit is current
#define GW_SCALE      0x4F01               // T222: write 0 Integer / 1 Fit / 2 Fill, only where the gateware has the VIC scaler with a scale (version 2, C64-type flag)
#define GW_SMOOTH     0x4F0B               // T227: write 0 Off / 1 On (smoothing of the VIC picture, horizontal blend); read: the smoothing in use. Version 3
#define GW_SCALE_AT   0x4F0A               // T222: the scale in use
#define GW_MAGIC      0x48
#define GW_F_GT_READY   0x01
#define GW_F_PIX_LOCKED 0x02
#define GW_F_RUNNING    0x04
#define GW_F_ROW_LOCKED 0x08
#define GW_F_C64_TYPE   0x10
#define GW_F_REFUSED    0x80
struct GwStatus {
    bool    present;                       // the magic answered
    uint8_t version, flags, mode, seen, done;
};
struct GwIo {
    void    (*wr)(uint32_t off, uint8_t v);
    uint8_t (*rd)(uint32_t off);
    void    (*wait_ms)(int ms);
};
bool video_gw_probe_io(const GwIo *io, GwStatus *st);
// A mode is up when the transmitter runs on a locked pixel clock in exactly the requested row, and the request was not refused.
bool video_gw_up(const GwStatus &st, int row);
// T222: a gateware whose picture is the VIC through the scaler with the scale register (a C64-type core's mode_req, version >= 2). Its table has seven rows
// (576p, 720p, 1080p at 50 Hz; 480p, 720p, 1080p, 800 x 600 at 60 Hz) and takes only the System Mode's family; the host's generic mode_req (version 1) has neither.
bool video_gw_has_scale(const GwStatus &st);
// T227: the same gateware with the smoothing register 0x4F0B (version >= 3).
bool video_gw_has_smooth(const GwStatus &st);
// Writes the smoothing (0 Off, 1 On) and polls 0x4F0B until it reads back (taken at the next frame start) or `timeout_ms` has passed. False: not in time, or not 0 / 1.
bool video_gw_set_smooth_io(const GwIo *io, int on, int timeout_ms);
// The row of the request for (sys, hdmi) in such a gateware: 1024 x 768 and 1280 x 1024 are taken as 800 x 600 at 60 Hz, every PC row at 50 Hz as 576p (U16 D5).
// In any other gateware the plain mode index.
int video_gw_row_for(const GwStatus &st, int sys, int hdmi);
// Writes the scale and polls 0x4F0A until it reads back (the gateware takes it at its next frame start) or `timeout_ms` has passed. False: not in time, or the
// value is not a scale.
bool video_gw_set_scale_io(const GwIo *io, int scale, int timeout_ms);
#define GW_SCALE_TIMEOUT_MS 200
// Writes the request and polls the status every 10 ms until the row is up or `timeout_ms` has passed. False: refused, or not up in time.
bool video_gw_apply_io(const GwIo *io, int sys, int hdmi, int timeout_ms);
#define GW_APPLY_TIMEOUT_MS 1000
// Board side: is it the gateware that takes the mode (the status answers its magic)? And the row it runs, -1 when it does not answer.
bool video_gateware_present(void);
int  video_gateware_row(void);
// Board side: put the mode into the gateware's request register and wait for the status (pix_locked, running, the mode) with a 1 s timeout.
// True when the row came up; the screen is enabled either way.
bool video_apply_gateware(int sys, int hdmi);
// T222: does the gateware under this core scale the VIC's picture with a scale register (video_gw_has_scale)? And set that scale (VID_SCALE_*). Board side.
bool video_gateware_vic_scale(void);
bool video_set_scale_gateware(int scale);
// T227: does that gateware also smooth (video_gw_has_smooth)? And set the smoothing (0 / 1). Board side.
bool video_gateware_vic_smooth(void);
bool video_set_smooth_gateware(int on);
// Board side (D11b): a signature of the screen that is attached. False: nothing is attached (hot-plug low); then *sig is not touched. The
// signature is made of the base block's checksum and extension count, read without the rest of the EDID.
bool video_edid_sig(uint32_t *sig);

// Board side: read the screen (false when no screen answers), set the mode, and the whole start
// sequence - a stored choice, else the screen's answer. Always declared; VIDEOMODE_NO_BOARD leaves the
// three out of videomode.cc, for a test or a bench that has no firmware under it and brings its own.
bool video_read_edid(uint8_t *buf, int max, int *len);
void video_apply(int sys, int hdmi);
void video_start(int stored_sys, int stored_hdmi, int *used_sys, int *used_hdmi);

#endif
