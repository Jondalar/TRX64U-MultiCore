; T74 measurement: what does a CPU read of an address nobody answers return? Runs on any Ultimate core.
; Parameters in $C0F0.. (written by openbus.py over REST), result table at $C200 (256 reads).
;   $C0F0 byte the VIC's idle fetch ($3FFF) will see      $C0F1 byte of $3F00-$3FFE (the 5 refresh reads per line)
;   $C0F2/$C0F3 target address lo/hi                       $C0F4 1: write $A5 to the target before every read
;   $C0F6 byte of $07F8-$07FF (the sprite pointer reads, 8 per line even with no sprite on)
;   $C0F5 trigger (python sets 1, the program clears it)   $C0FE $A5 once the program runs   $C0FF done flag
; DEN is off for the measurement, so the VIC does nothing but idle accesses, refresh and the sprite pointer reads: no
; bad lines, no sprite data.
*=$0801
        .word next, 10
        .byte $9e
        .text "2064"
        .byte 0
next    .word 0
*=$0810
        sei
        lda #0
        sta $C0F5
        sta $C0FF
        lda #$A5
        sta $C0FE
main    lda $C0F5
        beq main
        lda #0
        sta $C0FF
        sta $D015
        lda $C0F2
        sta sta_op+1
        sta lda_op+1
        sta lda_op2+1
        lda $C0F3
        sta sta_op+2
        sta lda_op+2
        sta lda_op2+2
        lda #$0B
        sta $D011               ; DEN off
        ldx #0
fill    lda $C0F1
        sta $3F00,x
        inx
        bne fill
        lda $C0F0
        sta $3FFF
        ldx #7
ptr     lda $C0F6
        sta $07F8,x
        dex
        bpl ptr
        ldy #3
frame   lda $D012
        cmp #$F0
        bne frame
f2      lda $D012
        cmp #$F0
        beq f2
        dey
        bne frame
        ldx #0
        lda $C0F4
        bne pre
rd
lda_op lda $DF00
        sta $C200,x
        inx
        bne rd
        beq fin
pre     lda #$A5
sta_op  sta $DF00
lda_op2 lda $DF00
        sta $C200,x
        inx
        bne pre
fin     lda #$1B
        sta $D011
        lda #0
        sta $C0F5
        lda #1
        sta $C0FF
        jmp main
