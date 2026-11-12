; The speaker: $C030 toggled every 510 machine cycles (LDA 4 + LDX 2 + 100 x (DEX 2 + BNE 3) - 1 + NOP 2 + JMP 3 = 510).
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
loop:   lda SPKR
        ldx #100
d:      dex
        bne d
        nop
        jmp loop
        * = $FFFA
        .word reset, reset, reset
