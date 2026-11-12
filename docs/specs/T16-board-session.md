# T16 board session: Alex's real cartridges

**Status:** first session 2026-10-04 (results at the end); completion built 2026-10-05 (`T16-cartridge-port.md` §13.11); the
final session (§Final session below) is open. Script: `tests/cart/t16_session.py` (wrapper `tests/cart/t16_session.sh`,
simulation `t16_sim.py`). Offline test: `tests/cart/t16_session.sh selftest` (67 cases: 4 carts read-only, write stages, the full
plan per cart, fault injections, abort paths; all pass). The first-session text below is kept as it was (history); where the
final session differs, the section at the end says so.

Carts (Alex, 2026-10-04): C64anabalt (plain 16K ROM, `rom16`), EasyFlash 1 with Timo's Castle (`ef1`),
MegaByter with Lykia (`megabyter`), GMod2 with Sam's Journey, EEPROM saves (`gmod2`). Q9 named Yeti Mountain and an
EF3 for P3-P5; this session uses the carts above instead.

## Before anyone touches the board

- `tests/lorenz.py` must not be running (it owns JTAG and REST; the script refuses to start then).
- Pins stay as measured; nothing here writes flash on the C64U itself. The C64U's flash holds **our older core
  c8753262** and our older app, so every power-on starts that core, not Gideon's.
- The script only reads (REST lens, JTAG status) unless `--write` is given.

## Per cart (Alex: power, plug, keys only)

| # | Alex | Script (run by the agent) | Time |
|---|---|---|---|
| 1 | Power off. Plug the cart. Power on, wait for the C64U's own boot. | - | 2 min |
| 2 | - | `tests/cart/t16_session.sh boot`: `load.sh c64_basic main-latest` into SRAM, `trxdbg boot staged ultimate.app` (fetched from the package), waits for REST, prints SLOT_STATUS | 3 min |
| 3 | Looks at the HDMI picture when asked (3-4 yes/no questions) | `run --cart X`: pre, boot, dump (2 passes), soak, pause100, matrix, internal | EF 25 min, others 12 min |
| 4 | Types `WRITE X` once; at the writer rounds presses keys (below); plays and saves in-game | `run --cart X --write --stages ...` (P4) | 25-35 min |
| 5 | Power off | - | |

Order: **rom16** (read-only, nothing can be damaged), **ef1** (most of the spec: bank switching, EAPI save),
**megabyter**, **gmod2** (EEPROM last). Whole session about 3.5 h with the write stages, about 1.5 h read-only.
These are estimates: the REST request time is not measured on the port yet; `pre` prints it and the projected
dump time. If large `readmem` replies stall (seen on bulk transfers from the board), use `--chunk 1024`.

Commands (after step 2, same power-on):

```
tests/cart/t16_session.sh run --cart rom16
tests/cart/t16_session.sh run --cart ef1                       # read-only stages
tests/cart/t16_session.sh run --cart ef1 --write --stages dump,write,writer,ingame
tests/cart/t16_session.sh run --cart megabyter --write --stages dump,write,writer
tests/cart/t16_session.sh run --cart gmod2 --write --stages dump,eeprom,write,writer,ingame
```

`--yes` skips the picture questions (they then count as OK, only for a rerun). Write stages need `dump` in the same
invocation (two byte-identical passes in that run), `--write` and the typed confirmation.

## What each stage does and where the result goes

| Stage | Does | Result goes to |
|---|---|---|
| pre | REST up, slot block present (SLOT_STATUS b31), no fault, ROM_WR_EN 0, request latency | §13.9 board note |
| boot | pins match the cart (CART_DETECT, P1), port armed, PC and raster move, screen text, Alex: picture OK | §9 P3.1, §13.9 |
| dump | all banks ROML (+ROMH) through the lens (`lens=cart`), `$DE00` per bank; pass 1 forward, pass 2 shuffled order (= bank switching by IO1 in any order); sha256 per bank and total; checks: not constant, banks differ, pass 1 = pass 2; saves `dump/*.bin`, `t16_backup_<cart>.crt`, sha256; compare with the baseline; reset into the game | §9 P3.2, P3.3, P3.4; put the sha256 into a table in §13.9 |
| soak | game runs (default 10 min on the EF, 1 min else; `--quick` 30 s), alive check every 5 s | §9 P3.3 |
| pause100 | 100 x JTAG machine pause/resume (random 40-300 ms holds), alive and fault checks, Alex: picture OK | §9 P3.6 |
| matrix | per hold 0.1 / 1 / 10 s x3: JTAG machine pause (option A, with a ROML read before/after the hold: must be identical), REST CPU pause, REST pause with GATE_CTRL b0 (freeze sessions), freeze menu open/close, lens reads while running. Option B (PARK_PHI2_RUN) is not built (§13.8): reported as not tested | §9 P5, §8.1 table, §12 Q3 |
| internal | EF/ROM only: preference Internal (runtime), reset: not armed, ext mask 0; the backup copy runs from the board's drive on `cart_slot` while the cart sits in the port; preference back | §9 P3.5 |
| write | flash carts: (1) ROM_WR_EN=0: autoselect sequence must NOT reach the chip; (2) ROM_WR_EN=1: autoselect ID ($01/$A4, MegaByter $C2/$58); (3) program 16 bytes per chip in a sector that is blank in both backups; (4) read back; (5) sector erase; (6) sector blank again; (7) full dump equals the backup. ROM_WR_EN is 1 only inside (2)/(3) and (5) | §9 P4 |
| eeprom | GMod2: 4 EEPROM words (default 1000-1003) read twice, pattern written, verified, originals written back, verified (bit-banged over REST with the machine held) | §9 P4 |
| writer | trex_crt on the device writes a test CRT (backup + pattern in a blank bank), then the backup CRT; the script sets Manual + Bus Sharing External first, ROM_WR_EN is 1 only while Alex runs the writer, and every round ends with a dump compared to the written image | §9 P4 |
| ingame | ROM_WR_EN=1 while Alex saves in-game (EF: EAPI writes the flash); dump shows the changed banks. Persistence: power-cycle, boot, rerun `--stages dump`, compare the sha256 with `after_save.sha256` | §9 P4 |

Alex's keys in the `writer` stage (twice per cart): file browser, select `t16_test_<cart>.crt` (round 1) or
`t16_backup_<cart>.crt` (round 2) in the board directory the script prints, "Write to Cartridge", confirm, wait for
the verify to finish, press Enter in the terminal. Round 2 always runs, also after a failed round 1.

Output: `~/.cache/trx64-t16/<cart>/` (`dump/` with the backups, `report-*.json`, `verified_backup.json`;
`T16_OUT` overrides). Keep the `dump/` folder: it is the only copy of the verified backup besides the board's drive.

## Safety rules the script enforces

- No write without `--write` and the typed confirmation; no write stage without two identical dumps in the same
  run; the first write of a stage is a probe that changes no bit; erase only in a sector that is `$FF` in the
  backup (no blank sector: the stage aborts, the cart is untouched; use the on-device writer instead).
- `SLOT_CTRL.ROM_WR_EN` is set only inside `WrEn()`, cleared and read back on every exit (exception, Ctrl-C,
  interpreter exit); the report ends with "ROM_WR_EN is 0".
- Any failed hard check aborts the session ("nothing further was written").

## Baseline: "equals the dump on Gideon's bitstream"

The cart's bytes do not depend on the reader, so any trusted earlier read is a valid reference. What exists:

| Cart | Reference found | Per-bank sha256 in repo |
|---|---|---|
| EF1 Timo's Castle | `~/Downloads/timoNew.crt`, `timo-rewwrite.crt`, `timo-ef-type32.crt` (2026-03-30): byte-identical payload, 64 banks ROML+ROMH | yes |
| MegaByter Lykia | `~/Downloads/lykianew.crt` (type 86) = `lykiaptv.crt` (type 19 header), 128 banks (2026-03-29) | yes |
| C64anabalt | `~/Downloads/canabalt.crt` (2026-03-30). `C64anabalt-header-fixed.crt` differs in 16127 of 16384 bytes (another release): not used | yes |
| GMod2 Sam's Journey | none (`~/Documents/C64/Sams_Journey_+6D_GP` is an empty folder; no GMod2 dump of his anywhere) | - |

Hashes: `tests/cart/data/baseline_candidates.json` (`t16_session.sh mkbaseline` regenerates it; the CRTs are not in
the repo). Provenance is **unverified**: they come from the March dumper/writer work on earlier hardware (Q9), not
from a documented run on Gideon's bitstream, and a game that saves to flash (Timo's Castle) may have changed since.
A match is strong evidence; a mismatch is only a WARN ("inconclusive"). `tests/cart/data/*.json` otherwise holds
pin captures only (SAMPLE, reset series), and `1541ultimate-tools/pinmap/runs` boundary-scan captures; no cart
dumps (its `flash-dump` is the C64U's own SPI flash). For GMod2 the evidence is self-consistency only: two
identical passes and the EEPROM round trip.

A run on Gideon's core, if the match fails or GMod2 needs a reference: `openFPGALoader -c usb-blaster --reset` boots
the **flash image, which is now our older core c8753262, not Gideon's**; it gives a second read of the same
bitstream family, not a baseline. Gideon's bitstream exists only in the backup dumps (`pinmap/runs/flash-dump/
pre-trx64-flash-2026-10-03.bin`, 0x000000-0x3FFFFF). Loading it into SRAM alone does not work: his boot ROM jumps
to the app at 0x220000, which is ours (longer than his 0x180000 limit, so nothing is copied; T15 §2 step 6).
A real Gideon run therefore needs flash 0x000000-0x3FFFFF restored from the dump (a flash write, Alex's decision,
procedure at the end of T15's "Flashed" section) and our image written back afterwards. Then, with only REST:

```
tests/cart/t16_session.sh run --cart ef1 --gideon        # plain DMA reads, Manual + External sharing, dump stage only
```

Gideon's firmware has no `trex_crt` and no lens; the plain DMA read reaching the cart is itself unproven on his
v3.15 core. Treat that run as optional.

## Open risks

1. **Bank switching by DMA depends on `$01`.** REST pokes to `$DE00` are bus cycles through the PLA; with the
   game's CPU port hiding I/O they miss the cart. DMA writes to `$00/$01` are not routed to the 6510 in M1 (Q8,
   §13.8). The dump stage detects this (banks must differ) and aborts; nothing is written. Fix would be Q8 or a
   dump right after reset.
2. **The trex_crt dumper/writer on our core.** The app refuses with Auto or External (`cart_check_dma_access`,
   Q5 skip not built); with Internal the port is unarmed at reset, so `cart_present` is not latched (§13.1/§13.2)
   and the PLA ignores the pins. The writer stage uses Manual + Bus Sharing External (passes both), unverified.
3. **The app never sets ROM_WR_EN** (no reference to `0x101A7000` in the app), so the on-device writer cannot
   reach the flash by itself; the script sets it via JTAG while Alex runs the writer. Anything that writes cart
   flash on this build needs it (also EAPI saves, hence the `ingame` stage).
4. **Writer rounds are destructive in between.** The EF1 writer erases the whole chip first; a failed round 2
   leaves a partial cart. Recovery is one more writer run with `t16_backup_<cart>.crt` (also in the cache dir).
   Write the cart only after both dump passes matched.
5. **Flash sequences are copied from the app's writer, not verified on these chips** (MegaByter in particular:
   expected autoselect `$C2/$58`). A wrong ID aborts at the probe, before any bit changes.
6. **GMod2:** Sam's Journey may have no blank flash sector (the `write` stage then aborts; `eeprom` is
   independent, order `dump,eeprom,write`). The EEPROM test rewrites words 1000-1003 and restores them; a save
   in-game is read back only through the on-device dumper (a full EEPROM read over REST is about 90k requests).
7. **"Alive" is a heuristic** (raster moves, PC takes two values, no fault); the picture questions are the real
   check. The REST DMA write path and the freeze-menu variant are untested with the real cart armed.
8. No FTP download of results (bulk RETR stalls on the board): the script only uploads CRTs and verifies by dump.
9. Power-on state: a crash with ROM_WR_EN set is cleared by the next bitstream load (reset value 0); if JTAG is
   gone while it is set, power-cycle.

**Decided (Alex, 2026-10-04): no comparison run on Gideon's core** (it would need his image written back into flash). Baselines are the March 2026 dumps where they exist, otherwise self-consistency.

## Results 2026-10-04 (bitstream main-latest, app 029da8ef)

| Cart | Dump | Soak / pause / matrix | Writes | Dump file on the C64U |
|---|---|---|---|---|
| C64anabalt (ROM 16K) | PASS, == March dump (`canabalt.crt`) | PASS | n/a | /USB1/t16_c64anabalt_20261004.crt |
| EF1 Timo's Castle | PASS via Gideon's DMA path, 64 banks x2 == March dump (`timoNew.crt`) | PASS (20x pause) | PASS: WR_EN=0 blocks, ID $01/$A4, program+readback, sector erase, full dump == backup; in-game EAPI highscore save (bank 16 ROMH, 100 B) PASS and persists over a power cycle | /USB1/t16_timos_castle_20261004.crt |
| MegaByter Lykia | PASS via lens with 3x majority, 128 banks == March dump (`lykianew.crt`) | runs (alive-check false negatives: idle loop at $403C, CAP_GEN advances) | NOT possible: game sets $01=$35 → ROML hidden; DMA write to $01 (Q8) not built | /USB1/t16_lykia_20261004.crt |
| GMod2 Sam's Journey | PASS via lens 3x majority, 2 identical passes (no reference) | not run | EEPROM: PASS (4 words read, pattern written/verified, originals restored). Flash: NOT possible (same $01 / Q8; autoselect read ROM bytes $09/$80) | /USB1/t16_sams_journey_20261004.crt |

Bugs found: lens REAL path drops single bytes to $00 (also under machine pause); continuous lens reads while running crashed C64anabalt; Q8 (DMA → 6510 port) not built; trex_crt dumper/writer do not work on our core; the app never sets ROM_WR_EN. Script fixes: alive check counts PCs only (the state bus holds $D012 as written, not the raster line); lens3 majority read path; 20 pauses via T16_PAUSE_N; matrix variants skippable; boot turns the speaker off. Completion of T16 (all of the above) in progress on t16-complete; the writer/flash-write part of this session is repeated after it.

## Final session (after `t16-complete`): everything in one go

What changed since the first session: Q8 (a DMA write of `$01` reaches the 6510 port for the hold), the DMA hold tick (DMA writes
to `$DE00` now reach MegaByter/GMod2-class latches), the freeze-session window (real cycles for a cartridge on the port),
the lens root cause (`trx_sys.v`), no write protection on the expansion port (T57 Decision 2026-10-05: ROM_WR_EN is always 1, the item
"Cartridge Port Writes" is gone), and trex_crt serving both cores (no unattended mode, Alex's decision: the tool is driven by his keys).

**Before:** the bitstream and the app of the branch under test in the package registry (`<branch>-latest`, CI run green;
`T16_VERSION=t16-complete-latest` or `--version`), `trex_crt.u2a` built (`apps/trex_crt/build.sh`, or `--app-u2a PATH`), the JTAG venv,
`tests/lorenz.py` not running. Nothing is written to the C64U's flash; the flash holds the old core, so every power-on starts that
core and `boot` loads the bitstream into SRAM.

**Commands** (`tests/cart/t16_session.sh`): `plan` prints the plan below; `all --write` runs it; `run --cart X --stages full --write`
runs one cart; `--stages` takes any list. `all --write` asks once (j/n); without `--write` the write stages are dropped.

| Cart | Stages (`full`) | Alex | About |
|---|---|---|---|
| rom16 C64anabalt | pre boot port dump app_up app_dump app_pref soak pause100 matrix internal | plug, a few yes/no | 45 min |
| ef1 Timo's Castle | pre boot gate port dump app_up app_dump app_pref write app_write app_err ingame powercycle persist soak pause100 matrix internal | plug, one in-game save (highscore), one power cycle | 75 min |
| megabyter Lykia | pre boot gate port dump app_up app_dump app_pref write app_write app_err ingame soak pause100 matrix | plug, an in-game save if Lykia has one | 65 min |
| gmod2 Sam's Journey | pre boot gate port dump app_up app_dump app_pref write eeprom app_write app_err ingame powercycle persist soak pause100 matrix | plug, one in-game save, one power cycle | 75 min |

About 4.3 h with the default soak times; `--quick` shortens soak and matrix (about 3.3 h). Alex's part per cart: swap the cart and power on, run trex_crt by its keys where the table says (the script prints the exact keys at each step)
(the script waits for his Enter), answer the picture questions (`--yes` counts them as OK), play and save once where listed.

What each new stage checks (all record PASS/FAIL/WARN with the spec reference, report in `~/.cache/trx64-t16/<cart>/`):

| Stage | Does | Spec |
|---|---|---|
| pre | slot block, no fault, the item "Cartridge Port Writes" is gone (REST answers no such item), ROM_WR_EN is 1 | T57 Decision 2026-10-05 |
| gate | ROM_WR_EN is 1 with the port armed; preference Internal + reset: not armed, bit still 1; everything back | T57 Decision 2026-10-05 |
| port | REST pause and whole-machine pause: `$01` as the game left it hides ROML from a plain DMA read; a DMA write of `$00=$2F,$01=$37` reaches the port (JTAG state bus), the plain read equals the lens read, the program's port is back after the resume, the game runs on | T16 Q8 |
| dump | pass 1 plain DMA reads (`$01` set by DMA), pass 2 lens in shuffled bank order: equal, bank switching changes data, equals the March baseline where one exists, backup CRT written. Any mismatch aborts (the old 3x majority is `--read-path lens3`) | T16 §13.11 |
| app_up / app_dump | trex_crt on the board (the script uploads `trex_crt.u2a`; Alex starts it: Menu button = freeze, F5 Tasks -> C64 Machine -> Dump Cartridge; the first time via file browser -> `trex_crt.u2a` -> Run App). He dumps to `t16_<cart>_app.crt` (F1, F3), then opens that file with 'Write to Cartridge' and presses F5 only (verify). The script checks from outside: no fault, gate and masks as before, the C64 reset and running, the dump file == ours when FTP can read it back; the verify and the cart name are Alex's yes/no. Every tool run is a freeze session, so this also covers T57 B3 | T57 B1, B3, §5.4, §5.5 |
| app_pref | preference Internal: port not armed; Alex starts the dumper, it must refuse with the 'port is not enabled' popup; nothing read or changed; Auto + reset: armed again | T57 B4, U3 |
| write | (first step with the gate cleared over JTAG, put back afterwards: the autoselect sequence must NOT reach the chip), ID probe, 16 bytes per chip in a blank sector, read back, sector erase, blank again, full dump equals the backup; `$00/$01` set by DMA first | T16 P4 |
| eeprom | GMod2: 4 words read twice, pattern, verify, originals back | T16 P4 |
| app_write | Alex writes a test CRT (backup + pattern in a blank sector) and then the backup CRT with trex_crt (file browser -> the CRT -> 'Write to Cartridge', F3, F5) (one pass, no policy any more): the script checks the gate is 1 before and after the app's write, a dump of the whole cart equals the written image; 'verified OK' is Alex's yes/no. GMod2: the app's own dump file is the image (the writer rewrites what is there, EEPROM included) | T57 B6/B7/B9 |
| app_err | a CRT without chips (Alex opens it with 'Write to Cartridge'): refused, the gate back, nothing erased. Accepts the validate-on-open popup (no writer screen; the cart check is sampled: first and last bank of each 8-bank sector) and the older refusal inside the writer screen (full dump); `--app-validates-open` makes the popup mandatory | T57 B10 |
| ingame | the gate is 1 for the running game; Alex saves; EF: banks differ from the backup; GMod2: the app's dump files before/after (read back over FTP when it works, else compared by hand) | T16 P4, T57 B8 |
| powercycle / persist | Alex power cycles (`--sram` when the board was booted with `load.sh`: bitstream and app are loaded again by the script; without it our core must come back by itself, else a clear message), the JTAG handle is reopened in every case; the saved state is still in the cartridge (dump / app dump sha256 == what `ingame` stored) | T57 B8 |

Safety (unchanged and added): no write without `--write` and the confirmation; a write stage needs two byte-identical dumps in this run;
erase only in a sector blank in the backup (not for the writer rounds, which erase the chip: the backup CRT is what comes back and
the stage ends only after a dump equals it); `WrEn` puts the gate back to the value the firmware left (1) (clearing it would take
away the in-game saves) and checks it at the end; Ctrl-C and exceptions restore it; the first mismatch aborts.

**Rules of the suite (2026-10-10 hardening, offline-proven by `selftest`, 67 cases):**
- The first dump of a cart writes `dump/t16_backup_<cart>.crt` once, read-only (O_EXCL). A later run keeps it (`== the FIRST backup` PASS) or, if the cart
  changed, writes `dump/t16_dump_<cart>-<time>.crt` and WARNs. Every restore (`writer` round 2, `app_write` BACKUP) writes the FIRST backup; a failed
  restore compare prints the restore recipe. A first-backup file that does not fit the cart is a FAIL and is left alone.
- No second compare against the same state: `check_cart` skips the dump when the previous one proved exactly this and nothing touched the cart.
- State bus (`trxdbg sb`): the CPU words are live only when halted or at a capture snap, never under REST's CPU pause alone. The suite reads them
  only halted (machine pause) or after two new capture snaps (`CAP_GEN`) since a hold; otherwise the stage fails with "state bus stale".
- Prompts are German, numbered steps, then Enter or j/n; nothing asks Alex to type a command. A stage exception, Ctrl-C (second one within 3 s ends
  the run) or SIGTERM is a FAIL line plus the report; write stages after a failure are skipped. Tracebacks go to `$T16_OUT/last_exception.txt`.
- `--dry-run` runs every stage against the simulated carts incl. failure paths (`t16_sim.py` faults: stalebus, oldapp, erasebad, norefuse, alexno,
  notours, jtaglost, rest_exc, kbint, kbint2, sigterm, plus the older ones).

Open for the session (not testable now): the lens root cause is confirmed only by the first long dump (`dump`, pass 1 plain vs pass 2
lens, 0 mismatches); EF3 (T57 Q7) is not part of the plan; the whole-machine pause variants of trex_crt are covered by `port` only.

EF3 is not part of the session and not board-tested in M1 (decided by Alex, 2026-10-04).

**No-cart smoke 2026-10-05 (bitstreams t16-complete-77446c9a and b8def9a3):** Q8 works over REST with the CPU pause alone (a DMA read of
`$A000` shows RAM after `writemem 0001=35`, BASIC ROM after `=37`) and with the whole-machine pause (`trxdbg freeze on`; `sb` shows
`$00`/`$01` as written, the program's port is back at the resume). **`trxdbg sb` shows the CPU words from a shadow that is refreshed only
while the machine is halted:** under the plain REST pause it keeps the pre-pause `$01`, so "sb shows 35 after the write" is no
evidence about the port in either direction; check with a DMA read of a ROM area, or `freeze on` first (`sb` now says so). The dump
stages use the DMA read, so they are not affected. After the write stage's sector test `t16_session.py` resets and waits for the program
(`reset_and_wait`) before the full dump (a game that crashed there read RAM and failed the compare for the wrong reason).
