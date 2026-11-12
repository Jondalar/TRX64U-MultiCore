; SPDX-License-Identifier: GPL-3.0-or-later
; T88 §5: a 16 KB "OS" of our own for the A/B bench of the Atari core's 6502 (tb_atari.vhd): it brings up
; ANTIC (text mode 2, a DLI, player/missile DMA), GTIA colours, a POKEY timer IRQ and the VBI NMI, and then
; polls registers in a loop. Nothing of Atari's. 64tass -b --nostart -o testos.rom testos.asm  (image = $C000-$FFFF)
        * = $C000
        .fill $2000, $FF
        * = $E000           ; character set: any pattern
        .for c = 0, c < 1024, c = c + 1
          .byte ((c * 37) ^ (c >> 3)) & $ff
        .next

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
COLPF1  = $D017
COLPF2  = $D018
GRACTL  = $D01D
HPOSP0  = $D000
SIZEP0  = $D008
COLPM0  = $D012
RANDOM  = $D20A
AUDF1   = $D200
AUDC1   = $D201
AUDCTL  = $D208
STIMER  = $D209
IRQEN   = $D20E
IRQST   = $D20E
SKCTL   = $D20F
SKSTAT  = $D20F

frames  = $80
dlis    = $81
irqs    = $82
tmp     = $84
sink    = $0600

        * = $E400
reset   sei
        cld
        ldx #$ff
        txs
        lda #0
        sta NMIEN
        sta DMACTL
        sta IRQEN
        ; PIA: port B all outputs, then $FF (OS ROM on, BASIC off, self test off)
        sta PBCTL
        lda #$ff
        sta PORTB
        lda #$3c
        sta PBCTL
        sta PACTL
        lda #$ff
        sta PORTB
        ; clear $80-$8f, fill screen $2100.. with a pattern, player data
        ldx #$10
        lda #0
-       sta frames-1,x
        dex
        bne -
        ldx #0
-       txa
        sta $2100,x
        eor #$5a
        sta $2200,x
        sta $2300,x
        txa
        asl
        sta $3400,x
        dex
        bne -
        ; display list at $2000: 3 blank, LMS mode 2, 11 x mode 2, one with DLI, 11 more, JVB
        ldx #0
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
        sta $2006+11
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
        lda #$30
        sta PMBASE
        lda #$0f
        sta COLPF1
        lda #$94
        sta COLPF2
        lda #$46
        sta COLPM0
        lda #$80
        sta HPOSP0
        lda #1
        sta SIZEP0
        lda #3
        sta GRACTL
        ; POKEY timer 1 IRQ
        lda #0
        sta AUDCTL
        lda #$a0
        sta AUDC1
        lda #$7f
        sta AUDF1
        lda #3
        sta SKCTL
        lda #1
        sta IRQEN
        sta STIMER
        ; ANTIC on: DL DMA + P/M DMA (players, missiles) + normal playfield; NMI: VBI + DLI
        lda #$2e
        sta DMACTL
        lda #$c0
        sta NMIEN
        cli
main    inx
        lda VCOUNT
        sta sink
        lda RANDOM
        sta sink+1
        lda $2100,x
        eor $3400,x
        sta sink+2
        lda NMIST
        sta sink+3
        inc sink+4
        lda frames
        sta sink+5
        ldy #8
-       lda $2200,y
        adc sink+4
        sta tmp
        dey
        bne -
        lda IRQST
        sta sink+6
        lda #$ff
        sta sink+7
        jmp main

nmi     pha
        bit NMIST
        bpl vbi
        ; display list interrupt: WSYNC stops the 6502 until the end of the line
        txa
        pha
        ldx dlis
        stx WSYNC
        stx COLBK
        inc dlis
        pla
        tax
        pla
        rti
vbi     inc frames
        lda #0
        sta dlis
        sta NMIRES
        pla
        rti

irq     pha
        lda #0
        sta IRQEN
        lda #1
        sta IRQEN
        inc irqs
        pla
        rti

        * = $FFFA
        .word nmi
        .word reset
        .word irq
