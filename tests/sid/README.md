# tests/sid: reSID-8 board suite (T25, SID-UPSTREAM)

The open audio bugs of GideonZ/1541ultimate as re-runnable tests for our SID engine (reSID8 = Dag Lem's reDIP at UltiSID
level). Built 2026-10-09, **not yet run on the board**.

    python3 tests/sid/fetch_media.py            # once: VICE testprogs, csdb media, LAMENOTER from ~/Downloads
    python3 tests/sid/resid8_board.py           # automated items, both chip models
    python3 tests/sid/resid8_board.py --ear     # + record the digi items to tests/sid/wav/*.wav
    python3 tests/sid/resid8_board.py --engine ultisid   # baseline on Gideon's core (UltiSID menu names): the bugs should show
    python3 tests/sid/resid8_board.py --dry-run          # REST calls only, no connection
    python3 tests/sid/resid8_board.py --selftest         # offline, see below

Needs the board free (no Lorenz, no JTAG/REST user), turbo off (1 MHz, the programs count cycles), REST on `--host`
(default 192.0.2.189, env `TRX_HOST`). No flash is written (`configs:save_to_flash` is never called). Every setting it
touches (speaker, SID socket/reSID8 addresses, range split, auto mirroring, chip model) is read first and put back at the end,
also after an error or Ctrl-C; the machine is reset at the end (`--no-reset-end`). The suite runs through failures; exit
status 1 if any check FAILed or ERRORed.

## Items (per chip model: `reSID8 1 Chip Model` = 6581, then 8580; reSID8 1 at $D400, everything else unmapped)

| id | issue | what is checked | pass criterion (the real chip's behaviour, from the issue) |
|---|---|---|---|
| lfsr | #954 | OSC3 read once per noise shift (`asm/lfsr.asm`, 256 samples at $C000) | bit 0 of sample t = bit 0 of t-23 xor t-18 (taps 22,17), 233 checks all fine; OSC3 bits are spaced 20,18,14,11,9,5,2,0 apart; first reads after the test bit are not `FF FF FF` |
| lockup | #954 | noise, noise+pulse (always low), noise, test bit 3.3 s (`asm/lockup.asm`) | after noise+pulse OSC3 reads `00` 256 times; after the test bit `fe fc fc fc f8 f8 f8 f0` |
| osc3 | #951 #756 #352 | `detectSidModel` of the SID player (`asm/osc3delay.asm`) | 6581 reads `01 03`, 8580 reads `00 03`; the first OSC3 reads per waveform are printed as INFO |
| wave10/20/40 | #756 | VICE `SID/waveforms/waveforms-{10,20,40}-{6581,8580}.prg` (triangle, saw, pulse against recorded real chips) | border green |
| adsr-* | #702 | VICE `SID/envelope`: `testFlip00toFF`, `testFlipFFto00`, `testADSRDelayBug`, `lft-adsr-test` | border green |

`--with-info` adds `waveforms-80` (noise: fails everywhere by design, information only, ~25 s each).
Own programs report through RAM (`$C3FE/$C3FF` = `5A A5` when done, data at `$C000..`); the VICE programs through `$D020`
(read twice, 1 s apart, until it is stable at 5 = pass or 2/10 = fail). The first REST read comes only after the program's own
run time (`wait` in the item table), because a DMA read inside a timed loop would disturb it. Programs are started with
`POST /v1/runners:run_prg`; the BASIC stub is `SYS 4096` (own) / `SYS 2061` (VICE).

## Ear items (`--ear`, no pass/fail)

Each item is launched (SID player or PRG/CRT runner over REST), `streams/audio:start` goes to this Mac (unicast, or
`--multicast 239.0.1.65`), 25 s (`--ear-seconds`) of the 48003 Hz stereo stream are written to `wav/<id>_<chip>.wav` with
RMS, peak, clipped samples, noise floor (quietest 10 ms) and DC per channel and the lost-packet count. For ear items both
engines are mapped ($D400, $D420) and set to the chip model. Listen to the 6581 and 8580 WAVs of the same item.
Fetched by `fetch_media.py`: Next Level `Insomnia.sid`, `InsomniaShort.sid`, `SkyBuster.sid` (#922), A Pig Quest V1.02 CRT,
Planet Golf, BOZUMA and The Last V8 (csdb cracks/releases, whichever the release lists; check what landed in `media/`).
- **A Pig Quest original**: Alex's dump of the original cartridge is on the board, `USB1/apigquest.crt`; copy it to
  `media/pigquest/apigquest_original.crt` (item `ear-pigquest-orig`, the Piggy 18 splash plays at start). The csdb TREX crack
  (`ear-pigquest`) opens its own menu first; the script presses 3 (PIGGY18 SPLASH).
- **LAMENOTER**: Alex's zip holds only the U64/PCM pair (`lamenoter-u64.prg` + `breadbox-pcm.reu`, sampler path, a control).
  The 8-bit Mahoney-on-SID pair (`lamenoter.prg` + `breadbox.reu`) is **not** in it and not fetched. A REU image has no REST
  load call: load it by hand (file browser, Load into REU, REU 16 MB), then `--ear --ear-prepared --items-ear ear-lamenoter-pcm`.
- Planet Golf, BOZUMA and The Last V8 come as D64: best-effort autostart (mount, `LOAD"*",8`, `RUN` after `--load-wait` s, recording from RUN). A game that needs `,8,1` or a key press records silence or a prompt (visible in the metrics): start it by hand then.
- Planet Golf's speech comes in play (ball out of bounds), the intro recording only catches the start.

## --selftest (offline, no board, no VICE emulator)

`resid_ref.py` is a cycle-counting 6502 (documented opcodes, access on the last cycle of an instruction, PAL raster counter
for $D011/$D012) in front of TRX64's vendored reSID (VICE 3.10 `src/resid`, 1.0-pre2, clocked cycle by cycle through the
sampler path, because the pipelined OSC3/ENV3 are only modelled there). `build_resid.sh` builds it from the TRX64 checkout
(`TRX64=...`, default `~/Development/C64/Tools/TRX64`); `selftest` runs every automated item on 6581 and 8580 and checks that the
verdicts match `RESID_EXPECT` in the script, and that the verdicts reject what #954 / #951 measured on the UltiSID (`verdicts`
rows). Validation of the reference itself: VICE `waveforms-10/20/40` compare 256 OSC3 samples bit by bit with recorded real
chips and match exactly on both models. Where the reference leaves the real chip (8580 test-bit refill after 3.3 s: reSID needs
~9.7 s) the board criterion stays the real chip's and `RESID_EXPECT` documents the deviation.

Not VICE: only the VICE testprogs' source and .prg files are used (svn export); the VICE emulator is not run. `trx64_sim.py`
drives the TRX64 daemon too, but its CPU-side read of $D41B/$D41C is a fastsid shadow, not reSID (`sid.rs` `read_osc3`), so it
cannot give the expected values for these tests; it is kept only as a plain C64 for program logic.

## Files

`resid8_board.py` (suite), `resid_ref.py` (offline reference), `trx64_sim.py` (TRX64 daemon client), `asm/*.asm` (own programs,
`tools/64tass.sh`, built into `.cache/prg/`), `fetch_media.py`, `build_resid.sh`. Git-ignored: `media/`, `.cache/`, `wav/`.
