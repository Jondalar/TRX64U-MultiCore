; Test runner, copied to $0800 by boot.asm (RAM in every memory map, Ultimax
; included). It works through a table of operations behind it:
;   1 lo hi val  write      2 lo hi  read, result to $0400+n
;   4 lo hi      read only  3 n      wait n x 1280 cycles      0  end
;   5 lo hi val lo hi   write, and read in the very next instruction
;   6 lo hi val  read until it is val (at most 65536 times), result to $0400+n
; At the end $07E7 = $AA.
        * = $0800
run     ldx #0
next    jsr fetch
        beq done
        cmp #1
        beq opw
        cmp #2
        beq opr
        cmp #3
        beq opd
        cmp #4
        beq opq
        cmp #5
        beq opx
        cmp #6
        bne done
        jmp opp
done    lda #$aa
        sta $07e7
        jmp *
opw     jsr fetch
        sta wa+1
        jsr fetch
        sta wa+2
        jsr fetch
wa      sta $ffff
        jmp next
opr     jsr fetch
        sta ra+1
        jsr fetch
        sta ra+2
ra      lda $ffff
        sta $0400,x
        inx
        jmp next
opq     jsr fetch
        sta qa+1
        jsr fetch
        sta qa+2
qa      lda $ffff
        jmp next
opx     jsr fetch
        sta xw+1
        jsr fetch
        sta xw+2
        jsr fetch
        pha
        jsr fetch
        sta xr+1
        jsr fetch
        sta xr+2
        pla
xw      sta $ffff
xr      lda $ffff
        sta $0400,x
        inx
        jmp next
opd     jsr fetch
        sta cnt
        stx xs
d2      ldy #0
d1      dey
        bne d1
        dec cnt
        bne d2
        ldx xs
        jmp next
opp     jsr fetch
        sta pa+1
        jsr fetch
        sta pa+2
        jsr fetch
        sta pv
        stx xs
        ldy #0
        ldx #0
pa      lda $ffff
        cmp pv
        beq +
        dex
        bne pa
        dey
        bne pa
+       ldx xs
        sta $0400,x
        inx
        jmp next
fetch   lda table
        inc fetch+1
        bne +
        inc fetch+2
+       cmp #0
        rts
cnt     .byte 0
pv      .byte 0
xs      .byte 0
table
