; The Apple //e's soft switches and the pieces every bench program uses. Written for this project (TREX).
.cpu "65c02"
KBD       = $C000     ; key + strobe (read);  $C000 write = 80STORE off
KBDSTRB   = $C010     ; clear the strobe;  read: bit 7 any key down
STORE80_OFF = $C000
STORE80_ON  = $C001
RDMAIN    = $C002
RDAUX     = $C003
WRMAIN    = $C004
WRAUX     = $C005
INTCXROM_OFF = $C006
INTCXROM_ON = $C007
STDZP     = $C008
ALTZP     = $C009
SLOTC3_OFF = $C00A
SLOTC3_ON = $C00B
COL80_OFF = $C00C
COL80_ON  = $C00D
ALTCHAR_OFF = $C00E
ALTCHAR_ON = $C00F
SPKR      = $C030
TXTCLR    = $C050     ; graphics
TXTSET    = $C051     ; text
MIXCLR    = $C052
MIXSET    = $C053
PAGE1     = $C054
PAGE2     = $C055
LORES     = $C056
HIRES     = $C057
AN3_ON    = $C05E     ; apple2.vhd's DHIRES_MODE is the AN3 flip-flop itself: the //e's double hires needs it CLEAR ($C05E), lores and normal hires need it SET
                      ; ($C05F: with it clear the timing generator shifts them at the wrong rate: no lores colours, no half-dot delay of hires bytes); the //e ROM sets it at start
AN3_OFF   = $C05F
BUTN0     = $C061     ; open apple
BUTN1     = $C062     ; closed apple
PDL0      = $C064
PTRIG     = $C070
LCBANK2_RD = $C080    ; language card: read RAM, write protected, bank 2
LCBANK2_RDWR = $C083
LCBANK1_RDWR = $C08B
LCROM_WR2 = $C081
; zero page scratch
PTR       = $06
PTR2      = $08
LOGP      = $0A
; bench log area in main RAM (the bench reads it back through the DMA window)
LOG       = $0300
