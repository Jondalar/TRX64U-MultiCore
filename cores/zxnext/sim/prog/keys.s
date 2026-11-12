; T116 S8: what the machine sees of the host's input: the eight half-rows of the keyboard matrix (port 0xFE, the row in the high byte), the sixteen extended keys
; (nextreg 0xB0 and 0xB1), the joysticks as Kempston 1 on port 0x1F (the left stick) and Kempston 2 on port 0x37 (the right), the Kempston mouse (ports 0xFBDF X,
; 0xFFDF Y, 0xFADF buttons). It samples all of it again and again into 0x8100.. and counts the passes in 0x8120; the bench changes the input between passes.
;   0x8100-0x8107 half-rows 0xFEFE .. 0x7FFE (low five bits)   0x8110 nextreg 0xB0   0x8111 nextreg 0xB1   0x8112 port 0x1F   0x8113 port 0x37
;   0x8114 mouse X   0x8115 mouse Y   0x8116 mouse buttons
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x07, 3                 ; 28 MHz
        nextreg 0x05, 0x42              ; joystick 1 = Kempston 1 (port 0x1F), joystick 2 = Kempston 2 (port 0x37)
        ld a, 0
        ld (0x8120), a
pass:   ld b, 0xFE
        ld hl, 0x8100
        ld d, 8
rows:   ld c, 0xFE
        in a, (c)
        and 0x1F
        ld (hl), a
        inc hl
        rlc b
        dec d
        jr nz, rows
        ld bc, 0x243B
        ld a, 0xB0
        out (c), a
        ld bc, 0x253B
        in a, (c)
        ld (0x8110), a
        ld bc, 0x243B
        ld a, 0xB1
        out (c), a
        ld bc, 0x253B
        in a, (c)
        ld (0x8111), a
        ld bc, 0x001F
        in a, (c)
        ld (0x8112), a
        ld bc, 0x0037
        in a, (c)
        ld (0x8113), a
        ld bc, 0xFBDF
        in a, (c)
        ld (0x8114), a
        ld bc, 0xFFDF
        in a, (c)
        ld (0x8115), a
        ld bc, 0xFADF
        in a, (c)
        ld (0x8116), a
        ld a, (0x8120)
        inc a
        ld (0x8120), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
        jp pass
