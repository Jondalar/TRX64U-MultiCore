#include "ops.h"
#include "ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

OpRegistry::OpRegistry() : owner_("?"), nops_(0), nstates_(0), nevents_(0)
{
    memset(ops_, 0, sizeof(ops_));
    memset(states_, 0, sizeof(states_));
    memset(events_, 0, sizeof(events_));
    err_[0] = 0;
}

// "domain.verb": a dot with text on both sides, nothing but letters, digits, '_' and '.' (a transport carries the name as it is).
bool OpRegistry::name_ok(const char *name)
{
    if (!name || !name[0]) {
        snprintf(err_, sizeof(err_), "%s: a registration without a name", owner_);
        return false;
    }
    int dots = 0;
    for (const char *p = name; *p; p++) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.';
        if (!ok || (*p == '.' && (p == name || !p[1]))) {
            snprintf(err_, sizeof(err_), "%s: \"%s\" is not a name of the form domain.verb", owner_, name);
            return false;
        }
        dots += *p == '.';
    }
    if (!dots) {
        snprintf(err_, sizeof(err_), "%s: \"%s\" is not a name of the form domain.verb", owner_, name);
        return false;
    }
    return true;
}

static bool args_ok(const ArgSpec *a, int n)
{
    if (n < 0 || n > OP_ARGS_MAX || (n && !a)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (!a[i].name || !a[i].name[0] || a[i].type > AT_BLOB) {
            return false;
        }
        if ((a[i].type == AT_INT || a[i].type == AT_ENUM) && a[i].lo > a[i].hi) {
            return false;
        }
        if (a[i].type == AT_ENUM && !a[i].labels) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(a[j].name, a[i].name)) {
                return false;
            }
        }
    }
    return true;
}

int OpRegistry::add_op(const char *name, const ArgSpec *args, int nargs, op_fn fn)
{
    if (!name_ok(name)) {
        return OPE_ARG;
    }
    if (const OpDef *o = find_op(name)) {
        snprintf(err_, sizeof(err_), "operation \"%s\" is owned by %s; the registration of %s is refused", name, o->owner, owner_);
        return OPE_FAILED;
    }
    if (!fn || !args_ok(args, nargs)) {
        snprintf(err_, sizeof(err_), "%s: operation \"%s\" has no handler or a bad argument list", owner_, name);
        return OPE_ARG;
    }
    if (nops_ >= OPS_MAX) {
        snprintf(err_, sizeof(err_), "%s: no room for operation \"%s\" (OPS_MAX)", owner_, name);
        return OPE_FAILED;
    }
    OpDef &d = ops_[nops_++];
    d.name = name;
    d.owner = owner_;
    d.args = args;
    d.nargs = nargs;
    d.fn = fn;
    return OPE_OK;
}

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

int OpRegistry::add_state(const char *name, state_fn get)
{
    if (!name_ok(name)) {
        return OPE_ARG;
    }
    if (const StateDef *s = find_state(name)) {
        snprintf(err_, sizeof(err_), "state \"%s\" is owned by %s; the registration of %s is refused", name, s->owner, owner_);
        return OPE_FAILED;
    }
    if (!get || nstates_ >= STATES_MAX) {
        snprintf(err_, sizeof(err_), "%s: state \"%s\" has no getter or there is no room (STATES_MAX)", owner_, name);
        return OPE_FAILED;
    }
    StateDef &s = states_[nstates_++];
    s.name = name;
    s.nh = (uint16_t)name_hash(name);
    s.owner = owner_;
    s.get = get;
    return OPE_OK;
}

RegMark OpRegistry::mark() const
{
    RegMark m = { nops_, nstates_, nevents_ };
    return m;
}

void OpRegistry::rollback(const RegMark &m)
{
    if (m.nops >= 0 && m.nops < nops_) {
        memset(&ops_[m.nops], 0, sizeof(OpDef) * (nops_ - m.nops));
        nops_ = m.nops;
    }
    if (m.nstates >= 0 && m.nstates < nstates_) {
        memset(&states_[m.nstates], 0, sizeof(StateDef) * (nstates_ - m.nstates));
        nstates_ = m.nstates;
    }
    if (m.nevents >= 0 && m.nevents < nevents_) {
        memset(&events_[m.nevents], 0, sizeof(EventDef) * (nevents_ - m.nevents));
        nevents_ = m.nevents;
    }
}

void OpRegistry::drop_owner(const char *name)
{
    int k = 0;
    for (int i = 0; i < nops_; i++) {
        if (ops_[i].owner && !strcmp(ops_[i].owner, name)) {
            continue;
        }
        if (k != i) {
            ops_[k] = ops_[i];
        }
        k++;
    }
    memset(&ops_[k], 0, sizeof(OpDef) * (nops_ - k));
    nops_ = k;
    k = 0;
    for (int i = 0; i < nstates_; i++) {
        if (states_[i].owner && !strcmp(states_[i].owner, name)) {
            continue;
        }
        if (k != i) {
            states_[k] = states_[i];
        }
        k++;
    }
    memset(&states_[k], 0, sizeof(StateDef) * (nstates_ - k));
    nstates_ = k;
    k = 0;
    for (int i = 0; i < nevents_; i++) {
        if (events_[i].owner && !strcmp(events_[i].owner, name)) {
            continue;
        }
        if (k != i) {
            events_[k] = events_[i];
        }
        k++;
    }
    memset(&events_[k], 0, sizeof(EventDef) * (nevents_ - k));
    nevents_ = k;
}

EventDef *OpRegistry::find_event(const char *name)
{
    for (int i = 0; i < nevents_; i++) {
        if (!strcmp(events_[i].name, name)) {
            return &events_[i];
        }
    }
    return 0;
}

int OpRegistry::add_event(const char *name, const ArgSpec *payload)
{
    if (!name_ok(name)) {
        return OPE_ARG;
    }
    if (EventDef *e = find_event(name)) {
        snprintf(err_, sizeof(err_), "event \"%s\" is owned by %s; the registration of %s is refused", name, e->owner, owner_);
        return OPE_FAILED;
    }
    if ((payload && !args_ok(payload, 1)) || nevents_ >= EVENTS_MAX) {
        snprintf(err_, sizeof(err_), "%s: event \"%s\" has a bad payload or there is no room (EVENTS_MAX)", owner_, name);
        return OPE_FAILED;
    }
    EventDef &e = events_[nevents_++];
    e.name = name;
    e.owner = owner_;
    e.has_payload = payload != 0;
    if (payload) {
        e.payload = *payload;
    }
    return OPE_OK;
}

const OpDef *OpRegistry::find_op(const char *name) const
{
    for (int i = 0; name && i < nops_; i++) {
        if (!strcmp(ops_[i].name, name)) {
            return &ops_[i];
        }
    }
    return 0;
}

const StateDef *OpRegistry::find_state(const char *name) const
{
    const uint16_t nh = name ? (uint16_t)name_hash(name) : 0;
    for (int i = 0; name && i < nstates_; i++) {
        if (states_[i].nh == nh && !strcmp(states_[i].name, name)) {
            return &states_[i];
        }
    }
    return 0;
}

// One value against its spec. 0 or a reason.
static const char *convert(const ArgSpec &a, const OpIn &in, OpArgs *out, int k, char *buf, int n)
{
    switch (a.type) {
    case AT_INT:
    case AT_BOOL: {
        long v;
        if (in.is_int) {
            v = in.i;
        } else if (in.s) {
            if (a.type == AT_BOOL && (!strcasecmp(in.s, "true") || !strcasecmp(in.s, "on"))) {
                v = 1;
            } else if (a.type == AT_BOOL && (!strcasecmp(in.s, "false") || !strcasecmp(in.s, "off"))) {
                v = 0;
            } else {
                char *end = 0;
                v = strtol(in.s, &end, 10);
                if (!in.s[0] || *end) {
                    snprintf(buf, n, "%s: \"%s\" is not a number", a.name, in.s);
                    return buf;
                }
            }
        } else {
            return "a number was expected";
        }
        if (v < a.lo || v > a.hi) {
            snprintf(buf, n, "%s: %d is outside %d .. %d", a.name, (int)v, (int)a.lo, (int)a.hi);   // the firmware's printf has no %ld
            return buf;
        }
        out->iv[k] = (int32_t)v;
        return 0;
    }
    case AT_ENUM: {
        if (in.is_int) {
            if (in.i < a.lo || in.i > a.hi) {
                snprintf(buf, n, "%s: index %d is outside 0 .. %d", a.name, (int)in.i, (int)a.hi);
                return buf;
            }
            out->iv[k] = in.i;
            return 0;
        }
        if (in.s) {
            for (int32_t i = a.lo; i <= a.hi; i++) {
                if (!strcasecmp(a.labels[i], in.s)) {
                    out->iv[k] = i;
                    return 0;
                }
            }
        }
        int w = snprintf(buf, n, "%s: \"%s\" is not one of", a.name, in.s ? in.s : "");
        for (int32_t i = a.lo; i <= a.hi && w > 0 && w < n; i++) {
            w += snprintf(buf + w, n - w, "%s %s", i > a.lo ? "," : "", a.labels[i]);
        }
        return buf;
    }
    case AT_STRING:
    case AT_PATH:
    case AT_SLOT: {
        if (!in.s) {
            return "text was expected";
        }
        size_t l = strlen(in.s);
        if (l > a.maxlen || ((a.type == AT_PATH || a.type == AT_SLOT) && !l)) {
            snprintf(buf, n, "%s: the text is %s", a.name, l ? "too long" : "empty");
            return buf;
        }
        if (a.type == AT_SLOT) {
            for (const char *p = in.s; *p; p++) {
                bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == ':';
                if (!ok) {
                    snprintf(buf, n, "%s: \"%s\" is not a slot id", a.name, in.s);
                    return buf;
                }
            }
        }
        out->sv[k] = in.s;
        return 0;
    }
    case AT_BLOB:
        if (!in.p) {
            return "bytes were expected";
        }
        out->pv[k] = in.p;
        out->nv[k] = in.n;
        return 0;
    }
    return "unknown type";
}

static int bind_args(const char *opname, const ArgSpec *spec, int nspec, const OpIn *in, int nin, OpArgs *out, char *why, int whylen)
{
    memset(out, 0, sizeof(*out));
    out->n = nspec;
    for (int k = 0; k < nin; k++) {
        bool known = false;
        for (int s = 0; s < nspec; s++) {
            known |= in[k].name && !strcmp(in[k].name, spec[s].name);
        }
        if (!known) {
            snprintf(why, whylen, "%s: no argument \"%s\"", opname, in[k].name ? in[k].name : "");
            return OPE_ARG;
        }
        for (int j = 0; j < k; j++) {
            if (!strcmp(in[j].name, in[k].name)) {
                snprintf(why, whylen, "%s: argument \"%s\" given twice", opname, in[k].name);
                return OPE_ARG;
            }
        }
    }
    for (int s = 0; s < nspec; s++) {
        const OpIn *given = 0;
        for (int k = 0; k < nin; k++) {
            if (!strcmp(in[k].name, spec[s].name)) {
                given = &in[k];
            }
        }
        if (!given) {
            snprintf(why, whylen, "%s: argument \"%s\" is missing", opname, spec[s].name);
            return OPE_ARG;
        }
        char buf[120];
        const char *bad = convert(spec[s], *given, out, s, buf, sizeof(buf));
        if (bad) {
            snprintf(why, whylen, "%s: %s", opname, bad);
            return OPE_ARG;
        }
    }
    return OPE_OK;
}

int OpRegistry::bind(const OpDef *d, const OpIn *in, int nin, OpArgs *out, char *why, int whylen) const
{
    return bind_args(d->name, d->args, d->nargs, in, nin, out, why, whylen);
}

static const char *base_of(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

void ops_note_path(CoreCtx *c, const char *path)
{
    if (!c || !path || !strchr(base_of(path), '.') || strlen(path) >= sizeof(c->noted[0])) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (!strcmp(c->noted[i], path)) {
            return;
        }
    }
    snprintf(c->noted[c->noted_next], sizeof(c->noted[0]), "%s", path);
    c->noted_next = (c->noted_next + 1) % 16;
}

const char *ops_find_path(CoreCtx *c, const char *name)
{
    for (int k = 1; c && k <= 16; k++) {                     // newest first
        const char *p = c->noted[(c->noted_next + 16 - k) % 16];
        if (p[0] && !strcmp(base_of(p), name)) {
            return p;
        }
    }
    return 0;
}

int OpRegistry::run(CoreCtx *c, const OpDef *d, const OpArgs &a, OpResult *r) const
{
    r->msg[0] = 0;
    r->more = false;
    r->ndata = 0;
    OpOut o = { r->msg, (int)sizeof(r->msg), false, r->data, OP_DATA_MAX, 0 };
    r->code = d->fn(c, a, o);
    if (r->code == OPE_OK) {
        for (int k = 0; k < d->nargs && k < a.n; k++) {
            if (d->args[k].type == AT_PATH && a.s(k)) {
                ops_note_path(c, a.s(k));
            }
        }
    }
    r->more = o.more;
    r->ndata = o.ndata < 0 ? 0 : o.ndata > OP_DATA_MAX ? OP_DATA_MAX : o.ndata;
    return r->code;
}

int OpRegistry::call(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r) const
{
    r->msg[0] = 0;
    r->more = false;
    r->ndata = 0;
    const OpDef *d = find_op(name);
    if (!d) {
        snprintf(r->msg, sizeof(r->msg), "no operation \"%s\"", name ? name : "");
        return r->code = OPE_NOT_FOUND;
    }
    OpArgs a;
    r->code = bind(d, in, nin, &a, r->msg, sizeof(r->msg));
    if (r->code != OPE_OK) {
        return r->code;
    }
    return run(c, d, a, r);
}

const char *OpRegistry::state(CoreCtx *c, const char *name, uint32_t *rev)
{
    const uint16_t nh = name ? (uint16_t)name_hash(name) : 0;
    for (int i = 0; name && i < nstates_; i++) {
        if (states_[i].nh != nh || strcmp(states_[i].name, name)) {   // the hash first: most names share a long prefix, and the menu asks about 100 times a key
            continue;
        }
        StateDef &s = states_[i];
        const char *v = s.get(c);
        uint32_t h = v ? name_hash(v) + 1 : 0;              // no value is its own hash
        if (!s.seen || h != s.hash) {
            s.seen = true;
            s.hash = h;
            s.rev++;
        }
        if (rev) {
            *rev = s.rev;
        }
        return v;
    }
    if (rev) {
        *rev = 0;
    }
    return 0;
}

int OpRegistry::subscribe(const char *name, event_fn fn, void *user)
{
    EventDef *e = find_event(name);
    if (!e) {
        return OPE_NOT_FOUND;
    }
    if (!fn || e->nsubs >= EVENT_SUBS_MAX) {
        return OPE_FAILED;
    }
    e->fn[e->nsubs] = fn;
    e->user[e->nsubs] = user;
    e->nsubs++;
    return OPE_OK;
}

int OpRegistry::unsubscribe(const char *name, event_fn fn, void *user)
{
    EventDef *e = find_event(name);
    if (!e) {
        return OPE_NOT_FOUND;
    }
    for (int i = 0; i < e->nsubs; i++) {
        if (e->fn[i] == fn && e->user[i] == user) {
            for (int k = i; k + 1 < e->nsubs; k++) {
                e->fn[k] = e->fn[k + 1];
                e->user[k] = e->user[k + 1];
            }
            e->nsubs--;
            return OPE_OK;
        }
    }
    return OPE_NOT_FOUND;
}

int OpRegistry::emit(const char *name, const OpIn *payload)
{
    EventDef *e = find_event(name);
    if (!e) {
        return OPE_NOT_FOUND;
    }
    OpArgs a;
    char why[120];
    int rc = bind_args(name, &e->payload, e->has_payload ? 1 : 0, payload, payload ? 1 : 0, &a, why, sizeof(why));
    if (rc != OPE_OK) {
        return rc;
    }
    for (int i = 0; i < e->nsubs; i++) {
        e->fn[i](e->user[i], e->name, a);
    }
    return e->nsubs;
}
