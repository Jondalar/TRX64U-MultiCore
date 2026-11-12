; Game port: the monitor's PREAD loop (11 cycles an iteration: LDA abs,X 4 + BPL 2 + INY 2 + BNE 3) on the four paddles, and the buttons.
; LOG + 0..3 the paddle values, LOG + 4 / 5 the buttons ($C061 / $C062), LOG + 8 a counter that tells a new round has been stored.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        stz LOG+8
round:  ldx #0
next:   jsr pread
        tya
        sta LOG,x
        inx
        cpx #4
        bne next
        lda BUTN0
        sta LOG+4
        lda BUTN1
        sta LOG+5
        inc LOG+8
        bra round
pread:  lda PTRIG
        ldy #0
        nop
        nop
pr2:    lda PDL0,x
        bpl pr3
        iny
        bne pr2
        dey
pr3:    rts
        * = $FFFA
        .word reset, reset, reset
