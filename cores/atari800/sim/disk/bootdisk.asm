; T108: the boot sectors of the disk bench, written here (no third-party software). Three 128-byte sectors at $0700.
; The OS reads sector 1, takes the header (flags, sector count, load address, DOSINI), loads the rest behind it and calls
; load+6, as every boot disk is started; a disk that needs BASIC off or the DOS vector does more, this one only has to prove that
; all three sectors arrived: the colour it paints the screen with sits in the LAST byte of sector 3, so a boot that stops after
; sector 2 shows another colour.
COLOR2  = $02C6
COLOR4  = $02C8
        * = $0700
        .byte 0                 ; flags
        .byte 3                 ; sectors to load
        .word $0700             ; load address
        .word dosini            ; goes to DOSINI
boot    lda colour              ; load+6: the OS calls this after the load and never gets control back
        sta COLOR2
        sta COLOR4
spin    jmp spin
dosini  rts
        .fill $087F - *, $EA
colour  .byte $46               ; sector 3, byte 127
