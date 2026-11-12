# VICE cartridge testprogs: baseline of the icepi cartridge model (m15)

Run 2026-10-02 on the C64U with the TRX64 bitstream m15 and the Ultimate app, with
`tests/run_vice_testprogs.sh` (`tests/cart/vice_testprogs.py`). It records what
icepi's cartridge model (`cores/icepi/c64_cartridge.v`, mapped by
`designs/c64_basic/cart_slot.v`) gets right before the generic cartridge decoder
(TODO T33) replaces it. Tests: VICE `testprogs/` C64-carts, C64-freezer, C64-bankio,
C64-openio.

How a test runs: the CRT goes to the app with `POST /v1/runners:run_crt` (it lands in
/Temp, nothing is written back), or the machine is rebooted for the tests without a
cartridge; a test .prg is put into C64 RAM over JTAG at READY and `RUN` is typed into
the keyboard buffer. The verdict is the one the test's source defines: almost all VICE
cart tests write it to `$D7FF` and show it as the border colour (5 or 13 pass, 2 or 10
fail), which the script reads from the VIC shadow on the state bus. The VIC mapping
tests are compared pixel for pixel with VICE's reference screenshots
(`C64-carts/ultimax/references`) through the Ultimate's video stream. At the end the
machine is rebooted, so no cartridge stays loaded.

## Summary

| result | count |
| --- | --- |
| pass | 32 |
| FAIL | 10 |
| judged by eye | 7 |
| manual (freeze button) | 14 |
| unsupported type | 15 |
| total | 78 |

Every failure comes from three places: the Action Replay / Retro Replay model (6), I/O
reads every cartridge answers (1), and the C64 core outside the cartridge model
(REU arbitration 1, open I/O 1, VIC in Ultimax with the RR 1).

## Results

Cart type: the CRT hardware id (`/1` = subtype 1, Nordic Replay). `-`: no cartridge.

| test | cart type | result | notes |
| --- | --- | --- | --- |
| 8kgame | 0 | pass | memory map in all eight `$01` settings |
| 16kgame | 0 | pass | |
| romram | 32 | pass | bank switch takes effect on the next fetch |
| rr-reu | 36 | FAIL | REU not reachable beside the RR, see D5 |
| exos | 44 | unsupported | EXOS (external KERNAL) |
| mmcr | 38 | unsupported | MMC Replay |
| ocean | 5 | judged | shows `$DE00`/`$DF00` reads; all 0 (bank 0), see ocean-io. Its "CBM80" is ASCII, so it never autostarts; started with `SYS32777` |
| ocean-io | 5 | FAIL | `PEEK($DE00)` gives 3 after `POKE $DE00,3`: the register is readable, see D1 |
| ef-eapi | 32 | pass | the CRT carries only the "eapi" signature; VICE injects the driver, the script injects eapi-am29f040-14 (TRX_CRT_cli) the same way |
| ef-test | 32 | pass | |
| tamtest | 32 | pass | no broken RAM or colour RAM bits |
| supergames | 8 | unsupported | Super Games |
| pagefox-godot, -writeram-8000, -writeram-a000 | 53 | unsupported | Pagefox (3 tests) |
| artest | 1 | FAIL | AR mode `$22`, see D2 |
| rrtest, rr2test | 36 | pass | RR mode `$22`, plain and REU-compatible map |
| nptest | 9 | unsupported | Atomic/Nordic Power: `cart_slot.v` refuses the AR variant 2 |
| nrtest, nr2test | 36/1 | unsupported | Nordic Replay: neither app nor model reads the subtype |
| arramwrite | 1 | FAIL | AR mode `$22` writes, see D2 |
| rrramwrite, rr2ramwrite | 36 | pass | |
| npramwrite | 9 | unsupported | |
| nrramwrite, nr2ramwrite | 36/1 | unsupported | |
| arramwrite.prg (ardummy) | 1 | FAIL | as arramwrite |
| safearramwrite.prg (ardummy) | 1 | pass | |
| rrramwrite.prg, rr2ramwrite.prg (rrdummy) | 36 | pass | |
| test-ramdetect-ar, -ramsizedetect-ar, -ariotest1-ar, -ariotest2-ar, -arramtest2-ar (ardummy) | 1 | pass | 5 tests |
| test-ramdetect, -ramsizedetect, -ariotest1, -ariotest2 (rrdummy) | 36 | pass | 4 tests |
| test-arramtest2 (rrdummy) | 36 | FAIL | RR modes `$20`/`$21` write the cartridge RAM, see D3 |
| test-arramtest2-nr | 36/1 | unsupported | Nordic Replay |
| rr-writeonce | 36 | FAIL | `$DE01` is not write-once, see D4 |
| rr-allowbank0 | 36 | FAIL | AllowBank=0 also stops the `$8000` RAM banking, see D4 |
| rr-allowbank1 | 36 | pass | |
| rr-nofreeze, rr-freeze | 36 | manual | need the freeze button, see D6 |
| ultimax | 0 | judged | CPU map as the readme's table; the glyphs (charset in cartridge ROM) flicker, see D7 |
| ultimax-ef | 32 | judged | as the readme's EasyFlash table |
| ultimax-rr | 36 | judged | as the readme's Retro Replay table (bank 0 at `$8000` and `$E000`) |
| ultimax-maxbasic | 61 | judged | boots to BASIC: the app reads CRT 61 (VICE: MAX Basic) as C64MegaCart, see D8 |
| ultimax-multimax | 70 | unsupported | MultiMAX |
| ultimax-vic-ef, 8k-vic-ef, 16k-vic-ef | 32 | pass | 0 of 64000 pixels differ from VICE |
| ultimax-vic-rr | 36 | FAIL | 11 % of the pixels differ, see D5 |
| 8k-vic-rr, 16k-vic-rr | 36 | pass | 0 pixels differ |
| ultimax-vic | 0 | judged | as the readme's table, one flickering cell (D7); no VICE reference |
| ultimax-vic-maxbasic | 61 | judged | boots to BASIC, as ultimax-maxbasic |
| ultimax-vic-multimax | 70 | unsupported | |
| freeze-test, -cia1, -cia2, -raster, -combined, freezeblock, batest, ciairq1t1, ciairq1t2, ciairq2t1, ciairq2t2 | - | manual | 11 tests, all need the freeze button (D6) |
| bankio | - | pass | |
| gauntlet | - | FAIL | open I/O does not return what the VIC fetched, see D9 |
| de00all | - | pass | the CPU runs from open I/O at `$DE00` (10 of 10 samples) |
| de00int | - | pass | IRQ calls into `$DE00` every frame; judged by the main loop and the black border staying |
| dadb | - | pass | the CPU runs from the colour RAM nibbles (10 of 10 samples) |
| trivial | - | manual | only shows open-bus values |

## Deviations

**D1 Every cartridge answers I/O reads with its registers or RAM.** `c64_cartridge.v:295-297`
selects `io_dout` (`reg_0` / `reg_1`) for every `$DExx` read of every type, and
`:431-435` puts it ahead of everything else; `:305` maps `$DF00-$DFFF` to cartridge
RAM for every type but AR/RR ("required for EasyFlash"). An Ocean cartridge has no
readable register and no RAM there: ocean-io reads back its bank number, and any
program that probes `$DE00`/`$DF00` for open bus (cartridge checks, gauntlet-style)
sees the model instead. For a normal cartridge (id 0) the `default` write branch
(`:274-280`) even latches `$DE00` writes into `reg_0`.

**D2 The Action Replay is the Retro Replay.** `c64_cartridge.v:112` enables one model
for ids 1 and 36, and `c64_action_replay` (`:457-492`) gets no type: AR and RR share the
`$DE01` register, the RAM banking and the PLA. AR mode `$22` reads C64 RAM at `$8000`
where VICE's AR (and the real AR, through bus contention) shows cartridge RAM OR C64
RAM (artest, row 13 `hhhh`), and its writes there follow the RR rules (arramwrite,
arramwrite.prg). The "safe" variant, which avoids the contention, passes.

**D3 RR writes in 8K/16K RAM mode reach the cartridge RAM.** test-arramtest2 runs
modes `$20`-`$23`; its result block at `$0EB2` reads `01 01 01 01 / 00 00 01 01 / 00 00 01 01`:
in modes `$20` and `$21` the data written to `$8000` landed in the cartridge RAM (AR IO
RAM and AR RAM "passed"), which the test expects only of AR and Nordic Replay, not of
the Retro Replay. The model writes the cartridge RAM whenever the PLA selects it
(`ram_we = we`, `c64_cartridge.v:675-676`), the AR5 behaviour.

**D4 `$DE01` (RR extended control).** It is rewritten on every write
(`c64_cartridge.v:636-642`, no lock after the first write outside flash mode):
rr-writeonce sees the second write take effect. AllowBank is applied to every RAM
access (`ram_addr`, `:677`), not only to the RAM in the I/O window: with AllowBank=0
the `$8000` RAM banks all alias bank 0 (rr-allowbank0 shows `3BANK3` in all four rows,
the test expects `-` in banks 1-3).

**D5 The VIC and the REU next to the Retro Replay.** ultimax-vic-rr: the model drives
GAME/EXROM statically (`c64_cartridge.v:689-690`), so the VIC gets the Ultimax map; VICE's
RR reference shows the character ROM at `$1000` and `$9000`, the board shows RAM there
(the readme lists a second RR variant and the Nordic Replay with RAM at `$1000`, so the
hardware is not uniform). 8k-vic-rr and 16k-vic-rr match VICE exactly. rr-reu: the RR
in REU-compatible mode (`$DE01` bit 6) leaves `$DF00` free, but the bus arbiter gives
I/O2 to the cartridge whenever GAME or EXROM is asserted
(`cores/icepi/c64_bus_arbiter.v:685-686`), so the REU's registers never answer and
the transfer check fails (border 2). Arbiter, not cartridge model, but T33 has to
carry the cartridge's own I/O2 decode out to it.

**D6 No freeze button.** `designs/c64_basic/cart_slot.v:195` ties the model's `freeze`
input to 0; the AR/RR freeze logic in the model is unreachable. All C64-freezer tests,
rr-nofreeze and rr-freeze need it (and a person to press it).

**D7 Flickering glyphs from cartridge ROM (cart_slot cache, not the model).** The
VIC reads cartridge ROM through cart_slot's 4 KB cache, and only the CPU is held
for a line that is not in yet (`wait_cpu = cpu_half && ...`, `cart_slot.v:294`); a VIC
fetch in phi1 takes whatever the slot holds. In
ultimax.crt the charset sits in ROMH while the CPU runs from ROML and ROMH; three
consecutive frames show different broken glyphs in the last rows. ultimax-vic shows
one broken cell; the EasyFlash VIC tests happened to be clean.

**D8 CRT id 61.** VICE's `CARTRIDGE_MAX_BASIC` is 61; the app's table
(`software/ultimate-app/software/io/c64/c64_crt.cc:73`) maps 61 to the provisional
C64MegaCart, so a MAX Basic CRT starts as C64MegaCart (id 88 in the model, 8K mode)
and the C64 boots to BASIC. MAX Basic itself is not in the model either.

**D9 Open I/O (C64 core).** gauntlet expects reads of unmapped `$DE00-$DEFF` to show
the byte the VIC fetched in the cycle before; the arbiter returns its last internal bus
value (`c64_bus_arbiter.v:698`, "read last bus state"), and the Warpspeed-signature
check fails. de00all and dadb (code running from open I/O and colour RAM) pass.

## Not covered

- carttest.d64 (exos and mmcr from a disk), aracidtest.prg (the combined version of the
  separate acid tests), mmcrplugin.prg: duplicates of tests above or need MMC Replay.
- The rr-freeze dumps are references for the manual freeze test.
