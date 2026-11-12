/*
 * ring.h — the T20 frame ring as the monitor's timeline (T21 §4.10): the
 * TRX64 rewind transport (frame, goto, rewind), marks, the reverse step and
 * the ring files, over the firmware's ring table (hw_ring_*).
 *
 * Entries are frame starts (the snaps on line 0). A land puts the machine on
 * one and leaves it paused there without cutting the timeline; whatever the
 * debugger then runs is logged and undone by the next land. A free run from
 * a land cuts the frames after it (TRX64's intervention cut).
 */
#ifndef TRXMON_RING_H
#define TRXMON_RING_H

#include "json.h"
#include "mach.h"

#define RING_MAX_MARKS 32

typedef struct
{
    int index;       /* in the ring now */
    uint32_t seq;
    uint64_t cycles; /* as reported (c64Cycles at that frame's start) */
    uint32_t frame;  /* cycles / cycles per frame, TRX64's frame number in ids */
    int target;      /* a land can go there */
    char id[32];     /* cp_<frame>_<seq> */
} ring_entry_t;

typedef struct
{
    int steps;
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint64_t cycle;
    int frame_from;  /* 1-based frame the replay started from */
} ring_rstep_t;

int ring_present(void);
/* A08: "Timewarp is off while the REU is enabled (A08)" while the REU is on (the firmware's ring status
 * says so), else NULL. Every way into the ring answers with it. */
#define RING_REU_OFF_TEXT "Timewarp is off while the REU is enabled (A08)"
const char *ring_off_reason(void);
/* reported cycles - the ring timeline's count of the machine's position (what trace records add) */
int64_t ring_cycle_offset(void);
/* Before the machine runs freely: an open land is committed */
void ring_before_run(void);
int ring_count(void);
int ring_entry(int index, ring_entry_t *e);
/* The entry the machine is in (the land's, else the newest), -1 none */
int ring_cursor(void);
int ring_find_id(const char *id);
const char *ring_land(int index);
/* reverse step n instructions; NULL or why not */
const char *ring_rstep(int n, ring_rstep_t *r);

/* TRX64 transport status line and object */
void ring_status_line(sbuf_t *out);
void ring_status_json(jw_t *w, int hit_end); /* hit_end: TRX64's hitEnd: true */
/* frame +-N; NULL or why not. at_end: the move stopped at an end */
const char *ring_frame_move(int delta, int *at_end);
const char *ring_goto(const char *target);

/* marks: NULL or why not */
const char *ring_mark_set(const char *name, ring_entry_t *e);
const char *ring_mark_drop(const char *name, uint64_t *was_cycles);
int ring_marks(void);
const char *ring_mark_name(int i);
int ring_mark_entry(int i, ring_entry_t *e); /* 0, or -1: its frame left the ring */
const char *ring_mark_of(uint32_t seq);

/* monitor verbs: rstep reverse rewind frame goto mark marks unmark; -1 when op is none of them */
int ring_verb(const char *op, int argc, char **argv, sbuf_t *out);

#endif
