; T116 S9: the machine in the timing TIMING (1 = 48K, 2 = 128K, 3 = +3, 4 = Pentagon: nextreg 0x03 bits 6:4 with bit 7 set) and nothing else; the bench measures the
; length of its frame. The marker A5 5A at 0x8000 says that the timing is set.
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x03, 0x80 + TIMING * 16
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done
