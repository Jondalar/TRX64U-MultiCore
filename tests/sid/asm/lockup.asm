; T25 SID-UPSTREAM / #954: noise lock-up. Noise, then noise+pulse with the pulse always low
; (pulse width $FFF), then pure noise again (OSC3 must read $00 = register cleared), then the
; test bit for about 3.3 s (register refills slowly). Layout as the issue's program:
; $C000: 64 OSC3 reads before, $C100: 256 reads after the combined phase,
; $C200: 64 reads after the test bit. Done markers $5A at $C3FE and $A5 at $C3FF.
        .cpu "6502"
SID     = $d400
DONE    = $c3ff
DONE2   = $c3fe
        * = $0801
        .word (+), 10
        .null $9e, format("%d", start)
+       .word 0

        * = $1000
start   sei
        lda #$ff
-       cmp $d012
        bne -
        lda #$0b
        sta $d011          ; no badlines from the next frame on
        lda #0
        sta $d015
        lda #$00
        sta SID+14
        lda #$10
        sta SID+15         ; freq $1000: a shift every 256 cycles
        lda #$ff
        sta SID+16
        lda #$0f
        sta SID+17         ; pulse width $FFF
        lda #$80
        sta SID+18         ; noise
        ldx #0
-       jsr sample
        sta $c000,x
        inx
        cpx #64
        bne -
        lda #$c0
        sta SID+18         ; noise + pulse
        jsr wait           ; ~330000 cycles, ~1300 shifts
        lda #$80
        sta SID+18         ; noise alone
        ldx #0
-       jsr sample
        sta $c100,x
        inx
        bne -
        lda #$88
        sta SID+18         ; test bit on
        ldy #10
-       jsr wait
        dey
        bne -
        lda #$80
        sta SID+18         ; test bit off
        ldx #0
-       jsr sample
        sta $c200,x
        inx
        cpx #64
        bne -
        lda #0
        sta SID+18
        lda #$1b
        sta $d011
        lda #$5a
        sta DONE2
        lda #$a5
        sta DONE
        cli
        rts

sample  txa                ; read OSC3 about once per shift
        pha
        ldx #50
-       dex
        bne -
        pla
        tax
        lda SID+27
        rts

wait    txa
        pha
        tya
        pha
        ldx #0
        ldy #0
-       dey
        bne -
        dex
        bne -
        pla
        tay
        pla
        tax
        rts
