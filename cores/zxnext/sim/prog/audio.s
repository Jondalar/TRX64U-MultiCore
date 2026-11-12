; T116 S7: each source of the Next's sound alone for 100 ms, then 100 ms of silence, each phase announced by OUT (0x77), n so that the bench cuts the sample
; stream where it changes: 1 AY 1 (tone A, period 218), 2 AY 2 (period 109), 3 AY 3 (period 73), 4 the DAC (port 0xFB, a square wave from a delay loop),
; 5 the beeper (port 0xFE bit 4, the same loop), 6 silence, 7 the end. The machine runs at 28 MHz so that the loops are plain T-state counts.
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x07, 3
        nextreg 0x08, 0x1A              ; the internal speaker, the DACs and Turbosound (three AY) on

        ld a, 1
        out (0x77), a
        ld a, 0xFF                      ; AY 1
        ld e, 218
        call ay_tone
        call wait100
        call ay_off

        ld a, 2
        out (0x77), a
        ld a, 0xFE                      ; AY 2
        ld e, 109
        call ay_tone
        call wait100
        call ay_off

        ld a, 3
        out (0x77), a
        ld a, 0xFD                      ; AY 3
        ld e, 73
        call ay_tone
        call wait100
        call ay_off

        ld a, 4
        out (0x77), a
        ld hl, 104
dacl:   ld a, 0xFF
        out (0xFB), a
        call half
        ld a, 0x00
        out (0xFB), a
        call half
        dec hl
        ld a, h
        or l
        jr nz, dacl

        ld a, 5
        out (0x77), a
        ld hl, 104
beepl:  ld a, 0x10
        out (0xFE), a
        call half
        ld a, 0x00
        out (0xFE), a
        call half
        dec hl
        ld a, h
        or l
        jr nz, beepl

        ld a, 6
        out (0x77), a
        call wait100
        ld a, 7
        out (0x77), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done

; A = the chip select value, E = the tone period (low byte; the high byte is 0): tone A on, full volume
ay_tone:
        ld bc, 0xFFFD
        out (c), a
        ld d, 0
        call ayw
        ld d, 1
        ld e, 0
        call ayw
        ld d, 7
        ld e, 0x3E
        call ayw
        ld d, 8
        ld e, 0x0F
        jp ayw
ay_off: ld d, 8
        ld e, 0
; D = register, E = value
ayw:    ld bc, 0xFFFD
        out (c), d
        ld bc, 0xBFFD
        out (c), e
        ret

; 100 ms at 28 MHz: two rounds of 55000 times (dec de, ld a,d, or e, jr nz) = 26 T
wait100:
        ld c, 2
w1:     ld de, 55000
w2:     dec de
        ld a, d
        or e
        jr nz, w2
        dec c
        jr nz, w1
        ret

; half a period of the square waves: 4 rounds of a 256 step DJNZ loop
half:   ld c, 4
hh:     ld b, 0
hb:     djnz hb
        dec c
        jr nz, hh
        ret
