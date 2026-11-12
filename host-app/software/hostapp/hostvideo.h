// The host application's picture (T164 WP17: G11-G13, D7, Q6, Q7, Q9). Pure policy; the board and the benches bring the hooks.
//
//   the screen decides   the rows the F8 Picture row, the V key and the web page offer are video_modes_offered() of the screen's EDID
//                        (app/video/edid.h; no EDID: the Q9 list). A row the screen does not name is never offered and never applied.
//   the host's default   one stored choice (host.cfg [Video], the T163 store): a resolution or "from the screen". It is kept when the
//                        screen does not take it, and used again when a screen that does is plugged in. It reaches the core app as
//                        video_current (the hand-over's `video` field).
//   Q6                   no Hz choice: the family is the stored `system` (PAL 50 Hz unless boot.cfg's old choice said NTSC) and only when
//                        the screen names no row of it, the other family is taken silently.
//   D11b                 about every 500 ms the hot-plug line and the EDID are looked at; a screen that does not name the running mode
//                        gets the default (if it names it) or the best fitting row, once. The same EDID again applies nothing.
//   Q7                   a change by the user (F8, V, the web page) is kept after any key and goes back to the confirmed mode after 8 s
//                        otherwise. Only with the mode-request gateware (a screen that refuses it cannot show the question otherwise).
//   boot.cfg             `video=` is read once: the first start that finds a choice there and none in host.cfg moves it over and clears it.
#ifndef HOSTVIDEO_H
#define HOSTVIDEO_H

#include <stdint.h>
#include "videomode.h"
#include "edid.h"

#define HV_POLL_MS    500           // D11b: the cadence of the hot-plug and EDID look
#define HV_CONFIRM_MS 8000          // Q7
#define HV_SETTLE_MS  2000          // how long a request may take until the transmitter and the screen's clock are up
#define HV_ACK_MS     500           // how long the request register may take to answer at all

// ---- the rows a screen offers (pure, inline) ------------------------------------------------------------------------------------
// The family the application uses for `sys` under `mask`: its own (Q6) unless the mask names no row in it.
inline int hv_family(uint16_t mask, int sys)
{
    uint16_t own = (uint16_t)((sys == VID_SYS_NTSC60 ? mask >> 6 : mask) & 0x3F);
    uint16_t other = (uint16_t)((sys == VID_SYS_NTSC60 ? mask : mask >> 6) & 0x3F);
    if (own == 0 && other != 0) {
        return sys == VID_SYS_NTSC60 ? VID_SYS_PAL50 : VID_SYS_NTSC60;
    }
    return sys;
}
inline bool hv_offered(uint16_t mask, int sys, int hdmi)
{
    return hdmi >= 0 && hdmi < VID_HDMI_COUNT && (mask & (1u << video_row_of(hdmi, hv_family(mask, sys)))) != 0;
}
// The resolutions in order (576p/480p ... 1280 x 1024); returns how many (at most VID_HDMI_COUNT).
inline int hv_list(uint16_t mask, int sys, int *out)
{
    int n = 0;
    for (int h = 0; h < VID_HDMI_COUNT; h++) {
        if (hv_offered(mask, sys, h)) {
            out[n++] = h;
        }
    }
    return n;
}
// The next offered resolution after `cur` (-1: none yet, so the first/last), wrapping. -1 when nothing is offered.
inline int hv_step(uint16_t mask, int sys, int cur, int dir)
{
    int l[VID_HDMI_COUNT];
    int n = hv_list(mask, sys, l);
    if (n == 0) {
        return -1;
    }
    int at = -1;
    for (int i = 0; i < n; i++) {
        if (l[i] == cur) {
            at = i;
        }
    }
    if (at < 0) {
        return dir < 0 ? l[n - 1] : l[0];
    }
    return l[(at + dir + n) % n];
}

// ---- the hooks -------------------------------------------------------------------------------------------------------------------
struct HvGwStatus {
    uint8_t flags;       // 0x4F06: bit 7 last request refused, 4 C64-type core, 3 row is locked, 2 running, 1 pixel clock locked, 0 transmitter ready
    uint8_t mode;        // 0x4F07: the mode hdmi_timing runs
    uint8_t written;     // 0x4F08
    uint8_t processed;   // 0x4F09
};
#define HV_GW_REFUSED  0x80
#define HV_GW_RUNNING  0x04
#define HV_GW_PIXLOCK  0x02
#define HV_GW_GTREADY  0x01

struct HostVideoIo {
    bool     (*hpd)(void);                                  // the hot-plug line
    bool     (*read_edid)(uint8_t *buf, int max, int *len); // false: nobody answered
    bool     (*gw_present)(void);                           // 0x4F04 reads 0x48: this host answers the mode request
    void     (*gw_write)(int row);                          // 0x4F00
    bool     (*gw_status)(HvGwStatus *st);                  // 0x4F06 .. 0x4F09
    void     (*legacy_apply)(int sys, int hdmi);            // the old firmware path: PLL and scan registers (a host without the request register)
    void     (*enable)(void);                               // HDMI output on
    uint32_t (*ms)(void);
    void     (*pause_ms)(int ms);
};

// The stored default (host.cfg [Video]); the board and the benches bring it. hdmi -1: from the screen. sys: VID_SYS_*.
void hv_saved_load(int *hdmi, int *sys);
void hv_saved_store(int hdmi, int sys);

// ---- the state and the calls ----------------------------------------------------------------------------------------------------
enum HvEvent { HV_NONE = 0, HV_FOLLOWED, HV_REVERTED, HV_CONFIRM_TICK };

struct HvState {
    bool     gw;             // the host answers the mode request
    uint16_t mask;           // the rows the screen offers now
    uint16_t native;         // T187 D5: the rows of mask its short video descriptors flag as native
    bool     have_edid;      // an EDID that read and parsed (false: nobody answered, the Q9 list)
    uint32_t sig;            // a digest of the EDID bytes last looked at; 0 with none
    bool     sig_valid;
    int      sys, hdmi;      // what is up (the effective family, the resolution); hdmi -1 before the first start
    int      def_hdmi;       // the stored choice, -1 from the screen
    int      def_sys;
    bool     pending;        // Q7: a change waits for a key
    int      pend_def;       // ... and the stored choice it would become (-1 from the screen)
    int      prev_sys, prev_hdmi;
    uint32_t deadline;
    uint32_t next_poll;
    bool     migrated;       // a choice from boot.cfg was moved over in this boot
    bool     loaded;         // the stored default has been read from host.cfg in this boot
};

const HostVideoIo *hostvideo_board_io(void);                // the board's hooks (hostvideo_board.cc); a bench brings its own
void hv_init(const HostVideoIo *io);                       // probes the host once
const HvState *hv_state(void);
bool hv_gateware(void);

// Reads the stored default from host.cfg; `boot_sys`/`boot_hdmi` are boot.cfg's old `video=` values (-1 when absent). The old choice moves into host.cfg
// once, when host.cfg holds none. Returns true when it moved (the caller clears boot.cfg's line and saves it).
bool hv_load_default(int boot_sys, int boot_hdmi);

// The start sequence (what video_start was): reads the screen, picks the default if offered else the best fitting row, applies it when it is not up
// already. With `use_saved` false the stored default is not consulted (hostapp.cc's call before the flash disk is mounted). With `use_saved` true the
// default is read from host.cfg here when hv_load_default has not run yet in this boot (a board without boot.cfg never calls it).
void hv_start(bool use_saved, int *used_sys, int *used_hdmi);

// A change by the user. False (nothing changed, `why` filled) for a row the screen does not name or a refused request. `hdmi` -1 = from the screen.
// With the gateware it starts the Q7 question (pending); without it the choice is stored at once, as before.
bool hv_user_set(int hdmi, char *why, int why_n);
// Any key during the question: keeps the mode and stores it. True when there was a question (the key is consumed).
bool hv_keep(void);
// The resolution "from the screen" asks for now (what hv_user_set(-1) would put up).
int hv_auto(void);
// LEFT / ESC during the question: back to the previous mode at once (T187 D5, Q-D5.2). True when there was a question.
bool hv_revert(void);
// Called from the UI loop: the hot-plug follow (D11b) and the revert timer (Q7). The event says what the screen did.
HvEvent hv_tick(void);
int hv_seconds_left(void);

#endif
