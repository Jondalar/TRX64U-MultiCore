// The C64 descriptor (T104.2 D4): "TRX2" is the ID block of the TRX64 Ultimate monolith and of the MultiCore `trx64` core alike. It is
// data plus a handful of operations: the three ROMs into the aperture, the reset, the freeze, the settings, and the .prg that a person
// picks in the browser - run (reset, wait for READY, place, type RUN or SYS) or load and start at an address (the CPU's program counter
// set directly, c64_go.cc). The drive A is the cbm1541 library (cbm1541.cc). What the menu offers - its areas, tabs, tasks, media slots, file types and
// settings - is not in this file: it is coreapp/cores/trx64u/menu.toml (T104.2 D10), compiled by tools/descrec.py into the record this file carries (menu_rec).
// This file supplies what the entries name: the operations, the state values, the ROMs, the freeze and the reset sequence.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "ops_glue.h"
#include "mousesink.h"
#include "cbm_prg.h"
#include "c64_regs.h"
#include "c64_go.h"
#include "c64_cart.h"
#include "appindex.h"                 // PRG_APP_OP
#include "u64cfg.h"
#include "ringops.h"
#include "monitor.h"
#include "online.h"
#include "diag.h"
#include "netops.h"
#include "trx64u_ops.h"
#include "t64.h"
#include "cbm1541.h"
#include "d1541_drive.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "cbm1541_desc.h"                // CBM1541_ROM only: the rest of it is the other cores' menu tables (the C64's is the record)
#pragma GCC diagnostic pop
#include "cores.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// ---- settings ------------------------------------------------------------------------------------------------------------
// GATE_CTRL b0 (trx_sys.v): with it the app's C64_STOP pauses the whole machine - the picture stands still under our menu, the
// state bus is writable - without it only the CPU, as before the freeze work. The one C64 register of this step that is a setting.
static const ArgSpec freeze_arg[] = { ARG_INT("value", 0, 1) };

static void wr32(CoreIo *io, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        io->wr(reg + i, (uint8_t)(v >> (8 * i)));
    }
}

static int op_freeze_mode(CoreCtx *c, const OpArgs &a, OpOut &)
{
    wr32(c->io, C64_GATE_CTRL, a.i(0) == 0 ? C64_GATE_FRZ : 0);
    return OPE_OK;
}

// ---- ROMs ----------------------------------------------------------------------------------------------------------------
// Three plain copies at the aperture's offsets. No read-back: the KERNAL array is write only on Gideon's boards, and the other two
// are the same register file.
static int put_rom(CoreCtx *c, const OpArgs &a, uint32_t ofs, uint32_t size, const char *role, OpOut &o)
{
    if (a.len(0) != size) {
        snprintf(o.msg, o.msglen, "%s: %u bytes, the machine takes %u", role, (unsigned)a.len(0), (unsigned)size);
        return OPE_FAILED;
    }
    c->io->rom_write(ofs, a.p(0), size);
    return OPE_OK;
}
static int op_put_basic(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_rom(c, a, C64_ROM_BASIC, C64_BASIC_BYTES, "BASIC", o); }
static int op_put_kernal(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int rc = put_rom(c, a, C64_ROM_KERNAL, C64_KERNAL_BYTES, "KERNAL", o);
    if (rc == OPE_OK) {
        trx64u_kernal_put(c, a.p(0), a.len(0));
    }
    return rc;
}
static int op_put_char(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_rom(c, a, C64_ROM_CHAR, C64_CHAR_BYTES, "CHAR", o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// The names in Gideon's /flash/roms (FTP listing of the board, 2026-10-28: basic-901226-01.bin, kernal-901227-03.bin,
// chargen-901225-01.bin) first, then other common names and those of the MultiCore line's ROM set.
static const RomSpec roms[4] = {
    { "BASIC", { "basic-901226-01.bin", "basic.bin", "basic.901226-01", "basic.901226-01.bin", "basic.rom", 0 }, { C64_BASIC_BYTES, 0 }, true, 0, "c64.put_basic" },
    { "KERNAL", { "kernal-901227-03.bin", "kernal.bin", "kernal.901227-03", "kernal.901227-03.bin", "kernal.rom", 0 }, { C64_KERNAL_BYTES, 0 }, true, 0, "c64.put_kernal" },
    { "CHAR", { "chargen-901225-01.bin", "chars.bin", "characters.901225-01", "characters.901225-01.bin", "chargen.bin", "chars.rom", 0 }, { C64_CHAR_BYTES, 0 }, true, 0, "c64.put_char" },
    CBM1541_ROM,
};

// ---- control -------------------------------------------------------------------------------------------------------------
// C64_MODE: 0x04 holds the machine in reset, 0x08 lets it go (ultimate_sys.vhd). The KERNAL's own reset sets $00/$01.
static void clear_job(CoreCtx *c);
static void forget_program(CoreCtx *c);

static int c64_init(CoreCtx *c, char *, int)
{
    c->io->mode_reset(true);
    if (c->drive && cfg_value(c, "Resets when C64 resets", 1)) {
        char m[80];
        ((CbmDrive *)c->drive)->reset(m, sizeof(m));                // "Resets when C64 resets": the 1541 starts again with the machine
    }
    clear_job(c);                                               // a reset ends a program that was still waiting for READY
    forget_program(c);                                          // T228 R20 1/7: and the machine that comes up has run no program
    return 0;
}

static int c64_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}

// ---- .prg ----------------------------------------------------------------------------------------------------------------
// Gideon's addresses (c64_subsys.cc, bootcrt.tas): BASIC at $0801, the pointer pair $2B/$2C and the three that follow, the keyboard buffer
// at $0277 with its count at $C6. A file runs from $0801 to $FFFF: the bytes go through the RAM's second port, under BASIC, the I/O area
// and the KERNAL alike, and a BASIC program that ends above $A000 leaves its pointers beyond MEMSIZ - RUN executes the SYS line all the same.
// ready_ms is the least the machine runs after the reset before the first look at READY; it has not been measured on a board (T104.2 5).
#define PRG_P00_HEADER 0x1A
#define C64_BASIC_START 0x0801
static CbmPrgParams run_params(void)
{
    CbmPrgParams p;
    memset(&p, 0, sizeof(p));
    p.basic_starts[0] = C64_BASIC_START;
    p.nbasic_starts = 1;
    p.txttab = C64_TXTTAB; p.txttab_min = 0x0801; p.txttab_max = 0x9FFF;
    p.vartab = 0x2D; p.arytab = 0x2F; p.strend = 0x31;
    p.kbd_buffer = 0x0277; p.kbd_count = 0xC6;
    p.ready_ms = 1000;
    p.p00_header = PRG_P00_HEADER;
    p.min_size = 3;
    p.max_size = PRG_P00_HEADER + 2 + (0x10000 - 0x0801);        // $0801-$FFFF, behind a .P00 header if there is one
    return p;
}

// "Load PRG and start from": the file goes where it says, anywhere in $0000-$FFFF, and nothing is relinked.
static CbmPrgParams load_params(void)
{
    CbmPrgParams p = run_params();
    p.nbasic_starts = 0;
    p.max_size = PRG_P00_HEADER + 2 + 0x10000;
    return p;
}

// READY is when BASIC's cold start has set the pointer pair; the banner and the editor loop need a moment more before they read the buffer.
#define READY_WAIT_MAX_MS 4000
#define READY_SETTLE_MS   300

// A program is started in two steps, because the menu holds the machine (C64_STOP) while an operation of it runs, and a machine that is
// held never reaches READY. The operation validates the file, resets the machine and queues the job; the service (c64.service, polled while
// the machine runs - service.h) waits for READY and then places the bytes and types RUN or SYS, or points the CPU. The time it waits is
// the time the machine has run, not the menu's.
enum { J_NONE = 0, J_RUN = 1, J_LOAD = 2, J_TYPE = 3 };

struct C64State {
    char program[56];                    // the last program started
    int job;
    char name[56];
    char path[FS_PATH_MAX];                      // the file the job came from
    char program_path[FS_PATH_MAX];              // the last program started (the Library's [P] mark)
    uint16_t start;
    bool p00, go;
    uint32_t size;
    uint32_t ran_ms, last_ms, ready_at, ready_ms;
    char script[64];                     // J_TYPE: the pieces to type, '|' between them (cbm_prg_script_step)
    int script_pos;
};

static C64State *c64_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new C64State();
        memset(c->state, 0, sizeof(C64State));
    }
    return (C64State *)c->state;
}
static void forget_program(CoreCtx *c)
{
    c64_state(c)->program[0] = 0;
    c64_state(c)->program_path[0] = 0;
}

static void clear_job(CoreCtx *c)
{
    if (c->state) {
        ((C64State *)c->state)->job = J_NONE;
    }
}

static const char *prg_name(CoreCtx *c)
{
    C64State *st = c64_state(c);
    return st->program[0] ? st->program : 0;
}

static const char *prg_path(CoreCtx *c)
{
    C64State *st = c64_state(c);
    return st->program_path[0] ? st->program_path : 0;
}

static const char *prg_pending(CoreCtx *c)
{
    C64State *st = c64_state(c);
    return st->job == J_RUN ? "run" : (st->job == J_LOAD ? "load" : (st->job == J_TYPE ? "type" : 0));
}

static bool is_p00(const char *path)
{
    size_t l = strlen(path);
    return l > 4 && !strcasecmp(path + l - 4, ".p00");
}

static uint8_t prgbuf[PRG_P00_HEADER + 2 + 0x10000];

// The whole file, at most p.max_size bytes, into prgbuf. 0 or a reason.
static int read_prg(CoreCtx *c, const char *path, const CbmPrgParams &p, uint32_t *size, char *msg, int msglen)
{
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t n = f->size();
    uint32_t skip = is_p00(path) ? p.p00_header : 0;
    if (n < skip + p.min_size || n > p.max_size) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s: %u bytes is not a program", path, (unsigned)fs_size32(n));
        return 1;
    }
    bool ok = f->read(0, prgbuf, (uint32_t)n);
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
        return 1;
    }
    uint32_t load = (uint32_t)(prgbuf[skip] | (prgbuf[skip + 1] << 8));
    uint32_t len = (uint32_t)n - skip - 2;
    if (load + len > 0x10000u) {                               // a file that would wrap past $FFFF is refused before the machine is touched
        snprintf(msg, msglen, "%s: $%04X + %u bytes runs past $FFFF", path, (unsigned)load, (unsigned)len);
        return 1;
    }
    *size = (uint32_t)n;
    return 0;
}

// The bytes go through the RAM's second port: MEMONLY on, so that $D000-$DFFF is RAM too and not a bus cycle into the chips. Never with
// the machine stopped by C64_STOP, where a byte is a bus cycle through the PLA (S15).
static int place(CoreCtx *c, const uint8_t *data, uint32_t size, const CbmPrgParams &p, bool p00, CbmPrgResult *r, char *msg, int msglen)
{
    c->io->ram_memonly(true);
    int rc = cbm_prg_place(c->io, data, size, p, p00, r, msg, msglen);
    c->io->ram_memonly(false);
    return rc;
}

static void remember(CoreCtx *c, const char *name)
{
    snprintf(c64_state(c)->program, sizeof(C64State::program), "%s", name);
    snprintf(c64_state(c)->program_path, sizeof(C64State::program_path), "%s", c64_state(c)->path);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// The operation's half: the file is read and checked, the machine reset, the job queued. The machine is not touched when the file is no
// program. A reset clears a job that is still waiting (c64_init), so the queue comes after it.
static int queue_prg(CoreCtx *c, int kind, const char *path, uint16_t start, char *msg, int msglen)
{
    CbmPrgParams p = kind == J_RUN ? run_params() : load_params();
    uint32_t size;
    if (read_prg(c, path, p, &size, msg, msglen)) {
        return 1;
    }
    bool p00 = is_p00(path);
    bool go = kind == J_LOAD && c64_go_available(c->io);
    if (kind == J_LOAD && !go) {
        uint32_t skip = p00 ? p.p00_header : 0;
        uint16_t load = (uint16_t)(prgbuf[skip] | (prgbuf[skip + 1] << 8));
        if (load < 0x0400) {
            snprintf(msg, msglen, "$%04X is in the zero page, stack or KERNAL work area: a typed SYS cannot start it, and this bitstream "
                     "cannot set the CPU's address", load);
            return 1;
        }
    }
    if (boot_cold_reset(c, msg, msglen)) {
        return 1;
    }
    C64State *st = c64_state(c);
    st->job = kind;
    st->start = start;
    st->p00 = p00;
    st->go = go;
    st->size = size;
    st->ran_ms = st->last_ms = st->ready_at = 0;
    st->ready_ms = p.ready_ms;
    fs_short_name(st->name, sizeof(st->name), base_name(path));
    snprintf(st->path, sizeof(st->path), "%s", path);
    printf("[c64] %s: reset, %s queued for READY\n", path, kind == J_RUN ? "run" : "load and start");
    return 0;
}

// The service's half, with the machine at READY.
static int fire(CoreCtx *c, C64State *st, char *msg, int msglen)
{
    CbmPrgParams p = st->job == J_RUN ? run_params() : load_params();
    CbmPrgResult r;
    memset(&r, 0, sizeof(r));
    if (st->job == J_RUN) {
        if (place(c, prgbuf, st->size, p, st->p00, &r, msg, msglen)) {
            return 1;
        }
        cbm_prg_type_start(c->io, p, r);
        printf("[c64] %s: $%04X-$%04X, %s\n", st->name, r.load, (unsigned)(r.end - 1) & 0xFFFF, r.basic ? "RUN" : "SYS");
        return 0;
    }
    if (st->go) {
        struct Pa { CoreCtx *c; C64State *st; const CbmPrgParams *p; CbmPrgResult *r; } pa = { c, st, &p, &r };
        struct F {
            static int cb(void *u, char *m, int n)
            {
                Pa *a = (Pa *)u;
                return place(a->c, prgbuf, a->st->size, *a->p, a->st->p00, a->r, m, n);
            }
        };
        if (c64_go(c->io, st->start, F::cb, &pa, msg, msglen)) {
            return 1;
        }
    } else {
        if (place(c, prgbuf, st->size, p, st->p00, &r, msg, msglen)) {
            return 1;
        }
        r.start = st->start;
        cbm_prg_type_start(c->io, p, r);
    }
    printf("[c64] %s: $%04X-$%04X, start $%04X (%s)\n", st->name, r.load, (unsigned)(r.end - 1) & 0xFFFF, st->start,
           st->go ? "CPU address set" : "SYS typed");
    return 0;
}

static void tell(CoreCtx *c, const char *what)
{
    printf("[c64] %s\n", what);
    snprintf(c->notice, sizeof(c->notice), "%s", what);
    c->notice_pending = true;
}

static int op_service_job(CoreCtx *c);
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int rc = op_service_job(c);
    o.more = cbm1541_service(c);                               // the drive's write-back, on the same task (T104.1 D15 poll)
    return rc;
}

static int op_service_job(CoreCtx *c)
{
    C64State *st = c64_state(c);
    if (!st->job) {
        return OPE_OK;
    }
    uint32_t now = c->io->now_ms();
    if (st->last_ms) {
        uint32_t dt = now - st->last_ms;
        st->ran_ms += dt > 200 ? 100 : dt;                     // a gap is the menu's: the machine did not run in it
    }
    st->last_ms = now;
    if (st->ran_ms < st->ready_ms) {
        return OPE_OK;
    }
    uint8_t b[2] = { 0, 0 };
    c->io->ram_read(C64_TXTTAB, b, 2);
    if ((uint16_t)(b[0] | (b[1] << 8)) != C64_BASIC_START && !(st->job == J_TYPE && st->ready_at)) {
        if (st->ran_ms > st->ready_ms + READY_WAIT_MAX_MS) {
            st->job = J_NONE;
            tell(c, "The machine did not reach READY: the program was not started (are the ROMs the right ones?)");
        }
        return OPE_OK;
    }
    if (!st->ready_at) {
        st->ready_at = st->ran_ms;
    }
    if (st->ran_ms - st->ready_at < READY_SETTLE_MS) {
        return OPE_OK;
    }
    if (st->job == J_TYPE) {
        if (st->ran_ms - st->ready_at > 60000) {
            st->job = J_NONE;
            tell(c, "The machine did not take the typed start (is the editor stuck?)");
        } else if (cbm_prg_script_step(c->io, run_params(), st->script, &st->script_pos)) {
            st->job = J_NONE;
        }
        return OPE_OK;
    }
    char msg[200];
    msg[0] = 0;
    bool ok = !fire(c, st, msg, sizeof(msg));
    if (ok) {
        remember(c, st->name);
    } else {
        tell(c, msg);
    }
    st->job = J_NONE;
    return OPE_OK;
}

// "DMA": the file goes into RAM where its load address says, nothing is reset, nothing is started (Gideon's DMA load without the run)
// A program of a .t64: its image (load address and data) is the prgbuf, as if the file were a .prg; the same queue and the same service then run it
static int load_t64(CoreCtx *c, const char *path, int index, uint32_t *size, char *msg, int msglen)
{
    int n = t64_load(c->fs, path, index, prgbuf, sizeof(prgbuf));
    if (n < 3) {
        snprintf(msg, msglen, "%s: entry %d cannot be read", path, index + 1);
        return 1;
    }
    *size = (uint32_t)n;
    return 0;
}
static int op_run_t64(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    CbmPrgParams p = run_params();
    uint32_t size;
    if (load_t64(c, a.s(0), a.i(1), &size, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (boot_cold_reset(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    C64State *st = c64_state(c);
    st->job = J_RUN;
    st->p00 = false;
    st->go = false;
    st->size = size;
    st->ran_ms = st->last_ms = st->ready_at = 0;
    st->ready_ms = p.ready_ms;
    fs_short_name(st->name, sizeof(st->name), base_name(a.s(0)));
    snprintf(st->path, sizeof(st->path), "%s", a.s(0));
    return OPE_OK;
}
static int op_dma_t64(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    CbmPrgParams p = load_params();
    uint32_t size;
    if (load_t64(c, a.s(0), a.i(1), &size, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    CbmPrgResult r;
    memset(&r, 0, sizeof(r));
    if (place(c, prgbuf, size, p, false, &r, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Loaded $%04X-$%04X into RAM", r.load, (unsigned)(r.end - 1) & 0xFFFF);
    return OPE_OK;
}

static int op_dma(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    CbmPrgParams p = load_params();
    uint32_t size;
    if (read_prg(c, a.s(0), p, &size, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    CbmPrgResult r;
    memset(&r, 0, sizeof(r));
    if (place(c, prgbuf, size, p, is_p00(a.s(0)), &r, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Loaded $%04X-$%04X into RAM", r.load, (unsigned)(r.end - 1) & 0xFFFF);
    return OPE_OK;
}
// "Set as Kernal / BASIC / Char ROM": the file becomes the setting (so every start takes it) and goes into the machine at once
static int op_set_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static const char *const keys[3] = { "Kernal ROM", "Basic ROM", "Char ROM" };
    int w = a.i(1);
    OpIn in[2] = { in_str("key", keys[w]), in_str("value", a.s(0)) };
    OpResult r;
    if (ops_call(c, "settings.set", in, 2, &r)) {
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return OPE_FAILED;
    }
    OpIn v[1] = { in_int("value", 0) };
    if (ops_call(c, "c64.fx_roms", v, 1, &r)) {
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "%s is now %s (the machine restarts on RESET)", keys[w], a.s(0));
    return OPE_OK;
}
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return queue_prg(c, J_RUN, a.s(0), 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o) { return queue_prg(c, J_LOAD, a.s(0), (uint16_t)a.i(1), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
// T228 R21 #14, FW user_file_interaction.cc:211-296 (S_runApp): the file goes into the REU's memory behind a 32 bit little-endian length (the length at 0, the bytes
// from 4), then /Flash/apps/<ext>.prg - ext the first three characters of the file's extension - is run as "Run PRG" runs a file. The REU memory is the DDR2
// window from 0 (reu_load in trx64u_ops.cc); a cartridge image in it refuses (FW :215).
static int op_run_with_app(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *path = a.s(0);
    const char *dot = strrchr(base_name(path), '.');
    uint64_t sz = 0;
    bool dir = false;
    if (!dot || !dot[1] || !c->fs->stat(path, &sz, &dir) || dir) {
        snprintf(o.msg, o.msglen, "No file %s", path);
        return OPE_FAILED;
    }
    if (sz > 0x1000000u - 4) {
        snprintf(o.msg, o.msglen, "The file is larger than the REU's memory");
        return OPE_FAILED;
    }
    if (c64_cart_active()) {
        snprintf(o.msg, o.msglen, "A cartridge uses the REU's memory: remove it first");
        return OPE_FAILED;
    }
    char app[64];
    app[0] = 0;
    FsEntry r[6];
    int nr = c->fs->list("/", r, 6);
    for (int i = 0; i < nr && !app[0]; i++) {
        if (!strcasecmp(r[i].name, "flash")) {
            snprintf(app, sizeof(app), "/%s/apps/%.3s.prg", r[i].name, dot + 1);
        }
    }
    uint64_t asz = 0;
    if (!app[0] || !c->fs->stat(app, &asz, &dir) || dir) {
        snprintf(o.msg, o.msglen, "There is no app for .%.3s files in the flash's apps folder", dot + 1);
        return OPE_FAILED;
    }
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(o.msg, o.msglen, "Cannot open %s", path);
        return OPE_FAILED;
    }
    static uint8_t buf[0x10000];
    uint32_t n = (uint32_t)sz;
    bool ok = true;
    for (uint32_t done = 0; ok && done < n;) {
        uint32_t k = n - done < sizeof(buf) ? n - done : (uint32_t)sizeof(buf);
        ok = f->read(done, buf, k);
        c->io->ddr_write(4 + done, buf, k);
        done += k;
    }
    c->fs->close(f);
    if (!ok) {
        snprintf(o.msg, o.msglen, "Cannot read %s (the medium is gone?)", path);
        return OPE_FAILED;
    }
    uint8_t len[4] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
    c->io->ddr_write(0, len, 4);
    return queue_prg(c, J_RUN, app, 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
// T104.1 D15, Mount & Run: reset, and type the script at READY (pieces of at most ten keys)
static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    C64State *st = c64_state(c);
    if (cbm_prg_script_check(a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    if (boot_cold_reset(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    st->job = J_TYPE;
    snprintf(st->script, sizeof(st->script), "%s", a.s(0));
    st->script_pos = 0;
    st->ran_ms = st->last_ms = st->ready_at = 0;
    st->ready_ms = run_params().ready_ms;
    printf("[c64] reset, typing queued for READY\n");
    return OPE_OK;
}
// The Library's .crt (c64_cart.cc): the cartridge is placed and the machine reset into it. The program that was running is gone with the reset.
static int op_run_crt(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (c64_cart_run(c, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    c64_state(c)->program[0] = 0;
    c64_state(c)->program_path[0] = 0;
    return OPE_OK;
}
static int op_remove_cart(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (c64_cart_remove(c, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    c64_state(c)->program[0] = 0;
    return OPE_OK;
}

// T180 D2, D5: the two slots of the C64 for the stored mounts and the media sets: the drive library's "Drive A" and the cartridge port's "Cartridge" (the .crt's path).
// A cartridge started from the stored mounts is the same as the Library's: the program that was running is gone with it.
static const char *const slot_names[] = { "Drive A", "Cartridge" };
static const char *slot_spec(CoreCtx *c, int i)
{
    if (i == 0) {
        return cbm1541_slot_spec(c);
    }
    return c64_cart_active() && c64_cart_path()[0] ? c64_cart_path() : 0;
}
static int slot_mount(CoreCtx *c, int i, const char *spec, char *msg, int msglen)
{
    if (i == 0) {
        return cbm1541_slot_mount(c, spec, msg, msglen);
    }
    if (c64_cart_run(c, spec, msg, msglen)) {
        return 1;
    }
    c64_state(c)->program[0] = 0;
    c64_state(c)->program_path[0] = 0;
    return 0;
}
static int slot_eject(CoreCtx *c, int i, char *msg, int msglen)
{
    if (i == 0) {
        return cbm1541_slot_eject(c, msg, msglen);
    }
    if (c64_cart_remove(c, msg, msglen)) {
        return 1;
    }
    c64_state(c)->program[0] = 0;
    return 0;
}
static const MountSlots mount_slots = { 2, slot_names, slot_spec, slot_mount, slot_eject };

static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };
static const ArgSpec run_args[] = { ARG_PATH("path") };
static const ArgSpec t64_args[] = { ARG_PATH("path"), ARG_INT("index", 0, 255) };
static const ArgSpec rom_set_args[] = { ARG_PATH("path"), ARG_INT("which", 0, 2) };
static const ArgSpec load_args[] = { ARG_PATH("path"), ARG_INT("start", 0, 0xFFFF) };

static const MemRegion c64_mem_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "RAM" } };
static const MemMap c64_mem = { c64_mem_regions, 1, true, 0 };

static void c64_register(OpRegistry &r)
{
    r.contributor("c64");
    MEM_OPS(r, c64_mem, 0xFFFF);
    r.add_op("c64.put_basic", rom_arg, 1, op_put_basic);
    r.add_op("c64.put_kernal", rom_arg, 1, op_put_kernal);
    r.add_op("c64.put_char", rom_arg, 1, op_put_char);
    r.add_op("c64.set_freeze_mode", freeze_arg, 1, op_freeze_mode);
    r.add_op("c64.run_prg", run_args, 1, op_run);
    r.add_op(PRG_APP_OP, run_args, 1, op_run_with_app);
    r.add_op("c64.load_prg", load_args, 2, op_load);
    r.add_op("c64.run_crt", run_args, 1, op_run_crt);
    r.add_op("c64.dma_prg", run_args, 1, op_dma);
    r.add_op("c64.run_t64", t64_args, 2, op_run_t64);
    r.add_op("c64.dma_t64", t64_args, 2, op_dma_t64);
    r.add_op("c64.set_rom", rom_set_args, 2, op_set_rom);
    r.add_op("c64.remove_cart", 0, 0, op_remove_cart);
    r.add_op("c64.service", 0, 0, op_service);
    r.add_op("c64.type_script", text_arg, 1, op_type);
    r.add_state("c64.program", prg_name);
    r.add_state("c64.pending", prg_pending);
    r.add_state("c64.program_path", prg_path);
    cbm1541_register(r);
    u64cfg_register(r);
    ringops_register(r);
    monitor_register(r);
    online_register(r);
    diag_register(r);
    trx64u_register(r);
}

// What the machine may have (T104.2 D3). The board strikes the names its probes deny: the monolith keeps all seven, the MultiCore `trx64`
// core has no SID sockets, and a build without the UCI loses that one.
static const char *const caps[] = { "iec", "drive_a", "cartridge", "reu", "ring", "uci", "sid_sockets" };

// The USB mouse as a 1351 on the POT lines (T104.1 5.6): the library keeps the position, the board's pots write it (mousesink.h).
static const MouseSinkSpec mouse = { 0, 0, 0, 0, 0, 0, MOUSE_POT1351 };

// The menu record: tools/gen_menu_rec.py writes it from menu.toml (the lint stage and tests/coreapp/run.sh compare), the packer puts the same bytes into
// the .cor. A start without a .cor (JTAG, ue2emu) reads this copy (bundle.cc).
#include "trx64u/menu_rec.inc"

extern const CoreDesc c64_desc;
// T162 D3: the state block of the bug report (reads without side effects only; the state bus and the debugger's command registers are left out)
static const ReportReg report_regs[] = {
    { "CAPS", C64_CAPS, 1 }, { "CAPS2", C64_CAPS2, 1 }, { "GATE_CTRL (b0 freeze)", C64_GATE_CTRL, 1 }, { "GATE_STATUS (b0 halted)", C64_GATE_STATUS, 1 },
};

const CoreDesc c64_desc = {
    { 'T', 'R', 'X', '2' },
    "TRX64U",                                  // the manifest name of trx64u.cor: the hand-over and the bundle's records are matched by it
    "TRX64 C64",
    "C64",
    "c64",
    "C64 Settings",
    1,
    roms, 4,
    0, 0,                                      // settings, tasks, file types and media slots: the menu record (menu_rec)
    0, 0,
    0, 0,
    0, 0,
    // the C64's picture goes through Gideon's video path, not through the pixel layer: no buffers, no registers (D5)
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { C64_GATE_STATUS, C64_GATE_HALTED, 200, true },       // b8 of the 32 bit GATE_CTRL: read as a word
    c64_init, c64_release, 0, 50,
    "c64.service", 20, false,                  // a PRG waits for READY while the machine runs, not while the menu holds it
    0, 0,
    c64_register,
    caps, 7,
    true,
    false, 0, 0, 0, 0, 0, &mouse, 0,
    "c64.type_script",
    "c64.run_prg",
    0, 0, 0, 0, 0,                             // cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    c64_menu_rec, sizeof(c64_menu_rec),
    0,                                         // core_id
    report_regs, (int)(sizeof(report_regs) / sizeof(report_regs[0])),
    0,                                         // rawkeys
    true,                                      // own_volume: the TRX64U mixer (T160 P4), no generic Volume
    0, 0, false,                               // pad, live_ms, menu_first
    cbm1541_media_flush, cbm1541_media_pending,   // T180 D4: the drive's write-back
    &mount_slots,                              // T180 D2, D5: Drive A and Cartridge
    &hk_c64, 0, true,                      // T181: the default hotkey map (hk_defaults.h); alt_video, no_tone (the TRX64-Ultimate bitstream has no host/trx_tone.v)
};
