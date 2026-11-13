; T218 bench program: the DSP-n coprocessor. LoROM under 1 MiB: the chip's data register at $30:8000, its status register at $30:C000 (DSP_LHRomMap.vhd, A0 = address bit 14).
; The firmware of the bench (tools/snes_dsp_fw.py) puts $1234 into DR and loops; this program waits for RQM (status bit 7), reads DR twice (low byte, high byte)
; and stores them at $7E0200/$7E0201, DONE. Ours, no third party code.
        .title "SNES DSP BENCH"
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
w_rqm:  lda $30C000
        bpl w_rqm
        lda $308000
        sta $7E0200
        lda $308000
        sta $7E0201
        lda #$D0
        sta $7EFFFF             ; DONE
forever: bra forever
isr:    rti
