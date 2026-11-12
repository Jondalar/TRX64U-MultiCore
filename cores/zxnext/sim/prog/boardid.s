; T116 D10: the board id the machine reports. NextReg 0x0F is read through ports 0x243B / 0x253B and stored at 0x8002; the marker A5 5A at 0x8000 says it is stored.
        org 0x6000
        di
        ld sp, 0x7FF0
        ld bc, 0x243B
        ld a, 0x0F
        out (c), a
        ld bc, 0x253B
        in a, (c)
        ld (0x8002), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done
