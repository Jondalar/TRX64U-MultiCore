; T89.1 bench program: the Disk II (slot 6) driven the way DOS 3.3's RWTS does, one command at a time from a mailbox in main RAM.
; Written for this project (TREX), no Apple ROM call. The bench (tb_a2_woz.vhd) is the application: it writes MB+1/MB+2 and then MB (the
; command), the program does it and clears MB. Results go to BUF ($2000..) and to LOG.
;   MB = $0200   +0 command (0 = idle), +1 argument, +2 argument 2
;   1 READ   MB+1 pages (256 bytes each) of latch bytes into BUF: LDA $C08C / BPL, 17 cycles a byte
;   2 STEP   MB+1: b7 direction (1 = towards track 0), b6:0 count of half-track steps (next magnet on, previous off)
;   3 PHASE  MB+1: b7 on/off (1 = on), b1:0 the magnet (a raw switch, for a quarter-track position)
;   4 RECAL  48 half steps down, then magnet 0 only: the head is at quarter track 0
;   5 WRITE  27 bytes: FF x6 (self sync, 40 cycles apart), D5 AA AD, A0..AF, FF x2; loads 32 cycles apart for data and 40 for sync
;   6 MOTOR  MB+1: 1 on, 0 off
        .include "apple.inc"
MB      = $0200
BUF     = $2000
PHI     = $10                   ; the magnet that is on
CNT     = $11
OLD2    = $12
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        stz MB
        stz MB+1
        stz MB+2
        stz PHI
        lda $C0EA               ; drive 1
        lda $C0E9               ; motor on
        lda $C0EE               ; Q7 off: read mode
        lda $C0EC               ; Q6 off
main:   lda MB
        beq main
        cmp #1
        bne c2
        jmp do_read
c2:     cmp #2
        bne c3
        jmp do_step
c3:     cmp #3
        bne c4
        jmp do_phase
c4:     cmp #4
        bne c5
        jmp do_recal
c5:     cmp #5
        bne c6
        jmp do_write
c6:     cmp #6
        bne done
        jmp do_motor
done:   stz MB
        jmp main

do_read:
        lda #<BUF
        sta PTR
        lda #>BUF
        sta PTR+1
        lda MB+1
        sta CNT
        lda $C0EE               ; read mode
        ldy #0
rd1:    lda $C0EC
        bpl rd1
        sta (PTR),y
        iny
        bne rd1
        inc PTR+1
        dec CNT
        bne rd1
        jmp done

do_step:
        lda MB+1
        and #$7F
        sta CNT
st1:    jsr onestep
        dec CNT
        bne st1
        jmp done

; one half step: direction from MB+1 b7; the next magnet on, the previous one off
onestep:
        lda PHI
        asl
        sta OLD2
        lda MB+1
        bmi down
        lda PHI
        inc a
        and #3
        bra newphi
down:   lda PHI
        dec a
        and #3
newphi: sta PHI
        asl
        tax
        lda $C0E1,x             ; the new magnet on
        ldx OLD2
        lda $C0E0,x             ; the old one off
        rts

do_phase:
        lda MB+1
        and #3
        asl
        tax
        lda MB+1
        bpl ph_off
        inx
ph_off: lda $C0E0,x
        jmp done

do_recal:
        lda #$80
        sta MB+1
        lda #48
        sta CNT
rc1:    jsr onestep
        dec CNT
        bne rc1
        ; magnet 0 only
        lda $C0E0
        lda $C0E2
        lda $C0E4
        lda $C0E6
        lda $C0E1
        stz PHI
        jmp done

do_motor:
        lda MB+1
        beq mot_off
        lda $C0E9
        jmp done
mot_off:
        lda $C0E8
        jmp done

wb      .macro
        lda #\1
        sta $C0ED               ; Q6 set while Q7 is set: the write latch is loaded
        cmp $C0EC               ; Q6 off: shift
        .rept (\2 - 10) / 2
        nop
        .next
        .endm

do_write:
        lda $C0ED               ; Q6 on (sense write protect)
        lda $C0EE               ; Q7 off
        ; the first byte: Q7 on loads the latch while Q6 is on, then the shift
        lda #$FF
        sta $C0EF
        cmp $C0EC
        .rept (40 - 10) / 2
        nop
        .next
        #wb $FF, 40
        #wb $FF, 40
        #wb $FF, 40
        #wb $FF, 40
        #wb $FF, 40
        #wb $D5, 32
        #wb $AA, 32
        #wb $AD, 32
        #wb $A0, 32
        #wb $A1, 32
        #wb $A2, 32
        #wb $A3, 32
        #wb $A4, 32
        #wb $A5, 32
        #wb $A6, 32
        #wb $A7, 32
        #wb $A8, 32
        #wb $A9, 32
        #wb $AA, 32
        #wb $AB, 32
        #wb $AC, 32
        #wb $AD, 32
        #wb $AE, 32
        #wb $AF, 32
        #wb $FF, 40
        #wb $FF, 40
        nop
        lda $C0EE               ; Q7 off: back to read mode
        jmp done

        * = $FFFA
        .word reset, reset, reset
