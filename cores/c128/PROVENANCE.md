# cores/c128 provenance (T113)

Spec: `docs/specs/T113-c128-core.md` (D1, §5). Third-party files keep their headers and are TREX-internal until the licence question is settled
(T113 Q9: "We use them, as all MiSTer and daughter projects do too", translated). Sources were read outside this repository
(MiSTer-devel/C128_MiSTer at `9bc3c5d`, 2026-09-10); `releases/` and `mra/` of that repository, which hold ROM images and bitstreams, were never fetched.
`tools/check_c128_tree.py` (bench `sim/run_scan.sh`, scenario S1) checks this file against the tree: every source file is listed here, the third-party
files carry their header, no ROM-like file is present.

## Taken from MiSTer-devel/C128_MiSTer (header kept)
- `vdc/vdc_top.sv`, `vdc/vdc_signals.sv`, `vdc/vdc_signals_h.sv`, `vdc/vdc_signals_v.sv`, `vdc/vdc_ramiface.sv`, `vdc/vdc_video.sv`: `rtl/vdc856x/`
  (Erik Scheffers, no licence text). Edits: `output` ports that are assigned in procedures become `output reg` (`vdc_top.sv` `db_out`;
  `vdc_ramiface.sv` `reg_ua reg_wc reg_da reg_ba busy rowbuf attrbuf charbuf dispaddr`); `vdc_signals_h.sv` / `vdc_signals.sv` / `vdc_top.sv` (T113 D1, T257): `reg_cdh` and `reg_hss` wired through, the capture window opens on the dot `ps` where the first character is loaded (`vstart`); `vdc_video.sv`: the scalar-port variant under
  `ifdef VERILATOR` is now under `VDC_SCALAR_BUFS` (never defined; it did not match `vdc_top.sv`'s array connection under Verilator 5).
  `vdc_top.sv` (T257 fault 5): R25[3:0] is taken once per line into `hss_l` (at the end of the last column), and the picture logic reads `hss_l`; the R25 read-back stays `reg_hss`.
  `vdc_signals_v.sv`: the `field` output and the interlaced-bitmap rule at the frame wrap (T113 D11; T257 fault 2: no line is fetched before row 1, so the
  second field carries on right after the first).
- `c128_mmu8722.vhd`: `rtl/mmu8722.vhd` (same author/terms). Edits: `crBank` expression parenthesised (width mismatch), port `do` renamed `dout`
  (`do` is a Verilog keyword), the `case page(15 downto 14)` ends in `when others` (GHDL), the negative-control define `C128_MUT_ONEBANK` in `c128_map.v`.
- `z80/cpu_z80.vhd`: `rtl/cpu_z80.vhd` (Erik Scheffers). Edits: `work.T80pa` instantiated as an entity, port `do` renamed `dout`, the reset inverted in a
  signal (`RESET_n => reset_n_s`; GHDL: "actual expression must be globally static").
- `z80/T80.vhd`, `T80_ALU.vhd`, `T80_MCode.vhd`, `T80_Pack.vhd`, `T80_Reg.vhd`, `T80pa.vhd`: `rtl/t80/` (Daniel Wallner 2001-2002, BSD-style; Sorgelig 2018).

## Taken from eilers/c128Mega65 (T257, header kept; GPL v3, Stefan Eilers and Jeff Flitton, read at `ef48a75`, 2026-11-11)
The side-effect-free read-back of the VDC (their `vdc_top.sv` / `vdc_ramiface.sv` / `vdcram.v` under `CORE/C128_MiSTer/rtl/vdc856x/`, a MEGA65 edit of Erik Scheffers' files):
- `vdc/vdc_top.sv`: the ports `dbg_addr`, `dbg_ram`, `dbg_reg`, the function `regval()` (R0-R37 without a read's side effects) and the `dbg_reg` process (38 = status, 39 = selected register),
  copied from their `vdc_top.sv:290-345`; `status` is a wire there as in theirs. Since T257 fault 3 the `dbg_reg` process runs on our `dbg_clk` (the ungated sys_clk), not on the machine's clock. The read values equal our `db_out` case (checked register by register, 2026-11-23).
- `vdc/vdc_ramiface.sv`: the ports `dbg_addr`, `dbg_do` and the two lines that connect the second RAM port through `shuffleAddr` (their `vdc_ramiface.sv:35-37,110-112`); their `dbg_rd` is not taken.
- `vdc/vdcram.v`: the idea and the port names `addr_b` / `dao_b` of their second read port; the code is ours (it reads every clock, no `rd_b`; since T257 fault 3 port B has its own clock `clk_b`, the ungated sys_clk).
Not taken: `vdc_peek.vhd` (the MEGA65's HyperRAM/REST window), their `vdcram.v` read gating, `dbg_rd`, the `clkdiv` move. The request/answer engine (`c128_x.v`, `vk_*`) and the register window (`c128_regs.v` 100-143) are ours.

## Taken from eilers/c128Mega65 (T170)
- `c128_iecfs.v`: from eilers/c128Mega65 (`CORE/C128_MiSTer/rtl/fpga64_sid_iec.vhd:1189-1202`, commit `ef48a75`, 2026-11-11; GPL-3.0; Stefan Eilers), which carries Erik Scheffers'
  MiSTer-devel/C128_MiSTer (Release 20240904, `5f0bdb4`; file header: Peter Wendrich 2014, Dar 2014, Alexey Melnikov 2021, Erik Scheffers 2022, no licence text). Taken: the
  fast serial wiring equations (`fs_o`, `fclk_o`, `iec_data_o`, `iec_srq_n_o`, `fs_i`, `cia1_flag_n`, `cia1_cnt_i`, `cia1_sp_i`), written again in Verilog in our polarity; no line of
  VHDL is copied, so there is no header to keep. The file was read outside this repository.

## Ours (written for this project, or copies of our own tree)
- `vdc/vdcram.v` (replaces the Intel altsyncram instance; T257: a second read port after the MEGA65 core's, see above), `vdc/vdc_cpu.v` (the CPU-visible part of the VDC, variant 2, not used by the core).
- `c128_map.v`, `c128_x.v`, `c128_keys.v`, `c128_regs.v`, `c128_rgbi.v`, `c128_romc.v`, `c128_rome.v`, `c128_chr.v`, `c128_joyswap.v` (T157), `c128_act.v` (T194): ours.
- `c128_ramd.v`, `c128_ddrx.v` (T140 D25/D27, the RAM in the DDR2; `c128_ddrx.v` replaces `c128_rome.v` in that build): ours. `c64_mem_c128d.v`: copy of `c64_mem_c128.v` with the RAM behind `c128_ramd.v`;
  `dma_win_c128.v`: copy of `cores/c64u/dma_win_c64u.v` that waits for the RAM port's answer.
- `cart_slot_c64u.v`, `cart_fill_c64u.v`, `slot_port_c64u.v`, `c64_turbo_c64u.v` (T257 D11, 2026-11-23): copies of the files of the same names in `cores/c64u/` (first line names the source; content unchanged), so the C128 reads nothing of the c64u tree for them.
  `cart_slot_c64u.v` and `cart_fill_c64u.v` are read but unused while the define `NO_CART_EMU` is set (the mono build sets it); they stay so the define is switchable.
- `c128_pixw.v`: copy of `cores/c264/rtl/trx/c264_pixw.v` (ours) for the VDC's 640-dot picture, every line written twice, three buffers, 7 bit word counter; T257: frames of up to 240 lines, the rows after a shorter frame written black (ours).
- `vicii_c128.v`: copy of `vendor/icepi/vicii-kawari/hdl/vicii.v` (Kawari, GPL-3.0-or-later). One change: the dot phase (`dot_rising`) stops while `stall` is up and the enable
  the dot-clocked logic sees is 0 then (the original lets raster_x count on while only the PHI generator stops: a stall of the ROM line cache moved the VIC against the CPU).
- `c64_bus_arbiter_c128.v`, `c64_pla_c128.v`, `cpu6510_c128.v`: copies of `vendor/icepi/c64_bus_arbiter.v`, `c64_pla.v`, `cpu6510.v` (GPL-3.0-or-later) with the
  C128 map, the 2 MHz bus cycles, the 8502's port bit 6.
- `c64_top_c128.v`, `c64_core_c128.v`, `c64_mem_c128.v`, `layer2_c128.v`: copies of the `cores/c64u` files, cut down and extended.
- `mono/c128_yc.v` (T140, 2026-10-30): derived from `vendor/mister/yc_out.sv` (Mike Simone, GPL-2.0-or-later): the same colour matrix, carrier, PAL switching
  and burst, rebuilt with the constant multiplications in DSP48s, a 26-bit carrier and fewer register stages. `mono/c128_avout.v` (T140): ours.
- `mono/top.v` (T140 D1, T164 WP18): copy of the host copy's `top.v` (`mono/host/top.v`, the A500's set) with the A/V port added and `c64_type` 1 (`mono/top.v.diff`). The Ethernet cut of T140 Q2b is the host's `NO_NET` / `g_net`; the C128 fork of `ultimate_sys.vhd` is gone.
- `iec_bus_c128.v` (T170): copy of `cores/c64u/iec_bus_c64u.v` (ours) with the SRQ line and the trace taps. `c128_iecfs.v` (above), `c128_iectrace.v` (T170 D2, the bus trace): ours.
- `mono/` (the flat build `build_mono.tcl`, `iec_srq_pins.xdc` (T170, the SRQ input's ball), `stubs/`, `reports/`), `sim/` (benches), `core.toml`, `core_sources.tcl`, `c64_core_ooc.xdc`.
- Unmodified files are read in place from `vendor/icepi`, `cores/c64u`; nothing there is edited.
- Variant switches: `TAPE`, `SOCK_AUDIO`, `Z80`, `SID2`, `VDC64`, `ROM_P2`, `RAM_P2`, `ROM_CACHE`, `MONO`, `VDC_WHOLE`; negative-control defines `C128_MUT_ONEBANK`,
  `C128_MUT_FASTC64`, `KEYS_MUT_NOCBM`, `ROMC_MUT_*`.
- VDC benches (S8, S9, T257 S4-S6): `sim/run_vkpeek.sh` (the read-back), `sim/run_vdc.sh` + `sim/vdc_model.py` (independent model from the 8563 programmer's reference), `sim/run_pixw.sh`,
  `sim/run_vdcsw.sh` (T257 fault 5; needs the demo's data, never in the repository) and `sim/run_ruler.sh` (T257 fault 4; needs the real ROMs, local only).

No ROM byte is in this tree.
