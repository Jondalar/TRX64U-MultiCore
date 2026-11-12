; T142 S8 bench program (pad_manual: after that the serial read by hand, $4016 / $4017, 16 bits each, into $0204..$0207 as the high and low byte of port 1, then port 2)
; (original text) : switch the automatic joypad read on, wait three frames, store $4218..$421B (JOY1L, JOY1H, JOY2L, JOY2H) in WRAM $0200..$0203, DONE.
; The bench presses buttons on the C64 side (joystick lines and keys). Ours, no third party code.
        .title "SNES CORE PAD MANUAL"
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
        stz $4200               ; no automatic read now
        lda #$01
        sta $4016
        lda #$00
        sta $4016               ; latched
        stz $20
        stz $21
        stz $22
        stz $23
        ldx #$0010
bit:    lda $4016
        lsr a
        lda $20
        rol a
        sta $20
        lda $21
        rol a
        sta $21
        lda $4017
        lsr a
        lda $22
        rol a
        sta $22
        lda $23
        rol a
        sta $23
        dex
        bne bit
        lda $21
        sta $0204
        lda $20
        sta $0205
        lda $23
        sta $0206
        lda $22
        sta $0207
        lda #$D0
        sta $7EFFFF
forever: bra forever
