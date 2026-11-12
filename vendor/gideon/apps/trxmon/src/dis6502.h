/*
 * dis6502.h — TRX64's disassembly line (trx64-static disasm6502.rs
 * disasm_line_ts): `$addr  bb bb bb  MNEMONIC ops`, bytes padded to 8,
 * mnemonic upper case, operand hex lower case, illegal opcodes by TRX64's
 * names, JAM holes as `JAM`.
 */
#ifndef TRXMON_DIS6502_H
#define TRXMON_DIS6502_H

#include "mlib.h"

typedef uint8_t (*dis_read_t)(void *ctx, uint16_t addr);

int dis_len(uint8_t opcode);
/* Appends one line for the instruction at addr; returns its size. With
 * marks, the address and the operand address carry span marks (spans.h)
 * read through `lens`. */
/* The raw bytes column of a line: on (TRX64), or off for the 40-column screen */
void dis_set_raw(int on);
/* A label in front of the address (the 80 column screen's ".C:c005"): the line starts with `prefix`
 * and the address has no '$'. 0: TRX64's "$c005". */
void dis_set_label(const char *prefix);
int dis_line(dis_read_t rd, void *ctx, uint16_t addr, sbuf_t *out, int marks, const char *lens);

#endif
