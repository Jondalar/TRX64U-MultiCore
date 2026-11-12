; T142 S8 bench program (and the line after the 16th bit, $0204 / $0205, spec 5 DKC): switch the automatic joypad read on, wait three frames, store $4218..$421B (JOY1L, JOY1H, JOY2L, JOY2H) in WRAM $0200..$0203, DONE.
; The bench presses buttons on the C64 side (joystick lines and keys). Ours, no third party code.
        .title "SNES CORE PAD TEST"
        .vector reset = reset
        .vector ereset = reset
        .org $008000
reset:  sei
        clc
        xce
        rep #$38
        ldx #$1FFF
        txs
        sep #$20
        lda #$8F
        sta $2100
        lda #$01
        sta $4200               ; automatic joypad read
        ldx #$0004
vb:     lda $4212
        and #$80
        beq vb
vb2:    lda $4212
        and #$80
        bne vb2
        dex
        bne vb
        lda $4218
        sta $0200
        lda $4219
        sta $0201
        lda $421A
        sta $0202
        lda $421B
        sta $0203
        lda $4016               ; Donkey Kong Country's "is a pad plugged in": by hand, after the automatic read has shifted all 16 bits out, bit 0 has to be 1
        and #$01
        sta $0204
        lda $4017
        and #$01
        sta $0205
        lda #$D0
        sta $7EFFFF
forever: bra forever
