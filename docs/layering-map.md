# Who owns what: Gideon's stock machine, the monolithic TRX64, and the MultiCore split

One table, read off the code on 2026-10-16. It answers two questions: **what is layer 1 (the host)
and what is layer 2 (a core)**, and **what a "C64U vanilla" core can be** without touching the
pinned host.

Legend: **in** = built and reachable today · **off** = the code is there, a build switch has it out ·
**tied** = instantiated but its inputs are constants · **absent** = not in the tree or not vendored ·
**no port** = would need a new partition port, i.e. a contract version bump · **host** = layer 1 by
the board, a core could not have it.

**Read this before the columns.** `L1` and `trx64` describe **the bitstream CI builds today**, and that
is still the T79 debug build. Three lines in `.gitea/workflows/bitstream.yml` decide it for the host
and every core, because nothing reads `core.toml`'s `features`:

| line | value | what it does |
|---|---|---|
| `:87` | `TRX_SID: redip2` | builds reDIP's **two** SIDs (`SID_REDIP`), so `sid_resid8 = 0` (`trx64/c64_core.v:885-889`) and the application shows no reSID8. **Against the decision of 2026-10-14** (T25.4 §3.3, T25.5 §1), but it was taken out for a reason: reSID8 costs **more LUTs**. T25.4 §166-169 states it: "+127 LUT incl. SRL/RAM, +8 %; scaled to Vivado about **+110 LUT**", with **2.5 BRAM tiles freed** and FF +1,130. |
| `:88` | `TRX_FEATURES: jtag` | JTAG debug master on; `uci`, `streams` off in the host and `reu`, `sampler`, `turbo` off in every core |
| `:89-90` | `T79_GREYBOX`, `T79_FLOOR: B` | the trial's grey-box partial and compact floorplan |

Measured, not inferred: the run that built the host image now on the board (`0e8fb06e58872c33`, run
828 job 4839, 2026-10-14 21:11) printed `=== SID: reDIP` and `=== features off: reu sampler uci turbo
streams (TRX_FEATURES)`. The override came in with `dd15aaf5` ("T79 measurement: build integ-m1 with
the two-SID reDIP instead of reSID8") and was never taken out again, so **MultiCore lost the reSID8
that the monolithic line has and that Alex had already accepted on the board.**

So the decided machine and the built machine are not the same machine, and the SID row below is the
built one.

Columns: **Gideon** = his stock C64U/U64 firmware and gateware · **Mono** = our one-bitstream line
(`TRX64-Ultimate`, `designs/c64_basic`, **branch `integ-m1`** - that repository's `main` is a documentation
branch and still carries the pre-T25.4 SID recipe, so it is the wrong thing to read) · **L1** = this
repository's host · **trx64** = the reference core as shipped · **vanilla** = what a `cores/c64u/` could be.

*The directory `cores/trx64` was deleted on 2026-11-08 (T115 D12); the file names `trx64/...` below are the deleted core as it was, the five TRX modules the host reads are now in `cores/common`.* *Since T115 (2026-10-25) the host is routed with `cores/c64ref` (a slim C64U copy, build only, never shipped), not with trx64; the `trx64` columns below describe the TRX64 core as it was built when it was the reference.*

## Board and I/O — layer 1, every core gets it for free

| Block | Gideon | Mono | L1 | trx64 | vanilla |
|---|---|---|---|---|---|
| Clocks, DDR2, config flash | in | in | **in** (`host/top.v:100-108`) | — | — |
| USB (ULPI + hub) | in | in | **in** (`ULPI_*`, `usb_phy.v`) | — | — |
| Ethernet (RMII + PHY) | in | in | **in** (`RMII_*`, `MDIO_*`) | — | — |
| SD card | in | in | **in** (`SD_*`) | — | — |
| ESP32 / WiFi UART | in | in | **in** (`WIFI_TXD/RXD/RTS/CTS`) | — | — |
| RISC-V CPU, application, filesystem | in | in | **in** (`ultimate_sys.vhd`) | — | — |
| HDMI out, VIC scaler, menu overlay | in | in | **in** (`top.v:1374` scaler, `:1387` overlay) | — | — |
| Audio codec I2S + speaker | in | in | **in** (`i2s_out.v`, `AUDIO_*`, `SPEAKER_DATA`) | — | — |
| Keyboard / joystick pins, Bling board | in | in | **in** (`KBJOY_*`, `BLING_*`) | — | — |
| Case and keyboard LEDs | in | in | **in** (`case_leds.v`, `bling_io.v`) | — | — |
| SID sockets (real chips on the board) | in | in | **host** (`SLOT_SID1n/SID2n`, `SLOT_SID_ADDR`) | — | — |
| Capability word the application reads | in | in | **in** | — | — |
| JTAG debug master | — | in | **in** (`jtag_dbg.v`, `TRX_FEATURES: jtag`) | — | — |

## Split blocks — machine half in the core, driver half in the host

| Block | Gideon | Mono | L1 | trx64 | vanilla |
|---|---|---|---|---|---|
| UCI (command interface) | in, $DF1C/$DFFC/$DE1C | in (`uci_unit.vhd`) | **off** (`NO_UCI`, `top.v:633`; `ultimate_sys.vhd:633` vs `:649`) | **off** — 10 `uci_*` ports exist in the contract | needs the host built with `uci` |
| UDP streams (video, audio) | in | in (`udp_stream.v`, `udp_audio.v`) | **off** (`NO_STREAMS`) | **off** — `pix_color/x/y`, `audio_l/r`, `audio_sample_en` in the contract | needs the host built with `streams` |

Both are a **build state of the host, not a property of a core.** `ci/lib.sh:196` keys the host on
`uci jtag streams`, `:204` keys a core on `reu sampler turbo` — the split is in the cache keys.

## The machine — layer 2, the core decides

| Block | Gideon | Mono | L1 | trx64 | vanilla |
|---|---|---|---|---|---|
| 6510, VIC-II, CIAs, RAM, ROMs | in | in | — | **in** (`c64_core.v`, `vendor/icepi/c64_top.v`) | in |
| SID | 2 UltiSID + 2 sockets | **reSID8** (`SID_MULTI`, `integ-m1` `build.tcl:79-82`); Alex accepted it on the board with Tuneful Eight and Sander's demos, all before MultiCore | — | **reDIP 2 as built** (log of run 828: `=== SID: reDIP`), **reSID8 as decided** (`core.toml`: leave `TRX_SID` unset) | reSID8, and it frees 2.5 BRAM tiles |
| REU 128 KB – 16 MB | in | in | — | **off** (`NO_REU`, `c64_top.v:1183`) | **switch on** |
| Sampler / Ultimate Audio Cartridge | in | in | — | **off** (`NO_SAMPLER`, `c64_core.v:489`) | **switch on** |
| Turbo ($D030/$D031/$D07A/$D0BC) | in | in | — | **off** (`NO_TURBO`, `c64_top.v:1579`) | **switch on** |
| Cartridge / expansion port | ~28 types | in | — | **in** (`cart_slot` `c64_core.v:619`, `slot_port` `:651`, `SLOT_*` in the contract) | in; the type list is unaudited |
| IEC bus arbiter | in | in | — | **in** (`layer2_trx64.v:448`, `cores/common/iec_bus.v`) | in |
| 1541 drive A | in | in | — | **in** (`cores/common/drive_l2.vhd`, `drive_unit.vhd`) | in |
| 1571, 1581, WD177x/MFM | in | absent | — | **absent** — VHDL in `vendor/gideon/fpga/1541/vhdl_source/`, `tools/vendor_gideon.sh` copies only the 14 1541 files | vendor + build, area unmeasured |
| Drive B (second unit) | in | absent | — | **no port** — one `drv_io_*` window, one `gcr_io_*` | contract bump |
| Tape, C2N play | in | **tied** (`designs/c64_basic/c64_core.v:515`) | — | **tied** (`trx64/c64_core.v:567`) | **no port** for the C2N pins → contract bump |
| Tape recorder | in (`CAPAB_C2N_RECORDER`) | absent | — | absent | absent |
| ACIA / SwiftLink | in | absent | — | absent, **no port** for the user port | contract bump |
| Paddles, user port | in | absent | — | **no port** | board I/O: a host change |

## TRX64's own, deliberately out of the vanilla core

| Block | Gideon | Mono | L1 | trx64 | vanilla |
|---|---|---|---|---|---|
| Timewarp ring (`trx_engine.v`) | — | in | — | **in** | **out** (Alex) |
| Debugger / monitor (`trx_dbg.v`, `drv_dbg.v`) | — | in | — | **in** | **out** (Alex) |
| Freeze / DMA window (`dma_win.v`) | — | in | — | **in** | out, partly reused |
| Register and I/O path (`trx_io.v`, `trx_sys.v`) | — | in | — | **in** | partly needed |

None of these six files has a single `ifdef`, so leaving them out is not a switch: it needs a new
`layer2_c64u.v` that presents the same 247 ports.

## So what can "vanilla" be

- **Free today, no host change, no contract change:** REU, sampler, turbo, the 8-SID reDIP, the
  cartridge port, the 1541 drive A, the IEC bus. That is the P1 set of T103, estimated at
  **12,750–13,450 LUT (71–75 %)** against the reference core's measured 14,103 (78.35 %).
- **One host rebuild away, for every core at once:** UCI, the UDP streams, and reSID8 as well -
  all three hang on the same two lines of `bitstream.yml`. None of it is free: UCI plus streams cost
  ~740 LUT in the host from mixed-tool figures, never measured on the current host, and reSID8 costs
  about **+110 LUT** in the core (Vivado-scaled, T25.4 §166-169) while freeing 2.5 BRAM tiles.
  **That cost is why it went out** (Alex, 2026-10-16, translated: "it was out here because back then
  it needed MORE LUTs"), and the core is the tightest partition there is: 14,103 of 18,000 LUT
  (78.35 %) is both what it uses and the highest that has ever routed. The rebuild also makes a new
  host image, so it moves `contract/host_pin.txt`. This is the question in T103 Q1.
- **A contract version bump away, i.e. every published core invalidated:** drive B, tape, ACIA.
- **Work, not a switch:** 1571/1581/MFM — vendoring plus an area that nobody has measured. A second
  drive of 1541 size (1,820 LUT, run 275) would put the core near 81–85 %, and 78 % is the highest
  that has ever routed.

Spec and the open questions: [T103](specs/done/T103-c64u-vanilla-core.md).
