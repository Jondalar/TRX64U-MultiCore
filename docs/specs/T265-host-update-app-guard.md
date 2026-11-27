# T265 — The host boots only its own application, recovers it by itself when another firmware replaced it, and puts out RGB

**Status:** ACCEPTED (2026-11-26), asked by Alex: "Todo 3: Das T265 und dabei auch das Hostmenu auf Matrix pipeline umstellen"
(D2 and D3 as proposed; building).
**Repo:** TRX64U-MultiCore — the host's boot ROM (`host/bootrom/`), a recovery program (new, in the host's flash slot), the host
app (`host-app/software/hostapp/`: the update writer, the descriptor, the recovery file), the host gateware (`host/`: the RGB
encoder), the matrix pipeline (the host lane), `tools/txu_pack.py`.
**Number:** T265.
**Origin:** Alex, 2026-11-26 (translated), after a forum user's board stopped booting: he ran Commodore 1.2.1 from the MultiCore
host and, inside it, a community "flash patch" updater for 1.2.1. Alex: "That is NOT good ... many people do that." His proposal:
"What if our host bitstream does not go up to 0x200000, and we have a trampoline safeguard in the range that Gideon's bitstream
normally covers? It does: is the code at 0x220000 our app? Yes: boot. No: boot a default core with the message 'you broke our
installation, please rerun the updater'." Then: "Make a spec for host updates: boot ROM with app protection; recovery otherwise
from /SD, best from the txu, or we put the app next to the settings as 'hostrecovery.app' or so; built with the matrix pipeline
and no longer in the old one, that is pending anyway; RGB video out in already." The cores need nothing of it: "the cores do not
need the protection (!)". After this host: "then we rebuild the remaining few DFX cores."
**Depends on:** T152 (update and flash protection), T164 (host package), T239.5 (matrix pipeline, host lane), T259 (A/V port
measured: RGB on the SCART cable), T140 (analogue picture; Y/C and composite stay there).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a user of the MultiCore host who starts a Commodore or Gideon firmware from the host and runs an updater inside it;
  the host lane of the pipeline; a user with a SCART RGB cable.
- **What happens today (board report, 2026-11-26, and the code):**
  - The updater (`Mech_FlashPatch_1.2.1.ue2`, 1,532,768 bytes) is one updater image (record {0x03000000, 0x176354,
    0x03000000}) with no FPGA bitstream in it (no sync word `AA995566`, no `.bit` header). It asks "Program 1.2.1 Mech?" and
    writes the "Ultimate Application" only.
  - It writes Gideon's application partition of the U64-II 50T layout, 0x220000 (`vendor/gideon/software/io/flash/w25q_flash.cc:53`).
    The host keeps its own application at the same address; the boot ROM loads it from there (`host/bootrom/bootrom.c:74`).
  - The boot ROM checks that the bitstream in flash carries the running bitstream's build stamp, but not whose application is at
    0x220000 (`host/bootrom/bootrom.c:398`). At the next power-on it starts the 1.2.1 application on the host gateware, which
    hangs. The board shows nothing. The way back is a USB Blaster.
  - F8 "Protect Flash from updates" locks 0x000000-0x3FFFFF before a firmware or a large core is started
    (`host-app/software/hostapp/host_core.cc:536`), but it is off by default (`flashlock_board.cc:16`, T152 D9).
  - The host has no analogue picture. The diag core showed on 2026-11-26 how RGB reaches a SCART TV (T259 §5): names map (A = R,
    B = G, C = B), MSB first, `AVID_SYNCn` = csync, `AVID_FB` high = the TV's RGB switch.
  - The host gateware is still built by the old pipeline; the matrix's host lane builds the host app only (T239.5).
- **Room in the flash:** the host bitstream in flash (`null_full.bit` of run 45814821390, compressed) is 1,592,533 bytes and ends
  at 0x184CD5. The bitstream slot ends at 0x220000. The rest of the slot, 635,691 bytes, is unused today, but it is the room
  the host bitstream grows into; only a fixed share of it can be given to this spec (D2).

Parts:
- **P1 The guard:** the boot ROM starts the application at 0x220000 only when it is the host's own.
- **P2 The recovery:** when it is not, the board repairs itself without a JTAG cable and says what happened.
- **P3 The writers:** every path that writes the host application keeps the guard's record and the recovery copy current
  (the `.txu` update, the installer from Commodore firmware, the board tools).
- **P4 RGB out:** the host puts out RGB on the A/V port (the menu and the cores' pictures) with the values T259 measured.
- **P5 The build:** the host gateware is built, packed and published by the matrix pipeline's host lane, not by the old one.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1 | Host app written by the `.txu`; power off and on: the host menu comes up. | board step | board session |
| S2 | P1 | The app slot holds another firmware's application (the 1.2.1 flash patch run inside a started 1.2.1); power off and on: the boot ROM does not start it (console line names the mismatch). Negative control: the same image with the guard's check removed starts it. | board step; boot ROM bench (`host/sim/flash_boot`) with a foreign image | board session; `ci/sims.txt` |
| S3 | P2 | After S2 the recovery starts the host application from the recovery copy; the host menu says that the flash application was replaced and that it was repaired, and the next power-on boots normally (S1). | board step | board session |
| S4 | P2 | No copy on the flash drive: the recovery finds `hostrecovery.app` in the root of a USB stick or the SD card and starts it (D6); none anywhere: the boot ROM waits for JTAG as today. | board step (flash copy deleted, file on a stick) | board session |
| S10 | P2 | The whole package, every pre-release and the release bundle carry `hostrecovery.app` of the same build as the host app. | the package's file list | matrix whole package; release bundle |
| S5 | P3 | An app-only `.txu` update and a host+app update: after each, S1 holds and the recovery copy is the new version. Negative control: power lost between writing the app and the record: the next power-on recovers (S3), never starts a half-written app. | `tests/hostapp` unit over the flash model; board step | apptests; board session |
| S6 | P3 | An installed host without the guard's record (every board today) updates by `.txu` to this host: S1 holds, and the record is written. | board step | board session |
| S7 | P4 | Host menu, then a DFX core (VC-20), PAL: the SCART RGB cable on Alex's Sony shows the picture in the right colours. | board step, photo | board session |
| S8 | P1, P4 | The host routes with WNS >= 0 inside its budget; the large cores' bitstreams do not change (their own boot ROM copies, `cores/<core>/mono/host/bootrom_pkg.vhd`). | Vivado report of the host lane; key compare | matrix host lane |
| S9 | P5 | A push that changes `host/` builds the host gateware in the matrix host lane, and the whole package carries it; the old pipeline builds no host. | the run's step lines | matrix |

## §3 Decision

- **D1 (Alex, 2026-11-26):** the four parts as he listed them: "Bootrom mit App-protection; recovery sonst aus der /SD ... aus der
  txu oder ... 'hostrecovery.app'; neu gebaut mit der Matrix-Pipeline und nicht mehr im alten; RGB video out rein schonmal". The
  check lives in the host's boot ROM only; large cores keep their own boot ROM copies (Alex: "die Cores brauchen den Schutz aber
  nicht"). The DFX cores are rebuilt against the new host afterwards ("dann bauen wir die verbliebenen paar DFX cores neu").
- **D2 (proposal):** the host bitstream slot becomes 0x000000-0x1DFFFF (1,966,080 bytes; today's bitstream uses 81 % of it). The
  range 0x1E0000-0x21FFFF (256 KiB) holds the recovery program and, in its last 4 KiB sector (0x21F000), the guard's record:
  magic, application length, CRC-32 of the application as written at 0x220000. App-only updaters write from 0x220000 up and
  leave this range alone; a full Gideon or Commodore update rewrites it together with the bitstream, which is a clean install of
  that firmware.
- **D3 (proposal):** the boot ROM (8 KiB, `bootrom_8k`) computes the CRC while it loads the application. No record (erased) = boot
  as today, so boards from before this host still boot (S6). Record present and CRC equal = boot. Record present and CRC
  different = load and start the recovery program from 0x1E0000. The recovery program reads the recovery copy (D4) into DDR2 and
  starts it; it writes nothing. The host application, started this way, sees that the flash application differs from itself,
  says so and writes itself, the copy and the record back (P3).
- **D4 (Alex, 2026-11-26: "A. Ja"):** the recovery copy is `/Flash/trx/hostrecovery.app` on the flash drive, next to the host
  settings (`hostcfg.h:13`): the application part as written at 0x220000 (record and data). Alex: "Can it be extracted from the
  .ue2/.cfw/.txu there when installing?" Proposal: the host application writes it itself. When it was started from flash (the boot
  ROM reports the source in its result block, `bootrom.c:265`) and the copy or the record is missing or differs from the flash
  application, it copies 0x220000 to the file and then writes the record. That covers every way in (the `.txu`, the installer
  from Commodore firmware `update_trx.cc`, which today never mounts the flash drive, a JTAG flash, the boards of today, S6) with
  one piece of code. The `.txu` writer also writes the copy directly after the application, before the record (P3 order).
  The free space of the flash drive is counted before the build (about 1.15 MB needed).
- **D5 (Alex, 2026-11-26, "okk, für das Video"):** at run time, by register: the output mode (off, RGB; later Y/C, composite) and the
  standard, which follows the core's raster; SYNCn and FB follow the mode (RGB: csync and high, T259 §5). Fixed in the gateware: the
  channel map and bit order (measured on the board, T259 §5) and the levels, burst and gain as constants, so constant products
  become shifts and adds and need no DSP (T259 D5). The diag core stays the tuning tool; its results become the host's constants.
  Measured cost of the alternative, the diag core's fully adjustable encoder (`diag_avout.v` + `diag_yc.v`, yosys `synth_xilinx`
  estimate): about 870 LUT, 510 FF, 14 DSP48. Alex: "But then we must have Rick's measurement for NTSC" - the NTSC constants come
  from Rick's values (T259 D3) before they are fixed.
- The order of writes for P3: application, recovery copy, record last.
- **D10 (Alex, 2026-11-26):** the host menu goes out progressive on RGB with two lines averaged into one (the menu's 540, 576 or 480
  lines onto the field), its 16:9 squeezed to 4:3: "Also A 4:3 erstmal" (after first saying "Im Hostmenü ist es mir egal ... dann mach
  erstmal dein interlaced"; A is the final word). Interlace (about +100 LUT) is not built now. The menu canvas follows the HDMI row
  (960x540, or the row's size below 960 wide, `coreapp/src/video.cc:111-146`). A core's picture keeps its own raster, progressive,
  one to one where it fits (VC-20 283 lines, Atari 288), two lines averaged where it is doubled (Apple II 384).
- **D11 (Alex, 2026-11-26: "Erstmal ja"):** the "Starting <name>" line of a direct start (`overlay.v`, not the pixel layer) shows on
  HDMI only, not on RGB, for now.
- **D9 (Alex, 2026-11-26: "An und auf RGB"):** at power-on the A/V output is on, in RGB. The setting is a host setting on F8 (host
  settings, Picture), stored with the other video values in `/Flash/boot.cfg` (`video=<sys>,<hdmi>,<av>`), so a user can switch it
  off; the register's reset value is RGB.
- **D8 (Alex, 2026-11-26) - withdrawn the same day.** First: "Dann streichen wir weiter am c64ref" (slim the reference core for the
  host's timing room). Built on a local branch: c64ref reduced to one flop per contract port bit (8,943 LUT, 27 BRAM tiles before). It
  gave the host nothing: the host's logic and P4's ~560 LUT sit in pb_l1 (13,271 of 17,400 LUT), c64ref in pb_l2; and a host routed
  against an empty region may route its static nets through pb_l2, where the DFX cores must later route around them. It also undid
  T115's decision that the reference is a slim C64U, not the null core. Alex: "Ja bitte" (discard): the branch is deleted, c64ref
  stays as T115 decided. The host's worst path before T265: run 45814821390 (`null.timing.rpt`), 0.109 ns,
  `ult/i_cpu/i_dmem/addr_i_reg[8]` to `hout/ovl/scr_reg/WEA[0]` (cpu_mmcm, 10 ns), 7 logic levels, 81 % route.
- **D12 (Alex, 2026-11-26: "aus dem c64ref schmeiß anderen Kram raus. Das kann doch nicht so schwer sein."):** c64ref stays a
  C64 (T115: a slim C64U, not the null core) and loses everything around the machine: the 1541 drive with GCR and IEC, the cartridge
  model (flash, EEPROM, slot, fill), UCI, the TRX register windows (`cwio`, `csys`, `win`: c64ref has no app), the audio socket. Kept:
  CPU, VIC-II, the two CIAs, RAM and ROMs, SID, the bus between them, their video and audio out. Every contract port whose driver goes
  is driven with its idle value. The cut list with the numbers of run 45814821390 is in §5.
- **D12 withdrawn, D13 (Alex, 2026-11-26, translated):** the second run (45888568985) routed the host against the slimmed c64ref
  (WNS +0.126 ns), and every DFX core built against it missed timing by 1.4-1.6 ns (null -1.501, vc20 -1.404, atari800 -1.644,
  ddt_novic -1.404), on the same path: the pixel layer's read data `hout/pxl/rdata` through the core's register window back into the
  CPU's `dmem_resp`, for the VC-20 also the drive's `l2/drv/i_gcr`. Without the register windows and the drive in c64ref those paths
  had no timing pressure when the host was routed, so their partition pins sat far from where the real cores need them. Alex: "We
  need a host that can serve the menu ... Whatever has to go out for it I don't care about at all. The host has to work, including
  analogue video out." c64ref is back to integration's state (T115); if the host does not route with P4, host logic goes, the VIC path
  (`NO_C64VIDEO`, about 372 LUT and 4 RAMB36) first; RGB stays.
- **D7 (Alex, 2026-11-26, Q4):** F8 "Protect Flash from updates" stays off by default: "No, we still have the 'first boot wizard',
  that is where I wanted to settle such questions" (translated). The first boot wizard has no spec in this repository yet.
- **D6 (Alex, 2026-11-26, Q3: "C: es sucht nochmal im Root aller Medien nach einer hostrecovery.app und wir legen die mit in den
  Release"):** when the flash drive has no copy, the recovery program looks for `hostrecovery.app` in the root of every medium
  (the USB sticks, the SD card) and starts the first it finds. The package and the release carry `hostrecovery.app` (S10). The
  recovery program therefore reads USB mass storage and the SD card besides the flash drive; its size against the 256 KiB of D2
  is measured while building. The started application checks that it fits the host in flash (host key, host-app interface, as the
  `.txu` does) before it writes anything.
- **D13 (Alex, 2026-11-27): the file a person puts on a stick or the SD card is `hostrecovery.bin`, not `hostrecovery.app`.** GitHub
  refuses `.app` as a release asset (HTTP 422 "name has a file extension that is not allowed", main run 45984624169, job Pre-release),
  so main has no pre-release. Alex (translated): "I do not care what the hostrecovery file is called ... .bit? .txr? pick something."
  `.bin` (a plain image; `.bit` names a bitstream here). The recovery program looks for `/SD/hostrecovery.bin` and
  `/<drive>/hostrecovery.bin`; the copy in the flash keeps its internal name `/Flash/trx/hostrecovery.app` (no person handles it).

## §4 Out

- Y/C and composite out of the host: T140, after Rick's NTSC values (T259 D3). Alex: "RGB video out rein schonmal".
- The large cores: no guard, no change (D1).
- Protecting against an updater that writes the bitstream slot too: that is a full install of the other firmware (D2).

## §5 Decisions taken while building

Designs (2026-11-26, read-only analyses with file:line): `docs/specs/T265/p1-p3-guard-and-recovery.md`, `p4-rgb-out.md` (sketch in
`p4-sketch/`, synthesised only), `p5-host-in-the-matrix.md`. Their decisions:

- **P5:** no GitHub job runs Vivado; the builder builds what the run's `work-order` names, and today the old `plan` job writes it and
  names the host only when a package or update is made (`ci/order.py:80-92`). Built: a `host` target in `ci/order.py` and
  `ci/fetch_bits.py`, a matrix job `host-bits` that the DFX lanes wait for, the work order written by the matrix; the old pipeline
  writes it only for pull requests and `t239.1` (Alex: "nicht mehr im alten"). The DFX lanes wait for `host-bits` explicitly.
  Files (branch `t265-ci`, not yet run; the proof of S9 is the first run on each branch kind): `ci/order.py`, `ci/fetch_bits.py` (target `host`; the
  flash fillers from `tools/core_toml.py flash-fillers`), `ci/orchestrate.py` (`order_cors`), `ci/dispatch.py` (outputs `host_bits`, `order_targets`),
  `ci/lane.sh` (`bits:host`), `.github/workflows/matrix.yml` (job `host-bits`, the work order and the click beside too), `.github/workflows/pipeline.yml`
  (work order only for a pull request and `t239.1`), `tools/check_bootrom_fresh.sh` (lint host: the boot ROM package is not older than its source;
  it does not rebuild it, so open Q4 of the design - the build image's gcc against the committed package - stays open). Tests: `ci/test_order.py`,
  `ci/test_fetch_bits.py`, `ci/test_orchestrate.py`.
- **P1:** the boot ROM uses 2,892 of 8,192 bytes (`host/bootrom_pkg.vhd`); CRC-32, record check and the jump to the recovery add about
  1.6 KiB. The length is compared first. A JTAG request boot skips the guard. The result block 0xFFC0 is cleared before the JTAG
  path; guard and recovery results go to 0xFF80 / 0xFF60. On a 100T the record and the recovery sit below the app at the same
  distances (app 0x3C0000).
  Built (`host/bootrom/bootrom.c`, branch `t265-bootrom`): record at app - 0x1000, recovery slot at app - 0x40000 (derived, not
  constants, so the 100T follows); a record is valid when magic and check word agree, a broken one counts as none. Valid record:
  header read first, length compared (12 + padded length), then the CRC-32 is computed while the application is copied; a
  difference ends in status 9 and the recovery's `{dest,length,run}` image (its CRC-32 trailer is checked before the jump, limit
  0x3EFF0 data bytes) is started with the status left at 9 for the host app; no usable recovery: status 10, the ROM waits for JTAG.
  Guard block 0xFF80 (magic 0x54323635, verdict 0 none / 1 match / 2 empty / 3 header or length / 4 CRC / 5 JTAG request not
  enforced, record length and CRC, flash length and CRC, `HOST_IFACE`, recovery status 0-4); the recovery block 0xFF60 is the
  recovery program's, the ROM only clears its magic. The result, guard and recovery magics are cleared before the JTAG-magic
  check. `-DTRX_GUARD=0` compiles the decision out (bench negative control, `host/sim/flash_boot/bootrom_noguard_pkg.vhd`);
  `linker.ld` asserts 1 KiB of stack below .bss. Size, measured with riscv64-elf-gcc 16.2 `-march=rv32i_zicsr` (not the build
  image's compiler, which gives 2,892 for the old source where this gives 2,941): old 2,941 bytes, with the guard 4,172
  (+1,231), without 3,225; .bss 1,056 (CRC table); stack room 8,192 - 4,172 - 1,056 = 2,964 bytes. Regenerated with
  `tools/build_bootrom.sh` (container image `u64-build-env`, the compiler of record): `bootrom.bin` 4,116 bytes with the guard,
  `noguard.bin` 3,181 bytes (`host/bootrom_pkg.vhd`, `host/sim/flash_boot/bootrom_noguard_pkg.vhd`, a1a7ec8bf).
  Bench `host/sim/flash_boot` (not run here, no GHDL): new cases guard_match, guard_foreign_crc (S2), guard_off (S2 control, ROM built
  with `-DTRX_GUARD=0`, analysed into `build/noguard`), guard_foreign_len (fewer than 3,000 bytes read), guard_empty_app, guard_bad_chk,
  guard_no_recovery, guard_rec_badcrc, guard_jtag_request, guard_100t, guard_time (64 KiB, at most 1.5 x the unguarded time); match_after
  checks verdict 0 and the reported CRC (no record = as today, S6); jtag_magic checks the stale blocks are cleared. The testbench dumps
  0xFF60-0xFF9F as a GUARD line.
  Tools: the host bitstream slot is 0x000000-0x1DFFFF (D2) in `tools/txu_pack.py` (`SLOT`) and `tools/trx_pkg.py` (`BIT_MAX`, also what
  `cfw_core.py` and `trx_pkg_check.py` refuse); a larger bitstream is refused with a control at the slot's edge (`txu_pack.py selftest`,
  `test_trx_pkg.py`); the selftest also compares `HOST_IFACE`, `FLASH_APPL`, the recovery distance and `MAX_APPL_SIZE` of `bootrom.c`.
- **P2:** the recovery program reuses the host app's drivers (USB with the hub over I2C, SD, FatFs with long names, read-only);
  estimate 110-160 KB of 258,048 bytes. It needs no clock synthesizer or HDMI: CPU and USB clocks come from the 50 MHz PHY clock.
  It never calls `init_flash_disk()` (that formats on a failed probe, `blockdev_flash.cc:177`). Order: flash copy, SD, USB; it keeps
  looking while nothing is found. A flash copy whose CRC differs from the record is accepted (a power cut between copy and record);
  the started application checks host key and interface itself.
- **P3:** the host application writes the recovery program, the copy and the record itself (no third `.txu` part, no installer
  change). `FW_BIT_SLOT` (`flashfw.h:16`), `txu_pack.py:33` and `trx_pkg.py:24` go from 0x220000 to 0x1E0000, or `clear_tail` erases
  the recovery on every host write. When a new copy cannot be written, the old copy is deleted before the record. Flash drive: about
  7.8 MB free in the worst case of the host's own files; the app checks the free space and says so.
- **P4:** the DFX host has no 15 kHz picture: cores write RGB565 frames to DDR2 and put a black dummy raster on `pix_*`. New
  `host/av_rgb.v` scans the pixel layer's shown buffer out at 15 kHz (own raster, a second reader inside `pix_layer`, `mig_arb`
  untouched), constants per D5, csync on SYNCn, FB high. Estimate about 560 LUT, 2 RAMB36, 0 DSP; pb_l1 from 76.3 % to about 79.5 % of
  17,400 LUT. The register lives in `host/mode_req.v` (host-owned, survives a core swap); standard 0 = follow the core's raster.
  No contract port changes (247 ports, VERSION 2).
- **D12 cut list (c64ref; numbers are the hierarchical report of the host run 45814821390 before the cut: pb_l2 8,943 LUT / 7,595 FF / 27 BRAM tiles / 1 DSP).**
  Kept: the machine and its video and audio out. Removed: everything around it. LUT / FF / RAMB36:

  | module (instance) | LUT | FF | RAMB36 | |
  |---|---|---|---|---|
  | `c64_core_c64ref` > `c64_top`: VIC-II (`video_kawari_0`) | 1,992 | 1,472 | 0 | kept |
  | CPU 6510 (`cpu6510_0`) | 700 | 252 | 0 | kept |
  | CIA 1 and 2 (`c64_redip_cia_0/1`) | 758 | 650 | 0 | kept |
  | `mem` (C64 RAM, ROMs, colour RAM) | 53 | 64 | 24 | kept |
  | `turbo` (NO_TURBO), `bus_arbiter`, `c64_top` glue | 86 + 74 + 79 | 86 + 43 + 98 | 0 | kept (inside `c64_top`) |
  | SID | 0 | 0 | 0 | the stand-in of T107 D9 (silence) is all c64ref has had since; it stays |
  | cartridge model, flash, EEPROM, AR (`cart`) | 890 | 793 | 0 | removed |
  | `slot` (expansion port) | 141 | 213 | 0 | removed |
  | `cfill` (cartridge image fill) | 229 | 156 | 0 | removed |
  | 1541 drive `drva` (6502, 2 VIA, CIA, WD177x, floppy; 1 RAMB18) | 2,339 | 1,494 | 0 | removed |
  | `gcr` codec | 80 | 49 | 0 | removed |
  | `uci` (command interface) | 165 | 206 | 0 | removed |
  | `cwio` (register window) | 450 | 668 | 0 | removed |
  | `csys` | 191 | 271 | 0 | removed |
  | `win` (DMA window), `iec` bus, `sock_audio` | 20 + 14 + 119 | 46 + 28 + 317 | 0 | removed |
  | layer 2's own glue and the `c64ref_keep` LUT1s | about 525 + 120 | 2 | 0 | removed; the keeps that guard constants of the machine stay (audio 48, sid levels 8) |

  What the cut leaves at the 247-port header (unchanged, `tools/contract_ports.py --check`): the ports of removed blocks are driven
  by `c64ref_tie_ff` flops (one DONT_TOUCH FDRE per bit) at the idle value of the decoupler in `host/top.v`: 0 for IEC_*_O, SLOT_PHI2,
  SLOT_DOTCLK, slot_rst_o, sid_sock_addr, cw_*, px_*, dma_*, drv_*, gcr_*, win_*, gate_frz, tx_view, cart_active, reu_*,
  paddle_dis_en and the ui_clk DDR2 client `c_*`; 1 for SLOT_*n/BA/DIR lines, slot_a, slot_d_o, slot_d_t, slot_rw, slot_ahi_t, sid_sock*_n
  and drive_leds_n. The inputs of removed blocks have no load. `drv_clk_o` and `drv_data_o` were undriven before and stay so.

  **Built (2026-11-26).** `c64_core_c64ref` keeps `c64_top_c64ref` and `mem`; the cartridge model, expansion port, UCI inputs, REU, DMA
  window, turbo registers and sockets are constants inside it, its ports shrank to the machine's. `layer2_c64ref.v` instantiates
  the core and the tie flops (754 + 41 + 176 FDRE) and nothing else; the files `c64ref_io`, `c64ref_sys`, `cart_fill`, `cart_slot`,
  `slot_port`, `dma_win`, `iec_bus`, `sid_sock_audio`, `c64ref_cw.vh` and the whole `gideon/` copy (drive, GCR, UCI) are deleted;
  the xdc lost the two cart clocks, `core_sources.tcl` the VHDL read, `core.toml` the 1541 ROM name. Shared files untouched.
  Three readers of removed paths fixed minimally: `host/sim/cart_unit` (run.sh, tb) and `ci/sims.deps` now read
  `cores/c64u/cart_slot_c64u.v` (the same module, checked by diff modulo the name); `ci/test_layers_flow.sh` takes
  `c64_turbo_c64ref.v` as its fixture file where it took `dma_win_c64ref.v`. `contract/host_pin.txt` updated (c64ref is a pinned
  part of the static). Expected pb_l2 logic after: VIC 1,992 + CPU 700 + CIA 758 + mem 53 + glue 239 + 56 keep LUT1 = about 3,800
  LUT (computed from the report's module numbers, not synthesised: yosys has no VHDL front end for the CPU), flops 7,595 - 4,243 + 971 tie
  flops = about 4,320, 24 RAMB36 (the drive's RAMB18 and the cartridge cache go). Checked with yosys: `hierarchy -check`, no implicit nets, widths clean for
  `layer2` (core as black box) and for `c64_core_c64ref` (top and mem as black boxes).

**Built, P2 and P3 (2026-11-26, branch `t265-recovery`; boot ROM, `txu_pack.py`, `trx_pkg.py`, the matrix files are other branches):**

- **Blocks.** The magic words the boot ROM and the recovery program must write are fixed in `host-app/software/hostapp/guard.h`: guard block (0xFF80) `0x54323635` (the boot ROM's `GUARD_VALID`; words magic, verdict 0-5 with 5 = JTAG request, not
  enforced, record length, record CRC, flash length, flash CRC, interface, recovery status), recovery block (0xFF60)
  `0x31564352` "RCV1", result magic `0x54313552`, statuses 1 (flash/request) and 9 (guard); recovery block words: magic, medium, stage address, stage length, stage CRC, copy equals record, 0,
  chk = ~(xor of words 0..6). The stage length is the file length (record + data padded to 4). The host application decodes them once (`guard_capture`, `hostapp.cc`) and clears the three magic words.
- **Recovery program** (`host-app/software/hostrecovery/recovery.cc`, `host-app/target/u64ii/riscv/hostrecovery/`): linked at 0x01800000, R1 (Gideon's file layer as the host app links it), the flash drive
  attached through `FlashRo` (a `BlockDevice_Flash` whose `write` refuses) and never formatted, no `init_flash_disk()`. **Deviation from "read-only: no disk_write linked":** R1's FatFs and
  `BlockDevice_Flash::write` are in the image (the file layer references them); nothing in `recovery.cc` calls a write, and the flash drive's device refuses one. R2 (an own `ffconf.h`
  with `FF_FS_READONLY`, an own diskio, a hook in the vendored `usb_scsi.cc`) is the way to "writes nothing by construction" if Alex wants it; it is not built. Order: flash copy, `.new`, SD root, every
  `Usb*` root; it searches every 500 ms and says so on the console every 5 s. The image rule is `guardimg.h` (shared with the test): the record rules of `fw_image_seal_parts` and a file of exactly
  12 + padded length. Hand-off: block, `NANO_START = 0`, interrupts off, `memcpy` to the record's load address, fence, jump (no trampoline: the recovery does not live at 0x30000).
  The slot image is `tools/rec_trailer.py` (record + data + CRC-32 trailer, limit 0x3EFF0 data bytes, refuses a bigger program); `ci/trxapps.sh` builds `hostrecovery` before `hostapp` and runs
  `check_app_size.py ... 0x3EFF0`; the host app links the image (`SRCS_APP = recovery_slot.app`, the Makefile stops with a message when it is not built). Size: not measured (no RISC-V build here).
- **Host app.** `guard.cc` (pure: record, copy, stamp, provisioning, repair check), `guard_board.cc` (capture, the background task at idle priority after the first frame, the gate:
  update and restore wait while it runs, `legacy_stage` waits too), `host_core_flow_guard.cc` (the repair job `SJ_REPAIR`, run first when the recovery started the application; no `direct_phase`).
  The copy is `/Flash/trx/hostrecovery.app` (+ `.new` + `.stm` "len crc" decimal); free space is checked before it is written (the file + 64 KiB); when a new copy cannot be written the old one,
  `.new` and the stamp are deleted before the record is written and the screen gets a note saying what happened and what to do.
- **Slots.** `FW_BIT_SLOT` is 0x1E0000; `FW_BIT_SLOT_FULL` (0x220000) is the slot of a whole Gideon/Commodore firmware (`fw_image_seal`, the restore of T137): without it the stock Commodore
  bitstream (0x217300 bytes) could not be restored any more. Such a write takes the recovery program and the record with it (a clean install of that firmware, D2); the host provisions both again on its next start.
- **.txu writer.** `UpdateJob` calls `guard_after_update` after `fw_write_parts` ok with the app part: copy, then record; a copy or record that fails is a warning line on the result page, the update
  stays written (a copy that differs from the record is taken by the recovery, a missing record means no guard). The recovery program is not part of the `.txu`: the host app writes it at its first start.
  Without `guard_install_fs` (the other benches) the step does nothing.
- **Package.** `hostrecovery.app` = the stamped `hostapp.app` (`ci/package.sh`, `cmp`), required by `ci/prerelease.sh`, copied into the bundle by `tools/make_release_mc.sh`, checked against the `.txu`
  app part by `tools/check_release_flash.py --recovery`, named in INSTALL.md (what is in here, the stick: the ROOT, known problems).
- **Tests.** `tests/hostapp/run_guard.sh` (`test_guard.cc`): cut sweeps over the provisioning (102 cut points), the update writer (239) and the repair (172) with a flash and a RAM disk that lose power at every
  operation; 12 source mutants. Not run: board, RISC-V build, the render bench with the new pages (`SJ_REPAIR`, the warn line), `tests/hostapp/run.sh` as a whole.
- **P4 built, gateware (2026-11-26, branch t265-rgb; Vivado, timing and board not run):** `host/av_rgb.v` in `hdmi_out` (`sys_clk`, own raster
  504 x 312 / 520 x 263, sync as `c128_avout.v`, window 364 + 1600 clocks, one sample per `sys_clk` by a phase accumulator, words as IOB flops,
  `AVID_CLK` = ODDR on the inverted `sys_clk` in `top.v`). Changed from the sketch: the sketch's line RAM with word holds could not work
  (the top line's word overwrote the old one two clocks before the last old pixel was used; fetch had a deadlock with an HDMI request waiting
  behind an RGB line), so the line RAM is two RAMs, A (first line) and B (second line of a pair), read at the same address every clock and
  no word is held: 4 RAMB36 instead of 2 (host block RAM 24.5 -> 28.5 of 35 tiles), 395 FF instead of 722. "Two lines averaged" = source
  taller than 1.25 x the window (the sketch's "taller than the window" would have squeezed the VC-20's 283 lines at 60 Hz); anything else is 1:1
  centred or cropped. Only format 1 (RGB565) is shown; format 2 leaves the port black. `pix_layer` exports its registers and takes the
  requests (`a_*`); an A/V line is never stopped by an HDMI request (it waits, at most 2 x 120 reads), an HDMI line is stopped as before.
  `mode_req` 0x4F0A (reset 0x01 = RGB on, b3:2 std), version byte 2. `top.v` AVID ports, `pins.xdc` (as `avid_pins.xdc` of the C128),
  `build.tcl`, `layers.txt`, `host_pin.txt`, `contract/version.toml` 0.2.1 -> 0.3.0 (247 ports, VERSION 2 unchanged), TOML A block
  `overlay_mode_status.av_mode` (offset 6 of the block at 0x10144F04, size 6 -> 7: a new block would be the 65th, over
  descrec's limit of 64; `rtl_check.py` against the RTL needs the TRX64-Ultimate checkout: not run). yosys `synth_xilinx -family xc7 -flatten`
  (`nice -n 15`): `av_rgb` 458 LUT, 395 FF, 4 RAMB36, 0 DSP; `pix_layer` +76 LUT, +64 FF against `origin/integration`; `mode_req` +6 LUT,
  +4 FF; total about 540 LUT, 463 FF (pb_l1 13,271 -> about 13,810 of 17,400 = 79.4 %, before Vivado's packing). Bench
  `host/sim/avrgb` (`ci/sims.txt` `avrgb`, tier 2; iverilog, the real `pix_layer` with the HDMI side running on the same buffer in the first
  phases, `mig_arb`, the MIG model): every sample of a frame in 7 phases (50 Hz menu 960 x 540 two-line average, flip to the second buffer,
  60 Hz crop 540 -> 240, std 0 follows a 263-line core (60 Hz, 640 x 384), std 0 on a 312-line core (50 Hz, 960 x 283 1:1 centred), 50 Hz 640 x 384,
  off) against a model written in the bench: line length, hsync width, broad lines, window position, FB, 11/16 levels, 4:3 squeeze. HDMI
  underruns while both read: 0. `host/sim/modepath` checks the register (reset value, write, read-back, version 2) and passes.
  `host/sim/lat` not rerun: it does not instantiate `pix_layer` (it cannot see the A/V load) and `ddr2_port.v` is unchanged.
- **P4 built, app (2026-11-26, branch t265-rgb):** `gw_av_write(mode, std)` in `HostVideoIo` (`hostvideo.h`, last member, null in an older
  rig) and `hv_av_apply(on)`; the board side writes `0x4F0A` only when `0x4F05 >= 2`. `boot.cfg` `video=<sys>,<hdmi>,<av>` (`BootCfg::vid_av`, default
  1, written only when 0, so a file at the default stays byte for byte the old one: `test_libguard`; an older host reads two fields
  and drops the third); the first start that moves `video=` to `host.cfg` keeps the third field. The value is
  written to the host after every load of `boot.cfg` and on every change. F8 row `Analogue out` (`SK_AV`, `settings.av`, Off / RGB, a toggle)
  after `Picture`, `HC_SET_SLOTS` 10 -> 11 (page layout unchanged: the UX review with pictures is the coordinator's). Units run once:
  `test_bootcfg`, `test_hostcore` (896), `test_hostvideo` (84), each green; the render bench and its golden (`hostrows.txt`, the settings row
  strings) were edited by hand and not run.

- **Bundle review (2026-11-26, branch t265), fixed:** the boot ROM wrote the flash application's length into the guard block only on
  a record path, so a host started with no record was never provisioned (`guard.cc` compares length and CRC): `load()` now writes
  `G_LEN` with `G_CRC` on every load of the flash application, and the bench checks it in the no-record case (ROM 4,148 bytes).
  c64ref's `drv_clk_o` / `drv_data_o` had lost their driver: tied idle 0 with the others (756 flops). The TOML A of the A/V
  register: a 65th block was over descrec's limit of 64, so it is `overlay_mode_status.av_mode` (offset 6). The recovery program
  links without the host log's outbyte wrap. RISC-V build (`tools/dev.sh riscv`): `hostrecovery.app` 181,868 bytes of a 258,032-byte
  slot, `hostapp.app` 1,349,996 bytes; `tools/dev.sh test` (fast tier) green. Left as designed: the recovery program starts a flash
  copy whose header and size match without checking its CRC (P2 decision above); the contract version moves 0.2.1 -> 0.3.0 for the
  host's new pins, the 247 partition ports unchanged.
- **First pipeline run of the bundle (45862833501), fixed:** Vivado stopped in c64ref's synthesis (`c64_core_c64ref.v:93`, Synth 8-1577:
  a declaration list with one initial value needs one on every name); lint: the `avrgb` bench had no owner in `ci/sims.lane`, the
  pre-release refusal test did not know `hostrecovery.app`; the host render bench: the walks to Cores and ROMs take one row more, and
  the F8 help line stood under all eleven slots, so a two-line help was cut (it now stands under the rows shown: 0 cut objects in
  every mode); `hdmi_out` expected status version 1. `flash_boot` ran into its 900 s limit: on the builder in the sim image it takes
  1,484 s, 27 cases, 158 ok, 0 FAIL, ALL PASS, among them the 11 guard cases (S1, S2 with its control ROM, S6, the 100T distances; the
  guard adds 1.9 % to a 64 KiB boot: 72,903 us against 71,519 us); its limit is now 3,600 s.
- **Board, 2026-11-26 (S7 for the host menu, Alex's photo):** the host menu on Alex's Sony over the SCART RGB cable, colours right, and on
  HDMI at the same time. Host `35da0749` (c64ref restored, D13; WNS +0.174 ns, pb_l1 13,735 of 17,400 LUT under floorplan B) with
  the null core (WNS +0.200 ns), both built by hand on the builder from this branch (`ci/bitstream.sh c64u --lane host` and
  `--lane core:null` in `vivado-env:2024.1` with `--env-file ci/vivado.env`), loaded into SRAM over JTAG, `hostapp.app` of this branch
  started by `tools/trxdbg.sh boot staged` with the menu marker set in DDR2 (`handover.h` 0x03EFF000 + 0xFF8, "MENU" and its
  complement), so the board did not boot on to its default entry. Nothing written to flash. Seen on the way: `c64ref_full.bit` alone
  shows no menu (c64ref's IO block answers "C64U", the host app takes the pixel layer only from a core that answers "TRX2",
  `trx_io.cc` `trx_io_caps`); without `ci/vivado.env` the floorplan is A and the host does not place (pb_l1 105 %). Known flaw,
  left as is (Alex: "Das ist mir total egal"): the right edge of the canvas is cut on RGB, with black beside it on the screen. Open:
  S7 with a DFX core (VC-20) - the DFX cores are not yet built against this host.
Open questions: none (Q1 = D4, Q2 = D5, Q3 = D6, Q4 = D7).

- **D13 built (2026-11-27, branch `recovery-bin`):** the file of the package, the release bundle and the pre-release is `hostrecovery.bin`
  (`ci/package.sh`, `ci/prerelease.sh`, `tools/make_release_mc.sh`, `tools/release_notes_mc.py`, `tools/check_release_flash.py`); the
  recovery program looks for `/SD/hostrecovery.bin` and `/<drive>/hostrecovery.bin` (`host-app/software/hostrecovery/recovery.cc`), the
  guard page names it (`host_core_flow_guard.cc`). The flash copy keeps `/Flash/trx/hostrecovery.app` (`guard.h` GUARD_COPY); the recovery
  program's own build files (`ci/trxapps.sh`, `ci/lane.sh`) are a different file and keep their names. Units: `tools/test_make_release_mc.py`
  OK (22), `ci/test_prerelease.sh` ok. Proof: the first green pre-release of main (T239.5 D27).
