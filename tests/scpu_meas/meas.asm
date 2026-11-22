; T241 S4: one board run that tells lost IRQs, lost TI increments, an NTSC latch,
; a slow clock and a fast CPU apart. 64tass. RUN, film the screen for about 60 s, photograph the end.
;
; First line: RMW = does a read-modify-write on $D019 acknowledge the raster flag (0 = yes, as on a C64;
; 1 = no: the CPU did not write the unmodified byte in the modify cycle, T241 cause of 2026-11-20).
; ASL and INC, at software 1 MHz and at the speed switch's speed (SW).
;
; At software 1 MHz ($D07A), once in the mirror mode the machine has (A),
; once after "registers on; $D077; registers off" (B, Master's Boom's mode).
; Each mode gets two windows, display on (A1, B1) and display off (A0, B0: no badlines).
; Each window lasts N raster IRQs (line $F8). Over it the program counts:
;   FR  raster IRQs (= N, the window)
;   UF  CIA1 timer A underflows (CIA1 timer B counts them)
;   IQ  CIA IRQ handler entries ($0314 chain)
;   TI  jiffy clock delta ($A0-$A2)
;   PH  PHI2 cycles (CIA2 timer A free-running, timer B counts its underflows)
;   IT  loop iterations of the workload (ZP, ROM reads, RAM writes, JSR/RTS)
; Header: OP = $D0B4 (199 = $C7 reset mode, 192 = $C0 mirror everything),
;         NT = $02A6 (1 PAL, 0 NTSC), LA = CIA1 timer A latch high byte (40 PAL, 42 NTSC).
; A C64 (PAL, c64re runtime, 2026-11-20) gives: RMW ASL 0 INC 0  SW ASL 0 INC 0, OP FF NT 01 LA 40, FR 01F4, UF 0256-0257, IQ = UF, TI = UF,
; PH 0095F68D-0095F696 (FR x 19656 = 0095F6A0 less the IRQ sync), IT about 02B6F9 in both windows.

N        = 500
cnt      = $f7          ; 3 bytes, RS-232 pointers (unused here)
frames   = $fb          ; 2 bytes
ciairq   = $fd          ; 2 bytes
chrout   = $ffd2
buf      = $cc00

        * = $0801
        .word (+), 2026
        .null $9e, format("%d", start)
+       .word 0

start   lda #$93                ; clear the screen: results on rows 0-15, the write test on rows 16-24
        jsr chrout
        sei
        sta $d07a               ; software 1 MHz (address-triggered)
        cli
        lda #<t_hdr
        ldy #>t_hdr
        jsr puts
        jsr rmwline
        jsr header
        lda #'A'
        ldx #1
        jsr measure
        lda #'A'
        ldx #0
        jsr measure
        sei                     ; Master's mode: registers on, mirror everything, registers off
        sta $d07e
        sta $d077
        sta $d07f
        cli
        jsr header
        lda #'B'
        ldx #1
        jsr measure
        lda #'B'
        ldx #0
        jsr measure
        lda #'B'                ; the write test in Master's mode, then in the reset mode
        jsr wtest
        sei
        sta $d07e
        lda #$c7
        sta $d0b3
        sta $d07f
        cli
        lda #'A'
        jsr wtest
        sei                     ; leave the timers and the vector as the KERNAL had them
        lda #0
        sta $d01a
        lda #$0f
        sta $d019
        lda #<$ea31
        sta $0314
        lda #>$ea31
        sta $0315
        lda #$08
        sta $dc0f
        sta $dd0e
        sta $dd0f
        cli
        rts

; ---- W: C64-RAM writes at the switch's speed, as the VIC shows them (film it) ---------------------------
; Rows 16-24 are written twice per frame, blank ($20) then solid ($A0), white. With no write lost they stay solid
; and steady; a lost second write is a blue hole for one frame. 250 frames with the fill in the lower border
; (from line 251), then 250 frames with the fill over the badlines of rows 0-3 (from line 60).
; The line "W<mode> SP xx" starts each test: SP = $D0B9 (bit 6 set: 1 MHz in effect; clear: 20 MHz).
wtest   sta wlab
        lda #'W'
        jsr chrout
        lda wlab
        jsr chrout
        lda #<t_sp
        ldy #>t_sp
        jsr puts
        sei
        sta $d07b               ; software 1 MHz off: the speed switch decides
        lda $d0b9
        cli
        jsr hex8
        lda #13
        jsr chrout
        sei
        ldx #179                ; colour RAM of rows 16-24: white
        lda #1
-       sta $da80,x
        sta $db34,x
        dex
        cpx #$ff
        bne -
        ldy #250
-       lda #251
-       cmp $d012
        bne -
        jsr fill
        dey
        bne --
        ldy #250
-       lda #60
-       cmp $d012
        bne -
        jsr fill
        dey
        bne --
        cli
        rts

fill    ldx #179
-       lda #$20
        sta $0680,x
        sta $0734,x
        lda #$a0
        sta $0680,x
        sta $0734,x
        dex
        cpx #$ff
        bne -
        rts

; ---- RMW: read-modify-write on $D019 against the raster flag --------------------------------------
rmwline sei
        lda $d011               ; raster compare at line $F8, which PAL and NTSC both have (the KERNAL leaves
        and #$7f                ; 311 there: no such line on NTSC, the flag would never come)
        sta $d011
        lda #$f8
        sta $d012
        jsr rmwa
        sta r_a1
        jsr rmwi
        sta r_i1
        sta $d07b               ; software 1 MHz off: the speed switch decides
        jsr rmwa
        sta r_a2
        jsr rmwi
        sta r_i2
        sta $d07a               ; software 1 MHz on again
        cli
        lda #<t_rmw
        ldy #>t_rmw
        jsr puts
        lda r_a1
        jsr hex4
        lda #<t_inc
        ldy #>t_inc
        jsr puts
        lda r_i1
        jsr hex4
        lda #<t_sw
        ldy #>t_sw
        jsr puts
        lda r_a2
        jsr hex4
        lda #<t_inc
        ldy #>t_inc
        jsr puts
        lda r_i2
        jsr hex4
        lda #13
        jmp chrout

rmwa    jsr wflag               ; A = raster flag after ASL $D019 (0 = acknowledged)
        asl $d019
        jmp rflag
rmwi    jsr wflag               ; A = raster flag after INC $D019
        inc $d019
rflag   lda $d019
        and #1
        rts
wflag   lda #$ff                ; clear every VIC flag, then wait for the raster compare to set bit 0
        sta $d019
-       lda $d019
        and #1
        beq -
        rts

; ---- header: OP, NT, LA ----------------------------------------------------------------------
header  lda #<t_op
        ldy #>t_op
        jsr puts
        lda $d0b4
        jsr hex8
        lda #<t_nt
        ldy #>t_nt
        jsr puts
        lda $02a6
        jsr hex8
        lda #<t_la
        ldy #>t_la
        jsr puts
        sei                     ; highest timer A high byte over about 4 periods; IRQs off, or the
        lda #0                  ; IRQ handler takes exactly the cycles after the reload ($40xx lasts 38)
        sta maxla
        ldx #0
        ldy #16
-       lda $dc05
        cmp maxla
        bcc +
        sta maxla
+       dex
        bne -
        dey
        bne -
        cli
        lda maxla
        jsr hex8
        lda #13
        jmp chrout

; ---- one window -------------------------------------------------------------------------------
measure sta mode
        stx screen
        sei
        lda #<irq
        sta $0314
        lda #>irq
        sta $0315
        lda $d011
        and #$6f                ; raster bit 8 clear; display off (no badlines) unless the window wants it on
        ldx screen
        beq +
        ora #$10
+       sta $d011
        lda #$f8
        sta $d012
        lda #$0f
        sta $d019
        lda #$01
        sta $d01a
        lda #$ff                ; CIA1 timer B counts timer A underflows
        sta $dc06
        sta $dc07
        lda #$51
        sta $dc0f
        lda #$ff                ; CIA2: timer A counts PHI2, timer B its underflows
        sta $dd04
        sta $dd05
        sta $dd06
        sta $dd07
        lda #$11
        sta $dd0e
        lda #$51
        sta $dd0f
        cli
        lda frames              ; start on a raster IRQ
-       cmp frames
        beq -
        sei
        lda #0
        sta frames
        sta frames+1
        sta ciairq
        sta ciairq+1
        sta cnt
        sta cnt+1
        sta cnt+2
        jsr snap
        ldx #9
-       lda s_now,x
        sta s_beg,x
        dex
        bpl -
        cli
        ldy #0
loop    inc cnt
        bne l1
        inc cnt+1
        bne l1
        inc cnt+2
l1      lda $a000,y             ; BASIC ROM
        sta buf,y
        eor $e000,y             ; KERNAL ROM
        sta buf+$100,y
        iny
        jsr sub
        lda frames+1
        cmp #>N
        bcc loop
        lda frames
        cmp #<N
        bcc loop
        sei
        jsr snap
        lda $d011               ; display on again for the printout
        ora #$10
        sta $d011
        cli
        ; print: mode, display (1 on, 0 off), FR UF IQ TI PH IT
        lda mode
        jsr chrout
        lda screen
        ora #'0'
        jsr chrout
        lda #<t_fr
        ldy #>t_fr
        jsr puts
        lda frames+1
        jsr hex8
        lda frames
        jsr hex8
        lda #<t_uf              ; CIA1 timer B counts down
        ldy #>t_uf
        jsr puts
        sec
        lda s_beg+3
        sbc s_now+3
        tax
        lda s_beg+4
        sbc s_now+4
        jsr hex8
        txa
        jsr hex8
        lda #<t_iq
        ldy #>t_iq
        jsr puts
        lda ciairq+1
        jsr hex8
        lda ciairq
        jsr hex8
        lda #<t_ti              ; TI counts up, $A0 high
        ldy #>t_ti
        jsr puts
        sec
        lda s_now+2
        sbc s_beg+2
        sta d0
        lda s_now+1
        sbc s_beg+1
        sta d1
        lda s_now+0
        sbc s_beg+0
        jsr hex8
        lda d1
        jsr hex8
        lda d0
        jsr hex8
        lda #13
        jsr chrout
        lda #<t_ph              ; CIA2 32 bits count down: begin - now
        ldy #>t_ph
        jsr puts
        sec
        lda s_beg+5
        sbc s_now+5
        sta d0
        lda s_beg+6
        sbc s_now+6
        sta d1
        lda s_beg+7
        sbc s_now+7
        sta d2
        lda s_beg+8
        sbc s_now+8
        jsr hex8
        lda d2
        jsr hex8
        lda d1
        jsr hex8
        lda d0
        jsr hex8
        lda #<t_it
        ldy #>t_it
        jsr puts
        lda cnt+2
        jsr hex8
        lda cnt+1
        jsr hex8
        lda cnt
        jsr hex8
        lda #13
        jmp chrout

sub     rts

; s_now: +0..2 TI ($A0,$A1,$A2), +3/+4 CIA1 TB lo/hi, +5..8 CIA2 TA lo, TA hi, TB lo, TB hi (called under SEI)
snap    lda $a0
        sta s_now
        lda $a1
        sta s_now+1
        lda $a2
        sta s_now+2
-       lda $dc07
        ldx $dc06
        cmp $dc07
        bne -
        stx s_now+3
        sta s_now+4
-       lda $dd07               ; timer B high, low; timer A high, low; timer B again
        sta s_now+8
        lda $dd06
        sta s_now+7
        lda $dd05
        sta s_now+6
        lda $dd04
        sta s_now+5
        lda $dd06
        cmp s_now+7
        bne -
        rts

; ---- the IRQ: raster IRQs counted and acknowledged here, CIA IRQs counted and passed on ------------
irq     lda $d019
        bpl notvic
        sta $d019
        inc frames
        bne +
        inc frames+1
+       jmp $ea81               ; KERNAL: PLA, TAY, PLA, TAX, PLA, RTI (a pending CIA IRQ comes again at once)
notvic  inc ciairq
        bne +
        inc ciairq+1
+       jmp $ea31

; ---- text output ------------------------------------------------------------------------------
puts    sta $22
        sty $23
        ldy #0
-       lda ($22),y
        beq +
        jsr chrout
        iny
        bne -
+       rts

hex8    pha
        lsr
        lsr
        lsr
        lsr
        jsr hex4
        pla
        and #$0f
hex4    cmp #10
        bcc +
        adc #6
+       adc #'0'
        jmp chrout

t_hdr   .null 13, "T241 S4 MEASUREMENT", 13
t_op    .null "OP "
t_nt    .null " NT "
t_la    .null " LA "
t_fr    .null " FR "
t_uf    .null " UF "
t_iq    .null " IQ "
t_ti    .null " TI "
t_ph    .null "  PH "
t_it    .null " IT "
t_sp    .null " SP "
t_rmw   .null "RMW ASL "
t_inc   .null " INC "
t_sw    .null "  SW ASL "

mode    .byte 0
wlab    .byte 0
r_a1    .byte 0
r_i1    .byte 0
r_a2    .byte 0
r_i2    .byte 0
screen  .byte 0
maxla   .byte 0
d0      .byte 0
d1      .byte 0
d2      .byte 0
s_beg   .fill 10
s_now   .fill 10
