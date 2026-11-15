# TODO — TRX64U MultiCore

The one list of the multi-core line: one host (layer 1), n swappable cores (layer 2) on the C64U / U64 Elite II.
IDs never change; Alex sets the order and drops items, Claude changes the status.

**How this file is kept:** a row is ID, milestone, status, short title, one line "Open" (what is still missing, at most
120 characters, equal to the "Open (one line ...)" line of its detail file; `tools/check_todo.py` checks both) and a
link. No histories, CI numbers or decision texts in a row: results go to the detail file (spec or `docs/todo/<ID>.md`,
dated entries, newest first), decisions to [docs/adr/](docs/adr/README.md).

Status: 🔨 in progress · ⏭ next · ⏸ later · 📅 planned · ✅ done · ✖ dropped or merged · ❌ won't do

**Milestones** ([ADR-0003](docs/adr/0003-milestones-m1-m2-m3.md)): the multi-core work is M3 (Alex, 2026-10-13). Rows
marked 1 or 2 came over from TRX64-Ultimate with the decoupling (own UI, own API; Alex, 2026-10-16) and keep their
number. The drop-in line stays in `TREX/TRX64-Ultimate`.

**Board:** open board steps in [docs/board-tests.md](docs/board-tests.md) (package main-7fda7e1f; older lists in docs/archive/board-tests/). Older overview paragraphs:
[docs/todo/overview-history.md](docs/todo/overview-history.md).

## In progress

| ID | M | Status | Task | Open | Details |
|---|---|---|---|---|---|
| T92 | 3 | 🔨 | Amstrad/Schneider CPC core from Amstrad_MiSTer in the host/core split (after T91, which builds the shared modules) | #17 CAT hang fixed, board PASS 11-15 (swap/eject untested, Alex: enough); floppy LED passed 11-04 (T159) | [T92](docs/todo/T92.md) |
| T97 | 3 | ✅ | Amiga 1200 runs AmigaVision from HDF: 68020, fast RAM, images above 4 GiB | Done (Alex 11-08); RAM line in Home > Status dropped | [T97](docs/todo/T97.md) |
| T104.2 | 2 | 🔨 | The generic app boots on the TRX64 Ultimate monolith, then a C64 descriptor and cbm1541 (D10) | Wanted (Alex 10-28): TRX64 runs our app; next: drive pages to main, RISC-V build, board run (S1) | [T104.2](docs/specs/T104.2-generic-app-on-trx64-ultimate.md) |
| T116 | 3 | 🔨 | ZX Spectrum Next core: the original FPGA source, RAM on DDR2, SD card served from an image | Board passed (Alex 10-28); D11 Auto passed (11-04); open: frame-lock and S6 picture-bench decisions (5.3) | [T116](docs/todo/T116.md) |
| T119 | 3 | 🔨 | VC-20 and ZX draw into a DDR2 frame buffer of the pixel layer, not via the vic_scaler | ZX 48K picture on the board (11-04, 5.15 fix holds) | [T119](docs/todo/T119.md) |

## Board test required

Built and benched; what is left is a step on the board (Alex, 2026-10-28, translated: "Let's make a section 'Board test required' in the TODO list and collect them there. We can then also hand that to the testers via GitHub."). Open = the board step, nothing else.

| ID | M | Status | Task | Open | Details |
|---|---|---|---|---|---|
| T89 | 3 | ✅ | Apple IIe core from NanoApple2 as a core in the host/core split | Done (Alex 10-30); rest open to contributors | [T89](docs/todo/T89.md) |
| T108 | 3 | 🔨 | The Atari 800 XL starts what you give it: a disk, an executable or a cartridge | Board S8: a game from D1: and a .xex (cartridge works, Alex 10-28); recheck the 10-23 app stop | [T108](docs/specs/T108-atari800-media.md) |
| T95 | 3 | 🔨 | NES core: NES_MiSTer (GPL v3), cycle accurate, 10 mapper modules | Board: NES switches to 720p and fills ~93 % of the height (T107 picture per core) | [T95](docs/todo/T95.md) |
| T107 | 1 | 🔨 | Every application chooses its own video mode, the host re-asserts its own when a core returns | D7/D8/D11 and the 720-wide UI built in T164 (board 11-02..04); Sony: pixel repetition; NES 720p retest open | [T107](docs/todo/T107.md) |
| T104.1 | 2 | 🔨 | Contribution model: libraries and apps register ops, state, menus; one service layer | Board: REST/WS to the core app of a running DFX core returns status and memory (Claude by curl) | [T104.1](docs/specs/T104.1-contribution-model.md) |
| T104.3 | 2 | 🔨 | Applications: ABI 3 svc table, the app runner, declarative UI rendered by the app (Alex 10-26) | Runner, host, 4 apps proven in ue2emu (merged 10-29, trxmon stays linked); board S8, overlay Q1 open | [T104.3](docs/specs/T104.3-applications.md) |
| T122 | 3 | 🔨 | Every core with a drive creates a new, formatted disk image from its menu (D81, Apple II, ADF, ATR, CPC) | Built; board B1, B3-B8 for all cores go to the testers (Alex 10-31); B2 A1200 passed | [T122](docs/specs/T122-create-blank-media.md) |
| T128 | 3 | 🔨 | A folder on the stick/SD as an Amiga hard disk, instead of an .hdf image | Board B1 passed 11-04 (A1200 boots a folder as HD0); B2-B6 open: write, delete, power cut, A500 KS 2.05/3.x | [T128](docs/todo/T128.md) |

## Planned

| ID | M | Status | Task | Open | Details |
|---|---|---|---|---|---|
| T99 | 2 | 📅 | Two SIDs, each with its own of Dag's filter settings (reDIP wrapper with NS=2) | Spec PROPOSED; the engine index is hardwired to 4 SIDs, so two SIDs share one engine | [T99](docs/todo/T99.md) |
| T110 | 3 | 📅 | What we ship is still the T79 debug build: reSID8, UCI, streams, REU, sampler and turbo are all off | bitstream.yml:92-95 decide it for host and cores (jtag_dbg out, was T46); the rebuild moves host_pin | [T110](docs/todo/T110.md) |
| T111 | 3 | 🔨 | A host application formats a stick or SD card as exFAT, for images above 4 GiB | Built: File Browser F formats exFAT (f_mkfs + own MBR); builder benches green; board B1-B4 are Alex's | [T111](docs/todo/T111.md) |
| T112 | 3 | 🔨 | One folder per core (.cor, .cfg, roms/); ROM search starts beside the core | Layer-2 and large .cor: ROMs and .cfg beside the .cor, benches green; board S1/S2 open | [T112](docs/todo/T112.md) |
| T125 | 3 | 📅 | BBC Micro core | Spec PROPOSED 2026-11-05: BeebFpga/MiSTer core, non-commercial licence; 10 questions open | [T125](docs/todo/T125.md) |
| T165 | 3 | 📅 | Acorn Archimedes core | On the core list (Alex, 2026-10-31); source and licence not chosen, spec not written | [T165](docs/todo/T165.md) |
| T173 | 3 | 📅 | Game Boy / Game Boy Color core | Spec PROPOSED (Alex 11-05): licence checked, DFX plausible (8-12.5k LUT est.); 10 questions open, nothing built | [T173](docs/todo/T173.md) |
| T183 | 3 | 📅 | Game Boy Advance core (large core, if it fits the XC7A50T) | Spec PROPOSED (Alex 11-05): expected no-go (est. 18.5-20k LUT vs 17k usable); synth trial decides, nothing built | [T183](docs/todo/T183.md) |
| T184 | 3 | 🔨 | WiFi in the host menu and the core apps: FTP over WiFi as over Ethernet (#45) | D1-D5 built; board 11-07: works (Alex); S2-S4 (power cycle, static address, boot without WiFi) open | [T184](docs/todo/T184.md) |
| T185 | 3 | 🔨 | C128: start with the standard ROMs or JiffyDOS, chosen in the settings (#46) | D4 built + tested (files selected, Stock default); board 11-11: works, faster (J.3 timing not measured) | [T185](docs/specs/T185-c128-jiffydos.md) |
| T186 | 3 | 🔨 | Core app FTP client: passive address of the control peer, no QUIT on a dead link, late 226 read | Built, Mac tests + 5 controls green; RISC-V + apptests green (20 min); board S4 open | [T186](docs/specs/T186-ftp-client-fixes.md) |
| T187 | 3 | 🔨 | Core app menus: same layout in every core, selection visible, no overlap (board 11-06, UX review) | Built D1-D9 incl. host Picture page, ux-review PASS (core+host), merged 11-11; open: S2, S5, S6 | [T187](docs/specs/T187-core-app-ux-rework.md) |
| T188 | 3 | 🔨 | Amiga cores: output frame follows the core (#48) | Merged 11-13 (run 1796 green); 'smoother' (Alex) not dated in spec; S1/S2/S4 board, S5 line open | [T188](docs/specs/T188-amiga-frame-lock.md) |
| T189 | 3 | 📅 | Host audio: tone preset Flat/Bass/Treble beside the master volume (#49) | DECIDED 11-07 (D1-D7): shelf before gain, 2 DSP, per-core .cfg, all cores; build open | [T189](docs/specs/T189-host-tone-preset.md) |
| T190 | 3 | 🔨 | Amiga cores: USB mouse sensitivity and acceleration settings (#51) | Merged main; S1-S2 proven; S3 board (Lemmings, A500) open | [T190](docs/specs/T190-amiga-mouse-settings.md) |
| T191 | 3 | 🔨 | Amiga cores: optional battery-backed clock at $DC0000, time from the board (#52) | Merged main; S8 board PASS 11-08; S7 Vivado per-module line open (yosys 248 LUT) | [T191](docs/specs/T191-amiga-rtc.md) |
| T192 | 3 | 🔨 | Epic: one C++ functional core, many UIs (host + core app): contract, bundles, search, TUI, remote | MVP merged main 11-09 (0c5612d2d); T192.1-3 built, T192.4-6 PROPOSED; board open | [T192](docs/specs/T192-ui-architecture-epic.md) |
| T192.1 | 3 | 🔨 | Shared UI core framework (both apps): state, intents + ids, events, jobs, token, WebSocket | Built, both apps; service in every core since D31 (11-10); S20 gate missing; board open | [T192.1](docs/specs/T192.1-ui-core-framework.md) |
| T192.2 | 3 | 🔨 | Host app on the framework: host_core domain + declarative host menu, 1:1 parity in ue2emu | D23 cut merged 11-09: flows + WiFi as jobs, parity walk 79 steps green; S10 radio board open | [T192.2](docs/specs/T192.2-host-app-on-core.md) |
| T192.3 | 3 | 🔨 | Core app on the framework: MenuCtl leaks out, intents and stable ids | D23 cut, D30, D31 built; open: board steps, D27 replacement (cursor left = up) unbuilt | [T192.3](docs/specs/T192.3-core-app-on-core.md) |
| T192.4 | 3 | 📅 | Search overlay (C= SPACE) for host and core app: settings, pages, actions | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) | [T192.4](docs/specs/T192.4-search-overlay.md) |
| T192.5 | 3 | 📅 | Text UI for both apps: local on HDMI and remote over ws:// | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) | [T192.5](docs/specs/T192.5-text-ui.md) |
| T192.6 | 3 | 📅 | Key/interface bundles (TOML) and safe mode C=+S | Spec PROPOSED 11-07 (open questions in §6); after the MVP (D20) | [T192.6](docs/specs/T192.6-bundles-safe-mode.md) |
| T193 | 3 | 📅 | Further CI simplification: generated run.yml per push, Gitea shows the children as a graph | Spec PROPOSED 11-08 (idea only) | [T193](docs/specs/T193-further-ci-simplification.md) |
| T194 | 2 | ✅ | C128 40 columns through the pixel layer: Integer/Fit/Fill as every core (#63) | In main since 11-09 (4d7c8b30f), #63 done; D12 Auto switch missed PETSCII Robots title (open) | [T194](docs/specs/done/T194-c128-40col-scaling.md) |
| T195 | 2 | 🔨 | Board buttons UI_BUTTON[2:0] (header reset as on the U64 Elite) in host and every core | Merged main 11-09; S1 bench pass; S3 Vivado line not recorded; S4 board (header button) open | [T195](docs/specs/T195-board-buttons.md) |
| T196 | 2 | 🔨 | XInput (Xbox) pads on a USB cable, console cores only (#55) | Merged main; S5 PASS 11-10; #55 missed presses: diag test first (11-15) | [T196](docs/specs/T196-xinput-pads.md) |
| T197 | 2 | 🔨 | Zip library (miniz) for any app; unzip in host F3 and cores with room | Built, merged main 11-13; open: S5 board | [T197](docs/specs/T197-zip-everywhere.md) |
| T198 | 2 | 🔨 | Per-core configs: boot menu overlay starts a core with a saved config (#58) | Merged main; ux PASS; S17 board (save, overlay, start, power cycle) open | [T198](docs/specs/T198-per-core-configs.md) |
| T199 | 2 | ✅ | VC-20: JiffyDOS setting switches KERNAL and 1541 ROM together (#59) | Merged main; S1-S5 tests, S6 board PASS 11-11 | [T199](docs/specs/done/T199-vc20-jiffydos.md) |
| T200 | 2 | 📅 | Network security: token on every changing route, grant, all cores, monitor, update (review) | Spec 11-10 (Alex), step by step | [T200](docs/specs/T200-network-security.md) |
| T201 | 2 | 🔨 | Diag core A/V test: test picture, live encoder knobs on HDMI, question sequence finds NTSC/colour settings (#59) | v2 D4 (60 Hz vsync walk) run 1686 green, WNS +0.185, diag.cor on SD; release with go; S7, S8 open | [T201](docs/specs/T201-diag-av-out.md) |
| T202 | 2 | 📅 | Zip as a folder: browse a .zip in F3, use one entry without unpacking all (#72) | Spec 11-11 (Alex), questions open | [T202](docs/specs/T202-zip-as-folder.md) |
| T203 | 2 | ✅ | Start log file: each host start writes what it saw (keys, cfg, decision) to SD, last 10 (#69) | Merged main; S1-S6 tests, S7 board PASS 11-11 | [T203](docs/specs/done/T203-start-log-file.md) |
| T204 | 3 | 📅 | Core app: Home directory picked with a folder browser (starts at the saved folder, else /) | Spec 11-11 (Alex D1); after the test release (D2) | [T204](docs/specs/T204-home-dir-picker.md) |
| T205 | 2 | 🔨 | Core app: settings in one place (F5: core, Video, Audio, Input); F6 goes | Built, T207 merged; ux-review PASS (emulator); board S6 open | [T205](docs/specs/T205-core-settings-one-place.md) |
| T206 | 2 | 🔨 | Core app: "Run Disk" beside every "Mount Disk" (SCPU, C128, Amiga, PET built; S3 board) | Built (SCPU key matrix, C128, Amiga, PET); S3 board open, no pass recorded | [T206](docs/specs/T206-run-disk-everywhere.md) |
| T207 | 2 | 🔨 | Core app: hotkeys learned by pressing the key, not stepped through with LEFT/RIGHT | Built; S1-S4 proofs in §5; S5 board (Alex) | [T207](docs/specs/T207-hotkey-learn.md) |
| T208 | 2 | 🔨 | SCPU: Metal Dust picture faults while scrolling, menu speed (mirror queue / IRQ timing) | VIC drops writes at AEC low; BA write hold (scpu-ba-hold): benches PASS, run 1925 green; board S4 PASS 11-15 (Alex) | [T208](docs/specs/T208-scpu-metal-dust.md) |
| T209 | 2 | 🔨 | Host + core app: one line per row (no sublines), closer rows, scroll before the edge, paging keys in the foot | Merged main; D1-D20 built; open: F5 editor paging keys (Alex), S8 board | [T209](docs/specs/T209-one-line-rows.md) |
| T210 | 2 | 🔨 | Core app: splash placed for 480p/720p from the first frame; F8 WiFi real state in every core | Merged main; ue2emu proofs; S3 board (splash 480p, F8 WiFi) open | [T210](docs/specs/T210-coreapp-modes-wifi.md) |
| T211 | 2 | 📅 | Host UI: own key names, F keys as navigation (no PAGEUP=F1 aliasing) | Alex 11-12 | [T211](docs/specs/T211-own-key-names.md) |
| T212 | 2 | 🔨 | Core app areas: F1 quick actions, F5 machine, F7 app settings (host value as default), status to F8 | D1-D17 decided; built on branch t212-core-areas (not merged); board open | [T212](docs/specs/T212-core-menu-areas.md) |
| T213 | 2 | 📅 | Epic: bitstream API (a) - API TOML per core, C modules in the service, child spec per core | Alex 11-12 | [T213](docs/specs/T213-bitstream-api.md) |
| T214 | 2 | 🔨 | Real datasette in C128, SCPU, VC-20, C16/Plus4 as on the PET | Merged to main; C16: own SAVE does not LOAD back (board 11-14); on t214-c264-tape; S4 per core open | [T214](docs/specs/T214-real-datasette-commodore-cores.md) |
| T215 | 2 | 🔨 | CI keys and the plan hash code only: a comment moves no key and starts no build | Merged to main, a-h proven (11-14); key_alias fixed (a1e69bf78), cache migrated, run 1906 green | [T215](docs/specs/T215-ci-hash-code-only.md) |
| T216 | 3 | 🔨 | PET finish: #78 #79 #80 #82 #87 #86(tape) bundled 11-15; built parts stay | §3 Q1-Q7 decided 11-15 (96K, ROM refuse, 2001+B2, keys both, pad generic, tape after T214) | [T216](docs/specs/T216-pet-finish.md) |
| T217 | 2 | 📅 | SCPU: Mount & Run of a PRG in a disk image reaches the 65816 (page 0 in SCPU SRAM) | Alex 11-12 | [T217](docs/specs/T217-scpu-mount-run-prg.md) |
| T218 | 3 | 🔨 | SNES: DSP-n cartridges (DSP-1/1B, 2, 3, 4) run; firmware from the SD (appended to the .sfc first, else dsp*.bin) | Merged to main; board S1 PASS 11-15 (Mario Kart, dsp1b from SD), #85 closed; open: S2/S4 board, S6 | [T218](docs/specs/T218-snes-dsp-n.md) |
| T219 | 2 | 🔨 | RIGHT enters a folder in every file browser, LEFT goes up (#90) | Host F3 + core app Library built (ue2emu pictures); keywalk golden regenerated; Alex: two browsers stay | [T219](docs/specs/T219-right-enters.md) |
| T220 | 3 | 📅 | Host network chip stale: "no network" while LAN works (#84 comment) | Backlog; read 11-15: 3 s poll, cause open | [T220](docs/specs/T220-net-chip-stale.md) |
| T221 | 2 | 📅 | SemVer for host, host menu, core app, every core; displays: branch SemVer+commit, release SemVer | D1-D19 decided 11-15; open: core start versions, TRX64-U contract, L/P mock-up | [T221](docs/specs/T221-semver-everywhere.md) |
| T225 | 2 | 📝 | Amiga: USB pad on game port 2 (1-/2-button, CD32), mapping dialog, saved per pad ID | Spec 11-15 (Alex) | [T225](docs/specs/T225-amiga-usb-pad-port2.md) |
| T226 | 2 | 📝 | Core app: "Loading cartridge" note; pad mapping on the Input tab, not Actions F7 | Spec 11-15 (Alex, board) | [T226](docs/specs/T226-coreapp-board-ux-1115.md) |
| T175 | 3 | 🔨 | Diag core: a timed probe engine plus TOML tests measure the joystick/paddle ports, results to SD | Merged; diag v2 with testers 11-04 (pins, stick2 ok, cd32 6/24); S4, S7 board, S8 numbers open | [T175](docs/todo/T175.md) |
| T176 | 3 | 📅 | Core menus on the screen at once from a saved picture (split out of T146 P5, issue #4) | Deferred (Alex 11-02): split out of T146 P5; not started | [T176](docs/todo/T176.md) |
| T177 | 3 | 📅 | Recovery boot: a key held at power-on starts a recovery image, even with a black host menu | DECIDED (Alex 11-02): app, RESTORE+F1, recovery.cor/backup, no flash, LED; build open | [T177](docs/todo/T177.md) |
| T178 | 3 | 📅 | JTAG tools (trxdbg, load, jtag_stat, partial load) through an FT232H as well as the USB-Blaster | Decided (Alex 11-03): pin Chris's repo + PRs; tools + JTAG recovery kit; --cable or TOML; build open | [T178](docs/todo/T178.md) |
| T179 | 3 | 📅 | A USB trace shows why a core on a USB stick is slow or missing at power-on | Spec decided (Alex 11-04, Q1-Q4); nothing built | [T179](docs/todo/T179.md) |
| T180 | 3 | 🔨 | Media housekeeping: a mount replaces what is mounted, mounts persist (F8), Recent/Favorites, file ops | D1-D10 built; open: media-set rows, +/- hint, names >127; board test | [T180](docs/todo/T180.md) |
| T182 | 3 | 🔨 | C64 with SuperCPU (65816, SuperRAM) as its own core C64SCPU | Board 11-10: SCPU ROM to READY, 1581; step 7 (REU, cart, Mount&Run) not built; CMD 1.4, JiffyDOS board open | [T182](docs/todo/T182.md) |
| T181 | 3 | 🔨 | Hotkeys while a core runs (menu, volume, ...) without opening the menu | Board 11-07: works (Alex, S1-S4); S5 C= + CTRL on Apple II, Atari, ZX, CPC never pressed | [T181](docs/todo/T181.md) |
| T166 | 3 | 📅 | Tester checklists: a GitHub wiki page or discussion with tickable test lists per core | Reminder (Alex 10-31): with the next test release; nothing posted yet | [T166](docs/todo/T166.md) |
| T167 | 3 | 🔨 | UI: a changing progress bar or percentage in an overlay shows what lies behind it while redrawing | Built; 5.10 passed on the board (11-02); 5.8 format/update page look and 5.9 TrxPage updater re-check open | [T167](docs/todo/T167.md) |
| T170 | 3 | 📅 | C128 core with real drives on the IEC port: 1541, and 1571 with fast serial (SRQ) | Spec proposed (Alex 10-31); D1/D2 decided; nothing built, needs room (T140 D24) | [T170](docs/specs/T170-c128-real-drives.md) |
| T171 | 3 | ✅ | MultiCore publishes into its own generic package trx64u-multicore, not into trx64-ultimate | Done: first CI publish run 279 -> main-a938c680 (2026-10-31); contract §1 written | [T171](docs/todo/T171.md) |
| T129 | 3 | 📅 | SNES core as a full (large) core | Planned as a full flat core (like the C128); source not chosen, spec not written | [T129](docs/todo/T129.md) |
| T130 | 3 | 📅 | Atari ST core | Planned; candidate MiSTeryNano has no licence file (authors to be asked); spec not written | [T130](docs/todo/T130.md) |
| T131 | 3 | 📅 | App API description and SDK for outside developers | Planned (Alex 10-26): foreign apps load (T104.3 Q3); API doc and SDK not written | [T131](docs/todo/T131.md) |
| T132 | 3 | 🔨 | Updater bugs from the 1.1.0 install: title off-centre, SD not taken for the flash backup | Both fixed in apps/ultimate `t135-installer`, proven in ue2emu; board check, submodule pin | [T132](docs/todo/T132.md) |
| T134 | 3 | ✅ | Commodore PET core | Board start 11-11, demos run; as is (Alex 11-11); reopened 11-12 for T216 | [T134](docs/todo/T134.md) |
| T135 | 3 | 🔨 | Direct boot of the default core, menu on a held key | S1-S7 proven in ue2emu; open: S8, S10, S12, B1, B2; board S1/S8/S9/S13/B2 (S2 10-28, S3 11-11 passed) | [T135](docs/todo/T135.md) |
| T136 | 3 | ✅ | A1200 in full AGA colour (24 bit to HDMI) | Done: S1-S5 passed (board: frame buffer holds 8-bit colour, WNS +0.380); merged to main with t148-core-folder | [T136](docs/todo/T136.md) |
| T137 | 3 | 🔨 | Host app: convert .cfw/.ue2 to .cor (built), restore original firmware (R, built), update | Restore passed; D11 look passed on the board (11-02); D10 restart built (T152 D11), board step open; P board step | [T137](docs/todo/T137.md) |
| T140 | 3 | 🔨 | Analogue picture on the A/V port: S-Video and composite, PAL and NTSC | #19/#21 fixed 11-09; #67 board pass 11-11; hang after warm reset, Reload core works (5.17); load stop; T170 SRQ | [T140](docs/todo/T140.md) |
| T141 | 3 | 🔨 | USB keyboard types into every core (C128 extra keys, Amiga raw keys), not only the menus | A1200 board ok; A500 queue + C128 extra keys built (Vivado+board open) | [T141](docs/todo/T141.md) |
| T142 | 3 | 🔨 | SNES as a large core from MiSTer SNES (snestang Verilog) | Board 11-02: sound correct (BRR sign), USB pad ok; open: menu verdict text | [T142](docs/todo/T142.md) |
| T143 | 3 | 🔨 | Neo Geo as a large core from MiSTer NeoGeo | Board 10-31: plays; stripes fixed in pixw (needs board); USB pad ok 11-02 (T156); load progress open | [T143](docs/todo/T143.md) |
| T146 | 3 | 🔨 | The host menu is on the screen at once: a saved picture of the last menu, LVGL builds behind it | Board: picture at 3.1 s (S1 < 1 s); core menus split out to T176 (11-02) | [T146](docs/todo/T146.md) |
| T147 | 3 | 📅 | A logo on the host app's start screen | Planned (Alex 10-28); no spec | [T147](docs/todo/T147.md) |
| T152 | 3 | 🔨 | Our update as .txu (host, app), split from foreign firmware; flash locked under large cores (F8) | S8 ESP32 flash built (U on inspect, 11-07); board: S2/S5/S6b/S7/Q5, locked text, S8 | [T152](docs/todo/T152.md) |
| T154 | 3 | 🔨 | Cartridge and battery-RAM saves go back to the user's file (Off/Ask/Auto), shared app base, all cores | Merged 10-29: service, C64 flash/EEPROM, EF3 built and benched; board S1 and P3/P4 open | [T154](docs/todo/T154.md) |
| T155 | 3 | 📅 | C128: run a cartridge from the generic app, then its write-back on the T154 service | Planned (Alex 10-29); spec to write later | [T155](docs/todo/T155.md) |
| T156 | 3 | 🔨 | USB gamepads drive the cores' own controllers, generic HID parsing in the app base; NES, SNES, Neo Geo | Board 11-02: NES, SNES, Neo Geo pads ok (Neo Geo map by position); open: real pad dump | [T156](docs/todo/T156.md) |
| T157 | 3 | 🔨 | Every core can swap the two joystick ports from its menu (generic app), issue #9 | Built: amiga, a1200, nes, c128, neogeo; vc20 none (Alex), snes none; CI bitstreams + board S5 open | [T157](docs/todo/T157.md) |
| T159 | 3 | 🔨 | Case LED 1 = floppy LED (drive motor + hard disk), menu On/Off, A1200 first | Board 11-04: A500, Atari, Apple II, CPC, C128 pass; C264 and VC-20 not seen yet | [T159](docs/todo/T159.md) |
| T160 | 3 | 🔨 | Every core has a master volume (SHIFT+/SHIFT- in the menu), gain in the host | A1200 board S3 passed; host gain + toast on every page built (T164 WP11), board S3 on another core open | [T160](docs/todo/T160.md) |
| T161 | 3 | 🔨 | A GitHub bug report needs version, core, steps, photos and the T162 report (issue forms) | Forms + lint built, on GitHub; S3 Alex looks | [T161](docs/todo/T161.md) |
| T162 | 3 | 🔨 | The host app writes a bug report file (F8 menu, /SD/report/, GET /v1/report) | Host and core app built and tested; board step open | [T162](docs/todo/T162.md) |
| T163 | 2 | 🔨 | Host and core settings in our own store, apart from Gideon's (FTP off after 1.1.0) | Pkg A, S3, S4 built; open: integration run, board FTP step | [T163](docs/todo/T163.md) |
| T164 | 2 | 🔨 | Next host package: HDMI modes + scaler in the host, flash lock, volume, lamp, port clock, NTP | Board 11-02 (c83a83c2): host, A1200 ok; D10p, WP8c in main; D10q built; C128 picture passed 11-04; board test | [T164](docs/todo/T164.md) |
| T138 | 3 | 🔨 | A1200 with 32 MB Zorro III fast RAM | Board S5 half: avail shows 32 MB (Alex 10-31); Deep Meet not tried yet | [T138](docs/todo/T138.md) |

## Done

| ID | M | Status | Task | Open | Details |
| T169 | 3 | ✅ | User manual rebuilt for the host app: one chapter per menu, engineering-manual style, emulator screenshots | Done (Alex 11-14, closed after the §5 check); left: colour page app fix + picture, Run Update for TRXBK on a backup-started firmware | [T169](docs/todo/T169.md) |
|---|---|---|---|---|---|
| T115 | 3 | ✅ | The host starts a full firmware from a .cfw (flash file + IPROG): Gideon's original and the large TRX64 | Done (Alex 11-14); proven in daily board use (.cfw start) | [T115](docs/todo/T115.md) |
| T109 | 3 | ✅ | A core that makes sound is heard, and silence cannot come back unseen | Done (Alex 11-14); sound proven in daily use, silence-watch board proof waived | [T109](docs/specs/done/T109-core-sound-reaches-the-speaker.md) |
| T174 | 3 | ✅ | CI efficiency: bitstream-input keys, one test cache for agents and CI, early red, NAS runner takes cheap jobs | Done (Alex 11-14); CI is much better since T174 | [T174](docs/todo/T174.md) |
| T168 | 3 | ✅ | Showcase: DDT's C64U demos (mandelbrUlt, look_ma_no_vic) as partial cores, picture over the host's HDMI | Done (Alex 11-14); both run on the board incl. the 6-engine ddt_mandel | [T168](docs/todo/T168.md) |
| T172 | 2 | ✅ | Bundle step: board installer on any branch on demand ([bundle] / dispatch), branch+commit or SemVer | Done (Alex 11-14); branch installers install on the board | [T172](docs/specs/done/T172-bundle-step.md) |
| T148 | 3 | ✅ | Every core has one folder beside its .cor: cfg, roms, saves; no shared files; F8 maintenance | Done (Alex 11-14); proven in daily board use (core folders) | [T148](docs/todo/T148.md) |
| T149 | 3 | ✅ | SD card at 25 MHz without errors: IOB capture and timing constraints for the SD pins | Done (Alex 11-14); proven in daily board use (SD at 25 MHz) | [T149](docs/todo/T149.md) |
| T153 | 3 | ✅ | The UI is an SDK (widgets, layouts, TOML content) with a written contract and a BDD suite run in ue2emu | Done (Alex 11-14); host-app migration is T192 D12, contract lint D7.1 moves to T192 | [T153](docs/todo/T153.md) |
| T158 | 3 | ✅ | The generic core app browses remote FTP servers (URL, login) like a medium, as the Ultimate app does | Done (Alex 11-14): Q1 ported, Q2 plain in settings, Q3 upload on unmount; URL entry not built | [T158](docs/todo/T158.md) |
| T139 | 3 | ✅ | Host app: file delete/copy/move, rename entries, settings page (F8) | Done (Alex 11-14); proven in daily board use (file management, rename, settings page) | [T139](docs/todo/T139.md) |
| T113 | 3 | ✅ | Commodore 128 core: analysis (source, cost, contract fit), then a spec | Done (Alex 11-14); known nit: VDC interlace in a Boulder game, no todo | [T113](docs/todo/T113.md) |
| T79 | 3 | ✅ | Partial reconfiguration trial: small static layer, C64 side swappable | Done: cores swap through ICAP, return to the host by IPROG works (Atari, NES) | [T79](docs/todo/T79.md) |
| T81 | 3 | ✅ | Core selector: small LVGL host app picks a core from USB/SD and starts the swap by softboot | Done: the host app menu (T84, T101); null core not built | [T81](docs/todo/T81.md) |
| T83 | 3 | ✅ | VIC-20 (VC-20) core from VIC20Nano as the second core in the T79 host/core split | Done (Alex, 2026-10-23): picture, sound, .d64 on the 1541 with autostart; the start click stays | [T83-vc20-core](docs/specs/done/T83-vc20-core.md) |
| T84 | 3 | ✅ | Boot chain: host + null core at 0x0, boot app finds `.cor` bundles on USB/SD, cores loaded through ICAP (no ESP32) | Menu starts cores by ICAP; host key in the .cor, checked (D5b); done (Alex), not built: update from the host app, null core | [T84-boot-chain](docs/specs/done/T84-boot-chain.md) |
| T87 | 3 | ✅ | CI per layer: host cache, per-core caches, pipeline without interleaving | Done; Q4 settled by D16; the finer split per core is T100 | [T87](docs/todo/T87.md) |
| T93 | 3 | ✅ | One core-application blueprint on the lean base (LVGL, descriptors, whitelist menus) | Done, reduced scope: superseded by T104; REST, net and the board check moved to T104 | [T93](docs/todo/T93.md) |
| T106 | 1 | ✅ | The host app answers a browser and answers us in JSON: see the menu, start a core, set the picture | Done (Alex, 2026-10-23): page and JSON work; its picture setting waits on T105 | [T106](docs/todo/T106.md) |
| T114 | 3 | ✅ | Every shipped core has a recognisable logo in the TRX64 LVGL theme and a pack-ready 32x32 icon | Done: nine logo sets, packed into every .cor, drawn by the boot menu (bench) | [T114](docs/todo/T114.md) |
| T118 | 3 | 🔨 | The HDMI picture is the same for every core, or we know which signal differs | Bench: encoder input identical for VC-20 and A1200 (hypothesis refuted); board difference open | [T118](docs/todo/T118.md) |
| T123 | 3 | ✅ | The flash holds only the host: the core partition carries a null core, not the TRX64 core | Done: board flash holds host + null core (null_full.bit at 0x0, run 1022 package, 2026-10-28) | [T123](docs/todo/T123.md) |
| T133 | 3 | ✅ | Host I+D cache for the RISC-V (branch t133-dcache): faster menu | Merged to main 2026-10-28 with the host pin (Alex: "Ja, darfst Du."); board-tested in run 1016/1022 | [T133](docs/todo/T133.md) |
| T85 | 3 | ✅ | Amiga (Minimig) core from NanoMig as the third core in the T79 host/core split | Done: A500 runs (generic app, disk change from the menu boots); board confirmed by Alex 2026-10-28 | [T85](docs/todo/T85.md) |
| T88 | 3 | ✅ | Atari 800 XL core from Mark Watson's MiSTer Atari800 core as a core in the host/core split | Done: the core runs; media (disk, .xex) continue in T108 (closed 2026-10-28, Alex) | [T88-atari800-core](docs/specs/done/T88-atari800-core.md) |
| T90 | 3 | ✅ | Commodore 264 core (C16, Plus/4) from C16Nano as a core in the host/core split | Done: Lykia, sound, 64K default, 1541 ROM, F7 reset; board confirmed by Alex 2026-10-28 | [T90](docs/todo/T90.md) |
| T94 | 3 | ✅ | TRX64HOST repository: host as base artefact, core pipeline, release sets and GitOps per board | Done: the repository works as designed (host + n cores, CI by change, packages); no deploy/retention script | [T94](docs/todo/T94.md) |
| T100 | 3 | ✅ | Host and each core get their own pipeline (the design for T94 P2/P3) | Done: lanes per core, build by change; D8: no CI release, releases by hand on GitHub (Alex 10-28); D7: benches nightly + tools/benches.sh, not in push; D14: app tests + render benches cached per test; D15: no Gideon app build (Alex 11-01) | [T100](docs/todo/T100.md) |
| T101 | 3 | ✅ | Core-neutral host app UI: scrolls, icons, confirms removal, lists only what is added by hand (extends T84) | Done: scrolls, icons, lazy start check, splash; the row/boot.cfg mismatch is fixed with T139 | [T101](docs/todo/T101.md) |
| T104 | 2 | ✅ | One generic application for every core, the C64 too (D9), described by each core's own bundle | Done: generic app on every DFX core; Reload core and fast seek seen on the board (Alex) | [T104](docs/todo/T104.md) |
| T121 | 3 | ✅ | README and user manual in engineering-manual style; architecture site without diary | Done: README, manual, site; each spec that changes operation updates its manual chapter in the same commit | [T121](docs/specs/done/T121-user-manual.md) |

## Dropped or merged

| ID | M | Status | Task | Open | Details |
|---|---|---|---|---|---|
| T150 | 3 | ❌ | Host/core edge registered and budgeted; CI margin check; a host change keeps core timing | Won't do (Alex 11-14); nothing built | [T150](docs/todo/T150.md) |
| T151 | 3 | ❌ | Every core keeps its inserted media across reset/restart (setting); "Reset and eject all" | Won't do (Alex 11-14); T180 D2 keeps the mounts | [T151](docs/todo/T151.md) |
| T24 | 2 | ✖ | Own LVGL UI and boot (from TRX64-Ultimate M2, 2026-10-16) | Covered by T101 (host UI) and T104 (core apps) | [T24](docs/todo/T24.md) |
| T32 | 2 | ✖ | Own TRX64U settings menu (from TRX64-Ultimate M2, 2026-10-16) | Covered by T104 (settings per core) and T101 (the host's) | [T32](docs/todo/T32.md) |
| T36 | 2 | ✖ | Decoupling stage 2: own UI and own API for everything (from TRX64-Ultimate M2, 2026-10-16) | Covered by T101, T104 and T106 | [T36](docs/todo/T36.md) |
| T46 | 3 | ✖ | Release build switch: leave out jtag_dbg (frees about 480 host LUT) | Merged into T110: leaving jtag_dbg out is one of the release build's switches | [T46](docs/todo/T46.md) |
| T48 | 2 | ✖ | App CPU headroom for the own UI (LVGL) (from TRX64-Ultimate M2, 2026-10-16) | Merged into T101: the host app's frame times are unmeasured | [T48](docs/todo/T48.md) |
| T52 | 2 | ✖ | Web interface out of the firmware into a loadable app (from TRX64-Ultimate M2, 2026-10-16) | Covered by T106 (page and JSON routes in the host app) | [T52](docs/todo/T52.md) |
| T98 | 3 | ✖ | The Amiga core can say why its sound crackles: the FIFO's counters out to the app | Merged into T85: the crackle and its measurement are T85's open item | [T98](docs/todo/T98.md) |
| T102 | 3 | ✖ | The Amiga 1200 core has an application (AGA, 2 MB chip RAM kept after the commit) | Merged into T97: one row for the Amiga 1200, gateware and application | [T102](docs/todo/T102.md) |
| T91 | 3 | ✖ | ZX Spectrum core from ZX-Spectrum_MiSTer in the host/core split (core order position 8; the CPC is T92) | Merged into T119 (the picture path through the DDR2 frame buffer); its open board finding lives there | [T91](docs/todo/T91.md) |
| T23 | 3 | ✖ | Other cores (candidates and licences: docs/other-cores.md) | Split into one row per core; candidates stay in docs/other-cores.md (closed 2026-10-28, Alex) | [other-cores](docs/other-cores.md) |
| T103 | 3 | ✖ | A second C64 core, Gideon's set as far as the untouched host allows, without ring/TRX64/trxdebug | Dropped: large cores start from the menu; a vanilla C64 on the MultiCore host is not needed (use the original) | [T103](docs/todo/T103.md) |
| T105 | 1 | ✖ | The picture a board comes up in: asked of the screen, kept in boot.cfg, one blind key out | Overtaken by T107 (the host stays at 1080p, each core picks its mode); mode choice moves to F8 (T139) | [T105](docs/todo/T105.md) |
| T126 | 3 | ✖ | Commodore PET core | Merged into T134 (same core; Alex, 2026-11-05) | [T126](docs/todo/T126.md) |
