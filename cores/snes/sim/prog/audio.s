; T142 bench program: reset into native mode and DONE at once; the S-SMP's boot stub (tools/snes_asm.py spcaudio) plays one voice and the bench counts the
; samples of audio_l / audio_r. Ours, no third party code.
        .title "SNES CORE AUDIO"
        .vector reset = reset
        .vector nmi = isr
        .vector ereset = reset
        .org $008000
reset:  sei
        clc
        xce
        rep #$38
        ldx #$1FFF
        txs
        sep #$20
        lda #$8F
        sta $2100
        stz $4200
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
isr:    rti
