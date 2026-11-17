// T164 WP16 (G2, G14): the application finds the host's gateware by the magic at 0x4F04 (not by a descriptor flag) and waits for a requested mode by
// the status register: pix_locked, running and the mode in use equal to the request, the refused bit only when the request counters agree.
// The register file is played by a small model that comes up `delay` polls after the request.
#include "check.h"
#include "videomode.h"
#include <string.h>

struct Model {
    uint8_t magic = GW_MAGIC, version = 1, flags = GW_F_GT_READY | GW_F_PIX_LOCKED | GW_F_RUNNING, mode = 2, seen = 0, done = 0;
    int delay = 3;                  // status polls after the request before the new row shows
    int polls = 0;
    int target = -1;
    bool refuse = false;            // the request is refused (the bit, counters equal)
    uint8_t up_flags = GW_F_GT_READY | GW_F_PIX_LOCKED | GW_F_RUNNING;   // the flags once the row is up
    uint8_t up_mode_xor = 0;        // a status that shows another row than the one asked for
    int waited_ms = 0;
    int writes = 0;
    uint8_t last_req = 0xFF;
    uint8_t scale = 1;              // T222: the scale in use (0x4F0A), written at 0x4F01
    int scale_delay = 2;            // reads of 0x4F0A before a written scale shows
    int scale_polls = 0;
    uint8_t scale_pend = 1;
    int scale_writes = 0;
    uint8_t follow = 0;             // T234: Frame follow (0x4F0C), written at 0x4F02
    uint8_t follow_pend = 0;
    int follow_delay = 2;           // reads of 0x4F0C before a written value shows
    int follow_polls = 0;
    int follow_writes = 0;
    int retune_delay = 3;           // status reads after the write before the transmitter is locked and running again
    bool retuning = false;
    int retune_polls = 0;
    bool retune_never = false;
};
static Model M;

static void wr(uint32_t off, uint8_t v)
{
    if (off == GW_FOLLOW) {
        M.follow_writes++; M.follow_pend = v; M.follow_polls = 0;
        M.flags = GW_F_GT_READY;                                     // the gateware re-tunes like for a mode request: not locked, not running
        M.retuning = true; M.retune_polls = 0;
        return;
    }
    if (off == GW_SCALE) { M.scale_writes++; M.scale_pend = v; M.scale_polls = 0; return; }
    if (off == GW_REQ) {
        M.writes++;
        M.last_req = v;
        M.seen++;
        M.polls = 0;
        M.target = v;
        if (M.refuse) {
            M.done = M.seen;
            M.flags = GW_F_REFUSED | GW_F_GT_READY | GW_F_PIX_LOCKED | GW_F_RUNNING;
        } else {
            M.flags = GW_F_GT_READY;     // the transmitter restarts: not locked, not running
        }
    }
}
static uint8_t rd(uint32_t off)
{
    if (off == GW_MAGIC_AT) return M.magic;
    if (off == GW_VERSION_AT) return M.version;
    if (off == GW_SCALE_AT) { if (++M.scale_polls > M.scale_delay) M.scale = M.scale_pend; return M.scale; }
    if (off == GW_FOLLOW_AT) { if (++M.follow_polls > M.follow_delay) M.follow = M.follow_pend; return M.follow; }
    if (off == GW_SEEN_AT) return M.seen;
    if (off == GW_DONE_AT) return M.done;
    if (off == GW_FLAGS_AT || off == GW_MODE_AT) {
        if (off == GW_FLAGS_AT && M.retuning && !M.retune_never && ++M.retune_polls > M.retune_delay) { M.flags = M.up_flags; M.retuning = false; }
        // the status is read as a block: count the block on the flags byte
        if (off == GW_FLAGS_AT && M.target >= 0 && !M.refuse) {
            M.polls++;
            if (M.polls > M.delay) {
                M.flags = M.up_flags;
                M.mode = (uint8_t)(M.target ^ M.up_mode_xor);
                M.done = M.seen;
            }
        }
        return off == GW_FLAGS_AT ? M.flags : M.mode;
    }
    return 0;
}
static void wait_ms(int ms) { M.waited_ms += ms; }
static const GwIo io = { wr, rd, wait_ms };

static void fresh() { M = Model(); }

int main()
{
    GwStatus st;
    // ---- the probe: the magic and the version, nothing else
    fresh();
    CHECK(video_gw_probe_io(&io, &st));
    CHECK(st.present && st.version == 1 && st.mode == 2);
    fresh(); M.magic = 0x00;                         // an old host: the window answers something else
    CHECK(!video_gw_probe_io(&io, &st));
    CHECK(!st.present);
    fresh(); M.magic = 0xFF;
    CHECK(!video_gw_probe_io(&io, &st));
    fresh(); M.version = 0;                          // the magic with no version is not a status we know
    CHECK(!video_gw_probe_io(&io, &st));

    // ---- the pure rule
    {
        GwStatus s = GwStatus();
        s.present = true; s.flags = GW_F_PIX_LOCKED | GW_F_RUNNING; s.mode = 7;
        CHECK(video_gw_up(s, 7));
        CHECK(!video_gw_up(s, 6));                                                  // another row
        s.flags = GW_F_RUNNING;                                                     // not locked
        CHECK(!video_gw_up(s, 7));
        s.flags = GW_F_PIX_LOCKED;                                                  // not running
        CHECK(!video_gw_up(s, 7));
        s.flags = GW_F_PIX_LOCKED | GW_F_RUNNING | GW_F_REFUSED; s.seen = 4; s.done = 4;   // refused and current
        CHECK(!video_gw_up(s, 7));
        s.done = 3;                                                                 // a stale refused bit while the new request is still crossing
        CHECK(video_gw_up(s, 7));
        s.present = false;
        CHECK(!video_gw_up(s, 7));
    }

    // ---- the wait: the request is written once, the row comes up after a few polls, well inside the second
    fresh();
    CHECK(video_gw_apply_io(&io, VID_SYS_NTSC60, VID_HDMI_720P, 1000));
    CHECK_EQ(M.writes, 1);
    CHECK_EQ(M.last_req, 7);                                                        // family * 6 + hdmi
    CHECK(M.waited_ms >= 30 && M.waited_ms < 100);                                  // not a fixed 400 ms
    // a mode that is up already costs no wait
    fresh(); M.mode = 2; M.delay = 0; M.polls = 100;
    CHECK(video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_1080P, 1000));
    CHECK(M.waited_ms <= 10);

    // ---- the ways it does not come up: the call gives up after the timeout and says no
    fresh(); M.up_flags = GW_F_GT_READY | GW_F_RUNNING;                              // never locks
    CHECK(!video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_720P, 1000));
    CHECK(M.waited_ms >= 990 && M.waited_ms <= 1010);
    fresh(); M.up_flags = GW_F_GT_READY | GW_F_PIX_LOCKED;                           // locks, never runs
    CHECK(!video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_720P, 300));
    fresh(); M.up_mode_xor = 1;                                                     // the status shows another row
    CHECK(!video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_720P, 300));
    // refused: no point in waiting
    fresh(); M.refuse = true;
    CHECK(!video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_800x600, 1000));          // row 3 does not exist (Q5)
    CHECK(M.waited_ms == 0);
    CHECK_EQ(M.writes, 1);
    // an absent gateware never reports a mode up
    fresh(); M.magic = 0;
    CHECK(!video_gw_apply_io(&io, VID_SYS_PAL50, VID_HDMI_720P, 100));

    // ---- T222: the VIC scaler's gateware (version 2, the C64-type flag): the scale register and the rows it takes
    {
        GwStatus s = GwStatus();
        s.present = true; s.version = 2; s.flags = GW_F_C64_TYPE | GW_F_RUNNING;
        CHECK(video_gw_has_scale(s));
        s.version = 1;                                                              // the host's generic mode_req: no scale register
        CHECK(!video_gw_has_scale(s));
        s.version = 2; s.flags = GW_F_RUNNING;                                      // version 2 without the flag
        CHECK(!video_gw_has_scale(s));
        s.flags = GW_F_C64_TYPE; s.present = false;
        CHECK(!video_gw_has_scale(s));
        s.present = true; s.version = 2;
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_1024x768), 9);        // 800 x 600 at 60 Hz
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_1280x1024), 9);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_800x600), 9);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_1080P), 8);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_PAL50, VID_HDMI_800x600), 0);          // no 50 Hz PC row: 576p
        CHECK_EQ(video_gw_row_for(s, VID_SYS_PAL50, VID_HDMI_1280x1024), 0);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_PAL50, VID_HDMI_720P), 1);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_480_576), 6);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_PAL60, VID_HDMI_1080P), 8);            // T222: PAL-60 runs the 60 Hz row, the gateware reports 8 back
        s.version = 1; s.flags = 0;                                                 // any other gateware: the plain index
        CHECK_EQ(video_gw_row_for(s, VID_SYS_NTSC60, VID_HDMI_1280x1024), 11);
        CHECK_EQ(video_gw_row_for(s, VID_SYS_PAL50, VID_HDMI_800x600), 3);
        // the apply goes through the mapping: 1280 x 1024 at 60 Hz writes row 9 and comes up
        fresh(); M.version = 2; M.flags = GW_F_GT_READY | GW_F_C64_TYPE; M.up_flags = GW_F_GT_READY | GW_F_PIX_LOCKED | GW_F_RUNNING | GW_F_C64_TYPE;
        CHECK(video_gw_apply_io(&io, VID_SYS_NTSC60, VID_HDMI_1280x1024, 1000));
        CHECK_EQ(M.last_req, 9);
        // the scale: written once, read back from 0x4F0A, a value that is no scale is not written
        fresh();
        CHECK(video_gw_set_scale_io(&io, VID_SCALE_FILL, GW_SCALE_TIMEOUT_MS));
        CHECK_EQ(M.scale, 2);
        CHECK_EQ(M.scale_writes, 1);
        CHECK(!video_gw_set_scale_io(&io, 3, GW_SCALE_TIMEOUT_MS));
        CHECK(!video_gw_set_scale_io(&io, -1, GW_SCALE_TIMEOUT_MS));
        CHECK_EQ(M.scale_writes, 1);
        fresh(); M.scale_delay = 1000;                                              // never shows: gives up after the timeout
        CHECK(!video_gw_set_scale_io(&io, VID_SCALE_INTEGER, 100));
        CHECK(M.waited_ms >= 90 && M.waited_ms <= 110);
    }

    // ---- T234: Frame follow (version 4, the Amiga cores' mode_req): found by the version, written once, read back at 0x4F0C, then the re-tune is waited for
    {
        GwStatus s = GwStatus();
        s.present = true; s.version = 4;
        CHECK(video_gw_has_follow(s));
        s.version = 3; CHECK(!video_gw_has_follow(s));                              // the C64 line's mode_req (smoothing): no such register
        s.version = 1; CHECK(!video_gw_has_follow(s));                              // the host's generic one
        s.version = 4; s.present = false; CHECK(!video_gw_has_follow(s));
        fresh(); M.version = 4;
        CHECK(video_gw_set_follow_io(&io, 1, GW_APPLY_TIMEOUT_MS));
        CHECK_EQ(M.follow, 1);
        CHECK_EQ(M.follow_writes, 1);
        CHECK_EQ(M.follow_writes, 1);
        CHECK(video_gw_set_follow_io(&io, 0, GW_APPLY_TIMEOUT_MS));
        CHECK_EQ(M.follow, 0);
        CHECK_EQ(M.follow_writes, 2);
        CHECK(!video_gw_set_follow_io(&io, 2, GW_APPLY_TIMEOUT_MS));               // not a value: not written
        CHECK(!video_gw_set_follow_io(&io, -1, GW_APPLY_TIMEOUT_MS));
        CHECK_EQ(M.follow_writes, 2);
        fresh(); M.version = 4; M.follow_delay = 1000;                              // never shows: gives up after the timeout
        CHECK(!video_gw_set_follow_io(&io, 1, 100));
        CHECK(M.waited_ms >= 90 && M.waited_ms <= 110);
        fresh(); M.version = 4; M.retune_never = true;                              // the register is taken, the transmitter never comes back
        CHECK(!video_gw_set_follow_io(&io, 1, 300));
        CHECK_EQ(M.follow, 1);
        fresh(); M.version = 4; M.retune_delay = 20;                                // the re-tune takes its time: waited for, not abandoned at the first status
        CHECK(video_gw_set_follow_io(&io, 1, GW_APPLY_TIMEOUT_MS));
        CHECK(M.waited_ms >= 50);
    }

    DONE("gwstatus");
}
