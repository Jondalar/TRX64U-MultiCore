; T116 S5 (board B): the Next's SD driver at 28 MHz, the way the boot ROM's sector read (rom 0x04E8) and the firmware that follows it do it: commands and bytes with no
; delay between the accesses (the CPU is held by the SPI master's wait line, so one transfer follows the other with no gap), CMD17 for block after block with the
; card still selected, 512 bytes by two INIR and the CRC by two INs. NBLK consecutive blocks from LBA0 (the bench filled them with its pattern).
;
;   0x8010 + 4 k: block k: R1, token, CRC16 high, CRC16 low        block k at 0x8100 + 512 k        0x8000/1: A5 5A when done
SPI_CS  equ 0xE7
SPI_DAT equ 0xEB
LBA0    equ 8192                ; low byte 0: the command's last LBA byte counts up
NBLK    equ 6

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

        ld hl, cmd17
        ld de, 0x8080           ; the command is copied: its LBA counts up
        ld bc, 6
        ldir
        ld hl, 0x8010           ; results
        ld (0x8072), hl
        ld hl, 0x8100           ; block
        ld (0x8074), hl
        ld a, NBLK
        ld (0x8076), a
blk:    ld hl, 0x8080
        ld b, 6
fc1:    ld a, (hl)
        out (SPI_DAT), a
        inc hl
        djnz fc1
        in a, (SPI_DAT)
        ld b, 24
fr1:    in a, (SPI_DAT)
        cp 0xFF
        jr nz, gotr1
        djnz fr1
gotr1:  ld (0x8090), a
        ld bc, 0xFFFF
tokw:   in a, (SPI_DAT)
        cp 0xFE
        jr z, gottok
        dec bc
        ld a, b
        or c
        jr nz, tokw
        ld a, 0xFF
gottok: ld (0x8091), a
        ld hl, (0x8074)
        ld bc, SPI_DAT          ; B = 0: 256 bytes
        inir
        inir
        ld (0x8074), hl         ; the next block
        in a, (SPI_DAT)
        ld (0x8092), a
        in a, (SPI_DAT)
        ld (0x8093), a
        ld hl, 0x8090
        ld de, (0x8072)
        ld bc, 4
        ldir
        ld (0x8072), de
        ld hl, 0x8084
        inc (hl)
        ld hl, 0x8076
        dec (hl)
        jr nz, blk

        ld a, 0xFF
        out (SPI_CS), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done

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
cmd17:  db 0x51, 0, 0, LBA0 >> 8, LBA0 & 0xFF, 0x55
