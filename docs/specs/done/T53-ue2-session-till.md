# T53 session at Till's: our core on an Ultimate 64 Elite II (XC7A100T)

**Status:** retired 2026-11-01 (Alex: "Till had no 100T board. Can this go, please?"): the session scripts and their selftest are removed; this page stays as history. Prepared 2026-10-04 for 2026-10-05 17:00 CEST, not run on the board. **Task:** TODO T53 (build is done, CI run 362; this is its
first board test), T35 (tape port), T38/T50 (keyboard on P7). **Scripts:** `tools/ue2_session.sh` (`ue2_session.py`, `ue2_checks.py`,
`ue2_trxdbg.py`, fake board `ue2_fake.py`). **Offline test:** `tests/ue2/ue2_selftest.sh` (all pass; no hardware).
Board facts used: `T53-ue2-build.md` (same pins as the C64U, only the FPGA differs), `T15-flash-boot.md` (boot ROM takes the app at 0x3C0000 on a
type-3 FPGA, only when the flash bitstream's build stamp equals the running one), the 100T table of `w25q_flash.cc`.

## 1. What can be tested tomorrow and what does not exist

| Item | State | At Till's |
|---|---|---|
| Bitstream configures on a UE2, boots our app, REST (`trx64` block, caps) | built (CI, WNS +0.133 ns), **never on a board** | `sram`, `checks identity caps screen` |
| Classic C64 keyboard on **P7** (T38 matrix) | built (`c64u_keyboard.v` scans a PCA9555 over I2C on all three buses, 0x21); P7 on a UE2 untested | `checks keyboard`: all 64 matrix keys, T+R+X chord bits, RUN/STOP+RESTORE |
| **T50 unlock (T+R+X at power-on)** | **not built**: no `flash_unlock.v`, the T44 gate has no register 0x17. Its source (Bling UART) does not exist on a UE2; T50 Q5: a classic keyboard on P7 gets no unlock | cannot be tested. The keyboard check proves the three keys arrive as matrix bits 22/17/23, the precondition. A polled P7 matrix has no "keys held at power-on" problem (the Bling question E0 does not arise) |
| T49 update package / on-device updater | not built | the flash is written from the Mac over JTAG (`flash`) |
| **Datasette / tape port (T35)** | **not implemented**: `top.v` header "Tied off: tape"; `c64_core.v` leaves `tape_cass_sense_n`/`tape_cass_motor_n` open, `tape_play_toggle` 0, no tape data to CIA FLAG; the `C2N_*` pins exist only in `constraints/c64u.xdc` | `checks tape` measures that (PLAY sense, motor), expects "no". A real tape LOAD works only on **Gideon's stock firmware**: do it first (section 3, step 0) as the baseline |
| Paddles / 1351 mouse (T47) | not built (POT reads 0xFF) | not tested. Joystick (T38, built) is a manual check |
| HDMI picture, audio (HDMI, analog), NTSC/PAL (T40) | built, board-tested on the C64U | `checks video modes` (you answer yes/no) |
| CRT / D64 from USB | the app's own file handling | `checks usb --usb /Usb0/x.d64 /Usb0/y.crt` |
| trex_crt dump/write (T57), cartridge port (T16) | not part of this session | - |
| Lorenz subset (12 tests, about 2 min) | JTAG only | `checks run lorenz`; never the full suite |
| Flash boot of our core on a 100T (boot ROM type 3, 0x3C0000, stamp gate) | built, **never executed** on a type-3 board | after `flash`: power cycle |

## 2. Package (on the Mac, nothing needs the NAS on site; the VPN is only a fallback for `fetch`)

`~/trx64-till/pkg` holds the 100T bitstream and the matching app, sha256-checked against the CI manifests. **Today it is the t53-ue2 run
(949cf77d, 100T WNS +0.133 ns, app d1784fe0): older than main, without the lens/gate work merged since.** Refresh it tomorrow from main:

1. `tea api --login nas "/repos/TREX/TRX64-Ultimate/actions/runs?status=waiting" | grep -c head_sha` must print 0 (the queue keeps one
   waiting run and a new dispatch replaces it).
2. `tea api --login nas -X POST /repos/TREX/TRX64-Ultimate/actions/workflows/pipeline.yml/dispatches -d '{"ref":"main","inputs":{"ue2":"true"}}'`
   (the 100T stage alone took 35 min; with the earlier stages and the queue plan for 1-2 h: dispatch by about 13:00).
3. `tools/ue2_session.sh fetch main-latest` (or `main-<8 digit sha>`), then `tools/ue2_session.sh doctor`. If main has no 100T files the
   fetch says so and prints the two commands above. The old package stays in `~/trx64-till/pkg-t53-ue2-latest`.

`doctor` checks: openFPGALoader 1.1.1, the `spiOverJtag_xc7a100tfgg484` bridge, pyftdi, the USB-Blaster driver of the tools fork, the package,
the Lorenz disks. Rehearsal with no hardware: `tools/ue2_session.sh --dry-run backup`, `--dry-run flash`, `--dry-run restore`, `--dry-run sram`,
`--dry-run checks run` (a fake board in `~/trx64-till-dryrun`).

Bring: the MacBook with charger, the USB-Blaster **and the cable that fits the UE2's JTAG header** (not verified here), a USB stick with a few
CRT/D64, a joystick, a tape with a known-good program, the classic keyboard (P7).

## 3. Checklist

Rules: the flash is only written by `flash` and `restore`. Plug the Blaster's USB **after** the UE2 is on and unplug it **before** a power cycle
(JTAG active at power-on can stop the FPGA configuration). Nothing here uses `--unprotect-flash`; a write-protected flash makes the script stop.
Times are estimates; none is measured on the UE2 (openFPGALoader prints a progress bar; if the first 4 MiB read takes more than 3 minutes,
the 16 MiB reads cost more than the estimate).

| # | Alex does | Script / command | Time |
|---|---|---|---|
| 0 | Power on the UE2 with **Gideon's stock firmware**, P7 keyboard + Datasette + HDMI connected. Write down the firmware version (menu). Type `LOAD` RETURN, press PLAY: does the tape load? Does the keyboard type? NTSC/PAL in the stock menu. Note the IP (`curl http://IP/v1/info`) | - | 15 min |
| 1 | Plug the Blaster (UE2 on) | `tools/ue2_session.sh detect` : IDCODE must be the XC7A100T alone in the chain, flash EF 40/70 18; otherwise it stops before touching anything | 1 min |
| 2 | wait | `tools/ue2_session.sh backup --label till-ue2` : whole flash read twice, compared, sha256, stored in `~/trx64-till/backup-<date>-till-ue2/` (`flash.bin`, `backup.json`). Different reads: stops, no backup. **Copy the folder to a second place now** (NAS over the VPN, a stick) | 8-12 min |
| 3 | wait | `tools/ue2_session.sh sram` : our 100T bitstream into SRAM (flash untouched), our app staged via the boot ROM, finds the IP (app console over JTAG, then the UDP ident broadcast, else it asks once), waits for REST, checks `trx64` block, speaker off | 3 min |
| 4 | answer the questions; press keys when asked | `tools/ue2_session.sh checks run --usb /Usb0/<file>.d64 /Usb0/<file>.crt` (paths as the Ultimate's file browser shows them; without `--usb` that check is skipped). Results: `~/trx64-till/results/checks-<time>.txt/.json`. The default list ends with `tape`: have the Datasette plugged in and a tape inserted by then | 20-30 min |
| 5 | optional | `tools/ue2_session.sh checks run lorenz` | 3 min |
| 6 | **Decision gate**: flash only if identity, screen and keyboard passed and Till agrees | - | |
| 7 | type `FLASH` once | `tools/ue2_session.sh flash` : re-reads both ranges and refuses unless the chip equals the backup there (also catches the wrong UE2), writes the app at 0x3C0000, then the bitstream at 0x0 (both padded to 4 KiB, nothing else), reads each back, reads the **whole chip** and requires every other byte to equal the backup, reconfigures from flash | 12-20 min |
| 8 | **Unplug the Blaster USB**, power off, power on, wait 30 s | `tools/ue2_session.sh checks run identity screen keyboard` : our core came up from flash by itself (boot ROM, type 3, 0x3C0000) | 5 min |
| 9 | Go back to Gideon's firmware when finished (the board is Till's) | `tools/ue2_session.sh restore` (type `RESTORE`), power cycle without the Blaster, stock firmware boots. Skip only if Till wants ours left on | 10-15 min |

### If something goes wrong

- **No JTAG answer / wrong IDCODE:** nothing was touched. Cable, power, Blaster plugged after power-on; `--dry-run` shows what the script expects.
- **Backup reads differ:** both reads are kept as `FAILED-*`, no `backup.json`; `flash` refuses without one. Reseat the cable, rerun.
- **`sram`: app does not boot or no REST:** `sram --host <ip>` if only the discovery failed; a console line `GURU`/`ASSERTION` is reported, then run `sram` again (reload the bitstream before the app, as on the C64U).
- **`flash` stops:** it says whether it failed before writing (nothing changed) or after (verify failed): then `restore` at once. `restore` reads the whole chip, rewrites only sectors that differ from the backup (`--full` rewrites everything), then verifies the **whole chip against the backup**: only then it says RESTORE OK.
- **Board dark after flash / after restore:** JTAG still works (it does not depend on the flash): plug the Blaster with the UE2 on (FPGA unconfigured), `detect`, `restore`; or `sram` to run ours without the flash.
- **Write protection (`block protection is set`):** the script stops. Do not force anything; tell me.
- **Wrong chip:** `flash`/`restore` compare the chip with the backup first. Two UE2s at Till's: one backup per board (`--label`), pass `--backup <dir>`.

## 4. What the scripts guarantee (code, selftest-checked against the fake)

- Flash access only through openFPGALoader's spiOverJtag bridge with `--skip-reset` (no firmware runs between steps). `ofl()` refuses to start with
  `--unprotect-flash`, `--protect-flash`, `--bulk-erase`, quad switches. openFPGALoader 1.1.1 writes the status register only on those
  (`spiFlash.cpp`: `disable_protection()` only with `--unprotect-flash`); `-f` erases only the 4 KiB/64 KiB blocks of the written range.
- `flash` writes exactly: bitstream (raw configuration stream cut from the `.bit`, IDCODE checked 0x?3631093, build stamp printed) at 0x000000-0x3BFFFF
  window, app (header checked) at 0x3C0000-0x57FFFF window. The FAT drive (0x580000) and the config (0xFE8000) are never written.
- `backup`/`flash`/`restore` end with a full-chip comparison; `backup.json` is written only after two identical reads.
- Typing `FLASH` / `RESTORE` is required (stdin, exact word).

## 5. Which result goes where

| Result | Goes into |
|---|---|
| detect: IDCODE/JEDEC; `sram` came up, `identity`, `caps`, `screen`, `video` | T53 §6 (replace "never on a board"), TODO row T53 |
| flash and power-cycle boot (step 7/8), image sha256, durations | T53 §7, T15 "Flashed" section (first type-3 flash boot) |
| `keyboard` (64 keys, chord bits, RUN/STOP+RESTORE) | T38 row (P7), T50 §2.4 / Q5 (chord on P7), TODO T53 |
| `modes` | T40 board B2/B3 |
| `usb` | TODO T53 |
| `tape` + stock-firmware tape baseline (step 0) | T35 and TODO row T35: "not built" is a result; the baseline says what the core must reproduce |
| Lorenz subset | T10/T53 |
| backup sha256 + where it is stored | `docs/c64u-flash-layout.md` (a UE2 section) |

## 6. Found while preparing (not fixed here)

1. `tools/trxdbg.py:121-122` refuses every IDCODE except the XC7A50T, so trxdbg, `tests/lorenz.py` and the other test scripts do not run on a
   UE2. `tools/ue2_trxdbg.py` lets the 100T through without touching the shared file (`ue2_trxdbg.py --run tests/lorenz.py ...` for scripts that
   import trxdbg). A two-line fix in `trxdbg.py` (accept 0x03631093 as well) would make the wrapper unnecessary. Also `boot check --dump` compares
   against 0x220000, the 50T slot; on the 100T it is 0x3C0000.
2. The T44 gate protects below 0x400000 (the 50T layout: bitstream 0-0x220000, app 0x220000-0x400000). On the 100T the app slot is
   0x3C0000-0x580000 and our app ends at about 0x545000 (1.6 MB): the part above 0x400000 is **not** covered by the gate. Matters when T49/T50 are
   built for type 3 (the unlock window and the gate limit must follow the FPGA type).
3. The boot ROM's app size limit is 0x1DFFF4 (the 50T partition). On the 100T the partition is 0x1C0000: the app (0x185784 in the package
   today) fits with about 0x3A000 to spare, but an app above 0x1BFFF4 would be accepted and run into the FAT drive at 0x580000. `flash` refuses
   such an app.
4. Our app on a UE2 will read Gideon's config (0xFE8000) and the FAT drive (0x580000) of the stock firmware once flashed; settings saves write
   there. The backup covers it; `restore` brings it back.

**Decided (Alex, 2026-10-04):** Till agrees to a flash of his UE2 with him sitting next to Alex. Order: SRAM first; flash only after identity, picture and keyboard pass. The UE2 board is the C64U board with the XC7A100T, so the JTAG header and cable are the same.
