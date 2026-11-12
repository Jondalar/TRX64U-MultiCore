; T142 bench program: reset into native mode, the APU handshake with the bench's own boot stub (tools/snes_asm.py spcstub: port 0 := $AA, port 1 := $BB, wait for
; $CC, answer $11 on port 2, echo port 0 to port 1), a fill and a checksum of a block of the work RAM, DONE. Ours, no third party code.
        .title "SNES CORE BOOT"
        .vector reset = reset
        .vector nmi = isr
        .vector ereset = reset
        .org $008000
reset:  sei
        clc
        xce
        rep #$38
        ldx #$1FFF
        txs
        sep #$20
        lda #$8F                ; forced blank
        sta $2100
        stz $4200               ; no NMI, no IRQ
w_aa:   lda $2140
        cmp #$AA
        bne w_aa
        lda #$CC
        sta $2140
w_11:   lda $2142
        cmp #$11
        bne w_11
        lda #$5A
        sta $2140
echo:   lda $2141
        cmp #$5A
        bne echo
        rep #$30
        ldx #$0000
fill:   txa
        sta $7E2000,x
        inx
        inx
        cpx #$1000
        bne fill
        ldx #$0000
        lda #$0000
sum:    clc
        adc $7E2000,x
        inx
        inx
        cpx #$1000
        bne sum
        sta $7E0100
        sep #$20
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
isr:    rti
