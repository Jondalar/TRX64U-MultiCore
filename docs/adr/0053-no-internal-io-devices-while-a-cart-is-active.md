# 0053. No internal IO devices in $DE00-$DFFF while a cartridge is active

Date: 2026-10-09

Status: Accepted

## Context

On 2026-10-09 Timo's Castle (real EasyFlash 1 in the port) crashed with SID noise on "Reset Highscore" and saved
nothing on our core. The game's EAPI copies itself to $DF80-$DFFF, the EasyFlash's own 256-byte RAM in IO2, and runs
from there. With Alex's settings our core also has internal devices in IO1/IO2: the UCI (Command Interface), Ultimate
Audio ($DF20-$DFFF) and the REU registers ($DF00). An internal device that answers in a range the cartridge uses
fights the cartridge's bus drivers (physical cart) or hides the cartridge's own registers/RAM (emulated .crt). Gideon's
app keeps the UCI on for every EasyFlash, moved to $DE1C (app commit "Keep the UCI at $DE1C for every EasyFlash").
For no CRT type do we know for sure which addresses in $DE00-$DFFF a program touches.

## Decision

Alex (2026-10-09): UCI off for emulated EasyFlash too, and in general; later the cartridge definitions may declare
free ranges so that the UCI can be switched selectively. Gideon's behaviour is not a constraint here.

- Quick fix, M1 (T72): while a cartridge runs, physical (port armed) or emulated (.crt), the REU, Ultimate Audio and
  the UCI are off, by definition, whatever the settings say (Alex 2026-10-09, after the board check below).
- Proper solution, M2 (T73, spec first): no internal device (UCI, Ultimate Audio, REU registers, ...) answers in
  $DE00-$DFFF while a cartridge is active, except in a range the cartridge definition declares free; the cartridge
  definitions get an IO usage field. No declaration means off.
- This deliberately deviates from Gideon's behaviour (UCI at $DE1C for every EasyFlash). Not offered upstream.

## Consequences

- Programs that need the UCI while a cartridge is plugged in or emulated work only with cartridge types whose
  definition names a free range.
- The cartridge definitions get an IO usage field; the firmware/core switches the internal devices by it.
- Board 2026-10-09: with Ultimate Audio enabled the CPU read back 123 of 124 bytes wrong at $DF80 (sampler registers
  over the cart RAM); with Ultimate Audio, REU and UCI disabled at runtime Timo's Castle's "Reset Highscore" works
  (Alex). Alex: the same class of problem was seen with Lykia, Mike's and Protovision carts.

## Links

TODO T72, T73, T57, T16; docs/specs/T57-trex-crt-both-cores.md; docs/specs/T16-cartridge-port.md; doc/cartridge_mapping_descriptor.md (app)
