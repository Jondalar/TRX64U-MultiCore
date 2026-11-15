// The diag core's test files: parser, compiler, evaluation, the two result files, the run (T175). See diagtest.h.
#include "diagtest.h"
#include "portdiag_regs.h"
#include "portdiag_tests.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the error
static int fail(DtErr *e, int line, const char *fmt, ...)
{
    if (e) {
        e->line = line;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
        va_end(ap);
    }
    return -1;
}

static void cpy(char *d, int n, const char *s)
{
    snprintf(d, n, "%s", s);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// one line of TOML: `key = value`, a value is a string, an integer (decimal or 0x..), true/false, or an array of those on one line
#define KV_ITEMS 16
enum { V_STR = 1, V_INT, V_BOOL, V_ARR_STR, V_ARR_INT };
struct Kv {
    char key[32];
    int type;
    char s[DT_STR];
    int64_t i;
    int n;                                  // array items
    char items[KV_ITEMS][DT_PATH];
    int64_t iitems[KV_ITEMS];
};

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

// a quoted string at p (p on the opening quote); the pointer after the closing quote, 0 on an error
static const char *parse_str(const char *p, char *out, int cap, DtErr *e, int line)
{
    p++;
    int n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char x = *p++;
            c = x == 'n' ? '\n' : x == 't' ? '\t' : x;
            if (!x) {
                fail(e, line, "a string ends inside an escape");
                return 0;
            }
        }
        if (n + 1 >= cap) {
            fail(e, line, "a string is longer than %d characters", cap - 1);
            return 0;
        }
        out[n++] = c;
    }
    if (*p != '"') {
        fail(e, line, "a string is not closed on this line");
        return 0;
    }
    out[n] = 0;
    return p + 1;
}

static const char *parse_int(const char *p, int64_t *out, DtErr *e, int line)
{
    char *end;
    bool neg = false;
    const char *q = p;
    if (*q == '-' || *q == '+') {
        neg = *q == '-';
        q++;
    }
    if (!isdigit((unsigned char)*q)) {
        fail(e, line, "a value is neither a string, a number, true/false nor an array");
        return 0;
    }
    long long v;
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
        v = (long long)strtoull(q + 2, &end, 16);
        if (end == q + 2) {
            fail(e, line, "bad hex number");
            return 0;
        }
    } else {
        v = (long long)strtoull(q, &end, 10);
    }
    *out = neg ? -v : v;
    return end;
}

// the rest of the line after the value must be empty or a comment
static bool tail_ok(const char *p, DtErr *e, int line)
{
    p = skip_ws(p);
    if (*p && *p != '#' && *p != '\r') {
        fail(e, line, "unexpected text after the value");
        return false;
    }
    return true;
}

static int parse_kv(const char *p, Kv *kv, DtErr *e, int line)
{
    memset(kv, 0, sizeof(*kv));
    int k = 0;
    while (isalnum((unsigned char)*p) || *p == '_') {
        if (k + 1 >= (int)sizeof(kv->key)) {
            return fail(e, line, "a key is too long");
        }
        kv->key[k++] = *p++;
    }
    kv->key[k] = 0;
    p = skip_ws(p);
    if (!k || *p != '=') {
        return fail(e, line, "expected key = value");
    }
    p = skip_ws(p + 1);
    if (*p == '"') {
        kv->type = V_STR;
        p = parse_str(p, kv->s, sizeof(kv->s), e, line);
        return p && tail_ok(p, e, line) ? 0 : -1;
    }
    if (!strncmp(p, "true", 4) || !strncmp(p, "false", 5)) {
        kv->type = V_BOOL;
        kv->i = p[0] == 't';
        p += kv->i ? 4 : 5;
        return tail_ok(p, e, line) ? 0 : -1;
    }
    if (*p == '[') {
        p = skip_ws(p + 1);
        kv->type = 0;
        while (*p && *p != ']') {
            if (kv->n >= KV_ITEMS) {
                return fail(e, line, "an array has more than %d items", KV_ITEMS);
            }
            if (*p == '"') {
                if (kv->type == V_ARR_INT) {
                    return fail(e, line, "an array mixes strings and numbers");
                }
                kv->type = V_ARR_STR;
                p = parse_str(p, kv->items[kv->n], sizeof(kv->items[0]), e, line);
            } else {
                if (kv->type == V_ARR_STR) {
                    return fail(e, line, "an array mixes strings and numbers");
                }
                kv->type = V_ARR_INT;
                p = parse_int(p, &kv->iitems[kv->n], e, line);
            }
            if (!p) {
                return -1;
            }
            kv->n++;
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
            } else if (*p != ']') {
                return fail(e, line, "an array must be closed with ] on the same line");
            }
        }
        if (*p != ']') {
            return fail(e, line, "an array must be closed with ] on the same line");
        }
        if (!kv->type) {
            kv->type = V_ARR_INT;            // an empty array is both
        }
        return tail_ok(p + 1, e, line) ? 0 : -1;
    }
    kv->type = V_INT;
    p = parse_int(p, &kv->i, e, line);
    return p && tail_ok(p, e, line) ? 0 : -1;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the pins
static const char *const pin_names[PD_LIVE_BITS] = {
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_fire", "p2_up", "p2_down", "p2_left", "p2_right", "p2_fire", "cmp0", "cmp1", "cmp2", "cmp3"
};
static int pin_index(const char *s)
{
    for (int i = 0; i < PD_LIVE_BITS; i++) {
        if (!strcmp(s, pin_names[i])) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the builder: one section after the other
enum { S_ROOT = 0, S_TEST, S_SUITE, S_PROMPT, S_STEP, S_EVAL, S_RESULT };

struct Builder {
    DiagFile *f;
    DtErr *e;
    int sec;
    int sec_line;
    // the entry being filled (a step or an eval is checked when it is complete)
    bool have_op;
    char op[16];
    bool got[24];                           // keys seen in this entry
    char into[DT_NAME], pinname[DT_NAME], promptref[DT_NAME], levelname[16];
    bool pins_given;
    int pins_mask;
    int cmp_mask;
    int dis, adj;
    DtStep st;
    DtEval ev;
    char ev_value[DT_NAME];
    char ev_field[16];
    bool kind_seen;
};

static int slot_find(const DiagTest &t, const char *name)
{
    for (int i = 0; i < t.nslots; i++) {
        if (!strcmp(t.slots[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int prompt_find(const DiagTest &t, const char *id)
{
    for (int i = 0; i < t.nprompts; i++) {
        if (!strcmp(t.prompts[i].id, id)) {
            return i;
        }
    }
    return -1;
}

static int slot_add(Builder &b, const char *name, uint8_t kind, int line)
{
    DiagTest &t = b.f->test;
    if (slot_find(t, name) >= 0) {
        return fail(b.e, line, "the result slot '%s' is used by two steps", name);
    }
    if (t.nslots >= DT_SLOTS) {
        return fail(b.e, line, "more than %d result slots", DT_SLOTS);
    }
    cpy(t.slots[t.nslots].name, DT_NAME, name);
    t.slots[t.nslots].kind = kind;
    return t.nslots++;
}

static const struct { const char *name; int op; } op_names[] = {
    { "end", DT_OP_END }, { "drive", DT_OP_DRIVE }, { "wait", DT_OP_WAIT }, { "sample", DT_OP_SAMPLE }, { "edges", DT_OP_EDGES },
    { "time_to_flip", DT_OP_TIME_TO_FLIP }, { "padctl", DT_OP_PADCTL }, { "loop", DT_OP_LOOP }, { "wait_user", DT_OP_WAIT_USER },
};

// the keys a step may carry, per op (besides `op`)
static bool step_key_ok(int op, const char *k)
{
    switch (op) {
    case DT_OP_DRIVE: return !strcmp(k, "pins") || !strcmp(k, "level");
    case DT_OP_WAIT: return !strcmp(k, "us") || !strcmp(k, "clocks");
    case DT_OP_SAMPLE: return !strcmp(k, "into");
    case DT_OP_EDGES: return !strcmp(k, "pin") || !strcmp(k, "us") || !strcmp(k, "clocks") || !strcmp(k, "into");
    case DT_OP_TIME_TO_FLIP: return !strcmp(k, "cmp") || !strcmp(k, "timeout_us") || !strcmp(k, "timeout_clocks") || !strcmp(k, "into");
    case DT_OP_PADCTL: return !strcmp(k, "dis") || !strcmp(k, "adj");
    case DT_OP_LOOP: return !strcmp(k, "count") || !strcmp(k, "body");
    case DT_OP_WAIT_USER: return !strcmp(k, "prompt");
    }
    return false;
}

static void entry_reset(Builder &b)
{
    b.have_op = false;
    b.op[0] = 0;
    memset(b.got, 0, sizeof(b.got));
    b.into[0] = b.pinname[0] = b.promptref[0] = 0;
    cpy(b.levelname, sizeof(b.levelname), "low");
    b.pins_given = false;
    b.pins_mask = 0;
    b.cmp_mask = 0;
    b.dis = b.adj = -1;
    memset(&b.st, 0, sizeof(b.st));
    b.st.prompt = -1;
    b.st.slot = -1;
    memset(&b.ev, 0, sizeof(b.ev));
    b.ev.slot = -1;
    b.ev_value[0] = 0;
    b.ev_field[0] = 0;
}

// a step is complete: check it and add it
static int step_done(Builder &b)
{
    DiagTest &t = b.f->test;
    int line = b.sec_line;
    if (!b.have_op) {
        return fail(b.e, line, "a [[step]] has no op");
    }
    DtStep s = b.st;
    s.line = line;
    int op = s.op;
    bool us = b.got[0] /*us*/, clocks = b.got[1] /*clocks*/;
    switch (op) {
    case DT_OP_DRIVE:
        if (!b.pins_given || !b.pins_mask) {
            return fail(b.e, line, "drive needs pins = [..] naming at least one of the ten port pins");
        }
        if (b.pins_mask & ~0x3FF) {
            return fail(b.e, line, "drive can only touch the ten port pins, not the comparator bits");
        }
        s.mask = (uint32_t)b.pins_mask;
        if (!strcmp(b.levelname, "low")) {
            s.level = 0;
        } else if (!strcmp(b.levelname, "release")) {
            s.level = 1;
        } else {
            return fail(b.e, line, "level must be \"low\" or \"release\"; the engine never drives a pin high");
        }
        break;
    case DT_OP_WAIT:
        if (us == clocks) {
            return fail(b.e, line, "wait needs exactly one of us / clocks");
        }
        s.in_us = us;
        break;
    case DT_OP_EDGES:
        if (us == clocks) {
            return fail(b.e, line, "edges needs exactly one of us / clocks");
        }
        s.in_us = us;
        if (!b.pinname[0] || pin_index(b.pinname) < 0) {
            return fail(b.e, line, "edges needs pin = one of the pin names (p1_up .. p2_fire, cmp0 .. cmp3)");
        }
        s.sel = (uint8_t)pin_index(b.pinname);
        break;
    case DT_OP_TIME_TO_FLIP:
        if (b.got[2] == b.got[3]) {
            return fail(b.e, line, "time_to_flip needs exactly one of timeout_us / timeout_clocks");
        }
        s.in_us = b.got[2];
        if (!b.cmp_mask) {
            return fail(b.e, line, "time_to_flip needs cmp = [..] naming comparator bits 0 .. 3");
        }
        s.sel = (uint8_t)b.cmp_mask;
        break;
    case DT_OP_PADCTL:
        if (b.dis < 0 && b.adj < 0) {
            return fail(b.e, line, "padctl needs dis and/or adj (0 or 1)");
        }
        s.pad = (uint16_t)((b.dis > 0 ? 1 : 0) | (b.dis >= 0 ? 2 : 0) | (b.adj > 0 ? 4 : 0) | (b.adj >= 0 ? 8 : 0));      // the engine's PADCTL: b0 DIS level, b1 DIS enable, b2 ADJ level, b3 ADJ enable
        break;
    case DT_OP_LOOP:
        if (!b.got[4] || !b.got[5] || s.count < 1 || s.count > 0xFFFF || s.body < 1 || s.body > 255) {
            return fail(b.e, line, "loop needs count = 1 .. 65535 and body = 1 .. 255 (the number of steps that follow)");
        }
        break;
    case DT_OP_WAIT_USER: {
        int pi = prompt_find(t, b.promptref);
        if (pi < 0) {
            return fail(b.e, line, "wait_user names the prompt '%s', which no [[prompt]] before it defines", b.promptref);
        }
        s.prompt = (int8_t)pi;
        break;
    }
    }
    if (op == DT_OP_SAMPLE || op == DT_OP_EDGES || op == DT_OP_TIME_TO_FLIP) {
        if (b.into[0]) {
            int sl = slot_add(b, b.into, op == DT_OP_SAMPLE ? DT_SLOT_SAMPLE : op == DT_OP_EDGES ? DT_SLOT_EDGES : DT_SLOT_TIME, line);
            if (sl < 0) {
                return -1;
            }
            s.slot = (int8_t)sl;
        }
    }
    if (t.nsteps >= DT_SRC_STEPS) {
        return fail(b.e, line, "more than %d steps (the step RAM holds 256 words)", DT_SRC_STEPS);
    }
    t.steps[t.nsteps++] = s;
    entry_reset(b);
    return 0;
}

static int eval_done(Builder &b)
{
    DiagTest &t = b.f->test;
    int line = b.sec_line;
    DtEval ev = b.ev;
    ev.line = line;
    if (!ev.verdict[0]) {
        return fail(b.e, line, "an [[eval]] needs a verdict text");
    }
    if (!ev.id[0]) {
        snprintf(ev.id, sizeof(ev.id), "e%d", t.nevals + 1);
    }
    for (int i = 0; i < t.nevals; i++) {
        if (!strcmp(t.evals[i].id, ev.id)) {
            return fail(b.e, line, "two evals have the id '%s'", ev.id);
        }
    }
    if (b.ev_value[0]) {
        cpy(ev.value, DT_NAME, b.ev_value);
        // "slot" or "slot.field"
        char nm[DT_NAME];
        cpy(nm, sizeof(nm), b.ev_value);
        char *dot = strchr(nm, '.');
        ev.field = DT_F_RAW;
        if (dot) {
            *dot = 0;
            const char *fld = dot + 1;
            ev.field = !strcmp(fld, "pins") ? DT_F_PINS : !strcmp(fld, "cmp") ? DT_F_CMP : !strcmp(fld, "ts") ? DT_F_TS : !strcmp(fld, "flags") ? DT_F_FLAGS : 255;
            if (ev.field == 255) {
                return fail(b.e, line, "'%s': a sample's fields are pins, cmp, ts and flags", fld);
            }
        }
        ev.slot = (int8_t)slot_find(t, nm);
        if (ev.slot < 0) {
            return fail(b.e, line, "the eval reads '%s', which no step stores into", nm);
        }
        if (ev.field != DT_F_RAW && t.slots[ev.slot].kind != DT_SLOT_SAMPLE) {
            return fail(b.e, line, "'%s' has fields only when a sample stored it", nm);
        }
        if (!(ev.has_eq || ev.has_min || ev.has_max || ev.has_min_us || ev.has_max_us || ev.differs[0])) {
            return fail(b.e, line, "an eval of a value needs eq, min/max, min_us/max_us or differs");
        }
        ev.differs_slot = -1;
        if (ev.differs[0]) {
            ev.differs_slot = (int8_t)slot_find(t, ev.differs);
            if (ev.differs_slot < 0 || t.slots[ev.differs_slot].kind != t.slots[ev.slot].kind) {
                return fail(b.e, line, "differs names '%s', which is no slot of the same kind", ev.differs);
            }
        }
        if ((ev.has_min_us || ev.has_max_us) && t.slots[ev.slot].kind != DT_SLOT_TIME) {
            return fail(b.e, line, "min_us / max_us are for a time_to_flip slot; '%s' is not one", nm);
        }
    } else {
        if (!ev.nall) {
            return fail(b.e, line, "an [[eval]] needs value = \"slot\" or all = [..]");
        }
        if (ev.has_eq || ev.has_min || ev.has_max || ev.has_min_us || ev.has_max_us || ev.has_mask || ev.differs[0]) {
            return fail(b.e, line, "an eval with all = [..] takes no thresholds");
        }
    }
    for (int a = 0; a < ev.nall; a++) {
        bool ok = false;
        for (int i = 0; i < t.nevals; i++) {
            ok |= !strcmp(t.evals[i].id, ev.all[a]);
        }
        if (!ok) {
            return fail(b.e, line, "all names the eval '%s', which is not defined before this one", ev.all[a]);
        }
    }
    if (t.nevals >= DT_EVALS) {
        return fail(b.e, line, "more than %d evals", DT_EVALS);
    }
    t.evals[t.nevals++] = ev;
    entry_reset(b);
    return 0;
}

static int section_end(Builder &b)
{
    if (b.sec == S_STEP) {
        return step_done(b);
    }
    if (b.sec == S_EVAL) {
        return eval_done(b);
    }
    return 0;
}

static int want_type(Builder &b, const Kv &kv, int type, int line)
{
    bool empty_arr = kv.n == 0 && (kv.type == V_ARR_STR || kv.type == V_ARR_INT) && (type == V_ARR_STR || type == V_ARR_INT);
    if (kv.type != type && !empty_arr) {
        const char *w = type == V_STR ? "a string" : type == V_INT ? "a number" : type == V_BOOL ? "true or false" : type == V_ARR_STR ? "an array of strings" : "an array of numbers";
        return fail(b.e, line, "'%s' must be %s", kv.key, w);
    }
    return 0;
}

static int list_add(Builder &b, char dst[][DT_NAME], int *n, int cap, const Kv &kv, int line)
{
    if (want_type(b, kv, V_ARR_STR, line)) {
        return -1;
    }
    for (int i = 0; i < kv.n; i++) {
        if (*n >= cap || (int)strlen(kv.items[i]) >= DT_NAME) {
            return fail(b.e, line, "'%s' has too many or too long items", kv.key);
        }
        cpy(dst[(*n)++], DT_NAME, kv.items[i]);
    }
    return 0;
}

static int apply_kv(Builder &b, const Kv &kv, int line)
{
    DiagFile &f = *b.f;
    const char *k = kv.key;
    switch (b.sec) {
    case S_ROOT:
        if (!strcmp(k, "kind")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (!strcmp(kv.s, "test")) {
                f.kind = DT_KIND_TEST;
            } else if (!strcmp(kv.s, "suite")) {
                f.kind = DT_KIND_SUITE;
            } else {
                return fail(b.e, line, "kind must be \"test\" or \"suite\"");
            }
            b.kind_seen = true;
            return 0;
        }
        return fail(b.e, line, "unknown key '%s' (before the first section only kind is allowed)", k);
    case S_TEST:
        if (!strcmp(k, "name") || !strcmp(k, "id")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (!strcmp(k, "name")) {
                cpy(f.test.name, sizeof(f.test.name), kv.s);
            } else {
                if (strlen(kv.s) >= DT_NAME || !kv.s[0]) {
                    return fail(b.e, line, "id must be 1 .. %d characters", DT_NAME - 1);
                }
                for (const char *c = kv.s; *c; c++) {
                    if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-') {
                        return fail(b.e, line, "id may hold letters, digits, _ and - only (it names the result files)");
                    }
                }
                cpy(f.test.id, sizeof(f.test.id), kv.s);
            }
            return 0;
        }
        if (!strcmp(k, "version")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            f.test.version = (int)kv.i;
            return 0;
        }
        if (!strcmp(k, "menu_input")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (!strcmp(kv.s, "keyboard")) {
                f.test.kbd_only = true;
            } else if (!strcmp(kv.s, "any")) {
                f.test.kbd_only = false;
            } else {
                return fail(b.e, line, "menu_input is \"any\" or \"keyboard\"");
            }
            return 0;
        }
        if (!strcmp(k, "needs")) {
            if (want_type(b, kv, V_ARR_STR, line)) {
                return -1;
            }
            for (int i = 0; i < kv.n; i++) {
                if (f.test.nneeds >= DT_LIST || strlen(kv.items[i]) >= sizeof(f.test.needs[0])) {
                    return fail(b.e, line, "needs has too many or too long items");
                }
                cpy(f.test.needs[f.test.nneeds++], sizeof(f.test.needs[0]), kv.items[i]);
            }
            return 0;
        }
        if (!strcmp(k, "requires")) {
            if (want_type(b, kv, V_ARR_STR, line)) {
                return -1;
            }
            for (int i = 0; i < kv.n; i++) {
                if (f.test.nrequires >= DT_LIST || strlen(kv.items[i]) >= DT_NAME * 2 || !strchr(kv.items[i], '.')) {
                    return fail(b.e, line, "requires items look like \"pins.drivable\" (test.eval id)");
                }
                cpy(f.test.requires[f.test.nrequires++], DT_NAME * 2, kv.items[i]);
            }
            return 0;
        }
        return fail(b.e, line, "unknown key '%s' in [test]", k);
    case S_SUITE:
        if (!strcmp(k, "name") || !strcmp(k, "id")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            cpy(!strcmp(k, "name") ? f.suite.name : f.suite.id, !strcmp(k, "name") ? sizeof(f.suite.name) : sizeof(f.suite.id), kv.s);
            return 0;
        }
        if (!strcmp(k, "tests")) {
            if (want_type(b, kv, V_ARR_STR, line)) {
                return -1;
            }
            for (int i = 0; i < kv.n; i++) {
                if (f.suite.ntests >= DT_LIST || strchr(kv.items[i], '/') || !kv.items[i][0]) {
                    return fail(b.e, line, "tests lists file names of the same folder (at most %d, no path)", DT_LIST);
                }
                cpy(f.suite.tests[f.suite.ntests++], DT_PATH, kv.items[i]);
            }
            return 0;
        }
        if (!strcmp(k, "stop_on_fail")) {
            if (want_type(b, kv, V_BOOL, line)) {
                return -1;
            }
            f.suite.stop_on_fail = kv.i != 0;
            return 0;
        }
        return fail(b.e, line, "unknown key '%s' in [suite]", k);
    case S_PROMPT: {
        DtPrompt &p = f.test.prompts[f.test.nprompts - 1];
        if (!strcmp(k, "id") || !strcmp(k, "text")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (!strcmp(k, "id")) {
                if (strlen(kv.s) >= DT_NAME || !kv.s[0]) {
                    return fail(b.e, line, "prompt id must be 1 .. %d characters", DT_NAME - 1);
                }
                for (int i = 0; i < f.test.nprompts - 1; i++) {
                    if (!strcmp(f.test.prompts[i].id, kv.s)) {
                        return fail(b.e, line, "two prompts have the id '%s'", kv.s);
                    }
                }
                cpy(p.id, sizeof(p.id), kv.s);
            } else {
                cpy(p.text, sizeof(p.text), kv.s);
            }
            return 0;
        }
        if (!strcmp(k, "timeout_s")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            if (kv.i < 0 || kv.i > 3600) {
                return fail(b.e, line, "timeout_s must be 0 .. 3600");
            }
            p.timeout_s = (int)kv.i;
            return 0;
        }
        return fail(b.e, line, "unknown key '%s' in [[prompt]]", k);
    }
    case S_STEP: {
        if (!strcmp(k, "op")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            for (size_t i = 0; i < sizeof(op_names) / sizeof(op_names[0]); i++) {
                if (!strcmp(kv.s, op_names[i].name)) {
                    b.st.op = (uint8_t)op_names[i].op;
                    b.have_op = true;
                    cpy(b.op, sizeof(b.op), kv.s);
                    return 0;
                }
            }
            return fail(b.e, line, "the engine has no step '%s' (end, drive, wait, sample, edges, time_to_flip, padctl, loop, wait_user)", kv.s);
        }
        if (!b.have_op) {
            return fail(b.e, line, "op must be the first key of a [[step]]");
        }
        if (!step_key_ok(b.st.op, k)) {
            return fail(b.e, line, "the key '%s' does not belong to the step '%s'", k, b.op);
        }
        if (!strcmp(k, "pins")) {
            if (want_type(b, kv, V_ARR_STR, line)) {
                return -1;
            }
            b.pins_given = true;
            for (int i = 0; i < kv.n; i++) {
                int ix = pin_index(kv.items[i]);
                if (ix < 0) {
                    return fail(b.e, line, "'%s' is not a pin name (p1_up p1_down p1_left p1_right p1_fire, p2_..)", kv.items[i]);
                }
                b.pins_mask |= 1 << ix;
            }
            return 0;
        }
        if (!strcmp(k, "level")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            cpy(b.levelname, sizeof(b.levelname), kv.s);
            return 0;
        }
        if (!strcmp(k, "us") || !strcmp(k, "clocks") || !strcmp(k, "timeout_us") || !strcmp(k, "timeout_clocks")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            if (kv.i < 1 || kv.i > (int64_t)PD_ARG_MAX) {
                return fail(b.e, line, "'%s' must be 1 .. %u", k, (unsigned)PD_ARG_MAX);
            }
            b.st.n = (uint32_t)kv.i;
            b.got[!strcmp(k, "us") ? 0 : !strcmp(k, "clocks") ? 1 : !strcmp(k, "timeout_us") ? 2 : 3] = true;
            return 0;
        }
        if (!strcmp(k, "into") || !strcmp(k, "pin") || !strcmp(k, "prompt")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (strlen(kv.s) >= DT_NAME) {
                return fail(b.e, line, "'%s' is too long", k);
            }
            cpy(!strcmp(k, "into") ? b.into : !strcmp(k, "pin") ? b.pinname : b.promptref, DT_NAME, kv.s);
            return 0;
        }
        if (!strcmp(k, "cmp")) {
            if (want_type(b, kv, V_ARR_INT, line)) {
                return -1;
            }
            for (int i = 0; i < kv.n; i++) {
                if (kv.iitems[i] < 0 || kv.iitems[i] > 3) {
                    return fail(b.e, line, "cmp bits are 0 .. 3");
                }
                b.cmp_mask |= 1 << kv.iitems[i];
            }
            return 0;
        }
        if (!strcmp(k, "dis") || !strcmp(k, "adj")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            if (kv.i != 0 && kv.i != 1) {
                return fail(b.e, line, "'%s' is 0 or 1", k);
            }
            (!strcmp(k, "dis") ? b.dis : b.adj) = (int)kv.i;
            return 0;
        }
        if (!strcmp(k, "count") || !strcmp(k, "body")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            if (kv.i < 0 || kv.i > 0xFFFF) {
                return fail(b.e, line, "'%s' is out of range", k);
            }
            if (!strcmp(k, "count")) {
                b.st.count = (uint32_t)kv.i;
                b.got[4] = true;
            } else {
                b.st.body = (uint16_t)kv.i;
                b.got[5] = true;
            }
            return 0;
        }
        return fail(b.e, line, "internal: key '%s'", k);
    }
    case S_EVAL: {
        DtEval &ev = b.ev;
        if (!strcmp(k, "id") || !strcmp(k, "value") || !strcmp(k, "verdict") || !strcmp(k, "fail") || !strcmp(k, "differs")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if ((!strcmp(k, "id") || !strcmp(k, "value")) && strlen(kv.s) >= DT_NAME) {
                return fail(b.e, line, "'%s' is too long", k);
            }
            if (!strcmp(k, "id")) {
                cpy(ev.id, sizeof(ev.id), kv.s);
            } else if (!strcmp(k, "differs")) {
                if (strlen(kv.s) >= DT_NAME) {
                    return fail(b.e, line, "'differs' is too long");
                }
                cpy(ev.differs, sizeof(ev.differs), kv.s);
            } else if (!strcmp(k, "value")) {
                cpy(b.ev_value, sizeof(b.ev_value), kv.s);
            } else if (!strcmp(k, "verdict")) {
                cpy(ev.verdict, sizeof(ev.verdict), kv.s);
            } else {
                cpy(ev.fail, sizeof(ev.fail), kv.s);
            }
            return 0;
        }
        if (!strcmp(k, "all")) {
            return list_add(b, ev.all, &ev.nall, DT_ALL, kv, line);
        }
        if (!strcmp(k, "each")) {
            if (want_type(b, kv, V_BOOL, line)) {
                return -1;
            }
            ev.each = kv.i != 0;
            return 0;
        }
        if (!strcmp(k, "index") || !strcmp(k, "mask") || !strcmp(k, "eq") || !strcmp(k, "min") || !strcmp(k, "max") || !strcmp(k, "min_us") || !strcmp(k, "max_us")) {
            if (want_type(b, kv, V_INT, line)) {
                return -1;
            }
            if (!strcmp(k, "index")) {
                if (kv.i < 0 || kv.i > 511) {
                    return fail(b.e, line, "index is 0 .. 511");
                }
                ev.index = (uint16_t)kv.i;
            } else if (!strcmp(k, "mask")) {
                ev.has_mask = true;
                ev.mask = (uint32_t)kv.i;
            } else if (!strcmp(k, "eq")) {
                ev.has_eq = true;
                ev.eq = kv.i;
            } else if (!strcmp(k, "min")) {
                ev.has_min = true;
                ev.min = kv.i;
            } else if (!strcmp(k, "max")) {
                ev.has_max = true;
                ev.max = kv.i;
            } else if (!strcmp(k, "min_us")) {
                ev.has_min_us = true;
                ev.min_us = kv.i;
            } else {
                ev.has_max_us = true;
                ev.max_us = kv.i;
            }
            return 0;
        }
        return fail(b.e, line, "unknown key '%s' in [[eval]]", k);
    }
    case S_RESULT:
        if (!strcmp(k, "format")) {
            if (want_type(b, kv, V_STR, line)) {
                return -1;
            }
            if (strcmp(kv.s, "toml")) {
                return fail(b.e, line, "format must be \"toml\" (the readable report is written next to it anyway)");
            }
            return 0;
        }
        if (!strcmp(k, "keys")) {
            return list_add(b, f.test.keys, &f.test.nkeys, DT_SLOTS, kv, line);
        }
        return fail(b.e, line, "unknown key '%s' in [result]", k);
    }
    return fail(b.e, line, "internal: section");
}

// the whole file checked once it is read
static int finish(Builder &b)
{
    DiagFile &f = *b.f;
    DiagTest &t = f.test;
    if (f.kind == DT_KIND_SUITE) {
        if (!f.suite.id[0] || !f.suite.ntests) {
            return fail(b.e, 0, "a suite needs [suite] with id and tests = [..]");
        }
        if (t.nsteps || t.nprompts || t.nevals) {
            return fail(b.e, 0, "a suite lists tests; it has no steps, prompts or evals of its own");
        }
        return 0;
    }
    if (!t.id[0]) {
        return fail(b.e, 0, "[test] needs an id");
    }
    if (!t.name[0]) {
        cpy(t.name, sizeof(t.name), t.id);
    }
    if (!t.nsteps) {
        return fail(b.e, 0, "a test has no [[step]]");
    }
    for (int i = 0; i < t.nsteps; i++) {
        const DtStep &s = t.steps[i];
        if (s.op == DT_OP_LOOP) {
            if (i + s.body >= t.nsteps) {
                return fail(b.e, s.line, "the loop wants %d steps after it, the file has fewer", s.body);
            }
            for (int j = i + 1; j <= i + s.body; j++) {
                if (t.steps[j].op == DT_OP_LOOP || t.steps[j].op == DT_OP_WAIT_USER || t.steps[j].op == DT_OP_END) {
                    return fail(b.e, t.steps[j].line, "a loop body holds no loop, wait_user or end");
                }
            }
        }
        if (s.op == DT_OP_END) {
            return fail(b.e, s.line, "a test ends by itself; remove the end step");
        }
    }
    for (int i = 0; i < t.nkeys; i++) {
        if (slot_find(t, t.keys[i]) < 0) {
            return fail(b.e, 0, "[result] keys names '%s', which no step stores into", t.keys[i]);
        }
    }
    return 0;
}

int dt_parse(const char *text, uint32_t len, DiagFile *out, DtErr *err)
{
    if (err) {
        err->line = 0;
        err->msg[0] = 0;
    }
    memset(out, 0, sizeof(*out));
    static Builder b;                       // large; the app parses one file at a time
    memset(&b, 0, sizeof(b));
    b.f = out;
    b.e = err;
    b.sec = S_ROOT;
    entry_reset(b);
    uint32_t pos = 0;
    int line = 0;
    char buf[400];
    while (pos < len) {
        uint32_t s = pos;
        while (pos < len && text[pos] != '\n') {
            pos++;
        }
        uint32_t n = pos - s;
        if (pos < len) {
            pos++;
        }
        line++;
        if (n >= sizeof(buf)) {
            return fail(err, line, "a line is longer than %d characters", (int)sizeof(buf) - 1);
        }
        memcpy(buf, text + s, n);
        buf[n] = 0;
        const char *p = skip_ws(buf);
        if (!*p || *p == '#' || *p == '\r') {
            continue;
        }
        if (*p == '[') {
            if (section_end(b)) {
                return -1;
            }
            bool arr = p[1] == '[';
            const char *q = p + (arr ? 2 : 1);
            char nm[24];
            int k = 0;
            while (*q && *q != ']' && k < 23) {
                nm[k++] = *q++;
            }
            nm[k] = 0;
            if (*q != ']' || (arr && q[1] != ']') || !tail_ok(q + (arr ? 2 : 1), err, line)) {
                return err && err->msg[0] ? -1 : fail(err, line, "a section header is not closed");
            }
            b.sec_line = line;
            DiagTest &t = out->test;
            if (!arr && !strcmp(nm, "test")) {
                b.sec = S_TEST;
            } else if (!arr && !strcmp(nm, "suite")) {
                b.sec = S_SUITE;
            } else if (!arr && !strcmp(nm, "result")) {
                b.sec = S_RESULT;
            } else if (arr && !strcmp(nm, "prompt")) {
                if (t.nprompts >= DT_PROMPTS) {
                    return fail(err, line, "more than %d prompts", DT_PROMPTS);
                }
                memset(&t.prompts[t.nprompts], 0, sizeof(DtPrompt));
                t.nprompts++;
                b.sec = S_PROMPT;
            } else if (arr && !strcmp(nm, "step")) {
                b.sec = S_STEP;
                entry_reset(b);
            } else if (arr && !strcmp(nm, "eval")) {
                b.sec = S_EVAL;
                entry_reset(b);
            } else {
                return fail(err, line, "unknown section %s%s%s", arr ? "[[" : "[", nm, arr ? "]]" : "]");
            }
            continue;
        }
        Kv kv;
        if (parse_kv(p, &kv, err, line)) {
            return -1;
        }
        if (apply_kv(b, kv, line)) {
            return -1;
        }
    }
    if (section_end(b)) {
        return -1;
    }
    return finish(b);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the compiler
static int to_ticks(uint32_t n, bool us, uint32_t tpus256, uint32_t *out, int line, DtErr *e)
{
    uint64_t v = us ? ((uint64_t)n * tpus256 + 255) / 256 : n;       // a time in microseconds rounds up: a wait is never shorter than asked
    if (v < 1 || v > PD_ARG_MAX) {
        return fail(e, line, "%u %s is %llu clocks at this clock; the engine counts 1 .. %u", (unsigned)n, us ? "us" : "clocks", (unsigned long long)v, (unsigned)PD_ARG_MAX);
    }
    *out = (uint32_t)v;
    return 0;
}

int dt_compile(const DiagTest &t, uint32_t tpus256, DtProgram *p, DtErr *err)
{
    memset(p, 0, sizeof(*p));
    if (!tpus256) {
        return fail(err, 0, "the engine reports no clock (ticks per microsecond is 0)");
    }
    int n = 0;
    for (int i = 0; i < t.nsteps; i++) {
        const DtStep &s = t.steps[i];
        uint32_t w = 0, v = 0;
        if (n + 2 > 255) {
            return fail(err, s.line, "the step list is longer than the step RAM");
        }
        p->word_of[i] = (uint16_t)n;
        switch (s.op) {
        case DT_OP_DRIVE: w = PD_STEP_WORD(PD_OP_DRIVE, 0, s.mask | (s.level ? s.mask << 10 : 0)); break;
        case DT_OP_WAIT:
            if (to_ticks(s.n, s.in_us, tpus256, &v, s.line, err)) {
                return -1;
            }
            w = PD_STEP_WORD(PD_OP_WAIT, 0, v);
            break;
        case DT_OP_SAMPLE: w = PD_STEP_WORD(PD_OP_SAMPLE, 0, 0); break;
        case DT_OP_EDGES:
            if (to_ticks(s.n, s.in_us, tpus256, &v, s.line, err)) {
                return -1;
            }
            p->words[n++] = PD_STEP_WORD(PD_OP_MASK, 0, 1u << s.sel);
            w = PD_STEP_WORD(PD_OP_EDGES, 0, v);
            break;
        case DT_OP_TIME_TO_FLIP:
            if (to_ticks(s.n, s.in_us, tpus256, &v, s.line, err)) {
                return -1;
            }
            w = PD_STEP_WORD(PD_OP_TIME_TO_FLIP, s.sel, v);
            if (s.slot >= 0) {
                p->timeout_ticks[s.slot] = v;
            }
            break;
        case DT_OP_PADCTL: w = PD_STEP_WORD(PD_OP_PADCTL, 0, s.pad); break;
        case DT_OP_LOOP: {
            int words = 0;
            for (int j = 1; j <= s.body; j++) {
                words += t.steps[i + j].op == DT_OP_EDGES ? 2 : 1;
            }
            if (words > 255) {
                return fail(err, s.line, "the loop body is longer than 255 words");
            }
            w = PD_STEP_WORD(PD_OP_LOOP, 0, (s.count << 8) | (uint32_t)words);
            break;
        }
        case DT_OP_WAIT_USER: w = PD_STEP_WORD(PD_OP_WAIT_USER, 0, s.prompt); break;
        }
        p->words[n++] = w;
    }
    p->words[n++] = PD_STEP_WORD(PD_OP_END, 0, 0);
    p->n = n;
    return 0;
}

int dt_plan(const DiagTest &t, int8_t slot_of[512])
{
    int n = 0;
    for (int i = 0; i < t.nsteps; i++) {
        const DtStep &s = t.steps[i];
        int reps = 1, body = 1;
        if (s.op == DT_OP_LOOP) {
            reps = (int)s.count;
            body = s.body;
            i++;
        }
        for (int r = 0; r < reps; r++) {
            for (int j = 0; j < body; j++) {
                const DtStep &x = t.steps[i + j];
                if (x.op == DT_OP_SAMPLE || x.op == DT_OP_EDGES || x.op == DT_OP_TIME_TO_FLIP) {
                    if (n >= 512) {
                        return -1;
                    }
                    slot_of[n++] = x.slot;
                }
            }
        }
        if (s.op == DT_OP_LOOP) {
            i += body - 1;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// evaluation
bool dt_value(const DiagTest &t, const DtResult &r, const DtEval &e, int index, int64_t *out)
{
    (void)t;
    int seen = 0;
    for (int i = 0; i < r.nfifo; i++) {
        if (r.slot_of[i] != e.slot) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        uint32_t w = r.fifo[i];
        switch (e.field) {
        case DT_F_PINS: *out = (w >> 6) & 0x3FF; break;
        case DT_F_CMP: *out = (w >> 2) & 0xF; break;
        case DT_F_TS: *out = w >> 16; break;
        case DT_F_FLAGS: *out = w & 3; break;
        default: *out = t.slots[e.slot].kind == DT_SLOT_TIME ? (w & 0xFFFFFF) : w; break;
        }
        return true;
    }
    return false;
}

static int slot_values(const DtResult &r, int slot)
{
    int n = 0;
    for (int i = 0; i < r.nfifo; i++) {
        n += r.slot_of[i] == slot;
    }
    return n;
}

static int fifo_index(const DtResult &r, int slot, int nth)
{
    for (int i = 0; i < r.nfifo; i++) {
        if (r.slot_of[i] == slot && nth-- == 0) {
            return i;
        }
    }
    return 0;
}

static int eval_one(const DiagTest &t, const DtResult &r, const DtEval &e)
{
    if (!e.value[0]) {
        for (int a = 0; a < e.nall; a++) {
            for (int i = 0; i < t.nevals; i++) {
                if (!strcmp(t.evals[i].id, e.all[a]) && r.verdict[i] != DT_PASS) {
                    return DT_FAIL;
                }
            }
        }
        return DT_PASS;
    }
    int nv = slot_values(r, e.slot);
    int from = e.each ? 0 : e.index, to = e.each ? nv : e.index + 1;
    if (nv == 0 || to > nv) {
        return DT_NODATA;
    }
    int res = DT_PASS;
    for (int ix = from; ix < to; ix++) {
        int64_t v;
        if (!dt_value(t, r, e, ix, &v)) {
            return DT_NODATA;
        }
        if (t.slots[e.slot].kind == DT_SLOT_TIME && (r.fifo[fifo_index(r, e.slot, ix)] >> 31)) {
            if (res == DT_PASS) {
                res = DT_NOFLIP;
            }
            continue;
        }
        int64_t m = e.has_mask ? (v & (int64_t)e.mask) : v;
        bool ok = true;
        if (e.has_eq) {
            ok &= m == e.eq;
        }
        if (e.has_min) {
            ok &= m >= e.min;
        }
        if (e.has_max) {
            ok &= m <= e.max;
        }
        if (e.has_min_us || e.has_max_us) {
            int64_t us = r.tpus256 ? (int64_t)((uint64_t)v * 256 / r.tpus256) : 0;
            ok &= !e.has_min_us || us >= e.min_us;
            ok &= !e.has_max_us || us <= e.max_us;
        }
        if (e.differs[0]) {
            DtEval o = e;
            o.slot = e.differs_slot;
            int64_t w;
            if (!dt_value(t, r, o, 0, &w)) {
                return DT_NODATA;
            }
            int64_t wm = e.has_mask ? (w & (int64_t)e.mask) : w;
            ok &= wm != m;
        }
        if (!ok) {
            res = DT_FAIL;
        }
    }
    return res;
}

void dt_evaluate(const DiagTest &t, DtResult *r)
{
    r->npass = r->nfail = 0;
    for (int i = 0; i < t.nevals; i++) {
        r->verdict[i] = (uint8_t)eval_one(t, *r, t.evals[i]);
        if (r->verdict[i] == DT_PASS) {
            r->npass++;
        } else {
            r->nfail++;
        }
    }
}

void dt_pins_text(uint32_t live, char *out, int n)
{
    static const char dirs[5] = { 'U', 'D', 'L', 'R', 'F' };
    int o = 0;
    for (int port = 0; port < 2; port++) {
        o += snprintf(out + o, n - o, "%sP%d", port ? "  " : "", port + 1);
        for (int b = 0; b < 5; b++) {
            o += snprintf(out + o, n - o, " %c%d", dirs[b], (int)((live >> (port * 5 + b)) & 1));
        }
    }
    o += snprintf(out + o, n - o, "  CMP");
    for (int b = 0; b < 4; b++) {
        o += snprintf(out + o, n - o, " %d", (int)((live >> (PD_CMP_SHIFT + b)) & 1));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the two files
struct Out {
    char *p;
    int cap, n;
    bool over;
    bool (*flush)(void *ud, const char *p, int n);       // T201 v2: a stream - the buffer goes to the card when it is full, `over` only when that fails (null: the buffer is the file)
    void *ud;
    void put(const char *fmt, ...)
    {
        if (over) {
            return;
        }
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        int k = vsnprintf(p + n, cap - n, fmt, ap);
        va_end(ap);
        if (k >= 0 && k >= cap - n && flush && n > 0) {
            if (!flush(ud, p, n)) {
                over = true;
                va_end(ap2);
                return;
            }
            n = 0;
            k = vsnprintf(p, cap, fmt, ap2);
        }
        va_end(ap2);
        if (k < 0 || k >= cap - n) {
            over = true;
            return;
        }
        n += k;
    }
    bool drain()                                        // the rest of a stream to the card
    {
        if (over || !flush) {
            return !over;
        }
        if (n > 0 && !flush(ud, p, n)) {
            over = true;
            return false;
        }
        n = 0;
        return true;
    }
    void str(const char *s)                 // a TOML basic string
    {
        put("\"");
        for (; *s; s++) {
            if (*s == '"' || *s == '\\') {
                put("\\%c", *s);
            } else if (*s == '\n') {
                put("\\n");
            } else if ((unsigned char)*s < 0x20) {
                put(" ");
            } else {
                put("%c", *s);
            }
        }
        put("\"");
    }
};

static const char *verdict_word(int v)
{
    return v == DT_PASS ? "PASS" : v == DT_FAIL ? "FAIL" : v == DT_NOFLIP ? "NOFLIP" : "NODATA";
}
static const char *prompt_word(int s)
{
    return s == DT_PR_OK ? "OK" : s == DT_PR_MISSED ? "MISSED" : s == DT_PR_SKIPPED ? "SKIPPED" : "NOT SHOWN";
}
static const char *kind_word(int k)
{
    return k == DT_SLOT_SAMPLE ? "sample" : k == DT_SLOT_EDGES ? "edges" : "time";
}

static bool slot_listed(const DiagTest &t, int s)
{
    if (!t.nkeys) {
        return true;
    }
    for (int i = 0; i < t.nkeys; i++) {
        if (!strcmp(t.keys[i], t.slots[s].name)) {
            return true;
        }
    }
    return false;
}

static void verdict_text(const DtEval &e, int v, char *out, int n)
{
    if (v == DT_PASS) {
        snprintf(out, n, "%s", e.verdict);
    } else if (v == DT_NOFLIP) {
        snprintf(out, n, "no flip within the timeout");
    } else if (v == DT_NODATA) {
        snprintf(out, n, "no value was measured");
    } else if (e.fail[0]) {
        snprintf(out, n, "%s", e.fail);
    } else {
        snprintf(out, n, "%s - not met", e.verdict);
    }
}

static bool has_eval(const DtResult &r) { return !strcmp(r.status, "done"); }

int dt_write_toml(const DiagTest &t, const DtResult &r, const char *core_version, const char *stamp, char *out, int cap)
{
    Out o = { out, cap, 0, false };
    o.put("# TRX64U Port Diagnostics result (T175). Raw words are the engine's FIFO words in order; see the spec for their layout.\n");
    o.put("[meta]\ntest = ");
    o.str(t.id);
    o.put("\nname = ");
    o.str(t.name);
    o.put("\nversion = %d\ncore = ", t.version);
    o.str(core_version ? core_version : "");
    o.put("\ndate = ");
    o.str(stamp);
    o.put("\nticks_per_us_x256 = %u\nstatus = ", (unsigned)r.tpus256);
    o.str(r.status);
    if (r.reason[0]) {
        o.put("\nreason = ");
        o.str(r.reason);
    }
    o.put("\n");
    if (t.nprompts) {
        o.put("\n[prompt]\n");
        for (int i = 0; i < t.nprompts; i++) {
            o.put("%s = ", t.prompts[i].id);
            o.str(prompt_word(r.prompt_state[i]));
            o.put("\n");
        }
    }
    for (int s = 0; s < t.nslots; s++) {
        if (!slot_listed(t, s)) {
            continue;
        }
        o.put("\n[slot.%s]\nkind = \"%s\"\nvalues = [", t.slots[s].name, kind_word(t.slots[s].kind));
        bool first = true;
        for (int i = 0; i < r.nfifo; i++) {
            if (r.slot_of[i] == s) {
                o.put("%s%u", first ? "" : ", ", (unsigned)r.fifo[i]);
                first = false;
            }
        }
        o.put("]\n");
        if (t.slots[s].kind == DT_SLOT_SAMPLE) {
            static const struct { const char *name; int shift; uint32_t mask; } f[] = { { "ts", 16, 0xFFFF }, { "pins", 6, 0x3FF }, { "cmp", 2, 0xF } };
            for (int k = 0; k < 3; k++) {
                o.put("%s = [", f[k].name);
                first = true;
                for (int i = 0; i < r.nfifo; i++) {
                    if (r.slot_of[i] == s) {
                        o.put("%s%u", first ? "" : ", ", (unsigned)((r.fifo[i] >> f[k].shift) & f[k].mask));
                        first = false;
                    }
                }
                o.put("]\n");
            }
        }
    }
    o.put("\n[raw]\nfifo = [");
    for (int i = 0; i < r.nfifo; i++) {
        o.put("%s%u", i ? ", " : "", (unsigned)r.fifo[i]);
    }
    o.put("]\n");
    if (has_eval(r) && t.nevals) {
        o.put("\n[eval]\n");
        for (int i = 0; i < t.nevals; i++) {
            char line[2 * DT_VERD + 24], vt[DT_VERD + 24];
            const DtEval &e = t.evals[i];
            int v = r.verdict[i];
            verdict_text(e, v, vt, sizeof(vt));
            snprintf(line, sizeof(line), "%s: %s", verdict_word(v), vt);
            o.put("%s = ", e.id);
            o.str(line);
            o.put("\n");
        }
    }
    return o.over ? -1 : o.n;
}

static void stamp_text(const char *stamp, char *out, int n)
{
    // YYYYMMDD-HHMMSS -> 2026-11-02 14:30:15; anything else (the counter) as it is
    if (strlen(stamp) == 15 && stamp[8] == '-') {
        snprintf(out, n, "%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c", stamp[0], stamp[1], stamp[2], stamp[3], stamp[4], stamp[5], stamp[6], stamp[7], stamp[9], stamp[10], stamp[11],
                 stamp[12], stamp[13], stamp[14]);
    } else {
        snprintf(out, n, "%s (no clock)", stamp);
    }
}

int dt_write_text(const DiagTest &t, const DtResult &r, const char *core_version, const char *stamp, char *out, int cap)
{
    Out o = { out, cap, 0, false };
    char when[40];
    stamp_text(stamp, when, sizeof(when));
    o.put("TRX64U Port Diagnostics\n=======================\n");
    o.put("Test:    %s (%s, version %d)\nCore:    %s\nDate:    %s\n", t.name, t.id, t.version, core_version ? core_version : "", when);
    o.put("Clock:   %u.%02u ticks per microsecond\n", (unsigned)(r.tpus256 / 256), (unsigned)((r.tpus256 % 256) * 100 / 256));
    if (t.nneeds) {
        o.put("Needs:   ");
        for (int i = 0; i < t.nneeds; i++) {
            o.put("%s%s", i ? ", " : "", t.needs[i]);
        }
        o.put("\n");
    }
    o.put("Status:  %s\n", r.status);
    if (r.reason[0]) {
        o.put("Reason:  %s\n", r.reason);
    }
    if (t.nprompts) {
        o.put("\nPrompts\n");
        for (int i = 0; i < t.nprompts; i++) {
            o.put("  %-8s %-9s %s\n", t.prompts[i].id, prompt_word(r.prompt_state[i]), t.prompts[i].text);
        }
    }
    if (r.nfifo) {
        o.put("\nMeasurements\n");
        for (int s = 0; s < t.nslots; s++) {
            if (!slot_listed(t, s)) {
                continue;
            }
            int k = 0;
            for (int i = 0; i < r.nfifo; i++) {
                if (r.slot_of[i] != s) {
                    continue;
                }
                if (k >= 64) {                         // the readable report shows the first 64 of a long loop; the .toml has them all
                    k++;
                    continue;
                }
                uint32_t w = r.fifo[i];
                char lbl[40];
                snprintf(lbl, sizeof(lbl), "%s[%d]", t.slots[s].name, k++);
                if (t.slots[s].kind == DT_SLOT_SAMPLE) {
                    char pt[96];
                    dt_pins_text(((w >> 6) & 0x3FF) | (((w >> 2) & 0xF) << PD_CMP_SHIFT), pt, sizeof(pt));
                    o.put("  %-14s sample  t=%u  %s\n", lbl, (unsigned)(w >> 16), pt);
                } else if (t.slots[s].kind == DT_SLOT_EDGES) {
                    o.put("  %-14s edges   %u\n", lbl, (unsigned)w);
                } else {
                    bool nf = (w >> 31) != 0;
                    if (nf) {
                        o.put("  %-14s time    no flip within %u ticks\n", lbl, (unsigned)(w & 0xFFFFFF));
                    } else {
                        uint64_t x100 = r.tpus256 ? (uint64_t)(w & 0xFFFFFF) * 25600 / r.tpus256 : 0;
                        o.put("  %-14s time    %u ticks = %u.%02u us\n", lbl, (unsigned)(w & 0xFFFFFF), (unsigned)(x100 / 100), (unsigned)(x100 % 100));
                    }
                }
            }
            if (k > 64) {
                o.put("  %-14s ... %d more values in the .toml\n", t.slots[s].name, k - 64);
            }
        }
    }
    if (has_eval(r) && t.nevals) {
        o.put("\nVerdicts\n");
        for (int i = 0; i < t.nevals; i++) {
            const DtEval &e = t.evals[i];
            int v = r.verdict[i];
            char vt[DT_VERD + 24];
            verdict_text(e, v, vt, sizeof(vt));
            o.put("  %-6s %s\n", verdict_word(v), vt);
        }
        o.put("\n%d passed, %d not passed\n", r.npass, r.nfail);
    }
    return o.over ? -1 : o.n;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// the run
uint32_t diag_live(CoreIo *io)
{
    uint32_t lo = io->rd(PD_LIVE), hi = io->rd(PD_LIVE + 1);
    return (lo | (hi << 8)) & ((1u << PD_LIVE_BITS) - 1);
}

static uint32_t rd32(CoreIo *io, uint32_t reg)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)io->rd(reg + i) << (8 * i);
    }
    return v;
}

static void wr_step(CoreIo *io, uint32_t w)
{
    for (int i = 0; i < 4; i++) {
        io->wr(PD_STEP + i, (uint8_t)(w >> (8 * i)));            // byte 3 stores the word and advances the address
    }
}

static uint32_t rd_fifo_word(CoreIo *io)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)io->rd(PD_FIFO + i) << (8 * i);           // the read of byte 3 pops
    }
    return v;
}

static char g_text[DT_FILE_MAX + 1];
static DiagFile g_file;
static DtProgram g_prog;
static DtResult g_res;
static char g_toml[40 * 1024];
static char g_txt[24 * 1024];

static void set_status(DtResult *r, const char *status, const char *reason)
{
    snprintf(r->status, sizeof(r->status), "%s", status);
    snprintf(r->reason, sizeof(r->reason), "%s", reason ? reason : "");
}

static void make_stamp(DiagEnv *env, char out[16])
{
    if (!env->stamp || !env->stamp(out)) {
        snprintf(out, 16, "n%06u", (unsigned)++env->counter);
    }
}

static void join(char *out, int n, const char *a, const char *b)
{
    snprintf(out, n, "%s/%s", a, b);
}

// "pins.drivable": the newest result of `pins` must show the eval as PASS. 0 holds, else the reason.
static int check_requires(DiagEnv *env, const DiagTest &t, char *why, int whylen)
{
    for (int i = 0; i < t.nrequires; i++) {
        char test[DT_NAME], ev[DT_NAME];
        const char *dot = strchr(t.requires[i], '.');
        int tl = (int)(dot - t.requires[i]);
        memcpy(test, t.requires[i], tl < (int)sizeof(test) ? tl : (int)sizeof(test) - 1);
        test[tl < (int)sizeof(test) ? tl : (int)sizeof(test) - 1] = 0;
        snprintf(ev, sizeof(ev), "%s", dot + 1);
        char dir[DT_PATH + 16];
        join(dir, sizeof(dir), env->folder, "results");
        static FsEntryShort list[64];
        int n = env->fs->list_short(dir, list, 64);
        char prefix[DT_NAME + 2], best[56];
        snprintf(prefix, sizeof(prefix), "%s_", test);
        best[0] = 0;
        for (int k = 0; k < n; k++) {
            size_t l = strlen(list[k].name);
            if (!list[k].dir && !list[k].cut && l < sizeof(best) && !strncasecmp(list[k].name, prefix, strlen(prefix)) && l > 5 && !strcasecmp(list[k].name + l - 5, ".toml") && strcmp(list[k].name, best) > 0) {
                snprintf(best, sizeof(best), "%s", list[k].name);
            }
        }
        if (!best[0]) {
            snprintf(why, whylen, "needs a result of '%s' (%s): run %s.toml first", test, t.requires[i], test);
            return -1;
        }
        char path[DT_PATH + 80];
        join(path, sizeof(path), dir, best);
        static char buf[24 * 1024];
        int len = env->fs->load(path, buf, sizeof(buf) - 1);
        if (len < 0) {
            snprintf(why, whylen, "cannot read %s", best);
            return -1;
        }
        buf[len] = 0;
        char needle[DT_NAME + 12];
        snprintf(needle, sizeof(needle), "\n%s = \"PASS", ev);
        const char *evsec = strstr(buf, "\n[eval]");
        if (!evsec || !strstr(evsec, needle)) {
            snprintf(why, whylen, "cannot run on this board: %s is not PASS in %s", t.requires[i], best);
            return -1;
        }
    }
    return 0;
}

static bool write_pair(DiagEnv *env, const char *id, const char *stamp, const char *toml, int ntoml, const char *txt, int ntxt, DiagOutcome *out, char *why, int whylen)
{
    char dir[DT_PATH + 16];
    join(dir, sizeof(dir), env->folder, "results");
    env->fs->mkdir(env->folder);
    env->fs->mkdir(dir);
    char a[DT_PATH + 64], b[DT_PATH + 64];
    snprintf(a, sizeof(a), "%s/%s_%s.toml", dir, id, stamp);
    snprintf(b, sizeof(b), "%s/%s_%s.txt", dir, id, stamp);
    if (!env->fs->save(a, toml, (uint32_t)ntoml)) {
        snprintf(why, whylen, "cannot write %s (read-only or full card?)", a);
        return false;
    }
    if (!env->fs->save(b, txt, (uint32_t)ntxt)) {
        if (env->fs->remove(a)) {                             // no half result: both files or neither
            snprintf(why, whylen, "cannot write %s (read-only or full card?)", b);
        } else {
            snprintf(why, whylen, "cannot write %s, and %s stays: the card refuses changes", b, a);
        }
        return false;
    }
    snprintf(out->last_toml, sizeof(out->last_toml), "%s", a);
    snprintf(out->last_txt, sizeof(out->last_txt), "%s", b);
    return true;
}

// One test that is parsed in g_file. Fills g_res, writes the files. Returns 0 when the files are written (whatever the verdicts), -1 with out->msg otherwise.
static int run_test(DiagEnv *env, const DiagTest &t, bool quiet, DiagOutcome *out)
{
    CoreIo *io = env->io;
    DtResult &r = g_res;
    memset(&r, 0, sizeof(r));
    set_status(&r, "error", "");
    char stamp[16];
    make_stamp(env, stamp);
    char why[160];
    why[0] = 0;
    bool wrote_hw = false;
    struct JoyMenuGuard {                                    // a test that asks for the keyboard only: the joystick ports are no menu keys until the result dialog
        DiagEnv *e;
        bool off;
        void back() { if (off) { off = false; e->joy_menu(e->joy_ud, true); } }
        ~JoyMenuGuard() { back(); }
    } jg = { env, t.kbd_only && env->joy_menu != 0 };
    if (jg.off) {
        env->joy_menu(env->joy_ud, false);
    }

    do {
        if (io->rd(PD_ID) != 'D' || io->rd(PD_ID + 1) != 'I' || io->rd(PD_ID + 2) != 'A' || io->rd(PD_ID + 3) != 'G') {
            set_status(&r, "error", "the core does not answer as DIAG");
            break;
        }
        r.tpus256 = rd32(io, PD_TICKS);
        DtErr ce;
        if (dt_compile(t, r.tpus256, &g_prog, &ce)) {
            snprintf(why, sizeof(why), "step %d: %s", ce.line, ce.msg);
            set_status(&r, "error", why);
            break;
        }
        memcpy(r.timeout_ticks, g_prog.timeout_ticks, sizeof(r.timeout_ticks));
        r.expect = dt_plan(t, r.slot_of);
        if (r.expect < 0) {
            set_status(&r, "error", "the test pushes more than 512 words into the result FIFO");
            break;
        }
        if (check_requires(env, t, why, sizeof(why))) {
            set_status(&r, "refused", why);
            break;
        }
        if (g_prog.n > PD_STEPS_MAX) {
            set_status(&r, "error", "the step list is longer than the step RAM");
            break;
        }
        if (!quiet) {
            char pr[120];
            snprintf(pr, sizeof(pr), "Running %s", t.name);
            env->ui->progress(pr);
        }
        io->wr(PD_CTRL, PD_CTRL_ABORT);
        io->wr(PD_CTRL, PD_CTRL_CLEAR);
        io->wr(PD_STEP_ADDR, 0);
        for (int i = 0; i < g_prog.n; i++) {
            wr_step(io, g_prog.words[i]);
        }
        wrote_hw = true;
        io->wr(PD_CTRL, PD_CTRL_RUN);
        env->delay_ms(1);                                    // the engine takes the RUN a few clocks later; until then STATUS still shows the DONE of the run before (ABORT sets it too)
        uint32_t limit = env->run_timeout_ms ? env->run_timeout_ms : 60000;
        uint32_t t0 = env->now_ms();
        set_status(&r, "done", "");
        for (;;) {
            uint8_t st = io->rd(PD_STATUS);
            if (st & PD_ST_STEPERR) {
                set_status(&r, "error", "the engine reports a step RAM error");
                break;
            }
            if (st & PD_ST_WAITGO) {
                uint32_t pc = io->rd(PD_PC) | ((uint32_t)io->rd(PD_PC + 1) << 8);
                int si = -1;
                for (int k = 0; k < t.nsteps; k++) {
                    if (g_prog.word_of[k] == pc) {
                        si = k;
                    }
                }
                if (si < 0 || t.steps[si].op != DT_OP_WAIT_USER) {
                    snprintf(why, sizeof(why), "the engine waits at step %u, which is no wait_user", (unsigned)pc);
                    set_status(&r, "error", why);
                    break;
                }
                int pi = t.steps[si].prompt;
                int a = env->ui->prompt(t.name, t.prompts[pi].text, pi + 1, t.nprompts, t.prompts[pi].timeout_s);
                if (a == DUI_ABORT) {
                    set_status(&r, "aborted", "stopped at the prompt by the person");
                    break;
                }
                r.prompt_state[pi] = a == DUI_CONTINUE ? DT_PR_OK : a == DUI_TIMEOUT ? DT_PR_MISSED : DT_PR_SKIPPED;
                io->wr(PD_CTRL, PD_CTRL_GO);
                // the engine has left the prompt when it is not waiting any more or stands at another step (the next prompt may follow at once)
                uint32_t g0 = env->now_ms();
                for (;;) {
                    uint32_t pc2 = io->rd(PD_PC) | ((uint32_t)io->rd(PD_PC + 1) << 8);
                    if (!(io->rd(PD_STATUS) & PD_ST_WAITGO) || pc2 != pc) {
                        break;
                    }
                    if (env->now_ms() - g0 >= 500) {
                        break;
                    }
                    env->delay_ms(1);
                }
                {
                    uint32_t pc2 = io->rd(PD_PC) | ((uint32_t)io->rd(PD_PC + 1) << 8);
                    if ((io->rd(PD_STATUS) & PD_ST_WAITGO) && pc2 == pc) {
                        set_status(&r, "error", "the engine did not leave the prompt after GO");
                        break;
                    }
                }
                t0 = env->now_ms();
                continue;
            }
            if (st & PD_ST_DONE) {
                break;
            }
            if (env->now_ms() - t0 > limit) {
                set_status(&r, "timeout", "the engine did not finish in time");
                break;
            }
            env->delay_ms(1);
        }
        uint8_t st = io->rd(PD_STATUS);
        int level = io->rd(PD_FIFO_LEVEL) | ((int)io->rd(PD_FIFO_LEVEL + 1) << 8);
        if (level > 512) {
            level = 512;
        }
        for (int i = 0; i < level; i++) {
            r.fifo[i] = rd_fifo_word(io);
        }
        r.nfifo = level;
        if (!strcmp(r.status, "done")) {
            if (st & PD_ST_OVERFLOW) {
                set_status(&r, "error", "the result FIFO overflowed");
            } else if (level != r.expect) {
                snprintf(why, sizeof(why), "the engine pushed %d words, the step list pushes %d", level, r.expect);
                set_status(&r, "error", why);
            }
        }
        if (!strcmp(r.status, "done")) {
            dt_evaluate(t, &r);
        }
    } while (0);
    jg.back();
    if (wrote_hw) {
        io->wr(PD_CTRL, PD_CTRL_ABORT);                      // every output released, whatever happened
    }
    if (strcmp(r.status, "done")) {
        r.nfifo = strcmp(r.status, "error") ? r.nfifo : 0;   // an error leaves no half-decoded data in the file
    }
    int nt = dt_write_toml(t, r, env->core_version, stamp, g_toml, sizeof(g_toml));
    int nx = dt_write_text(t, r, env->core_version, stamp, g_txt, sizeof(g_txt));
    if (nt < 0 || nx < 0) {
        snprintf(out->msg, sizeof(out->msg), "the result of %s does not fit the buffers", t.id);
        return -1;
    }
    if (!write_pair(env, t.id, stamp, g_toml, nt, g_txt, nx, out, why, sizeof(why))) {
        snprintf(out->msg, sizeof(out->msg), "%s", why);
        return -1;
    }
    out->ntests++;
    if (!strcmp(r.status, "done") && !r.nfail) {
        out->npass++;
    } else if (!strcmp(r.status, "refused")) {
        out->nrefused++;
    } else if (!strcmp(r.status, "done")) {
        out->nfail++;
    } else {
        out->nerror++;
    }
    if (!quiet) {
        char sum[300];
        int k = snprintf(sum, sizeof(sum), "%s: %s", t.name, r.status);
        if (r.reason[0]) {
            k += snprintf(sum + k, sizeof(sum) - k, " - %s", r.reason);
        }
        if (!strcmp(r.status, "done")) {
            k += snprintf(sum + k, sizeof(sum) - k, "\n%d passed, %d not passed\n", r.npass, r.nfail);
            for (int i = 0; i < t.nevals && k < 200; i++) {
                char vt[DT_VERD + 24];
                verdict_text(t.evals[i], r.verdict[i], vt, sizeof(vt));
                k += snprintf(sum + k, sizeof(sum) - k, "%s %s\n", verdict_word(r.verdict[i]), vt);
            }
        }
        const char *fn = strrchr(out->last_txt, '/');
        snprintf(sum + (k < (int)sizeof(sum) - 60 ? k : (int)sizeof(sum) - 60), 60, "\nReport: results/%s", fn ? fn + 1 : "");
        env->ui->show("Port Diagnostics", sum);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// T201: the A/V tests. A file with kind = "avtest" walks combinations of the A/V encoder's knobs (docs/specs/T201-diag-av-out.md): per combination it sets the knobs, asks
// the narrow questions, and writes the answers with the combination to results/<id>_<stamp>.toml and .txt. No probe engine is involved.
//   [test] name, id, version        [ask] questions = [...], timeout_s, first_gate (a NO to the first question skips the others), stop_after_ok (all YES ends the walk)
//   [base] knob = "label"           the knobs that stay fixed (every knob not named in the file keeps the value the A/V page holds)
//   [sweep] knob = ["label", ...]   the axes: the walk is the product of the lists in file order, the last axis changes fastest
// A knob's key is its row's name in lower case with '_' for blanks ("v_sign"); its value is the label the page shows ("Inverted"), compared without regard to case. When the
// standard is set by the file and burst start, burst end and blank level are not, they follow the standard (PAL 28 / 99 / 77, NTSC 20 / 102 / 73).
static const char *const av_l_std[]    = { "PAL 50 Hz", "NTSC 60 Hz", "PAL 60 Hz", "NTSC 50 Hz" };
static const int16_t     av_v_std[]    = { 0, 1, 2, 3 };
static const char *const av_l_groups[] = { "Y=A, C=B, CVBS=C", "Y=A, C=C, CVBS=B", "Y=B, C=A, CVBS=C", "Y=C, C=A, CVBS=B", "Y=B, C=C, CVBS=A", "Y=C, C=B, CVBS=A" };
static const int16_t     av_v_groups[] = { 0, 1, 2, 3, 4, 5 };
static const char *const av_l_bits[]   = { "MSB first", "Reversed" };
static const int16_t     av_v_bits[]   = { 0, 1 };
static const char *const av_l_vsign[]  = { "Normal", "Inverted" };
static const char *const av_l_trim[]   = { "-480 Hz", "-450 Hz", "-420 Hz", "-390 Hz", "-360 Hz", "-330 Hz", "-300 Hz", "-270 Hz", "-240 Hz", "-210 Hz", "-180 Hz", "-150 Hz",
    "-120 Hz", "-90 Hz", "-60 Hz", "-30 Hz", "0 Hz", "+30 Hz", "+60 Hz", "+90 Hz", "+120 Hz", "+150 Hz", "+180 Hz", "+210 Hz", "+240 Hz", "+270 Hz", "+300 Hz", "+330 Hz",
    "+360 Hz", "+390 Hz", "+420 Hz", "+450 Hz", "+480 Hz" };
static const int16_t     av_v_trim[]   = { -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const char *const av_l_bstart[] = { "16", "18", "20", "22", "24", "26", "28", "30", "32", "36", "40" };
static const int16_t     av_v_bstart[] = { 16, 18, 20, 22, 24, 26, 28, 30, 32, 36, 40 };
static const char *const av_l_bend[]   = { "90", "94", "96", "98", "99", "100", "102", "104", "106", "110" };
static const int16_t     av_v_bend[]   = { 90, 94, 96, 98, 99, 100, 102, 104, 106, 110 };
static const char *const av_l_bamp[]   = { "0 %", "50 %", "75 %", "100 %", "125 %", "150 %" };
static const int16_t     av_v_bamp[]   = { 0, 16, 24, 32, 40, 48 };
static const char *const av_l_gain[]   = { "0 %", "50 %", "75 %", "100 %", "125 %", "150 %", "190 %" };
static const int16_t     av_v_gain[]   = { 0, 8, 12, 16, 20, 24, 30 };
static const char *const av_l_sync[]   = { "0", "8", "16", "24", "32" };
static const int16_t     av_v_sync[]   = { 0, 8, 16, 24, 32 };
static const char *const av_l_blank[]  = { "64", "68", "73", "77", "80", "84", "88" };
static const int16_t     av_v_blank[]  = { 64, 68, 73, 77, 80, 84, 88 };
static const char *const av_l_white[]  = { "8/16", "9/16", "10/16", "11/16", "12/16", "13/16", "14/16", "15/16", "16/16" };
static const int16_t     av_v_white[]  = { 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const char *const av_l_eq[]     = { "Off", "On" };
static const char *const av_l_vlen[]   = { "3 lines", "2.5 lines", "6 lines" };
static const int16_t     av_v_vlen[]   = { 6, 5, 12 };                     // half lines: a vsync of 3 lines is 6 half lines
static const char *const av_l_vpos[]   = { "-8", "-4", "0", "+4", "+8" };
static const int16_t     av_v_vpos[]   = { -8, -4, 0, 4, 8 };
static const char *const av_l_lines[]  = { "263", "262" };
#define AVN(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
const AvKnob dt_av_knobs[DT_AV_N] = {
    { "Standard",        av_l_std,    AVN(av_l_std),    0, av_v_std },
    { "Groups",          av_l_groups, AVN(av_l_groups), 5, av_v_groups },
    { "Bits A",          av_l_bits,   AVN(av_l_bits),   0, av_v_bits },
    { "Bits B",          av_l_bits,   AVN(av_l_bits),   0, av_v_bits },
    { "Bits C",          av_l_bits,   AVN(av_l_bits),   0, av_v_bits },
    { "V sign",          av_l_vsign,  AVN(av_l_vsign),  1, av_v_bits },
    { "Subcarrier trim", av_l_trim,   AVN(av_l_trim),   16, av_v_trim },
    { "Burst start",     av_l_bstart, AVN(av_l_bstart), 6, av_v_bstart },
    { "Burst end",       av_l_bend,   AVN(av_l_bend),   4, av_v_bend },
    { "Burst level",     av_l_bamp,   AVN(av_l_bamp),   3, av_v_bamp },
    { "Chroma gain",     av_l_gain,   AVN(av_l_gain),   3, av_v_gain },
    { "Sync level",      av_l_sync,   AVN(av_l_sync),   0, av_v_sync },
    { "Blank level",     av_l_blank,  AVN(av_l_blank),  3, av_v_blank },
    { "White level",     av_l_white,  AVN(av_l_white),  3, av_v_white },
    { "Equalizing",      av_l_eq,     AVN(av_l_eq),     0, av_v_bits },
    { "Vsync length",    av_l_vlen,   AVN(av_l_vlen),   0, av_v_vlen },
    { "Vsync position",  av_l_vpos,   AVN(av_l_vpos),   2, av_v_vpos },
    { "Lines 60 Hz",     av_l_lines,  AVN(av_l_lines),  0, av_v_bits },
};

#define AV_AXES   DT_AV_N
#define AV_MAXSTEPS 400
#define AV_QS     4
#define AV_CH     4                // labels of a choice question
#define AV_WORD   16               // a label or an answer word
struct AvAxis { int knob, n; int idx[KV_ITEMS]; };
struct AvTest {
    char name[DT_STR], id[DT_NAME];
    int version;
    char q[AV_QS][DT_PATH]; int nq;
    char good[AV_QS][AV_WORD]; int ngood;   // the answer that counts as good, per question ("YES" when the file names none)
    char ch[AV_QS][AV_CH][AV_WORD]; int nch[AV_QS];   // a question with a choice of answers: the labels (nch 0: a YES / NO question)
    int timeout_s;
    bool gate, stop_ok;
    int base[DT_AV_N];                   // -1: the page's value
    AvAxis axis[AV_AXES]; int naxis;
    int total;
};

static bool av_key_is(const char *key, const char *knob)   // "v_sign" is "V sign"
{
    for (; *key && *knob; key++, knob++) {
        char k = (char)(*knob == ' ' ? '_' : tolower((unsigned char)*knob));
        if (*key != k) {
            return false;
        }
    }
    return !*key && !*knob;
}
static int av_knob_of(const char *key)
{
    for (int k = 0; k < DT_AV_N; k++) {
        if (av_key_is(key, dt_av_knobs[k].key)) {
            return k;
        }
    }
    return -1;
}
static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return !*a && !*b;
}
static int av_label_of(int k, const char *v)
{
    for (int i = 0; i < dt_av_knobs[k].n; i++) {
        if (ieq(dt_av_knobs[k].labels[i], v)) {
            return i;
        }
    }
    return -1;
}
static int av_idx_of_val(int k, int v)
{
    for (int i = 0; i < dt_av_knobs[k].n; i++) {
        if (dt_av_knobs[k].val[i] == v) {
            return i;
        }
    }
    return dt_av_knobs[k].def;
}

static bool is_avtest(const char *text, uint32_t len)
{
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *q = skip_ws(p);
        if (!strncmp(q, "kind", 4) && (q[4] == ' ' || q[4] == '=' || q[4] == '\t')) {
            const char *e = (const char *)memchr(q, '\n', (size_t)(end - q));
            const char *v = (const char *)memchr(q, '"', (size_t)(end - q));
            return v && (!e || v < e) && (size_t)(end - v) >= 8 && !strncmp(v, "\"avtest\"", 8);
        }
        if (*q == '[') {
            return false;                         // the first section: no kind line before it
        }
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        p = nl ? nl + 1 : end;
    }
    return false;
}

static AvTest g_av;

static int av_parse(const char *text, uint32_t len, AvTest *t, DtErr *err)
{
    memset(t, 0, sizeof(*t));
    for (int k = 0; k < DT_AV_N; k++) {
        t->base[k] = -1;
    }
    t->timeout_s = 120;
    int sect = 0;                                 // 0 top, 1 test, 2 ask, 3 base, 4 sweep
    int line = 0;
    bool kind = false;
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        int ll = nl ? (int)(nl - p) : (int)(end - p);
        static char buf[320];
        line++;
        if (ll >= (int)sizeof(buf)) {
            return fail(err, line, "a line is longer than %d characters", (int)sizeof(buf) - 1);
        }
        memcpy(buf, p, (size_t)ll);
        buf[ll] = 0;
        p = nl ? nl + 1 : end;
        const char *q = skip_ws(buf);
        if (!*q || *q == '#' || *q == '\r') {
            continue;
        }
        if (*q == '[') {
            if (q[1] == '[') {
                return fail(err, line, "an A/V test has no [[tables]]");
            }
            char nm[24];
            int i = 0;
            q++;
            while (*q && *q != ']' && i < 23) {
                nm[i++] = *q++;
            }
            nm[i] = 0;
            sect = !strcmp(nm, "test") ? 1 : !strcmp(nm, "ask") ? 2 : !strcmp(nm, "base") ? 3 : !strcmp(nm, "sweep") ? 4 : -1;
            if (sect < 0) {
                return fail(err, line, "unknown section [%s]", nm);
            }
            continue;
        }
        Kv kv;
        if (parse_kv(q, &kv, err, line)) {
            return -1;
        }
        if (sect == 0) {
            if (strcmp(kv.key, "kind")) {
                return fail(err, line, "unknown key %s", kv.key);
            }
            kind = kv.type == V_STR && !strcmp(kv.s, "avtest");
        } else if (sect == 1) {
            if (!strcmp(kv.key, "name") && kv.type == V_STR) {
                cpy(t->name, sizeof(t->name), kv.s);
            } else if (!strcmp(kv.key, "id") && kv.type == V_STR) {
                cpy(t->id, sizeof(t->id), kv.s);
            } else if (!strcmp(kv.key, "version") && kv.type == V_INT) {
                t->version = (int)kv.i;
            } else {
                return fail(err, line, "unknown key %s in [test]", kv.key);
            }
        } else if (sect == 2) {
            if (!strcmp(kv.key, "questions") && kv.type == V_ARR_STR && kv.n >= 1 && kv.n <= AV_QS) {
                t->nq = kv.n;
                for (int i = 0; i < kv.n; i++) {
                    cpy(t->q[i], sizeof(t->q[i]), kv.items[i]);
                }
            } else if (!strcmp(kv.key, "good") && kv.type == V_ARR_STR && kv.n >= 1 && kv.n <= AV_QS) {
                t->ngood = kv.n;
                for (int i = 0; i < kv.n; i++) {
                    if (strlen(kv.items[i]) >= AV_WORD) {
                        return fail(err, line, "a good answer is at most %d characters", AV_WORD - 1);
                    }
                    cpy(t->good[i], sizeof(t->good[i]), kv.items[i]);
                }
            } else if (!strncmp(kv.key, "choices_", 8) && kv.key[8] >= '1' && kv.key[8] <= '0' + AV_QS && !kv.key[9] && kv.type == V_ARR_STR && kv.n >= 2 && kv.n <= AV_CH) {
                int qn = kv.key[8] - '1';
                t->nch[qn] = kv.n;
                for (int i = 0; i < kv.n; i++) {
                    if (strlen(kv.items[i]) >= AV_WORD) {
                        return fail(err, line, "a choice is at most %d characters", AV_WORD - 1);
                    }
                    cpy(t->ch[qn][i], sizeof(t->ch[qn][i]), kv.items[i]);
                }
            } else if (!strcmp(kv.key, "timeout_s") && kv.type == V_INT && kv.i >= 0 && kv.i <= 3600) {
                t->timeout_s = (int)kv.i;
            } else if (!strcmp(kv.key, "first_gate") && kv.type == V_BOOL) {
                t->gate = kv.i != 0;
            } else if (!strcmp(kv.key, "stop_after_ok") && kv.type == V_BOOL) {
                t->stop_ok = kv.i != 0;
            } else {
                return fail(err, line, "[ask] takes questions = [1 to %d strings], good = [one answer per question], choices_N = [2 to %d labels] for question N, timeout_s, first_gate, stop_after_ok", AV_QS, AV_CH);
            }
        } else {
            int k = av_knob_of(kv.key);
            if (k < 0) {
                return fail(err, line, "%s is not a knob of the A/V page", kv.key);
            }
            if (sect == 3) {
                if (kv.type != V_STR) {
                    return fail(err, line, "%s takes one label in quotes", kv.key);
                }
                int i = av_label_of(k, kv.s);
                if (i < 0) {
                    return fail(err, line, "%s has no value \"%s\"", dt_av_knobs[k].key, kv.s);
                }
                t->base[k] = i;
            } else {
                if (kv.type != V_ARR_STR || kv.n < 1) {
                    return fail(err, line, "%s takes a list of labels in quotes", kv.key);
                }
                for (int a = 0; a < t->naxis; a++) {
                    if (t->axis[a].knob == k) {
                        return fail(err, line, "%s is swept twice", kv.key);
                    }
                }
                AvAxis &ax = t->axis[t->naxis++];
                ax.knob = k;
                ax.n = kv.n;
                for (int i = 0; i < kv.n; i++) {
                    ax.idx[i] = av_label_of(k, kv.items[i]);
                    if (ax.idx[i] < 0) {
                        return fail(err, line, "%s has no value \"%s\"", dt_av_knobs[k].key, kv.items[i]);
                    }
                }
            }
        }
    }
    if (!kind) {
        return fail(err, 0, "not an A/V test (kind = \"avtest\" is missing)");
    }
    if (!t->id[0] || !t->nq) {
        return fail(err, 0, "an A/V test needs [test] id and [ask] questions");
    }
    if (t->ngood && t->ngood != t->nq) {
        return fail(err, 0, "[ask] good has %d answers for %d questions", t->ngood, t->nq);
    }
    for (int i = 0; i < AV_QS; i++) {
        if (t->nch[i] && i >= t->nq) {
            return fail(err, 0, "[ask] choices_%d names a question the file does not have", i + 1);
        }
        if (!t->ngood) {
            cpy(t->good[i], sizeof(t->good[i]), "YES");
        }
        if (!t->nch[i] && i < t->nq && !ieq(t->good[i], "YES") && !ieq(t->good[i], "NO")) {
            return fail(err, 0, "[ask] good for question %d is YES or NO", i + 1);
        }
        if (t->nch[i] && i < t->nq) {
            bool found = false;
            for (int c = 0; c < t->nch[i]; c++) {
                found |= ieq(t->ch[i][c], t->good[i]);
            }
            if (!found) {
                return fail(err, 0, "[ask] good for question %d is not one of its choices", i + 1);
            }
        }
    }
    t->total = 1;
    for (int a = 0; a < t->naxis; a++) {
        t->total *= t->axis[a].n;
        if (t->total > AV_MAXSTEPS) {
            return fail(err, 0, "the walk has more than %d combinations", AV_MAXSTEPS);
        }
    }
    return 0;
}

static const char *av_ans_word(int a)
{
    return a == DUI_CONTINUE ? "YES" : a == DUI_SKIP ? "NO" : a == DUI_TIMEOUT ? "MISSED" : "SKIPPED";
}

// The result files of a walk are written in pieces: the header creates both (write_pair, as every result), each piece after it is appended through an open file.
struct AvSink {
    CoreFs *fs;
    char path[DT_PATH + 64];
    uint32_t off;
    bool bad;
};
static bool av_flush(void *ud, const char *p, int n)
{
    AvSink *k = (AvSink *)ud;
    CoreFile *f = k->fs->open(k->path, true);
    if (!f) {
        k->bad = true;
        return false;
    }
    bool ok = f->writable() && f->write(k->off, p, (uint32_t)n) && f->sync();
    k->fs->close(f);
    if (!ok) {
        k->bad = true;
        return false;
    }
    k->off += (uint32_t)n;
    return true;
}

static int run_avtest(DiagEnv *env, const AvTest &t, DiagOutcome *out)
{
    if (!env->av_set || !env->av_get) {
        snprintf(out->msg, sizeof(out->msg), "%s: this application has no A/V page to drive", t.id);
        return -1;
    }
    char stamp[16];
    make_stamp(env, stamp);
    int cur[DT_AV_N], applied[DT_AV_N];
    for (int k = 0; k < DT_AV_N; k++) {
        cur[k] = t.base[k] >= 0 ? t.base[k] : env->av_get(env->av_ud, k);
        applied[k] = -1;
    }
    bool fixed_std = t.base[AVK_STD] >= 0, fixed_b[3] = { t.base[AVK_BSTART] >= 0, t.base[AVK_BEND] >= 0, t.base[AVK_BLANK] >= 0 };
    for (int a = 0; a < t.naxis; a++) {
        int k = t.axis[a].knob;
        fixed_std |= k == AVK_STD;
        fixed_b[0] |= k == AVK_BSTART;
        fixed_b[1] |= k == AVK_BEND;
        fixed_b[2] |= k == AVK_BLANK;
    }
    // Both files get their header first (a card that refuses writes says so before the walk starts); the rest is appended as the walk goes, so no walk is too long for a buffer.
    static char hb_t[4096], hb_x[1024], wb_t[4096], wb_x[4096];
    Out h = { hb_t, sizeof(hb_t), 0, false, 0, 0 };
    Out hx = { hb_x, sizeof(hb_x), 0, false, 0, 0 };
    h.put("# TRX64U Port Diagnostics A/V result (T201). [knobs]: the values of the knobs the file does not walk. One [step.N] per combination: the knobs that differ, then the answers to the questions in order (YES / NO, or the choice's label).\n[meta]\ntest = ");
    h.str(t.id);
    h.put("\nname = ");
    h.str(t.name);
    h.put("\nversion = %d\ncore = ", t.version);
    h.str(env->core_version ? env->core_version : "");
    h.put("\ndate = ");
    h.str(stamp);
    h.put("\nsteps_planned = %d\nquestions = [", t.total);
    for (int i = 0; i < t.nq; i++) {
        h.put(i ? ", " : "");
        h.str(t.q[i]);
    }
    h.put("]\ngood = [");
    for (int i = 0; i < t.nq; i++) {
        h.put(i ? ", " : "");
        h.str(t.good[i]);
    }
    h.put("]\n\n[knobs]\n");
    for (int k = 0; k < DT_AV_N; k++) {
        h.put("%s = ", dt_av_knobs[k].key);
        h.str(dt_av_knobs[k].labels[cur[k]]);
        h.put("\n");
    }
    hx.put("A/V test %s (%s)\nDate %s\n\nEvery combination whose first answer is YES, with all its answers; GOOD marks the ones where every answer is the good one (", t.name, t.id, stamp);
    for (int i = 0; i < t.nq; i++) {
        hx.put("%s%s", i ? ", " : "", t.good[i]);
    }
    hx.put(").\nThe summary is at the end.\n\n");
    if (h.over || hx.over) {
        snprintf(out->msg, sizeof(out->msg), "the header of %s does not fit its buffer", t.id);
        return -1;
    }
    char why[200];
    if (!write_pair(env, t.id, stamp, hb_t, h.n, hb_x, hx.n, out, why, sizeof(why))) {
        snprintf(out->msg, sizeof(out->msg), "%s", why);
        return -1;
    }
    AvSink st_t = { env->fs, "", (uint32_t)h.n, false }, st_x = { env->fs, "", (uint32_t)hx.n, false };
    snprintf(st_t.path, sizeof(st_t.path), "%s", out->last_toml);
    snprintf(st_x.path, sizeof(st_x.path), "%s", out->last_txt);
    Out o = { wb_t, sizeof(wb_t), 0, false, av_flush, &st_t };
    Out x = { wb_x, sizeof(wb_x), 0, false, av_flush, &st_x };
    int done = 0, nq1 = 0, ngood = 0;
    bool aborted = false;
    char title[DT_STR + 8];
    snprintf(title, sizeof(title), "%s", t.name[0] ? t.name : t.id);
    for (int s = 0; s < t.total && !aborted && !o.over && !x.over; s++) {
        int rem = s;
        int pick[DT_AV_N];
        for (int k = 0; k < DT_AV_N; k++) {
            pick[k] = cur[k];
        }
        for (int a = t.naxis - 1; a >= 0; a--) {
            pick[t.axis[a].knob] = t.axis[a].idx[rem % t.axis[a].n];
            rem /= t.axis[a].n;
        }
        if (fixed_std) {
            int sv = dt_av_knobs[AVK_STD].val[pick[AVK_STD]];
            bool ntsc = sv == 1 || sv == 3;
            if (!fixed_b[0]) pick[AVK_BSTART] = av_idx_of_val(AVK_BSTART, ntsc ? 20 : 28);
            if (!fixed_b[1]) pick[AVK_BEND] = av_idx_of_val(AVK_BEND, ntsc ? 102 : 99);
            if (!fixed_b[2]) pick[AVK_BLANK] = av_idx_of_val(AVK_BLANK, ntsc ? 73 : 77);
        }
        bool std_changed = false;
        for (int k = 0; k < DT_AV_N; k++) {
            if (applied[k] != pick[k]) {
                env->av_set(env->av_ud, k, pick[k]);
                std_changed |= k == AVK_STD;
                applied[k] = pick[k];
            }
        }
        env->delay_ms(std_changed ? 700 : 150);        // the System Mode and the TV's lock need their time
        if (env->av_freeze) {
            env->av_freeze(env->av_ud, true);          // the marker stands still while the questions are open: the picture is judged on a still image
        }
        o.put("\n[step.%d]\n", s + 1);
        for (int k = 0; k < DT_AV_N; k++) {
            bool show = pick[k] != cur[k];
            for (int a = 0; a < t.naxis; a++) {
                show |= t.axis[a].knob == k;
            }
            if (show) {
                o.put("%s = ", dt_av_knobs[k].key);
                o.str(dt_av_knobs[k].labels[pick[k]]);
                o.put("\n");
            }
        }
        char aw[AV_QS][AV_WORD];
        int first = 99;                                // the first answer as a DUI_ code
        bool good = true;
        for (int qi = 0; qi < t.nq; qi++) {
            int a;
            if (aborted || (qi > 0 && t.gate && first != DUI_CONTINUE)) {   // after ESC no further question is asked
                a = 99;
            } else if (t.nch[qi]) {
                const char *items[AV_CH];
                for (int c = 0; c < t.nch[qi]; c++) {
                    items[c] = t.ch[qi][c];
                }
                int pk = 0;
                a = env->ui->choice(title, t.q[qi], s + 1, t.total, t.timeout_s, items, t.nch[qi], &pk);
                if (a == DUI_CONTINUE) {
                    cpy(aw[qi], sizeof(aw[qi]), t.ch[qi][pk]);
                }
            } else {
                a = env->ui->question(title, t.q[qi], s + 1, t.total, t.timeout_s);
            }
            if (a == DUI_ABORT) {
                aborted = true;
                a = 99;
            }
            if (!(t.nch[qi] && a == DUI_CONTINUE)) {
                cpy(aw[qi], sizeof(aw[qi]), av_ans_word(a));
            }
            if (qi == 0) {
                first = a;
            }
            good &= ieq(aw[qi], t.good[qi]);
        }
        o.put("answers = [");
        for (int qi = 0; qi < t.nq; qi++) {
            o.put(qi ? ", \"%s\"" : "\"%s\"", aw[qi]);
        }
        o.put("]\n");
        done++;
        if (first == DUI_CONTINUE) {
            nq1++;
            x.put("Step %d%s: ", s + 1, good ? " GOOD" : "");
            for (int k = 0; k < DT_AV_N; k++) {
                x.put("%s%s=%s", k ? ", " : "", dt_av_knobs[k].key, dt_av_knobs[k].labels[pick[k]]);
            }
            x.put("; answers: ");
            for (int qi = 0; qi < t.nq; qi++) {
                x.put("%s%s", qi ? ", " : "", aw[qi]);
            }
            x.put("\n");
        }
        if (good && !aborted) {
            ngood++;
            if (t.stop_ok) {
                break;
            }
        }
    }
    o.put("\n[result]\nstatus = \"%s\"\nsteps_asked = %d\nsteps_first_yes = %d\nsteps_good = %d\n", aborted ? "aborted" : "done", done, nq1, ngood);
    x.put("\nStatus: %s. %d of %d combinations walked, %d with the first answer YES, %d good.\n", aborted ? "stopped by the tester" : "done", done, t.total, nq1, ngood);
    if (!ngood) {
        x.put("No combination was good.\n");
    }
    bool wrote = o.drain() & x.drain();
    if (env->av_freeze) {
        env->av_freeze(env->av_ud, false);
    }
    if (env->av_done) {
        env->av_done(env->av_ud);
    }
    if (!wrote || st_t.bad || st_x.bad) {
        snprintf(out->msg, sizeof(out->msg), "cannot go on writing %s (read-only or full card?); %d combinations are in the file", st_t.bad ? out->last_toml : out->last_txt, done);
        return -1;
    }
    out->ntests++;
    if (aborted) {
        out->nerror++;
    } else if (ngood) {
        out->npass++;
    } else {
        out->nfail++;
    }
    char sum[400];
    snprintf(sum, sizeof(sum), "%s: %s\n%d of %d combinations walked, %d with the first answer YES, %d good\n\nReport: results/%s_%s.txt", t.name, aborted ? "stopped" : "done", done, t.total, nq1, ngood, t.id, stamp);
    env->ui->show("Port Diagnostics", sum);
    return 0;
}

static int load_parse(DiagEnv *env, const char *path, DiagOutcome *out)
{
    int n = env->fs->load(path, g_text, DT_FILE_MAX);
    if (n < 0) {
        snprintf(out->msg, sizeof(out->msg), "cannot read %s (missing, or longer than %d bytes)", path, DT_FILE_MAX);
        return -1;
    }
    DtErr e;
    if (dt_parse(g_text, (uint32_t)n, &g_file, &e)) {
        const char *fn = strrchr(path, '/');
        if (e.line) {
            snprintf(out->msg, sizeof(out->msg), "%s line %d: %s", fn ? fn + 1 : path, e.line, e.msg);
        } else {
            snprintf(out->msg, sizeof(out->msg), "%s: %s", fn ? fn + 1 : path, e.msg);
        }
        return -1;
    }
    return 0;
}

int diag_run_file(DiagEnv *env, const char *path, DiagOutcome *out)
{
    memset(out, 0, sizeof(*out));
    {
        int n = env->fs->load(path, g_text, DT_FILE_MAX);
        if (n >= 0 && is_avtest(g_text, (uint32_t)n)) {                  // T201: an A/V test has its own parser and runner
            DtErr e;
            if (av_parse(g_text, (uint32_t)n, &g_av, &e)) {
                const char *fn = strrchr(path, '/');
                if (e.line) {
                    snprintf(out->msg, sizeof(out->msg), "%s line %d: %s", fn ? fn + 1 : path, e.line, e.msg);
                } else {
                    snprintf(out->msg, sizeof(out->msg), "%s: %s", fn ? fn + 1 : path, e.msg);
                }
                return -1;
            }
            return run_avtest(env, g_av, out);
        }
    }
    if (load_parse(env, path, out)) {
        return -1;
    }
    if (g_file.kind == DT_KIND_TEST) {
        return run_test(env, g_file.test, false, out);
    }
    // a suite: every test of the list, in a row, each with its own two files; the suite itself gets a short summary
    DiagSuite su = g_file.suite;
    char dir[DT_PATH + 16];
    snprintf(dir, sizeof(dir), "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) {
        *sl = 0;
    }
    static char sum[2048];
    int k = snprintf(sum, sizeof(sum), "Suite %s\n", su.name[0] ? su.name : su.id);
    char stamp[16];
    make_stamp(env, stamp);
    for (int i = 0; i < su.ntests; i++) {
        char tp[DT_PATH + 120];
        snprintf(tp, sizeof(tp), "%s/%s", dir, su.tests[i]);
        DiagOutcome one;
        memset(&one, 0, sizeof(one));
        if (load_parse(env, tp, &one)) {
            k += snprintf(sum + k, sizeof(sum) - k, "  not run  %s\n", one.msg);
            out->nerror++;
            if (su.stop_on_fail) {
                break;
            }
            continue;
        }
        if (g_file.kind != DT_KIND_TEST) {
            k += snprintf(sum + k, sizeof(sum) - k, "  not run  %s is a suite (suites do not nest)\n", su.tests[i]);
            out->nerror++;
            continue;
        }
        char id[DT_NAME];
        snprintf(id, sizeof(id), "%s", g_file.test.id);
        int rc = run_test(env, g_file.test, true, &one);
        const DtResult &r = g_res;
        if (rc) {
            k += snprintf(sum + k, sizeof(sum) - k, "  error    %s: %s\n", id, one.msg);
            out->nerror++;
            snprintf(out->msg, sizeof(out->msg), "%s", one.msg);
            break;                                              // the card refuses writes: the next test would too
        }
        out->ntests++;
        out->npass += one.npass;
        out->nfail += one.nfail;
        out->nrefused += one.nrefused;
        out->nerror += one.nerror;
        k += snprintf(sum + k, sizeof(sum) - k, "  %-8s %s", one.npass ? "pass" : one.nrefused ? "refused" : one.nfail ? "FAIL" : "error", id);
        if (r.reason[0]) {
            k += snprintf(sum + k, sizeof(sum) - k, " - %s", r.reason);
        }
        k += snprintf(sum + k, sizeof(sum) - k, "\n");
        if (su.stop_on_fail && (one.nfail || one.nerror)) {
            break;
        }
    }
    k += snprintf(sum + k, sizeof(sum) - k, "%d test(s): %d pass, %d fail, %d refused, %d error\n", out->ntests + 0, out->npass, out->nfail, out->nrefused, out->nerror);
    int w = 0;
    {
        // the suite's own pair: the readable summary and a TOML with the same lines
        static char tm[2600];
        Out o = { tm, sizeof(tm), 0, false };
        o.put("# TRX64U Port Diagnostics suite result (T175)\n[meta]\nsuite = ");
        o.str(su.id);
        o.put("\ndate = ");
        o.str(stamp);
        o.put("\ntests = %d\npass = %d\nfail = %d\nrefused = %d\nerror = %d\nsummary = ", out->ntests, out->npass, out->nfail, out->nrefused, out->nerror);
        o.str(sum);
        o.put("\n");
        char why[160];
        if (!o.over && write_pair(env, su.id, stamp, tm, o.n, sum, k, out, why, sizeof(why))) {
            w = 1;
        } else {
            snprintf(out->msg, sizeof(out->msg), "%s", o.over ? "the suite summary does not fit" : why);
        }
    }
    char shown[300];
    snprintf(shown, sizeof(shown), "%s", sum);
    env->ui->show("Port Diagnostics", shown);
    return w ? 0 : -1;
}

// True when the file at p is byte for byte a text this app shipped earlier under that name (portdiag_superseded): only such a file is
// replaced by the new example. A file the person edited, or any other name, is left alone.
static bool superseded_here(CoreFs *fs, const char *p, uint64_t sz, const char *name)
{
    static char buf[DT_FILE_MAX + 1];
    if (sz == 0 || sz > DT_FILE_MAX) {
        return false;
    }
    int n = -1;
    for (const PortdiagTest *o = portdiag_superseded; o->name; o++) {
        if (strcmp(o->name, name) || strlen(o->text) != sz) {
            continue;
        }
        if (n < 0) {
            n = fs->load(p, buf, DT_FILE_MAX);
        }
        if (n == (int)sz && !memcmp(buf, o->text, (size_t)sz)) {
            return true;
        }
    }
    return false;
}

int diag_install_examples(CoreFs *fs, const char *folder)
{
    char dir[DT_PATH + 16];
    if (!fs->mkdir(folder)) {
        return -1;
    }
    snprintf(dir, sizeof(dir), "%s/tests", folder);
    if (!fs->mkdir(dir)) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < PORTDIAG_NTESTS; i++) {
        char p[DT_PATH + 64];
        snprintf(p, sizeof(p), "%s/%s", dir, portdiag_tests[i].name);
        uint64_t sz;
        bool d;
        if (fs->stat(p, &sz, &d)) {
            if (d || !superseded_here(fs, p, sz, portdiag_tests[i].name)) {
                continue;                                          // never over a file: the person may have edited it
            }
        }
        if (!fs->save(p, portdiag_tests[i].text, (uint32_t)strlen(portdiag_tests[i].text))) {
            return -1;
        }
        n++;
    }
    return n;
}
