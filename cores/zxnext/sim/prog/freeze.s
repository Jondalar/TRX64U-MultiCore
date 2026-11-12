; T116 S9: a program that keeps the machine and the DDR2 busy at a steady rhythm so that a Freeze can be seen in it: 150 rounds, each of them logs its number
; (OUT 0x77), copies 64 bytes from one 8 KB page to another (DDR2 writes) and waits a fixed number of instructions. The bench runs it with and without
; Freezes at chosen rounds and compares the logs, master cycle for master cycle.
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x07, 3                 ; 28 MHz
        nextreg 0x56, 20
        nextreg 0x57, 21
        ld a, 0
        ld (0x7F00), a
round:  ld a, (0x7F00)
        inc a
        ld (0x7F00), a
        out (0x77), a
        ld (0xC000), a
        ld hl, 0xC000
        ld de, 0xE000
        ld bc, 64
        ldir
        ld b, 200
wt:     djnz wt
        ld a, (0x7F00)
        cp 150
        jr nz, round
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done
