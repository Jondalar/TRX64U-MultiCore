; scpu2_ops.asm - T241.1 P2: the cycles of every documented 6502 opcode at 1 MHz, against the 6502's (64tass).
; 201 cases (ops_tab.inc, made by gen_ops.py): each opcode once, the indexed reads and (zp),Y also across a page
; (variant x), the branches not taken (n), taken (t) and taken across a page (x), ADC/SBC # in decimal mode (d).
; Each case runs once in a stub between "STA $DD0E" (CIA2 timer A starts) and "LDA $DD04"; its cycles are the empty
; stub's reading minus its own. Screen off, no sprites, no interrupt source: the count is exact on a C64.
; All cases run in mirror mode $c7 (the reset's) and again in $c0 (Master's Boom's: zero page and stack are mirrored
; too), each pass with its own empty-stub reading. Without a SuperCPU `mode` writes nothing.
; Shown: every case whose cycles differ, as  <m> <opcode><variant> <expected>><measured>  (m: 7 = $c7, 0 = $c0).
; The expected cycles are the 6502's; a stock C64 shows none different. On the 65C816 (emulation mode) a case that
; differs is the core's fault or a documented 65C816 difference; none is known for the documented 6502 opcodes.

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

STUB    = $3000                 ; the stub: STA $DD0E, the case's bytes, the tail
STUB2   = $32f9                 ; the stub of a branch across a page: STA $DD0E at $32f9, the branch at $32fc, the tail at $3300
ci      = $c040                 ; the case
tailo   = $c041                 ; the tail's offset in the stub's page
rlo     = $c042                 ; timer A low byte read by the tail
base    = $c043                 ; the empty stub's reading
savesp  = $c044
nshow   = $c045                 ; entries shown
jvec    = $c046                 ; 2: where the run jumps (STUB or STUB2)
got     = $c048
mi      = $c049                 ; the mirror mode: 0 $c7, 1 $c0
ncol    = $c04a                 ; entries in the row being written

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda issc
        beq +
        jsr spd1
+       lda #$7f                ; no interrupt source
        sta $dc0d
        lda $dc0d
        lda #0
        sta $d01a
        lda #$ff
        sta $d019
        jsr scr_off
        lda #$fa                ; DEN is latched at line $30: this frame's badlines come anyway, so start below them
        jsr vlin
        lda #0
        sta $d015
        sta $dd0e               ; CIA2 timer A stopped, latch $ffff
        lda #$ff
        sta $dd04
        sta $dd05
        lda #$80                ; ($f7) -> $3180, ($f9) -> $31ff
        sta $f7
        lda #$31
        sta $f8
        sta $fa
        lda #$ff
        sta $f9
        jsr mkstub2
        lda #0
        sta nshow
        sta ncol
        sta mi
        ldx #2
        ldy #0
        jsr at
nextm   ldx mi
        lda mtab,x
        jsr mode
        ldx #$ff                ; the empty stub, in this mode
        jsr runcase
        lda rlo
        sta base
        ldx #0
loop    jsr runcase
        sec
        lda base
        sbc rlo
        sta got
        jsr cntchk
        ldx ci
        cmp c_cyc,x
        beq +
        jsr cntfail
        jsr show
+       ldx ci
        inx
        cpx #NCASE
        bne loop
        inc mi
        lda mi
        cmp #2
        bne nextm
        lda #$c7                ; the reset's mode again
        jsr mode
        jsr scr_on
        ldx #1
        ldy #0
        jsr at
        #print t_hd
        ldx #20
        ldy #0
        jsr at
        jmp done

show    lda nshow               ; one entry: <m> <op><var> <exp>><got>, three per row, rows 2-20
        cmp #57
        bcs _r
        inc nshow
        ldx mi
        lda mchr,x
        jsr put
        jsr space
        ldx ci
        lda c_op,x
        jsr hex8
        ldx ci
        lda c_var,x
        jsr put
        jsr space
        ldx ci
        lda c_cyc,x
        jsr hex4
        lda #$3e                ; ">"
        jsr put
        lda got
        jsr dec8
        jsr space
        inc ncol
        lda ncol
        cmp #3
        bne _r
        lda #0
        sta ncol
        jmp nl
_r      rts

mtab    .byte $c7, $c0

runcase stx ci                  ; X = the case ($ff: the empty stub); leaves the tail's timer reading in rlo
        lda #$8d                ; STA $DD0E
        sta STUB
        lda #$0e
        sta STUB+1
        lda #$dd
        sta STUB+2
        ldy #3
        cpx #$ff
        beq _tail
        lda c_op,x
        sta STUB,y
        iny
        lda c_len,x
        cmp #2
        bcc _tail
        lda c_lo,x
        sta STUB,y
        iny
        lda c_len,x
        cmp #3
        bcc _tail
        lda c_hi,x
        sta STUB,y
        iny
_tail   sty tailo
        ldx #0
-       lda tail,x
        sta STUB,y
        iny
        inx
        cpx #TAILN
        bne -
        lda #<STUB
        sta jvec
        lda #>STUB
        sta jvec+1
        tsx
        stx savesp
        ldx ci
        cpx #$ff
        beq _set
        lda c_kind,x
        cmp #1                  ; a branch across a page: STUB2 with this opcode
        bne +
        lda c_op,x
        sta STUB2+3
        lda #<STUB2
        sta jvec
        lda #>STUB2
        sta jvec+1
        jmp _set
+       cmp #2                  ; JMP/JSR abs: to the tail
        bne +
        lda tailo
        sta STUB+4
        lda #>STUB
        sta STUB+5
        jmp _set
+       cmp #3                  ; JMP (ind): the pointer at $3150 holds the tail
        bne +
        lda tailo
        sta $3150
        lda #>STUB
        sta $3151
        jmp _set
+       cmp #4                  ; RTS: the tail - 1 on the stack
        bne +
        lda #>STUB
        pha
        ldy tailo
        dey
        tya
        pha
        jmp _set
+       cmp #5                  ; RTI: the tail and P on the stack
        bne +
        lda #>STUB
        pha
        lda tailo
        pha
        lda #$24
        pha
        jmp _set
+       cmp #6                  ; BRK: the vector at $fffe (RAM: $01 = $35) holds the tail
        bne _set
        lda tailo
        sta $fffe
        lda #>STUB
        sta $ffff
        lda #$35
        sta $01
_set    ldx ci
        cpx #$ff
        bne +
        lda #$24
        pha
        ldy #0
        ldx #0
        beq ++
+       lda c_p,x
        pha
        lda c_y,x
        tay
        lda c_x,x
        tax
+       lda #$11                ; the value STA $DD0E writes: force load, start
        plp
        jmp (jvec)

after   ldx savesp              ; the tail jumps here
        txs
        lda #$37
        sta $01
        cld
        sei
        rts

tail    lda $dd04               ; copied behind the case's bytes (and at $3300 by mkstub2)
        sta rlo
        jmp after
TAILN   = * - tail

mkstub2 lda #$8d                ; STA $DD0E at $32f9, a branch +2 at $32fc (target $3300: another page), NOP NOP, the tail at $3300
        sta STUB2
        lda #$0e
        sta STUB2+1
        lda #$dd
        sta STUB2+2
        lda #$d0
        sta STUB2+3
        lda #$02
        sta STUB2+4
        lda #$ea
        sta STUB2+5
        sta STUB2+6
        ldx #0
-       lda tail,x
        sta $3300,x
        inx
        cpx #TAILN
        bne -
        rts

        .include "ops_tab.inc"
        .include "lib.inc"

        .enc "screen"
title   .null "scpu2 ops"
t_hd    .null "differing: m op var exp>got, m 7=c7 0=c0"
mchr    .text "70"
