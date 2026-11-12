; T116 S5 (board B): a multi-block read that is stopped, followed at once by a single-block read, at 28 MHz with no gap between the SPI accesses: CMD18 of
; LBA_A, three blocks, CMD12 straight after the third block's CRC (the card has asked the application for the fourth by then), then CMD17 of LBA_B and
; CMD17 of LBA_C. The application is slow (the bench's +LAT): its answer to the fourth block arrives after the host has moved on, and must not be taken for
; the answer to LBA_B.
;
;   0x8010 R1 of CMD18    blocks 0..2 of the CMD18: token, CRC16 high, low at 0x8011, 0x8015, 0x8019 (data at 0x8100 + 512 k)    0x8030 R1 of CMD12
;   0x8034 R1, 0x8035 token, 0x8036/7 CRC of LBA_B (data at 0x8700)    0x8038 R1, 0x8039 token, 0x803A/B CRC of LBA_C (data at 0x8900)
SPI_CS  equ 0xE7
SPI_DAT equ 0xEB
LBA_A   equ 8192
LBA_B   equ 8200
LBA_C   equ 8201

        org 0x6000
        di
        ld sp, 0xBFF0
        ld a, 0xFE
        out (SPI_CS), a
        ld b, 12
dummy:  call rx
        djnz dummy
        ld hl, cmd0
        call cmd
        ld hl, cmd8
        call cmd
        call rx
        call rx
        call rx
        call rx
        ld hl, cmd55
        call cmd
        ld hl, acmd41
        call cmd
        ld hl, cmd58
        call cmd
        call rx
        call rx
        call rx
        call rx

        nextreg 0x07, 3         ; 28 MHz

        ; ---- CMD18, three blocks
        ld hl, cmd18
        call fcmd
        ld (0x8010), a
        ld hl, 0x8100
        ld de, 0x8011           ; token, CRC high, CRC low of block 0 at 0x8011..13
        call fblk
        ld de, 0x8015
        call fblk
        ld de, 0x8019
        call fblk
        ; ---- CMD12 at once
        ld hl, cmd12
        call fcmd
        ld (0x8030), a
        ; ---- CMD17 of LBA_B and LBA_C
        ld hl, cmd17b
        call fcmd
        ld (0x8034), a
        ld hl, 0x8700
        ld de, 0x8035
        call fblk
        ld hl, cmd17c
        call fcmd
        ld (0x8038), a
        ld hl, 0x8900
        ld de, 0x8039
        call fblk

        ld a, 0xFF
        out (SPI_CS), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done

; the six bytes at HL back to back, the byte of the transfer before, then the R1 (a byte other than 0xFF within 24 reads): A = R1
fcmd:   ld b, 6
fc1:    ld a, (hl)
        out (SPI_DAT), a
        inc hl
        djnz fc1
        in a, (SPI_DAT)
        ld b, 24
fr1:    in a, (SPI_DAT)
        cp 0xFF
        ret nz
        djnz fr1
        ret

; the token, 512 bytes to HL, the CRC: (DE) = token, (DE+1) = CRC high, (DE+2) = CRC low. HL is not kept.
fblk:   ld bc, 0xFFFF
ftw:    in a, (SPI_DAT)
        cp 0xFE
        jr z, ftok
        dec bc
        ld a, b
        or c
        jr nz, ftw
        ld a, 0xFF
ftok:   ld (de), a
        inc de
        ld bc, SPI_DAT          ; B = 0: 256 bytes
        inir
        inir
        in a, (SPI_DAT)
        ld (de), a
        inc de
        in a, (SPI_DAT)
        ld (de), a
        ret

tx:     out (SPI_DAT), a
        jr wait16
rx:     in a, (SPI_DAT)
wait16: push bc
        ld b, 4
w16:    djnz w16
        pop bc
        ret

cmd:    ld b, 6
cmdl:   ld a, (hl)
        call tx
        inc hl
        djnz cmdl
        call rx
        ld b, 24
r1w:    call rx
        cp 0xFF
        ret nz
        djnz r1w
        ret

cmd0:   db 0x40, 0, 0, 0, 0, 0x95
cmd8:   db 0x48, 0, 0, 0x01, 0xAA, 0x87
cmd55:  db 0x77, 0, 0, 0, 0, 0x65
acmd41: db 0x69, 0x40, 0, 0, 0, 0x77
cmd58:  db 0x7A, 0, 0, 0, 0, 0xFD
cmd18:  db 0x52, 0, 0, LBA_A >> 8, LBA_A & 0xFF, 0x55
cmd12:  db 0x4C, 0, 0, 0, 0, 0x61
cmd17b: db 0x51, 0, 0, LBA_B >> 8, LBA_B & 0xFF, 0x55
cmd17c: db 0x51, 0, 0, LBA_C >> 8, LBA_C & 0xFF, 0x55
