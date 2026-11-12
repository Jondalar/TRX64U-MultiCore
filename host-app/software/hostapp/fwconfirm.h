// The two questions that come before the flash is written (T137 D5, T135 D9). Pure code: keys and a clock go in, a verdict comes out,
// so tests/hostapp/test_fwconfirm.cc can drive every wrong way and count the writes behind it.
//
//   open (R, the file is checked) -> ASK1: the details page, "Y continues, any other key cancels"
//   Y                                      -> ASK2: "This is the last question", RETURN within 10 s
//   RETURN, 1 s or more after ASK2 began   -> GO (the only way to a first erase)
//   any other key in ASK1 or ASK2, ESC / RUN-STOP, 10 s in ASK2, 60 s without a key in ASK1 -> ABORT (nothing written)
// The key repeat of a held Y is not an answer to the second question: every key in the first second of ASK2 is ignored.
#ifndef HOST_FWCONFIRM_H
#define HOST_FWCONFIRM_H

#include <stdint.h>

#define FWC_ARM_MS      10000     // the RETURN of the second question has to come within this
#define FWC_IDLE_MS     60000     // no key at all in the first question
#define FWC_MIN_GAP_MS  1000      // the second question takes no key this long after it appeared

enum FwcState { FWC_CLOSED, FWC_ASK1, FWC_ASK2, FWC_STARTED, FWC_CANCELLED };
enum FwcKey { FWCK_Y, FWCK_RETURN, FWCK_ESCAPE, FWCK_OTHER };
enum FwcEvent { FWCE_NOTHING, FWCE_REDRAW, FWCE_GO, FWCE_ABORT };

struct FwConfirm {
    FwcState state;
    uint32_t t_key;          // the last key of any kind
    uint32_t t_arm;          // when ASK2 began
};

void fwc_open(FwConfirm *c, uint32_t now);
FwcEvent fwc_key(FwConfirm *c, FwcKey k, uint32_t now);
FwcEvent fwc_tick(FwConfirm *c, uint32_t now);          // the time-outs; a few times a second
int fwc_left(const FwConfirm *c, uint32_t now);         // seconds left of the 10 s in ASK2, else 0

#endif
