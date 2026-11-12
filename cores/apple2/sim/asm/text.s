; Text page 1 filled with a pattern of screen codes the bench can compute (code = 5 * low byte of the address + 11 * high byte).
; The machine stays in this loop. Written for this project (TREX); no Apple ROM call.
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
        lda ALTCHAR_OFF
        lda STORE80_OFF
        lda #0
        sta PTR
        lda #4
        sta PTR+1
page:   ldy #0
byte:   tya
        asl a
        asl a
        sty PTR2
        clc
        adc PTR2            ; 5 * y
        sta PTR2
        lda PTR+1
        asl a
        sta LOGP            ; 2 * hi
        asl a
        asl a               ; 8 * hi
        clc
        adc LOGP            ; 10 * hi
        clc
        adc PTR+1           ; 11 * hi
        clc
        adc PTR2
        sta (PTR),y
        iny
        bne byte
        inc PTR+1
        lda PTR+1
        cmp #8
        bne page
halt:   bra halt
        * = $FFFA
        .word reset, reset, reset
