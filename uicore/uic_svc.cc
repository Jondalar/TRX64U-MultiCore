// uicore service: revision, event ring, the intent dispatcher, questions, jobs, history, the token. Everything here runs on the UI task except where a
// function says it is the network side (uic_net.cc).
#include "uic_priv.h"
#include <stdio.h>
#include <string.h>

void uic_lock(UicService *s) { if (s->port.lock) s->port.lock(s->port.user); }
void uic_unlock(UicService *s) { if (s->port.unlock) s->port.unlock(s->port.user); }

static uint32_t now(UicService *s) { return s->port.now_ms ? s->port.now_ms(s->port.user) : 0; }
static bool on_ui(UicService *s) { return !s->port.on_ui_task || s->port.on_ui_task(s->port.user); }

void uic_init(UicService *s, const UicPort *port, UicDomain *dom, char *snap_buf, int snap_cap)
{
    memset(s, 0, sizeof(*s));
    s->port = *port;
    s->dom = dom;
    s->snap = snap_buf;
    s->snap_cap = snap_cap;
    s->rev = 1;
    s->hist_cur = 0;
}

const char *uic_r_name(int r)
{
    static const char *const n[] = { "taken", "refused", "asked", "held", "blocked", "gone", "busy", "bad" };
    return r >= 0 && r <= UR_BAD ? n[r] : "bad";
}

uint32_t uic_next_rev(UicService *s) { return ++s->rev; }

void uic_note(UicService *s)
{
    if (!on_ui(s)) { s->violations++; return; }
    s->rev++;
}

// ---- event ring -------------------------------------------------------------------------------------------------------------------------
void uic_ev_store(UicService *s, uint32_t rev, const char *text)
{
    uic_lock(s);
    if (s->ev_count == UIC_EVENTS) {
        s->ev_floor = s->ev[s->ev_head].rev;
        s->ev_head = (s->ev_head + 1) % UIC_EVENTS;
        s->ev_count--;
        s->overflows++;
    }
    UicEvent &e = s->ev[(s->ev_head + s->ev_count) % UIC_EVENTS];
    e.rev = rev;
    snprintf(e.text, sizeof(e.text), "%s", text);
    s->ev_count++;
    uic_unlock(s);
}

// ---- questions ---------------------------------------------------------------------------------------------------------------------------
static void ev_ask(UicService *s)
{
    uint32_t rev = uic_next_rev(s);
    char buf[UIC_EV_MAX];
    UicW w(buf, sizeof(buf));
    w.open('{');
    w.ks("t", "ask");
    w.ku("rev", rev);
    w.ks("q", s->q_id);
    w.ks("text", s->q.text);
    w.kb("destructive", s->q.destructive);
    if (s->q.def[0]) w.ks("default", s->q.def);
    if (s->q.timeout_ms) w.ku("timeout_ms", s->q.timeout_ms);
    w.close('}');
    uic_ev_store(s, rev, w.ok ? buf : "{\"t\":\"resync\"}");
}

static void ev_closed(UicService *s, const char *by, const char *choice)
{
    uint32_t rev = uic_next_rev(s);
    char buf[UIC_EV_MAX];
    UicW w(buf, sizeof(buf));
    w.open('{');
    w.ks("t", "closed");
    w.ku("rev", rev);
    w.ks("q", s->q_id);
    w.ks("by", by);
    w.ks("choice", choice);
    w.close('}');
    uic_ev_store(s, rev, buf);
}

static void q_open_with(UicService *s, const UicQuestion &q)
{
    s->q = q;
    s->q_n++;
    snprintf(s->q_id, sizeof(s->q_id), "q%u", (unsigned)s->q_n);
    s->q_open = true;
    s->q_since = now(s);
    ev_ask(s);
}

bool uic_ask(UicService *s, const UicQuestion &q, char *id_out)
{
    if (!on_ui(s)) { s->violations++; return false; }
    if (s->q_open) return false;
    q_open_with(s, q);
    s->q_polled = false;
    if (id_out) snprintf(id_out, 12, "%s", s->q_id);
    return true;
}

bool uic_close(UicService *s, const char *by, const char *choice)
{
    if (!on_ui(s)) { s->violations++; return false; }
    if (!s->q_open) return false;
    s->q_open = false;
    s->q_settle = true;
    ev_closed(s, by, choice ? choice : "");
    return true;
}

static void poll_question(UicService *s, uint32_t t)
{
    UicQuestion dq;
    memset(&dq, 0, sizeof(dq));
    bool has = s->dom->question(&dq);
    if (s->q_settle) {
        if (!has) {
            s->q_settle = false;
            return;
        }
        if (!strcmp(dq.text, s->q.text)) return;               // still the question that was answered: the screen has not caught up yet
        s->q_settle = false;                                    // another question took its place (the second step of a two-step ask): it is a new one
    }
    if (has && s->q_open && s->q_polled && strcmp(dq.text, s->q.text)) {
        s->q_open = false;                                      // the screen's question changed under an open one: the old id is closed, the new one opens
        ev_closed(s, "screen", "");
    }
    if (has && !s->q_open) {
        q_open_with(s, dq);
        s->q_polled = true;
    } else if (!has && s->q_open) {
        s->q_open = false;
        ev_closed(s, "screen", "");
    } else if (s->q_open && s->q.timeout_ms && (int32_t)(t - s->q_since) >= (int32_t)s->q.timeout_ms) {
        const char *d = s->q.def[0] ? s->q.def : "no";
        s->dom->answer(d);
        s->q_open = false;
        s->q_settle = true;
        ev_closed(s, "timeout", d);
    }
}

static void do_answer(UicService *s, const UicIntent &in, UicReply *r)
{
    if (!s->q_open || (in.q[0] && strcmp(in.q, s->q_id))) {
        r->r = UR_GONE;
        snprintf(r->why, sizeof(r->why), "%s", s->q_open ? "that question is closed" : "no question is open");
        return;
    }
    if (strcmp(in.choice, "yes") && strcmp(in.choice, "no")) {
        r->r = UR_BAD;
        snprintf(r->why, sizeof(r->why), "choice is yes or no");
        return;
    }
    s->dom->answer(in.choice);
    s->q_open = false;
    s->q_settle = true;
    ev_closed(s, in.local ? "screen" : "remote", in.choice);
    snprintf(r->q, sizeof(r->q), "%s", s->q_id);
    r->r = UR_TAKEN;
}

// ---- jobs --------------------------------------------------------------------------------------------------------------------------------
static const char *const PHASE[] = { "queued", "running", "asking", "done", "failed", "cancelled" };

static void ev_job(UicService *s, const UicJob &j)
{
    uint32_t rev = uic_next_rev(s);
    char buf[UIC_EV_MAX];
    UicW w(buf, sizeof(buf));
    w.open('{');
    w.ks("t", "job");
    w.ku("rev", rev);
    w.ks("job", j.id);
    w.ks("kind", j.kind);
    w.ks("phase", PHASE[j.phase]);
    w.ks("step", j.step);
    w.ki("done", j.done);
    w.ki("total", j.total);
    w.kb("cancelable", j.cancelable);
    if (j.phase >= 3) {
        w.kb("ok", j.ok);
        w.ks("text", j.text);
    }
    w.close('}');
    uic_ev_store(s, rev, w.ok ? buf : "{\"t\":\"resync\"}");
}

static UicJob *job_alloc(UicService *s)
{
    for (int i = 0; i < UIC_JOBS; i++) if (!s->job[i].used) return &s->job[i];
    for (int i = 0; i < UIC_JOBS; i++) if (s->job[i].phase >= 3) return &s->job[i];   // the oldest finished one is reused
    return 0;
}

static UicJob *job_find_id(UicService *s, const char *id)
{
    for (int i = 0; i < UIC_JOBS; i++) if (s->job[i].used && !strcmp(s->job[i].id, id)) return &s->job[i];
    return 0;
}

static UicJob *job_new(UicService *s, const char *kind, const char *title, bool cancelable)
{
    UicJob *j = job_alloc(s);
    if (!j) return 0;
    memset(j, 0, sizeof(*j));
    j->used = true;
    snprintf(j->id, sizeof(j->id), "j%u", (unsigned)++s->job_n);
    snprintf(j->kind, sizeof(j->kind), "%s", kind);
    snprintf(j->title, sizeof(j->title), "%s", title);
    j->phase = 1;
    j->cancelable = cancelable;
    return j;
}

bool uic_job_begin(UicService *s, const char *kind, const char *title, bool cancelable, char *id_out)
{
    if (!on_ui(s)) { s->violations++; return false; }
    UicJob *j = job_new(s, kind, title, cancelable);
    if (!j) return false;
    if (id_out) snprintf(id_out, 8, "%s", j->id);
    ev_job(s, *j);
    return true;
}

void uic_job_step(UicService *s, const char *id, int done, int total, const char *step)
{
    UicJob *j = job_find_id(s, id);
    if (!j || j->phase >= 3) return;
    if (j->done == done && j->total == total && !strcmp(j->step, step ? step : "")) return;
    j->done = done;
    j->total = total;
    snprintf(j->step, sizeof(j->step), "%s", step ? step : "");
    ev_job(s, *j);
}

void uic_job_end(UicService *s, const char *id, bool ok, const char *text)
{
    UicJob *j = job_find_id(s, id);
    if (!j || j->phase >= 3) return;
    j->phase = j->cancel_req && !ok ? 5 : ok ? 3 : 4;
    j->ok = ok;
    snprintf(j->text, sizeof(j->text), "%s", text ? text : "");
    ev_job(s, *j);
}

bool uic_job_cancel_local(UicService *s, const char *id)
{
    UicJob *j = job_find_id(s, id);
    if (!j || j->phase >= 3 || !j->cancelable) return false;
    j->cancel_req = true;
    return true;
}

bool uic_job_cancel_requested(UicService *s, const char *id)
{
    UicJob *j = job_find_id(s, id);
    return j && j->cancel_req;
}

static void poll_jobs(UicService *s)
{
    UicJobInfo ji[UIC_JOBS];
    memset(ji, 0, sizeof(ji));
    int n = s->dom->jobs(ji, UIC_JOBS);
    for (int i = 0; i < UIC_JOBS; i++) s->job[i].mirrored_seen = false;
    for (int k = 0; k < n; k++) {
        UicJob *j = 0;
        for (int i = 0; i < UIC_JOBS; i++) if (s->job[i].used && s->job[i].key[0] && s->job[i].phase < 3 && !strcmp(s->job[i].key, ji[k].key)) j = &s->job[i];
        if (!j) {
            j = job_new(s, ji[k].kind, ji[k].title, ji[k].cancelable);
            if (!j) continue;
            snprintf(j->key, sizeof(j->key), "%s", ji[k].key);
            j->done = ji[k].done;
            j->total = ji[k].total;
            snprintf(j->step, sizeof(j->step), "%s", ji[k].step);
            ev_job(s, *j);
        } else if (j->done != ji[k].done || j->total != ji[k].total || strcmp(j->step, ji[k].step)) {
            j->done = ji[k].done;
            j->total = ji[k].total;
            snprintf(j->step, sizeof(j->step), "%s", ji[k].step);
            j->cancelable = ji[k].cancelable;
            ev_job(s, *j);
        }
        j->mirrored_seen = true;
    }
    for (int i = 0; i < UIC_JOBS; i++) {
        UicJob &j = s->job[i];
        if (j.used && j.key[0] && j.phase < 3 && !j.mirrored_seen) {
            bool ok = true;
            char text[80];
            text[0] = 0;
            s->dom->job_result(j.key, &ok, text, sizeof(text));
            j.phase = j.cancel_req ? 5 : ok ? 3 : 4;
            j.ok = ok;
            snprintf(j.text, sizeof(j.text), "%s", text);
            if (j.total) j.done = j.total;
            ev_job(s, j);
        }
    }
}

static void do_cancel(UicService *s, const UicIntent &in, UicReply *r)
{
    UicJob *j = 0;
    if (in.job[0]) {
        j = job_find_id(s, in.job);
    } else {
        for (int i = 0; i < UIC_JOBS; i++) if (s->job[i].used && s->job[i].phase < 3) j = &s->job[i];
    }
    if (!j || j->phase >= 3) {
        r->r = UR_GONE;
        snprintf(r->why, sizeof(r->why), "no such job running");
        return;
    }
    snprintf(r->job, sizeof(r->job), "%s", j->id);
    if (!j->cancelable) {
        r->r = UR_REFUSED;
        snprintf(r->why, sizeof(r->why), "this job cannot be cancelled");
        return;
    }
    if (j->key[0] && !s->dom->cancel_job(j->key)) {
        r->r = UR_REFUSED;
        snprintf(r->why, sizeof(r->why), "the job cannot be cancelled now");
        return;
    }
    j->cancel_req = true;
    r->r = UR_TAKEN;
}

// ---- history (epic D19d) -----------------------------------------------------------------------------------------------------------------
void uic_history_add(UicService *s, const char *text)
{
    if (!text || !text[0]) return;
    if (s->hist_n && !strcmp(s->hist[s->hist_n - 1], text)) {
        s->hist_cur = s->hist_n;
        return;
    }
    if (s->hist_n == UIC_HISTORY) {
        memmove(s->hist[0], s->hist[1], sizeof(s->hist[0]) * (UIC_HISTORY - 1));
        s->hist_n--;
    }
    snprintf(s->hist[s->hist_n++], UIC_HISTORY_LEN + 1, "%s", text);
    s->hist_cur = s->hist_n;
}

void uic_history_clear(UicService *s)
{
    s->hist_n = 0;
    s->hist_cur = 0;
    memset(s->hist, 0, sizeof(s->hist));
}

static void do_history(UicService *s, const UicIntent &in, UicReply *r)
{
    if (!s->hist_n) {
        r->r = UR_GONE;
        snprintf(r->why, sizeof(r->why), "no entries yet");
        return;
    }
    if (!strcmp(in.dir, "prev")) {
        if (s->hist_cur > 0) s->hist_cur--;
    } else if (!strcmp(in.dir, "next")) {
        if (s->hist_cur < s->hist_n) s->hist_cur++;
    } else {
        r->r = UR_BAD;
        snprintf(r->why, sizeof(r->why), "dir is prev or next");
        return;
    }
    snprintf(r->text, sizeof(r->text), "%s", s->hist_cur < s->hist_n ? s->hist[s->hist_cur] : "");
    s->dom->set_text(r->text);
    r->r = UR_TAKEN;
}

// ---- token (T192.1 D-G, Q3) --------------------------------------------------------------------------------------------------------------
static bool hex_token(const char *b, int n)
{
    if (n < UIC_TOKEN_LEN) return false;
    for (int i = 0; i < UIC_TOKEN_LEN; i++) {
        char c = b[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static void token_poll(UicService *s, uint32_t t)
{
    if (s->quiet) return;
    if (!s->port.file_load || (s->tok_polled && (int32_t)(t - s->tok_polled) < 2000)) return;
    s->tok_polled = t ? t : 1;
    char b[80];
    int n = s->port.file_load(s->port.user, b, sizeof(b));
    uic_lock(s);
    if (n >= 0 && hex_token(b, n)) {
        memset(s->tok, 0, sizeof(s->tok));
        memcpy(s->tok, b, UIC_TOKEN_LEN);
        s->tok_have = true;
        s->tok_miss = 0;
    } else if (n < 0 && ++s->tok_miss >= 2) {
        memset(s->tok, 0, sizeof(s->tok));
        s->tok_have = false;
    }
    uic_unlock(s);
}

static void do_grant(UicService *s, const UicIntent &in, UicReply *r)
{
    if (!s->port.file_save) {
        r->r = UR_REFUSED;
        snprintf(r->why, sizeof(r->why), "this build has no token");
        return;
    }
    // look at the file once more: a token file made by someone else since the last look is not ours to replace
    char b[80];
    int n = s->port.file_load ? s->port.file_load(s->port.user, b, sizeof(b)) : -1;
    if (s->tok_have || (n >= 0 && hex_token(b, n))) {
        r->r = UR_REFUSED;
        snprintf(r->why, sizeof(r->why), "a token exists; delete %s to hand out a new one", "the token file");
        if (n >= 0 && hex_token(b, n) && !s->tok_have) {
            uic_lock(s);
            memset(s->tok, 0, sizeof(s->tok));
            memcpy(s->tok, b, UIC_TOKEN_LEN);
            s->tok_have = true;
            uic_unlock(s);
        }
        return;
    }
    uint8_t mat[160];
    int m = 0;
    if (s->port.entropy) m = s->port.entropy(s->port.user, mat, 96);
    if (m < 0) m = 0;
    uint32_t t1 = now(s);
    int l = snprintf((char *)mat + m, sizeof(mat) - m, "|%u|%u|%u|%s", (unsigned)t1, (unsigned)++s->grant_n, (unsigned)s->rev, in.value);
    uint8_t h[32];
    uic_sha256(mat, (size_t)(m + l), h);
    char hex[UIC_TOKEN_LEN + 2];
    static const char X[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex[i * 2] = X[h[i] >> 4];
        hex[i * 2 + 1] = X[h[i] & 15];
    }
    hex[UIC_TOKEN_LEN] = '\n';
    hex[UIC_TOKEN_LEN + 1] = 0;
    if (!s->port.file_save(s->port.user, hex, UIC_TOKEN_LEN + 1)) {
        r->r = UR_REFUSED;
        snprintf(r->why, sizeof(r->why), "the token file could not be written");
        return;
    }
    uic_lock(s);
    memset(s->tok, 0, sizeof(s->tok));
    memcpy(s->tok, hex, UIC_TOKEN_LEN);
    s->tok_have = true;
    uic_unlock(s);
    memcpy(r->token, hex, UIC_TOKEN_LEN);
    r->token[UIC_TOKEN_LEN] = 0;
    r->r = UR_TAKEN;
}

// ---- the dispatcher ----------------------------------------------------------------------------------------------------------------------
static void apply_inner(UicService *s, const UicIntent &in, UicReply *r)
{
    memset(r, 0, sizeof(*r));
    r->rev = s->rev;
    if (!on_ui(s)) {
        s->violations++;
        r->r = UR_BAD;
        snprintf(r->why, sizeof(r->why), "an intent was applied off the UI task");
        return;
    }
    const char *owner = s->dom->keys_owner();
    const bool menu_owner = !strcmp(owner, "menu");
    switch (in.kind) {
    case UD_GRANT:
        do_grant(s, in, r);
        return;
    case UD_SELECT:
    case UD_ACTIVATE:
    case UD_SET:
        if (s->q_open) {
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "a question is open: answer it first");
        } else if (!menu_owner) {
            r->r = UR_BLOCKED;
            snprintf(r->why, sizeof(r->why), "the keys belong to %s now", owner);
        } else {
            s->dom->intent(in, r);
        }
        break;
    case UD_OP:
        if (s->q_open) {
            r->r = UR_BUSY;
            snprintf(r->why, sizeof(r->why), "a question is open: answer it first");
        } else {
            s->dom->intent(in, r);
        }
        break;
    case UD_KEY:
    case UD_BACK:
    case UD_OPEN:
    case UD_TEXT:
        s->dom->intent(in, r);
        break;
    case UD_ANSWER:
        do_answer(s, in, r);
        break;
    case UD_CANCEL:
        do_cancel(s, in, r);
        break;
    case UD_HISTORY:
        do_history(s, in, r);
        break;
    default:
        r->r = UR_BAD;
        snprintf(r->why, sizeof(r->why), "unknown intent");
        return;
    }
    if (r->r == UR_TAKEN || r->r == UR_HELD || r->r == UR_ASKED) s->rev++;
    r->rev = s->rev;
}

void uic_apply(UicService *s, const UicIntent &in, UicReply *r)
{
    apply_inner(s, in, r);
    if (s->port.log) {
        static const char *const N[] = { "none", "select", "activate", "set", "key", "history", "back", "open", "answer", "cancel", "op", "grant", "text" };
        char line[160];
        const char *arg = in.item[0] ? in.item : in.key[0] ? in.key : in.choice[0] ? in.choice : in.target;
        if (in.local && in.kind == UD_KEY) return;                // the board's own keys stay out of the log
        snprintf(line, sizeof(line), "[ui] %s%s%s%s -> %s", in.local ? "local " : "", in.kind >= 0 && in.kind <= UD_TEXT ? N[in.kind] : "?", arg[0] ? " " : "", arg,
                 uic_r_name(r->r));
        s->port.log(s->port.user, line);
    }
}

void uic_quiet(UicService *s, bool on) { s->quiet = on; }
void uic_hold(UicService *s) { uic_lock(s); }
void uic_release(UicService *s) { uic_unlock(s); }

void uic_local(UicService *s, const UicIntent &in, UicReply *r)
{
    UicIntent x = in;
    x.local = true;
    uic_lock(s);
    s->local_gen++;                                           // a remote intent that waits was made for a screen this one changes: it is refused when its turn comes
    uic_unlock(s);
    uic_apply(s, x, r);
}

void uic_toast(UicService *s, const char *text)
{
    uint32_t rev = uic_next_rev(s);
    char buf[UIC_EV_MAX];
    UicW w(buf, sizeof(buf));
    w.open('{');
    w.ks("t", "toast");
    w.ku("rev", rev);
    w.ks("text", text);
    w.close('}');
    uic_ev_store(s, rev, w.ok ? buf : "{\"t\":\"resync\"}");
}

void uic_event(UicService *s, const char *type, const char *value)
{
    uint32_t rev = uic_next_rev(s);
    char buf[UIC_EV_MAX];
    UicW w(buf, sizeof(buf));
    w.open('{');
    w.ks("t", type);
    w.ku("rev", rev);
    w.ks("v", value);
    w.close('}');
    uic_ev_store(s, rev, w.ok ? buf : "{\"t\":\"resync\"}");
}

// ---- snapshot ----------------------------------------------------------------------------------------------------------------------------
int uic_render(UicService *s, char *out, int cap)
{
    UicW w(out, cap);
    w.open('{');
    w.ki("ui", UIC_CONTRACT);
    w.ks("app", s->dom->app());
    w.ku("rev", s->rev);
    w.ks("keys_owner", s->dom->keys_owner());
    s->dom->snapshot(w);
    w.sep();
    w.key("dialog");
    if (s->q_open) {
        w.open('{');
        w.ks("q", s->q_id);
        w.ks("text", s->q.text);
        w.kb("destructive", s->q.destructive);
        w.kopen("choices", '[');
        w.open('{'); w.ks("id", "yes"); w.ks("label", "Yes"); w.ks("key", s->q.yes_key[0] ? s->q.yes_key : "ret"); w.close('}');
        w.sep();
        w.open('{'); w.ks("id", "no"); w.ks("label", "No"); w.ks("key", s->q.no_key[0] ? s->q.no_key : "esc"); w.close('}');
        w.close(']');
        if (s->q.def[0]) w.ks("default", s->q.def);
        if (s->q.timeout_ms) w.ku("timeout_ms", s->q.timeout_ms);
        w.close('}');
    } else {
        w.raw("null");
    }
    w.kopen("jobs", '[');
    for (int i = 0; i < UIC_JOBS; i++) {
        const UicJob &j = s->job[i];
        if (!j.used) continue;
        w.sep();
        w.open('{');
        w.ks("id", j.id);
        w.ks("kind", j.kind);
        w.ks("title", j.title);
        w.ks("phase", PHASE[j.phase]);
        w.ks("step", j.step);
        w.ki("done", j.done);
        w.ki("total", j.total);
        w.kb("cancelable", j.cancelable);
        if (j.phase >= 3) {
            w.kopen("result", '{');
            w.kb("ok", j.ok);
            w.ks("text", j.text);
            w.close('}');
        }
        w.close('}');
    }
    w.close(']');
    w.close('}');
    return w.length();
}

static uint32_t hash_text(const char *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 16777619u;
    return h;
}

static void render_cache(UicService *s)
{
    uic_lock(s);
    s->snap_valid = false;
    uic_unlock(s);
    uint32_t rev = s->rev;
    int n = uic_render(s, s->snap, s->snap_cap);
    // Some state changes reach no uic_note (a REST call that set something, a media poll): the same rev rendering differently is told by its hash.
    if (n > 0) {
        uint32_t h = hash_text(s->snap, n);
        if (s->snap_hash_rev == rev && s->snap_hash != h) {
            rev = ++s->rev;
            n = uic_render(s, s->snap, s->snap_cap);
            h = n > 0 ? hash_text(s->snap, n) : h;
        }
        s->snap_hash = h;
        s->snap_hash_rev = rev;
    }
    if (n < 0) {
        n = snprintf(s->snap, s->snap_cap, "{\"ui\":%d,\"rev\":%u,\"error\":\"the state does not fit\"}", UIC_CONTRACT, (unsigned)rev);
    }
    uic_lock(s);
    s->snap_len = n;
    s->snap_rev = rev;
    s->snap_valid = true;
    s->snap_at = now(s);
    uic_unlock(s);
}

int uic_service(UicService *s)
{
    if (!on_ui(s)) { s->violations++; return 0; }
    int applied = 0;
    uint32_t t = now(s);
    s->dom->tick(t);
    poll_question(s, t);
    poll_jobs(s);
    for (int i = 0; i < UIC_SLOTS; i++) {
        uic_lock(s);
        bool take = s->slot[i].state == 1;
        if (take) s->slot[i].state = 4;           // taken: the waiting task can no longer free it
        uic_unlock(s);
        if (!take) continue;
        UicReply rep;
        if (s->slot[i].in.kind != UD_GRANT && s->slot[i].gen != s->local_gen) {   // T192.3 S10: the person pressed a key since: the local input wins, the remote one is refused
            memset(&rep, 0, sizeof(rep));
            rep.r = UR_BLOCKED;
            rep.rev = s->rev;
            snprintf(rep.why, sizeof(rep.why), "local input: a key was pressed on the screen since the request was made");
        } else {
            uic_apply(s, s->slot[i].in, &rep);
        }
        applied++;
        uic_lock(s);
        s->slot[i].out = rep;
        s->slot[i].state = s->slot[i].state == 3 ? 0 : 2;   // 3: the waiting task gave up meanwhile; nobody reads the answer
        uic_unlock(s);
    }
    token_poll(s, t);
    bool want = s->req_gen != s->done_gen;
    if (want || (s->nclients > 0 && (!s->snap_valid || s->snap_rev != s->rev || (int32_t)(t - s->snap_at) >= 500))) {
        uint32_t gen = s->req_gen;
        render_cache(s);
        s->done_gen = gen;
    }
    return applied;
}
