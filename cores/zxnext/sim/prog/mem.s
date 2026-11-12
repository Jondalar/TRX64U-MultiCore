; T116 S3: the machine's memory as the Z80N uses it - copies between 8 KB pages, 128K style paging, the MMU, the DMA, code that rewrites itself, a 16 KB copy -
; at the CPU speed SPEED (0 = 3.5, 1 = 7, 2 = 14, 3 = 28 MHz). It leaves checksums at CPU 0x8002.. and logs them with OUT (0x77); the bench runs it
; on a one-cycle SRAM model and on the DDR2 behind the caches and compares the final memory image and the log byte for byte.
; The stack and the program live in the display RAM (0x6000-0x7FFF, block RAM inside the machine), so that the 8 KB pages the program maps over
; 0x8000-0xFFFF are free to be remapped.
LOG     equ 0x77
        org 0x6000
        di
        ld sp, 0x7FF0
        nextreg 0x07, SPEED     ; CPU speed
        ld a, 0xA0
        out (LOG), a            ; the first log line: the bench takes its time zero here

        ; ---- 1: 2 KB of a rotate-and-xor sequence in page 20 (at 0xC000), copied with LDIR to page 40 (at 0xE000)
        nextreg 0x56, 20
        nextreg 0x57, 40
        ld hl, 0xC000
        ld bc, 2048
        ld a, 3
f1:     ld (hl), a
        rlca
        xor c
        xor 0x35
        ld e, a
        inc hl
        dec bc
        ld a, b
        or c
        ld a, e
        jr nz, f1
        ld hl, 0xC000
        ld de, 0xE000
        ld bc, 2048
        ldir
        ld hl, 0xE000
        ld bc, 2048
        call csum
        ld a, e
        ld (0x8002), a
        out (LOG), a
        ld a, d
        ld (0x8003), a
        out (LOG), a

        ; ---- 2: LDDR back over the original, in the other direction
        ld hl, 0xE7FF
        ld de, 0xC7FF
        ld bc, 2048
        lddr
        ld hl, 0xC000
        ld bc, 2048
        call csum
        ld a, e
        ld (0x8004), a
        out (LOG), a
        ld a, d
        ld (0x8005), a
        out (LOG), a

        ; ---- 3: 128K style paging: a marker in each of eight banks through port 0x7FFD, then read back in the other order
        ld e, 7
p3w:    ld a, e
        ld bc, 0x7FFD
        out (c), a
        add a, 0x40
        ld (0xC000), a
        dec e
        ld a, e
        cp 0xFF
        jr nz, p3w
        ld e, 0
p3r:    ld a, e
        ld bc, 0x7FFD
        out (c), a
        ld a, (0xC000)
        out (LOG), a
        inc e
        ld a, e
        cp 8
        jr nz, p3r
        ld a, 0
        ld bc, 0x7FFD
        out (c), a

        ; ---- 4: the MMU: pages 20, 30, 40, 50 each get their number at the first byte of the window, then read back in another order
        nextreg 0x56, 20
        ld a, 0x20
        ld (0xC000), a
        nextreg 0x56, 30
        ld a, 0x30
        ld (0xC000), a
        nextreg 0x56, 40
        ld a, 0x40
        ld (0xC000), a
        nextreg 0x56, 50
        ld a, 0x50
        ld (0xC000), a
        nextreg 0x56, 40
        ld a, (0xC000)
        out (LOG), a
        nextreg 0x56, 20
        ld a, (0xC000)
        out (LOG), a
        nextreg 0x56, 50
        ld a, (0xC000)
        out (LOG), a
        nextreg 0x56, 30
        ld a, (0xC000)
        out (LOG), a
        ld (0x8006), a

        ; ---- 5: the DMA copies 1 KB from page 20's window (0xC000) to 0xE400 (page 40's)
        nextreg 0x56, 20
        nextreg 0x57, 40
        ld hl, dmaprog
        ld b, dmaend - dmaprog
        ld c, 0x6B
        otir
        ld hl, 0xE400
        ld bc, 1024
        call csum
        ld a, e
        ld (0x8007), a
        out (LOG), a
        ld a, d
        ld (0x8008), a
        out (LOG), a

        ; ---- 6: code that rewrites itself: ld a,n / ret at 0x8100, n patched from outside the routine every round, the sum of what it returns
        nextreg 0x54, 4
        nextreg 0x55, 5
        ld hl, 0x8100
        ld (hl), 0x3E
        inc hl
        ld (hl), 0
        inc hl
        ld (hl), 0xC9
        ld b, 40
        ld c, 0
smc:    ld a, b
        ld (0x8101), a
        call 0x8100
        add a, c
        ld c, a
        djnz smc
        ld a, c
        ld (0x8009), a
        out (LOG), a
        ; and a routine that patches its own instruction: inc a / ret becomes dec a / ret and back, 25 times
        ld hl, 0x8120
        ld (hl), 0x3C
        inc hl
        ld (hl), 0xC9
        ld b, 25
        ld c, 100
smc2:   ld a, c
        call 0x8120
        ld c, a
        ld a, (0x8120)
        xor 0x01                ; 0x3C (inc a) <-> 0x3D (dec a)
        ld (0x8120), a
        djnz smc2
        ld a, c
        ld (0x800A), a
        out (LOG), a

        ; ---- 7: a 16 KB copy: pages 20 and 21 (0xC000, written with a counting pattern) to pages 40 and 41 (the window 0x8000, slots 4 and 5)
        nextreg 0x56, 20
        nextreg 0x57, 21
        ld hl, 0xC000
        ld de, 0xC001
        ld bc, 16383
        ld (hl), 0x11
        ldir
        ld hl, 0xC000
        ld b, 64
        ld a, 1
c7:     ld (hl), a              ; (a stamp every 256 bytes)
        inc a
        inc h
        djnz c7
        nextreg 0x54, 40
        nextreg 0x55, 41
        ld hl, 0xC000
        ld de, 0x8000
        ld bc, 16384
        ldir
        ld hl, 0x8000
        ld b, 64
        ld c, 0
s7:     ld a, (hl)
        add a, c
        ld c, a
        inc h
        djnz s7
        ld a, c
        out (LOG), a
        nextreg 0x54, 4
        nextreg 0x55, 5
        ld a, c
        ld (0x800B), a

        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
        ld a, 0xEE
        out (LOG), a
done:   jr done

; HL = start, BC = count: E = the sum of the bytes, D = a rotating sum (d = rol(d) + byte)
csum:   ld de, 0
cl:     ld a, (hl)
        add a, e
        ld e, a
        ld a, d
        rlca
        add a, (hl)
        ld d, a
        inc hl
        dec bc
        ld a, b
        or c
        jr nz, cl
        ret

dmaprog:
        db 0x83                 ; WR6: disable the DMA
        db 0x7D                 ; WR0: A to B, the start of port A and the length follow
        dw 0xC000
        dw 1024
        db 0x14                 ; WR1: A is memory, increments
        db 0x28                 ; WR2: B is memory, increments
        db 0xAD                 ; WR4: continuous mode, the start of port B follows
        dw 0xE400
        db 0x82                 ; WR5: stop at the end of the block
        db 0xCF                 ; WR6: load
        db 0x87                 ; WR6: enable
dmaend:
