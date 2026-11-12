; T89.1 bench program: the ProDOS block device in slot 7. Written for this project (TREX), no Apple ROM call; the card's ROM (rtl/roms/hdd.a65) is the unit under test.
; The bench writes the arguments into the mailbox and then MB (the command); the program does it and clears MB.
;   MB = $0200   +0 command (0 idle)  +1 unit byte ($70 unit 1, $F0 unit 2)  +2/+3 buffer lo/hi  +4/+5 block lo/hi  +6 the block command (0 status, 1 read, 2 write)
;   1 CALL   the ProDOS entry of the card ($C70A) with $42..$47 set from the mailbox; A, X, Y and the carry come back in R_A, R_X, R_Y, R_C
;   2 FILL   512 bytes at the buffer: byte j of page 0 is j xor MB+4, of page 1 j xor MB+4 xor $55
;   3 BOOT   JMP $C700: the card loads block 0 to $0800 and jumps to $0801, or goes on to slot 6
        .include "apple.inc"
MB      = $0200
R_A     = $0210
R_X     = $0211
R_Y     = $0212
R_C     = $0213
BOOTS   = $0214
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        inc BOOTS
        stz MB
main:   lda MB
        beq main
        cmp #1
        bne c2
        jmp do_call
c2:     cmp #2
        bne c3
        jmp do_fill
c3:     cmp #3
        bne done
        jmp $C700
done:   stz MB
        jmp main

do_call:
        lda MB+6
        sta $42
        lda MB+1
        sta $43
        lda MB+2
        sta $44
        lda MB+3
        sta $45
        lda MB+4
        sta $46
        lda MB+5
        sta $47
        jsr $C70A
        sta R_A
        stx R_X
        sty R_Y
        php
        pla
        and #1
        sta R_C
        jmp done

do_fill:
        lda MB+2
        sta PTR
        lda MB+3
        sta PTR+1
        ldy #0
f1:     tya
        eor MB+4
        sta (PTR),y
        iny
        bne f1
        inc PTR+1
f2:     tya
        eor MB+4
        eor #$55
        sta (PTR),y
        iny
        bne f2
        jmp done

        * = $FFFA
        .word reset, reset, reset
