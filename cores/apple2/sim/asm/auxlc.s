; Aux RAM, ALTZP, 80STORE + PAGE2 and the language card, with results in main RAM at LOG (the bench reads them back through the window):
;   LOG+0 main $6000 after writing aux $6000   (11)      LOG+1 aux $6000 through RAMRD (22)         LOG+2 main $6000 again (11)
;   LOG+3 zero page $10 main after ALTZP write (33)      LOG+4 zero page $10 through ALTZP (44)      LOG+5 the subroutine on the aux stack returned (55)
;   LOG+6 $0400 PAGE1 main (77)   LOG+7 $0400 PAGE2 with 80STORE (66)
;   LOG+8 LC bank 2 $D000 (88)  LOG+9 LC bank 1 $D000 (AA)  LOG+10 LC $E000 shared (99)  LOG+11 LC bank 2 again (88)  LOG+12 $E000 with ROM read (00: our ROM has nothing there)
;   LOG+13 aux $6000 read, written and read again through RAMRD (23: a cached line must follow the write)
;   LOG+15 $A5 when everything is done.
; The language card part runs from RAM at $0900: it replaces the ROM it would run from.
        .include "apple.inc"
        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        lda COL80_OFF
        lda TXTSET
        ; --- aux RAM through RAMWRT / RAMRD
        lda #$11
        sta $6000
        sta WRAUX
        lda #$22
        sta $6000
        sta WRMAIN
        lda $6000
        sta LOG+0
        sta RDAUX
        lda $6000
        sta RDMAIN
        sta LOG+1
        lda $6000
        sta LOG+2
        sta RDAUX
        lda $6000           ; the line is in the cache now
        lda #$23
        sta WRAUX
        sta $6000           ; and changes under it
        sta WRMAIN
        lda $6000
        sta RDMAIN
        sta LOG+13
        ; --- ALTZP: zero page and stack in aux
        lda #$33
        sta $10
        sta ALTZP
        lda #$44
        sta $10
        jsr sub
        sta STDZP
        lda $10
        sta LOG+3
        sta ALTZP
        lda $10
        sta STDZP
        sta LOG+4
        lda #$55
        sta LOG+5
        ; --- 80STORE + PAGE2: the text page in aux
        lda #$77
        sta $0400
        sta STORE80_ON
        sta PAGE2
        lda #$66
        sta $0400
        sta PAGE1
        lda $0400
        sta LOG+6
        sta PAGE2
        lda $0400
        sta PAGE1
        sta LOG+7
        sta STORE80_OFF
        ; --- the language card, from RAM
        ldx #0
cp:     lda lc,x
        sta $0900,x
        inx
        cpx #lc_end-lc
        bne cp
        jmp $0900
sub:    lda #$55
        rts
back:   lda #$A5
        sta LOG+15
halt:   bra halt
lc:     lda LCBANK2_RDWR        ; RAM read, write enabled (two reads in a row), bank 2
        lda LCBANK2_RDWR
        lda #$88
        sta $D000
        lda #$99
        sta $E000
        lda LCBANK1_RDWR        ; bank 1
        lda LCBANK1_RDWR
        lda #$AA
        sta $D000
        lda $D000
        sta LOG+9
        lda LCBANK2_RDWR        ; and back to bank 2
        lda LCBANK2_RDWR
        lda $D000
        sta LOG+8
        sta LOG+11
        lda $E000
        sta LOG+10
        lda LCROM_WR2           ; ROM read: $E000 is ROM again
        lda $E000
        sta LOG+12
        jmp back
lc_end:
        * = $FFFA
        .word reset, reset, reset
