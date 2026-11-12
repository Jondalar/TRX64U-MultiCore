# T169 — The user manual describes the host application as it is today, menu by menu, with emulator screenshots

**Status:** DECIDED (2026-10-31, Alex).
**Repo:** TRX64U-MultiCore — `docs/manual/`, `docs/img/`.
**Number:** T169 (TODO table).
**Origin:** Alex, 2026-10-31: "look at the manual in TRX64-Ultimate, we need something like it for the host app too. NOT for
the cores please." Asked whether to rebuild `docs/manual/` or add a second manual beside it: "No, rebuild ... The rules: Boeing
engineering manual style ... so clear, precise function descriptions, no blah, screenshots where they fit. OK? Use the emulator
for the screenshots." (translated)
**Depends on:** T121 (style and the first manual), the host application of `main`.
**Milestone:** M3.

---

## §1 The problem

- P1 `docs/manual/04-host-menu-and-boot-loader.md` describes the host of 2026-10-23: F3 is "Add core". Today F3 is the file
  browser (with U update `.txu` T152, R restore T137, P package), F8 is Settings (T163 `host.cfg`, T152 Protect Flash, T164 NTP,
  T159 LED), and there is a Colors view, a web page and a bug report (T162). None of these is in the manual.
- P2 TRX64-Ultimate's `docs/manual/` covers its firmware one menu per chapter: every page, every item, keys, values, defaults,
  with emulator screenshots. Ours has placeholders ("to be added") instead of pictures.
- P3 Chapters 2, 3 and 8 (install, first start, way back) predate the updater of T152 and the restore of T137.
- P4 Chapter 5 (cores) is out of scope (Alex: not for the cores).

## §2 Scenarios

```gherkin
Scenario: S1 every view of the host application has its chapter
  Then docs/manual has one chapter per view of hostui.cc (boot menu, file browser, edit, network, settings, colors) plus
       update/restore, web page and REST/FTP, bug report
  And  every key, item, value and default in a table is read from the code (file:line in the agent's report, not in the manual)
  And  a control: a key the code does not handle, written into a table, is caught by the review against hostui.cc

Scenario: S2 pictures come from the emulator
  Then each chapter's screens are PNGs from tools/ui_emu.sh --headless runs of the host app of main, under docs/img/host-*
  And  the coordinator opens every PNG before the merge; no "to be added" placeholder stays for a screen the emulator can show

Scenario: S3 install, first start and way back match the updater and restore of today
  Then chapters 2, 3 and 8 describe the .txu update (T152) and the vendor restore (T137) as the code runs them
```

## §3 Decision

- Rebuild `docs/manual/` in place (Alex). Style of T121 §3: Boeing engineering manual, numbered paragraphs, WARNING / CAUTION /
  NOTE, short imperative steps, Simplified Technical English, clear and precise function descriptions, no narrative ("no blah").
- Structure after TRX64-Ultimate's manual: one chapter per menu, a table per page (item, values, default, effect).
- Screenshots from the ue2 emulator (`tools/ui_emu.sh`), where they help.
- Content from the code only, not from specs or comments.

## §4 Out

- The cores (Alex): chapter 5 is not rewritten; the core applications' menus are not documented here.
- Photos of the board; the release text; GitHub.

## §5 Built

**State (2026-10-31, branch `t169-manual`, built; coordinator review open).** `docs/manual/` holds chapters 00-16, one per view or topic:
00 index, 01 safety, 02 installation, 03 first start, 04 boot menu (F1), 05 file browser (F3), 06 edit menu (F5), 07 network / web page /
colour test (F7), 08 settings (F8), 09 update, restore, package (U, R, P), 10 bug report, 11 cores (text unchanged, Alex: no core
documentation; only two references fixed), 12 ROMs, 13 full firmwares, 14 recovery, 15 troubleshooting, 16 glossary. Pictures: 27 PNGs
`docs/img/host-*.png` from `tools/ui_emu.sh --headless` runs of the host app c721bc07 (built with `HOSTAPP_EMU_FLASH`, so the update writes
in ue2emu; `tests/hostapp/emu_txu.sh`). `tools/check_manual_links.py` checks every `](...)` target of `docs/manual/*.md` and `README.md` and
every `N.N` / `N.N.N` / `chapter N` reference of the manual: 0 findings.

**Source of chapters 02 and 14 (installer).** The updater that runs on the Commodore firmware 1.1.0 is the one pinned by the submodule
`apps/ultimate` (commit 32e5fd5f, `update_trx.cc`, read with `git show`; the checkout in the main tree is at an older commit). Flow as
coded: header lines, `Package OK.`, two warranty questions (default No), for a package that holds the host and an installed bitstream
without TRX stamp: medium choice (SD / USB), `<medium>/cores/` with `TRXBK_<date>_nnn.BIN` + `.CFW` + the old firmware as `.cor`
(`<vendor>-<version>.cor`) + the `.cor`/`.cfw` files beside the `.ue2` (up to 24), `Flash:/boot.cfg` (countdown 5, direct 1, default = the
backup), write + verify (3 tries), `First start: <name>`, power off. The restore bundle path (`About to restore. Continue?`, settings
question) is as coded.

**Dropped, and why.**
- `02`/`01`/`03`/`15`: "start the updater from a C64 core, else black picture". Not in the updater code (it runs after the vendor
  file browser hands over); no source. Dropped.
- Old 02 backup text: "the backup goes to the first medium with room, always with the config pages, the bundle needs 4.3 MB more".
  The pinned updater asks for the medium and writes into `cores/`; the size is computed (`Backing up the flash (x.y MB)`); the 4.3 MB
  figure is not in the code. Replaced by the coded flow.
- "Only a JTAG cable puts the raw `.BIN` back" (old 2.4.3, 14.4.3). The code writes a 64-byte header and a 12-byte trailer around the raw
  image; nothing in the code or the host says how a `.BIN` is flashed. Dropped.
- "A JTAG adapter active at power-on stops the FPGA from loading" (01, 02.6, 14, 15, 16, README). `docs/board-tooling.md` states Alex's
  correction of 2026-10-13: the FPGA loads from flash with or without the Blaster. Dropped everywhere. (The project memory still holds
  the old claim; it is stale.)
- "The backup holds the board's network password in clear" (01, README 3.7). Not in the updater code; it stores the configuration pages
  of the flash. Reworded to that.
- "Tested on the board on 2026-10-10: restore without and with settings" (old 14.2.1), "Tested: Gideon 3.15a and the large TRX64 firmware"
  (13.1.2, README 4.3), "about 40 s the first time, then about 10 s" (13.2.6): history or measurements without a source in the code.
  Dropped.
- Badge `found` and the Add core page (F3 as a separate view) in 03, 13, 15, README: no scan since 2026-10-25. Replaced by **A** in the File
  Browser. `docs/img/host-boot-menu-found.png` removed (no link left; the T121 spec still names it as history).
- Stale refs: 15 pointed to old 4.4.2 / 3.6 / chapter 6 paths (`roms/<folder>/`); now the core folder `cores/<name>/roms/`.

**Colour test picture.** `docs/img/host-colors.png` was a byte copy of `host-network.png` and is deleted. Re-run (`key f7`, `key c`, `key h`
in ue2emu, host app c721bc07): the screen does not change. Cause in the code: `on_key` case `V_NET` sets `view = V_COLORS` without `go()`;
`apply_updates` only calls `refresh_current()`, so `vwin[V_COLORS]` (parent of the swatches and of `c_pat`) stays hidden. The colour page
cannot be seen. Chapter 7.5 carries a NOTE. App fix for the coordinator: `go(V_COLORS)` in that branch.

**Known app defects found (not fixed here).** (1) The copy/move progress line prints `39%%` (`fo_progress` puts `%%` through a printf
that has no `%%`, hostui.cc:2992; the restore page avoids it with `fw_pct`, hostui.cc:3433). (2) In the colour test the key V is taken
first by the global HDMI step (hostui.cc:4841), so the `v` pattern is unreachable. (3) The colour page is not shown (above).

**Open points.**
- Whether the `.txu` is part of the release folder (`tools/release_mc.sh` / `make_release_mc.sh` attach `.cfw`, `.ue2`, `.cor`,
  `INSTALL.md`, `SHA256SUMS` only). 02.1.4 carries a NOTE.
- Whether the file browser of a firmware that the host started from the backup `.cor` offers **Run Update** for `TRXBK_*.CFW`
  (14.3 NOTE). The host itself has no code for `TRXBK`.
- The raw bitstream and application files for the cable way (14.5): the release does not name them (NOTE in 14).
- 01.5.2 "the shipped bitstreams are the debug build; reSID8, UCI, streams, REU, sampler, turbo off" is carried over from README 3.6, not
  checked against the build.
- The behaviour of the installer when the installed bitstream is already ours (`installed_axss != 0`: two warranty questions, then the
  normal backup to the root of the first medium, no cores folder, no `boot.cfg`) is read from the code but not described in chapter 2.
- Pictures of the installer screens (vendor updater) do not exist; the emulator runs the host app only.
