; T116 S5: the Next talks SPI to its SD card the way the boot ROM and NextZXOS do (ports 0xE7 chip select, 0xEB data): the init commands, one block read
; (CMD17), one block write (CMD24) and the same block read back. Everything the card says is stored at CPU 0x8010 and up (physical 0x48010); the
; bench knows what the image holds and what this program wrote and compares. The last thing the program does is the marker A5 5A at 0x8000.
;
;   0x8010 CMD0 R1          0x8011 CMD8 R1       0x8012-0x8015 CMD8 R7 (00 00 01 AA)
;   0x8016 CMD55 R1         0x8017 ACMD41 R1     0x8018 CMD58 R1     0x8019-0x801C OCR
;   0x8020 CMD17 R1         0x8021 data token    0x8022 CRC16 high   0x8023 CRC16 low    block at 0x8100-0x82FF
;   0x8024 CMD24 R1         0x8025 data response 0x8026 busy bytes (low)  0x8027 busy bytes (high)
;   0x8028 read back: R1    0x8029 token         0x802A CRC high     0x802B CRC low      block at 0x8500-0x86FF
;   0x802C CMD17 beyond the image: R1   0x802D token   0x802E first data byte   0x802F last data byte (block at 0x8800-0x89FF)
;   written block at 0x8400-0x85FF is the program's own pattern: byte i = (3 i + 5) mod 256
SPI_CS  equ 0xE7
SPI_DAT equ 0xEB
RDLBA   equ 8192                ; a block of the image that the bench filled with its pattern (tools/zxnext_progs.py --fill)
WRLBA   equ 12000               ; a block of free space in the image

        org 0x6000
        di
        ld sp, 0xBFF0
        ld a, 0xFE              ; card 0 selected
        out (SPI_CS), a
        ld b, 12                ; some clocks with nothing on the line
dummy:  call rx
        djnz dummy

        ld hl, cmd0
        call cmd
        ld (0x8010), a
        ld hl, cmd8
        call cmd
        ld (0x8011), a
        call rx
        ld (0x8012), a
        call rx
        ld (0x8013), a
        call rx
        ld (0x8014), a
        call rx
        ld (0x8015), a
        ld hl, cmd55
        call cmd
        ld (0x8016), a
        ld hl, acmd41
        call cmd
        ld (0x8017), a
        ld hl, cmd58
        call cmd
        ld (0x8018), a
        call rx
        ld (0x8019), a
        call rx
        ld (0x801A), a
        call rx
        ld (0x801B), a
        call rx
        ld (0x801C), a

        ; ---- read a block of the image
        ld hl, cmd17r
        call cmd
        ld (0x8020), a
        ld de, 0x8100
        call getblock
        ld (0x8021), a
        ld a, h
        ld (0x8022), a
        ld a, l
        ld (0x8023), a

        ; ---- the pattern to write: byte i = 3 i + 5
        ld hl, 0x8400
        ld bc, 512
        ld a, 5
fillp:  ld (hl), a
        add a, 3
        inc hl
        dec bc
        push af
        ld a, b
        or c
        jr z, filled
        pop af
        jr fillp
filled: pop af

        ; ---- write it
        ld hl, cmd24w
        call cmd
        ld (0x8024), a
        ld a, 0xFF
        call tx                 ; one byte of gap, then the token
        ld a, 0xFE
        call tx
        ld hl, 0x8400
        ld bc, 512
wloop:  ld a, (hl)
        call tx
        inc hl
        dec bc
        ld a, b
        or c
        jr nz, wloop
        ld a, 0xFF              ; two CRC bytes (the card does not check them)
        call tx
        ld a, 0xFF
        call tx
        call rx                 ; the byte shifted in during the last CRC byte
        call rx                 ; the data response
        ld (0x8025), a
        ld bc, 0                ; busy: the card holds the line low until the block is written
busy:   call rx
        cp 0xFF
        jr z, busydone
        inc bc
        ld a, b
        cp 0xFF
        jr nz, busy
busydone:
        ld a, c
        ld (0x8026), a
        ld a, b
        ld (0x8027), a

        ; ---- read it back
        ld hl, cmd17w
        call cmd
        ld (0x8028), a
        ld de, 0x8500
        call getblock
        ld (0x8029), a
        ld a, h
        ld (0x802A), a
        ld a, l
        ld (0x802B), a

        ; ---- a block beyond the end of the image: the app answers with zeros, the card still sends token and block
        ld hl, cmd17x
        call cmd
        ld (0x802C), a
        ld de, 0x8800
        call getblock
        ld (0x802D), a
        ld a, (0x8800)
        ld (0x802E), a
        ld a, (0x89FF)
        ld (0x802F), a

        ld a, 0xFF              ; card deselected
        out (SPI_CS), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done

; ---- SPI. A transfer takes 16 CPU clocks whatever the speed: the delay after each access is at least that.
tx:     out (SPI_DAT), a
        jr wait16
rx:     in a, (SPI_DAT)         ; the byte of the transfer before; starts the next one (sending 0xFF)
wait16: push bc
        ld b, 4
w16:    djnz w16
        pop bc
        ret

; send the six bytes at HL, then wait for the R1 (a byte other than 0xFF within 24 reads): A = R1 (0xFF if none)
cmd:    ld b, 6
cmdl:   ld a, (hl)
        call tx
        inc hl
        djnz cmdl
        call rx                 ; (the byte shifted in while the CRC went out)
        ld b, 24
r1w:    call rx
        cp 0xFF
        ret nz
        djnz r1w
        ret

; wait for the data token (0xFE), then 512 bytes to DE and the two CRC bytes (HL = CRC high, low): A = the token or 0xFF
getblock:
        ld bc, 0xFFFF
tokw:   call rx
        cp 0xFE
        jr z, gottok
        dec bc
        push af
        ld a, b
        or c
        jr z, notok
        pop af
        jr tokw
notok:  pop af
        ld a, 0xFF
        ret
gottok: ld bc, 512
gb:     call rx
        ld (de), a
        inc de
        dec bc
        ld a, b
        or c
        jr nz, gb
        call rx
        ld h, a
        call rx
        ld l, a
        ld a, 0xFE
        ret

cmd0:   db 0x40, 0, 0, 0, 0, 0x95
cmd8:   db 0x48, 0, 0, 0x01, 0xAA, 0x87
cmd55:  db 0x77, 0, 0, 0, 0, 0x65
acmd41: db 0x69, 0x40, 0, 0, 0, 0x77
cmd58:  db 0x7A, 0, 0, 0, 0, 0xFD
cmd17r: db 0x51, 0, 0, RDLBA >> 8, RDLBA & 0xFF, 0x55
cmd24w: db 0x58, 0, 0, WRLBA >> 8, WRLBA & 0xFF, 0x55
cmd17w: db 0x51, 0, 0, WRLBA >> 8, WRLBA & 0xFF, 0x55
cmd17x: db 0x51, 0x00, 0x7F, 0xFF, 0xFF, 0x55
