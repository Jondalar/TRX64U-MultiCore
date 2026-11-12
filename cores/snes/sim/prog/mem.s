; T142 S2/S3 bench program: every kind of access the machine makes - code from the ROM across bank switches, tables in other banks, the work RAM through
; absolute, long, indexed, direct page, stack and block move, DMA from the ROM into the work RAM, from the work RAM into the video RAM, from the ROM into the colour
; RAM - and a picture. What it leaves in WRAM, VRAM and the audio RAM must be the same behind the caches as in front of a memory that is never late. Ours, no third
; party code.
        .title "SNES CORE MEM TEST"
        .vector reset = reset
        .vector nmi = isr
        .vector ereset = reset
CHK     = $0100                 ; 16 bit checksums: CHK+0 ROM tables, +2 WRAM fill, +4 stack, +6 block move, +8 DMA to WRAM, +10 video RAM readback
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
        stz $420B
        stz $420C
w_aa:   lda $2140
        cmp #$AA
        bne w_aa
        lda #$CC
        sta $2140
w_11:   lda $2142
        cmp #$11
        bne w_11
        ; ---- 1. tables in other banks of the ROM, summed 16 bit with long indexed loads (banks 1..7), twice (the second pass runs from the cache)
        .pushmode
        php
        sep #$20
        pha
        lda #$01
        sta $7EF001
        pla
        plp
        .popmode
        rep #$30
        stz CHK+0
        ldy #$0002
pass:   ldx #$0000
t1:     lda $018000,x
        clc
        adc $028000,x
        adc $038000,x
        adc $048000,x
        adc $058000,x
        adc $068000,x
        adc $078000,x
        clc
        adc CHK+0
        sta CHK+0
        inx
        inx
        cpx #$0400
        bne t1
        dey
        bne pass
        ; ---- 2. the work RAM: a pattern in bank 7E, summed
        .pushmode
        php
        sep #$20
        pha
        lda #$02
        sta $7EF002
        pla
        plp
        .popmode
        ldx #$0000
f1:     txa
        asl
        clc
        adc #$0013
        eor #$A55A
        sta $7E2000,x
        inx
        inx
        cpx #$2000
        bne f1
        ldx #$0000
        lda #$0000
s1:     clc
        adc $7E2000,x
        inx
        inx
        cpx #$2000
        bne s1
        sta CHK+2
        ; ---- 3. the stack: push 256 words, pop and add them, with a subroutine in between
        .pushmode
        php
        sep #$20
        pha
        lda #$03
        sta $7EF003
        pla
        plp
        .popmode
        lda #$0000
        sta CHK+4
        ldx #$0100
p1:     phx
        jsr bump
        dex
        bne p1
        ldx #$0100
p2:     pla
        clc
        adc CHK+4
        sta CHK+4
        dex
        bne p2
        ; ---- 4. block moves: 7E:2000 -> 7F:0000 (4 KiB), then 7F:0000 -> 7E:6000, summed
        .pushmode
        php
        sep #$20
        pha
        lda #$04
        sta $7EF004
        pla
        plp
        .popmode
        ldx #$2000
        ldy #$0000
        lda #$0FFF
        mvn $7E,$7F
        ldx #$0000
        ldy #$6000
        lda #$0FFF
        mvn $7F,$7E
        phk                     ; MVN leaves the data bank at its destination: back to bank 0 for the registers
        plb
        ldx #$0000
        lda #$0000
s2:     clc
        adc $7E6000,x
        inx
        inx
        cpx #$1000
        bne s2
        sta CHK+6
        ; ---- 5. DMA: ROM bank 2 ($8000.., 2 KiB of the table) into the work RAM through $2180 (WMADD 7F:3000), summed
        .pushmode
        php
        sep #$20
        pha
        lda #$05
        sta $7EF005
        pla
        plp
        .popmode
        sep #$20
        stz $2181
        lda #$30
        sta $2182
        lda #$01
        sta $2183
        lda #$00
        sta $4300               ; A -> B, one register
        lda #$80
        sta $4301               ; $2180
        rep #$20
        lda #$8000
        sta $4302
        sep #$20
        lda #$02
        sta $4304
        rep #$20
        lda #$0800
        sta $4305
        sep #$20
        lda #$01
        sta $420B
        rep #$30
        ldx #$0000
        lda #$0000
s3:     clc
        adc $7F3000,x
        inx
        inx
        cpx #$0800
        bne s3
        sta CHK+8
        ; ---- 6. DMA: work RAM 7E:6000 (4 KiB) into the video RAM (words at 0), the ROM (bank 3) into the colour RAM
        .pushmode
        php
        sep #$20
        pha
        lda #$06
        sta $7EF006
        pla
        plp
        .popmode
        sep #$20
        lda #$80
        sta $2115               ; increment after the high byte
        rep #$20
        lda #$0000
        sta $2116
        sep #$20
        lda #$01
        sta $4310               ; A -> B, two registers
        lda #$18
        sta $4311               ; $2118
        rep #$20
        lda #$6000
        sta $4312
        sep #$20
        lda #$7E
        sta $4314
        rep #$20
        lda #$1000
        sta $4315
        sep #$20
        lda #$02
        sta $420B
        stz $2121
        lda #$00
        sta $4320
        lda #$22
        sta $4321               ; $2122
        rep #$20
        lda #$8000
        sta $4322
        sep #$20
        lda #$03
        sta $4324
        rep #$20
        lda #$0200
        sta $4325
        sep #$20
        lda #$04
        sta $420B
        ; ---- 7. read the video RAM back (the PPU's read latch) and sum it
        .pushmode
        php
        sep #$20
        pha
        lda #$07
        sta $7EF007
        pla
        plp
        .popmode
        lda #$80
        sta $2115
        rep #$20
        lda #$0000
        sta $2116
        lda $2139               ; the first read after setting the address is stale
        rep #$30
        ldx #$0000
        lda #$0000
        sta CHK+10
s4:     lda $2139               ; low byte (16 bit access: low then high, increment after the high read)
        clc
        adc CHK+10
        sta CHK+10
        inx
        cpx #$0400
        bne s4
        ; ---- 8. a picture: mode 1, BG1 tiles from the video RAM, screen on, two frames
        .pushmode
        php
        sep #$20
        pha
        lda #$08
        sta $7EF008
        pla
        plp
        .popmode
        sep #$20
        lda #$01
        sta $2105               ; BG mode 1
        lda #$00
        sta $2107               ; BG1 map at VRAM word 0
        sta $210B               ; BG1 characters at word 0
        lda #$01
        sta $212C               ; BG1 on the main screen
        lda #$0F
        sta $2100               ; screen on, full brightness
        ldx #$0002
vb:     lda $4212
        and #$80
        beq vb                  ; wait for vblank
vb2:    lda $4212
        and #$80
        bne vb2                 ; ... and for its end
        dex
        bne vb
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
bump:   rep #$30
        lda CHK+4
        inc
        sta CHK+4
        rts
isr:    rti
        ; the tables: bank 1..7, 2 KiB each at $8000 ($02:8000.. is the DMA source of step 5)
        .org $018000
        .prng 11, 2048
        .org $028000
        .prng 22, 2048
        .org $038000
        .prng 33, 4096
        .org $048000
        .prng 44, 2048
        .org $058000
        .prng 55, 2048
        .org $068000
        .prng 66, 2048
        .org $078000
        .prng 77, 2048
