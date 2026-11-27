; The XEX boot loader of the Atari 800 XL descriptor (T93 D6, T88 D8). Own code, written for this project.
;
; The application presents a .xex file as a bootable single-density disk: sectors 1-3 are this loader (the OS loads them to
; $0700 and calls $0706), sectors 4... are the file's bytes, 128 to a sector, zero padded. The loader reads the file through
; SIOV, one sector at a time, and does what an Atari DOS binary loader does:
;   - segments "[$FFFF] start end data", the $FFFF marker is optional and may repeat
;   - after a segment: if INITAD ($02E2) is non-zero, JSR through it, then clear it
;   - at the end of the file: JMP through RUNAD ($02E0) if it is non-zero, else RTS to the OS
;   - BASIC is switched off first (PORTB bit 1, BASICF), as programs that use $A000-$BFFF expect
; It lives at $0700-$087F with its sector buffer at $0880-$08FF and uses no zero page (the store is self-modifying), so a
; program may use all of page zero. A segment that overlaps $0700-$08FF destroys the loader: not supported.
; The application patches the 24-bit file length at FILELEN (offset 9 of sector 1).

        * = $0700

PORTB   = $d301
BASICF  = $03f8
INITAD  = $02e2
RUNAD   = $02e0
DCB     = $0300
SIOV    = $e459
BUF     = $0880
dst     = stdst + 1
COLBK   = $d01a

        .byte 0                 ; boot flags
        .byte 3                 ; boot sectors
        .word $0700             ; load address
        .word initrts           ; DOSINI: the OS calls it after the boot, it must just return
        jmp start               ; $0706: the OS JSRs here
filelen .byte 0, 0, 0           ; $0709: bytes of the .xex, patched by the application
initrts rts

start   lda PORTB
        ora #$02
        sta PORTB
        lda #1
        sta BASICF
        lda #0
        sta INITAD
        sta INITAD+1
        sta RUNAD
        sta RUNAD+1
        lda #4
        sta secno
        lda #0
        sta secno+1
        lda #128
        sta bufpos              ; empty buffer: the first byte reads sector 4
        lda filelen
        sta rem
        lda filelen+1
        sta rem+1
        lda filelen+2
        sta rem+2

seg     jsr getw                ; the first word of a segment: $FFFF (optional marker) or the start address
        bcs done
        lda wlo
        and whi
        cmp #$ff
        bne gotsta
        jsr getw
        bcs done
gotsta  lda wlo
        sta dst
        lda whi
        sta dst+1
        jsr getw                ; the end address (inclusive)
        bcs done
        lda wlo
        sta endp
        lda whi
        sta endp+1

ld      jsr getbyte
        bcs done                ; a truncated file ends like a complete one
stdst   sta @w $0000               ; the operand is the destination pointer: self-modifying, no zero page
        lda dst
        cmp endp
        bne nxt
        lda dst+1
        cmp endp+1
        beq segend
nxt     inc dst
        bne ld
        inc dst+1
        jmp ld

segend  lda INITAD
        ora INITAD+1
        beq seg
        jsr callini
        lda #0
        sta INITAD
        sta INITAD+1
        jmp seg
callini jmp (INITAD)

done    lda RUNAD
        ora RUNAD+1
        beq norun
        jmp (RUNAD)
norun   rts

getw    jsr getbyte
        bcs gw_e
        sta wlo
        jsr getbyte
        bcs gw_e
        sta whi
        clc
gw_e    rts

getbyte lda rem
        ora rem+1
        ora rem+2
        beq eof
        ldx bufpos
        cpx #128
        bne have
        jsr readsec
        ldx #0
have    lda BUF,x
        inx
        stx bufpos
        pha
        lda rem
        bne d0
        lda rem+1
        bne d1
        dec rem+2
d1      dec rem+1
d0      dec rem
        pla
        clc
        rts
eof     sec
        rts

readsec lda #3
        sta tries
rtry    lda #$31
        sta DCB
        lda #1
        sta DCB+1
        lda #$52
        sta DCB+2
        lda #$40
        sta DCB+3
        lda #<BUF
        sta DCB+4
        lda #>BUF
        sta DCB+5
        lda #7
        sta DCB+6
        lda #0
        sta DCB+7
        lda #128
        sta DCB+8
        lda #0
        sta DCB+9
        lda secno
        sta DCB+10
        lda secno+1
        sta DCB+11
        jsr SIOV
        tya
        bmi rfail
        inc secno
        bne rdone
        inc secno+1
rdone   rts
rfail   dec tries
        bne rtry
rerr    inc COLBK
        jmp rerr

secno   .word 0
bufpos  .byte 0
rem     .byte 0, 0, 0
wlo     .byte 0
whi     .byte 0
endp    .byte 0, 0
tries   .byte 0
