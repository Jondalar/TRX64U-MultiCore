# Audit of the ✅ rows in TODO.md (2026-10-04)

Every M1 row marked ✅ was checked against its spec (or, without a spec, against Gideon's
behaviour for the C64U user), section by section: built (code, not comments) and tested on
the board (spec as-built text, session docs, commit messages). Read-only, four agents. Gap
audit §6 decisions were applied and not re-reported.

## Verdict per row

| Row | Verdict | Why |
|---|---|---|
| T01 Flash | holds | row text "read-only" stale; custom KERNAL/drive ROM untested |
| T02 Overlay | partly | menu joystick navigation missing (`U64II_KEYB_JOY` reads 0xFF, T47); USB keyboard untested |
| T03 SD | partly | only the mount is evidenced; write, hot-plug, card detect untested |
| T04 Rocker | **does not hold** | Freeze is not wired: `btn_freeze` reaches ITU button 2, nothing consumes it, no cartridge freeze |
| T05 IEC/1541 | partly | SAVE/disk write, G64, Run Disk, real drive on the DIN port untested |
| T06 CRT | partly | freezer button missing (see T04); AR/RR 7 VICE fails without an Alex decision; Ultimax VIC flicker (D7) open; row text stale (GMod2 EEPROM is tested) |
| T07 USB | partly | USB keyboard, mouse, gamepad untested |
| T08 LEDs | partly | selector sources "Drive A/B Pwr", "IRQ Line" are constants (`top.v:1551-1555`); LED strip untested |
| T09 Keyboard LEDs | holds | lighting modes untested |
| T10 Lorenz | holds | |
| T11 DDR2 path | holds | rests on T05/T06/T33 board runs |
| T12 Freeze | partly | Q9 debugger pause as a session not built (moved to T55 without an Alex decision); menu box over the dimmed still, joystick in the menu, cart start/reset from the menu, ordered commit, physical cart in a freeze: untested; look omits AMD ID/status and UCI registers (no decision) |
| T13 Audio/debug stream | partly | row text wrong: debug/IEC streams were dropped (G15); audio carries one SID only |
| T14 I2C | holds | board evidence indirect |
| T15 Flash boot | holds | `boot flash`, lock button, bad-header paths sim only; BUILD register reads 0 over JTAG (open); spec §6 stale (33 vs 16) |
| T16.2 Cart write-back | partly | missing without decision: REST `/v1/cartridge*` (§8.3), ABI `cart_wb_*` (§8.2), `cartwb.u2a` inspector (§8.4), rocker power-off warning (§7.6, "-> T26" but T26 doesn't carry it), cart writes in the ring and `.c64re` (§7.8, "-> T20" but T20 doesn't carry it); triggers other than the menu not tested one by one |
| T17 Speaker | **does not hold** | no recorded test, no level check against Gideon |
| T20 Delta ring | **does not hold** | missing without decision: restore of SID, REU, drive A, cart flash/EEPROM and the other banking types; `.c64re` SID/drive/REU/cart nodes; loading a TRX64-written `.c64re`; whole-ring load (`ringload` takes one anchor); native `.trxring`; play back/forward and the F11 state machine; USB F9-F12 keys; periodic keyframes; input log/replay; window 28-40 s instead of 60 s; §13 test items; open questions 3, 4, 8, 11, 12 |
| T28 UCI | partly | board test is one identify at $DF1C; $DE1C with EF, abort, multi-part, the app's UCI tests, comparison with Gideon untested; row text contradicts itself |
| T31 CI | partly | missing without decision: ue2emu app-test stage, Mac hardware-in-the-loop runner, host firmware job, remaining Verilator benches (`bling_io` fails on main) |
| T33 Cart decoder | holds | small items: IO2 open bus not measured on Gideon's core; AR/RR kept as a dedicated block against the letter of decision 6 |
| T38 Input | partly | reset byte, freeze byte, USB keyboard, physical + app keys together untested; paddles/mouse/WASD/swap open in T47 |
| T39 Reset sense | holds | port P20 reset sense untested |
| T40 NTSC | partly | Lorenz in NTSC on the board missing; M1-M7 measurements on Gideon's core missing; TOD board check missing; 60 Hz still fix board-untested |
| T44 Flash gate | moved | out of M1 (Alex 2026-10-04), gate open |
| T53 UE2 build | **does not hold** | board test open (Till, 2026-10-05); gate gap void since T44 is out; T49 dual-bitstream package not built |

## Missing parts without a decision (need Alex)

T04 rocker freeze · T02 menu joystick · T08 LED sources · T12 Q9 · T16.2 REST/ABI/inspector/power-off warning/ring writes · T20 the list above · T31 stages · T40 NTSC Lorenz and Gideon measurements · T06 AR/RR failures and D7.

## Text to fix in TODO

T01 "read-only", T06 "GMod2 EEPROM open", T13 "debug stream", T28 contradiction, T15 spec §6.

## Decisions on the missing parts (Alex, 2026-10-04, one by one)

| # | Item | Decision |
|---|---|---|
| A01 | Rocker Freeze to the cartridge (T04/T06) | M1 only if it works out; bottom of the backlog |
| A02 | Menu by joystick (T02) | out |
| A03 | LED selector sources (T08) | out |
| A04 | LED strip and lighting effects (led_engine) | out of the bitstream (parked at tag park/led-effects); keyboard data path, SHIFT LOCK LED, case LED on/off stay; "who wants the original C64U feeling boots Gideon's firmware" |
| A05 | Mixer hardware side: app gains (master x UltiSID L/R) applied after reDIP for line out/headphones; speaker gain checked | M1 (T62 build); parts that need T25 documented and tested with T25; Alex listens |
| A06 | Monitor stop shows true values | M1, part of T21. Rule: a breakpoint stop IS a freeze, only at an exactly deterministic point; one stop mechanism |
| A07.1 | REST for the cartridge state (dirty, save, discard) | M1. Rule: what a person can do in the app, REST can do |
| A07.2 | cartwb.u2a inspector | out (the monitor replaces it) |
| A07.3 | Power-off warning while a save is pending | M1 |
| A08 | Timewarp with the REU | REU enabled = no ring, no Timewarp (T20 spec decision) |
| A08.1 | SID after a rewind | not restored, accepted: the SID may be off until it gets new data (said repeatedly) |
| A08.2 | Drive A restored by the ring (CPU, VIAs, head) | later (not M1) |
| A08.3 | Cartridge in the ring | M1: bank selection of every cart type restored by the ring; the changed flash/EEPROM content (save) staying after a rewind is accepted ("ok, auch wenn nicht optimal") |
| A08.4 | VIC internals after a rewind (compare latch on lines 300-311/0: one raster IRQ may be lost or doubled) | accepted |
| A09 | Full .c64re exchange with TRX64 (board snapshot with SID/drive/cart; loading TRX64-written, compressed .c64re) | M2/M3 (Alex: moved there earlier; it was not written down) |
| A10 | Timewarp play backward/forward like a video | not M1 (later) |
| A11 | USB F9-F12 transport keys (T20 §11.4) | obsolete: Timewarp is operated through its own UI on the C64U |
| A12 | Timewarp window about 30 s (28-40 s measured) instead of 60 s | enough, accepted |
| A13 | Load a saved Timewarp ring | out. Wider: **M1 has no .c64re and no .c64rering dump or load at all** (menu, REST, monitor verbs); the existing entries and their code are removed (**done 2026-10-06**, branches a13-no-dump / a13-no-dump-app; Alex: "Den Code darfst Du wegwerfen"); M2/M3 rebuilds it with the full exchange (A09) |
