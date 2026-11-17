# T97 — The Amiga 1200 core boots AmigaVision from its hard disk image

**Status:** PROPOSED (2026-10-14). 2026-10-15: D3 decided and built (gateware + application), D4 read (§5.2). 2026-10-16: P1 built
(TG68K as a 68020, §5.4) and benched; its area (an estimate, §5.4) and timing are the builder's to measure; P5 untouched.
**Status check 2026-11-13:** DONE (Alex 2026-11-08): AmigaVision boots on the board from the large .cor (package 1018, 2026-10-28); the A1200 is a large core since 2026-10-25. No longer "PROPOSED".
**Repo:** TRX64U-MultiCore — `cores/amiga1200/` (its descriptor and recipe), `cores/amiga/rtl/` (the
shared gateware: CPU wrapper, memory, the HDF path), the core application (T93, built outside this
repository).
**Number:** T97 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-14, after opening the A1200 core: "ok, the AGA core, can it do .hdf and if
not, what do we have to do?" and, naming his collection, "/path/to/Development/C64/Tools/
amiga-roms/AmigaVision.rom, MegaAGS-Kickstart.rom … that is about the perfect collection. On the
Pocket a dream" (translated).
**Depends on:** T85 (the Amiga core and its HDF path), T85 §6 (the A1200 core as it stands today),
T93 (the core application).
**Milestone:** M3.

---

## §1 The problem

Alex has the AmigaVision (MegaAGS) collection on this machine and plays it on an Analogue Pocket. He
wants the same thing on the C64U: pick the Amiga 1200 core in the boot menu, point it at his hard
disk image, and be in the AmigaVision menu.

What happens today, measured on 2026-10-14:

- The A1200 core exists and is AGA (T85 §6), but its CPU is `fx68k`, a **68000**. AmigaVision is a
  WHDLoad collection for the A1200/A4000, i.e. 68020 and up. Most of it will not start, and what
  starts is the part that would have run on an A500 anyway.
- His image is `MegaAGS.hdf`, **9,008,283,648 bytes (8.4 GiB)**. The core takes the image size from
  the application in a 32-bit register **in bytes** (`amiga_regs.v`, `hdf_size0`/`hdf_size1`), so
  everything above 4 GiB − 1 is unreachable. The sector address itself is not the limit: the request
  LBA is 32 bits in **sectors** (`amiga_sdc.v`, `mb_req_lba`), which reaches 2 TiB.
- Fast RAM: the signals are wired (`amiga_core.v` passes `fastram_config` and the data path), but how
  much the core actually offers, and whether WHDLoad's requirement is met, is **not measured**.
- `MegaAGS-Kickstart.rom` and `AmigaVision.rom` are 524,288 bytes each, i.e. ordinary Kickstart
  images, and the application already has a "Kickstart file (at reload)" entry. That part is in place.

Why it matters: without this the A1200 core is a chipset demonstration. With it, it is the machine
Alex actually uses — and it is the first core of ours that has to carry a gigabyte-sized medium.

**Parts (MECE):**

- **P1 CPU.** A 68020 in the partition, in place of or beside `fx68k`, with the bus behaviour the
  chipset expects.
- **P2 Area.** The partition is 18,000 LUT and 50 BRAM. Today the Amiga core occupies 13,034 LUT, of
  which `fx68k` is 3,209. Whatever the 020 costs has to come out of the 4,966 that are free.
- **P3 Hard disk images above 4 GiB.** The size path from the application to `amiga_sdc`.
- **P4 Fast RAM.** How much the core offers, where it lives, and whether WHDLoad is satisfied.
- **P5 The machine as a whole.** Kickstart 3.1, 2 MB chip RAM, the AmigaVision menu on screen, a
  game started from it.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: the 020 runs the chipset (P1)
  Given the A1200 core built with the 68020 and the AGA chipset
  When  the whole-core bench runs the same boot the 68000 build runs today
  Then  the machine reaches the same state (screen, vertical blank counter, DDR2 window untouched)
  And   no cycle is granted that the memory did not acknowledge

Scenario: it still fits (P2)
  Given the A1200 core with the 020
  When  the core stage of the pipeline implements it
  Then  pb_l2 is below 18,000 LUT and 50 BRAM tiles, WNS is positive, pr_verify is compatible
  And   the host's own numbers do not move (the core is a partial, layer 1 is untouched)

Scenario: an 8.4 GiB image is addressable end to end (P3)
  Given MegaAGS.hdf of 9,008,283,648 bytes on the medium
  When  the application mounts it and the machine reads the last sector of the image
  Then  the sector the machine receives is the last sector of the file
  And   a read beyond the image is refused, not wrapped

Scenario: WHDLoad finds its fast RAM (P4)
  Given the core configured as the A1200 core ships
  When  the machine asks the memory system what it has
  Then  at least <the number P4 establishes> of fast RAM answers, at the address the Amiga expects

Scenario: AmigaVision comes up (P5)
  Given Kickstart from cores/amiga1200's ROM setting, MegaAGS.hdf mounted
  When  the core is started from the boot menu
  Then  the AmigaVision menu is on the screen (photo)
  And   a title started from it runs with sound and input
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | the 020 runs the chipset | `cores/amiga/sim/run_l2.sh` with the 020 build | by hand, then `ci/sims.txt` |
| S2 | P2 | it still fits | `amiga1200.utilization_pb_l2.rpt`, `.timing_neg.rpt`, `.pr_verify.rpt` | CI `bitstream` |
| S3 | P3 | 8.4 GiB end to end | `cores/amiga/sim/run_sdc.sh` (built, §5.1) plus the application's own mount (T102) | bench + board |
| S4 | P4 | fast RAM | the number is read (§5.2); the Workbench free-memory read-back is a board step | board |
| S5 | P5 | AmigaVision comes up | board session, photo | by hand |

Rules kept from the template: the negative in S3 (a read beyond the image is refused, not wrapped)
has to be able to fail — a size field that silently truncates is exactly the defect being fixed.

## §3 Decision

Open until the measurements of §1's unknowns are in. The decisions this spec has to take:

- **D1 Which 68020 (taken 2026-10-16 in the build, §5.4; withdrawn if the builder's M3-M5 do not fit).** TG68K: the wrapper already
  instantiates it and the upstream NanoMig runs it with this chipset. The decision was meant to wait for a measured number; the
  number now is a yosys estimate (§5.4), the Vivado one is still open.
- **D2 Where the 020 lives (taken 2026-10-16: in `cores/amiga1200/`).** The OCS `amiga` core stays on fx68k with every file
  unchanged (its cache key does not move, §5.4); the price is a copy of `cpu_wrapper.v` in the A1200 tree. The flip to the shared
  tree stays open if M3 shows both CPUs fit together.
- **D3 The size field (decided 2026-10-15, §5.1: sectors, announced by a FEATURES bit).** The size travels in sectors instead of bytes, which makes the existing
  32-bit registers reach 2 TiB and matches the LBA that is already in sectors. The application has to
  write it that way, so this is a change on both sides and cannot be done in gateware alone.
- **D4 Fast RAM.** Read from the gateware 2026-10-15: up to 5.5 MB Zorro II at $200000 (§5.2); whether WHDLoad is satisfied is a board measurement.

## §4 Out

- No 68030, no MMU, no FPU. AmigaVision runs on an 020; anything beyond is a different machine.
- No CD32, no AGA-only hardware beyond what the chipset already has.
- No write-back strategy change: the HDF path stays as T85 built it (the application stages sectors).
- Nothing about the ROMs themselves — Kickstart files stay outside the repository and outside every
  release, unlike the Atari's OS (T88 §5.11), which Alex allowed in writing for that core only.

## §5 Decisions taken while building

### 5.1 D3 The size field travels in sectors — decided and built (2026-10-15)

**Decision.** `hdf_size0/1` hold 512-byte sectors when the core says so; the core says so in a new read-only register `FEATURES`
(`0x00C`, bit 0), driven by the generic `HDF_SECT` (`layer2_amiga` -> `amiga_core` -> `amiga_regs` / `amiga_sdc`). The A1200 recipe sets
`HDF_SECT=1`; the Amiga core keeps 0 (bytes). *Why a generic and not a plain change:* the application in the `trx64-coreapps` package
writes bytes; a gateware that reads sectors would turn its 4 MiB image into 2 GiB. With the feature bit an application can ask first: the
A1200 application writes sectors if the bit is set and bytes if not, and then refuses an image of 4 GiB and more with a message, so it
also works against the run-35 partial (which has no register: it reads 0).

**What reaches 2 TiB.** Size: `sdc_img_size <= {23'd0, size, 9'd0}` (64 bit); `nanomig.v` takes `total_sectors <= sdc_img_size[40:9]`, 32
bit, so the IDENTIFY words 57/58 carry the full count. LBA: `mb_req_lba` was always the sector number. Application: `File::seek64` /
`get_size64` (FatFs `FSIZE_t`, exFAT: `chan_fat/full/ffconf.h` has `FF_FS_EXFAT 1`); `FileInfo.size` is `uint32_t`, so it must not
be used for an HDF (an image of 9,008,283,648 bytes would show up there as 418,349,056). The largest image: 2^32 - 1 sectors.

**Measured** (`cores/amiga/sim/run_sdc.sh`, iverilog on the real `mig_arb` and a DDR2 model):

```
== bytes (HDF_SECT=0), 4 MiB image                                       PASS tb_sdc errors=0
== sectors (HDF_SECT=1), MegaAGS.hdf 9,008,283,648 bytes, last sector 17,594,303
   ok: mount of HD0 announced with its size in bytes (9008283648)
   ok: mailbox: read request for the last sector of the image (lba 17594303)         PASS tb_sdc errors=0
== negative control: the same register value into a core that reads bytes
   FAIL: mount of HD0 announced with its size in bytes                                  (the bench fails, as it must)
```

`run_cfg.sh` (T102 S2) carries the same size through the whole `amiga_core`: after writing 17,594,304 into `0x184..0x187` and mounting,
`core.sdc.sdc_img_size` is 9,008,283,648. `software/io/amiga/tests/run_cfgdump.sh` prints the application's side: 9,008,283,648 bytes ->
register `010c77c0` (sectors) or refused (bytes); 4,294,967,296 -> refused in bytes mode, `00800000` in sectors mode; 2 TiB refused either way.

**Not proved.** The Amiga reaches a sector through the IDE registers: `nanomig.v` reports CHS only (IDENTIFY word 49 = 0, "lba not
supported"; words 60/61 not given) and computes `ide_sdc_sector` from cylinder, head and sector with the geometry from the RDB. Whether
`scsi.device` and the MegaAGS RDB reach the whole 8.4 GiB through that is not benched (it is P5, on the board), and no file larger than
4 GiB has been mounted by the application: that needs exFAT media and Alex.

### 5.2 D4 / P4 Fast RAM — read from the gateware (2026-10-15)

| `FAST_CFG` | Fast RAM at $200000 | How |
|---|---|---|
| 0 | none | |
| 1 | 2 MB | one Zorro II card (`cpu_wrapper.v`, autoconfig `5'b01001`) |
| 2 | 4 MB | `5'b01010` |
| 3 | **5.5 MB** | 4 MB + 1 MB + 0.5 MB: three cards, the last two "stolen from SlowRAM" (`nanomig.v:209`, `slowram_config` = 0) |
| 4..7 | not offered | Zorro III needs `ENABLE_RAM32`, which is not defined (`cfg_z3 = 0`); value 4 would autoconfigure a half megabyte card |

The most this core offers is **5.5 MB, Zorro II, from $200000 to $77FFFF**, 16 bit wide. The DDR2 window keeps 8 MB for the Amiga
(`amiga_mem` `p2_addr[22:1]`, `sel_z2ram` decodes $200000..$9FFFFF) and puts Kickstart at $780000 (`amiga.cc` `W_ROM`), so the 5.5 MB end
exactly under the ROM; the 1.5 MB beyond 4 MB are the slow RAM's (`slowram_config` = 0 then). Benched: 4 MB only (`run_l2check.sh`, `FAST=1`,
Zorro II autoconfig by the test ROM); 2 MB and 5.5 MB are read, not run. **Whether WHDLoad and AmigaVision are satisfied is not decided
by this**: it is a board measurement (boot AmigaVision with 4 MB and with 5.5 MB and read the free fast memory in Workbench). The A1200
application offers None / 2M / 4M / 5.5M and defaults to None.

### 5.3 D1 / D2 The 68020 — the comparison on paper (written 2026-10-15, corrected 2026-10-16)

Read from the wrapper and from the named upstream tree. Corrected on 2026-10-16 against the code and the benches: the licence (all
three kernel files say LGPL v3, not "GPL v3 in three of them"), the file count (three are taken, `TG68K.vhd` is not needed), the clock
enable (it is **not** enough as the wrapper has it, §5.4), and the CPU type (the A1200 application writes `CPU=00`, §5.4).

**What the wrapper asks of a CPU (read in `cpu_wrapper.v`).** A clock-enable CPU, not a 68000 bus: ports `clk, nReset, clkena_in,
data_in[15:0], IPL[2:0], addr_out[31:0], data_write[15:0], nWr, nUDS, nLDS, busstate[1:0]` (0 fetch, 1 no memory access, 2 read data, 3
write data), `longword`, `CPU[1:0]` (00 68000, 01 68010, 11 68020), `CACR_out`, `VBR_out`, `skipFetch`, `nResetOut`. The enable is
`cpu_clkena = (~cpu_req & tg68_armed) | chipready | ramready`: the CPU runs on internal cycles and stalls until the chip RAM or the fast
RAM port answers. (As written this is right only where `ce28` is always 1, as in NanoMig; here it needs a `& ce28`, §5.4.) `TG68K_A24` is defined unconditionally, so the address is cut to 24 bits (what a 68EC020 does, and what the 8 MB
window can serve). `CPU_SLOW14` makes the clock enable every second cycle (the 14 MHz of a real A1200, and a two-cycle path for the
kernel's register-to-register logic). With `ENABLE_TG68K` and no `ENABLE_FX68K` the TG68K serves all `CPU` values; with both
(`CPU_SWITCHABLE`) the wrapper picks by `cpucfg[1]` and pays for both. The instantiation passes the generics `SR_Read`,
`VBR_Stackframe`, `extAddr_Mode`, `MUL_Mode`, `DIV_Mode`, `BitField` = 2 (switchable by `CPU`), `MUL_Hardware` = 1 (DSP multiplier).
The wrapper has no cache (`ENABLE_CACHE` is not defined, `cpu_cache_new.v` was not taken, T85 NOTICE).

**Candidates.**

| | TG68K (`TG68KdotC_Kernel`, Gubener; NanoMig `src/tg68k/`) | other 68020 cores |
|---|---|---|
| Fits the wrapper | Yes: the wrapper instantiates it, ports above are its ports | Each needs an adapter from its bus to the `clkena` / `busstate` handshake; none is in the tree |
| Sources | VHDL, 4 files in NanoMig (`TG68K.vhd` 12 KB, `TG68K_ALU.vhd` 45 KB, `TG68K_Pack.vhd` 9 KB, `TG68KdotC_Kernel.vhd` 134 KB), commit a19604a; `TG68K.vhd` is the 68000 bus top and nothing instantiates it, so three are taken. Licence: **LGPL v3 or later in all of them** (read from the headers; the "GPL" sentence in them is the LGPL notice's boilerplate) | not looked at; none is in the tree |
| Proven with this chipset | Upstream NanoMig runs AGA with a 68EC020 on this chipset code (its README: "Accelerated 68020 support + CPU cache", "OCS, ECS, AGA chipset"; Pinball Fantasies AGA on an IcePi-Zero) - upstream's claim, not ours | - |
| Simulation | The wrapper's `VERILATOR` branch expects the lower-case ports of upstream `sim/TG68KdotC_Kernel.v`, a 1.67 MB tool-converted Verilog translation (upstream says it works and is not readable); `src/tg68k/verilog/` is an incomplete AI translation that upstream says malfunctions in the first instruction - not usable. Vivado reads the VHDL (`read_vhdl`, as `gideon_vhdl.tcl` already does for Gideon's VHDL) | - |
| Area | T85 carried "TG68K instead of fx68k (smaller per a forum thread, not verified)": **it is not smaller**, §5.4 (about 4,000-5,000 LUT against fx68k's 3,209 of our 13,034; free in `pb_l2` is 4,966) | - |
| Timing | **Unknown.** Our chipset enable `ce28` runs on `sys_clk` 31.5278 MHz (31.7 ns), so a kernel path without `CPU_SLOW14` is a single-cycle path at 31.5 MHz on a -1 Artix-7 | - |

**D1 proposal (taken, §3): TG68K**, because it is the only candidate whose bus the wrapper already speaks and the only one
upstream has run with this chipset. The decision is withdrawn if M1 or M3 below does not fit.

**D2 proposal (taken, §3): the 020 lives in `cores/amiga1200/` as that core's own CPU** (`cores/amiga1200/rtl/tg68k/`, `-verilog_define ENABLE_TG68K`
in its `core_sources.tcl`, `read_vhdl` of the four files), so that the A500 core stays on the cycle-exact fx68k and its sources untouched.
The shared wrapper already has the `ifdef`s. *Lost:* one gateware for both cores (T85 §6) - the CPU is the one thing they would differ in,
and `core.toml` already lists `cores/amiga1200` in its own `sources`. If M3 shows that both CPUs fit together (`CPU_SWITCHABLE`), the
shared tree is the cleaner place and this flips.

**What I want measured on the builder (Vivado, out of context synthesis for xc7a50t, no routing for M1-M4).**

1. **M1** `TG68KdotC_Kernel` alone with the wrapper's generics (all 2, `MUL_Hardware` 1): LUT, FF, DSP48, BRAM.
2. **M2** the same with `MUL_Hardware` 0 and with `MUL_Mode`/`DIV_Mode` 1 (32 bit) fixed instead of switchable: what the area levers are worth.
3. **M3** the A1200 recipe (`layer2_amiga`, `-generic CHS_INIT=16 -generic MEM_INIT=3 -generic HDF_SECT=1`) with `-verilog_define ENABLE_TG68K`
   and the four VHDL files: hierarchical utilisation (`cpu_wrapper` alone), total against 18,000 LUT / 50 BRAM / 80 DSP; and with both CPUs
   (`CPU_SWITCHABLE`) for D2.
4. **M4** the same synthesis with `CPU_SLOW14` defined, for the area and for the next item.
5. **M5** the `bitstream` stage of the core with the 020 (the real thing): WNS and the worst path's start/end (is it inside the kernel?),
   `pr_verify` compatible. Run once without and once with `CPU_SLOW14` if the first fails timing.

Then, in the bench: S1 (the same boot with the 020) needs the translated Verilog above for Verilator; getting it into the tree is part of the work (done, §5.4).

### 5.4 P1 The 68020 — built and benched, area estimated (2026-10-16, requested by the coordinator after Alex's board session)

**Why now.** Alex at the board, 2026-10-16: Kickstart 3.1 (`exec 40.10`, 40.68, the A1200 build, compiled for the 68EC020) sends the
core into a permanent reset; 1.3 boots. A 68000 cannot run it, so P1 is the condition for this core, not an improvement of it.

**What is in the tree.**

| Where | What |
|---|---|
| `cores/amiga1200/rtl/tg68k/` | `TG68K_Pack.vhd`, `TG68K_ALU.vhd`, `TG68KdotC_Kernel.vhd` from NanoMig `a19604a`, LGPL v3 (`rtl/NOTICE`); the kernel's entity renamed `TG68KdotC_Kernel_core` (3 lines) by `tools/vendor_tg68k.sh` |
| `cores/amiga1200/rtl/tg68k_020.v` | takes the name `TG68KdotC_Kernel` that the wrapper instantiates and ties the kernel's `CPU` to `11` |
| `cores/amiga1200/rtl/cpu_wrapper.v` + `.diff` | the shared wrapper with the one change below; `tools/check_wrapper_fork.sh` fails when either side moves |
| `cores/amiga1200/sim/tg68k/TG68KdotC_Kernel.v` | NanoMig's tool-generated Verilog translation, byte for byte, for Verilator only |
| `cores/amiga1200/core_sources.tcl` | `-verilog_define ENABLE_TG68K`, the three VHDL files, the wrapper fork; no fx68k, no microrom copy |
| `cores/amiga/sim/run_cpu020.sh`, `gen_testrom.py` (`MODE=cpu020`), `tb_l2.sv` (`C020`), `run_l2.sh` (`TG68K=1`) | the bench; `ci/sims.txt` `amiga_cpu020`, `ci/sims.deps` |

**Two defects in the plan of §5.3, found by the bench, not by reading.**

1. *The application writes `CPU=00`* (`cores/amiga1200/app_regs.txt`). The wrapper hands that byte to the kernel, and TG68K with `CPU=00` is
   a 68000: the same test ROM below traps on it (flag `BAD0`, measured). `tg68k_020.v` therefore ties `CPU` to `11` in the A1200 core,
   so the machine does not depend on a byte an application may or may not write. (`cpucfg[1]` also reaches `ide_fast`; that stays as the
   application sets it, harmless.)
2. *`cpu_clkena` needs `& ce28`.* The kernel is clocked by `sys_clk`; `ce28` is a one-clock pulse nine times in ten (`amiga_ce.v`), and `chipready` /
   `ramready` are registers that only change on `ce28`, so one of them stays high across a missing `ce28` and the kernel takes the enable
   twice. Measured: after such a pair the CPU runs one step too far and the next long address word is the word before it (`move.w #$7FFF,$DFF09A`
   wrote to `$7FFFF09A`; the display never came up, in 68000 mode as well). Taking the enable only on a `ce28` tick fixes it: the whole test
   ROM, picture included, runs. NanoMig does not see this because its clock is the 28 MHz itself. The change is the only difference of the fork.

**What the bench proves** (`sh cores/amiga/sim/run_cpu020.sh`, Verilator, the A1200 generics `CHS_INIT=16 MEM_INIT=3 HDF_SECT=1`):
`MODE=cpu020` is the test ROM, not a label. It sets vector 4 (illegal instruction) to a handler that writes `$BAD0` to `$208`, then runs
`EXTB.L D0`, `MULU.L D1,D2` (32 bit result) and `BFEXTU D0{24:8},D3`, all 68020-only, stores the results in chip RAM, writes `$020F`,
and carries on to the normal display set-up.

```
== TG68K build (the A1200 core's CPU)
row 270: first yellow column 40, last 671                          (the picture is there, as with fx68k)
cpu020: EXTB.L ffffff80, MULU.L 24101100, BFEXTU 00000080, flag 020f     PASS tb_l2 errors=0
== negative control: fx68k (68000, the OCS core's CPU) on the same ROM
cpu020: EXTB.L 00000000, MULU.L 00000000, BFEXTU 00000000, flag bad0      FAIL x8 (as it must; the bench script requires them)
PASS amiga_cpu020
```

A second control, done by hand and not in the script: the TG68K build with `CPU` passed through (what the application writes, `00`) gives the
same `flag bad0` - this is what shows that the tie in `tg68k_020.v` matters.

**Not proved.** Kickstart 40.68 itself (no ROM is in the repository; the `BOOT=1` mode of `run_l2.sh` takes one by hand and was not run with the 020); AGA
screen modes on the 020 build; anything about timing or routing (no Vivado here); the VHDL as **Vivado** reads it (nothing here elaborates
VHDL: no GHDL, no Vivado - the Verilog around it was elaborated with yosys against a port-for-port stub of the entity, and the port and generic
names of `tg68k_020.v` were compared with the entity's by script). `CPU_SLOW14` is not used: it makes the paths two-cycle only for the CPU's behaviour, and no
multicycle constraint exists in `host/*.xdc`, so Vivado would still time them as single-cycle.

**The OCS core is untouched.** Nothing under `cores/amiga/` changed except `cores/amiga/sim/` (outside its `sources`), `cores/amiga/core_sources.tcl`
is not touched, and `ooc_key xc7a50t amiga` (the function of `ci/lib.sh`) is `6d93df0b91878555` before and after the change (the A1200's moved from
`fcf7bf2f42364575`, as it must). `cores/amiga/rtl/NOTICE` still says "Not taken: TG68K"; that is true of the `amiga` core and was left so for the
key - the A1200 notice is `cores/amiga1200/rtl/NOTICE`.

**Area - an ESTIMATE, not a measurement** (yosys 0.69, `synth_xilinx -family xc7`, 2026-10-16; no Vivado on this machine; the kernel
it reads is the Verilog translation, not the VHDL Vivado will read, and it carries three latches the VHDL may not):

| | yosys, hierarchical | yosys, flattened | Vivado (run 393/35, measured) |
|---|---:|---:|---:|
| fx68k (LUT / FF) | 3,493 / 1,383 | 3,282 / 1,399 | 3,242 / 1,466 (T85 §5), 3,209 (§6) |
| TG68K, all generics 2, `MUL_Hardware` 1 (LUT / FF) | **5,004** / 966, + 12 RAM32M, 10 DSP48 | 13,661 (not credible) | not measured |

Yosys is about +8 % over Vivado on fx68k hierarchically (+1 % flattened) - less than the 15-25 % the task assumed; the flattened TG68K
figure is 2.7 times the hierarchical one (the ALU has a combinational loop in the translation, ABC gives up on it) and is discarded. Taking
the 5,004 and removing 0-25 % of yosys overhead gives **about 4,000-5,000 LUT for TG68K in Vivado, against fx68k's 3,209: +800 to +1,800**.
The core then lands at 13,034 - 3,209 + (4,000 to 5,000) = **13,825 to 14,825 LUT, 76.8 % to 82.4 % of `pb_l2`'s 18,000, about 14,300
(79.5 %) in the middle**. FFs go down by about 400, the two block RAM tiles of fx68k's microcode ROM go away (17 -> about 15 tiles), DSP goes
up by about 10 (budget 80).

**Verdict: synthesis fits (3,200 or more LUT under 18,000), routing is not promised.** The reference core routes at 14,103 (78.35 %) and the
OCS Amiga at 13,034 (72.4 %); the upper end of the estimate, 14,825, is above everything that has routed in `pb_l2`, the middle is 200 above
it. The skill's data point is that density, not edge pins, decides (the host did not route at 82 %). Timing is unknown: the kernel at 31.5 MHz
(31.7 ns) on a -1 Artix-7 has no number from us. Both are the builder's to answer: **M3** (out-of-context utilisation of the A1200 recipe)
is the first thing to read; if it says above about 14,100 LUT, the levers in order are `MUL_Hardware` 0 (frees the DSPs, costs LUT), `DIV_Mode`
/ `MUL_Mode` fixed to 1 instead of switchable, `BitField` 1 instead of 2 - and every one of them must be checked against what Kickstart 3.1 and WHDLoad
execute before it is taken.

### 5.5 P2 Area and timing — measured on the builder (CI run 866, job 5375, 2026-10-16)

The estimate of §5.4 held. Vivado read the vendored VHDL, implemented the partial against the pinned host
`0e8fb06e58872c33` and routed it:

| | report line | number |
|---|---|---|
| layer-2 module, out of context | `=== T85 amiga1200 synth \| Slice LUTs* \| 13425 \| ... \| 41.18` | 13,425 LUT, 8,340 FF, 16.5 BRAM, 18 DSP |
| in `pb_l2`, after routing | `amiga1200.utilization_pb_l2.rpt` | **14,055 LUT of 18,000 = 78.08 %** (12,565 logic + 1,490 memory), 8,342 FF (23.17 %), 16.5 of 50 BRAM (33 %), 18 of 80 DSP |
| timing | `=== core amiga1200: WNS 0.114 ns` | **+0.114 ns**, THS 0.000, WHS +0.051 |
| partition | `=== core amiga1200: pr_verify passed` | partial 1,333,650 B, full 1,652,109 B |

Against the OCS core's 13,034 LUT that is **+1,021**, inside the predicted +800 to +1,800 and 245 below the
middle of the estimate. 78.08 % is under the 78.35 % the reference core routes at, so no lever of §5.4 is
needed: `MUL_Hardware` stays 1 and the DSPs stay in. BRAM went from 17 to 16.5 tiles as predicted; FFs did
**not** drop by 400 (8,342 against the OCS core's 8,340 - the microcode ROM's registers were block RAM, not
flip-flops).

What this does **not** say: that Kickstart 3.1 boots. The run proves the recipe builds and closes timing;
only S8 on the board answers the picture, and it is still open.

**S8 on the board, 2026-10-18:** Kickstart 3.1 (40.068) boots to its insert-disk picture on the run-893 build
(Alex: "Yes! that is the 3.1 kickstart", translated, photo); fast RAM off, no HDF, so S4 and S5 stay open.

### 5.6 AGA wide fetches got 16 of 64 bits (2026-10-19); NOT the cause of the AmigaVision stripes (board, run 904)

**Board observation (Alex, 2026-10-19):** the AmigaVision menu on the A1200 core shows about 75 % of every 64-pixel group
as colour 0 stripes. A/B: run 893 and run 903 (T85 on main) show the same, so T85 is not the cause; Turrican (OCS,
FMODE 0) is unaffected.

**Cause, read in the code (not in a comment):** with FMODE 1..3 `denise_bitplanes.v:368-385` builds BPLxDAT from
`{data_in, chip48_fmode}`, `chip48` being words 1..3 of the 8-byte aligned group (FMODE 3: all 48 bits, FMODE 1/2:
`[47:32]`); the sprites do the same (`denise_sprites_shifter.v:92`). `amiga_core.v` tied `.chip48(48'd0)`, and
`amiga_mem` had only a 16-bit `dout`, so three of four words of every wide fetch were zero. The same on the 68020 side:
`.fastram_dout48(48'd0)` while `nanomig.v:489` fills the cache line with `{fastram_dout, fastram_dout48}` (a 16-byte line
miss delivered one real word of four). **Corrected in 5.9:** that fill is inside `ifdef ENABLE_CACHE`, which no core defines, so on the
A1200 nobody reads `fastram_dout48`; only the chip side (Denise, sprites) uses the wide fetch. The consumers order the group big endian: `dout` = word at the aligned
address, `[47:32]` = +2 bytes, `[31:16]` = +4, `[15:0]` = +6.

**Decision (Alex, 2026-10-19: "Bitte ja", translated: "Yes please")**, built:
- `amiga_mem` gets `dout48` and `p2_dout48`: words 1..3 of the aligned 4-word group of the requested word, latched in the
  same clock as `chip_dout` / `p2_dout`. Hit: the four banks of the group are read with the same index in the same clock
  anyway (`bank_q`), so it is a 2:1 mux. Miss: the critical-word path also carries the group (`crit48` on ui_clk,
  `crit48_q` in sys_clk), so the three words are valid when the answer is delivered. An unaligned request is not a wide
  fetch; it gets the three other words of its aligned group, which nobody reads.
- `amiga_core.v` wires both. The OCS core shares the module; FMODE 0 never looks at the new ports.

**Cost (estimate, no Vivado ran):** no block RAM and no new port (all 8 banks were already read every clock): 2 x 48 flops
sys_clk (`chip_dout48`, `p2_dout48`) + 48 + 48 flops for `crit48` / `crit48_q` = 192 FF, about 100 LUT for the muxes and
the group select. Against 14,055 LUT (78.08 %) that is below 1 %; routing is unmeasured.

**Proof:** `cores/amiga/sim/run_wide.sh` (`tb_wide.sv`, CI `amiga_wide`): `amiga_mem` + DDR2 model, a known 64-bit pattern,
every word of two lines as the wanted one, chip miss and hit, fast RAM port miss and hit, a hit across the two ports: 50
reads, 0 errors. Negative control `NEG=1` (consumers see the old tie-off) fails with `dout48 000000000000`. `tb_fmode`
(denise side) is unchanged and stays. Existing benches green: amiga_mem (membench), l2, rate, cfg, cpu020, fuzz, audio,
equiv. **Open:** the picture on the board (next A1200 bitstream).


### 5.7 The stripes were an undeclared net (2026-10-19, full-core sim of the real AmigaVision boot)
`amiga_core.v` connected `.dout48(m_dout48)` / `.p2_dout48(p2_dout48)` without ever declaring the two wires: Verilog made them
1 bit wide, so Denise got `{data_in, 47'b0, bit}` in FMODE 1..3 (§5.6 wired the right ports but not the right width; `tb_wide`
and `tb_aga` instantiate `amiga_mem` with their own 48-bit wires and could not see it). Reproduced in `tb_l2` booting
AmigaVision.rom + AmigaVision.hdf (`+HDF=`, TG68K, 2 MB chip, AGA): at 18.4 s the launcher shows the 64 px period / 48 px
stripes (`shot_18.ppm`); probe at line 100: FMODE=3, hires, 8 planes, DDFSTRT $38, DDFSTOP $D8, `bpldata` = 0 in the stripes
(COLOR00 is the teal; the black background is index $F0..$FF), every bitplane register held `{word0, 0..0001}`. Fix: declare
`wire [47:0] m_dout48, p2_dout48` (`amiga_core.v:172`); `tb_l2` now checks the widths. Open: board picture on the next bitstream.

### 5.8 The wide fetch does not route with the default directives (run 908, 2026-10-20)

With `m_dout48`/`p2_dout48` really 48 bits wide the A1200 core synthesizes to 14,817 LUT (was 14,055) and `route_design`
ends with "Design is not legally routed. There are 3 node overlaps" (nets in `cpu_wrapper`). Taking `p2_dout48` out again
was weighed and not taken: the 68020's cache fills its 64-bit lines from it (`nanomig.v:489`), which may also be why
Another World under WHDLoad hung (wrong, see 5.9: `ENABLE_CACHE` is undefined, that path is not built). Alex: "Mach das" ("do that") to a second routing attempt. `core.toml` now names
this core's own directives (`place = AltSpreadLogic_high`, `route = AggressiveExplore`); `ci/bitstream_layers.sh`
applies them to this core's key and run only, so the host and the other cores keep their cache entries.

**Run 909 (fa269a5e), cancelled by Alex after about 2 h.** With the new directives the router reached 0 node overlaps,
but only through long detours: `Intermediate Timing Summary | WNS=-7.257 | TNS=-195.920`, placement had ended at
WNS +0.039 ns. `pb_l2` holds 15,605 of 18,000 LUT (86.7 %) while BRAM is at 35 % and DSP at 22 %, so the lever is
density, not directives. Largest piece of distributed RAM: Paula's floppy FIFO (`paula_floppy_fifo.v`, 2048 x 16,
about 999 LUT); its read and the `out <= in` bypass share one output register, so Vivado builds LUTRAM instead of a
block RAM. Next attempt (one, not a directive sweep): the read in its own register, the bypass mux behind it.

**Decision (Alex, 2026-10-20):** A1200-only changes go into copies in `cores/amiga1200/rtl/`, taken out of the shared
`minimig-aga` glob like `cpu_wrapper.v`, so the A500 keeps its sources and cache key. "The copies really do not hurt.
Once it runs properly and the bugs are out, the gateware never changes again, in my view" (translated).

**Built (Alex, 2026-10-20: "so what are we waiting for?", translated):** `cores/amiga1200/rtl/paula_floppy_fifo.v`
reads the RAM into its own register `q`, keeps the empty-write bypass in `byp` and muxes `out = sel ? byp : q`;
`core_sources.tcl` and `run_l2.sh` (TG68K=1) take it instead of the shared file. Proof of equal behaviour:
`cores/amiga1200/sim/fifo/run.sh` runs both side by side for 400,000 cycles of random rd/wr/reset/enable,
`cycles=400000 reads=55264 bypass_cycles=107171 full_cycles=108383 errors=0`, `PASS tb_fifo`; the negative control
(one flipped output bit) gives `errors=400000`, `FAIL tb_fifo`. Inference, yosys `synth_xilinx` as a first look:
shared file `192 RAM64M`, the copy `1 RAMB36E1`. Whether Vivado does the same is the next run's
`=== T85 amiga1200 synth | LUT as Memory` line (was 1,490 in `pb_l2` at run 893). The directives of run 909 stay,
so only the FIFO changes against it.

**Run 910 (68692d07), routed:** `=== core amiga1200: WNS 0.039 ns`, `pr_verify passed`; `amiga1200.utilization_pb_l2.rpt`
**14,582 LUT of 18,000 = 81.01 %** (was 86.7 % in run 909), `LUT as Memory 722`, `Block RAM Tile 18.5` (37 %). Vivado
put the FIFO into block RAM as yosys did. Loaded into SRAM on the board the same day (Kickstart 40.68 boots, core
running). **Stripes gone (Alex's photo, 2026-10-20, AGA):** the AmigaVision menu shows the full picture, the cause of
5.6/5.7 is fixed on the board.

### 5.9 Fast RAM 5.5 MB: the slow RAM the cards stole was also served by the chipset (run 910, 2026-10-20)

**Board (Alex, run 910, app `chip 2M slow 1.5M fast 5.5M`):** with fast RAM on, WHDLoad (Another World, Simon the Sorcerer) preloads and then
never takes over; leaving AmigaVision gave Guru `8000 0025` (TRAP #5) with the task block at $0020223C (fast RAM). With fast RAM off Another
World starts.

**Not the cause (read in the code):** the 48-bit line fill suspected first. `nanomig.v:437-560` (cache, line fill, write buffer) is inside
`ifdef ENABLE_CACHE`; no `core_sources.tcl`, no bench and no `-D` defines it, `cpu_cache_new` is not in the tree. The A1200 uses the
`else` branch (`nanomig.v:553`): `fastram_addr = ram_addr[23:1]`, `ram_dout = fastram_dout`, no `fastram_dout48`. `amiga_mem` p2 returns
the right words (`tb_fuzz`, `tb_wide`), and a 68020 pattern test through all three cards (below) finds no error.

**Cause:** `fastram_config = 3` (5.5 MB) makes `nanomig.v:209` clear `slowram_config`, which only the CPU's own port
(`cpu_wrapper` `slowramcfg`) sees. The chipset got the raw `memory_config` (`nanomig.v:1461` -> `minimig.v:673` -> `gary.v:171-173`
`t_sel_slow`), so with `slow 1.5M` it still decoded $C00000-$D7FFFF as slow RAM, and `minimig_sram_bridge.v:74` puts that at DDR2 window
$600000-$77FFFF - the bytes of fast cards 2 and 3 (`cpu_wrapper.v:219`, $600000-$7FFFF -> $600000-$7F0000). Kickstart finds both
(slow RAM at $C00000, fast RAM at $600000), exec adds both to its free lists and hands the same physical memory out twice: tasks, stacks
and loaded code overwrite each other. That fits the Guru (a task running into non-code); the address $20223C lies in card 1, not in the
doubled range, so the Guru is explained only if exec's own structures moved there too - **the board has not shown this yet**.

**Measurement (sim, `tb_l2` TG68K, AGA 8 planes hires FMODE 3 running beside, `MODE=fastpatdma`, `FAST_CFG 3`, `MEM_CFG 0F`):** the ROM writes
$600000+n and then $C00000+n (n = i * $4004, 90 times) and reads $600000+n back. Main (shared `amiga_core.v`):
`fastpat: RAW 0, verify 0, mixed 0, code 0, alias 90` and `FAIL: slow RAM window does not alias the fast RAM stolen from it`. With the fix:
`fastpat: RAW 0, verify 0, mixed 0, code 0, alias 0, flag f457`, `PASS tb_l2 errors=0`. The pattern part (1400 long writes with read-back
over $200000-$77FFFF, read again after all were written, byte / word / long / odd-address mixes across the card edge at $600000 and
at the end $77FFFF, code run from each card then patched and run again) is clean on main and after the fix: the fast RAM data path
itself is fine. Controls (`sh cores/amiga/sim/run_fastpat.sh`): the shared `amiga_core.v` fails with `alias 90`; `FAST_CFG 0` fails with
`flag 0000`. `run_cpu020.sh` still passes.

**Fix (A1200 only, Alex's rule of 5.8):** `cores/amiga1200/rtl/amiga_core.v`, a copy of `cores/amiga/rtl/trx/amiga_core.v` with one change
recorded in `amiga_core.diff` (`tools/check_wrapper_fork.sh` checks both forks): `memory_config` to the nanomig has bits [3:2] cleared
when `fast_b[1:0] == 3`. The same defect exists in the A500 sources when its application offers fast 5.5M with slow RAM; it keeps its
sources and its cache key. **Cost (estimate, no Vivado ran):** two AND gates, no flop.

**Open:** board (next A1200 bitstream): WHDLoad Another World / Simon with fast 5.5M; if it still stalls, the next suspect is the 2 MB
and 4 MB card combinations (not benched with AGA DMA; the pattern test ran at 5.5 MB) and the timing of a real HDF load into fast RAM.

### 5.10 The picture centres on the display window - in the application, not in the gateware (2026-10-20)

**Board (run 910, AmigaVision, PAL hires, 15 kHz, AGA):** the picture sits left and high in the 1440 x 1080 output and a grey bar shows below it.
**Cause (read in the code):** `amiga_fb.v` crops a fixed window out of Denise's stream (`SKIP_X = 36` hires pixels after hblank, `SKIP_Y = 10`
lines after vblank, 720 x 270, every line written to two rows; parameters lines 18-21 of the shared file). The window is right for the standard
screen (DIWSTRT `$2C81`, DIWSTOP `$2CC1`) and for nothing else; what the chipset shows outside the display window is the border colour
(COLOR00), which AmigaVision sets to grey.

**Decision (Alex, 2026-10-20):** the A1200 picture centres itself on the Amiga's display window (DIWSTRT / DIWSTOP, with AGA DIWHIGH), and what
lies outside the display window is black. Then, on the first design (the centring logic in the frame writer): **"In the app?! Move it out."**
(translated: the logic belongs into the application, not into the gateware; it also saves LUTs). Coordinator QC after the first commit: A1200-only
changes live in copies under `cores/amiga1200/rtl` (5.8), the A500 keeps its sources and its cache key; and no destination registers, the pixel
layer's position does the centring.

**Design.**
- *Gateware, A1200 copies only (`git diff main -- cores/amiga/` shows sim files only):*
  - `rtl/minimig.v` (fork, `minimig.diff`): the display window is **snooped from the register bus** (`reg_address` / `custom_data_in`, where CPU and
    copper writes both arrive) with the decode of `agnus_bitplanedma.v` (DIWSTRT `$08E`, DIWSTOP `$090`, DIWHIGH `$1E4` with the ECS/AGA gate):
    40 flops, `diw_o = {vdiwstop, vdiwstrt, hdiwstop, hdiwstrt}`. `rtl/nanomig.v` (fork, `nanomig.diff`) only carries the port. Nothing of the
    chipset's behaviour changed; the layer-2 partition ports did not.
  - `rtl/amiga_regs.v` (fork, `amiga_regs.diff`): read-only `DIW_H` 0C0 and `DIW_V` 0C4 (two flops deep, the application reads twice); the crop window as
    **write-only** registers `FB_SKIP_X` 0D0 (9 bit, hires pixels after hblank), `FB_SKIP_Y` 0D2 (8, lines after vblank), `FB_COLS` 0D4 (7, words of 8
    pixels, <= 90), `FB_LINES` 0D6 (9, <= 270); defaults 36 / 10 / 90 / 270 = what the fixed window was. FEATURES b3 says they exist. A write copies them
    into sys_clk with the existing configuration commit (`cfg_tgl`, `win_b` in `amiga_core.v`).
  - `rtl/amiga_fb.v` (fork, `amiga_fb.diff`): the four numbers are inputs instead of parameters. The window lands at the **top left** of the 720 x 540
    buffer. **Nothing outside the window is written.** A line whose source ends before the window is full is padded with black, word by word (the
    shared file repeated the last pixel for one word).
  - `rtl/amiga_core.v` (fork, `amiga_core.diff`) wires it; `core_sources.tcl`, `sim/run_l2.sh` and `tools/check_wrapper_fork.sh` know the four new forks.
- *Application* (`coreapp/src/fbwin.{h,cc}`, a base library like `mousesink`; the Amiga descriptor names the registers and the three measured
  constants, `coreapp/cores/amiga.cc` `picture_window`, A1200 variant only; `Video::set_view` in `coreapp/src/video.cc`): every 100 ms it reads
  `DIW_H` / `DIW_V` twice, turns them into source coordinates (`x = 2 * diw_x - 182` hires pixels after hblank, `y = diw_y - 27` lines after vblank -
  measured, see the bench), clamps them to the stream (753 pixels, 286 lines), crops what is wider than 720 x 270 to its middle and rounds the width
  down to whole words (the edges lose evenly). Stop <= start, or smaller than a word: the default. Two looks must agree and 500 ms must have passed
  since the last move (a split screen does not flicker the picture). A move is: VIDEO_OFF, 60 ms for the frame in flight, the four registers,
  VIDEO_ON, then **wait until as many frames came as there are buffers** (every buffer has the new geometry; 1 s at most), then `set_view(cols * 8,
  lines * 2)`: the layer shows only that top-left part of the picture (`PX_SIZE`), its position moved so that the part sits where the whole picture
  was (`PX_POS` + half the missing size at the layer's scale), the stride still the picture's. The buffers are not cleared and not rewritten: what
  lies outside the display window is not on the screen; behind the layer it is the host's black. A gateware without FEATURES b3 is left alone; the
  Amiga 500 descriptor has no record.

**Rule since 2026-10-21 (Alex, A1200 PAL board; supersedes the cropping and `set_view` text above):** like MiSTer / PiMiga the picture's height
fills the screen, the width follows the Amiga, a 4:3 screen is pillarboxed; the 3.1 insert-disk screen shows the full 4:3 frame with its purple
border. `fbwin_compute` centres the full frame (90 words x 270 lines = the layer) on the middle of the display window (same mapping) in source
coordinates `mid = (start + stop) / 2`. **Letterbox, since the board result of the same day:** AmigaVision's AGA screen has DIW v `$025-$0ED`
(source lines 10..210, middle 110); centring 270 lines needs skip_y -25, the clamp made it 0 (`picture window: DIW h 081-1C1 v 025-0ED -> at 36/0`),
10 lines of border above the picture and 60 below. Alex: "If we have to letterbox anyway, the border at the top and at the bottom should be the
same size." (translated). So the clamp is gone: `half = min(def_lines / 2, mid, bottom - mid)`, `skip_y = mid - half`, `lines = 2 * half`, with
`bottom = max(src_h, def_skip_y + def_lines)`. Horizontally the same in pixels: `hw = min(360, mid, right - mid)` with `right = max(src_w,
def_skip_x + 720)` = 756, `cols = hw / 4` whole words (odd pixels lost evenly), `skip_x = mid - 4 * cols`. Where the full frame fits centred
nothing changes (the standard screen `$81-$1C1` / `$2C-$12C` gives 36 / 10, 90 x 270); the AmigaVision case gives skip_y 0, 220 lines (10 border lines
above and below its 200). An invalid window returns false and the service keeps the current position. Ten equal looks and `min_apply_ms` 2000 stay.
The service calls `Video::set_view(cols * 8, lines * 2)` at the end of the fill phase **only when the size differs from the applied one** (the
layer centres a smaller view on the screen, the bars stay black; the full size restores the whole layer); a move of the position alone never touches
the layer. Each move prints `[core] picture window: DIW h .. v .. -> at x/y, cols x lines`. The bench lines below that speak of 640 x 512, 80 words
or `PX_SIZE` changes describe the first design.

**Bench lines.**
- Gateware, `sh cores/amiga/sim/run_fbwin.sh` (`amiga_fbwin`, tier 2; `tb_l2` WINCHK, the stripes ROM with DIWSTRT `$2C81` / DIWSTOP `$2CC1`, buffers cleared
  first). Window 1 (skip 56 / 8, 60 words x 100 lines): `winchk: DIW_H 01c10081 DIW_V 012c002c` (the snooped copper-list values),
  `winchk 1: row 100 first yellow column 20, last 475`, `winchk 1: column 150 yellow rows 18..199`, `PASS tb_l2 errors=0`; columns 480..719 and rows
  200..539 stay 0. Window 2 (skip 120, the source line ends inside the window): column 610 is the border colour, column 700 is black, PASS.
  **Negative control** (`A12SHARED=1`: shared `amiga_core.v`, `minimig.v`, `nanomig.v`, fixed window): `winchk: DIW_H 00000000 DIW_V 00000000`,
  `FAIL: first stripe lands at column 20 (source x 76, skip 56)`, `row 100 first yellow column 40, last 671`, `PASS tb_l2 errors=7` (the script asks
  for that). `x = 76` is the first stripe and line 17 the first line of the standard screen: the calibration of the application's constants.
- Application, `tests/coreapp/test_fbwin.cc` (62 checks) with 14 negative controls (`fbw_*` in `tests/coreapp/run.sh`): the standard screen gives
  {76, 17, 80, 256}; an off-centre window, a window past the end of the line, one bigger than the picture, stop before start (also far right, where
  the clamps alone would leave a window), zeros; the sequence VIDEO_OFF -> registers -> VIDEO_ON -> three frames -> `PX_SIZE` 640 x 512 and `PX_POS`
  (320, 28) for the standard screen, (352, 28) / 608 x 512 for the next; the old geometry stays on the layer until the third frame; the timeout
  without frames; one look is not enough; polling faster than the interval does not count; a split screen is not followed; no registers without the
  FEATURES bit; the 500 has no record.
- `run_cpu020.sh`, `run_fastpat.sh` and the A500 build of `run_l2.sh` pass as before (the standard picture is unchanged: first stripe column 40, rows
  14..525). One test expectation changed: the right edge check of `tb_l2` covers columns 690..714 (was 690..719), because the last three columns of
  the default window are now black padding where the shared file repeated the border colour.

**Cost (estimate, yosys `synth_xilinx` of the forks against the shared files, no Vivado ran):** `amiga_fb` +70 LUT +14 FF, `amiga_regs` +43 LUT +113 FF
(the 40-bit two-stage synchroniser, four registers, the DIW read mux), the snoop in `minimig.v` +10 LUT +40 FF, `win_b` in `amiga_core` +33 FF: about
**+125 LUT and +200 FF** in `pb_l2` (first design: +185 / +205). On run 910's 81.01 % that is about 81.7 %. If it must shrink further: the
synchroniser's second stage (-40 FF), or a per-frame latch of the DIW.

**Open / not verified:** no Vivado run; no board. The mapping `x = 2 * diw_x - 182`, `y = diw_y - 27` is measured at one point in simulation (the
standard window) and assumed linear (2 hires pixels per lores unit is the chipset's definition; the lines are 1:1) - an AmigaVision screen on the
board is the proof, as is the pixel layer taking a smaller `PX_SIZE` with the full stride (not run on the board). PAL only (the NTSC vertical
blanking ends five lines earlier; the core is PAL, CHIPSET bit 1 stays 0). A window centred left of the standard one is clamped at the stream's left
edge (it cannot reach back before hblank ends); the right and bottom edges clip at the stream end. A move blanks the picture for the settle time plus
three frames (about 0.15 s). `x_origin`, `y_origin` live in the descriptor, not in the gateware, so a correction after the board is an application change.

### 5.11 Three block-RAM levers for `pb_l2` (run 912 did not converge at 81 %, 2026-10-20)

**Decision (Alex, 2026-10-20):** "No, do all 3 together. Then build. Not incremental." (translated). Run 910's analysis: Paula's floppy
DMA buffer (295 LUTRAM), the two `amiga_sdc` queues (190 LUTRAM) and the Gayle IDE buffer (296 LUT around 2 RAMB36); aim about -700 LUT.
A1200 only: copies under `cores/amiga1200/rtl/`, each the shared file plus a recorded `.diff` (`tools/check_wrapper_fork.sh` checks
all four forks), taken out of the shared glob in `core_sources.tcl` and in `run_l2.sh` (TG68K branch). `cores/amiga/rtl/**` is
byte-identical, so the A500 cache key is unchanged.

**Lever 1, `paula_floppy.v` (`paula_floppy.diff`).** The sector buffer `fd_dma_buf_even/odd` (2 x 256 x 8) had two reads, both
into registers that hold between enables: `fd_dma_buf_out` (fifo side, `!fd_dma_is_writing && clk7_en && fifo_wr`) and
`sdc_byte_out_data` (sd side, `fd_dma_is_writing`, a combinational read at `sdc_byte_addr` with the byte select and bit
interleave in front of the register), and two writes in different states (2 from the sd card, 6 from the CPU path) that never
coincide. Now: the buffer is `{even, odd}` words in two block-RAM copies (one per read register, so each keeps its own hold and
no latency changes), each copy as two 8-bit RAMs (even, odd) with one write statement each (`fd_dma_mw*`; the first version wrote byte lanes of a
16-bit word and failed on the board, see "Finding" below), the sd-side read registered raw and the byte select
(`sdc_byte_addr[0]`, registered with it) behind the register: `sdc_byte_out_data` is an 8-bit 2:1 mux of two registers, the value
and the cycle it appears in are the shared file's. `fd_dma_buffer_trigger_wrD` moved from the always block to the module (the
write enable is a wire now). **Proof:** `cores/amiga1200/sim/floppybuf/run.sh` runs shared file and copy side by side, every
output compared on every clock, driven like the machine (DSKLEN twice, CPU draining DSKDATR for a two-sector DMA read with the sd
model streaming 512 bytes per request, CPU pushing a valid MFM sector - sync, header with checksum, payload with checksum - through
DSKDAT for a DMA write, sd model reading the buffer back): `cycles=110211 sd_reads=8 sd_writes=4 cpu_words=4352 (data 4247)
readback ok=2048 bad=0 errs=0`, `PASS tb_floppybuf`; the 2048 read-back bytes are also compared with the 512 data bytes the bench
encoded (absolute check, independent of the shared file). Negative controls (the copy mutated, same bench): sd byte select
inverted `errs=102027`, fifo read address +1 `errs=7743`, sd-side write lane swapped `errs=97443` (re-measured 2026-10-23 on the four-RAM version), all `FAIL tb_floppybuf`.
The module has no reset for `fd_dma_is_writing`, `cpu_wr_state`, `cpu_wr_cnt`, `fd_dma_buffer_trigger_wr` (they power up 0 in the FPGA, X in
iverilog); the bench deposits 0. A same-address write and read in one cycle returns the old word in simulation as in the shared
file; a RAMB36 is not specified to do so for every mode; the state machine keeps reader and writer of one sector apart (state 4 waits for
the fifo before the next sector is requested), which the bench does not prove for a same-cycle collision.

**Finding (board, 2026-10-22): the byte-lane buffer reads back zeros, and why.** Turrican 2 `.adf` did not boot; chip-RAM dump over JTAG: the
MFM track buffer had 11 sectors with good headers and the right data checksum, every data longword `AAAA` (MFM zero). Headers come from the state
machine, the checksum from the incoming SD bytes (state 2), only the data words come out of the RAM. *Proven* (Vivado 2024.1, `paula_floppy` out of
context, xc7a50t, the A1200 copy of 5.11): the funcsim netlist instantiates `fd_dma_mem_f_reg` (the fifo-side copy, `DOBDO = fd_dma_buf_out`) as a
RAMB18E1 with `.WEA({1'b0,1'b0})` and `.WEBWE(4'b0)`: **no write enable at all, the RAM is never written and reads 0.** The other copy
`fd_dma_mem_s_reg` got `WEA = {fd_dma_mem_s_reg_i_19_n_0, fd_dma_mem_s_reg_i_20_n_0}` (the two byte lanes), `ENARDEN = fd_dma_mw`. The Vivado
inference report listed both as "256 x 16 (READ_FIRST) W | 256 x 16 (WRITE_FIRST) R", with no hint of the dropped write. Variant measured:
the same code with only ONE array (both reads from `fd_dma_mem_f`) gets a driven `WEA` on both RAMB18 - so the fault appears with two arrays that are
written by two identical byte-lane statements (the sd side kept its write, the fifo side lost it). *Not proven:* which Vivado pass removes
the second copy's `WEA` (the log has no message for it; not found in the synthesis log, a 2024.1 inference defect is the reading). The RTL
bench could not see it: the RTL semantics of the two arrays are right, the fault exists only in the synthesised netlist.
**Fix (decision B, Alex 2026-10-22):** block RAM stays; four 8-bit arrays `fd_dma_mem_{fe,fo,se,so}`, each with exactly one write statement in
its own `always` block (enable `fd_dma_mw && even` / `&& !even`), no part-select, reads `{fe, fo}` into `fd_dma_buf_out` (same enable as before)
and `{se, so}` into `fd_dma_sdc_raw` (same enable). Timing is that of 5.11 and of the shared file. **Proof:** `sim/floppybuf/synth_check.sh`
(vivado-env, by hand, not in `ci/sims.txt`) synthesises the module and (1) lints every `fd_dma_mem*` RAMB for a write enable that is not constant 0,
(2) simulates the synthesised netlist (iverilog in the sim image with the Xilinx primitive models; vivado-env has no simulator kernel) with
`tb_floppybuf -DNETLIST` against the shared file on every clock (bits the reference leaves X and the first 100 clocks are not compared: the
netlist powers up 0). Its negative control is the byte-lane version (`sim/floppybuf/paula_floppy_byte_lanes.v`, the file that went to the
board) and must fail the lint and the netlist sim. **Measured 2026-10-23 (builder, Vivado 2024.1):** lint, copy: `fd_dma_mem_{fe,se,so,fo}_reg: write
enable driven`; byte-lane version: `fd_dma_mem_f_reg: write enable constant 0 (never written)`; netlist sim, copy: `cycles=110211 sd_reads=8
sd_writes=4 cpu_words=4352 (data 4247) readback ok=2048 bad=0 errs=0`, `PASS tb_floppybuf`; byte-lane netlist: `MISMATCH` from t=324425000 ps
(the first data word of the first DMA read: reference `03eaae`, netlist `02aaaa`, MFM zero data), i.e. it reproduces the board. In the CI sims
`run.sh` adds `lane_lint.sh` (every block-RAM array 8 bits wide, one write statement, no part-select), with the same byte-lane file as negative
control. The Vivado check is not a CI gate (the Sims job has no Vivado image); until the bitstream stage checks it, run it by hand before any
change to the sector buffer.
**Cost (Vivado synth, OOC, `paula_floppy` with its fifo):** byte-lane version 486 LUT, 2 RAMB18 + 1 RAMB36 (one RAMB18 never written); fix 505 LUT, 4 RAMB18 + 1
RAMB36 (+19 LUT, +1 BRAM tile; the pre-5.11 shared file is about +300 LUT).

**Lever 2, `amiga_sdc.v` + `trx_afifo_rr.v` (`amiga_sdc.diff`).** The shared `trx_afifo` reads `mem[rptr]` combinationally (the
consumer takes `rdata` in the cycle it sees `!empty`), which is distributed RAM: 4 x 156 and 4 x 128 bit = 48 RAM32M. `trx_afifo_rr`
keeps pointers, gray codes, `full` and `empty` and registers the read: `q <= mem[pop ? rptr+1 : rptr]` on every `rclk` edge,
so `q == mem[rptr]` whenever not empty (the word is written before its gray pointer, the pointer needs two `rclk` edges,
`q` is reloaded on each). Only `amiga_sdc` uses it (`amiga_mem`, the other consumer of `trx_afifo`, reads `rdata` while empty and keeps
the shared module); `amiga_sdc.diff` is the two instance names. **Proof:** `cores/amiga1200/sim/afifo/run.sh`: both FIFOs
on two unrelated clocks (10/7, 7/10, 10/10 ns; AW 2 and 3), random push/pop and rare resets, `full` on every wclk edge, `empty`
and `rdata` while not empty on every rclk edge: `pushes=15105 pops=15100 full-cycles=16990 back-to-back-pops=3172 errs=0`
(and the other two ratios, `errs=0`), `PASS tb_afifo`; negative control without the read-ahead address `errs=8864`, `FAIL tb_afifo`.
`cores/amiga/sim/run_sdc.sh` now also runs the whole sector service bench (bytes and sectors) on the copy: `PASS tb_sdc errors=0` twice.

**Lever 3, Gayle `io_buf` (`trx_tdp16`): nothing to fix in the RAM, the 296 LUT are the logic of `ide.v`.** yosys `synth_xilinx`:
`trx_tdp16` alone is 2 LUT2 + 2 RAMB36E1 (4096 x 16 as two 4Kx9 side by side, no output mux); `ide` with its two `trx_tdp16` is
343 LUT + 4 RAMB36E1 (146 + 69 FF, 12 CARRY4). `gayle.v` instantiates `ide` twice (`ide0`, `ide1`, `port_num = addr[12]`), so
8 RAMB36 and about 2 x 300 LUT are the two IDE channels; the "296 LUT around 2 RAMB36" of run 910 is consistent with one channel's `ide` row (343 LUT in yosys, Vivado
needs fewer); the report itself was not available here. A wider/narrower split or cascade attributes would not remove LUT that are counters (`io_cnt`, `mgmt_cnt`), status
and read multiplexers. Not changed. **Option for Alex (a feature, not an area trick):** channel 1 is HD1 (`hdf_present[1]`, `ide_req[5:3]`);
a core with one HDF could drop `ide1` (about -300 LUT, -4 RAMB36); not done, nothing decided.

**Numbers (yosys `synth_xilinx -flatten`, a first look, not Vivado):**

| module | shared | A1200 copy |
|---|---|---|
| `paula_floppy` (with the BRAM fifo) | 588 LUT logic + 48 RAM64M + 1 RAM32M (about 784 LUT sites), 1 RAMB36 | 532 LUT + 1 RAM32M (about 536), 1 RAMB36 + 2 RAMB18 |
| `amiga_sdc` | 595 LUT + 48 RAM32M (about 787 sites) | 593 LUT, 5 RAMB18 + 2 RAMB36 |
| `ide` | 343 LUT, 4 RAMB36 | unchanged |

Expected: about -250 (lever 1) and -190 (lever 2) LUT, about 440 in all, not the 700 aimed at: lever 3 gives nothing. BRAM tiles:
+1 (lever 1) and +4 to +6 (lever 2, Vivado picks RAMB36 or RAMB18 pairs), 18.5 of 50 now, so about 25 of 50. The A1200 build with
the three directives of 5.8 decides, as for the FIFO of 5.8.

**Benches that stay green** (all `PASS`): `run_cpu020.sh` (TG68K build with the copies, fx68k control fails), `run_fastpat.sh` (+ both
controls), `sim/fifo/run.sh`, `run_sdc.sh`, `run_l2.sh` (A500, fx68k, unchanged), and the A1200 build (TG68K, AGA, 2 MB, sector sizes) of
the two paths the copies sit in: `DISK=1 MODE=disk` (track read by DMA from an ADF through the block-RAM buffer, 11 sectors decode to the ADF's
bytes) and `IDE=1 MODE=ide` (HDF sector through the Gayle mailbox and the copied `amiga_sdc`), both `PASS tb_l2 errors=0`, now in
`run_l2check.sh`. New benches registered in `ci/sims.txt` / `ci/sims.deps`: `amiga1200_floppybuf`, `amiga1200_afifo`.

### 5.12 The interrupt guard in the core application (2026-10-23, Alex: "Then build it")

**Problem.** The fourth hang with the same picture (10-21 module playing, 10-22 AmigaVision menu, 10-23 AmigaVision loading from HDF;
JTAG `rvpc` while hung: IRQ line 1, IRQ edge count frozen at 35,496, PC moving). The core poll task runs with `mstatus.MIE` 0 and no
critical section open; the tick stops, `PollTask::step` returns 0 for ever (its clock is the tick) and the task spins on `taskYIELD()`.
The HDF sectors come from that task, so the Amiga stops loading. Who clears MIE is still open: the software's MIE writes were read on
10-22 (clean); the rvlite trap, `mret` and interrupt-insertion paths (`csr.vhd`, `execute.vhd`, `decode.vhd`, `bus_converter.vhd`) were
read on 10-23 without a finding. Alex asked whether the core or a measurement bus should carry it: no - the processor is the host's,
not the core's; a trace in the host is a host change (frozen).

**Decision.** In the application (`coreapp/src/run.cc`, `irq_guard`): every `BoardOs::sleep_ms` (the executor's and the poll task's
only way to give up the processor) reads `mstatus`; MIE 0 there is always the defect, because nobody may sleep or yield inside a critical
section. It sets MIE again and logs `irq guard: MIE was 0 in '<task>' (mstatus, mepc, mcause), hit n` (first eight hits, then powers
of two); `irq_guard_hits` and `irq_guard_last[4]` (mstatus, mepc, mcause, tick) are symbols for a JTAG read. `mepc`/`mcause` name the
last trap taken, the first candidate for the one that returned with MIE 0.

**Scenarios.** S1 (control, every start): the start sequence clears MIE and yields once through `sleep_ms(0)`; the console must say
`irq guard: self-test ok` (one hit). Without the guard the application stops right there (the control can fail). S2 (board): the next
occurrence prints the guard line and AmigaVision keeps loading; the line's `mepc` goes into this section. The menu loop sleeps through
FreeRTOS directly, not through `BoardOs`: a hang there is not covered.

**Board, 2026-10-24 (release `trx64mc-main-54e63c47`, A1200, AmigaVision): the guard caught it, the machine kept running.** Console:
`irq guard: MIE was 0 in 'Core poll' (mstatus 00000080, mepc 00085D04, mcause 0000000B), hit 6` and again as hit 7 (hits 1-5 earlier
in the same session, the first the self-test). Read from `coreapp.bin` of the package (objdump at VMA 0x30000): 0x85D04 is
`mul a0,a0,a5` (`BoardOs::now_ms`, tick x 5), not an `ecall`; yet `mcause` 0x0B is the rvlite's ECALL cause (`decode_comb.vhd:199`, the
only place that sets `0 & X"B"`; an interrupt carries bit 4, `11011`). So the last trap was taken as an ECALL with the PC of a `mul`:
the instruction word and the PC of one decode did not belong together, or a trap was recorded for a stage whose PC was already the next
one. `mstatus` 0x80 (MIE 0, MPIE 1) is the state between a trap's entry and its `mret`. Not yet read: where the ECALL in Core poll's
path is (`taskYIELD` in `BoardOs::sleep_ms`) relative to 0x85D04 and the icache line it shares, and why the handler's `mret` did not
restore MIE. The processor is the host's (`apps/ultimate/fpga/cpu_unit/rvlite`, frozen): the guard stays the remedy until Alex decides.

**Decision (Alex, 2026-10-24):** "An agent should investigate what would have to change in the CPU and, on a branch, see whether
host+trx64 route with the changes." (translated) - an investigation and a trial route on a branch (`t97-rvlite`), no merge into the
frozen host without his next go. Also from the board: the A500 plays its intro and Turrican with clean sound; the A1200 has none.

**The A1200 has no sound (Alex, board, 2026-10-24, release `trx64mc-main-54e63c47`; the A500 in the same release is clean; Alex remembers sound on the
generic application in earlier releases). Read and measured 2026-10-24, cause NOT found.**
- *Gateware path is the A500's.* `amiga_audio` (FIFO, resampler, `mute = pause_eff`, `clr = hold`), the Paula audio files and `layer2_amiga.v`'s
  `audio_l/r` are shared; the A1200 copies (`cores/amiga1200/rtl/*.diff`) touch no audio line (nanomig/minimig: display window; paula_floppy: the sector
  buffer and, since 3af0a8a45, `mount_seen`; amiga_sdc: queues and the pending flags; amiga_regs: window registers; cpu_wrapper). The application has no
  audio register: the generic descriptor writes the same bytes as `cores/amiga1200/app_regs.txt`, `AM_*` has no audio/volume/mixer register, and the old
  application (`t85-amigaapp`, `software/io/amiga/amiga.cc`) wrote none either (it only hid the C64 mixer rows). The host mutes only on `c64_halted |
  app_c64_pause` (`host/top.v:1173`), i.e. in Freeze.
- *What changed for the A1200 between the releases.* `partial_sha256` of `amiga1200.cor` is `378dcfa3...` in 2895ec1a, f6f3dfdc, 66eefd51 and c82b04a2
  (only the application differs there: `app_sha256` bdfed0c5 / 55428f41 / 80254c26); 54e63c47 is the first with new gateware (3af0a8a45: `mnt_pend`,
  `mount_seen`) and also the first with the interrupt guard (2889e01fb) and `disk.create` (T122). `amiga.cor` (A500) is byte-identical in all of them.
- *Measured (builder, `trx64-sim-env:4`, `run_l2.sh` audio case with `TG68K=1 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1`, the A1200 as built):* `audio_l`
  2,048,256 with the gateware of HEAD and the same with `SDC_OLD=1 PF_OLD=1` (the state before 3af0a8a45); FIFO KPIs under 0 / over 0, fill 481. So Paula's
  tone reaches the core output on the 68020 build, before and after the disk-change change. `run_l2check.sh` (the same case with fx68k) times out in CI
  (`amiga_l2` 3000 s, run 956), so that case was not in a green CI line for 54e63c47.
- *Not excluded.* (1) The machine's real-time rate with AGA/2 MB/WHDLoad: the FIFO only plays while it is filled to 480 pairs in machine time; the A500 ran
  4-9 % too slow under Turrican (T85, 2026-10-19), the A1200 was seen with `catch-up lost 65535 ticks` and 207 underflows (above, 2026-10-20); no
  audio KPI of the A1200 has been read since the generic application replaced the fork's, which printed `audio: fifo ... underflows` every ten seconds
  (`STAT_SEL` 8-11). (2) Software the tone ROM does not do (Kickstart 3.1 `audio.device`, long-word writes to `AUDxLEN/PER`, samples above 1 MB chip).
  The next board step that decides: read `STAT_SEL` 8-11 (under/over/fill) and `STAT_SEL` 11 (catch-up) on the A1200 while AmigaVision should be playing.

**2026-10-24, sound comes and goes (Alex: "now there is music again ... weird. So it is 'chance'", translated); a KPI line.** The
Amiga core has no JTAG words (`layer2_amiga.v`: `trx_j_rdata = 0`), so the audio counters cannot be read from outside. The generic
application's Amiga service is now `amiga.poll` (`coreapp/cores/amiga.cc`): `engines_poll` (the former `media.poll` body, `engines.cc`)
plus, every 10 s while the machine runs, `[amiga] audio over .. under .., fill now .. min .. max .. (target 480), catch-up lost .. ticks,
deepest debt ..` from STAT_SEL 8-11 (FEATURES b1/b2). The next silent start names whether the FIFO runs dry (under, fill) or the machine
falls behind real time (catch-up). Tests: test_desc 695, test_winslot 54, test_sectors 81, test_amiga500 28, fbwin 107 checks, 0 failed.
### 5.13 The hang: an interrupt over a multiply is trapped twice (rvlite, patch 0006) — cause found, bench, fix on branch `t97-rvlite` (2026-10-24)

**Cause.** `tools/patches/gideon/0006-rvlite-mul-pipeline.patch` (the multiplier pipeline, 2026-10-05) holds a multiply in `execute`
for two cycles and keys that on `exec_i.reg_write_sel = WB_MUL_L/H` alone (`execute.vhd`, `mul_inst`, line 108 before the fix). The
interrupt dummy that `decode_comb.vhd` puts in place of an instruction (`procedure irq`, line 66; the illegal-instruction dummy is the
same) clears `reg_write` but keeps the `reg_write_sel` of the instruction it replaces. An interrupt that lands on a `mul`, `mulh`,
`mulhsu` or `mulhu` therefore reaches `execute` as a "multiply": `mul_hold` stalls it for two cycles. `csr_req.trap.trap` is
`inst_valid and flow_target = TRGT_TRAP` (`execute.vhd:142` in the fixed file), a level, and `csr.vhd:101/120` applies it in every cycle with `ena_i`
(= `wb_ready`) high: the first cycle takes MPIE := MIE (1), MIE := 0, the second MPIE := MIE (0). The handler saves `mstatus` with
MPIE = 0 (portASM `csrr t0, mstatus` / `store_x t0, 29*4(sp)`), its restore does `csrw mstatus` and `mret`, and the task continues with
MIE = 0 and MPIE = 1: **`mstatus` 0x80**. With the tick off, `PollTask::step` returns 0 for ever (5.12).

**The board evidence fits, and the "impossible" line is not one.** `mepc 00085D04` is the `mul` of `BoardOs::now_ms` where the tick hit
Core poll; portASM restores the resumed task's `mepc` with `csrw mepc` (`processed_source`), so `mepc` read later in that task is its own
resume address. `mcause 0x0B` is whatever trap came last (an ECALL, another task's `taskYIELD`), as no trap followed: `mepc` and `mcause`
of the guard line come from two different traps. The ECALL-decode / PC-of-a-mul puzzle of 5.12 needs no fetch or I-cache defect; the
fetch -> decode hand-over (word and PC) is checked by the bench and holds. Why Core poll: its loop is `ecall` plus `now_ms` (`mul`) and
interrupts arrive often. The defect is latent since 10-05 and needs an interrupt on a multiply.

**Bench** `host/sim/rvtrap` (CI `rvtrap`, `rvtrap_ctl`). The rvlite as the host builds it (`rvlite_wrapper`: core, I-cache, both
`bus_converter`s, arbiter; the vendored sources with patches), `prog.S` (assembled in `u64-build-env:ci`, `build.sh`; `prog.hex` is
committed): two tasks under a FreeRTOS-style handler (`ecall` = yield with context switch, level interrupt = tick, restore by `csrw
mepc` / `csrw mstatus` / `mret` as portASM), a task-side MIE check like the guard, code placed so that the I-cache refills on every
call and return. Memory and IO answer after random wait states, the interrupt rises at random phases and falls on the handler's IO
write (and now and then by itself). A monitor (`monitor.inc`, spliced into a sim copy of `core.vhd` by `splice.py`, so it reads the
core's own signals) checks: the word decode takes equals the program memory at its PC; every retired instruction is the PC its
predecessor leads to (pc+4, branch/`jal` target, `mtvec` after a trap, `mepc` after `mret`); an ECALL-cause trap only on an ECALL word,
an interrupt dummy only while MIE = 1; `mstatus.MIE/MPIE` and `mepc` against a model that is updated by what retires. Results of
both tasks are compared with a run without interrupts. Builder, `trx64-sim-env:4`, GHDL, niced, one process.

- Old RTL (the tree as it was at `6c13cf7e1`, patches up to 0006), seeds 1-3, periods 300/900/2500, waits 0/3: **FAIL, 18 of 18 runs**.
  First error of the typical run: `TB: ERROR @7588 mstatus MIE/MPIE '0''1' but retired instructions give '1''1' (last: mret pc=00000284)`,
  next cycle `... '0''0' ...`, then `interrupt taken with MIE = 0 (last: interrupt dummy pc=000004D0)` (0x4D0 is `mul a0,s3,s4`). A cycle
  history of the first error: `mret` retires with MIE := 1; the interrupt dummy for 0x4D0 is in `execute` with `trap=1`, `exrdy=0`,
  `eov=0` for two cycles, `ms` goes `11` -> `01` -> `00`. Per run 9 to 503 errors and, in the software, `sw_MIE0` (the task-side MIE check
  firing) between 3 and 158, e.g. `seed 3 period 300 waits 0: FAIL TB: result A=7440EA3A B=FFD0814B sw_MIE0=158 irqs=502 errors=503`.
  The task results are unchanged (the damage is the interrupt state only).
- Fixed RTL (patch 0007), seeds 1-6 x periods 300/900/2500 x waits 0/3: **PASS, 36 runs, 0 errors**, `sw_MIE0=0`, results
  `A=7440EA3A B=FFD0814B` equal to the reference run without interrupts, 28 to 623 interrupts per run.
- Controls (`MODE=ctl`, each must fail; on the fixed RTL): `mret_nop` (`mret` does not restore MIE), `trap_twice` (a trap writes MPIE := 0),
  `csr_ena_high` (`ena_i` tied high): `ctl| all controls failed as required`.
- A reference run (no interrupts, no waits) must reproduce the result words; the bench cannot pass with a program that does not run.

**Fix** (`tools/patches/gideon/0007-rvlite-mul-hold-only-for-a-multiply.patch`, on top of 0006; `vendor/gideon` is generated from the
submodule and the patches, the submodule itself is untouched): `mul_inst` also requires `exec_i.reg_write = '1'`, which every real
multiply has (`decode_comb.vhd:147`, set for the whole ALU_REG group) and both dummies clear. One AND term in `execute.vhd`, no state.
The CSR level-apply is left alone: after the fix only a multiply is ever held, and a multiply has no CSR action.

**Trial build** (builder, `vivado-env:2024.1`, `docker run --init`, the host stage as `ci/bitstream_layers.sh` `build_host` runs it:
`TRX_STAGE=host TRX_BUILD=build_host TRX_CORE=""`, `TRX_SID=redip2 TRX_FEATURES=jtag T79_GREYBOX=1 T79_FLOOR=B` as the workflow sets
them, a private `/cache`, host and reference core `trx64`, worktree of this branch with patch 0007; 30 min):
`WNS +0.056 ns, WHS +0.032 ns`, `All user specified timing constraints are met`, 0 failing endpoints of 59,642; route status
`Number of Failed Nets = 0` at the end of the router (the 118 of the intermediate phase are resolved); `timing_neg.rpt`: no paths.
`pb_l1`: 10,171 of 13,000 LUT = **78.24 %** (8,293 + 1,878 fixed; the figure of the post-`opt_design` report, as the CI log's `T79 pb_l1`
line), 21.5 of 25 RAMB36 (86 %), 9 of 40 DSP; `pb_l2` (reference core): 14,042 of 18,000 = 78.01 %. Whole device: 23,430 Slice LUTs
(71.87 %), 60.5 BRAM tiles. `c64_basic.bit` and the partial were written; `host.bit` (grey box) did not, as known (T94). No baseline of
the unpatched host was built here (the change is one AND input in one stage); against the 75-78 % band of the skill the 78.24 % is at the
top edge, and a one-term change moving it is placement, not logic.

**Not proven.** Nothing on the board: the cause is proven in simulation on the vendored RTL and the board evidence of 5.12 fits it
line by line, but only a board run of a bitstream with 0007 (several hours of AmigaVision loading from HDF, the guard line staying
silent) closes it. The guard (5.12) stays in the application meanwhile. The bench has the software's tick (IO ack) and the core's
signals; it does not have the real interrupt controller, the DDR2 path or the Amiga core's bus load. Whether the A1200's missing sound
(5.12) has anything to do with this was not looked at. Branch `t97-rvlite`: not pushed, `contract/host_pin.txt` not updated, not merged;
the host is frozen and this patch changes `vendor/gideon` (host key) - Alex's decision.

### 5.14 The A1200 as a LARGE core: one flat bitstream, started as a `.cfw` (2026-10-25)

**Why.** In CI 961 the A1200 (14,158 LUT, 78.7 % of `pb_l2`) no longer routes as a partial against the new host (reference core `c64ref`).
**Decision (Alex, 2026-10-25):** "Kannst auch einen Agent losjagen, der ihn dann als single core umbaut schon und später müssen wir dann die
pipeline dafür klären" ("you can also send an agent that already rebuilds it as a single core; the pipeline for it we settle later",
translated). So the A1200 becomes a large core like the C128 (T113 Q12): one full bitstream of the XC7A50T, no partition, started by the host
through the full-firmware path (T115 §7: `.cfw`, scratch file, IPROG). The partial recipe (`core.toml`, `core_sources.tcl`) stays as it is until
the pipeline decision; nothing here changes what the pipeline builds.

**What is where**

| piece | file | taken from |
|---|---|---|
| flat top | `cores/amiga1200/mono/build_mono.tcl` | structure of `cores/c128/mono/build_mono.tcl` (branch `t113-reset`), the C128 machine left out |
| the A1200's file lists | `cores/amiga1200/core_sources.tcl` | unchanged content; stops after defining `mm_v`, `a12_vhdl`, `a12_defs`, `a12_generics` when `TRX_MONO=1` (the partial's synth reads the same variables) |
| build entry | `cores/amiga1200/mono/build.sh` | new (the C128 has `ci/bitstream_mono.sh` only) |
| descriptor | `cores/amiga1200/mono/core.toml` | C128 style: `delivery = "cfw"`, `mono_env`; **not** `core.toml` - see below |
| stage script, key, `.cfw` packer/checker | `ci/bitstream_mono.sh`, `ci/lib.sh` (`mono_key`, `build_update_core`), `tools/pack_cfw_mc.sh`, `tools/cfw_core.py`, `tools/core_toml.py` | `t113-reset`, generic parts only (commit e5877357b) |

**Flat top contents.** The host's own files, read exactly as `host/build.tcl` lists them and not edited (`contract/host_pin.txt` stays valid):
Gideon's RISC-V and the blocks around it (`$gvhdl` of `host/gideon_vhdl.tcl`: UART, SPI/SD, Ethernet, USB through `usb_phy.v`, I2C),
`top.v`, the DDR2 controller and arbiter (`ddr2_ctrl`, `ddr2_lean`, `cal_seq`, `ddr2_port`), the pixel layer and text (`pix_layer`, `c64_text`),
the scaler (`vic_scaler`, `pal_ram`), HDMI (`vendor/icepi/hdmi`, `hdmi_gtp.v`, the GTP IP), the clock chip (`cdce_i2c`), keyboards
(`c64u_keyboard`, `bling_keyboard`, `app_input`), overlay, JTAG debug master, `dfx_decouple` (present; nothing swaps, so it is never closed), the five TRX modules of
`cores/trx64` and `cores/common/iec_bus.v`; plus the A1200 as layer 2: `cores/amiga/layer2_amiga.v` renamed to the module `layer2` that
`top.v` instantiates (a copy in `host/<build>/layer2_mono.v`, the three generics `CHS_INIT=16 MEM_INIT=3 HDF_SECT=1` written in as parameter
defaults; `top.v` passes none), minimig-aga / nanomig / `trx/*` and the A1200's own copies in `cores/amiga1200/rtl` with TG68K (VHDL).
Defines: `INFER_DPRAM ENABLE_TG68K` (the partial's) and the host switches of the DFX build (`NO_UCI NO_STREAMS`; JTAG debug master on).
Not in it: the pblocks `pb_l1` / `pb_l2` / `pb_cpu`, `HD.RECONFIGURABLE`, out-of-context checkpoints, the host cache, the ICAP swap, the
reference core `c64ref`, Gideon's 1541 VHDL (the Amiga has no drive).

**Build command** (builder, `vivado-env:2024.1`, nothing else running; `vendor/gideon` must exist, `tools/gideon_sources.sh`):

    docker run --rm --name a1200-mono -v $PWD:/src -w /src vivado-env:2024.1 nice -n 15 timeout 3h cores/amiga1200/mono/build.sh

Output `host/build_mono_amiga1200/amiga1200_mono.bit` plus `timing.rpt`, `utilization*.rpt`, `route_status.rpt`; exit 1 on no bitstream or WNS < 0.
The `.cfw`: `cores/amiga1200/mono/build.sh --pack --bit <bit> --app-bin <coreapp.bin>` (= `tools/pack_cfw_mc.sh`, needs the `container` CLI and
`u64-build-env`); `coreapp.bin` is the generic core application of `tools/build_coreapp.sh`, the A1200 being the record `amiga1200_desc` in
`coreapp/cores/amiga.cc`. `tools/pack_cfw_mc.sh` renames the bitstream's design to `amiga1200` (the row's identity, T115 D7) and picks
`cores/amiga1200/logo/icon.rgb565` as the row's icon. Bench: `tools/test_cfw_core.py` (33 tests, the packer with a stub, against the A1200).

**Where the descriptor sits.** Not in `cores/amiga1200/core.toml`: on `t115-soc` that file is what makes the DFX stage build the core, and
that branch has no `bit-mono` stage - `delivery = "cfw"` there would make `ci/package.sh` expect a `.cor` that is never built. So the large-core
descriptor is `cores/amiga1200/mono/core.toml`; `tools/core_toml.py` does not list it, `--check` validates it, and `get` / `env` read it when
`TRX_CORE_DESC` names it (`mono/build.sh` sets that). When the pipeline takes the core as a large one, the file moves up to `core.toml` and the
partial descriptor goes (the change on `t115-soc` that drops `core.toml` from the DFX build does the second half).

**What the pipeline needs later (open, Alex decides).**
1. `.gitea/workflows/bitmono.yml` and the `bit-mono` call in `pipeline.yml`, `ci/package.sh` / `ci/update.sh` / the release scripts taking
   `<core>_mono.bit` and `build_update_core` (all in `t113-reset`, not taken here), the skip of `delivery = "cfw"` in `ci/bitstream_layers.sh`.
2. `mono_key` (`ci/lib.sh`) keys on the host, the reference core `c64ref` and the core's `sources`; with `core.toml` gone the A1200 needs its
   `sources` from the moved descriptor.
3. Which pipeline builds `coreapp.bin` for a `.cfw` of this core (the C128 uses stage `trxapps`), and the boot-menu line of the `.cfw`.
4. `host/layers.txt`: `cores/amiga1200/mono` is under `cores/amiga1200`, so it is covered while the A1200 has a `core.toml`; after the move
   it needs a section (the layer check will say).
5. The partial's cache entries and `amiga1200.cor` leave the release set when the DFX build drops the core; `app_regs.txt` / the `.app`
   stay (the same application descriptor serves the `.cfw`).

**Trial build: not run.** The builder had a `GITEA-ACTIONS-*` container (a Bitstreams host job, task 3257) from 12:18 to past 15:09 CEST, polled every
10 minutes for 3 hours; by the rule (no Vivado beside a CI run) nothing was started. Checked without Vivado: the Tcl files parse (`info complete`), the
`layer2` rename and the three parameter substitutions hit exactly once, `tools/test_cfw_core.py` passes, `ci/layers_check.sh` passes. Unchecked until
the first run: that the flat design synthesizes (module-name clashes between host and Amiga Verilog were scanned: none; VHDL library clashes of TG68K
against Gideon's were not run), its LUT/BRAM/DSP, whether it routes, WNS/WHS. `vendor/gideon` was generated from the main checkout's `apps/ultimate` for it.


### 5.15 Application defects of 2026-10-28: RAM line in Home > Status, Kickstart Auto, ':' (2026-11-07, wave 1)
- **RAM line: dropped (Alex, 2026-11-08: "drop it", translated; the settings show the memory setting). Reverted first (2026-11-08).** Built on w1-t97 (`CoreDesc::status_items`, `home_status_ram`), merged as d04a611c8 and reverted in ee529362d:
  with it the core application halts at start in ue2emu (vAssertCalled in vQueueDelete, run 1296 emu; bisected f87a5e73f PASS, d04a611c8 FAIL). Cause
  not found; one guess (unproven) is the core app task's stack, which a larger `CoreDesc` copy (`bundle.cc`, `apphost.cc`) could overflow.
- **Kickstart Auto: not reproducible from the code.** `ks_pick` with the three real files (`/path/to/Development/C64/Tools/amiga-roms`: kick13.rom 34.5,
  AmigaVision.rom and MegaAGS-Kickstart.rom 40.68) answers the first 3.1 for the A1200 and kick13 for the A500; `test_romset.cc` pins both. The board's
  console line `[core] Kickstart <dir>/<file> (... version V.R)` (`romset.cc`, `romset_put`) names what was loaded - needed to go on (board step).
- **':' as '=': not reproducible from the code.** The tables (`amiga_keys.h`, `amiga_input.v` matrix map) contain no path from ':' to '=' (raw 0x0C);
  which keyboard and which Kickstart keymap were in use is not recorded (board step).

### 5.16 The wide fetch returned the word at A-2 for fetches at 4 mod 8 (2026-11-17, GitHub Discussion #94, Turrican II AGA)
Two testers saw wrong 16-px bands every 32 px in the background of Turrican II AGA on the A1200 (pre-release main-13c2cde9), with every Fast
RAM setting; with Zorro III 32 MB the bands also moved. The OCS Turrican 2 always ran (Alex). Found by reading (scratchpad amiga-z3/READ.md):
`dout48` / `p2_dout48` of the A1200 `amiga_mem.v` returned words 1..3 of the 4-word group whatever word was asked for (§5.6). FMODE 1/2 show
`[47:32]` as pixels 16-31 of a fetch, so a fetch at 4 mod 8 bytes got the word at A-2 instead of A+2; at a row start A-2 lies outside the row
(the left edge). OCS (FMODE 0) never reads `chip48`; FMODE 3 at 8-byte alignment is not affected. §5.6's "an unaligned request is not a wide
fetch" no longer holds: 4 mod 8 is a valid FMODE 1/2 fetch.
- **Fix** (commit 09e1b0b58, A1200 copy only): `[47:32]` is always the word at A+2 (hit: bank `{r_w[2:1],1}`; miss: `u_w[1] ? group word 3 :
  word 1`); a request at 0 mod 8 bytes keeps the full {+2,+4,+6}. The A500 copy keeps the old lines: it has no AGA, so it never reaches them.
- **Proof:** sim `amiga1200_wide` (`cores/amiga1200/sim/wide`, chip, Zorro II and Zorro III port, hit and miss; commit f9a9594a8 adds six
  first reads at word 2 and word 6 of fresh lines, each checked to be a miss): "wide fetch: 120 reads, 0 errors; amiga_mem reads 165 miss 12" /
  "PASS tb_wide". Controls: `OLD=1` (both old lines) fails with 34 errors, `OLD=crit` (only the miss line old) fails with 6 errors, exactly the
  six miss reads (a pre-push review found that the first bench never missed at 4 mod 8). The A500 bench `amiga_wide` asserted the old rule, which
  is why no bench caught it.
- **Zorro III:** the reading found no defect in the Z3 path (address, cache tags, DDR2 users, write ordering). The moving bands are most likely
  the same defect showing whatever lies at A-2. If they remain after the fix, next is the 32-bit addressing that only the Z3 setting unmasks
  (`cpu_wrapper.v:284`).
- **Open:** board check with Turrican II AGA. Test build `trx64mc-test-73fb8d63` (GitHub pre-release, `amiga1200.cor` from Gitea run 2376,
  WNS +0.085 ns) is out; the retest is asked in Discussion #94 (Alex 2026-11-17: push to GitHub and ask for a retest).
- **Retest results (#94, 2026-11-17):** Turrican II AGA "Graphic solved", the music stutters a little with Fast RAM 5.5M or 32M (not
  analysed yet). Worms DC (AGA): no sound, the picture shows ~100 ms then 1.5-2 s blank. Reading (scratchpad amiga-z3/WORMS.md): the
  change cannot cause it - `chip48` reaches only Denise's bitplane and sprite pixels; `p2_dout48` feeds a line fill under `ENABLE_CACHE`,
  which no build defines; the two .cor differ only in these two lines (app identical but the version stamp). Ranked candidates: (1) the
  T188 frame follow on a 50 Hz HDMI mode when the game's frame rate leaves ~49.2-50.3 Hz (NTSC BEAMCON0 or a VARBEAMEN mode): the HDMI
  frame length jumps up to 24 lines, a TV re-syncs and mutes for 1-2 s; T188 S4 was never recorded as passed; (2) a reset/Guru loop
  (e.g. Zorro III 32M left on: 32-bit addressing); (3) a chipset stall (weak fit). Separating step: the same game with the old
  main-13c2cde9 `amiga1200.cor`, and with a 60 Hz HDMI mode (the follow is off there).
- **Wide fetch, unaligned pointers (WinUAE custom.cpp as the nearest reference, read 2026-11-17):** the new rule equals WinUAE for FMODE 1
  at every alignment and for FMODE 3 at 0 and 4 mod 8. Same in old and new, not needed for the reports: FMODE 3 at 2/6 mod 8 (word 3 in
  [31:16]), FMODE 2's duplicated word, and the pointer step that drops the low bits after the first fetch (A1200 forks of
  `agnus_bitplanedma.v` / `agnus_spritedma.v`).
