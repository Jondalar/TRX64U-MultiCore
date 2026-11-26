// The tape deck of the PET (T91 D9, T92 D9, T134): a tape file converted to the pulse stream (tape_pulses.h) in the core's DDR2
// window, and the engine's registers - base, position, play, status - that the core has at its own offsets (cores/pet/app/lib/pet_regs.h). The ZX Spectrum's and the CPC's apps have their own copies of this file (T239.5 D16).
// The operations of the menu (insert, play, stop, rewind, block by block, eject) and the service that takes the play bit back when the
// engine stops itself are the same for every core with a deck; each descriptor registers them under its own names. Pure: CoreIo and CoreFs only.
#ifndef COREAPP_TAPE_DECK_H
#define COREAPP_TAPE_DECK_H

#include "ctx.h"
#include "tape_pulses.h"

struct TapeRegs {
    uint16_t ctrl;                // b0 play: a rising edge starts the tape, a falling edge stops it
    uint16_t base;                // 32 bit DDR2 byte address of the stream
    uint16_t pos;                 // 32 bit: write = the position a seek loads, read = the position now (25 bit)
    uint16_t seek;                // any write loads pos
    uint16_t status;              // b0 playing, b1 end of tape, b2 stop marker
    uint32_t win_off;             // the stream's place in the DDR2 window (offset from 0x01000000)
    uint8_t slot;                 // T180 D2: the deck's number among the descriptor's MountSlots (mount_notify)
    uint8_t c64tap;               // T134: 1 = the .tap files are Commodore .tap (TAPE_C64), not the Spectrum's bare blocks
};

#define TAPE_ST_END  0x02
#define TAPE_ST_STOP 0x04

struct TapeDeck {
    bool loaded, playing;
    char name[56];
    char path[FS_PATH_MAX];               // T180 D2: where the tape came from, for the stored mounts
    TapeIndex idx;
};

// The tape in `path` is converted and put in the window; the engine stops and stands at the start. play: and starts it. stop48: a TZX
// "stop the tape if in 48K mode" block stops the tape (the running machine is a 48K Spectrum). 0, or a reason in msg.
int deck_insert(CoreCtx *c, TapeDeck *d, const TapeRegs &r, const char *path, bool play, bool stop48, char *msg, int msglen);
int deck_play(CoreCtx *c, TapeDeck *d, const TapeRegs &r, char *msg, int msglen);
void deck_stop(CoreCtx *c, TapeDeck *d, const TapeRegs &r);
void deck_rewind(CoreCtx *c, TapeDeck *d, const TapeRegs &r);
// The start of the previous (dir < 0: the current one first, if the position is inside it) or the next block.
int deck_block_step(CoreCtx *c, TapeDeck *d, const TapeRegs &r, int dir, char *msg, int msglen);
void deck_eject(CoreCtx *c, TapeDeck *d, const TapeRegs &r);
// One pass of the poll: the engine has stopped itself (stop marker, end) -> the play bit goes back to 0.
void deck_service(CoreCtx *c, TapeDeck *d, const TapeRegs &r);
const char *deck_image(const TapeDeck *d);

#endif
