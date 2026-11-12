# T123 — The flash holds only the host: the core partition carries a null core, not the TRX64 core

**Status:** BUILDING (2026-10-24).
**Repo:** TRX64U-MultiCore — a new core folder `cores/null/` (never routed into the host), the packaging stage (`ci/update.sh`,
`tools/make_update.sh`), `INSTALL.md`. The host (`host/`, `contract/`) does not change.
**Number:** T123.
**Origin:** Alex, 2026-10-24, reading `INSTALL.md` ("flash 0x000000: the full bitstream ... holds layer 1 (the host) and the TRX64 core"):
"Wait. So without .cor files the trx64 core is there anyway?" - "That we need it to build the host bitstream was clear to me. But why does
it have to go into the flash?!" - "So build the empty guest once, as it were" - "Write a spec and then build it." (translated)
**Depends on:** T94 (host/core split, cached host checkpoint), T79.1 (the contract), T84 (host key in AXSS).
**Milestone:** M3.

---

## §1 The problem

The `.cfw` writes the full bitstream `c64_basic.bit` to flash 0x000000. It is the host stage's routed design **with the reference core
`trx64`** in the partition (`ci/bitstream_layers.sh:257`, `cp $coredir/${core}_full.bit $out/c64_basic.bit`). So every installed
machine carries the TRX64 C64 core in its flash although the user starts every core, `trx64` included, from a `.cor`; the menu has no
entry for the resident core. Nobody decided this; the flow simply writes the checkpoint it routed.

- **P1 An empty partition is not possible as such.** The host stage already tries a grey box (`host/build.tcl:449`, `host.bit`):
  every cached host since T87 has none, the current one (`host-08872197d14e48ed`) fails with
  `ERROR: [DRC HDPR-72] ... Reconfigurable module 'l2' has port 'ui_clk' driven by dedicated routing ...`. A grey box buffers the
  clock port with a LUT; the static side was routed with a dedicated clock net into l2.
- **P2 The boot menu needs the resident core.** Read from the code (2026-10-24): the host app reaches the pixel layer's registers
  (`host/pix_layer.v`) through the core (`px_re`, `px_we`, `px_rdata` on the contract; `cores/trx64/trx_io.v` page 6 decodes them),
  identifies the host by `TRX_ID` "TRX2", `TRX_CAPS2` bit 1 and `BUILD` (`host-app/software/hostapp/hostapp.cc:31`, `cor.cc:217`:
  without them it checks no `.cor` against the host), takes the flip/VSYNC interrupt the core raises from `px_ev_flip`/`px_ev_vsync`,
  and the host's scaler locks to the core's `pix_*` raster. A plain empty partition gives a menu without a picture.
- **P3 Packaging.** `ci/update.sh` packs `c64_basic.bit` into both update files; it must pack the null core's full bitstream instead,
  for this host's checkpoint, and keep the AXSS word (host key) of the host.
- **P4 Nothing else changes.** The host's routing is the cached one (locked), so every `.cor` built against it still fits; the host pin
  (`tools/host_pin.sh`) stays; the Ultimate tester file (`trx64_update_*`, the TRX64 monolith line) keeps its bitstream.

## §2 Scenarios

| # | Part | Scenario | Proof | Control (must be able to fail) |
|---|------|----------|-------|--------------------------------|
| S1 | P2 | The null core answers what the host app reads: `TRX_ID` "TRX2", CAPS with VIDEO and PX565, CAPS2 bit 1, `BUILD` = the `build` input, pixel-layer page reads and writes pass to `px_*`, the flip/VSYNC interrupt rises and clears as the host app's handler expects, every other address of the window is acknowledged (no 1 ms bus timeout), `pix_*` runs a raster the scaler locks to | bench `null_regs` (iverilog) driving the core's I/O window as the app does | `ctl_noid` (ID byte wrong), `ctl_nopx` (px_we never pulses), `ctl_noack` |
| S2 | P1 | `core.tcl` builds `null` against the cached host: routed, timing met, `null_full.bit` with the host's AXSS | CI `bitstreams` log `=== core null: WNS` | - (a failed build writes no bitstream) |
| S3 | P4 | The host part of `null_full.bit` equals that of `trx64_full.bit`: same static routing | routing/placement dump of every non-l2 net and cell from both checkpoints, compared (empty diff) | the compare with one line changed reports it |
| S4 | P3 | The `.cfw` packs `null_full.bit`: `make_update.sh` checks its header and AXSS as before | `update` stage log, `.cfw` checker | - |
| S5 | P2, P4 | Board (Alex): cold start from flash, the boot menu shows, host key line `host 08872197` in the menu, every core in the list starts, "return to host" comes back to the menu | board step | - |

## §3 Decision

A null core `cores/null/` (`experimental = false`, no app, never in the boot menu: `core.toml` says it is the flash filler and
`ci/lib.sh` keeps it out of the `.cor` list). `layer2_null.v` holds only the menu's services of P2 - a register block with ID, CAPS,
BUILD and the page-6 pass-through, the interrupt, the idle raster - and drives every other contract output to its idle value
(T79.1 rule 5). Every clock port has at least one register load, so the boundary nets stay dedicated (P1). It is built by `core.tcl`
against the cached host like every core; `ci/update.sh` packs its `null_full.bit` into the host update file
(`trx64host_update_*`). `INSTALL.md` says the flash holds the host and an empty core.

## §4 Out

- The host stage's own grey box attempt (`host.bit`) stays as it is (host frozen).
- The tester file `trx64_update_*` keeps the TRX64 bitstream (the TRX64-Ultimate line).

## §5 As built

### 5.1 Audit: what the host boot app asks of the core partition (2026-10-24, read from the code)

Linked into `hostapp` (`host-app/target/u64ii/riscv/hostapp/Makefile`): the C64 side is gone; what reaches a region that `ultimate_sys.vhd`
routes into the partition (`r_cw` = 0x101A0000-0x101AFFFF, 0x1004C000 EEPROM window, 0x10048000 sampler window; `r_drive`, `r_gcr`, `r_dma`,
`r_sidcv`) is only the TRX IO block. Everything else the app touches (ITU, ICAP 0x10060600, SD, flash, USB, I2C, U2P/`C64_*` registers,
`MATRIX_KEYB`, `U64II_KEYB_JOY`, the HDMI/video mode registers) lives in the host.

| Reader | Address (TRX_IO_BASE = IOBASE + 0x1A0000) | What it needs | Served by |
|---|---|---|---|
| `trx_io_caps()` (`trx_io.cc`) | ID 0x000-0x003, CAPS 0x004 | "TRX2"; the CAPS byte is cached | `null_regs` 0x54 0x52 0x58 0x32, CAPS 0xC0 |
| `trx_lv_available()` (`trx_lv_port.cc`) | CAPS | `TRX_CAPS_VIDEO` (0x40) and `TRX_CAPS_PX565` (0x80), else no menu at all | CAPS = 0xC0 |
| `running_host_key()` (`hostapp.cc`) | CAPS2 0x005 b1, BUILD 0x008-0x00B | the host key (the AXSS word, `usr_access`) refuses a `.cor` of another host (`cor.cc`) | CAPS2 = 0x02, BUILD = `usr_access` |
| `trx_io_caps3()` / `resid8` | CAPS3 0x006 | only read when asked; 0 is "no reSID8" | reads 0 |
| `trx_irq_attach()` / `trx_irq_enable()` | IRQ_EN 0x010 (RW), IRQ_CAUSE 0x014 (write 1 clears), ITU high interrupt 2 | the line is `cw_irq` | `irq = \|(irq_en & cause)`, `cause \|= {flip, vsync}` |
| `flip_irq()` (`trx_lv_port.cc`) | clears with `ioWrite8(TRX_IRQ_CAUSE, act)`; enables `TRX_IRQ_FLIP` 0x80 only | `px_ev_flip` (b7) sets the cause; `px_ev_vsync` (b6) is set, never enabled by the app | `null_regs` (event in the cycle of the clear stays set) |
| LVGL port (`trx_lv_port.cc`) | page 6, 0x6080-0x60A0: PX_CTRL, PX_FRONT, PX_BACK, PX_SIZE, PX_STRIDE, PX_POS, PX_GALPHA, PX_KEY (32-bit, byte 3 commits) | the registers are the host's `pix_layer.v` (`cw_addr[11:0]` goes to it directly); the core only has to raise `px_we` / `px_re` for page 6 and return `px_rdata` one clock after the address | `px_we`/`px_re` = request & page 6 & not the EEPROM window; `rdata = q_p6 ? px_rdata : q` |
| `trx_io_slim()` | page 6 0x60C0 (TX_CTRL, `c64_text.v`) | read through the same page-6 path (`px_rdata = pxl_rdata \| tx_rdata`) | page 6 pass-through |
| coreboot/legacy hand-over | `trx_lv_yield_layer()` writes PX_CTRL/PX_GALPHA, `ioWrite8(ITU_IRQ_GLOBAL, 0)` | page 6 and the ITU (host) | as above |
| any other address of the window | none in the host app (older Gideon code probes EEPROM 0x1004C000, sampler 0x10048000, page 4/8) | an access without `ack` runs into the host's bus timeout | every other access: `ack` one clock after the request, data 0 |

Not used by the host app, so not served: DDR_CAL 0x020-0x027 (the boot ROM waits for DDR2 before any app code), the reSID8 block 0x040-0x04D,
pages 1-5, 7, 8, `r_dma` (no C64 class is linked; a stray DMA byte ends at the host's `dma_tmo`), `r_sidcv`, `dbg_*` (JTAG: `trx_j_rvalid` = 1, data 0).
Not touched by the host app: audio (`audio_l/r` 0, the mixer is the host's), DDR2 (`c_*`, `e_*` idle: the pixel layer reads the menu buffer from the
host's own DDR2 client), the cartridge port, the IEC bus, the 1541 and GCR windows (answered with `ack` for safety).

The picture: the host draws the menu with `pix_layer.v` over the scaler's output. The scaler (`vic_scaler.v`, `vclk` = `sys_clk`) needs a
`pix_x`/`pix_y` raster from the partition to lock; without it the output has no timing and the layer has no frame to draw over (P2). The raster of
the NES and ZX cores is used unchanged: 504 dots of 4 `sys_clk`, 312 lines, `pix_color` 0 (PAL-shaped; the output mode is the host's, T105).
`c64_ce` must be 1: it is the clock enable of the host's `BUFGCE` for `c64_gclk`.

Size: what the app needs is a 12-register block, a raster and one flop per clock; no RAM, no DSP, no change to the host. Measured in 5.5: 75 LUT and 47 FF
after synthesis. After implementation the partition holds 2,105 LUT, and 2,055 of them are not logic: Vivado puts one LUT on every constant output bit of the
partition (2,167 partition pin locations) to drive it. They cost nothing in the flash and nothing the host or another core could use (the partition is
reserved for a core either way); a flop per constant bit (T87 style) would turn them into 2,055 FF and was not done.

Contract outputs and the idle value the null core drives (the decoupler's value in `host/top.v` where it has one; the NES core's tie-off otherwise):

| Output(s) | Idle | Why |
|---|---|---|
| `cw_ack`, `cw_rdata`, `cw_irq`, `px_we`, `px_re` | the register block | S1 |
| `pix_color`, `pix_x`, `pix_y` | the raster, colour 0 | the scaler locks |
| `drv_io_ack`, `gcr_io_ack` | `ack` one clock after the request | no timeout |
| `drv_io_rdata`, `gcr_io_rdata`, `drv_io_nc`, `gcr_io_nc` | 0 | |
| `c64_ce`, `trx_j_rvalid` | 1 | gate of `c64_gclk`; JTAG read completes (decoupler value) |
| `SLOT_ADDRDIR/ADDROEn/BA/DATADIR/DATAOEn/IO1n/IO2n/ROMHn/ROMLn`, `slot_a`, `slot_ahi_t`, `slot_d_o`, `slot_d_t`, `slot_rw` | 1 | cartridge port released |
| `SLOT_DOTCLK`, `SLOT_PHI2`, `slot_rst_o` | 0 | |
| `IEC_ATN_O/CLOCK_O/DATA_O` | 0 | |
| `c64_atn_o`, `c64_clk_o`, `c64_data_o`, `drv_clk_o`, `drv_data_o`, `drive_leds_n` (111) | 1 | bus released |
| `c2n_*_en/_o`, `paddle_*`, `sid_sock_addr`, `pp_*` | 0 | pads released (T103) |
| `sid_sock1_n`, `sid_sock2_n` | 1 | sockets deselected |
| `c64_halted`, `gate_frz`, `cart_active`, `ap_req`, `c_en`, `e_en`, `*_wren`, `dbg_*`, `d7ff_*`, `dma_done_tgl`, `drv_tgl`, `drv_we`, `reu_*`, `smp_f_tgl`, `win_*` | 0 | nothing running; `c64_halted` 0 and not the swap value 1, because 1 would blank the scaler window and mute the mixer for good |
| `audio_l`, `audio_r`, `c_addr`, `c_cmd`, `c_wdf_*`, `e_*`, `drv_addr/be/wdata`, `ap_addr/wdata`, `ram_rdata`, `sid_lv_data`, `smp_f_addr`, `tx_view`, `cram_rdata`, `dma_rdata_q`, `drive_dbg`, `trx_j_rdata`, `win_*` buses, `c64_leds` | 0 | |

Clocks of the contract: `cpu_clk`, `sys_clk` (loaded by the register block and the raster), `ui_clk`, `mclk`, `c64_gclk`, `sid2x_clk` and `adc_bclk` each get
one DONT_TOUCH toggle flop (`null_clk_load`, nothing reads it). Not clock-loaded: `mclk_rst`, `sys_rst`, `ui_rst`, `cpu_rst` (resets), `drv_t4/t16/t1k` (strobes).

### 5.2 The null core and the packaging (built)

- `cores/null/`: `layer2_null.v` (ports generated from the contract; `tools/contract_ports.py --check`: "null matches (247 ports)"),
  `null_regs.v`, `core_sources.tcl` (OOC synthesis, sets `l2_dcp`), `core.toml` (`flash_filler = true`, `experimental = false`, `app = "none"`, order 98).
- `flash_filler` is a field of `tools/core_toml.py`; `list` still names every descriptor (the layered build, the lint, the contract check and the
  cache keys need the null core), `list-cor` leaves it out. `tools/pack_cores_mc.sh` and `ci/package.sh` use `list-cor`, so there is no `null.cor`; the release
  tools take their `.cor` list from the package, and `tools/test_make_release_mc.py` skips the filler. The bitstreams stage builds it like every core
  (`build_layered` loops over `layer_cores`): `=== core null: WNS` in the log, `null_full.bit` and `null_partial.bit` in the package.
- `ci/update.sh` takes `null_full.bit` from the package manifest, checks that its AXSS host key equals `c64_basic.bit`'s (`cor_pack.py hostkey`), and
  packs it into `trx64host_update_*` through `build_update_host`; `tools/make_update.sh` runs `trx_pkg.py blobs`, which finds the AXSS word. The tester
  file `trx64_update_*` keeps `c64_basic.bit`.
- `INSTALL.md` (`tools/release_notes_mc.py`), the comment of `tools/make_release_mc.sh` and `docs/manual/02-installation.md` say: flash 0x000000 holds the
  host with an empty (null) core.

### 5.3 S1: bench `null_regs` (builder, container `trx64-sim-env:4`, 2026-10-24)

`sh cores/null/sim/run.sh` (`ci/sims.txt` tier 1 `null_regs`, `ci/sims.deps` `cores/null`): `layer2_null` driven through `cw_*` as the app does, with a
stub of `pix_layer.v` behind page 6. Result: `PASS null_regs (189 checks)`. Controls (same bench, one line of `null_regs.v` broken, the pattern checked
to match): `ctl_noid` (ID byte 0 wrong) `FAIL ID byte 0`, `ctl_nopx` (`px_we` never) `FAIL page 6 write: px_we in the cycle of the request`,
`ctl_noack` (no `ack`) 107 of 189 checks failed. Also checked: the raster frame is exactly 504 x 4 x 312 `sys_clk`, every clock load toggles, the idle values
of the outputs that matter, yosys `check -assert` clean with `-noautowire`.

### 5.4 S3: the static part of the null image is the host's (`tools/static_diff.sh`)

`tools/static_dump.tcl` writes, from a routed checkpoint, one line per primitive cell outside `l2/` (LOC, BEL; an I/O buffer LOC only: the two
checkpoints report the BEL of the DQS `OBUFTDS` differently without the pad having moved), one line per net with no pin on the `l2` cell and no constant
(its whole `ROUTE`), and for the nets that do touch `l2` (and VCC/GND, which reach the core's cells too) one line per routing node outside the
partition's footprint. `tools/static_footprint.tcl` defines the footprint: per tile row the first and last column of the sites of `pb_l2`, two columns
wider, closed over the clock-region break rows. `tools/static_diff.sh <host static_routed.dcp> <core_routed.dcp> <outdir>` dumps both (two Vivado launches),
sorts, diffs; of the boundary nodes that differ, `tools/static_classify.tcl` drops those with a wire in the footprint (a long line is named after one tile
and runs six or eighteen tiles away: `INT_L_X20Y104/LV_L18` of `l2_dma_done_tgl` was the one such node in the first full comparison). Left out on purpose:
the clock delivery to the loads (`HCLK_LEAF_CLK_*` nodes follow the loads of the core: trx64 loads every clock region, the null core a few), so a changed
static leaf-clock route is not seen; the cells' placement is compared in full. Controls: three copies of the reference dump with one cell line, one net
line, one node line changed; each must be reported as exactly one changed line (`--selftest <dump>` runs them without Vivado).
`ci/bitstream_layers.sh` runs it after the core stage of every `flash_filler` core, together with a check that `<core>_full.bit` carries the host key
(`cor_pack.py hostkey`) as its AXSS word; a failure fails the core, the report goes into the package as `null.static_diff.rpt`.

### 5.5 S2 and S3: the trial build (builder, 2026-10-24, host `host-08872197d14e48ed`, `vivado-env:2024.1`)

`host/core.tcl` with `TRX_CORE=null`, `TRX_HOST_KEY8=08872197`, against the cached `static_routed.dcp`; no CI container was running; a few minutes.

- `=== core null: WNS 0.039 ns` (TNS 0, WHS +0.050, 27,801 endpoints), `=== core null: pr_verify passed`, `DFX DRC finished with 0 Errors`; no HDPR line,
  no `Route 35-341`, `T87 (core, before/after opt_design): 0 layer-2 output pins on a constant net`.
- Synthesis (OOC): 75 LUT, 47 FF, 0 RAMB, 0 DSP. In the partition after implementation: 2,105 LUT / 47 FF (`(l2)` 2,055 LUT = the tie LUTs of the constant
  outputs, `regs` 45 LUT / 20 FF, each `null_clk_load` 1 LUT / 1 FF). Whole design 11,902 LUT, of which 9,797 are the host's.
- `null_full.bit` 1,436,569 bytes (its header length), `null_partial.bit` 1,016,414 bytes; `cor_pack.py hostkey` and `trx_pkg.axss` both read `08872197`, the host
  key; the file is below the 0x220000 flash window. (The cached `trx64_full.bit` is 1,783,161 bytes: the bitstream is compressed, an empty partition packs smaller.)
- S3, `static_diff.sh` on `static_routed.dcp` against `core_routed.dcp`: `the static part is the same: 27544 cells, 5789 net routes, 85228 boundary-net nodes
  compared, 0 differences`, controls C, N and B reported, `PASS static_diff`. The first comparisons (before the footprint was by tile row and column, and
  before the leaf-clock nodes were set aside) differed in clock-tree leaf nodes, break-row tiles and one long line; each was explained by the core's own routing,
  none by a static cell or a static-only net.
- Not run: the host update file itself (`ci/update.sh` needs the Gitea package and the RISC-V toolchain image), S4 beyond the AXSS/length check of the image above,
  S5 (board). The first CI run reads `=== core null: WNS`, the `null.static_diff.rpt` and the update stage's `host key ... in both images` line.

### 5.6 CI 957: the null image carried another host key than c64_basic.bit (fixed, 2026-10-24)

`update` stopped with `null_full.bit carries host key 'cee54845', c64_basic.bit '08872197'`. Cause, read from the code and the builder's cache:
`build_core` (`ci/bitstream_layers.sh`) passed `TRX_HOST_KEY8` = the first 8 hex of the host ENTRY key `H` (`host_key`, `ci/lib.sh`), and the
flash-filler check compared against the same value. The entries `host-cee54845915a45a6` and `host-08872197d14e48ed` hold one build (same `static_sha`
5a0ebcee..., same `from` 28e9fe53, both written 2026-10-17 09:05); the second is the first under its pre-`ba2ebf33` name (that commit took `VENDOR.md` out of
`tree_key` and so moved every key; the entry was re-keyed in place on 2026-10-19, manifest rewritten, files untouched). The key function now yields `cee54845`,
but `trx64_full.bit` in the entry (= `c64_basic.bit`, and the host every `.cor` is bound to) still carries the AXSS it was built with, `08872197`. The trial build
of 5.5 ran against the entry under its old name, which is why it did not show.

Fix: `build_core` takes the key from the static itself (`host_axss`: `cor_pack.py hostkey` of the host entry's reference-core full image) for `TRX_HOST_KEY8`
and for the flash-filler check; `core_entry_ok` rejects a cached flash-filler entry whose image carries another key (the entry made by run 957 is built again).
`host/`, `contract/`, `host_key` and the host pin are untouched, so no `.cor` changes. A host built fresh still gets `H[:8]` (host stage unchanged), so for it
both rules give the same word. `ci/test_layers_flow.sh` step 9 (host whose static carries `cafe0123` under another entry name; a null entry with a wrong key is no
hit) fails on the old script (`nul_full.bit does not carry the host key of c64_basic.bit`) and passes now.
