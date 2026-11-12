; SPDX-License-Identifier: GPL-3.0-or-later
; T88: a 16 KB "OS" of our own for the whole-machine benches of the Atari core (tb_a8_*.vhd): no Atari code in it. It checks the
; memory the core has (base RAM, the extended banks of PORTB, BASIC and cartridge reads through $A000/$8000, the bank register
; of an AtariMax cartridge), writes the results to $0600.., brings up an ANTIC text screen with a display list interrupt, a
; POKEY tone and the vertical blank interrupt, and then polls the keyboard, the console keys and the joystick in its main loop
; and keeps the last values at $0082.. and in the last screen row. The bench reads all of it back through the app's window.
;   64tass -b --nostart -o testos2.rom testos2.asm        (image = $C000-$FFFF; the hole at $D000-$D7FF is the chips)
;
; results ($0600..):
;  600 RAM check ($5A at $0700)    601 BASIC/cart byte at $A000   602 $A001   603 $BFFF   604 byte at $8000
;  605/606 extended bank errors (16 bit, 4 banks x 256 bytes)   607 main bank byte at $4000 after the bank test (expect $77)
;  608..60B the first byte of each extended bank after the test (bank 0..3: $00,$11,$22,$33 ^ 0)
;  610 AtariMax: $A000 after $D503   611 after $D50F   612 $BFFF after $D500   613 $A000 after $D500
;  620 SIO: bytes of the command frame the OS sends (5)
; variables: $80 frames, $82 KBCODE, $83 CONSOL, $84 PORTA, $85 STRIG0, $86 SKSTAT, $87 RANDOM, $88 tx done flag
        * = $C000
        .fill $2000, $FF
        * = $E000           ; character set: any pattern (every code a different glyph)
        .for c = 0, c < 1024, c = c + 1
          .byte ((c * 37) ^ (c >> 3)) & $ff
        .next

PORTA   = $D300
PORTB   = $D301
PACTL   = $D302
PBCTL   = $D303
DLISTL  = $D402
NMIEN   = $D40E
NMIRES  = $D40F
NMIST   = $D40F
WSYNC   = $D40A
VCOUNT  = $D40B
DMACTL  = $D400
CHBASE  = $D409
PMBASE  = $D407
COLBK   = $D01A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
CONSOL  = $D01F
STRIG0  = $D010
AUDF1   = $D200
AUDC1   = $D201
AUDF3   = $D204
AUDC3   = $D205
AUDF4   = $D206
AUDC4   = $D207
AUDCTL  = $D208
KBCODE  = $D209
RANDOM  = $D20A
SEROUT  = $D20D
IRQEN   = $D20E
IRQST   = $D20E
SKCTL   = $D20F
SKSTAT  = $D20F
STIMER  = $D209

frames  = $80
lastf   = $81
z_kbc   = $82
z_cons  = $83
z_pa    = $84
z_trig  = $85
z_sk    = $86
z_rnd   = $87
txdone  = $88
bank    = $90
mask    = $91
errlo   = $92
errhi   = $93
tmp     = $94
res     = $0600

        * = $E400
reset   sei
        cld
        ldx #$ff
        txs
        lda #0
        sta NMIEN
        sta DMACTL
        sta IRQEN
        ; PIA: port B all outputs, then $FF (OS ROM on, BASIC off, self test off, extended RAM off)
        sta PBCTL
        lda #$ff
        sta PORTB
        lda #$3c
        sta PBCTL
        sta PACTL
        lda #$ff
        sta PORTB
        ; clear $80-$9f and the results
        ldx #$20
        lda #0
-       sta frames-1,x
        dex
        bne -
        ldx #0
-       sta res,x
        sta res+$100,x
        dex
        bne -
        ; --- base RAM
        lda #$5a
        sta $0700
        lda $0700
        cmp #$5a
        bne +
        lda #1
        sta res
+       lda #$77
        sta $4000               ; main bank
        ; --- BASIC (or the cartridge) at $A000/$8000: BASIC on (PORTB bit 1 = 0), OS on
        lda #$fd
        sta PORTB
        lda $a000
        sta res+1
        lda $a001
        sta res+2
        lda $bfff
        sta res+3
        lda $8000
        sta res+4
        lda #$ff
        sta PORTB
        ; --- extended banks: PORTB bit 4 = 0 (CPU), bits 3:2 = bank; fill $4000-$40FF with i ^ (bank*$11)
        ldy #0
fill    sty bank
        tya
        asl
        asl
        ora #$e3
        sta PORTB
        lda bank
        asl
        asl
        asl
        asl
        ora bank                ; bank * $11
        sta mask
        ldx #0
-       txa
        eor mask
        sta $4000,x
        inx
        bne -
        iny
        cpy #4
        bne fill
        ; verify them all
        lda #0
        sta errlo
        sta errhi
        ldy #0
chk     sty bank
        tya
        asl
        asl
        ora #$e3
        sta PORTB
        lda bank
        asl
        asl
        asl
        asl
        ora bank
        sta mask
        ldx #0
-       txa
        eor mask
        cmp $4000,x
        beq +
        inc errlo
        bne +
        inc errhi
+       inx
        bne -
        lda $4000               ; the first byte of this bank (i = 0: mask)
        sta res+8,y
        iny
        cpy #4
        bne chk
        lda errlo
        sta res+5
        lda errhi
        sta res+6
        lda #$ff
        sta PORTB
        lda $4000
        sta res+7
        ; --- AtariMax 1 Mbit style bank register (cartridge type 2): D500-D50F selects bank + enable, D510 disables
        lda #$fd
        sta PORTB
        sta $d503
        lda $a000
        sta res+$10
        sta $d50f
        lda $a000
        sta res+$11
        sta $d500
        lda $bfff
        sta res+$12
        lda $a000
        sta res+$13
        lda #$ff
        sta PORTB
        ; --- the screen: display list at $2000, 24 text lines at $2100, a pattern in them
        ldx #0
-       txa
        sta $2100,x
        eor #$5a
        sta $2200,x
        sta $2300,x
        eor #$33
        sta $2400,x
        dex
        bne -
        lda #$70
        sta $2000
        sta $2001
        sta $2002
        lda #$42
        sta $2003
        lda #$00
        sta $2004
        lda #$21
        sta $2005
        ldx #0
        lda #$02
-       sta $2006,x
        inx
        cpx #23
        bne -
        lda #$82
        sta $2006+11            ; a DLI half way down
        lda #$41
        sta $2006+23
        lda #$00
        sta $2006+24
        lda #$20
        sta $2006+25
        lda #$00
        sta DLISTL
        lda #$20
        sta DLISTL+1
        lda #$e0
        sta CHBASE
        lda #$0f
        sta COLPF1
        lda #$94
        sta COLPF2
        lda #$22
        sta COLBK
        lda #8                  ; CONSOL: START/SELECT/OPTION released (a 0 bit pulls the line low), speaker off
        sta CONSOL
        ; --- POKEY: a tone on channel 1 (AUDF 31 = 989.6 Hz PAL), the keyboard scan on
        lda #0
        sta AUDCTL
        lda #$a8
        sta AUDC1
        lda #$1f
        sta AUDF1
        lda #3
        sta SKCTL
        ; --- ANTIC on: DL DMA + normal playfield; NMI: VBI + DLI
        lda #$22
        sta DMACTL
        lda #$c0
        sta NMIEN
        cli
main    lda frames
        cmp lastf
        beq main
        sta lastf
        lda SKSTAT
        sta z_sk
        lda KBCODE
        sta z_kbc
        lda CONSOL
        sta z_cons
        lda PORTA
        sta z_pa
        lda STRIG0
        sta z_trig
        lda RANDOM
        sta z_rnd
        ; the last screen row shows them (so the picture depends on the inputs)
        lda z_kbc
        sta $2100+23*40
        lda z_cons
        sta $2100+23*40+1
        lda z_pa
        sta $2100+23*40+2
        lda z_trig
        sta $2100+23*40+3
        lda z_sk
        sta $2100+23*40+4
        ; a command to send? the bench writes $89 = 1 to ask for a SIO command frame
        lda $89
        beq main
        lda #0
        sta $89
        jsr siocmd
        jmp main

; --- a SIO command frame: 19200 baud, five bytes $31 $52 $01 $00 $84, the command line low around it
siocmd  lda #$28                ; AUDCTL: channels 3+4 joined, 1.79 MHz clock for channel 3
        sta AUDCTL
        lda #$28                ; 16 bit divisor $0028 + 7 = 47 -> about 19200 baud on the machine's clock (the handler learns it)
        sta AUDF3
        lda #0
        sta AUDF4
        lda #$a0
        sta AUDC3
        sta AUDC4
        lda #$23                ; SKCTL: serial output, asynchronous, keyboard scan stays on
        sta SKCTL
        lda #$34                ; PBCTL: CB2 = 0 (command asserted)
        sta PBCTL
        ldx #0
sendlp  lda frame,x
        sta SEROUT
        ; wait for the byte: about 12 lines of 64 us (a byte at 19200 baud is 520 us)
        ldy #12
w1      lda VCOUNT
w2      cmp VCOUNT
        beq w2
        dey
        bne w1
        inx
        cpx #5
        bne sendlp
        lda #$3c                ; command line high again
        sta PBCTL
        lda #3
        sta SKCTL
        lda #0
        sta AUDCTL
        lda #1
        sta txdone
        rts
frame   .byte $31, $52, $01, $00, $84

nmi     pha
        bit NMIST
        bpl vbi
        txa
        pha
        ldx #0
        lda VCOUNT
        sta WSYNC
        sta COLBK
        pla
        tax
        pla
        rti
vbi     inc frames
        lda #$22
        sta COLBK
        sta NMIRES
        pla
        rti

irq     rti

        * = $FFFA
        .word nmi
        .word reset
        .word irq
