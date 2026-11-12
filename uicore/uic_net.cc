// uicore network side: token gate, the /v1/ui routes, the WebSocket client session. Runs on network tasks: it copies and queues, it never touches UI state.
#include "uic_priv.h"
#include <stdio.h>
#include <string.h>

static uint32_t now(UicService *s) { return s->port.now_ms ? s->port.now_ms(s->port.user) : 0; }

static void sleep_ms(UicService *s, int ms)
{
    if (s->port.sleep_ms) s->port.sleep_ms(s->port.user, ms);
}

// ---- token gate (T192.1 D-G, S2) -------------------------------------------------------------------------------------------------------
bool uic_route_public(const char *method, const char *path)
{
    if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/v1/ui/hello"))) return true;
    if (!strcmp(method, "POST") && !strcmp(path, "/v1/ui/grant")) return true;
    return false;
}

static bool token_matches(UicService *s, const char *given)
{
    if (!given || !given[0]) return false;
    char b[UIC_TOKEN_LEN + 2];
    memset(b, 0, sizeof(b));
    strncpy(b, given, UIC_TOKEN_LEN + 1);          // one character more than a token: a longer string keeps a non-zero byte at the end
    b[UIC_TOKEN_LEN + 1] = 0;
    char c[UIC_TOKEN_LEN + 2];
    uic_lock(s);
    bool have = s->tok_have;
    memcpy(c, s->tok, sizeof(c) < sizeof(s->tok) ? sizeof(c) : sizeof(s->tok));
    uic_unlock(s);
    if (!have) return false;
    return uic_ct_equal(b, c);
}

int uic_gate(UicService *s, const char *method, const char *path, const char *query_tok, const char *bearer, const char *xtrx)
{
    if (!s->port.file_save) return 0;               // a build without a token (benches of other parts)
    if (uic_route_public(method, path)) return 0;
    bool ok = token_matches(s, query_tok);
    ok = token_matches(s, bearer) || ok;
    ok = token_matches(s, xtrx) || ok;
    return ok ? 0 : 401;
}

// ---- intents -----------------------------------------------------------------------------------------------------------------------------
bool uic_intent_parse(const char *js, int n, UicIntent *in)
{
    memset(in, 0, sizeof(*in));
    char d[16], v[100];
    if (uic_json_get(js, n, "do", d, sizeof(d)) != 1) return false;
    static const struct { const char *n; int k; } K[] = {
        { "select", UD_SELECT }, { "activate", UD_ACTIVATE }, { "set", UD_SET }, { "key", UD_KEY }, { "history", UD_HISTORY },
        { "back", UD_BACK }, { "open", UD_OPEN }, { "answer", UD_ANSWER }, { "cancel", UD_CANCEL }, { "text", UD_TEXT }, { "op", UD_OP }
    };
    for (unsigned i = 0; i < sizeof(K) / sizeof(K[0]); i++) if (!strcmp(d, K[i].n)) in->kind = K[i].k;
    if (!in->kind) return false;
    uic_json_get(js, n, "id", in->cid, sizeof(in->cid));
    uic_json_get(js, n, "item", in->item, sizeof(in->item));
    uic_json_get(js, n, "key", in->key, sizeof(in->key));
    uic_json_get(js, n, "value", in->value, sizeof(in->value));
    uic_json_get(js, n, "target", in->target, sizeof(in->target));
    uic_json_get(js, n, "q", in->q, sizeof(in->q));
    uic_json_get(js, n, "choice", in->choice, sizeof(in->choice));
    uic_json_get(js, n, "job", in->job, sizeof(in->job));
    uic_json_get(js, n, "dir", in->dir, sizeof(in->dir));
    uic_json_raw(js, n, "args", in->args, sizeof(in->args));
    if (uic_json_get(js, n, "menu", v, sizeof(v))) in->menu = !strcmp(v, "true");
    if (uic_json_get(js, n, "submit", v, sizeof(v))) in->submit = !strcmp(v, "true");
    if (in->kind == UD_SET && !in->item[0] && !in->key[0]) return false;
    if (in->kind == UD_OP && !in->item[0]) return false;
    return true;
}

int uic_ack_json(const UicIntent &in, const UicReply &r, bool ws, char *out, int cap)
{
    UicW w(out, cap);
    w.open('{');
    if (ws) w.ks("t", "ack");
    if (in.cid[0]) {
        bool num = true;
        for (const char *c = in.cid; *c; c++) if (*c < '0' || *c > '9') num = false;
        w.sep();
        w.key("id");
        if (num) w.raw(in.cid); else w.str(in.cid);
    }
    w.ku("rev", r.rev);
    w.ks("r", uic_r_name(r.r));
    if (r.job[0]) w.ks("job", r.job);
    if (r.q[0]) w.ks("q", r.q);
    if (r.why[0]) w.ks("why", r.why);
    if (r.text[0] || in.kind == UD_HISTORY) w.ks("text", r.text);
    w.close('}');
    return w.length();
}

void uic_submit(UicService *s, const UicIntent &in, UicReply *r)
{
    memset(r, 0, sizeof(*r));
    int idx = -1;
    uic_lock(s);
    for (int i = 0; i < UIC_SLOTS; i++) {
        if (s->slot[i].state == 0) {
            idx = i;
            s->slot[i].state = 4;                     // reserved while the intent is copied; the UI task takes state 1 only
            break;
        }
    }
    uic_unlock(s);
    if (idx < 0) {
        r->r = UR_BUSY;
        r->rev = s->rev;
        snprintf(r->why, sizeof(r->why), "too many intents waiting");
        return;
    }
    UicIntent x = in;
    x.local = false;
    if (x.kind == UD_GRANT) snprintf(x.value, sizeof(x.value), "%u", (unsigned)now(s));
    s->slot[idx].in = x;
    uic_lock(s);
    s->slot[idx].gen = s->local_gen;
    s->slot[idx].state = 1;
    uic_unlock(s);
    for (int waited = 0; waited <= 3000; waited += 5) {
        uic_lock(s);
        bool done = s->slot[idx].state == 2;
        if (done) {
            *r = s->slot[idx].out;
            s->slot[idx].state = 0;
        }
        uic_unlock(s);
        if (done) return;
        sleep_ms(s, 5);
    }
    uic_lock(s);
    if (s->slot[idx].state == 2) {
        *r = s->slot[idx].out;
        s->slot[idx].state = 0;
        uic_unlock(s);
        return;
    }
    s->slot[idx].state = s->slot[idx].state == 1 ? 0 : 3;   // not yet taken: free it; being applied: the UI task frees it
    uic_unlock(s);
    r->r = UR_BUSY;
    r->rev = s->rev;
    snprintf(r->why, sizeof(r->why), "the UI is still busy with an earlier action: send it again");
}

// ---- snapshot ----------------------------------------------------------------------------------------------------------------------------
int uic_snapshot(UicService *s, char *out, int cap, uint32_t *rev)
{
    uic_lock(s);
    uint32_t gen = ++s->req_gen;         // always a fresh render: a reader never gets a state older than its request
    uic_unlock(s);
    // The copy is made in the same locked look that finds the cache valid: the UI task clears snap_valid under the lock before it writes the buffer (T192 L1), so a
    // copy can never see a render half written.
    for (int waited = 0;; waited += 5) {
        uic_lock(s);
        if ((int32_t)(s->done_gen - gen) >= 0 && s->snap_valid) {
            int n = -1;
            if (s->snap_len < cap) {
                memcpy(out, s->snap, s->snap_len);
                out[s->snap_len] = 0;
                n = s->snap_len;
                if (rev) *rev = s->snap_rev;
            }
            uic_unlock(s);
            return n;
        }
        uic_unlock(s);
        if (waited >= 3000) return -1;
        sleep_ms(s, 5);
    }
}

// ---- the routes --------------------------------------------------------------------------------------------------------------------------
static int err_body(char *out, int cap, int *status, int st, const char *msg)
{
    *status = st;
    UicW w(out, cap);
    w.open('{');
    w.ks("error", msg);
    w.close('}');
    return w.length();
}

int uic_http(UicService *s, const char *method, const char *path, const char *body, int body_n, char *out, int cap, int *status)
{
    const bool get = !strcmp(method, "GET"), post = !strcmp(method, "POST");
    if (!strcmp(path, "/v1/ui/hello")) {
        if (!get) return err_body(out, cap, status, 405, "GET only");
        UicW w(out, cap);
        w.open('{');
        w.ki("ui", UIC_CONTRACT);
        w.ki("ui_min", UIC_CONTRACT_MIN);
        w.ks("app", s->dom->app());
        uic_lock(s);
        bool have = s->tok_have;
        uic_unlock(s);
        w.kb("grant", s->port.file_save && !have);
        w.close('}');
        *status = 200;
        return w.length();
    }
    if (!strcmp(path, "/v1/ui/grant")) {
        if (!post) return err_body(out, cap, status, 405, "POST only");
        UicIntent in;
        memset(&in, 0, sizeof(in));
        in.kind = UD_GRANT;
        UicReply r;
        uic_submit(s, in, &r);
        if (r.r == UR_TAKEN) {
            UicW w(out, cap);
            w.open('{');
            w.ks("token", r.token);
            w.close('}');
            *status = 200;
            return w.length();
        }
        return err_body(out, cap, status, r.r == UR_BUSY ? 503 : 403, r.why[0] ? r.why : "no grant");
    }
    if (!strcmp(path, "/v1/ui/state")) {
        if (!get) return err_body(out, cap, status, 405, "GET only");
        int n = uic_snapshot(s, out, cap, 0);
        if (n < 0) return err_body(out, cap, status, 504, "the UI did not answer, or the state does not fit");
        *status = 200;
        return n;
    }
    if (!strcmp(path, "/v1/ui/intent")) {
        if (!post) return err_body(out, cap, status, 405, "POST only");
        UicIntent in;
        if (!uic_intent_parse(body, body_n, &in)) return err_body(out, cap, status, 400, "not an intent: {\"do\":\"activate|select|set|key|back|open|answer|cancel|history\", ...}");
        UicReply r;
        uic_submit(s, in, &r);
        int n = uic_ack_json(in, r, false, out, cap);
        *status = r.r == UR_TAKEN || r.r == UR_HELD || r.r == UR_ASKED ? 200 : r.r == UR_GONE ? 404 : r.r == UR_BAD ? 400 : 409;
        return n;
    }
    return -2;
}

// ---- WebSocket client --------------------------------------------------------------------------------------------------------------------
void uic_client_open(UicService *s, UicClient *c)
{
    memset(c, 0, sizeof(*c));
    c->snap_on = true;
    uic_lock(s);
    s->nclients++;
    uic_unlock(s);
}

void uic_client_close(UicService *s, UicClient *c)
{
    (void)c;
    uic_lock(s);
    if (s->nclients > 0) s->nclients--;
    uic_unlock(s);
}

// out = {"t":"snap", + cache without its opening brace; caller holds the lock
static int copy_snap_locked(UicService *s, char *out, int cap)
{
    static const char head[] = "{\"t\":\"snap\",";
    int hl = (int)sizeof(head) - 1;
    if (hl + s->snap_len >= cap) return -1;
    memcpy(out, head, hl);
    memcpy(out + hl, s->snap + 1, s->snap_len - 1);
    out[hl + s->snap_len - 1] = 0;
    return hl + s->snap_len - 1;
}

int uic_client_next(UicService *s, UicClient *c, char *out, int cap)
{
    if (c->closing) return 0;
    if (!c->hello_sent) {
        UicW w(out, cap);
        w.open('{');
        w.ks("t", "hello");
        w.ki("ui", UIC_CONTRACT);
        w.ki("ui_min", UIC_CONTRACT_MIN);
        w.ks("app", s->dom->app());
        w.ku("rev", s->rev);
        w.close('}');
        c->hello_sent = true;
        c->hello_rev = s->rev;
        return w.length();
    }
    int n = 0;
    uic_lock(s);
    if (!c->first_snap_sent) {
        if (c->snap_on && s->snap_valid && s->snap_rev >= c->hello_rev) {
            n = copy_snap_locked(s, out, cap);
            if (n > 0) {
                c->snap_rev = s->snap_rev;
                c->ev_rev = s->snap_rev;
                c->first_snap_sent = true;
            }
        } else if (!c->snap_on) {
            c->snap_rev = s->rev;
            c->ev_rev = s->rev;
            c->first_snap_sent = true;
            n = snprintf(out, cap, "{\"t\":\"note\",\"rev\":%u}", (unsigned)s->rev);
        }
        uic_unlock(s);
        return n > 0 ? n : 0;
    }
    if (c->ev_rev < s->ev_floor) {                       // events fell out of the ring before this client read them
        n = snprintf(out, cap, "{\"t\":\"resync\",\"rev\":%u}", (unsigned)s->rev);
        c->closing = true;
        uic_unlock(s);
        return n;
    }
    for (uint32_t i = 0; i < s->ev_count; i++) {
        const UicEvent &e = s->ev[(s->ev_head + i) % UIC_EVENTS];
        if (e.rev > c->ev_rev) {
            n = snprintf(out, cap, "%s", e.text);
            if (n >= cap) n = -1;
            c->ev_rev = e.rev;
            uic_unlock(s);
            return n > 0 ? n : 0;
        }
    }
    if (c->snap_on) {
        if (s->snap_valid && s->snap_rev > c->snap_rev) {
            n = copy_snap_locked(s, out, cap);
            if (n > 0) c->snap_rev = s->snap_rev;
        }
    } else if (s->rev != c->snap_rev) {
        c->snap_rev = s->rev;
        n = snprintf(out, cap, "{\"t\":\"note\",\"rev\":%u}", (unsigned)s->rev);
    }
    uic_unlock(s);
    return n > 0 ? n : 0;
}

int uic_client_in(UicService *s, UicClient *c, const char *msg, int n, char *out, int cap)
{
    char t[16], v[16];
    UicIntent in;
    if (uic_intent_parse(msg, n, &in)) {
        UicReply r;
        uic_submit(s, in, &r);
        return uic_ack_json(in, r, true, out, cap);
    }
    if (uic_json_get(msg, n, "t", t, sizeof(t)) == 1) {
        if (!strcmp(t, "sub")) {
            uic_json_get(msg, n, "push", v, sizeof(v));
            c->snap_on = strcmp(v, "note") != 0;
            UicW w(out, cap);
            w.open('{');
            w.ks("t", "sub");
            w.ks("push", c->snap_on ? "snap" : "note");
            w.close('}');
            return w.length();
        }
        if (!strcmp(t, "get")) {
            static const char head[] = "{\"t\":\"snap\",";
            int hl = (int)sizeof(head) - 1;
            int len = uic_snapshot(s, out + hl - 1, cap - hl + 1, 0);   // the snapshot starts with '{' which the head replaces
            if (len < 0) {
                UicW w(out, cap);
                w.open('{');
                w.ks("t", "error");
                w.ks("why", "the UI did not answer");
                w.close('}');
                return w.length();
            }
            memcpy(out, head, hl);
            return hl + len - 1;
        }
    }
    UicW w(out, cap);
    w.open('{');
    w.ks("t", "error");
    w.ks("why", "not an intent and not a known message");
    w.close('}');
    return w.length();
}
