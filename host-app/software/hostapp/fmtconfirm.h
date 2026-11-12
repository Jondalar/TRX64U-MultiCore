// The deliberate act that must come before a format writes anything (T111 D7, Q2 decided by Alex 2026-10-26). Pure code: keys and a
// clock go in, a verdict comes out, so tests/hostapp/test_fmtconfirm.cc can drive every wrong way and count the writes behind it.
//
//   open (F on a medium)  -> TYPING: the red page; the person types the medium's name as the list shows it
//   RETURN, name exact    -> ARMED: "RETURN again within 10 s"
//   RETURN, 1 s or more later, within 10 s  -> START (the only way to a first write)
//   any other key in ARMED, 10 s without the second RETURN, 60 s without a key in TYPING, ESC / RUN-STOP in TYPING,
//   the medium taken away  -> CANCEL
// The key repeat of a held RETURN is not a second RETURN: a RETURN less than 150 ms after the previous one is ignored (and counts as
// "the previous one" for the next), and one less than 1 s after arming is ignored too (the first repeat of a key held down comes 250-660 ms after the press).
#ifndef HOST_FMTCONFIRM_H
#define HOST_FMTCONFIRM_H

#include <stdint.h>

#define FMTC_NAME_MAX      24
#define FMTC_ARM_MS        10000     // the second RETURN has to come within this
#define FMTC_IDLE_MS       60000     // no key at all while typing
#define FMTC_MIN_GAP_MS    1000      // the second RETURN counts only this long after arming
#define FMTC_REPEAT_MS     150       // a RETURN this close to the previous one is a key repeat

enum FmtcState { FMTC_CLOSED, FMTC_TYPING, FMTC_ARMED, FMTC_STARTED, FMTC_CANCELLED };
enum FmtcKey { FMTK_CHAR, FMTK_RETURN, FMTK_BACKSPACE, FMTK_ESCAPE, FMTK_OTHER };
enum FmtcEvent {
    FMTC_NOTHING,     // nothing to redraw
    FMTC_REDRAW,      // the page changed (typed text, a message, a second of the countdown)
    FMTC_GO,          // the first write may begin: both RETURNs came
    FMTC_ABORT        // the act is over without a write
};

struct FmtConfirm {
    FmtcState state;
    char name[FMTC_NAME_MAX];       // what has to be typed (the medium as the list shows it)
    char typed[FMTC_NAME_MAX];
    int n;
    uint32_t t_key;                 // the last key of any kind
    uint32_t t_arm;                 // when ARMED began
    uint32_t t_ret;                 // the last RETURN
    bool have_ret;
    const char *msg;                // one line for the page ("the name does not match"), NULL = none
};

void fmtc_open(FmtConfirm *c, const char *medium, uint32_t now);
FmtcEvent fmtc_key(FmtConfirm *c, FmtcKey k, int ch, uint32_t now);
// The clock and the medium: time-outs, and the medium taken away. Call it a few times a second.
FmtcEvent fmtc_tick(FmtConfirm *c, uint32_t now, bool medium_present);
// Seconds left of the 10 s while ARMED, else 0.
int fmtc_arm_left(const FmtConfirm *c, uint32_t now);

#endif
