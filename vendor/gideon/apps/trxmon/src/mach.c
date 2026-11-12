/*
 * mach.c — run control, stops, breakpoints, observers and memory lenses over
 * the debug hardware (see mach.h).
 *
 * Stops (trx_dbg.v): the hardware closes the gate on the edge it sees and
 * keeps a stop record until CLEAR_STOP. An instruction boundary (IB) is the
 * edge that ends an opcode fetch: the opcode at IPC is in the CPU and nothing
 * else of it has run, so IPC is the PC a debugger reports, and resuming runs
 * that instruction without fetching it again (a breakpoint does not trip
 * twice).
 */
#include "mach.h"
#include "ring.h"
#include "trx_lens.h"

mach_t M;

static uint32_t s_bmp[2048]; /* the exec bitmap as written */
static const char *s_transient_reason;
/* the run-to in force (mach_read_stop gives the units back at every stop; a stop an observer lets go needs them again) */
static struct
{
    int kind; /* 0 none, 1 until, 2 next, 3 ret */
    uint16_t addr;
    uint8_t s0;
} s_tr;

/* The memory lenses are software/io/c64/trx_lens.c, the firmware's code too
 * (REST machine:readmem); this is the hardware under them (hw.h). */
static lens_hw_t s_lens = {hw_sb_block, hw_ram_read, hw_rom, hw_drive_peek, hw_lk, hw_bus_read_held, hw_session_read};

/* State-bus words the lenses need, taken once per request */
void mach_snapshot(void)
{
    s_lens.lk = hw_has_lk() ? hw_lk : 0;
    lens_snapshot(&s_lens);
}

uint8_t mach_port(void) { return lens_state()->port; }
uint8_t mach_ddr(void) { return lens_state()->ddr; }
uint8_t mach_latch(void) { return lens_state()->latch; }
int mach_game(void) { return lens_state()->game; }
int mach_exrom(void) { return lens_state()->exrom; }
int mach_cart_present(void) { return lens_state()->cart; }
/* icepi's cartridge model register 0; what it means depends on the type */
int mach_cart_bank(void) { return lens_state()->cart_reg0 & 0x7F; }
uint8_t mach_vic_reg(int r) { return ((r & 0x3F) < 0x30) ? lens_state()->vic[r & 0x3F] : 0xFF; }
uint16_t mach_raster_line(void)
{
    const lens_state_t *S = lens_state();
    return (uint16_t)(S->vic[0x12] | ((S->vic[0x11] & 0x80) << 1));
}

uint16_t mach_vic_bank(void)
{
    const lens_state_t *S = lens_state();
    uint8_t pins = (uint8_t)((S->pa_out & S->ddra) | (uint8_t)~S->ddra);
    return (uint16_t)((3 - (pins & 3)) << 14);
}

void mach_init(void)
{
    memset(&M, 0, sizeof(M));
    memcpy(M.lens, "cpu", 4);
    memcpy(M.focus, "auto", 5);
    M.owner = "human";
    M.next_bp = 1;
    M.present = 1;
    memset(s_bmp, 0, sizeof(s_bmp));
    mach_snapshot();
}

int mach_running(void) { return M.running; }

uint64_t mach_cycles(void) { return (uint64_t)((int64_t)hw_cycles() + M.cyc_off); }

int plat_wait_flag(const volatile int *flag, int want, const volatile int *alive, uint32_t timeout_ms)
{
    uint32_t t0 = plat_ms();
    while (*flag != want) {
        if (alive && !*alive) {
            return 0;
        }
        if (timeout_ms && ((uint32_t)(plat_ms() - t0) >= timeout_ms)) {
            return 0;
        }
        plat_sleep(PLAT_SLEEP_MIN_MS);
    }
    return 1;
}

/* trxmon runs above the firmware's network tasks (main.c, duty_sleep). A wait that only yields keeps them off the
 * processor for its whole timeout, and a replay chains thousands of such waits: the network then starves for
 * seconds, the WiFi UART runs out of receive buffers and a reply the firmware waits for without a timeout is lost
 * (T77: the whole network stayed dead until a power cycle). So every BREATHE_MS of waiting, sleep one
 * PLAT_SLEEP_MIN_MS: lower tasks get the processor, the wait goes on afterwards. A gap of more than
 * BREATHE_MS + PLAT_SLEEP_MIN_MS between waits starts a new window (the task did other things meanwhile). */
#define BREATHE_MS 20u

static uint32_t s_breath_ms;

static void breathe(void)
{
    uint32_t now = plat_ms();
    if ((uint32_t)(now - s_breath_ms) >= BREATHE_MS + PLAT_SLEEP_MIN_MS) {
        s_breath_ms = now;  /* first wait after a pause in waiting: start a new window */
    } else if ((uint32_t)(now - s_breath_ms) >= BREATHE_MS) {
        plat_sleep(PLAT_SLEEP_MIN_MS);
        s_breath_ms = plat_ms();
    }
}

static int wait_status(uint32_t want_set, uint32_t want_clear, uint32_t ms)
{
    breathe();  /* also between the short waits a replay chains */
    uint32_t t0 = plat_ms();
    for (int n = 0;; n++) {
        if (n >= 64) {
            plat_yield();
            breathe();
        }
        uint32_t st = hw_status();
        if (((st & want_set) == want_set) && !(st & want_clear)) {
            return 1;
        }
        if ((uint32_t)(plat_ms() - t0) > ms) {
            return 0;
        }
    }
}

static int bp_index_at(uint16_t pc)
{
    for (int i = 0; i < M.nbp; i++) {
        if (M.bp[i].enabled && (M.bp[i].pc == pc)) {
            return i;
        }
    }
    return -1;
}

int mach_bp_at(uint16_t pc)
{
    int i = bp_index_at(pc);
    return (i < 0) ? 0 : (int)M.bp[i].num;
}

void mach_read_stop(void)
{
    stop_t *s = &M.stop;
    hw_stop_record(&s->rec);
    s->cycles = mach_cycles();
    s->instrs = hw_instrs();
    hw_cmd(CMD_CLEAR_STOP, 0);
    hw_cyc_cmp_off();
    s->pc = s->rec.ipc;
    s->at_ib = s->rec.flags & 1;
    s->bp_num = mach_bp_at(s->pc);
    s->obs = -1;
    switch (s->rec.reason) {
    case RS_PAUSE: s->reason = "pause"; break;
    case RS_STEP: s->reason = "step"; break;
    case RS_BITMAP:
        s->reason = "breakpoint";
        if (!s->bp_num) {
            for (int i = 0; i < M.nobs; i++) {
                obs_t *o = &M.obs[i];
                if (o->on && (o->trig == OBS_EXEC) && (o->unit < 0) && (s->pc >= o->lo) && (s->pc <= o->hi)) {
                    s->reason = "observer";
                    s->obs = i;
                    break;
                }
            }
        }
        break;
    case RS_UNIT_ADDR:
    case RS_UNIT_RAS:
        if (M.transient) {
            s->reason = s_transient_reason;
        } else {
            s->reason = "observer";
            for (int i = 0; i < M.nobs; i++) {
                if (M.obs[i].on && (M.obs[i].unit == s->rec.unit)) {
                    s->obs = i;
                    break;
                }
            }
        }
        break;
    case RS_CYC_CMP: s->reason = "cycles"; break;
    case RS_DRIVE: s->reason = "drive"; break;
    case RS_DRV_STEP: s->reason = "drive-step"; break;
    default: s->reason = "stop"; break;
    }
    if (M.transient) {
        mach_disarm_transient();
    }
    M.have_stop = 1;
    M.running = 0;
    M.disasm_cursor_set = 0;
    mach_snapshot();
}

static int stop_is_jam(void);
static void mark_jam(void);

int mach_own(void)
{
    if (M.owned) {
        return 1;
    }
    /* A machine that runs (the menu is closed) goes on running: taking run
     * control holds it for a moment (PAUSE_CB), then RUN lets it go again,
     * unless the caller pauses it next. */
    int was_running = !(hw_status() & ST_SHUT);
    if (hw_acquire() < 0) {
        return 0;
    }
    M.owned = 1;
    M.running = 0;
    wait_status(ST_STOP_NEW, 0, 50);
    if (hw_status() & ST_STOP_NEW) {
        mach_read_stop(); /* the hold's record, so the next stop is recorded */
        if (stop_is_jam()) {
            /* a CPU the menu's freeze stopped while it was stuck: no boundary came, the opcode at its address is a
             * JAM (the attach path asks the same question as every other stop) */
            mark_jam();
        }
    }
    if (was_running) {
        hw_cmd(CMD_RUN, 0);
        M.running = 1;
        M.jam_ref = 0;
        M.have_stop = 0;
    }
    return 1;
}

/* The JAM opcodes ($02 $12 ... $F2): the 6502N stays in its decode state for ever, no instruction boundary
 * comes again and no interrupt is taken. The debug block has no stop reason for it (T21 Q7); it is seen from
 * outside. */
static int is_jam_op(uint8_t op)
{
    return ((op & 0x0F) == 0x02) && ((op >> 4) != 0x8) && ((op >> 4) != 0xA) && ((op >> 4) != 0xC) && ((op >> 4) != 0xE);
}

/* The machine runs but no instruction has started for JAM_CYCLES cycles (about 0.1 s, 5 frames), and it is not
 * held by a DMA, the firmware or the frame alignment: the CPU is stuck. Watched on every poll while running. */
#define JAM_CYCLES 100000u

static uint32_t s_jam_quiet; /* a verdict that was no JAM: not asked again before this time (a CPU the hardware stalls for long) */

static int jam_watch(uint32_t st)
{
    if (s_jam_quiet && ((int32_t)(plat_ms() - s_jam_quiet) < 0)) {
        M.jam_ref = 0;
        return 0;
    }
    if (st & (ST_HELD | ST_ALIGNING | ST_SHUT)) {
        M.jam_ref = 0;
        return 0;
    }
    uint32_t ins = hw_instrs();
    uint64_t cyc = hw_cycles();
    if (!M.jam_ref || (ins != M.jam_ins) || (cyc < M.jam_cyc)) {
        M.jam_ref = 1;
        M.jam_ins = ins;
        M.jam_cyc = cyc;
        return 0;
    }
    return (cyc - M.jam_cyc) >= JAM_CYCLES;
}

/* The stop just read is a stuck CPU: the opcode at IPC is a JAM and the stop is not on its instruction
 * boundary (the hardware stopped at the next cycle because no boundary came). At the boundary of the JAM
 * the CPU has not run it yet. */
static int stop_is_jam(void)
{
    hw_cpu_t c;
    hw_cpu(&c);
    return !M.stop.at_ib && is_jam_op(c.op);
}

static void mark_jam(void)
{
    M.stop.reason = "jam";
    M.stop.bp_num = 0;
    M.stop.obs = -1;
}

static int obs_dispatch(int was_transient);

/* Runs on after a stop no observer wanted to keep (a false condition, an ignored hit, a log or cmd action): the
 * stop was read and cleared already, so only RUN is left; nothing the people watching it would see changes. */
static void resume_quiet(void)
{
    hw_cmd(CMD_RUN, 0);
    M.running = 1;
    M.have_stop = 0;
    M.jam_ref = 0;
}

int mach_poll(void)
{
    mach_olog_drain();
    uint32_t st = hw_status();
    if (M.owned && !(st & ST_OWNER)) {
        /* the firmware stopped the C64 for itself (menu, DMA load, reset) and
         * took run control back (trx_dbg_yield); it lets the machine go on */
        M.owned = 0;
        M.running = 1;
        M.jam_ref = 0;
        M.have_stop = 0;
        M.frame++;
        ring_before_run();
        if (M.transient) {
            mach_disarm_transient();
        }
        M.stop.reason = "firmware";
        M.stop.pc = M.stop.rec.ipc;
        M.stop.cycles = mach_cycles();
        M.stop.instrs = hw_instrs();
        return 2;
    }
    if (!M.running) {
        return 0;
    }
    if (st & ST_STOP_NEW) {
        int was_transient = M.transient;
        mach_read_stop();
        if (!obs_dispatch(was_transient)) {
            if (was_transient) {
                /* an observer's stop inside a run-to that goes on: the units come back */
                if (s_tr.kind == 1) {
                    mach_arm_until(s_tr.addr);
                } else if (s_tr.kind == 2) {
                    mach_arm_next(s_tr.addr, s_tr.s0);
                } else if (s_tr.kind == 3) {
                    mach_arm_ret(s_tr.s0);
                }
            }
            resume_quiet();
            return 0;
        }
        M.frame++;
        return 1;
    }
    if (jam_watch(st)) {
        /* stuck: hold it where it is (no boundary comes, so at the next cycle) and say what it is */
        M.jam_ref = 0;
        mach_pause();
        if (stop_is_jam()) {
            mark_jam();
        } else {
            /* not a JAM (a long wait on something): the machine goes on, looked at again later - each verdict
             * costs two bounded waits, so not before two seconds have passed */
            s_jam_quiet = plat_ms() + 2000;
            mach_run();
            return 0;
        }
        return 1;
    }
    return 0;
}

int mach_pause(void)
{
    if (!mach_own()) {
        return 0;
    }
    if (!M.running) {
        return 0;
    }
    int no_boundary = 0;
    hw_cmd(CMD_PAUSE_IB, 0);
    if (!wait_status(ST_STOP_NEW | ST_SHUT, 0, 50)) {
        /* no instruction boundary came (the CPU is held, or jammed): stop at
         * the next cycle */
        hw_cmd(CMD_PAUSE_CB, 0);
        wait_status(ST_STOP_NEW | ST_SHUT, 0, 50);
        no_boundary = 1;
    }
    mach_read_stop();
    if (no_boundary && stop_is_jam()) {
        mark_jam(); /* RUN/STOP on a stuck CPU: not "pause" at the JAM's address */
    }
    M.frame++;
    return 1;
}

void mach_run(void)
{
    if (!mach_own() || M.running) {
        return;
    }
    ring_before_run(); /* a run from a land cuts the frames after it */
    if (!M.transient) {
        mach_flow_reset(); /* a free run: the interrupts it took are not known (a run-to keeps what it knows) */
    }
    hw_cmd(CMD_RUN, 0);
    M.running = 1;
    M.jam_ref = 0;
    M.have_stop = 0;
    M.frame++;
}

int mach_step(int n, int cycles)
{
    if (!mach_own()) {
        return -1;
    }
    if (M.running) {
        mach_pause();
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 0xFFFF) {
        n = 0xFFFF;
    }
    hw_cmd(cycles ? CMD_STEP_CYCLES : CMD_STEP_INSTR, (uint16_t)n);
    int ok = wait_status(ST_STOP_NEW, ST_STEPPING, 2000);
    if (!ok) {
        hw_cmd(CMD_PAUSE_CB, 0);
        wait_status(ST_STOP_NEW, ST_STEPPING, 50);
    }
    mach_read_stop();
    if (!ok && stop_is_jam()) {
        mark_jam(); /* the step of a JAM never ends */
    }
    M.frame++;
    return ok ? 0 : -1;
}

const char *mach_flow_tag(int kind)
{
    return kind == FL_IRQ ? "irq" : kind == FL_NMI ? "nmi" : kind == FL_BRK ? "brk" : "main";
}

int mach_flow_current(void) { return M.nflow ? M.flow[M.nflow - 1].kind : FL_MAIN; }

void mach_flow_reset(void) { M.nflow = 0; }

static uint16_t vec16(uint16_t a) { return (uint16_t)(mach_peek8("cpu", a) | (mach_peek8("cpu", (uint16_t)(a + 1)) << 8)); }

int mach_step_flow(void)
{
    if (!mach_own()) {
        return -1;
    }
    if (M.running) {
        mach_pause();
    }
    hw_cpu_t c0;
    mach_cpu(&c0);
    int at_ib = c0.at_ib;
    uint8_t op0 = c0.op, s0 = c0.s;
    uint16_t pc0 = mach_pc(&c0);
    if (mach_step(1, 0) < 0) {
        return -1;
    }
    if (!at_ib) {
        return 0; /* the step ended an instruction already begun: nothing to classify */
    }
    hw_cpu_t c1;
    mach_cpu(&c1);
    uint16_t pc1 = mach_pc(&c1);
    int kind = -1;
    if (op0 == 0x00) {
        kind = FL_BRK;
    } else if (op0 == 0x40) {
        if (M.nflow) {
            M.nflow--; /* RTI */
        }
        return 0;
    } else if ((uint8_t)(s0 - c1.s) == 3) {
        /* an interrupt after the instruction: the sequence pushed PCH, PCL and P and the next boundary is the
         * handler's first instruction */
        if (pc1 == vec16(0xFFFA)) {
            kind = FL_NMI;
        } else if (pc1 == vec16(0xFFFE)) {
            kind = FL_IRQ;
        }
    }
    if (kind >= 0) {
        if (M.nflow == MAX_FLOW) {
            memmove(&M.flow[0], &M.flow[1], sizeof(flow_t) * (MAX_FLOW - 1));
            M.nflow--;
        }
        flow_t *f = &M.flow[M.nflow++];
        f->kind = (uint8_t)kind;
        f->pc = pc1;
        f->ret = pc0;
        f->cyc = mach_cycles();
    }
    return 0;
}

int mach_drv_step(int n)
{
    if (!mach_own()) {
        return -1;
    }
    if (M.running) {
        mach_pause();
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 0xFFFF) {
        n = 0xFFFF;
    }
    hw_cmd(CMD_DRV_STEP, (uint16_t)n);
    int ok = wait_status(ST_STOP_NEW, 0, 2000);
    if (ok) {
        mach_read_stop();
    }
    return ok ? 0 : -1;
}

/* Breakpoints and exec observers share the exec bitmap */

static void bmp_rebuild(uint16_t lo, uint16_t hi)
{
    for (int idx = lo >> 5; idx <= (hi >> 5); idx++) {
        uint32_t w = 0;
        uint16_t base = (uint16_t)(idx << 5);
        for (int i = 0; i < M.nbp; i++) {
            if (M.bp[i].enabled && ((M.bp[i].pc >> 5) == idx)) {
                w |= 1u << (M.bp[i].pc & 31);
            }
        }
        for (int i = 0; i < M.nobs; i++) {
            obs_t *o = &M.obs[i];
            if (!o->on || (o->trig != OBS_EXEC) || (o->unit >= 0) || (o->hi < base) || (o->lo > (uint16_t)(base + 31))) {
                continue;
            }
            for (int b = 0; b < 32; b++) {
                uint16_t a = (uint16_t)(base + b);
                if ((a >= o->lo) && (a <= o->hi)) {
                    w |= 1u << b;
                }
            }
        }
        if (w != s_bmp[idx]) {
            s_bmp[idx] = w;
            hw_bmp_word(idx, w);
        }
    }
    int any = 0;
    for (int i = 0; (i < M.nbp) && !any; i++) {
        any = M.bp[i].enabled;
    }
    for (int i = 0; (i < M.nobs) && !any; i++) {
        any = M.obs[i].on && (M.obs[i].trig == OBS_EXEC) && (M.obs[i].unit < 0);
    }
    hw_bmp_enable(any);
}

int mach_bp_add(uint16_t pc)
{
    if (M.nbp >= MAX_BP) {
        return -1;
    }
    mach_own();
    bp_t *b = &M.bp[M.nbp++];
    b->num = M.next_bp++;
    b->pc = pc;
    b->enabled = 1;
    bmp_rebuild(pc, pc);
    return (int)b->num;
}

static void bp_remove(int i)
{
    uint16_t pc = M.bp[i].pc;
    for (int k = i; k < M.nbp - 1; k++) {
        M.bp[k] = M.bp[k + 1];
    }
    M.nbp--;
    bmp_rebuild(pc, pc);
}

int mach_bp_del_num(uint32_t num)
{
    for (int i = 0; i < M.nbp; i++) {
        if (M.bp[i].num == num) {
            bp_remove(i);
            return 1;
        }
    }
    return 0;
}

int mach_bp_del_addr(uint16_t pc)
{
    int n = 0;
    for (int i = 0; i < M.nbp;) {
        if (M.bp[i].pc == pc) {
            bp_remove(i);
            n++;
        } else {
            i++;
        }
    }
    return n;
}

void mach_bp_clear(void)
{
    while (M.nbp > 0) {
        bp_remove(M.nbp - 1);
    }
}

/* Observers: exec on the bitmap, load/store/raster on a watch unit each. The condition is held in the unit as far
 * as it goes (cond.h) and the rest is checked after the stop; the action is break (the stop stays), log (a record
 * in the unit when nothing needs the registers, else read at the stop) or cmd (monitor commands at the stop);
 * after a log or cmd the machine runs on (T21 §4.4.1, T55). */

void (*mach_cmd_hook)(const obs_t *o);

int mach_obs_find(const char *name)
{
    for (int i = 0; i < M.nobs; i++) {
        if (m_streq(M.obs[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int unit_free(int u)
{
    for (int i = 0; i < M.nobs; i++) {
        if (M.obs[i].on && (M.obs[i].unit == u)) {
            return 0;
        }
    }
    return 1;
}

static int ring_runs(void)
{
    hw_ring_status_t rs;
    return hw_log_present() && (hw_ring_status(&rs) == 0) && rs.present && rs.on && !rs.reu_off;
}

/* The record a hardware log writes carries address, data, PC and cycle, not the registers: fields beyond the PC
 * need the stop */
static int log_fields_in_record(const obs_t *o)
{
    for (int i = 0; i < o->nlogf; i++) {
        if (o->logf[i].mem || (o->logf[i].reg != 'p')) {
            return 0;
        }
    }
    return 1;
}

/* The units' hit counters are 16 bits and wrap; the drain reads them every poll and sums the differences in 32 bits.
 * The first PASS hits are no action: the actions of a unit are its hits beyond PASS. */
static struct
{
    uint16_t last;  /* the counter at the last look */
    uint32_t total; /* hits since the unit was armed */
    uint32_t seen;  /* actions already added to the observer */
    uint16_t pass;
} s_hc[2];

/* the actions the unit has made since the last call (0 for a unit not looked at yet) */
static uint32_t hc_new(int u, uint16_t cur, int commit)
{
    uint32_t total = s_hc[u].total + (uint16_t)(cur - s_hc[u].last);
    uint32_t actions = total > s_hc[u].pass ? total - s_hc[u].pass : 0;
    uint32_t fresh = actions - s_hc[u].seen;
    if (commit) {
        s_hc[u].last = cur;
        s_hc[u].total = total;
        s_hc[u].seen = actions;
    }
    return fresh;
}

static void obs_arm(obs_t *o)
{
    if (M.transient || (o->unit < 0)) {
        return;
    }
    /* arming clears the unit's HITS; the first PASS hits are no action */
    uint16_t pass = o->thin ? o->thin : o->pass;
    s_hc[o->unit & 1].last = 0;
    s_hc[o->unit & 1].total = 0;
    s_hc[o->unit & 1].seen = 0;
    s_hc[o->unit & 1].pass = pass;
    if (o->hw_log) {
        o->hits = 0; /* the unit's HITS restart, and the hits that count are the ones after PASS */
    }
    uint32_t ctrl = U_ENABLE | (o->hw_log ? U_A_LOG : 0);
    uint32_t data = o->q.has_data ? (((uint32_t)o->q.dmask << 8) | o->q.dval) : 0;
    switch (o->trig) {
    case OBS_LOAD: ctrl |= U_T_READ; break;
    case OBS_STORE: ctrl |= U_T_WRITE; break;
    case OBS_RASTER: ctrl |= U_T_CYCLE | U_RASTER; break;
    default: ctrl |= U_T_IB; break;
    }
    hw_unit(o->unit, ctrl, o->q.lo, o->q.hi, data, o->q.reg, pass);
}

const char *mach_obs_set(int i, int on)
{
    obs_t *o = &M.obs[i];
    if (on == o->on) {
        return 0;
    }
    if (!on) {
        int was_bmp = (o->trig == OBS_EXEC) && (o->unit < 0);
        if (!M.transient && (o->unit >= 0)) {
            hw_unit_off(o->unit);
        }
        o->on = 0;
        o->unit = -1;
        if (was_bmp) {
            bmp_rebuild(o->lo, o->hi);
        }
        return 0;
    }
    /* the plan: what the unit holds of the condition, whether the log can run in it */
    int units = (o->trig == OBS_LOAD) || (o->trig == OBS_STORE);
    cond_split(&o->cond, units, o->lo, o->hi, &o->q);
    if (o->trig == OBS_RASTER) {
        o->q.lo = o->lo;
        o->q.hi = o->hi;
    }
    o->hw_log = (o->act == OA_LOG) && !o->q.nrest && log_fields_in_record(o) &&
                ((o->trig != OBS_EXEC) || (o->cond.root < 0)) && (o->trig != OBS_RASTER) && ring_runs();
    int need_unit = (o->trig != OBS_EXEC) || o->hw_log;
    int u = unit_free(0) ? 0 : unit_free(1) ? 1 : -1;
    if (need_unit && (u < 0)) {
        if (o->trig == OBS_EXEC) {
            o->hw_log = 0; /* the bitmap and a stop per hit instead */
            need_unit = 0;
        } else {
            return "both watch units are taken: at most two load/store/raster observers can be on";
        }
    }
    o->unit = need_unit ? (int8_t)u : -1;
    o->on = 1;
    o->thin = 0;
    o->last_ms = 0;
    if (o->hw_log) {
        hw_log_read(0, 0, &(uint32_t){ 0 }); /* what was logged before is not wanted */
    }
    if (o->trig == OBS_EXEC) {
        bmp_rebuild(o->lo, o->hi);
    }
    obs_arm(o);
    return 0;
}

const char *mach_obs_add(const obs_t *proto)
{
    if (mach_obs_find(proto->name) >= 0) {
        return "an observer with that name exists";
    }
    if (M.nobs >= MAX_OBS) {
        return "too many observers";
    }
    mach_own();
    obs_t *o = &M.obs[M.nobs];
    memset(o, 0, sizeof(*o));
    int n = m_strlen(proto->name);
    if (n > (int)sizeof(o->name) - 1) {
        n = (int)sizeof(o->name) - 1;
    }
    memcpy(o->name, proto->name, (size_t)n);
    o->trig = proto->trig;
    o->lo = proto->lo;
    o->hi = proto->hi;
    o->unit = -1;
    o->act = proto->act;
    memcpy(o->cond_src, proto->cond_src, sizeof(o->cond_src));
    o->cond_src[sizeof(o->cond_src) - 1] = 0;
    const char *ce = cond_parse(o->cond_src, &o->cond);
    if (ce) {
        return ce;
    }
    o->nlogf = proto->nlogf;
    memcpy(o->logf, proto->logf, sizeof(o->logf));
    memcpy(o->cmd, proto->cmd, sizeof(o->cmd));
    o->cmd[sizeof(o->cmd) - 1] = 0;
    M.nobs++;
    const char *err = mach_obs_set(M.nobs - 1, 1);
    if (err) {
        M.nobs--;
    }
    return err;
}

void mach_obs_del(int i)
{
    mach_obs_set(i, 0);
    for (int k = i; k < M.nobs - 1; k++) {
        M.obs[k] = M.obs[k + 1];
    }
    M.nobs--;
}

const char *mach_obs_ignore(int i, uint32_t n)
{
    obs_t *o = &M.obs[i];
    o->ignore = 0;
    o->pass = 0;
    o->thin = 0;
    if (o->on && (o->unit >= 0) && !o->q.nrest && (n <= 0xFFFF)) {
        o->pass = (uint16_t)n; /* the unit counts: PASS, with HITS cleared by arming */
        obs_arm(o);
        return "hw";
    }
    o->ignore = n;
    return "soft";
}

uint32_t mach_obs_hits(const obs_t *o)
{
    if (o->hw_log && (o->unit >= 0) && o->on) {
        /* the hits since the last drain are still in the unit's counter (the drain adds them to o->hits) */
        return o->hits + hc_new(o->unit & 1, (uint16_t)(hw_hits() >> (16 * o->unit)), 0);
    }
    return o->hits;
}

void mach_rearm_observers(void)
{
    for (int u = 0; u < 2; u++) {
        hw_unit_off(u);
    }
    for (int i = 0; i < M.nobs; i++) {
        if (M.obs[i].on && (M.obs[i].unit >= 0)) {
            obs_arm(&M.obs[i]);
        }
    }
}

/* ---- the observer log ---------------------------------------------------- */

static char s_olog[MACH_OLOG][96];
static int s_olog_n, s_olog_head;
static uint32_t s_olog_seq; /* lines pushed so far: the readers (screen, RPC) keep their own position in it */

void mach_olog_push(const char *line)
{
    int n = m_strlen(line);
    if (n > (int)sizeof(s_olog[0]) - 1) {
        n = (int)sizeof(s_olog[0]) - 1;
    }
    memcpy(s_olog[s_olog_head], line, (size_t)n);
    s_olog[s_olog_head][n] = 0;
    s_olog_head = (s_olog_head + 1) % MACH_OLOG;
    if (s_olog_n < MACH_OLOG) {
        s_olog_n++;
    }
    s_olog_seq++;
}

int mach_olog_count(void) { return s_olog_n; }

const char *mach_olog_line(int i)
{
    return s_olog[(s_olog_head - s_olog_n + i + 2 * MACH_OLOG) % MACH_OLOG];
}

uint32_t mach_olog_seq(void) { return s_olog_seq; }

const char *mach_olog_next(uint32_t *cur)
{
    uint32_t oldest = s_olog_seq - (uint32_t)s_olog_n;
    if ((int32_t)(*cur - oldest) < 0) {
        *cur = oldest; /* the reader fell behind the ring: it goes on with the oldest line left */
    }
    if ((int32_t)(*cur - s_olog_seq) >= 0) {
        return 0;
    }
    const char *l = mach_olog_line((int)(*cur - oldest));
    (*cur)++;
    return l;
}

int mach_live_log_active(void)
{
    for (int i = 0; i < M.nobs; i++) {
        if (M.obs[i].on && ((M.obs[i].act == OA_LOG) || (M.obs[i].act == OA_CMD))) {
            return 1;
        }
    }
    return 0;
}

/* ---- the live rate ----------------------------------------------------------
 * A log observer can hit 160,000 times a second (INC $d020 in a loop). What is shown live (screen, RPC
 * notification, obs log) is metered: LOG_RATE lines a second after a burst of LOG_BURST; the hits beyond that are
 * counted and reported as one "+K skipped" line (at most once a second, or when the flood stops). A hit that
 * would cost a stop (software log, do cmd) costs nothing more than the counter once the budget is gone. */
#define LOG_RATE       60u   /* lines per second: a frame's worth of interrupts (exec $ea31) still shows whole */
#define LOG_BURST      20u
#define LOG_NOTICE_MS  1000u /* the skipped count is reported at most this often while the flood goes on */
#define LOG_IDLE_MS    250u  /* ... and this long after the last skip */
#define THIN_CYC       19656u /* a thinned software log stops about once per frame of machine time */
#define THIN_IDLE_MS   150u  /* ... and gives up waiting for its PASS count after this long without a stop */

static uint32_t s_tok = LOG_BURST * 1000u; /* milli-lines */
static uint32_t s_tok_ms;
static uint32_t s_skipped;  /* hits not shown since the last notice */
static uint32_t s_skip_ms;  /* the last skip */
static uint32_t s_notice_ms;

static void live_refill(uint32_t now)
{
    uint32_t dt = now - s_tok_ms;
    s_tok_ms = now;
    if (dt > 5000) {
        dt = 5000;
    }
    s_tok += dt * LOG_RATE;
    if (s_tok > LOG_BURST * 1000u) {
        s_tok = LOG_BURST * 1000u;
    }
}

static void live_notice(uint32_t now)
{
    if (!s_skipped) {
        return;
    }
    char t[48];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    sb_putc(&b, '+');
    sb_dec(&b, s_skipped);
    sb_put(&b, " skipped");
    mach_olog_push(sb_str(&b));
    s_skipped = 0;
    s_notice_ms = now;
}

static void live_skip(uint32_t n)
{
    s_skipped += n;
    s_skip_ms = plat_ms();
}

/* 1: this hit is shown (or its command runs); 0: counted as skipped */
static int live_take(void)
{
    uint32_t now = plat_ms();
    live_refill(now);
    if (s_tok >= 1000) {
        s_tok -= 1000;
        return 1;
    }
    live_skip(1);
    return 0;
}

static void live_tick(void)
{
    if (!s_skipped) {
        s_notice_ms = plat_ms();
        return;
    }
    uint32_t now = plat_ms();
    if (((uint32_t)(now - s_skip_ms) >= LOG_IDLE_MS) || ((uint32_t)(now - s_notice_ms) >= LOG_NOTICE_MS)) {
        live_notice(now);
    }
}

static const char *const s_trig_word[] = { "exec", "load", "store", "raster" };

/* "obs NAME: store $d020=$05  pc=$c005 cyc=N" - the place, the fields, the cycle (TRX64's line) */
static void olog_line(sbuf_t *b, const obs_t *o, int trig, uint16_t addr, int val, uint64_t cyc)
{
    sb_put(b, "obs ");
    sb_put(b, o->name);
    sb_put(b, ": ");
    sb_put(b, s_trig_word[trig]);
    sb_put(b, " $");
    sb_hex(b, addr, 4);
    if (val >= 0) {
        sb_put(b, "=$");
        sb_hex(b, (uint32_t)val, 2);
    }
    sb_put(b, "  ");
}

/* What a poll may read of the hardware log: a ring that fills faster than it is read must not keep the serve loop
 * (and with it the screen, REST and the menu) in this function */
#define DRAIN_CALLS 4

void mach_olog_drain(void)
{
    int any = 0;
    for (int i = 0; i < M.nobs; i++) {
        any |= M.obs[i].on && M.obs[i].hw_log;
        /* a thinned software log whose hits stopped coming: the unit still waits for its PASS count */
        obs_t *o = &M.obs[i];
        if (o->on && o->thin && (o->unit >= 0) && ((uint32_t)(plat_ms() - o->last_ms) >= THIN_IDLE_MS)) {
            uint32_t h = (uint16_t)(hw_hits() >> (16 * o->unit));
            o->hits += h;
            live_skip(h);
            o->thin = 0;
            o->pass = 0;
            obs_arm(o);
        }
    }
    live_tick();
    if (!any) {
        return;
    }
    uint32_t now_ms = plat_ms();
    live_refill(now_ms);
    uint32_t hits_now = hw_hits();
    uint32_t shown[2] = { 0, 0 }, lost_total = 0;
    int ended = 0; /* the reader is at the present */
    for (int call = 0; call < DRAIN_CALLS; call++) {
        uint32_t want = s_tok / 1000;
        if (!want) {
            break;
        }
        if (want > 16) {
            want = 16;
        }
        hw_logrec_t r[16];
        uint32_t lost = 0;
        int n = hw_log_read(r, (int)want, &lost);
        lost_total += lost;
        uint64_t now = hw_cycles();
        for (int k = 0; k < n; k++) {
            const obs_t *o = 0;
            for (int i = 0; i < M.nobs; i++) {
                if (M.obs[i].on && M.obs[i].hw_log && (M.obs[i].unit == r[k].unit)) {
                    o = &M.obs[i];
                }
            }
            if (!o) {
                continue; /* an observer that was turned off or moved to the other unit meanwhile */
            }
            s_tok -= 1000;
            shown[r[k].unit & 1]++;
            /* the record holds the cycle count low 15 bits; it is less than 32768 cycles old */
            uint64_t cyc = now - ((now - r[k].cyc15) & 0x7FFF);
            char t[96];
            sbuf_t b;
            sb_init(&b, t, sizeof(t));
            olog_line(&b, o, r[k].ib ? OBS_EXEC : r[k].write ? OBS_STORE : OBS_LOAD, r[k].addr,
                      r[k].ib ? -1 : r[k].data, cyc);
            sb_put(&b, "pc=$");
            sb_hex(&b, r[k].ipc, 4);
            sb_put(&b, " cyc=");
            sb_dec(&b, (uint64_t)((int64_t)cyc + M.cyc_off));
            mach_olog_push(sb_str(&b));
        }
        if (n < (int)want) {
            ended = 1;
            break;
        }
    }
    /* the hits the units counted that were not shown: dropped for the rate, or overwritten before they were read.
     * What is left in the ring is not wanted any more (it is older than what the screen shows): the reader
     * moves to the present, the count comes from the units' counters. */
    uint32_t dropped = 0;
    for (int u = 0; u < 2; u++) {
        int mine = 0;
        for (int i = 0; i < M.nobs; i++) {
            mine |= M.obs[i].on && M.obs[i].hw_log && (M.obs[i].unit == u);
        }
        uint32_t produced = hc_new(u, (uint16_t)(hits_now >> (16 * u)), 1);
        if (mine && (produced > shown[u])) {
            dropped += produced - shown[u];
        }
        for (int i = 0; i < M.nobs; i++) {
            if (M.obs[i].on && M.obs[i].hw_log && (M.obs[i].unit == u)) {
                M.obs[i].hits += produced; /* the unit's counter is 16 bits: the observer keeps the sum */
            }
        }
    }
    if (!ended) {
        uint32_t skip = 0;
        hw_log_read(0, 0, &skip);
    } else if (lost_total > dropped) {
        dropped = lost_total;
    }
    if (dropped) {
        live_skip(dropped);
    }
}

/* ---- a stop that belongs to observers ---------------------------------------- */

static void soft_env(const stop_t *s, const obs_t *o, cond_env_t *e)
{
    hw_cpu_t c;
    mach_cpu(&c);
    e->a = c.a;
    e->x = c.x;
    e->y = c.y;
    e->sp = c.s;
    e->fl = c.p;
    e->pc = s->pc;
    e->rl = mach_raster_line();
    e->cy = (int64_t)s->cycles;
    if ((o->trig == OBS_LOAD) || (o->trig == OBS_STORE)) {
        e->val = s->rec.data;
        e->addr = s->rec.addr;
    } else {
        e->val = 0; /* TRX64: an exec observer sees the PC as the address and no value */
        e->addr = s->pc;
    }
}

static void soft_log_line(const stop_t *s, const obs_t *o)
{
    char t[96];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    int access = (o->trig == OBS_LOAD) || (o->trig == OBS_STORE);
    olog_line(&b, o, o->trig, access ? s->rec.addr : s->pc, access ? s->rec.data : -1, s->cycles);
    hw_cpu_t c;
    mach_cpu(&c);
    if (!o->nlogf) {
        sb_put(&b, "pc=$");
        sb_hex(&b, s->pc, 4);
        sb_put(&b, " a=$");
        sb_hex(&b, c.a, 2);
    }
    for (int i = 0; i < o->nlogf; i++) {
        const logf_t *f = &o->logf[i];
        if (i) {
            sb_putc(&b, ' ');
        }
        if (f->mem) {
            sb_putc(&b, '$');
            sb_hex(&b, f->addr, f->addr < 0x100 ? 2 : 4);
            sb_putc(&b, '=');
            uint32_t v = mach_peek8("cpu", f->addr);
            if (f->word) {
                v |= (uint32_t)mach_peek8("cpu", (uint16_t)(f->addr + 1)) << 8;
            }
            sb_hex(&b, v, f->word ? 4 : 2);
            continue;
        }
        switch (f->reg) {
        case 'p': sb_put(&b, "pc=$"); sb_hex(&b, s->pc, 4); break;
        case 'a': sb_put(&b, "a="); sb_hex(&b, c.a, 2); break;
        case 'x': sb_put(&b, "x="); sb_hex(&b, c.x, 2); break;
        case 'y': sb_put(&b, "y="); sb_hex(&b, c.y, 2); break;
        case 's': sb_put(&b, "sp="); sb_hex(&b, c.s, 2); break;
        default: sb_put(&b, "fl="); sb_hex(&b, c.p, 2); break;
        }
    }
    sb_put(&b, " cyc=");
    sb_dec(&b, s->cycles);
    mach_olog_push(sb_str(&b));
}

/* A software log (or cmd) on a watch unit costs a stop per hit, and a stop costs the machine a lot of time (the
 * run that follows waits for the frame alignment). When the hits come faster than one per frame, the unit is told
 * to pass the hits between two stops (PASS): about one stop per frame of machine time, so the machine keeps running
 * and the screen still gets a line per frame. The hit rate is measured against the machine's own cycles (the time
 * the machine stood still is not in them), so it does not depend on how long a stop takes. The passed hits count
 * as hits and as skipped. Only where the unit holds the whole condition (a rest checked after the stop would be
 * counted wrong). */
static void thin_adapt(obs_t *o, const stop_t *s)
{
    if ((o->unit < 0) || o->q.nrest || (o->trig == OBS_RASTER) || o->hw_log) {
        return;
    }
    uint32_t since = (uint32_t)o->thin + 1; /* the hits since the last stop: the passed ones and this one */
    uint16_t prev = o->thin;
    uint64_t dcyc = (o->last_cyc && (s->cycles > o->last_cyc)) ? s->cycles - o->last_cyc : 0;
    o->last_cyc = s->cycles;
    o->last_ms = plat_ms();
    if (prev) {
        o->hits += prev;
        live_skip(prev);
    }
    uint32_t want = 0;
    if (dcyc) {
        uint64_t per = (uint64_t)since * THIN_CYC / dcyc; /* hits in a frame of machine time */
        want = per > 1 ? (uint32_t)(per - 1) : 0;
        if (want < 3) {
            want = 0;
        }
        if (want > 0x7FFF) {
            want = 0x7FFF;
        }
    }
    o->thin = (uint16_t)want;
    if (o->thin || prev) {
        o->pass = 0; /* the ignore count is used up: the first stop came */
        obs_arm(o);
    }
}

/* One observer at a stop: the condition's software part, the ignore count, the hit, the action. 1: it wants the
 * machine to stay stopped. */
static int obs_fire(int i, const stop_t *s)
{
    obs_t *o = &M.obs[i];
    if (o->q.nrest) {
        cond_env_t e;
        soft_env(s, o, &e);
        for (int k = 0; k < o->q.nrest; k++) {
            if (!cond_eval(&o->cond, o->q.rest[k], &e)) {
                return 0;
            }
        }
    }
    if (o->ignore) {
        o->ignore--;
        return 0;
    }
    o->hits++;
    switch (o->act) {
    case OA_LOG: {
        if (live_take()) {
            soft_log_line(s, o);
        }
        thin_adapt(o, s);
        return 0;
    }
    case OA_CMD: {
        if (live_take() && mach_cmd_hook) {
            mach_cmd_hook(o);
        }
        thin_adapt(o, s);
        return 0;
    }
    default: return 1;
    }
}

/* The stop just read, in a run: observers whose condition is false, whose ignore count is not used up or whose
 * action is log/cmd let the machine run on. 1: the stop stays. */
static int obs_dispatch(int was_transient)
{
    stop_t *s = &M.stop;
    int bitmap = s->rec.reason == RS_BITMAP;
    int unit = (s->rec.reason == RS_UNIT_ADDR) || (s->rec.reason == RS_UNIT_RAS);
    if (!bitmap && !(unit && !was_transient)) {
        return 1;
    }
    int halt = s->bp_num > 0;
    int first = -1;
    int seen = 0;
    for (int i = 0; i < M.nobs; i++) {
        const obs_t *o = &M.obs[i];
        int mine = bitmap ? (o->on && (o->trig == OBS_EXEC) && (o->unit < 0) && (s->pc >= o->lo) && (s->pc <= o->hi))
                          : (o->on && (o->unit == s->rec.unit));
        if (!mine) {
            continue;
        }
        seen++;
        if (obs_fire(i, s)) {
            halt = 1;
            if (first < 0) {
                first = i;
            }
        }
    }
    if (!seen && !s->bp_num) {
        return 1; /* a stop no observer knows: report it */
    }
    if (halt) {
        s->obs = (s->bp_num > 0) ? -1 : first;
        s->reason = s->bp_num ? "breakpoint" : "observer";
    }
    return halt;
}

static void transient_begin(const char *reason)
{
    hw_unit_off(0);
    hw_unit_off(1);
    M.transient = 1;
    s_transient_reason = reason;
}

void mach_arm_until(uint16_t addr)
{
    s_tr.kind = 1;
    s_tr.addr = addr;
    transient_begin("until");
    hw_unit(0, U_ENABLE | U_T_IB | U_ONESHOT, addr, addr, 0, 0, 0);
}

/* Step over a JSR: stop at the return address with the stack back at S0,
 * or anywhere the stack went above S0 (the routine dropped its frame). */
void mach_arm_next(uint16_t ret_pc, uint8_t s0)
{
    s_tr.kind = 2;
    s_tr.addr = ret_pc;
    s_tr.s0 = s0;
    transient_begin("next");
    hw_unit(0, U_ENABLE | U_T_IB | U_ONESHOT, ret_pc, ret_pc, 0, UNIT_REG(R_SEL_S, R_OP_GE, s0, 0xFF), 0);
    if (s0 < 0xFF) {
        hw_unit(1, U_ENABLE | U_T_IB | U_ONESHOT, 0x0000, 0xFFFF, 0, UNIT_REG(R_SEL_S, R_OP_GE, s0 + 1, 0xFF), 0);
    }
}

/* Run until the current frame returns: the first IB after an RTS or RTI
 * that left S above S0. */
void mach_arm_ret(uint8_t s0)
{
    s_tr.kind = 3;
    s_tr.s0 = s0;
    transient_begin("return");
    uint32_t reg = (s0 < 0xFF) ? UNIT_REG(R_SEL_S, R_OP_GE, s0 + 1, 0xFF) : 0;
    hw_unit(0, U_ENABLE | U_T_IB | U_PREV_RET | U_ONESHOT, 0x0000, 0xFFFF, 0, reg, 0);
}

void mach_disarm_transient(void)
{
    M.transient = 0;
    mach_rearm_observers();
}

/* CPU */

/* A CPU write the hardware has staged but not loaded yet (the gate stays
 * shut until the next step or run) */
static struct
{
    int on, pc_on;
    hw_regs_t r;
    uint16_t pc;
    uint8_t op;
} s_pend;

void mach_cpu(hw_cpu_t *c)
{
    hw_cpu(c);
    if (s_pend.on && !hw_cpu_write_pending()) {
        s_pend.on = 0; /* loaded: the state bus shows it now */
    }
    if (s_pend.on) {
        c->a = s_pend.r.a;
        c->x = s_pend.r.x;
        c->y = s_pend.r.y;
        c->s = s_pend.r.s;
        c->p = s_pend.r.p;
        c->settled = 1;
        if (s_pend.pc_on) {
            c->ipc = s_pend.pc;
            c->op = s_pend.op;
            c->pc = (uint16_t)(s_pend.pc + 1);
            c->at_ib = 1;
        }
    }
}

const char *mach_cpu_write(int mask, const hw_regs_t *r, uint16_t pc)
{
    if (!hw_has_cpu_write()) {
        return "this bitstream has no CPU write path (CAPS2 b2; T21 §5.1)";
    }
    if (!mach_own()) {
        return "the debug block is not available";
    }
    if (M.running) {
        return "the machine runs: pause it first (pause)";
    }
    hw_cpu_t c;
    mach_cpu(&c);
    if (!c.at_ib || !c.settled) {
        return "mid-instruction: step to the boundary first (z)";
    }
    hw_regs_t v = { c.a, c.x, c.y, c.s, c.p };
    if (mask & W_A) {
        v.a = r->a;
    }
    if (mask & W_X) {
        v.x = r->x;
    }
    if (mask & W_Y) {
        v.y = r->y;
    }
    if (mask & W_S) {
        v.s = r->s;
    }
    if (mask & W_P) {
        v.p = r->p;
    }
    int to = s_pend.on && s_pend.pc_on ? s_pend.pc : -1;
    uint8_t op = s_pend.op;
    if (mask & W_PC) {
        int b = mach_peek("cpu", pc, 0);
        if (b < 0) {
            return "the opcode there is in I/O the debugger cannot read without a bus cycle";
        }
        to = pc;
        op = (uint8_t)b;
    }
    if (hw_cpu_write(&v, to, op) < 0) {
        return "the state bus refused the staged words (the gate is not shut)";
    }
    s_pend.on = 1;
    s_pend.r = v;
    s_pend.pc_on = to >= 0;
    s_pend.pc = (uint16_t)to;
    s_pend.op = op;
    M.disasm_cursor_set = 0;
    return 0;
}

uint16_t mach_pc(const hw_cpu_t *c) { return c->ipc; }

/* Memory lenses */

int mach_valid_lens(const char *l)
{
    return lens_valid(l) && !m_streq(l, "drive8");
}


int mach_peek(const char *lens, uint16_t a, int sidefx)
{
    return lens_peek(&s_lens, lens, a, sidefx);
}

uint8_t mach_peek8(const char *lens, uint16_t a)
{
    int v = mach_peek(lens, a, 0);
    return (v < 0) ? 0x00 : (uint8_t)v;
}

const char *mach_write(const char *lens, uint16_t a, const uint8_t *b, int n)
{
    if (m_streq(lens, "rom") || m_streq(lens, "cart")) {
        return "the rom and cart lenses are read-only here";
    }
    int io_lens = m_streq(lens, "io");
    int cpu_lens = m_streq(lens, "cpu");
    for (int i = 0; i < n; i++) {
        uint16_t x = (uint16_t)(a + i);
        int io = (x >= 0xD000) && (x < 0xE000) && (io_lens || (cpu_lens && (lens_pla(x) == R_IO)));
        if (io) {
            if (!hw_bus_held()) {
                return "an I/O write is a bus cycle: it needs the machine stopped (a debugger stop or the menu's C64_STOP)";
            }
            hw_bus_write(x, b[i]);
        } else {
            hw_ram_write(x, &b[i], 1);
        }
    }
    mach_snapshot();
    return 0;
}
