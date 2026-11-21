; scpu5_chain.asm - T241.1 P5: the raster interrupt chain of Master's Boom, at 1 MHz (64tass).
; Master's Boom in play (T241 §5): raster interrupts at lines 87, 145 and 217. Its dispatcher reads $d012 62 cycles after
; the interrupt sequence starts and takes the mid handler only for line 146: on a stock C64 the earliest read lands at
; line 146 cycle 0, no margin. A CPU that is one cycle early there reads 145 and the chain stops (stutter, then freeze).
; Part 1, the edge: a raster interrupt at line 145, the handler reads $d012 K cycles after the interrupt sequence starts
; (K = 19 + the length of a delay sled). K rises from a start value; 8 interrupts per K, each in another phase of the
; main loop. Shown per condition: the first K at which a read sees line 146 (information: which phases a K meets is
; not fixed), and the first K at which all 8 do (checked: the earliest read, the margin Boom's dispatcher has).
;   a  screen off, the main loop a NOP sled (2 cycles per instruction)
;   b  screen off, the main loop Boom's spin: LDA abs, BEQ back (4 + 3 cycles)
;   c  the interrupt at line 144, line 145 a badline (screen on, y-scroll 1): the read comes after the badline's stall
;   d  8 sprites over lines 141-161, screen on (y-scroll 3), the spin
; Part 2, the soak: the three-interrupt chain of Boom for 500 frames in condition d, the dispatcher's read at the
; stock C64's K of d (zero margin), about 6000 cycles of work in the line-217 handler, the spin as the main loop.
; Counted: reads that see line 145 (early), and interrupts out of order. A stock C64 shows 0 and 0.
; Expected values: a stock PAL C64's (ref/stock_pal.txt); "*" marks a value off the C64's.

        .cpu "6502"
        * = $0801
        .word (+), 2026
        .byte $9e
        .text format("%d", start)
        .byte 0
+       .word 0

NSL     = 110                   ; the delay sled: K = 19 + m, m = 0..NSL
NSAMP   = 8
NFR     = 500
SPINF   = $c300                 ; the spin's flag: never set
NOPS    = $2000                 ; the NOP sled $2000-$2fff
ci      = $c040
curk    = $c041
samp    = $c042
hits    = $c043
kany    = $c044
kall    = $c045
savesp  = $c046
dvec    = $c047                 ; 2: into the delay sled
got     = $c049
avec    = $c04a                 ; 2: the handler after the read
pvec    = $c04c                 ; 2: into the phase sled
st      = $c04e                 ; the chain's state: 0 top next, 1 mid next, 2 bottom next
early   = $c04f                 ; 2
order   = $c051                 ; 2
frames  = $c053                 ; 2
ksoak   = $c055

start   sei
        lda #<title
        ldy #>title
        jsr head
        lda issc
        beq +
        jsr spd1
+       lda #$7f
        sta $dc0d
        lda $dc0d
        lda #0
        sta SPINF
        sta $d01a
        lda #$0f
        sta $d019
        jsr nopsled
        ldx #1
        ldy #0
        jsr at
        #print t_hd
        lda #0
        sta ci
nextc   jsr cond
        jsr sweep
        jsr scr_on
        lda #$1b
        sta $d011
        lda #0
        sta $d015
        lda ci
        clc
        adc #2
        tax
        ldy #0
        jsr at
        ldx ci
        lda cname,x
        jsr put
        jsr space
        jsr space
        lda kany                ; information only: which of the 8 phases a K meets is not fixed
        jsr dec8
        jsr space
        jsr space
        lda kall
        jsr dec8
        lda ci
        asl a
        tax
        lda kall
        cmp exk+1,x
        jsr mark
        inc ci
        lda ci
        cmp #4
        bne nextc
        lda #3                  ; part 2 in condition d
        sta ci
        jsr cond
        jsr soak
        lda #$1b
        sta $d011
        lda #0
        sta $d015
        ldx #7
        ldy #0
        jsr at
        #print t_soak
        lda ksoak
        jsr dec8
        jsr nl
        #print t_frames
        lda frames
        sta d24
        lda frames+1
        sta d24+1
        jsr dec16
        #print t_early
        lda early
        sta d24
        lda early+1
        sta d24+1
        jsr dec16
        lda early
        ora early+1
        jsr mark
        #print t_order
        lda order
        sta d24
        lda order+1
        sta d24+1
        jsr dec16
        lda order
        ora order+1
        jsr mark
        lda #$81                ; the KERNAL's timer A interrupt on again
        sta $dc0d
        ldx #9
        ldy #0
        jsr at
        jmp done

dec16   lda #0                  ; d24 (16 bit) in 5 columns
        sta d24+2
        sta dlead
        ldx #4
        jmp decx

mark    php                     ; Z from the compare: " " when equal, "*" when not; counts
        inc nchk
        plp
        beq +
        inc nfail
        lda #$2a
        jmp put
+       jmp space

cond    ldx ci                  ; the condition's screen and sprites
        lda c_d011,x
        sta $d011
        lda c_spr,x
        beq +
        jmp spr8
+       lda #0
        sta $d015
        rts

nopsled lda #<NOPS              ; $ea from $2000 to $2ffc, then JMP $2000
        sta zp_t
        lda #>NOPS
        sta zp_t+1
        ldy #0
-       lda #$ea
        sta (zp_t),y
        iny
        bne -
        inc zp_t+1
        lda zp_t+1
        cmp #$30
        bne -
        lda #$4c
        sta $2ffd
        lda #<NOPS
        sta $2ffe
        lda #>NOPS
        sta $2fff
        rts

; ---- part 1 ---------------------------------------------------------------------------------------------------------------

sweep   ldx ci
        lda kstart,x
        sta curk
        jsr setk
        lda #0
        sta samp
        sta hits
        sta kany
        sta kall
        lda #<sweepc
        sta avec
        lda #>sweepc
        sta avec+1
        jsr setp
        tsx
        stx savesp
        lda #$35
        sta $01
        lda #<irq
        sta $fffe
        lda #>irq
        sta $ffff
        ldx ci
        lda c_line,x
        sta $d012
        lda #1
        sta $d01a
        lda #$0f
        sta $d019
        lda ci
        bne spin
        cli
        jmp NOPS
        .align 8                ; the spin's branch never crosses a page
spin    cli
-       lda SPINF               ; Boom's spin
        beq -
        jmp -

setk    lda curk                ; dvec = dsled + NSL - (curk - 19)
        sec
        sbc #19
        sta dtmp
        sec
        lda #<(dsled+NSL)
        sbc dtmp
        sta dvec
        lda #>(dsled+NSL)
        sbc #0
        sta dvec+1
        rts

setp    lda samp                ; pvec = psled + 7 - (samp and 7): 3 to 10 cycles after the work, so the main loop's phase moves
        and #7
        sta dtmp
        sec
        lda #<(psled+7)
        sbc dtmp
        sta pvec
        lda #>(psled+7)
        sbc #0
        sta pvec+1
        rts

irq     jmp (dvec)              ; 7 (sequence) + 5 + m + 3 (sled) + 4 (the read in LDA's last cycle) = K
dsled   .fill NSL, $c9
        .byte $c5, $ea
        lda $d012
        sta got
        jmp (avec)

sweepc  lda #$0f
        sta $d019
        lda got
        cmp #146
        bcc +
        inc hits
+       inc samp
        lda samp
        cmp #NSAMP
        bne _ph
        lda hits                ; NSAMP reads at this K
        beq _adv
        lda kany
        bne +
        lda curk
        sta kany
+       lda hits
        cmp #NSAMP
        bne _adv
        lda curk
        sta kall
        jmp fin
_adv    lda #0
        sta samp
        sta hits
        inc curk
        lda curk
        cmp #19+NSL+1
        bcs fin                 ; kall stays 0: the edge was not found
        jsr setk
_ph     jsr setp
        jmp (pvec)
psled   .fill 7, $c9
        .byte $c5, $ea
        rti

fin     ldx savesp              ; back to the caller of sweep or soak, I still set
        txs
        lda #0
        sta $d01a
        lda #$0f
        sta $d019
        lda #$37
        sta $01
        rts

; ---- part 2 ---------------------------------------------------------------------------------------------------------------

soak    lda #0
        sta st
        sta early
        sta early+1
        sta order
        sta order+1
        sta frames
        sta frames+1
        sta samp
        lda exk+7               ; the stock C64's K of condition d, where all reads see 146 (0 = none recorded: this run's)
        bne +
        lda kall
+       sta ksoak
        sta curk
        jsr setk
        jsr setp
        lda #<soakc
        sta avec
        lda #>soakc
        sta avec+1
        tsx
        stx savesp
        lda #$35
        sta $01
        lda #<irq
        sta $fffe
        lda #>irq
        sta $ffff
        lda #87
        sta $d012
        lda #1
        sta $d01a
        lda #$0f
        sta $d019
        jmp spin

soakc   lda #$0f                ; the dispatcher: by the line read
        sta $d019
        lda got
        cmp #96
        bcc _top
        cmp #145
        beq _early
        cmp #146
        bcc _bad
        cmp #152
        bcc _mid
        cmp #217
        bcc _bad
        cmp #226
        bcc _bot
_bad    inc order
        bne +
        inc order+1
+       jmp fin
_early  inc early               ; Boom would take its multiplexer path here and lose the frame's chain
        bne _mid
        inc early+1
_mid    lda st
        cmp #1
        beq +
        jsr bump
+       lda #217
        sta $d012
        lda #2
        sta st
        rti
_top    lda st
        beq +
        jsr bump
+       lda #145
        sta $d012
        lda #1
        sta st
        rti
_bot    lda st
        cmp #2
        beq +
        jsr bump
+       jsr work
        lda #87
        sta $d012
        lda #0
        sta st
        inc frames
        bne +
        inc frames+1
+       lda frames+1
        cmp #>NFR
        bne +
        lda frames
        cmp #<NFR
        bne +
        jmp fin
+       inc samp
        jsr setp
        jmp (pvec)

bump    inc order
        bne +
        inc order+1
+       rts

work    ldx #0                  ; about 5900 cycles, as the game logic in Boom's line-217 handler
-       lda $c800,x
        sta $cc00,x
        lda $c900,x
        sta $cd00,x
        inx
        bne -
        rts

c_d011  .byte $0b, $0b, $19, $1b
c_spr   .byte 0, 0, 0, 1
cname   .byte $01, $02, $03, $04        ; a b c d
kstart  .byte 55, 52, 73, 33
c_line  .byte 145, 145, 144, 145
; the stock PAL C64's first K with one read of 146 and with all reads of 146, per condition
exk     .byte 61, 62                ; a
        .byte 58, 62                ; b
        .byte 79, 82                ; c
        .byte 39, 43                ; d

        .include "lib.inc"

        .enc "screen"
title   .null "scpu5 chain"
t_hd    .null "c  any  all  (k in cycles)"
t_soak  .null "soak (cond d) at k="
t_frames .null "frames"
t_early .null " early"
t_order .null " order"
