; T25 SID-UPSTREAM / #951, #756, #352: OSC3 pipeline delay (SID model detection).
; Part 1 is detectSidModel of the SID player (sidcrt/player/advanced/detection.asm) as it is:
; test bit set, then sawtooth, read OSC3 twice. Real 6581: $01 then $03; real 8580: $00 then $03.
;   $C000 = first read, $C001 = second read.
; Part 2 (information): for each single waveform 1/2/4 the first three OSC3 reads right after the
; test bit is released, 3 bytes each: $C010 triangle, $C020 saw, $C030 pulse (width $800).
; Done markers $5A at $C3FE and $A5 at $C3FF.
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
        lda #$ff           ; make sure the check is not done on a bad line
-       cmp $d012
        bne -
        lda #$48           ; test bit should be set
        sta SID+18
        sta SID+15
        lsr                ; activate sawtooth waveform
        sta SID+18
        lda SID+27
        sta $c000
        lda SID+27
        sta $c001

        lda #$08
        sta SID+18
        lda #$00
        sta SID+14
        sta SID+15
        lda #$08
        sta SID+17         ; pulse width $800
        lda #$ff
        sta SID+15         ; frequency high
        ldx #0
        ldy #$10           ; triangle
        jsr probe
        ldy #$20           ; saw
        jsr probe
        ldy #$40           ; pulse
        jsr probe
        lda #0
        sta SID+18
        sta SID+15
        lda #$5a
        sta DONE2
        lda #$a5
        sta DONE
        cli
        rts

; X = output offset (advances by $10), Y = waveform bit. Test bit, then the waveform, then 3 reads.
probe   lda #$08
        sta SID+18
        tya
        sta SID+18
        lda SID+27
        sta $c010,x
        lda SID+27
        sta $c011,x
        lda SID+27
        sta $c012,x
        txa
        clc
        adc #$10
        tax
        rts
