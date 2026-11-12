; T116 S10: the smallest firmware the Next's boot ROM starts (TBBLUE.FW, loaded at 0x6000). It proves that the boot ROM found the card, read the file
; and jumped: two marker bytes at CPU 0x8000 (physical 0x48000, which the bench reads out of the DDR2 model), the border red, then it stands.
        org 0x6000
        di
        ld sp, 0xBFF0
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
        ld a, 2
        out (0xFE), a
done:   jr done
