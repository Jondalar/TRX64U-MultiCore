; lib.inc - the shared code of the SuperCPU board suite (T241.1). Our own code; 64tass.
; Every program includes it once, behind its BASIC line. The routines run in emulation mode, 8-bit registers.
; Screen output: 40 x 25 text, strings in screen code (64tass .enc "screen", lowercase in the source).
; Timing: CIA2 timer A and B chained count PHI2 cycles (32 bit); every number this suite prints is in PHI2 cycles.

scr     = $0400
zp_ptr  = $fb                   ; screen pointer
zp_t    = $fd                   ; string pointer
L       = $c000                 ; the library's data block ($c000-$c03f); the programs use $c040 and up
el      = L+$00                 ; 4: elapsed PHI2 cycles of the last c2start/c2stop pair, calibrated
el0     = L+$04                 ; 4: the cycles of an empty c2start/c2stop pair (calib)
nfail   = L+$08                 ; checks that failed
d24     = L+$09                 ; 3: dec24 work
dtmp    = L+$0c                 ; 2: dec24 work
dcnt    = L+$0e
dlead   = L+$0f
ex      = L+$10                 ; 4: the expected value of chkx/chkt/chkge
tol     = L+$14                 ; 2: the tolerance of chkt
issc    = L+$16                 ; 1 if a SuperCPU answers
crow    = L+$17                 ; the row of the last at
nchk    = L+$18                 ; checks made
hti     = L+$19                 ; 2: the title of head
mstyle  = L+$1b                 ; 0: a check prints " ok" / " *"; 1: one mark, " " / "*"
wvec    = L+$1c                 ; 2: callw jumps here

print   .macro                  ; print a 0-terminated screen-code string at the cursor
        lda #<\1
        sta zp_t
        lda #>\1
        sta zp_t+1
        jsr prs
        .endm

expect  .macro                  ; ex = a 32-bit constant
        lda #<\1
        sta ex
        lda #>\1
        sta ex+1
        lda #`\1
        sta ex+2
        lda #0
        sta ex+3
        .endm

; ---- text -------------------------------------------------------------------------------------------------------------

at      stx crow                ; cursor to row X, column Y
        lda rowlo,x
        sta zp_ptr
        lda rowhi,x
        sta zp_ptr+1
        tya
        clc
        adc zp_ptr
        sta zp_ptr
        bcc +
        inc zp_ptr+1
+       rts

nl      ldx crow                ; cursor to column 0 of the next row
        inx
        ldy #0
        jmp at

prs     ldy #0                  ; print the string at (zp_t)
        lda (zp_t),y
        beq +
        jsr put
        inc zp_t
        bne prs
        inc zp_t+1
        bne prs
+       rts

put     ldy #0                  ; one screen code; keeps X
        sta (zp_ptr),y
        inc zp_ptr
        bne +
        inc zp_ptr+1
+       rts

space   lda #$20
        jmp put

hex8    pha                     ; A as two hex digits
        lsr a
        lsr a
        lsr a
        lsr a
        jsr hex4
        pla
hex4    and #$0f                ; the low nibble of A as one hex digit
        cmp #10
        bcc +
        sbc #9                  ; 10..15 -> screen codes 1..6 (a..f)
        jmp put
+       ora #$30
        jmp put

prel    lda el+3                ; el as a decimal number, 8 columns ("ovf" above 24 bits)
        beq +
        #print t_ovf
        rts
+       lda el
        sta d24
        lda el+1
        sta d24+1
        lda el+2
        sta d24+2
prel7   lda el                  ; el in decimal, 7 columns
        sta d24
        lda el+1
        sta d24+1
        lda el+2
        sta d24+2
        ldx #6
        lda #0
        sta dlead
        jmp decx

dec24   ldx #7                  ; d24 (24 bit) in decimal, right-aligned in 8 columns
        lda #0
        sta dlead
decx    lda #0
        sta dcnt
_sub    sec
        lda d24
        sbc p10l,x
        sta dtmp
        lda d24+1
        sbc p10m,x
        sta dtmp+1
        lda d24+2
        sbc p10h,x
        bcc _dig
        sta d24+2
        lda dtmp+1
        sta d24+1
        lda dtmp
        sta d24
        inc dcnt
        jmp _sub
_dig    lda dcnt
        ora dlead
        bne _show
        cpx #0
        beq _show
        lda #$20
        jsr put
        jmp _next
_show   lda #1
        sta dlead
        lda dcnt
        ora #$30
        jsr put
_next   dex
        bpl decx
        rts

dec8    sta d24                 ; A in decimal, 3 columns
        lda #0
        sta d24+1
        sta d24+2
        sta dlead
        ldx #2
        jmp decx

p10l    .byte <1, <10, <100, <1000, <10000, <100000, <1000000, <10000000
p10m    .byte >1, >10, >100, >1000, >10000, >100000, >1000000, >10000000
p10h    .byte `1, `10, `100, `1000, `10000, `100000, `1000000, `10000000

rowlo   .byte <(scr+range(25)*40)
rowhi   .byte >(scr+range(25)*40)

; ---- checks: print " ok" or " *", count -------------------------------------------------------------------------------

chkx    jsr cmp32               ; el == ex
        bne bad
        jmp good
chkge   jsr cmp32               ; el >= ex (unsigned)
        bcc bad
        jmp good
bad     inc nfail
        inc nchk
        lda mstyle
        beq +
        lda #$2a                ; "*"
        jmp put
+       #print t_bad
        rts
good    inc nchk
        lda mstyle
        beq +
        lda #$20
        jmp put
+       #print t_ok
        rts

chkt    sec                     ; |el - ex| <= tol
        lda el
        sbc ex
        sta dtmp
        lda el+1
        sbc ex+1
        sta dtmp+1
        lda el+2
        sbc ex+2
        tax
        lda el+3
        sbc ex+3
        bcs _pos                ; el >= ex: dtmp is the difference
        eor #$ff                ; negate the 32-bit difference (high bytes only matter for "too big")
        sta d24+2
        txa
        eor #$ff
        tax
        lda dtmp
        eor #$ff
        clc
        adc #1
        sta dtmp
        lda dtmp+1
        eor #$ff
        adc #0
        sta dtmp+1
        txa
        adc #0
        tax
        lda d24+2
        adc #0
_pos    bne _bad                ; difference >= 2^24
        txa
        bne _bad                ; difference >= 2^16
        lda dtmp+1
        cmp tol+1
        bcc _good
        bne _bad
        lda tol
        cmp dtmp
        bcs _good
_bad    jmp bad
_good   jmp good
callw   jmp (wvec)              ; jsr callw = jsr (wvec)

cmp32   lda el+3                ; compare el with ex, high byte first: Z and C as CMP does
        cmp ex+3
        bne +
        lda el+2
        cmp ex+2
        bne +
        lda el+1
        cmp ex+1
        bne +
        lda el
        cmp ex
+       rts

summary jsr nl                  ; the last line: "n checks, m failed"
        lda nchk
        jsr dec8
        #print t_chk
        lda nfail
        jsr dec8
        #print t_failed
        rts

; ---- timing ------------------------------------------------------------------------------------------------------------

c2start lda #0                  ; CIA2 timer A and B, chained, from $ffffffff
        sta $dd0e
        sta $dd0f
        lda #$ff
        sta $dd04
        sta $dd05
        sta $dd06
        sta $dd07
        lda #%01010001          ; TB: counts TA underflows, force load, start
        sta $dd0f
        lda #%00010001          ; TA: force load, start, continuous
        sta $dd0e
        rts

c2stop  lda #0                  ; stop; el = the cycles counted - el0
        sta $dd0e
        sta $dd0f
        sec
        lda $dd04
        eor #$ff
        sbc el0
        sta el
        lda $dd05
        eor #$ff
        sbc el0+1
        sta el+1
        lda $dd06
        eor #$ff
        sbc el0+2
        sta el+2
        lda $dd07
        eor #$ff
        sbc el0+3
        sta el+3
        rts

c2arm   lda #0                  ; CIA2 TA stopped and loaded with $ffff, TB loaded and counting TA underflows. Then
        sta $dd0e               ; "sty $dd0e" with Y = $11 starts the count and with Y = 0 stops it: the same instruction
        sta $dd0f               ; at both ends, so no calibration is needed; c2get reads the count into el
        lda #$ff
        sta $dd04
        sta $dd05
        sta $dd06
        sta $dd07
        lda #%01010001
        sta $dd0f
        lda #%00010000
        sta $dd0e
        rts

c2get   pha                     ; el = the count of the stopped timers, minus A
        ldx #3
-       lda $dd04,x
        eor #$ff
        sta el,x
        dex
        bpl -
        pla
elsub   sta dtmp                ; el = el - A
        sec
        lda el
        sbc dtmp
        sta el
        lda el+1
        sbc #0
        sta el+1
        lda el+2
        sbc #0
        sta el+2
        lda el+3
        sbc #0
        sta el+3
        rts

eldiv16 ldx #4                  ; el = el / 16
-       lsr el+3
        ror el+2
        ror el+1
        ror el
        dex
        bne -
        rts

calib   lda #0                  ; el0 = an empty start/stop pair, at the speed now set
        sta el0
        sta el0+1
        sta el0+2
        sta el0+3
        jsr c2start
        jsr c2stop
        ldx #3
-       lda el,x
        sta el0,x
        dex
        bpl -
        rts

vlin    cmp $d012               ; wait for raster line A (0-255, bit 8 clear)
        bne vlin
        bit $d011
        bmi vlin
        rts

scr_off lda $d011               ; no badlines
        and #$ef
        sta $d011
        rts

scr_on  lda $d011
        ora #$10
        sta $d011
        rts

spr8    ldx #63                 ; 8 sprites at Y = $8c (lines 140-160), X = 24, 54, ..., 234; data $ff at $0340 (pointer 13)
        lda #$ff
-       sta $0340,x
        dex
        bpl -
        ldx #7
        lda #13
-       sta $07f8,x
        dex
        bpl -
        ldy #0
        lda #24
-       sta $d000,y
        pha
        lda #$8c
        sta $d001,y
        pla
        clc
        adc #30
        iny
        iny
        cpy #16
        bne -
        lda #0
        sta $d010
        lda #$ff
        sta $d015
        rts

; ---- the SuperCPU ------------------------------------------------------------------------------------------------------

detect  lda #0                  ; issc = 1 when the SuperCPU registers answer ($d0b3 reads $ff on a C64: an unused VIC register)
        sta issc
        lda $d0b3
        cmp #$ff
        beq +
        inc issc
+       rts

spd1    sta $d07a               ; software 1 MHz
        rts

spd20   sta $d07b               ; 20 MHz (the speed switch permitting)
        rts

mode    pha                     ; mirror mode A: $c7 (the reset mode), $c0 ($d077), $80 ($d076), $40 ($d075), $00 ($d074)
        sta $d07e               ; hardware registers on
        pla
        cmp #$c7
        bne +
        sta $d0b3
        jmp _off
+       cmp #$c0
        bne +
        sta $d077
        jmp _off
+       cmp #$80
        bne +
        sta $d076
        jmp _off
+       cmp #$40
        bne +
        sta $d075
        jmp _off
+       sta $d074
_off    sta $d07f               ; hardware registers off
        rts

head    sta hti                 ; A/Y = the title: clear the screen, then <title>  sc:<0|1> b3:.. b9:.. b6:..
        sty hti+1
        jsr $e544
        lda #0
        sta nfail
        sta nchk
        jsr detect
        ldx #0
        ldy #0
        jsr at
        lda hti
        sta zp_t
        lda hti+1
        sta zp_t+1
        jsr prs
        #print t_sc
        lda issc
        jsr hex4
        #print t_b3
        lda $d0b3
        jsr hex8
        #print t_b9
        lda $d0b9
        jsr hex8
        #print t_b6
        lda $d0b6
        jsr hex8
        jmp nl

done    jsr summary             ; restore what the program changed, then return to BASIC
        lda #$37
        sta $01
        lda #$1b
        sta $d011
        lda #0
        sta $d015
        lda #$81
        sta $dc0d
        ldx crow                ; READY one row below the cursor row set here (BASIC prints a return first): row <= 23, no scroll
        inx
        cpx #23
        bcc +
        ldx #22
+       ldy #0
        clc
        jsr $fff0
        cli
        rts

        .enc "screen"
t_ok    .null " ok"
t_bad   .null " *"
t_ovf   .null "     ovf"
t_chk   .null " checks,"
t_failed .null " failed"
t_sc    .null "  sc:"
t_b3    .null " b3:"
t_b9    .null " b9:"
t_b6    .null " b6:"
        .enc "none"
