; T25 SID-UPSTREAM / GideonZ/1541ultimate#954: sample SID voice 3 noise (OSC3) once per LFSR shift,
; 256 samples to $C000. Program text after the issue's test program, with the screen blanked only
; AFTER the last badline of a frame (DEN is read at raster line $30), so no badline steals cycles.
; freq $1000: accumulator bit 19 rises every 2^20/$1000 = 256 cycles. Loop body is exactly 256 cycles.
; Done markers $5A at $C3FE and $A5 at $C3FF (the board script clears it first).
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
-       cmp $d012          ; last badline of this frame is over at raster $ff
        bne -
        lda #$0b
        sta $d011          ; DEN off from the next frame: no badlines
        lda #0
        sta $d015          ; no sprites
        lda #$08
        sta SID+18         ; test bit: reset accumulator
        lda #$00
        sta SID+14
        lda #$10
        sta SID+15         ; freq $1000
        lda #$80
        sta SID+18         ; noise, gate off, test released
        ldx #0
        jmp loop
        .align 256
loop    lda SID+27         ; 4
        sta $c000,x        ; 5
        ldy #47            ; 2
d       dey                ; 2
        bne d              ; 3/2  -> 5*47-1 = 234
        nop                ; 2
        nop                ; 2
        nop                ; 2
        inx                ; 2
        bne loop           ; 3   total 4+5+2+234+6+2+3 = 256
        lda #0
        sta SID+18         ; voice 3 off
        lda #$1b
        sta $d011
        lda #$5a
        sta DONE2
        lda #$a5
        sta DONE
        cli
        rts
