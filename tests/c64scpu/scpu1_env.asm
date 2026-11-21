; scpu1_env.asm - T241.1 P1: PAL or NTSC - what the VIC is, what the KERNAL decided, and why (64tass).
; Rows:
;   vic    cycles per frame (16 frames) and lines per frame: PAL 19656 and 312, NTSC 17095 and 263
;   kernal $02a6 as found at the start, and CIA1 timer A's period (16 periods). The KERNAL loads the latch with $4025
;          (period 16422) when $02a6 = 1 (PAL) and $4295 (17046) when $02a6 = 0 (NTSC). Checks: the period matches $02a6,
;          and $02a6 says PAL on a PAL VIC ("*ntsc on pal" when not: the jiffy clock then runs 3.7 % slow)
;   tod    PHI2 cycles per TOD second, the mains flag set for the VIC's standard: PAL 985248, NTSC 1022730 (within 1 %)
;   cint   the KERNAL's PAL test (CINT, $ff81) replayed from 9 start lines at 1 MHz and at 20 MHz. P = it decided PAL,
;          N = NTSC. CINT clears $d019 and sets the raster compare to line 311 ($e5a0), clears the screen ($e518), waits
;          for a raster line with low byte 0 and reads the line-311 flag. It sees PAL only when the screen clear ended at
;          line 256 or later: from line 256 the wait passes line 311, before it the wait stops at line 256.
;          1m: on a PAL VIC every start line must give P ("*" under an N).
;          md: the outcome the measured length of $e518 at 20 MHz predicts: P from start line + length >= 258 lines,
;              N up to 254, ? at 255-257 (no check). On an NTSC VIC: N everywhere (there is no line 311).
;          20: "*" under an outcome that disagrees with md.
;   e518   the length of $e518 at 1 MHz and at 20 MHz
;   clear  the length of the screen clear $e544 at 20 MHz. It writes 1000 screen bytes (mirrored to the C64 RAM in every
;          mirror mode but $40 and $00) and 1000 colour-RAM bytes (I/O): 2000 writes on the C64 bus, one per PHI2 cycle at
;          most, so it can not take fewer than 2000 cycles.
; Without a SuperCPU the 20 MHz rows repeat the 1 MHz measurement (the same machine).
; Board use: reset the machine, LOAD and RUN this program first; $02a6 then shows what the reset decided.

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NLIN    = 9
r1      = $c040                 ; 9: $02a6 after the replay at 1 MHz
r20     = $c049                 ; 9: at 20 MHz
mdl     = $c052                 ; 9: the model: 0 N, 1 P, 2 ?
f_cyc   = $c05b                 ; 3: cycles per frame
f_lin   = $c05e                 ; 2: lines per frame
ta_per  = $c060                 ; 3: timer A period
tod_c   = $c063                 ; 3: cycles per TOD second
e1      = $c066                 ; 3: $e518 at 1 MHz
e20     = $c069                 ; 3: $e518 at 20 MHz
clr20   = $c06c                 ; 3: $e544 at 20 MHz
a6_0    = $c06f                 ; $02a6 at the start
mx_lo   = $c070
li      = $c071
cra0    = $c072
ispal   = $c073
tlast   = $c074
tw      = $c075                 ; 2
at20    = $c077
lin16   = $c078                 ; 2: $e518 at 20 MHz in whole lines
ri      = $c07a

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda $02a6
        sta a6_0
        lda issc
        beq +
        jsr spd1
+       jsr calib
        lda #$7f
        sta $dc0d
        lda $dc0d
        jsr m_lines
        jsr m_frame
        jsr m_ta
        jsr m_tod
        lda #0                  ; the replays and lengths clear the screen: the report comes afterwards
        sta at20
        jsr m_e518
        ldx #0
        jsr replay
        lda issc
        beq +
        jsr spd20
        jsr calib
+       inc at20
        jsr m_e518
        ldx #NLIN
        jsr replay
        jsr m_clear
        lda issc
        beq +
        jsr spd1
        jsr calib
+       jsr restore
        jsr report
        lda issc
        beq +
        jsr spd20               ; back to the speed the switch gives
+       jmp done

; ---- measurements -------------------------------------------------------------------------------------------------------

m_lines lda #0                  ; the highest raster line: 256 + the highest low byte seen with bit 8 set
        sta mx_lo
        jsr vlin
-       lda $d012               ; leave line 0
        beq -
_w      lda $d011
        bpl _b8
        lda $d012
        cmp mx_lo
        bcc _w
        sta mx_lo
        jmp _w
_b8     lda $d012
        bne _w
        bit $d011               ; low byte 0 read in line 256: not yet line 0
        bmi _w
        clc
        lda mx_lo
        adc #1
        sta f_lin
        lda #1
        adc #0
        sta f_lin+1
        ldx #0                  ; ispal = 312 lines
        cmp #>312
        bne +
        lda f_lin
        cmp #<312
        bne +
        inx
+       stx ispal
        rts

m_frame jsr c2arm               ; 16 frames from line 0 to line 0
        lda #0
        jsr vlin
        ldy #$11
        sty $dd0e
        ldx #16
_f      lda $d012
        beq _f
        lda #0
        jsr vlin
        dex
        bne _f
        ldy #0
        sty $dd0e
        lda #4                  ; the stop path has DEX and BNE (4 cycles) more than the start path
        jsr c2get
        jsr eldiv16
        ldx #2
-       lda el,x
        sta f_cyc,x
        dex
        bpl -
        rts

m_ta    jsr c2arm               ; 16 periods of CIA1 timer A, from underflow flag to underflow flag
        lda $dc0d
-       lda $dc0d
        and #1
        beq -
        ldy #$11
        sty $dd0e
        ldx #16
_t      lda $dc0d
        and #1
        beq _t
        dex
        bne _t
        ldy #0
        sty $dd0e
        lda #4
        jsr c2get
        jsr eldiv16
        ldx #2
-       lda el,x
        sta ta_per,x
        dex
        bpl -
        rts

m_tod   lda $dc0e               ; the mains flag of the VIC's standard (PAL 50 Hz, NTSC 60 Hz), the clock set (it runs
        sta cra0                ; from the tenths write), then 10 tenths
        and #$7f
        ldx ispal
        beq +
        ora #$80
+       sta $dc0e
        lda $dc0f               ; writes set the clock, not the alarm
        and #$7f
        sta $dc0f
        lda #$01
        sta $dc0b
        lda #0
        sta $dc0a
        sta $dc09
        sta $dc08
        jsr tenth
        bcs _to
        jsr c2start
        ldx #10
-       jsr tenth
        bcs _to
        dex
        bne -
        jsr c2stop
        ldx #2
-       lda el,x
        sta tod_c,x
        dex
        bpl -
        jmp _r
_to     jsr c2stop
        lda #0
        sta tod_c
        sta tod_c+1
        sta tod_c+2
_r      lda cra0
        sta $dc0e
        rts

tenth   lda $dc08               ; wait for the next tenth; C = 1 after about 4 s at 1 MHz (no TOD)
        sta tlast
        lda #0
        sta tw
        sta tw+1
_w      lda $dc08
        cmp tlast
        bne _ch
        inc tw
        bne _w
        inc tw+1
        lda tw+1
        cmp #200
        bcc _w
        sec
        rts
_ch     clc
        rts

m_e518  lda #$30                ; $e518 from line 48, into e1 (at20 = 0) or e20 (at20 = 1)
        jsr vlin
        jsr c2start
        jsr $e518
        jsr c2stop
        ldx #0
        lda at20
        beq +
        ldx #e20-e1
+       lda el
        sta e1,x
        lda el+1
        sta e1+1,x
        lda el+2
        sta e1+2,x
        rts

replay  stx li                  ; CINT from each start line; X = 0: into r1, NLIN: into r20
        lda #0
        sta ri
_l      ldy ri
        lda lines,y
        jsr vlin
        jsr $ff81
        lda #$7f                ; CINT enabled the timer A interrupt: off again
        sta $dc0d
        lda $dc0d
        ldx li
        lda $02a6
        sta r1,x
        inc li
        inc ri
        lda ri
        cmp #NLIN
        bne _l
        rts

m_clear lda #$30
        jsr vlin
        jsr c2start
        jsr $e544
        jsr c2stop
        ldx #2
-       lda el,x
        sta clr20,x
        dex
        bpl -
        rts

restore lda a6_0                ; $02a6 and timer A's latch as found
        sta $02a6
        beq +
        lda #$25
        ldx #$40
        bne ++
+       lda #$95
        ldx #$42
+       sta $dc04
        stx $dc05
        lda #$7f
        sta $dc0d
        lda $dc0d
        rts

; ---- the model ----------------------------------------------------------------------------------------------------------

model   lda e20                 ; lin16 = e20 / 63; per start line s = line + lin16: mdl = N below 255, ? at 255-257, P from 258
        sta d24
        lda e20+1
        sta d24+1
        lda e20+2
        sta d24+2
        lda #0
        sta lin16
        sta lin16+1
-       sec
        lda d24
        sbc #63
        tax
        lda d24+1
        sbc #0
        tay
        lda d24+2
        sbc #0
        bcc _go
        sta d24+2
        sty d24+1
        stx d24
        inc lin16
        bne -
        inc lin16+1
        bne -
_go     ldx #0
_l      lda #0                  ; N
        ldy ispal
        beq _set
        clc
        lda lines,x
        adc lin16
        sta d24
        lda #0
        adc lin16+1
        bne _hi                 ; s >= 256
        lda d24
        cmp #255
        bne _n
        lda #2                  ; s = 255: ?
        bne _set
_n      lda #0
        beq _set
_hi     cmp #1
        bne _p                  ; s >= 512
        lda d24
        cmp #2
        bcs _p
        lda #2                  ; s = 256, 257: ?
        bne _set
_p      lda #1
_set    sta mdl,x
        inx
        cpx #NLIN
        bne _l
        rts

; ---- the report ---------------------------------------------------------------------------------------------------------

report  lda #<title
        ldy #>title
        jsr head
        #print t_vic            ; vic
        ldx #2
-       lda f_cyc,x
        sta el,x
        dex
        bpl -
        lda #0
        sta el+3
        jsr prel7
        #print t_lines
        lda f_lin
        sta d24
        lda f_lin+1
        sta d24+1
        lda #0
        sta d24+2
        sta dlead
        ldx #2
        jsr decx
        lda ispal
        beq _ntsc
        #print t_pal
        #expect 19656
        jmp _vchk
_ntsc   #print t_ntsc
        #expect 17095
_vchk   lda #4
        sta tol
        lda #0
        sta tol+1
        ldx #2
-       lda f_cyc,x
        sta el,x
        dex
        bpl -
        jsr chkt
        jsr nl                  ; kernal
        #print t_a6
        lda a6_0
        jsr hex4
        #print t_ta
        ldx #2
-       lda ta_per,x
        sta el,x
        dex
        bpl -
        jsr prel7
        lda a6_0
        beq +
        #expect 16422
        jmp ++
+       #expect 17046
+       lda #4
        sta tol
        jsr chkt
        lda ispal
        beq +
        lda a6_0
        bne +
        #print t_ntscpal
        inc nfail
+       inc nchk
        jsr nl                  ; tod
        #print t_tod
        ldx #2
-       lda tod_c,x
        sta el,x
        dex
        bpl -
        jsr prel7
        lda ispal
        beq +
        #expect 985248
        jmp ++
+       #expect 1022730
+       lda #<9852
        sta tol
        lda #>9852
        sta tol+1
        jsr chkt
        ldx #5                  ; the replay
        ldy #0
        jsr at
        #print t_cint
        jsr nl
        #print t_lhead
        jsr model
        jsr nl                  ; 1m: P expected on a PAL VIC, N on an NTSC VIC
        #print t_1m
        ldx #0
-       lda r1,x
        ldy ispal
        jsr entry
        inx
        cpx #NLIN
        bne -
        jsr nl                  ; 20: against the model
        #print t_20m
        ldx #0
-       lda r20,x
        ldy mdl,x
        jsr entry
        inx
        cpx #NLIN
        bne -
        jsr nl                  ; md
        #print t_model
        ldx #0
-       jsr space
        jsr space
        lda mdl,x
        tay
        lda pnq,y
        jsr put
        jsr space
        inx
        cpx #NLIN
        bne -
        jsr nl                  ; lengths
        #print t_e518
        ldx #2
-       lda e1,x
        sta el,x
        dex
        bpl -
        jsr prel7
        #print t_20
        ldx #2
-       lda e20,x
        sta el,x
        dex
        bpl -
        jsr prel7
        jsr nl
        #print t_clear
        ldx #2
-       lda clr20,x
        sta el,x
        dex
        bpl -
        jsr prel7
        #expect 2000
        jmp chkge

entry   sta dtmp                ; "  P " / "  N*": A = the outcome (0 N, 1 P), Y = the expected (0, 1; 2 = no check); keeps X
        sty dtmp+1
        jsr space
        jsr space
        ldy dtmp
        lda pnq,y
        jsr put
        lda dtmp+1
        cmp #2
        beq _sp
        inc nchk
        cmp dtmp
        beq _sp
        inc nfail
        lda #$2a
        jmp put
_sp     jmp space

pnq     .byte $0e, $10, $3f     ; N P ?
lines   .byte 0, 100, 180, 200, 210, 220, 230, 240, 250

        .include "lib.inc"

        .enc "screen"
title   .null "scpu1 env"
t_vic   .null "vic frame"
t_lines .null " cyc lines "
t_pal   .null " pal"
t_ntsc  .null " ntsc"
t_a6    .null "kernal $02a6="
t_ta    .null " ta period"
t_ntscpal .null " *ntsc on pal"
t_tod   .null "tod cycles/s "
t_cint  .null "cint replay (p=pal n=ntsc), start line:"
t_lhead .null "     0 100 180 200 210 220 230 240 250"
t_1m    .null "1m"
t_20m   .null "20"
t_model .null "md"
t_e518  .null "$e518 1mhz"
t_20    .null "  20mhz"
t_clear .null "$e544 clear 20mhz"
