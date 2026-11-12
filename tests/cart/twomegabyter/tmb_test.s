; TwoMegabyter test cartridge (CRT 89; T33 §4.2 B2), built by build.sh from
; Protovision's TwoMegabyter code 1.0.0 (Chester Kollschen / Knights of Bytes),
; which is not in this repository: build.sh unpacks it and includes its boot
; block, flash API and highscore demo here.
;
; Protovision's boot block starts it (its `game:` loop is replaced by a jump to
; tmb_main). The test runs from RAM at $0800 (the flash API needs $0000-$0FFF,
; Ultimax) and leaves its results in screen RAM:
;   $0400  banks 0-123 whose ROML/ROMH markers did not read back (0)
;   $0401  mode errors: $DE02 = 2 (8K, BASIC at $A000), 3 (off, RAM at $8000),
;          1 (Ultimax, ROMH at $E000), 0 (16K) (0)
;   $0402  the counter the highscore page holds after this run (+1 per run;
;          the sector at $1F0000 = banks 124-127 is the save area)
;   $0403  the page read back after the save differs (0)
;   $07E7  $AA when done
; and the same as text from row 2.

        .setcpu "6502"
        .import __RAMCODE_LOAD__, __RAMCODE_RUN__, __RAMCODE_SIZE__

        .segment "BOOT"
        .include "boot_patched.s"

tmb_main:
        lda #<__RAMCODE_LOAD__
        sta $fb
        lda #>__RAMCODE_LOAD__
        sta $fc
        lda #<__RAMCODE_RUN__
        sta $fd
        lda #>__RAMCODE_RUN__
        sta $fe
        ldx #>(__RAMCODE_SIZE__ + 255)
        ldy #0
@copy:  lda ($fb),y
        sta ($fd),y
        iny
        bne @copy
        inc $fc
        inc $fe
        dex
        bne @copy
        jmp test_start

        .segment "RAMCODE"
        .include "twomegabyter.ca65.s"
        .include "twomegabyter-highscore-demo.ca65.s"

R_BANKS = $0400
R_MODES = $0401
R_COUNT = $0402
R_VERIFY = $0403
R_DONE  = $07e7
MARK_L  = $4c                   ; "L" at offset $1FF1 of each bank, the bank at $1FF0
MARK_H  = $48                   ; "H" at offset $3FF1, the bank at $3FF0
LAST_MARKED = 124               ; banks 124-127 are the save sector

test_start:
        sei
        lda #$03                ; VIC bank 0, screen $0400, upper case, display on
        sta $dd00
        lda #$15
        sta $d018
        lda #$1b
        sta $d011
        lda #$00
        sta $d020
        lda #$06
        sta $d021
        ldx #0
@cls:   lda #$20
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $06e8,x
        lda #$01
        sta $d800,x
        sta $d900,x
        sta $da00,x
        sta $dae8,x
        inx
        bne @cls
        lda #0
        sta R_BANKS
        sta R_MODES
        sta R_COUNT
        sta R_VERIFY
        sta R_DONE

        ; every bank's markers, through $DE00, in 16K mode
        ldx #0
@bank:  stx $de00
        cpx $9ff0
        bne @bad
        lda $9ff1
        cmp #MARK_L
        bne @bad
        cpx $bff0
        bne @bad
        lda $bff1
        cmp #MARK_H
        beq @next
@bad:   inc R_BANKS
@next:  inx
        cpx #LAST_MARKED
        bne @bank
        lda #0
        sta $de00

        ; the modes of $DE02: bit 0 EXROM, bit 1 GAME
        lda #$5a
        sta $8000               ; 16K: the write goes to the RAM below
        lda #$02                ; 8K: BASIC at $A000
        sta $de02
        lda $a001
        cmp #$e3
        beq :+
        inc R_MODES
:       lda #$03                ; off: the RAM at $8000
        sta $de02
        lda $8000
        cmp #$5a
        beq :+
        inc R_MODES
:       lda #$01                ; Ultimax: ROMH at $E000
        sta $de02
        lda $fff1
        cmp #MARK_H
        beq :+
        inc R_MODES
:       lda #$00                ; 16K
        sta $de02
        lda $9ff1
        cmp #MARK_L
        beq :+
        inc R_MODES
:
        ; one more save through Protovision's highscore demo
        jsr highscore_load
        lda #0
        bcs :+                  ; no page saved yet
        lda highscore_data + 1
:       clc
        adc #1
        sta R_COUNT
        sta highscore_data + 1
        ldx #2
:       txa
        sta highscore_data,x
        inx
        bne :-
        jsr highscore_save
        lda #0
        sta highscore_data + 1
        jsr highscore_load
        bcs @verr
        lda highscore_data + 1
        cmp R_COUNT
        bne @verr
        ldx #2
:       txa
        cmp highscore_data,x
        bne @verr
        inx
        bne :-
        beq @shown
@verr:  inc R_VERIFY
@shown:
        ldx #0
:       lda text,x
        beq :+
        sta $0400 + 80,x
        inx
        bne :-
:       lda R_BANKS
        ldx #80 + 40 + 14
        jsr hex
        lda R_MODES
        ldx #80 + 80 + 14
        jsr hex
        lda R_COUNT
        ldx #80 + 120 + 14
        jsr hex
        lda R_VERIFY
        ldx #80 + 160 + 14
        jsr hex
        lda R_BANKS
        ora R_MODES
        ora R_VERIFY
        bne :+
        lda #$05                ; green border: all well
        .byte $2c
:       lda #$02                ; red
        sta $d020
        lda #$aa
        sta R_DONE
@idle:  jmp @idle

; A as two hex digits at $0400 + X
hex:    pha
        lsr
        lsr
        lsr
        lsr
        jsr @digit
        pla
        and #$0f
@digit: cmp #10
        bcc :+
        sbc #9                  ; screen code of A = 1
        bcs @put
:       ora #$30
@put:   sta $0400,x
        inx
        rts

        .macpack cbm
text:   scrcode "twomegabyter t33 test                   "
        scrcode "bank errors  $                          "
        scrcode "mode errors  $                          "
        scrcode "saves        $                          "
        scrcode "save errors  $"
        .byte 0
