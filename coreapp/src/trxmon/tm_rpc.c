/*
 * rpc.c — JSON-RPC dispatch (see rpc.h).
 *
 * Every method that runs the machine answers as TRX64 does; the stop that
 * ends a free run comes later as notifications (debug/breakpoint_hit,
 * debug/observer_hit, debug/stopped). A run that answers when it stops
 * (session/run, the monitor's until / n / ret) is held as the one pending
 * request and answered from rpc_poll().
 */
#include "rpc.h"
#include "json.h"
#include "mach.h"
#include "mon.h"
#include "ring.h"
#include "trace.h"
#include "spans.h"
#include "ws.h"

#define SESSION_ID   "c64u"
#define MAX_TOK      512
#define MAX_READ     (32 * 1024)
#define RUN_CAP_MS   10000u

static char s_reply[MAX_READ * 2];
static char s_text[48 * 1024];
static char s_note[4096];
static uint8_t s_mem[MAX_READ];
static char s_b64[MAX_READ * 4 / 3 + 8];
static jtok_t s_tok[MAX_TOK];

static sbuf_t R;
static jw_t W;
static int s_quit;
static int s_cap; /* debug/run {cycles} armed CYC_CMP */

enum { P_NONE = 0, P_RUN, P_MON, P_LOCAL };
static struct
{
    int kind;
    int cid;
    char id[48];
    int idlen;
    mon_pending_t mp;
    uint32_t deadline;
} P;

/* request context */
static const char *Q;   /* the request text */
static int Qparams;     /* index of params, -1 */
static char Qid[48];
static int Qidlen;
static int Qcid;

/* ---- writing ----------------------------------------------------------- */

static void begin(void)
{
    sb_init(&R, s_reply, sizeof(s_reply));
    jw_init(&W, &R);
    jw_obj(&W);
    jw_kstr(&W, "jsonrpc", "2.0");
    jw_key(&W, "id");
    if (Qidlen) {
        jw_raw(&W, Qid, Qidlen);
    } else {
        jw_null(&W);
    }
}

static void begin_result(void)
{
    begin();
    jw_key(&W, "result");
}

static void finish(int cid)
{
    jw_end_obj(&W);
    if (R.full) {
        /* the answer did not fit: say so instead of sending broken JSON */
        sb_init(&R, s_reply, sizeof(s_reply));
        jw_init(&W, &R);
        jw_obj(&W);
        jw_kstr(&W, "jsonrpc", "2.0");
        jw_key(&W, "id");
        if (Qidlen) {
            jw_raw(&W, Qid, Qidlen);
        } else {
            jw_null(&W);
        }
        jw_key(&W, "error");
        jw_obj(&W);
        jw_key(&W, "code");
        jw_i64(&W, -32603);
        jw_kstr(&W, "message", "the answer is larger than trxmon's buffer; ask for less");
        jw_end_obj(&W);
        jw_end_obj(&W);
    }
    if ((cid >= 0) && Qidlen) { /* a notification (no id) gets no answer */
        srv_send(cid, R.p, R.len);
    }
}

static void send_error(int code, const char *msg, const char *arg)
{
    if (!Qidlen) {
        return; /* a notification gets no answer */
    }
    begin();
    jw_key(&W, "error");
    jw_obj(&W);
    jw_key(&W, "code");
    jw_i64(&W, code);
    char m[256];
    sbuf_t b;
    sb_init(&b, m, sizeof(m));
    sb_put(&b, msg);
    if (arg) {
        sb_put(&b, arg);
    }
    jw_kstr(&W, "message", sb_str(&b));
    jw_end_obj(&W);
    finish(Qcid);
}

static sbuf_t NB;
static jw_t NW;

static void note_begin(const char *method)
{
    sb_init(&NB, s_note, sizeof(s_note));
    jw_init(&NW, &NB);
    jw_obj(&NW);
    jw_kstr(&NW, "jsonrpc", "2.0");
    jw_kstr(&NW, "method", method);
    jw_key(&NW, "params");
    jw_obj(&NW);
    jw_kstr(&NW, "session_id", SESSION_ID);
}

static void note_send(void)
{
    jw_end_obj(&NW);
    jw_end_obj(&NW);
    srv_broadcast(NB.p, NB.len);
}

static void registers_str(jw_t *w, const char *key)
{
    char t[96];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    mon_register_dump(&b);
    jw_key(w, key);
    jw_strn(w, t, b.len);
}

static void pacing(jw_t *w)
{
    jw_key(w, "pacing");
    jw_obj(w);
    jw_kstr(w, "mode", "realtime");
    jw_ku64(w, "ratio", 1);
    jw_end_obj(w);
}

static void stop_obj(jw_t *w)
{
    jw_obj(w);
    jw_kstr(w, "reason", M.stop.reason);
    jw_ku64(w, "pc", M.stop.pc);
    jw_ku64(w, "cycles", M.stop.cycles);
    if (M.stop.bp_num) {
        jw_ku64(w, "breakpointId", (uint64_t)M.stop.bp_num);
    }
    if (M.stop.obs >= 0) {
        jw_kstr(w, "breakpoint", M.obs[M.stop.obs].name);
    }
    jw_end_obj(w);
}

static void breakpoints(jw_t *w)
{
    jw_arr(w);
    for (int i = 0; i < M.nbp; i++) {
        jw_obj(w);
        jw_ku64(w, "num", M.bp[i].num);
        jw_ku64(w, "addr", M.bp[i].pc);
        jw_end_obj(w);
    }
    jw_end_arr(w);
}

static uint16_t cur_pc(void)
{
    hw_cpu_t c;
    mach_cpu(&c);
    return mach_pc(&c);
}

/* TRX64 build_debug_state */
static void debug_state(jw_t *w)
{
    jw_obj(w);
    jw_kstr(w, "runState", M.running ? "running" : "paused");
    pacing(w);
    jw_ku64(w, "pc", cur_pc());
    jw_ku64(w, "cycles", mach_cycles());
    jw_ku64(w, "frame", M.frame);
    jw_key(w, "breakpoints");
    breakpoints(w);
    jw_key(w, "stop");
    if (M.have_stop && !M.running) {
        stop_obj(w);
    } else {
        jw_null(w);
    }
    jw_kstr(w, "controlOwner", M.owner);
    jw_end_obj(w);
}

static void identity(jw_t *w)
{
    hw_video_t v;
    hw_video(&v);
    jw_kstr(w, "model", v.hz60 ? "c64-ntsc" : "c64-pal");
    jw_kstr(w, "videoStandard", v.hz60 ? "NTSC" : "PAL");
    jw_kstr(w, "chip", v.hz60 ? "6567" : "6569");
    jw_ku64(w, "cyclesPerLine", v.cycles_per_line);
    jw_ku64(w, "linesPerFrame", v.lines);
    jw_ku64(w, "cyclesPerFrame", v.frame_cycles);
    jw_ku64(w, "cpuHz", v.cpu_hz);
    jw_key(w, "frameRate");
    char t[16];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    sb_dec(&b, v.fps_x100 / 100);
    sb_putc(&b, '.');
    sb_putc(&b, (char)('0' + (v.fps_x100 / 10) % 10));
    sb_putc(&b, (char)('0' + v.fps_x100 % 10));
    jw_raw(w, b.p, b.len);
    jw_key(w, "canvas");
    jw_obj(w);
    jw_ku64(w, "width", 384);
    jw_ku64(w, "height", 272);
    jw_end_obj(w);
}

/* ---- params ------------------------------------------------------------ */

static int param(const char *key) { return json_get(Q, s_tok, Qparams, key); }

static int param_u64(const char *key, uint64_t *v) { return json_u64(Q, s_tok, param(key), v); }

static void take_source(void)
{
    M.owner = json_streq(Q, s_tok, param("source"), "llm") ? "llm" : "human";
}

/* ---- stops ------------------------------------------------------------- */

/* The pending request's id stands in for the current one while it is answered */
typedef struct
{
    char id[48];
    int idlen, cid;
} qsave_t;

static void q_swap_in(qsave_t *sv)
{
    memcpy(sv->id, Qid, (size_t)Qidlen);
    sv->idlen = Qidlen;
    sv->cid = Qcid;
    memcpy(Qid, P.id, (size_t)P.idlen);
    Qidlen = P.idlen;
    Qcid = P.cid;
}

static void q_swap_out(const qsave_t *sv)
{
    memcpy(Qid, sv->id, (size_t)sv->idlen);
    Qidlen = sv->idlen;
    Qcid = sv->cid;
    P.kind = P_NONE;
}

static void finish_run(void)
{
    qsave_t sv;
    q_swap_in(&sv);
    if (m_streq(M.stop.reason, "cycles") && !M.stop.at_ib) {
        mach_step(1, 0); /* end the instruction in flight, as TRX64's budget does */
    }
    begin_result();
    jw_obj(&W);
    jw_ku64(&W, "c64Cycles", M.stop.cycles);
    if (m_streq(M.stop.reason, "breakpoint") || m_streq(M.stop.reason, "observer")) {
        jw_key(&W, "breakpoint");
        jw_obj(&W);
        jw_ku64(&W, "pc", M.stop.pc);
        jw_ku64(&W, "num", (uint64_t)M.stop.bp_num);
        registers_str(&W, "registers");
        jw_end_obj(&W);
    } else if (!m_streq(M.stop.reason, "cycles") && !m_streq(M.stop.reason, "step")) {
        jw_key(&W, "stop");
        stop_obj(&W);
    }
    jw_end_obj(&W);
    finish(Qcid);
    q_swap_out(&sv);
}

/* The monitor reply: {output|error, spans, machine} */
static void mon_reply(int rc, sbuf_t *t)
{
    /* strip the marks into a separate spans buffer, then write output first */
    static char spans[16384];
    sbuf_t sb;
    sb_init(&sb, spans, sizeof(spans));
    jw_t sw;
    jw_init(&sw, &sb);
    int n = span_strip(t->p, t->len, &sw);
    begin_result();
    jw_obj(&W);
    jw_key(&W, rc == MON_ERR ? "error" : "output");
    jw_strn(&W, t->p, n);
    jw_key(&W, "spans");
    if (sb.full) {
        jw_raw(&W, "[]", 2); /* too many to tell: none rather than broken JSON */
    } else {
        jw_raw(&W, sb.p, sb.len);
    }
    jw_key(&W, "machine");
    mon_machine_json(&W);
    jw_end_obj(&W);
}

static void finish_mon(int capped)
{
    qsave_t sv;
    q_swap_in(&sv);
    sbuf_t t;
    sb_init(&t, s_text, sizeof(s_text));
    mon_finish(&P.mp, capped, &t);
    mon_reply(MON_OK, &t);
    finish(Qcid);
    q_swap_out(&sv);
}

/* A stop the machine made by itself (a breakpoint, an observer, a cap) */
static void notify_stop(void)
{
    int bp = m_streq(M.stop.reason, "breakpoint");
    int ob = m_streq(M.stop.reason, "observer");
    if (bp || ob) {
        note_begin(bp ? "debug/breakpoint_hit" : "debug/observer_hit");
        jw_ku64(&NW, "pc", M.stop.pc);
        if (bp) {
            jw_ku64(&NW, "num", (uint64_t)M.stop.bp_num);
        }
        jw_ku64(&NW, "cycles", M.stop.cycles);
        if (ob) {
            jw_key(&NW, "observer");
            if (M.stop.obs >= 0) {
                jw_str(&NW, M.obs[M.stop.obs].name);
            } else {
                jw_null(&NW);
            }
            jw_knull(&NW, "message");
        }
        registers_str(&NW, "registers");
        note_send();
    }
    note_begin("debug/stopped");
    jw_key(&NW, "stop");
    stop_obj(&NW);
    registers_str(&NW, "registers");
    note_send();
}

/* The person at the machine (ui.c, T21 §8): a run-to verb typed at the screen waits here like a
 * client's, and a stop that nobody waited for is kept for the screen. */
static char s_local_text[2048];
static int s_local_len, s_local_done;
static int s_unclaimed;  /* a stop came with no run waiting for it */
static int s_user_stop;  /* a breakpoint or an observer stopped the machine */

static void finish_local(int capped)
{
    sbuf_t t;
    sb_init(&t, s_local_text, (int)sizeof(s_local_text) - 1);
    mon_finish(&P.mp, capped, &t);
    s_local_len = t.len;
    s_local_text[t.len] = 0;
    s_local_done = 1;
    P.kind = P_NONE;
}

static void stop_came(int capped)
{
    s_cap = 0;
    if (m_streq(M.stop.reason, "breakpoint") || m_streq(M.stop.reason, "observer") || m_streq(M.stop.reason, "jam")) {
        s_user_stop = 1; /* a JAM is a stop nobody asked for: the person sees where the CPU hangs (T21 8.5) */
    }
    if (P.kind == P_RUN) {
        finish_run();
    } else if (P.kind == P_MON) {
        finish_mon(capped);
    } else if (P.kind == P_LOCAL) {
        finish_local(capped);
    } else {
        s_unclaimed = 1;
        notify_stop();
    }
}

static void note_running(void);

/* The lines of the log observers as they come (debug/observer_log, {"line"}): the same lines and the same rate as
 * the screen shows; nobody is told about lines from before the poll that sees them */
static uint32_t s_olog_cur;
static int s_olog_init;

static void notify_olog(void)
{
    if (!s_olog_init) {
        s_olog_init = 1;
        s_olog_cur = mach_olog_seq();
        return;
    }
    /* a holder that has sent nothing (not even the pong to the keepalive ping at 20 s) for 25 s does not read either: the
     * firmware's send would wait for its full buffer for up to 25 s. It gets no log until it is heard from again. */
    if (srv_holder_idle_ms() > 25000u) {
        s_olog_cur = mach_olog_seq();
        return;
    }
    /* a packet costs the firmware's network stack milliseconds: the lines of 100 ms go out together */
    static uint32_t last_flush;
    uint32_t pending = mach_olog_seq() - s_olog_cur;
    uint32_t now = plat_ms();
    if (!pending || ((pending < 16) && ((uint32_t)(now - last_flush) < 100))) {
        return;
    }
    last_flush = now;
    const char *l;
    int n = 0;
    srv_cork(1);
    while ((n < 24) && ((l = mach_olog_next(&s_olog_cur)) != 0)) {
        note_begin("debug/observer_log");
        jw_kstr(&NW, "line", l);
        note_send();
        n++;
    }
    srv_cork(0);
}

void rpc_poll(void)
{
    int r = mach_poll();
    notify_olog();
    if (r == 1) {
        stop_came(0);
    } else if (r == 2) {
        /* the firmware took the machine (menu, DMA load, reset): a waiting run
         * ends here, and the machine runs on once the firmware lets it */
        if (P.kind != P_NONE) {
            stop_came(1);
        }
        note_running();
    } else if ((P.kind != P_NONE) && ((int32_t)(plat_ms() - P.deadline) > 0)) {
        mach_pause();
        stop_came(1);
    }
}

void rpc_client_gone(int cid)
{
    if (P.cid == cid) {
        P.cid = -1;
    }
}

int rpc_quit_requested(void) { return s_quit || mon_quit_requested(); }

int rpc_local_exec(const char *cmd, sbuf_t *out)
{
    M.owner = "human";
    rpc_poll();
    mon_set_narrow(1);
    mon_pending_t mp;
    int rc = mon_exec(cmd, out, &mp);
    mon_set_narrow(0);
    if (rc == MON_PENDING) {
        if (P.kind != P_NONE) {
            sb_reset(out);
            sb_put(out, "another run is still waiting for its stop; pause first");
            mach_pause();
            return MON_ERR;
        }
        P.kind = P_LOCAL;
        P.cid = -1;
        P.idlen = 0;
        P.deadline = plat_ms() + RUN_CAP_MS;
        P.mp = mp;
        return MON_PENDING;
    }
    if ((P.kind != P_NONE) && !M.running) {
        /* a pause (RUN/STOP) ends a run that was waiting for its stop, as debug/pause does */
        stop_came(0);
    }
    return rc;
}

int rpc_local_take(sbuf_t *out)
{
    if (!s_local_done) {
        return 0;
    }
    s_local_done = 0;
    sb_putn(out, s_local_text, s_local_len);
    return 1;
}

int rpc_local_waiting(void) { return P.kind == P_LOCAL; }

int rpc_take_unclaimed(void)
{
    int r = s_unclaimed;
    s_unclaimed = 0;
    return r;
}

int rpc_take_user_stop(void)
{
    int r = s_user_stop;
    s_user_stop = 0;
    return r;
}

int rpc_stops_armed(void)
{
    for (int i = 0; i < M.nbp; i++) {
        if (M.bp[i].enabled) {
            return 1;
        }
    }
    for (int i = 0; i < M.nobs; i++) {
        if (M.obs[i].on) {
            return 1;
        }
    }
    return s_cap || M.drv_bp_on;
}

static int hold_pending(int kind, uint32_t ms)
{
    if (P.kind != P_NONE) {
        send_error(-32001, "another run is still waiting for its stop; debug/pause ends it", 0);
        return 0;
    }
    P.kind = kind;
    P.cid = Qcid;
    memcpy(P.id, Qid, (size_t)Qidlen);
    P.idlen = Qidlen;
    P.deadline = plat_ms() + ms;
    return 1;
}

/* ---- methods ----------------------------------------------------------- */

/* every notification trxmon sends (note_begin callers); ping lists them */
static const char *const notifications[] = { "debug/breakpoint_hit", "debug/observer_hit", "debug/observer_log",
                                             "debug/stopped", "debug/running", "debug/paused" };

static void put_method_names(jw_t *w); /* after the methods table */

static void m_ping(void)
{
    begin_result();
    jw_obj(&W);
    jw_kstr(&W, "runtime_version", RUNTIME_VERSION);
    jw_kstr(&W, "version", TRXMON_VERSION);
    jw_kstr(&W, "backend", "c64u");
    jw_knull(&W, "project");
    jw_key(&W, "build");
    jw_obj(&W);
    jw_kstr(&W, "trxmon_git", TRXMON_BUILD);
    jw_kstr(&W, "firmware_git", plat_firmware_git());
    jw_kstr(&W, "firmware_version", plat_firmware_version());
    jw_end_obj(&W);
    jw_key(&W, "capabilities");
    jw_obj(&W);
    jw_key(&W, "methods");
    jw_arr(&W);
    put_method_names(&W);
    jw_end_arr(&W);
    jw_key(&W, "notifications");
    jw_arr(&W);
    for (unsigned i = 0; i < sizeof(notifications) / sizeof(notifications[0]); i++) {
        jw_str(&W, notifications[i]);
    }
    jw_end_arr(&W);
    jw_end_obj(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_session_create(void)
{
    begin_result();
    jw_obj(&W);
    jw_kstr(&W, "sessionId", SESSION_ID);
    jw_kstr(&W, "mode", "hardware");
    jw_kstr(&W, "diskPath", "");
    jw_ku64(&W, "c64Cycles", mach_cycles());
    jw_ku64(&W, "pc", cur_pc());
    jw_knull(&W, "trace");
    jw_kbool(&W, "attached", 1);
    identity(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_session_list(void)
{
    begin_result();
    jw_arr(&W);
    jw_obj(&W);
    jw_kstr(&W, "sessionId", SESSION_ID);
    jw_kstr(&W, "mode", "hardware");
    jw_kstr(&W, "diskPath", "");
    jw_ku64(&W, "c64Cycles", mach_cycles());
    jw_end_obj(&W);
    jw_end_arr(&W);
    finish(Qcid);
}

static void m_session_close(void)
{
    begin_result();
    jw_obj(&W);
    jw_kbool(&W, "existed", 1);
    jw_key(&W, "released");
    jw_arr(&W);
    jw_end_arr(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void media_entry(int role)
{
    char path[256];
    int n = plat_media(role, path, sizeof(path));
    if (n <= 0) {
        jw_null(&W);
        return;
    }
    jw_obj(&W);
    jw_kstr(&W, "path", path);
    jw_end_obj(&W);
}

static void m_session_state(void)
{
    hw_cpu_t c;
    mach_cpu(&c);
    uint64_t cyc = mach_cycles();
    uint8_t d011 = mach_vic_reg(0x11), d016 = mach_vic_reg(0x16), d018 = mach_vic_reg(0x18);
    uint16_t bank = mach_vic_bank();
    uint32_t screen = ((d018 >> 4) & 15u) << 10, chargen = ((d018 >> 1) & 7u) << 11;
    uint32_t bitmap = (d018 & 8) ? 0x2000 : 0;
    begin_result();
    jw_obj(&W);
    jw_ku64(&W, "c64Cycles", cyc);
    jw_kstr(&W, "runState", M.running ? "running" : "paused");
    jw_kbool(&W, "powered", 1);
    jw_kstr(&W, "mode", "hardware");
    jw_kstr(&W, "backend", "c64u");
    jw_kbool(&W, "warp", 0);
    pacing(&W);
    jw_key(&W, "cpu");
    jw_obj(&W);
    jw_ku64(&W, "pc", mach_pc(&c));
    jw_ku64(&W, "a", c.a);
    jw_ku64(&W, "x", c.x);
    jw_ku64(&W, "y", c.y);
    jw_ku64(&W, "sp", c.s);
    jw_ku64(&W, "flags", c.p);
    jw_ku64(&W, "cycles", cyc);
    jw_kbool(&W, "atInstructionBoundary", c.at_ib);
    jw_end_obj(&W);
    jw_key(&W, "vic");
    jw_obj(&W);
    int paused_pos = !M.running && M.have_stop;
    jw_ku64(&W, "rasterLine", paused_pos ? (uint64_t)(M.stop.rec.pos >> 7) : mach_raster_line());
    jw_ku64(&W, "rasterCycle", paused_pos ? (uint64_t)(M.stop.rec.pos & 0x7F) : 0);
    jw_ku64(&W, "mode", (uint64_t)(((d011 >> 5) & 3) | (((d016 >> 4) & 1) << 2)));
    jw_ku64(&W, "bank", (uint64_t)(bank >> 14));
    jw_ku64(&W, "screenPtr", screen);
    jw_ku64(&W, "chargenPtr", chargen);
    jw_ku64(&W, "bitmapPtr", bitmap);
    jw_ku64(&W, "screenBase", (uint64_t)bank + screen);
    jw_ku64(&W, "chargenBase", (uint64_t)bank + chargen);
    jw_ku64(&W, "bitmapBase", (uint64_t)bank + bitmap);
    jw_ku64(&W, "colorBase", 0xD800);
    jw_ku64(&W, "border", mach_vic_reg(0x20) & 15);
    jw_ku64(&W, "background", mach_vic_reg(0x21) & 15);
    jw_end_obj(&W);
    jw_key(&W, "vectors");
    jw_obj(&W);
    uint16_t v[4];
    static const uint16_t va[4] = { 0xFFFE, 0xFFFA, 0x0314, 0x0318 };
    for (int i = 0; i < 4; i++) {
        v[i] = (uint16_t)(mach_peek8("cpu", va[i]) | (mach_peek8("cpu", (uint16_t)(va[i] + 1)) << 8));
    }
    jw_ku64(&W, "irq", v[0]);
    jw_ku64(&W, "nmi", v[1]);
    jw_ku64(&W, "cinv", v[2]);
    jw_ku64(&W, "cbinv", v[3]);
    jw_end_obj(&W);
    if (M.have_stop && !M.running) {
        jw_kstr(&W, "stopReason", M.stop.reason);
    }
    jw_kstr(&W, "controlOwner", M.owner);
    jw_kbool(&W, "streamPump", 0);
    identity(&W);
    jw_key(&W, "media");
    jw_obj(&W);
    jw_key(&W, "cart");
    media_entry(0);
    jw_key(&W, "disk");
    media_entry(1);
    jw_end_obj(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_debug_state(void)
{
    begin_result();
    debug_state(&W);
    finish(Qcid);
}

static void note_running(void)
{
    note_begin("debug/running");
    pacing(&NW);
    note_send();
}

static void m_debug_run(int is_continue)
{
    take_source();
    rpc_poll(); /* a stop that already came is told first */
    uint64_t cycles;
    if (!is_continue && param_u64("cycles", &cycles) && cycles) {
        if (!mach_own()) {
            send_error(-32001, "the debug block is not available", 0);
            return;
        }
        hw_cyc_cmp((uint32_t)(hw_cycles() + cycles - 1));
        s_cap = 1;
    }
    int was = M.running;
    mach_run();
    if (!was) {
        note_running();
    }
    begin_result();
    debug_state(&W);
    finish(Qcid);
}

static void m_debug_pause(void)
{
    take_source();
    rpc_poll();
    int was = mach_pause();
    if (P.kind == P_RUN) {
        finish_run();
    } else if (P.kind == P_MON) {
        finish_mon(0);
    }
    if (!M.have_stop) {
        /* paused before anything ran: describe the machine as it stands */
        M.stop.reason = "pause";
        M.stop.pc = cur_pc();
        M.stop.cycles = mach_cycles();
        M.have_stop = 1;
    }
    M.stop.reason = "pause";
    if (was) {
        note_begin("debug/paused");
        jw_key(&NW, "stop");
        stop_obj(&NW);
        note_send();
    }
    begin_result();
    debug_state(&W);
    finish(Qcid);
}

static void m_debug_step(void)
{
    take_source();
    rpc_poll();
    if (P.kind != P_NONE) {
        send_error(-32001, "a run is waiting for its stop; debug/pause first", 0);
        return;
    }
    if (mach_step(1, 0) < 0) {
        send_error(-32603, "step: the machine did not stop", 0);
        return;
    }
    M.stop.reason = "step";
    note_begin("debug/stopped");
    jw_key(&NW, "stop");
    stop_obj(&NW);
    registers_str(&NW, "registers");
    note_send();
    begin_result();
    debug_state(&W);
    finish(Qcid);
}

static void m_session_run(void)
{
    take_source();
    rpc_poll();
    if (M.running) {
        send_error(-32001, "session is running under the autonomous loop; use debug/pause before manual session/run", 0);
        return;
    }
    uint64_t cycles = 19705;
    param_u64("cycles", &cycles);
    if (!cycles) {
        cycles = 1;
    }
    if (!mach_own()) {
        send_error(-32001, "the debug block is not available", 0);
        return;
    }
    if (!hold_pending(P_RUN, 2000u + (uint32_t)(cycles / 500u))) {
        return;
    }
    hw_cyc_cmp((uint32_t)(hw_cycles() + cycles - 1));
    mach_run();
}

static void m_break_add(void)
{
    uint64_t pc = 0;
    param_u64("pc", &pc);
    int num = mach_bp_add((uint16_t)pc);
    if (num < 0) {
        send_error(-32602, "debug/break_add: too many breakpoints", 0);
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_ku64(&W, "num", (uint64_t)num);
    jw_key(&W, "breakpoints");
    breakpoints(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_break_del(void)
{
    uint64_t id;
    if (param_u64("id", &id)) {
        mach_bp_del_num((uint32_t)id);
    } else {
        mach_bp_clear();
    }
    begin_result();
    jw_obj(&W);
    jw_kbool(&W, "deleted", 1);
    jw_key(&W, "breakpoints");
    breakpoints(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_break_list(void)
{
    begin_result();
    jw_obj(&W);
    jw_key(&W, "breakpoints");
    breakpoints(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static int read_range(uint32_t addr, uint32_t len, const char *lens, uint8_t *dst)
{
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = mach_peek8(lens, (uint16_t)(addr + i)); /* wraps at $FFFF like the 6510 */
    }
    return (int)len;
}

static void m_read_memory(void)
{
    int ranges = param("ranges");
    char lens[8];
    if (!json_is(s_tok, ranges, JT_ARR)) {
        /* C64RE's readMemoryRange: {addr, length} -> {addr, length, bytes:[...]} */
        uint64_t addr, len;
        if (!param_u64("addr", &addr) || (addr > 0xFFFF)) {
            send_error(-32602, "read_memory: `ranges` must be an array of {addr, len, lens?}", 0);
            return;
        }
        if (!param_u64("length", &len) && !param_u64("len", &len)) {
            len = 1;
        }
        if ((len < 1) || (len > MAX_READ)) {
            send_error(-32602, "read_memory: 32768 bytes is the cap — a per-frame predicate reads a region, not the machine", 0);
            return;
        }
        if (json_str(Q, s_tok, param("lens"), lens, sizeof(lens)) < 0) {
            memcpy(lens, "cpu", 4);
        }
        if (!mach_valid_lens(lens)) {
            send_error(-32602, "read_memory: lens must be cpu|ram|rom|io|cart, got ", lens);
            return;
        }
        read_range((uint32_t)addr, (uint32_t)len, lens, s_mem);
        begin_result();
        jw_obj(&W);
        jw_ku64(&W, "addr", addr);
        jw_ku64(&W, "length", len);
        jw_kstr(&W, "lens", lens);
        jw_key(&W, "bytes");
        jw_arr(&W);
        for (uint32_t i = 0; i < len; i++) {
            jw_u64(&W, s_mem[i]);
        }
        jw_end_arr(&W);
        jw_ku64(&W, "c64Cycles", mach_cycles());
        jw_end_obj(&W);
        finish(Qcid);
        return;
    }
    /* check every range first, so an error answers before anything is written */
    uint32_t total = 0;
    for (int k = 0; k < s_tok[ranges].size; k++) {
        int r = json_item(s_tok, ranges, k);
        uint64_t a, l;
        if (!json_u64(Q, s_tok, json_get(Q, s_tok, r, "addr"), &a) || (a > 0xFFFF)) {
            send_error(-32602, "read_memory: each range needs `addr` in $0000..$FFFF", 0);
            return;
        }
        if (!json_u64(Q, s_tok, json_get(Q, s_tok, r, "len"), &l) || (l < 1) || (l > 0x10000)) {
            send_error(-32602, "read_memory: each range needs `len` in 1..=65536", 0);
            return;
        }
        total += (uint32_t)l;
        if (total > MAX_READ) {
            send_error(-32602, "read_memory: more than 32768 bytes asked for, 32768 is the cap — a per-frame predicate reads a region, not the machine", 0);
            return;
        }
        int sp = json_get(Q, s_tok, r, "space");
        if (json_is(s_tok, sp, JT_STR) && !json_streq(Q, s_tok, sp, "c64") && !json_streq(Q, s_tok, sp, "") &&
            !json_streq(Q, s_tok, sp, "drive8")) {
            send_error(-32602, "session/read_memory: no powered drive at that unit (drive8 is drive A)", 0);
            return;
        }
        int ln = json_get(Q, s_tok, r, "lens");
        if (ln >= 0) {
            if ((json_str(Q, s_tok, ln, lens, sizeof(lens)) < 0) || !mach_valid_lens(lens)) {
                send_error(-32602, "read_memory: lens must be cpu|ram|rom|io|cart", 0);
                return;
            }
        }
    }
    begin_result();
    jw_obj(&W);
    jw_key(&W, "chunks");
    jw_arr(&W);
    for (int k = 0; k < s_tok[ranges].size; k++) {
        int r = json_item(s_tok, ranges, k);
        uint64_t a = 0, l = 0;
        json_u64(Q, s_tok, json_get(Q, s_tok, r, "addr"), &a);
        json_u64(Q, s_tok, json_get(Q, s_tok, r, "len"), &l);
        if (json_str(Q, s_tok, json_get(Q, s_tok, r, "lens"), lens, sizeof(lens)) < 0) {
            memcpy(lens, "cpu", 4);
        }
        int drive = json_streq(Q, s_tok, json_get(Q, s_tok, r, "space"), "drive8");
        read_range((uint32_t)a, (uint32_t)l, drive ? "drive8" : lens, s_mem);
        int bl = ws_base64(s_mem, (int)l, s_b64);
        jw_obj(&W);
        jw_ku64(&W, "addr", a);
        jw_ku64(&W, "len", l);
        jw_kstr(&W, "lens", lens);
        if (drive) {
            jw_kstr(&W, "space", "drive8");
        }
        jw_key(&W, "bytes");
        jw_strn(&W, s_b64, bl);
        jw_end_obj(&W);
    }
    jw_end_arr(&W);
    jw_ku64(&W, "c64Cycles", mach_cycles());
    jw_ku64(&W, "rasterLine", mach_raster_line());
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_monitor_exec(void)
{
    char cmd[512];
    if (json_str(Q, s_tok, param("command"), cmd, sizeof(cmd)) < 0) {
        cmd[0] = 0;
    }
    take_source();
    rpc_poll();
    sbuf_t t;
    sb_init(&t, s_text, sizeof(s_text));
    mon_pending_t mp;
    int rc = mon_exec(cmd, &t, &mp);
    if (rc == MON_PENDING) {
        if (!hold_pending(P_MON, RUN_CAP_MS)) {
            mach_pause();
            return;
        }
        P.mp = mp;
        return;
    }
    mon_reply(rc, &t);
    finish(Qcid);
}

static void m_monitor_state(void)
{
    begin_result();
    jw_obj(&W);
    jw_kstr(&W, "device", "c64");
    jw_ku64(&W, "cpuPortDirection", mach_ddr());
    jw_ku64(&W, "cpuPortValue", mach_latch());
    jw_ku64(&W, "exrom", (uint64_t)mach_exrom());
    jw_ku64(&W, "game", (uint64_t)mach_game());
    jw_key(&W, "cartBank");
    if (mach_cart_present()) {
        jw_u64(&W, (uint64_t)mach_cart_bank());
    } else {
        jw_null(&W);
    }
    jw_knull(&W, "asmCursor");
    identity(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_keep_alive(void)
{
    begin_result();
    jw_obj(&W);
    jw_kbool(&W, "armed", 0);
    jw_knull(&W, "armedSeconds");
    jw_knull(&W, "deadlineMs");
    jw_knull(&W, "keptAliveUntilMs");
    jw_kbool(&W, "keptForever", 0);
    jw_knull(&W, "holding");
    jw_end_obj(&W);
    finish(Qcid);
}

/* ---- the frame ring (T21 §4.10, T20): the checkpoint, mark, transport,
 * ringbuffer and snapshot methods, runtime/reverse_step ------------------- */

static int ring_or_error(void)
{
    if (!ring_present()) {
        send_error(-32001, "no frame ring (the bitstream has no T20 engine, or the firmware no ring table)", 0);
        return 0;
    }
    if (ring_off_reason()) { /* A08 */
        send_error(-32001, ring_off_reason(), 0);
        return 0;
    }
    return 1;
}

static void cp_ref(jw_t *w, const ring_entry_t *e)
{
    jw_obj(w);
    jw_kstr(w, "id", e->id);
    jw_key(w, "label");
    const char *mk = ring_mark_of(e->seq);
    if (mk) {
        jw_str(w, mk);
    } else {
        jw_null(w);
    }
    jw_ku64(w, "frame", e->frame);
    jw_ku64(w, "cycles", e->cycles);
    jw_kbool(w, "pinned", 0);
    jw_ku64(w, "byteSize", 0);
    jw_ku64(w, "createdAtMs", 0);
    jw_end_obj(w);
}

static void cp_stats(jw_t *w)
{
    int n = ring_count();
    ring_entry_t a, b;
    int ok = (n > 0) && (ring_entry(0, &a) == 0) && (ring_entry(n - 1, &b) == 0);
    jw_obj(w);
    jw_ku64(w, "count", (uint64_t)n);
    jw_ku64(w, "pinnedCount", 0);
    jw_ku64(w, "totalBytes", 0);
    jw_ku64(w, "budgetBytes", 0);
    jw_ku64(w, "oldestFrame", ok ? a.frame : 0);
    jw_ku64(w, "newestFrame", ok ? b.frame : 0);
    jw_ku64(w, "diskImageVersions", 0);
    jw_ku64(w, "diskPoolBytes", 0);
    jw_ku64(w, "slotBytes", 0);
    jw_ku64(w, "slotCount", 0);
    jw_ku64(w, "freeSlots", 0);
    jw_end_obj(w);
}

/* checkpoint/list: the entries a land can go to, oldest first; at most 400
 * (a stride over the ring when it holds more; `stride` asks for one) */
static void m_cp_list(void)
{
    if (!ring_or_error()) {
        return;
    }
    int n = ring_count();
    uint64_t stride = 0;
    param_u64("stride", &stride);
    if (!stride) {
        stride = (uint64_t)(n + 399) / 400;
        if (!stride) {
            stride = 1;
        }
    }
    begin_result();
    jw_obj(&W);
    jw_key(&W, "checkpoints");
    jw_arr(&W);
    for (int i = 0; i < n; i++) {
        ring_entry_t e;
        if ((ring_entry(i, &e) < 0) || !e.target) {
            continue;
        }
        if (((uint64_t)(n - 1 - i) % stride) && !ring_mark_of(e.seq)) {
            continue; /* the head and every stride-th before it, and the marked ones */
        }
        cp_ref(&W, &e);
    }
    jw_end_arr(&W);
    jw_key(&W, "stats");
    cp_stats(&W);
    jw_ku64(&W, "stride", stride);
    jw_end_obj(&W);
    finish(Qcid);
}

static int cp_param(const char *method)
{
    char id[48];
    if (json_str(Q, s_tok, param("id"), id, sizeof(id)) <= 0) {
        send_error(-32602, method, ": id required");
        return -1;
    }
    int i = ring_find_id(id);
    if (i < 0) {
        send_error(-32001, method, ": unknown id (the frame has left the ring)");
    }
    return i;
}

static void m_cp_capture(void)
{
    if (!ring_or_error()) {
        return;
    }
    /* the board captures every frame start by itself: the anchor is the one
     * the machine is in */
    int i = ring_cursor();
    ring_entry_t e;
    if ((i < 0) || (ring_entry(i, &e) < 0)) {
        send_error(-32001, "checkpoint/capture: no anchors yet (the ring fills while the machine runs)", 0);
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_key(&W, "ref");
    cp_ref(&W, &e);
    jw_key(&W, "stats");
    cp_stats(&W);
    jw_kstr(&W, "note", "the C64 Ultimate records every frame start; this is the one the machine is in, not the "
                        "cycle it stands on");
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_cp_restore(void)
{
    if (!ring_or_error()) {
        return;
    }
    int i = cp_param("checkpoint/restore");
    if (i < 0) {
        return;
    }
    rpc_poll();
    char then[8] = "keep";
    json_str(Q, s_tok, param("then"), then, sizeof(then));
    int was = M.running;
    ring_entry_t e;
    ring_entry(i, &e);
    const char *er = ring_land(i);
    if (er) {
        send_error(-32001, "checkpoint/restore: ", er);
        return;
    }
    if (m_streq(then, "run") || (m_streq(then, "keep") && was)) {
        mach_run();
    }
    ring_entry(i, &e);
    begin_result();
    jw_obj(&W);
    jw_key(&W, "restored");
    cp_ref(&W, &e);
    jw_key(&W, "state");
    debug_state(&W);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_cp_pin(void)
{
    if (!ring_or_error()) {
        return;
    }
    if (cp_param("checkpoint/pin") < 0) {
        return;
    }
    send_error(-32001, "checkpoint/pin: the C64 Ultimate's ring cannot hold a frame against eviction yet (T20 "
                       "keyframe slots); mark/set names it", 0);
}

static void mark_obj(jw_t *w, const char *name, const ring_entry_t *e)
{
    jw_obj(w);
    jw_kstr(w, "name", name);
    jw_kstr(w, "anchorId", e->id);
    jw_ku64(w, "cycle", e->cycles);
    jw_ku64(w, "frame", (uint64_t)e->index + 1);
    uint64_t live = mach_cycles();
    char t[24];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    uint64_t d = live > e->cycles ? live - e->cycles : 0;
    hw_video_t vid;
    hw_video(&vid);
    sb_dec(&b, d / vid.cpu_hz);
    sb_putc(&b, '.');
    uint64_t ms = (d % vid.cpu_hz) * 1000u / vid.cpu_hz;
    sb_putc(&b, (char)('0' + ms / 100));
    sb_putc(&b, (char)('0' + (ms / 10) % 10));
    sb_putc(&b, (char)('0' + ms % 10));
    jw_key(w, "secondsBack");
    jw_raw(w, b.p, b.len);
    jw_end_obj(w);
}

static void m_mark_set(void)
{
    if (!ring_or_error()) {
        return;
    }
    char name[24];
    if (json_str(Q, s_tok, param("name"), name, sizeof(name)) <= 0) {
        send_error(-32602, "mark/set: name required", 0);
        return;
    }
    ring_entry_t e;
    const char *er = ring_mark_set(name, &e);
    if (er) {
        send_error(-32001, "mark/set: ", er);
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_key(&W, "mark");
    mark_obj(&W, name, &e);
    jw_ku64(&W, "used", (uint64_t)ring_marks());
    jw_ku64(&W, "cap", RING_MAX_MARKS);
    char t[64];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    sb_put(&b, "MARK ");
    sb_put(&b, name);
    sb_put(&b, " @ Cycle ");
    sb_dec(&b, e.cycles);
    jw_key(&W, "message");
    jw_strn(&W, b.p, b.len);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_mark_list(void)
{
    if (!ring_or_error()) {
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_key(&W, "marks");
    jw_arr(&W);
    for (int i = 0; i < ring_marks(); i++) {
        ring_entry_t e;
        if (ring_mark_entry(i, &e) == 0) {
            mark_obj(&W, ring_mark_name(i), &e);
        }
    }
    jw_end_arr(&W);
    jw_ku64(&W, "cap", RING_MAX_MARKS);
    jw_ku64(&W, "used", (uint64_t)ring_marks());
    jw_kbool(&W, "pinned", 0);
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_mark_drop(void)
{
    if (!ring_or_error()) {
        return;
    }
    char name[24];
    if (json_str(Q, s_tok, param("name"), name, sizeof(name)) <= 0) {
        send_error(-32602, "mark/drop: name required", 0);
        return;
    }
    uint64_t was;
    if (ring_mark_drop(name, &was)) {
        send_error(-32001, "mark/drop: no mark named ", name);
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_kstr(&W, "dropped", name);
    jw_ku64(&W, "cycles", was);
    char t[48];
    sbuf_t b;
    sb_init(&b, t, sizeof(t));
    sb_put(&b, "UNMARK ");
    sb_put(&b, name);
    jw_key(&W, "message");
    jw_strn(&W, b.p, b.len);
    jw_end_obj(&W);
    finish(Qcid);
}

static void transport_reply(int hit_end)
{
    begin_result();
    ring_status_json(&W, hit_end);
    finish(Qcid);
}

static void m_goto_target(const char *method, const char *t)
{
    rpc_poll();
    const char *er = ring_goto(t);
    if (er) {
        send_error(-32001, method, er);
        return;
    }
    transport_reply(0);
}

static void m_mark_goto(void)
{
    if (!ring_or_error()) {
        return;
    }
    char name[24];
    if (json_str(Q, s_tok, param("name"), name, sizeof(name)) <= 0) {
        send_error(-32602, "mark/goto: name required", 0);
        return;
    }
    m_goto_target("mark/goto: ", name);
}

static void m_tr_status(void)
{
    if (!ring_or_error()) {
        return;
    }
    transport_reply(0);
}

static void m_tr_frame(void)
{
    if (!ring_or_error()) {
        return;
    }
    int d = param("delta");
    if (d < 0) {
        send_error(-32602, "transport/frame: delta required", 0);
        return;
    }
    const char *t = Q + s_tok[d].start;
    int neg = t[0] == '-';
    uint32_t v;
    if (!json_is(s_tok, d, JT_NUM) || !m_parse_dec(t + neg, s_tok[d].end - s_tok[d].start - neg, &v)) {
        send_error(-32602, "transport/frame: delta must be an integer", 0);
        return;
    }
    rpc_poll();
    int end;
    const char *er = ring_frame_move(neg ? -(int)v : (int)v, &end);
    if (er) {
        send_error(-32001, er, 0);
        return;
    }
    transport_reply(end);
}

static void m_tr_goto(void)
{
    if (!ring_or_error()) {
        return;
    }
    char t[32];
    uint64_t n;
    if (json_str(Q, s_tok, param("target"), t, sizeof(t)) <= 0) {
        sbuf_t b;
        sb_init(&b, t, sizeof(t));
        if (param_u64("frame", &n)) {
            sb_dec(&b, n);
        } else if (param_u64("cycle", &n)) {
            sb_putc(&b, 'c');
            sb_dec(&b, n);
        } else {
            send_error(-32602, "transport/goto: one of target | frame | cycle required", 0);
            return;
        }
        sb_str(&b);
    }
    m_goto_target("", t);
}

static void m_tr_pause(void)
{
    if (!ring_or_error()) {
        return;
    }
    rpc_poll();
    mach_pause();
    transport_reply(0);
}

static void m_tr_play(void)
{
    char dir[8] = "fwd";
    json_str(Q, s_tok, param("direction"), dir, sizeof(dir));
    if (m_streq(dir, "back")) {
        send_error(-32001, "transport/play back: not available on this host (no paced replay; transport/frame steps "
                           "back)", 0);
        return;
    }
    rpc_poll();
    mach_run();
    transport_reply(0);
}

static void m_tr_toggle(void)
{
    rpc_poll();
    if (M.running) {
        mach_pause();
    } else {
        mach_run();
    }
    transport_reply(0);
}

static void m_reverse_step(void)
{
    if (!ring_or_error()) {
        return;
    }
    uint64_t n = 1;
    param_u64("n", &n);
    rpc_poll();
    ring_rstep_t r;
    const char *er = ring_rstep((int)(n ? n : 1), &r);
    if (er) {
        send_error(-32001, er, 0);
        return;
    }
    begin_result();
    jw_obj(&W);
    jw_ku64(&W, "stepsTaken", (uint64_t)r.steps);
    jw_ku64(&W, "pc", r.pc);
    jw_ku64(&W, "a", r.a);
    jw_ku64(&W, "x", r.x);
    jw_ku64(&W, "y", r.y);
    jw_ku64(&W, "sp", r.s);
    jw_ku64(&W, "p", r.p);
    jw_ku64(&W, "cycle", r.cycle);
    jw_key(&W, "undoneWrites");
    jw_raw(&W, "[]", 2);
    jw_kbool(&W, "inspectOnly", 0);
    jw_kstr(&W, "note", "replayed from the frame start: CPU, RAM and CIAs exact, the VIC's registers replayed (its "
                        "internals, SID, REU and drive not put back); a run from here cuts the frames after it; the "
                        "undone writes are not listed on this host");
    jw_end_obj(&W);
    finish(Qcid);
}

static void m_quit(void)
{
    s_quit = 1;
    begin_result();
    jw_obj(&W);
    jw_kbool(&W, "quitting", 1);
    jw_kstr(&W, "runState", M.running ? "running" : "paused");
    jw_end_obj(&W);
    finish(Qcid);
}

/* ---- the instruction trace (T21 §4.11.2): TRX64's who_wrote, crash_triage, reverse depth and the ring dump ---- */

static char s_tr[12288];

static void trace_reply(const char *method, const char *er, sbuf_t *b)
{
    if (er) {
        send_error(-32001, method, er);
        return;
    }
    begin_result();
    jw_raw(&W, b->p, b->len);
    finish(Qcid);
}

static void m_who_wrote(void)
{
    uint64_t addr, limit = 8;
    if (!param_u64("addr", &addr)) {
        send_error(-32602, "runtime/who_wrote: missing addr (0-65535)", 0);
        return;
    }
    param_u64("limit", &limit);
    sbuf_t b;
    sb_init(&b, s_tr, sizeof(s_tr));
    jw_t w;
    jw_init(&w, &b);
    const char *er = trace_who_wrote_json(&w, (uint16_t)(addr & 0xFFFF), (int)limit);
    trace_reply("runtime/who_wrote: ", er, &b);
}

static void m_crash_triage(void)
{
    uint64_t pc = 0;
    int have = param_u64("pc", &pc);
    sbuf_t b;
    sb_init(&b, s_tr, sizeof(s_tr));
    jw_t w;
    jw_init(&w, &b);
    const char *er = trace_triage_json(&w, have, (uint16_t)(pc & 0xFFFF));
    trace_reply("runtime/crash_triage: ", er, &b);
}

static void m_set_reverse_depth(void)
{
    sbuf_t b;
    sb_init(&b, s_tr, sizeof(s_tr));
    jw_t w;
    jw_init(&w, &b);
    const char *er = trace_depth_json(&w);
    trace_reply("runtime/set_reverse_depth: ", er, &b);
}

static void m_debug_run_(void) { m_debug_run(0); }
static void m_debug_continue_(void) { m_debug_run(1); }

/* The one list of what is served: dispatch and ping's capabilities both read it */
static const struct
{
    const char *name;
    void (*fn)(void);
} methods[] = {
    { "ping", m_ping },
    { "debug/run", m_debug_run_ },
    { "debug/continue", m_debug_continue_ },
    { "daemon/keep_alive", m_keep_alive },
    { "session/create", m_session_create },
    { "session/list", m_session_list },
    { "session/close", m_session_close },
    { "session/state", m_session_state },
    { "session/run", m_session_run },
    { "session/read_memory", m_read_memory },
    { "debug/state", m_debug_state },
    { "debug/pause", m_debug_pause },
    { "debug/step", m_debug_step },
    { "debug/break_add", m_break_add },
    { "debug/break_del", m_break_del },
    { "debug/break_list", m_break_list },
    { "monitor/exec", m_monitor_exec },
    { "monitor/state", m_monitor_state },
    { "trxmon/quit", m_quit },
    { "checkpoint/list", m_cp_list },
    { "checkpoint/capture", m_cp_capture },
    { "checkpoint/restore", m_cp_restore },
    { "checkpoint/pin", m_cp_pin },
    { "checkpoint/unpin", m_cp_pin },
    { "mark/set", m_mark_set },
    { "mark/list", m_mark_list },
    { "mark/drop", m_mark_drop },
    { "mark/goto", m_mark_goto },
    { "transport/status", m_tr_status },
    { "transport/frame", m_tr_frame },
    { "transport/goto", m_tr_goto },
    { "transport/pause", m_tr_pause },
    { "transport/play", m_tr_play },
    { "transport/toggle", m_tr_toggle },
    { "runtime/reverse_step", m_reverse_step },
    { "runtime/who_wrote", m_who_wrote },
    { "runtime/crash_triage", m_crash_triage },
    { "runtime/set_reverse_depth", m_set_reverse_depth },
};

static void put_method_names(jw_t *w)
{
    for (unsigned i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        jw_str(w, methods[i].name);
    }
}

static int prefix(const char *m, const char *p) { return m_strnieq(m, p, m_strlen(p)) && (m_strlen(m) >= m_strlen(p)); }

/* -32601 with the way out (T21 §7.4) */
static void not_offered(const char *m)
{
    static const char *const sandbox[] = { "sandbox/", "runtime/candidate_", "runtime/scenario_", "runtime/overlay_run",
                                           "runtime/promote_branch", "runtime/snapshot_tree", "vic/", "recorder/", "vsf/",
                                           "session/model", "audio/", "batch/", "session/set_pacing",
                                           "session/turbo", "runtime/find_cheat", "runtime/component_diff", 0 };
    static const char *const rest[] = { "media/", "session/load_prg", "runtime/run_prg", "session/type",
                                        "session/key_down", "session/key_up", "session/release_keys", "session/joystick_",
                                        "session/pot_", "session/screenshot", "session/frame_indices", "session/reset",
                                        "session/power", "session/drive", "device/",
                                        "runtime/swap_disk_and_continue", "runtime/render_screen", 0 };
    static const char *const t20[] = { "checkpoint/", "transport/", "runtime/diff_checkpoints", "runtime/mark", 0 };
    for (int i = 0; sandbox[i]; i++) {
        if (prefix(m, sandbox[i])) {
            send_error(-32601, m, ": not on C64 Ultimate hardware; use a local TRX64 sandbox");
            return;
        }
    }
    for (int i = 0; rest[i]; i++) {
        if (prefix(m, rest[i])) {
            send_error(-32601, m, ": media, machine and input are the Ultimate's REST API (/v1/...), not trxmon");
            return;
        }
    }
    for (int i = 0; t20[i]; i++) {
        if (prefix(m, t20[i])) {
            send_error(-32601, m, ": not on the C64 Ultimate yet (a T20 part that is not built)");
            return;
        }
    }
    if (prefix(m, "ringbuffer/") || prefix(m, "snapshot/")) {
        send_error(-32601, m, ": the M1 firmware writes and loads no .c64re or .c64rering files (TODO T66)");
        return;
    }
    if (prefix(m, "trace/build_from_ring")) {
        send_error(-32601, m, ": the M1 firmware writes no .c64retrace files (TODO T66)");
        return;
    }
    if (prefix(m, "trace/") || prefix(m, "debug/memory_access_map")) {
        send_error(-32601, m, ": the trace store and its analyses are host-side: an export of a window to C64RE/TRX64 comes with TODO T66");
        return;
    }
    send_error(-32601, "method not found: ", m);
}

void rpc_init(void)
{
    s_quit = 0;
    s_cap = 0;
    s_unclaimed = 0; /* a start has no stop pending (the machine state is read again) */
    s_user_stop = 0;
    memset(&P, 0, sizeof(P));
    P.cid = -1;
}

void rpc_request(int cid, const char *text, int len)
{
    Qcid = cid;
    Qidlen = 0;
    Q = text;
    int n = json_parse(text, len, s_tok, MAX_TOK);
    if ((n < 0) || !json_is(s_tok, 0, JT_OBJ)) {
        Qidlen = 4;
        memcpy(Qid, "null", 4);
        send_error(n == -2 ? -32600 : -32700, n == -2 ? "request too large" : "parse error", 0);
        return;
    }
    int id = json_get(text, s_tok, 0, "id");
    if (id >= 0) {
        int il = s_tok[id].end - s_tok[id].start;
        int st = s_tok[id].start;
        if (s_tok[id].type == JT_STR) { /* keep the quotes */
            st--;
            il += 2;
        }
        if (il > (int)sizeof(Qid)) {
            il = 4;
            memcpy(Qid, "null", 4);
        } else {
            memcpy(Qid, text + st, (size_t)il);
        }
        Qidlen = il;
    }
    Qparams = json_get(text, s_tok, 0, "params");
    char m[64];
    if (json_str(text, s_tok, json_get(text, s_tok, 0, "method"), m, sizeof(m)) < 0) {
        send_error(-32600, "invalid request: `method` must be a string", 0);
        return;
    }
    mach_snapshot();
    for (unsigned i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        if (m_streq(m, methods[i].name)) {
            methods[i].fn();
            return;
        }
    }
    not_offered(m);
}
