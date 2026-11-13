# T135 — The default core starts at power-on without the menu; a held key or fire button brings the menu

**Status:** BUILDING (2026-10-27): Q1-Q7 answered; installer (D8, D8a, D9) built 2026-10-29, board open. 2026-11-13 status check: state confirmed: BUILDING, merged in waves (w1-t135); S1-S7 in ue2emu, board S1/S3/S8/S9/S13/B2 open (as row).
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`), the core application's "Return to host
menu" (`coreapp/`), boot.cfg (`bootcfg.cc`); no gateware change planned (open: Q3).
**Number:** T135 (registry: `TODO.md`).
**Origin:** Alex, GitHub issue #2 comment of 2026-10-27 (English in the original): "what about skipping the menu and
directly booting default core - use C= key or joy port 1 button while powering on to invoke boot menu - should be
configurable in settings in boot menu". Then, 2026-10-27 (translated): "OK. The spec for booting straight through, please."
And (translated): "Oh yes. And at boot the cartridge (expansion) port *must* be completely off. Best in the host anyway."
**Depends on:** T84 (host app, boot.cfg), T101 (menu from boot.cfg), T115 (large cores, hand-over page, return to host),
T133 (caches; independent, both shorten the start).
**Milestone:** M3.

---

## §1 The problem

**Who:** everyone who powers the C64U on to use one core, nearly always the same one.

**What they want:** power on, and the default core runs, as on a stock C64U. The menu only when they ask for it.

**What happens today** (board, host app main-b5cd63d8, 2026-10-26/27, console over JTAG):
- The host app builds the whole LVGL menu before it shows anything: `[t+1870 ms] phase Building the menu`,
  `[t+9770 ms] phase menu built`, first menu frame at t+14.2 s (no caches); 4.1 s with the T133 caches.
- Then the countdown runs (`countdown=7` in `/Flash/boot.cfg` on the test board; `bootcfg.h:3`: "seconds before the
  default starts by itself; 0 = never"). The default core starts after first frame + countdown: about 21 s today.
- There is no way to skip the menu: countdown 0 means "never start", not "start at once".

**Why it matters:** the start of a machine is the first impression (issue #2: "takes ages"). Every second until the
default core runs is waiting for a menu nobody looks at.

**Parts (MECE):**
- **P1 Decide early.** Before any LVGL work: is direct boot on, is there a valid default, is the menu key held?
- **P2 The menu keys.** C= on the C64U keyboard, fire on joystick port 1; read at power-on, before the user lets go.
  A USB keyboard enumerates late (about 1 s, `USB hardware initialised` at t+105 ms, the stick seen later), so it can
  only count if we wait for it.
- **P3 Start the default.** The same start as when the countdown ends (layer-2 `.cor`, large `.cor`, `.cfw`), with its
  checks; nothing new, only earlier.
- **P4 When the default cannot start.** Missing file, changed file, wrong host key, removed medium: the menu with the
  reason, never a black screen and never a loop.
- **P5 Return to the host.** "Return to host menu" (core app SYSTEM menu, T115 held page) reloads the host by IPROG;
  a host that boots straight through would start the default again at once: the user could never reach the menu
  that way. The host must know it was asked for the menu.
- **P6 The setting.** On/off in the menu (F5 edit view, next to the countdown), stored in `/Flash/boot.cfg`; default
  for new and existing boot.cfg files (Q1).
- **P8 The expansion port while the host runs.** Today the host leaves it to the layer-2 core and forces only the
  buffer enables during a swap (`host/top.v:1608-1615`: with `core_decouple` OEn = 1, PHI2 = 0, BA = 1, IO/ROM lines 1,
  `slot_rst_o` 0). The null core holds the buffers off and PHI2 low but does **not** hold the port's RESET
  (`cores/null/layer2_null.v:318-333`, `slot_rst_o = 0`), and the host drives `SLOT_PWR = 1` always (`host/top.v:139`).
  A cartridge plugged in at power-on is therefore powered, not in reset, and sees no clock while the menu runs: its
  state is undefined (an EF3/KCS may start its own logic, drive GAME/EXROM, or hold the bus). Required (Alex): the port
  completely off during the boot and the menu, enforced by the host, not by whichever core is loaded.
- **P7 Feedback.** What the screen shows during the direct start (black, a splash line, the core's name), so a hang is
  distinguishable from a start.

## §2 Scenarios

```gherkin
Scenario: S1 power-on, direct boot on, no key held
  Given boot.cfg has direct=1 and a valid default (a layer-2 .cor)
  When  the board is powered on and no key is held
  Then  the default core runs; the console shows "[host] direct boot: <name>" and no "phase Building the menu"
  And   the time from power-on to the core's first picture is measured and written in §5 (target: at most 3 s, Q5 decided)

Scenario: S2 C= held at power-on
  Given direct=1
  When  C= is held while powering on (and released at any time after)
  Then  the menu appears with the countdown stopped
  And   the default is not started

Scenario: S3 fire on joystick port 1 held at power-on
  Same as S2 with the fire button of port 1, also when it stays held for as long as the person likes (Q9: the joystick never drives the host menu).

Scenario: S4 default missing or refused
  Given direct=1 and the default's file is gone (or changed, or for another host key)
  When  powered on
  Then  the menu appears with the reason on the default's row ("missing", "changed", ...) and no countdown
  And   no restart loop (control: delete the file, power on: menu, not black)

Scenario: S5 return to host from a core
  Given direct=1, the default core runs
  When  "Return to host menu" is chosen in the core's SYSTEM menu
  Then  the host menu appears and stays (countdown stopped)
  And   control: without the return marker the host boots the default again (proves the marker is what holds it)

Scenario: S6 setting off
  Given direct=0
  Then  today's behaviour exactly: menu, countdown, default

Scenario: S7 the setting in the menu
  When  direct boot is switched in F5 and the board is powered off and on
  Then  boot.cfg holds the new value (read back over FTP) and S1 or S6 follows

Scenario: S10 first start after the MultiCore install
  Given the updater asked for the cores medium (USB chosen), created /USB1/cores, wrote the backup as
        /USB1/cores/commodore-110.cor and boot.cfg with it as the default
  When  the board is powered on after the install
  Then  the backed-up firmware (Commodore 1.1.0 or Gideon's) runs straight away, its row is the default in the menu
  And   C= at power-on shows the MultiCore menu; nothing is written to the flash by this start

Scenario: S11 the two warranty questions
  When  the updater reaches the flash step
  Then  it shows "You are leaving the warranty of Gideon and Commodore. Do you want that?", then "Do you really want
        that?", both with No preselected
  And   No at either: the flash is unchanged (read back 0x000000 and 0x220000, SHA-256 equal to before), the popup says
        nothing was written

Scenario: S13 the default is on a USB stick that mounts late (GitHub issue #14)
  Given direct=1 and the default is /USB2/cores/commodore-110.cor
  When  the board is powered on and the stick is listed 3 s, or 12 s, after the host app's start
  Then  the screen says "Waiting for USB2 - hold C= for the menu" until the stick is listed, then the default starts; "missing" is never shown
  And   the medium listed for 1.5 s and the file still absent -> "Default core: file missing" (menu, no countdown)
  And   no medium for 30 s (BOOT_WAIT_MS) -> the same; C= or fire ends the wait at once
  Bench: `tests/hostapp/test_directboot.cc` "#14 S1-S6" (fake clock, fake medium, the host's own loop `directboot_look`);
  control: the old 4 s wait without the listing rule returns DD_MISSING for the 12 s stick, the test asserts that difference.

Scenario: S12 direct start of a staged large core
  Given the backup .cor is the default and /Flash/legacy/fullcore.bit holds it with its stamp
  When  powered on
  Then  the console shows no SHA-256 pass and no flash write, only "scratch file complete", then the IPROG
  And   control: truncate the stamp -> the host copies the bitstream again (and says so) before the IPROG

Scenario: S9 expansion port off while the host runs
  Given a cartridge in the port (EF3, then a plain 8K cart) and the null core in layer 2
  When  the board is powered on and the menu (or the direct start's decision) runs
  Then  JTAG SAMPLE shows SLOT_ADDROEn = SLOT_DATAOEn = 1, PHI2 = DOTCLK = 0, the port's RESET held low, and the port
        supply as decided in Q7
  And   the same with a layer-2 core that drives the port (c64ref) loaded but not yet released by the host (D6)
  And   control: the host's port lock off -> SAMPLE shows the core's levels (proves the lock is what holds them)

Scenario: S8 large core and .cfw as default
  S1 with a large .cor (amiga1200) and with a .cfw (Gideon 3.15a) as the default: both start straight through.
```

Benches: `tests/hostapp` gets the decision function (boot.cfg + key state + return marker + default state -> menu or
start, with the reason) as a pure function with a table test; the render bench shows the S4 menu. Board: S1-S8 by Alex
(power cycles are his), console read over JTAG.

## §3 Decision (proposed, open points in Q)

- **D1 One decision function before LVGL** (`hostui.cc` startup, before `trx_lv_open`): inputs boot.cfg, the return
  marker, the menu keys, the default's cheap check (file present, size as recorded; the full SHA-256 check stays in the
  start path as today). Output: start the default, or build the menu with a reason.
- **D2 Keys:** read the C64U matrix directly (`U64II_KEYB_ROW`/`COL`, `U64II_KEYB_JOY`, `host-app/software/system/u64.h`)
  polled over a 0.5 s window from the host app's start (Q4, decided) and, since Q4b, on until the jump into the core; C= = row 7 bit 5 of the C64 matrix, fire = joystick
  port 1 bit 4. USB keyboards do not count and are not waited for (Q2, decided).
- **D3 Return marker:** the core application writes a "menu requested" word into the DDR2 hand-over page before IPROG
  (the page survives IPROG, T115 board-proven); the host reads and clears it in D1. A power cycle loses DDR2, so a
  plain power-on never sees a stale marker.
- **D4 Setting:** `direct=0|1` in boot.cfg (unknown keys are ignored by older hosts, `bootcfg.cc` parser), shown and
  toggled in F5 beside the countdown.
- **D6a (Alex, 2026-10-30, "Ja"):** the port lock keeps PHI2 and DOTCLK running at the C64 rate while RESET is held and the buffers are
  off, as a real C64 with reset pressed does: a Kung Fu Flash restarts itself again and again without the clock (board 2026-10-30: boundary
  scan shows every port pin of the FPGA static under the lock, the cartridge's LED flickers fast, a press of its reset button after the
  core start makes it work, Gideon's firmware boots it at once). Host gateware, next host package (T164).
- **D6 Port lock in the host** (gateware, `host/top.v`): a host-owned lock, set at power-on and by every IPROG back to
  the host, forces the expansion port off whatever layer 2 drives: buffers off, PHI2/DOTCLK low, IO/ROM/BA idle, the
  port's RESET low (and the supply per Q7). The host application releases it only when it starts a core that uses the
  port (after the core is loaded and out of decouple); the null core never gets it released. Large cores carry the host's
  `top.v` and so the same lock; their core application releases it. Same mechanism as `core_decouple` for these lines,
  but its own bit, so a swap and the lock do not depend on each other.
- **D7 Direct boot is on by default** (new and existing boot.cfg without a `direct=` line).
- **D8 The installer sets the machine up, the backup becomes the default** (Alex, 2026-10-27, translated: "Let the
  installer do it. Ask where the cores should go - SD, USB. Create the folder. Create the backup's bit as a .cor. Enter it
  as the default."). The MultiCore updater (`apps/ultimate/software/application/update_u2p/update_trx.cc`), after it
  wrote the backup and before it flashes:
  1. asks where the cores go: SD or USB (only media that are present; the one holding the backup first);
  2. creates the cores folder there (`/cores`, T112's per-core folders below it are the cores' own business);
  3. packs the backed-up old firmware (its flash bitstream at 0x000000 and its app at 0x220000, already read for the
     backup) as a large `.cor` (T115 §7 layout: bitstream + app, manifest, SHA-256; the same records
     `tools/pack_large_cor.sh` writes) into that folder, named after the firmware it found (e.g. `commodore-110.cor`,
     `gideon-315a.cor`; the name from the bitstream / app header, else `backup-<date>.cor`);
  4. copies the firmware files that came with the install (the `.cor` cores and `.cfw` files of the release bundle
     lying beside the update file; Alex, 2026-10-27, translated: "Wait - it should copy the firmwares too") into the
     same folder; files already there with the same content are skipped (narrowed by D8c: the release ships none);
  5. writes `/Flash/boot.cfg` with the backup `.cor` as the default, the copied cores as further entries, `direct=1`.
  Then the first power-on after the install runs the old firmware straight away (S10); C= / fire gives the MultiCore
  menu. Open while building: the updater has to write a file on the flash disk (`/Flash`, the FAT in the W25Q128
  above the app; today it writes only 0x000000, 0x220000 and, for a restore, the config pages) - if that is not
  practical, the updater leaves a request beside the backup and the host's first start writes boot.cfg (same result).
- **D8a Order and place (Alex, 2026-10-29):** after the two D9 questions and before the first flash write the installer asks for the medium (SD or USB;
  only media present with enough room; the one holding the update file first). Backup AND cores always go to `/<medium>/cores/` at the medium's root,
  never a folder below the update file. Order: D9 question 1, D9 question 2, medium question, backup into `/<medium>/cores`, backup `.cor` there, copy
  the cores, boot.cfg, flash. This replaces "backup to the first medium with room" and the place of D8 step 1. Built: §5 "Installer".
- **D9 Two confirmations before the flash is written** (Alex, 2026-10-27, translated: "And then ask / say twice: You are
  leaving the warranty of Gideon and Commodore. Do you want that? Do you really want that? Only then flash."). After the
  backup and the setup steps of D8, before the first flash write, two popups in this order, each defaulting to No:
  "You are leaving the warranty of Gideon and Commodore. Do you want that?" and "Do you really want that?". Either No:
  nothing is written to the flash (the backup and the copied files stay on the medium), the updater says so and stops.
  They replace today's single "About to update. Continue?" popup.
  **Cancel goes back (Alex, 2026-10-29):** "When I cancel, it should simply go back to the menu" (translated): a No at
  either question, or any cancel before the first flash write, returns to the menu the updater was started from instead of
  stopping on a "nothing written" screen.
- **D8a Where backup and cores go (Alex, 2026-10-29):** "Better: after the 'ARE YOU SURE?' it asks where to put the backup
  and the cores, and it has to start in the root of a medium with /cores ... not /SD/some/deep/folder/cores" (translated).
  After the two questions of D9 (and before the first flash write) the installer asks for the medium (SD or USB, only
  those present with room, the one holding the update file first). Backup and cores always go to `/<medium>/cores/` at the
  medium's root, never to a folder below the update file. This replaces the order of D8 step 1 ("asks where the cores go"
  after the backup) and today's backup to the first medium with room (`update_trx.cc` `backup_pass`).
- **D9a The two questions always come (Alex, 2026-10-29):** "I want to show it, so it would be good if the safety questions
  come" (translated), on the build that skipped them when our own MultiCore firmware is already installed. The two D9
  questions are asked on every MultiCore install, also over our own firmware; the D8 setup steps (backup .cor, cores,
  boot.cfg) still run only when a foreign firmware is replaced.
- **D8b Backup icon (Alex, 2026-10-29):** "a, you are right, recovery" (translated): every backup `.cor` carries one fixed
  "recovery" icon (the same for every vendor), so it reads as the way back in the boot menu.
  **Look (Alex, 2026-10-31, with a screenshot of common "restore" pictograms: "roughly like that is good", translated):** a ring
  arrow running counter-clockwise (arrow head at the left, pointing down) around a clock face or a centre dot, as the usual
  "restore / history" pictogram; the clockwise cyan arrow of 2026-10-29 is replaced. Drawn by `tools/core_logos/recovery.py`, no third-party artwork.
- **D8c The release ships no cores (Alex, 2026-10-29):** "Not the .cor files we build here in the pipeline; [the
  firmwares meant] the host bitstream and the restore file of the original firmware .. the rest the user fetches from
  GitHub himself, please", then: "If a .cor lies there it may copy it, just do not put one into the installer, that is
  the point" (translated). The installer package and its release carry the host only; no pipeline `.cor` goes into it.
  D8 step 4 stays: what the user has put beside the update file is copied.
- **D10 A direct start skips the checks** (Alex, 2026-10-27, translated: "Skip all checks. It has to go into the flash
  if it is a large core. And it is one after the install. But if it is already there, it only needs one check before
  the jump that it is complete."). On a direct start the host does not run the start checks of the menu path (no SHA-256
  of the file, no host-key or start-check pass, no byte comparison). For a large `.cor` (the backup default is one,
  D8): the bitstream must be in the flash scratch file (`/Flash/legacy/fullcore.bit`, `legacy.h`). If the scratch file
  already holds this core (a stamp written with it: the `.cor`'s manifest id, size and bitstream SHA-256 from the
  manifest), the only check before the IPROG is that the scratch file is complete (its stamp present and its length
  as recorded), not a byte comparison with the `.cor`. If it holds another core or none, the bitstream is copied into it
  first (as `legacy_stage` does today) and the stamp written last. The application is read from the `.cor` into DDR2 as
  today. **Changed 2026-10-29 (Alex: "a, but then the host needs a display for it!!!", translated):** the installer does
  NOT write the scratch file; the host copies the bitstream once at the first direct start, and shows it while it does
  (what it copies, a progress bar, "do not switch off"), on the direct-start display of D5 (text layer, no LVGL).
- **D5 Feedback** (Q6: b or c, our choice by look and cost): first try (c) through the host's hardware text layer
  (`host/overlay.v`, the Ultimate menu overlay at 0x10140000: screen RAM 0x1000, colour RAM 0x2000) - one centred line
  "Starting <name> - hold C= for the menu" written before the key window, no LVGL, cost a few microseconds. If that
  looks poor on the 1080p picture, (b): the splash as one pre-rendered RGB565 picture copied to the pixel layer (no
  LVGL objects), with the name in the text layer on top. The time of each goes into §5; LVGL is not started on a
  direct start.
- **Q1** Answered 2026-10-27 (Alex, translated): "Direct, yes. The restore system should become the default entry. So my
  backup .cfw or .ue2." -> D7, D8.
- **Q2** Answered 2026-10-27 (Alex, translated): "USB keyboard no." Only C= and port-1 fire; no wait for USB.
- **Q3** Answered 2026-10-27 (Alex, translated): "The reset button does exactly the right thing today - reset the running core. Leave it like that." Direct boot and the menu keys act at power-on only; a reset (button, core menu, REST) resets the running core as today; "Return to host menu" always shows the menu (D3).
- **Q4** Answered 2026-10-27 (Alex: "B."): a short window, 0.5 s from the host application's start, in which C= or port-1 fire is taken; every direct start is that much longer.
- **Q4b** Answered 2026-10-28 at the board (Alex: "Ja, a) so kann ich den Start abbrechen..." - "Yes, (a): that way I can
  cancel the start"): the keys are also polled while the start line is shown and the default is being staged, up to the jump;
  a key seen there stops the start and opens the menu. Why: the board log of 2026-10-28 (below, §5) showed the line up only
  0.3 s before the window closed, so a reaction to the line came too late.
- **Q9** Answered 2026-11-11 (Alex, on issue #69, translated: "We defined it - we navigate in the host by keyboard. So switch that off please."):
  the joystick does not drive the host menu at all; port-1 fire stays only as the power-on menu key (Q2/Q4/Q4b). Why: a held fire became a
  RETURN in the menu through Gideon's matrix driver (`keyboard_c64.cc:278`, `joy_mtrx = 0x01`) with key repeat; the fire guard dropped only until
  the first empty poll (`directboot.cc` `fireguard_drop`, key < 0 = released), so the first repeat after `first_delay` started the default entry.
- **Q5** Answered 2026-10-27 (Alex: "Yes."): target power-on -> core picture at most 3 s (S1's pass mark); the measured value goes into §5.
- **Q7** Answered 2026-10-27 (Alex: "Fits. Let's start like that."): "off" = buffers off, no clock, RESET held low; `SLOT_PWR` stays 1 as in Gideon's design (measured 1 with and without a cart, T16 §3 line 119/695) until T16 P0 step g (multimeter on port pin 2/3 with SLOT_PWR 0) shows it switches the +5 V.
- **Q6** Answered 2026-10-27 (Alex, translated): "Well, b or c. Try what looks good but does not cost too much time." -> D5.

- **Bug B1 (GitHub issue #6, 2026-10-29, a tester):** over a modified firmware ("1.1.1+pkrl") the installer made no backup
  `.cor` and set no direct start; over stock 1.1.1 it did. Code: `update_trx.cc` decides "foreign firmware" by
  `installed_axss == 0` (`show_installed` -> `walk_axss`: any AXSS/USR_ACCESS word in the flash bitstream counts as ours).
  A self-built bitstream with USR_ACCESS set (e.g. a TIMESTAMP) therefore looks like ours (E, to be confirmed with that
  file). Fix: "ours" = the AXSS word is one of our stamps / host keys, anything else is foreign.
- **Bug B2 (GitHub issue #6, 2026-10-30, a tester, three backup files attached):** the installer run as a guest under MultiCore
  (a backup `.cor` of Commodore 1.1.0+s2p8 started from the MultiCore host) wrote a backup `.cfw` that is smaller than the good one and
  does not work; the same installer run in the plain 1.1.0+s2p8 firmware (no host in flash) wrote a good one. Alex, 2026-10-31
  (translated): "I think there was no valid restore .cor because it made the update from a MultiCore guest." Reading: as a guest the
  running firmware is in DDR2, not in the flash; the flash holds the MultiCore host and host app, so a backup read from the flash is not
  the running firmware. **Confirmed 2026-10-31** from the attached files (`tools/cfw_core.py` `probe`): the two good backups are byte-equal
  (bitstream 2,192,121 bytes sha256 992c6578..., design `u64_mk2_artix` 2026/02/28, no AXSS; application 1,058,080 bytes ddf51c25...,
  strings "1.1.0", "s2p8", "PRKL"); the bad one holds our host: bitstream 1,494,901 bytes, design `top` 2026/10/25 11:30:36 with an
  AXSS write, application 716,064 bytes with "TRX64" and "host app". The R restore of T137 would refuse that `.cfw` (its hash is not in
  `known_fw.h`); a backup `.cor` with that content would pass today. Same report as discussion #15 (update under
  1.1.0s2p8 loaded from the host: too short, no `.cor`, broken `.cfw` backup). Fix direction: the installer detects that it runs as a
  guest (valid hand-over block for its core, or our host stamp in the flash bitstream) and then makes no flash backup; the T137 restore
  refuses a backup whose bitstream carries one of our host stamps (`fwrestore.cc` computes `bit_axss` and does not check it yet).

- **D11 One installer, built by the CI at the end (Alex, 2026-10-31).** After a board session in which the tester file
  `trx64_update_*` (the TRX64 monolith: `c64_basic.bit` + Ultimate app) was handed over as "the way back to MultiCore" by mistake (the board
  stayed black; the flash dump of the morning was written back over JTAG), Alex (translated): "from now on we build the installer in the CI at
  the end, then that problem is gone", and "from the TRX64 monolith we only take the .cor files. The rest the other repository builds itself."
  So: the last CI stage builds exactly one MultiCore installer (`.ue2`/`.cfw`, clearly named, from this run's host bitstream, host app and
  installer app) and checks its content (bitstream design, host key, app version) before it goes into the package; `ci/update.sh` stops making
  `trx64_update_*`; from TRX64-Ultimate only `trx64g.cor` / `trx64u.cor` are packed (contract §1/§2: their updater files stay theirs).

- **D12 No countdown line with direct boot on (Alex, 2026-11-04).** Board photo (app t164@c83a83c2): the boot menu showed "Countdown
  stopped" with its bar. Alex (translated): "no, just leave it out when Direct Boot = ON, then the info has no value, whatever it says".
  So: with `direct=1` the boot menu shows neither the countdown text nor its bar (`cd_label_obj`, `cd_bar_obj` in `hostui.cc`); with
  `direct=0` it stays as it is.
- **D13 The "hold C=" line is not drawn at full width in 720p; the app does it (Alex, 2026-11-04).** Board 11-04 (stored 720p): the
  feedback line of D5 filled the whole screen width. Cause (`host/overlay.v:148`): the overlay picks its scale from the box size against the
  active area, x: 4 when 4 x (40 cols x 8 px = 320) <= 1280, so 4x in 720p; y independently from the box height. Alex (translated): "if we
  can solve it in the app, that would be nice". So: the host app sizes the box so the gateware picks 3x3 in 720p (e.g. 41 columns and
  enough blank rows that 4 x height > 720) and keeps 4x4 in 1080p; no gateware change.

## §5 Decisions taken while building

Built 2026-10-27 on branch `t135-direct`: host application and core application. Not built there: D6 / P8 / S9 (gateware port lock).
D8 / D9 / S10 / S11 (updater and installer) are built since 2026-10-29, see "Installer" at the end of this section. Open: board steps S1-S8, S12 (power cycles are Alex's), the measured times below.

- **D1 decision** `host-app/software/hostapp/directboot.{h,cc}` (pure, `directboot_decide`): no boot.cfg -> return marker -> held key -> `direct=0` ->
  no default -> the default's file. Table test `tests/hostapp/test_directboot.cc`. Caller `hostui.cc` `direct_phase()` (line 2800), called first
  in `startup()` (2900), before `Keyboard_C64`, before `trx_lv_open`. Waits at most 1.5 s for a boot.cfg that cannot be read yet (the flash disk
  mounts late), at most 4 s for a default on SD/USB whose file is not there yet (not for `/Flash`); both after the text line is up.
- **D2 keys** `menukeys.cc`: the registers and their order are `Keyboard_C64::scan()`'s. C= = write `U64II_KEYB_COL` 0x7F (column 7), read
  `U64II_KEYB_ROW` bit 5 low (`modifier_map` index 0x3D); fire port 1 = write `U64II_KEYB_JOY` 1, read it, bit 4 low (`keyb_joy_select` 1 =
  port 1 in `scan()`), the keyboard deselected (COL 0xFF) meanwhile. The spec's guess "row 7 bit 5" and "bit 4" was right in `scan()`'s
  naming. A sample latches; samples at `ultimate_main` entry, after the init functions, after the video mode (`hostapp.cc` 68-79), in every wait
  of `direct_phase`, and a window of 500 ms from the start of `direct_phase` (Q4). The window starts at the UI task, not at the scheduler:
  the scheduler's first 500 ms are gone by then (UI task starts after t+1.8 s, measured 2026-10-26), the early samples cover them.
- **D3 marker** `handover.{h,cc}`: two words at page offset 0xFF8 (`HANDOVER_MENU_MAGIC`, its complement), outside `struct Handover` and its sum, so
  old readers stay valid. Written by `coreapp/src/hw.cc:443` (`hw_return_to_host`, before the IPROG); read and cleared by `direct_phase`.
  A marker also wins over `direct=0` (Q3: "Return to host menu" always shows the menu; the old behaviour restarted the countdown).
- **D4 setting** `bootcfg.{h,cc}`: `direct=` (absent = 1, D7), written after `countdown=`; the F5 view shows it beside the countdown and **B**
  toggles it (`hostui.cc` 1012, 1032, 2678; footer text). A core application's rewrite of boot.cfg (libguard) goes through `BootCfg`, so `direct=` survives.
- **D5 feedback** `ovtext.{h,cc}`, first variant (c) only: the hardware text layer, 8x9 cells, 40 columns, the overlay centres and scales the box
  (4x4 output pixels per cell). One line "Starting <name> - hold C= for the menu" when it fits 40 columns, else the name on one line and "Hold
  C= for the menu" under it. Written before the key window, off again before the jump and before the menu. Cost measured on the board: open.
  Variant (b) (pre-rendered splash) not built; falls to Alex's judgement of how (c) looks.
- **D10 skip the checks** layer-2 `.cor`: `core_stage(..., skip_checks)` (`coreboot.cc:59,84`), no hash of any part, the header probe (contract,
  host key) stays because a wrong host key hangs the ICAP. Large `.cor` / `.cfw`: `legacy_stage(..., direct)` (`legacy.cc:193`). Stamp file
  `/Flash/legacy/fullcore.stm`, one line: `cor|<core id>|<bitstream length>|<bitstream SHA-256 of the manifest>` (large `.cor`) or
  `cfw|<file size>|<mtime>|<bitstream offset>|<length>` (`.cfw`, which has no hashes) - `legacy_stamp_text`. A direct start with a matching stamp, the
  scratch file of the recorded length and one run of clusters: no bitstream read, no compare, no flash write (`legacy.cc:269`, console "scratch
  file complete"). Otherwise the bitstream is copied as before (read, compare, write if different, compare again) and the stamp written last
  (`legacy.cc:368`); `write_scratch` removes the stamp first (`legacy.cc:107`). A menu start also stamps after its own verified copy. The installer
  (D8) writes the stamp in this format after the bitstream, or the first direct start copies once.
- **Failure** `direct_run()` returns the reason; the menu comes up with it as the note, the default's row shows its badge, `cd_dead` (no countdown),
  never a loop (S4). A changed default (size/mtime/header differ) goes to the menu; an entry without stored mtime (an installer wrote it) starts as it is.
- **Console** `[t+N ms] direct boot: decision begins`, `... text line up (N ms to write it)`, `... decision START|<reason> (cfg, direct, marker, C=,
  fire, samples)`, `[host] direct boot: <name>`, `[t+N ms] direct boot: jump`.
- **Render bench** `HOSTUI_RENDER_TEST` compiles `direct_phase` out (no hardware); `tests/hostapp/render/render.cc` stubs follow the new signatures.
- **D6 port lock, application half** (gateware by the T133 agent, `a799ec827` on `t133-dcache`, moved here from its `docs/todo/T133.md`): `host/top.v`
  `port_lock` = not (JTAG 0x10002A bit 0 or the application's release bit); the application's bit is in `ultimate_sys` at the icap region +0x21 =
  CPU address 0x10060621 (write bit 0 = 1 releases; reads back; reset value 0 = locked, so every IPROG back to the host and every CPU reset locks).
  Locked: SLOT_ADDROEn/DATAOEn/ADDRDIR/DATADIR/IO1n/IO2n/ROMLn/ROMHn/BA = 1, PHI2 = DOTCLK = 0, address/data/R/W idle, `slot_rst_o` = 1 (the port's RESET
  held low), whatever layer 2 drives; `SLOT_PWR` stays 1 (Q7). Separate from `core_decouple`. JTAG: write 0x10002A bit 0 = 1 releases; reading
  0x10002A returns {port_lock, application bit, JTAG bit}. Host with the lock: host key f452ef8a, WNS host +0.142 ns, null +0.144 ns, pb_l1 11,013/14,200
  LUT, pb_l2 12,138/16,800 LUT (image: builder `~/t133dbg_out8/null_full.bit`). With this gateware nothing reaches the port until released:
  - host app: `coreboot.cc` `core_jump` writes `ICAP_PORT_REL = 1` right after the decouple is off (`ICAP_DC = 0`), before the jump, console
    `[host] expansion port released`. Covers the menu start and the direct start (both end in `core_jump`). Never for the null core (the host never
    jumps to it), nothing for a `.cfw` (`legacy_jump`: a foreign full bitstream has no lock).
  - core app: `hw_release_expansion_port()` (`coreapp/src/hw.cc`) called in `ultimate_main` (`coreapp.cc`) before the machine runs, console
    `[core] expansion port released`; a large core's flat build carries the same `top.v` lock and needs it, for a layer-2 core it repeats the host's bit.
    A bitstream without the ICAP loader writes nothing.
  - Older host gateware without the register ignores the write.

- 2026-10-28, board, package t136-a1200-colour 6fb4b2c2 (run 1018), console over JTAG, default trx64u.cor (large):
  - no key held: "[t+405 ms] direct boot: decision begins", "[t+585 ms] direct boot: text line up", "[t+985 ms] direct boot:
    decision START (cfg 1, direct 1, marker 0, C= 0, fire 0, 37 key samples)"; Alex pressed C= when he saw the line: too late (Q4b).
  - C= held at power-on: "[t+395 ms] direct boot: decision Menu: C= was held (... C= 1 ..., 5 key samples)" (S2 passed);
    "[t+3080 ms] phase menu built, drawing the first frame" (times from the app's start, the flash load before it not counted).

### S1-S7 in ue2emu, 2026-11-07 (branch `w1-t135`)

`tests/hostapp/emu_t135_direct.sh --kit <dir> --old-elf <elf>` (not in CI, macOS, ue2emu S44 host flow): the host app built with
`HOSTAPP_EMU_FLASH` (this tree), the VIC-20 `.cor` on an emulated USB stick, `coreapp.elf` as the core's application. It first lets the
browser make the VIC-20 the default (boot.cfg is written by the host itself, read back from the flash image at 0x580000 with
`tests/updater/fatread.py`), then runs one function per scenario. Every assertion reads the console log or boot.cfg, never a picture.
Run of 2026-11-07 (`hostapp_emu.elf` from the builder, `old-elf` = commit 4b181c16f):

```
PASS S1 no key: decision START, direct boot jump, no menu build (decision at 2060 ms, jump at 3025 ms)
PASS S2 C= held: menu built, nothing starts (control: the S1 run of the same flash does start)
PASS S3 fire port 1 held: menu built, nothing starts after the release (FireGuard)
PASS S3 control: the elf before the fire guard starts the default after the release (red as it must be)
PASS S4 default missing: reason on the row, no jump, one decision (no restart loop)
PASS S4 default changed (same bytes, other time stamp): reason, no jump
PASS S4 default refused (file grown, no longer a bundle): reason, no jump
PASS S5 return to host with the marker: menu stays, the default is not started again (control: S1, same flash, marker 0, starts)
PASS S6 direct=0: decision "Direct boot is off", menu built, countdown ends in the start
PASS S7 F8 "Direct boot" row toggles: boot.cfg read back from the flash image says direct=0 (was direct=1)
PASS S7 then power-on: as S6 (Direct boot is off, menu, the countdown starts the core)
emu_t135_direct: 0 FAIL
```

- **S3 found a bug.** `Keyboard_C64` turns the fire button of port 1 into RETURN and the menu replayed it, so the default started although
  the decision was "menu". Fix: `FireGuard` (`directboot.{h,cc}`), armed in `direct_phase` after the decision, used by `host_getch` /
  `host_getch_nowait` until the button is let go. Unit table and controls `db_fireguard_off`, `db_fireguard_never_ends` in `tests/hostapp/run.sh`.
  The red case is in the script: the elf before the fix passes the decision line and then logs `step 4: Jump` (S3 control).
- S1 shows the emulated times (decision 2060 ms, jump 3025 ms after the host app's start); power-on to the first picture is a board step.
- S5 runs the whole way: direct start, the core's CTRL+RESTORE menu, F8, "Return to host menu", `icap: IPROG WBSTAR 0 ... the menu marker set`,
  second `decision Menu requested by the core (... marker 1 ...)`, one jump in the whole log.
- Builder (`u64-build-env:ci`, CI cache), 2026-11-07: `ci/apptests.sh hostapp` `=== apptests hostapp: PASS in 127s`, controls `db_missing_starts`, `db_changed_starts`, `db_countdown_not_stopped`, `db_marker_ignored`, `db_off_ignored`, `db_fireguard_off`, `db_fireguard_never_ends`, `ourfw_*` each `fails as it must`; `ci/trxapps.sh` `=== trxapps done in 24m24s` (hostapp.app d0462c50...). The host render bench first failed to link (`undefined reference to fireguard_drop`): `directboot.cc` added to `tests/hostapp/render/run.sh`.
- Left (next agent): S8, S10, S12, B1, B2. Board steps for Alex: S1 time to picture, S3 with a real fire button, S8 `.cfw` default, S9 JTAG SAMPLE, S13 late stick, B2 real installer run.


### Installer (D8, D8a, D9, T132), built 2026-10-29 on branch `t135-installer` (apps/ultimate branch `t135-installer`, `update_trx.cc`)

- **D8a (Alex, 2026-10-29, via the coordinator): the order and the place.** D9 question 1, D9 question 2, then the medium question (SD or USB;
  only media that are present and have room; the one holding the update file first), then the backup (`TRXBK_..BIN` and `.CFW`), the backup `.cor`,
  the copied cores and boot.cfg, all in `/<medium>/cores/` at the medium's root (a medium with several partitions: in its first partition that has
  room), never beside the update file. This replaces "backup to the first medium with room" for a MultiCore install; the other packages keep
  `backup_pass` (first medium with room, root of the medium) and their one question.
- **When it applies.** `!restoring`, the installed bitstream has no stamp (not ours: Gideon, Commodore) and the package's application contains the
  string `/Flash/boot.cfg` (it is the MultiCore host: the tester package, whose application is the Ultimate app, does not). Not for a restore bundle
  and not over our own firmware (the warranty is gone already, and a "backup .cor" of the host would be a loop as a default): both keep today's one
  popup "About to update/restore. Continue?". **D9a (Alex, 2026-10-29):** the two warranty questions are asked on every MultiCore install, also over our own
  firmware (No still returns to the menu); the D8 steps stay for a replaced foreign firmware only; a package whose application is not the host keeps its one popup.
- **T132 bug 1.** `UserInterface::set_screen_title` centres by the length of the string it gets; the old title carried two colour codes and two blanks
  as characters and sat two columns left of the middle. The title is plain text now and redrawn in the updater's colour; the header lines
  (package, date, flash, FPGA, board) are centred by `header_line`.
- **T132 bug 2, cause.** `target/u64ii/riscv/update_trx/Makefile` did not link `sd_card.cc`, `sdio.cc`, `sdcard_manager.cc`: the updater's file manager
  had no root entry "SD", so `backup_pass` saw only `usb*`. Reproduced in ue2emu (control `s7_old_updater_sd`: "No USB/SD with 7.9 MB free") and gone with
  the three files linked (`s6_sd_only`: the backup, the restore bundle, the `.cor` and the copies on the SD card).
- **Backup .cor.** Large `.cor` (T115 §7), `origin` backup, `flash_bit` 0, `flash_app` 0x220000, `flash_id` = the chip's `get_type_string` (T137 D1/D2);
  bitstream record = the `.bit` file as it lies in the flash (a raw image without header: the raw image), application record = the image behind the 12
  byte header (`dlen` bytes); `idcode` and `map` (identity with an AXSS word, else gideon_bl4) read from the configuration data like `cor_pack.py
  scan_config`; SHA-256 of both parts (`sha256.c` from the host app). Name and id from the old application's own text "Software version:   X":
  X starting with 1 = `commodore-<x>`, 3 = `gideon-<x>`, else `firmware-<x>` (dots dropped, lower case); no version text: `backup-<date>`. **D8b (Alex, 2026-10-29):** every backup `.cor` carries one fixed recovery icon (circular arrow, 32 x 32 RGB565, `tools/core_logos/recovery/{logo-32.png,icon.rgb565}`,
  generated by `tools/core_logos/recovery.py`, embedded as `recovery_icon.h` in the updater), record 4, `icon_size`/`icon_sha256` in the manifest; still byte-equal to `cor_pack.py pack-large --icon`.
  Proof: the `.cor` the updater wrote is **byte-equal** to `cor_pack.py pack-large --origin backup` of the same flash image (`tests/updater/emu_check.py`).
  Not packable (reason shown, a popup asks "Go on without it?", No = back to the menu): application not loaded and started at 0x30000, IDCODE not the XC7A50T.
- **Bundle copy.** The updater is started by the launching firmware, which hands it no path, so the update file is found again on the media: every
  `.ue2`/`.cfw` whose size is the package's size (+0..64 bytes of header) and whose bytes at the manifest's place equal this manifest; the search runs
  breadth first (root, then three levels, at most 60 x level folders per level). The `.cor` and `.cfw` files in that folder are copied (compare first:
  same size and bytes = skipped, else written and read back); not copied: the update file itself, its twin, `trx64host_update*`, `trx64_update*`, `TRXBK*`.
  Not found: the screen says so and nothing is copied.
- **boot.cfg.** `Flash:/boot.cfg` through a mount of the flash drive that never formats (a drive that does not read is reported, popup "Go on anyway?").
  Entries: backup first (default 0), then the copied files in the order of the folder; names from the `.cor` manifest, else the file name; `size` the file
  size, `mtime` 0 and hash empty, so the host checks each entry once at its first start and starts a default without a stored check as it is
  (`hostui.cc` DD_OK rule above). `countdown=5`, `direct=1`, `video=-1,-1`. An existing boot.cfg is kept as `boot.cfg.bak`. Path names are the updater's
  (`/USB0/cores/...`, `/SD/cores/...`); the host uses the same file manager names (same hub port = same name; not measured on the board).
- **Not built, asked:** (1) D10 changed (Alex, 2026-10-29): the installer does NOT write `/Flash/legacy/fullcore.bit`; the host does it later, with a display.
  (2) - (3) nothing else.
- **Cancel returns to the menu (D9).** Run Update releases the launcher's user interface, stops the ESP32 and jumps into the updater: the launcher is
  gone from RAM, there is nothing to return into. Its flash is untouched before the first write, so the updater restarts the machine and the launcher's
  own start-up shows its menu: on a TRX64 bitstream (ICAP ID 0x7A) by the IPROG of `hw_return_to_host` after writing the T135 D3 menu marker into the hand-over
  page (the host then shows its menu, not the default); on Gideon's and Commodore's bitstreams (no ICAP there, T115 §7) by `wifi_machine_reboot()`, the
  ESP32 command behind the firmware's own "Power cycle". Any No, the menu button, "no backup", "no room" before the first write goes there. Errors
  (package damaged, wrong flash) still say "Nothing written" and switch off.
- **ue2emu proof** (`tests/updater/emu_t135.sh`, Commodore 1.1.0 as the launcher, flash made from c64u_v1.1.0.ue2, SD and USB images): results in the
  history of T135. Not provable there: the IPROG branch (no ICAP in the emulator), the menu button as cancel (the rocker needs the TRX IO block, the updater
  runs on Commodore's bitstream), the time of the hashing and the copying on the board, the host's first start from this boot.cfg.

### Issue #14: "Default core missing" with the default on USB
Cause: `hostui.cc` `direct_phase()` waited `DIRECT_MEDIUM_WAIT_MS` = 4 s for a default on SD / USB, then decided `DB_MISSING`; for that,
`directboot_stops_countdown` is true and `direct_phase` sets `cd_dead`, so the menu's own wait (`BOOT_WAIT_MS` 30 s, main loop) never ran:
the row went "missing" -> "ready" when the stick mounted (lazy re-probe), but the countdown stayed dead. A USB stick needs longer than 4 s.
Fix: `directboot_look` (`directboot.{h,cc}`) runs the wait: up to `BOOT_WAIT_MS` (the same 30 s), shows "Waiting for <medium>" through
`ovtext_line` while the medium's root is not listed, ends with "missing" only when the root has been listed for `DB_GRACE_MS` (1.5 s) and the file
is still absent, or the limit ran out; C= / fire end it. `hostui.cc` `direct_env_*` is the hardware side. The wait text is the hardware text layer,
not LVGL: no ue2emu walk of the menu. Board check (S13 on a real stick) is Alex's.

**D12 (2026-11-04, branch `t135-d12`).** `cd_update` in `hostui.cc` hides `cd_label_obj` and `cd_bar_obj` while `cfg.direct` is set and shows them again
otherwise; it runs on every refresh of the menu, so switching Direct boot off in F8 and going back to F1 brings the line back without a reboot. The
subtitle and the footer are unchanged (out of scope). Bench (`tests/hostapp/render`, builder, `u64-build-env:ci`, `ci/trxapps.sh` rc=0):
`D12 direct=1: label 0 bar 0`, `D12 direct=0: label 1 bar 1 text "Starting Core number 1 in 5 s"`, `D12 direct switched off in F8: label 1 bar 1 text
"Starting Core number 1 in 5 s"` (and hidden again when switched on), `render bench: 861 checks, 0 failed`. Negative control `cdalways`
(`-DTRX_CD_ALWAYS`, line left visible under direct boot): `control cdalways: the countdown line under direct boot is caught, as it must (2 D12 checks red)`.
RISC-V: hostapp.app 867908 bytes, coreapp.app 1639116 bytes (326952 of the slot unused); apptests hostapp / coreapp / loader PASS. The countdown-second area
checks of the bench now start from `direct=0`, since a hidden line has no area.

**D13 (2026-11-04, branch `t135-d13`).** `ovtext.cc` writes a box of 41 x 21 cells of 8 x 9 pixels (328 x 189) for every line, text up to 40
columns centred in its middle row(s) (row 10 for one line, rows 9 and 10 for two), all other cells blanks on colour 0x01 (white on palette 0
black); `ovtext_start_line` and `ovtext_line` (the "Waiting for <medium>" line of `hostui.cc`) both go through the one `put_rows`, so every
caller has the same box. The rule of `host/overlay.v` (largest n in 1..4 with n x box <= area, x at most 2 x y) then gives, for the 9 mode
rows of `host/hdmi_modes.vh` (screen/colour RAM use 861 of 4096 bytes, `r_nl` 6 bits holds 21, `r_cpl` 8 bits holds 41):

| mode (index) | hact x vact | SX x SY | box drawn | before (40 x 1-2) |
|---|---|---|---|---|
| 576p (0) | 720 x 576 | 2 x 3 | 656 x 567 | 2 x 4 |
| 480p (6) | 640 x 480 | 1 x 2 | 328 x 378 | 2 x 4 |
| 720p (1, 7) | 1280 x 720 | **3 x 3** | 984 x 567 | 4 x 4 = 1280 (full width) |
| 1080p (2, 8) | 1920 x 1080 | **4 x 4** | 1312 x 756 | 4 x 4 |
| 800x600 (9) | 800 x 600 | 2 x 3 | 656 x 567 | 2 x 4 |
| 1024x768 (10) | 1024 x 768 | 3 x 4 | 984 x 756 | 3 x 4 |
| 1280x1024 (11) | 1280 x 1024 | 3 x 4 | 984 x 756 | 4 x 4 |

No mode cuts (SX = 1 only in 480p, where the box is whole). 720p and 1080p are square. The other modes are not: 41 columns is the
smallest width over 1280 / 4, so 480p cannot keep the 2 x of before (2 x 328 > 640), and 576p, 480p, 800x600, 1280x1024 get SY from the height
that 720p needs; that is the price of doing it without gateware. The "before" column is the arithmetic of `overlay.v` for 320 x 9..18, not measured.
Bench `host/sim/ovl_hold/run.sh` (iverilog, `host/overlay.v` as it is, the app's register writes, a frame per mode row, box measured in the
second frame, expectation from this table, listed in `ci/sims.txt`/`ci/sims.lane` as `ovl_hold`), Mac, 2026-11-04:
`mode 7: 1280 x 720, box 41 x 21 cells, drawn x 148..1131 y 76..642 = 984 x 567, scale 3 x 3`, `mode 8: 1920 x 1080 ... = 1312 x 756, scale 4 x 4`,
the other rows as in the table, `OVL_HOLD ALL PASS`. Control (the old layout 40 x 2 cells against the 720p expectation 3 x 3):
`mode 7: 1280 x 720, box 40 x 2 cells, drawn x 0..1279 y 324..395 = 1280 x 72, scale 4 x 4` -> `control c_old (40 x 2 cells, 720p): FAIL as it has to`.
Mac unit test `tests/hostapp/test_directboot.cc`: `ovtext_fill` puts the text in row 10 (rows 9 and 10 for two lines), centred, everything else blank
on colour 0x01: `directboot: ok`.
RISC-V (builder, `u64-build-env:ci`, CI cache mounted, `ci/trxapps.sh` on the code commit, 27 min): rc=0, hostapp.app 873228 bytes, coreapp.app 1640016 bytes
(`check_app_size` ok), application host tests (`ci/apptests.sh`) passed. Not done: the look on the board (Alex), the text pixels of the box in the bench (the bench measures the box, not the glyphs).

### Issue #69: fire held, countdown still ran (2026-11-10)

Report (a tester, main-2bd67963, C64U): power on with joystick 1 fire held, the menu comes up, then the default auto-loads, keys do not stop it.
Cause: `directboot_decide` answered `DB_NOCFG` before it looked at the keys; when boot.cfg was not readable within the decision (flash disk
mounted late), the held fire/C= was ignored, the countdown was not stopped (`DB_NOCFG` keeps it) and ran once boot.cfg was read later.
Fix: a held C= or fire wins over `DB_NOCFG` (`directboot.cc`), so the countdown is given up like in S2/S3. Unit table in `test_directboot.cc`.
Not proven here: that this was the board's cause (the log line `direct boot: decision ... cfg 0` would show it); board step for Alex.

### Q9: the joystick does not drive the host menu (2026-11-11, issue #69)

`host-app/software/io/c64/keyboard_c64.{h,cc}` (the copy both host and core app compile; the vendor copy is not built) got `set_keyboard_only(bool)`, default off so the
core app is unchanged; with it on, `scan()` skips the whole joystick part (no `KEYB_JOY` read, no other-port peek, no `joy_mtrx`) and reads the matrix only.
`hostui.cc` sets it right after `new Keyboard_C64` (`matrix_kb->set_keyboard_only(true)`). `FireGuard` (`directboot.{h,cc}`, its arm/drop calls in `hostui.cc`,
its unit table) is removed: it covered only the first empty poll, nothing else needs it; the power-on fire key stays in `menukeys.cc`, which reads `KEYB_JOY` itself.
`KEYB_ROW` does not mix the joystick in: `host/ultimate_sys.vhd` builds it from `keys` alone (columns in `KEYB_COL`), `keys` = `kb_keys | bl_keys` in `host/top.v`,
the joysticks are the separate `joys` bits read through `KEYB_JOY`. Not read: how the keyboard controllers behind `kb_keys` treat a joystick on the real matrix lines.
Evidence: `emu_t135_direct.sh` S3 now holds port-1 fire for 30 s, never released (menu built, no `step 4: Jump`); control S3 with the pre-Q9 elf must start the default
(first event, `first_delay`, repeats of the old driver). The matrix keyboard still works: S5/S7 drive the menu with keys. Board step for Alex: hold fire at power-on and keep
holding 5 s -> the menu stays, nothing starts; then a key on the C64U keyboard still moves in the menu.

Proof run 2026-11-11 (new elf: this branch ce7a820b9 built by `tools/dev.sh riscv`; old elf: e021ede96, the commit before Q9; ue2emu on the Mac; `emu_t135_direct.sh --kit <new> --old-elf <old>`, rc 0):

```
PASS S3 fire port 1 held for 30 s (never released): menu built, no key from the joystick, nothing starts (Q9: the host menu is keyboard only)
PASS S3 control: the elf before Q9 starts the default from the repeated RETURN of the held fire (red as it must be)
PASS S5 return to host with the marker: menu stays, the default is not started again (control: S1, same flash, marker 0, starts)
PASS S7 F8 "Direct boot" row toggles: boot.cfg read back from the flash image says direct=0 (was direct=1)
PASS S7 then power-on: as S6 (Direct boot is off, menu, the countdown starts the core)
```

S1, S2, S4 (3 lines) and S6 also PASS in the same run (11 PASS, 0 FAIL). The S5 key walk was stale after the T187 core menu rework (15 downs in the System tab ran past
"Return to host menu"; it failed identically with the old elf, so not Q9): it now presses one down from Restart core, as `emu_host.sh` does.

Board 2026-11-11 (Alex, host update `trx64host_update_5cc93ede.txu`, main 5cc93edeb, flashed by Alex): port-1 fire held at power-on, the host
menu does not move and nothing starts ("Feuerknopf klappt bei mir", translated: "the fire button works for me"). #69 fixed on Alex's board.
