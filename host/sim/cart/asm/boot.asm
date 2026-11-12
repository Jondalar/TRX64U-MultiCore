; Start of a test cartridge: CBM80 at $8000 (BASE = $8000) or the reset
; vector of an Ultimax cartridge at $E000 (BASE = $E000). Copies the runner
; and its table from BASE + $100 to $0800 and starts it.
        * = BASE
        .word cold, cold
        .byte $c3, $c2, $cd, $38, $30
cold    sei
        ldx #$ff
        txs
        cld
        ldx #0
-       lda BASE + $100,x
        sta $0800,x
        lda BASE + $200,x
        sta $0900,x
        lda BASE + $300,x
        sta $0a00,x
        lda BASE + $400,x
        sta $0b00,x
        inx
        bne -
        jmp $0800
nmi     rti
