; Aux RAM traffic for the stall measurement: zero page and stack in aux (ALTZP), a loop of increments, a subroutine call and a table walk through the aux text page
; (80STORE + PAGE2); with COL80 = 1 the 80 column text screen reads aux every machine cycle as well.
        .include "apple.inc"
        .weak
COL80   = 0
        .endweak
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTSET
        lda MIXCLR
        lda PAGE1
.if COL80
        sta COL80_ON
.else
        sta COL80_OFF
.endif
        sta STORE80_ON
        lda PAGE2               ; $0400-$07FF in aux
        sta ALTZP
        ldy #0
loop:   inc $10                 ; zero page in aux
        jsr sub                 ; stack in aux
        lda $0400,y             ; the aux text page
        eor $10
        sta $0400,y
        iny
        bne loop
        inc $11
        bra loop
sub:    inc $12
        rts
        * = $FFFA
        .word reset, reset, reset
