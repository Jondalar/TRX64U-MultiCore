; A machine that is never idle: the text page filled with a pattern, then forever a 24 bit counter in main RAM, the speaker toggled by the counter's
; bit 6, the keyboard strobe polled. (Freeze compares two such machines.)
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTSET
        lda MIXCLR
        lda PAGE1
        lda COL80_OFF
        stz PTR
        lda #4
        sta PTR+1
page:   ldy #0
fill:   tya
        eor PTR+1
        sta (PTR),y
        iny
        bne fill
        inc PTR+1
        lda PTR+1
        cmp #8
        bne page
        stz LOG
        stz LOG+1
        stz LOG+2
loop:   inc LOG
        bne chk
        inc LOG+1
        bne chk
        inc LOG+2
chk:    lda LOG
        and #$40
        beq nospk
        lda SPKR
nospk:  lda KBD
        bpl loop
        lda KBDSTRB
        bra loop
        * = $FFFA
        .word reset, reset, reset
