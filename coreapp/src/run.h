// The application's life after identification (T93 D4 steps 3-8, the Freeze loop). Board code.
#ifndef COREAPP_RUN_H
#define COREAPP_RUN_H

#include "desc.h"
#include "handover.h"
#include "board.h"

// h: the hand-over, null where the profile has none (a developer start, the monolith).
// The page of a core that cannot be started (T115 7 H1): the reason, "Press RETURN to return to the host menu" where the board has a way back
// (start.h held_offers_return), the keys of the USB keyboard and the matrix. The core stays held; never returns.
void coreapp_held(const char *why, const BoardProfile *board) __attribute__((noreturn));

void coreapp_run(const CoreDesc *desc, const Handover *h, const BoardProfile *board) __attribute__((noreturn));


#endif
