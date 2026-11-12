/*
 * cond.c — see cond.h. The parser is TRX64's (crates/trx64-monitor/src/observers.rs parse_cond): or -> and ->
 * comparison -> primary, the same tokens, the same words in the errors.
 */
#include "cond.h"

#include "hw.h"
#include "mlib.h"

typedef struct
{
    cond_t *c;
    const char *p;
    const char *err;
    char tok[24];
    int tlen;   /* 0: end */
} ps_t;

static int is_alpha(char c) { return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')); }
static int is_digit(char c) { return (c >= '0') && (c <= '9'); }
static int is_hex(char c) { return is_digit(c) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')); }

/* the next token into ps->tok; tlen 0 at the end */
static void next_tok(ps_t *s)
{
    const char *p = s->p;
    for (;;) {
        while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
            p++;
        }
        if (!*p) {
            s->tlen = 0;
            s->tok[0] = 0;
            s->p = p;
            return;
        }
        int n = 0;
        if (((p[0] == '<') || (p[0] == '>') || (p[0] == '=') || (p[0] == '!')) && (p[1] == '=')) {
            n = 2;
        } else if (((p[0] == '&') && (p[1] == '&')) || ((p[0] == '|') && (p[1] == '|'))) {
            n = 2;
        } else if ((p[0] == '<') || (p[0] == '>') || (p[0] == '(') || (p[0] == ')')) {
            n = 1;
        } else if ((p[0] == '$') && is_hex(p[1])) {
            n = 1;
            while (is_hex(p[n])) {
                n++;
            }
        } else if ((p[0] == '%') && ((p[1] == '0') || (p[1] == '1'))) {
            n = 1;
            while ((p[n] == '0') || (p[n] == '1')) {
                n++;
            }
        } else if (is_digit(p[0]) || is_alpha(p[0])) {
            /* a word or a bare hex number ("f5", "0a", "c000"): told apart in parse_primary */
            while (is_digit(p[n]) || is_alpha(p[n])) {
                n++;
            }
        } else {
            p++; /* a character no token starts with is dropped, as TRX64's tokenizer does */
            continue;
        }
        if (n > (int)sizeof(s->tok) - 1) {
            n = (int)sizeof(s->tok) - 1;
        }
        memcpy(s->tok, p, (size_t)n);
        s->tok[n] = 0;
        s->tlen = n;
        s->p = p + n;
        return;
    }
}

static int new_node(ps_t *s, int kind, int op, int l, int r, int32_t v)
{
    if (s->c->count >= COND_NODES) {
        s->err = "condition too long";
        return 0;
    }
    int i = s->c->count++;
    s->c->n[i].kind = (uint8_t)kind;
    s->c->n[i].op = (uint8_t)op;
    s->c->n[i].l = (int8_t)l;
    s->c->n[i].r = (int8_t)r;
    s->c->n[i].v = v;
    return i;
}

static int parse_or(ps_t *s);

static int parse_primary(ps_t *s)
{
    if (!s->tlen) {
        s->err = "unexpected end of condition";
        return 0;
    }
    char t[24];
    memcpy(t, s->tok, sizeof(t));
    next_tok(s);
    if (t[0] == '(') {
        int e = parse_or(s);
        if (s->err) {
            return 0;
        }
        if (!s->tlen || (s->tok[0] != ')')) {
            s->err = "missing )";
            return 0;
        }
        next_tok(s);
        return e;
    }
    if (t[0] == '$') {
        uint32_t v;
        if (!m_parse_hex(t + 1, -1, &v)) {
            s->err = "bad hex";
            return 0;
        }
        return new_node(s, CN_NUM, 0, 0, 0, (int32_t)v);
    }
    if (t[0] == '%') {
        uint32_t v = 0;
        for (const char *q = t + 1; *q; q++) {
            v = (v << 1) | (uint32_t)(*q - '0');
        }
        return new_node(s, CN_NUM, 0, 0, 0, (int32_t)v);
    }
    /* the words first: `a` is the register, `$a` the number (every other word that reads as hex has a digit or
     * a letter beyond a-f in it only when it is a name: pc, sp, fl, rl, val, addr, cy, x, y) */
    static const char *const ids[] = { "a", "x", "y", "pc", "sp", "fl", "rl", "val", "addr", "cy" };
    for (int i = 0; i < 10; i++) {
        if (m_strieq(t, ids[i])) {
            return new_node(s, CN_ID, i, 0, 0, 0);
        }
    }
    uint32_t v;
    if (m_parse_hex(t, -1, &v)) {
        return new_node(s, CN_NUM, 0, 0, 0, (int32_t)v);
    }
    s->err = "unknown term (use a/x/y/pc/sp/fl/rl/val/addr/cy, a number in hex ($a for the number a), == != < > <= >= && ||)";
    return 0;
}

static int cmp_op(const char *t)
{
    if (m_streq(t, "==")) return CO_EQ;
    if (m_streq(t, "!=")) return CO_NE;
    if (m_streq(t, "<")) return CO_LT;
    if (m_streq(t, ">")) return CO_GT;
    if (m_streq(t, "<=")) return CO_LE;
    if (m_streq(t, ">=")) return CO_GE;
    return -1;
}

static int parse_cmp(ps_t *s)
{
    int l = parse_primary(s);
    while (!s->err && s->tlen && (cmp_op(s->tok) >= 0)) {
        int op = cmp_op(s->tok);
        next_tok(s);
        int r = parse_primary(s);
        if (s->err) {
            return 0;
        }
        l = new_node(s, CN_BIN, op, l, r, 0);
    }
    return l;
}

static int parse_and(ps_t *s)
{
    int l = parse_cmp(s);
    while (!s->err && s->tlen && m_streq(s->tok, "&&")) {
        next_tok(s);
        int r = parse_cmp(s);
        if (s->err) {
            return 0;
        }
        l = new_node(s, CN_BIN, CO_AND, l, r, 0);
    }
    return l;
}

static int parse_or(ps_t *s)
{
    int l = parse_and(s);
    while (!s->err && s->tlen && m_streq(s->tok, "||")) {
        next_tok(s);
        int r = parse_and(s);
        if (s->err) {
            return 0;
        }
        l = new_node(s, CN_BIN, CO_OR, l, r, 0);
    }
    return l;
}

const char *cond_parse(const char *src, cond_t *c)
{
    static char msg[64];
    memset(c, 0, sizeof(*c));
    c->root = -1;
    ps_t s = { c, src, 0, { 0 }, 0 };
    next_tok(&s);
    if (!s.tlen) {
        return 0;
    }
    int root = parse_or(&s);
    if (!s.err && s.tlen) {
        sbuf_t b;
        sb_init(&b, msg, sizeof(msg));
        sb_put(&b, "trailing '");
        sb_put(&b, s.tok);
        sb_put(&b, "' in condition");
        return sb_str(&b);
    }
    if (s.err) {
        return s.err;
    }
    c->root = (int8_t)root;
    return 0;
}

int64_t cond_eval(const cond_t *c, int node, const cond_env_t *e)
{
    const cnode_t *n = &c->n[node];
    switch (n->kind) {
    case CN_NUM: return n->v;
    case CN_ID:
        switch (n->op) {
        case CI_A: return e->a;
        case CI_X: return e->x;
        case CI_Y: return e->y;
        case CI_PC: return e->pc;
        case CI_SP: return e->sp;
        case CI_FL: return e->fl;
        case CI_RL: return e->rl;
        case CI_VAL: return e->val;
        case CI_ADDR: return e->addr;
        default: return e->cy;
        }
    default: break;
    }
    int64_t l = cond_eval(c, n->l, e);
    int64_t r = cond_eval(c, n->r, e);
    switch (n->op) {
    case CO_EQ: return l == r;
    case CO_NE: return l != r;
    case CO_LT: return l < r;
    case CO_GT: return l > r;
    case CO_LE: return l <= r;
    case CO_GE: return l >= r;
    case CO_AND: return (l != 0) && (r != 0);
    default: return (l != 0) || (r != 0);
    }
}

/* ---- what the hardware holds ------------------------------------------- */

static int flatten(const cond_t *c, int node, int8_t *out, int *n)
{
    const cnode_t *x = &c->n[node];
    if ((x->kind == CN_BIN) && (x->op == CO_AND)) {
        return flatten(c, x->l, out, n) && flatten(c, x->r, out, n);
    }
    if (*n >= COND_REST) {
        return 0;
    }
    out[(*n)++] = (int8_t)node;
    return 1;
}

/* a conjunct "ID op NUM" or "NUM op ID": the id, the operator as if the id were on the left, the number */
static int id_cmp_num(const cond_t *c, int node, int *id, int *op, int32_t *v)
{
    const cnode_t *x = &c->n[node];
    if ((x->kind != CN_BIN) || (x->op > CO_GE)) {
        return 0;
    }
    const cnode_t *l = &c->n[x->l];
    const cnode_t *r = &c->n[x->r];
    if ((l->kind == CN_ID) && (r->kind == CN_NUM)) {
        *id = l->op;
        *op = x->op;
        *v = r->v;
        return 1;
    }
    if ((l->kind == CN_NUM) && (r->kind == CN_ID)) {
        static const uint8_t mirror[] = { CO_EQ, CO_NE, CO_GT, CO_LT, CO_GE, CO_LE };
        *id = r->op;
        *op = mirror[x->op];
        *v = l->v;
        return 1;
    }
    return 0;
}

void cond_split(const cond_t *c, int units, uint16_t lo, uint16_t hi, cond_hw_t *q)
{
    memset(q, 0, sizeof(*q));
    q->lo = lo;
    q->hi = hi;
    if (c->root < 0) {
        return;
    }
    int8_t all[COND_REST];
    int na = 0;
    if (!units || !flatten(c, c->root, all, &na)) {
        q->rest[0] = c->root;
        q->nrest = 1;
        return;
    }
    for (int i = 0; i < na; i++) {
        int id, op;
        int32_t v;
        int taken = 0;
        if (id_cmp_num(c, all[i], &id, &op, &v)) {
            if ((id == CI_VAL) && (op == CO_EQ) && !q->has_data && (v >= 0) && (v <= 0xFF)) {
                q->has_data = 1;
                q->dmask = 0xFF;
                q->dval = (uint8_t)v;
                taken = 1;
            } else if ((id == CI_ADDR) && (v >= 0) && (v <= 0xFFFF)) {
                taken = 1;
                switch (op) {
                case CO_EQ: q->lo = q->lo > v ? q->lo : (uint16_t)v; q->hi = q->hi < v ? q->hi : (uint16_t)v; break;
                case CO_GE: q->lo = q->lo > v ? q->lo : (uint16_t)v; break;
                case CO_GT:
                    if (v == 0xFFFF) { q->lo = 1; q->hi = 0; } else if (q->lo < v + 1) { q->lo = (uint16_t)(v + 1); }
                    break;
                case CO_LE: q->hi = q->hi < v ? q->hi : (uint16_t)v; break;
                case CO_LT:
                    if (v == 0) { q->lo = 1; q->hi = 0; } else if (q->hi > v - 1) { q->hi = (uint16_t)(v - 1); }
                    break;
                default: taken = 0; break; /* != stays software */
                }
            } else if (!q->reg && ((id == CI_A) || (id == CI_X) || (id == CI_Y) || (id == CI_SP)) && (v >= 0) &&
                       (v <= 0xFF)) {
                int sel = (id == CI_A) ? R_SEL_A : (id == CI_X) ? R_SEL_X : (id == CI_Y) ? R_SEL_Y : R_SEL_S;
                int rop = -1;
                int32_t rv = v;
                switch (op) {
                case CO_EQ: rop = R_OP_EQ; break;
                case CO_NE: rop = R_OP_NE; break;
                case CO_LT: if (v >= 1) rop = R_OP_LT; break;
                case CO_GE: rop = R_OP_GE; break;
                case CO_GT: if (v <= 0xFE) { rop = R_OP_GE; rv = v + 1; } break;
                case CO_LE: if (v <= 0xFE) { rop = R_OP_LT; rv = v + 1; } break;
                default: break;
                }
                if (rop >= 0) {
                    q->reg = UNIT_REG(sel, rop, rv, 0xFF);
                    taken = 1;
                }
            }
        }
        if (!taken) {
            q->rest[q->nrest++] = all[i];
        }
    }
}

static void fmt_node(const cond_t *c, int node, sbuf_t *b, int nested)
{
    static const char *const ids[] = { "a", "x", "y", "pc", "sp", "fl", "rl", "val", "addr", "cy" };
    static const char *const ops[] = { "==", "!=", "<", ">", "<=", ">=", "&&", "||" };
    const cnode_t *n = &c->n[node];
    if (n->kind == CN_NUM) {
        /* hex without `$`, padded to 2 / 4 / 8 digits so `0a` is never taken for the register `a` */
        uint32_t v = (uint32_t)n->v;
        sb_hex(b, v, v <= 0xFF ? 2 : v <= 0xFFFF ? 4 : 8);
        return;
    }
    if (n->kind == CN_ID) {
        sb_put(b, ids[n->op]);
        return;
    }
    int logic = n->op >= CO_AND;
    if (nested) {
        sb_putc(b, '(');
    }
    /* comparisons inside && / || need no parentheses; a comparison's own operands are terms or nested */
    fmt_node(c, n->l, b, logic ? (c->n[n->l].kind == CN_BIN && c->n[n->l].op >= CO_AND && c->n[n->l].op != n->op) : 1);
    if (logic) {
        sb_putc(b, ' ');
    }
    sb_put(b, ops[n->op]);
    if (logic) {
        sb_putc(b, ' ');
    }
    fmt_node(c, n->r, b, logic ? (c->n[n->r].kind == CN_BIN && c->n[n->r].op >= CO_AND && c->n[n->r].op != n->op) : 1);
    if (nested) {
        sb_putc(b, ')');
    }
}

void cond_fmt(const cond_t *c, int node, char *out, int cap)
{
    sbuf_t b;
    sb_init(&b, out, cap);
    fmt_node(c, node, &b, 0);
    sb_str(&b);
}
