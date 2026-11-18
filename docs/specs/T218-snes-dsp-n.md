# T218 — SNES: cartridges with a DSP-n coprocessor (DSP-1/1B, DSP-2, DSP-3, DSP-4) run, the firmware comes from the SD card

**Status:** DECIDED (2026-11-13). 2026-11-15 sync: BUILT and merged to main (c982d8392; `snes_sys.vhd:140` `USE_DSPn => '1'`, `snes.cc:83` setting, `ci/sims.txt:231` bench `snes_dsp`). Not recorded anywhere: the pipeline's area/timing result (S6), the app-test pass (S1-S4, S7), a board pass of S4 with real firmware; S4 for DSP-2/3/4 has no game owned. Board: S1 PASS 2026-11-15; S2 and S7 closed as done 2026-11-18 (shipped and in use, end of §5).
**Repo:** TRX64U-MultiCore — `cores/snes` (gateware, glue, sim), `coreapp/cores/snes*.{cc,h}` (loader), `tools/snes_gen.py`, manual 11.14 and 12.2.
**Number:** T218 (registry: the table in `TODO.md`).
**Origin:** GitHub issue #85 "Add DSP-n coprocessor to SNES core" (Super Mario Kart is refused with the chip message). Alex, 2026-11-13: "Gut. Todo+spec+bauen." (translated, accepting the proposal D1-D3 below).
**Depends on:** T142 (the SNES core; D8 "DSP-n out of v1", D9 "firmware never ships", S12).
**Milestone:** M3.

---

## §1 The problem

Read from the code (2026-11-13): `snes_sys.vhd` instantiates `DSP_LHRomMap` with `USE_DSPn => '0'`, so `DSPn.vhd` is not built; `snes_rom.cc` refused every header with a high nibble 0 and a low nibble above 2 (the DSP-n chipset bytes). Games with a DSP-1 (Super Mario Kart, Pilotwings, ...), DSP-2 (Dungeon Master), DSP-3 (SD Gundam GX), DSP-4 (Top Gear 3000) do not start.

- **P1 Chip in the machine:** the µPD77C25-class chip (`DSPn.vhd`, vendored) behind the cartridge mapper, with RAM for one firmware: program 2048 x 24, data ROM 1024 x 16, data RAM 2 x 2048 x 8 (the vendored file's).
- **P2 Firmware source:** the firmware never ships (T142 D9); the user supplies it, appended to the `.sfc` or as a file.
- **P3 Which chip:** the header says "a DSP" (chipset byte 0x03..0x06), not which one.
- **P4 Refusal without firmware:** a message that names the file(s) and the folder.
- **P5 Nothing else changes:** other chips stay refused (§4), non-DSP games unchanged.
- **P6 Area and timing:** block RAM is the tight resource (T142 §5: 71 of 75 tiles, LUT 21,769 = 66.8 %).

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1, P2 | Mario Kart (DSP-1) with `dsp1b.bin` as a separate file: the 8192 bytes go to the chip's port in order, `SNES_ROM_TYPE` = 0x80, the game runs | `tests/coreapp/test_snes.cc` (loader writes, order, rom_type); sim `snes_dsp` (the chip runs a firmware written through the register block, a 65816 program reads its DR `$1234`); the game itself: board | CI apptests, sims; board step: Mario Kart with `dsp1b.bin` |
| S2 | P2 | The same game with the firmware appended (file = power-of-two ROM size + 8192): the appended firmware is used, even when a file exists; the ROM size/mask is the file size minus 8192 | `test_snes.cc` (appended wins, ROM bytes in the window equal the image without the tail, mask 0x7FFFF) | CI apptests; board |
| S3 | P4 | No firmware anywhere: the game is refused before the machine is touched; the message names the chip, the file(s) (`dsp1b.bin or dsp1.bin`) and the folder | `test_snes.cc` (message text, `io.log` empty, program not loaded) | CI apptests |
| S4 | P3 | DSP-2/3/4 games pick `dsp2.bin`/`dsp3.bin`/`dsp4.bin` and mapper byte 0x90/0xA0/0xB0; a file of the wrong size is not taken | `test_snes_rom.cc` (title -> chip, rom_type; ExHiROM + DSP refused), `test_snes.cc` | CI apptests; board for each game (none owned: open) |
| S5 | P5 | Non-DSP games unchanged: `boot` bench real == ref; SA-1/Super FX/... refused as before | sims `snes_boot` etc.; `test_snes.cc` S12 | CI sims, apptests |
| S6 | P6 | The flat build routes and meets timing with the chip | `utilization.rpt` / `timing.rpt` of the pipeline run (lane large-snes) | CI vivado (after the coordinator's push) |
| S7 | P3 | The menu setting "DSP firmware" (Auto, DSP-1..DSP-4): a fixed choice replaces the title rule for every DSP cartridge (mapper byte and firmware file follow, the refusal names the chosen chip's file); non-DSP cartridges are unaffected | `test_snes.cc` | CI apptests; board |
| S8 | D6 | The same real DSP-1 firmware once as read (low byte first) and once with every word reversed (high byte first): both are recognised, the reversed one is turned back, the bytes written to the chip are identical; a file that is neither (random bytes) is written as it is; on the board Mario Kart runs with the reversed file | `test_snes.cc` with a firmware of our own in valid uPD7725 words (no real firmware in the tree); the real `dsp1b.bin` from the board checked once both ways outside the tree, result in §5 | apptests; board |

## §3 Decisions

- **D1 (Alex, 2026-11-13: "Gut. Todo+spec+bauen."):** build the DSP-n coprocessor into the SNES core. `snes_sys.vhd` sets `USE_DSPn => '1'`. No `.mif` from upstream; the firmware RAMs are block RAM written by the application.
- **D2 Firmware (decided with Alex):** never in the tree/`.cor`/package (T142 D9). The application takes it (a) appended to the `.sfc` - the file size (without a 512 byte copier header) is a power of two >= 32 KiB plus 8192; the last 8192 bytes are the firmware - or (b) from a separate file. **Precedence, Alex 2026-11-13 ("Ne passt"): appended first, else the separate file.** Separate file names: DSP-1 `dsp1b.bin`, else `dsp1.bin`; DSP-2 `dsp2.bin`; DSP-3 `dsp3.bin`; DSP-4 `dsp4.bin`. Folders, same order as the other cores' files (`mount_default` of the ZX Next): the core's own `roms/` folder (`<core folder>/roms/`), then `<root>/roms/snes/` of each root. Size exactly 8192.
- **D2a Format:** 8192 bytes = 2048 program words of 3 bytes, then 1024 data words of 2 bytes, **low byte first** (ares `load.cpp`: `readl(3)` for the program, `readl(2)` for the data). The register block (`snes_regs.v` 0x140 rewind, 0x141 the next byte) assembles the words and writes them into the chip's RAMs while the reset is held. **Not verified against a real firmware file** (none in the tree, none owned): the bench uses a firmware of our own (`tools/snes_dsp_fw.py`).
- **D2b Which chip:** by the header's title, as the known games: `DUNGEON MASTER` = DSP-2, `SD GUNDAM GX` = DSP-3, `TOP GEAR 3000` / `TG3000` = DSP-4, every other DSP cartridge = DSP-1. DSP-1 vs DSP-1B is only the firmware file (the chip is the same hardware; `REV` is unused). Mapper byte = `0x80 | (chip-1) << 4 | mapping` (LoROM 0, HiROM 1; `DSP_LHRomMap.vhd` decodes it; the address ranges are upstream's).
- **D3 Refusal:** only when no firmware is found; the text names the file(s) and the folder. SA-1, Super FX, Cx4, S-DD1, SPC7110, ST010/011, add-ons stay refused as in T142 S12.
- **D2c One firmware at a time:** the vendored `DSPn.vhd` holds all chips' firmware in an 8096 x 24 and a 7168 x 16 ROM; `tools/snes_gen.py` (counted edits, "T218") replaces them by a 2048 x 24 and a 1024 x 16 RAM with the write port `FW_*`. ST010/ST011 (VER bit 2) are not built (they would need a bigger data RAM and a 10 MHz clock).

- **D4 (Alex, 2026-11-13: "A setting in the menu for the firmware choice does make sense. Auto / 1.2.3.4", translated):** the SNES core setting `DSP firmware`: `Auto`, `DSP-1`, `DSP-2`, `DSP-3`, `DSP-4`, default `Auto` = the title rule of D2b, taken at the next reset (`WHEN_RESET`, op `snes.set_dspfw`). A fixed choice replaces the chip for every DSP cartridge (the mapper byte's chip bits and the firmware file follow); cartridges without a DSP are unaffected. Declared like the other SNES settings (in `coreapp/cores/snes.cc`; the core's `.toml` carries no settings).
- **D5 (review):** `DSP_LHRomMap`'s `DSPn` reset is gated by mapper bit 7 (`snes_gen.py`: `RST_N and MAP_CTRL(7) and ...`), because `MAP_DSP_SEL` is also true for plain mapper bytes; a plain cartridge never runs the chip.
- **D6 (Alex, 2026-11-16: "A", answer to GitHub #85, a tester: a big-endian DSP-n firmware hangs the game after the title and the beep):** the application recognises a firmware file whose words are high byte first (each 3-byte program word and each 2-byte data word reversed) and turns it around before it writes the chip; the game runs. Application only (`snes.cc`), no gateware. How it is recognised: from the structure of the uPD7725 words, proven on a real little-endian file and its reversed copy (S8).

## §4 Out (decided with Alex beforehand)

SA-1, Super FX, Cx4, S-DD1, SPC7110, ST010/ST011, BSX, Sufami, MSU-1 (T142 §4, unchanged). Save states. A DSP-n in ExHiROM (no such cartridge; refused).

## §5 Filled in while building (2026-11-13, branch t218-snes-dsp)

- **Gateware:** `tools/snes_gen.py` edits (counted): `DSPn: firmware write port`, `program ROM -> 2048 x 24 firmware RAM`, `data ROM -> 1024 x 16 firmware RAM`, `LHRomMap: firmware port` / `port map`, and one fix GHDL needs (`DATA_RAM_ADDR_A or x"40"`: the 8 bit literal or-ed to an 11 bit vector is refused by GHDL 6; written as `("000" & x"40")`, the same value). `gen/` regenerated on the builder (`SNES_CONTAINER=docker`, image `trx-ghdl6`).
- **Expected area [E, not measured]:** block RAM: program 2048 x 24 (3 x RAMB18), data ROM 1024 x 16 (1 x RAMB18), data RAM 2 x 2048 x 8 true dual port (2 x RAMB18) = 6 RAMB18 = **3 tiles**: 71 + 3 = **74 of 75**; one tile of slack, the pipeline decides (S6). LUT: the chip's ALU, multiplier (1 DSP slice class) and decode, order of 1.0-1.5 k LUT [E]. If the build does not fit, stop and report; no feature is dropped. The `USE_DSPn` hierarchy is also not removed from timing: the chip runs on the machine's enable (`DSP_CE`, 7.6 MHz).
- **Application:** `snes_rom.cc` (chip 3..6 accepted, title -> chip, rom_type), `snes.cc` (firmware search, appended before file, `write_dsp_fw`, message), `snes_regs.h` (0x140/0x141). The RISC-V .bss was full (overflow 9,408 bytes with a static firmware buffer): the firmware shares the 32 KiB scratch buffer with the image copy.
- **Bench `dsp` (sim, builder, Verilator in `trx64-sim-env:4`):** `SNES PASS dsp: real == ref` and `SNES PASS dsp: the chip ran the firmware written by the register block (DR = 1234)`: type 0x80, a firmware of our own (LDI DR, $1234; JP self) goes through registers 0x140/0x141, the 65816 program polls `$30:C000` bit 7 and reads `$30:8000` twice: `34 12`. `boot` still passes (real == ref). This proves the register stream, the byte order of our own writer, the chip map for a LoROM DSP-1 under 1 MiB, the read path. It does **not** prove any real firmware, HiROM or >= 1 MiB DSP maps, DSP-2/3/4, or a game.
- **Bench `dsp`, strengthened:** the firmware of our own loads four values into DR, each after the 65816 took the previous one: data word 5, data word 1023 (last), data word 0 (first, directly after the 6144 program bytes: a wrong split or a wrong word counter wrap gives another value), and `$BEEF` from program word 2046, reached by a jump from word 9, with word 2047 (the last program word, bytes 6141..6143) a JRQM loop. The bench expects `47 29 fd 4f 34 12 ef be` and passes; `boot` passes (real == ref, with the chip gated). The wrong-split failure is by construction (the values differ), not run as a mutant.
- **Defaults, unverified on board:** Q2 firmware byte order (low byte first, program then data) is per the ares/bsnes loader (`readl(3)` / `readl(2)`), not verified against a real file; Q3 the appended rule (power of two >= 32 KiB + 8192, copier header ignored); Q4 folders and names (the core's own `roms/`, then `<root>/roms/snes/`; `dsp1b.bin` before `dsp1.bin`). They stay as built unless Alex says otherwise.
- **Loader detail:** the separate firmware file is read with the image's handle closed (opened again afterwards), so the real file system is never asked for two files at once.
- **Not run (rules of this repo):** the app tests (CI only); Vivado (pipeline); the board.

## Open questions

- Title list for DSP-2/3/4 (D2b): other titles with those chips (alternate names, hacks) load the DSP-1 firmware under Auto; the setting (D4) is the way round it.
- Firmware byte order is per the ares/bsnes loader, unverified on board; the first real firmware file proves it (S1 board).
- 2026-11-15, board S1 PASS (Alex): Super Mario Kart (Europe) (.sfc 524288 bytes, no firmware appended) runs with `dsp1b.bin` from `/SD/cores/snes/roms/` (8192 bytes, program + data as one file), DSP firmware setting Auto; Alex: "klappt" ("works"), GitHub #85 closed by Alex. Build on the SD: snes.cor of c982d839.

## §5 D6 (2026-11-16)

- **Built:** `snes_dsp_fw_fix()` in `coreapp/cores/snes_rom.{h,cc}`, called at the top of `write_dsp_fw` (`snes.cc`), so both sources (appended, separate file) are covered. App only.
- **Rule (from `DSPn.vhd`, cited in the code):** a program word's type is bits 23:22 (l.115). Only JP words (type 10) have combinations the chip does not decode: branch field = bits 21:13 (l.126), decoded l.407-442 as 0 / 010 + condition code 0..12 or 15 / 10 + low six bits 0. OP, RT, LD decode every value, so they do not vote. A reading "holds" with >= 16 JP words and >= 7/8 of them valid; the file is turned (each 3-byte program word, each 2-byte data word) only when the high-byte-first reading holds and ours does not. Otherwise written unchanged.
- **Real files (all five, little-endian, `~/Desktop/backup/roms/snes/`, read outside the tree), valid/total JP words, ours -> high-first reading:** dsp1 273/273 -> 18/136; dsp1b 273/273 -> 16/130; dsp2 456/456 -> 82/227; dsp3 348/348 -> 23/151; dsp4 555/555 -> 45/270. Reversed copies show the mirror image. Detector on all ten: the five as they are are left unchanged, the five reversed are turned and equal the originals byte for byte.
- **Not turned:** 200 seeded random 8192-byte files (0 turned); the test patterns `i*7+1` and `i*13+5` of `test_snes.cc` (unchanged).
- **App test S8:** `test_snes.cc` builds a firmware of our own in valid words (JP/LD/OP, data words), low first as a file, reversed as a file, reversed appended: the stream to the chip is the same; random bytes are written as they are. Not run (CI); compiles (syntax check) and the RISC-V build was started.
- **Board:** reversed file on Mario Kart still to do.
- Board 2026-11-16 S8 PASS (Alex, package main-aaaee0d5): Super Mario Kart runs with `dsp1b.bin` reversed (high byte first) in `/SD/cores/snes/roms/`; the original (low byte first) was put back afterwards.
- Open (pre-push review of the release notes, 2026-11-16, from the code): the "DSP firmware" setting (F5) is read only when a game is run (`snes_run` -> `apply_dsp_choice`); a Reset does not run `snes_run` again, so the change takes effect on the next run from F3, not at the next reset as D4 and the toast "(takes effect at the next reset)" say. Fix the code or the toast and D4.
- **2026-11-18 - board steps closed as done (shipped and in use):** S2 (Mario Kart with the firmware appended), S7 (DSP firmware setting) - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
