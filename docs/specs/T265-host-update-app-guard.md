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
- **D8 (Alex, 2026-11-26, on the host's timing margin: "Dann streichen wir weiter am c64ref"):** the reference core c64ref (build_only,
  routed with the host) is slimmed further to give the host route room for P1 and P4. Measured before: the host's worst path in run
  45814821390 (`null.timing.rpt`) is 0.109 ns, `ult/i_cpu/i_dmem/addr_i_reg[8]` to `hout/ovl/scr_reg/WEA[0]` (cpu_mmcm, 10 ns), 7 logic
  levels, 81 % of the delay is route: a placement/congestion path in the host's static part, not in c64ref. What c64ref loses is read
  from its utilization report and kept to what the partition contract and the reference role need.
- **D7 (Alex, 2026-11-26, Q4):** F8 "Protect Flash from updates" stays off by default: "No, we still have the 'first boot wizard',
  that is where I wanted to settle such questions" (translated). The first boot wizard has no spec in this repository yet.
- **D6 (Alex, 2026-11-26, Q3: "C: es sucht nochmal im Root aller Medien nach einer hostrecovery.app und wir legen die mit in den
  Release"):** when the flash drive has no copy, the recovery program looks for `hostrecovery.app` in the root of every medium
  (the USB sticks, the SD card) and starts the first it finds. The package and the release carry `hostrecovery.app` (S10). The
  recovery program therefore reads USB mass storage and the SD card besides the flash drive; its size against the 256 KiB of D2
  is measured while building. The started application checks that it fits the host in flash (host key, host-app interface, as the
  `.txu` does) before it writes anything.

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
- **P1:** the boot ROM uses 2,892 of 8,192 bytes (`host/bootrom_pkg.vhd`); CRC-32, record check and the jump to the recovery add about
  1.6 KiB. The length is compared first. A JTAG request boot skips the guard. The result block 0xFFC0 is cleared before the JTAG
  path; guard and recovery results go to 0xFF80 / 0xFF60. On a 100T the record and the recovery sit below the app at the same
  distances (app 0x3C0000).
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

Open questions: none (Q1 = D4, Q2 = D5, Q3 = D6, Q4 = D7).
