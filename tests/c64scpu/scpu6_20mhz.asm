; scpu6_20mhz.asm - T241.1 P6: the SuperCPU at 20 MHz against the real SuperCPU's behaviour (64tass).
; Every row is a load timed in PHI2 cycles (CIA2) at 20 MHz, with the value a real SuperCPU gives:
;   reg      scpu3's register load (257215 cycles at 1 MHz): 1/20.2995 of it at 20 MHz (within 3 %)
;   io rd    256 x LDA $d020: an I/O read waits for the C64 bus, one PHI2 cycle each (within 16)
;   mir wr   256 x LDA/STA $0400,x in mirror mode $c7: each write goes to the C64 bus, one PHI2 cycle each (within 16)
;   fast wr  256 x STA $3100,x in mode $80 (BASIC optimisation, only $0400-$07ff mirrored): 2560 CPU cycles (within 10 %)
;   bigr, conf, code   scpu3's loads in mode $80 (reads only): the real SuperCPU's memory is static RAM without a cache,
;            so it runs them at the full factor too (within 5 %). This core's 8 KiB cache misses show here.
;   badl on  the write test of VICE-testprogs SCPU/badline (test_badline.s): 65536 writes to $07e7 in mode $80 from
;            line 255, screen on. Real NTSC SuperCPU: $10fa2 (69538). One write per PHI2 cycle, none in a badline's 40
;            DMA cycles, gives 69536 for NTSC and 68776 for PAL: expected 68778 on PAL (within 30)
;   badl off the same with the screen off: 65538 (within 10)
; Needs a SuperCPU and the speed switch at 20 MHz ($d0b9 bit 6 = 0); otherwise it says so and measures nothing.

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NROW    = 9
ri      = $c040
rep     = $c041

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda issc
        bne +
        #print t_nosc
        jmp done
+       jsr spd20
        lda $d0b9
        and #$40
        beq +
        #print t_slow
        inc nfail
        inc nchk
        jmp done
+       #print t_fast
        lda #$7f
        sta $dc0d
        lda $dc0d
        jsr gencode
        jsr calib
        ldx #2
        ldy #0
        jsr at
        #print t_cols
        lda #0
        sta ri
nextr   lda ri
        clc
        adc #3
        tax
        ldy #0
        jsr at
        ldx ri
        lda nmlo,x
        sta zp_t
        lda nmhi,x
        sta zp_t+1
        jsr prs
        ldx ri                  ; the row's mode and screen
        lda r_mode,x
        jsr mode
        ldx ri
        lda r_scr,x
        beq +
        jsr scr_on
        jmp ++
+       jsr scr_off
+       ldx ri
        lda wlo,x
        sta wvec
        lda whi,x
        sta wvec+1
        lda r_line,x
        jsr vlin
        jsr c2start
        jsr callw
        jsr c2stop
        jsr space
        jsr prel7
        lda ri                  ; the expected value and tolerance
        asl a
        asl a
        tax
        lda r_ex,x
        sta ex
        lda r_ex+1,x
        sta ex+1
        lda r_ex+2,x
        sta ex+2
        lda #0
        sta ex+3
        lda r_tol,x
        sta tol
        lda r_tol+1,x
        sta tol+1
        jsr space
        ldx #3                  ; print the expected value, el kept around it
-       lda el,x
        sta save,x
        lda ex,x
        sta el,x
        dex
        bpl -
        jsr prel7
        ldx #3
-       lda save,x
        sta el,x
        dex
        bpl -
        jsr chkt
        inc ri
        lda ri
        cmp #NROW
        beq +
        jmp nextr
+       lda #$c7
        jsr mode
        jsr scr_on
        ldx #3+NROW
        ldy #0
        jsr at
        jmp done

save    .byte 0, 0, 0, 0

; ---- the loads -----------------------------------------------------------------------------------------------------------

        .align 256              ; all loads in one page, as in scpu3: no branch crosses a page
w_reg   ldy #200
_o      ldx #0
_i      dex
        bne _i
        dey
        bne _o
        rts

w_io    ldx #0
-       lda $d020
        dex
        bne -
        rts

w_mir   ldx #0                  ; the screen's own bytes written back: one bus write per pass
-       lda $0400,x
        sta $0400,x
        dex
        bne -
        rts

w_fast  ldx #0                  ; $3100: not one of the code blocks of gencode
-       sta $3100,x
        dex
        bne -
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

w_badl  ldy #0                  ; 65536 writes to $07e7 (test_badline.s, with 8-bit index registers)
        ldx #0
-       txa
        sta $07e7
        dex
        bne -
        dey
        bne -
        rts

gencode lda #$00                ; scpu3's 32 code blocks at $2000 + k * $400
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

; per row: the load, the mirror mode, the screen (1 on), the start line, the expected PHI2 cycles and the tolerance
wlo     .byte <w_reg, <w_io, <w_mir, <w_fast, <w_bigr, <w_conf, <w_code, <w_badl, <w_badl
whi     .byte >w_reg, >w_io, >w_mir, >w_fast, >w_bigr, >w_conf, >w_code, >w_badl, >w_badl
r_mode  .byte $c7, $c7, $c7, $80, $80, $80, $80, $80, $80
r_scr   .byte 0, 0, 0, 0, 0, 0, 0, 1, 0
r_line  .byte 250, 250, 250, 250, 250, 250, 250, 255, 255
r_ex    .dword 12671, 256, 256, 126, 6461, 10512, 1155, 68778, 65538
r_tol   .dword 380, 16, 16, 13, 323, 526, 58, 30, 10
nmlo    .byte <n0, <n1, <n2, <n3, <n4, <n5, <n6, <n7, <n8
nmhi    .byte >n0, >n1, >n2, >n3, >n4, >n5, >n6, >n7, >n8

        .include "lib.inc"

        .enc "screen"
title   .null "scpu6 20mhz"
t_nosc  .null "no supercpu: nothing to measure"
t_slow  .null "speed switch at 1 mhz ($d0b9 bit 6): *"
t_fast  .null "20 mhz"
t_cols  .null "load        phi2 expected"
n0      .null "reg     "
n1      .null "io rd   "
n2      .null "mir wr  "
n3      .null "fast wr "
n4      .null "bigr    "
n5      .null "conf    "
n6      .null "code    "
n7      .null "badl on "
n8      .null "badl off"
