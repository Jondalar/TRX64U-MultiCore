# T192 — Epic: one functional core, many user interfaces (host app and generic core app)

**Status:** PROPOSED (2026-11-07), asked by Alex. Epic: spec and architecture only; no build while the finish plan
(`docs/todo/finish-plan-2026-11-07.md`) is open, unless Alex says so. MVP build released by D20 (2026-11-08).
**Repo:** TRX64U-MultiCore — host app (`host-app/software/hostapp/`), generic core app (`coreapp/`), view layer (`app/view/`),
REST/WS (`coreapp/src/rest.cc`, `wsproto.cc`).
**Number:** T192.
**Architecture document:** `docs/specs/T192-arc42.md` (arc42 sections).
**Related:** T104.1 (contribution model, one service layer), T104.3 (applications, ABI 3, .u2a loader), T106 (host web), T153 (UI SDK
and contract), T187 (core menus layout), T181 (hotkeys).
**Milestone:** M3.

---

## §1 The problem

Alex, 2026-11-07 (translated), six questions on the host app and the generic core app:
1. Is it MVC by architecture?
2. Could someone else design a second UI and build it against the functional layer?
3. Could such different UIs, keys and shortcuts be stored as bundles and made selectable?
4. A general function "C= SPACE" like Spotlight or Alfred: type, then pick a submenu or a function directly.
5. What would a real TUI instead of the graphical UI cost?
6. Must it all be Gideon's C++ classes and framebuffer-less LVGL rendering, or would another architecture free us (headless
   backend, LVGL UI in front, REST/RPC via an API, TUI locally and through a terminal client)?

First reading (coordinator, 2026-11-07, from the code), corrected by the analyses of the same day (evidence: file:line below):
- Core app: functional layer = descriptors (`coreapp/src/desc.h`) and named ops (`ops_glue`, the same names REST uses: `rest.cc`
  `PUT /v1/machine:reset` -> `machine.warm_reset`; typed `ArgSpec`, `ops.h:30`); controller `MenuCtl` (`menuctl.cc`, 3918 lines, 113
  methods, no `lv_` call) hands rows out as text (`row(i, left, right, sub)`); view `ui.cc` / `screens.cc` (LVGL, call no op:
  grep finds only `g_desc->short_title`). Closest pattern: MVVM, `MenuCtl` = view-model with presentation hints in the model
  (`Item::tone/chip/bkey/primary`). The view layer is clean; the controller leaks toward the functional layer (pad wizard
  `menuctl.cc:3800-3918`, monitor `3037-3043`, timewarp `3775-3779`, write-back `1391-1405`, a file manager with `FsEntry list_[512]`,
  `key()` 271 lines of raw `KEY_*` codes `2988-3258`).
- Host app: NOT "no functional layer". 91 files / 19331 lines, only `hostui.cc` (7628 lines, 39 %), `coreboot.cc` (2 hits) and `legacy.cc`
  (2 hits) contain `lv_`; about 90 files are UI-free modules with callbacks and bench seams (`core_stage/swap/jump`, `fw_write`, `txu_*`,
  `flashprot_*`, `fo_*`, `fmt_*`, `bootcfg_*`, `wifi_net_*`, `hv_*`, `directboot_*`, `web_parse`). The mixing sits in `hostui.cc`: each
  "flow" is one function that probes, builds text and blocks in `while (host_getch() < 0) vTaskDelay(20)` (~25 waits), and `on_key`
  (`hostui.cc:6162`, 225 lines) holds every key and every business action. No state/command/event layer between the modules and the view.

- **P1 Separation:** core app: split the leaks out of `MenuCtl` (ten places, `coreapp.md` §2). Host app: a `host_core` service
  (state, commands, one event stream, flows as job state machines) between the UI-free modules and `hostui.cc`; ~3-3.5k of the 7.6k
  lines are logic that would move. The start sequence exists twice (`start_core`/`start_full` `3657/3722`, `direct_run` `6486`).
- **P2 Contract:** no written, versioned interface (rows, ops, state, events, intents). Core app: `/v1/ui/state` is a pull snapshot
  `"v":1` without schema or events; no input route (`rpccontract.cc:26-28,128` refuse `key_down/up`); items carry no stable id
  (`sel_` is private); T153 D7.1 lint not built; T153 Q1: the widget layer is internal, so "a second UI" is not an official concept.
  Host app: JSON without version, 6 routes, no jobs/settings/browser (`webui.cc:231-310`).
- **P3 Bundles:** data today: tabs, nodes, node hotkeys, route (record TRXD v3, `descrec.cc`), 7 colour schemes (`trxview.cc:57`).
  Code today: menu key map (`MenuCtl::key`), layout templates (`screens.cc:544-916`), the host's whole layout and key map (`on_key`,
  footers as ~20 literals, `hostui.cc:1114-1146`); `ui.cc:84` always sets the PANEL skin. A bundle table needs record v4;
  a renderer inside a `.u2a` contradicts T104.3 D0.
- **P4 Search:** index parts exist (`MenuTree` + `menu_dump`, `Setting.key/label/group`, `OpRegistry`, `goto_spec`), the flat index,
  ranking, and activation by id through the confirm/dirty rules do not; letters are reserved for context functions (#34).
- **P5 Text UI:** `terminal` layout (80x40, `screens.cc:770-775`) and `/v1/ui/state` exist; estimate ~1000-1500 lines for a cell-grid
  view of the core app (estimate, not measured); timeline, memory maps, medium panel and the live box are graphic. Host app: needs P1 first.
- **P6 Remote:** core app: 100 % of the ops over REST/WS, the menu is a read-only snapshot, no key/intent route, no job events.
  Host app: its own small API (T106), no WebSocket, no auth, no events. REST bypasses `ui.confirm` and locks.

### §1.1 Requirements harvest

Sources: `GitHub` = issues of this repository (#n) and discussions (D33, D56, D15, D44, D13), 43 issues and 15 discussions read for UI
relevance; `H` = host app code, `C` = core app code. Requesters by role only.

| # | Requirement | Source |
|---|---|---|
| R1 | Remote control of the menu: open, send a key, read the full UI state, know when a key was taken (`seq`); for scripts and e2e tests | D56; `/v1/ui/state` |
| R2 | Hotkey that opens the core menu while the core runs, configurable per core, usable without a USB keyboard | D33, #10 |
| R3 | In-core hotkeys while the core runs (volume +/-), not only in the menu | #10, D33; `hotkeys.h:1-4` |
| R4 | Page up/down on familiar keys (+/- exists; C=+F3/F5, optionally C=+F1/F7), concurrent or switchable | #34, D33 |
| R5 | Letters are reserved for context functions; a type-to-find mode needs its own entry gesture | #34, D33 |
| R6 | Global find/launch ("C= SPACE"): type, pick a function, setting or submenu directly | D33 |
| R7 | Text UI as a first-class option, locally | D33 |
| R8 | Text entries like an editor: cursor, home/end, insert, long names scrolling | #42, #38 |
| R9 | File browser: per-core start folder, remember last folder, go-home key, middle-cut names with extension, file ops in every core, zip | #40, #38, #20, #43 |
| R10 | Menu fits any display: safe area for old TVs, readable at 720p/1080p, follows host mode; canvas stays fixed | #24, #32, #50 |
| R11 | Picture/mode choice as a list before applying, confirm-or-revert | #50, #16 |
| R12 | Speed: fast boot menu, direct boot with C= to interrupt, saved menu picture in core apps, responsive navigation, retry on slow USB | #2, #4, #14, D15 |
| R13 | Per-core configurations as launchable entries in the boot menu | #58, #40 |
| R14 | Per-core settings in a uniform place (volume, tone, port swap, mouse speed, scanlines) | #9, #10, #49, #51, #31 |
| R15 | Activity indicator for long loads | #12 |
| R16 | Optional UI/UX framework for host and core apps; cores stay buildable by others | D13, D33 |
| R17 | Menu reachable even after a wrong install | #25 |
| R18 | Any key stops the boot countdown; direct boot stops under fixed reasons | H `hostui.cc:6164`, `directboot.h:43` |
| R19 | Picture change: 8 s confirm, any key keeps (first key of any view is consumed), `V` steps modes from any view; network change gets the same question | H `hostvideo.h:23`, `hostui.cc:6165-6190`, `6030`; C `picture_confirm_pending` `run.cc:839-847` |
| R20 | Armed delete (DEL, DEL), replace question on copy/move | H `hostui.cc:6168-6169`, `6264+` |
| R21 | Core/full-firmware start: stepped progress, cancel until the last check, distinct "loaded not started", confirm only for foreign firmware, flash protect read back before handover | H `3640-3716`, `3790-3796` |
| R22 | Update/restore/convert/format: refuse pages, full check before any write, ask page, phased run page, result page; no network route while format runs | H `5508-5660`, `3797-3800` |
| R23 | Refuse texts come from the producing module, not from the UI | H `refuse`, `cor_result_str` |
| R24 | Confirmations (`ui.confirm`: RETURN yes, Left no), file-delete confirm, write-back question (asked again at next open) must gate every route, remote included | C spec D2, `menuctl.cc:1391-1405` |
| R25 | Live vs at-reset settings marked in the row; changes held until Save when Auto Save Config is Off/Ask | C `desc.h:55,92`, `menuctl.cc:731`, `settings.cc:316` |
| R26 | Write-back of cartridge/battery RAM: question and block, flush on reset, eject, core change, power-off, menu open | C `writeback.h:3-5,17-22` |
| R27 | Locks: app tab `lock` keeps RUN/STOP from closing; monitor owns every key; app dialog and picture confirm take keys first | C `menuctl.cc:2303`, `run.cc:839-847` |
| R28 | Hot-plug: browser relist every 2 s in the core menu, media poll every 3 s in the host | C `run.cc:902-904`; H `hostui.cc:6835-6945` |
| R29 | Stepped jobs with progress, cancel, `busy()`/`progress()`; app jobs 4 slots | C `menuctl.h`, T104.3 |
| R30 | Single writer: UI state only touched from the UI task; others go through a queue/mailbox | C `uistate.h:3-5,20-24`; H `webui.h` |
| R31 | Canvas 960x540 or the mode's own size; layout rebuilt after a mode change | H `trxview.h`; C `ui_set_canvas` |
| R32 | Menu picture saved when the menu is settled, shown at boot, not on a compact canvas | H `6676`, T146 |
| R33 | Secrets scrubbed from the bug report; same facts from menu and REST | H `bugreport.h:37-39` |

Tensions:
- **C1** letter jump (#34, refused) vs type-to-find (D33): both want letters; needs a distinct entry gesture.
- **C2** in-core hotkeys (#10, D33) vs two separate input paths (menu / host keyboard).
- **C3** remote key injection (D56) vs unauthenticated REST (host: FTP without password, `hostui.cc:1791`; no auth on either API).
- **C4** C64U muscle memory (C=+F3/F5) vs rethinking the menu around the C64 keyboard: the case for selectable key bundles.
- **C5** larger/native menu (#32) vs cost: framebuffer-less RISC-V UI, LUT and DDR2 shared with the core.
- **C6** polish requests (#31, #49) vs the LUT budget of full cores; the same applies to anything a new UI adds in the core partition.
- **C7** optional UI framework (D33) vs requests that assume one UI and one key scheme (#24, #10); a second UI must not break core apps that bypass the framework.
- **C8** speed came from pre-rendered pictures and caches (#4, D15); a second UI or TUI must keep that path.

## §2 Scenarios

Proof kinds: **bench** = render bench with the board's own `menu_build`, host bench `hostui_test_*`, unit test run by `ci/apptests.sh`;
**emu** = ue2emu S44 (`--control` / `--script`, `png <path>`; phase A = core app as any core; flow = host -> .cor -> back);
**board** = a step on the C64U. Every row has a negative control that can fail. Child = the spec that builds it (§3 Breakdown).

| # | Solves | Scenario | Test (proof; negative control) | Child |
|---|---|---|---|---|
| S1 | D12, P1 | Old `hostui.cc` and the new declarative host menu show the same screen for every screen (home, boot menu, browser, status, settings, help, footers, toasts) with the same keys and texts | emu: scripted key walk on both builds, `png` per step, text/navigation/action/state diff = 0 (pixel diff only for screens listed as layout-unchanged, D19b); control: change one footer literal in the new menu, the diff must go red | T192.2 |
| S2 | D12, R21 | Core start (DFX): stepped progress, cancel until the last check, same end state as `start_core` | emu flow host -> .cor -> back, both builds; control: a key at the last check must not cancel | T192.2 |
| S3 | D12, D13, R21 | Full-firmware start: confirm for foreign firmware only, flash protect read back before handover, `lock_flash()` | emu + bench on `flashlock*`; control: unreadable protect state refuses the handover | T192.2 |
| S4 | D12, R22 | Update `.txu`: refuse page, full check before any write, ask page, phased run, result page | bench `hostui_test_*` + emu; control: corrupt `.txu` is refused with the module's text, no write | T192.2 |
| S5 | D12, R22 | Restore flow: same pages and result as `restore_flow` | emu + bench; control: wrong image refused before any write | T192.2 |
| S6 | D12, R22 | Convert flow: same pages and result as `convert_flow` | emu + bench; control: unsupported input refused | T192.2 |
| S7 | D12, R22 | Format flow: same pages; no network route while it runs | emu + bench; control: a web command during format is refused | T192.2 |
| S8 | D12, R20 | File ops: armed delete (DEL, DEL), replace question on copy/move | emu; control: a single DEL deletes nothing | T192.2 |
| S9 | D12, R13, R18 | Boot menu: countdown, any key stops it, per-core entries | emu; control: no key, the countdown ends in the start | T192.2 |
| S10 | D12 | WiFi page: scan, join, error text from `wifi_net_*` | bench + board (needs a radio); control: wrong key shows the module's refuse text | T192.2 |
| S11 | D12, R19 | Picture change: 8 s confirm, first key of any view consumed, unanswered reverts | emu with `V` and no key; control: a key keeps the mode, no key reverts | T192.2 |
| S12 | D12, R18 | Direct boot runs before the UI and uses the same start sequence as the menu; any stop reason aborts it | emu; control: stop reason set, no start | T192.2 |
| S13 | D12, D13, P1 | Menu start and direct boot run one `start_sequence`; a second start while one runs is refused; the lock rule is unchanged (lock for a full image, none for DFX) | bench (host_core, no LVGL) + emu; control: second start accepted = red | T192.2 |
| S14 | D4 | First WebUI login works without a token and hands it out | unit test on the auth module + emu/board; control: a second fresh login must not get it | T192.1 |
| S15 | D4 | Afterwards a REST/WS request without the token is refused, with it accepted | unit test + emu `--control` request; control: wrong token refused | T192.1 |
| S16 | D4 | The token as URL parameter is accepted (header optional) | unit test; control: parameter with one changed digit refused | T192.1 |
| S17 | D4 | The token is shown in the menu and kept as a plain file on the board | render bench (page text) + board file read; control: delete the file, a new token is made and the old one stops working | T192.1 |
| S18 | D11, R24 | A remote client with the token gets the same confirmation question as the screen and may answer it, destructive ones and an update included | emu: remote start of an update asks, yes proceeds; control: no answer, nothing runs; the question is never skipped | T192.1 |
| S19 | D14, P2 | Both apps serve the same versioned state/intent/event contract with stable item ids; a client fails cleanly on an unknown version | unit test against host and core app; control: bump the version, client refuses | T192.1 |
| S20 | D14, P1 | Core app: pad wizard, write-back, file manager run behind ops; `MenuCtl` has no `pad_*`/`wb_*` call; an item activated by id gives the key's confirm question | grep gate + apptests + emu phase A; control: re-add one call, gate red | T192.3 |
| S21 | D3 | The search opens only by the chord C= SPACE; letters stay context functions everywhere | emu phase A key script + bench; control: a letter on a list acts as before, never opens the search | T192.4 |
| S22 | D3 | While the overlay is open it owns every key; nothing reaches the page underneath (F-keys, RUN/STOP, RETURN) | emu with `png` before/after, selection unchanged; control: close the overlay, the same key now acts | T192.4 |
| S23 | D8, P4 | Search finds a setting, a page and an action in host and core app; a file name finds nothing | bench on the index; control: a file with a unique name on the stick must not appear | T192.4 |
| S24 | D3, R24 | Activating an action from the search runs the same confirm/dirty rules as the menu key | emu; control: a destructive action without the question = red | T192.4 |
| S25 | D6, P5 | The text UI runs locally on the HDMI picture as a character grid and takes the board keyboard | emu `png` + board; control: a key the grid does not know does nothing | T192.5 |
| S26 | D6, D4 | The same screen and keys arrive over `ws://` (token per D4) in a terminal on another machine | emu `--control` ws client, text compare with the local grid; control: no token, connection refused | T192.5 |
| S27 | D6 | Graphic-only screens (timeline, memory map, live box) show a text fallback | bench; control: a screen without fallback fails | T192.5 |
| S28 | D2, D9 | Default key set is TRX64; the prepared "C64U classic" is selectable and the same key does a different thing | emu key script per set; control: same key, two results, not equal | T192.6 |
| S29 | D9 | A bundle is a TOML edited on a computer, copied to the board, active after a restart; no build, no tool | board (edit, copy over FTP/USB, restart) + emu with the file on the stick; control: unedited file leaves the old behaviour | T192.6 |
| S30 | D9, D7 | A broken TOML falls back to the built-in bundle and says so | unit test on the parser + emu; control: a valid file is not rejected | T192.6 |
| S31 | D7 | C=+S held at host start = safe mode: built-in TRX key set, scheme and text mode; no clash with RESTORE+F1 recovery | emu with `hold` at start; control: without the chord the chosen bundle is used; RESTORE+F1 still boots recovery | T192.6 |
| S32 | D5 | The host holds the default, a core may override it for itself; a core without override uses the default | emu flow (host -> core -> back) with two cores; control: override removed, the default returns | T192.6 |

D10 (no density choice) needs no scenario.

## §3 Decisions

- **D1 (Alex, 2026-11-07, translated: "C++ is the core then, kick Rust out of the design"):** the functional layer stays C++ on the
  board's RISC-V application, on Gideon's drivers and FreeRTOS; no Rust backend, no rewrite. Freedom comes from the boundary (headless
  functional layer, views as clients), not from the language.
- **D2 (Alex, 2026-11-07, translated: "Mine, TRX."; question: default key set C64U classic or TRX64):** the default key set is the
  TRX64 grammar (F1-F8 pick the area, numbers pick the tab); "C64U classic" may exist as a selectable set, not as the default.
- **D3 (Alex, 2026-11-07, translated: "A yes, and when the search is in the overlay over the screen, the keys go into the search -
  NEVER into the UI below"):** only the chord C= SPACE opens the search; letters stay context functions everywhere, no type-to-find.
  While the search overlay is open it owns every key; nothing reaches the page underneath.
- **D4 (Alex, 2026-11-07, translated: "C: build a simple auth with a token, simple tokens shown in the UI and stored in the file
  system, one can fetch it and then work with it. WebUI at the first login WITHOUT, then with. Bearer away or however, so that I just
  append it to the URL at the top. No encryption"; options were A auth first, B local network without auth):** the remote key/intent
  route and REST/WS get a simple token: one token per app, shown in the menu and kept as a plain file; the first WebUI login works
  without it and hands it out, afterwards every request needs it; it is accepted as a URL parameter (a header may also work); plain HTTP,
  no encryption.
- **D5 (Alex, 2026-11-07: "C"; options A host F8 for all cores, B per core, C host default plus per-core override):** the interface
  choice (key set, colour scheme, density, text mode) is set in the host as the default; each core may override it for itself.
- **D6 (Alex, 2026-11-07, translated: "A, that is then simply WS://"; options A both, B local first, C terminal first):** the text UI
  comes in the first build both locally (the menu as a character grid on the HDMI picture, board keyboard) and remotely in a terminal on
  another machine; the remote path is a WebSocket (`ws://`, token per D4) carrying the same text screen and the keys, no telnet, no SSH.
- **D7 (Alex, 2026-11-07, translated: "D: with T+R+X the host starts with the TRX key set .. or we think of something like C=+S, i.e.
  safe mode"; options were reset chord at menu open and/or 10 s keep-or-revert):** the rescue from a broken bundle is a key chord held
  while the host starts: the host then starts with the built-in TRX interface (safe mode). Chord: C=+S (Alex, 2026-11-07: "make it C=+S");
  it resets key set, colour scheme, density and text mode, and does not clash with T177's RESTORE+F1 recovery boot.
- **D8 (Alex, 2026-11-07: "C"; options A current folder, B all media with an index, C no files):** the search indexes settings, pages
  and actions only; no files.
- **D9 (Alex, 2026-11-07, translated: "B, where we prepare B and then the testers in the tester discussion may work it out themselves
  ... it has to be testable: edit your TOML in an editor, copy it to the board, restart, see the result"; options A TRX64 only, B plus
  C64U classic, C plus a C64 layout):** two key bundles ship: TRX64 (default, D2) and a prepared "C64U classic" that the testers finish
  in the tester discussion. A bundle is a plain TOML file: edited on a computer, copied to the board, active after a restart; no build,
  no tool. A broken file must not lock anyone out (D7 safe mode, fallback to the built-in bundle).
- **D10 (Alex, 2026-11-07: "A"; options A one fixed size as today, B an extra "Large"):** no density choice; the graphical menu keeps
  one fixed size. The interface choice of D5/D7 is therefore key set, colour scheme and text mode.
- **D11 (Alex, 2026-11-07, translated: "A, updates too!"; options A a token may answer everything, B destructive confirms only by a
  board key, C B with a setting to allow):** a remote client with the token may answer every confirmation, destructive ones and firmware
  updates included. The confirmations themselves stay (the remote client is asked the same question the screen asks).
- **D12 (Alex, 2026-11-07, translated: "C: host_core, and with the emulator rebuild the host menu 1:1 until it behaves exactly the
  same, but declaratively, and running as a client of the headless core"; options A refactor flow by flow, B build host_core beside and
  replace at the end):** a headless `host_core` (state, commands, events, flows as jobs) is built; the host menu is rebuilt as a
  declarative client of it, and in ue2emu (S44) the new menu is compared with the old one until it behaves exactly the same (same
  screens, keys, texts, flows); then it replaces `hostui.cc`. Parity is proven in the emulator, screen by screen and flow by flow.
- **D13 (Alex, 2026-11-07: "A"; options A keep, B lock on a DFX start too):** the T152 flash lock stays as built: `lock_flash()` for a
  full image (.cfw, large .cor, direct start of one), not for a DFX core, which runs under the host; the common start sequence keeps
  this rule.
- **D14 (Alex, 2026-11-07: "A"; options A one shared core for host app and core app with the domain logic per app, B host_core for the host only and a separate contract for the core app):** T192.1 is ONE shared core framework for both apps: state snapshot, intents with stable ids, event stream, jobs (progress, confirmations), token and WebSocket, written once; the domain logic stays per app (host: core start, flash, update, files; core app: ops, settings, media). The text UI, the search, the terminal client and the bundles are built once on it and serve both apps.

### Breakdown (child specs, dependency order)

1. **T192.1** shared core framework: state snapshot, intents + stable ids, event stream, jobs/confirmations, token, WebSocket, schema/version (D14, D4, D11). Depends on nothing. S14-S19.
2. **T192.2** host app on it: `host_core` domain (start sequence, flash, update, files) + declarative host menu, 1:1 parity in ue2emu (D12, D13). Depends on T192.1. S1-S13.
3. **T192.3** core app on it: `MenuCtl` leaks out (pad, monitor, timewarp, write-back, file manager), intents/ids on the framework (D14). Depends on T192.1. S20.
4. **T192.4** search overlay for both apps (D3, D8). Depends on T192.1; hosts in T192.2/.3. S21-S24.
5. **T192.5** text UI local + `ws://` for both apps (D6). Depends on T192.1; per app on T192.2/.3. S25-S27.
6. **T192.6** key/interface bundles + safe mode (D2, D5, D7, D9). Depends on T192.1 and the key map as table "key -> intent" (T192.2/.3). S28-S32.

T192.2 and T192.3 may run in parallel after T192.1; T192.4-.6 after both (or per app once its adapter exists). Each child gets its own spec file at build time; rows are in `TODO.md`.
- **D15 (Alex, 2026-11-07, translated: "192.2 first candidate A1200, because there is really a lot in it"; confirmed: meant the core
  app on the shared core):** the first core moved fully onto the shared core (T192.3) and proven in ue2emu is the Amiga 1200.
- **D16 (Alex, 2026-11-07, translated: "the local shell app would not matter, would it? ... we could build the macOS/Linux client
  separately as a project beside it"):** the terminal client for macOS/Linux is a separate project beside this repository; it binds to
  the board only through the T192.1 contract (ws://, token). Its language is open (D1 covers the board's core, not this client);
  candidate: the ratatui shell of `trx64-cli` (`TRX64/crates/trx64-cli/src/tui.rs`) with its in-process engine replaced by a WS client.
- **D17 (Alex, 2026-11-07: "yes"):** T192.5 starts from the TRX64-Ultimate monitor (ADR 0060, spec T21): its cell model `ui_screen_t`
  (up to 80x50 characters, one attribute per row; 40x23 narrow, 80x40 wide, `apps/trxmon/src/ui.h:12-54`), `ui_render()`, the command
  line with editing and history (`ui.c`). Its output there is Gideon's hardware character overlay (`app_api_impl.cc:462-485`), which the
  MultiCore host does not have: here the grid is drawn by a glyph blitter into the pixel layer and sent over ws:// (D6). Blitter (Alex,
  2026-11-07, translated: "LVGL is really slow .. but yes, let us start like that"): software on the RISC-V, writing glyphs straight into
  the pixel layer without LVGL (dirty cells only), no gateware (the host's gateware is immutable). Its draw time is measured against
  LVGL's for the same page; if it is still too slow, gateware is a new question.
- **D18 (Alex, 2026-11-07, translated: "A for now"; options A destructive ops ask over REST too, B REST with the token runs them
  directly):** a destructive op called over REST (`POST /v1/ops/...`) asks the same confirmation as the menu; the caller answers it
  (allowed by D11). No REST path bypasses a confirmation.
- **D19 (Alex, 2026-11-08, review of T192.1-.6, translated: "Good direction ... otherwise: build. No further architecture round"):** three
  corrections, the rest is built as specified.
  (a) REST may not bypass confirmations: every destructive action, including the existing `/v1/ops/*` routes, goes through the same
  confirmation gate as the UI intents (D18 made binding for the child specs); one safety model, not two.
  (b) Parity is not pixel-identical in general: the pass is the same texts, navigation, actions and states. A pixel compare runs only for
  screens whose layout is meant to stay unchanged; a harmless rendering difference does not block the rebuild.
  (c) The text UI reuses only the cell grid and the blitter of the monitor; the `ui.c` command line with its history is not taken over.
  Text entry stays with the shared input/intent system.
  (d) (Alex, 2026-11-08, translated: "the last 20-50 entries could go into a volatile cache that is gone after the session"; session =
  "reboot/core change, exactly"): the shared input system keeps an entry history in RAM only, 20-50 entries (build choice), never
  written to flash or SD; it is cleared by a reboot or a core change, not by closing the menu or a remote client disconnecting.
- **D20 (Alex, 2026-11-08, translated: "Make an MVP. So service + 1 UI + API"; "the UI == TRX user interface"; "host app, Amiga 1200
  app, review"; "afterwards it is meant to be 1 app for everything, with config etc."):** the build starts with an MVP, not with the
  children in order. MVP = the service layer of T192.1 (state with stable ids, intents, events, jobs/questions, token), the graphical
  TRX interface (LVGL) driven only through intents, and the API (REST + WebSocket on that service), in the host app and in the core
  app on the Amiga 1200 core. The MVP code is written as one app whose role (host or core) and features come from configuration, because
  the goal after the MVP is a single app for host and cores; the MVP does not have to merge the two builds yet. Search (T192.4), text UI
  (T192.5), bundles (T192.6) and full parity (S1; T192.3's parity pictures) come after the MVP. Open §6 questions of the children take the
  recorded recommendation (D19: "no further architecture round"). Way of working (Alex, same day): branches `t192` and `integ-1` from
  `main`; agents build ("give it gas"), tests only in the emulator (ue2emu) and benches, no board; the `ux-review` skill and the review
  agent run on every result; finished features are merged into `integ-1`, test builds come from there.
- **D21 (Alex, 2026-11-08, translated: "Important - build out the full API from the start. Because then you can test and iterate over the
  API right after we have an app"):** the MVP's API is not cut to the MVP's UI scope. From the first build, the service exposes every page,
  every item (stable id), every setting, every action/op, every job and every question of the app it runs in (core app and host app), as
  snapshot, intents and events over REST and WebSocket. The UI part of the MVP may stay small (D20); the API is complete, so every later
  step (T192.2/.3 parity, the other cores, search, text UI) is tested and iterated over the API.
  (Alex, same day, translated: "ideally the declaration generates the functions/endpoints"): exposure is derived, not hand-written. What
  an app can do is declared once (menu tree, descriptor settings, ops table, jobs, questions); ids, snapshot items, intents and their
  routing are generated from that declaration; the REST/WS routes stay generic (state, intent by id, events), with no handler per
  function. A new declared row is reachable over the API without further code; the coverage test only fails on behaviour that bypasses
  the declaration.
  (Alex, same day, translated: "Important - the host's UI should keep looking the same as far as possible"): moving the host app onto
  the service changes nothing on screen; before/after pictures of the touched host pages in ue2emu are compared, a visible difference
  is a finding; a change with a good reason is allowed and recorded with its reason (Alex: "if there is a good reason, we allow it").
  The `ux-review` skill / ux-specialist reviews the before/after pictures of every touched page (Alex: "the UX agent/skill should
  supervise too").
  A question raised over the API looks the same on screen as a local one, no "asked remotely" mark (Alex, same day, translated: "that
  does not matter; there are not N people in front of it but exactly 1 person and at most their LLM friend").
- **D22 (Alex, 2026-11-09, translated: "Only trx64U will get this UI. Gideon and Commodore we do not touch"):** of the C64 cores only
  `trx64u` gets the T192 UI, service and API. `trx64g` (Gideon's Ultimate application), Commodore's firmware and the other C64 cores
  (`c64ref`, `c64scpu`) get none of it; generic T192 rules exempt them, they are never changed to fit.
  Refined (Alex, 2026-11-09, translated: "C64 cores that come from Gideon, Commodore or are based on their bitstream - THOSE we do
  not touch"): the exemption covers only C64 cores of Gideon's or Commodore's making or built on their bitstream (Gideon's firmware,
  `trx64g`'s Ultimate application, `c64ref` = the slim C64U copy). Our own C64 cores, `trx64u` and `c64scpu` (derived from our C128
  core, Kawari VIC), follow the generic T192 rules like any other core (e.g. T192.3 D27, the Library left arrow).
- **D23 (Alex, 2026-11-09, translated: "Until the UI, including the UX, works completely as an API client, it is a botch"):** the next
  T192 stage is the parity cut (T192.2 P1 host_core, T192.3 the ten leaks out of `MenuCtl`, key map as a table): the local UI becomes a
  client of the service with no path of its own. The MVP's key replay into the old UI is a stopgap, not a state to ship. API additions
  (remote input as a virtual device, `machine:menu_button`, the picture question, snapshot gaps) are built on the cut, not before it.
  Scope (Alex, same day, translated: "No, in T192 for now only host + A1200, please"): the cut and the service cover the host app and the
  core app on the Amiga 1200 only (H3 stays). `MenuCtl` is shared by every core, so the cut must leave every other core's menu as it is
  (same pictures, same keys), with the service off there.
  Widened (Alex, 2026-11-09, translated: "the tests into the A1200 menu please, that one goes to the testers with it. If they are *also* in trx64u, that does
  no harm"): the service stays on for the Amiga 1200 and the TRX64U (T192.3 section 5); the 49 scenarios of T153 run on both.

## §4 Out

- A Rust backend (D1).
- Telnet and SSH for the text UI (D6): the remote path is `ws://` only.
- Encryption of the remote routes (D4): plain HTTP/WS with a token.
- Files in the search (D8).
- A density choice (D10); an extra "Large" menu size.
- A renderer or code inside a bundle or `.u2a` (D9, T104.3 D0): bundles are data.
- A separate contract per app (D14 option B), a host-only `host_core`.
- A build before the finish plan is closed (header), unless Alex says so (released for the MVP by D20).
- Deferred past the MVP by D20: S1 parity (T192.2), T192.3's parity pictures, T192.4 search, T192.5 text UI, T192.6 bundles.

## §5 Build

Nothing built.

## §6 Open questions

All questions of the first round are answered (2026-11-07):
- Q1 T152 lock -> D13. Q2 bundles in a `.u2a` -> D9: bundles are plain TOML data, T104.3 D0 unchanged. Q3 remote before auth -> D4,
  D11. Q4 a second UI as an official concept -> yes: D6 (text UI) and D12 (declarative host menu as a client of host_core). Q5 search
  gesture -> D3. Q6 host app -> D12. Q7 bundles -> D9. UX questions: key set D2, choice place D5, rescue D7, search scope D8,
  density D10, remote confirms D11.
Second round: shared core for both apps -> D14. No open question.
Next: §2 scenarios are written (S1-S32); MVP build per D20 on `t192`, merged into `integ-1` (an `integ-*` branch: sims run there, as intended).
D23 progress (2026-11-09): the core app's half of the parity cut is built on `t192-cut-core` (T192.3 §5: the ten leaks out of `MenuCtl`, the key table, the gate); the host app's half is `t192-cut-host`.
