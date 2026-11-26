// T265: what the host application's UI asks of the guard's board half (guard_board.cc). Kept apart from guard.h, which is pure.
#ifndef HOST_GUARDNOTE_H
#define HOST_GUARDNOTE_H

#define GUARD_NOTE_MAX 120

// Creates the background job when this application was started from the flash (verdict no record or match). Once; call it after the first menu frame.
void guard_start_background(void);
// The job runs: a flash writer (update, restore) waits and says so.
bool guard_busy(void);
// Waits (at most two minutes) until the job is done: the writers that must not run beside it (legacy_stage) call it first.
void guard_wait_idle(void);
// A line the job left for the screen (something it could not write), once; *tone is an HcTone.
bool guard_take_note(char *text, int max, int *tone);
// The file layer for the writers that run in the UI task (the .txu update, the repair).
void guard_board_install(void);

#endif
