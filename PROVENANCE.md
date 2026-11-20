# Provenance

Every third-party or derived building block of the TRX64U MultiCore tree: where it comes from, who wrote it, under
which licence, and what we changed. State: 2026-10-24.

Every licence below is taken from a licence file or a file header named in the row. "No header" means: the file
carries no licence text; such a file is listed by location and origin, not as licensed. Per-subtree records that are
more detailed than this page are linked, not repeated:

| Record | Covers |
|---|---|
| `vendor/icepi/ORIGIN.md` | the C64 gateware tree: commits it was taken at, the former patches, later edits |
| `host-app/PROVENANCE.md` | every file of `host-app/software` that is a copy of the Ultimate application, with sha256 and changes |
| `cores/c64u/gideon/PROVENANCE.md` | Gideon's VHDL as copied into the c64u core, file by file |
| `cores/<name>/rtl/NOTICE` | one per vendored core (amiga, amiga1200, apple2, atari800, c264, cpc, neogeo, nes, snes, ti99, vc20, zx, zxnext): upstream, commit, header terms, what was left out, what was changed |
| `cores/<name>/rtl/COPYING`, `LICENSE` | the licence texts the upstream trees ship (cpc, nes, snes, zx, zxnext: GPL; c264: GPL-3; amiga `src/fx68k`: GPL-3) |
| `LICENSING.md`, `LICENSE` | the licence of our own code |

## 1. What is built, and where licences meet

The design is one host (layer 1) and swappable cores (layer 2) on the same board (XC7A50T). The licences that meet are
those of one bitstream, so they are listed per bitstream, not for the whole tree (section 12):

- **Host image (`.cfw`)**: a full bitstream with the host and the null core (`cores/null`, ours, no machine in it), plus
  the host application. The flash holds the host and no machine.
- **Core (`.cor`)**: the partial bitstream of one core (`cores/<name>/`), the core application, an icon.
- **Applications**: the host application (`host-app/`), the core application (`coreapp/`, `app/`), and Gideon
  Zweijtzer's Ultimate application that the `trx64` and `c64u` cores run. The last is not built from this repository
  (T100 D15); what it shares with ours (the software that `host-app/` and `coreapp/` compile) is our own copy in `vendor/gideon`.

Vivado builds run from `host/` as the working directory. `host/build.tcl` builds the host (it is routed together with
the reference core `trx64`), `host/core.tcl` builds every other core against the cached host result.

## 2. Rights holders

This project combines third-party gateware and software under several licences (GPL-3, CERN-OHL-S-2.0, LGPL, MIT,
Apache-2.0, and some files with restricted terms or none stated; the sections below list them per unit). Every origin
and licence is recorded as the files state it. Rights holders who see a problem: please contact us, and we remove the
part concerned.

Our own code is GPL-3.0-or-later, its top levels included. Dag Lem's reDIP SID/CIA stays CERN-OHL-S-2.0, our changes to
it too. In his email of 2026-10-06 he allows the use as long as the modified work itself stays open under that licence;
he suggested a CERN-OHL-S-2.0 top level, the top levels here are GPL-3.0-or-later and mark the CERN-OHL-S parts as
Available Components in the sense of CERN-OHL-S-2.0 section 1.2 (`LICENSING.md`).

## 3. The host (layer 1): `host/`, `cores/common/`

Built by `host/build.tcl`, `host/gideon_vhdl.tcl`, `tools/vivado_lib.tcl`. The host reads `vendor/icepi/hdmi` (the HDMI
encoder, section 4), `vendor/gideon` (section 5) and the files below. Own code: GPL-3.0-or-later; only some files carry
an SPDX header.

| Block | Path | Origin / what it follows | Licence |
|---|---|---|---|
| Top level and board glue | `host/top.v`, `vic_scaler.v`, `pal_ram.v`, `cdce_i2c.v`, `mode_ctl.v`, `c64u_keyboard.v`, `bling_keyboard.v`, `bling_io.v`, `case_leds.v`, `jtag_dbg.v`, `rv_dbg.v`, `i2s_out.v`, `con_ring.v`, `udp_stream.v`, `udp_audio.v`, `stream_mux.v`, `pktport_host.v`, `reu_ddr_map.v`, `app_i2c.v`, `app_input.v`, `usb_phy.v`, `hdmi_gtp.v`, `tmds.v`, `dfx_decouple.v`, `overlay.v`, `pix_layer.v`, `c64_text.v`, `cart_fill.v`, `icap_test/`, `cores/common/iec_bus.v` | Ours. Several follow the behaviour of Gideon's software or VHDL and say so in their headers (`i2s_out.v`, `usb_phy.v`, `app_i2c.v`, `c64u_keyboard.v`, `bling_keyboard.v`, `reu_ddr_map.v`; `overlay.v` follows the register map and drawing of his char_generator); no Gideon source is copied into them as far as the headers say | GPL-3.0-or-later |
| DDR2 controller and PHY | `host/ddr2_lean.v`, `ddr2_ctrl.v`, `ddr2_port.v` (incl. `mig_arb`, `cpu_mem_bridge`), `cal_seq.v`, `ddr2_pins.xdc`; `designs/ddr2_test/` | Ours. `cal_seq.v` and `ddr2_lean.v` follow the approach of Gideon's `ddr2_calibrator_u64ii.c` and his HSTL pin standards (headers), with different rules and none of his code | GPL-3.0-or-later |
| Glue around Gideon's entities | `host/ultimate_sys.vhd`, `uci_unit.vhd`, `flash_gate.vhd`, `usb_dpram.vhd`, `pseudo_dpram_8x32_x7.vhd`; `cores/common/drive_unit.vhd`, `drive_mem.vhd`, `drive_l2.vhd`, `drive_l2_empty.v` | Ours, around Gideon's entities (section 5); `usb_dpram.vhd` and `pseudo_dpram_8x32_x7.vhd` are architectures for his entities | GPL-3.0-or-later |
| Boot ROM | `host/bootrom/bootrom.c`, `crt0.S`, `linker.ld`, `mkpkg.py` -> `host/bootrom_pkg.vhd` (generated) | `bootrom.c` says in its header that it is made from Gideon's U64-II boot loader (`bootloader_u64ii.c`, vendored, GPL-3): a derivative of GPL-3 code | GPL-3 (derivative) |
| Overlay fonts | `host/mem/ovl_font8.mem`, `ovl_font12.mem`, made by `tools/font2mem.py` | Gideon's `char_generator_rom_pkg.vhd` and `font_pkg.vhd` (header of `font2mem.py`) | GPL-3 (`vendor/gideon/LICENSE.txt`) |
| Pin constraints | `host/c64u.xdc` | Posted by Gideon Zweijtzer in the 1541ultimate issue 615 (https://github.com/GideonZ/1541ultimate/issues/615#issuecomment-3853426291, header of the file), copied verbatim. No licence stated for it. `host/pins.xdc`, `ddr2_pins.xdc`, `usb_pins.xdc`, `gtp_pins.xdc` take balls and standards from it | none stated for `c64u.xdc`; the others are ours |
| Null core | `cores/null/` (`layer2_null.v`, `null_regs.v`) | Ours | GPL-3.0-or-later |
| Layer-1 / layer-2 contract | `contract/`, `host/layers.txt` | Ours | GPL-3.0-or-later |
| MIG pin list | `host/mig_ddr2.prj` | Configuration of AMD's MIG 7 Series IP. The MIG controller is not built; the file is read only by `tools/pincheck.tcl` for pad names | AMD/Xilinx terms (section 9) |

## 4. The C64 gateware: `vendor/icepi`

Started as the C64 gateware of icepi-zero-c64 (Mateusz Nalewajski, "m1nl", https://github.com/m1nl/icepi-zero-c64, a
fork of it, branch `easyflash-writable`). Since 2026-10-01 it is our own code, edited in place (`vendor/icepi/ORIGIN.md`:
commits, former patches, later edits). The whole tree carries the icepi repository's licence file `vendor/icepi/LICENSE`
(GNU GPL version 3); the parts below name their own licence where they have one. Read by `cores/c64u`
(the C64 inside their layer 2), `cores/atari800` (`6502n` only) and, for `hdmi/`, by the host.

"Read, pruned" means a build reads the files but the block is not instantiated with the parameters we use.

| Block | Path (`vendor/icepi/...`) | Origin (author / project, URL) | Version / commit | Licence | Notes |
|---|---|---|---|---|---|
| icepi C64 glue | `c64_top.v`, `cpu6510.v`, `c64_bus_arbiter.v`, `c64_ram.v`, `c64_keyboard.v`, `c64_tape.v`, `c64_reu.v`, `c64_redip_sid.v`, `c64_redip_cia.v`, `vicii_kawari.v`, `usb_hid_host_dual.v` | Mateusz Nalewajski, icepi-zero-c64 | a50750d745a426e5b5f138843695ac713b50297f | GPL-3.0-or-later (headers "Copyright 2026 Mateusz Nalewajski ... either version 3") | Modified by us (former patches 0001, 0003-0009, 0020-0050 and later edits: state bus, snapshot ports, REU, SID) |
| Cartridge, flash and EEPROM models | `c64_cartridge.v`, `c64_flash_amd.v`, `c64_eeprom_93c86.v` | Mateusz Nalewajski, icepi-zero-c64 | a50750d | GPL-3.0-or-later (file headers) | `c64_cartridge.v` modified by us |
| `vicii_kawari.vh` | `vicii_kawari.vh` | icepi-zero-c64 | a50750d | No header; `vendor/icepi/LICENSE` by location | Unmodified |
| Our additions | `c64_pla.v`, `c64_turbo.v`, `c64_redip_sid/sid_multi.sv` | Ours (T29 turbo engine, T25.4 eight SIDs) | - | `SPDX-License-Identifier` in each file (GPL-3.0-or-later) | New files, not from icepi |
| Action Replay PLA | `c64_action_replay/c64_action_replay_pla.v` (+ `.txt`) | icepi-zero-c64; "autogenerated by espresso from c64_action_replay_pla.txt" (header) | a50750d | No header; `vendor/icepi/LICENSE` by location only | Unmodified |
| CDC helpers | `cdc/cdc_sync.v`, `cdc_pulse.v`, `cdc_handshake.v` | Mateusz Nalewajski | a50750d | GPL-3.0-or-later (headers) | Unmodified |
| 6502N (C64 CPU, also the Atari's) | `6502n/*.vhd` | Gideon Zweijtzer, 1541ultimate; `6502n/README.md`: "Source: https://github.com/GideonZ/1541ultimate/tree/master/fpga/6502n/vhdl_source" | Upstream commit not recorded (came with icepi a50750d) | No licence header in the VHDL files; upstream 1541ultimate is GPL-3 (`vendor/gideon/LICENSE.txt`), here `vendor/icepi/LICENSE` by location | Modified by us (`proc_core`, `proc_control`, `proc_interrupt`, `proc_registers`: state bus and load ports) |
| T65 | `T65/*.vhd` | Daniel Wallner, Mike Johnson, Wolfgang Scherr, Morten Leikvoll (FPGAARCADE) | Ver 315 (header of `T65.vhd`); upstream URL not recorded | BSD-style, 3 clauses incl. "Redistributions in synthesized form must reproduce the above copyright notice" (headers) | Unmodified; read, pruned (`cpu6510` selects the 6502N through `CPU_MODEL(1)`) |
| VIC-II: vicii-kawari | `vicii-kawari/hdl/*`, colour/luma `*.bin` | Randy Rossi, https://github.com/randyrossi/vicii-kawari | ca8c43dc69358173e5f0cb632a8c3b91f309cb5a | GPL-3 (`vicii-kawari/LICENSE`; headers "Copyright (c) 2022 Randy Rossi") | Modified by us: `vicii.v`, `sprites.v`, `registers_scaled.v`. Kawari's own DVI output is left open |
| SID: reDIP-SID | `c64_redip_sid/reDIP-SID/gateware/*.sv`, `*.svh`, `*.mem`; `c64_redip_sid/sid_api.sv` | Dag Lem, https://github.com/daglem/reDIP-SID | f89f53e3108bcb03e8f23505bc3a70d0c206e0e6 | CERN-OHL-S v2 (`reDIP-SID/LICENSE`; headers "Copyright (C) 2022 - 2023 Dag Lem ... CERN-OHL-S v2") | Modified by us (T25.4): the pipelines take the number of SIDs as a parameter; `sid_core.sv` is new (CERN-OHL-S header); `sid_api.sv` is the icepi wrapper on it. `sid_multi.sv` (8 SIDs) is ours, GPL-3.0-or-later. Meant to go back to Dag Lem as separate commits |
| SID glue | `c64_redip_sid/dc_blocker.v`, `muladd.sv` | icepi-zero-c64 (`dc_blocker`: Mateusz Nalewajski) | a50750d | `dc_blocker.v` GPL-3.0-or-later (header); `muladd.sv`: no header, `vendor/icepi/LICENSE` by location | Unmodified |
| CIA: reDIP-CIA | `c64_redip_cia/reDIP-CIA/gateware/CIA/*.sv`, `c64_redip_cia/cia_core.sv` | Dag Lem, https://github.com/daglem/reDIP-CIA | 7e077151804106269eb522b63ccca29b7279d9fe | CERN-OHL-S v2 (`reDIP-CIA/LICENSE`; headers "Copyright (C) 2025 Dag Lem ... CERN-OHL-S v2") | Modified by us: `cia_core.sv` and every CIA file except `bcd_add`/`bcd_update`/`cia_pkg` (state bus, snapshot) |
| VIA: reDIP-CIA VIA | `c64_redip_cia/reDIP-CIA/gateware/VIA/*.sv`, `via_core.sv`, `c64_redip_via.v` | Dag Lem (VIA files); Mateusz Nalewajski (`c64_redip_via.v`) | 7e07715 / a50750d | CERN-OHL-S v2 (headers "Copyright (C) 2025 - 2026 Dag Lem"); `c64_redip_via.v` GPL-3.0-or-later | Unmodified; read, pruned |
| icepi internal 1541 | `c64_c1541.v`, `c64_c1541/*.v`, `iecdrv_via6522.vhd`, `mem/*.mem` | `c1541_drv/gcr/logic`: Dar (darfpga), Sorgelig / Alexey Melnikov (MiSTer), reworked by m1nl; `c1541_track`: Sorgelig; `iecdrv_ram/rom`, `c64_c1541.v`: Mateusz Nalewajski; `iecdrv_via6522`: Gideon Zweijtzer | a50750d | `c1541_drv/gcr/logic`: `SPDX-License-Identifier: GPL-3.0-or-later`; `c1541_track.v`: Lesser GPL version 3 or later (header, "Copyright (c) 2016 Sorgelig"); `iecdrv_*.v`, `c64_c1541.v`: GPL-3.0-or-later; `iecdrv_via6522.vhd`: "License: GPL 3.0" | Unmodified; read, pruned (the drive is Gideon's, section 5) |
| HDMI encoder | `hdmi/hdmi.v`, `*.sv` | Sameer Puri, hdl-util/hdmi, https://github.com/hdl-util/hdmi (headers name the author); `hdmi.v` "Converted from SystemVerilog to Verilog" | Came with icepi a50750d; upstream commit not recorded | **MIT OR Apache-2.0** (upstream README "dual-licensed under MIT and Apache 2.0"; `LICENSE-MIT`, `LICENSE-APACHE` in `hdmi/`) | Modified by us (T40): `RUNTIME`/`MAX_PACKETS` parameters and `vic_code`/`video_rate` inputs in `hdmi.v`, `packet_picker.sv`, `auxiliary_video_information_info_frame.sv` / T164 WP4: the host reads its own fork `host/hdmi_enc/` (same licences, headers kept, `*.diff` against this directory) |
| USB HID host | `usb_hid_host/rtl/*.v`, `rom/usb_hid_host_rom.mem` (from `rom/ukp.s`) | nand2mario, reworked by Mateusz Nalewajski (headers "Copyright 2023 nand2mario, Copyright 2026 Mateusz Nalewajski"); upstream URL not recorded | e49217661b275554807760d4e12097b0f189316a | Apache-2.0 (`usb_hid_host/LICENSE`; header of `usb_hid_host.v` and `rom/ukp.s`); the two `*_rom.v` files have no header | Unmodified. The C64 core holds it in reset; whether Vivado trims it was not checked |

## 5. Gideon Zweijtzer's FPGA blocks: `vendor/gideon`, `cores/c64u/gideon`, `tools/patches/gideon`

Parts of Gideon Zweijtzer's 1541ultimate (`fpga/`, upstream https://github.com/GideonZ/1541ultimate), taken by
`tools/vendor_gideon.sh` and `tools/vendor_gideon_app.sh` (run by `tools/gideon_sources.sh <checkout>`) from a checkout of
our fork of it (branch `trx64/main`). `vendor/gideon` is our own copy and is in the repository (T120, Alex 2026-11-01: "own copy");
there is no submodule any more. `vendor/gideon/VENDOR.md` records the commits it was made from (`32e5fd5f` at the time of
writing). Besides the VHDL it holds the software of the Ultimate application that `host-app/`, `coreapp/` and the update_trx
installer compile, and the third-party trees they link (LVGL, lwIP, FreeRTOS, FatFS, httpd) under their own licences.
Licence of the repository: GNU GPL version 3, copied as `vendor/gideon/LICENSE.txt`. Most files name Gideon Zweijtzer /
Gideon's Logic Architectures / Gideon's Logic B.V. in the header; many have no header at all; only `via6522.vhd`
states a licence itself ("License: GPL 3.0").

`tools/vendor_gideon.sh` refuses any file containing "without written permission". Clean-room rule: Gideon's UltiSID
(`fpga/sid6581`), Copper (`fpga/io/copper`), `lp_filter` (`io/audio/lp_filter*`) and sampler are not vendored, not used,
and not read for this document (not read (clean room)); our own SID engine, sampler and audio path are in
`cores/*/trx_sid` (section 6). Gideon's own firmware update files (`.cfw`) are not part of this repository or of our releases; they come from the
upstream project (https://ultimate64.com and the GitHub releases of 1541ultimate).

All rows: author Gideon Zweijtzer, licence GPL-3 (`vendor/gideon/LICENSE.txt`). Used in the host unless noted.

| Block | Path (`vendor/gideon/fpga/...`) | Notes |
|---|---|---|
| rvlite RISC-V CPU | `cpu_unit/rvlite/vhdl_source/*.vhd` incl. `bootrom_8k.vhd`, `rvlite_wrapper.vhd` | Patched: 0005 (debug PC tap in `core.vhd`, `rvlite_wrapper.vhd`), 0006 (own multiply pipeline in `execute.vhd`, `multiply.vhd`). Boot ROM content is ours (`bootrom_pkg.vhd`, section 3) |
| IO and memory busses | `ip/busses/vhdl_source/io_bus_pkg.vhd`, `mem_bus_pkg.vhd`, `io_bus_splitter.vhd`, `io_bus_bridge2.vhd`, `slot_bus_pkg.vhd`, `mem_bus_arbiter_pri_32.vhd`, `mem_bus_arbiter_pri.vhd`, `mem_to_mem32.vhd`, `endianness_pkg.vhd` | Unmodified. The DDR2 arbiter `mig_arb` is ours (`host/ddr2_port.v`) |
| ITU, u2p_io | `io/itu/vhdl_source/itu.vhd`, `itu_pkg.vhd`; `altera/u2p_io.vhd` | Unmodified |
| UART with DMA and SLIP | `io/uart_lite/vhdl_source/*.vhd` (8 files) | Unmodified |
| SPI (flash) | `io/spi/vhdl_source/spi.vhd`, `spi_peripheral_io.vhd` | Unmodified; our write gate is `host/flash_gate.vhd`. The SD card of the flat builds of `amiga1200` and `zxnext` only uses `cores/amiga1200/sd/sd_spi.vhd`, a copy of both files with IOB flops on the pins, a later MISO sample and a capability bit (T149 D5); the host and the DFX cores keep Gideon's block |
| Ethernet RMII MAC | `io/rmii/vhdl_source/crc32.vhd`, `rmii_transceiver.vhd`, `eth_filter.vhd`, `eth_transmit.vhd`, `ethernet_interface.vhd`; `ip/free_queue/vhdl_source/*.vhd` | `eth_transmit.vhd` patched (0003). `ethernet_rmii.vhd` is vendored but not read by the build |
| USB 2 host with nano CPU | `io/usb2/vhdl_source/*.vhd` (20 files), `ip/nano_cpu/vhdl_source/*.vhd`, `altera/distributed_stack.vhd`, `altera/align_read_to_bram.vhd`, `ip/memory/vhdl_source/dpram_8x16.vhd`, `dpram_8x32.vhd` (entities) | Unmodified; the RAM architectures are ours (`usb_dpram.vhd`) |
| Command interface (UCI) | `io/command_interface/vhdl_source/*.vhd`, `ip/memory/vhdl_source/dpram.vhd`, `ip/synchroniser/vhdl_source/synchronizer_gzw.vhd` | Unmodified; instantiated in `host/uci_unit.vhd` |
| 1541 drive | `1541/vhdl_source/*.vhd` (incl. `via6522.vhd`); `6502n/vhdl_source/*.vhd` as the drive CPU | Patched: 0001, 0002 (`c1541_drive.vhd`, `floppy_stream.vhd`), 0004 (`cpu_part_1541.vhd`, `cpu6502.vhd`). The 6502n entities are renamed `drv_*` by `tools/vendor_gideon.sh` (mechanical). Used by the host and, through `cores/common`, by the cores with a drive |
| Infrastructure | `ip/clock/vhdl_source/fractional_div.vhd`, `ip/srl_fifo/vhdl_source/srl_fifo-xilinx.vhd` (uses Xilinx `unisim` SRL16E), `ip/sync_fifo`, `ip/async_fifo`, `ip/synchroniser`, `ip/memory/vhdl_source/spram.vhd`, `pseudo_dpram_8x32.vhd` (entity), `io/sigma_delta_dac/vhdl_source/noise_generator.vhd` (floppy sound) | Unmodified; the `pseudo_dpram` architecture is ours |
| Vendored, not synthesized | `ip/video/vhdl_source/char_generator_*.vhd`, `ip/video/vhdl_gen/font_pkg.vhd`, `ethernet_rmii.vhd`, `target/simulation/.../tl_string_util_pkg.vhd`, `software/io/usb/nano_minimal.nan`, `tools/parse_nano.py`, `software/portable/riscv/bootloader_u64ii.c` | References and simulation only; the fonts become block-RAM content (section 3), the boot loader is the reference of `host/bootrom/bootrom.c` |

**Patches** (`tools/patches/gideon`, applied by `tools/vendor_gideon.sh`; changes to GPL-3 files, own work):
`0001-c1541-drive-debug-status` (`c1541_drive.vhd`: `dbg_status` output), `0002-floppy-head-on-track-18`
(`floppy_stream.vhd`), `0003-eth-transmit-store-and-forward` (`eth_transmit.vhd`: 2048-byte FIFO, start at 1536 bytes),
`0004-drive-cpu-sync` (`cpu_part_1541.vhd`, `cpu6502.vhd`), `0005-rvlite-dbg-pc` (`core.vhd`, `rvlite_wrapper.vhd`),
`0006-rvlite-mul-pipeline` (`execute.vhd`, `multiply.vhd`).

**Copy inside the c64u core** (`cores/c64u/gideon/`): a self-contained set of the same blocks plus the 1581 controller
(`wd177x.vhd`), tape (`c2n_*.vhd`) and further 1541 files. `cores/c64u/gideon/PROVENANCE.md` lists every file with its
path under `vendor/gideon`, whether it is the generated `vendor` form or the unchanged `app` form, and our changes (the
1571 removed from `mm_drive_cpu.vhd`, `mm_drive.vhd`, `drive_registers.vhd`). It records that none of them carries the
clause "not supposed to be used in other projects without written permission". Licence GPL-3.

**Atari 800 XL core**: `cores/atari800/proc_core_a8.vhd` is a copy of `vendor/icepi/6502n/proc_core.vhd` (Gideon, GPL-3)
that differs in one line (header); `proc_interrupt_a8.vhd` is the matching interrupt unit.

## 6. Cores: `cores/*`

Every core is a directory with a `core.toml` (id, version, sources). Each core's layer 2 (`layer2_<id>.v`) and glue are
our code, GPL-3.0-or-later. The rows below name only what is third-party or derived; the `NOTICE` files carry the full
detail (commits, which files, header wording, what was left out). Upstream trees are copied unchanged by a
`tools/vendor_*.sh` script that pins the commit; `gen/` directories hold what a generator (`tools/*_gen.*`) makes of
them, mechanically.

| Core | Third-party / derived content | Origin (author, URL, commit) | Licence as the files state it | What we changed / wrote | Record |
|---|---|---|---|---|---|
| `trx64` (reference C64 core) | The C64 gateware of `vendor/icepi` (section 4); Gideon's application | see sections 4, 5 | see section 4 | `c64_core.v`, `c64_mem.v`, `cart_slot.v`, `cart_fill.v`, `trx_*.v`, `slot_port.v`, `dma_win.v`, `drv_dbg.v`, `trx_dbg.v` wrap icepi's `c64_top` and `c64_cartridge`; `trx_sid/` (next row) | `vendor/icepi/ORIGIN.md` |
| `trx_sid` (in `trx64`; a copy in `c64u`) | Digital voice follows libresidfp/reSIDfp (Antti Lankila, Dag Lem, Leandro Nini), filter follows reSID 1.0's linear 8580 path (headers of `rtl/trx_sid_engine.v`, `model/sidref.h`); combined-waveform tables computed with libresidfp (`tools/fetch_libresidfp.sh`, pinned commit, https://github.com/libsidplayfp/libresidfp); filter curve tables generated from the application's `software/io/audio/sid_coeff.c` and `u64_config.cc` (GPL-3, header of `model/gen_curves.py`) | libresidfp, reSID | GPL-2.0-or-later (libresidfp/reSID) for the algorithms and the generated `mem/*.mem` data; our files carry `SPDX-License-Identifier: GPL-3.0-or-later` and "Written without UltiSID, reDIP or other HDL sources" (`rtl/trx_sid_engine.v`) | The engine, `rtl/trx_sampler.v` (written from the application's side of the interface) and the audio path are ours | file headers |
| `c64u` | `cores/c64u/gideon/` (section 5); `vendor/icepi`; 8-SID reDIP | see sections 4, 5 | GPL-3 and as section 4 | own copies of the layer-2 files (`*_c64u.v`), `gideon/c64u_l2.vhd` and the other wrappers named in `gideon/PROVENANCE.md` | `cores/c64u/gideon/PROVENANCE.md` |
| `vc20` | VIC20Nano (MiSTle-Dev) `src/`: `m6561.vhd`, `vic20.vhd`, `vic20_clocks.vhd` (MikeJ / FPGAARCADE, Wolfgang Scherr), `vc20_via6522.vhd` (Gideon, renamed `via6522`), `vic20_keyboard.vhd`, `t65/` | https://github.com/MiSTle-Dev/VIC20Nano, 9089828d8a21ea3075c0df9c87418b42766ab289 | `m6561.vhd`, `vic20.vhd`, `vic20_clocks.vhd`: "(c) 2003-2009 MikeJ (FPGAArcade), (c) 2011-2015 WoS ... any commercial use is forbidden as well. This code must be run on Replay hardware only." (headers, kept); `vc20_via6522.vhd` GPL 3.0; T65 BSD-style; `vic20_keyboard.vhd` no header | `m6561`: colour output; `vic20`: memories, ports, PS/2 and Gowin parts removed; `vic20_clocks`: initial value; `vc20_via6522`: renamed. `trx/` is ours | `cores/vc20/rtl/NOTICE` |
| `ti99` | Mega99 (zeldin) `src/`: `mainboard.v`, `tms9900/`, `tms9918/`, `tms9919/`, `tms5200/`, `fdc1771/`, `groms.v`, `grom_storage_*.v`, `console_rom.v`, `peb*.v`, `address_decoder.v`, `multiplexer.v`, `keymatrix.v`, `scratchpad_ram.v` | https://github.com/zeldin/Mega99, 82ce2157f62688eb0cd58332b52eba0eeed77538 | LGPL-3.0 (`LICENSE-Mega99.txt` kept); the per-file licence lines are listed in the NOTICE | `mainboard.v`: parameters INCLUDE_VSP / INCLUDE_TIPI / NUM_GROMS, clock ports; `peb.v`: parameter INCLUDE_TIPI; `grom_storage_bram.v`: per-page arrays (equivalent, simulated); `clkgen.v` replaced by `trx/clkgen_nco.v`; board-level files left out. `trx/` is ours | `cores/ti99/rtl/NOTICE` |
| `c264` (C16, Plus/4) | FPGATED `ted.v`, `colors_to_rgb.v` (Istvan Hegedus; colour values Jozsef Laszlo), `t65/` | https://github.com/MiSTle-Dev/C16Nano, 82fed408f899509c4b569c02c1bc4bcf23017276, copied by `tools/vendor_c16nano.sh` | GPL v3 or later (headers of `ted.v`, `colors_to_rgb.v`; `rtl/LICENSE`); T65 BSD-style | `trx/` is written again in our text from `c16.v`, `mos8501.v`, `mos6529.v`, `c16_keymatrix.v` (behaviour theirs); `ted.v` takes a clock enable | `cores/c264/rtl/NOTICE` |
| `amiga` (OCS/ECS, 68000) | Minimig-AGA chipset (Dennis van Weeren, Jakub Bednarski, Rok Krajnc, Alexey Melnikov, Tobias Gubener and others), FX68K (Jorge Cwik), NanoMig wrapper (Till Harbaum et al.) | https://github.com/MiSTle-Dev/NanoMig, a19604a19a6100033a29281d2be3287b65bc9c0e | No LICENSE file upstream. Minimig-AGA: mostly GPL v3 or later in the headers, about 40 files without licence text, `ide.v` BSD-3-clause; `fx68k`: directory `LICENSE` GPL v3; `nanomig.v`: no licence text | Changes marked "TRX" in the files; `rtl/trx/` and the rest of the glue are ours | `cores/amiga/rtl/NOTICE`, `rtl/src/fx68k/LICENSE` |
| `amiga1200` (AGA, 68020) | TG68K kernel (Tobias Gubener; patches by MikeJ, Till Harbaum, Rok Krajnc and others) from NanoMig; forks of Minimig-AGA files kept as file plus `.diff` | NanoMig as above | TG68K: Lesser GPL version 3 or later (headers of the three VHDL files); the forks inherit the `amiga` terms | `tg68k_020.v` ours; kernel entity renamed (`tools/vendor_tg68k.sh`); `cpu_wrapper.v` one change (`cpu_wrapper.diff`); `trx_afifo_rr.v` ours | `cores/amiga1200/rtl/NOTICE` |
| `apple2` (Apple //e) | NanoApple2: `apple2.vhd`, `timing_generator.vhd`, `video_generator.vhd`, `disk_ii.vhd`, `drive_ii.vhd` (Szombathelyi Gyorgy on Stephen A. Edwards' Apple ][+), `R65Cx2.vhd` (A. Daly on Peter Wendrich's FPGA64), `mouse/applemouse.vhd`, `mouse/pia6821.vhd` (John E. Kent), `mouse/jt6805/` (Jose Tejada) | https://github.com/MiSTle-Dev/NanoApple2, 4977e0494235cb2f812d71ce78f5ab2cf675747a | The repository declares GPL v3 as a whole; the five machine files, `R65Cx2.vhd` and `applemouse.vhd` carry credit lines only, no licence text; `pia6821.vhd` "adheres to the GNU public license"; `jt6805`: GPL v3 in every file | Clock enables, ROM contents as ports (no ROM in the tree), one CPU, extra outputs; the sha256 of every pristine file is in the NOTICE; `rtl/trx/a2_palette_pkg.vhd` derived from `vga_controller.vhd`; the other `rtl/trx` files ours | `cores/apple2/rtl/NOTICE` |
| `atari800` (Atari 800 XL) | Mark Watson's Atari800 core, the files listed in `tools/vendor_atari800.sh`; `cpu_65xx.vhd` ("All Rights Reserved" by Peter Wendrich) is not used | https://github.com/MiSTer-devel/Atari800_MiSTer, 1440e41 | Watson's files: "happy for anyone to use this for non-commercial use ... contact me for explicit permission" (headers); `cart_logic`, `pbi_rom`, `freezer_logic` LGPL | Four patches `tools/patches/atari800/0001-0004` (conversion fixes); our `cpu.vhd`, `proc_core_a8.vhd`, `proc_interrupt_a8.vhd`, `trx/*.vhd` (incl. `pokeymax.vhd`, a one-POKEY replacement of the same name; GPL-3.0-or-later) | `cores/atari800/rtl/NOTICE` |
| `cpc` (Amstrad CPC) | Gate Array (`GA40010`), CRTC `UM6845R.v`, PPI `i8255.v`, `Amstrad_MMU.v`, `color_mix.sv`, FDC `u765/u765.sv` (Gyorgy Szombathelyi, Sorgelig); Z80 and AY from the `zx` core | https://github.com/MiSTer-devel/Amstrad_MiSTer, 6c2c39b | GPL v2 or later in the headers of the Gate Array files, `UM6845R.v`, `i8255.v`, `color_mix.sv`, `u765.sv`; `GA40010/rslatch.v` no licence text; `Amstrad_MMU.v` "(C) 2018 Sorgelig", no terms | `gen/u765.sv` is `tools/cpc_gen.py` applied to `u765.sv`; `trx/cpc_board.v` written from `Amstrad_motherboard.v` | `cores/cpc/rtl/NOTICE`, `rtl/COPYING` |
| `nes` | NES_MiSTer machine (`nes.v`, `ppu.sv`, `apu.sv`, `cart.sv`), 13 mapper files, `t65/` (Daniel Wallner and others) | https://github.com/MiSTer-devel/NES_MiSTer, 49a0a66 (Ludvig Strigeus' copyright and "This program is GPL Licensed" in the files) | GPL v3 (`rtl/COPYING`); several upstream files (`apu.sv`, `regs_savestates.sv`, the mappers) carry no licence text of their own | `gen/` made by `tools/nes_gen.py` (clock enables, savestate registers, mapper selection, T65 to Verilog); `trx/nes_regs.v` derives its iNES header decode from upstream `NES.sv` | `cores/nes/rtl/NOTICE` |
| `zx` (Spectrum 48K/128K) | `ula.sv` (Sorgelig), `ym2149.sv` (MikeJ, Sorgelig), `T80/` (Daniel Wallner, MikeJ, Sorgelig) | https://github.com/MiSTer-devel/ZX-Spectrum_MiSTer, 33774ad | `ula.sv` GPL v3 or later; `ym2149.sv`, T80 BSD wording; `rtl/COPYING` | `gen/t80pa.v` made by `tools/zx_gen.sh` (GHDL, two renames); `keyboard.sv` (non-commercial clause) is not taken | `cores/zx/rtl/NOTICE` |
| `zxnext` (ZX Spectrum Next) | ZX Spectrum Next FPGA source (Alvin Albrecht, Victor Trucco, Fabio Belavenuto, Garry Lancaster, Charlie Ingley and others): `zxnext.vhd`, `audio`, `cpu` (Z80N), `device`, `rom`, `serial`, `misc`, `video`. `rom/bootrom.vhd` and `rom/bootrom_ab.vhd` are the FPGA core's own boot ROM as machine-generated VHDL ("generated with romgen v3.0 by MikeJ"), source of the upstream tree; not NextZXOS, not a Spectrum ROM | https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA, b047011 (core version 3.02.03) | GPL v3 or later in the headers; `rtl/COPYING`; BSD-style in `t80n.vhd`, `ym2149.vhd`, `spi_master.vhd`; the boot ROM files carry no licence header (the NOTICE names `bootrom.vhd`, not `bootrom_ab.vhd`) | `gen/` made by `tools/zxnext_gen.py`; `ip/zxn_ip.vhd` ours | `cores/zxnext/rtl/NOTICE` |
| `snes` (Super Nintendo) | SNES_MiSTer machine by srg320: S-CPU (`CPU.vhd`, `65C816/`), S-PPU (`PPU.vhd`), S-SMP (`SMP.vhd`, `SPC700/`), S-DSP (`DSP.vhd`), `SWRAM.vhd`, `CEGen.vhd`, `SNES.vhd`, the plain cartridge mapper `chip/DSP/DSP_LHRomMap.vhd` with `OBC1.vhd`, `chip/SRTC.vhd`, `chip/RTC4513.vhd`; the DSP-n core `DSPn.vhd` is vendored but not built | https://github.com/MiSTer-devel/SNES_MiSTer, bdcd33c5 (2026-10-23) | no licence header in the files; GPL-3.0 by the repository's `LICENSE` (`rtl/COPYING`) | `SMP.vhd` is the upstream file but for the 64 byte boot ROM (a chip ROM dump), which `tools/vendor_snes.sh` cuts out (the user's `spc700.rom` replaces it); `gen/` made by `tools/snes_gen.py` (GHDL, edits counted, clock enables); the memories (Altera wrappers `bram.vhd`, `mlab.vhd` are not taken) and the machine's top `ip/snes_ip.vhd`, `ip/snes_sys.vhd` are ours; the MiSTer shell (`SNES.sv`, `main.v`, SDRAM, HPS) is not taken, the glue in `trx/` is written for this project from its behaviour | `cores/snes/rtl/NOTICE` |
| `neogeo` (SNK Neo Geo MVS/AES) | NeoGeo_MiSTer: furrtek's chip models (`cells`, `io`, `video`, `mem/memcard.v`, `mem/backup.v`; Sean Gonsalves), the CPU wrappers, FX68K (Jorge Cwik), T80 (Daniel Wallner, MikeJ, Sorgelig), jt10 / jt49 (Jose Tejada); `neogeo.sv` is kept as the reference of our top and is not built | https://github.com/MiSTer-devel/NeoGeo_MiSTer, ffaab14 | GPL v2 or later (io, mem, part of video), GPL v3 or later (cells, most of video, cpu wrappers), FX68K GPL v3 or later by `fx68k.txt` (the .sv files name the copyright only), jt10 / jt49 GPL v3 (own `LICENSE`), T80 BSD-style; `register.v` and `jt12_pcm.v` carry no header | `trx/neo_core.v` is `neogeo.sv` with the MiSTer framework, the Neo Geo CD, the SDRAM / DDR3 controllers and the altsyncram memories replaced (marked "TRX"); the memories, DDR2 service, register block, picture writer and audio hand-over are ours; no ROM, BIOS or game anywhere | `cores/neogeo/rtl/NOTICE` |
| `null` | none | ours | GPL-3.0-or-later | the flash filler (section 1) | `cores/null/core.toml` |

The `NOTICE` file of each core is the record of its upstream, commit, header terms, what was left out and what was changed.

## 7. Applications: `host-app/`, `coreapp/`, `app/`, `vendor/gideon/software`

| Part | Path | Origin | Licence | What we changed |
|---|---|---|---|---|
| Ultimate application software | `vendor/gideon/software`, `vendor/gideon/apps`, `vendor/gideon/tools`, `vendor/gideon/target`, `vendor/gideon/roms` (our own copy of Gideon Zweijtzer's 1541ultimate, https://github.com/GideonZ/1541ultimate, our fork, branch `trx64/main`, commit in `vendor/gideon/VENDOR.md`; files listed in `tools/gideon_app.list`) | Gideon Zweijtzer and contributors | GPL-3 (`LICENSING.md`); third-party trees inside it (FreeRTOS, FatFS, lwIP, LVGL, httpd) under their own licences, which sit next to their files | Our branch on top of his; what `host-app/`, `coreapp/` and the update_trx installer compile. The application the `trx64` and `c64u` cores run is built elsewhere (T100 D15) |
| Host application copies | `host-app/software/<dir>/` except `hostapp/` | Byte-for-byte copies of the Ultimate application's files, licence headers untouched; `host-app/PROVENANCE.md` lists every file with origin and sha256 and records the commit of the fork the copy is taken from | GPL-3 (Gideon's headers); `software/api/jsmn.h` MIT (its header) | Marked MODIFIED in that list: `keyboard_c64.cc`, `trx_io.cc/.h`, `usb_hid.cc/.h`, `trx_lv_port.cc/.h`; the patches are `host-app/patches/` (GPL-3, they change GPL-3 files) |
| Linked, not copied | `host-app/software/FreeRTOS`, `chan_fat`, `httpd`, `lvgl`, `lwip` | Symlinks to the copies in `vendor/gideon/software` | their own licences (next to their files) | none |
| Makefiles | `host-app/target/` | Derived from `target/common`, `target/libs/riscv/{lwip,lvgl}` and `target/u64ii/riscv/ultimate` of the Ultimate application (trimmed) | GPL-3 | C64 side removed |
| Host application proper | `host-app/software/hostapp/` (incl. `sha256.c`: "SHA-256 (FIPS 180-4), written for the TRX64 host boot app"), `tools/hostapp_vendor.py`, `tools/build_hostapp.sh` | Ours | GPL-3.0-or-later | - |
| Core application | `coreapp/`, `app/` (`app/loaders`, `app/video`, `app/view`, `app/lib/testlib`) | Ours, built on the host-app base; in several places it follows Gideon's behaviour (headers name `u64_config.cc`, `c1541.cc`, his `.cfg` format) | GPL-3.0-or-later | - |
| Cartridge descriptors | `coreapp/cores/c64_cart_desc.h` | 19 rows (T154: GMod2 and EasyFlash 3 added) and 7 flash parts of Gideon's (`vendor/gideon/software/io/c64/trx_cart_desc.h`, fork commit `aa2ef5dc`, file sha256 `7fd374aa94bb`), byte for byte, for the CART_TYPE values `coreapp/cores/c64_cart.cc` starts; the file there is generated by TRX64-Ultimate `tools/gen_cart_descriptors.py` from `tools/cart_descriptors/*.json`. `c64_cart.cc` follows the behaviour of `c64_crt.cc` (`read_chip_packet`, `auto_mirror`, `configure_cart`) and `c64.cc` (`start_cartridge`, `set_cartridge`) and `trx_cart.cc` (`trx_cart_configure`, `trx_xport_shared`, `trx_cart_irq`, the flash geometry of `trx_flash_geom.h`) and `c64_subsys.cc` (when the file is written, the .tmp / .bak replace); T154 follows their behaviour, no code of them is copied | GPL-3 (Gideon's header; the rows) | The rows of the types this application does not start are left out ; the flash parts of the seven flash types are in (T154), with the same bytes |
| Drive library | `app/lib/cbm1541/` (`d1541_gcr`, `d1541_drive`, `d1541_wd`, `d1541_fs`) | Keeps the logic of Gideon's `disk_image.cc`, `c1541.cc`, `wd177x.cc`/`mfmdisk.cc` and `filesystem_d64.cc` (headers say which parts): derived from GPL-3 code | GPL-3 (derivative) | Software GCR codec, poll instead of interrupt, no 1571 |
| U64 register writes of the core application (T104.2 UI) | `coreapp/src/u64cfg.cc`, `coreapp/src/u64_resample.inc` | Ported from Gideon's `software/u64/u64_config.cc` (`effectuate_settings`, `setMixer`, `setSpeakerMixer`, the SID sockets, addressing and UltiSID functions, `auto_mirror`, `setCpuSpeed`, `SetResampleFilter`, the palette programming) and `software/system/u64.h` (register addresses); `u64_resample.inc` is lines 2364-2455 of `u64_config.cc` unchanged; the keys are his item names | GPL-3 (derivative; the `.inc` a copy) | The values come from the core's settings, not a ConfigStore; the picture mode goes through `video_apply` |
| Cartridge port, REU, fast reset | `coreapp/src/trx64u_ops.cc` | Follows `software/io/c64/c64.cc` (`ConfigureU64SystemBus`, `set_emulation_flags`, `fastresetPatch`, `fastresetOrg`: two byte arrays copied) | GPL-3 (derivative) | Writes through `CoreIo` |
| ESP32 control module, Wi-Fi, power | `coreapp/src/hw.cc` (`hw_esp32_rpc`), `coreapp/src/netops.cc`, `u64cfg.cc` (power) | Request layouts and command numbers of `software/u64ctrl/main/rpc_calls.h` and `software/io/wifi/wifi_cmd.cc`; the data streams follow `software/io/network/data_streamer.cc` (`calculate_udp_headers`) | GPL-3 (derivative) | A raw request over the SLIP link of the rocker driver; no tasks |
| Frame ring (Timewarp) | `coreapp/src/ring.cc`, `ring.h`→`trx_ring.h`, `ringfile.cc`, `trx_c64re.h` | Copies of TRX64-Ultimate `software/ultimate-app/software/io/c64/trx_ring.cc/.h` and `trx_c64re.cc/.h` (same author: ours; the headers say what is changed: the InitFunction is gone, `ring_autostart`) | GPL-3.0-or-later (ours) | See the headers; the Timewarp screen is declarative (`ringops.cc`, `menu.toml`), no screen code of `trx_timewarp_ui.cc` |
| Monitor engine | `coreapp/src/trxmon/` (`tm_mon.c`, `tm_mach.c`, `tm_ring.c`, `tm_trace.c`, `tm_rpc.c`, `tm_ui.c`, `tm_hw_trx.c`, `tm_asm6502.c`, `tm_dis6502.c`, ..., with `README.md`) | Copies of `vendor/gideon/apps/trxmon/src` and `vendor/gideon/software/io/c64/trx_lens.c` | GPL-3 | None except the file names (prefix `tm_`); `monitor.cc` replaces trxmon's `main.c` (trx table after `app_api_net.cc`, platform calls, screen session) |
| Monitor RPC server | `coreapp/src/monsrv.cc`, `monsrv.h` | The network half of `vendor/gideon/apps/trxmon/src/main.c` (commit `aa2ef5dc`) over lwIP sockets; the holder buffers are allocated while it is connected | GPL-3.0 | No blocking wait (polled by `mon_serve` in the main loop), no detached task |
| FTP client | `coreapp/src/ftp_client.cc`, `ftp_client.h` | Copies of `vendor/gideon/software/network/ftp_client.*` (Gideon Zweijtzer's firmware) | GPL-3.0 | None; a header line names the source |
| FTP medium | `coreapp/src/netfs.cc`, `netfs.h` | Ours; the list parser and the connect/login sequence follow `vendor/gideon/software/filesystem/filesystem_ftp.cc` | GPL-3.0-or-later | A folder of the application's CoreFs with the file held in RAM, in place of the firmware's file manager tree with a temp file cache |
| Assembly64 / CommoServe client | `coreapp/src/a64_assembly.cc`, `a64_assembly.h`, `a64_http.cc`, `a64_http.h` | Copies of `vendor/gideon/software/network/assembly.*` and `http_request.*` (Gideon Zweijtzer's firmware) | GPL-3.0 | The download goes to RAM instead of a TempfileWriter, the class HttpRequest and the test main are dropped |
| Online search | `coreapp/src/online.cc`, `online.h` | Ours; the query is built the way `vendor/gideon/software/userinterface/assembly_search.cc` builds it | GPL-3.0-or-later | The form, the results and the files are menu nodes, states and a folder of the CoreFs (netfs.cc) |
| Applications on ABI 3 (T104.3) | `apps/unzip`, `apps/softboot`, `apps/trex_crt` (with `PROVENANCE` lines in each `README.md`), `app/sdk/` | Copies of `vendor/gideon/apps/<name>/src` (commit `aa2ef5dc` of the fork); `app/sdk/u2p_compat.c` is the ABI 2 table of the same copy's `app_loader/app_api.h` on our service tables, its `u2p_vsnprintf` is `trex_crt/src/ui_shim.c`; `libc.c` is `unzip/src/libc.c` plus the same kind; `apps/miniz/src` is miniz (MIT) as `unzip/src/miniz` of that copy carries it (moved out of `apps/unzip` into the zip library, T197) | GPL-3 (derivative); miniz MIT | The character screen (`screen_open`, `win_*`, `getch`) is gone: the screens are a declaration (`app.toml`) the base draws; each application's entry is a C++ `glue.cc`; division is ours (`app/sdk/libc.c`) |
| Library, monitor, diagnostics, text settings | `coreapp/src/libfiles.cc`, `t64.cc`, `monitor.cc`, `diag.cc`, `ringops.cc`, `menuctl.cc`, `screens.cc` | Ours; the viewer and the file operations follow the behaviour of Gideon's browser (`C= A`, `C= C`, `C= V`, View, Hex View, Rename, Delete, quick seek) | GPL-3.0-or-later | - |
| UI fonts | LVGL's built-in Montserrat and UNSCII (`host-app/software/hostapp/lv_conf.h`); `app/view/trxview.cc` says no font binary is added to the repository | LVGL (`vendor/gideon/software/lvgl`) | per LVGL, next to its files | - |

`host-app/PROVENANCE.md` names the commit of the fork the copies were checked against, `32e5fd5f` (the sha256 of every
copied file equals that of `vendor/gideon`); `cores/c64u/gideon/PROVENANCE.md` still names `aa2ef5dc` and the path `apps/ultimate`,
which is now `vendor/gideon` (the VHDL files are the same).

## 8. Logos, icons, documentation fonts

| Content | Path | Origin | Licence |
|---|---|---|---|
| Core logos | `cores/*/logo/` (`logo-32.png`, `icon.rgb565`), `tools/core_logos/` | Drawn by `tools/core_logos/build.py`: "TRX64 UI adaptations, not vendor-supplied originals" (`tools/core_logos/README.md`); they take historic machine cues (Commodore `C=` badge with five colour stripes, Apple six-colour apple, and the other machines' marks) | Ours, GPL-3.0-or-later. The marks they refer to belong to their owners: a trademark question, not a copyright licence |
| Firmware marks | `host-app/software/hostapp/cfw_icons.h` (made by `tools/gen_cfw_icons.py`) | From the c64u, trx64 and generic logos above | as above |
| Documentation font | `docs/lvgl_ui_terminal/fonts/IBMPlexMono-Regular.ttf` + `OFL.txt` | IBM Plex Mono, Copyright 2017 IBM Corp. | SIL Open Font License 1.1 (`OFL.txt`) |
| Documentation font | `docs/lvgl_ui_terminal/fonts/LiberationSans-Regular.ttf`, `docs/lvgl_ui_project/fonts/LiberationSans-Regular.ttf` | Symbolic links into an LVGL checkout path that is not in this tree: no font file and no Liberation licence text here | not determined |

## 9. AMD/Xilinx IP and primitives

| Block | Path | Used in | Origin | Version | Licence | Notes |
|---|---|---|---|---|---|---|
| 7 Series GT wizard (`gt_hdmi`) | configuration in `host/build.tcl`, `designs/hdmi_test/build.tcl` | host, `hdmi_test` | AMD/Xilinx `xilinx.com:ip:gtwizard` | Vivado 2024.1 (`vivado/README.md`) | AMD/Xilinx IP terms; no licence text in the repo. The image is installed with `--agree XilinxEULA,3rdPartyEULA` (`vivado/install_config_2024.1.txt`) | Generated at build time (`trx_ip` in `tools/vivado_lib.tcl`) into the build cache, never stored in the repository |
| MIG 7 Series DDR2 controller | none | none | AMD/Xilinx `xilinx.com:ip:mig_7series` | - | - | Not generated and not built (nothing in `host/build.tcl` or `tools/vivado_lib.tcl` creates it); only its configuration file is read for pad names (section 3) |
| MMCM DRP words | the table in `host/mode_ctl.v`, printed by `tools/mmcm_drp_gen.v` | host | AMD's XAPP888 functions `mmcm_pll_drp_func_7s_mmcm.vh` (clk_wiz 6.0 in the Vivado image), evaluated once; the `.vh` is not in the repo | Vivado 2024.1 | AMD/Xilinx terms for the `.vh`; the table holds only computed register values | The generator `include`s the file from the image at run time |
| Primitives | MMCME2_BASE/ADV, PLLE2_BASE, BUFG, BUFGCE, BUFGMUX (`zxnext`), ODDR, IOBUF, IDELAYE2, IDELAYCTRL, ISERDESE2, OSERDESE2, STARTUPE2, BSCANE2, ICAPE2, RAMB18E1/RAMB36, SRL16E (through `unisim` in Gideon's `srl_fifo-xilinx.vhd`); inferred block RAM and DSP | host, cores, `ddr2_test`, `hdmi_test` | AMD/Xilinx unisim library | Vivado 2024.1 | as above | Instantiated in our sources, no Xilinx source in the repo. No XPM macros are used |

## 10. ROMs and the GitHub copy

The repository ships no ROM image in a release, a `.cor`, a `.cfw`, a package or a bitstream. The cores' Kernals, BASICs,
Kickstarts, character ROMs and Disk II ROMs are written by the application from the user's files (the names are in each
`core.toml`, field `roms`); `tools/check_no_roms.sh` and `tools/check_no_rom.py` check the tree and the packages.

- **Simulation inputs**: the C64 and 1541 simulations read four Commodore images from `roms/` (Kernal 901227-03, BASIC
  901226-01, character ROM 901225-01, 1541-II; Commodore, Cloanto), the Atari bench the Atari XL OS, Atari BASIC and the
  upstream core's PBI BIOS from `cores/atari800/roms/`. The images are not part of the published source: one README per
  folder (`roms/README.md`, `cores/atari800/roms/README.md`) names the files the benches expect, their size and hash, and
  links to legal sources. `tools/github_snapshot.py` makes the published copy and checks it: no file with a ROM or image
  extension, no private host name or address.
- **Not a ROM**: `cores/zxnext/rtl/rom/bootrom.vhd` and `bootrom_ab.vhd` are the VHDL source of the Spectrum Next FPGA
  core's own boot ROM (section 6); they are part of the published source. NextZXOS and the Spectrum ROMs come from the user's own
  files, never from the tree.
- **A ROM that is not here**: the 64 byte boot ROM of the SNES's S-SMP (`SMP.vhd`, constant `IPLROM`) is a dump of Nintendo's chip ROM; `tools/vendor_snes.sh` cuts it out of
  the vendored file and `tools/check_snes_tree.py` refuses it. The application (`coreapp/cores/snes.cc`, `spc_ipl`) carries the 64 bytes, byte for byte from SNES_MiSTer `bdcd33c5` `rtl/SMP.vhd:76`, and writes them into the machine at the start (second licence exception, Alex 2026-10-29, T142 Q3; CLAUDE.md). The DSP-1B, Cx4 and BSX
  `.mif` files of the upstream are not taken either. The S-DSP's gaussian interpolation table (`DSP_PKG.vhd`, 512 entries) is a table of the chip that the upstream source
  carries and the machine cannot sound right without; it stays (open for Alex, T142 report).
- **Data in the bitstreams that is not a ROM**: `host/bootrom_pkg.vhd` (our boot ROM, section 3), the overlay fonts, the SID
  tables (`reDIP-SID/gateware/*.mem`, `trx_sid/mem/*.mem`), the kawari colour/luma tables (`vicii-kawari/hdl/*.bin`), the
  USB HID ROM (`usb_hid_host/rom/*.mem`), the Action Replay PLA.

## 11. Tools, tests, CI

Ours, GPL-3.0-or-later, SPDX in few files: `tools/` (including `legacy_boot/`, which starts Gideon's firmware from a
vanilla `.cfw` and contains none of it), `ci/`, `tests/`, `host/sim`, `cores/*/sim`, `vivado/` and `tools/ghdl6/`
(Dockerfiles; the Vivado image holds AMD's tree and is not published). `tools/vendor_*.sh`, `tools/*_gen.*` and
`tools/fetch_libresidfp.sh` fetch or convert third-party trees and are not part of a release. Third-party test programs
and media (VICE test programs, Lorenz, scene releases) are fetched by the scripts; no such media directory was found in
the tree; this was not audited file by file.

## 12. Licence summary

**Distribution.** The host update file (`.cfw`: the host with the null core) and the cores (`.cor`) are published as
built bitstreams together with the source (GitHub releases), never with a ROM. The AMD/Xilinx parts in them are
primitives instantiated in our sources and IP generated at build time from the configuration in `host/build.tcl` (the
GT wizard); no generated AMD/Xilinx source is in the repository. Rights holders who see a problem: please contact us,
and the part concerned is removed (section 2).

**Licences that meet, per bitstream**

- **Host image (`.cfw`)**: host and null core. GPL-3.0-or-later (ours, `cores/null`); GPL-3 (Gideon's rvlite, buses, UART,
  SPI, Ethernet MAC, USB host, UCI, 1541 drive and our patches; the boot ROM derived from his loader; the overlay
  fonts); MIT OR Apache-2.0 (HDMI encoder, `vendor/icepi/hdmi`); AMD/Xilinx IP terms (GT wizard, primitives). The host
  application in the same file: GPL-3 (copies of the Ultimate application, MIT for `jsmn.h`); FreeRTOS, FatFS, lwIP,
  LVGL and httpd under the licences in `vendor/gideon/software`; our code GPL-3.0-or-later.
- **`trx64` and `c64u` (`.cor`)**: GPL-3 / GPL-3.0-or-later (icepi glue and models, vicii-kawari, Gideon's blocks, our
  code; with `trx_sid`: GPL-2.0-or-later algorithms, tables generated with libresidfp); CERN-OHL-S v2 (reDIP-SID and
  reDIP-CIA, Dag Lem, modified by us); Apache-2.0 (`usb_hid_host`, if not trimmed); MIT OR Apache-2.0 (HDMI encoder);
  AMD/Xilinx primitives. Without a header: icepi's 6502N (Gideon, GPL-3 upstream), `muladd.sv`, the Action Replay PLA,
  the two `usb_hid_host` ROM wrappers, `vicii_kawari.vh`. Read but not in the bitstream: T65 (BSD-style), `c1541_track`
  (Lesser GPL 3+), the rest of icepi's internal 1541 and the reDIP VIA. `c64u` also carries `cores/c64u/gideon` (GPL-3).
- **`vc20`**: the "Replay hardware only" headers of three files (MikeJ, Wolfgang Scherr); Gideon's VIA (GPL-3); T65
  (BSD-style).
- **`ti99`**: community port (not by the maintainers) of zeldin's Mega99, LGPL-3.0 (licence text kept in `rtl/src`). No console ROM or GROM is in the
  repository: the person's own set (MAME `ti99_4a`) is read from the stick. Marked `experimental`.
- **`atari800`**: Watson's non-commercial terms in the upstream files, three LGPL files, our GPL-3.0-or-later code.
- **`c264`, `nes`, `snes`, `zx`, `zxnext`, `cpc`**: GPL v3 / v2-or-later upstream, BSD-style T65/T80/YM2149; `cpc`: two files
  without terms (`rslatch.v`, `Amstrad_MMU.v`); `nes`, `snes`: several upstream files without a header (the SNES files have none).
- **`amiga`, `amiga1200`, `apple2`**: files without licence text (Minimig-AGA, `nanomig.v`, the Apple //e machine files,
  `R65Cx2.vhd`); FX68K GPL-3; TG68K Lesser GPL 3+; the NanoApple2 repository declares GPL v3, NanoMig declares none.
- **`null`**: ours only.
- Our own code: GPL-3.0-or-later (`LICENSE`, ADR-0029); SPDX headers only in some files (T68).
- ROMs of Commodore, Atari, Apple, Amiga, Amstrad and Sinclair machines: not in a bitstream (section 10).

`designs/ddr2_test` and `designs/hdmi_test` (bring-up designs) contain only our own code and AMD/Xilinx IP.
