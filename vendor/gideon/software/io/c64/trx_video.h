// The freeze menu on the TRX64 Ultimate bitstream's pixel layer (TRX64-Ultimate
// docs/specs/T12-freeze-overlay.md §4-§5, §12; ARCH-lean-state-access.md §7.5,
// §9.2, §15).
//
// With Interface Type = Freeze, the app's freeze pauses the whole machine there
// (GATE_CTRL b0) and its menu accesses are answered by the freeze session
// responder (the slim core, trx_io_slim(): bus cycles on the paused machine, no
// session). The VIC stands still, so the menu the app writes to the C64 text
// screen is drawn over a dimmed still of the last C64 frame on the HDMI pixel
// layer: by the bitstream's C64 text layer where it has one, else here, in
// software, into the pixel layer.
//
// On Gideon's bitstreams (no "TRX2" block), without HDMI, or when the layer's
// buffers cannot be had, nothing here touches the hardware and the freeze stays
// the CPU-only pause with the menu on the VIC, as before.
#ifndef TRX_VIDEO_H
#define TRX_VIDEO_H

#if (U64 == 2) && !defined(RECOVERYAPP)

// From C64::freeze(), right before the stop: shows the still on the layer
// (waits for it to be on screen), makes the stop a machine pause and starts the
// renderer.
void trx_video_freeze_enter(void);

// From C64::freeze(), last (the menu's screen is up): the text layer shows it
// (TX_CTRL b0; on the slim core with its VIEW first), and the session's writes from
// here on are listed for the commit (SH_CTRL PROTO off; not on the slim core).
void trx_video_menu_on(void);

// After trx_video_freeze_enter(): this freeze is on the slim core with the pixel layer
// up, so C64::freeze_slim() does it (the machine pause, no chip written).
bool trx_video_slim_active(void);
// The freeze menu is on the text layer and the person gets it back (an app returned, T21 §8.12): TX_VIEW,
// the VIC view the layer draws it with ($D018's character set select, the screen and charset area), is
// written as menu_on() wrote it. Logs when it had changed.
void trx_video_menu_view(void);

// The menu's background colour (C64::set_colors on a slim freeze): the text layer's $D021 view and
// the colour the still is dimmed towards, from the next freeze. No C64 register is written.
void trx_video_menu_background(int background);

// From the freeze's restore (restore_io(), restore_slim()), first: the hardware text layer
// off, before the program's screen goes back; Gideon's restore follows (PROTO on, where
// there is a session).
void trx_video_menu_off(void);

// From C64::unfreeze(), after the resume: waits for the running machine's
// picture, hides the layer and puts the stop back to the CPU-only pause.
void trx_video_freeze_leave(void);

// The 80 column monitor in Freeze mode (T21 §8.11): the overlay is under the pixel layer and the text
// layer, so both are taken away while the wide window is open. Begin returns false (nothing changed)
// unless a freeze with the hardware text layer and its menu is up; End puts TX_CTRL and PX_CTRL back
// as they were, if that freeze is still up (idempotent; a freeze that ended has set them itself).
bool trx_video_wide_begin(void);
void trx_video_wide_end(void);

// True on a bitstream with the freeze menu's pixel layer (CAPS).
bool trx_video_has_layer(void);

// The "Overlay Transparency" setting (0..3: 0, 25, 50, 75 % of the still
// under the menu) and the menu's background colour; used from the next freeze.
void trx_video_set_look(int transparency, int background);

// Timewarp's bar (TRX64-Ultimate docs/specs/T20-delta-ring.md §16.7): while it
// is up the menu box is hidden and the still is shown at full brightness, with
// one bar over the lower border. Begin returns false when the freeze has no
// pixel layer on screen (no HDMI, older bitstream): then the caller keeps the
// window over the menu. There is no end call: both ways out close the menu,
// and the freeze's teardown puts the bar away with the layer.
enum { TW_GLYPH_PAUSE, TW_GLYPH_BACK, TW_GLYPH_FWD, TW_GLYPH_OLDEST, TW_GLYPH_NEWEST };

typedef struct {
    int glyph;              // TW_GLYPH_*
    const char *pos;        // large, after the glyph ("-10.4 s")
    const char *info;       // small, after it ("frame 1234 / 1500")
    const char *hint;       // small, right-aligned ("RETURN jump  STOP cancel")
    int track_pos;          // the cursor on the ring's span, 0 = oldest
    int track_len;          // the span (entries); 0 draws no track
    bool warn;              // info in light red (no frame a rewind can reach)
} trx_tw_bar_t;

bool trx_video_tw_begin(void);
// The still of the frame the machine drew last (after a scrub step), shown
// from the next trx_video_tw_bar() on.
void trx_video_tw_still(void);
void trx_video_tw_bar(const trx_tw_bar_t *bar);

#else

static inline void trx_video_freeze_enter(void) { }
static inline void trx_video_freeze_leave(void) { }
static inline void trx_video_menu_on(void) { }
static inline bool trx_video_slim_active(void) { return false; }
static inline void trx_video_menu_view(void) { }
static inline void trx_video_menu_background(int) { }
static inline void trx_video_menu_off(void) { }
static inline bool trx_video_tw_begin(void) { return false; }
static inline bool trx_video_wide_begin(void) { return false; }
static inline void trx_video_wide_end(void) { }

#endif

#endif
