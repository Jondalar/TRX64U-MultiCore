// The application's life after identification (T93 D4 steps 3-8, the Freeze loop). Board code.
#ifndef COREAPP_RUN_H
#define COREAPP_RUN_H

#include "desc.h"
#include "handover.h"
#include "board.h"

// h: the hand-over, null where the profile has none (a developer start, the monolith).
// The page of a core that cannot be started (T115 7 H1): the reason, "Press RETURN to return to the host menu" where the board has a way back
// (start.h held_offers_return), the keys of the USB keyboard and the matrix. The core stays held; never returns.
// The picture, the splash and the REST gate (port 80 answers 503 with the step, rest.h): the first thing the core app task does, before the description of a core that
// has none compiled in waits for its bundle's medium (T239.3 P2-2, M1). Once; coreapp_run and coreapp_held rely on it. h: as for coreapp_run.
void coreapp_open_early(const Handover *h);
void coreapp_wait_named(const char *what);      // names a wait on the splash (step 0) and on port 80's 503 text; the bundle's medium wait uses it

void coreapp_held(const char *why, const BoardProfile *board) __attribute__((noreturn));

void coreapp_run(const CoreDesc *desc, const Handover *h, const BoardProfile *board) __attribute__((noreturn));


#endif
