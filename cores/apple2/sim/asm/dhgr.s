; Double hires: sixteen bars of 28 dots, colours 0..15 (tables from mkrom.py: dh_aux in aux RAM, dh_main in main), then black.
; 80COL + HIRES + the double hires switch ($C05E, AN3), 80STORE with PAGE2 selecting the aux page.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTCLR
        lda MIXCLR
        lda HIRES
        sta STORE80_ON
        lda PAGE2               ; the hires page at $2000 is aux
        ldx #0
        jsr fill_aux
        lda PAGE1
        ldx #0
        jsr fill_main
        sta COL80_ON
        lda AN3_ON              ; $C05E: double hires (AN3 off on the //e)
halt:   bra halt
fill_aux:
        ldy #0
ca:     lda dh_aux,y
        .for pg = $20, pg < $40, pg += 1
        sta pg*$100,y
        .next
        iny
        bne ca
        rts
fill_main:
        ldy #0
cm:     lda dh_main,y
        .for pg = $20, pg < $40, pg += 1
        sta pg*$100,y
        .next
        iny
        bne cm
        rts
        .include "tables.inc"
        * = $FFFA
        .word reset, reset, reset
