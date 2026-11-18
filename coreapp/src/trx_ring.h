// The frame ring of the TRX64 Ultimate bitstream (TRX64-Ultimate
// docs/specs/T20-delta-ring.md, ARCH-lean-state-access.md §15.7-§15.9): the
// app side of the engine (trx_engine.v, page 2 of the IO block).
//
// The hardware writes, while the log is on, every C64 RAM write (stream 0),
// every colour RAM and cartridge RAM write (stream 1) and one marker per
// frame with the chip block of that frame's snap (stream 2) into the upper
// DDR2. This module reads stream 2 into a timeline in app memory (one entry
// per frame boundary: its snap position, cycle count, stream positions and
// the 58 state-bus words of its chip block) and puts the machine back on any
// entry the logs still reach: undo of streams 0/1 back to the entry, the
// registers the subset load leaves out replayed as bus cycles, a stop at the
// entry's own snap position, and the CPU and CIA flip-flops loaded from the
// block on that edge (the sequence tb_ring.cpp "restore" plays).
//
// Only the U64-II/C64U application builds this; on a bitstream without the
// engine (CAPS b2) every call refuses and nothing touches the hardware.
#ifndef TRX_RING_H
#define TRX_RING_H
// PROVENANCE: TRX64-Ultimate software/ultimate-app/software/io/c64/trx_ring.h, plus trx_ring_engine_init() and ring_autostart (T104.2 UI).

#include <stdint.h>

#define TRX_BLOCK_WORDS 58   // the capture table's reset value (c64_core.v map)

// One frame boundary (the snap that starts frame "seq").
struct TrxFrame {
    uint32_t seq;            // our own number, never reused (the marker numbers restart)
    uint32_t marker;         // F1 frame number
    uint32_t s0, e0;         // stream 0 records of the frame: [s0, e0) (28-bit counts)
    uint32_t s1, e1;         // stream 1, the same
    uint64_t cycles;         // phi2 cycles since the ring started (F2 steps added up)
    uint16_t line, x;        // VIC position of the snap edge (F3)
    uint8_t  flags;          // F1 flags
    uint8_t  bflags;         // B2 flags (b0 torn)
    bool     block;          // a whole chip block came with it
    uint32_t c24;            // F2: the machine's phi2 cycle count after the snap edge, low 24 bits
                             // (the debug block's CYCLES counts the same edges, T21)
    uint32_t w[TRX_BLOCK_WORDS];
};

struct TrxRingStatus {
    bool     present;        // the bitstream has the engine
    bool     on;             // the log runs
    bool     reu_off;        // the REU is enabled: no ring (A08)
    int      frames;         // timeline entries
    int      targets;        // entries a restore can go to
    uint32_t head_seq, oldest_seq;
    uint32_t log_cnt[3];
    uint32_t dropped;
    uint32_t log_stat;
    uint32_t resyncs;        // stream 2 records skipped to find a marker again
    uint32_t spurious_f4;    // F4 records the drain wrote inside a chip block
};

// One instruction of the trace (TRX64's DeltaEntry, 24 bytes; T21 §4.11.1): the registers and PC
// BEFORE it, its opcode and operand bytes (0 for those the opcode does not have), the
// timeline's cycle count of the edge that ended its opcode fetch, and its writes as
// stream 0 records [write_start, write_start + write_count).
struct TrxTraceRec {
    uint64_t cycle;
    uint32_t write_start;
    uint16_t pc;
    uint16_t write_count;
    uint8_t  a, x, y, sp, p, opcode, b1, b2;
};

struct TrxWriteRec {         // TRX64's WriteRec
    uint16_t addr;
    uint8_t  old_value, new_value;
};

struct TrxTraceHit {         // a write of whowrote's address
    uint64_t cycle;
    uint16_t addr, pc;
    uint8_t  old_value, new_value;
    uint16_t reserved;
    uint32_t index;          // of the writing instruction in the history
};

struct TrxTraceStatus {
    bool     present, on, gap;
    uint32_t records;        // in the history
    uint32_t capacity;       // the area holds this many
    uint32_t lost;           // records the engine dropped (DROPPED)
    uint64_t oldest_cycle, newest_cycle;
};

struct TrxRestoreStats {
    uint32_t ms;             // stop to release
    uint32_t ms_stop;        // until the machine stood (on line 0) and the log was read
    uint32_t ms_pos;         // until it stood on the frame's snap position
    uint32_t frames;         // timeline entries undone
    uint32_t rec0, rec1;     // records undone
    uint32_t ram_writes;     // C64 RAM bytes written (the records coalesced per address)
    uint32_t bus_writes;     // register replay and colour RAM, as C64 bus cycles
    uint32_t cart_bytes;     // cartridge RAM bytes put back in DDR2
    uint32_t other;          // stream 1 records of spaces nothing undoes (counted)
    uint32_t next_seq;       // the seq the re-run's first frame boundary gets
    bool     vic_latches;    // the VIC's IRQ and collision latches at the load equal the block's
};

// The application starts the engine once (allocates its buffers, starts the poll task); the log itself starts 6 s later when ring_autostart is set.
extern bool ring_autostart;
void trx_ring_engine_init(void);

#if (U64 == 2) && !defined(RECOVERYAPP)

// The machine's frame rate in the System Mode that runs now (T40: PAL and
// NTSC-50 are 63 cycles x 312 lines, NTSC and PAL-60 65 x 263), read from
// C64_VIDEOFORMAT each call: whole frames per second for the UI's 1 s / 10 s
// steps, hundredths of a frame per second for time spans, phi2 cycles per frame.
// Tests: 1 the engine applies the stream 0 records (when the bitstream has APPLY), 0 the CPU
// walks them (the older way). Returns whether APPLY is in use after the call.
bool     trx_ring_apply_hw(int mode);
int      trx_ring_fps(void);
uint32_t trx_ring_fps_x100(void);
uint32_t trx_ring_frame_cycles(void);

// The instruction trace (stream 3). present: the bitstream has the former (CAPS2 b6) and the
// ring runs. ctl: on 1/0 sets the trace gate, -1 asks; returns the gate or -1 when not there.
// read: records of the history by index (0 = oldest); writes: stream 0 records by physical
// index (a record's write_start); whowrote: newest first.
bool trx_ring_trace_present(void);
int  trx_ring_trace_ctl(int on);
bool trx_ring_trace_status(TrxTraceStatus *st);
int  trx_ring_trace_read(uint32_t first, uint32_t n, TrxTraceRec *out);
int  trx_ring_trace_writes(uint32_t start, uint32_t n, TrxWriteRec *out);
int  trx_ring_trace_whowrote(uint16_t addr, int limit, TrxTraceHit *hits, bool *exhausted);
// The observer log of the debug block (trx_ring.cc): the stream 1 records that came since the last call; max 0 moves the reader to the present.
int  trx_ring_log_read(uint64_t *out, int max, uint32_t *lost);

// Starts the log (restart: the timeline and the areas are emptied) with a
// capture every frame. Returns false without the engine.
bool trx_ring_start(void);
// Log off; the timeline stays (it can still be restored to until the next
// start, which empties it).
void trx_ring_stop(void);
void trx_ring_status(TrxRingStatus *st);
// A08: the text why the ring refuses while the REU is on, NULL when it does not
const char *trx_ring_reu_refusal(void);

// Reads the markers that came since the last call into the timeline.
void trx_ring_poll(void);

// The timeline: entry 0 is the oldest. Copies an entry out (false: no such).
int  trx_ring_count(void);
bool trx_ring_frame(int index, TrxFrame *out);
// The index of the newest entry at or before "frames back" from the newest
// that a restore can go to (a whole chip block, logs still reach it); -1 none.
int  trx_ring_target(int frames_back);

// The index of the entry with this seq (indices move when old entries are
// dropped, seqs do not); -1 when it is gone.
int  trx_ring_find(uint32_t seq);

// Puts the machine back on entry "index" and lets it run from there (the
// frames after it are cut). The machine may run or be paused (also in a
// freeze session). Returns 0, or a negative error (trx_ring_error()).
int  trx_ring_restore(int index, TrxRestoreStats *stats);
const char *trx_ring_error(int code);

// The state of entry "index" without touching the live machine's run: a short
// pause to read RAM and colour RAM, then the undo applied in software.
// ram: 65536 bytes (C64 RAM, $D800-$DBFF the RAM under the I/O), color: 1024
// (low nibbles). Returns 0 or a negative error.
// cart, when not NULL: 65536 bytes of the cartridge RAM window, read in the same pause and undone with the rest
int  trx_ring_materialize(int index, uint8_t *ram, uint8_t *color, uint8_t *cart = 0);
// Many entries at once (oldest first in idx[], n of them): one pause, the
// undo walked back from the head once. ram/color: n buffers each.
int  trx_ring_materialize_many(const int *idx, int n, uint8_t **ram, uint8_t **color, uint8_t **carts = 0);

// The freeze menu's deferred rewind: the menu chooses an entry, the restore
// happens when the menu closes (C64::unfreeze() asks).
void trx_ring_set_pending(int index);
bool trx_ring_pending(void);
// From C64::unfreeze() when trx_ring_pending(): restores and runs.
void trx_ring_unfreeze(void);

// Timewarp's scrub (TRX64-Ultimate docs/specs/T20-delta-ring.md §16.7), with
// the freeze menu up: puts the paused machine on entry "index" (from the entry
// it showed last, by the logged values), runs one frame so the still capture
// holds that frame's picture, and stops again. The first call ends the freeze
// session. From then on trx_ring_pending() is true: the unfreeze rewinds to
// the pending entry, or without one to the newest, from the scrub's state.
int  trx_ring_scrub_show(int index, TrxRestoreStats *stats);
bool trx_ring_scrubbing(void);

// The debugger's moves (T21 §4.10, trxmon): puts the machine on entry
// "index" and leaves it paused exactly on that frame's snap edge, the
// debugger holding it (page 3 hold, OWNER as it was), without cutting the
// timeline. The log stays on from there, so whatever the debugger runs is
// undone by the next land. The stop record says CYC_CMP (reason 7); the app
// sets IPC itself. Returns 0 or a negative error (trx_ring_error()).
int  trx_ring_land(int index, TrxRestoreStats *stats);
// The debugger runs on from where a land left the machine: the frames after
// the entry it stands in are cut, what ran since the land is that entry's,
// capture and the timeline go on. Nothing to do when nothing was landed.
void trx_ring_commit(void);
// The entry the last land put the machine on (its seq), 0 when none is open.
uint32_t trx_ring_landed(void);
// An entry a land or restore can go to (a whole block, the logs reach it).
bool trx_ring_reachable(int index);

// Loads a machine state that is not in the ring (a .c64re with our chip
// block): RAM, colour RAM and the block put in as a restore puts a ring entry,
// then the ring restarts. ram as above.
// The memories a loaded state brings with it (FW io/c64/trx_ring.h TrxLoadMem): ram and col always; the cartridge RAM only when the file carries it, and the
// cartridge image through rom_put, called while the machine stands (it writes the image piece by piece: a cartridge is megabytes). All go into DDR2 before
// the machine is let go.
struct TrxLoadMem {
    const uint8_t *ram;      // 65536 (C64 RAM, $D800-$DBFF the RAM under the I/O)
    const uint8_t *col;      // 1024, low nibbles
    const uint8_t *cart;     // 65536 of cartridge RAM, or NULL
    void (*rom_put)(void *ctx);   // puts the cartridge image into DDR2, or NULL
    void *rom_ctx;
};
// The cartridge types a .c64re can carry back whole (FW trx_ring.cc c_cart_rows: banking registers replayed, state only in the ROM window, the cartridge RAM
// and the EEPROM): true when type (C64_CARTRIDGE_TYPE) is on the list, name its row; the types that keep a 93C86. The writer refuses by the same list.
bool trx_ring_cart_state_ok(uint8_t type, const char **name);
bool trx_ring_cart_has_eeprom(uint8_t type);
// The turbo state a loaded file carries (the packed word of block_section), put in with the next load_state.
void trx_ring_set_turbo(uint32_t pack);
int  trx_ring_load_state(const TrxLoadMem *mem, const uint32_t *w, uint16_t x);

// The native ring copy (.trxring, FW trx_ring.h): the three log areas through the engine's mover (off and len multiples of 16), and the install of a saved
// ring. Every count in ents is rebased so that the newest entry's are 0; areas(ctx) runs with the log off and the counters at 0 and writes the records
// back with trx_ring_area_write(), returning 0 or a negative error of its own.
uint32_t trx_ring_area_size(int s);
uint32_t trx_ring_rec_size(int s);
void trx_ring_area_read(int s, uint32_t off, void *dst, uint32_t len);
void trx_ring_area_write(int s, uint32_t off, const void *src, uint32_t len);
typedef int (*trx_area_fn)(void *ctx);
int trx_ring_install(const TrxFrame *ents, int n, const TrxLoadMem *mem, trx_area_fn areas, void *ctx);

#else

// Other products: no frame ring (the REST verbs answer 503).
static inline bool trx_ring_apply_hw(int) { return false; }
static inline int  trx_ring_fps(void) { return 50; }
static inline uint32_t trx_ring_fps_x100(void) { return 5012; }
static inline uint32_t trx_ring_frame_cycles(void) { return 19656; }
static inline bool trx_ring_start(void) { return false; }
static inline bool trx_ring_trace_present(void) { return false; }
static inline int  trx_ring_trace_ctl(int) { return -1; }
static inline bool trx_ring_trace_status(TrxTraceStatus *st) { st->present = false; return false; }
static inline int  trx_ring_trace_read(uint32_t, uint32_t, TrxTraceRec *) { return 0; }
static inline int  trx_ring_log_read(uint64_t *, int, uint32_t *lost) { *lost = 0; return 0; }
static inline int  trx_ring_trace_writes(uint32_t, uint32_t, TrxWriteRec *) { return 0; }
static inline int  trx_ring_trace_whowrote(uint16_t, int, TrxTraceHit *, bool *e) { *e = true; return 0; }
static inline void trx_ring_stop(void) { }
static inline const char *trx_ring_reu_refusal(void) { return 0; }
static inline void trx_ring_status(TrxRingStatus *st) { st->present = false; }
static inline int  trx_ring_count(void) { return 0; }
static inline bool trx_ring_frame(int, TrxFrame *) { return false; }
static inline int  trx_ring_target(int) { return -1; }
static inline int  trx_ring_find(uint32_t) { return -1; }
static inline int  trx_ring_restore(int, TrxRestoreStats *) { return -1; }
static inline int  trx_ring_scrub_show(int, TrxRestoreStats *) { return -1; }
static inline void trx_ring_set_pending(int) { }
static inline const char *trx_ring_error(int) { return "no frame ring in this bitstream"; }
static inline bool trx_ring_pending(void) { return false; }
static inline bool trx_ring_scrubbing(void) { return false; }
static inline int  trx_ring_land(int, TrxRestoreStats *) { return -1; }
static inline void trx_ring_commit(void) { }
static inline uint32_t trx_ring_landed(void) { return 0; }
static inline bool trx_ring_reachable(int) { return false; }
static inline void trx_ring_unfreeze(void) { }

#endif

#endif
