# Architecture decision records

One file per decision of the TRX64-Ultimate project, numbered in chronological order (Michael Nygard format: Title, Date, Status, Context, Decision, Consequences, Links). Alex's own words are quoted where they were given. A decision that was overturned stays as its own record and points to its successor.

Where a record says "Formerly: decision N", it is one of the numbered decisions of the 2026-10-05 architecture reset (docs/architecture-reset-2026-10-05.md); old references such as "decision 9" resolve to it.

Rules: a new decision gets the next number, a changed decision gets a new record and the old one is set to "Superseded by NNNN". TODO.md rows and specs link here instead of repeating the decision text. Design decisions that are internal to one spec (the D1-D13 / Q1-Q15 lists inside a spec) stay in that spec.

| No. | Title | Date | Status | Formerly | TODO IDs |
|---|---|---|---|---|---|
| [0001](0001-reDIP-stays-default-no-copper.md) | reDIP SID stays the default; no Copper; own SID engine only selectable | 2026-10-02 | Superseded by 0032 |  | T25 |
| [0002](0002-own-api-gideon-rest-frozen.md) | Own API (TRX64 WS JSON-RPC) is the target; Gideon's REST is no longer extended | 2026-10-02 | Accepted |  | T32, T36, T52 |
| [0003](0003-milestones-m1-m2-m3.md) | Milestones M1, M2, M3 | 2026-10-03 | Accepted |  |  |
| [0004](0004-m1-release-candidate-internal-testers.md) | M1 release candidate for TREX-internal testers | 2026-10-03 | Superseded by 0015 |  | T37, T40, T50, T53 |
| [0005](0005-golden-stub-boot.md) | Golden stub: a never-writable minimal bitstream at 0x0 | 2026-10-03 | Accepted |  | T26 |
| [0006](0006-trex-crt-on-both-cores.md) | trex_crt (dump / write / inspect) 100 % on both cores | 2026-10-03 | Accepted |  | T57 |
| [0007](0007-led-effects-out-of-bitstream.md) | LED strip and lighting effects out of the bitstream (A04) | 2026-10-04 | Accepted |  | T08, T09 |
| [0008](0008-timewarp-off-while-reu-on.md) | Timewarp is off while the REU is enabled (A08) | 2026-10-04 | Accepted |  | T20 |
| [0009](0009-flash-gate-open-in-m1.md) | Flash write gate open in M1; T44 and T50 out of M1 | 2026-10-04 | Accepted |  | T50, T44 |
| [0010](0010-reu-or-cartridge-one-16mb-block.md) | REU as on Gideon's core: one 16 MB expansion-port block, REU or cartridge, never both | 2026-10-04 | Accepted |  | T60, T51 |
| [0011](0011-wall-clock-in-software.md) | Wall clock in software, no LUTs (G05) | 2026-10-04 | Accepted |  | T61 |
| [0012](0012-app-dma-into-io-in-m1.md) | App DMA into I/O while the C64 runs is built in M1 (G06) | 2026-10-04 | Accepted |  | T63 |
| [0013](0013-sampler-joins-the-sid-work.md) | The sampler / Ultimate Audio (G14) is built together with the SID work | 2026-10-04 | Accepted |  | T25 |
| [0014](0014-goal-private-alex-firmware.md) | Goal: a private "Alex firmware" only (EF3 and Wasteland) | 2026-10-04 | Superseded by 0016 |  |  |
| [0015](0015-no-100t-no-public-release.md) | No 100T and no public release; T53 dropped | 2026-10-04 | Accepted |  | T49, T53 |
| [0016](0016-back-to-the-drawing-board.md) | Back to the drawing board: drop-in C64U, TRX64 monitor, Timewarp | 2026-10-04 | Accepted |  |  |
| [0017](0017-d01-1541-emulation-stays.md) | 1541 emulation stays | 2026-10-05 | Accepted | decision 1 | T56 |
| [0018](0018-d02-turbo-out-of-the-budget.md) | Turbo out of the budget | 2026-10-05 | Superseded by 0027 | decision 2 | T29 |
| [0019](0019-d03-udp-streams-stay.md) | UDP video/audio streams stay | 2026-10-05 | Accepted | decision 3 | T13 |
| [0020](0020-d04-release-without-jtag-debug.md) | Release/RC without JTAG debug | 2026-10-05 | Accepted | decision 4 | T46 |
| [0021](0021-d05-no-hard-lut-gate.md) | No hard 26,000 LUT gate | 2026-10-05 | Accepted | decision 5 |  |
| [0022](0022-d06-own-ddr2-controller.md) | Own lean DDR2 controller instead of MIG | 2026-10-05 | Accepted | decision 6 | T11, OPT-ddr2, OPT-mig, T41 |
| [0023](0023-floppy-in-software-rejected.md) | Floppy in software on the RISC-V / one 6502 core time-shared: rejected | 2026-10-05 | Rejected |  |  |
| [0024](0024-slim-state-path.md) | SLIM: one state path, a lean write log (replaces the freeze session) | 2026-10-05 | Accepted |  | T12, T20, T21, SLIM |
| [0025](0025-d07-ring-and-still-formats-free.md) | Ring and still-picture formats are free to optimise | 2026-10-06 | Accepted | decision 7 | T20, T66 |
| [0026](0026-d08-slimmer-pix-layer.md) | Slimmer pix_layer | 2026-10-06 | Accepted | decision 8 | OPT-pix |
| [0027](0027-d09-turbo-back-into-m1.md) | Turbo (T29) back into M1 | 2026-10-06 | Accepted | decision 9 | T29 |
| [0028](0028-d10-one-integration-branch.md) | One integration branch integ-m1 | 2026-10-06 | Accepted | decision 10 | T31 |
| [0029](0029-d11-licence-gpl3-or-later.md) | Licence: GPL-3.0-or-later for our code | 2026-10-06 | Accepted | decision 11 | T25, T34, T68 |
| [0030](0030-timewarp-dump-and-trace-export-out-of-m1.md) | No .c64re / ring dump or load and no trace export in M1 (A13) | 2026-10-06 | Accepted |  | T20, T21, T66 |
| [0031](0031-trxmon-single-rpc-connection.md) | trxmon accepts exactly one RPC connection | 2026-10-06 | Accepted |  | T21 |
| [0032](0032-sid-dag-lems-redip-instead-of-own-engine.md) | SID: Dag Lem's reDIP brought up to UltiSID level; no own clean-room engine | 2026-10-06 | Accepted |  | T25 |
| [0033](0033-update-packages-now-50t-no-unlock.md) | Update packages built now for the internal testers: 50T only, no unlock | 2026-10-06 | Accepted |  | T49 |
| [0034](0034-ef3-as-dedicated-block-full-emulation.md) | EF3 as a dedicated c64_ef3 block with full emulation | 2026-10-06 | Superseded by 0041 |  | T51 |
| [0035](0035-ddt-demos-on-hdmi-via-pr.md) | DDT's demos on HDMI: via a pull request to his repos | 2026-10-07 | Superseded by 0036 |  | T22 |
| [0036](0036-t22-wont-do.md) | T22 (DDT's demos on HDMI) is won't do | 2026-10-07 | Accepted |  | T22 |
| [0037](0037-resid8-8-sid-default-pulsefix-0.md) | reSID8: 8-SID reDIP is the default build, PULSE_FIX = 0 | 2026-10-07 | Accepted |  | T25 |
| [0038](0038-mixer-floor-a-channel-that-is-on-is-never-zero.md) | Mixer floor: a channel that is on is never rounded to gain 0 | 2026-10-07 | Accepted |  | T25 |
| [0039](0039-resid8-filter-curves-dag-only-no-table.md) | reSID8: Filter Curve = Dag's curves only; curve table out; formula stays in hardware | 2026-10-07 | Accepted |  | T25, T69 |
| [0040](0040-filter-on-off-per-engine.md) | Filter On/Off per engine (reSID enable_filter(false)) | 2026-10-07 | Accepted |  | T25, T69 |
| [0041](0041-ef3-into-the-generic-decoder.md) | EF3 goes into the generic decoder: banking and EasyFlash behaviour only | 2026-10-07 | Accepted |  | T33, T51 |
| [0042](0042-ef3-chip-erase-is-a-real-8mb-erase.md) | EF3 chip erase is a real 8 MB erase | 2026-10-07 | Accepted |  | T57, T51 |
| [0043](0043-ef3-scope-no-freezer-no-usb-complete-dump.md) | EF3 scope: no freezer modes, no USB, complete dump included | 2026-10-07 | Accepted |  | T57, T51 |
| [0044](0044-gideons-vanilla-app-never-on-our-bitstream.md) | Gideon's unmodified app never runs on our bitstream | 2026-10-07 | Accepted |  | T60 |
| [0045](0045-ddr2-done-without-cold-start-series.md) | OPT-ddr2 done without the cold-start series | 2026-10-07 | Accepted | – | OPT-ddr2 |
| [0046](0046-palette-default-is-the-apps.md) | The app's palette decides, including the default | 2026-10-07 | Accepted | – | T59 |
| [0047](0047-tape-and-user-port-to-m2.md) | Tape port, user port and real SID sockets move to M2 | 2026-10-08 | Accepted | – | T35, T58, T37 |
| [0048](0048-sid-variant-b-to-m2.md) | reSID variant B and sampler collision jitter move to M2 | 2026-10-08 | Accepted | – | T25 |
| [0049](0049-mixer-pan-keeps-rounding.md) | The app's mixer keeps rounding the pan factor | 2026-10-08 | Accepted | – | T25 |
| [0050](0050-updater-restore-bundle-config-asked.md) | The updater writes a restore bundle with the config; restoring the config is asked | 2026-10-09 | Accepted | – | T49 |
| [0051](0051-sampler-rate-stays-exact-in-m1.md) | The sampler keeps the exact rate in M1, not Gideon's fetch slop | 2026-10-09 | Accepted | – | T25 |
| [0052](0052-robot-framework-bdd-tests.md) | Robot Framework BDD tests; full migration is an exit criterion of M2 | 2026-10-09 | Accepted | – | T71.1, T71 |
| [0053](0053-no-internal-io-devices-while-a-cart-is-active.md) | No internal IO devices in $DE00-$DFFF while a cartridge is active | 2026-10-09 | Accepted | – | T72, T73 |
| [0054](0054-ring-apply-behind-a-build-switch.md) | Ring APPLY behind a build switch, the app's CPU walk as the path without it | 2026-10-12 | Accepted | – | T80, T79 |
| [0055](0055-cores-load-through-icap-not-the-esp32.md) | Cores load through ICAP in the host, not through the ESP32 | 2026-10-12 | Accepted | supersedes the concept's guest loading, amends 0005 | T84, T26, T79 |
| [0056](0056-amiga-kickstart-per-chipset-no-disk.md) | Each Amiga core picks its own Kickstart by the image's version word, and no core comes up with a disk in the drive | 2026-10-16 | Accepted | answers T104 §5.7 | T104, T85, T102, T97 |
| [0057](0057-every-application-owns-the-video-mode.md) | Every application owns the video mode, not only the host | 2026-10-16 | Accepted | extends T105 | T105, T107, T104 |
| [0058](0058-full-bitstream-cores-by-iprog-from-a-flash-file.md) | Full-bitstream cores start by IPROG from a flash-drive file | 2026-10-21 | Accepted | amends 0055, T84 D7/Q3 | T115, T84 |
| [0059](0059-picture-path-is-the-hosts-for-every-core.md) | The picture path from the sources to the pins is the host's, for every core | 2026-10-31 | Accepted | extends 0057 | T164, T107 |
| [0060](0060-tmds-bit-repetition-instead-of-pixel-repetition.md) | Modes below 1080p repeat TMDS bits, not pixels | 2026-10-31 | Proposed | – | T164, T107 |
| [0061](0061-one-video-out-for-the-host-and-the-large-cores.md) | One video-out source for the host and every large core | 2026-10-31 | Accepted | – | T164, T107 |
| [0062](0062-the-screen-decides-which-modes-are-offered.md) | The screen decides which modes are offered; the host holds a default, each core its own | 2026-10-31 | Accepted | – | T164, T107, T163 |
| [0063](0063-pb-l1-grows-cores-that-do-not-fit-become-large.md) | The host block grows with the video path; a core that no longer fits becomes a large core | 2026-10-31 | Accepted in principle, size Proposed | – | T164, T107 |

## Decisions that were left where they are

- docs/done-audit-2026-10-04.md: decisions A01-A03, A05-A07, A08.1-A08.4 are listed there (A04, A08 and A13 have records above).
- Spec-internal decisions stay in their spec: SLIM D1-D6 (summarised in [0024](0024-slim-state-path.md)), T25.5 decisions 1-9, T29 Q1-Q15, T33 D1-D7, T44, T49, T50, T51 D1-D13, T57 Q1-Q7 (summarised), T60, T31 decisions 1-5, T12 Q1-Q9, T16 Q-list, G06, OPT-* options.
- [ARCH-lean-state-access.md](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/ARCH-lean-state-access.md): the user decisions of 2026-10-01.
