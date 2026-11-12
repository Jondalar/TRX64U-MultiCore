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
    r->job = 2;                                         // after the reset: the machine's init clears a job that was still waiting
    snprintf(r->script, sizeof(r->script), "%s", text);
    r->script_pos = 0;
    r->ran_ms = r->last_ms = r->ready_at = 0;
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

int cbm_run_service(CoreCtx *c, CbmRun *r, const CbmRunMachine &m)
{
    if (!r->job) {
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
    uint8_t b[2] = { 0, 0 };
    c->io->ram_read(m.prg.txttab, b, 2);
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
    if (cbm_prg_place(c->io, prgbuf, size, m.prg, is_p00(path), &res, msg, msglen)) {
        return 1;
    }
    fs_short_name(r->program, sizeof(r->program), base_name(path));
    printf("[%s] %s: $%04X-$%04X loaded%s\n", m.tag, base_name(path), res.load, (unsigned)(res.end - 1) & 0xFFFF,
           res.basic ? " (BASIC, not started)" : "");
    return 0;
}
