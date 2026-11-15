#include "cbm_run.h"
#include "boot.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// One buffer for the file, shared by the machines of this image: 64 KB and the .P00 header. Only one operation runs at a time (exec.h).
static uint8_t prgbuf[0x1A + 2 + 0x10000];

static bool is_p00(const char *path)
{
    size_t l = strlen(path);
    return l > 4 && !strcasecmp(path + l - 4, ".p00");
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void tell(CoreCtx *c, const CbmRunMachine &m, const char *what)
{
    printf("[%s] %s\n", m.tag, what);
    snprintf(c->notice, sizeof(c->notice), "%s", what);
    c->notice_pending = true;
}

// The whole file into prgbuf; checks the size window, and what the machine refuses. 0 or a reason.
static int read_prg(CoreCtx *c, const CbmRunMachine &m, const char *path, uint32_t *size, char *msg, int msglen)
{
    const CbmPrgParams &p = m.prg;
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
    if (load + len > 0x10000u) {                        // a file that would wrap past $FFFF is refused before the machine is touched
        snprintf(msg, msglen, "%s: $%04X + %u bytes runs past $FFFF", path, (unsigned)load, (unsigned)len);
        return 1;
    }
    if (m.refuse) {
        const char *why = m.refuse(c, load, len);
        if (why) {
            snprintf(msg, msglen, "%s", why);
            return 1;
        }
    }
    *size = (uint32_t)n;
    return 0;
}

void cbm_run_clear(CbmRun *r)
{
    r->job = 0;
}

const char *cbm_run_name(const CbmRun *r)
{
    return r->program[0] ? r->program : 0;
}

const char *cbm_run_pending(const CbmRun *r)
{
    return r->job ? (r->job == 2 ? "type" : "run") : 0;
}

// T217: the bytes just written to the CPU's own memory are in the DDR2; its write-through cache may still hold older lines of them: invalidate it
// (the register finishes in 512 clocks, the same wait as the ROM images'). 0 or a reason.
static int mem_sync(CoreCtx *c, const CbmRunMachine &m, char *msg, int msglen)
{
    if (!m.prg.mem_ddr) {
        return 0;
    }
    c->io->wr(m.mem_flush_reg, 0x01);
    uint32_t t0 = c->io->now_ms();
    while (c->io->rd(m.mem_flush_reg) & 0x01) {
        if (c->io->now_ms() - t0 > 100) {
            snprintf(msg, msglen, "the CPU's cache did not take the program (flush did not finish)");
            return 1;
        }
        c->io->delay_ms(1);
    }
    return 0;
}

static const uint8_t zero_keys[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

int cbm_run_queue_script(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *text, char *msg, int msglen)
{
    if (cbm_prg_script_check(text, msg, msglen)) {
        return 1;
    }
    if (strlen(text) >= sizeof(r->script)) {
        snprintf(msg, msglen, "the typed text is too long");
        return 1;
    }
    if (boot_cold_reset(c, msg, msglen)) {
        return 1;
    }
    if (r->kphase) {
        c->io->matrix_keys(zero_keys);                  // a typed start still holding a key is replaced
    }
    r->job = 2;                                         // after the reset: the machine's init clears a job that was still waiting
    snprintf(r->script, sizeof(r->script), "%s", text);
    r->script_pos = 0;
    r->ran_ms = r->last_ms = r->ready_at = 0;
    r->kidx = 0xFF;
    r->kphase = 0;
    r->kwait = true;                                    // the matrix method starts when the prompt is on the screen
    r->kt = 0;
    printf("[%s] reset, typing queued for READY\n", m.tag);
    return 0;
}

int cbm_run_queue(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *path, char *msg, int msglen)
{
    uint32_t size;
    if (read_prg(c, m, path, &size, msg, msglen)) {
        return 1;
    }
    if (boot_cold_reset(c, msg, msglen)) {
        return 1;
    }
    r->job = 1;                                         // after the reset: the machine's init clears a job that was still waiting
    r->p00 = is_p00(path);
    r->size = size;
    r->ran_ms = r->last_ms = r->ready_at = 0;
    fs_short_name(r->name, sizeof(r->name), base_name(path));
    printf("[%s] %s: reset, run queued for READY\n", m.tag, path);
    return 0;
}

// ---- T206: the typed start through the key matrix ----------------------------------------------------------------------------------
#define K_LSHIFT_IDX 15
#define SHIFT_LEAD_MS 40                                // L SHIFT is down alone this long before the key joins it: the scan never sees the key without it
#define KEYS_PROMPT_MIN_MS 600                          // after RETURN: the least before the screen is asked for the prompt again
#define KEYS_PROMPT_MAX_MS 240000                       // ... and the most (a LOAD from the drive runs at the drive's speed)

// Unshifted keys by index (col * 8 + row), the PETSCII character they type; 0 = a key that types no character in a name or a command.
static const char key_char[64] = {
    0, '\r', 0, 0, 0, 0, 0, 0,        '3', 'W', 'A', '4', 'Z', 'S', 'E', 0,
    '5', 'R', 'D', '6', 'C', 'F', 'T', 'X',   '7', 'Y', 'G', '8', 'B', 'H', 'U', 'V',
    '9', 'I', 'J', '0', 'M', 'K', 'O', 'N',   '+', 'P', 'L', '-', '.', ':', '@', ',',
    0, '*', ';', 0, 0, '=', 0, '/',            '1', 0, 0, '2', ' ', 0, 'Q', 0 };
// The characters that need L SHIFT, and the key it goes with.
static const struct { char ch; char base; } shifted[] = {
    { '!', '1' }, { '"', '2' }, { '#', '3' }, { '$', '4' }, { '%', '5' }, { '&', '6' }, { '\'', '7' }, { '(', '8' }, { ')', '9' },
    { '<', ',' }, { '>', '.' }, { '?', '/' }, { '[', ':' }, { ']', ';' } };

static int idx_of(char ch)
{
    for (int i = 0; i < 64; i++) {
        if (key_char[i] == ch) {
            return i;
        }
    }
    return -1;
}

bool cbm_key_for(uint8_t ch, CbmKey *k)
{
    int i = ch ? idx_of((char)ch) : -1;
    if (i >= 0) {
        k->idx = (uint8_t)i;
        k->shift = false;
        return true;
    }
    if (ch >= 0xC1 && ch <= 0xDA) {                     // SHIFT + letter
        k->idx = (uint8_t)idx_of((char)(ch - 0xC1 + 'A'));
        k->shift = true;
        return true;
    }
    for (unsigned n = 0; n < sizeof(shifted) / sizeof(shifted[0]); n++) {
        if (shifted[n].ch == (char)ch) {
            k->idx = (uint8_t)idx_of(shifted[n].base);
            k->shift = true;
            return true;
        }
    }
    return false;
}

static void put_keys(CoreCtx *c, int a, int b)           // a, b: key indexes down (-1: none)
{
    uint8_t m[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (a >= 0) {
        m[a / 8] |= (uint8_t)(1u << (a % 8));
    }
    if (b >= 0) {
        m[b / 8] |= (uint8_t)(1u << (b % 8));
    }
    c->io->matrix_keys(m);
}

// The screen at m.screen: the last row with a character on it is "READY." alone (screen codes 18 5 1 4 25 46, the rest blank) - the BASIC prompt
// with nothing running and nothing half typed. The RAM window sees the screen on the SuperCPU (the pages $02-$FF are mirrored to the C64 RAM
// and the VIC reads it); the BASIC pointers in page 0 are not (cores/c64scpu/scpu_mem.v: mir_tab).
static bool screen_at_prompt(CoreCtx *c, const CbmRunMachine &m)
{
    static uint8_t scr[1000];
    c->io->ram_read(m.screen, scr, sizeof(scr));
    for (int row = 24; row >= 0; row--) {
        const uint8_t *p = scr + row * 40;
        bool blank = true;
        for (int i = 0; i < 40; i++) {
            blank = blank && (p[i] & 0x7F) == 32;
        }
        if (blank) {
            continue;
        }
        static const uint8_t ready[6] = { 18, 5, 1, 4, 25, 46 };
        for (int i = 0; i < 40; i++) {
            if ((p[i] & 0x7F) != (i < 6 ? ready[i] : 32)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static int type_keys(CoreCtx *c, CbmRun *r, const CbmRunMachine &m)
{
    uint32_t el = r->ran_ms - r->kt;
    if (r->kphase == 1) {
        if (el >= SHIFT_LEAD_MS) {
            put_keys(c, K_LSHIFT_IDX, r->kidx);
            r->kphase = 2;
            r->kt = r->ran_ms;
        }
        return OPE_OK;
    }
    if (r->kphase == 2) {
        if (el >= m.key_hold_ms) {
            put_keys(c, -1, -1);
            r->kphase = 0;
            r->kt = r->ran_ms;
        }
        return OPE_OK;
    }
    if (el < m.key_gap_ms) {
        return OPE_OK;
    }
    if (!r->script[r->script_pos] || (r->script[r->script_pos] == '|' && !r->script[r->script_pos + 1])) {
        r->job = 0;                                     // all typed (the last RETURN needs no prompt after it)
        return OPE_OK;
    }
    if (r->kwait) {
        bool first = !r->ready_at;
        if (el < (first ? 0u : KEYS_PROMPT_MIN_MS) || !screen_at_prompt(c, m)) {
            if (first ? r->ran_ms > (uint32_t)m.ready_ms + m.giveup_ms : el > KEYS_PROMPT_MAX_MS) {
                r->job = 0;
                tell(c, m, first ? "The machine did not reach READY: the program was not started (are the ROMs the right ones?)"
                                 : "The machine did not come back to READY: the typed start stopped");
            }
            return OPE_OK;
        }
        if (first) {
            r->ready_at = r->ran_ms;
            return OPE_OK;                              // READY seen: the next look types, after settle_ms below
        }
        r->kwait = false;
    }
    if (r->ready_at && r->ran_ms - r->ready_at < m.settle_ms && r->script_pos == 0) {
        return OPE_OK;
    }
    if (r->script[r->script_pos] == '|') {
        r->script_pos++;
    }
    uint8_t ch = (uint8_t)r->script[r->script_pos++];
    CbmKey k;
    if (!cbm_key_for(ch, &k)) {
        cbm_key_for('?', &k);                           // what no key types is the wildcard, as in a name inside the quotes
    }
    r->kidx = k.idx;
    r->kshift = k.shift;
    r->kwait = ch == '\r';
    r->kt = r->ran_ms;
    if (k.shift) {
        put_keys(c, K_LSHIFT_IDX, -1);
        r->kphase = 1;
    } else {
        put_keys(c, k.idx, -1);
        r->kphase = 2;
    }
    return OPE_OK;
}

// T217: a program on a machine that is started through the key matrix (m.screen): READY is the screen (as for a typed script), the bytes go into the
// CPU's own memory (m.prg.mem_ddr), and the start - RUN, or SYS <address> - is typed like a person's, as job 2. The keyboard buffer and the BASIC
// pointers are page 0 of the SuperCPU's SRAM, which the application's RAM window does not reach (cores/c64scpu/scpu_mem.v: mir_tab mirrors CPU writes
// of the pages $02-$FF to the C64 RAM, one way).
static int place_and_type(CoreCtx *c, CbmRun *r, const CbmRunMachine &m)
{
    if (!r->ready_at) {
        if (screen_at_prompt(c, m)) {
            r->ready_at = r->ran_ms;
        } else if (r->ran_ms > (uint32_t)m.ready_ms + m.giveup_ms) {
            r->job = 0;
            tell(c, m, "The machine did not reach READY: the program was not started (are the ROMs the right ones?)");
        }
        return OPE_OK;
    }
    if (r->ran_ms - r->ready_at < m.settle_ms) {
        return OPE_OK;
    }
    char msg[200];
    msg[0] = 0;
    CbmPrgResult res;
    memset(&res, 0, sizeof(res));
    if (cbm_prg_place(c->io, prgbuf, r->size, m.prg, r->p00, &res, msg, sizeof(msg)) || mem_sync(c, m, msg, sizeof(msg))) {
        r->job = 0;
        tell(c, m, msg);
        return OPE_OK;
    }
    if (res.basic) {
        snprintf(r->script, sizeof(r->script), "RUN\r");
    } else {
        snprintf(r->script, sizeof(r->script), "SYS%u\r", (unsigned)res.start);
    }
    printf("[%s] %s: $%04X-$%04X, %s\n", m.tag, r->name, res.load, (unsigned)(res.end - 1) & 0xFFFF, res.basic ? "RUN" : "SYS");
    snprintf(r->program, sizeof(r->program), "%s", r->name);
    r->job = 2;
    r->script_pos = 0;
    r->kidx = 0xFF;
    r->kphase = 0;
    r->kwait = false;
    r->kt = r->ran_ms;
    return OPE_OK;
}

int cbm_run_service(CoreCtx *c, CbmRun *r, const CbmRunMachine &m)
{
    if (!r->job) {
        if (r->kphase) {                                // a reset ended the job with a key down: let go
            put_keys(c, -1, -1);
            r->kphase = 0;
        }
        return OPE_OK;
    }
    uint32_t now = c->io->now_ms();
    if (r->last_ms) {
        uint32_t dt = now - r->last_ms;
        r->ran_ms += dt > 200 ? 100 : dt;               // a gap is the menu's: the machine did not run in it
    }
    r->last_ms = now;
    if (r->ran_ms < m.ready_ms) {
        return OPE_OK;
    }
    if (r->job == 2 && m.screen) {
        return type_keys(c, r, m);
    }
    if (r->job == 1 && m.screen) {
        return place_and_type(c, r, m);
    }
    uint8_t b[2] = { 0, 0 };
    cbm_prg_mem_read(c->io, m.prg, m.prg.txttab, b, 2);
    uint16_t txt = (uint16_t)(b[0] | (b[1] << 8));
    bool basic_up = false;
    for (int i = 0; i < m.prg.nbasic_starts; i++) {
        basic_up = basic_up || txt == m.prg.basic_starts[i];
    }
    if (r->job == 2 && r->ready_at) {
        basic_up = true;                                // typing runs a program that may move the pointer: READY was seen once
    }
    if (!basic_up) {
        if (r->ran_ms > (uint32_t)m.ready_ms + m.giveup_ms) {
            r->job = 0;
            tell(c, m, "The machine did not reach READY: the program was not started (are the ROMs the right ones?)");
        }
        return OPE_OK;
    }
    if (!r->ready_at) {
        r->ready_at = r->ran_ms;
    }
    if (r->ran_ms - r->ready_at < m.settle_ms) {
        return OPE_OK;
    }
    if (r->job == 2) {
        if (r->ran_ms - r->ready_at > 60000) {
            r->job = 0;
            tell(c, m, "The machine did not take the typed start (is the editor stuck?)");
            return OPE_OK;
        }
        if (cbm_prg_script_step(c->io, m.prg, r->script, &r->script_pos)) {
            r->job = 0;
        }
        return OPE_OK;
    }
    char msg[200];
    msg[0] = 0;
    CbmPrgResult res;
    memset(&res, 0, sizeof(res));
    if (cbm_prg_place(c->io, prgbuf, r->size, m.prg, r->p00, &res, msg, sizeof(msg))) {
        tell(c, m, msg);
    } else {
        cbm_prg_type_start(c->io, m.prg, res);
        printf("[%s] %s: $%04X-$%04X, %s\n", m.tag, r->name, res.load, (unsigned)(res.end - 1) & 0xFFFF, res.basic ? "RUN" : "SYS");
        snprintf(r->program, sizeof(r->program), "%s", r->name);
    }
    r->job = 0;
    return OPE_OK;
}

int cbm_load_now(CoreCtx *c, CbmRun *r, const CbmRunMachine &m, const char *path, char *msg, int msglen)
{
    uint32_t size;
    if (read_prg(c, m, path, &size, msg, msglen)) {
        return 1;
    }
    CbmPrgResult res;
    if (cbm_prg_place(c->io, prgbuf, size, m.prg, is_p00(path), &res, msg, msglen) || mem_sync(c, m, msg, msglen)) {
        return 1;
    }
    fs_short_name(r->program, sizeof(r->program), base_name(path));
    printf("[%s] %s: $%04X-$%04X loaded%s\n", m.tag, base_name(path), res.load, (unsigned)(res.end - 1) & 0xFFFF,
           res.basic ? " (BASIC, not started)" : "");
    return 0;
}
