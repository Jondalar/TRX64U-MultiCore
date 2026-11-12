/*
 * asm6502.c — TRX64's one-line assembler (crates/trx64-monitor/src/
 * assembler.rs assemble_line, a port of c64re's assembler6502.ts): the
 * documented NMOS opcodes, found by reversing the disassembler's own table,
 * so `a` and `d` agree byte for byte.
 */
#include "asm6502.h"
#include "dis6502_table.h"

static const char *const documented[] = {
    "LDA", "LDX", "LDY", "STA", "STX", "STY", "TAX", "TAY", "TXA", "TYA", "TSX", "TXS", "PHA", "PHP",
    "PLA", "PLP", "AND", "ORA", "EOR", "BIT", "ADC", "SBC", "CMP", "CPX", "CPY", "INC", "DEC", "INX",
    "INY", "DEX", "DEY", "ASL", "LSR", "ROL", "ROR", "JMP", "JSR", "RTS", "RTI", "BCC", "BCS", "BEQ",
    "BNE", "BMI", "BPL", "BVC", "BVS", "CLC", "SEC", "CLD", "SED", "CLI", "SEI", "CLV", "BRK", "NOP", 0
};

static int name_eq(const char *a, const char *b)
{
    return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]);
}

static int is_documented(const char *m)
{
    for (int i = 0; documented[i]; i++) {
        if (name_eq(documented[i], m)) {
            return 1;
        }
    }
    return 0;
}

/* TRX64's MICROCODE_TABLE holds the documented opcodes only; the
 * disassembler's table also has the undocumented ones, of which NOP
 * (everything but $EA) and SBC #imm ($EB) carry documented names. The
 * lowest opcode of a (mnemonic, mode) wins, as TRX64's index. */
static int lookup(const char *m, int mode)
{
    for (int op = 0; op < 256; op++) {
        if ((dis_ops[op].mode != mode) || !name_eq(dis_names[dis_ops[op].name], m)) {
            continue;
        }
        if (name_eq(m, "NOP") && (op != 0xEA)) {
            continue;
        }
        if (op == 0xEB) {
            continue;
        }
        return op;
    }
    return -1;
}

static int has_mode(const char *m, int mode) { return lookup(m, mode) >= 0; }

static int hexval(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

static void err2(sbuf_t *e, const char *a, const char *b, const char *c)
{
    sb_put(e, a);
    if (b) {
        sb_put(e, b);
    }
    if (c) {
        sb_put(e, c);
    }
}

/* parse_value: %bin, $hex, decimal, or bare hex; wide when written wide */
static int parse_value(const char *t, int n, int32_t *v, int *wide, sbuf_t *e)
{
    char raw[40];
    int rn = n < 39 ? n : 39;
    memcpy(raw, t, (size_t)rn);
    raw[rn] = 0;
    if (!n) {
        sb_put(e, "missing operand");
        return 0;
    }
    int32_t x = 0;
    if (t[0] == '%') {
        if (n == 1) {
            err2(e, "bad binary operand '", raw, "'");
            return 0;
        }
        for (int i = 1; i < n; i++) {
            if ((t[i] != '0') && (t[i] != '1')) {
                err2(e, "bad binary operand '", raw, "'");
                return 0;
            }
            x = (x << 1) | (t[i] - '0');
        }
        *v = x;
        *wide = (n - 1 > 8) || (x > 0xFF);
        return 1;
    }
    if (t[0] == '$') {
        if ((n == 1) || (n > 9)) {
            err2(e, "bad hex operand '", raw, "'");
            return 0;
        }
        for (int i = 1; i < n; i++) {
            int h = hexval(t[i]);
            if (h < 0) {
                err2(e, "bad hex operand '", raw, "'");
                return 0;
            }
            x = (x << 4) | h;
        }
        *v = x;
        *wide = (n - 1 > 2) || (x > 0xFF);
        return 1;
    }
    int dec = 1, hex = 1;
    for (int i = 0; i < n; i++) {
        dec &= (t[i] >= '0') && (t[i] <= '9');
        hex &= hexval(t[i]) >= 0;
    }
    if (dec && (n <= 9)) {
        for (int i = 0; i < n; i++) {
            x = x * 10 + (t[i] - '0');
        }
        *v = x;
        *wide = x > 0xFF;
        return 1;
    }
    if (hex && (n <= 8)) {
        for (int i = 0; i < n; i++) {
            x = (x << 4) | hexval(t[i]);
        }
        *v = x;
        *wide = (n > 2) || (x > 0xFF);
        return 1;
    }
    err2(e, "bad operand '", raw, "'");
    return 0;
}

static int ends_with(const char *s, int n, const char *suf)
{
    int k = m_strlen(suf);
    return (n >= k) && !memcmp(s + n - k, suf, (size_t)k);
}

static void put_hex(sbuf_t *e, uint32_t v)
{
    sb_putc(e, '$');
    int d = 1;
    for (uint32_t t = v >> 4; t; t >>= 4) {
        d++;
    }
    sb_hex(e, v, d);
}

int asm6502_line(const char *text, uint16_t pc, uint8_t *bytes, sbuf_t *e)
{
    while ((*text == ' ') || (*text == '\t')) {
        text++;
    }
    int n = m_strlen(text);
    while ((n > 0) && ((text[n - 1] == ' ') || (text[n - 1] == '\t') || (text[n - 1] == '\r'))) {
        n--;
    }
    if (!n) {
        sb_put(e, "empty instruction");
        return 0;
    }
    char trimmed[80];
    int tn = n < 79 ? n : 79;
    memcpy(trimmed, text, (size_t)tn);
    trimmed[tn] = 0;
    int alpha3 = n >= 3;
    for (int i = 0; alpha3 && (i < 3); i++) {
        char c = m_lower(text[i]);
        alpha3 = (c >= 'a') && (c <= 'z');
    }
    if (!alpha3 || ((n > 3) && (text[3] != ' ') && (text[3] != '\t'))) {
        err2(e, "unparsable instruction '", trimmed, "'");
        return 0;
    }
    char m[4] = { (char)(m_lower(text[0]) - 32), (char)(m_lower(text[1]) - 32), (char)(m_lower(text[2]) - 32), 0 };
    char ml[4] = { m_lower(text[0]), m_lower(text[1]), m_lower(text[2]), 0 };
    /* the operand without any white space, lower case */
    char op[64];
    int on = 0;
    for (int i = 3; (i < n) && (on < 63); i++) {
        if ((text[i] != ' ') && (text[i] != '\t')) {
            op[on++] = m_lower(text[i]);
        }
    }
    op[on] = 0;
    if (!is_documented(m)) {
        err2(e, "unknown mnemonic '", ml, "'");
        return 0;
    }
    if (!on || ((on == 1) && (op[0] == 'a'))) {
        int o = lookup(m, M_IMP);
        if (o < 0) {
            o = lookup(m, M_ACC);
        }
        if (o < 0) {
            err2(e, "'", ml, "' requires an operand");
            return 0;
        }
        bytes[0] = (uint8_t)o;
        return 1;
    }
    int32_t v;
    int wide;
    if (has_mode(m, M_REL)) {
        if (!parse_value(op, on, &v, &wide, e)) {
            return 0;
        }
        if ((v < 0) || (v > 0xFFFF)) {
            sb_put(e, "branch target ");
            put_hex(e, (uint32_t)v);
            sb_put(e, " out of range");
            return 0;
        }
        int32_t off = v - ((pc + 2) & 0xFFFF);
        if ((off < -128) || (off > 127)) {
            sb_put(e, "branch out of range (");
            if (off < 0) {
                sb_putc(e, '-');
                sb_dec(e, (uint64_t)-off);
            } else {
                sb_dec(e, (uint64_t)off);
            }
            sb_put(e, " bytes)");
            return 0;
        }
        bytes[0] = (uint8_t)lookup(m, M_REL);
        bytes[1] = (uint8_t)off;
        return 2;
    }
    if (op[0] == '#') {
        if (!parse_value(op + 1, on - 1, &v, &wide, e)) {
            return 0;
        }
        if (v > 0xFF) {
            sb_put(e, "immediate value ");
            put_hex(e, (uint32_t)v);
            sb_put(e, " overflows a byte");
            return 0;
        }
        int o = lookup(m, M_IMM);
        if (o < 0) {
            err2(e, "'", ml, "' has no immediate mode");
            return 0;
        }
        bytes[0] = (uint8_t)o;
        bytes[1] = (uint8_t)v;
        return 2;
    }
    if (op[0] == '(') {
        if (ends_with(op, on, ",x)") && (on >= 4)) {
            if (!parse_value(op + 1, on - 4, &v, &wide, e)) {
                return 0;
            }
            if (v > 0xFF) {
                sb_put(e, "(zp,x) operand ");
                put_hex(e, (uint32_t)v);
                sb_put(e, " not zero-page");
                return 0;
            }
            int o = lookup(m, M_INDX);
            if (o < 0) {
                err2(e, "'", ml, "' has no (zp,x) mode");
                return 0;
            }
            bytes[0] = (uint8_t)o;
            bytes[1] = (uint8_t)v;
            return 2;
        }
        if (ends_with(op, on, "),y") && (on >= 4)) {
            if (!parse_value(op + 1, on - 4, &v, &wide, e)) {
                return 0;
            }
            if (v > 0xFF) {
                sb_put(e, "(zp),y operand ");
                put_hex(e, (uint32_t)v);
                sb_put(e, " not zero-page");
                return 0;
            }
            int o = lookup(m, M_INDY);
            if (o < 0) {
                err2(e, "'", ml, "' has no (zp),y mode");
                return 0;
            }
            bytes[0] = (uint8_t)o;
            bytes[1] = (uint8_t)v;
            return 2;
        }
        if (ends_with(op, on, ")") && (on >= 2)) {
            int comma = 0;
            for (int i = 1; i < on - 1; i++) {
                comma |= op[i] == ',';
            }
            if (!comma) {
                if (!parse_value(op + 1, on - 2, &v, &wide, e)) {
                    return 0;
                }
                if (v > 0xFFFF) {
                    sb_put(e, "indirect operand ");
                    put_hex(e, (uint32_t)v);
                    sb_put(e, " overflows 16 bits");
                    return 0;
                }
                int o = lookup(m, M_IND);
                if (o < 0) {
                    err2(e, "'", ml, "' has no indirect mode");
                    return 0;
                }
                bytes[0] = (uint8_t)o;
                bytes[1] = (uint8_t)v;
                bytes[2] = (uint8_t)(v >> 8);
                return 3;
            }
        }
        char raw[64];
        int k = 0;
        for (int i = 3; (i < n) && (k < 63); i++) {
            if ((text[i] != ' ') && (text[i] != '\t')) {
                raw[k++] = text[i];
            }
        }
        raw[k] = 0;
        err2(e, "bad indirect operand '", raw, "'");
        return 0;
    }
    int zpm = M_ZP, absm = M_ABS, vn = on;
    if (ends_with(op, on, ",x")) {
        zpm = M_ZPX;
        absm = M_ABSX;
        vn -= 2;
    } else if (ends_with(op, on, ",y")) {
        zpm = M_ZPY;
        absm = M_ABSY;
        vn -= 2;
    } else {
        for (int i = 0; i < on; i++) {
            if (op[i] == ',') {
                char raw[64];
                int k = 0;
                for (int j = 3; (j < n) && (k < 63); j++) {
                    if ((text[j] != ' ') && (text[j] != '\t')) {
                        raw[k++] = text[j];
                    }
                }
                raw[k] = 0;
                err2(e, "bad index suffix in '", raw, "'");
                return 0;
            }
        }
    }
    if (!parse_value(op, vn, &v, &wide, e)) {
        return 0;
    }
    if ((v < 0) || (v > 0xFFFF)) {
        sb_put(e, "operand ");
        put_hex(e, (uint32_t)v);
        sb_put(e, " overflows 16 bits");
        return 0;
    }
    int fits_zp = (v <= 0xFF) && !wide;
    if (fits_zp) {
        int o = lookup(m, zpm);
        if (o >= 0) {
            bytes[0] = (uint8_t)o;
            bytes[1] = (uint8_t)v;
            return 2;
        }
    }
    int o = lookup(m, absm);
    if (o >= 0) {
        bytes[0] = (uint8_t)o;
        bytes[1] = (uint8_t)v;
        bytes[2] = (uint8_t)(v >> 8);
        return 3;
    }
    static const char *const mname[] = { "imp", "acc", "imm", "zp", "zpx", "zpy", "abs", "absx", "absy",
                                         "ind", "indx", "indy", "rel" };
    sb_put(e, "'");
    sb_put(e, ml);
    sb_put(e, "' has no ");
    if (fits_zp) {
        sb_put(e, mname[zpm]);
        sb_put(e, " or ");
    }
    sb_put(e, mname[absm]);
    sb_put(e, " mode");
    return 0;
}

/* ---- the block assembler (TRX64 assemble_block) --------------------------- */

enum { CTX_BRANCH, CTX_IMM, CTX_OTHER };

typedef struct
{
    asm_block_t *b;
    char unk[96];  /* the unknown labels of the line, ", " joined */
    int nunk;
} actx_t;

static int find_label(const asm_block_t *b, const char *name, int n)
{
    for (int i = 0; i < b->nlab; i++) {
        if ((m_strlen(b->lab[i].name) == n) && !memcmp(b->lab[i].name, name, (size_t)n)) {
            return i;
        }
    }
    return -1;
}

static void set_label(asm_block_t *b, const char *name, int n, uint16_t v)
{
    int i = find_label(b, name, n);
    if (i < 0) {
        if (b->nlab >= ASM_LABELS) {
            return;
        }
        i = b->nlab++;
        if (n > 23) {
            n = 23;
        }
        memcpy(b->lab[i].name, name, (size_t)n);
        b->lab[i].name[n] = 0;
    }
    b->lab[i].v = v;
}

static int is_ident(const char *t, int n)
{
    if (!n || !(((t[0] | 32) >= 'a' && (t[0] | 32) <= 'z') || t[0] == '_')) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static void add_unk(actx_t *x, const char *t, int n)
{
    sbuf_t s;
    int len = m_strlen(x->unk);
    sb_init(&s, x->unk + len, (int)sizeof(x->unk) - len);
    if (x->nunk) {
        sb_put(&s, ", ");
    }
    sb_putn(&s, t, n);
    sb_str(&s);
    x->nunk++;
}

static int trim(const char **t, int n)
{
    while (n && ((**t == ' ') || (**t == '\t'))) {
        (*t)++;
        n--;
    }
    while (n && (((*t)[n - 1] == ' ') || ((*t)[n - 1] == '\t') || ((*t)[n - 1] == '\r'))) {
        n--;
    }
    return n;
}

static int eval_expr(const char *e, int n, uint16_t pc, int ctx, actx_t *x, int32_t *out, sbuf_t *err)
{
    n = trim(&e, n);
    char sel = 0;
    const char *body = e;
    int bn = n;
    if (n && ((e[0] == '<') || (e[0] == '>'))) {
        sel = e[0];
        body++;
        bn--;
    }
    int32_t total = 0;
    int sign = 1;
    int start = 0;
    for (int i = 0; i <= bn; i++) {
        int end = (i == bn) || (((body[i] == '+') || (body[i] == '-')) && (i > 0));
        if (!end) {
            continue;
        }
        const char *t = body + start;
        int tn = trim(&t, i - start);
        if (!tn) {
            sb_put(err, "missing term in '");
            sb_putn(err, e, n);
            sb_putc(err, '\'');
            return 0;
        }
        int32_t v;
        int wide;
        char dummy[96];
        sbuf_t de;
        sb_init(&de, dummy, sizeof(dummy));
        if ((tn == 1) && (t[0] == '*')) {
            v = pc;
        } else if (parse_value(t, tn, &v, &wide, &de)) {
        } else if (is_ident(t, tn)) {
            int li = find_label(x->b, t, tn);
            if (li >= 0) {
                v = x->b->lab[li].v;
            } else {
                add_unk(x, t, tn);
                v = (ctx == CTX_BRANCH) ? pc : (ctx == CTX_IMM) ? 0 : 0xFFFF;
            }
        } else {
            sb_put(err, "bad term '");
            sb_putn(err, t, tn);
            sb_putc(err, '\'');
            return 0;
        }
        total += sign * v;
        if (i < bn) {
            sign = (body[i] == '+') ? 1 : -1;
        }
        start = i + 1;
    }
    int32_t v = total & 0xFFFF;
    *out = (sel == '<') ? (v & 0xFF) : (sel == '>') ? ((v >> 8) & 0xFF) : total;
    return 1;
}

/* the operand of an instruction as the literal text asm6502_line takes */
static int resolve_operand(const char *mn, const char *opnd, uint16_t pc, int wide, actx_t *x, char *out,
                           sbuf_t *err)
{
    char c[64];
    int cn = 0;
    for (int i = 0; opnd[i] && (cn < 63); i++) {
        if ((opnd[i] != ' ') && (opnd[i] != '\t')) {
            c[cn++] = opnd[i];
        }
    }
    c[cn] = 0;
    if (!cn || ((cn == 1) && ((c[0] | 32) == 'a'))) {
        memcpy(out, c, (size_t)cn + 1);
        return 1;
    }
    char lw[64];
    for (int i = 0; i <= cn; i++) {
        lw[i] = m_lower(c[i]);
    }
    const char *pre = "", *suf = "";
    int es = 0, en = cn;
    if (c[0] == '#') {
        pre = "#";
        es = 1;
        en = cn - 1;
    } else if ((lw[0] == '(') && ends_with(lw, cn, ",x)")) {
        pre = "(";
        suf = ",x)";
        es = 1;
        en = cn - 4;
    } else if ((lw[0] == '(') && ends_with(lw, cn, "),y")) {
        pre = "(";
        suf = "),y";
        es = 1;
        en = cn - 4;
    } else if ((lw[0] == '(') && ends_with(lw, cn, ")")) {
        pre = "(";
        suf = ")";
        es = 1;
        en = cn - 2;
    } else if (ends_with(lw, cn, ",x")) {
        suf = ",x";
        en = cn - 2;
    } else if (ends_with(lw, cn, ",y")) {
        suf = ",y";
        en = cn - 2;
    }
    if (en < 0) {
        en = 0;
    }
    int32_t v;
    int w;
    char dummy[96];
    sbuf_t de;
    sb_init(&de, dummy, sizeof(dummy));
    if (parse_value(c + es, en, &v, &w, &de)) {
        memcpy(out, c, (size_t)cn + 1);
        return 1;
    }
    char m[4] = { (char)(m_lower(mn[0]) - 32), (char)(m_lower(mn[1]) - 32), (char)(m_lower(mn[2]) - 32), 0 };
    int ctx = (m_strlen(mn) == 3 && has_mode(m, M_REL)) ? CTX_BRANCH : (pre[0] == '#') ? CTX_IMM : CTX_OTHER;
    int before = x->nunk;
    if (!eval_expr(c + es, en, pc, ctx, x, &v, err)) {
        return 0;
    }
    int this_unk = x->nunk > before;
    if (this_unk) {
        v &= 0xFFFF;
    }
    if ((v < 0) || (v > 0xFFFF)) {
        sb_put(err, "value ");
        if (v < 0) {
            sb_putc(err, '-');
            sb_dec(err, (uint64_t)-v);
        } else {
            sb_dec(err, (uint64_t)v);
        }
        sb_put(err, " out of range in '");
        sb_put(err, opnd);
        sb_putc(err, '\'');
        return 0;
    }
    sbuf_t o;
    sb_init(&o, out, 64);
    sb_put(&o, pre);
    sb_putc(&o, '$');
    if ((pre[0] == '#') || ((v <= 0xFF) && !wide && !this_unk && (ctx != CTX_BRANCH))) {
        sb_hex(&o, (uint32_t)v, 2);
    } else {
        sb_hex(&o, (uint32_t)v, 4);
    }
    sb_put(&o, suf);
    sb_str(&o);
    return 1;
}

static void line_err(asm_block_t *b, int line, const char *msg)
{
    sbuf_t s;
    int len = m_strlen(b->err);
    sb_init(&s, b->err + len, (int)sizeof(b->err) - len);
    sb_put(&s, "line ");
    sb_dec(&s, (uint64_t)line);
    sb_put(&s, ": ");
    sb_put(&s, msg);
    sb_putc(&s, '\n');
    sb_str(&s);
    b->nerr++;
}

static void put_byte(asm_block_t *b, int *nb, uint8_t v)
{
    if (*nb < ASM_BYTES) {
        b->bytes[*nb] = v;
    }
    (*nb)++;
}

void asm6502_block(const char *src, uint16_t origin, asm_block_t *b)
{
    memset(b, 0, sizeof(*b));
    b->origin = origin;
    static uint8_t wide_lines[512]; /* line index -> sized wide in pass 1 */
    memset(wide_lines, 0, sizeof(wide_lines));
    for (int pass = 1; pass <= 2; pass++) {
        uint16_t pc = b->origin, org = b->origin;
        int emitted = 0, nb = 0;
        b->err[0] = 0;
        b->nerr = 0;
        const char *p = src;
        for (int idx = 0; *p; idx++) {
            const char *ls = p;
            while (*p && (*p != '\n')) {
                p++;
            }
            int ln = (int)(p - ls);
            if (*p) {
                p++;
            }
            int n = idx + 1;
            /* comment */
            for (int i = 0; i < ln; i++) {
                if (ls[i] == ';') {
                    ln = i;
                    break;
                }
            }
            const char *l = ls;
            ln = trim(&l, ln);
            if (!ln) {
                continue;
            }
            char et[160];
            sbuf_t e;
            sb_init(&e, et, sizeof(et));
            actx_t x;
            x.b = b;
            x.unk[0] = 0;
            x.nunk = 0;
            /* *= e */
            const char *org_e = 0;
            int org_n = 0;
            if (l[0] == '*') {
                const char *r = l + 1;
                int rn = trim(&r, ln - 1);
                if (rn && (r[0] == '=')) {
                    org_e = r + 1;
                    org_n = rn - 1;
                }
            }
            /* name = e */
            if (!org_e) {
                int eq = -1;
                for (int i = 0; i < ln; i++) {
                    if (l[i] == '=') {
                        eq = i;
                        break;
                    }
                }
                if (eq > 0) {
                    const char *nm = l;
                    int nn = trim(&nm, eq);
                    int ok = nn > 0;
                    for (int i = 0; ok && (i < nn); i++) {
                        char c = nm[i];
                        ok = (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_';
                    }
                    if (ok) {
                        int32_t v;
                        if (!eval_expr(l + eq + 1, ln - eq - 1, pc, CTX_OTHER, &x, &v, &e)) {
                            line_err(b, n, sb_str(&e));
                        } else if (!x.nunk) {
                            set_label(b, nm, nn, (uint16_t)v);
                        } else if (pass == 2) {
                            sb_putc(&e, '\'');
                            sb_putn(&e, nm, nn);
                            sb_put(&e, "' uses an undefined label: ");
                            sb_put(&e, x.unk);
                            line_err(b, n, sb_str(&e));
                        }
                        continue;
                    }
                }
            }
            /* label: */
            const char *rest = l;
            int rn = ln;
            if (!org_e) {
                for (int i = 0; i < ln; i++) {
                    if (l[i] == ':') {
                        const char *nm = l;
                        int nn = trim(&nm, i);
                        int ok = nn > 0;
                        for (int k = 0; ok && (k < nn); k++) {
                            char c = nm[k];
                            ok = (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_';
                        }
                        if (ok) {
                            if ((pass == 1) && (find_label(b, nm, nn) >= 0)) {
                                sb_put(&e, "label '");
                                sb_putn(&e, nm, nn);
                                sb_put(&e, "' defined twice");
                                line_err(b, n, sb_str(&e));
                                sb_init(&e, et, sizeof(et));
                            }
                            set_label(b, nm, nn, pc);
                            rest = l + i + 1;
                            rn = trim(&rest, ln - i - 1);
                        }
                        break;
                    }
                }
                if (!rn) {
                    continue;
                }
            }
            /* head and tail */
            char head[16];
            int hn = 0;
            while ((hn < rn) && (rest[hn] != ' ') && (rest[hn] != '\t') && (hn < 15)) {
                head[hn] = m_lower(rest[hn]);
                hn++;
            }
            head[hn] = 0;
            const char *tail = rest + hn;
            int tn = trim(&tail, rn - hn);
            char tbuf[96];
            if (tn > 95) {
                tn = 95;
            }
            memcpy(tbuf, tail, (size_t)tn);
            tbuf[tn] = 0;
            int is_org = org_e || m_streq(head, ".org");
            if (is_org) {
                if (!org_e) {
                    org_e = tbuf;
                    org_n = tn;
                }
                if (emitted) {
                    line_err(b, n, "a second *= would start a second segment \xe2\x80\x94 one block is one contiguous patch");
                    continue;
                }
                int32_t v;
                if (!eval_expr(org_e, org_n, pc, CTX_OTHER, &x, &v, &e)) {
                    line_err(b, n, sb_str(&e));
                } else if (x.nunk || (v < 0) || (v > 0xFFFF)) {
                    sb_put(&e, "*= needs a value known where it stands: '");
                    const char *oe = org_e;
                    sb_putn(&e, oe, trim(&oe, org_n));
                    sb_putc(&e, '\'');
                    line_err(b, n, sb_str(&e));
                } else {
                    pc = (uint16_t)v;
                    org = pc;
                }
                continue;
            }
            int is_byte = m_streq(head, ".byte") || m_streq(head, ".by") || m_streq(head, ".db");
            int is_word = m_streq(head, ".word") || m_streq(head, ".wo") || m_streq(head, ".dw");
            if (head[0] == '.' && !is_byte && !is_word) {
                sb_put(&e, "unsupported directive '");
                sb_putn(&e, rest, hn);
                sb_put(&e, "' \xe2\x80\x94 this is not a build system");
                line_err(b, n, sb_str(&e));
                continue;
            }
            if (is_byte || is_word) {
                /* items, comma separated; an empty one is an error */
                int empty = 0;
                for (int s0 = 0, i = 0; i <= tn; i++) {
                    if ((i == tn) || (tbuf[i] == ',')) {
                        const char *it = tbuf + s0;
                        empty |= trim(&it, i - s0) == 0;
                        s0 = i + 1;
                    }
                }
                if (empty) {
                    line_err(b, n, "empty item in a data list");
                    continue;
                }
                for (int s0 = 0, i = 0; i <= tn; i++) {
                    if ((i != tn) && (tbuf[i] != ',')) {
                        continue;
                    }
                    int32_t v;
                    sbuf_t ie;
                    char it[96];
                    sb_init(&ie, it, sizeof(it));
                    if (!eval_expr(tbuf + s0, i - s0, pc, is_byte ? CTX_IMM : CTX_OTHER, &x, &v, &ie)) {
                        line_err(b, n, sb_str(&ie));
                    } else if (is_byte) {
                        if ((pass == 2) && ((v < 0) || (v > 0xFF))) {
                            sb_put(&ie, ".byte value ");
                            if (v < 0) {
                                sb_putc(&ie, '-');
                                sb_dec(&ie, (uint64_t)-v);
                            } else {
                                sb_dec(&ie, (uint64_t)v);
                            }
                            sb_put(&ie, " overflows a byte");
                            line_err(b, n, sb_str(&ie));
                        } else {
                            put_byte(b, &nb, (uint8_t)v);
                        }
                    } else {
                        put_byte(b, &nb, (uint8_t)v);
                        put_byte(b, &nb, (uint8_t)(v >> 8));
                    }
                    pc = (uint16_t)(pc + (is_byte ? 1 : 2));
                    s0 = i + 1;
                }
                emitted = 1;
            } else {
                char res[64];
                int wide = (idx < 512) && wide_lines[idx];
                if (!resolve_operand(head, tbuf, pc, wide, &x, res, &e)) {
                    line_err(b, n, sb_str(&e));
                    continue;
                }
                if ((pass == 1) && x.nunk && (idx < 512)) {
                    wide_lines[idx] = 1;
                }
                char text[96];
                sbuf_t ts;
                sb_init(&ts, text, sizeof(text));
                sb_put(&ts, head);
                if (res[0]) {
                    sb_putc(&ts, ' ');
                    sb_put(&ts, res);
                }
                uint8_t ib[3];
                char ae[128];
                sbuf_t as;
                sb_init(&as, ae, sizeof(ae));
                int sz = asm6502_line(sb_str(&ts), pc, ib, &as);
                if (sz) {
                    for (int i = 0; i < sz; i++) {
                        put_byte(b, &nb, ib[i]);
                    }
                    pc = (uint16_t)(pc + sz);
                } else if (pass == 2) {
                    line_err(b, n, sb_str(&as));
                } else {
                    pc = (uint16_t)(pc + 3);
                }
                emitted = 1;
            }
            if ((pass == 2) && x.nunk) {
                char ue[128];
                sbuf_t us;
                sb_init(&us, ue, sizeof(ue));
                sb_put(&us, "undefined label: ");
                sb_put(&us, x.unk);
                line_err(b, n, sb_str(&us));
            }
        }
        b->origin = org;
        if (pass == 2) {
            if (nb > ASM_BYTES) {
                line_err(b, 0, "the block is larger than 4096 bytes");
            }
            b->nbytes = b->nerr ? 0 : nb;
        } else if (b->nerr) {
            return;
        }
    }
}
