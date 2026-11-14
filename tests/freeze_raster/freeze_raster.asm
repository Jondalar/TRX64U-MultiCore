; T12 test 7 (docs/specs/T12-freeze-overlay.md §8.2, §14.6): a raster-timed
; program for freeze/resume on the board (freeze_raster.py).
;
; Five colour splits per frame (border and background change at fixed lines)
; from a stable raster interrupt (double IRQ: the first IRQ arms the next
; line and waits in NOPs, so the second one comes with at most one cycle of
; jitter; a $D012 compare at the line's end takes that cycle out). Every split
; appends an 8-byte record to a log ring at $4000-$7FFF:
;   +0 $D012 when the record is written
;   +1 +2 CIA 1 timer A (free running, one PAL frame = 19656 cycles): the
;         cycle in the frame
;   +3 +4 the program's frame counter (one per frame, at the last split)
;   +5 +6 CIA 2 timer B, which counts CIA 2 timer A's underflows (also one
;         frame): the machine's frame counter, independent of the IRQs
;   +7    split number 0-4
; $FB/$FC: records written (wraps at 2048), $FD/$FE the frame counter.
; KERNAL and BASIC are switched out ($01 = $35): the vectors at $FFFA-$FFFF
; are this program's; the main loop is JMP to itself.

LOG     = $4000
NSPLIT  = 5
lp      = $f7           ; pointer into the log
si      = $f9           ; split number
cnt     = $fb           ; records written
frm     = $fd           ; frame counter

        * = $c000

start   sei
        lda #$35
        sta $01
        lda #$7f                ; no CIA interrupts
        sta $dc0d
        sta $dd0d
        lda $dc0d
        lda $dd0d
        lda #<nmi
        sta $fffa
        lda #>nmi
        sta $fffb
        lda #<irq1
        sta $fffe
        lda #>irq1
        sta $ffff
        lda #0
        sta cnt
        sta cnt+1
        sta frm
        sta frm+1
        sta si
        lda #<LOG
        sta lp
        lda #>LOG
        sta lp+1
        ; frame timers: CIA 2 timer B counts CIA 2 timer A, both timer A one frame
        lda #$ff
        sta $dd06
        sta $dd07
        lda #$c7                ; 19655: a period of 19656 cycles
        sta $dc04
        sta $dd04
        lda #$4c
        sta $dc05
        sta $dd05
        lda #$51                ; timer B: TA underflows, force load, start
        sta $dd0f
        lda #$11                ; timer A: continuous, force load, start
        sta $dc0e
        sta $dd0e
        ; the raster interrupt at the first split
        lda #$1b
        sta $d011
        lda lines
        sta $d012
        lda #$01
        sta $d01a
        lda #$ff
        sta $d019
        cli
loop    jmp loop

; first IRQ: on line L, jitter up to 7 cycles
irq1    pha
        txa
        pha
        tya
        pha
        lda #<irq2
        sta $fffe
        lda #>irq2
        sta $ffff
        inc $d012
        lda #$01
        sta $d019
        tsx
        cli
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop

; second IRQ: on line L + 1, jitter at most one cycle
irq2    txs                     ; the second IRQ's frame is dropped
        ldx #8
wait    dex
        bne wait
        bit $00
        lda $d012
        cmp $d012
        beq stable              ; +1 cycle while still the same line
stable  ldx si
        lda bcol,x
        sta $d020
        lda gcol,x
        sta $d021
        ; the record
        ldy #0
        lda $d012
        sta (lp),y
        iny
        lda $dc04
        sta (lp),y
        iny
        lda $dc05
        sta (lp),y
        iny
        lda frm
        sta (lp),y
        iny
        lda frm+1
        sta (lp),y
        iny
        lda $dd06
        sta (lp),y
        iny
        lda $dd07
        sta (lp),y
        iny
        txa
        sta (lp),y
        ; next record
        clc
        lda lp
        adc #8
        sta lp
        bcc +
        inc lp+1
        lda lp+1
        cmp #>(LOG + $4000)
        bne +
        lda #>LOG
        sta lp+1
+       inc cnt
        bne +
        inc cnt+1
+       lda cnt+1
        and #$07
        sta cnt+1
        ; next split; the frame counter at the last one
        inx
        cpx #NSPLIT
        bne +
        ldx #0
        inc frm
        bne +
        inc frm+1
+       stx si
        lda lines,x
        sta $d012
        lda #<irq1
        sta $fffe
        lda #>irq1
        sta $ffff
        lda #$01
        sta $d019
        pla
        tay
        pla
        tax
        pla
nmi     rti

; split lines: L and L + 1 are no bad lines ($D011 YSCROLL 3: L & 7 = 3 is one)
lines   .byte 56, 96, 136, 176, 216
bcol    .byte 2, 5, 6, 7, 14
gcol    .byte 0, 11, 12, 15, 6
