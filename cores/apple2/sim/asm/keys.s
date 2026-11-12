; Keyboard log: every key the machine delivers is stored as four bytes at LOG + 4 n (a 16 bit pointer, so there is room for a few hundred keys): the key byte
; ($C000, bit 7 = strobe), $C010 (bit 7 = any key down) read in the same breath, and the buttons $C061 / $C062 (bit 7 = Open / Closed Apple). The strobe is
; cleared by the $C010 read.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda #<LOG
        sta PTR
        lda #>LOG
        sta PTR+1
loop:   lda KBD
        bpl loop
        ldy #0
        sta (PTR),y
        lda KBDSTRB
        iny
        sta (PTR),y
        lda BUTN0
        iny
        sta (PTR),y
        lda BUTN1
        iny
        sta (PTR),y
        clc
        lda PTR
        adc #4
        sta PTR
        bcc loop
        inc PTR+1
        bra loop
        * = $FFFA
        .word reset, reset, reset
