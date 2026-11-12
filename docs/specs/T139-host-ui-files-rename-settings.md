# T139 — The host app manages files, renames menu entries and has a settings page

**Status:** PROPOSED (2026-10-28), asked by Alex at the board.
**Repo:** TRX64U-MultiCore — the host application only (`host-app/software/hostapp/`, `tests/hostapp/`). No gateware.
**Number:** T139.
**Origin:** Alex, 2026-10-28, after testing run 1022 (translated): "File browser F3: no Delete or Move/Copy? We need those
in any case. Edit menu F5: with C a Change Display Name (V can go from the legend in this context), the name typed in,
updated in boot.cfg and on screen. In general: the thing needs a host menu Settings, I suggest F8: Direct Boot to Default
(on/off); if that is off, Autostart from Menu (on/off); Video Mode (resolution etc., and analogue picture provided for
here)." On the proposal that followed: "passt alles" ("all fine"), and on the name input: "that exists already in the
generic app for 'Create Disk image'".
**Depends on:** T84 (boot menu, boot.cfg), T101 (start check), T105 (V picture key), T135 (direct boot).
**Milestone:** M3.

---

## §1 The problem

The host's four views are F1 menu, F3 browser, F5 edit, F7 network (`hostui.cc` `nav_key`). Read from the code
(branch `t136-a1200-colour`, 3e2e46ee3):

- **P1 File operations.** The browser (F3) offers A (add to the menu), S (start) and F (format a medium) only. A file
  cannot be deleted, copied or moved; Alex has to use FTP from a computer to clean `/SD/cores` (today: `/SD/cores-old`,
  stale `.cfw` beside the new `.cor`).
- **P2 Rename.** An entry's display name is taken from the package when it is added (`bootcfg_add`, `BootEntry.name`).
  It can only be changed by removing and adding the entry again, which loses its place in the order.
- **P3 Settings are scattered.** Direct boot is B in the edit view, the countdown LEFT/RIGHT in the edit view (0 =
  never), the picture V everywhere (steps through the modes). There is no page that shows them together.
- **P4 Text input.** The host app (LVGL) has no text field. The generic application has one (`UIStringEdit`,
  `host-app/software/userinterface/`, used by "Create disk image").

## §2 Scenarios

```gherkin
Scenario: S1 delete a file
  Given the browser on /SD/cores with old.cor selected
  When  DEL is pressed, then DEL again
  Then  old.cor is gone from the card and from the list; the selection stays on the neighbour
  And   one DEL and then any other key: the file is still there (control)

Scenario: S2 deleting a file the menu points at
  Given boot.cfg has an entry whose path is /SD/cores/amiga.cor
  When  DEL is pressed on that file
  Then  the confirmation names the entry ("menu entry Amiga points at this file") before the second DEL

Scenario: S3 copy and move
  Given a file selected in the browser
  When  C (copy) or M (move) is pressed, a target directory is chosen with the browser and RETURN confirms
  Then  the target holds a byte-identical file (SHA-256 equal); after M the source is gone
  And   a target without room for the file: refused before one byte is written, the note gives free and needed bytes
  And   a target that has a file of that name: asks before it is replaced

Scenario: S4 rename a menu entry
  Given the edit view with an entry selected
  When  C is pressed, a name is typed and RETURN confirms
  Then  the edit view and the menu show the new name, boot.cfg holds it (read back), order and default unchanged
  And   ESC leaves the name as it was; an empty name is refused

Scenario: S5 the settings page
  Given any view
  When  F8 (SHIFT+F7 on the C64 keyboard) is pressed
  Then  the settings page shows: direct boot on/off, autostart on/off with its seconds (only while direct boot is off),
        picture mode; each change is written to boot.cfg and read back after a power cycle
  And   the edit view no longer has B and LEFT/RIGHT; its legend no longer shows V; V still steps the picture in every view
        (T105, the blind way back)

Scenario: S6 boot.cfg stays readable
  Given a boot.cfg written by the host of run 1022
  When  the new host reads it and saves it
  Then  every field survives (bench: parse -> format -> parse is identical); autostart off is countdown=0 as before
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1, S2 | P1 | `tests/hostapp` file-operation test (host fs on a RAM disk), plus board step | CI app stage, board |
| S3 | P1 | `tests/hostapp` copy/move test incl. the no-room and exists cases, plus board step | CI app stage, board |
| S4 | P2, P4 | `tests/hostapp` bootcfg rename test; board: typed on the C64 keyboard and a USB keyboard | CI, board |
| S5 | P3 | board step with photo; boot.cfg read back over FTP | board session |
| S6 | P3 | `tests/hostapp` test_bootcfg round trip | CI app stage |

## §3 Decision

- **D1 Browser keys** (F3): DEL twice = delete (as in the edit view), C = copy, M = move. Copy and move pick the target
  with the browser itself (a "pick target" mode: RETURN on a directory enters it, a confirm key takes the current
  directory). Directories: delete only when empty; copy and move of files only. Before writing, free space is checked
  (`/Flash` is small). The copy reads and writes in blocks with a progress note.
- **D2 Delete warns about menu entries** that point at the file (S2); it does not remove them, the menu shows them as
  missing as today.
- **D3 Rename** (F5, C): a modal text field, key behaviour taken from the generic application's `UIStringEdit`
  (cursor keys, DEL, INS, HOME/END, RETURN, ESC), drawn in LVGL. Maximum length = `BootEntry.name` (47 characters).
- **D4 Settings page on F8**, a fifth view. Rows: Direct boot (on/off), Autostart from menu (on/off, seconds 1..60; shown
  only while direct boot is off), Picture (the existing HDMI modes and the system PAL/NTSC). Autostart off is
  `countdown=0`; the seconds that were set are kept in a new field `autostart_s` so switching on restores them. B and
  LEFT/RIGHT leave the edit view; V leaves the edit view's legend and stays a global key.
- **D5 Analogue picture**: not on the page until the encoder exists (no grey row without a function). `video=` keeps its
  syntax; an analogue mode becomes a new value then.

## §4 Out

- The analogue encoder itself (discussed 2026-10-28, not decided).
- Converting `.cfw`/`.ue2` to `.cor` and restoring the original firmware: T137 (both depend on the same package marker).
- Recursive copy or delete of directories.
- Moving an entry with SPACE + cursor in the edit view (asked 2026-10-28): `+/-` already moves it. Alex, translated: "I did
  not have +/- on my radar at all - that is perfect as it is!"

## §5 Build notes

- 2026-10-28, SD reads (found while testing T139 on the board): with the host gateware of T133 (new floorplan) the SD
  card returned wrong data now and then at 25 MHz SCK (a .cor SHA-256 mismatch on the first read, OK on the second; one
  flipped bit 0x08->0x88 in a bitstream copied to flash; core app directory listings of the exFAT SD shrinking from 14 to
  0 entries over 40 min). The SD pins have no IOB or input-delay constraint (host/pins.xdc:235-241), MISO is sampled in
  fabric one 100 MHz cycle before SCK rises (spi.vhd:71-76); the 20 ns half period is marginal. Built on t139-host-ui:
  data CRC16 checked on every block (CID, CSD too) with up to 3 re-reads (6f789bdbf), and the read/write SCK halved to
  12.5 MHz (540b0618f). Board, run 1029 host app: every core's .cor SHA-checked twice by Alex, `sdio_crc_errors = 0`,
  `sdio_crc_giveups = 0` (read over JTAG at 0xE85D4), no SHA mismatch. Gateware fix (IOB / constraints) is a decision
  of Alex's (host change).

- **2026-11-01, board: M (move) in the File Browser "landed one level too high"** (Alex moved .nes files inside /SD/NES; they were in /SD afterwards).
  Cause: in the target pick `hostui.cc` took RETURN as "confirm the folder the browser shows" whatever row was highlighted (`browse_pick_confirm`
  uses `bpath`), while D1 says RETURN on a directory enters it; a highlighted folder was therefore never the target, its parent was. Now RETURN on a
  highlighted folder (not "..") enters it, RETURN on ".." or on a file row takes the folder shown (the header "Move to: <path>" always names it); the
  footer says so. Bench: `tests/hostapp/render/render.cc` (move into the current folder and into a subfolder, exact paths), control
  `t139_pick_return_parent` (old behaviour) fails. The core application's "Move to..." picker has an explicit row 0 "Move here" and enters folder rows, it
  never had this fault; `tests/coreapp/test_menuctl.cc` now pins its exact destination paths (control `mc_move_target`).
