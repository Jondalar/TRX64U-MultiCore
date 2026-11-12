// REST verbs of the TRX64 Ultimate frame ring (TRX64-Ultimate
// docs/specs/T20-delta-ring.md §11.5, §15 app part; software/io/c64/trx_ring.h).
// Only the U64-II/C64U application compiles this file.
#include "routes.h"
#include "c64.h"
#include "trx_ring.h"
#include <stdio.h>
#include <stdlib.h>

static bool refuse_frozen(ResponseWrapper *resp)
{
    C64 *c64 = C64::getMachine();
    if (c64 && c64->is_frozen()) {
        resp->error("The menu has the machine; close it first");
        resp->json_response(HTTP_CONFLICT);
        return true;
    }
    return false;
}

// A08: no ring while the REU is enabled; 409 with the message the menu shows
static bool refuse_reu(ResponseWrapper *resp)
{
    const char *why = trx_ring_reu_refusal();
    if (why) {
        resp->error(why);
        resp->json_response(HTTP_CONFLICT);
        return true;
    }
    return false;
}

static bool refuse_absent(ResponseWrapper *resp)
{
    TrxRingStatus st;
    trx_ring_status(&st);
    if (!st.present) {
        resp->error("No frame ring in this bitstream");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return true;
    }
    return false;
}

// frames back from "frames" or "ms" (0 when neither is given)
static int frames_back(ArgsURI &args)
{
    int f = args.get_int("frames", -1);
    if (f >= 0) {
        return f;
    }
    int ms = args.get_int("ms", 0);
    return (ms * (int)trx_ring_fps_x100() + 50000) / 100000;
}

API_DOC(GET, timewarp, status,
    TAG("Timewarp")
    SUMMARY("State of the frame ring")
    DESCRIPTION("The TRX64 Ultimate bitstream records every frame: the chip state at the frame's "
                "start and every memory write with the byte it replaced. This returns how much of "
                "that the ring holds: frames, frames a rewind can go to, seconds, and the engine's "
                "counters. While the REU is enabled the ring is off: on is false and reason says why (A08).")
    PATH("/v1/timewarp:status", "getTimewarpStatus", "")
    RESPONSE("200", "application/json", "ErrorResponse", "The ring's state.", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(GET, timewarp, status, NULL, ARRAY( {  }))
{
    TrxRingStatus st;
    trx_ring_status(&st);
    if (!st.present) {
        resp->error("No frame ring in this bitstream");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return;
    }
    resp->json->add("on", st.on);
    if (st.reu_off) {
        resp->json->add("reason", trx_ring_reu_refusal());
    }
    resp->json->add("frames", st.frames);
    resp->json->add("targets", st.targets);
    resp->json->add("seconds_x100", (int)(((uint64_t)st.frames * 10000) / trx_ring_fps_x100()));
    resp->json->add("head_seq", (int)st.head_seq);
    resp->json->add("oldest_seq", (int)st.oldest_seq);
    resp->json->add("stream0", (int)st.log_cnt[0]);
    resp->json->add("stream1", (int)st.log_cnt[1]);
    resp->json->add("stream2", (int)st.log_cnt[2]);
    resp->json->add("dropped", (int)st.dropped);
    resp->json->add("log_stat", (int)st.log_stat);
    resp->json->add("resyncs", (int)st.resyncs);
    resp->json->add("spurious_f4", (int)st.spurious_f4);
    TrxFrame f;
    if (st.frames >= 2 && trx_ring_frame(st.frames - 2, &f) && f.e0 != 0xFFFFFFFFu) {
        resp->json->add("last_frame_writes", (int)((f.e0 - f.s0) & 0x0FFFFFFF));
        resp->json->add("last_frame_other", (int)((f.e1 - f.s1) & 0x0FFFFFFF));
        resp->json->add("last_frame_pc", (int)(f.w[2] & 0xFFFF));
        resp->json->add("last_frame_x", (int)f.x);
    }
    resp->json_response(HTTP_OK);
}

API_DOC(GET, timewarp, frame,
    TAG("Timewarp")
    SUMMARY("One frame of the ring")
    DESCRIPTION("What the ring holds about one frame: its snap position, cycle count, where its "
                "writes are in the log, and the 6510's registers at its start.")
    PATH("/v1/timewarp:frame", "getTimewarpFrame", "")
    PARAM("frames", "integer", "Frames back from the newest frame.", "0", "50")
    PARAM("index", "integer", "The frame by its index in the ring, instead of frames.", "", "1200")
    PARAM("seq", "integer", "The frame by its seq, instead of frames.", "", "4481")
    RESPONSE("200", "application/json", "ErrorResponse", "The frame.", "")
    RESPONSE_ERROR("404", "No such frame", "")
    RESPONSE_ERROR("409", "Timewarp is off while the REU is enabled (A08)", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(GET, timewarp, frame, NULL, ARRAY( { {"frames", P_OPTIONAL}, {"index", P_OPTIONAL}, {"seq", P_OPTIONAL} }))
{
    if (refuse_absent(resp) || refuse_reu(resp)) {
        return;
    }
    int idx = trx_ring_count() - 1 - args.get_int("frames", 0);
    if (args.get_int("seq", -1) >= 0) {
        idx = trx_ring_find(args.get_int("seq", -1));
    } else if (args.get_int("index", -1) >= 0) {
        idx = args.get_int("index", -1);
    }
    TrxFrame f;
    if (!trx_ring_frame(idx, &f)) {
        resp->error("No such frame");
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    char buf[16];
    resp->json->add("index", idx);
    resp->json->add("seq", (int)f.seq);
    resp->json->add("marker", (int)f.marker);
    resp->json->add("flags", (int)f.flags);
    resp->json->add("line", (int)f.line);
    resp->json->add("x", (int)f.x);
    resp->json->add("cycles", (int)f.cycles);
    resp->json->add("s0", (int)f.s0);
    resp->json->add("e0", (int)f.e0);
    resp->json->add("s1", (int)f.s1);
    resp->json->add("e1", (int)f.e1);
    resp->json->add("block", f.block);
    if (f.block) {
        sprintf(buf, "%04X", (unsigned)(f.w[2] & 0xFFFF));
        resp->json->add("pc", buf);
        resp->json->add("p", (int)((f.w[2] >> 16) & 0xFF));
        resp->json->add("a", (int)(f.w[1] & 0xFF));
        resp->json->add("xr", (int)((f.w[1] >> 8) & 0xFF));
        resp->json->add("yr", (int)((f.w[1] >> 16) & 0xFF));
        resp->json->add("sp", (int)((f.w[1] >> 24) & 0xFF));
    }
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, timewarp, start,
    TAG("Timewarp")
    SUMMARY("Start the frame ring")
    DESCRIPTION("Empties the ring and starts recording from the next frame on. Refused while the REU is "
                "enabled: the ring does not record the REU's contents (A08).")
    PATH("/v1/timewarp:start", "startTimewarp", "")
    RESPONSE("200", "application/json", "ErrorResponse", "Recording.", "")
    RESPONSE_ERROR("409", "Timewarp is off while the REU is enabled (A08)", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(PUT, timewarp, start, NULL, ARRAY( {  }))
{
    if (refuse_reu(resp)) {
        return;
    }
    if (!trx_ring_start()) {
        resp->error("No frame ring in this bitstream");
        resp->json_response(HTTP_SERVICE_UNAVAILABLE);
        return;
    }
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, timewarp, stop,
    TAG("Timewarp")
    SUMMARY("Stop the frame ring")
    DESCRIPTION("Stops recording. What the ring holds stays until the next start.")
    PATH("/v1/timewarp:stop", "stopTimewarp", "")
    RESPONSE("200", "application/json", "ErrorResponse", "Not recording.", "")
)
API_CALL(PUT, timewarp, stop, NULL, ARRAY( {  }))
{
    trx_ring_stop();
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, timewarp, rewind,
    TAG("Timewarp")
    SUMMARY("Put the machine back on an earlier frame")
    CAUTION("machine-state", "The frames after the chosen one are dropped: the machine runs on from there.")
    DESCRIPTION("Restores the machine to the start of a frame the ring holds, given as frames or "
                "milliseconds back from the newest one, and lets it run from there. RAM, colour RAM, "
                "cartridge RAM, the CPU and both CIAs are exact; the VIC's registers are replayed, "
                "its internal counters and write-only compare are not restored, the SID, REU and "
                "drive are left as they are.")
    PATH("/v1/timewarp:rewind", "rewindTimewarp", "")
    PARAM("frames", "integer", "Frames back from the newest frame.", "", "100")
    PARAM("ms", "integer", "Milliseconds back (used when frames is not given).", "0", "2000")
    PARAM("index", "integer", "The frame by its index in the ring (timewarp:frame shows it), instead of frames or ms.", "", "1200")
    PARAM("seq", "integer", "The frame by its seq (timewarp:frame shows it; unlike the index it does not move when old frames are dropped).", "", "4481")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine runs from the chosen frame.", "")
    RESPONSE_ERROR("404", "No frame that far back", "")
    RESPONSE_ERROR("409", "The menu has the machine; close it first, or Timewarp is off while the REU is enabled (A08)", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(PUT, timewarp, rewind, NULL, ARRAY( { {"frames", P_OPTIONAL}, {"ms", P_OPTIONAL}, {"index", P_OPTIONAL}, {"seq", P_OPTIONAL} }))
{
    if (refuse_absent(resp) || refuse_reu(resp) || refuse_frozen(resp)) {
        return;
    }
    int idx = args.get_int("index", -1);
    int seq = args.get_int("seq", -1);
    if (seq >= 0) {
        idx = trx_ring_find(seq);
    } else if (idx < 0) {
        idx = trx_ring_target(frames_back(args));
    }
    if (idx < 0) {
        resp->error("No frame that far back");
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    int head = trx_ring_count() - 1;
    TrxRestoreStats st;
    int err = trx_ring_restore(idx, &st);
    resp->json->add("frames_back", head - idx);
    resp->json->add("next_seq", (int)st.next_seq);
    resp->json->add("ms", (int)st.ms);
    resp->json->add("ms_stop", (int)st.ms_stop);
    resp->json->add("ms_pos", (int)st.ms_pos);
    resp->json->add("records", (int)st.rec0);
    resp->json->add("records_other", (int)st.rec1);
    resp->json->add("ram_writes", (int)st.ram_writes);
    resp->json->add("bus_writes", (int)st.bus_writes);
    resp->json->add("cart_bytes", (int)st.cart_bytes);
    resp->json->add("vic_latches_exact", st.vic_latches);
    if (err) {
        resp->error(trx_ring_error(err));
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, timewarp, scrub,
    TAG("Timewarp")
    SUMMARY("Show an earlier frame the way the Timewarp bar does (scrub step)")
    CAUTION("machine-state", "The machine stays paused on the shown frame until timewarp:scrubend.")
    DESCRIPTION("One step of the Timewarp scrub without the menu: puts the paused machine on the frame "
                "that many frames back from the newest and lets it draw that frame (the still capture "
                "holds it). The first step pauses the machine; timewarp:scrubend ends it. For tests.")
    PATH("/v1/timewarp:scrub", "scrubTimewarp", "")
    PARAM("frames", "integer", "Frames back from the newest frame.", "0", "50")
    PARAM("apply", "integer", "1: the engine applies the log (APPLY), 0: the CPU walks it; stays until changed.", "", "1")
    RESPONSE("200", "application/json", "ErrorResponse", "The frame is shown.", "")
    RESPONSE_ERROR("404", "No frame that far back", "")
    RESPONSE_ERROR("409", "Timewarp is off while the REU is enabled (A08)", "")
    RESPONSE_ERROR("500", "The step failed", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(PUT, timewarp, scrub, NULL, ARRAY( { {"frames", P_OPTIONAL}, {"apply", P_OPTIONAL} }))
{
    if (refuse_absent(resp) || refuse_reu(resp)) {
        return;
    }
    int ap = args.get_int("apply", -1);
    bool hw = trx_ring_apply_hw(ap);
    int idx = trx_ring_target(args.get_int("frames", 0));
    if (idx < 0) {
        resp->error("No frame that far back");
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    TrxRestoreStats st;
    int err = trx_ring_scrub_show(idx, &st);
    resp->json->add("index", idx);
    resp->json->add("frames", trx_ring_count());
    resp->json->add("records", (int)st.rec0);
    resp->json->add("records_other", (int)st.rec1);
    resp->json->add("ms", (int)st.ms);
    resp->json->add("apply", hw);
    if (err) {
        resp->error(trx_ring_error(err));
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    resp->json_response(HTTP_OK);
}

API_DOC(PUT, timewarp, scrubend,
    TAG("Timewarp")
    SUMMARY("End a scrub: rewind to a frame and run, or go back to the newest")
    CAUTION("machine-state", "The frames after the chosen one are dropped: the machine runs on from there.")
    DESCRIPTION("Ends a timewarp:scrub session like RETURN (frames given: the machine goes to that frame "
                "and runs) or RUN/STOP (no frames: back to the newest frame with a chip block).")
    PATH("/v1/timewarp:scrubend", "scrubendTimewarp", "")
    PARAM("frames", "integer", "Frames back from the newest frame to rewind to; leave out to go back to the newest.", "", "50")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine runs.", "")
    RESPONSE_ERROR("409", "No scrub is open, or Timewarp is off while the REU is enabled (A08)", "")
    RESPONSE_ERROR("503", "No frame ring in this bitstream", "")
)
API_CALL(PUT, timewarp, scrubend, NULL, ARRAY( { {"frames", P_OPTIONAL} }))
{
    if (refuse_absent(resp)) {
        return;
    }
    // A scrub that is open (the REU was enabled while it ran) must still be ended, or the machine stays
    // paused; with none open the REU's refusal is the answer.
    if (!trx_ring_scrubbing() && refuse_reu(resp)) {
        return;
    }
    if (!trx_ring_scrubbing()) {
        resp->error("No scrub is open");
        resp->json_response(HTTP_CONFLICT);
        return;
    }
    int f = args.get_int("frames", -1);
    if (f >= 0) {
        trx_ring_set_pending(trx_ring_target(f));
    }
    trx_ring_unfreeze();
    resp->json->add("frames", trx_ring_count());
    resp->json_response(HTTP_OK);
}
