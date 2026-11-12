/*
 * hw.h — the TRX64 Ultimate debug hardware as trxmon uses it.
 *
 * The register map is the bitstream's (TRX64-Ultimate designs/c64_basic:
 * trx_io.v page 0, trx_sys.v, trx_dbg.v page 3, c64_core.v state bus; T21
 * spec §5.1). hw_trx.c reaches it through the firmware's trx table; the host
 * tests link a fake (host/fake_hw.c) behind the same calls.
 */
#ifndef TRXMON_HW_H
#define TRXMON_HW_H

#include <stdint.h>

/* page 3 CMD */
#define CMD_PAUSE_CB    0x01
#define CMD_PAUSE_IB    0x02
#define CMD_RUN         0x03
#define CMD_STEP_INSTR  0x05
#define CMD_STEP_CYCLES 0x06
#define CMD_DRV_PAUSE   0x10
#define CMD_DRV_RUN     0x11
#define CMD_DRV_STEP    0x12
#define CMD_CLEAR_STOP  0x20

/* page 3 STATUS */
#define ST_SHUT      (1u << 0)
#define ST_DBG_STOP  (1u << 1) /* a debugger stop is in force: the signal that opens the window's D page without C64_STOP */
#define ST_AT_IB     (1u << 2)
#define ST_STOP_NEW  (1u << 3)
#define ST_HELD      (1u << 4)
#define ST_FW_STOP   (1u << 5)
#define ST_ALIGNING  (1u << 6) /* the hold is off, the gate waits for frame alignment */
#define ST_STEPPING  (1u << 7)
#define ST_REASON(s) (((s) >> 8) & 15)
#define ST_OWNER     (1u << 20)

/* stop REASON */
#define RS_PAUSE     1
#define RS_STEP      2
#define RS_BITMAP    3
#define RS_UNIT_ADDR 5
#define RS_UNIT_RAS  6
#define RS_CYC_CMP   7
#define RS_DRIVE     8
#define RS_DRV_STEP  9

/* watch unit CTRL */
#define U_ENABLE   0x0001
#define U_T_IB     (1u << 1)
#define U_T_READ   (2u << 1)
#define U_T_WRITE  (3u << 1)
#define U_T_RW     (4u << 1)
#define U_T_CYCLE  (5u << 1)
#define U_RASTER   (1u << 4)
#define U_A_COUNT  (1u << 5)
#define U_A_LOG    (2u << 5)
#define U_ONESHOT  (1u << 8)
#define U_CHAIN    (1u << 9)
#define U_PREV_RET (1u << 14)
/* REG qualifier */
#define R_SEL_A 1
#define R_SEL_X 2
#define R_SEL_Y 3
#define R_SEL_S 4
#define R_SEL_P 5
#define R_OP_EQ 0
#define R_OP_NE 1
#define R_OP_LT 2
#define R_OP_GE 3
#define UNIT_REG(sel, op, val, mask) \
    ((uint32_t)(sel) | ((uint32_t)(op) << 8) | ((uint32_t)(val) << 16) | ((uint32_t)(mask) << 24))

typedef struct
{
    uint8_t a, x, y, s, p;
    uint16_t pc;  /* the CPU's PC register */
    uint16_t ipc; /* address of the last opcode fetch (= the reported PC at an IB) */
    uint8_t op;   /* the opcode at ipc */
    uint8_t at_ib, sync;
    uint8_t ddr, latch, port; /* $00, $01 latch, $01 as read */
    uint8_t settled;          /* a..p are the registers after the pending update (an IB, CPU write path) */
} hw_cpu_t;

/* A, X, Y, S, P */
typedef struct
{
    uint8_t a, x, y, s, p;
} hw_regs_t;

typedef struct
{
    uint8_t reason, flags, unit, data;
    uint16_t pos; /* {line 9, cycle 7} */
    uint16_t addr, ipc;
} hw_stop_t;

/* Set up from the firmware's trx table; 0 when the debug page is not there. */
int hw_init(const void *trx_api);
uint32_t hw_caps(void);

uint32_t hw_status(void);
void hw_cmd(uint8_t cmd, uint16_t arg);
void hw_owner(int on);
uint64_t hw_cycles(void);
uint32_t hw_instrs(void);
void hw_cyc_cmp(uint32_t at); /* arms; stops on the edge that ends cycle `at` */
void hw_cyc_cmp_off(void);
void hw_stop_record(hw_stop_t *r);
/* At a stop on an IB the CPU's update for the opcode fetch is still to come
 * (9 clocks after the phi2 fall the gate stopped on): it writes back the
 * previous instruction's A/X/Y/S/P. With the CPU write path (CAPS2 b2) hw_cpu
 * gives the registers after it (settled = 1), otherwise as they stand. */
void hw_cpu(hw_cpu_t *c);

/* The CPU write path (T21 §3.3/§5.1, CAPS2 b2): registers and a redirect at
 * an IB, while the gate is shut. The CPU's hidden words are staged with A X Y
 * S P = r (the caller merges what it writes into the settled values), the
 * pending write-back taken out, and with pc >= 0 the fetch moved to pc whose
 * opcode is op; they load at the next opening edge, so nothing costs a cycle.
 * IPC, its opcode and the IB flag follow at once. 0, or -1 when refused. */
int hw_has_cpu_write(void);
int hw_cpu_write(const hw_regs_t *r, int pc, uint8_t op);
int hw_cpu_write_pending(void); /* the staged load has not happened yet */
void hw_ipc_set(uint16_t ipc, uint8_t op, int at_ib);
/* The hidden CPU words (chip 1 w8-w14): fetch state, interrupt decided, PC
 * register and the byte the last fetch read */
void hw_cpu_fetch(int *in_fetch, int *intg, uint16_t *pc, uint8_t *din);
uint32_t hw_sb(int chip, int word);
void hw_sb_block(int chip, int word, uint32_t *dst, int n);

void hw_bmp_word(int idx, uint32_t bits);
void hw_bmp_enable(int on);
void hw_unit(int n, uint32_t ctrl, uint16_t lo, uint16_t hi, uint32_t data, uint32_t reg, uint16_t pass);
void hw_unit_off(int n);
uint32_t hw_hits(void); /* {unit 1, unit 0} */
uint32_t hw_drv_stat(void);
/* Drive A (drv_dbg.v; its 64 K image in DDR2 at 0x0EE0000, drive_unit.vhd
 * g_ram_base, written through by the drive's CPU): a byte as the drive's
 * CPU reads it, -1 for the VIAs ($1800-$1FFF, not on the state bus) */
int hw_drive_peek(uint16_t a);
uint32_t hw_drv_cycles(void);
/* the drive comparator: exec at an IB (0 off) */
void hw_drv_cmp(uint16_t a, int on);

/* Memories, all without a bus cycle */
void hw_ram_read(uint16_t a, uint8_t *d, int n);
void hw_ram_write(uint16_t a, const uint8_t *s, int n);
uint8_t hw_rom(uint16_t off); /* the ROM block: char 0x0000, KERNAL 0x2000, BASIC 0x4000 */

/* The lens unit (T21 §3.4.1, CAPS2 b4; LK_REQ/LK_RES, trx_lens.h): a look at the
 * cartridge (real 0), or one bus cycle with the select forced (real 1), at address
 * a as class cls (0 ROML, 1 ROMH, 2 IO1/I-O, 3 IO2/I-O). Returns LK_RES, 0 when
 * the unit did not answer. */
int hw_has_lk(void);
uint32_t hw_lk(uint16_t a, int cls, int real);

/* One C64 bus cycle through the window (I/O writes, and sidefx reads on a bitstream
 * without the lens unit). Only while the gate is shut and the machine is held: by the
 * menu's C64_STOP or by a debugger stop (ST_DBG_STOP; ultimate_sys opens the D page for
 * either). hw_bus_held() says whether it is. */
int hw_bus_held(void);
uint8_t hw_bus_read(uint16_t a);
int hw_bus_read_held(uint16_t a); /* -1 when not held */
/* The colour RAM through the shadow session's window (a debugger stop is a session, T12 §14.8); no
 * bus cycle, no C64_STOP needed. -1 without a session or on a bitstream that has none. */
int hw_session_read(uint16_t a);
void hw_bus_write(uint16_t a, uint8_t v);

/* The T20 frame ring through the firmware (trx->ring; T21 §4.10) */
typedef struct
{
    int present, on, landed;
    int reu_off; /* A08: the REU is enabled: no ring, no Timewarp */
    int frames, targets;
    uint32_t head_seq, oldest_seq, landed_seq;
} hw_ring_status_t;

typedef struct
{
    uint32_t seq;
    uint32_t c24;    /* CYCLES after the snap edge, low 24 bits */
    uint64_t cycles; /* phi2 cycles since the ring started */
    uint16_t line, x;
    uint8_t flags;
    uint8_t target;  /* a land can go there */
} hw_ring_frame_t;

int hw_ring_present(void);
int hw_ring_status(hw_ring_status_t *st);
int hw_ring_frame(int index, hw_ring_frame_t *f); /* 0, or < 0 when there is none */
int hw_ring_find(uint32_t seq);
/* The machine onto entry index, paused on its snap edge; 0 or an error text */
const char *hw_ring_land(int index);
void hw_ring_commit(void);

/* The video System Mode that runs now (T40; trx->video, the firmware reads it on each call): 50 Hz
 * modes are 63 x 312 = 19656 cycles at 985248 Hz, 60 Hz modes 65 x 263 = 17095 at 1022727 Hz.
 * PAL without the call (an older firmware). */
typedef struct
{
    uint16_t lines, cycles_per_line;
    uint32_t frame_cycles, cpu_hz, fps_x100;
    int hz60;
} hw_video_t;

void hw_video(hw_video_t *v);

/* The instruction trace (T21 §4.11, trx->ring trace_*): TRX64's DeltaEntry */
typedef struct
{
    uint64_t cycle; /* the T20 timeline's count after the edge that ended the opcode fetch */
    uint32_t write_start;
    uint16_t pc, write_count;
    uint8_t a, x, y, sp, p, opcode, b1, b2; /* the registers and PC BEFORE the instruction */
} hw_trace_rec_t;

typedef struct
{
    uint16_t addr;
    uint8_t old_value, new_value;
} hw_trace_write_t;

typedef struct
{
    uint64_t cycle;
    uint16_t addr, pc;
    uint8_t old_value, new_value;
    uint16_t flags; /* HW_HIT_*: values the trace cannot give (writes of I/O addresses) */
    uint32_t index; /* of the writing instruction in the history */
} hw_trace_hit_t;
#define HW_HIT_OLD_UNKNOWN 1
#define HW_HIT_NEW_UNKNOWN 2
/* hw_trace_whowrote's *exhausted: bit 0 the scan reached the start of the history; bit 1 an I/O address was
 * scanned from the instruction records and writes through a zero page pointer could not be checked */
#define HW_WHO_EXHAUSTED 1
#define HW_WHO_UNRESOLVED 2

typedef struct
{
    int present, on, gap;
    uint32_t records, capacity, lost;
    uint64_t oldest_cycle, newest_cycle;
} hw_trace_status_t;

int hw_trace_present(void);
int hw_trace_status(hw_trace_status_t *st);
int hw_trace_ctl(int on); /* 1/0 sets the gate, -1 asks; the gate, or < 0 */
int hw_trace_read(uint32_t first, uint32_t n, hw_trace_rec_t *out);
int hw_trace_whowrote(uint16_t addr, int limit, hw_trace_hit_t *hits, int *exhausted);

/* The debug block's log action (watch unit action LOG, trx_dbg.v; ring->log_read, T55): a record per hit while the
 * frame ring runs, without a stop */
typedef struct
{
    uint16_t addr, ipc;
    uint8_t data;
    uint8_t unit;  /* the watch unit that logged it */
    uint8_t write; /* the access was a write */
    uint8_t ib;    /* the hit was an instruction fetch (type IB) */
    uint16_t cyc15; /* CYCLES low 15 bits, before the edge */
} hw_logrec_t;

int hw_log_present(void);
/* Records since the last call; max 0 moves the reader to the present. *lost: records the area overwrote. */
int hw_log_read(hw_logrec_t *out, int max, uint32_t *lost);

/* Firmware services (the trx table) */
int hw_acquire(void);
void hw_release(void);
void hw_announce(uint16_t port);
/* Runs fn(arg) in a task of its own after app_main returned (trx->detach) */
int hw_detach(void (*fn)(void *), void *arg);
int hw_stop_requested(void); /* the loader wants the detached task to end */
/* trx->ui_request: the firmware opens the overlay and calls fn(arg) in the UI task with a valid ui
 * table (T21 §8.5). U2P_OK queued, U2P_ERR_BUSY one is queued, U2P_ERR_UNSUPPORTED no such call. */
int hw_ui_request(void (*fn)(void *), void *arg);

#endif
