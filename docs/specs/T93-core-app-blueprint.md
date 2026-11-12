# T93 — One core-application blueprint on the lean base serves the Amiga, Atari 800, Apple II, Spectrum and CPC cores

**Status:** BUILDING (2026-10-15, M1 of 5 for atari800 + nes, 5.3-5.5): all 5 open questions answered by Alex on 2026-10-13, the place decided on 2026-10-15 (§5.1).
**Repo:** TRX64U-MultiCore, `coreapp/` (Alex, 2026-10-15; the line said TRX64-Ultimate `software/coreapp/` until then).
The lean base it shares is in this repository too: `host-app/software/hostapp` (LVGL, `cor.cc`, `discover.cc`, `bootcfg.cc` from
T84/T101) and the services under `host-app/software/`, so nothing has to be copied across repositories.
**Number:** T93.
**Milestone:** M2.
**Origin:** Alex, 2026-10-13, T84 D12: "rather the lean app .. yes. That is better as the way forward. For VIC20/C16 I still see it with the C64
Ultimate app UI, because they are close. The rest not" (translated).
**Depends on:** T84 (lean host app `software/hostapp`, `.cor`, D10 base, D11 settings layering, D12), T79.1 (host/core contract),
T85 (Amiga: the requirement list in code), T88 Atari 800, T89 Apple II, T91 ZX Spectrum, T92 CPC (the descriptors to fill).
**Touches (consequences, not edited here):** T85, T88, T89, T91, T92 say "an Ultimate-app image on Gideon's base, one app per core with
`#ifdef`" (T91 D1: "decided for every core"). D12 replaces that; those sentences are stale as soon as this spec is accepted.

Decided by Alex before this spec (T84 D11, D12), not re-asked:

- The host and the TRX64 (C64) core keep their settings in the flash. Every other core keeps them in `<name>.cfg` beside its `<name>.cor`,
  reads the flash settings as its defaults (host == default, core == local override), and never writes the flash. The host passes the
  `.cor` path at the handover.
- Freeze for every core like the C64: the machine stops, the full menu is up, leaving the menu resumes exactly. A reset from the menu
  closes the menu. Menus contain only what fits the core.
- VC-20 (T83) and C16 (T90) may stay on Gideon's Ultimate app. Amiga, Atari 800, Apple II, Spectrum and CPC use the blueprint.

---

## §1 The problem

**Who:** Alex at the board, who wants to pick a core and have a menu that offers exactly what that machine has; whoever writes the
application of the next core.

**What happens today.** The VC-20 application (`1541ultimate-wt-vc20app` (removed 2026-10-22; branch `t83-vc20app` on Gitea), `doc/VC20.md`) and the Amiga application
(`1541ultimate-wt-amigaapp` (removed 2026-10-22; branch `t85-amigaapp` on Gitea), `doc/AMIGA.md`, `software/io/amiga/amiga.cc`, 1,316 lines) are Gideon's complete Ultimate app with the C64
side switched off by `#ifdef VC20_APP` / `AMIGA_APP`. Read from those two documents:

- The product is a **blacklist**: each document carries a "Removed from the menus" table of about 25 rows (drives, SoftIEC, printer,
  modem, tape, streams, Assembly 64, monitor, cartridge, REU, SID pages, palette, LED, power, file-type menus, ...), plus a "Gated out"
  table. The Amiga table repeats most of the VC-20 table and adds its own. Every further core would derive the same list again, and a
  new Gideon feature appears in every core's menu until someone notices.
- The image is large: the VC-20 image is 1.39 MB (`doc/VC20.md`); the lean host app is 588.5 KB (`hostapp.bin`, t84-bootapp build).
- The `#ifdef`s live inside Gideon's tree (`usb_hid.cc` calls `amiga_mouse_motion`; config pages, task menu, file-type registration,
  `U64Config::init`, `ultimate_main` are all patched). That is the opposite of D10 ("the foundation of Gideon's Ultimate app hangs in
  everywhere") and it makes every upstream rebase (3.15 and later) a merge of five patch sets.
- Settings: Gideon's config pages write the flash. D11 forbids that for every core but the host and TRX64, and nothing implements the
  `.cfg` layering.

**What the five new cores need** is the same list over and over (core ID check, ROM files from the three roots with a retry loop,
register-window service task, pixel-layer or palette video, Freeze, reset/reload, disk/tape/snapshot media served from SD/USB, input
mapping, settings page, REST for the tests). The Amiga app is the only place where that list exists as code today.

Parts (MECE):

- **P1 Shared services.** What every blueprint core uses, built once on the lean base.
- **P2 Per-core description.** What differs per core, and where it is written down.
- **P3 Menu and settings.** A whitelist from the description; `.cfg` layering (D11).
- **P4 Handover and start.** How the app learns which core it is and where its `.cor` lies; start order.
- **P5 Freeze and the picture.** Menu on the lean base's LVGL pixel layer while the core's own picture also uses a pixel layer.
- **P6 Packaging.** One image or one per core; how it relates to the `.cor` and to CI.
- **P7 Migration.** How the Amiga app moves off Gideon's base.
- **P8 Size and speed.** Budget on a CPU without data cache.

---

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the app starts from the .cor and finds its core (P4)
  Given /USB1/cores/amiga.cor started by the host app, the core held in reset
  When  the app starts
  Then  it reads the handover (path /USB1/cores/amiga.cor), reads the core ID block, finds the descriptor "AMIG" and logs "[core] AMIG Amiga, cor /USB1/cores/amiga.cor"
  And   an ID block that matches no descriptor, or a descriptor other than the one the .cor manifest names, gives a message on screen and the core stays held

Scenario: S2 ROM files (P1)
  Given a descriptor with required ROM "kick.rom" (256 or 512 KB) and no stick inserted
  When  the app starts
  Then  within about 10 s the screen names the missing file and the roots searched, it retries every 0.5 s without a restart
  And   inserting the stick starts the core by itself; a file of a wrong size is refused by name and size; the core is never released with a missing required ROM

Scenario: S3 the menu is a whitelist (P3)
  Given any blueprint core
  When  the menu tree is dumped (REST `GET /v1/core/menu`, and a native render test)
  Then  it equals the golden list generated from the descriptor plus the fixed shared entries (D8)
  And   no entry exists that is not in one of the two (no Drive A, SID, REU, cartridge, monitor, printer, modem, LED, power, streams, Assembly 64, apps)

Scenario: S4 Freeze like the C64 (P5)
  Given a running core, Interface = Freeze
  When  the menu button, F10, ScrLock or REST `machine:menu_button` opens the menu
  Then  the app sets C64_STOP, waits for the core's PAUSED (at most 200 ms, else a console line and the menu opens anyway), switches the pixel layer to the menu, and the machine's state does not change while the menu is up
  And   closing the menu restores the core's layer set-up and releases C64_STOP; the machine continues where it stopped
  And   Reset from the menu closes the menu and the core boots
  And   a key that closed the menu (RETURN/RUN-STOP) is not delivered to the machine

Scenario: S5 settings layering (P3)
  Given flash settings with a network and clock set, and no amiga.cfg
  When  the app starts, then the user changes "Chip RAM" and, in the network page, the hostname, and leaves the page
  Then  the core runs with the flash network and clock values, both changes are in /USB1/cores/amiga.cfg, the flash is byte-identical to before (read back and hashed)
  And   deleting amiga.cfg (or "Reset to host defaults") returns every value to the flash value at the next start

Scenario: S6 the .cfg path follows the .cor (P4)
  Given the same core as /SD/cores/amiga.cor and as /USB1/cores/amiga.cor
  Then  the .cfg is /SD/cores/amiga.cfg and /USB1/cores/amiga.cfg respectively, never a fixed place
  And   with the medium write-protected or removed the settings still work for the session and the screen says once that they cannot be saved

Scenario: S7 media services keep their promises (P1)
  Given the Amiga descriptor and a fake window and register model (native test)
  When  an ADF is mounted, written by the model, idle for 1 s, ejected; an HDF request arrives; the stick is pulled
  Then  the file holds the written tracks / sectors, the write-back never runs while paused, a failed write keeps the tracks dirty and says so once
  And   the same service code, given the Apple descriptor's track callbacks, passes the same dirty-log and flush tests (shared, not copied)

Scenario: S8 input reaches the machine (P1)
  Given the C64U key matrix, two joysticks and a USB mouse
  When  keys are tapped shorter than one UI pass, the mouse moves, the menu opens and closes
  Then  every tap is seen once (timer scan), mouse deltas reach the descriptor's mouse sink at most once per its interval, and while the menu is up nothing goes to the machine

Scenario: S9 REST for the tests (P1)
  Given a running blueprint core
  When  `PUT /v1/machine:reset`, `:reboot`, `:menu_button`, `GET /v1/machine:readmem?address=..&length=..` and the drive/config endpoints are called
  Then  each does what the descriptor declares for that core (or answers "not implemented" when it declares none), all other C64 endpoints answer "not implemented"

Scenario: S10 back to the host (P4)
  Given any blueprint core
  When  "Return to host menu" is chosen
  Then  the host app's boot menu appears (T84 Q1: the same reload as a power cycle through ICAP IPROG, WBSTAR 0)

Scenario: S11 a new core costs one descriptor (P2)
  Given a native test descriptor "TEST" with ROMs, two settings, one media slot, one task
  When  it is added to the table
  Then  the menu, settings, .cfg, ROM loading, media and REST for it work with no change to a shared source file
  And   `tools/check_coredesc.py` fails a descriptor with a missing mandatory field

Scenario: S12 the Amiga on the blueprint equals the Amiga on Gideon's base (P7)
  Given amiga.cor with the blueprint image and amiga.cor with the old image (same partial)
  When  the Amiga checklist of doc/AMIGA.md is run on both (Kickstart, DF0 boot, ADF write-back, HDF, mouse, Freeze, reset, reload)
  Then  the same results; only then the `AMIGA_APP` branch of the 1541ultimate fork may be removed

Scenario: S13 size and speed (P8)
  Given the release build of the blueprint image
  Then  the image size and, on the board, "key to screen" for a selection move, menu open after PAUSED, and resume are logged
  And   image <= the budget of D14, selection move < 50 ms (the host app's target, T84), the other two times are measured and written into §5 (no gate before the first measurement)

Scenario: S14 nothing else changes (negative)
  Given the host app, the VC-20 and C16 applications on Gideon's base, the TRX64 core
  Then  `tests/hostapp/run.sh` and the host app build are unchanged and green, and the blueprint image contains no flash write path (/Flash is mounted read-only; `nm` shows no flash program/erase symbol)
```

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P4 | start, identity, descriptor lookup | `tests/coreapp/` native (fake ID block) + board log | CI `app`, board |
| S2 | P1 | ROM roots, retry, size check | native with a fake file system; board with a stick pulled/inserted | CI `app`, board |
| S3 | P3 | whitelist menu | native render + golden list from the descriptor; REST dump | CI `app`, board |
| S4 | P5 | Freeze, layer switch, no key leak | native with a fake core (status bit); the core's own `run_pause.sh` for the gateware side; board by hand (photos) | CI `app`, `sims`, board |
| S5 | P3 | `.cfg` layering, flash untouched | native with a fake flash + hash; board: flash read back over JTAG before/after | CI `app`, board |
| S6 | P4 | `.cfg` beside the `.cor`, read-only medium | native; board with a write-protected SD | CI `app`, board |
| S7 | P1 | media services shared | native (window + register models, one test suite for all services) | CI `app` |
| S8 | P1 | input | native (timer scan model); board by hand | CI `app`, board |
| S9 | P1 | REST subset | board script (T71 style) and native request tests | CI `app`, board |
| S10 | P4 | return to host | board by hand (Alex's photo) | board |
| S11 | P2 | new core = one descriptor | native `TEST` descriptor; `tools/check_coredesc.py` in `ci/lint.sh` | CI `lint`, `app` |
| S12 | P7 | Amiga parity | board by hand, both images, same checklist | board |
| S13 | P8 | size, speed | build report + board console log | CI `app`, board |
| S14 | all | nothing else changes | existing suites + a symbol check | CI `lint`, `app` |

Third-party items (Kickstart, Atari OS, Apple ROMs, Spectrum/CPC ROMs, disk and tape images) are never checked in; the tests that need
them run by hand on the board with Alex's files, the native tests use our own small test images.

---

## §3 Decision

### Requirement list: what the two Gideon-based apps needed and where it lives in the blueprint

"Lean base" = `software/hostapp` on t84-bootapp (FreeRTOS, USB host with mass storage and HID, SD/FatFS, file system layer,
lwIP + RMII + FTP, LVGL on the pixel layer with resident views, C64U matrix timer scan, `.cor` reader, ICAP loader, config store
code, RTC code). It has no REST, no telnet, no WiFi/ESP32 path, no HDMI/video settings page, no mouse/joystick path to a core.

| Need (source) | Blueprint service | In the lean base today |
|---|---|---|
| core ID check `AMIG`/`VC20` (amiga.cc, vc20.cc) | Start (D4), descriptor lookup | no |
| ROM search in `/flash/roms/<core>`, `/usb0..3/roms/<core>`, `/sd/roms/<core>`, names, sizes, retry 0.5 s, read-back | ROM manager (D5) | file system yes, manager no |
| write ROM/Kickstart/frame buffers through the core window / ROM apertures | window access helpers (D5) | no |
| ADF whole-file in window + dirty log + idle flush; HDF mailbox sector service | media services: image-in-window, sector service (D6) | no |
| Apple track window + nibblize, Atari SIO frames + XEX loader, ZX/CPC tape pulse stream, snapshots, CPC DSK | media plugins registered by descriptors (D6) | no |
| mouse, keyboard, joystick, USB gamepad | input sinks (D7) | `usb_hid.cc` vendored; keyboard to the UI yes; no sink |
| pixel layer set-up and flip (`FRAME_SEQ`, `FB_LATEST`, `PX_BACK`) | video service (D9) | layer drive yes (LVGL), no core flip loop |
| Freeze `C64_STOP` + PAUSED, layer off/on, watchdog silent while frozen | Freeze controller (D9) | no |
| reset / reload / settings written while held | core control hooks (D4) | no |
| Config page "Amiga Settings", applied at reset | settings from the descriptor (D10) | config store yes, no pages |
| task menu, file-type context menus, Config menu pruning | whitelist menu (D8) | LVGL views only for the boot menu |
| REST `machine:*`, drives, configs; FTP | REST subset (D12), FTP as is | FTP yes, REST no |
| watchdog `[amiga] STALL` counters, console lines | diagnostics hook (D4) | console yes |
| network, clock, FTP, Ident, HDMI scan resolution / digital video mode | shared groups from the flash defaults (D10) | network and RTC code yes; no pages; no video settings |
| return to the selector (T84 Q1) | "Return to host menu" (D13) | ICAP loader yes; IPROG return works on the board (Atari, NES; 2026-10-18) |

### D1 The blueprint is a library plus a table, not a framework with plug-in loading

`software/coreapp/` builds one RISC-V image from: the lean base (shared with the host app, see D2), the shared services (D5-D13), and the
core table `cores/*.cc`, one translation unit per core holding that core's descriptor and hooks. Descriptors are C++ constant tables,
because their hooks are code that is reviewed next to the core's register file; a data-file descriptor (JSON in the manifest) lost: it needs
a parser and a second source of truth for the same registers, and the hooks (nibblizer, tape converter, SIO answers) cannot be data anyway.

### D2 Where the code lives; the GPL base is vendored once

The vendored Gideon files and the host app's own shared code (`cor`, `sha256`, `hostfs`, `bootcfg`, the LVGL port, the keyboard scan) move
into one directory, `software/trxbase/` (`PROVENANCE.md` and `vendor.list` as today); `software/hostapp` and `software/coreapp` both build
against it. That is a refactor of t84-bootapp, done on that branch before the blueprint is built (consequence for T84, not edited here).
Build: `tools/build_coreapp.sh` in `u64-build-env`, same nice/JOBS rules as `build_hostapp.sh`. Licence handling is the host app's
(GPL-3 base with its headers untouched; the cores themselves are TREX-internal, T83 D13). Nothing from `software/ultimate-app` outside
`vendor.list` is used. VC-20 and C16 are untouched.

### D3 The per-core descriptor

One `struct CoreDesc` per core. A field that does not apply is absent (null/empty), never a stub. Mandatory fields are checked by
`tools/check_coredesc.py` (S11).

| Field | What it holds | Example, from the existing apps and specs |
|---|---|---|
| `id` | 4-byte core ID, display name, `.cor`/`.cfg` basename rule, minimum contract version | `AMIG` "Amiga"; `A8XL`, `APL2`, `ZXSP`, CPC |
| `title` | menu title | "Amiga (TRX64 host)" |
| `window` | core window base (`IOBASE + 0x1A0000`), DDR2 window base and size, register-file header | Amiga: `0x101A0000`, DDR2 window `0x1000000` |
| `roms[]` | role, file names (first match, case-insensitive), allowed sizes, required, check hook (e.g. reset vector), target (aperture or window offset), roots order | Atari `ATARIXL.ROM` 16,384 + `BASIC.ROM` 8,192; Apple `apple2e.rom` with vector $FA62; ZX `48.rom`/`128.rom`; Kickstart 256/512 KB list with Auto |
| `settings[]` | groups of typed items (enum, int, bool, file choice): key, label, values, default, `when` (live / at next reset), apply hook or register | Amiga: Chip/Slow/Fast RAM, Chipset, Floppy drives, write protect |
| `tasks[]` | task-menu entries: label, hook, `closes_menu` | Reset, Reload Kickstart + Reset (both close the menu), Eject DFn |
| `filetypes[]` | extension -> list of actions (label, hook) | `.adf` Mount DF0..3; `.hdf` Mount HD0/1; `.atr`; `.dsk/.do/.po/.nib`; `.tap/.tzx/.cdt`; `.sna/.z80`; `.xex` |
| `media[]` | slots: kind (floppy, hard disk, tape, snapshot, program, cartridge image), count, writable, service id and its callbacks | Amiga DF0-3 image-in-window with dirty log, HD0-1 sector mailbox |
| `input` | key source (matrix / matrix + USB), key translation hook, joystick ports and swap, mouse sink and interval, special keys (RESTORE, LARROW as ESC) | Amiga mouse pair at most every 2 ms |
| `video` | path (pixel layer with buffers, pitch, size, scale, position, flip registers; or palette path with palette hook), geometry for the menu switch | Amiga RGB565 720x540 x3, scale 2, x 240 |
| `freeze` | PAUSED register and mask, timeout, enter/leave hooks, "resume needs" (e.g. frame-boundary stop) | Amiga: STATUS 0x0A8 bit 6; VC-20 reference: 0x0A4 bit 1 |
| `control` | init (core held), release, reset, reload, settings-to-registers | Amiga reset sequence `CTRL`, `C64_MODE`, `0A1..0A6` |
| `service` | optional service task body and its period | Amiga flip, dirty log, HDF mailbox, mouse |
| `rest` | which `machine:*` and which memory view (`readmem`/`writemem` address map) | Amiga: readmem 8 MB from the window; writemem refused |
| `diag` | watchdog / counters printed to the console | `[amiga] STALL` |

Request to the core specs, not a blocker: one PAUSED register/mask convention for all new cores would let `freeze` shrink to "default".

### D4 Start order (the same for every core)

1. Read the handover (D11). 2. Identity: read the core ID block; no match -> message, core stays held (S1). 3. Build the resident views
for the descriptor (D8, D14) while the ROMs load. 4. ROM manager (D5), retry loop. 5. Window set-up: clear what the descriptor says,
copy ROMs, frame buffers, layer. 6. Settings -> registers (while held). 7. Release: `control.release`, then the service task. 8. The
menu may open from here; messages of steps 2-5 use the menu path without Freeze (the machine is not running yet, as in the Amiga app).
Reset hooks (`Reset`, `Reload`) call 5-7 again as the descriptor says (a plain reset keeps RAM, a reload clears it, as the Amiga app).

### D5 ROM manager and window access

One implementation of: root order (`/flash/roms/<name>`, `/usb0..3/roms/<name>`, `/sd/roms/<name>`; the `<name>` is the descriptor's folder),
case-insensitive name lists, size and check hooks, write to aperture/window, read back, the 0.5 s retry and the "not found" screen that
names the files and roots. `/flash` is read-only for the blueprint (S14); on the TRX64 Ultimate monolith profile one file, `/flash/trx64u/c64.cfg`, is written (T104.2 Q1, Alex 2026-10-18). ROMs and OS images stay out of the repo, CI and `.cor`.

### D6 Media services (shared code, per-core callbacks)

| Service | Used by | Behaviour taken from |
|---|---|---|
| image-in-window with dirty log | Amiga ADF | amiga.cc: whole file in the window, dirty tracks written after 1 s idle / 5 s, on eject and reset, failure keeps dirty |
| track window | Apple II (.nib copy; .dsk/.do/.po nibblized by a plugin) | T89 D8 |
| sector service (block request mailbox) | Amiga HDF; Atari SIO frames; Apple HDD later | amiga.cc HDF; T88 D8 |
| tape feeder (file -> pulse stream in the window, block index, play/pause/stop/rewind) | Spectrum `.tap/.tzx`, CPC `.cdt` (one converter family) | T91 D9, T92 D9 |
| snapshot / program loader | ZX `.sna/.z80`, Atari `.xex`, later CPC `.sna` | T91 D10, T88 D8 |

Rules of the services: write-back never runs while the machine is paused; a pulled stick is reported once and the in-window copy stays; the
files are owned by the app (it flushes on eject, reset and Freeze exit). Tape SAVE, WOZ, ProDOS HDD and the like are phase 2 in the core
specs and not in this spec. A converter that one core needs (nibblizer, TZX) is added to the shared media code once, never copied.

### D7 Input

The lean base's C64U matrix timer scan feeds both the menu and the core's `keys[63:0]` path (hardware); the app gates what it chooses: with
the menu up the app owns the keys and the core is paused. On leaving the menu the app releases `C64_STOP` only when the matrix has been
idle for a debounce time, so the closing key is not typed into the machine (S4; the debounce is a measured value, §5). USB HID goes
through `InputSink` callbacks declared by the descriptor (`on_mouse(dx,dy,buttons)`, `on_key`, `on_pad`), replacing the
`amiga_mouse_motion` call inside Gideon's `usb_hid.cc` by a registered function pointer in the vendored copy (listed as MODIFIED in
`PROVENANCE.md`). Mouse sensitivity, acceleration and wheel direction exist only for a core with a mouse sink; joystick-swapper and
the settings that never reach a core (documented in "Joystick and mouse" of both Gideon-based apps) do not exist.

### D8 The menu is a whitelist

*Superseded for the generic application by T104.1 Q3 (Alex, 2026-10-18): open world - every contribution of a
loaded library or application appears, the description may hide single nodes by id.*

The menu is generated, never pruned. Three sources and nothing else:

| Source | Entries |
|---|---|
| Descriptor | task entries, settings groups, file types and their actions, the file browser's visible extensions, media slots in the status page |
| Shared fixed set (every blueprint core) | Return to host menu (D13); Network (IP, hostname, FTP/REST on or off); Clock/time zone; HDMI scan resolution and digital video mode; System information (core name/version/id, app build, IP, mounted media, ROM names); "Reset to host defaults" (D10); file browser roots USB/SD/Flash(read-only) and Create Directory |
| Mouse group | only when the descriptor has a mouse sink |

Not carried at all (the content of both "Removed from the menus" tables, stated once as a rule instead of as a list): everything of the C64
machine, drives and their IEC world, cartridge, REU, SID and audio routing pages, tape/modem/printer, streams, Assembly 64, monitor and
developer entries, apps (`.u2a`), firmware update packages, configuration write to flash (Save/Reset/Clear Flash), LED and power pages,
palette, Interface Type (see Q1). A golden list per core is generated from the descriptor and compared in S3, so a new fixed entry is a
visible diff in every core's list. Menu keys as the host app: menu button, F10, ScrLock, REST `machine:menu_button`; the keymapper
(F1 PageUp, F3 Help, F5 Tasks, F7 PageDown) is taken as the host app takes it. Resident views only (D14).

### D9 Freeze and the picture on one pixel layer

Both the Amiga/Atari/Apple/CPC picture and the lean base's LVGL menu use the host's pixel layer (the core window forwards page 6, T83 §5, T85
D5; to be confirmed for each core's window skeleton). They cannot show at once, so the menu is Freeze-only (Q1): on enter the app saves the
core's layer registers (`PX_*`), sets `C64_STOP`, waits for PAUSED, then points the layer at the menu's buffer (960x540, scale 2, as the host
app); on leave it restores the saved registers and releases the stop. The core's frame buffers are not touched (the core is paused and does
not write). The palette-path cores (Spectrum) switch the same way; their raster underneath keeps the host scaler's lock (T91 D12: the stop is
at a frame boundary, at most 20 ms). The flip loop (`FRAME_SEQ` -> `PX_BACK`) is part of the video service and is silent while paused, as is the
watchdog. Messages of the start phase use the menu without Freeze. A reset from the menu goes through while paused, closes the menu, and the
core boots (the pause yields to a reset in the cores' gateware).

### D10 Settings: `.cfg` layering (T84 D11)

- **Read:** at start the app reads the flash config pages (the vendored `config.cc` / flash store, read only) into the "host" layer, then the
  `<name>.cfg` into the "core" layer. A value is the core layer's if set there, else the host layer's, else the descriptor default.
- **Write:** only into `<name>.cfg`, whichever group the item belongs to (shared groups included). Format: Gideon's `.cfg` format
  (`filetype_cfg.cc`, file-backed config pages), so the file is also readable by his tools and keys keep their meaning.
- **Which groups are shared:** Network/Ethernet, Clock and time zone, Services (FTP, REST), HDMI scan resolution and digital video mode,
  Mouse. A shared group in a core app edits the core layer only.
- **Reset to host defaults:** removes the core layer (the `.cfg` or one group), applied at the next start (live where the item is live).
- **Applied:** items with `when = at next reset` are written to the core while it is held, as the Amiga app does; live items at once.
- **Save:** Q3. **Path:** D11. A medium that cannot be written is reported once; settings work for the session.

### D11 Handover

The host app writes a handover block before the jump and the app reads it first: magic, version, the `.cor` path (as the host opened it, up to
255 characters), the core name and version from the manifest, the host build id. Place: one 4 KiB page in DDR2 that lies in neither the
app's load range (0x30000..0xE80000), the staging areas (0x03000000..0x03C00000), the trampoline (0x03F00000) nor any core window (the
Amiga's is 0x1000000..0x1FFFFFF); proposed 0x03EFF000, fixed in `coreboot.h`/`cor.h` and in the contract (T79.1) when built. An app that
finds no valid block (JTAG load, developer start) uses no `.cfg`, has no path, says so on the system page, and still runs. Consequence for T84:
`core_jump` fills the block (the staged copy keeps it, as the stage pages are not touched by the trampoline).

### D12 REST and FTP

FTP is the host app's daemon unchanged. REST is a small server on the vendored `httpd` with Gideon-compatible paths and JSON for what
applies, so the T71 robot tests and existing scripts keep working: `machine:reset|reboot|menu_button|readmem|writemem` (per descriptor
`rest`), `drives` mount/unmount/status for the descriptor's media slots, `configs` get/put for the descriptor's settings, `version`, plus
`GET /v1/core` (identity, descriptor name, menu dump for S3). Everything else answers "not implemented"; there is no C64 subsystem behind it.
Compatible-path subset over an own API: it costs nothing extra and keeps the existing test scripts; the cost is that the subset must stay a
subset (no endpoint without a descriptor field behind it).

### D13 Return to host

Every blueprint app has "Return to host menu": the ICAP `IPROG` reload with `WBSTAR 0` (T84 Q1), the same end state as a power
cycle. No host-watched key combination.

### D14 One image for all blueprint cores; budget and speed

**One image.** The same `coreapp.bin` is packed into every blueprint `.cor`; the descriptor is chosen at run time from the core ID block.
Reasons: (a) the identity check already exists as a run-time gate in every app, and one image removes the class "wrong app in the wrong
`.cor`" instead of guarding it; (b) one build, one test run and one version number cover five cores, and a shared-service fix is one
artifact; (c) the code of five descriptors is small next to the base (D14 table) and the image is nowhere near any limit (staging area 4
MiB `STAGE_APP_MAX`, load range 0x30000..0xE80000); (d) T87's layer caches key the app on its own sources, not on a core. Lost: per-core
images would be smaller and a descriptor bug could not enter a sibling's image. Both are small against (a)-(c); a development build may list
only some cores (`CORES=amiga`) to compile faster, a release build lists all.

| Item | Number | Basis |
|---|---|---|
| lean host app image | 588.5 KB | measured, `hostapp.bin` t84-bootapp |
| VC-20 image on Gideon's base | 1.39 MB | `doc/VC20.md` |
| blueprint image | **estimate 0.8 - 1.0 MB; budget <= 1.2 MB** | host app + REST + settings pages + media services + five descriptors; no measurement exists, the S13 report replaces this line |
| staging limit for an app | 4 MiB | `STAGE_APP_MAX` |
| LVGL draw buffer (one) | 1,036,800 B | 960 x 540 x 2, computed |
| Amiga picture buffers | 3 x 1 MB spacing, 777,600 B used each | amiga.cc |

**Speed.** The CPU has no data cache (about 0.4 us per DDR2 load, stores posted; about 1 us per redrawn pixel, T84 board notes), so the host
app's rules are mandatory: every page of every menu is built once at start (D4 step 3, behind the ROM load, not at first open) and kept;
a page change is the hidden flag plus changed labels; lists are fixed row windows refilled, no animations, shadows, gradients, opacity or
full-screen blends. The host app's full build was 628 ms and the full render 910 ms; the settings pages are generated from a descriptor but
are built by the same code path, so their build time is added to the start, not to the menu open.

### D15 Migration of the Amiga app

The Amiga application is the first descriptor: its logic is the reference and is moved, not rewritten.

| Gideon-based Amiga app | In the blueprint |
|---|---|
| `amiga.cc` start, Kickstart scan and Auto choice, window clear/copy, reset sequence | `cores/amiga.cc` hooks + ROM manager |
| `amiga_menu_state` Freeze, PAUSED wait, layer off/on, watchdog silence | Freeze controller + video service (layer switched to the menu instead of switched off) |
| service task (flip, HDF mailbox, floppy dirty log, mouse, write-back policy) | `service` hook + media services |
| Config page "Amiga Settings" | `settings[]` |
| task menu, `.adf` / `.hdf` context menu | `tasks[]`, `filetypes[]` |
| REST machine:* with readmem from the window | `rest` |
| `amiga_mouse_motion` in `usb_hid.cc` | `InputSink` |
| `amiga_hides_*`, `amiga_prune_tasks`, `task_kept`, "Removed from the menus" | gone (whitelist) |
| `AMIGA_APP` / `AMIGA=1` build in the 1541ultimate fork | kept as the oracle until S12 passes, then removed from the fork |

The register sequences of `doc/AMIGA.md` are taken as written; the document still says that nothing was run against the core except the
menu check of 2026-10-13, so the first board run of the Amiga app and of the blueprint are the same event: S12 compares two unproven
images and is therefore also the first Amiga board checklist (risk R1). Per-core register constants live in one header per core next to
the gateware's register file (`cores/<name>/trx/<name>_regs.h`), included by the descriptor, so a register change is one reviewed diff.

---

## Risks

- **R1 Amiga proof.** The Amiga app has not been run against the core. A failure in S12 may be the core, the old app or the blueprint.
  Mitigation: gateware simulations stay the judge for the core (`sim/amiga/run_pause.sh`); keep the old image buildable until S12 passes.
- **R2 Layer sharing.** The Freeze menu depends on every core's window forwarding page 6 and on a clean hand-back of the layer registers
  (the layer registers are write-only on the host, T84 notes: the saved set must be the set the app wrote, not a readback).
- **R3 Key leak at resume.** The matrix reaches the core in hardware. The debounce before `C64_STOP = 0` costs menu-close latency; to be
  measured (§5).
- **R4 GPL base drift.** One vendored copy for two apps; a MODIFIED file (`usb_hid.cc` sink, `keyboard_c64.h`) must be listed in
  `PROVENANCE.md` each time.
- **R5 Size growth.** Descriptors are cheap, media converters are not (nibblizer, TZX/CDT); the budget check in S13 stops creep.
- **R6 Spec drift.** T85/T88/T89/T91/T92 still say "Ultimate app on Gideon's base"; until edited they contradict this spec.
- **R7 Register drift.** Constants written from specs "on paper" (the Amiga document says so). Header next to the `.v` file; an automatic
  comparison against the gateware is a later step.

## §4 Out

- VC-20 (T83) and C16 (T90): they stay on Gideon's Ultimate app (D12). Not migrated, not blocked.
- The C64 / TRX64 core's application: unchanged; it keeps the full Ultimate app and the flash settings.
- Any change to the host app's behaviour, to the `.cor` format, to the ICAP loader, or to the gateware, apart from the handover page (D11).
- "Overlay on HDMI" (menu over a running machine): not in the blueprint (Q1).
- Telnet, the monitor, snapshots, the TRX64 ring, Timewarp, SID/REU/cartridge/UCI for these cores.
- Apps (`.u2a`) and firmware update packages inside a core application.
- Writing the flash from a core application, in any form (D11 of T84).
- Phase-2 media of the core specs (tape SAVE, WOZ, ProDOS HDD, Mockingboard, Saturn/RamWorks, AGA): the services leave room, nothing is built here.
- A USB keyboard as the machine's key source: the core specs decided the C64 matrix first; `InputSink.on_key` is the place when it comes.
- A plug-in loader or descriptors as data files (D1).
- Edits to the other specs' sentences about the app (R6) and to `software/hostapp` (D2): consequences, done with the build.

## §5 Decisions taken while building

**5.1 Where it lives: here, 2026-10-15.** Alex chose `TRX64U-MultiCore/coreapp/` over the Ultimate repository's
`software/coreapp/`, which is what this spec said until today. What made it the cheaper of the two: the cores, their
`core.toml` descriptors and the packer that builds a `.cor` are in this repository, and so is the lean base - T84's and
T101's work landed in `host-app/software/hostapp` (LVGL, `cor.cc`, `discover.cc`, `bootcfg.cc`, `listwin.h`) with the
services under `host-app/software/`. The other way round would have meant two repositories and a second pipeline for the
`.app` artefacts for every core-application change.

**5.2 What the descriptors claim today, and what is true (measured 2026-10-15).** `cores/amiga/core.toml` and
`cores/amiga1200/core.toml` say `app = "blueprint"`, and so do `cores/atari800` and `cores/nes`. The applications that are
actually packed into the Amiga `.cor` files - `dist/coreapps/amiga.app` (1,376,964 B) and `amiga1200.app` (1,377,676 B) -
are **builds of Gideon's base** (`make AMIGA=1`, `AMIGA1200=1` in the 1541ultimate fork), not the blueprint, which does not
exist yet. The Amiga does not suffer from it because that application works; the Atari 800 XL and the NES do: their gateware
routes, their benches are green, and they have **no `.cor` at all** because `app = "blueprint"` names something unbuilt.
That, and not the gateware, is what stands between the Atari core and a board test.

**5.3 Scope of this build (coordinator, 2026-10-15).** The blueprint is built for the two cores that have no application at all
and so no `.cor`: **atari800 and nes**. The Amiga and the A1200 keep the Gideon-base applications that work (5.2); Apple II,
Spectrum and CPC have no core in the tree, so their descriptors are not written. Everything of D1-D13 that those two cores need
is in: library and table, the shared base, start and control hooks, ROM manager and window helpers, the media services for
Atari SIO and XEX, Atari cartridges and NES iNES images, keyboard and joystick for the Atari (the NES pad is out: T95 section 4),
the whitelist menu, the video service and Freeze, settings with the `.cfg`, the return to the host menu. Out, and said so in the
report instead: the Amiga and A1200 descriptors, the three cores that do not exist, the NES controller, REST (D12: the
coordinator's scope names D1, D2, D4-D10 and D13, so the REST server and the descriptor's `rest` field are not built; FTP is the
base's daemon, unchanged), the board test, a release.

**5.4 M1: the skeleton builds and links (2026-10-15, measured).** `tools/build_coreapp.sh` builds `coreapp/src` on the base of
`tools/build_hostapp.sh` (same container, same libraries, same `nice`/`JOBS`); the Makefile is
`host-app/target/u64ii/riscv/coreapp/Makefile`, hostapp's with the application's own sources, so it reads the shared base where T84
left it. **D2's move into `software/trxbase/` is not done**: the host app is not touched for it, `coreapp` VPATHs
`host-app/software/*` directly, which is the same sharing without a refactor of a working program. Result: `coreapp.app`
**586,632 bytes** (text 457,096 + data 129,520; `size output/coreapp.out`) with the base, the file system layer, LVGL, an empty core table
and the message screen; `hostapp.app` after the additive changes below is 645,960 bytes (it was 630.3 KiB = 645,427). Changes to the
shared base, all additive and listed MODIFIED in `host-app/PROVENANCE.md` with patches in `host-app/patches/`:
`trx_io.cc/.h` get `trx_io_alt_id` (the pixel layer is found for a core whose ID block is not "TRX2": `trx_io_caps()` insists on
"TRX2" and the Atari answers "A8XL", the NES "NES_"); `trx_lv_port.cc/.h` get `trx_lv_suspend()` / `trx_lv_resume()` (the one
layer goes to a core's picture and back, LVGL and its views and buffers stay); the host app's `coreboot.cc` fills the hand-over page.
**The hand-over (D11) as built:** `host-app/software/hostapp/handover.{h,cc}`, a 360-byte struct at `0x03EFF000` (magic "TRXH",
version, a rotate-xor sum over the rest, the `.cor` path, the manifest's name and version, the host key as 8 hex digits).
`core_stage` remembers the path, `core_jump` fills the page before the trampoline copies (the trampoline's target range
0x30000..0xE80000 and its source 0x03000000..0x03C00000 do not contain it; the page lies in 0x03C00000..0x03F00000, which no
spec region uses). The block tells noise from a block, not a fresh block from the previous core's (a JTAG start after a core start
sees the old block): that limit is stated in `handover.h`. `tests/coreapp/run.sh`: handover (25 checks, every single flipped byte
of the page is rejected, the `.cfg` path rules of S6) and identity (S1: wrong ID, wrong manifest name, no hand-over), each with a
negative control (5.5).

**5.5 How the native tests prove they can fail.** `tests/coreapp/run.sh` copies the product sources, breaks one expression with
`sed`, builds the same test against the copy and demands a failing run; a `sed` expression that changes nothing aborts the run
(a control that cannot break anything is not a control). The product tree carries no mutation switch.

**5.6 M2: the Atari descriptor, ROMs, window writes, the start sequence (2026-10-15).** `coreapp/cores/atari800.cc` +
`atari800_regs.h`, `romfind.cc`, `boot.cc`, `video.cc`. **What the code says, not the headers** (read 2026-10-15):
- *Register block:* `cores/atari800/trx/a8_regs.vhd` (CTRL 0A0 b0 hold / b1 halt / b2 keep RAM, RAM_SEL 0A1, FLAGS 0A2 b0 swap / b1 PAL
  / b2 XEX mode, CART1 0A3, THROTTLE 0A5, ROM_SEL 0A6, STATUS 0A8 b1 held, FB 0C0-0CB, SIO 0D0-0D5). The constants are in
  `coreapp/cores/atari800_regs.h`, **next to the descriptor, not next to the gateware** as D15 says: `cores/` is not this task's to
  add files to (the register header becomes the gateware's own file when Alex wants it there).
- *ROM aperture:* `host/ultimate_sys.vhd` `r_rom` sends app offset 0x0000-0x1FFF (U64_BASIC_BASE) to window address 0x4000 and
  0x2000-0x3FFF (U64_KERNAL_BASE) to 0x2000; `a8_win.vhd` turns 0x4000.. into array offset 0 and 0x2000.. into 0x2000. So the app
  sees the selected 16 KB array contiguously at `U64_ROMS_BASE`, the OS file goes in as it is (ROM_SEL 0) and BASIC's 8 KB at offset 0
  (ROM_SEL 1) - `tb_a8_machine.vhd`'s `load_rom` does the same with the window addresses, which is the third source that agrees.
- *Reset:* the Amiga app's pattern - CTRL hold + `C64_MODE` reset while ROMs and settings go in, CTRL 0 + `C64_MODE` unreset to release.
  A warm reset (Reset in the menu) is CTRL 0x05 (hold + keep RAM: `a8_core.vhd` looks at b2 when the reset *begins*), a cold start 0x01
  (the 64 KB sweep, 65,536 clocks = 2.1 ms; `reset_ms` = 5 covers it).
- *Settings:* RAM size (RAM_SEL 0-6; 7 = Axlon needs the whole 4 MB window and is not offered), Video standard (FLAGS b1; the PAL bit is
  taken at the next reset), Joystick ports (FLAGS b0, live), CPU speed (THROTTLE 1/2/4/8, live). Items "at next reset" are written
  while the core is held, live ones at once (D10).
- *Picture:* 384 x 288 RGB565, scale 3 = 1152 x 864 at x0 = 384 + 42, y0 = 108 (T88 D5: hblank hides the window's last 28 columns,
  the picture sits 14 hires pixels left of the window's middle, "the app shifts X0"). Layer registers per `pix_layer.v` as
  `trx_lv_port.cc` writes them; `PX_CTRL` = scale << 4 | 2 | enable (the Amiga app's 0x23 for scale 2).
- *Finding for T88/T95:* the cores' four frame buffers sit at FB_BASE 0x01D00000 + n x 1 MiB, so buffer 3 is at **0x02000000, outside
  the application's 16 MiB DDR2 window** (0x01000000..0x01FFFFFF): the core writes it, the application cannot clear it (the video
  service clears the three it can reach and says nothing about the fourth; its first frames are whatever the DDR2 held). The Amiga
  uses three buffers. Needs a decision in the core specs (three buffers, or the window made bigger).
- *ROM files:* `ATARIXL.ROM` (16,384) and `BASIC.ROM` or `ATARIBAS.ROM` (8,192), both required, roots `/flash`, `/usb0..3`, `/sd`
  each under `roms/atari800`, **root first, then name**: the first root with an acceptable file wins (so a user's stick does not
  silently beat the flash, nor the other way round). Retry every 0.5 s, the "not found" text after 20 passes (10 s), a wrong-size file
  at once, by name and size; the core is never released while a required ROM is missing (`tests/coreapp/test_boot_atari.cc`).
- *What is not proven:* **boot to READY on the real core.** The register sequence is the one `tb_a8_machine.vhd` uses and the model test
  checks its order (held before the first ROM byte, settings before the release, release last, nothing released while a ROM is missing),
  but no bench ran the application's sequence against the VHDL and no board was touched; that is the board test.
- *Tests (M2):* `test_romfind` (37 checks, 5 controls), `test_boot_atari` (54 checks, 6 controls), `test_video` (37 checks, 4 controls),
  `test_settings` (60 checks, 7 controls: layering, one-place write, flash image hashed before and after, write-protected card, pulled
  stick, no bundle path, the reader on CRLF/comments/foreign sections/bad labels). `coreapp.app` is 598,724 bytes with the Atari
  descriptor.

**5.7 M3: Atari media - SIO, ATR, the .xex loader, cartridges (2026-10-15).** `cores/atari_sio.cc`, `atari_atr.cc`, `atari_xex.cc` with
`6502/xexboot.asm`, `atari_cart.cc`, `atari_media.cc`.
- *The SIO bridge as the code has it* (`a8_regs.vhd` 0D0-0D5, `a8_core.vhd` SB_* states, `sio_handler.vhdl`): a raw UART, no frame
  logic. Read a register = write SIO_ADDR, SIO_GO b0, wait SIO_STATUS b0, read the 15-bit SIO_RDATA; the bridge needs a machine tick,
  so it **never finishes while the core is held** (the service therefore runs only when the machine runs, never during Freeze, and
  every access has a 200-read timeout). Register 2 pops the receive FIFO but only if it is not empty - on an empty FIFO the
  handler returns the previous word, so the status (register 3, bit 8 = empty) is read first. A receive word is
  `{count[6:0], byte[7:0]}`: bytes 1-5 of a command frame carry counts 1-5, and when the command line rises one more word is written
  with count 5 and a stale byte; data the Atari sends after our ACK has count 0. The frame is complete on a word that is not "byte
  n+1" after five bytes with count 5. The transmit FIFO holds 256 bytes, a 256-byte sector with COMPLETE and checksum is 259: the
  sender watches the fill level. The divisor register is not written (the OS' own speed is used; the Hias high-speed index command
  `?` is NAKed, so the OS stays at 19200 baud).
- *Drive behaviour:* D1:-D4: = device $31-$34. R (read), W/P (write: ACK, data frame, ACK, COMPLETE), S (status: motor on, b5 double
  density, b3 write protected, $FF, $E0), N (the 12-byte drive configuration), O (taken and ignored); format and anything else is
  NAKed; a wrong command checksum is NAKed and reads nothing; a drive with no disk **does not answer** (as an Atari without that
  drive; the OS' own timeouts then make a cold start without any disk a few seconds slower). ACK and COMPLETE are 300 us apart and
  300 us after the command line (the spec minimum is 250 us); **these two numbers are not measured** (no board), they are
  `T2_US`/`T5_US`. Writes go to the file at once; the file is synced after 1 s without a write and on every reset/eject; a failed
  write or sync gives the Atari an ERROR and the screen one line, once per drive until a write works again.
- *Images:* ATR (16-byte header, 128 or 256 byte sectors, in double density the first three sectors packed at 128 bytes; a sector size
  of 512 is refused by name) and XFD (128-byte sectors, or the 184,320-byte double density image with padded boot sectors).
- *The .xex:* **the core's XEX mode is not used.** `FLAGS b2` maps the upstream "PBI BIOS" (`roms/pbi_bios.rom`, "MiSTer SDEMU") that
  talks to an SD emulation in the upstream HPS wrapper; `a8_core.vhd` has nothing behind `$D1xx` for it to read, so the application
  cannot drive it (needs a decision if the mode is wanted). Instead the application presents the file as a **boot disk** over the SIO
  bridge, which needs nothing from the gateware: sectors 1-3 are `xexboot.asm` (own code, 371 bytes at $0700, entry $0706, buffer at
  $0880, no zero page, 24-bit file length patched at offset 9), sectors 4... the file. It switches BASIC off (PORTB bit 1, BASICF),
  loads `[$FFFF] start end data` segments with or without the marker, calls INITAD after a segment and clears it, jumps through
  RUNAD, and returns to the OS when there is none. **Run, not read:** `tests/coreapp/test_atari_xex.cc` executes the loader on a small
  6502 against the disk (segments across sector boundaries, INIT once and before the next segment, RUN, no RUN, a truncated file, a
  disk that fails -> three tries then stop). Limits: a segment that overlaps $0700-$08FF destroys the loader, and a program that
  loads from the disk by itself finds no DOS on it. `tools/gen_xexboot.sh --check` (in `run.sh`) fails when `xexboot_bin.h` differs
  from what 64tass makes of the `.asm`.
- *Cartridges:* raw 2/4/8/16 KB or `.car` (type, size and the header checksum, a mismatch is loaded and told), data into the DDR2
  window at +0x400000 while the core is held, `CART1` written in the hold hook so it is in place when the reset ends, then a cold
  start. The CAR type numbers are **atari800's `cartridge_info.h`** (fetched 2026-10-15), the modes `cart_logic.vhd`'s constants; a
  type is listed only where the core's mode has the same name and size (53 of atari800's 83 types are listed; the others - 5200, M091, THECART and the like - are refused by number).
  This is a name correspondence; no bench ran a game through it.
- *Tests:* `test_atari_sio` (94 checks, 9 controls), `test_atari_xex` (54, 3 incl. the 6502 bytes), `test_atari_media` (55, 7). The SIO
  model (`tests/coreapp/sio_model.h`) is mine, written from the VHDL above, so it shares my reading of the VHDL; the gateware bench
  that would settle it is not run here. `coreapp.app` 609,468 bytes.

**5.8 M4: the NES descriptor (2026-10-15).** `cores/nes.cc`, `nes_regs.h`, `ines.cc`; register block read from `cores/nes/trx/nes_regs.v`,
`nes_flags.v`, `nes_win.v`, `nes_mem.v`.
- *The image goes through the DDR2 window, not through the cartridge DMA window.* T95 5.9 calls `nes_win` (the 64 KB `WIN_PAGE` window
  over the host's C64-RAM / ROM apertures) "the only way to put an image in". The code shows that way cannot carry a CHR image: CHR
  is `p_rom = 1`, which the host reaches only through its ROM apertures (`ultimate_sys.vhd` `r_rom`: window addresses 0x0000-0x0FFF,
  0x2000-0x5FFF and a 1 KB colour RAM), so per 64 KB page about 20 KB of the CHR address space exist and the rest cannot be written.
  PRG (`p_rom = 0`, the whole 64 KB) could. The application therefore writes **PRG at CART_BASE + 0 and CHR at CART_BASE + 0x200000
  straight into the DDR2 window** (CART_BASE = the window's start, 0x01000000, written explicitly), fast (a memcpy) and complete; the
  cache is flushed by the reset the application holds (`nes_mem.v`, "the machine was reset: flush"), so the missing invalidation of a
  direct write does not matter. `WIN_PAGE` is not used. Not run against the gateware.
- *Start:* the core is held and waits (`needs_program`); the first `.nes` of the Files page (M5) holds, writes the 16 header bytes into
  INES 0E0-0EF (the core derives the flags word, `nes_flags.v`), CART_BASE, PRG, CHR, SYSTEM (NTSC/PAL/Dendy setting), then releases
  (`boot_finish`). A file the core cannot run is refused **before** the machine is touched: no iNES magic, a mapper outside the ten
  built modules (T95 5.10 list, `ines_mapper_supported`), PRG or CHR above 2 MB (the layout of the window), a file shorter than the
  header says; a trainer is skipped (image offset 528). A stick pulled mid-copy leaves the machine held and says so.
- *The header parser* computes the same mapper flags word as `nes_flags.v` with the same expressions, so the application and the core can
  be compared; `test_ines.cc` checks four words computed by hand before the code ran (NROM-256 0x4100, UxROM 0x8302, MMC3+battery
  0x2002404, NES 2.0 MMC1 0x800208101) plus a dirty 1.0 header, mapper 257 and the sizes.
- *Picture:* 256 x 224 at scale 3 = 768 x 672 centred (x0 576, y0 204). Freeze: STATUS 0x0A4 b1. No pad (T95 section 4: deferred by Alex).
- *Tests (M4):* `test_ines` (27 checks, 7 controls), `test_nes` (62, 6). `coreapp.app` 613,572 bytes with both descriptors.

**5.9 M5: Freeze, the menu, settings, return to the host, the two .app files (2026-10-15).**
- *Freeze* (`freeze.cc`, `run.cc`): menu button (ITU button 1), F10, ScrLock or the menu key -> `C64_STOP`, wait for the descriptor's PAUSED
  bit (Atari STATUS 0x0A8 b1, NES 0x0A4 b1) at most 200 ms (then a console line and the menu opens anyway), the core's layer off
  (`Video::hide`), the LVGL layer on (`trx_lv_resume`). Leaving: the core's layer registers written again from the set the application
  wrote (never read back, R2), then `Keyboard_C64::wait_free()` plus a **40 ms debounce** (a first value, `Freeze`'s constructor
  argument, **not measured**), then `C64_STOP = 0`. A reset from the menu goes through while stopped (the cores' gateware lets the reset
  take the hold away, `a8_core.vhd` `hold_eff`) and closes the menu. The NES, before its first program, opens the menu on the browser
  without Freeze and cannot close it. The loop logs `menu open N ms after the request` and `machine running N ms after the close key`
  (ms resolution); **no board, so no S13 numbers**. `tests/coreapp/test_freeze.cc` runs the sequence against a fake core whose PAUSED bit
  rises late and a key that stays down: the key-leak control removes the wait and the machine runs while the key is still down.
- *The menu* (`menutree.cc`, `menuctl.cc`, `ui.cc`): pages Machine (F1: tasks, media slots), Files (F3: the browser; it shows folders and
  the files some action of the descriptor wants; one action runs at once, several give a chooser), Settings (F5: left/right/return,
  `[next reset]` marks reset-time items, `(default)` an item not set in the `.cfg`), System (F7: Core, Application, Network, Media, ROMs,
  Reset to host defaults, Return to host menu). Generated, never pruned: `tests/coreapp/golden/menu_<core>.txt` are the dumps the
  tests compare, and a property test checks that every entry is the descriptor's own or one of the fixed ones and that none of the C64
  machine's words appears. A made-up core `TEST` (ROMs, two settings, a slot, a task, a file type) boots, saves its `.cfg`, lists its menu
  and passes the descriptor check without a line changed in `coreapp/src` (S11). `desc_check.cc` is the C++ counterpart of the
  `tools/check_coredesc.py` the spec names (the descriptors are C++ constants); it is run by `tests/coreapp/run.sh`, not by `ci/lint.sh`.
- *Settings:* the three layers and the `.cfg` beside the `.cor` as in 5.6. **The host layer is empty in production**: the two cores' items
  (RAM size, video standard, joystick ports, CPU speed, console type) have no counterpart in the flash settings, so `FlashLayer` is
  not given an implementation on the board (null); the layering and its flash-untouched property are tested with a fake flash. The shared
  groups of D8/D10 (Network, Clock and time zone, Services, HDMI scan resolution and digital video mode) are **not built**: neither
  core needs them, and the lean base has no code that applies them. They are the place the host layer would matter.
- *Return to host menu* (`hw_return_to_host`): the standard 7-series IPROG sequence (dummy, sync, NOP, WBSTAR := 0, CMD := IPROG, NOP)
  through the ICAP data port the host app uses for the partial (`0x10060610`, enable `0x10060614`). It works on the board from the Atari and the NES
  apps (2026-10-18, Alex, 2026-10-18 (translated): "the Atari and the NES have the option and it works. With Return to Host"); the function refuses when the ID byte is not 0x7A.
- *No flash write (S14):* the vendored flash driver's `write_page`/`erase_sector` are in the linked image (they hang on the `Flash`
  vtable that the file system layer needs for `/Flash` reads: `nm` shows them), so the symbol check S14 asks for **cannot be met without
  patching the driver**. What holds instead: the application's own code never calls them (`tests/coreapp/test_noflash.cc` scans its
  sources), `CoreFs` refuses every write, delete and read-write open under `/flash` (`fs_path_is_flash`; a `.cfg` beside a `.cor` that lies
  in the flash stays in memory), and `FLASH_EXTRA_PROTECTION` keeps the chip's own write protection on.
- *Artefacts:* `tools/build_coreapp.sh --dist` writes `dist/coreapps/atari800.app` and `nes.app` (the same 630,320 bytes: one image, D14).
  `tools/cor_pack.py pack` + `check` with `atari800_partial.bit` of `dist/pkg-9bf12877` gives a valid `atari800.cor` (1,960,267 bytes,
  app 630,320). `tools/host_pin.sh --check`: the host is unchanged. **An incident of mine:** the first `--dist` copied the blueprint over
  `dist/coreapps/amiga.app` and `amiga1200.app` too (their `core.toml` says `blueprint`, 5.2). `amiga.app` was restored byte for byte from
  the `app` record of `dist/pkg-9bf12877/amiga.cor` (1,376,964 bytes); the original `amiga1200.app` (1,377,676 bytes, never uploaded) was
  not recoverable and was replaced by the 1541ultimate worktree's current `result_amiga1200/ultimate.app` (1,377,688 bytes, built after
  the T102 commit): rebuild and re-check that file before it is uploaded. The script now writes only `atari800` and `nes`.
- *Size:* `coreapp.app` 630,320 bytes = 615.5 KiB with both descriptors, the menu, the SIO drive and the loader (budget D14: 1.2 MB).
- *Not built, for the record* (each is a decision for Alex, see the report): REST (D12), the shared Network/Clock/HDMI groups, rename /
  delete / copy / move / create directory in the browser (Q5), WiFi (Q4), the USB HID input sinks (D7: the C64U matrix and joystick ports
  reach the core in hardware; the Atari's paddles and a USB keyboard were not part of the Atari decisions Q10/Q11 as built), the Amiga
  and A1200 descriptors, Apple II / Spectrum / CPC.

---

### 5.10 QC of the agent's five milestones (coordinator, 2026-10-15)

Checked here, not taken on trust:

- `tests/coreapp/run.sh`: **15 test programs, every one green, every negative control red** (the script breaks the product
  source with `sed`, rebuilds and demands a failing run; a control that changes nothing or crashes aborts). About 2 minutes.
- `ci/lint.sh` green in 25 s, `tools/host_pin.sh --check` "the host is unchanged (257 files)", `python3 tools/check_todo.py` ok.
- **No ROM in an application image:** the first 256 bytes of `atarixl.rom` and of `ataribas.rom` are not in
  `dist/coreapps/atari800.app` or `nes.app` (both 630,320 B, one image for both cores).
- The hand-over page is where the comment says it is: `HANDOVER_ADDR 0x03EFF000` is one 4 KiB page below
  `TRAMPOLINE_ADDR 0x03F00000`, above the staging areas (`STAGE_APP_ADDR 0x03000000`, `STAGE_PART_ADDR 0x03400000` +8 MiB)
  and outside the application's load range - read in `coreboot.h`, not in the report.
- `tools/cor_pack.py pack` + `check` with `atari800_partial.bit` of package `main-9bf12877` and host key `0e8fb06e`:
  a valid `atari800.cor` of 1,960,267 B.
- Both images uploaded to the generic package **`trx64-coreapps/1`** (that is the version `ci/package.sh` reads,
  `COREAPPS_VERSION` defaults to 1), so the next green run packs `atari800.cor` itself. The NES has no partial bitstream
  yet, so `nes.cor` stays skipped by name.

**The incident, and what it cost.** The agent's first `build_coreapp.sh --dist` wrote the blueprint over
`dist/coreapps/amiga.app` and `amiga1200.app`, because both descriptors also say `app = "blueprint"`. `amiga.app` it
restored byte for byte from the application record in `dist/pkg-9bf12877/amiga.cor`; for `amiga1200.app` it put a
*different* build in place (1,377,688 B) and reported the original as lost. It was not lost: the package
`trx64-coreapps/1` holds it, and `dist/coreapps/amiga1200.app` is now that file again, byte for byte (1,377,676 B).
Checked for all five: `amiga`, `c264`, `trx64` and `vc20` were already identical to the published ones. `--dist` now writes
only the cores in `BLUEPRINT_CORES` (atari800, nes).

### 5.11 The application runs in the emulator, and what the emulator cannot be (2026-10-15)

Alex, 2026-10-15: "do you test the app in the emulator? It can do LVGL" (translated). It was not in this spec; it is now.

`ue2emu` (`/path/to/Development/u64-emulator`, S37) runs an unmodified application ELF on an emulated RISC-V and models
the TRX IO block's page 0 and the RGB565 pixel layer, headless, with a control script that can take a PNG. Measured here
with the images of 2026-10-15:

- `hostapp.elf`: boots, `[TRX UI] LVGL 9.6.0 on the pixel layer`, the boot menu renders 960 x 540, `px_ctrl 0x23` (layer on).
- `coreapp.elf`: boots the same way and then stops exactly as it should:
  `This application has no description for the core with the ID TRX2. The core stays held.` - start, core ID, descriptor
  search and the message on the layer all run.

What the emulator models is the **host**. The core ID is a constant (`crates/ue2-core/src/devices/trx.rs:42`,
`const ID: &[u8;4] = b"TRX2"`), our descriptors want `"A8XL"` and `"NES_"`, and there is no flag for it. So:

- **reachable with a selectable ID** (an emulator change, not a change here): start sequence, menu tree, settings layers,
  the file browser on a real FAT image, the Freeze UI, ROM search in the flash, return to the host menu - with screenshots
  and `expect-console` as a CI stage. Every write to a core register would land in the `ack0` nothing of S41.
- **not reachable at all:** an Atari or NES picture. That needs the core's own register block and machine in the emulator,
  which is a second core there, not a flag. That stays the GHDL bench and the board.

**Hand-over stops every receive DMA (2026-11-09, board, found on integ-1 T192).** `legacy_jump` and `core_jump` stopped
only the RMII receiver; the WiFi `uart_dma` kept its armed receive address with interrupts off, so one more ESP32 packet
landed in the host heap after the copy had put the next application there. Board: A1200 core app `GURU MEDITATION`
at `0x0012880C` (task U-II Main); DDR2 there held `43 FF D4 50 3C 00 FF ...` (an `EVENT_RECV_PACKET`, `rpc_calls.h:220`,
with a 60-byte WiFi broadcast) = `rx_bufs[11].data` of the host's `esp32.packets` (0x124470 + 16 + 11*1572). Both jumps
now set bit 7 (soft_reset) of the uart_dma flowctrl register (`WIFI_UART_BASE + 3`) after interrupts are off:
`uart_dma.vhd` clears `rx_addr_valid`, `rx_dma.vhd` returns to idle with no write pending; the other bits are written
back, the ESP32 runs on. Board: 1 of 5 A1200 starts broke before, 3 of 3 clean after, the 12 rx buffers' range matched
`coreapp.bin` byte for byte. Open: the core app's own update jump (`coreapp/src/hw.cc` run_at, `appsvc.cc`
appsvc_handover) has the same gap (fixed on t192-mvp-hostapi with T192.3).

## Open questions for Alex (one line each)

1. Q1: Menu over a running machine ("Overlay on HDMI") is dropped in the blueprint because the pixel layer cannot show the picture and the menu at once: Freeze only, yes?
   **Answered (Alex, 2026-10-13):** "only Freeze": pixel-layer cores have Freeze only; no menu over a running machine.
2. Q2: The Amiga becomes the blueprint's first descriptor and its old image stays as the oracle until S12 passes, yes (the alternative is a smaller core first, which delays the Amiga app)?
   **Answered (Alex, 2026-10-13):** "a)": the Amiga is the first descriptor; its Gideon-based image stays as the oracle until S12 passes.
3. Q3: Settings in a core app: save to `<name>.cfg` at once on every change, or only on an explicit "Save settings" (as Gideon's "Save to Flash")?
   **Answered (Alex, 2026-10-13):** "a": every change is written to `<name>.cfg` at once; no separate save step.
4. Q4: WiFi (ESP32) in the host and the core apps: out (Ethernet only, as the host app today), or in?
   **Answered (Alex, 2026-10-13):** "b": WiFi (ESP32 over the WiFi UART) is in, for the host app and the core apps; the driver comes from Gideon's GPL code into the shared base.
5. Q5: File browser in a core app: rename / delete / copy / move of files allowed, or browse and mount only (plus Create Directory)?
   **Answered (Alex, 2026-10-13):** "a": the file browser in a core app may rename, delete, copy and move files (plus create directories).

### 5.12 The bundles carried the `.app`, not the image: both blueprint cores were dead on the board (2026-10-15)

Alex, after putting the seven cores on the stick: "der Atari macht gar nix" ("the Atari does nothing").
Over JTAG the host's own log was clean - it staged the bundle, loaded 1,329,576 bytes through the ICAP,
handed over and jumped - and then nothing from the application, not even its banner. `rvpc` found the
RISC-V circling three instructions at `0x00030F00`, which `coreapp.elf` names
`freertos_risc_v_trap_handler`: a trap, and the first stores of the handler trapping again on a stack
that was already gone.

A read of the DDR2 at the load address against the file settled it. The board held the file shifted by
twelve bytes:

```
board 0x30000: 00000300 249e0900 00000300 | 73700430 1701e600 ...
file  0x30000:                              73700430 1701e600 ...
```

Those twelve bytes are the `.app` header - load `0x00030000`, length `0x00099E24`, entry `0x00030000` -
and `coreapp.app[12:] == coreapp.bin` is true. Record 3 of a bundle is the **raw image**: the boot app
copies it to `app_load` and jumps to `app_entry`, both `0x30000`. So the entry point held the header and
the application executed its own load address.

`tools/build_coreapp.sh` copied `coreapp.app` into `dist/coreapps/<core>.app`. The four Gideon-base
images (`trx64`, `vc20`, `c264`, `amiga`) are raw and start with code (`7370 0430`), which is why only
the two blueprint cores were affected - and why nobody noticed when the Amiga went in.

Fixed in three places, because one of them is a rule and the other two are the proof:
- `tools/build_coreapp.sh` copies `coreapp.bin`. The file keeps the name `<core>.app`, which is what
  `ci/package.sh` and `tools/pack_cores_mc.sh` look for.
- `tools/cor_pack.py pack()` refuses an image whose first three words are the load, the length and the
  entry of that same file. The test is exact: three words that do not describe this file are data.
- `tools/test_cor_pack.py` has both controls - the real shape is refused, and an image that only looks
  like one passes (12 tests).

Confirmed against the files themselves: `coreapp.app` is refused with the header printed back,
`coreapp.bin` packs. The two bundles of `trx64mc-main-51e89821` were repacked by hand for the board test
(1,960,255 and 1,949,860 bytes, twelve bytes smaller each, checked against host key `0e8fb06e` and
contract 1); the released ones are still the broken pair and must be replaced in the next package.

### 5.13 The rocker: what reaches the application, and what was wrong in this section before (2026-10-15)

Alex: "das Freeze menu geht aber mit der Wippe nicht auf wie bei den anderen Cores", and, exactly right:
"wenn es doch in den Cores mit Gideon App klappt, dann muss es doch mit dem Ersatz UI auch gehen und kann
NUr am Code bei uns liegen." Later: "Kopier doch EINFACH den Code von Gideon dazu .. weia, ist GLP, darf
man doch abschreiben."

**The button read is already his, line for line.** `run.cc:245` calls `hw_menu_button_pressed()`
(`hw.cc`): `ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS`, rising edge of `ITU_BUTTON1` - the same register,
mask and edge as `c64.cc:2147` and `overlay.h:163`, and both builds carry `-DIOBASE=0x10000000`. There
was never anything to copy there.

What the rocker has to pass to get that far, read end to end this time:

1. The ESP32 sends the event as a **packet**: `send_button_event()` fills an RPC header whose first byte
   is the code and hands it to `my_uart_transmit_packet()` (`u64ctrl/main/rpc_dispatch.c:389-402`,
   codes in `rpc_calls.h:220-221`). It sends nothing on its own otherwise - keepalives go out only when
   the host asks (`rpc_dispatch.c:375-387`). A module nobody touches is legitimately silent.
2. `rx_dma` lifts an event out of the stream in `idle` and in `first`, that is as the first byte of a
   packet (`rx_dma.vhd:79-93`); `first` is reached only after a buffer address has been written.
3. The stage in front holds a byte until the receiver takes it: `in_ready <= out_ready or not
   out_valid_i` (`slip_decoder.vhd:45`), and `rx_dma`'s `in_ready` is high only while it collects or
   drops (`rx_dma.vhd:67`).
4. The host turns `EVENT_MENU` into 200 ms of ITU button 1, `EVENT_FREEZE` into button 2
   (`ultimate_sys.vhd:862-887`). The module control bits of `flowctrl` go nowhere in this host
   (`ultimate_sys.vhd:592-594`: `boot`, `enable`, `route` are `open`), so nothing an application writes
   there can disable the ESP32.

Gideon's applications keep that path moving in the UART interrupt: arm the receiver with a buffer
address, read the length of the finished packet, pop it, arm again (`dma_uart.cc:168-224`). Ours did none
of it. Two hand-rolled attempts at the same thing (a flush, then a polled drain) did not make the rocker
work on the board, so Alex: "Verdammt nochmal implementiere 1:1 wie Gideon." That is what
`hw_rocker_init()` now does: **his driver**. `io/uart/dma_uart.cc` and `io/uart/cmd_buffer.c` are
vendored into `host-app/software/` (GPL, listed in `host-app/PROVENANCE.md`, built into the core
application), and the bring-up is the register order of `Esp32::EnableRunMode()` (`esp32.cc:138-154`):
baud rate 115200, loopback off, flow control off, receive buffer cleared, SLIP off, interrupt on, then
SLIP on. `ModuleCtrl()` is left out on purpose - its boot/enable/route bits go nowhere in this host. The
receive callback refuses every packet, which is the case his interrupt already handles: it frees the
buffer and re-arms the receiver, so we carry none of the data and the stream still runs.

**The baud rate is the find of that step.** Nothing in our application had ever written the UART's
divisor, so it kept whatever the application before us left in it, and `SetBaudRate(115200)` is the first
thing his run mode does. The loopback self-test below is blind to it: with loopback on, both ends of the
link share the one divisor, so a wrong rate still passes. It is the one difference that could silence a
module which is sending correctly.

**Two claims that stood here before are withdrawn.** That "after the first event byte the block swallows
every later one" does not hold: with SLIP on, the event byte carries `last`, so `dropping` is left on
the same byte (`rx_dma.vhd:152-155`). And nothing had parked the stream either - see the measurement
below. The flush-only `hw_rocker_init()` of the first attempt was therefore not a fix, and it did not
behave like one on the board.

**Measured on the board (2026-10-15, JTAG, Atari and NES core).** The UART's loopback makes the whole
path testable without the module: `txd` is held high and `rxd` comes from our own transmitter
(`uart_dma.vhd:269-271`), so the ESP32 neither hears nor says anything. Posting `0x81` to ourselves and
watching button 2 - the bit nothing in this application reads - is a self-test that runs in every build:

- `[core] rocker path: ok (event 0x81 to button 2 arrived after 1 ms)` at start, and `ok` again after
  10 s of running.
- With `ROCKER_INJECT_MENU_MS` set (a hook, 0 in every build that ships), one `0x82` is posted instead:
  on the released Atari core `[core] menu open 548 ms after the request`, and on the NES, whose menu is
  already open, `[core] menu closed, machine running 1 ms after the close key`. So the main loop reacts
  to the event and the freeze menu opens and closes on it.
- With `ROCKER_COUNT_BYTES_MS` set, the receiver counts raw bytes for three seconds with SLIP off:
  `[core] ESP32 said 0 bytes in 3 s`. That is not a dead module - it is point 1 above, nobody was
  pressing.

**Answered in 5.16, and not by a press:** the link ran at the wrong speed. Everything from `rx_dma`
onwards was proven here; the one thing this section never checked is the only thing the loopback test
cannot see, because in loopback both ends share the one divisor. What is established is that an event
which reaches `rx_dma` opens the menu.

Two things found on the way that are worth keeping:
- A running machine says nothing about the application. The Atari kept running BASIC while the app was
  dead in a trap loop - the core makes the picture, not the app.
- `tools/trxdbg.sh` masks its address to 24 bits (`trxdbg.py:149`) and reaches the debug block and DDR2,
  not the CPU's I/O bus. Two "measurements" of the button register through it were worthless.

### 5.16 The rocker, found: the link ran at 115200, the module speaks 5 Mbit/s (2026-10-15)

Alex, after the second board attempt: "Es startet .. aber der Freeze button macht nichts. Claude - das
kann nicht so schwer sein. Verdammt nochmal implementiere 1:1 wie Gideon. Was ist denn los?" He was
right about the method and right that it had to be our code. The piece that was copied was the wrong
one.

`hw_rocker_init()` followed `Esp32::EnableRunMode()` (`esp32.cc:139-154`). That is the **module control**
path: it switches the module's power off and on and talks to its ROM bootloader, which listens at
115200. The path that configures the link the module's *firmware* runs on is `wifi_command_init()`
(`wifi_cmd.cc:32-44`):

```
esp32.uart->SetBaudRate(5000000);   // 6666666 at CLOCK_FREQ 66.67 MHz
esp32.uart->FlowControl(true);
esp32.uart->ClearRxBuffer();
esp32.uart->EnableSlip(true);
esp32.uart->SetReceiveCallback(wifi_rx_isr);
esp32.uart->EnableIRQ(true);
```

The module's own side agrees and is not negotiable: `u64ctrl` opens UART1 with
`.baud_rate = UART_BAUDRATE_INIT` = **5,000,000** and `.flow_ctrl = UART_HW_FLOWCTRL_INIT` = CTS/RTS,
threshold 122 (`u64ctrl/main/my_uart.c:403-408`, `my_uart.h:14-15`). Only `cmd_setbaud` (CMD_SET_BAUD
0x03) ever changes that, and nothing in our application sends it. Nor can we reset the module into a
different state: our host leaves its boot, enable and route lines `open` (`ultimate_sys.vhd:592-594`),
which is why `ModuleCtrl()` was left out - correctly, for the wrong reason. At 115200 against a 5 Mbit/s
transmitter every byte is sampled 43 times too slowly, no SLIP packet ever completes, and the event code
never reaches the first byte of a packet in `rx_dma`.

And this is why the question survived two board sessions: **the loopback self-test is structurally blind
to it.** `uart_dma.vhd:269-271` feeds our own transmitter back into our own receiver, so both ends use
the one divisor and the test passes at any rate at all. It proves `rx_dma` to the ITU bit and nothing
beyond. `[core] ESP32 said 0 bytes in 3 s` was read as "nobody pressed"; with a 43x rate error it would
have read the same with somebody pressing.

The fix is `wifi_command_init()`'s sequence, and a second test that cannot be blind:

- **The link test**: send `CMD_IDENTIFY` (0x02, a four-byte RPC header) and wait for the answer.
  `cmd_identify()` fills the buffer it received, so the reply carries our own header back followed by
  major, minor and the module's name (`rpc_calls.h:17-21, 32-37`; `rpc_dispatch.c:38-46`). One reply
  proves the divisor, the flow control, the module and both directions of the wire. It runs in every
  build, before the loopback test, and prints `[core] ESP32 link: ok (V<major>.<minor> <name>, <n> ms)`.
- **Its negative control**: `-DROCKER_WRONG_BAUD=1` opens the link at 115200 again. The identify must go
  unanswered while the loopback test still passes - which is the whole reason for having both.
- The wait is counted in milliseconds (`getMsTimer`), not in passes: a tick here is 5 ms
  (`configTICK_RATE_HZ` 200), so a loop of 300 `vTaskDelay(1)` would be a second and a half at every
  start of a core whose module is silent, not the 300 ms it reads like.
- The receive callback still refuses every packet, and now keeps the first 24 bytes of the last one, so
  the identify can be read without taking a buffer out of his pool.

`FlowControl(true)` comes with it, as his: with `cts_enable` clear our transmitter ignores the module's
RTS, which only matters once we send - and the identify sends.

Two more things settle it. His own U64 branch never calls run mode at all: it writes
`ModuleCtrl(ESP_MODE_RUN)` and then `wifi_command_init()`, with the reason beside it - "on the U64-II the
module is already running before the FPGA even loads. So we're NOT calling EnableRunMode here"
(`wifi.cc:236-245`). And `wifi_command_init()` is not conditional on WiFi being switched on: it runs from
`InitFunction wifi_init(..., 52)` at every application start (`wifi.cc:28`, `WiFi::Init` -> `esp32.cc:107`),
which is why the rocker works in his applications on a board with the radio off.

### 5.14 The menu felt slow, and media that mounts late (2026-10-15)

Alex, at the board: "Also ich kann das menu bedienen .. aber sehr laggy.. das war beim HOST menu auch mal
so, aber es ging dann da besser", and "USB1 ist erst sichtbar, wenn man z.Bsp auf Machine oder wo ander
hingeht und dan nzurück". Both already had an answer in the host application; the core application had
simply never been given it.

- **One key per pass, and a render that waits.** The menu branch took a single key per iteration and
  then slept 10 ms, so a held key moved the cursor once per pass, and the drawing happened at LVGL's
  next refresh tick on top of that. The host's loop takes *every* queued key first (a held key's repeats
  collapse into one move), then renders once and calls `lv_refr_now(NULL)` - "render now, not at the
  next refresh tick" (`hostui.cc:1712-1731`). `run.cc` now does the same, through the new
  `trx_lv_refresh_now()` in the port (it takes the port lock and does nothing when the layer is taken
  away or suspended) and `ui_render_now()`.
- **A stick that mounts after the menu opened.** `MenuCtl::load_dir()` ran on F3 and on every change of
  directory, so the first listing of the root was the one taken before USB enumeration finished: leaving
  the page and coming back was the only way to see `/Usb1`. The host application looks again every few
  seconds for exactly this reason (`hostui.cc:1760-1769`). `MenuCtl::relist()` now reads the browser's
  directory again (every 2 s, only when no key is waiting), compares an FNV-1a sum over the names so an
  unchanged directory costs no redraw, and lets the selection follow its entry by name so a new entry
  above the cursor does not move it.

### 5.15 Two things a core application must not inherit, and one thing JTAG cannot do (2026-10-15)

Found while chasing a black screen that was mine to begin with. Alex: "Ok .. du hast es kaputt gemacht,
nach wie vor. Sams Journey startet nicht!"

- **The machine's stop register is in the host.** `C64_CARTREGS_BASE + 1` (`hw.cc`) keeps what the
  application before us wrote, so a core application that is replaced with its Freeze menu open leaves
  the machine stopped, and the next one releases its core into a stop nobody can see. `coreapp_run()`
  now clears it once at the start, which is the state the host's power-up has.
- **A core shifted in over JTAG is not a core the host handed over.** The black screen was not the
  application: `tools/t79/partial_load.py` puts the partial into the FPGA (CFG_IN, `DONE` true) but the
  host application's own path does more around it (hold, hand-over page, release), and the picture stayed
  black until Alex booted the same bundle from the boot menu, where it started at once. A JTAG partial is
  for looking at gateware, not for testing a bundle. `Video::poll()` shows how the black ends up on
  screen: it flips the layer only when the core's frame counter moves (`video.cc:55-68`), so a core that
  renders nothing leaves the layer on the buffer `Video::init()` blacked out - machine running, HDMI
  locked, picture black.
- **`COREAPP_AUTORUN_PATH`** (`run.cc`, empty in every build that ships) runs one file through the
  descriptor's first action - the same call the browser's "Run" makes - and then prints the core's frame
  counter for three seconds. It answers "is this core producing frames at all" without a keyboard and
  without anyone at the board.

Also found, and not ours: `make -j` can break the build. `all: $(OUTPUT) gitinfo $(RESULT) $(FINAL)`
(`host-app/target/common/rules.mk:23`) lets `gitinfo` rewrite `gitinfo.h` with `>` while the same make
compiles, and a source that includes it mid-rewrite fails with `'APP_BUILD_DATE' was not declared`.
`tools/build_coreapp.sh` is therefore run with `JOBS=1` until the rule writes the file atomically; the
file is vendored (`tools/hostapp_vendor.py`), so the fix belongs in its patch set, not in a direct edit.

### 5.17 Spectrum and CPC (2026-10-22)

The descriptors 5.3 left out now exist: `coreapp/cores/zx.cc` and `cpc.cc` (T91 and T92 section 5), with the shared tape converter and deck, the ROM-set mask `RomSpec.set` and `CoreIo::palette()`.

<!--
Closing: Status -> DONE (date) + which scenarios are green where; TODO row and history entry,
in the same commit as the code.
-->
