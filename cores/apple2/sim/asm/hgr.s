; Hires page 1 with five bars of artifact colour, 8 bytes each: green, violet, orange, blue, white ($2A/$55, $55/$2A, $AA/$D5, $D5/$AA, $7F).
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTCLR
        lda MIXCLR
        lda PAGE1
        lda HIRES
        lda AN3_OFF             ; AN3 set: lores and hires shift at their own rate (apple.inc)
        lda COL80_OFF
        lda STORE80_OFF
        ldy #0
copy:   lda bars,y              ; the same 256 bytes in each of the 32 pages of the screen, unrolled (about 40 ms)
        .for pg = $20, pg < $40, pg += 1
        sta pg*$100,y
        .next
        iny
        bne copy
halt:   bra halt
        .include "tables.inc"
        * = $FFFA
        .word reset, reset, reset
