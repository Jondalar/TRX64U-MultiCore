#include "svc.h"
#include "ops_glue.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void svc_fail(OpResult *r, int code, const char *fmt, ...)
{
    r->code = code;
    r->more = false;
    r->ndata = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->msg, sizeof(r->msg), fmt, ap);
    va_end(ap);
}

int svc_call(CoreCtx *c, const char *name, const JsonObj *args, OpResult *r)
{
    const OpDef *d = ops_of(c)->find_op(name);
    if (!d) {
        svc_fail(r, OPE_NOT_FOUND, "No operation \"%s\" is registered", name);
        return r->code;
    }
    OpIn in[OP_ARGS_MAX];
    int n = 0;
    if (args) {
        for (int k = 0; k < args->n; k++) {
            bool known = false;
            for (int a = 0; a < d->nargs; a++) {
                known |= !strcmp(d->args[a].name, args->key[k]);
            }
            if (!known && args->v[k].t != J_NULL) {
                svc_fail(r, OPE_ARG, "%s has no argument \"%s\"", name, args->key[k]);
                return r->code;
            }
        }
    }
    for (int a = 0; a < d->nargs; a++) {
        const ArgSpec &as = d->args[a];
        const JsonVal *v = args ? args->get(as.name) : 0;
        if (!v || v->t == J_NULL) {
            continue;                                              // the registry says which one is missing
        }
        if (as.type == AT_BLOB) {
            svc_fail(r, OPE_ARG, "%s: argument \"%s\" is a blob; bytes do not travel over a transport", name, as.name);
            return r->code;
        }
        bool numeric = as.type == AT_INT || as.type == AT_ENUM || as.type == AT_BOOL;
        if (numeric) {
            if (v->t == J_NUM && !v->frac) {
                in[n++] = in_int(as.name, v->n);
            } else if (v->t == J_BOOL) {
                in[n++] = in_int(as.name, v->b ? 1 : 0);
            } else if (v->t == J_STR) {
                const char *t = v->s + (v->s[0] == '-' ? 1 : 0);
                bool digits = *t != 0;
                for (const char *q = t; *q; q++) {
                    digits &= *q >= '0' && *q <= '9';
                }
                if (digits && strlen(v->s) < 11) {
                    in[n++] = in_int(as.name, (int32_t)atol(v->s));    // a number that came as text (a query, a form): an enum takes its index so
                } else if (as.type == AT_BOOL && !strcmp(v->s, "true")) {
                    in[n++] = in_int(as.name, 1);
                } else if (as.type == AT_BOOL && !strcmp(v->s, "false")) {
                    in[n++] = in_int(as.name, 0);
                } else {
                    in[n++] = in_str(as.name, v->s);               // an enum's label, or a number as text
                }
            } else {
                svc_fail(r, OPE_ARG, "%s: argument \"%s\" is a number or a label, not a fraction", name, as.name);
                return r->code;
            }
        } else if (v->t == J_STR) {
            in[n++] = in_str(as.name, v->s);
        } else {
            svc_fail(r, OPE_ARG, "%s: argument \"%s\" is text", name, as.name);
            return r->code;
        }
    }
    return ops_call(c, name, in, n, r);
}

int svc_http_status(int code)
{
    switch (code) {
    case OPE_OK: return 200;
    case OPE_ARG: return 400;
    case OPE_NOT_FOUND: return 404;
    case OPE_IO: return 500;
    case OPE_FORMAT: return 422;
    case OPE_BUSY: return 503;
    case OPE_UNSUPPORTED: return 501;
    case OPE_FAILED: return 409;
    }
    return 500;
}

bool svc_put_result(char *out, int cap, int *pos, const OpResult &r)
{
    int p = *pos;
    char b64[((OP_DATA_MAX + 2) / 3) * 4 + 4];
    b64[0] = 0;
    if (r.ndata > 0) {
        b64_encode(r.data, r.ndata, b64, sizeof(b64));
    }
    if (!json_put(out, cap, &p, "{\"code\":%d,\"msg\":", r.code) || !json_put_string(out, cap, &p, r.msg) ||
        (r.ndata > 0 && (!json_put(out, cap, &p, ",\"data\":") || !json_put_string(out, cap, &p, b64))) ||
        !json_put(out, cap, &p, ",\"more\":%s}", r.more ? "true" : "false")) {
        out[*pos] = 0;
        return false;
    }
    *pos = p;
    return true;
}

static const char *type_name(uint8_t t)
{
    static const char *const n[] = { "int", "bool", "enum", "string", "path", "slot", "blob" };
    return t < 7 ? n[t] : "?";
}

static bool put_arg(char *out, int cap, int *p, const ArgSpec &a)
{
    if (!json_put(out, cap, p, "{\"name\":") || !json_put_string(out, cap, p, a.name) || !json_put(out, cap, p, ",\"type\":\"%s\"", type_name(a.type))) {
        return false;
    }
    if (a.type == AT_INT) {
        return json_put(out, cap, p, ",\"min\":%d,\"max\":%d}", (int)a.lo, (int)a.hi);
    }
    if (a.type == AT_ENUM) {
        if (!json_put(out, cap, p, ",\"labels\":[")) {
            return false;
        }
        for (int32_t k = a.lo; k <= a.hi; k++) {
            if ((k > a.lo && !json_put(out, cap, p, ",")) || !json_put_string(out, cap, p, a.labels[k])) {
                return false;
            }
        }
        return json_put(out, cap, p, "]}");
    }
    if (a.type == AT_STRING || a.type == AT_PATH || a.type == AT_SLOT) {
        return json_put(out, cap, p, ",\"maxlen\":%u}", (unsigned)a.maxlen);
    }
    return json_put(out, cap, p, "}");
}

bool svc_put_op(char *out, int cap, int *pos, const OpDef &d)
{
    int p = *pos;
    bool ok = json_put(out, cap, &p, "{\"name\":") && json_put_string(out, cap, &p, d.name) && json_put(out, cap, &p, ",\"owner\":") &&
              json_put_string(out, cap, &p, d.owner) && json_put(out, cap, &p, ",\"args\":[");
    for (int i = 0; ok && i < d.nargs; i++) {
        ok = (i == 0 || json_put(out, cap, &p, ",")) && put_arg(out, cap, &p, d.args[i]);
    }
    ok = ok && json_put(out, cap, &p, "]}");
    if (!ok) {
        out[*pos] = 0;
        return false;
    }
    *pos = p;
    return true;
}

bool svc_put_state(CoreCtx *c, char *out, int cap, int *pos, const StateDef &d)
{
    uint32_t rev = 0;
    const char *v = ops_of(c)->state(c, d.name, &rev);
    int p = *pos;
    bool ok = json_put(out, cap, &p, "{\"name\":") && json_put_string(out, cap, &p, d.name) && json_put(out, cap, &p, ",\"owner\":") &&
              json_put_string(out, cap, &p, d.owner) && json_put(out, cap, &p, ",\"value\":") &&
              (v ? json_put_string(out, cap, &p, v) : json_put(out, cap, &p, "null")) && json_put(out, cap, &p, ",\"rev\":%u}", (unsigned)rev);
    if (!ok) {
        out[*pos] = 0;
        return false;
    }
    *pos = p;
    return true;
}

bool svc_put_event(char *out, int cap, int *pos, const EventDef &d)
{
    int p = *pos;
    bool ok = json_put(out, cap, &p, "{\"name\":") && json_put_string(out, cap, &p, d.name) && json_put(out, cap, &p, ",\"owner\":") &&
              json_put_string(out, cap, &p, d.owner) && json_put(out, cap, &p, ",\"payload\":");
    ok = ok && (d.has_payload ? put_arg(out, cap, &p, d.payload) : json_put(out, cap, &p, "null")) && json_put(out, cap, &p, "}");
    if (!ok) {
        out[*pos] = 0;
        return false;
    }
    *pos = p;
    return true;
}
