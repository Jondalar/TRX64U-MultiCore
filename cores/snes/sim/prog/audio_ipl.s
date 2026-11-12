; T142 bench program: uploads a small S-SMP program through the real boot ROM's port protocol (the 64 bytes the application writes), blocks of
; (destination, length, bytes), then starts it. The program writes one voice into the S-DSP; the bench counts the samples. Ours, no third party code.
        .title "SNES CORE AUDIO IPL"
        .vector reset = reset
        .vector nmi = isr
        .vector ereset = reset
        .org $008000
blocks: .db $00,$02,$04, $00,$03,$00,$03                      ; $0200: sample directory
        .db $00,$03,$09, $C3,$78,$78,$78,$78,$78,$78,$78,$78    ; $0300: one looping BRR block
        .db $00,$04,$38                                         ; $0400: the program and its table
        .db $CD,$00,$F5,$14,$04,$C4,$F2,$3D,$F5,$14,$04,$C4,$F3,$3D,$C8,$24,$D0,$F0,$2F,$FE
        .db $0C,$7F,$1C,$7F,$2C,$00,$3C,$00,$0D,$00,$2D,$00,$3D,$00,$4D,$00,$5D,$02,$00,$7F,$01,$7F,$02,$00,$03,$10,$04,$00,$05,$00,$07,$7F,$6C,$20,$4C,$01
reset:  sei
        clc
        xce
        rep #$38
        ldx #$1FFF
        txs
        sep #$30
        .a8
        .x8
        lda #$8F
        sta $2100
        stz $4200
        ldx #$00
        lda #$CC
        sta $13                 ; the value that starts the first block
        lda #$03
        sta $14                 ; blocks left
w_aa:   lda $2140
        cmp #$AA
        bne w_aa
blk:    lda blocks,x
        sta $2142
        inx
        lda blocks,x
        sta $2143
        inx
        lda blocks,x
        sta $10                 ; length
        inx
        lda #$01
        sta $2141
        lda $13
        sta $2140
w_st:   lda $2140
        cmp $13
        bne w_st
        ldy #$00
byte:   lda blocks,x
        sta $2141
        sty $12
        lda $12
        sta $2140
w_e:    lda $2140
        cmp $12
        bne w_e
        inx
        iny
        cpy $10
        bne byte
        lda $10
        clc
        adc #$02
        sta $13                 ; the next block starts with length + 2
        lda $14
        sec
        sbc #$01
        sta $14
        bne blk
        lda #$00
        sta $2142
        lda #$04
        sta $2143               ; start at $0400
        stz $2141
        lda $13
        sta $2140
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
isr:    rti
