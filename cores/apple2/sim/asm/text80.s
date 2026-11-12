; 80 columns: main RAM text page 1 holds the pattern of text.s (code = 5 * low byte + 11 * high byte of the address), the aux text page (written with 80STORE +
; PAGE2) the same pattern xor $5A; screen column 2 k comes from aux, 2 k + 1 from main.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda TXTSET
        lda MIXCLR
        lda ALTCHAR_OFF
        sta STORE80_ON
        lda PAGE2               ; stores to $0400-$07FF go to aux
        ldx #$5A
        jsr fill
        lda PAGE1
        ldx #0
        jsr fill
        sta COL80_ON
halt:   bra halt
fill:   stx LOGP+1              ; the xor mask
        stz PTR
        lda #4
        sta PTR+1
page:   ldy #0
byte:   tya
        asl a
        asl a
        sty PTR2
        clc
        adc PTR2                ; 5 * y
        sta PTR2
        lda PTR+1
        asl a
        sta LOGP                ; 2 * hi
        asl a
        asl a                   ; 8 * hi
        clc
        adc LOGP                ; 10 * hi
        clc
        adc PTR+1               ; 11 * hi
        clc
        adc PTR2
        eor LOGP+1
        sta (PTR),y
        iny
        bne byte
        inc PTR+1
        lda PTR+1
        cmp #8
        bne page
        rts
        * = $FFFA
        .word reset, reset, reset
