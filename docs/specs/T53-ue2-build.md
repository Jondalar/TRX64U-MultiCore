# T53 — Ultimate 64 Elite II (XC7A100T) build

**Status:** placeholder (Alex, 2026-11-01: "that is only a placeholder until we know the 100T geometry ... that only comes with the C77 board, which comes out from February or so"): stage `bit-ue2` stays; no board has a 100T yet. Spec 2026-10-03, branch `t53-ue2` (on top of `t31-ci`). **Task:** TODO.md T53 (M1, RC item).
**Shares with:** T31 (the pipeline stage `bit-ue2`), T49 (the update package carries both bitstreams),
T15 (boot ROM picks the app address by FPGA type), T50 (updater picks the bitstream).
Evidence: Gideon's public constraints (GitHub issue 615, copy in the tools fork `pinmap/c64u_gideon_issue615.xdc`),
the headers of his `external/u64e2_50t.bit` / `u64e2_100t.bit`, Vivado's package data for both parts
(run on the NAS, section 2), `update_u64ii.cc`, `bootloader_u64ii.c`. No UE2 hardware is here; no UE2 VHDL of
Gideon's beyond the files the app repo ships was read. Gideon's UltiSID, Copper and lp_filter sources were not opened.

## 1. Goal

`designs/c64_basic` builds for the XC7A100T-FGG484-1 as a second bitstream, `c64_basic_100t.bit`, by the pipeline's
optional last stage. CI build only: whether it runs on a board is for Mike (has a UE2) and Till (two) to say.

## 2. Is the UE2 wired like the C64U? Evidence

| Evidence | Reading |
|---|---|
| One XDC, titled "C64 Ultimate / U64 Elite II constraints" (issue 615) | Gideon publishes one pin file for both boards |
| Both `u64e2_50t.bit` and `u64e2_100t.bit` carry the design name `/u64_mk2_artix`, Vivado 2024.1, built 2026-09-10 11:00 and 11:05, parts `7a50tfgg484` and `7a100tfgg484` | one source tree and one constraint set, built for two parts |
| **Vivado package data, both parts** (`get_package_pins`, 484 balls each): the same 484 ball names; for every ball our design uses (259 `PACKAGE_PIN` in `pins.xdc`, `gtp_pins.xdc`, `usb_pins.xdc`, `constraints/c64u.xdc`, plus the 49 DDR2 pads of `mig_ddr2.prj`): **identical bank and pin function on both parts, none differs** | the footprint is compatible for everything we constrain |
| The 35 balls whose function differs between the parts are all `NC` on the 50T and bank 13 I/O on the 100T (bank 13 does not exist on the 50T package) | extra I/O the C64U board never uses; whether the UE2 board routes anything to them is not known, and our design does not touch them |

Conclusion: **same pin file, same part package, only the FPGA size differs.** `pins.xdc`, `gtp_pins.xdc`,
`usb_pins.xdc`, `c64_core_ooc.xdc` and the MIG pin map are used unchanged. T49's earlier remark "the UE2 has another FPGA and
pinout" is the FPGA only. An own pin file is not needed. The check is repeatable: `tools/pincheck.tcl`
(section 4).

## 3. What does differ, and what the build has to do

1. **Part**: `xc7a100tfgg484-1` for synthesis, the out-of-context C64 core, the IP cores and the MIG.
2. **MIG project file**: `constraints/mig_ddr2.prj` names `<TargetFPGA>xc7a50t-fgg484/-1</TargetFPGA>`. The build
   writes a copy with the part replaced into the build directory (no second file to keep in step) and hands that
   to the IP.
3. **Capabilities**: bits 29:28 of the capability word are the FPGA type (`CAPAB_FPGA_TYPE`, `itu.h`): 2 = 50T,
   3 = 100T. Ours is `0x25C40222` (type 2). The 100T build sets type 3 (`0x35C40222`). That is how the app
   (`getFpgaType()`) knows the board: the updater (`update_u64ii.cc`: type 2 writes bitstream at 0x000000 and app
   at 0x220000, type 3 the 100T bitstream and the app at **0x3C0000**), the flash driver (`w25q_flash.cc`), the
   `.u2p` handling and **our boot ROM** (`bootrom.c:380`: type 3 loads the app from 0x3C0000, as Gideon's loader).
   A 100T bitstream with type 2 would send the updater and the boot ROM to the wrong address: the type is part of
   this item, not optional.
4. **Caches**: the Vivado IP cache key (`trx_ip`) does not contain the part; for a part other than the 50T it does
   (the 50T keys stay as they are, so the existing cache is not thrown away). The out-of-context checkpoint key
   already contains the part.
5. **Build directory**: `TRX_BUILD` (default `build`) so a 100T build next to a 50T one does not overwrite it.
6. **Flash**: the 100T bitstream is 3.6 MB (device constant, not design dependent), the app slot starts at 0x3C0000
   (3.75 MB): it fits. Nothing here writes a flash.

Not different: DDR2, flash chip, HDMI GTP lanes (quad 216, four lanes on both parts), clocks, USB PHY, Ethernet.
Timing: the same speed grade (-1); a larger device only spreads the placement.

## 4. Build, tools, pipeline

- `designs/c64_basic/build.tcl` reads `TRX_PART` (default `xc7a50tfgg484-1`) and `TRX_BUILD`; the FPGA type
  parameter of `top.v` follows the part (`synth_design -generic`).
- `tools/pincheck.tcl`: the package comparison of section 2 for any two parts plus every ball of our constraint
  files; `tools/pincheck.sh` runs it in `vivado-env` on the NAS.
- Pipeline (`T31-ci-pipeline.md`): stage `bit-ue2` runs `ci/bitstream.sh ue2` **after `package`**, only for a
  `workflow_dispatch` with `ue2: true`, a tag `v*` or a `release/*` branch. Alex, 2026-10-03: development builds the
  50T only; the 100T comes "at the end, when the thing is round"; it never blocks or fails the 50T result. For T53
  itself one dispatched run proves the build and its timing.
- Gate: WNS >= 0, as for the 50T. Utilisation and timing reports are kept as `c64_basic_100t.*.rpt`.

## 5. The update package (T49)

Gideon's `update.ue2` carries `u64e2_50t.swp`, `u64e2_100t.swp` and one `ultimate.app`; the updater picks by
`getFpgaType()`. Ours does the same: the `.cfw` carries `c64_basic.bit` (50T), `c64_basic_100t.bit` (100T) and one
`ultimate.app`, each as a blob in the T49 container; the updater writes only the blob that matches the running
FPGA (type 2: 50T bitstream at 0x000000, app at 0x220000; type 3: 100T bitstream, app at 0x3C0000) and refuses
before writing anything when the package has none for it. The app is one image for both (T31 decision 7). The
package stage's `manifest.json` and `manifest_100t.json` hold the sha256 of each blob. Until a 100T bitstream
exists for the commit, a package carries the 50T one only and a UE2 refuses (T49 decision 4).

## 6. Not done / needs a board

- Whether the bitstream configures and boots on a UE2 (Mike, Till). The type-3 capability and the 0x3C0000 app
  address are the places that would show a mistake first.
- Whether bank 13 balls carry anything on the UE2 board. Not needed for our design.
- UE2-only hardware differences that are not in the FPGA (none known; the UE2 shares the C64U's boards, Gideon's
  one pin file).

## 7. As built

**Dispatched run 362** (branch `t53-ue2`, pipeline with `ue2: true`, 2026-10-04): all six stages green, the 100T stage
35 min 13 s, published as `c64_basic_100t.*` and `manifest_100t.json` into the package `t53-ue2-949cf77d` / `t53-ue2-latest`.

| | XC7A100T (`c64_basic_100t.bit`) | XC7A50T (`c64_basic.bit`, run 357) |
|---|---|---|
| WNS / WHS | **+0.133 ns** / +0.009 ns (0 failing endpoints of 67546) | +0.148 ns |
| Slice LUTs | 26,574 of 63,400 (41.9 %) | 26,511 of 32,600 (81.3 %) |
| LUT as memory | 1,341 of 19,000 (7.1 %) | |
| Slice registers | 27,979 of 126,800 (22.1 %) | 27,981 of 65,200 (42.9 %) |
| Block RAM tiles | 60 of 135 (44.4 %) | 60 of 75 (80.0 %) |
| DSP | 13 of 240 | 13 of 120 |
| Bonded IOB | 178 of 285 (62.5 %) | 178 of 250 (71.2 %) |
| GTP / MMCM | 4 of 4 / 5 of 6 | 4 of 4 / 5 of 5 |
| File | 3,163,142 bytes (fits below 0x3C0000 = 3,932,160) | 1,732,365 bytes |

Same design, the same 178 I/O, so the netlists differ only in the part and the capability word. Vivado steps of the
100T run (elapsed): the C64 core out of context 7:14 and the two IP cores 0:47 and 2:48 (all new for this part, the 50T
ones come from the cache), the top synth 6:59, opt 0:59, place 6:42, phys_opt 0:42, route 3:56, phys_opt 0:38,
bitstream 0:55; about 34 min of the job's 35. Place and route took about as
long as on the 50T (6:42 + 3:56 against 5:46 + 3:30) although the device is twice the size.

The only warnings new for the 100T are the MIG's `Mig7series 79-144` ("Invalid Input Clock Period 200, setting to
200.02"), which the IP generator prints when it is made for the first time for a part; the 50T IP came from the cache.

Gaps this leaves open: no UE2 board test (Mike, Till); the type-3 capability and the app address 0x3C0000 are
checked by reading the code, not on a board; `pincheck` shows the 35 bank-13 balls the 100T has beyond the 50T,
which the design does not use.

Earlier failed 100T runs on the CI branch (353, 355) were not a 100T problem: `t31-ci` did not yet read `TRX_PART`
and `TRX_BUILD`, so Vivado built the 50T design and the stage then found no `build_100t/c64_basic.bit` and failed.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

T53: UE2 build (XC7A100T) as a second CI bitstream

Milestone M1, status ✖. Open (one line, as in the TODO table): – (dropped)

Decisions: [ADR-0015 No 100T and no public release; T53 dropped](../adr/0015-no-100t-no-public-release.md); [ADR-0004 M1 release candidate for TREX-internal testers](../adr/0004-m1-release-candidate-internal-testers.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Stays dropped (Alex 2026-10-06): no 100T in the tester package; the only 100T machine is Commodore's C77 (2027), Till's UE2 is a 50T and takes the C64U bitstream.** UE2 build (Ultimate 64 Elite II, XC7A100T): our design for the 100T as a second CI job/bitstream; check whether the UE2 board is wired like the C64U apart from the FPGA (else its own pin file; Gideon's issue-615 XDC); the T49 package then carries both bitstreams (like Gideon's u64e2_50t/u64e2_100t + one ultimate.app) and the updater picks by FPGA size T+R+X unlock for the classic keyboard (T50 Q5): test with Alex's real C64 keyboard on the C64U's P7 header Test hardware: Mike has a UE2, Till two (both also have C64Us, so the RC stays C64U-only) Spec docs/specs/T53-ue2-build.md (branch t53-ue2, 2026-10-03): the pins are the C64U's (Vivado package data: identical bank/function for all 259 constrained balls + 49 DDR2 pads; `tools/pincheck.sh`), so no own pin file; the build sets capability FPGA type 3 (boot ROM and updater then use app address 0x3C0000); `build.tcl` takes `TRX_PART`; the 100T is the optional last CI stage (dispatch `ue2: true`, tag, release/*), never on normal pushes (Alex)
- **Till's UE2, 2026-10-05:** the board is an XC7A50T (IDCODE 0x0362C093), not a 100T; our 50T c64_basic main-latest + app ran from SRAM (REST up, board C64U). Classic keyboard on P7: 64/64 matrix keys, I2C 0 NACKs (REST probe, 30 ms). HDMI on Till's Eyoyo 4:3 monitor: whole picture green (Gideon's firmware also unsatisfying there); suspect the fixed HDMI 1080p50 output vs Gideon's EDID/DDC choice (configure_hdmi_output), not checked. Gideon's update.ue2 run on our SRAM core flashed nothing (the T44 gate blocks below 0x400000, as designed); flash backup (one read, not verified by a second) ~/trx64-till/backup-2026-10-05-till-ue2-50t/flash1.bin sha256 331249f8.
- **Done 2026-10-04 on t53-ue2 (on top of t31-ci):** dispatched run 362 built the 100T bitstream green: WNS +0.133 ns, 26,574 LUTs (41.9 % of 63,400), 60 BRAM, bitstream 3.16 MB; board test at Mike's/Till's still open Found 2026-10-04: the T44 flash gate protects only below 0x400000, but the 100T app slot runs 0x3C0000-0x580000 (our app ends near 0x545000): the gate must cover the 100T layout; boot ROM app size limit on the 100T is 0x1C0000. Alex 2026-10-04 (late): no 100T package build for the session at Till's on 2026-10-05.
- **Dropped (Alex, 2026-10-04): there will be no 100T.**
