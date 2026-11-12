; T142 bench program: sums of the bytes the HiROM cartridge shows at $C0..$FF:0000/8123/FFFF and, for the banks $00..$3F, at :8123 (the mirror of $C0..$FF:8123),
; into the work RAM $0100 (16 bit each) and DONE. The host patches this program over a real 4 MiB image (tools/hirom_check.py gives the sums it must see). Ours, no third party code.
        .title "SNES CORE HIROM"
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
        sep #$30
        .a8
        .x8
        stz $4200
        stz $10
        stz $11
        ldx #$C0
b1:     phx
        plb
        lda $0000
        clc
        adc $10
        sta $10
        lda $11
        adc #$00
        sta $11
        lda $8123
        clc
        adc $10
        sta $10
        lda $11
        adc #$00
        sta $11
        lda $FFFF
        clc
        adc $10
        sta $10
        lda $11
        adc #$00
        sta $11
        inx
        bne b1
        lda $10
        sta $7E0100
        lda $11
        sta $7E0101
        stz $10
        stz $11
        ldx #$00
b2:     phx
        plb
        lda $8123
        clc
        adc $10
        sta $10
        lda $11
        adc #$00
        sta $11
        inx
        cpx #$40
        bne b2
        lda $10
        sta $7E0102
        lda $11
        sta $7E0103
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
isr:    rti
