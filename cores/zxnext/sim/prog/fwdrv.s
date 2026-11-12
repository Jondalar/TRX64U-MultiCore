; T116 S5 (board C): the SD driver of the Next's own firmware (TBBLUE.FW, the code that prints "Error reading TBBLUE.FW data!"), written down from its
; instructions (module at 0x6000: select 0x791A, send_cmd 0x79FB, rcvr_datablock 0x792F, disk_read 0x7C39), not from anyone's comment, as our own program:
;   - 28 MHz (nextreg 7 = 3), one IN or OUT of port 0xEB per byte, a command is OUT E7=FE, one IN, six OUT; CMD12 is followed by eight INs; the R1 is polled for 250 INs
;   - a block: poll for a byte other than 0xFF at most 5000 INs, it has to be 0xFE; then 512 bytes by the loop IN / LD (BC),A / INC BC / DEC DE / LD A,D / OR E / JR NZ,
;     then two INs for the CRC (not checked); the next block of a CMD18 follows at once; a block that fails ends the loop
;   - after the loop CMD12 (its result is not looked at), then OUT E7=FF and one IN
; Two clusters of eight blocks are read, a CMD17 in front and between: LBA 8192.. (CMD18 of 8), LBA 8200.. (CMD18 of 8), LBA 8208 (CMD17), as FatFS does for a file in
; two runs. Results: 0x8100 + k: 1 = block k came in, 0 = the token poll gave up (k = 0..16, 17 blocks), 0x8200 + 2 k: the polls the token took (word), data of
; block k at 0x8400 + 512 k (k < 17: up to 0xA3FF), 0x8001 = 5A when done.
SPI_CS  equ 0xE7
SPI_DAT equ 0xEB
LBA0    equ 8192
; POLLS is given by the bench (--define POLLS=n): 5000 is rcvr_datablock's limit (0x1388); fewer make a slower application count as the firmware's would

        org 0x6000
        di
        ld sp, 0xBFF0
        ld a, 0xFE
        out (SPI_CS), a
        ld b, 12
dummy:  in a, (SPI_DAT)
        push bc
        ld b, 4
dw:     djnz dw
        pop bc
        djnz dummy
        ld hl, cmd0
        call icmd
        ld hl, cmd8
        call icmd
        call irx4
        ld hl, cmd55
        call icmd
        ld hl, acmd41
        call icmd
        ld hl, cmd58
        call icmd
        call irx4

        nextreg 0x07, 3         ; 28 MHz

        ld hl, 0x8400
        ld (0x8070), hl         ; where the next block goes
        xor a
        ld (0x8072), a          ; k

        ; ---- two runs of eight blocks (CMD18) and a single block (CMD17)
        ld de, LBA0
        ld bc, 8
        call dread
        ld de, LBA0 + 8
        ld bc, 8
        call dread
        ld de, LBA0 + 16
        ld bc, 1
        call dread

        ld a, 0xFF
        out (SPI_CS), a
        ld a, 0xA5
        ld (0x8000), a
        ld a, 0x5A
        ld (0x8001), a
done:   jr done

; ---- disk_read: DE = LBA (low 16 bits; the high word is 0), BC = count
dread:  ld (0x8074), de
        ld (0x8076), bc
        dec bc
        ld a, b
        or c
        ld a, 0x51              ; CMD17 for one block
        jr z, dr1
        ld a, 0x52              ; CMD18
dr1:    ld (0x807A), a
        call sendc              ; A = command, LBA in 0x8074
        or a
        jr nz, drend
drblk:  call rblock             ; Z set = failed
        jr z, drstop
        ld hl, (0x8076)
        dec hl
        ld (0x8076), hl
        ld a, h
        or l
        jr nz, drblk
drstop: ld a, (0x807A)
        cp 0x52
        jr nz, drend
        ld a, 12
        call sendc12
drend:  ld a, 0xFF
        out (SPI_CS), a
        in a, (SPI_DAT)
        ret

; ---- one byte out (0x786A is IN/OUT with 50 T-states between two accesses; the SPI master takes 16 CPU clocks for a byte and does not hold the CPU, so
; a second access closer than that is lost, on the board as here)
tx:     out (SPI_DAT), a
        push bc
        ld b, 3
txd:    djnz txd
        pop bc
        ret

; ---- select (0x791A): CS low, one IN
sel:    ld a, 0xFE
        out (SPI_CS), a
        in a, (SPI_DAT)
        ret

; ---- send_cmd (0x79FB) for CMD17/18 with the LBA of 0x8074 (command number in A); returns R1 in A (0xFF when none came)
sendc:  push af
        call sel
        pop af
        or 0x40
        call tx
        xor a
        call tx                 ; argument: 0, 0, hi, lo (block addressing)
        call tx
        ld a, (0x8075)
        call tx
        ld a, (0x8074)
        call tx
        ld a, 0x01              ; CRC byte (not checked by the card)
        call tx
        jr r1poll
sendc12:
        call sel
        ld a, 0x4C
        call tx
        xor a
        call tx
        call tx
        call tx
        call tx
        ld a, 0x01
        call tx
        ld b, 8                 ; CMD12: eight INs thrown away (firmware 0x7AB0)
sk8:    in a, (SPI_DAT)
        djnz sk8
r1poll: ld bc, 250              ; 0x78C6: IN until a byte other than 0xFF, 250 times
r1l:    in a, (SPI_DAT)
        cp 0xFF
        ret nz
        dec bc
        ld a, b
        or c
        jr nz, r1l
        ld a, 0xFF
        ret

; ---- rcvr_datablock (0x792F) into (0x8070): Z set = the token did not come or was not 0xFE
rblock: ld bc, POLLS
        ld hl, 0
tkl:    inc hl
        in a, (SPI_DAT)
        cp 0xFF
        jr nz, gotk
        push bc                 ; the firmware's poll is about 360 T-states (0x7940-0x7962: the call of 0x787F with a count of one, its stack work, the IX counter)
        ld b, 22
tkd:    djnz tkd
        pop bc
        dec bc
        ld a, b
        or c
        jr nz, tkl
gotk:   ld (0x807C), a
        ld a, (0x8072)          ; k
        ld e, a
        ld d, 0
        ex de, hl               ; DE = polls, HL = k
        add hl, hl
        ld bc, 0x8200
        add hl, bc
        ld (hl), e
        inc hl
        ld (hl), d
        ld a, (0x8072)
        ld l, a
        ld h, 0
        ld bc, 0x8100
        add hl, bc
        ld a, (0x807C)
        cp 0xFE
        jr z, tokok
        ld (hl), 0
        xor a                   ; Z set
        ret
tokok:  ld (hl), 1
        ld hl, (0x8070)
        ld b, h
        ld c, l
        ld de, 512
rd1:    in a, (SPI_DAT)         ; 0x787F
        ld (bc), a
        inc bc
        dec de
        ld a, d
        or e
        jr nz, rd1
        ld (0x8070), bc
        ld bc, 0x807E           ; the CRC: two bytes through the same loop (0x787F with a count of two; INs closer than 16 clocks are lost)
        ld de, 2
rd2:    in a, (SPI_DAT)
        ld (bc), a
        inc bc
        dec de
        ld a, d
        or e
        jr nz, rd2
        ld a, (0x8072)
        inc a
        ld (0x8072), a
        or 1                    ; Z clear
        ret

; ---- initialisation, as the boot ROM leaves the card
icmd:   ld b, 6
icl:    ld a, (hl)
        out (SPI_DAT), a
        inc hl
        djnz icl
        in a, (SPI_DAT)
        ld b, 24
ir1:    in a, (SPI_DAT)
        cp 0xFF
        ret nz
        djnz ir1
        ret
irx4:   ld b, 4
i4:     in a, (SPI_DAT)
        push bc
        ld b, 4
i4d:    djnz i4d
        pop bc
        djnz i4
        ret

cmd0:   db 0x40, 0, 0, 0, 0, 0x95
cmd8:   db 0x48, 0, 0, 0x01, 0xAA, 0x87
cmd55:  db 0x77, 0, 0, 0, 0, 0x65
acmd41: db 0x69, 0x40, 0, 0, 0, 0x77
cmd58:  db 0x7A, 0, 0, 0, 0, 0xFD
