        * = $c000
        sei
        lda #$00
        sta $df02
        lda #$c1
        sta $df03
        lda #0
        sta $df04
        sta $df05
        sta $df06
        sta $df07
        lda #1
        sta $df08
        lda #$90
        sta $df01          ; stash $C100 page to REU 0
        ldx #0
v       lda regs,x
        sta $df21,x
        inx
        cpx #23
        bne v
        lda #$03           ; enable + repeat
        sta $df20
        cli
        rts
regs    .byte $3f, $88, 0          ; VOL +1, PAN +2, +3
        .byte $01,$00,$00,$00      ; START +4
        .byte $00,$00,$01,$00      ; LENGTH +8
        .byte $00,$8e,$00,$00      ; RATE +C
        .byte $00,$00,$00,$00      ; A +10
        .byte $00,$00,$01,$00      ; B +14
