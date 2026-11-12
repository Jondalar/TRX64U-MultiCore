/*
 * mach.h — the machine as the RPC methods and the monitor verbs see it:
 * ownership, run control, stops, breakpoints, observers, memory lenses.
 */
#ifndef TRXMON_MACH_H
#define TRXMON_MACH_H

#include "cond.h"
#include "hw.h"
#include "mlib.h"

#define MAX_BP  64
#define MAX_OBS 8

/* OBS_RASTER (`rb`, `obs .. when raster`): lo/hi are the raster position {line 9, cycle 7} the unit compares */
enum { OBS_EXEC = 0, OBS_LOAD, OBS_STORE, OBS_RASTER };
enum { OA_BREAK = 0, OA_LOG, OA_CMD };
#define MAX_LOGF 6

typedef struct
{
    uint32_t num;
    uint16_t pc;
    uint8_t enabled;
} bp_t;

/* A `do log` field: a register (reg: a x y s p f = sp pc fl) or the byte / little-endian word at addr */
typedef struct
{
    uint8_t mem, word;
    char reg;
    uint16_t addr;
} logf_t;

typedef struct
{
    char name[24];
    uint8_t trig; /* OBS_* */
    uint16_t lo, hi;
    uint8_t on;
    int8_t unit; /* watch unit while armed (load/store/raster, an exec observer that logs in hardware), -1 */
    uint32_t hits;
    /* T55: condition and action */
    uint8_t act;           /* OA_* */
    char cond_src[56];     /* the `if` text */
    cond_t cond;
    cond_hw_t q;           /* what the unit holds of it; q.rest is checked after the stop */
    uint8_t hw_log;        /* the log runs in the unit (a record into the ring, no stop) */
    uint8_t nlogf;
    logf_t logf[MAX_LOGF];
    char cmd[64];          /* OA_CMD: the monitor command(s), separated by ';' */
    uint32_t ignore;       /* software: hits still to skip */
    uint16_t pass;         /* hardware: PASS programmed */
    uint16_t thin;         /* software log / cmd on a unit: hits the unit passes between two stops (the live rate) */
    uint32_t last_ms;      /* ... when it stopped last (wall clock), */
    uint64_t last_cyc;     /* ... and at which machine cycle */
} obs_t;

/* The interrupt flow (TRX64 CpuFlowFrame, T55): the frames the stepping verbs saw the CPU enter */
enum { FL_MAIN = 0, FL_IRQ, FL_NMI, FL_BRK };
#define MAX_FLOW 8

typedef struct
{
    uint8_t kind; /* FL_IRQ, FL_NMI, FL_BRK */
    uint16_t pc;  /* the handler's first instruction */
    uint16_t ret; /* the PC before the step that entered it */
    uint64_t cyc;
} flow_t;

/* What a stop was, in the contract's words */
typedef struct
{
    const char *reason; /* "pause" "step" "breakpoint" "observer" "cycles" "until" "next" "return" "drive" "jam" */
    uint16_t pc;
    uint64_t cycles;
    uint32_t instrs;
    int at_ib;
    int bp_num;      /* the numbered breakpoint at pc, 0 when none */
    int obs;         /* the observer that stopped, -1 */
    hw_stop_t rec;
} stop_t;

typedef struct
{
    int present;
    int owned;
    int running;
    int have_stop;
    stop_t stop;
    uint32_t frame; /* bumped at every run-state change, as TRX64's ctrl_frame */
    bp_t bp[MAX_BP];
    int nbp;
    uint32_t next_bp;
    obs_t obs[MAX_OBS];
    int nobs;
    int transient; /* the watch units are lent to until/next/ret */
    char lens[8];  /* sticky lens for m/d */
    int sidefx;
    const char *owner; /* controlOwner: "human" or "llm" */
    uint16_t disasm_cursor, mem_cursor;
    int disasm_cursor_set, mem_cursor_set;
    int64_t cyc_off; /* reported cycles - the debug block's CYCLES: a ring land moves time back */
    int device;      /* 0 the C64, 8 drive A (`device drive8`) */
    int jam_ref;     /* a reference for the jam watch is taken: the instruction count and cycles at the last progress */
    uint32_t jam_ins;
    uint64_t jam_cyc;
    flow_t flow[MAX_FLOW];
    int nflow;
    char focus[8];   /* "auto" "main" "irq" "nmi" "brk" "none" */
    int drv_bp_on;   /* the drive comparator: one exec breakpoint */
    uint16_t drv_bp;
} mach_t;

extern mach_t M;

/* Platform hooks (main.c on the target, host/fake_hw.c in the tests) */
uint32_t plat_ms(void);
void plat_yield(void); /* let tasks of the same priority run */
/* Sleep ms (a real sleep: tasks of lower priority run, the serve loop among them). Never shorter than
 * PLAT_SLEEP_MIN_MS, whatever the firmware's delay_ms rounds to. */
#define PLAT_SLEEP_MIN_MS 10
void plat_sleep(uint32_t ms);
/* Waits until *flag == want. Returns 1 then; 0 when *alive (if given) is 0 or timeout_ms (0: none) is
 * over first. Sleeps PLAT_SLEEP_MIN_MS between looks, never yields in a loop: the task that sets the flag
 * (the serve loop) runs below the UI task's priority and gets the CPU only while this one sleeps. */
int plat_wait_flag(const volatile int *flag, int want, const volatile int *alive, uint32_t timeout_ms);
/* Files through the firmware's fs table (NULL / < 0 without one) */
void *plat_fopen(const char *path, const char *mode);
int plat_fread(void *buf, int n, void *f);
int plat_fwrite(const void *buf, int n, void *f);
void plat_fclose(void *f);
int plat_mkdir(const char *path);
int plat_exists(const char *dir, const char *name);
/* The entries of a directory (fs->dir, T55): cb per entry, non-zero stops. 0, or < 0 (no such directory, or the
 * firmware has no listing). rmdir removes an empty directory; 0 or < 0. */
/* A setting of the firmware (config->get, text); the length, or < 0 */
int plat_config(const char *category, const char *item, char *buf, int len);
typedef int (*plat_dir_cb)(void *ctx, const char *name, uint32_t size, int is_dir);
int plat_dir(const char *path, plat_dir_cb cb, void *ctx);
int plat_rmdir(const char *path);

void mach_init(void);
int mach_own(void);
/* The machine's cycle count as reported (c64Cycles): the debug block's
 * CYCLES, set back by ring lands (ring.c) */
uint64_t mach_cycles(void);
int mach_running(void);
/* Pause at the next instruction boundary (a cycle boundary if none comes).
 * Fills M.stop. Returns 1 when the machine had been running. */
int mach_pause(void);
void mach_run(void);
/* One instruction, and the flow tracker told what it was: an interrupt taken (the stack went down by three and
 * the PC is a vector's target), BRK, RTI. 0 on success, -1 when the machine did not stop. */
int mach_step_flow(void);
int mach_flow_current(void);
const char *mach_flow_tag(int kind);
void mach_flow_reset(void);
/* Steps n instructions (cycles = 0) or n cycles; blocking. 0 on success. */
int mach_step(int n, int cycles);
/* A stop that came while running: fills M.stop and returns 1. 2: the
 * firmware took run control back (it stopped the C64 for itself). */
int mach_poll(void);
void mach_read_stop(void);

/* Breakpoints (the exec bitmap) */
int mach_bp_add(uint16_t pc);       /* returns its number */
int mach_bp_del_num(uint32_t num);  /* 1 if one went */
int mach_bp_del_addr(uint16_t pc);  /* how many went */
void mach_bp_clear(void);
int mach_bp_at(uint16_t pc);        /* number of an enabled one at pc, 0 */

/* Observers. mach_obs_add takes name, trig, lo, hi, cond_src, act, logf/nlogf and cmd of `proto` (the rest is
 * computed); NULL or why not. */
int mach_obs_find(const char *name);
const char *mach_obs_add(const obs_t *proto);
void mach_obs_del(int i);
const char *mach_obs_set(int i, int on);
/* `ignore`: skip the next n hits. Returns "hw" (PASS in the unit), "soft" (counted after each stop) */
const char *mach_obs_ignore(int i, uint32_t n);
/* hits as the list shows them: the stops that fired, for a hardware log the unit's hit count less PASS */
uint32_t mach_obs_hits(const obs_t *o);
/* The log of `do log` lines and `do cmd` output (obs log): the newest MACH_OLOG lines, oldest first */
#define MACH_OLOG 40
int mach_olog_count(void);
const char *mach_olog_line(int i);
void mach_olog_push(const char *line);
/* The live view of the same lines: every line has a number (mach_olog_seq counts them); a reader keeps its own
 * position, mach_olog_next gives the line at *cur and moves it on (NULL when it is caught up; a reader that fell
 * behind the ring jumps to the oldest line left). The hits are metered (a rate, a burst, "+K skipped" lines). */
uint32_t mach_olog_seq(void);
const char *mach_olog_next(uint32_t *cur);
/* A log or cmd observer is on: the screen keeps more output rows while the machine runs */
int mach_live_log_active(void);
/* Reads what the hardware logged (units with the LOG action) into the log; cheap when nothing is armed */
void mach_olog_drain(void);
/* Runs the monitor command(s) of a `do cmd` observer at its stop (set by mon.c) */
extern void (*mach_cmd_hook)(const obs_t *o);

/* Watch units lent to a run-to (until, next, ret); given back by mach_poll /
 * mach_pause when the stop comes. */
void mach_arm_until(uint16_t addr);
void mach_arm_next(uint16_t ret_pc, uint8_t s0);
void mach_arm_ret(uint8_t s0);
void mach_disarm_transient(void);
void mach_rearm_observers(void);

/* CPU */
/* The registers as the monitor reports them: at an IB with the previous
 * instruction's write-back done, with a write not yet loaded shown as made */
void mach_cpu(hw_cpu_t *c);
/* Register writes and a redirect (T21 §3.3) at an IB of the paused machine:
 * mask of W_* with the values in r, and with W_PC the new PC. They take no
 * cycle: the CPU takes them on the next edge the gate lets through. NULL, or
 * why not. */
#define W_A  0x01
#define W_X  0x02
#define W_Y  0x04
#define W_S  0x08
#define W_P  0x10
#define W_PC 0x20
const char *mach_cpu_write(int mask, const hw_regs_t *r, uint16_t pc);
uint16_t mach_pc(const hw_cpu_t *c); /* the PC the contract reports: IPC */

/* Memory through a lens: "cpu" "ram" "rom" "io" "cart". A byte the lens
 * cannot reach without a bus cycle reads as -1 (io: SID, colour RAM,
 * IO1/IO2) unless sidefx is on and the machine is held. */
void mach_snapshot(void); /* takes the state-bus words the lenses need, once per request */
int mach_peek(const char *lens, uint16_t a, int sidefx);
uint8_t mach_peek8(const char *lens, uint16_t a);
/* Writes; 0 on success, else a reason */
const char *mach_write(const char *lens, uint16_t a, const uint8_t *b, int n);
int mach_valid_lens(const char *l);
/* Drive A: steps its CPU alone (the C64 stays paused); 0 on success */
int mach_drv_step(int n);

/* Banking as the machine has it now */
uint8_t mach_port(void); /* $01 as read */
uint8_t mach_ddr(void);
uint8_t mach_latch(void);
int mach_game(void);  /* 1 = high, not asserted */
int mach_exrom(void);
int mach_cart_present(void);
int mach_cart_bank(void);
uint16_t mach_vic_bank(void);
uint8_t mach_vic_reg(int r);
uint16_t mach_raster_line(void);

#endif
