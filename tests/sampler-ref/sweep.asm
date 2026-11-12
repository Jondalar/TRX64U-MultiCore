; Same voice as pan.asm, then a mailbox loop: $C0F0 -> pan ($DF22), $C0F1 -> vol ($DF21), $C0F2 -> ctrl ($DF20).
; Load sine.bin at $C100 first (trxdbg poke), run with SYS 49152.
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
        sta $df01
        ldx #0
v       lda regs,x
        sta $df21,x
        inx
        cpx #23
        bne v
        lda #$03
        sta $df20
        sta $c0f2
        cli
loop    lda $c0f0
        sta $df22
        lda $c0f1
        sta $df21
        lda $c0f2
        sta $df20
        jmp loop
regs    .byte $3f, $88, 0
        .byte $01,$00,$00,$00
        .byte $00,$00,$01,$00
        .byte $00,$8e,$00,$00
        .byte $00,$00,$00,$00
        .byte $00,$00,$01,$00
