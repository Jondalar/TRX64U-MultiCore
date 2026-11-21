; scpu4_irq.asm - T241.1 P4: how late an IRQ is taken, per interrupted instruction, at 1 MHz (64tass).
; The main program runs a sled: 4 KiB of one instruction, then JMP back. CIA1 timer A interrupts it 64 times; the
; handler's first instruction reads the timer's low byte. d = the latch - that value = the cycles from the timer's
; reload to the read: the time the CPU takes to finish the interrupted instruction, plus the interrupt sequence, plus
; the read. The handler moves the latch by 0-15 cycles per interrupt, so the interrupt meets every cycle of the
; instruction. Shown per sled: the lowest and the highest d. Expected: a stock PAL C64's values (ref/stock_pal.txt);
; "*" marks a value off the C64's. Screen off, no sprites, the KERNAL banked out ($01 = $35, the vector at $fffe).
; The sleds: nop (2 cycles), lda zp (3), lda abs (4), lda (zp),y (5), inc zp (5), inc abs (6), inc abs,x (7),
; jmp abs to the next instruction (3), bne taken to the next instruction (3, no page crossed), pha/pla (3/4).

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NS      = 64                    ; interrupts per sled
NSLED   = 10
SLED    = $2000                 ; $2000-$2fff
BASE    = $0150                 ; the timer latch: BASE + 0..15
si      = $c040                 ; the sled
n       = $c041                 ; interrupts taken
curlat  = $c042                 ; the latch's low byte of the running period
tmpy    = $c043
savesp  = $c044
dmin    = $c045
dmax    = $c046
plen    = $c047
pofs    = $c048
samp    = $c100                 ; NS bytes: d

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda issc
        beq +
        jsr spd1
+       lda #$7f
        sta $dc0d
        lda $dc0d
        lda #0
        sta $d01a
        lda #$ff
        sta $d019
        jsr scr_off
        lda #0
        sta $d015
        lda #$00                ; ($f7) -> $c200 for lda (zp),y
        sta $f7
        lda #$c2
        sta $f8
        ldx #1
        ldy #0
        jsr at
        #print t_hd
        lda #0
        sta si
nexts   jsr fill
        jsr runsled
        jsr stats
        lda si
        clc
        adc #2
        tax
        ldy #0
        jsr at
        ldx si
        lda nmlo,x
        sta zp_t
        lda nmhi,x
        sta zp_t+1
        jsr prs
        lda dmin
        jsr dec8
        lda si                  ; check the lowest
        asl a
        tax
        lda dmin
        cmp exd,x
        jsr mark
        lda dmax
        jsr dec8
        lda si
        asl a
        tax
        lda dmax
        cmp exd+1,x
        jsr mark
        inc si
        lda si
        cmp #NSLED
        bne nexts
        lda $02a6               ; the KERNAL's timer A again (latch by $02a6), its interrupt on
        beq +
        lda #$25
        ldx #$40
        bne ++
+       lda #$95
        ldx #$42
+       sta $dc04
        stx $dc05
        lda #$11
        sta $dc0e
        jsr scr_on
        ldx #2+NSLED
        ldy #0
        jsr at
        jmp done

mark    php                     ; Z from the compare: " " when equal, "*" when not; counts
        inc nchk
        plp
        beq +
        inc nfail
        lda #$2a
        jmp put
+       jmp space

; ---- one sled ------------------------------------------------------------------------------------------------------------

fill    lda #<SLED              ; the sled si from $2000 up to $2ff0, then JMP $2000
        sta zp_t
        lda #>SLED
        sta zp_t+1
        ldx si
        lda pidx,x
        sta pofs
        tay
        lda pat,y               ; the pattern's length
        sta plen
_rep    ldx #0
_b      ldy pofs
        iny
        stx dtmp
        tya
        clc
        adc dtmp
        tay
        lda pat,y
        ldy #0
        sta (zp_t),y
        inc zp_t
        bne +
        inc zp_t+1
+       inx
        cpx plen
        bne _b
        ldx si
        cpx #7                  ; jmp: the operand is the next instruction
        bne +
        jsr jmpfix
+       lda zp_t+1
        cmp #$2f
        bcc _rep
        lda zp_t
        cmp #$f0
        bcc _rep
        ldy #0
        lda #$4c
        sta (zp_t),y
        iny
        lda #<SLED
        sta (zp_t),y
        iny
        lda #>SLED
        sta (zp_t),y
        rts

jmpfix  sec                     ; the JMP just written ends at zp_t: its operand (zp_t-2, zp_t-1) = zp_t (zp_ptr is free here:
        lda zp_t                ; the next row sets it again with at)
        sbc #2
        sta zp_ptr
        lda zp_t+1
        sbc #0
        sta zp_ptr+1
        ldy #0
        lda zp_t
        sta (zp_ptr),y
        iny
        lda zp_t+1
        sta (zp_ptr),y
        rts

runsled lda #0                  ; NS interrupts into samp
        sta n
        tsx
        stx savesp
        lda #$35
        sta $01
        lda #<irq
        sta $fffe
        lda #>irq
        sta $ffff
        lda #$00
        sta $dc0e
        lda #<BASE
        sta $dc04
        sta curlat
        lda #>BASE              ; timer stopped: the high byte write loads the counter
        sta $dc05
        lda #$81
        sta $dc0d
        lda $dc0d
        lda #$11
        sta $dc0e
        ldx #0
        ldy #0
        lda #1                  ; Z = 0 for the bne sled
        cli
        jmp SLED

irq     lda $dc04               ; d = curlat - the timer's low byte
        sty tmpy
        eor #$ff
        sec
        adc curlat
        ldy n
        sta samp,y
        iny
        sty n
        tya
        and #15
        clc
        adc #<BASE
        sta $dc04               ; the next period's latch
        sta curlat
        lda $dc0d
        cpy #NS
        bcs +
        ldy tmpy
        rti
+       ldx savesp              ; done: back to the caller of runsled, I still set
        txs
        lda #$7f
        sta $dc0d
        lda $dc0d
        lda #0
        sta $dc0e
        lda #$37
        sta $01
        rts

stats   lda #$ff
        sta dmin
        lda #0
        sta dmax
        ldx #NS-1
-       lda samp,x
        cmp dmin
        bcs +
        sta dmin
+       cmp dmax
        bcc +
        sta dmax
+       dex
        bpl -
        rts

; the patterns: length, bytes
pat     .byte 1, $ea                    ; 0 nop
        .byte 2, $a5, $02               ; 3 lda zp
        .byte 3, $ad, $00, $c2          ; 6 lda abs
        .byte 2, $b1, $f7               ; 10 lda (zp),y
        .byte 2, $e6, $02               ; 13 inc zp
        .byte 3, $ee, $00, $c2          ; 16 inc abs
        .byte 3, $fe, $00, $c2          ; 20 inc abs,x
        .byte 3, $4c, $00, $00          ; 24 jmp (operand fixed by jmpfix)
        .byte 2, $d0, $00               ; 28 bne +0
        .byte 2, $48, $68               ; 31 pha pla
pidx    .byte 0, 2, 5, 9, 12, 15, 19, 23, 27, 30

nmlo    .byte <n0, <n1, <n2, <n3, <n4, <n5, <n6, <n7, <n8, <n9
nmhi    .byte >n0, >n1, >n2, >n3, >n4, >n5, >n6, >n7, >n8, >n9

; the stock PAL C64's lowest and highest d per sled
exd     .byte 12, 13                ; nop
        .byte 12, 14                ; lda zp
        .byte 12, 15                ; lda abs
        .byte 12, 16                ; lda (zp),y
        .byte 12, 16                ; inc zp
        .byte 12, 17                ; inc abs
        .byte 12, 18                ; inc abs,x
        .byte 12, 14                ; jmp
        .byte 13, 15                ; bne taken (one more than the other sleds on the C64)
        .byte 12, 15                ; pha pla

        .include "lib.inc"

        .enc "screen"
title   .null "scpu4 irq"
t_hd    .null "sled        low  hi   (cycles)"
n0      .null "nop         "
n1      .null "lda zp      "
n2      .null "lda abs     "
n3      .null "lda (zp),y  "
n4      .null "inc zp      "
n5      .null "inc abs     "
n6      .null "inc abs,x   "
n7      .null "jmp next    "
n8      .null "bne taken   "
n9      .null "pha pla     "
