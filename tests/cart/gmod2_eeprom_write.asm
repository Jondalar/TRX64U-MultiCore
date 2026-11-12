; GMod2 93C86 EEPROM stimulus for gmod2_eeprom_test.py (64tass).
;
; Bit-bangs the Microwire EEPROM through $DE00 (bit 6 chip select, bit 5 clock,
; bit 4 data in; bit 7 reads data out) and leaves its result in screen RAM:
;
;   $0400   'R' while running, 'D' when done
;   $0401   status: 1 = words 5..8 were not the pattern: written and read back
;                   2 = the pattern was there (it survived the write-back to
;                       the .eep file): ERAL, then read back as $FFFF
;   $0402   words that did not read back as expected
;   $0403   ready polls that timed out
;   $0404-$0405  word 5 as read at the start (high, low)
;
; Runs from RAM at $0800; the cartridge ROM is off while chip select is high.

GMOD2       = $de00
CS          = $40
CLK         = $20
DI          = $10

FIRST_WORD  = 5
WORDS       = 4

RESULT      = $0400
R_STATE     = RESULT + 0
R_STATUS    = RESULT + 1
R_MISMATCH  = RESULT + 2
R_TIMEOUT   = RESULT + 3
R_FIRST_HI  = RESULT + 4
R_FIRST_LO  = RESULT + 5

word_hi = $fb
word_lo = $fc
addr    = $fd
index   = $fe
count   = $02                   ; $02/$03

        * = $0800

start   sei
        cld
        ldx #$ff
        txs
        lda #0
        ldx #$0f
-       sta RESULT,x
        dex
        bpl -
        lda #$52                ; 'R'
        sta R_STATE

        lda #FIRST_WORD
        jsr read_word
        lda word_hi
        sta R_FIRST_HI
        lda word_lo
        sta R_FIRST_LO

        ; is the pattern already there?
        ldx #0
-       stx index
        txa
        clc
        adc #FIRST_WORD
        jsr read_word
        ldx index
        lda word_hi
        cmp pat_hi,x
        bne fresh
        lda word_lo
        cmp pat_lo,x
        bne fresh
        inx
        cpx #WORDS
        bne -

        ; persisted: erase everything, expect $FFFF
        jsr ewen
        lda #%10010000          ; 1 00 10xxxxxxxx: ERAL
        ldx #0
        jsr instr12
        jsr finish_write
        ldx #0
-       stx index
        txa
        clc
        adc #FIRST_WORD
        jsr read_word
        lda word_hi
        and word_lo
        cmp #$ff
        beq +
        inc R_MISMATCH
+       ldx index
        inx
        cpx #WORDS
        bne -
        lda #2
        jmp done

fresh   jsr ewen
        ldx #0
-       stx index
        txa
        clc
        adc #FIRST_WORD
        sta addr
        ; 1 01 aaaaaaaaaa: WRITE, then 16 data bits
        lda #%10100000
        ldx addr
        jsr instr12
        ldx index
        lda pat_hi,x
        jsr byte_out
        ldx index
        lda pat_lo,x
        jsr byte_out
        jsr finish_write
        ldx index
        inx
        cpx #WORDS
        bne -

        ; read back as one sequential READ over all words
        lda #FIRST_WORD
        jsr start_read
        ldx #0
-       stx index
        jsr byte_in
        sta word_hi
        jsr byte_in
        ldx index
        cmp pat_lo,x
        bne bad
        lda word_hi
        cmp pat_hi,x
        beq +
bad     inc R_MISMATCH
+       ldx index
        inx
        cpx #WORDS
        bne -
        jsr deselect
        lda #1

done    sta R_STATUS
        jsr deselect
        lda #$44                ; 'D'
        sta R_STATE
-       jmp -

; ---------------------------------------------------------------------------

deselect
        lda #0
        sta GMOD2
        rts

; one clock with data bit C: clock low with the bit, then clock high
bit_out lda #CS
        bcc +
        ora #DI
+       sta GMOD2
        ora #CLK
        sta GMOD2
        rts

; clock once and return data out in C (sampled after the rising edge)
bit_in  lda #CS
        sta GMOD2
        lda #CS | CLK
        sta GMOD2
        lda GMOD2
        asl a
        rts

byte_out
        ldx #8
-       asl a
        pha
        jsr bit_out
        pla
        dex
        bne -
        rts

byte_in lda #0
        ldx #8
-       pha
        jsr bit_in
        pla
        rol a
        dex
        bne -
        rts

; start bit, then the 12 bits A:X (A holds the 2 opcode bits and the top
; address bits in bits 7..4 as "op1 op0 a9 a8", X the low 8 address bits)
instr12 pha
        jsr deselect
        lda #CS                 ; select with clock low
        sta GMOD2
        sec
        jsr bit_out             ; start bit
        pla
        asl a                   ; skip the leading 1 of the literal (start bit)
        ldy #4                  ; op1 op0 a9 a8
-       asl a
        pha
        jsr bit_out
        pla
        dey
        bne -
        txa
        jmp byte_out_y

byte_out_y
        ldy #8
-       asl a
        pha
        jsr bit_out
        pla
        dey
        bne -
        rts

ewen    lda #%10011000          ; 1 00 11xxxxxxxx: EWEN
        ldx #0
        jsr instr12
        jmp deselect

; deselect to start programming, reselect, wait for data out = 1 (ready)
finish_write
        jsr deselect
        lda #CS
        sta GMOD2
        lda #0
        sta count
        sta count + 1
-       lda GMOD2
        bmi +
        inc count
        bne -
        inc count + 1
        bne -
        inc R_TIMEOUT
+       jmp deselect

; READ at word A; data follows
start_read
        tax
        lda #%11000000          ; 1 10 00: READ, address bits 9..8 = 0
        jmp instr12

read_word
        jsr start_read
        jsr byte_in
        sta word_hi
        jsr byte_in
        sta word_lo
        jmp deselect

pat_hi  .byte $12, $a5, $00, $7f
pat_lo  .byte $34, $5a, $ff, $80

        .cerror * > $0b00, "the stimulus must fit the three pages the boot code copies"
