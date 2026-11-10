// A tape file as the pulse stream of the shared tape engine (T91 D9, T92 D9; cores/zx/shared/zxcpc_tape.v). The application converts the
// whole tape before it starts: .tap (the Spectrum's bare blocks, ROM loader timings) and .tzx / .cdt (the TZX container; a CDT is a TZX
// with the Amstrad's blocks, the timings are 3.5 MHz T-states in both). The stream is 16 bit little-endian words:
//   0x0001-0xFFFD  the level holds for that many ticks, then it toggles
//   0xFFFF         a gap: the next two words (low, high) are a 32 bit tick count; the level holds and does NOT toggle after it
//   0xFFFE         "stop the tape": the engine stops, a Play resumes it
//   0x0000         the end of the tape
// A tick is one 3.5 MHz unit. What the engine cannot play is refused with a reason, never skipped: a pulse shorter than 16 ticks (a turbo
// loader, T91 Q5), a CSW or generalised data block, call and jump blocks, a block id this file does not know. Pure: CoreFile only.
#ifndef COREAPP_TAPE_PULSES_H
#define COREAPP_TAPE_PULSES_H

#include "fsif.h"

#define TAPE_BLOCKS_MAX 256               // the block index: where each pulse-making block starts (a rewind goes to one of them)
#define TAPE_MAX_WORDS (2u * 1024u * 1024u)   // 4 MB: the stream's room in the DDR2 window (T91 D5)
#define TAPE_MIN_PULSE 16                 // ticks: the engine's stream rule (zxcpc_tape.v)

#define TAPE_STOP 0xFFFEu
#define TAPE_GAP 0xFFFFu

enum TapeFormat { TAPE_TAP = 0, TAPE_TZX = 1 };

// Receives the words in order. Returns false when it cannot take them (the DDR2 write is the caller's, a bench keeps a vector).
class TapeOut {
public:
    virtual ~TapeOut() { }
    virtual bool put(const uint16_t *w, uint32_t n) = 0;
};

struct TapeIndex {
    uint32_t words;                       // the stream's length in words, the end marker not counted
    uint32_t nblocks;
    uint32_t start[TAPE_BLOCKS_MAX];      // word index of the first word of each block that makes pulses, ascending
    bool truncated;                       // more blocks than the index holds: the later ones are on the tape but not in the index
};

// Converts `f` (format by the caller, from the extension). stop48: a TZX "stop the tape if in 48K mode" block stops the tape (the
// Spectrum 48K); false: it is ignored (the 128K, the CPC). Returns 0, or a reason in msg.
int tape_convert(CoreFile *f, TapeFormat fmt, bool stop48, TapeOut *out, TapeIndex *idx, char *msg, int msglen);

// The extension decides: ".tap" is a TAP, ".tzx" and ".cdt" are TZX containers (case-insensitive). False for anything else.
bool tape_format_of(const char *path, TapeFormat *fmt);

// The index entry the position is in (the last start <= pos); 0 when the index is empty.
uint32_t tape_block_at(const TapeIndex *idx, uint32_t pos_words);

#endif
