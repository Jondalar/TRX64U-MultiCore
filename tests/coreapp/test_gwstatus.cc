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
};
static Model M;

static void wr(uint32_t off, uint8_t v)
{
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
    if (off == GW_SEEN_AT) return M.seen;
    if (off == GW_DONE_AT) return M.done;
    if (off == GW_FLAGS_AT || off == GW_MODE_AT) {
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

    DONE("gwstatus");
}
