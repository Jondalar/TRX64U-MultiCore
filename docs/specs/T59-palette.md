# T59: Custom palettes as in Gideon's core

Status: ✅ done 2026-10-07 (on integ-m1 and main; CI run 514 green, board test passed). Spec written before the RTL (2026-10-07).
Clean room: the scaler and palette RTL of Gideon's core were not opened; everything here is read from the app's C++ (`u64_config.cc`, `control_target.cc`, `palette_command.h`, `u64.h`) and from our own RTL.

## 1. Write paths (all end in the same bus writes)

| Path | Code | Result |
|---|---|---|
| Config item "Palette Definition" (`CFG_PALETTE`), applied at boot and on every settings apply | `U64Config::effectuate_settings`: a file name -> `load_palette_vpl`, empty -> `set_palette_rgb(default_colors)` | `program_palette_rgb` |
| A `.vpl` file from the file browser / menu | `load_palette_vpl` -> `set_palette_rgb` | `program_palette_rgb` |
| UCI target `$04` (control), commands `$51` GET, `$52` SET all 16, `$53` SET one colour, `$54` RESET (`control_target.cc`, `palette_command.h`; Xander's mandelbrot-upic) | `set_palette_rgb`, `set_palette_color`, `reset_palette` | `program_palette_rgb` / `program_palette_color` |
| REST | none: no endpoint touches the palette; a config change through `PUT /v1/configs/...` reaches the config item above | as the first row |

`program_palette_rgb` writes, with byte stores, `C64_PALETTE + 4n + {0,1,2}` = R, G, B of colour n (byte +3 skipped), then the YUV table at `C64_PALETTE + 0x400 + 4n` (from `rgb_to_yuv`), and under `U64 == 2` (our app build) also `U64II_HDMI_PALETTE + 4n` = 0x10145000 (the overlay's own palette, already implemented in `overlay.v` since the menu overlay, unchanged here). GET (`$51`) answers from the app's shadow copy `active_palette` (or `default_colors`), never from the hardware: the registers are write only.

## 2. Where the writes landed before (finding)

`U2P_IO_BASE` is 0x10100000 on our target (Makefile `-DU2P_IO_BASE=0x10100000`, not Gideon's 0xA0000000), so `C64_PALETTE` is **0x10180800** (IO address 0x180800). `ultimate_sys.vhd` decoded 0x1800xx (core config page 3), 0x1810xx, 0x1818xx, 0x185xxx and 0x188000-0x18FFFF; 0x1808xx matched no region (`r_none`): every palette write was **dropped**. The HDMI-palette copy at 0x10145000 did reach `overlay.v`, which is why only the menu overlay ever followed a palette. So the app needs no address change and no capability bit; the decode was missing.

## 3. Design

- `ultimate_sys.vhd`: new region `r_pal` = 0x180800-0x180FFF. Outputs `pal_we`, `pal_addr[5:0]`, `pal_wdata[7:0]` (cpu_clk): a write with `address[10:6] = 0`, i.e. entries 0-15 at 4n+0..3 (the pad byte +3 is passed and ignored by the RAM). The YUV table (+0x400) and 0x840-0x8FF / 0xC40-0xFFF are accepted and dropped. Reads give 0 (write only).
- `pal_ram.v` (new): 16 x 24 bit in distributed RAM (R, G, B byte arrays like `overlay.v`), write port in the bus clock, asynchronous read port by colour index, reset contents = Colodore (`initial`, as in `overlay.v`), so nothing changes until the app writes. Byte writes, immediate effect.
- Consumers (every place that turns a VIC colour index into RGB on the HDMI path):
  1. `vic_scaler.v`: `palette()` replaced by a `pal_ram` read at stage s2 (the same place and depth as the old ROM function).
  2. `c64_text.v` (freeze menu text layer): its own copy of the function replaced by a second `pal_ram` fed with the same bus (stage E3).
  3. App `trx_video.cc` (pixel layer, still and time-warp bar): its hard-coded `c_palette` is now the app's palette (`u64_get_palette_rgb`, refreshed each render pass, and at begin of the time-warp view); remade colours invalidate the mixed still colours.
  Not consumers: `pix_layer.v` and the UDP video stream (`udp_stream.v`) carry the 4-bit colour index, not RGB (the stream's receiver applies its own palette, as with Gideon's); the still capture stores indices; `overlay.v` has its own palette written through 0x10145000 (already working).
- Analog / YUV: our board drives HDMI only (no analog video out in `top.v`), so the YUV table at +0x400 has no consumer and is dropped on purpose. If an analog path is ever built, the table is the place to hang it.
- Update timing: **immediate** (each byte write is visible to the next pixel read). The app writes the whole table in a burst, so a frame in progress can show a mix of old and new colours once; Gideon's behaviour (immediate or at the next frame) cannot be read from the app: board check B1 below.
- Clock domains: the write port is in `cpu_clk`, the read in `pix_clk`; both are asynchronous groups in `pins.xdc`, so the path RAM -> `rgb` register is not timed (as the overlay's palette already is). The value is quasi-static; a read in the same cycle as a write may return a mix of the old and new byte for **one pixel**, nothing latches it. No metastability reaches a state machine: the read result goes only into the `rgb` data register.
- Reset / default: Colodore until the app writes (it does at boot: `effectuate_settings` writes `default_colors` or the configured .vpl). A bitstream reload without an app restart shows Colodore.

## 4. Default blue (decision, [ADR-0046](../adr/0046-palette-default-is-the-apps.md))

The RTL reset palette (and the former hard-wired palette) has colour 6 = **2E2C9B** (Colodore). The app's `default_colors` has **2C29B1** (most entries differ slightly: 1 = F7F7F7 against FFFFFF, 2 = 8D2F34 against 813338, 4 = 9835A4, 5 = 4CB442; 0, 11, 12 and 15 are equal). After this change the app's default decides: once the app has written its boot palette the picture, the freeze text layer and the freeze menu use `default_colors`, as on Gideon's core. Visible effect: slightly different blues/whites after the first app start; between the bitstream load and the first app write Colodore shows.

## 5. Cost and timing

- LUT: two `pal_ram` instances (3 x 16 x 8 distributed RAM each: about 4 RAM32M = 16 LUT per instance) and the decode (about 5 LUT); the removed ROM functions were about 30-40 LUT each. Expected net: about 0. BRAM: 0. Real numbers: section 7.
- Timing risk: the video path keeps its depth (index register -> RAM read -> `rgb` register); the RAM read is a LUT6, the same level as the ROM function.

## 6. Test plan

Sim:
- `tb_scaler` (iverilog, `ci/sims.txt` bench `scaler`, PAL and `-DV60`): frames 0-2 must show Colodore (reset contents), then the bench writes a different 16-colour palette through the bus port in an unrelated clock (R, G, B, and 0xFF into the pad byte) and frames 3-4 are checked against it for every active pixel; the pattern covers all 16 indices. The existing checks (counters, 1920 x 1080, line lengths, hs, jumps) run over all frames.
- `tb_input` step 6 (GHDL, bench `input`): the decode: 0x180800 and 0x18083E reach `pal_*` with the right address and data, entry 16 (0x180840), the YUV table (0x180C00), 0x180FFF, the core config page (0x1800FF) and 0x181000 do not.
- The Verilator benches that contain `c64_text` (statebus, ring) tie the new ports idle.
Board (docs/archive/board-tests/board-tests-pending.md, row T59-PAL): B1 mandelbrot-upic colours (UCI $52 palette); B2 a `.vpl` from the file browser and from the config item; B3 compare the C64 picture with Gideon's core on the same palette; B4 the freeze menu and time-warp bar follow the palette; B5 UCI $54 reset restores the default palette.

## 7. Status and history

Dated entries, newest first.

- 2026-10-07 evening, **done** (Alex: "T59 auf ✅"): CI run 514 (integ-m1 e2b2ca4) green incl. the tier-2 benches; c64_basic 26019 LUTs (+66 vs run 504), BRAM 68.5 (+0), WNS +0.028 ns (run 504: +0.076). Dropped by Alex: the Timewarp bar check in a custom palette and the comparison with Gideon's core.
- 2026-10-07 evening: **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED** (T59-PAL 1, 2, 4, 5, 6): mandelbrot-upic shows its own palette; the freeze stays behind the menu in the custom palette; a .vpl (/USB1/green.vpl, green mono) applied through the file browser "Apply Palette" takes effect at once including the menu; "Palette Definition" reset to default gives the normal colours. **Open:** the Timewarp bar in a custom palette (Claude alone, later) and the comparison with Gideon's core (3).
- 2026-10-07: CI run 510 (push, tier 1) and 512 (dispatch, same sources, everything cached): lint, sims (scaler PASS with the palette test), app (compiles) and the c64u bitstream green; c64_basic WNS +0.028 ns (integ-m1 run 508: +0.076, placement noise). LUT/BRAM: the CI log does not print them (reports stay in the NAS cache), so no number yet. **Not run in CI:** the tier-2 benches (`input` with the new GHDL decode step 6, `statebus`, `ring_trace_core`, `ultsys`) because tier 2 runs only on main, integ-* or a `[sims]` commit message (which fails the lint's own select.sh check, and a dispatch on a feature branch is still tier 1); they run when the branch is merged into integ-m1.
- 2026-10-07: built (branch `t59-palette`, app `t59-palette-app`); sim `scaler` PASS in PAL and V60; GHDL `input` step 6 written (runs in CI). CI numbers: see below once the run is in.
