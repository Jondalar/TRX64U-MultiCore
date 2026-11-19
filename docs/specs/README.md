# docs/specs

Specs of the TRX64U MultiCore line (one host, n swappable cores; layout since 2026-10-13, `CLAUDE.md`, T94). Every piece of work starts here as a spec ([_TEMPLATE.md](_TEMPLATE.md)); numbers come from the table in `TODO.md`.

## The split

- **MultiCore** (this repository): host (layer 1) and swappable cores (layer 2) on the C64 Ultimate / U64 Elite II. Its specs are in this directory.
- **TRX64-Ultimate**: the 1:1 drop-in line (full C64, monitor, Timewarp ring, cartridge port, SID/turbo, update path). Its specs are in [TRX64-Ultimate/docs/specs](https://github.com/Jondalar/TRX64-Ultimate/tree/main/docs/specs).
- Before the split (up to 2026-10-13) both lines shared one numbering and one directory. The specs that are the TRX64-Ultimate line's were removed here; the list is below.

## Layout

- `docs/specs/`: open specs (table below) and `_TEMPLATE.md`.
- `docs/specs/done/`: finished specs: done, merged into another spec, dropped, overtaken, or won't do. Status and date are in each file's header; the history is in `docs/todo/<ID>.md`.
- "Belonged to TRX64-Ultimate": the list at the end; the files are gone from this repository.
- Still here from before the split, because MultiCore changed them: T53 (`T53-ue2-build.md`, in `docs/specs/`). The others are in `done/`: T44, T49, T53-ue2-session-till, T79 (two files), T83, T84 (two files), T87.

## Open specs

| Spec | Task | State |
|---|---|---|
| [T104.1](done/T104.1-contribution-model.md) | Contribution model: libraries and apps register ops, state, menus; one service layer | Board: REST/WS to the core app of a running DFX core returns status and memory (Claude by curl) |
| [T104.2](T104.2-generic-app-on-trx64-ultimate.md) | The generic app boots on the TRX64 Ultimate monolith, then a C64 descriptor and cbm1541 (D10) | Wanted (Alex 10-28): TRX64 runs our app; next: drive pages to main, RISC-V build, board run (S1) |
| [T104.3](done/T104.3-applications.md) | Applications: ABI 3 svc table, the app runner, declarative UI rendered by the app (Alex 10-26) | Runner, host, 4 apps proven in ue2emu (merged 10-29, trxmon stays linked); board S8, overlay Q1 open |
| [T107](done/T107-application-owns-the-picture.md) | Every application chooses its own video mode, the host re-asserts its own when a core returns | D7/D8/D11 designed in T164 (host); Sony cause found (pixel repetition, §5); host app 720-wide UI open |
| [T108](T108-atari800-media.md) | The Atari 800 XL starts what you give it: a disk, an executable or a cartridge | Board S8: a game from D1: and a .xex (cartridge works, Alex 10-28); recheck the 10-23 app stop |
| [T111](done/T111-exfat-format.md) | A host application formats a stick or SD card as exFAT, for images above 4 GiB | Built: File Browser F formats exFAT (f_mkfs + own MBR); builder benches green; board B1-B4 are Alex's |
| [T112](T112-core-folders.md) | One folder per core (.cor, .cfg, roms/); ROM search starts beside the core | Layer-2 and large .cor: ROMs and .cfg beside the .cor, benches green; board S1/S2 open |
| [T116](T116-zx-next-core.md) | ZX Spectrum Next core: the original FPGA source, RAM on DDR2, SD card served from an image | Board passed (Alex 10-28); D11 Auto passed (11-04); open: frame-lock and S6 picture-bench decisions (5.3) |
| [T118](T118-hdmi-geometry-per-core.md) | The HDMI picture is the same for every core, or we know which signal differs | Bench: encoder input identical for VC-20 and A1200 (hypothesis refuted); board difference open |
| [T119](done/T119-vc20-zx-pixel-layer.md) | VC-20 and ZX draw into a DDR2 frame buffer of the pixel layer, not via the vic_scaler | ZX 48K picture on the board (11-04, 5.15 fix holds) |
| [T122](T122-create-blank-media.md) | Every core with a drive creates a new, formatted disk image from its menu (D81, Apple II, ADF, ATR, CPC) | Built; board B1, B3-B8 for all cores go to the testers (Alex 10-31); B2 A1200 passed |
| [T125](T125-bbc-micro-core.md) | BBC Micro core | Spec PROPOSED 2026-11-05: BeebFpga/MiSTer core, non-commercial licence; 10 questions open |
| [T128](T128-folder-hdd.md) | A folder on the stick/SD as an Amiga hard disk, instead of an .hdf image | Board B1-B6: mount a folder as HD0 on the A1200 (and A500 with KS 2.05/3.x), write, delete, power cut |
| [T135](done/T135-direct-boot.md) | Direct boot of the default core, menu on a held key | S1-S7 proven in ue2emu; open: S8, S10, S12, B1, B2; board S1/S3/S8/S9/S13/B2 |
| [T137](done/T137-host-convert-and-restore.md) | Host app: convert .cfw/.ue2 to .cor (built), restore original firmware (R, built), update | Restore passed; D11 look passed on the board (11-02); D10 restart built (T152 D11), board step open; P board step |
| [T138](done/T138-a1200-zorro3-fast-ram.md) | A1200 with 32 MB Zorro III fast RAM | Board S5 half: avail shows 32 MB (Alex 10-31); Deep Meet not tried yet |
| [T140](T140-analog-video-out.md) | Analogue picture on the A/V port: S-Video and composite, PAL and NTSC | #19/#21 fixed 11-09; #67 board pass 11-11; hang after warm reset, Reload core works (5.17); load stop; T170 SRQ |
| [T141](T141-usb-keyboard-in-cores.md) | USB keyboard types into every core (C128 extra keys, Amiga raw keys), not only the menus | A1200 board ok; A500 shares the queue (built, Vivado+board open); C128 later |
| [T142](T142-snes.md) | SNES as a large core from MiSTer SNES (snestang Verilog) | Board 11-02: sound correct (BRR sign), USB pad ok; open: menu verdict text |
| [T143](T143-neogeo.md) | Neo Geo as a large core from MiSTer NeoGeo | Board 10-31: plays; sprite stripes fixed in pixw (needs board), load progress, USB pad |
| [T146](done/T146-menu-snapshot.md) | The host menu is on the screen at once: a saved picture of the last menu, LVGL builds behind it | Board: picture at 3.1 s (S1 < 1 s); core menus split out to T176 (11-02) |
| [T152](done/T152-update-and-flash-protection.md) | Our update as .txu (host, app), split from foreign firmware; flash locked under large cores (F8) | S8 ESP32 flash built (U on inspect, 11-07); board: S2/S5/S6b/S7/Q5, locked text, S8 |
| [T154](T154-cartridge-and-save-writeback.md) | Cartridge and battery-RAM saves go back to the user's file (Off/Ask/Auto), shared app base, all cores | Merged 10-29: service, C64 flash/EEPROM, EF3 built and benched; board S1 and P3/P4 open |
| [T156](T156-usb-gamepads.md) | USB gamepads drive the cores' own controllers, generic HID parsing in the app base; NES, SNES, Neo Geo | Board 11-02: NES, SNES, Neo Geo pads ok (Neo Geo map by position); open: real pad dump |
| [T157](T157-joystick-port-swap.md) | Every core can swap the two joystick ports from its menu (generic app), issue #9 | Built: amiga, a1200, nes, c128; vc20 none (Alex); CI bitstreams + board S5 open |
| [T159](done/T159-led-load-indicator.md) | Case LED 1 = floppy LED (drive motor + hard disk), menu On/Off, A1200 first | Board 11-04: A500, Atari, Apple II, CPC, C128 pass; C264 and VC-20 not seen yet |
| [T160](done/T160-master-volume.md) | Every core has a master volume (SHIFT+/SHIFT- in the menu), gain in the host | A1200 board S3 passed; host gain + toast on every page built (T164 WP11), board S3 on another core open |
| [T161](done/T161-github-issue-forms.md) | A GitHub bug report needs version, core, steps, photos and the T162 report (issue forms) | Forms + lint built, on GitHub; S3 Alex looks |
| [T162](done/T162-bug-report-dump.md) | The host app writes a bug report file (F8 menu, /SD/report/, GET /v1/report) | Host and core app built and tested; board step open |
| [T163](done/T163-own-settings-store.md) | Host and core settings in our own store, apart from Gideon's (FTP off after 1.1.0) | Pkg A, S3, S4 built; open: integration run, board FTP step |
| [T164](done/T164-host-package-2.md) | Next host package: HDMI modes + scaler in the host, flash lock, volume, lamp, port clock, NTP | Board 11-02 (c83a83c2): host, A1200 ok; D10p, WP8c in main; D10q built; C128 picture passed 11-04; board test |
| [T167](done/T167-overlay-redraw.md) | UI: a changing progress bar or percentage in an overlay shows what lies behind it while redrawing | Built; 5.10 passed on the board (11-02); 5.8 format/update page look and 5.9 TrxPage updater re-check open |
| [T170](T170-c128-real-drives.md) | C128 core with real drives on the IEC port: 1541, and 1571 with fast serial (SRQ) | Spec proposed (Alex 10-31); D1/D2 decided; nothing built, needs room (T140 D24) |
| [T173](T173-gameboy-core.md) | Game Boy / Game Boy Color core | Spec PROPOSED (Alex 11-05): licence checked, DFX plausible (8-12.5k LUT est.); 10 questions open, nothing built |
| [T175](T175-port-diag-core.md) | Diag core: a timed probe engine plus TOML tests measure the joystick/paddle ports, results to SD | Gateware + core app merged (t175-diag); Vivado run + engine/board steps open |
| [T176](T176-core-menu-snapshot.md) | Core menus on the screen at once from a saved picture (split out of T146 P5, issue #4) | Deferred (Alex 11-02): split out of T146 P5; not started |
| [T177](T177-recovery-boot.md) | Recovery boot: a key held at power-on starts a recovery image, even with a black host menu | Spec PROPOSED (Alex 11-02); Decided (Alex 11-02): app, RESTORE+F1, recovery.cor/backup, no flash, LED; build open |
| [T178](T178-jtag-ft232h.md) | JTAG tools (trxdbg, load, jtag_stat, partial load) through an FT232H as well as the USB-Blaster | Decided (Alex 11-03): pin Chris's repo + PRs; tools + JTAG recovery kit; --cable or TOML; build open |
| [T179](T179-usb-trace.md) | A USB trace shows why a core on a USB stick is slow or missing at power-on | Spec decided (Alex 11-04, Q1-Q4); nothing built |
| [T180](done/T180-media-housekeeping.md) | Media housekeeping: a mount replaces what is mounted, mounts persist (F8), Recent/Favorites, file ops | D1-D10 built; open: media-set rows, +/- hint, names >127; board test |
| [T181](T181-hotkeys-in-running-core.md) | Hotkeys while a core runs (menu, volume, ...) without opening the menu | Board 11-07: works (Alex, S1-S4); S5 C= + CTRL on Apple II, Atari, ZX, CPC never pressed |
| [T182](T182-c64scpu-core.md) | C64 with SuperCPU (65816, SuperRAM) as its own core C64SCPU | Board: VICE ROM to READY, 1581; CMD ROM 1.4 fixed in sim (§5.11), board + JiffyDOS test open |
| [T183](T183-gba-core.md) | Game Boy Advance core (large core, if it fits the XC7A50T) | Spec PROPOSED (Alex 11-05): expected no-go (est. 18.5-20k LUT vs 17k usable); synth trial decides, nothing built |
| [T184](done/T184-wifi-in-host.md) | WiFi in the host menu and the core apps: FTP over WiFi as over Ethernet (#45) | D1-D5 built; board 11-07: works (Alex); S2-S4 (power cycle, static address, boot without WiFi) open |
| [T185](T185-c128-jiffydos.md) | C128: start with the standard ROMs or JiffyDOS, chosen in the settings (#46) | D4 built + tested (files selected, Stock default); board 11-11: works, faster (J.3 timing not measured) |
| [T186](done/T186-ftp-client-fixes.md) | Core app FTP client: passive address of the control peer, no QUIT on a dead link, late 226 read | Built, Mac tests + 5 controls green; RISC-V + apptests green (20 min); board S4 open |
| [T187](done/T187-core-app-ux-rework.md) | Core app menus: same layout in every core, selection visible, no overlap (board 11-06, UX review) | Built D1-D9 incl. host Picture page, ux-review PASS (core+host), merged 11-11; open: S2, S5, S6 |
| [T188](done/T188-amiga-frame-lock.md) | Amiga cores: output frame follows the core (#48) | Done (Alex 11-15); merged 11-13 (run 1796 green) |
| [T189](T189-host-tone-preset.md) | Host audio: tone preset Flat/Bass/Treble beside the master volume (#49) | DECIDED 11-07 (D1-D7): shelf before gain, 2 DSP, per-core .cfg, all cores; build open |
| [T190](done/T190-amiga-mouse-settings.md) | Amiga cores: USB mouse sensitivity and acceleration settings (#51) | Merged main; S1-S2 proven; S3 board (Lemmings, A500) open |
| [T191](T191-amiga-rtc.md) | Amiga cores: optional battery-backed clock at $DC0000, time from the board (#52) | Merged main; S8 board PASS 11-08; S7 Vivado per-module line open (yosys 248 LUT) |
| [T192 arc42](T192-arc42.md) | Architecture of the UI framework (arc42), companion of the T192 epic | Draft |
| [T192](T192-ui-architecture-epic.md) | Epic: one C++ functional core, many UIs (host + core app): contract, bundles, search, TUI, remote | MVP merged main 11-09 (0c5612d2d); T192.1-3 built, T192.4-6 PROPOSED; board open |
| [T192.1](done/T192.1-ui-core-framework.md) | Shared UI core framework (both apps): state, intents + ids, events, jobs, token, WebSocket | MVP built on both apps (core: A1200 only, H3); review fixes in; token risk H2 accepted for MVP; board open |
| [T192.2](done/T192.2-host-app-on-core.md) | Host app on the framework: host_core domain + declarative host menu, 1:1 parity in ue2emu | D23 cut (t192-cut-host): nav, rows, intents, stepped flows in host_core; WiFi modals, walk open |
| [T192.3](T192.3-core-app-on-core.md) | Core app on the framework: MenuCtl leaks out, intents and stable ids | D23 cut + v1 state deleted, 49 T153 on v2, picture/dialog Qs; open: §5 (board, NES pad), D30 unbuilt |
| [T192.4](T192.4-search-overlay.md) | Search overlay (C= SPACE) for host and core app: settings, pages, actions | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) |
| [T192.5](T192.5-text-ui.md) | Text UI for both apps: local on HDMI and remote over ws:// | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) |
| [T192.6](T192.6-bundles-safe-mode.md) | Key/interface bundles (TOML) and safe mode C=+S | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) |
| [T193](T193-further-ci-simplification.md) | Further CI simplification: generated run.yml per push, Gitea shows the children as a graph | Spec PROPOSED 11-08 (idea only) |
| [T195](done/T195-board-buttons.md) | Board buttons UI_BUTTON[2:0] (header reset as on the U64 Elite) in host and every core | Merged main 11-09; S1 bench pass; S3 Vivado line not recorded; S4 board (header button) open |
| [T196](T196-xinput-pads.md) | XInput (Xbox) pads on a USB cable, console cores only (#55) | Merged main; S5 board PASS 11-10; S6 tester (third-party pad) open |
| [T197](done/T197-zip-everywhere.md) | Zip library (miniz) for any app; unzip in host F3 and cores with room | Built, merged main 11-13; open: S5 board |
| [T198](done/T198-per-core-configs.md) | Per-core configs: boot menu overlay starts a core with a saved config (#58) | Merged main; ux PASS; S17 board (save, overlay, start, power cycle) open |
| [T200](T200-network-security.md) | Network security: token on every changing route, grant, all cores, monitor, update (review) | Spec 11-10 (Alex), step by step |
| [T201](T201-diag-av-out.md) | Diag core A/V test: test picture, live encoder knobs on HDMI, question sequence finds NTSC/colour settings (#59) | v2 D4 (60 Hz vsync walk) run 1686 green, WNS +0.185, diag.cor on SD; release with go; S7, S8 open |
| [T202](T202-zip-as-folder.md) | Zip as a folder: browse a .zip in F3, use one entry without unpacking all (#72) | Spec 11-11 (Alex), questions open |
| [T204](T204-home-dir-picker.md) | Core app: Home directory picked with a folder browser (starts at the saved folder, else /) | Spec 11-11 (Alex D1); after the test release (D2) |
| [T205](T205-core-settings-one-place.md) | Core app: settings in one place (F5: core, Video, Audio, Input); F6 goes | Built, T207 merged; ux-review PASS (emulator); board S6 open |
| [T206](done/T206-run-disk-everywhere.md) | Core app: "Run Disk" beside every "Mount Disk" (SCPU, C128, Amiga, PET built; S3 board) | Alex D1 11-11 |
| [T207](done/T207-hotkey-learn.md) | Core app: hotkeys learned by pressing the key, not stepped through with LEFT/RIGHT | Built; S1-S4 proofs in §5; S5 board (Alex) |
| [T208](done/T208-scpu-metal-dust.md) | SCPU: Metal Dust picture faults while scrolling, menu speed (mirror queue / IRQ timing) | Done (Alex 11-15); BA write hold, board S4 PASS 11-15; trace code only in refs/backup/t208-trace |
| [T209](T209-one-line-rows.md) | Host + core app: one line per row (no sublines), closer rows, scroll before the edge, paging keys in the foot | Merged main; D1-D20 built; open: host paging keys, S8 board |
| [T210](done/T210-coreapp-modes-wifi.md) | Core app: splash placed for 480p/720p from the first frame; F8 WiFi real state in every core | Merged main; ue2emu proofs; S3 board (splash 480p, F8 WiFi) open |
| [T211](T211-own-key-names.md) | Host UI: own key names, F keys as navigation (no PAGEUP=F1 aliasing) | Alex 11-12 |
| [T212](T212-core-menu-areas.md) | Core app areas: F1 quick actions, F5 machine, F7 app settings (host value as default), status to F8 | D1-D17 decided; built on branch t212-core-areas (not merged); board open |
| [T213](T213-bitstream-api.md) | Epic: bitstream API (a) - API TOML per core, C modules in the service, child spec per core | Alex 11-12 |
| [T214](T214-real-datasette-commodore-cores.md) | Real datasette in C128, SCPU, VC-20, C16/Plus4 as on the PET | Merged to main (run 871 green); board LOAD per core (S4) |
| [T216](T216-pet-finish.md) | PET finish: business keyboard per model, SuperPET row only there, 4040 ROM guide | Merged to main; S1 cause, S4 board open |
| [T217](T217-scpu-mount-run-prg.md) | SCPU: Mount & Run of a PRG in a disk image reaches the 65816 (page 0 in SCPU SRAM) | Alex 11-12 |
| [T218](T218-snes-dsp-n.md) | SNES: DSP-n cartridges (DSP-1/1B, 2, 3, 4) run; firmware from the SD (appended to the .sfc first, else dsp*.bin) | Built on t218-snes-dsp, sim proof; open: pipeline (S6), board S1/S2/S4 |
| [T53](T53-ue2-build.md) | Ultimate 64 Elite II (XC7A100T) build | Placeholder until the 100T board geometry is known |
| [T89.1](T89.1-apple2-disk-hdd.md) | The Apple IIe reads WOZ disks bit by bit and boots from a ProDOS hard disk in slot 7 | Proposed, nothing built |
| [T92](T92-cpc-core.md) | Amstrad/Schneider CPC core from Amstrad_MiSTer in the host/core split (after T91, which builds the shared modules) | Board 11-02: CAT works, game starts with sound (GitHub #17); left: floppy LED (T159) |
| [T95](T95-nes-core.md) | NES core: NES_MiSTer (GPL v3), cycle accurate, 10 mapper modules | Board: NES switches to 720p and fills ~93 % of the height (T107 picture per core) |
| [T99](T99-resid-two-sids.md) | Two SIDs, each with its own of Dag's filter settings (reDIP wrapper with NS=2) | Spec PROPOSED; the engine index is hardwired to 4 SIDs, so two SIDs share one engine |

## Belonged to TRX64-Ultimate

Removed from this repository; the specs live in [TRX64-Ultimate/docs/specs](https://github.com/Jondalar/TRX64-Ultimate/tree/main/docs/specs) under the same file name.

- [ARCH-lean-state-access](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/ARCH-lean-state-access.md): Lean state access: one state bus for T12, T16.2, T20, T21
- [G06-dma-cycle-steal](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/G06-dma-cycle-steal.md): App DMA into the C64's I/O area while the C64 runs (cycle steal)
- [OPT-bling](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-bling.md): bling: LED effects out of bling_io
- [OPT-ddr2](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-ddr2.md): ddr2: our own lean DDR2 controller instead of MIG
- [OPT-kb](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-kb.md): kb — make c64u_keyboard smaller and shallower
- [OPT-mig](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-mig.md): mig: a smaller MIG (DDR2 controller) by configuration
- [OPT-pix](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-pix.md): pix — make pix_layer smaller without changing what the user sees
- [OPT-rvmul](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/OPT-rvmul.md): rvmul: rvlite register file -> multiplier path
- [PROP-001-cartridge-writeback-interface](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/PROP-001-cartridge-writeback-interface.md): Cartridge Write-Back Interface (CWBI)
- [PROP-002-cartridge-mapping-descriptor](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/PROP-002-cartridge-mapping-descriptor.md): Cartridge Mapping Descriptor
- [SLIM-state-path](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/SLIM-state-path.md): one state path, a lean write log (2026-10-05)
- [T12-freeze-overlay](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T12-freeze-overlay.md): Freeze menu on the machine pause: shadow I/O, C64 text overlay, pixel layer
- [T15-flash-boot](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T15-flash-boot.md): Boot the Ultimate app from flash
- [T16-board-session](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T16-board-session.md): board session: Alex's real cartridges
- [T16-cartridge-port](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T16-cartridge-port.md): The real cartridge (expansion) port
- [T16.2-app-cartridge-writeback](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T16.2-app-cartridge-writeback.md): Writable cartridges: hardware interface and the app's write-back
- [T20-delta-ring](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T20-delta-ring.md): Delta ring, dump, restore
- [T21-monitor](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T21-monitor.md): Machine code monitor at TRX64 level
- [T24-lvgl-ui](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T24-lvgl-ui.md): Own LVGL UI on the pixel layer
- [T24-ui-project-review](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T24-ui-project-review.md): Review of the LVGL UI project (`docs/lvgl_ui_project/`)
- [T24-ui-wiring](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T24-ui-wiring.md): wiring: how the LVGL UI reaches every function of the app
- [T24.1-pip-box](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T24.1-pip-box.md): Live C64 picture box (picture-in-picture): hardware and register spec
- [T25-cleanroom-multisid](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25-cleanroom-multisid.md): Clean room: UltiSID, Copper, lp_filter (overview)
- [T25.1-sid-engine](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.1-sid-engine.md): Clean-room SID engine (UltiSID replacement, up to 8 SIDs)
- [T25.2-copper](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.2-copper.md): Clean-room Copper (raster-timed VIC register writer)
- [T25.3-lp-filter](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.3-lp-filter.md): Clean-room audio decimator (lp_filter / resampler replacement)
- [T25.4-redip-ultisid](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.4-redip-ultisid.md): reDIP SID up to UltiSID level (8 SIDs in one pipeline)
- [T25.5-resid8-menus](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.5-resid8-menus.md): reSID8: the SID menus of the app for our engine
- [T25.6-sampler](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T25.6-sampler.md): Sampler / Ultimate Audio (gap G14)
- [T26.0-esp32-facts](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T26.0-esp32-facts.md): step 0: ESP32 facts and the way back (spec)
- [T27-u64-cyclone-port](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T27-u64-cyclone-port.md): TRX64 Ultimate on the old U64 (Cyclone V 5CEBA4F23C8): feasibility
- [T28-uci](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T28-uci.md): UCI (Ultimate Command Interface) on TRX64 Ultimate
- [T29-turbo](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T29-turbo.md): Turbo: a C64 CPU faster than 1 MHz
- [T31-ci-pipeline](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T31-ci-pipeline.md): Staged CI: lint, sims, app, bitstreams, package
- [T33-generic-cart-decoder](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T33-generic-cart-decoder.md): Descriptor-driven cartridge decoder (TREX-PROP-002 on our core)
- [T37-sid-sockets](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T37-sid-sockets.md): Real SID chips in the C64U's SID sockets
- [T38-T39-input-reset](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T38-T39-input-reset.md): T38/T39 — App keyboard/joystick injection and the C64 reset interrupt
- [T40-ntsc](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T40-ntsc.md): System Mode: PAL, NTSC, PAL-60, NTSC-50 (and the two "/L" variants)
- [T47-paddles-mouse](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T47-paddles-mouse.md): Paddles, 1351 mouse, joystick swap, WASD-to-joystick, NMI sense
- [T50-update-unlock](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T50-update-unlock.md): Flash unlock for updates (T+R+X at power-on)
- [T51-ef3](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T51-ef3.md): EasyFlash 3 emulation, writable 8 MB
- [T57-trex-crt-both-cores](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T57-trex-crt-both-cores.md): trex_crt (cart dumper / writer / inspector) on both cores
- [T59-palette](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T59-palette.md): Custom palettes as in Gideon's core
- [T60-reu-16mb](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T60-reu-16mb.md): REU up to 16 MB, with Gideon's enable and size
- [T70-u64-re-minimal](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T70-u64-re-minimal.md): TRX64 RE core for the old U64 (Cyclone V), minimal scope
- [T71-robot-tests](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T71-robot-tests.md): Robot Framework tests (BDD) for TRX64 Ultimate
- [T78-wifi-rpc-timeout](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T78-wifi-rpc-timeout.md): WiFi RPC replies with a timeout
- [T80-lean-ring](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T80-lean-ring.md): a lean ring engine (draft)
- [T82-vic-check-and-slim](https://github.com/Jondalar/TRX64-Ultimate/blob/main/docs/specs/T82-vic-check-and-slim.md): Our VIC-II matches Gideon's measured on the raster tests, and costs only the LUTs it needs
