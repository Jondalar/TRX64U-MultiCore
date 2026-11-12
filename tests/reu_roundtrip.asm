; REU round trip: fill $C100 with seed+00..seed+FF (seed in $02, set by the
; Mac so every run writes new data), stash it to REU bank 1 at $0000,
; clear $C200 and fetch the page back there. The Mac checks $C100 == $C200
; and the DDR2 at the REU's bank 1 (tests/reu_test.py).
        * = $c000

reu_cmd     = $df01
reu_c64     = $df02         ; lo, hi
reu_reu     = $df04         ; lo, hi, bank
reu_len     = $df07         ; lo, hi

        ldx #0
fill    txa
        clc
        adc $02
        sta $c100,x
        lda #0
        sta $c200,x
        inx
        bne fill

        lda #$c1            ; stash $C100 -> REU 1:0000
        ldy #%10010000      ; execute, C64 -> REU
        jsr transfer
        lda #$c2            ; fetch REU 1:0000 -> $C200
        ldy #%10010001      ; execute, REU -> C64
        jsr transfer
        rts

; A = C64 page, Y = command; 256 bytes, REU bank 1, address 0
transfer
        sta reu_c64+1
        lda #0
        sta reu_c64
        sta reu_reu
        sta reu_reu+1
        sta reu_len
        lda #1
        sta reu_reu+2
        sta reu_len+1
        sty reu_cmd
        rts
