; T142 S5 bench program: a picture of known pixels. BG1 in mode 0 (2 bits a pixel): tile 0 is colour 1, tile 1 is colour 2; the map has tile 0 in its left half and
; tile 1 in its right half; colour 1 is pure red and colour 2 pure blue (15 bit BGR). The bench reads the frame buffer at known places. Ours, no third party code.
        .title "SNES CORE VIDEO TEST"
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
        sta $2100               ; forced blank
        stz $4200
        lda #$80
        sta $2115               ; VRAM address increments after the high byte
        ; colours: CGRAM 1 = $001F (red), 2 = $7C00 (blue)
        lda #$01
        sta $2121
        lda #$1F
        sta $2122
        lda #$00
        sta $2122
        lda #$00
        sta $2122
        lda #$7C
        sta $2122
        ; tile 0 at word $1000: plane 0 all ones, plane 1 zeros (colour 1); tile 1 at $1008: plane 0 zeros, plane 1 ones (colour 2)
        rep #$20
        lda #$1000
        sta $2116
        sep #$20
        ldx #$0008
t0:     lda #$FF
        sta $2118
        lda #$00
        sta $2119
        dex
        bne t0
        ldx #$0008
t1:     lda #$00
        sta $2118
        lda #$FF
        sta $2119
        dex
        bne t1
        ; the map at word 0: 32 rows of 32 entries, entries 16..31 of a row are tile 1
        rep #$20
        ldy #$0000              ; row
row:    tya
        asl
        asl
        asl
        asl
        asl
        clc
        adc #$0010
        sta $2116               ; word address of entry 16 of the row
        sep #$20
        ldx #$0010
m1:     lda #$01
        sta $2118
        lda #$00
        sta $2119
        dex
        bne m1
        rep #$20
        iny
        cpy #$0020
        bne row
        sep #$20
        lda #$00
        sta $2105               ; BG mode 0
        sta $2107               ; BG1 map at word 0
        lda #$01
        sta $210B               ; BG1 characters at word $1000
        lda #$01
        sta $212C               ; BG1 on the main screen
        lda #$0F
        sta $2100               ; screen on
        ldx #$0004
vb:     lda $4212
        and #$80
        beq vb
vb2:    lda $4212
        and #$80
        bne vb2
        dex
        bne vb
        lda #$D0
        sta $7EFFFF
forever: bra forever
