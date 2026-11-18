# Board test — packages main-7fda7e1f, main-91f629ce (2026-11-04), main-126c2078 (2026-11-05) and main-b8b6bcf7 (2026-11-06)

**Closed 2026-11-18.** Every step of this list is done or closed in its spec (Alex 2026-11-18: "that is old stuff that has
long been running" and "above all you must tick it off in the TODOs/specs, or it turns up again with some agent", translated).
A new board list carries only what is new since the last board session; it never re-lists the steps of this file.

One list, one session. The board runs the tester release t164-c83a83c2 (Alex, 11-03); this package is `main` at
`7fda7e1f`, CI run 1147 green, Gitea package `trx64u-multicore` version `main-7fda7e1f`. Earlier lists are in
`docs/archive/board-tests/`.

Who: **A** = Alex at the board · **C** = Claude over FTP/REST/JTAG · **C+A** = Claude does it, Alex looks.
Anything that fails: write down the core, the picture mode and what the screen shows (a photo is best).

Checked before (C, 11-04): `.txu`, `.cfw` and `.ue2` match `SHA256SUMS`; the `.txu` carries `null_full.bit` (not
`c64_basic.bit`) and the host app of 867 812 B; `tools/check_release_flash.py`: `.cfw` and `.ue2` byte-equal to the
`.txu`, both probes fail as they must.

## Session 2026-11-14 (Alex: "Das messen wir hoffentlich heute mal", translated: "we hope to measure that today")

| # | Who | Do | See | From |
|---|---|---|---|---|
| D.1 | A | T140 analogue picture: S-Video and composite on the A/V port, PAL and NTSC, on a core that has the AV out (C128). | Picture is stable and the colours are right in all four combinations; record the findings in `docs/todo/T140.md`. | T140 (stays open) |

## Package main-b8b6bcf7 (2026-11-06, CI run 1185 green) - WiFi (#45), home folder (#40), hotkeys (T181), C128 JiffyDOS (#46)

Not yet installed: C installs it over FTP (`.txu` to `/SD/txu/`, the `.cor` files to `/SD/cores/`, the 11-05 ones to an archive folder) and checks
it with `tools/release_mc.sh` before the session; A updates the host with M.1's steps. The C64SCPU core is not part of this session (steps 7
and 9 open). The C128 picture (T164 "C128 black") passed on 11-04 (1.4).

| # | Who | Do | See | From |
|---|---|---|---|---|
| W.1 | A | No LAN cable. Host F8, WiFi: scan, pick the network, password, connect. | **Fail 11-06 (main-a9ef1237):** state "Disabled", S/M said "There is no WiFi module to talk to"; the module's radio was off and the host page had no On/Off. Fix: T184 D3 amended. | T184 S1 |
| W.2 | C | `curl ftp://<that ip>/SD/` from the Mac. | **Pass 11-06:** `/SD/` listed over WiFi at 192.0.2.44 (network from the module's own memory). Top bar said "no network" (fix with D3 amended). | T184 S1 |
| W.3 | A | Power-cycle. | **Pass 11-06:** WiFi up again by itself (Alex). | T184 S2 |
| W.4 | A+C | Start the C64 core; F8 System shows the WiFi row (state, network, IP) and On/Off; C lists `/SD/` over FTP at that IP. | **Pass 11-06:** row "Link up, iPhonedation, 192.0.2.44 - On"; FTP listing with trx64u running (17 entries, 0.26 s). The trx64u menu drew in a quarter of the 1080p screen and was slow (analysis open). | T184 S3, D3 |
| W.5 | A | WiFi row Off in the core, then On. | The link goes down and comes back; nothing else changes. | T184 D3  Deferred to the package with the host On/Off. |
| W.6 | A | Host F8 WiFi: forget the network; LAN cable back in; power-cycle. | The host menu comes up as fast as before; Ethernet FTP works. | T184 S4  Deferred to the package with the host On/Off. |
| H.1 | A | Any core, F5 Settings: "Home Directory" to a folder, "Enter Home on Startup" Yes; leave and start the core. | F3 opens in that folder. | T180 D10 (#40) |

**Stopped 11-06 (Alex):** the UI work since 11-01 (T180 D10 rows, T181 hotkey rows, T184 D3 WiFi rows, T185 ROM set) had no UX review. F5 Settings of the A1200 runs past 18 rows, the selection scrolls out of view, the "Picture" toast covers the last rows, Home Directory is out of sight; trx64u draws its menu in a quarter of the 1080p screen and is slow. H.1, K.*, J.2, J.3 wait for the UX rework; J.1 and F.1 can run.
| K.1 | A | C64 core, a game running: CTRL + RESTORE. | The menu opens; again: it closes. The game never sees the keys. | T181 S1 |
| K.2 | A | C64 core: CTRL + "+" held, then CTRL + "-". | The volume steps up (repeats while held), then down; nothing is drawn over the picture, only the volume changes. | T181 |
| K.3 | A | C64 core: CTRL + J, CTRL + P, CTRL + R. | Ports swap; the picture mode changes; the machine resets. | T181 |
| K.4 | A | Apple II, Atari 800, ZX, CPC: C= + CTRL + RESTORE in each. | The menu opens in each; typing in the core still works. | T181 open point |
| K.5 | A | C64 core, F5: "Hotkey modifier" Off. | CTRL + RESTORE does nothing special any more. | T181 D3 |
| J.1 | C | Put Alex's JiffyDOS kit files anywhere on the stick or SD. C128, F5 Settings: with "ROM set" Standard the four JiffyDOS rows say "Not selected". Select the files (RETURN on a row opens the selector, or F3 on the file, "Use as JiffyDOS ... ROM"): C128 KERNAL, C64 KERNAL, 1541, 1581. | The machine starts Standard by default; each file is taken or refused with its reason; selected files show by name. | T185 D4 |
| J.2 | A | C128, F5 "ROM set" JiffyDOS (files selected as J.1), restart; with nothing selected it must start Stock and say so; then the same in C64 mode (GO 64). | The JiffyDOS banner in both modes. | T185 S6 |
| J.3 | A | `LOAD"$",8` and one program from a `.d64` in the 1541, then a `.d81` in the 1581; once with Standard. | Loads work in both; JiffyDOS clearly faster. | T185 S6 |
| F.1 | A | Any core, F3 `/ftp`: browse a real FTP server on the LAN (e.g. the NAS), load one file. | The listing and the file arrive; leave it idle 10 min, browse again: it reconnects without hanging. | T186 S4 |

## Package main-126c2078 (2026-11-05, CI run 1160 green) - T180 media housekeeping and #38 long names

Checked (C, 11-05): `tools/release_mc.sh main-126c2078` (not published): every `.cor` against the host key, `check_release_flash.py`
PASS for `.cfw` and `.ue2` (byte-equal to the `.txu`), both probes fail as they must, the zip holds all 23 files. On the SD (C, 11-05):
`/SD/txu/trx64host_update_126c2078.txu` and the 18 `.cor` in `/SD/cores/`, read back byte-equal; the 11-04 ones moved to `/SD/archive-20261104/`.
GitHub tester release (Alex, 11-05: "both in", translated): the whole bundle, C128 and Port Diagnostics included (t164 had neither).

| # | Who | Do | See | From |
|---|---|---|---|---|
| M.1 | A | F3, `txu/trx64host_update_126c2078.txu`, update; the box restarts by itself. | **Fail 11-05:** question box right ("Only the app changes", host not written again), then "The flash may not boot - the erase did not take (is the flash write protected?) ... address 0x220000"; RETURN repeats it every time, also after Protect Flash was set Off. Cause: Protect Flash On had locked 0x0-0x3FFFFF with SRL before a large core start; SRL holds until power-off. Nothing erased (the chip refused the first erase): ESC + power cycle. Fix: T152 (check the lock before writing; the box must not say "do not switch off" when nothing was written). **Pass 11-05 after a power cycle:** update written (Alex: "the update works", translated). Published on GitHub as pre-release `trx64mc-main-126c2078` (Alex's go, 11-05). | T152 D11 |
| M.2 | A | Amiga 1200, F3: an `.adf` whose name is longer than 55 characters (shorter than 128). | The row shows the name cut with `..` and `.adf` at the end. | #38 |
| M.3 | A | Same file: Enter, mount in DF0. | It mounts; the status line shows the cut name. | #38 |
| M.4 | A | Same file: Enter, Rename, add or remove characters, Enter. | Renamed; the list shows the new name. No "no operation". | #38, T180 D6 |
| M.5 | A | Mount a second disk in the occupied DF0. | The first goes out, the second is in (no stall, no error). | T180 D1 |
| M.6 | A | F8 "Persist medium mounts" Yes; mount DF0; leave the core and start it again. | DF0 is mounted again at the start. | T180 D2 |
| M.7 | A | Media tab: save a set "test", change the mounts, load "test". | The core resets with the saved mounts. | T180 D5 |
| M.8 | A | In the C64 core write to a `.d64` (SAVE), then return to the host. | The saved file is on the image when it is mounted again. | T180 D4 |
| M.9 | A | F3 in any core: SPACE marks two files, copy them into a folder. | Both copied. | T180 D6 |
| M.10 | A | F3 in a long folder and in the host File Browser: "+" / "-". | One page down / up. | T180 D7 |
| M.11 | A | F3 in any core. | No `[Recent]` / `[Favorites]` rows. | T180 D3 |

## Package main-91f629ce (2026-11-04, CI run 1151 green) - retest of the 11-04 findings

Checked (C): `.txu`/`.cfw`/`.ue2` match `SHA256SUMS`; `.txu` host = `null_full.bit` (host unchanged, 96F6AB08), app 873364 B;
`check_release_flash.py` PASS for `.cfw` and `.ue2`, probes fail as they must. On the SD (C, 11-04): `/SD/trx64host_update_91f629ce.txu`
(read back byte-equal) and the 18 `.cor` in `/SD/cores/` (sizes equal); the main-7fda7e1f ones moved to `/SD/archive-7fda7e1f/`.
Rebuilt in Vivado: c128, c264, vc20 (LED); everything else is app work.

| # | Who | Do | See | From |
|---|---|---|---|---|
| N.1 | A | F3, `trx64host_update_91f629ce.txu`, update, then power-cycle by hand. The update runs on the OLD app, so it looks as before. | **Pass 11-04.** Top bar after the power cycle: `app 0.2.0+91f629ce`. |  |
| N.1b | A | F3, the same `.txu` again (new app now). | **Pass 11-04.** Box "Nothing to write" with "any key returns to the File Browser" inside it after a blank line (D10). The restart by itself (D11) shows only at the next real update (next package). | T152 D10 |
| N.2 | A | Direct boot on, stored 720p, power-cycle; C= held for the menu. | **Pass 11-04.** Splash, saved menu picture and menu whole and centred (960 x 540 at 1x); the "hold C=" line 3x, not full width. | T164 boot720, T135 D13 |
| N.3 | A | Boot menu with Direct boot on. | **Pass 11-04.** No countdown line, no bar. Direct boot off in F8, back to F1: the line is back. | T135 D12 |
| N.4 | A | Change the picture (V or F8), press nothing / press a key. | **Pass 11-04.** A box "Keep this picture?" in the middle with the countdown and "Press any key to keep it"; same in a core menu. | T164 Q7 |
| N.5 | A | F8 and side bar at 800 x 600 and 480p. | **Pass 11-04.** No text ends in dots. | T164 D10q |
| N.6 | A | A core's Picture row with Auto. | **Pass 11-04.** "Auto (W x H from host)". | T164 D10c |
| N.7 | A | C128, C264, VC-20 with LED Load Indicator on: at READY, then load from disk. | **Pass 11-04 C128:** no blue any more; the red flicker follows the drive's activity (Alex: "it only blinks when it shovels data", translated). C264, VC-20 not seen. Lamp steady red at READY (no blue flicker), flickers while the drive's LED is on. | T159 |
| N.8 | A | Rest of the 7fda7e1f list: 2.3 joystick, 2.4 sound, part 3. | As there. | |

## 0. Install

| # | Who | Do | See |
|---|---|---|---|
| 0.1 | C | Put `trx64host_update_7fda7e1f.txu` and every `.cor` of the package on the SD card (`/SD/` and `/SD/cores/`) over FTP. | Done 11-04 (C): 18 `.cor` in `/SD/cores/` (sizes equal), `.txu` in `/SD/` (read back byte-equal); the c83a83c2 `.cor` moved to `/SD/archive-c83a83c2/`. Start no core before 0.3. |
| 0.2 | A | Host menu, F3, `trx64host_update_7fda7e1f.txu`, update, answer the question. | **Pass 11-04:** "complete and intact", app only (host 96F6AB08 kept), written and verified in 6 s. Findings: keys only in the footer (T152 D10), no restart by itself (T152 D11). |
| 0.3 | A | Power-cycle. | Host menu with picture; top bar shows the new host. |
| 0.4 | A | Boot menu: look at the core list. | No core says "built for another host". |

## 1. Fixed since c83a83c2 (each was a board finding)

| # | Who | Do | See | From |
|---|---|---|---|---|
| 1.1 | A | Host menu: change the picture mode with V or F8 Picture, press nothing. | **11-04:** returns to the old mode, but the question is a foot line, no popover (Q7 amended). | T164 Q7, picnote |
| 1.2 | A | Same, press a key within 8 s; then power-cycle. | **11-04, 720p: the host starts in 720p (picsave works), but splash, saved menu picture and menu are drawn too big, only the top-left part shows (branch t164-boot720).** | #24, picsave |
| 1.3 | A | Host mode set (e.g. 720p); start a core whose Picture is Auto (A1200, NES). | **Pass 11-04.** The value says only "Auto": D10c amended, "Auto (W x H from host)". **Built (D10q): the value reads "Auto (1280 x 720 from host)", board open.** | #24, D10c |
| 1.4 | A | Start C128. | **Pass 11-04** (READY, 122365 bytes free). LED Load Indicator on: lamp flickers blue fast, see 1.7. | T140 c128pic, c128tim |
| 1.5 | A | Start ZX in 48K mode. | **Pass 11-04** (Alex: "I am in the 48K Speccy"). | T119 5.15 |
| 1.6 | A | ZX Next, board issue Auto (default), once with each of your two images (the 2 GiB one stopped with "wrong board revision" on 11-02). | **Pass 11-04:** both images boot. | T116 D11 |
| 1.7 | A | F8 "LED Load Indicator" on; A500, then Atari 800 / Apple II / CPC: load from disk. | **Pass 11-04: Apple II, Atari 800, A500, CPC.** 11-04 C128: blue fast flicker = 1541 motor on the hard-disk input (T159 history), fix t159-1541led.** | T159 |
| 1.8 | A | Host menu at 576p and 480p: home page, file browser, boot menu; one core menu. | No text ends in "…", keys column whole. **11-04, 800 x 600: three texts end in dots (F8 page, side bar).** **Built (D10q): measured side bar and F8 label column in every compact mode, board open.** | T164 D10p, D10q |

## 2. Regression (short)

| # | Who | Do | See |
|---|---|---|---|
| 2.1 | A | Start each DFX core once (vc20, atari800, apple2, ddt_mandel, ddt_novic), menu key back to the host. | **Pass 11-04.** |
| 2.2 | A | Start each large core once (amiga, amiga1200, c264, cpc, nes, snes, neogeo, trx64u, trx64g). | **Pass 11-04.** |
| 2.3 | A | Joystick in a game of one DFX core and of the A1200. | **Pass 11-04.** Directions and fire reach the game. |
| 2.4 | A | Sound in the A1200 and one DFX core. | **Pass 11-04.** Clean, no crackle. |
| 2.5 | C | FTP list and one download; host web page in Safari. | **Pass 11-04 (C):** FTP list, `.txu` download 2464872 B; a file written over FTP is dated Nov 04 08:28 = CET now (NTP); web page HTTP 200 (curl, not Safari). |

## 3. Open from earlier lists (only if time)

| # | Who | Do | See | From |
|---|---|---|---|---|
| 3.1 | A | Amiga core, joystick in physical port 2, menu "Joystick ports" Normal, then Swapped. | The stick moves in the game in both settings as the menu says. | T157 S5 |
| 3.2 | A | F3, `diag.cor`, **S**; Write the example tests; run `pins.toml` with nothing in the ports. | `pins` passes; results in `/SD/cores/diag/results/`. | T175 |
