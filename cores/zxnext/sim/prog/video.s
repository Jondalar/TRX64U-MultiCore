; T116 S4/S6: Layer 2 in the mode MODE (0 = 256x192 8 bit, 1 = 320x256 8 bit, 2 = 640x256 4 bit), filled through the MMU with a pattern that is a
; function of the byte's offset n in the Layer 2 memory: byte(n) = ((n and 255) + 3 * (n >> 8) + 1) and 255, 0xE3 (the transparent colour) replaced by 0xE4.
; The Layer 2 palette is the identity (index i = RGB332 i, as the nextreg 0x41 write makes it), so the bench can say which colour every dot must have.
; The last things the program does: Layer 2 on (port 0x123B bit 1) and the marker A5 5A at 0x8000; then it stands and the machine draws frames.
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x07, 3                 ; 28 MHz: the fill is 80 KB
        ; the palette: Layer 2 first palette, index i = value i
        nextreg 0x43, 0x10
        nextreg 0x40, 0
        ld c, 0
pal:    ld a, c
        nextreg 0x41, a
        inc c
        jr nz, pal
        ; the clip window and the mode
        nextreg 0x70, MODE * 16
        nextreg 0x1C, 1                 ; reset the Layer 2 clip index
        nextreg 0x18, 0
        nextreg 0x18, CLIPX2
        nextreg 0x18, 0
        nextreg 0x18, CLIPY2
        ; the fill: NPAGES pages of 8 KB, 16 up (Layer 2 active bank 8), through the window at 0xC000 (slot 6)
        ld e, 0                         ; the page counter
        ld d, 1                         ; d = 3 * (n >> 8) + 1: 96 more after each page
fp:     ld a, e
        add a, 16
        nextreg 0x56, a
        ld hl, 0xC000
        ld b, 32
row:    ld c, 0
col:    ld a, d
        add a, c
        cp 0xE3
        jr nz, ok
        inc a
ok:     ld (hl), a
        inc hl
        inc c
        jr nz, col
        ld a, d
        add a, 3
        ld d, a
        djnz row
        inc e
        ld a, e
        cp NPAGES
        jr nz, fp
        ld bc, 0x123B
        ld a, 2
        out (c), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done
