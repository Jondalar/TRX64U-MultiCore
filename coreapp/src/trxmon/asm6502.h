/*
 * asm6502.h — TRX64's one-line 6502 assembler (the monitor's `a`).
 */
#ifndef TRXMON_ASM6502_H
#define TRXMON_ASM6502_H

#include "mlib.h"

/* Assembles one instruction (no address prefix) for address pc into bytes
 * (up to 3). Returns the size, or 0 with TRX64's reason in e. */
int asm6502_line(const char *text, uint16_t pc, uint8_t *bytes, sbuf_t *e);

/* TRX64's block assembler (assemble_block, Spec 809 §5): labels, `name = e`,
 * `*`, `<`/`>`, `+`/`-`, .byte/.word, one `*=`/.org before the first byte,
 * comments after `;`. Two passes; a label used before it is defined sizes its
 * line wide. bytes are kept only when no line has an error. */
#define ASM_LABELS 64
#define ASM_BYTES  4096
typedef struct
{
    uint16_t origin;
    int nbytes;
    uint8_t bytes[ASM_BYTES];
    struct
    {
        char name[24];
        uint16_t v;
    } lab[ASM_LABELS];
    int nlab;
    int nerr;
    char err[1024]; /* "line N: why" lines */
} asm_block_t;

void asm6502_block(const char *src, uint16_t origin, asm_block_t *b);

#endif
