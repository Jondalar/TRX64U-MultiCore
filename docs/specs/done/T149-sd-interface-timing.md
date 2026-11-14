# T149 — The SD card reads without errors at 25 MHz, with timing margin from the FPGA

**Status:** DONE (2026-11-14, Alex: "Yes", translated). Closed: proven in daily board use (SD at 25 MHz). Earlier status: PROPOSED (2026-10-28), decided by Alex; scope cut to the large cores the same day (D5); rework on `t148-core-folder`. 2026-11-13 status check: D5 built and merged (sd_spi IOB flops, 25 MHz only in the A1200 and ZX Next flat builds, host 12.5 MHz; d9d239fc0); open: S3 (CI large) and S4 (board, crc errors 0 at 25 MHz).
**Repo:** TRX64U-MultiCore — the host gateware (`host/pins.xdc`, the SPI sampling path of the SD interface, timing
constraints), every large core's flat build (they carry the host's top level), the SD driver speed (`sd_card.cc`).
A host change: approved by Alex (below); `contract/host_pin.txt` updated in the same commit; every DFX core is rebuilt.
**Number:** T149.
**Origin:** Alex, 2026-10-28, after the board showed no SD error at 12.5 MHz (T139 §5) and the choice "stay at 12.5 MHz or
repair the gateware" (translated): "No, do 2, rebuilding the cores is no issue I think. Please, such topics are essential
and must be solved, cleanly. 25 MHz with margin is simply faster; the Amiga 1200 had become very good in speed in the hard
disk emulation."
**Depends on:** T133 (the floorplan change that exposed it), T139 §5 (the measurement).
**Milestone:** M3.

---

## §1 The problem

- SD SPI runs at 25 MHz SCK for reads and writes (`RW_SPEED` 1 at 100 MHz, `sd_card.cc`), a 20 ns half period.
- `spi.vhd:71-76` samples MISO in fabric in the 100 MHz cycle that raises SCK; the pins have no IOB property and no
  input/output delay constraint (`host/pins.xdc:235-241`; `host.timing.rpt` check_timing: no_input_delay 68,
  no_output_delay 125, in the old and the new host alike). Estimated path: SCK clock-to-out + card output delay (up to
  14 ns) + IBUF + route = 21-25 ns against 20 ns.
- Since T133's floorplan the route got worse: wrong bytes now and then (T139 §5). At 12.5 MHz: 0 errors in ~50 MB.
- Parts: **P1** the capture of MISO at a fixed, short, timed path; **P2** SCK/MOSI/SSn outputs at a fixed path; **P3** the
  router sees the paths (constraints); **P4** the same in every flat build; **P5** back to 25 MHz with the CRC check kept.

## §2 Scenarios

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1-P3 | Vivado: the MISO capture flop and the SCK/MOSI/SSn flops are in IOB sites (`=== T149 SD IOB: ... IN_IOB=1`, from the site; Vivado leaves `IS_IOB` empty), the SD paths are timed and met (`=== T149 SD paths:`) | CI host report |
| S2 | P1 | SPI bench (spi.vhd + the new input stage) with a modelled card delay of 14 ns + board 3 ns at 25 MHz: every byte correct; control: the old sampling point fails at that delay | `ci/sims.txt` |
| S3 | P4 | the large cores' reports show the same IOB placement | CI large |
| S4 | P5 | board: every .cor SHA-checked twice at 25 MHz, `sdio_crc_errors = 0` over JTAG; A1200 HDF speed as before | board |

## §3 Decision

- **D1** The MISO sample is taken by an IOB input flop (`IOB TRUE`), one cycle earlier in the SPI state machine's terms
  (the sampling point moves so the extra register stage does not cost the margin), or the SCK is launched so that the
  capture has a full half period plus the register; the exact form from reading spi.vhd (§5).
- **D2** SCK, MOSI, SSn outputs come from IOB output flops.
- **D3** `set_input_delay`/`set_output_delay` (or the equivalent) for the SD pins against the SPI clock, so the router
  times them.
- **D4** `RW_SPEED` back to 1 (25 MHz); the data CRC16 check with re-reads stays (T139). Limited by D5.
- **D5 Scope (Alex, 2026-10-28, after run 1032).** Run 1032 built T149 into the host: the SD paths met everywhere, but the
  host re-route moved the partition pins and the DFX cores lost timing at the host/core edge (VC-20 WNS -0.981 ns: the RISC-V
  data-side address `ult/i_cpu/i_dmem/addr_i_reg[9]`, fan-out 41, crosses the edge unregistered, 6.3 ns of wire, then the
  drive's address compare `l2/drv/i_drive/param_0_reg/CE`); the C128 large core lost its drive path (`l2/drva/i_drive/i_cpu`
  -> `i_cia1/data_out_reg`, +0.004 ns before, -0.6 ns in route). Run cancelled by Alex. Asked whether the A1200 and the ZX Next
  can run at 25 MHz, "they have the hard disk images as a must and are large cores", and given the plan (host and DFX cores stay
  on today's gateware at 12.5 MHz, the two large cores get T149 and 25 MHz): "Yes, let us try it that way" (translated).
  - The host gateware is not changed (no host re-route, no DFX rebuild); `contract/host_pin.txt` stays as it was.
  - T149's SD block and constraints are built only into the flat builds of `amiga1200` and `zxnext` (run 1032: A1200 WNS
    +0.026, ZX Next +0.269, all four SD flops in IOB sites, SD slacks positive). The C128 large core is not in this change.
  - The SD speed follows the gateware: 25 MHz only where the gateware has T149, 12.5 MHz elsewhere (host app, DFX cores).
  - The edge weakness is its own spec (the host/core edge registered and budgeted), before any further host change.

## §4 Out

- Faster SD modes (SD native 4-bit bus): not wired on this board.

## §5 Build notes

- **Form (D1, D2)** read from `spi.vhd` (mode 0): at rate 1 the edge that raised SCK also sampled MISO in fabric, 20 ns
  after the clock that made the falling edge; the data arrives >= 23.5 ns after it (SCK pad 5 + card 14 + board 3 +
  input 1.5). `host/sd_spi.vhd` (host-owned copy, `sd_spi` + `sd_spi_peripheral_io`, flash keeps Gideon's block): SCK, MOSI,
  SSn from flops that only drive the pad (attribute IOB, no extra stage); MISO through one IOB input flop, taken one clock
  after the rising edge (falling edge + 30 ns, ~6.5 ns margin; next card change >= 12 ns after the sample); received bits
  in their own register `rx`. Needs rate >= 1 (the driver uses 1, 3, 200, 500).
- **Constraints (D3)** `host/pins.xdc`, against cpu_clk (`mmcm_ddr/CLKOUT2`, 100 MHz; every large core reads `host/top.v`
  and `pins.xdc`): MISO input delay 23.5/3.5 ns, multicycle 3 setup / 2 hold; MOSI/SSn output delay 6.0/-5.0 ns, multicycle
  2/3. `trx_report_sd` (`tools/vivado_lib.tcl`) prints IOB sites and pad slacks in host, core and large-core flows.
- **D4** `RW_SPEED` 1 again; CRC16 check and re-reads kept; patch regenerated.
- **S2 bench** `host/sim/sd_spi` (builder): new block rate 1/2/3 at card 14 ns, rate 1 at 19 ns and 0 ns: 0/2000 errors;
  controls fail as they must: old `spi.vhd` rate 1 at 14 ns `rx_errors=1986/2000`, new block at 28 ns `1986/2000`.
- **Vivado trial** (mini top, builder, not the host): all four flops in IOB sites; MISO setup +7.240 / hold +0.899 ns,
  MOSI/SSn setup +6.145 / hold +17.6 ns. The MISO hold margin depends on the clock-tree allowance (1.5 ns): S1 on the
  real host decides. Slew left at default; FAST is the next lever if the board needs it.
- **D5 built** (d9d239fc0, t148): host and contract byte-identical to fae567fb6 (host key a0549dffa5cd11c8 unchanged);
  `cores/amiga1200/sd/` holds `sd_spi.vhd`, `sd_timing.xdc`, `sd_mono.tcl` (copies `ultimate_sys.vhd` into the build dir with
  `i_sd` switched to `sd_spi_peripheral_io`, refuses unless the pattern occurs once); sourced by the amiga1200 and zxnext flat
  builds only. Speed: `SDIO_CTRL` (0x08) bit 7 reads 1 in the T149 block, 0 in Gideon's (`spi_peripheral_io.vhd:128`, upper
  nibble "0000"); `sdio_set_rw_speed(3, 1)` takes 25 MHz only on that bit; host app and core app share the driver.
- Open: S3 (CI, the two flat builds), S4 (board).
