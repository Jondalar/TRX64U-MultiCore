; scpu8_shadow.asm - T241.1 P8: what the CPU reads and where its writes go, per $01 configuration, at 1 and 20 MHz (64tass).
; The SuperCPU runs the KERNAL and BASIC from its own copies (the ROM shadow) and keeps its own RAM; the C64's PLA
; decides per $01 what is seen at $a000, $d000 and $e000. Per configuration $30-$37 (no cartridge):
;   read   the byte at $a000, $d000, $e000: r = the RAM marker, b = the BASIC image's byte, k = the KERNAL image's byte,
;          c = the character ROM's byte, i = the I/O register ($d000, sprite 0 X), ? = none of them
;   write  $a5 written to $a000, $d000, $e000, then read in $34 (all RAM) and $35 (I/O): r = the RAM has it, i = the
;          I/O register has it, - = neither
; The BASIC, KERNAL and character bytes are read first in $37 and $33, so any ROM image works. Expected: the C64's PLA
; ("*" at the end of a row that differs), the same at 1 MHz and 20 MHz.

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

ci      = $c040                 ; the configuration 0-7
sp      = $c041
romA    = $c042
romE    = $c043
chD     = $c044
rd      = $c045                 ; 3: the bytes read in the configuration
rr      = $c048                 ; 3: the RAM after the writes
io      = $c04b                 ; the I/O register after the writes
cls     = $c04c                 ; 6: the classes: read a, d, e, write a, d, e
nbad    = $c052

MA      = $5a                   ; the RAM markers
MD      = $5d
ME      = $5e
VIO     = $77                   ; the I/O register's value
VW      = $a5                   ; the value written

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda #$7f
        sta $dc0d
        lda $dc0d
        lda $a000               ; the images, in $37
        sta romA
        lda $e000
        sta romE
        lda #$33
        sta $01
        lda $d000
        sta chD
        lda #$37
        sta $01
        ldx #1
        ldy #0
        jsr at
        #print t_hd
        lda #0
        sta ci
nextc   lda ci
        clc
        adc #2
        tax
        ldy #0
        jsr at
        lda ci
        ora #$30
        jsr hex8
        lda #0
        sta nbad
        lda #0
        sta sp
        jsr onecfg
        lda #1
        sta sp
        jsr onecfg
        inc nchk
        lda nbad
        beq +
        inc nfail
        lda #$2a
        jsr put
+       inc ci
        lda ci
        cmp #8
        bne nextc
        lda #0                  ; sprite 0 X as the KERNAL left it
        sta $d000
        lda issc
        beq +
        jsr spd20
+       ldx #11
        ldy #0
        jsr at
        jmp done

onecfg  lda sp                  ; one configuration at one speed: measure, classify, print, compare
        beq +
        jsr spd20
        jmp ++
+       jsr spd1
+       lda #$34                ; the RAM markers
        sta $01
        lda #MA
        sta $a000
        lda #MD
        sta $d000
        lda #ME
        sta $e000
        lda #$35                ; the I/O register
        sta $01
        lda #VIO
        sta $d000
        lda ci                  ; read in the configuration
        ora #$30
        sta $01
        lda $a000
        sta rd
        lda $d000
        sta rd+1
        lda $e000
        sta rd+2
        lda #VW                 ; write in the configuration
        sta $a000
        sta $d000
        sta $e000
        lda #$34
        sta $01
        lda $a000
        sta rr
        lda $d000
        sta rr+1
        lda $e000
        sta rr+2
        lda #$35
        sta $01
        lda $d000
        sta io
        lda #$37
        sta $01
        lda rd                  ; classify the reads
        ldx #MA
        ldy romA
        jsr clsr
        sta cls
        lda rd+1
        ldx #MD
        jsr clsd
        sta cls+1
        lda rd+2
        ldx #ME
        ldy romE
        jsr clsr
        cmp #$02                ; a ROM byte at $e000 is the KERNAL's: k
        bne +
        lda #$0b
+       sta cls+2
        ldx #0                  ; classify the writes
-       lda rr,x
        cmp #VW
        bne +
        lda #$12                ; r
        bne ++
+       lda #$2d                ; -
        cpx #1
        bne +
        ldy io
        cpy #VW
        bne +
        lda #$09                ; i
+       sta cls+3,x
        inx
        cpx #3
        bne -
        jsr space               ; print and compare with the PLA
        ldx #0
-       lda cls,x
        jsr put
        inx
        cpx #3
        bne -
        jsr space
        ldx #3
-       lda cls,x
        jsr put
        inx
        cpx #6
        bne -
        jsr space
        lda ci                  ; expected: 6 screen codes per configuration
        asl a
        sta dtmp
        asl a
        clc
        adc dtmp
        tay
        ldx #0
-       lda cls,x
        cmp plat,y
        beq +
        inc nbad
+       iny
        inx
        cpx #6
        bne -
        rts

clsr    stx dtmp                ; A = the byte, X = the RAM marker, Y = the ROM byte: r, b (ROM) or ?
        sty dtmp+1
        cmp dtmp
        bne +
        lda #$12                ; r
        rts
+       cmp dtmp+1
        bne +
        lda #$02                ; b
        rts
+       lda #$3f
        rts

clsd    stx dtmp                ; $d000: r, c (character ROM), i (I/O) or ?
        cmp dtmp
        bne +
        lda #$12
        rts
+       cmp chD
        bne +
        lda #$03                ; c
        rts
+       cmp #VIO
        bne +
        lda #$09                ; i
        rts
+       lda #$3f
        rts

        .enc "screen"
plat    .text "rrrrrr"          ; $30
        .text "rcrrrr"          ; $31
        .text "rckrrr"          ; $32
        .text "bckrrr"          ; $33
        .text "rrrrrr"          ; $34
        .text "rirrir"          ; $35
        .text "rikrir"          ; $36
        .text "bikrir"          ; $37
        .enc "none"

        .include "lib.inc"

        .enc "screen"
title   .null "scpu8 shadow"
t_hd    .null "01 rd  wr   rd  wr   (1 mhz, 20 mhz)"
