; Generic AMD flash stimulus for flash_amd_test.py (64tass).
;
; Talks to the flash chip(s) with raw command sequences, no driver, so the
; same program covers every cartridge the core emulates: EasyFlash, GMod2,
; C64MegaCart and MegaByter. The harness assembles it once per cartridge with
; -D defines (register addresses, flash mode, unlock cycles, windows, the banks
; to test) and builds a CRT whose banks carry a marker in their first bytes, so
; an erase that reaches too far shows up as a missing marker.
;
; Runs from RAM at $0800 in Ultimax mode, where only $0000-$0FFF is RAM. It runs
; once per cartridge load, with no input, and leaves its result in screen RAM:
;
;   $0400   'R' while running, 'D' when done
;   $0401   status: 1 = test sector blank or stale, erased and written
;                   2 = pattern from an earlier run found (it survived the
;                       write-back to the CRT file) and erased again
;   $0402-$0405  CHIPS = 2 only: manufacturer and device ID of chip 0, then of
;           chip 1, after an autoselect whose unlock cycles alternate between
;           the chips (chip 1 first, as EAPIInit does it)
;   $0410 + 16 * chip, per chip:
;     +0  manufacturer ID          +1  device ID
;     +2  status read while the sector erase was busy (DQ7 should be 0)
;     +3  sector erase timed out (1)
;     +4  bytes not $FF in the erased sector (first 16 of each bank)
;     +5  neighbour banks whose marker was destroyed by the sector erase
;     +6  program timeouts           +7  pattern bytes that did not read back
;     +8  result of programming $F0 then $3C into one byte (expect $30)
;     +9  boot sector: bytes not $FF after its erase (only if BOOT_BANK < $FF)
;     +10 boot sector: neighbour banks whose marker was destroyed

        .weak
BANK_REG    = $de00             ; bank register
BANK_OR     = $00               ; bits kept set in every bank write (GMod2: $C0)
MODE_REG    = $de02             ; mode register ($DE00 when shared with the bank)
MODE_FLASH  = $05               ; value that selects Ultimax / flash mode
MODE_READ   = MODE_FLASH        ; mode for reading the flash (GMod2 only reads in 8K mode)
READ_OR     = BANK_OR           ; bank register bits while reading
CHIPS       = 2                 ; 2 = ROML chip at $8000 and ROMH chip at $E000
WINDOW0     = $80               ; high byte of chip 0's write window in Ultimax
WINDOW1     = $e0               ; high byte of chip 1's write window (CHIPS = 2)
RWINDOW0    = WINDOW0           ; high byte of chip 0's read window
RWINDOW1    = WINDOW1
U1_BANK     = $ff               ; bank for the first unlock cycle, $FF = any
U1_OFS      = $0555             ; its offset inside the 8 KiB window
U2_BANK     = $ff
U2_OFS      = $02aa
ID_OFS      = 1                 ; autoselect address of the device ID
TEST_BANK   = 8                 ; first bank of a uniform test sector
SECTOR_BANKS = 8                ; banks in that sector
BOOT_BANK   = $ff               ; a bank that is a small boot sector, $FF = none
BOOT_BANKS  = 1                 ; banks in that boot sector
        .endweak

PATTERN_LEN = 16
MARKER_LEN  = 4                 ; the harness writes "BNK", bank number at +0..+3

RESULT      = $0400
R_STATE     = RESULT + $00
R_STATUS    = RESULT + $01
R_BOTH_ID   = RESULT + $02
R_CHIP      = RESULT + $10
C_MFR       = 0
C_DEV       = 1
C_BUSY      = 2
C_ETIMEOUT  = 3
C_BLANK     = 4
C_NEIGHBOUR = 5
C_PTIMEOUT  = 6
C_VERIFY    = 7
C_AND       = 8
C_BOOTBLANK = 9
C_BOOTNEIGH = 10

STATE_RUNNING = $52             ; 'R'
STATE_DONE    = $44             ; 'D'

ptr     = $fb                   ; pointer into the chip window
cur_bank = $fd
chip    = $fe
tmp     = $02
count   = $03                   ; 16-bit poll counter, $03/$04
bank_i  = $05
res     = $06                   ; pointer to this chip's result block, $06/$07
cur_or = $08                   ; READ_OR or BANK_OR, whichever mode is on

        * = $0800

start   sei
        cld
        ldx #$ff
        txs
        lda #$2f
        sta $00
        lda #$37
        sta $01

        lda #0
        ldx #$3f
-       sta RESULT,x
        dex
        bpl -
        lda #STATE_RUNNING
        sta R_STATE

        lda #0
        sta cur_bank
        sta chip
        jsr use_read

        .if CHIPS == 2
        ; the chips are independent: interleaved sequences must not disturb
        ; each other
        jsr use_flash
        lda #$aa
        ldy #<U1_OFS
        ldx #>U1_OFS
        jsr store_both
        lda #$55
        ldy #<U2_OFS
        ldx #>U2_OFS
        jsr store_both
        lda #$90
        ldy #<U1_OFS
        ldx #>U1_OFS
        jsr store_both
        ldx #0
-       stx chip
        jsr use_read
        txa
        asl a
        tax
        ldy #0
        lda (ptr),y
        sta R_BOTH_ID,x
        ldy #ID_OFS
        lda (ptr),y
        sta R_BOTH_ID + 1,x
        jsr reset_chip
        ldx chip
        inx
        cpx #2
        bne -
        .endif

        ; was the pattern already there on chip 0? (written by an earlier run)
        lda #0
        sta chip
        jsr chip_setup
        lda #TEST_BANK
        jsr set_bank
        ldy #PATTERN_LEN - 1
-       lda (ptr),y
        cmp pattern,y
        bne fresh
        dey
        bpl -
        lda #2
        sta R_STATUS
        bne run                 ; always taken
fresh   lda #1
        sta R_STATUS

run     lda #0
        sta chip
chiploop
        jsr chip_setup
        jsr test_chip
        inc chip
        lda chip
        cmp #CHIPS
        bne chiploop

        lda #STATE_DONE
        sta R_STATE
-       jmp -

; ---------------------------------------------------------------------------

; ptr = start of this chip's window, res = its result block
chip_setup
        jsr use_read
        ldx chip
        txa
        asl a
        asl a
        asl a
        asl a
        clc
        adc #<R_CHIP
        sta res
        lda #>R_CHIP
        sta res + 1
        rts

test_chip
        ; autoselect
        lda #$90
        jsr command
        jsr use_read
        lda #0
        jsr set_bank
        ldy #0
        lda (ptr),y
        ldy #C_MFR
        sta (res),y
        ldy #ID_OFS
        lda (ptr),y
        ldy #C_DEV
        sta (res),y
        jsr reset_chip

        ; sector erase of the uniform test sector
        lda #TEST_BANK
        jsr erase_sector
        ldy #C_BUSY
        lda tmp
        sta (res),y
        bcc +
        ldy #C_ETIMEOUT
        lda #1
        sta (res),y
+       lda #TEST_BANK
        ldx #SECTOR_BANKS
        ldy #C_BLANK
        jsr count_not_blank
        lda #TEST_BANK - 1
        ldy #C_NEIGHBOUR
        jsr check_marker
        lda #TEST_BANK + SECTOR_BANKS
        ldy #C_NEIGHBOUR
        jsr check_marker

        lda R_STATUS
        cmp #2
        beq boot                ; persisted run: leave the sector blank

        ; program the pattern
        lda #TEST_BANK
        sta cur_bank
        ldx #0
-       stx bank_i
        lda pattern,x
        ldy bank_i
        jsr program
        bcc +
        ldy #C_PTIMEOUT
        jsr inc_result
+       ldx bank_i
        inx
        cpx #PATTERN_LEN
        bne -

        jsr use_read
        lda #TEST_BANK
        jsr set_bank
        ldy #PATTERN_LEN - 1
-       lda (ptr),y
        cmp pattern,y
        beq +
        sty bank_i
        ldy #C_VERIFY
        jsr inc_result
        ldy bank_i
+       dey
        bpl -

        ; programming only clears bits: $F0 then $3C leaves $30
        lda #$f0
        ldy #PATTERN_LEN
        jsr program
        lda #$3c
        ldy #PATTERN_LEN
        jsr program
        jsr reset_chip          ; a real chip reports the 0->1 attempt (DQ5) until reset
        jsr use_read
        lda #TEST_BANK
        jsr set_bank
        ldy #PATTERN_LEN
        lda (ptr),y
        ldy #C_AND
        sta (res),y

boot
        .if BOOT_BANK != $ff
        lda chip
        bne done                ; boot sectors exist on single-chip carts only
        lda #BOOT_BANK
        jsr erase_sector
        lda #BOOT_BANK
        ldx #BOOT_BANKS
        ldy #C_BOOTBLANK
        jsr count_not_blank
        lda #BOOT_BANK - 1
        ldy #C_BOOTNEIGH
        jsr check_marker
        .if BOOT_BANK + BOOT_BANKS < 256
        lda #BOOT_BANK + BOOT_BANKS
        ldy #C_BOOTNEIGH
        jsr check_marker
        .endif
        .endif
done    rts

; ---------------------------------------------------------------------------

set_bank
        sta cur_bank
        ora cur_or
        sta BANK_REG
        rts

; switch the cartridge to reading / to flash mode, keeping the bank, and point
; ptr at the matching window of this chip
use_read
        lda #READ_OR
        sta cur_or
        .if MODE_REG != BANK_REG
        lda #MODE_READ
        sta MODE_REG
        .endif
        ldx chip
        lda rwindows,x
        jmp use_common

use_flash
        lda #BANK_OR
        sta cur_or
        .if MODE_REG != BANK_REG
        lda #MODE_FLASH
        sta MODE_REG
        .endif
        ldx chip
        lda windows,x
use_common
        sta ptr + 1
        lda #0
        sta ptr
        lda cur_bank
        jmp set_bank

; write A to the unlock-1 address, after both unlock cycles
command pha
        jsr use_flash
        jsr unlock
        lda #U1_BANK
        jsr cycle_bank
        pla
        ldy #<U1_OFS
        ldx #>U1_OFS
        jmp store

unlock  lda #U1_BANK
        jsr cycle_bank
        lda #$aa
        ldy #<U1_OFS
        ldx #>U1_OFS
        jsr store
        lda #U2_BANK
        jsr cycle_bank
        lda #$55
        ldy #<U2_OFS
        ldx #>U2_OFS
        jmp store

; switch to the unlock cycle's bank, unless it is "any" ($FF); the callers set
; the target bank again afterwards
cycle_bank
        cmp #$ff
        beq +
        jmp set_bank
+       rts

; store A at window + X:Y (offset high:low inside the 8 KiB window)
store   pha
        tya
        sta ptr
        txa
        ldx chip
        clc
        adc windows,x
        sta ptr + 1
        pla
        ldy #0
        sta (ptr),y
        lda #0
        sta ptr
        lda windows,x
        sta ptr + 1
        rts

; store A at X:Y in chip 1's window, then in chip 0's
store_both
        sta tmp
        sty bank_i
        stx count
        lda #1
        sta chip
        lda tmp
        jsr store
        lda #0
        sta chip
        lda tmp
        ldy bank_i
        ldx count
        jmp store

reset_chip
        jsr use_flash
        lda #$f0
        ldy #0
        sta (ptr),y
        rts

; program A at offset Y of bank cur_bank; C set on timeout. Polls DQ7 the way
; a driver does, so programming over non-blank bits still ends.
program sta tmp
        sty tmp2
        lda cur_bank
        pha
        lda #$a0
        jsr command
        pla
        jsr set_bank
        lda tmp
        ldy tmp2
        sta (ptr),y
        jsr use_read
        ldy tmp2
        lda #0
        sta count
        sta count + 1
-       lda (ptr),y
        eor tmp
        and #$80
        beq +
        inc count
        bne -
        inc count + 1
        bne -
        sec
        rts
+       clc
        rts

tmp2    .byte 0

; erase the sector containing bank A; tmp = first status read; C set on timeout
erase_sector
        pha
        lda #$80
        jsr command
        jsr unlock
        pla
        jsr set_bank
        lda #$30
        ldy #0
        sta (ptr),y
        jsr use_read
        ldy #0
        lda (ptr),y
        sta tmp
        lda #0
        sta count
        sta count + 1
        ldx #4                  ; up to 4 x 65536 polls
-       lda (ptr),y
        cmp #$ff
        beq +
        inc count
        bne -
        inc count + 1
        bne -
        dex
        bne -
        sec
        rts
+       clc
        rts

; count bytes not $FF in the first PATTERN_LEN bytes of X banks from bank A,
; into result byte Y
count_not_blank
        sta bank_i
        stx tmp
        sty tmp2
        jsr use_read
-       lda bank_i
        jsr set_bank
        ldy #PATTERN_LEN - 1
blank1  lda (ptr),y
        cmp #$ff
        beq +
        sty count
        ldy tmp2
        jsr inc_result
        ldy count
+       dey
        bpl blank1
        inc bank_i
        dec tmp
        bne -
        rts

; bank A must still carry its marker "BNK",A; if not, increment result byte Y
check_marker
        sta bank_i
        sty tmp2
        jsr use_read
        lda bank_i
        jsr set_bank
        ldy #0
        lda (ptr),y
        cmp #$42                ; 'B'
        bne bad
        iny
        lda (ptr),y
        cmp #$4e                ; 'N'
        bne bad
        iny
        lda (ptr),y
        cmp #$4b                ; 'K'
        bne bad
        iny
        lda (ptr),y
        cmp bank_i
        bne bad
        rts
bad     ldy tmp2
        ; fall through

inc_result
        lda (res),y
        clc
        adc #1
        sta (res),y
        rts

windows .byte WINDOW0, WINDOW1
rwindows .byte RWINDOW0, RWINDOW1

; no $FF in it, so a blank byte never passes for a written one
pattern .byte $a5, $5a, $c3, $3c, $0f, $f0, $81, $7e
        .byte $12, $34, $56, $78, $9a, $bc, $de, $01

        .cerror * > $1000, "the stimulus must fit in RAM below $1000 (Ultimax)"
