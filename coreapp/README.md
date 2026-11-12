# The core-application blueprint (T93)

One application on the lean base (LVGL, Freeze only) that every core without Gideon's Ultimate application runs. This
build serves **atari800**, **nes**, **apple2**, **zx**, **cpc**, **zxnext**, **vc20**, **c264** and the A1200 (spec section 5.3; T89, T91, T92, T116, T83 and T90 section 5); the A500 Amiga keeps its
Gideon-base application, the C64 reference core (`trx64`) and `c64u` keep the full Ultimate app (the VC-20 and the C16/Plus/4 left it on 2026-10-22, Alex).

- `src/` the shared services: start and identity (`start.cc`), the hand-over (`host-app/software/hostapp/handover.*`), settings
  and the `.cfg` format (`settings.cc`, `cfgfile.cc`), the ROM manager (`romfind.cc`, `boot.cc`), the whitelist menu (`menutree.cc`,
  `menuctl.cc`), the video service and Freeze (`video.cc`, `freeze.cc`), the board glue (`hw.cc`, `fm_fs.cc`, `ui.cc`, `run.cc`) and
  `coreapp.cc` (the program).
- the service layer (T104.1): `ops.*` the registry of operations, state and events, `exec.*` the executor and the core lock, `service.*` a
  contributor's poll task, `ops_glue.*` how the descriptor, the menu and the boot code call by name. The descriptor holds operation names,
  not functions; each core registers its own as a built-in contributor.
- USB gamepads (T156): `hidpad.*` the HID report descriptor parser and report decoder, `pad.*` the pad service (two pads in plug order, the map per core button, the register pair per player, the capture of "Map buttons"), `pad_ops.cc` its operations and states. A descriptor's `pad` record (`PadSpec`) opts a core in (NES, SNES); the USB driver's hook is `usb_hid_set_pad_sink` (host-app/software/io/usb/usb_hid.*), registered in `run.cc`.
- the loader (T104.1 D10-D12): `libfmt.h` the .u2a format 3, `libapi.h` what a library sees and returns (`lib_register`), `libload.*` the
  loader (checks, placement, relocation, registration, rollback). Libraries are built with `app/lib/lib.mk` and packed by `tools/u2a_pack.py`;
  `tests/loader/run.sh` runs the loader against a model of the heap. Wired to the board: `libboard.cc` loads the libraries of `<root>/trx/lib` and of the `.cor` at every start (`lib_board_load`, called from `run.cc`).
- the drive library (T104.1 D15, built 2026-10-22): `app/lib/cbm1541/` (`d1541_gcr` the disk in memory, `d1541_wd` the 1581's controller, `d1541_drive` the drive, `d1541_fs` a .d64 / .d81 as a read only folder) is portable and pure; `cores/cbm1541.cc` is its contributor (`drive.*` operations, states `drive.drive_a.*`), `cores/cbm1541_desc.h` the descriptor pieces a core with the capability `drive_a` splices in (VC-20, C264, C64). `CoreIo` has the drive window (`drv_rd/drv_wr`, `ddr_abs_*`, `drv_buffer`). Built in, not loaded; spec T104.1 5.7.
- new disk images (T122): `newfile.*` where a new file goes and under what name (the folder the menu is on, a free name, never over a file; the 1541 drive uses it too), `newdisk.*` the builders for a blank Apple II DOS 3.3 `.dsk`, Amiga OFS `.adf`, Atari DOS 2.0S `.atr` and CPC `.dsk` and the operation `disk.create` (the 1541's own are in `app/lib/cbm1541`).
- the write-back service (T154): `writeback.*` what a game writes to its cartridge or battery RAM goes back to the user's file - Off / Ask / Auto, quiet time, flush before reset / eject / core change / power-off / menu open, NAME.tmp and NAME.bak; `writeback_crt.*` the .crt writer. A core registers its medium and reports changes with `wb_mark()`; the C64's flash and EEPROM path is `cores/c64_cart.cc`.
- `cores/` one translation unit per core: its descriptor and hooks, next to that core's register constants. Media code a core owns sits beside
  it: `tape_pulses.*` (a .tap/.tzx/.cdt as the pulse stream of the Spectrum's and the CPC's tape engine) and `tape_deck.*` (insert, play, blocks,
  eject: shared by `zx.cc` and `cpc.cc`), `zx_snap.*` (.sna/.z80), `cpc_disk.*` (the two DSK drives with write-back), `cbm_run.*` (Run PRG and
  Load PRG of the VIC-20 and the 264: the job around the PRG library, shared by `vc20.cc` and `c264.cc`).
- `tools/build_coreapp.sh` builds it (`--dist` copies the image to `dist/coreapps/atari800.app`, `nes.app`, `apple2.app`, `zx.app`, `cpc.app`, `zxnext.app`, `vc20.app` and `c264.app`: one image, D14, the core
  is chosen at run time from the ID block; the Amiga names stay untouched).
- `tests/coreapp/run.sh` runs the logic natively with a negative control per test.

Spec: `docs/specs/T93-core-app-blueprint.md`, what was decided while building is its section 5.
