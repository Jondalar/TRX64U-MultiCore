; T142 bench program: a self-checking block move (MVN 7E -> 7F, 16 bytes) and the data bank after it; DONE ($D0) only when the copy is right, $EE when not.
        .title "SNES CORE MVN TEST"
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
        stz $4200
        rep #$30
        ldx #$0000
fill:   txa
        eor #$3C5A
        sta $7E2000,x
        inx
        inx
        cpx #$0020
        bne fill
        lda #$0001
        sta $7EF000             ; marker: filled
        ldx #$2000
        ldy #$0000
        lda #$001F
        mvn $7E,$7F
        phk
        plb
        lda #$0002
        sta $7EF000             ; marker: moved
        ldx #$0000
chk:    lda $7F0000,x
        cmp $7E2000,x
        bne bad
        inx
        inx
        cpx #$0020
        bne chk
        sep #$20
        lda #$D0
        sta $7EFFFF
forever: bra forever
bad:    sep #$20
        lda #$EE
        sta $7EFFFF
        bra forever
