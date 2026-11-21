; scpu7_mirror.asm - T241.1 P7: which CPU writes reach the C64 RAM, per mirror mode, at 1 MHz and 20 MHz (64tass).
; The CPU reads its own RAM, so the C64 RAM is read through the VIC: sprite 0 shows the 64-byte block of the test byte,
; sprite 1 lies on it with $ff at the test byte's place and 0 elsewhere. They collide ($d01e bit 1) exactly when the
; C64 RAM's test byte is not 0. Per test: in mode $c0 (everything mirrored) the byte is set to 0 in both RAMs, then in
; the mode under test the CPU writes $ff to it, then the VIC looks. 1 = the write reached the C64 RAM.
; Table: per address, the modes $c7 (reset), $c0 ($d077), $80 ($d076), $40 ($d075), $00 ($d074), each "1 MHz 20 MHz".
; Expected on the SuperCPU: the mirror windows of VICE's scpu64mem.c (lines 81-86), as cores/c64scpu/scpu_mem.v has
; them: $c7 pages $02-$ff, $c0 $00-$ff, $80 $04-$07, $40 $40-$7f, $00 $80-$bf. On a C64 every write is in its RAM: all 1.
; Rows below the table, in mode $c7:
;   burst  256 writes back to back ($2c00-$2cff), then three of them read: $2c00, $2c80, $2cfe     expected 111
;   rmw    INC of a 0 ($2d00) and DEC of a 1 ($2d41)                                               expected 10
;   16bit  one 16-bit STA (native mode) to $2d80/$2d81                                              expected 11
;   mvn    MVN of 64 bytes $ff from $2e00 to $2f00, then $2f00 and $2f3e read                       expected 11
;   (16bit and mvn need a 65816: "--" on a C64)
; "*" marks a result off the expected.

        .cpu "65816"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NADR    = 8
ta      = $f7                   ; 2: the test address (zero page, for STA (ta),y)
ai      = $c040
mi      = $c041
sp      = $c042
bb      = $c043                 ; the test byte's VIC bank base, high byte
res     = $c044                 ; the cell's two results: bit 1 = 1 MHz, bit 0 = 20 MHz
xi      = $c045
got     = $c046                 ; the extra rows' result bits
nbit    = $c047

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda #$7f
        sta $dc0d
        lda $dc0d
        jsr sprset
        ldx #1
        ldy #0
        jsr at
        #print t_hd
        lda #0
        sta ai
nexta   lda ai
        clc
        adc #2
        tax
        ldy #0
        jsr at
        ldx ai
        lda adhi,x
        jsr hex8
        ldx ai
        lda adlo,x
        jsr hex8
        jsr space
        lda #0
        sta mi
nextm   lda #0
        sta res
        lda #0
        sta sp
        jsr onecell
        asl res
        ora res
        sta res
        lda #1
        sta sp
        jsr onecell
        asl res
        ora res
        sta res
        jsr space               ; the two digits
        lda res
        lsr a
        and #1
        ora #$30
        jsr put
        lda res
        and #1
        ora #$30
        jsr put
        lda #3                  ; expected: both 1 on a C64; on the SuperCPU when the mode's bit is in the address's mask
        ldx issc
        beq _cmp
        ldx ai
        lda exm,x
        ldx mi
        and mbit,x
        beq _cmp
        lda #3
_cmp    cmp res
        jsr mark
        inc mi
        lda mi
        cmp #5
        bne nextm
        inc ai
        lda ai
        cmp #NADR
        beq +
        jmp nexta
+       jsr extras
        lda $dd00               ; VIC bank 0 again, mode $c7, the switch's speed
        ora #3
        sta $dd00
        lda #0
        sta $d015
        lda issc
        beq +
        lda #$c7
        jsr mode
        jsr spd20
+       ldx #15
        ldy #0
        jsr at
        jmp done

mark    php                     ; Z from the compare: " " when equal, "*" when not; counts
        jsr cntchk
        plp
        beq +
        jsr cntfail
        lda #$2a
        jmp put
+       jmp space

; ---- one cell ------------------------------------------------------------------------------------------------------------

onecell ldx ai                  ; the address ai, the mode mi, the speed sp: A = 1 when the write reached the C64 RAM
        lda adlo,x
        sta ta
        lda adhi,x
        sta ta+1
        jsr speed
        lda #$c0
        jsr mode
        jsr setrb
        lda #0
        ldy #0
        sta (ta),y
        ldx mi
        lda modes,x
        jsr mode
        lda #$ff                ; the write under test
        ldy #0
        sta (ta),y
        jsr rb
        pha                     ; mode $c7 again: the screen output must reach the C64 RAM
        lda #$c7
        jsr mode
        pla
        rts

speed   lda sp                  ; 0: 1 MHz, 1: 20 MHz (without a SuperCPU spd1 and spd20 write nothing)
        beq +
        jmp spd20
+       jmp spd1

sprset  lda #100                ; sprites 0 and 1 on each other at X = Y = 100
        sta $d000
        sta $d001
        sta $d002
        sta $d003
        lda #0
        sta $d010
        sta $d017
        sta $d01b
        sta $d01c
        sta $d01d
        lda #1
        sta $d027
        lda #2
        sta $d028
        lda #3
        sta $d015
        rts

setrb   lda ta+1                ; for the byte at ta: the mask block at bank + $0380, the pointers at bank + $07f8,
        and #$c0                ; the VIC on the byte's bank. Writes: the caller is in mode $c0
        sta bb
        lda #$80
        sta zp_t
        lda bb
        ora #$03
        sta zp_t+1
        lda #0
        ldy #63
-       sta (zp_t),y
        dey
        bpl -
        lda ta
        and #$3f
        tay
        lda #$ff
        sta (zp_t),y
        lda #$f8
        sta zp_t
        lda bb
        ora #$07
        sta zp_t+1
        lda ta+1                ; pointer 0 = (ta - bank) / 64
        and #$3f
        asl a
        asl a
        sta dtmp
        lda ta
        rol a
        rol a
        rol a
        and #3
        ora dtmp
        ldy #0
        sta (zp_t),y
        lda #$0e                ; pointer 1: $0380 / 64
        iny
        sta (zp_t),y
        lda bb                  ; $dd00 bits 1-0 = 3 - bank
        rol a
        rol a
        rol a
        and #3
        eor #3
        sta dtmp
        lda $dd00
        and #$fc
        ora dtmp
        sta $dd00
        rts

rb      lda #250                ; A = 1 when sprites 0 and 1 collided in one whole frame
        jsr vlin
        lda $d01e
        lda #0
        jsr vlin
        lda #250
        jsr vlin
        lda $d01e
        and #2
        beq +
        lda #1
+       rts

; ---- the rows below the table (mode $c7) -----------------------------------------------------------------------------------

extras  ldx #11
        ldy #0
        jsr at
        #print t_burst
        lda #0
        sta sp
-       jsr burst
        jsr shown
        inc sp
        lda sp
        cmp #2
        bne -
        jsr nl
        #print t_rmw
        lda #0
        sta sp
-       jsr rmw
        jsr shown
        inc sp
        lda sp
        cmp #2
        bne -
        jsr nl
        #print t_16
        lda issc
        bne +
        #print t_none
        jmp _mvn
+       lda #0
        sta sp
-       jsr st16
        jsr shown
        inc sp
        lda sp
        cmp #2
        bne -
_mvn    jsr nl
        #print t_mvn
        lda issc
        bne +
        #print t_none
        rts
+       lda #0
        sta sp
-       jsr domvn
        jsr shown
        inc sp
        lda sp
        cmp #2
        bne -
        rts

shown   lda got                 ; print nbit result bits of got (highest first), "*" when got differs from xi
        cmp xi
        php
        jsr space
        ldx nbit
-       asl got
        lda #0
        rol a
        ora #$30
        jsr put
        dex
        bne -
        plp
        jmp mark

probe   pha                     ; mode $c0, the readback set up for the address in A (low) / X (high); keeps nothing
        stx ta+1                ; result: got = got * 2 + the readback; returns with the readback in A
        pla
        sta ta
        lda #$c0
        jsr mode
        jsr setrb
        lda #$c7
        jsr mode
        jsr rb
        lsr a
        rol got
        rts

burst   jsr speed               ; xi/nbit/got for the burst row: expected 111
        lda #$00
        ldx #$2c
        ldy #0
        jsr clrc0x
        lda #$c7
        jsr mode
        ldx #0
        lda #$ff
-       sta $2c00,x
        inx
        bne -
        lda #0
        sta got
        lda #$00
        ldx #$2c
        jsr probe
        lda #$80
        ldx #$2c
        jsr probe
        lda #$fe
        ldx #$2c
        jsr probe
        lda #3
        sta nbit
        lda #7
        sta xi
        asl got                 ; left-align the 3 bits in got for shown
        asl got
        asl got
        asl got
        asl got
        lda xi
        asl a
        asl a
        asl a
        asl a
        asl a
        sta xi
        rts

clrc0x  sta zp_t                ; mode $c0: 256 bytes at A/X = 0
        stx zp_t+1
        lda #$c0
        jsr mode
        ldy #0
        lda #0
-       sta (zp_t),y
        iny
        bne -
        rts

rmw     jsr speed               ; INC of 0 at $2d00 and DEC of 1 at $2d41: expected 10
        lda #$c0
        jsr mode
        lda #0
        sta $2d00
        lda #1
        sta $2d41
        lda #$c7
        jsr mode
        inc $2d00
        dec $2d41
        lda #0
        sta got
        lda #$00
        ldx #$2d
        jsr probe
        lda #$41
        ldx #$2d
        jsr probe
        lda #2
        sta nbit
        lda got
        asl a
        asl a
        asl a
        asl a
        asl a
        asl a
        sta got
        lda #%10000000
        sta xi
        rts

st16    jsr speed               ; a 16-bit STA of $ffff to $2d80: expected 11
        lda #$c0
        jsr mode
        lda #0
        sta $2d80
        sta $2d81
        lda #$c7
        jsr mode
        clc
        xce
        rep #$20
        .al
        lda #$ffff
        sta $2d80
        sep #$20
        .as
        sec
        xce
        lda #0
        sta got
        lda #$80
        ldx #$2d
        jsr probe
        lda #$81
        ldx #$2d
        jsr probe
        jmp two

domvn   jsr speed               ; MVN of 64 bytes $ff from $2e00 to $2f00: expected 11
        lda #$c0
        jsr mode
        ldx #63
-       lda #$ff
        sta $2e00,x
        lda #0
        sta $2f00,x
        dex
        bpl -
        lda #$c7
        jsr mode
        clc
        xce
        rep #$30
        .al
        .xl
        ldx #$2e00
        ldy #$2f00
        lda #63
        mvn 0,0
        sep #$30
        .as
        .xs
        sec
        xce
        lda #0
        sta got
        lda #$00
        ldx #$2f
        jsr probe
        lda #$3e
        ldx #$2f
        jsr probe
two     lda #2
        sta nbit
        lda got
        asl a
        asl a
        asl a
        asl a
        asl a
        asl a
        sta got
        lda #%11000000
        sta xi
        rts

; the addresses, their VIC banks are 0, 0, 0, 0, 0, 1, 2, 3
adlo    .byte $f0, $10, $40, $f0, $00, $00, $00, $00
adhi    .byte $00, $01, $03, $07, $28, $50, $a8, $c8
; per address the modes that mirror it on the SuperCPU: bit 4 $c7, 3 $c0, 2 $80, 1 $40, 0 $00
exm     .byte %01000, %01000, %11000, %11100, %11000, %11010, %11001, %11000
modes   .byte $c7, $c0, $80, $40, $00
mbit    .byte %10000, %01000, %00100, %00010, %00001

        .include "lib.inc"

        .enc "screen"
title   .null "scpu7 mirror"
t_hd    .null "addr  c7  c0  80  40  00  (1/20 mhz)"
t_burst .null "burst "
t_rmw   .null "rmw   "
t_16    .null "16bit "
t_mvn   .null "mvn   "
t_none  .null " --"
