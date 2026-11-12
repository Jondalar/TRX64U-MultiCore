; T89.1 bench program (t89-write): the Disk II (slot 6) driven the way DOS 3.3's RWTS does it, a command at a time from a mailbox in main RAM.
; Written for this project (TREX) from the documented routines (Beneath Apple DOS ch. 6-7: RDADR16, READ16 with POSTNIB, PRENIBBLE, WRITE16, the track
; format), no Apple ROM call. The bench (tb_a2_rwts.vhd) is the application: it fills MB+1.. and then MB (the command), the program does it and clears MB.
;   MB = $0200   +0 command (0 = idle)  +1 sector  +2 track (the number in the address fields)  +3 result  +4 attempts of the address search
;   1 READSEC  find the address field (track MB+2, sector MB+1), RDADR16 / READ16 / POSTNIB: 256 bytes to OUT; result 0 ok, 1 no address field, 3 data error
;   2 WRITESEC PRENIBBLE SRC (256 bytes) into the 343 nibbles, find the address field, WRITE16: 5 self sync (40 cycles), D5 AA AD, 343 nibbles, DE AA EB, FF
;              (every load 32 cycles after the one before, 40 after a sync); result 0, 1, or 2 (write protected)
;   3 RECAL    48 half steps down, then magnet 0 only
;   4 FORMAT   the track under the head, as DOS 3.3's INIT lays it out: 14 sync, then 16 x (address field with track MB+2, 6 sync, data field of
;              343 zero nibbles, 14 sync); result 0 or 2
;   5 SELECT   MB+1: 0 = drive 1, 1 = drive 2 (the motor stays on)
;   7 STEP    MB+2 half steps, up (MB+1 = 0) or down (MB+1 = $80); the last magnet stays on
;   6 DUMP     MB+1 pages of latch bytes to DUMP ($3000): LDA $C08C / BPL
        .include "apple.inc"
MB      = $0200
AFLD    = $0210                 ; the four fields of an address field: AFLD+3 volume, +2 track, +1 sector, +0 checksum
SRC     = $2000                 ; WRITESEC: the 256 bytes to write
AUX     = $2100                 ; PRENIBBLE: the 86 secondary values
PRI     = $2200                 ; PRENIBBLE: the 256 primary values
ENC     = $2300                 ; the 343 nibbles of a data field (ENC2 is the second page, 87 of them)
ENC2    = $2400
DNIBL   = $2500                 ; nibble -> 6 bit value
RAUX    = $2600                 ; READ16: the secondary values as read
RPRI    = $2700                 ; READ16: the primary values as read
OUT     = $2800                 ; POSTNIB: the 256 bytes
AF      = $2900                 ; FORMAT: 16 address fields of 8 nibbles
DUMP    = $3000
SLOT    = $60                   ; slot 6: the X register of every access below

PHI     = $10
CNT     = $11
OLD2    = $12
ZT      = $14
ZS      = $15
RETRY   = $16
EP      = $18                   ; 16 bit pointer
PREV    = $1A
GRP     = $1B
IDX     = $1C
ZC      = $1D                   ; counter of the sync loops of FORMAT
SAVY    = $1E
T26     = $26
T27     = $27

; ---- timing macros --------------------------------------------------------------------------------------------------------------
; The write latch is loaded by STA $C08D,X (the store is the fifth cycle of the instruction); a disk byte is 32 cycles, a self sync byte 40. A macro or a
; loop below is written so that the cycles between two stores add up to the byte time of the first one, counted instruction by instruction.
; wb value, period, pre: CMP Q6L (shift), the value, padding, STA Q6H. `period` = cycles from the previous store to this one, `pre` = cycles since the
; previous store that the macro does not contain.
wb      .macro
        cmp $C08C,x             ; 4
        lda #\1                 ; 2
        .if ((\2 - 11 - \3) % 2) == 1
        bit $00                 ; 3
        .rept (\2 - 11 - \3 - 3) / 2
        nop
        .next
        .else
        .rept (\2 - 11 - \3) / 2
        nop
        .next
        .endif
        sta $C08D,x             ; 5
        .endm

; fscont m: after a store of a sync byte, write m more FF, 40 cycles apart, A = $FF. Entered 8 cycles after that store; leaves 7 cycles elapsed.
; (ZC is a zero page cell; the STA to it is forced to the absolute form: 4 cycles, the DEC is 5)
fscont  .macro
        lda #\1                 ; 2
        sta @w ZC               ; 4
        lda #$FF                ; 2
_a:     cmp $C08C,x             ; 4
        bit $00                 ; 3
        .rept 10
        nop                     ; 20
        .next
        sta $C08D,x             ; 5
        dec ZC                  ; 5
        bne _a                  ; 3   period 4+23+5+5+3 = 40
        .endm

; the 343 nibbles of ENC / ENC2 at 32 cycles each. Entered with 5 cycles elapsed since the store before it (the macro's own LDY #0 and, when the
; argument is 0, a BIT; with 1 the caller has spent 3 cycles), leaves 2 cycles elapsed.
datafield .macro
        ldy #0                  ; 2
        .if \1 == 0
        bit $00                 ; 3
        .endif
_la:    lda ENC,y               ; 4
        cmp $C08C,x             ; 4
        iny                     ; 2
        .rept 6
        nop                     ; 12
        .next
        sta $C08D,x             ; 5   27 from the top
        beq _lb                 ; 2 / 3
        jmp _la                 ; 3
_lb:    lda ENC2,y              ; 4
        cmp $C08C,x             ; 4
        iny                     ; 2
        cpy #87                 ; 2
        .rept 6
        nop                     ; 12
        .next
        sta $C08D,x             ; 5   29 from the top
        bne _lb                 ; 3 / 2
        .endm

        * = $F000
reset:  sei
        cld
        ldx #$ff
        txs
        stz MB
        stz MB+1
        stz MB+2
        stz MB+3
        stz PHI
        jsr mkdnibl
        ldx #SLOT
        lda $C0EA               ; drive 1
        lda $C0E9               ; motor on
        lda $C0EE               ; Q7 off: read mode
        lda $C0EC               ; Q6 off
main:   ldx #SLOT
        lda MB
        beq main
        cmp #1
        bne c2
        jmp do_rdsec
c2:     cmp #2
        bne c3
        jmp do_wrsec
c3:     cmp #3
        bne c4
        jmp do_recal
c4:     cmp #4
        bne c5
        jmp do_format
c5:     cmp #5
        bne c6
        jmp do_select
c6:     cmp #6
        bne c7
        jmp do_dump
c7:     cmp #7
        bne done
        jmp do_step
done:   stz MB
        jmp main

; ---- tables ---------------------------------------------------------------------------------------------------------------------
xlate:  .byte $96,$97,$9A,$9B,$9D,$9E,$9F,$A6,$A7,$AB,$AC,$AD,$AE,$AF,$B2,$B3
        .byte $B4,$B5,$B6,$B7,$B9,$BA,$BB,$BC,$BD,$BE,$BF,$CB,$CD,$CE,$CF,$D3
        .byte $D6,$D7,$D9,$DA,$DB,$DC,$DD,$DE,$DF,$E5,$E6,$E7,$E9,$EA,$EB,$EC
        .byte $ED,$EE,$EF,$F2,$F3,$F4,$F5,$F6,$F7,$F9,$FA,$FB,$FC,$FD,$FE,$FF
swap2:  .byte 0,2,1,3

; DNIBL: every nibble that is not one of the 64 reads as $80 (the checksum fails), the others as their 6 bit value
mkdnibl:
        ldy #0
        lda #$80
mk1:    sta DNIBL,y
        iny
        bne mk1
        ldx #0
mk2:    lda xlate,x
        tay
        txa
        sta DNIBL,y
        inx
        cpx #64
        bne mk2
        rts

; ---- the stepper (as asm/woz.s) -------------------------------------------------------------------------------------------------
onestep:
        lda PHI
        asl
        sta OLD2
        lda MB+1
        bmi down
        lda PHI
        inc a
        and #3
        bra newphi
down:   lda PHI
        dec a
        and #3
newphi: sta PHI
        asl
        tax
        lda $C0E1,x             ; the new magnet on
        ldx OLD2
        lda $C0E0,x             ; the old one off
        rts

do_recal:
        lda #$80
        sta MB+1
        lda #48
        sta CNT
rc1:    jsr onestep
        dec CNT
        bne rc1
        lda $C0E0
        lda $C0E2
        lda $C0E4
        lda $C0E6
        lda $C0E1
        stz PHI
        jmp done

do_step:
        lda MB+2
        sta CNT
st1:    jsr onestep
        dec CNT
        bne st1
        jmp done

do_select:
        lda MB+1
        beq sel1
        lda $C0EB
        bra sel2
sel1:   lda $C0EA
sel2:   lda $C0E9
        jmp done

do_dump:
        lda #<DUMP
        sta EP
        lda #>DUMP
        sta EP+1
        lda MB+1
        sta CNT
        lda $C0EE
        ldy #0
dm1:    lda $C0EC
        bpl dm1
        sta (EP),y
        iny
        bne dm1
        inc EP+1
        dec CNT
        bne dm1
        jmp done

; ---- RDADR16: the address field, as DOS 3.3 reads it ----------------------------------------------------------------------------
; C clear: found, the fields in AFLD; C set: not found within 1,024 bytes or a bad checksum / epilogue
rdadr16:
        ldy #$FC
        sty T26
rda2:   iny
        bne rda3
        inc T26
        beq rdaerr
rda3:   lda $C08C,x
        bpl rda3
rda4:   cmp #$D5
        bne rda2
        nop
rda5:   lda $C08C,x
        bpl rda5
        cmp #$AA
        bne rda4
        ldy #3
rda6:   lda $C08C,x
        bpl rda6
        cmp #$96
        bne rda4
        lda #0
        sta T27
rda7:   lda $C08C,x
        bpl rda7
        rol a
        sta T26
rda8:   lda $C08C,x
        bpl rda8
        and T26
        sta AFLD,y
        eor T27
        sta T27
        dey
        bpl rda7
        tay
        bne rdaerr
rda9:   lda $C08C,x
        bpl rda9
        cmp #$DE
        bne rdaerr
        nop
rda10:  lda $C08C,x
        bpl rda10
        cmp #$AA
        bne rdaerr
        clc
        rts
rdaerr: sec
        rts

; the address field of track ZT, sector ZS: up to 64 tries of RDADR16
find_addr:
        lda #64
        sta RETRY
fa1:    jsr rdadr16
        bcs fa_next
        lda AFLD+2
        cmp ZT
        bne fa_next
        lda AFLD+1
        cmp ZS
        bne fa_next
        clc
        rts
fa_next:
        dec RETRY
        bne fa1
        sec
        rts

; ---- READ16: the data field, as DOS 3.3 reads it --------------------------------------------------------------------------------
; C clear: the 342 values are in RAUX (86) and RPRI (256), the checksum and DE AA were good
read16: ldy #$20
rd1:    dey
        beq rderr
rd2:    lda $C08C,x
        bpl rd2
rd3:    eor #$D5
        bne rd1
        nop
rd4:    lda $C08C,x
        bpl rd4
        cmp #$AA
        bne rd3
        ldy #$56
rd5:    lda $C08C,x
        bpl rd5
        cmp #$AD
        bne rd3
        lda #0
rd6:    dey
        sty T26
rd7:    ldy $C08C,x
        bpl rd7
        eor DNIBL,y
        ldy T26
        sta RAUX,y
        bne rd6
rd8:    sty T26
rd9:    ldy $C08C,x
        bpl rd9
        eor DNIBL,y
        ldy T26
        sta RPRI,y
        iny
        bne rd8
rd10:   ldy $C08C,x
        bpl rd10
        cmp DNIBL,y
        bne rderr
rd11:   lda $C08C,x
        bpl rd11
        cmp #$DE
        bne rderr
        nop
rd12:   lda $C08C,x
        bpl rd12
        cmp #$AA
        bne rderr
        clc
        rts
rderr:  sec
        rts

; POSTNIB: RAUX / RPRI -> OUT
postnib:
        ldy #0
pn1:    ldx #$56
pn2:    dex
        bmi pn1
        lda RPRI,y
        lsr RAUX,x
        rol a
        lsr RAUX,x
        rol a
        sta OUT,y
        iny
        bne pn2
        ldx #SLOT
        rts

do_rdsec:
        lda MB+1
        sta ZS
        lda MB+2
        sta ZT
        jsr find_addr
        bcc rs1
        lda #1
        sta MB+3
        jmp done
rs1:    jsr read16
        bcc rs2
        lda #3
        sta MB+3
        jmp done
rs2:    jsr postnib
        stz MB+3
        jmp done

; ---- PRENIBBLE: SRC -> AUX (86) and PRI (256) -----------------------------------------------------------------------------------
; byte i: PRI[i] = b >> 2; AUX[i mod 86] gets the two low bits, swapped, at bit 2*(i div 86)
prenib:
        ldy #85
        lda #0
pz1:    sta AUX,y
        dey
        bpl pz1
        ldy #0
        stz IDX
        stz GRP
pnl:    lda SRC,y
        pha
        lsr a
        lsr a
        sta PRI,y
        pla
        and #3
        phy
        tay
        lda swap2,y
        ldy GRP
        beq pnns
pnsh:   asl a
        dey
        bne pnsh
pnns:   ldx IDX
        ora AUX,x
        sta AUX,x
        ply
        inc IDX
        lda IDX
        cmp #86
        bne pnc
        stz IDX
        inc GRP
        inc GRP
pnc:    iny
        bne pnl
        ldx #SLOT
        rts

; the 343 nibbles: value n = XLATE[s(n) xor s(n-1)] over the 86 AUX values and the 256 PRI values, then XLATE[s(341)]
encode:
        lda #<ENC
        sta EP
        lda #>ENC
        sta EP+1
        stz PREV
        ldy #0
en1:    lda AUX,y
        jsr encb
        iny
        cpy #86
        bne en1
        ldy #0
en2:    lda PRI,y
        jsr encb
        iny
        bne en2
        lda PREV
        tax
        lda xlate,x
        sta (EP)
        ldx #SLOT
        rts
encb:   pha
        eor PREV
        tax
        lda xlate,x
        sta (EP)
        inc EP
        bne eb1
        inc EP+1
eb1:    pla
        sta PREV
        rts

; ---- WRITESEC: WRITE16 ----------------------------------------------------------------------------------------------------------
do_wrsec:
        jsr prenib
        jsr encode
        lda MB+1
        sta ZS
        lda MB+2
        sta ZT
        jsr find_addr
        bcc ws1
        lda #1
        sta MB+3
        jmp done
ws1:    lda $C08D,x             ; Q6 on: sense write protect
        lda $C08E,x             ; Q7 off
        bpl ws2
        jmp wprot
ws2:
        .rept WDELAY            ; extra cycles before the write starts: moves the first written bit against the bit grid of the old track
        nop
        .next
        lda #$FF
        sta $C08F,x             ; Q7 on with Q6 on: loads FF, write mode: sync byte 1
        #wb $FF, 40, 0
        #wb $FF, 40, 0
        #wb $FF, 40, 0
        #wb $FF, 40, 0          ; sync byte 5
        #wb $D5, 40, 0
        #wb $AA, 32, 0
        #wb $AD, 32, 0
        #datafield 0
        #wb $DE, 32, 2
        #wb $AA, 32, 0
        #wb $EB, 32, 0
        #wb $FF, 32, 0
        cmp $C08C,x
        lda $C08E,x             ; Q7 off a few cycles after the FF is loaded: it is cut short, as in DOS
        lda $C08C,x             ; Q6 off: read mode
        stz MB+3
        jmp done
wprot:  lda #2
        sta MB+3
        jmp done

; ---- FORMAT ---------------------------------------------------------------------------------------------------------------------
; AF: for sector s the 8 nibbles of the volume, track, sector and checksum fields, 4-and-4 (odd = (b >> 1) | $AA, even = b | $AA)
put44:  pha
        lsr a
        ora #$AA
        sta AF,y
        iny
        pla
        ora #$AA
        sta AF,y
        iny
        rts

mkaf:   ldy #0
        stz ZS
mkaf1:  lda #254
        jsr put44
        lda MB+2
        jsr put44
        lda ZS
        jsr put44
        lda #254
        eor MB+2
        eor ZS
        jsr put44
        inc ZS
        lda ZS
        cmp #16
        bne mkaf1
        rts

do_format:
        jsr mkaf
        lda #<ENC               ; 343 zero nibbles: XLATE[0] = $96
        sta EP
        lda #>ENC
        sta EP+1
        ldy #0
fe1:    lda #$96
        sta (EP),y
        iny
        bne fe1
        inc EP+1
        ldy #87
fe2:    lda #$96
        dey
        sta (EP),y
        bne fe2
        ldx #SLOT
        ldy #$F8                ; Y = 8 * sector, kept through the whole track (the gap before sector 0 runs the end of the loop once: +8)
        lda $C08D,x
        lda $C08E,x
        bpl fw2
        jmp fwprot
fw2:
        lda #$FF
        sta $C08F,x             ; Q7 on with Q6 on: sync byte 1 of the 14 before the first address field
        jmp gap_cont
sec_top:
        #wb $D5, 40, 5
        #wb $AA, 32, 0
        #wb $96, 32, 0
        .for k = 0, k < 8, k += 1
        cmp $C08C,x
        lda AF+k,y
        bit $00
        .rept 8
        nop
        .next
        sta $C08D,x             ; 4+4+3+16+5 = 32
        .next
        #wb $DE, 32, 0
        #wb $AA, 32, 0
        #wb $EB, 32, 0
        #wb $FF, 32, 0
        #fscont 5               ; 6 sync bytes
        #wb $D5, 40, 7
        #wb $AA, 32, 0
        #wb $AD, 32, 0
        sty SAVY                ; 3
        #datafield 1            ; ldy #0 (2) after the 3 of STY
        ldy SAVY                ; 3
        #wb $DE, 32, 5
        #wb $AA, 32, 0
        #wb $EB, 32, 0
        #wb $FF, 32, 0          ; sync byte 1 of 14
gap_cont:
        #fscont 12
        cmp $C08C,x             ; 4
        tya                     ; 2
        clc                     ; 2
        adc #8                  ; 2
        tay                     ; 2
        lda #$FF                ; 2
        cpy #128                ; 2
        .rept 6
        nop                     ; 12      7 + 4 + 8 + 2 + 2 + 12 + 5 = 40
        .next
        sta $C08D,x             ; 5   sync byte 14
        beq fmt_end             ; 2 / 3
        jmp sec_top             ; 3   -> 5 cycles elapsed at sec_top
fmt_end:
        cmp $C08C,x
        lda $C08E,x             ; Q7 off
        lda $C08C,x             ; Q6 off
        stz MB+3
        jmp done
fwprot: lda #2
        sta MB+3
        jmp done

        * = $FFFA
        .word reset, reset, reset
