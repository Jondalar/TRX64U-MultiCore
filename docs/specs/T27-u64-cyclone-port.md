# T27: TRX64 Ultimate on the old U64 (Cyclone V 5CEBA4F23C8): feasibility

**Status:** analysis and trial synthesis done 2026-10-08 (branch `t27-feasibility`); no port, nothing merged. **Task:** TODO.md T27 (M3, ⏸).
**Question (Alex, 2026-10-08):** does the gateware fit into the old Ultimate 64 at all?

All numbers below are measured by Quartus Prime Lite 18.1.1 for `5CEBA4F23C8` (speed grade -8, the slowest) on the builder, from the
tree of main f285d51, through the stand-ins listed in section 5. Reports: generic package `trx64-t27` in Gitea, versions
`9f5889a2-534`, `bb44f36a-539`, `e3cd2c4f-540`, `cf3758c1-541`, `dc1a4c6c-542` (the run number is the suffix). Scripts (on branch `t27-feasibility`, not merged; only this spec is in main): `tools/t27/`, workflow
`.gitea/workflows/t27-synth.yml` (dispatch only). Code and measurements count; comments in the sources were not used as evidence.

## 1. Answer

**It does not fit as it is, and it does not fit with the cuts that leave the TRX64 features in.** 

| | 5CEBA4 (old U64) | what TRX64 Ultimate needs |
|---|---|---|
| ALMs | 18,480 | **21,477** measured (22,212 on the real device before the fitter gave up: 2,245 LABs needed, 1,848 exist); 120 % |
| M10K | 308 | **286** measured, 93 %, plus about 14 for the DDR2 interface the stand-in leaves out: 97 % |
| DSP blocks | 66 | 25 |
| fractional PLLs | 4 | 6 instances in the design; 4 after merging, all used |
| registers | 36,960 | 32,416-34,944 |

- With the Quartus-specific RAM rewrites (no loss of function, about -2,500 ALM, an estimate) and Intel's DDR2 interface (+1,700 ALM, measured by
  synthesising Gideon's `nios_c5` system, section 6) the full feature set stays at about **20,700 ALM, 112 %**.
- With the cuts below it reaches about 17,800 ALM (96 %): not closable at this speed grade. Below 90 % (16,600) needs the Timewarp ring engine out as well
  (-1,700), which is the point of TRX64.
- **Speed:** the doubled clock (`clk2x`: SID engine and turbo CPU) reaches 47.9-50.8 MHz with 8 SIDs against the 63 MHz the C64U runs; with 2 SIDs 72 MHz. That is
  Gideon's own U64 turbo limit (48 MHz, T27 note of 2026-10-04). The app CPU clock (100 MHz) reaches 68-77 MHz, which is the 66.7 MHz Gideon's Nios II runs at.
- **HDMI 1080p is impossible** on this chip: no transceivers, and the fastest LVDS transmitter is 640 Mbit/s at -8 (TMDS 1080p50 needs 1,489 Mbit/s per lane). The old U64 does
  480p/576p; so does Gideon's firmware.

Recommendation, not a decision: T27 as "TRX64 Ultimate on the U64" stays ⏸. What fits is a different, smaller product (C64 core + RISC-V app + 2 SIDs, 480p/576p,
no ring); that is a new design with its own spec, not a port of this one (section 9).

## 2. How it was measured

- `tools/t27/lists.tcl` evaluates the file lists of `designs/c64_basic/build.tcl` with the Vivado commands shimmed, so the lists are not copied. `prep.sh` makes a scratch tree
  and applies the changes below with `sed`; nothing in the repository's sources is changed. `svconv.sh` runs sv2v v0.0.13 over the Verilog/SystemVerilog
  (Quartus 18.1 Lite cannot read reDIP: unions, typed enums, `unique0`, `for (genvar ...)`; Yosys without the slang plugin fails too). The VHDL stays VHDL.
- `qproj.tcl` makes the Quartus project, `ci_quartus.sh` runs analysis and synthesis, fitter, assembler and timing. The image is `docker.io/raetro/quartus:pocket`
  (Quartus Prime Lite **18.1.1** with Cyclone V device files, public). The Quartus 18.1.0 tree of `1541ultimate` (the `u64-build-env` image) has **Cyclone IV E only** and
  cannot target the 5CEBA4; Intel's device-file downloads redirect to a generic page now.
- Two tops: `c64_core` alone with the real clocks of `c64_core_ooc.xdc` (run 534), and the whole `top` (runs 539-541). The whole design on the 5CEBA4 stops in the
  fitter (it does not fit), so its timing was taken on `5CEBA5F23C8` (29,080 ALM, same package and speed grade, 74 % full): optimistic against a 5CEBA4 packed to 95 %.
- Not constrained: the clock groups and multicycle paths of `pins.xdc`/`build.tcl`, the pin placement (189 of 224 pins, unplaced). Only the per-clock Fmax is used, which needs neither.

Changes the scratch tree needs before Quartus takes it (each is a porting item, section 5):
`c64_tape.v` names its generate block; `sid_filter.sv` reads a precomputed tanh table (no `$tanh`/`$acos`/`$rtoi` in Quartus); `top.v` declares `joys_cpu_s2` above its first use;
`c64_mem.v` clocks its second RAM port on the rising edge (M10K has no inverted port clock); Gideon's `dpram.vhd` is replaced by his own `fpga/altera/dpram.vhd`;
our `usb_dpram.vhd` and `bootrom_8k.vhd` by `altsyncram` versions (a shared-variable RAM written from two processes becomes 65,600 registers in Quartus); `drive_mem.vhd` has no variable slice;
two Gideon 6502n packages go into their own library; the `.mem` paths are bare names; RAM arrays carry `ramstyle = no_rw_check`.

## 3. The board: old U64 against the C64U

Evidence is the code of `1541ultimate` (Nios II build, `target/u64`, `software/`), the Qsys system inside `external/u64.sof`, and the Intel datasheet. "unknown" means no source in
the repository or on this machine says it.

| | old U64 (Rev 1.x, "U64MK1") | C64U / U64-II (our target) |
|---|---|---|
| FPGA | Cyclone V 5CEBA4F23C8, -8 (header of `u64.sof`: device, Quartus 18.1.0; Qsys `AUTO_DEVICE_SPEEDGRADE` 8) | Artix-7 XC7A50T-1 |
| Soft CPU | Nios II/f, 66.67 MHz, 4 KB icache, 4 KB boot RAM (Qsys `nios_small` in the SOF; `ALT_CPU_FREQ`, `NIOS2_ICACHE_SIZE`) | Gideon's rvlite RISC-V, 100 MHz |
| RAM | DDR2, 64 MB address span for the app (`AVALON2MEM_0_SPAN 67108864`, REU window 16 MB at 0x1000000, `c64.h`); **width, part, speed unknown**. Controller: Gideon's own, not Intel's (Qsys has `avalon2mem`, no EMIF; the `nios_c5` EMIF system in the repository is a 5CEBA2U15 test, 8-bit DQ, not this board) | MT47H64M16, 16 bit, DDR2-300 behind our own PHY |
| Video out | HDMI at **720x576@50 and 720x480@60 only** on `U64 == 1` (`SetVideoMode`, `hdmi_scan.cc`); timing in FPGA registers, no transmitter chip is programmed in that path (a search of `software/u64` finds no HDMI-chip driver); analogue video too. Two reconfigurable PLLs in the SOF (`pll_reconfig_lite` x2: video PLL, HDMI PLL; M/N/frac set by `fpll.cc`) | 1080p50/60 through four GTP transmitters, CDCEL824 synthesiser |
| Audio | SGTL5000 codec over I2C (`codec.cc`, `u2p_init.cc`) | codec on the C64U board, I2S |
| USB | ULPI PHY + hub (`usb_hwinit`, `initialize_usb_hub`, nano CPU) | the same ULPI scheme (our `usb_phy.v`) |
| Ethernet | RMII (`CAPAB_ETH_RMII`, `mdio.c`); PHY type not in code | KSZ8081 |
| WiFi | ESP32 (`make esp32_raw_u64`) | ESP32 (control module) |
| Flash | 8 MB, S25FL064L class (JEDEC 01:60:17, from the JTAG notes); layout bitstream @0, firmware @0x290000, ROMs @0x400000, flash disk @0x480000 (`update_u64.cc`) | W25Q128 16 MB, bitstream slot 0x220000, app 0x220000-0x400000 |
| C64 side | the U64 **is** the motherboard: real cartridge port, user port, joystick ports, IEC; a PLD (`C64_PLD_*`) drives the two SID sockets (voltage, shunts, caps) and joystick lines | the C64U keyboard unit + the port buffers |
| Clocks | PAL/NTSC colour clock from the video PLL (fractional, `fpll.cc`) | MMCMs from 50 MHz, CDCE for HDMI |
| Revisions | 1.1, 1.2, 1.3, 1.4 and the 2.5 "TI HDMI" board differ (`getBoardRevision`); only 1.2 was examined (Alex's) | n/a |

Unknown and needed before any design: the DDR2 part and width (board photo or schematic), the HDMI output stage of revisions 1.x, the PHY type, pin assignment (no `.qsf`
or `.sdc` of the U64 is in the repository; the FPGA sources of the U64 core are not published).

## 4. Capacity, measured

Device (fitter report): 18,480 ALM, 1,848 LABs, 36,960 registers, 308 M10K (3,153,920 bits), 66 DSP blocks, 4 fractional PLLs, 224 user pins.

Per group, ALMs from the fitter on `5CEBA5` (run 540, 8 SIDs); Vivado numbers from the C64U build (CI run 516):

| group | Vivado LUT6 | Quartus ALM | M10K | DSP | note |
|---|---:|---:|---:|---:|---|
| C64 core (`c64_core`) | 10,793 | 8,731 | 181 | 13 | alone on the 5CEBA4: 9,884 ALM, 182 M10K, 11,369 regs |
| of it: SIDs + audio mixer/resampler (`c64_trx_sid`), 8 SIDs | 3,339 | 2,653 | 32 | 12 | with 2 SIDs (`c64_redip_sid`, 933 ALM) the whole design is -1,774 ALM, DSP 25 -> 15 |
| of it: VIC (Kawari) / 6510 / 2 CIAs | 1,819 / 1,230 / 876 | 1,134 / 698 / 705 | 2 / 0 / 0 | | |
| of it: cart slot + EF3 logic | 987 | 731 | 11 | | |
| of it: `trx_dbg`, `trx_sampler` | 778, 430 | 367, 397 | 7, 0 | | sampler is T25.6 |
| of it: C64 RAM + ROM (`c64_mem`) | | 105 | 128 | | 2 x 32 KB RAM, 2 x 16 KB ROM; 128 blocks for 1.05 Mbit (width/depth rounding) |
| Gideon's `ultimate_sys` | 6,455 | 6,539 | 33 | 3 | |
| of it: 1541 drive (`drive_unit`) | 1,828 | 3,241 | 4 | 0 | **2,140 ALM / 4,455 registers is `drive_mem`**: the line cache is an asynchronous-read array, Quartus makes registers of it |
| of it: rvlite / USB / Ethernet | 2,132 / 685 / 416 | 869 / 611 / 375 | 12 / 6 / 4 | 3 | |
| `trx_engine` (ring, Timewarp) | 1,368 | 1,698 | 13 | 0 | 3,686 registers against 2,243 on Xilinx: its `tab` array is registers too |
| `hdmi` + `pix_layer` + `overlay` + `vic_scaler` | 630 + 554 + 396 + 152 | 606 + 478 + 190 + 105 | 0 + 7 + 16 + 21 | 0+3+4+0 | |
| UDP video + audio streams | 544 | 749 | 4 | | |
| `mig_arb`, `trx_io`, `trx_sys`, `cart_fill`, keyboard, rest | | ~2,000 | | | |
| **total** | **26,019** | **21,477** | **286** | **25** | 74 % of the 5CEBA5, 120 % of the 5CEBA4 |

(Vivado LUT6 -> ALM is about 0.8 for the same logic.) Left out by the stand-ins: `jtag_dbg` (BSCAN tied off, about 500 ALM in Vivado terms, a development switch, T46),
and the DDR2 interface (section 6).

Clocks (Fmax inside one clock, run 540 = 8 SIDs, run 541 = 2 SIDs; requirement = what the C64U runs):

| clock | needed | 8 SIDs | 2 SIDs | verdict |
|---|---:|---:|---:|---|
| `clk` dot4x (and the gated `gclk`) | 31.5-32.8 MHz | 37.2 (42.0, 40.3 core alone) | 40.2 | met, +15-25 % |
| `clk2x` SID engine, turbo CPU | 63-65.5 MHz | **47.9** (50.8 core alone) | 72.2 | fails with 8 SIDs |
| app CPU clock (rvlite) | 100 MHz | **67.8** | 77.0 | fails |
| pixel clock | 148.9 MHz | **133.3** | 127.8 | fails; moot, no 1080p |
| `ui_clk` (DDR2 side, stand-in at 200 MHz) | 75 MHz | 77.8 | 78.4 | stand-in, not a statement about the EMIF |
| ULPI, 50 MHz | 60, 50 MHz | 86.5, 94.4 | 99.1, 87.9 | met |

`snd_clk` (12.29 MHz) 105 MHz. The paths that limit the 2x clock are the 17-level cart-register-to-CPU path of the `cpu_rdy` multicycle (a two-period path in Vivado; the
constraint is not ported) and the reDIP SID pipeline.

**Cuts and what they buy (ALM, from the fitter):** SID 8 -> 2: -1,774 (measured). UDP streams: -749. Sampler: -397. Timewarp ring engine: -1,698. Drive line cache as a
RAM and the `tab` array as a RAM: about -2,500 together (an estimate from the Xilinx figures, no loss of function, rewrite work). Add the DDR2 interface +1,700.

| build | ALM | of 18,480 |
|---|---:|---:|
| as measured (A5 fit) | 21,477 | 116 % |
| + RAM rewrites (-2,500), + Intel DDR2 (+1,700) | ~20,700 | 112 % |
| + SID 2 (-1,774) | ~18,900 | 102 % |
| + no streams, no sampler (-1,146) | ~17,800 | 96 % |
| + no ring engine (-1,698) | ~16,100 | 87 % |

At 96 % the fitter packed the 5CEBA5 to 96 % of its LABs at 74 % of ALMs (2,791 of 2,908 LABs, run 540); the 5CEBA4 would fail on LABs before ALMs.
M10K: 286 + about 14 for the EMIF sequencer memory = 300 of 308. The 18-block rest is also what every JTAG/debug node and the Intel sequencer need.

## 5. Xilinx-only blocks and their Cyclone V counterparts

| Xilinx | where | Cyclone V | effort |
|---|---|---|---|
| `MMCME2_ADV/BASE` x4, `PLLE2_BASE` | top.v, usb_phy.v | `altpll` / fractional PLL; the 5CEBA4 has **4**, the design has 6 instances (sys + sid2x, ref, ddr/audio/cpu, pix, usb 24 MHz, DDR2 ui) -> merge (the USB 24 MHz forwarder from a counter, ui clock from the EMIF) | low, but all 4 are used |
| MMCM DRP (`mode_ctl.v`, T40 System Mode switch) | top.v | fPLL dynamic reconfiguration (`altpll_reconfig`); Gideon's `fpll.cc` already drives one | medium |
| `BUFG`, `BUFGCE` (machine pause, mode switch) | top.v | global clock network; `altclkctrl` / clock-enable for the gate | low |
| `ODDR`, `IOBUF`, `IBUF`, `OBUF` | top.v, usb_phy.v | `altddio_out`, tri-state pads (used in the trial as such) | low |
| `ddr2_lean` + `cal_seq` (OSERDES/ISERDES/IDELAYE2/IDELAYCTRL) | ddr2_ctrl.v | **Intel DDR2 EMIF** (sequencer + controller + PHY), 2,272 ALUT / 2,228 regs / 14 M10K | high: width, part and pins unknown; the arbiter's 128-bit port must be re-fitted |
| `gt_hdmi` (4 x GTP, 1.489 Gbit/s) | hdmi_gtp.v | none: no transceivers. `altlvds_tx` (J = 10): 640 Mbit/s true LVDS, 550 Mbit/s emulated | not portable: 480p/576p at most, new scaler target |
| `STARTUPE2` (flash clock) | top.v | ASMI block (`altasmi_parallel`) | low-medium |
| `USR_ACCESSE2` (build stamp for the boot ROM's identity check) | top.v | `altchip_id` or a constant from the build | low |
| `BSCANE2` (`jtag_dbg`) | jtag_dbg.v | `sld_virtual_jtag` | low |
| `SRL16E` (Gideon's `srl_fifo-xilinx.vhd`) | VHDL | MLAB/registers (his `srl_fifo-altera.vhd` exists) | low |
| shared-variable true dual-port RAMs (VHDL) | dpram, bootrom, usb | `altsyncram` (Gideon's `altera/dpram.vhd`) | low-medium |
| RAM descriptions with read-during-write and asynchronous reads | `c64_mem`, `color_mem`, `drive_mem`, `trx_engine`, `cart_slot`, SID/audio tables | synchronous-read rewrites, MLAB; two accessors per port | medium: a dozen arrays, each needs a look |
| SystemVerilog in reDIP | cores/icepi | sv2v in the flow (or rewrite) | low |
| M10K single-port-per-clock rule | c64 RAM has three accessors on two clocks | duplicate or arbitrate | medium |

## 6. Calibration: Gideon's core on the same chip

The repository has no report for it: no `.rpt`, `.qsf`, `.sdc` or Verilog top of the U64 core, only `external/u64.sof` (2,466,008 bytes). What the file says: Quartus 18.1.0, device
5CEBA4F23C8, system `nios_small` of 2022-09-24 with Nios II/f at 66.67 MHz, `io_bridge` x2, `avalon2mem`, `pll_reconfig_lite` x2. A fitted bitstream was turned into the same
format for comparison (`quartus_cpf`, `bitstream_sizes.txt` of runs 540/541): the number of non-zero bytes follows the amount of configured logic and routing.

| | non-zero bytes | gzip -9 | compressed bitstream |
|---|---:|---:|---:|
| Gideon's U64 core (5CEBA4) | 957,643 (SOF) | 804,650 | 2,466,008 (SOF), 2,632,660 (RBF) |
| TRX64 Ultimate, 8 SIDs (5CEBA5) | 1,445,884 | 1,282,196 | 3,993,931 |
| TRX64 Ultimate, 2 SIDs (5CEBA5) | 1,396,373 | 1,234,550 | 3,993,931 |

That reads as: Gideon's configuration holds about two thirds of what ours does. As an indicator, not a measurement, his core is then around 70 % of the chip (about 13,000 ALM);
the 5CEBA4 is not empty with it. His core has no 8-SID engine, no Timewarp ring, no 1080p scaler; it has a Nios II/f in place of rvlite.

**Flash:** our compressed bitstream is 3.9 MB on the 5CEBA5 (the 5CEBA4 is a smaller device but ours would still exceed the 2.56 MB of the slot before the firmware at 0x290000). The 8 MB flash
needs a new layout (bitstream up to ~4 MB, app, FAT); Gideon's updater and `update.u64` flow do not know it.

**EMIF cost** (run 542): synthesising Gideon's `nios_c5` system (Nios II + Intel DDR2 EMIF, soft controller, UniPHY sequencer) for the 5CEBA4: the EMIF block is 2,272 ALUT, 2,228 registers,
110,896 memory bits (14 M10K): about 1,700 ALM. Part of that is sequencer firmware memory the application cannot use.

## 7. What the differences mean

- **RAM and the ring.** Our need is about 120 MB/s (video 52, ring <= 12, trace 8, capture 3, mover ~48; `OPT-ddr2.md`) of 600 MB/s peak on the C64U. Even an 8-bit DDR2-400 (400 MB/s peak) would carry it at 576p, where the video share is about 12 MB/s instead of 52. The width is unknown; the ring is not what stops the port, the logic is.
- **HDMI.** 576p only (27 MHz pixel clock, 270 Mbit/s TMDS). The 1080p scaler, `pix_layer`'s 148.9 MHz domain and the GTP stage have no place; the overlay would go onto 576p. 720p (742 Mbit/s) is
  already above the -8 limit of 640 Mbit/s.
- **Turbo (T29).** 8 SIDs cap `clk2x` at 48 MHz. The measured 47.9 MHz is the turbo limit Gideon's own U64 has. With 2 SIDs 72 MHz: 64 MHz turbo is plausible, but not closed on a 96 % full chip.
- **Timewarp and trace.** Logic-bound, not memory-bound (section 4).

## 8. Firmware side

Would the RISC-V app (`target/u64ii/riscv`) run unchanged? **No.**
- The old U64 firmware is Nios II (`target/u64/nios2`, `-DU64=1`) built from the same sources with `#if U64 == 1/2` branches: the SID sockets are driven through the PLD (`C64_PLD_SIDCTRL*`)
  instead of I2C port expanders, video modes are `SetVideoMode` instead of `SetVideoMode1080p`, no I2C-driven HDMI synthesiser, no keyboard unit/Bling board, the flash layout above.
  Our app is the `U64 == 2` build; a third variant (`U64 == 3`: RISC-V, v1 hardware) is needed.
- Our `ultimate_sys.vhd` emulates the U64-II register set (video registers at `U64II_HDMI_REGS`, the I2C bus, the keyboard board); on the old U64 the FPGA must provide the v1 set
  (PLD access, the two PLL reconfigurators, the 480p/576p timing registers). Those blocks are Gideon's closed core; we would write them.
- Boot: `software/bootrom/` reads the app from flash through `STARTUPE2`; on Cyclone V that is the ASMI block and the identity check needs another build stamp.
- The Nios `ultimate.app` and the RISC-V app share no binary format: the existing updater (Nios) can write a RISC-V app and a new bitstream if the package carries them, but a
  failed write means JTAG.

## 9. Effort and risks

| phase | content | rough size |
|---|---|---|
| 0 | board facts: DDR2 part/width, HDMI stage, PHY, pin list, PLD protocol (needs the schematic or a board photo and a logic probe; **no board access in this task**) | days, depends on Gideon/Chris |
| 1 | a reduced core in the 5CEBA4: C64 core, 2 SIDs, 6510 turbo only if it meets, rvlite, 480p/576p, no ring; the RAM rewrites, the PLL budget | 3-4 weeks |
| 2 | Intel EMIF integration, ASMI boot, new flash layout, v1 register set (PLD, PLLs, video) | 3-4 weeks |
| 3 | app variant `U64 == 3`, update path, board bring-up | 3+ weeks, board time |
| 4 | the TRX64 extras (ring/Timewarp, streams, sampler) as far as the chip takes them | does not close, section 4 |

Biggest risks: (1) logic and M10K are both at the limit, so every later feature costs the next one; (2) timing at -8 (turbo 48 MHz, app CPU 68-77 MHz); (3) the board is only known
through Gideon's firmware: pins, DDR2, HDMI and the PLD protocol come from an unpublished design; (4) revisions 1.1-1.4 and 2.5 differ; (5) bitstream larger than Gideon's slot, a brick when a write
fails; (6) Quartus 18.1 Lite only (no Pro for Cyclone V), SystemVerilog needs sv2v, and the CI image comes from a public registry (the licensed tree we have is Cyclone IV E only).

## 10. How a tester would install it (note only, nothing built)

- JTAG: USB-Blaster at the 2x5 header, `openFPGALoader --skip-load-bridge` after loading Gideon's SOF (`doc/JTAG_Recovery.md` of `1541ultimate`; macOS, `sudo`).
  This is also the recovery path.
- `update.u64` file: Gideon's Nios updater writes the segments of the package (bitstream at 0, app at the firmware offset). It would need the new layout; the sector
  protection and the `.u64` header are his. Same brick exposure as the C64U `.cfw`.
- No tester without a USB-Blaster until a golden-image scheme exists (T26).

## 11. Open questions for Alex

1. Is T27 closed as "no, not as TRX64 Ultimate", or do you want the smaller product (C64 core + RISC-V app + 2 SIDs, 480p/576p) as its own task?
2. Do you have the U64 schematics or a clear board photo to read the DDR2 part and the HDMI stage, or should Chris/Gideon be asked for the pin list and the PLD protocol?
3. Should the trial be repeated with the RAM rewrites done (drive cache, `tab`, the c64 RAM's third port) to turn the estimate of -2,500 ALM into a measurement?
4. Which revisions do the TREX testers own (1.2 only, or 1.4 / U64E)?
5. Is the Quartus 18.1 Lite plus sv2v flow acceptable as a CI stage (public `raetro/quartus` image), or should the licensed tree get the Cyclone V device files?

## Status and history

T27: port TRX64 Ultimate to the old U64 (Cyclone V)

Milestone M3, status ⏸. Open (one line, as in the TODO table): Does not fit (120 % ALM); Alex decides

Dated entries, newest first.

- 2026-10-08: trial synthesis on the builder (runs 523-542 of CI, the last six measurements). C64 core alone fits the 5CEBA4 (9,884 ALM, 182 M10K, 13 DSP, `clk2x` 50.8 MHz against 63);
  the whole design needs 21,477 ALM / 286 M10K (120 % / 93 %), 6 PLL instances against 4, and cannot do 1080p; 2 SIDs save 1,774 ALM and lift `clk2x` to 72 MHz; the Intel DDR2 EMIF costs about 1,700 ALM
  and 14 M10K; Gideon's SOF read for calibration. Quartus 18.1.1 Lite with Cyclone V from `raetro/quartus:pocket`; the 18.1.0 tree of 1541ultimate cannot target the device. No board access, no port.
- 2026-10-08: moved here from the one-line row (Alex's question: can the gateware go into the old U64 at all). Earlier text unchanged: Gideon's U64 turbo goes only to 48 MHz (Alex 2026-10-04).
