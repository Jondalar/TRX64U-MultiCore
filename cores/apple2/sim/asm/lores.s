; Lores page 1: 16 colour blocks of two columns (colour n at columns 2 n and 2 n + 1, both halves of the cell), the rest black.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTCLR
        lda MIXCLR
        lda PAGE1
        lda LORES
        lda AN3_OFF             ; AN3 set: lores and hires shift at their own rate (apple.inc)
        lda COL80_OFF
        lda STORE80_OFF
        ldx #0              ; row
row:    lda rowlo,x
        sta PTR
        lda rowhi,x
        sta PTR+1
        ldy #0
col:    cpy #32
        bcs black
        tya
        lsr a               ; colour = column / 2
        sta PTR2
        asl a
        asl a
        asl a
        asl a
        ora PTR2
        bra put
black:  lda #0
put:    sta (PTR),y
        iny
        cpy #40
        bne col
        inx
        cpx #24
        bne row
halt:   bra halt
        .include "tables.inc"
        * = $FFFA
        .word reset, reset, reset
