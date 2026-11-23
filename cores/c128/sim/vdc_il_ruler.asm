; T257 fault 2: a ruler for the VDC's interlaced bitmap (R8 = 3). Written for this project (TREX), 64tass syntax.
; Build: 64tass -a -o vdc_il_ruler.prg vdc_il_ruler.asm   (C128 mode, LOAD and RUN; 64 KiB of VDC RAM)
;
; The VDC RAM holds a 640 x 480 picture the way Busted Boulders keeps its title: picture line 2m at $4000 + 80m (block A, the even lines),
; picture line 2m+1 at $8B00 + 80m (block B, the odd lines), m = 0..239. Every picture line p shows:
;   - a frame: pixel 0 and 639, all of line 0 and line 479;
;   - ten diagonals, pixels x and x+1 with x = (p mod 64) + 64j: in a right weave they are straight, a field that is k lines late
;     draws its rows 2k pixels to the right of the others (a second diagonal), a field shown twice gives steps of two rows;
;   - ruler ticks: lines p mod 48 = 0, 1 are set (white) over the left and right 80 pixels;
;   - box A (x 16..111) in the even lines 96..222 only, box B (x 528..623) in the odd lines 97..223 only: woven, both are striped
;     (grey); a field shown twice shows its box solid and the other box not at all;
;   - its own number p in binary, bit 8 .. bit 0 in bytes 30 .. 38 (x 240..311, a set bit is 6 pixels wide): woven right, the bit 0 column
;     is striped and every column halves its frequency to the left; a field shown twice shows bit 0 solid;
;   - the mode letter (A..D, 32 x 32 pixels) at x 576..607, lines 16..47.
; SPACE: next mode, RUN/STOP: back to BASIC (the registers as they were). The modes:
;   A  R8 = 3, R12/R13 = $4000 static (the registers of Busted Boulders' title, white on black)
;   B  R8 = 3, R12 = $40 or $8B written in every vertical blank, the field parity measured from two vblank lengths as the game does ($3A47)
;   C  R8 = 1 (interlace sync only), 5 lines a row, 240 lines a field, R12/R13 = $4000
;   D  R8 = 0 (not interlaced), 5 lines a row, 240 lines, R12/R13 = $4000: the reference (block A, the even picture lines)

        .cpu "6502"
        .weak
STARTMODE = 0                           ; the mode after the start (0 = A); -D STARTMODE=n for a run that starts in another mode
        .endweak
vdcadr  = $d600
vdcdat  = $d601
zp      = $fb                           ; $fb/$fc pointer, saved and restored

        * = $1c01
        .word (+), 2026
        .null $9e, format("%d", start)
+       .word 0

start   sei
        lda $d030
        sta sav_d030
        lda #1
        sta $d030                       ; 2 MHz
        lda zp
        sta sav_zp
        lda zp+1
        sta sav_zp+1
        ldy #0                          ; save the registers the modes write
-       ldx regs_d,y
        jsr vdc_r
        sta savereg,y
        iny
        cpy #nregs
        bne -
        lda #$30                        ; R28 bit 4: 64 KiB RAM addressing before the first byte goes in (the KERNAL leaves 16 KiB)
        ldx #28
        jsr vdc_w
        jsr fill
        lda #STARTMODE
        sta mode
        lda #0
        sta keyprev
        jsr setmode

mainlp  lda mode
        cmp #1
        beq modeb
        jsr keys                        ; modes A, C, D: only the keys
        jmp mainlp

; mode B, as the game: two field periods from vblank start to vblank start, par = 1 if the first is shorter; then R12 in every
; vertical blank: (frame and 1) xor par = 1 -> $8B, else $40
modeb   jsr wvbe                        ; let the new timing settle
        jsr wvbs
        jsr wvbe
        jsr wvbs
        lda #0
        sta c1
        sta c1+1
        sta c1+2
        sta c2
        sta c2+1
        sta c2+2
-       inc c1                          ; first period: the blank ...
        bne +
        inc c1+1
        bne +
        inc c1+2
+       lda vdcadr
        and #$20
        bne -
-       inc c1                          ; ... and the picture after it
        bne +
        inc c1+1
        bne +
        inc c1+2
+       lda vdcadr
        and #$20
        beq -
-       inc c2                          ; second period
        bne +
        inc c2+1
        bne +
        inc c2+2
+       lda vdcadr
        and #$20
        bne -
-       inc c2
        bne +
        inc c2+1
        bne +
        inc c2+2
+       lda vdcadr
        and #$20
        beq -
        lda c1+2                        ; par = c1 < c2
        cmp c2+2
        bne +
        lda c1+1
        cmp c2+1
        bne +
        lda c1
        cmp c2
+       lda #0
        rol a
        eor #1
        sta par
        jsr wvbe
        lda #0
        sta frame
mbloop  jsr wvbs                        ; vertical blank starts
        lda frame
        and #1
        eor par
        beq +
        lda #$8b
        bne ++
+       lda #$40
+       ldx #12
        jsr vdc_w
        jsr keys
        lda mode
        cmp #1
        beq +
        jmp mainlp                      ; SPACE: another mode
+       jsr wvbe
        inc frame
        jmp mbloop

wvbs    lda vdcadr                      ; wait for the vertical blank to start (status bit 5 rises)
        and #$20
        beq wvbs
        rts
wvbe    lda vdcadr                      ; wait for it to end
        and #$20
        bne wvbe
        rts

; the keyboard, CIA 1 column 7: bit 4 SPACE, bit 7 RUN/STOP (0 = pressed); a key counts once, when it goes down
keys    lda #$7f
        sta $dc00
        lda $dc01
        eor #$ff
        and #$90
        tax
        eor keyprev
        stx keyprev
        and keyprev                     ; newly pressed
        beq kdone
        bmi kstop
        lda mode
        clc
        adc #1
        and #3
        sta mode
        jsr setmode
kdone   rts
kstop   pla                             ; back to BASIC: the registers as they were
        pla
        ldy #0
-       ldx regs_d,y
        lda savereg,y
        jsr vdc_w
        iny
        cpy #nregs
        bne -
        lda sav_zp
        sta zp
        lda sav_zp+1
        sta zp+1
        lda sav_d030
        sta $d030
        lda #$ff
        sta $dc00
        cli
        rts

; the registers of mode `mode`, then the mode letter into both blocks
setmode lda mode
        asl a
        tax
        lda mtab,x
        sta zp
        lda mtab+1,x
        sta zp+1
        ldy #0
-       lda (zp),y
        cmp #$ff
        beq +
        tax
        iny
        lda (zp),y
        jsr vdc_w
        iny
        bne -
+       ; the letter: glyph row g = (p - 16) / 4 in lines p = 16..47, 4 x 4 pixels a dot, bytes 72..75
        lda mode
        asl a
        asl a
        asl a
        sta gofs
        lda #16
        sta pline
-       lda pline
        sec
        sbc #16
        lsr a
        lsr a
        clc
        adc gofs
        tay
        lda glyphs,y
        jsr expand                      ; -> lbuf+72..75
        lda pline
        jsr lineaddr                    ; -> taddr
        lda taddr
        clc
        adc #72
        tay
        lda taddr+1
        adc #0
        ldx #18
        jsr vdc_w
        tya
        ldx #19
        jsr vdc_w
        ldx #31
        stx vdcadr
        ldy #72
lput    bit vdcadr
        bpl lput
        lda lbuf,y
        sta vdcdat
        iny
        cpy #76
        bne lput
        inc pline
        lda pline
        cmp #48
        bne -
        rts

; glyph byte A -> 4 bytes at lbuf+72, each dot 4 pixels wide
expand  ldx #0
-       sta gtmp
        lda #0
        asl gtmp
        bcc +
        ora #$f0
+       asl gtmp
        bcc +
        ora #$0f
+       sta lbuf+72,x
        lda gtmp
        inx
        cpx #4
        bne -
        rts

; the VRAM address of picture line A (0..255 only: the letter lines) -> taddr: block A or B, 80 bytes a line
lineaddr pha
        lsr a
        sta gtmp                        ; m
        lda #0
        sta taddr
        sta taddr+1
        ldx gtmp
        beq ++
-       lda taddr
        clc
        adc #80
        sta taddr
        bcc +
        inc taddr+1
+       dex
        bne -
+       pla
        and #1
        bne +
        lda taddr+1
        clc
        adc #$40
        sta taddr+1
        rts
+       lda taddr+1
        clc
        adc #$8b
        sta taddr+1
        rts

; the whole picture: lines p = 0..479, even ones to block A, odd ones to block B
fill    lda #0
        sta pline
        sta pline+1
        lda #<$4000
        sta adra
        lda #>$4000
        sta adra+1
        lda #<$8b00
        sta adrb
        lda #>$8b00
        sta adrb+1
fline   jsr build
        lda pline
        and #1
        bne +
        lda adra+1
        ldx #18
        jsr vdc_w
        lda adra
        ldx #19
        jsr vdc_w
        lda adra
        clc
        adc #80
        sta adra
        bcc ++
        inc adra+1
        bne ++
+       lda adrb+1
        ldx #18
        jsr vdc_w
        lda adrb
        ldx #19
        jsr vdc_w
        lda adrb
        clc
        adc #80
        sta adrb
        bcc +
        inc adrb+1
+       ldx #31
        stx vdcadr
        ldy #0
-       bit vdcadr
        bpl -
        lda lbuf,y
        sta vdcdat
        iny
        cpy #80
        bne -
        inc pline
        bne +
        inc pline+1
+       lda pline+1
        beq fline
        lda pline
        cmp #<480
        bne fline
        rts

; picture line pline (0..479) -> lbuf
build   ldy #79
        lda #0
-       sta lbuf,y
        dey
        bpl -
        lda pline                       ; line 0 and 479: all black
        ora pline+1
        bne +
        jmp bfull
+       lda pline+1
        beq +
        lda pline
        cmp #<479
        bne +
        jmp bfull
+       lda #$80                        ; frame
        sta lbuf
        lda #$01
        sta lbuf+79
        jsr mod48                       ; ticks
        cmp #2
        bcs +
        ldy #9
        lda #$ff
-       sta lbuf,y
        sta lbuf+70,y
        dey
        bpl -
+       lda pline+1                     ; boxes, lines 96..223
        bne bdiag
        lda pline
        cmp #96
        bcc bdiag
        cmp #224
        bcs bdiag
        and #1
        bne +
        ldy #11
        lda #$ff
-       sta lbuf+2,y
        dey
        bpl -
        bmi bdiag
+       ldy #11
        lda #$ff
-       sta lbuf+66,y
        dey
        bpl -
bdiag   lda pline                       ; x = (p mod 64) + 64j, j = 0..9: byte x/8 + 8j, the same bit each time
        and #63
        sta gtmp
        lsr a
        lsr a
        lsr a
        tay                             ; byte of x within the 64 pixels
        lda gtmp
        and #7
        tax
        lda bits,x                      ; pixel x
        sta gtmp2
        lda bits+1,x                    ; pixel x+1 (0 when x+1 is the next byte)
        sta gtmp3
        ldx #10
-       lda lbuf,y
        ora gtmp2
        ora gtmp3
        sta lbuf,y
        lda gtmp3                       ; x+1 in the next byte: bit 7 there
        bne +
        cpy #79
        beq +
        lda lbuf+1,y
        ora #$80
        sta lbuf+1,y
+       tya
        clc
        adc #8
        tay
        dex
        bne -
        lda pline                       ; the line number in binary, bytes 30 (bit 8) .. 38 (bit 0): $7E for a 1
        sta gtmp
        lda pline+1
        sta gtmp2
        ldy #38
-       lda #0
        lsr gtmp2
        ror gtmp
        bcc +
        lda #$7e
+       sta lbuf,y
        dey
        cpy #29
        bne -
        rts
bfull   ldy #79
        lda #$ff
-       sta lbuf,y
        dey
        bpl -
        rts

; pline mod 48 -> A (pline < 512)
mod48   lda pline
        sta gtmp
        lda pline+1
        sta gtmp2
-       lda gtmp2
        bne +
        lda gtmp
        cmp #48
        bcc ++
+       lda gtmp
        sec
        sbc #48
        sta gtmp
        lda gtmp2
        sbc #0
        sta gtmp2
        jmp -
+       lda gtmp
        rts

vdc_w   stx vdcadr                      ; register X := A
-       bit vdcadr
        bpl -
        sta vdcdat
        rts
vdc_r   stx vdcadr                      ; A := register X
-       bit vdcadr
        bpl -
        lda vdcdat
        rts

bits    .byte $80, $40, $20, $10, $08, $04, $02, $01, $00

; the registers each mode writes (register, value), $ff ends; the restore on exit writes back the values read at the start
regs_d  .byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 20, 21, 23, 24, 25, 26, 27, 28
nregs   = * - regs_d
mtab    .word m_il, m_il, m_c, m_d
m_il    .byte 0, $7e, 1, $50, 2, $66, 3, $49, 4, $33, 5, $07, 6, $30, 7, $32, 8, $03, 9, $09
        .byte 12, $40, 13, $00, 20, $10, 21, $00, 23, $09, 24, $00, 25, $80, 26, $f0, 27, $00, 28, $30, $ff
m_c     .byte 0, $7e, 1, $50, 2, $66, 3, $49, 4, $33, 5, $03, 6, $30, 7, $32, 8, $01, 9, $04
        .byte 12, $40, 13, $00, 20, $10, 21, $00, 23, $04, 24, $00, 25, $80, 26, $f0, 27, $00, 28, $30, $ff
m_d     .byte 0, $7e, 1, $50, 2, $66, 3, $49, 4, $33, 5, $03, 6, $30, 7, $32, 8, $00, 9, $04
        .byte 12, $40, 13, $00, 20, $10, 21, $00, 23, $04, 24, $00, 25, $80, 26, $f0, 27, $00, 28, $30, $ff
glyphs  .byte $18, $3c, $66, $66, $7e, $66, $66, $00   ; A
        .byte $7c, $66, $66, $7c, $66, $66, $7c, $00   ; B
        .byte $3c, $66, $60, $60, $60, $66, $3c, $00   ; C
        .byte $78, $6c, $66, $66, $66, $6c, $78, $00   ; D

mode    .byte 0
keyprev .byte 0
par     .byte 0
frame   .byte 0
c1      .fill 3
c2      .fill 3
pline   .fill 2
adra    .fill 2
adrb    .fill 2
taddr   .fill 2
gofs    .byte 0
gtmp    .byte 0
gtmp2   .byte 0
gtmp3   .byte 0
sav_zp  .fill 2
sav_d030 .byte 0
savereg .fill nregs
lbuf    .fill 80
