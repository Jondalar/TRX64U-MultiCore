; Anti-tearing: hires page 1 is all white ($7F), page 2 all black; at every vertical blank the program shows the other page, so frame n is
; white and frame n + 1 black. $C019 bit 7 is low in the vertical blank.
        .include "apple.inc"
VBLBIT  = $C019
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTCLR
        lda MIXCLR
        lda HIRES
        lda AN3_OFF             ; AN3 set: lores and hires shift at their own rate (apple.inc)
        lda COL80_OFF
        lda STORE80_OFF
        lda PAGE1
        lda #$7F                ; page 1 white, page 2 black (unrolled, about 40 ms each)
        ldy #0
wh:     .for pg = $20, pg < $40, pg += 1
        sta pg*$100,y
        .next
        iny
        bne wh
        lda #$00
bl:     .for pg = $40, pg < $60, pg += 1
        sta pg*$100,y
        .next
        iny
        bne bl
        stz LOG
loop:   lda VBLBIT              ; wait for the vertical blank ($C019 bit 7 is low in it)
        bmi loop
        lda LOG                 ; flip, in the blank
        eor #1
        sta LOG
        beq p1
        lda PAGE2
        bra v
p1:     lda PAGE1
v:      lda VBLBIT              ; and wait for the blank to be over
        bpl v
        bra loop
        * = $FFFA
        .word reset, reset, reset
