; EasyFlash write stimulus for easyflash_eapi_test.py (64tass).
;
; Unlike the 1541 Ultimate suite this goes through the cartridge's own, stock
; EAPI (eapi-am29f040): the cartridge model emulates the Am29F040 command sequences,
; so the driver is exercised exactly as a game would use it.
;
; Runs from RAM at $0800, copied there by the boot code in bank 0 ROMH. It runs
; once per cartridge load, with no input, and leaves its result in screen RAM,
; which the harness reads over JTAG (tools/trxdbg.py, C64 RAM):
;
;   $0400   'R' while running, 'D' when done
;   $0401   status: 1 = sector was blank or stale, erased, pattern written and
;                       read back
;                   2 = pattern from an earlier run found (it survived the
;                       write-back to the CRT file), sector erased again so
;                       the next run starts blank
;                   $81 = EAPIInit failed (driver did not recognise the chip)
;   $0402   carry after EAPIInit (0 = chip supported)
;   $0403   A after EAPIInit (device ID, or error reason)
;   $0404   X after EAPIInit (manufacturer ID)
;   $0405   Y after EAPIInit (number of banks)
;   $0406   carry after erasing the ROML sector
;   $0407   carry after erasing the ROMH sector
;   $0408   EAPIWriteFlash calls that returned an error
;   $0409   pattern bytes that did not read back
;   $040A   bytes not $FF after the erase (first 16 of each bank, both chips)
;
; The test sector is the one starting at bank 8, a whole erase sector (8 banks
; of one chip) away from bank 0, which holds this code and the EAPI.

EAPI_SRC        = $b800         ; bank 0 ROMH in 16K mode
EAPI_RAM        = $c000
EAPIInit        = EAPI_RAM + $14
EAPIWriteFlash  = $df80
EAPIEraseSector = $df83
EAPISetBank     = $df86

EF_BANK         = $de00
EF_MODE         = $de02
EF_16K          = $07

TEST_BANK       = 8
SECTOR_BANKS    = 8
PATTERN_LEN     = 16

RESULT          = $0400
R_STATE         = RESULT + $00
R_STATUS        = RESULT + $01
R_INIT_C        = RESULT + $02
R_INIT_A        = RESULT + $03
R_INIT_X        = RESULT + $04
R_INIT_Y        = RESULT + $05
R_ERASE_LO      = RESULT + $06
R_ERASE_HI      = RESULT + $07
R_WRITE_ERR     = RESULT + $08
R_VERIFY_ERR    = RESULT + $09
R_BLANK_ERR     = RESULT + $0a
RESULT_LEN      = $0b

STATE_RUNNING   = $52           ; 'R'
STATE_DONE      = $44           ; 'D'

        * = $0800

start   sei
        cld
        ldx #$ff
        txs
        lda #$2f
        sta $00
        lda #$37                ; both ROM areas visible, as EAPI requires
        sta $01

        lda #0
        ldx #RESULT_LEN - 1
-       sta RESULT,x
        dex
        bpl -
        lda #STATE_RUNNING
        sta R_STATE

        ; bank 0 in 16K mode puts the EAPI image at $B800-$BAFF
        lda #0
        sta EF_BANK
        lda #EF_16K
        sta EF_MODE
        ldy #0
-       lda EAPI_SRC,y
        sta EAPI_RAM,y
        lda EAPI_SRC + $100,y
        sta EAPI_RAM + $100,y
        lda EAPI_SRC + $200,y
        sta EAPI_RAM + $200,y
        iny
        bne -

        jsr EAPIInit
        sta R_INIT_A
        stx R_INIT_X
        sty R_INIT_Y
        lda #0
        rol a
        sta R_INIT_C
        beq initok
        lda #$81
        jmp finish

initok  jsr select_test_bank
        ldx #PATTERN_LEN - 1
-       lda $8000,x
        cmp pattern,x
        bne fresh
        lda $a000,x
        eor #$ff
        cmp pattern,x
        bne fresh
        dex
        bpl -

        ; the pattern is there: an earlier run wrote it and it came back from
        ; the CRT file. Erase it again so the next load starts blank.
        jsr erase_sector
        jsr check_blank
        lda #2
        jmp finish

fresh   jsr erase_sector
        jsr check_blank

        lda #TEST_BANK
        jsr EAPISetBank
        ldx #0
wloop   stx index
        lda pattern,x
        ldy #$80                ; ROML: $8000 + x
        jsr EAPIWriteFlash
        bcc +
        inc R_WRITE_ERR
+       ldx index
        lda pattern,x
        eor #$ff
        ldy #$e0                ; ROMH, addressed as in Ultimax: $E000 + x
        jsr EAPIWriteFlash
        bcc +
        inc R_WRITE_ERR
+       ldx index
        inx
        cpx #PATTERN_LEN
        bne wloop

        jsr select_test_bank
        ldx #PATTERN_LEN - 1
-       lda $8000,x
        cmp pattern,x
        beq +
        inc R_VERIFY_ERR
+       lda $a000,x
        eor #$ff
        cmp pattern,x
        beq +
        inc R_VERIFY_ERR
+       dex
        bpl -
        lda #1

finish  sta R_STATUS
        lda #0
        sta EF_BANK
        lda #STATE_DONE
        sta R_STATE
-       jmp -

; Erase the test sector on both chips.
erase_sector
        lda #TEST_BANK
        jsr EAPISetBank
        lda #TEST_BANK
        ldy #$80
        jsr EAPIEraseSector
        lda #0
        rol a
        sta R_ERASE_LO
        lda #TEST_BANK
        ldy #$e0
        jsr EAPIEraseSector
        lda #0
        rol a
        sta R_ERASE_HI
        rts

; Count bytes that are not $FF in the first PATTERN_LEN bytes of every bank of
; the test sector, ROML and ROMH.
check_blank
        lda #EF_16K
        sta EF_MODE
        lda #TEST_BANK
        sta blank_bank
bnext   lda blank_bank
        sta EF_BANK
        ldx #PATTERN_LEN - 1
-       lda $8000,x
        cmp #$ff
        beq +
        inc R_BLANK_ERR
+       lda $a000,x
        cmp #$ff
        beq +
        inc R_BLANK_ERR
+       dex
        bpl -
        inc blank_bank
        lda blank_bank
        cmp #TEST_BANK + SECTOR_BANKS
        bne bnext
        rts

select_test_bank
        lda #EF_16K
        sta EF_MODE
        lda #TEST_BANK
        sta EF_BANK
        rts

; Neither the pattern nor its complement (written to ROMH) contains $FF, so a
; blank byte never passes for a written one.
pattern .byte $a5, $5a, $c3, $3c, $0f, $f0, $81, $7e
        .byte $12, $34, $56, $78, $9a, $bc, $de, $01

index       .byte 0
blank_bank  .byte 0
