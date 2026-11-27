#include "rpc.h"
#include "rest.h"
#include "rpccontract.h"
#include "svc.h"
#include "ops_glue.h"
#include <stdio.h>
#include <string.h>

// Names beyond the TRX64 contract (rpccontract.cc answers that one) that an operation of the service layer answers. A name here is data: its operation may not be
// registered by this core, and the call then says so.
const RpcAlias rpc_aliases[] = {
    { "session/warm_reset", "machine.warm_reset" },
    { "media/eject", "drive.eject" },
};
const int rpc_naliases = (int)(sizeof(rpc_aliases) / sizeof(rpc_aliases[0]));

void rpc_open(RpcSession *s, CoreCtx *c, RpcLock *lock)
{
    memset(s, 0, sizeof(*s));
    s->c = c;
    s->lock = lock;
}

namespace {

struct Guard {
    RpcSession *s;
    explicit Guard(RpcSession *x) : s(x) { if (s->lock) s->lock->lock(); }
    ~Guard() { if (s->lock) s->lock->unlock(); }
};

// An event reaches here on the task that emitted it. It is turned into a notification and queued; nothing is sent from this task.
void on_event(void *user, const char *name, const OpArgs &payload)
{
    RpcSession *s = (RpcSession *)user;
    const EventDef *e = 0;
    OpRegistry *reg = ops_of(s->c);
    for (int i = 0; i < reg->nevents(); i++) {
        if (!strcmp(reg->event_at(i)->name, name)) {
            e = reg->event_at(i);
        }
    }
    char note[RPC_NOTE_MAX];
    int p = 0;
    bool ok = json_put(note, sizeof(note), &p, "{\"jsonrpc\":\"2.0\",\"method\":") && json_put_string(note, sizeof(note), &p, name) &&
              json_put(note, sizeof(note), &p, ",\"params\":{");
    if (ok && e && e->has_payload) {
        const ArgSpec &a = e->payload;
        ok = json_put_string(note, sizeof(note), &p, a.name) && json_put(note, sizeof(note), &p, ":");
        if (ok && (a.type == AT_INT)) {
            ok = json_put(note, sizeof(note), &p, "%d", (int)payload.i(0));
        } else if (ok && a.type == AT_BOOL) {
            ok = json_put(note, sizeof(note), &p, payload.i(0) ? "true" : "false");
        } else if (ok && a.type == AT_ENUM) {
            ok = json_put_string(note, sizeof(note), &p, a.labels[payload.i(0)]);
        } else if (ok && a.type != AT_BLOB) {
            ok = json_put_string(note, sizeof(note), &p, payload.s(0));
        } else if (ok) {
            ok = json_put(note, sizeof(note), &p, "null");
        }
    }
    ok = ok && json_put(note, sizeof(note), &p, "}}");
    if (!ok) {
        return;
    }
    Guard g(s);
    if (s->qcount >= RPC_QUEUE) {
        s->dropped++;
        return;
    }
    memcpy(s->q[(s->qhead + s->qcount) % RPC_QUEUE], note, p + 1);
    s->qcount++;
}

int put_id(const JsonRpcReq &r, char *out, int cap, int *p)
{
    return json_put(out, cap, p, "\"id\":%s", r.has_id ? r.id : "null");
}

int reply_error(const JsonRpcReq &r, int code, const char *msg, char *out, int cap)
{
    int p = 0;
    if (!json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",") || !put_id(r, out, cap, &p) || !json_put(out, cap, &p, ",\"error\":{\"code\":%d,\"message\":", code) ||
        !json_put_string(out, cap, &p, msg) || !json_put(out, cap, &p, "}}")) {
        return -1;
    }
    return p;
}

int reply_result(const JsonRpcReq &r, const char *result_json, char *out, int cap)
{
    int p = 0;
    if (!json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",") || !put_id(r, out, cap, &p) || !json_put(out, cap, &p, ",\"result\":%s}", result_json)) {
        return -1;
    }
    return p;
}

}

// The name the registry knows a method by: the method itself ("settings.set"), else the method with its slashes turned into dots ("session/state" is
// "session.state", "trace/run/start" is "trace.run.start"; a registered name has dots only). False when no operation of the registry has either.
static bool rpc_registered(OpRegistry *reg, const char *method, char *name, int cap)
{
    if (reg->find_op(method)) {
        snprintf(name, cap, "%s", method);
        return true;
    }
    if (!strchr(method, '/') || (int)strlen(method) >= cap) {
        return false;
    }
    int i = 0;
    for (; method[i]; i++) {
        name[i] = method[i] == '/' ? '.' : method[i];
    }
    name[i] = 0;
    return reg->find_op(name) != 0;
}

// Runs the operation `name` for the request and writes the response: a result, or the error of the operation (-32601 when the name is no operation).
static int rpc_call_op(RpcSession *s, const JsonRpcReq &r, const char *name, char *result, int rcap, char *out, int cap)
{
    OpRegistry *reg = ops_of(s->c);
    OpResult res;
    int rp = 0;
    s->c->fs->file_loading(true);
    svc_call_net(s->c, name, r.has_params ? &r.params : 0, &res);       // D19a: a destructive operation does not run on a socket, it says where to ask
    // T239.3 D41 (review): this task may wait, the menu's may not: an operation that failed on a file the medium is still fetching (/Online) is asked again until it is there, 120 s at most
    for (uint32_t t0 = s->c->io->now_ms(); res.code != OPE_OK && s->c->fs->file_loading(false) && s->c->io->now_ms() - t0 < 120000; ) {
        s->c->io->delay_ms(300);
        s->c->fs->file_loading(true);
        svc_call_net(s->c, name, r.has_params ? &r.params : 0, &res);
    }
    if (res.code == OPE_NOT_FOUND && !reg->find_op(name)) {
        return reply_error(r, -32601, res.msg, out, cap);       // no such method
    }
    if (res.code == OPE_OK) {
        svc_put_result(result, rcap, &rp, res);
        return r.has_id ? reply_result(r, result, out, cap) : 0;
    }
    return reply_error(r, res.code, res.msg, out, cap);
}


void rpc_close(RpcSession *s)
{
    OpRegistry *reg = ops_of(s->c);
    for (int i = 0; i < s->nsub; i++) {
        reg->unsubscribe(s->sub[i], on_event, s);
    }
    s->nsub = 0;
}

int rpc_handle(RpcSession *s, const char *msg, int n, char *out, int cap)
{
    JsonRpcReq *r = new JsonRpcReq;
    int code;
    char why[160];
    int len;
    if (json_parse_rpc(msg, n, r, &code, why, sizeof(why))) {
        r->has_id = false;                                          // a request that could not be read has no id to answer with
        len = reply_error(*r, code, why, out, cap);
        delete r;
        return len;
    }
    OpRegistry *reg = ops_of(s->c);
    const char *m = r->method;
    char result[RPC_RESULT_MAX];
    char name[64];
    int rp = 0;
    result[0] = 0;
    if (!r->version_ok) {
        len = reply_error(*r, -32600, "\"jsonrpc\" has to be \"2.0\"", out, cap);
    } else if (!strcmp(m, "service.ops")) {
        const JsonVal *f = r->params.get("from");
        int from = f && f->t == J_NUM ? f->n : 0;
        json_put(result, sizeof(result), &rp, "{\"count\":%d,\"ops\":[", reg->nops());
        int i = from;
        for (; i < reg->nops(); i++) {
            int mark = rp;
            if ((i > from && !json_put(result, sizeof(result) - 40, &rp, ",")) || !svc_put_op(result, sizeof(result) - 40, &rp, *reg->op_at(i))) {
                rp = mark;
                break;
            }
        }
        json_put(result, sizeof(result), &rp, "],\"next\":");
        if (i < reg->nops() && i > from) {
            json_put(result, sizeof(result), &rp, "%d}", i);
        } else {
            json_put(result, sizeof(result), &rp, "null}");
        }
        len = r->has_id ? reply_result(*r, result, out, cap) : 0;
    } else if (!strcmp(m, "service.state")) {
        const JsonVal *nm = r->params.get("name");
        if (nm && nm->t == J_STR) {
            const StateDef *d = reg->find_state(nm->s);
            if (!d) {
                len = reply_error(*r, OPE_NOT_FOUND, "No such state value", out, cap);
            } else {
                svc_put_state(s->c, result, sizeof(result), &rp, *d);
                len = reply_result(*r, result, out, cap);
            }
        } else {
            json_put(result, sizeof(result), &rp, "{\"count\":%d,\"state\":[", reg->nstates());
            for (int i = 0; i < reg->nstates(); i++) {
                int mark = rp;
                if ((i && !json_put(result, sizeof(result) - 8, &rp, ",")) || !svc_put_state(s->c, result, sizeof(result) - 8, &rp, *reg->state_at(i))) {
                    rp = mark;
                    break;
                }
            }
            json_put(result, sizeof(result), &rp, "]}");
            len = reply_result(*r, result, out, cap);
        }
    } else if (!strcmp(m, "service.subscribe") || !strcmp(m, "service.unsubscribe")) {
        const JsonVal *ev = r->params.get("event");
        bool sub = !strcmp(m, "service.subscribe");
        if (!ev || ev->t != J_STR) {
            len = reply_error(*r, -32602, "\"event\" is the name of an event", out, cap);
        } else {
            int idx = -1;
            for (int i = 0; i < s->nsub; i++) {
                if (!strcmp(s->sub[i], ev->s)) {
                    idx = i;
                }
            }
            int rc = OPE_OK;
            if (sub && idx < 0) {
                if (s->nsub >= RPC_SUBS_MAX || strlen(ev->s) >= sizeof(s->sub[0])) {
                    rc = OPE_FAILED;
                } else {
                    rc = reg->subscribe(ev->s, on_event, s);
                    if (rc == OPE_OK) {
                        memcpy(s->sub[s->nsub++], ev->s, strlen(ev->s) + 1);
                    }
                }
            } else if (!sub) {
                if (idx < 0) {
                    rc = OPE_NOT_FOUND;
                } else {
                    reg->unsubscribe(ev->s, on_event, s);
                    for (int k = idx; k + 1 < s->nsub; k++) {
                        memcpy(s->sub[k], s->sub[k + 1], sizeof(s->sub[k]));
                    }
                    s->nsub--;
                }
            }
            if (rc == OPE_OK) {
                len = r->has_id ? reply_result(*r, "{\"code\":0,\"msg\":\"\",\"more\":false}", out, cap) : 0;
            } else {
                len = reply_error(*r, rc, rc == OPE_NOT_FOUND ? "No such event, or not subscribed" : "Too many subscriptions", out, cap);
            }
        }
    } else if (rpc_registered(reg, m, name, sizeof(name))) {
        // an operation the registry holds answers first: a linked library's method (D31) is served before the contract table, and a name of the table
        // that a library registered is the library's
        len = rpc_call_op(s, *r, name, result, sizeof(result), out, cap);
    } else if ((len = rpc_contract_handle(s, *r, out, cap)) != -2) {
        // a method of the TRX64 runtime contract: answered, or -32601 with the reason (rpccontract.cc)
        if (!r->has_id) {
            len = 0;
        }
    } else {
        const char *alias = m;
        for (int a = 0; a < rpc_naliases; a++) {
            if (!strcmp(m, rpc_aliases[a].method)) {
                alias = rpc_aliases[a].op;
            }
        }
        len = rpc_call_op(s, *r, alias, result, sizeof(result), out, cap);
    }
    if (!r->has_id) {
        len = 0;                                                    // a notification is not answered, not even with an error
    }
    delete r;
    return len;
}

int rpc_next(RpcSession *s, char *out, int cap)
{
    Guard g(s);
    if (!s->qcount) {
        return 0;
    }
    int n = (int)strlen(s->q[s->qhead]);
    if (n >= cap) {
        return 0;
    }
    memcpy(out, s->q[s->qhead], n + 1);
    s->qhead = (s->qhead + 1) % RPC_QUEUE;
    s->qcount--;
    return n;
}
