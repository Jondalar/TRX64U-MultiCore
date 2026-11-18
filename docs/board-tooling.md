# Board tooling: load, flash, debug and restore the C64U

Written by the 1541ultimate session on 2026-10-13 and ingested here with T94; the board rules are in `CLAUDE.md`,
the restore points below. Checked on ingest: the JTAG library API this describes matches
`~/Development/1541ultimate-tools/tooling` as it is today, and `tools/load.sh` fetched from the generic
package `trx64-ultimate`. Since T171 (2026-10-31) this repository's CI writes only into its own generic package
`trx64u-multicore` (`MC_PKG` in `ci/pkg.env`): stage `package` and stage `update` (`ci/update.sh`) write `<branch>-<commit8>`
and `<branch>-latest` into it, and `tools/release_mc.sh` makes the Gitea releases (with a tag) from it. `trx64-ultimate`
(`FW_PKG`) is the firmware repository's; we only read it, for the files named by `release/imports/*.pin`
(`tools/pack_import_cor.sh`). `ci/lint.sh` guards that no write path names it. Versions that MultiCore published into
`trx64-ultimate` before T171 stay there (not deleted) and are no longer reachable through the default of `load.sh`. `tools/load.sh <local file>` needs no package at all.

## Board tooling

Board: C64U (= U64 Elite II mainboard), XC7A50T, USB-Blaster on the dock. Same build serves both boards.

### Rules
- Never power-cycle the board; Alex does. Never touch pins.
- Flash writes are released (Alex, 2026-10-13): run them, name the file, the address and the result afterwards. The
  auto-mode classifier blocks them. No flash dump before writing (test board, decided).
- SRAM loads, JTAG debug and partial loads are not flash writes and may be run.
- Deploy freeze (2026-10-13): nothing new onto the board until T94 delivers one host with all cores.
- **The FPGA loads from flash, Blaster or no Blaster** (Alex, 2026-10-13: "the FPGA does load from flash, that
  information is *wrong*"). An older note claimed the opposite - that a plugged Blaster keeps the C64U from
  configuring at power-on - and it came along with this document from the 1541ultimate session. It was already
  contradicted by two observations on 2026-10-08 (the board configured from flash with the Blaster plugged into the
  Mac) and is now corrected here. What is true: openFPGALoader with `--fpga-part` replaces the SRAM content, so
  after a flash write the running image is the loader's bridge until the next power cycle.

### JTAG Python environment
- Libraries `u64ii_jtag.py` (TAP, IR codes, `IR_CFG_IN`, `IR_CAPTURE_DONE`) and `u64ii_bscan.py` (`Blaster`, the
  FTDI MPSSE driver) live in `~/Development/1541ultimate-tools/tooling/` (Gitea `TREX/1541ultimate-tools`,
  branch `jtag/blaster-sample`). They are not in this repository.
- They need `pyftdi`, which is only in the venv `~/.cache/1541ultimate-tools/jtag-venv`
  (override: `U64II_JTAG_VENV`). `tooling/u64ii_bscan.sh` creates it on first use.
- `tools/trxdbg.sh` and `tools/vc20_load.sh` run through that venv. `tools/trxdbg.py` finds the
  tooling directory via `TRX_TOOLING` (default the path above). Running `trxdbg.py` with the system python fails
  with "pyftdi is missing".
- Only one JTAG user at a time: openFPGALoader, trxdbg and the load scripts all take the Blaster.

### Load into SRAM (no flash write)
```
tools/load.sh path/to/c64_basic.bit                   # local file
tools/load.sh c64_basic <package-version>             # fetched from the Gitea generic package
openFPGALoader -c usb-blaster -m file.bit             # what load.sh runs (retries 3x, checks "done 1")
```
`load.sh` fetches from the package `TREX/generic/trx64u-multicore/<version>/<design>.bit` (`MC_PKG`, T171).
After an SRAM load the boot ROM finds no matching app in flash and waits for one over JTAG:
```
tools/trxdbg.sh boot staged ultimate.app follow       # flash-format image via DDR2, boot ROM starts it
tools/trxdbg.sh riscv app.bin 0x30000 follow          # raw image (no 12-byte header)
```
After an app crash, reload the bitstream before loading the next app, otherwise every app crashes in its init.

### Partial bitstream (DFX core swap) on a running host
```
tools/trxdbg.sh decouple on                           # T79.1 section 7: cut the core off
~/.cache/1541ultimate-tools/jtag-venv/bin/python tools/t79/partial_load.py core_partial.bit
tools/trxdbg.sh decouple off
```
or through the host's ICAP (the RISC-V loads it from DDR2): `tools/t79/icap_load.py PARTIAL.bit` (strips the `.bit`
header, not bit-reversed). `partial_load.py` bit-reverses each byte because the Blaster shifts LSB first; about
2.6 s for 1.45 MB, DONE stays high.

### Flash (released to Claude; Alex power-cycles)
Layout: bitstream at 0x0, app at 0x220000 (`docs/c64u-flash-layout.md`). The app file of TRX64-Ultimate's package `trx64-ultimate` (`ultimate.app`)
already carries the 12-byte flash header, little-endian {dest 0x30000, length, run 0x30000}; write it as is.
```
! openFPGALoader -c usb-blaster --fpga-part xc7a50tfgg484 -f c64_basic.bit
! openFPGALoader -c usb-blaster --fpga-part xc7a50tfgg484 -f --offset 0x220000 --file-type bin ultimate.app
```
`--fpga-part` makes openFPGALoader load its spiOverJtag bridge first (the SRAM content is replaced). Then Alex
unplugs the Blaster's USB and power-cycles - the write is mine to run (Alex, 2026-10-13: "you are allowed to
flash! have been all along ... for 3 weeks", translated), the power cycle is his alone. The `!` prefix in the
block above is how a command is handed to Alex in the chat; it is not a rule about who may flash.

### Installing from Commodore firmware 1.1.0
Commodore's firmware 1.1.0 accepts a `.ue2` update file and not a `.cfw`. The CI's stage `update` (`ci/update.sh`,
from the parts of one commit's package) writes one update file per run, as `.cfw` and as `.ue2`. The monolith updater
`trx64_update_*` (T49, Ultimate application) is TRX64-Ultimate's and is not made here any more (T100 D15):

| File | Holds | For |
|---|---|---|
| `trx64host_update_<commit8>_<date>` | `null_full.bit` (the same host + the null core, T123) + the MultiCore boot application | this line: host only, no machine in the flash |

It lands in the package `trx64u-multicore/<branch>-<commit8>`; `tools/release_mc.sh` attaches the
`trx64host_update_*` pair to a release. On a machine that runs Commodore 1.1.0, start the `.ue2`:
1. Copy `trx64host_update_*.ue2` to a USB stick (any folder).
2. In the firmware's file browser pick the stick, the `.ue2`, **Run Update** (T49 section 6.1).
3. Read installed/package, YES, wait for "Update OK"; do not pull the stick or switch off meanwhile. The machine
   switches itself off; switch it on again. The update writes 0x000000 and 0x220000, so Commodore's firmware is gone
   from the flash afterwards (way back: its own `.ue2`, run from our menu the same way).

A machine that already runs our firmware takes the `.cfw` or the `.ue2` the same way. Cores (`.cor`, large `.cor`)
and the C64 firmwares (`.cfw` of Gideon 3.15, Commodore 1.1.0, TRX64-Ultimate) are not part of the update file: they
come from the stick and are added to the boot menu by hand.

### Installing from Commodore firmware 1.1.0
Commodore's firmware 1.1.0 accepts a `.ue2` update file and not a `.cfw`. The CI's stage `update` (`ci/update.sh`,
from the parts of one commit's package) writes one update file per run, as `.cfw` and as `.ue2`. The monolith updater
`trx64_update_*` (T49, Ultimate application) is TRX64-Ultimate's and is not made here any more (T100 D15):

| File | Holds | For |
|---|---|---|
| `trx64host_update_<commit8>_<date>` | `null_full.bit` (the same host + the null core, T123) + the MultiCore boot application | this line: host only, no machine in the flash |

It lands in the package `trx64u-multicore/<branch>-<commit8>`; `tools/release_mc.sh` attaches the
`trx64host_update_*` pair to a release. On a machine that runs Commodore 1.1.0, start the `.ue2`:
1. Copy `trx64host_update_*.ue2` to a USB stick (any folder).
2. In the firmware's file browser pick the stick, the `.ue2`, **Run Update** (the procedure of T49 section 6.1).
3. Read installed/package, YES, wait for "Update OK"; do not pull the stick or switch off meanwhile. The machine
   switches itself off; switch it on again. The update writes 0x000000 and 0x220000, so Commodore's firmware is gone
   from the flash afterwards (way back: its own `.ue2`, run from a firmware whose file browser offers Run Update).

Updating from the MultiCore host application itself is built since T152: the package also holds `trx64host_update_<commit8>.txu` (a tag's package: `trx64host_update_<x.y.z>.txu`)
(`tools/txu_pack.py`: the host `null_full.bit` and the host app of the run, SHA-256 per part, the host key and the host-app interface
in its header). On the stick, File Browser (F3), select it, **U**; two questions (Y, then RETURN after a second); only the parts that
differ are written, read back, then switch the power off and on. It never carries cores (a changed host key makes the partial cores
need a rebuild; the question counts them), and the vendors' firmware is T137's (R, P). `tests/hostapp/emu_txu.sh` runs it in ue2emu
(HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH build) and leaves the flash image behind. F8 "Protect Flash from updates" (default off, T152 D9) locks flash
0x000000-0x3FFFFF before a large core gets the machine (T152 D4/D6/D7). Cores (`.cor`, large `.cor`) and the C64
firmwares (`.cfw` of Gideon 3.15, Commodore 1.1.0, TRX64-Ultimate) are not part of the update file: they come from
the stick and are added to the boot menu by hand.

### Board network and files
- Board IP 192.0.2.189 (not the name `c64u`). FTP with curl, e.g.
  `curl -s ftp://192.0.2.189/USB1/cores/` and `curl -T file ftp://192.0.2.189/USB1/cores/`;
  Python ftplib does not reach it from macOS.
- Cores and settings: `/USB1/cores/*.cor`, `<name>.cfg` beside the `.cor` (T84 D11); `/Flash/boot.cfg`,
  `default=` is a 0-based index.
- Machine ROMs the applications load (never in the repo, never in a bundle): the C64 set in `/Flash/roms`,
  the C16/Plus-4 set in `/Flash/roms/c16` as `kernal.bin`, `basic.bin`, `3plus1lo.bin`, `3plus1hi.bin`
  (16,384 bytes each, PAL; the function ROM only matters for the Plus/4 model). The names and the search
  order are `roms_dirs`/`*_names` in the app's `software/io/c16/c16.cc`: `/flash/roms/c16` first, then
  `roms/c16` on USB and SD. Copied there on 2026-10-14 from the local VICE 3.7.1 `data/PLUS4` images
  (kernal 318004-05, basic 318006-01, 3plus1 317053-01/317054-01), read back and compared.
- Syslog: `python3 python/syslogd.py` in the 1541ultimate tree (UDP 514, fallback 64514).

### Restore points
- RC1 (drop-in, integ-m1): Gitea release `trx64-integ-m1-692f643d` in `TREX/TRX64-Ultimate` (`.cfw`/`.ue2` update
  files, zip, apps).
- Current flash (2026-10-26): run-406 host at 0x0 + host app bad8bb42 at 0x220000 (main-9417f353 package); before that (run 406, Amiga host + lean host app): generic package `trx64-ultimate/t85-amiga-787d8efc` (pre-T171, still in the old package)
  (`c64_basic.bit`, `c64_basic_pb_l2_partial.bit`, `ultimate.app`).

### Debug reference
`tools/trxdbg.sh` (docstring at the top of `tools/trxdbg.py` lists every command): RAM/DDR2 peek/poke, RISC-V console
and PC (`rvpc` with the ELF for names), boot ROM state, DDR2 calibration, pause/freeze, state bus, decouple.
ESP32 note: its GPIO8-11 are the FPGA JTAG lines; with the Blaster attached they are blocked for the ESP.
