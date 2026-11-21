; scpu3_1mhz.asm - T241.1 P3: the CPU at 1 MHz against a C64, with the cache hit and missed (64tass).
; Seven loads, each timed in PHI2 cycles by the CIA2 timers, in four conditions:
;   a.off  reset mirror mode ($c7), screen off (no badlines, no sprites): exact
;   b.off  mirror everything ($c0, the mode Master's Boom plays in), screen off: exact
;   a.spr  reset mirror mode, screen on, 8 sprites over lines 140-160: within 64 cycles (the start's phase in its line moves
;          the window's end against the stalls; the C64 itself varied by up to 13 between runs)
;   b.spr  mirror everything, screen on, 8 sprites: within 64 cycles
; The expected values are a stock PAL C64's (ref/stock_pal.txt; the board's trx64g gave the same, step 0 2026-11-21).
; "*" marks a value off the C64's.
; The loads:
;   reg   registers only                          zp    zero page and stack
;   mem   768 bytes of data (always in a cache)   bigr  reads over 32 KiB, one per 16-byte line
;   conf  two reads 8 KiB apart (one line of an 8 KiB direct-mapped cache: every read misses)
;   code  32 code blocks 1 KiB apart (every fetch misses such a cache)
;   wcnf  a write and a read 8 KiB apart
; On a C64 without a SuperCPU the program runs the same loads (a and b are then the same machine).

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NW      = 7
wi      = $c040                 ; the load
ci      = $c041                 ; the condition
tmpk    = $c042
rep     = $c043

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda #1
        sta mstyle
        jsr gencode
        ldx #1
        ldy #0
        jsr at
        #print t_cols
        lda issc
        beq +
        jsr spd1
+       jsr calib
        lda #0
        sta wi
nextw   lda wi
        clc
        adc #2
        tax
        ldy #0
        jsr at
        ldx wi
        lda nmlo,x
        sta zp_t
        lda nmhi,x
        sta zp_t+1
        jsr prs
        lda #0
        sta ci
nextc   jsr measure
        jsr space
        jsr prel7
        jsr expget
        lda ci
        and #2
        beq +
        jsr chkt
        jmp ++
+       jsr chkx
+       inc ci
        lda ci
        cmp #4
        bne nextc
        inc wi
        lda wi
        cmp #NW
        bne nextw
        lda issc
        beq +
        lda #$c7
        jsr mode
+       jsr scr_on
        lda #0
        sta $d015
        ldx #2+NW
        ldy #0
        jsr at
        #print t_leg
        jmp done

measure lda issc                ; el = the cycles of load wi in condition ci
        beq _cond
        lda ci
        and #1
        beq +
        lda #$c0
        bne ++
+       lda #$c7
+       jsr mode
_cond   lda ci
        and #2
        bne _spr
        jsr scr_off
        lda #0
        sta $d015
        jmp _go
_spr    jsr scr_on
        jsr spr8
_go     ldx wi
        lda wlo,x
        sta wvec
        lda whi,x
        sta wvec+1
        lda #$fa
        jsr vlin
        jsr calib               ; in the mode just set, at line 250: no DMA in the calibration either
        jsr c2start
        jsr callw
        jmp c2stop

expget  lda wi                  ; ex = the C64's value of (wi, ci); tol = 64
        asl a
        asl a
        clc
        adc ci
        sta tmpk
        asl a
        clc
        adc tmpk
        tax
        lda extab,x
        sta ex
        lda extab+1,x
        sta ex+1
        lda extab+2,x
        sta ex+2
        lda #0
        sta ex+3
        sta tol+1
        lda #64
        sta tol
        rts

; ---- the loads -----------------------------------------------------------------------------------------------------------

        .align 256              ; all loads in one page: no branch crosses a page, so scpu6 runs the same loops as here
w_reg   ldy #200
_o      ldx #0
_i      dex
        bne _i
        dey
        bne _o
        rts

w_zp    ldy #200
_o      ldx #0
_i      sta $02
        pha
        pla
        inc $02
        dex
        bne _i
        dey
        bne _o
        rts

w_mem   ldy #40
_o      ldx #0
_i      sta $c800,x
        lda $c900,x
        inc $cb00,x             ; $cb00: not the 8 KiB cache's line index of the loop's own code (T241.1 review)
        dex
        bne _i
        dey
        bne _o
        rts

w_bigr  lda #4
        sta rep
_r      lda #$20
        sta _rd+2
_p      ldx #0
_rd     lda $2000,x
        txa
        clc
        adc #16
        tax
        bne _rd
        inc _rd+2
        lda _rd+2
        cmp #$a0
        bne _p
        dec rep
        bne _r
        rts

w_conf  ldy #64
_o      ldx #0
_i      lda $2000,x
        lda $4000,x
        inx
        bne _i
        dey
        bne _o
        rts

w_code  ldy #64
_o      jsr $2000
        dey
        bne _o
        rts

w_wcnf  ldy #64
_o      ldx #0
_i      sta $2100,x
        lda $4100,x
        inx
        bne _i
        dey
        bne _o
        rts

gencode lda #$00                ; 32 blocks at $2000 + k * $400: four NOPs, then JMP to the next block (the last: RTS)
        sta zp_t
        lda #$20
        sta zp_t+1
        ldx #0
_b      ldy #0
        lda #$ea
        sta (zp_t),y
        iny
        sta (zp_t),y
        iny
        sta (zp_t),y
        iny
        sta (zp_t),y
        iny
        cpx #31
        beq _last
        lda #$4c
        sta (zp_t),y
        iny
        lda #$00
        sta (zp_t),y
        iny
        lda zp_t+1
        clc
        adc #4
        sta (zp_t),y
        sta zp_t+1
        inx
        bne _b
_last   lda #$60
        sta (zp_t),y
        rts

wlo     .byte <w_reg, <w_zp, <w_mem, <w_bigr, <w_conf, <w_code, <w_wcnf
whi     .byte >w_reg, >w_zp, >w_mem, >w_bigr, >w_conf, >w_code, >w_wcnf
nmlo    .byte <n0, <n1, <n2, <n3, <n4, <n5, <n6
nmhi    .byte >n0, >n1, >n2, >n3, >n4, >n5, >n6

; the stock PAL C64's cycles, per load: a.off, b.off, a.spr, b.spr (24 bit each)
extab   .long 257215, 257215, 277851, 277851       ; reg
        .long 1025215, 1025215, 1107311, 1107309   ; zp
        .long 215295, 215295, 232603, 232603       ; mem
        .long 131147, 131147, 141464, 141465       ; bigr
        .long 213391, 213391, 230649, 230649       ; conf
        .long 23439, 23439, 24911, 24913           ; code
        .long 229775, 229775, 247831, 247821       ; wcnf

        .include "lib.inc"

        .enc "screen"
title   .null "scpu3 1mhz"
t_cols  .null "load   a.off    b.off    a.spr    b.spr"
t_leg   .null "a=$c7 b=$c0 off:exact spr:+-64 *=not c64"
n0      .null "reg "
n1      .null "zp  "
n2      .null "mem "
n3      .null "bigr"
n4      .null "conf"
n5      .null "code"
n6      .null "wcnf"
