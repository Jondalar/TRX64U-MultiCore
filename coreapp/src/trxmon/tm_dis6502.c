/*
 * dis6502.c — see dis6502.h. The opcode table is generated from TRX64's.
 */
#include "dis6502.h"
#include "spans.h"

#include "dis6502_table.h"

int dis_len(uint8_t opcode)
{
    switch (dis_ops[opcode].mode) {
    case M_IMP:
    case M_ACC: return 1;
    case M_ABS:
    case M_ABSX:
    case M_ABSY:
    case M_IND: return 3;
    default: return 2;
    }
}

static int s_raw = 1;
static const char *s_label;

void dis_set_raw(int on) { s_raw = on; }
void dis_set_label(const char *prefix) { s_label = prefix; }

int dis_line(dis_read_t rd, void *ctx, uint16_t addr, sbuf_t *out, int marks, const char *lens)
{
    uint8_t op = rd(ctx, addr);
    int mode = dis_ops[op].mode;
    int size = dis_len(op);
    uint8_t b1 = rd(ctx, (uint16_t)(addr + 1));
    uint8_t b2 = rd(ctx, (uint16_t)(addr + 2));
    uint16_t abs = (uint16_t)(b1 | (b2 << 8));

    /* $addr */
    char t[8];
    sbuf_t s;
    sb_init(&s, t, sizeof(t));
    /* with a label the address is "C:c005" after a one character mark, not "$c005" */
    int skip = 0;
    if (s_label) {
        sb_put(out, s_label);
        skip = 1;
    } else {
        sb_putc(&s, '$');
    }
    sb_hex(&s, addr, 4);
    if (marks) {
        span_mark(out, t, 5 - skip, addr, ROLE_PC, lens, 1);
    } else {
        sb_putn(out, t, 5 - skip);
    }
    sb_put(out, "  ");
    if (s_raw) {
        for (int i = 0; i < 3; i++) {
            if (i < size) {
                if (i) {
                    sb_putc(out, ' ');
                }
                sb_hex(out, rd(ctx, (uint16_t)(addr + i)), 2);
            } else {
                sb_put(out, i ? "   " : "  ");
            }
        }
        /* "bb bb bb" is 8; one byte leaves 6 spaces, two leave 3 (padEnd(8)) */
        sb_put(out, "  ");
    }
    sb_put(out, dis_names[dis_ops[op].name]);

    /* operand: (prefix, address text, suffix) */
    const char *pre = "", *suf = "";
    int digits = 0;
    uint16_t val = 0;
    int role = ROLE_OPERAND;
    switch (mode) {
    case M_IMP:
    case M_ACC: return size;
    case M_IMM:
        sb_put(out, " #$");
        sb_hex(out, b1, 2);
        return size;
    case M_ZP: digits = 2; val = b1; break;
    case M_ZPX: digits = 2; val = b1; suf = ",x"; break;
    case M_ZPY: digits = 2; val = b1; suf = ",y"; break;
    case M_ABS:
        digits = 4;
        val = abs;
        if ((op == 0x20) || (op == 0x4C)) {
            role = ROLE_TARGET;
        }
        break;
    case M_ABSX: digits = 4; val = abs; suf = ",x"; break;
    case M_ABSY: digits = 4; val = abs; suf = ",y"; break;
    case M_IND: digits = 4; val = abs; pre = "("; suf = ")"; break;
    case M_INDX: digits = 2; val = b1; pre = "("; suf = ",x)"; break;
    case M_INDY: digits = 2; val = b1; pre = "("; suf = "),y"; break;
    case M_REL:
        digits = 4;
        val = (uint16_t)(addr + size + (int8_t)b1);
        role = ROLE_TARGET;
        break;
    }
    sb_putc(out, ' ');
    sb_put(out, pre);
    sb_init(&s, t, sizeof(t));
    sb_putc(&s, '$');
    sb_hex(&s, val, digits);
    if (marks) {
        span_mark(out, t, digits + 1, val, role, lens, 1);
    } else {
        sb_putn(out, t, digits + 1);
    }
    sb_put(out, suf);
    return size;
}
