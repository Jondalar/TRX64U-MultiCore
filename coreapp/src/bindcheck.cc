#include "bindcheck.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool text(const char *s) { return s && s[0]; }

// A refusal that names a thing: kept in one buffer, valid until the next check.
static char why_[240];
static const char *bind_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why_, sizeof(why_), fmt, ap);
    va_end(ap);
    return why_;
}

static const ArgSpec *arg_of(const OpDef *o, const char *name)
{
    for (int i = 0; i < o->nargs; i++) {
        if (!strcmp(o->args[i].name, name)) {
            return &o->args[i];
        }
    }
    return 0;
}

// The operation behind a descriptor entry exists and its bindings fill every argument with a value of the argument's type. `what` says
// where the entry is ("tasks", "filetypes"); `file_ok`: the entry has a file to bind with $file; `ask_ok`: it asks for an address.
const char *bind_check(const OpRegistry &reg, const char *what, const char *label, const char *op, const OpBind *b, int nb, bool file_ok,
                              bool ask_ok, bool text_ask_ok)
{
    const OpDef *o = text(op) ? reg.find_op(op) : 0;
    if (!o) {
        return bind_fail("%s: \"%s\" calls operation \"%s\", which no contributor registered", what, label, op ? op : "");
    }
    if (nb < 0 || (nb && !b)) {
        return bind_fail("%s: \"%s\" has a binding count without a table", what, label);
    }
    for (int i = 0; i < nb; i++) {
        const ArgSpec *a = arg_of(o, b[i].arg);
        if (!a) {
            return bind_fail("%s: \"%s\" binds \"%s\", which %s has no argument of", what, label, b[i].arg, op);
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(b[j].arg, b[i].arg)) {
                return bind_fail("%s: \"%s\" binds \"%s\" twice", what, label, b[i].arg);
            }
        }
        bool text_arg = a->type == AT_STRING || a->type == AT_PATH || a->type == AT_SLOT;
        bool num_arg = a->type == AT_INT || a->type == AT_BOOL || a->type == AT_ENUM;
        switch (b[i].kind) {
        case BIND_FILE:
            if (!file_ok) return bind_fail("%s: \"%s\" has no file to bind to \"%s\"", what, label, b[i].arg);
            if (!text_arg) return bind_fail("%s: \"%s\" binds the file to \"%s\" of %s, which is not text", what, label, b[i].arg, op);
            break;
        case BIND_INT:
            if (!num_arg) return bind_fail("%s: \"%s\" binds a number to \"%s\" of %s, which is not a number", what, label, b[i].arg, op);
            if (b[i].i < a->lo || b[i].i > a->hi) {
                return bind_fail("%s: \"%s\" binds %d to \"%s\" of %s, which takes %d .. %d", what, label, (int)b[i].i, b[i].arg, op, (int)a->lo, (int)a->hi);
            }
            break;
        case BIND_STR: {
            if (!b[i].s) return bind_fail("%s: \"%s\" binds no text to \"%s\"", what, label, b[i].arg);
            bool label_ok = false;
            for (int32_t k = a->lo; a->type == AT_ENUM && k <= a->hi; k++) {
                label_ok |= !strcasecmp(a->labels[k], b[i].s);
            }
            if (!(text_arg && strlen(b[i].s) <= a->maxlen) && !label_ok) {
                return bind_fail("%s: \"%s\" binds \"%s\" to \"%s\" of %s, which does not take it", what, label, b[i].s, b[i].arg, op);
            }
            break;
        }
        case BIND_TEXT:
            if (!text_ask_ok) return bind_fail("%s: \"%s\" binds the typed text to \"%s\", but the entry asks for none (ask)", what, label, b[i].arg);
            if (a->type != AT_STRING) return bind_fail("%s: \"%s\" binds the typed text to \"%s\" of %s, which is not a string", what, label, b[i].arg, op);
            break;
        case BIND_ASK:
            if (!ask_ok) return bind_fail("%s: \"%s\" binds \"%s\" to the typed address, but the action asks for none (ask)", what, label, b[i].arg);
            if (!num_arg || a->lo > 0 || a->hi < 0xFFFF) {
                return bind_fail("%s: \"%s\" binds the typed address to \"%s\" of %s, which does not take 0 .. 65535", what, label, b[i].arg, op);
            }
            break;
        default:
            return bind_fail("%s: \"%s\" has a binding of an unknown kind", what, label);
        }
    }
    for (int i = 0; i < o->nargs; i++) {
        bool bound = false;
        for (int j = 0; j < nb; j++) {
            bound |= !strcmp(b[j].arg, o->args[i].name);
        }
        if (!bound) {
            return bind_fail("%s: \"%s\" leaves argument \"%s\" of %s unbound", what, label, o->args[i].name, op);
        }
    }
    return 0;
}

