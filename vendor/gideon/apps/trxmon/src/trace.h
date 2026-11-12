/*
 * trace.h — the instruction trace as the monitor's history (T21 §4.11): TRX64's
 * chis, whowrote, triage, revdepth and trace over the firmware's
 * trace records (hw_trace_*), and the same as JSON-RPC results.
 *
 * Records are the instructions of the timeline the machine is on, oldest
 * first; a record's registers are BEFORE its instruction, its cycle the T20
 * timeline's count after the edge that ended its opcode fetch (what a stop on
 * that instruction reports, less the ring's offset to the reported cycles).
 */
#ifndef TRXMON_TRACE_H
#define TRXMON_TRACE_H

#include "json.h"
#include "mach.h"
#include "hw.h"

/* There are records to read: the bitstream has the former and the ring runs */
int trace_present(void);

/* monitor verbs: trace chis whowrote triage revdepth; -1 when op is none of them */
int trace_verb(const char *op, int argc, char **argv, sbuf_t *out);

/* JSON-RPC results (the writer is inside "result"); NULL, or why not (-32001) */
const char *trace_who_wrote_json(jw_t *w, uint16_t addr, int limit);
const char *trace_triage_json(jw_t *w, int have_pc, uint16_t pc);
const char *trace_depth_json(jw_t *w);

/* The record n back from the stop at ring cycle `vp` (the count after the fetch edge of the
 * instruction the machine stands at: its own record is not written yet): 0 and the record,
 * or -1 when the history holds fewer than n records before vp. */
int trace_rec_back(uint64_t vp, int n, hw_trace_rec_t *r);

#endif
