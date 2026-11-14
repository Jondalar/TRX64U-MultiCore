# cores/c128 provenance (T113)

Spec: `docs/specs/T113-c128-core.md` (D1, §5). Third-party files keep their headers and are TREX-internal until the licence question is settled
(T113 Q9: "We use them, as all MiSTer and daughter projects do too", translated). Sources were read outside this repository
(MiSTer-devel/C128_MiSTer at `9bc3c5d`, 2026-09-10); `releases/` and `mra/` of that repository, which hold ROM images and bitstreams, were never fetched.
`tools/check_c128_tree.py` (bench `sim/run_scan.sh`, scenario S1) checks this file against the tree: every source file is listed here, the third-party
files carry their header, no ROM-like file is present.

## Taken from MiSTer-devel/C128_MiSTer (header kept)
- `vdc/vdc_top.sv`, `vdc/vdc_signals.sv`, `vdc/vdc_signals_h.sv`, `vdc/vdc_signals_v.sv`, `vdc/vdc_ramiface.sv`, `vdc/vdc_video.sv`: `rtl/vdc856x/`
  (Erik Scheffers, no licence text). Edits: `output` ports that are assigned in procedures become `output reg` (`vdc_top.sv` `db_out`;
  `vdc_ramiface.sv` `reg_ua reg_wc reg_da reg_ba busy rowbuf attrbuf charbuf dispaddr`); `vdc_video.sv`: the scalar-port variant under
  `ifdef VERILATOR` is now under `VDC_SCALAR_BUFS` (never defined; it did not match `vdc_top.sv`'s array connection under Verilator 5).
- `c128_mmu8722.vhd`: `rtl/mmu8722.vhd` (same author/terms). Edits: `crBank` expression parenthesised (width mismatch), port `do` renamed `dout`
  (`do` is a Verilog keyword), the `case page(15 downto 14)` ends in `when others` (GHDL), the negative-control define `C128_MUT_ONEBANK` in `c128_map.v`.
- `z80/cpu_z80.vhd`: `rtl/cpu_z80.vhd` (Erik Scheffers). Edits: `work.T80pa` instantiated as an entity, port `do` renamed `dout`, the reset inverted in a
  signal (`RESET_n => reset_n_s`; GHDL: "actual expression must be globally static").
- `z80/T80.vhd`, `T80_ALU.vhd`, `T80_MCode.vhd`, `T80_Pack.vhd`, `T80_Reg.vhd`, `T80pa.vhd`: `rtl/t80/` (Daniel Wallner 2001-2002, BSD-style; Sorgelig 2018).

## Ours (written for this project, or copies of our own tree)
- `vdc/vdcram.v` (replaces the Intel altsyncram instance), `vdc/vdc_cpu.v` (the CPU-visible part of the VDC, variant 2, not used by the core).
- `c128_map.v`, `c128_x.v`, `c128_keys.v`, `c128_regs.v`, `c128_rgbi.v`, `c128_romc.v`, `c128_rome.v`, `c128_chr.v`, `c128_joyswap.v` (T157), `c128_act.v` (T194): ours.
- `c128_ramd.v`, `c128_ddrx.v` (T140 D25/D27, the RAM in the DDR2; `c128_ddrx.v` replaces `c128_rome.v` in that build): ours. `c64_mem_c128d.v`: copy of `c64_mem_c128.v` with the RAM behind `c128_ramd.v`;
  `dma_win_c128.v`: copy of `cores/c64u/dma_win_c64u.v` that waits for the RAM port's answer.
- `c128_pixw.v`: copy of `cores/c264/rtl/trx/c264_pixw.v` (ours) for the VDC's 640 x 200 picture, every line written twice, three buffers, 7 bit word counter.
- `vicii_c128.v`: copy of `vendor/icepi/vicii-kawari/hdl/vicii.v` (Kawari, GPL-3.0-or-later). One change: the dot phase (`dot_rising`) stops while `stall` is up and the enable
  the dot-clocked logic sees is 0 then (the original lets raster_x count on while only the PHI generator stops: a stall of the ROM line cache moved the VIC against the CPU).
- `c64_bus_arbiter_c128.v`, `c64_pla_c128.v`, `cpu6510_c128.v`: copies of `vendor/icepi/c64_bus_arbiter.v`, `c64_pla.v`, `cpu6510.v` (GPL-3.0-or-later) with the
  C128 map, the 2 MHz bus cycles, the 8502's port bit 6.
- `c64_top_c128.v`, `c64_core_c128.v`, `c64_mem_c128.v`, `layer2_c128.v`: copies of the `cores/c64u` files, cut down and extended.
- `mono/c128_yc.v` (T140, 2026-10-30): derived from `vendor/mister/yc_out.sv` (Mike Simone, GPL-2.0-or-later): the same colour matrix, carrier, PAL switching
  and burst, rebuilt with the constant multiplications in DSP48s, a 26-bit carrier and fewer register stages. `mono/c128_avout.v` (T140): ours.
- `mono/top.v` (T140 D1, T164 WP18): copy of the host copy's `top.v` (`mono/host/top.v`, the A500's set) with the A/V port added and `c64_type` 1 (`mono/top.v.diff`). The Ethernet cut of T140 Q2b is the host's `NO_NET` / `g_net`; the C128 fork of `ultimate_sys.vhd` is gone.
- `mono/` (the flat build `build_mono.tcl`, `stubs/`, `reports/`), `sim/` (benches), `core.toml`, `core_sources.tcl`, `c64_core_ooc.xdc`.
- Unmodified files are read in place from `vendor/icepi`, `cores/c64u`; nothing there is edited.
- Variant switches: `TAPE`, `SOCK_AUDIO`, `Z80`, `SID2`, `VDC64`, `ROM_P2`, `RAM_P2`, `ROM_CACHE`, `MONO`, `VDC_WHOLE`; negative-control defines `C128_MUT_ONEBANK`,
  `C128_MUT_FASTC64`, `KEYS_MUT_NOCBM`, `ROMC_MUT_*`.
- VDC benches (S8, S9): `sim/run_vdc.sh` + `sim/vdc_model.py` (independent model from the 8563 programmer's reference), `sim/run_pixw.sh`.

No ROM byte is in this tree.
