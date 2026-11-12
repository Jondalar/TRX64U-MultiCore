# Open board tests — TRX64U MultiCore (as of 2026-10-16, main 8c160432, run 62)

One list, one session. Sources: every row of `TODO.md`, the specs of those rows, and what Alex
reported from the board since 2026-10-14. A row says "board-tested" only with his report or a
measurement behind it, never because a spec says so.

Who: **A** = Alex at the board (keys, plugs, eyes) · **C+A** = Claude over FTP/HTTP/JTAG, Alex
confirms the picture · **C** = Claude alone, no board attention needed.

Everything named here is already on the board's stick (`192.0.2.189`), uploaded 2026-10-16:
`/Usb1/cores/` the seven `.cor` of this release, `/Usb1/trx64host_update_8c160432_20261016.cfw`
the installer, `/Usb1/roms/amiga/` `kick13.rom` + `AmigaVision.rom` + `MegaAGS-Kickstart.rom`
(and the old `df0.adf`, left there on purpose - see C3).

## 0. First, or nothing else counts

The flash still holds a host application from before T105/T106. Without this step the V key, the
browser page and the new core UI are not on the board at all.

| # | Who | Step | What says it worked |
|---|---|---|---|
| 0a | A | Start a C64 core, open `/Usb1/trx64host_update_8c160432_20261016.cfw` in the file browser, **Run Update**, answer the backup question **YES** | `TRXBK_<date>_nnn.BIN` and `.CFW` appear beside it |
| 0b | A | Power-cycle (JTAG adapter out first) | The host menu comes up, top bar says `app` and `host` |

## 1. The host (T84, T101, T105, T106)

| # | Who | Step | What says it worked |
|---|---|---|---|
| A1 | A | The boot menu with 8 entries: cursor past the last visible row | It scrolls. He reported on 2026-10-15 that it does not |
| A2 | A | Press **V** | The resolution steps, the picture changes at once, the footer legend names the key |
| A3 | A | Power-cycle after A2 | The resolution it was left on comes back (it is in `/Flash/boot.cfg`) |
| A4 | A | A screen that lists no 50 Hz mode (the tester's case) | A picture, not a black screen - EDID picked what the screen can do |
| A5 | C+A | `http://<board>/` from a phone or a Mac on the same network | The page shows the menu, the media and the picture |
| A6 | C+A | Start a core from that page | The core comes up; nothing was typed on the board |
| A7 | C | `GET /v1/status` | JSON with the same facts as the page |
| A8 | A | The top bar when the cable is in | An address, not `no network`. The tester's board said `no network` with its ethernet lights on |
| A9 | A | **A** adds a bundle, **F5** reorders, countdown | The menu is his; nothing in the release writes it |

## 2. The core application (T93, T104, T95, T88)

| # | Who | Step | What says it worked |
|---|---|---|---|
| B1 | A | Start `atari800.cor`, then `nes.cor`: the rocker (Freeze) | The menu opens. The ESP32 link rate fix (`wifi_command_init`, 5 Mbaud with CTS/RTS) has never been seen on hardware |
| B2 | A | The new UI: **F1** Home, **F3** Library, **F5** machine, **F7** Actions, shifted **F8** System; the digits switch tabs | The nav rail and the tab strip follow, the content pane changes |
| B3 | A | Is it still laggy | He reported a laggy menu on 2026-10-15 |
| B4 | A | `USB1` in the browser right after the start | It is there without going to another screen and back (his report of 2026-10-15) |
| B5 | A | NES: load `sams-journey-demo-1.0.0-ntsc.nes`, play | It starts. **space** is B, **F3** Select, **F1** Start, joystick 1 and 2 give directions and A |
| B6 | A | Atari 800 XL: the menu, an `.atr`, a cartridge, the settings, sound | Each one, or a named failure. Only typing BASIC by hand is proven (2026-10-15) |

## 3. The Amiga cores (ADR-0056, T85, T97, T102)

| # | Who | Step | What says it worked |
|---|---|---|---|
| C1 | A | Start `amiga.cor` with all three ROMs in `/Usb1/roms/amiga/` | It boots **Kickstart 1.3**; the log line names `version 34.5` |
| C2 | A | Start `amiga1200.cor`, same directory | It boots **Kickstart 3.1** - `AmigaVision.rom` or `MegaAGS-Kickstart.rom`, neither of which carries a version in its name; the log says `version 40.68` |
| C3 | A | Both cores, right after the start, with `df0.adf` lying in that directory | **No disk is in DF0.** It used to be mounted by itself |
| C4 | A | The settings: "Kickstart file (at reload)" | The first entry reads `Auto (1.3 first)` on the OCS core and `Auto (3.1 first)` on the AGA one |
| C5 | A | Mount an `.adf` from the browser | Still works - only the automatic mount went |
| C6 | A | Amiga sound: does it still lag or crackle | The audio FIFO with rate adaption has never been heard on the board (T85) |
| C7 | A | A1200: AmigaVision from `.hdf` | T97 open; images above 4 GiB and the 68020 are not built |
| C8 | A | T128: F7 -> "Mount folder as HD0", pick Horst's A600 tree, size `1`, Reset; A1200 with Kickstart 3.1 (spec T128 B1) | Workbench screen from the folder; photo + console log |
| C9 | A | T128: start a program from the folder, save a file (Notepad / `Copy`), wait 2 s, look at the stick on the Mac (B2) | The file is in the folder, byte for byte; `*`, `?` in a name show as `%2a`, `%3f` |
| C10 | A | T128: `Delete`, `Rename`, `MakeDir`, `Protect +s`, `FileNote` in a Shell; `Info` (B3) | The folder follows; a `.uaem` for protection and comment; `Info` shows the size set |
| C11 | A | T128: press reset/F5 in the middle of a copy, then cut the power during another one (Alex cuts it) (B4) | Nothing written one second before is lost; the folder mounts again; no half file |
| C12 | C+A | T128: the console lines `took ... ms` (requests over 20 ms) during the boot and a big copy; Workbench boot time folder against `.hdf` (B5) | The numbers decide `FH_HANDLES` and the read-ahead (spec T128 5.5 no. 1) |
| C13 | A | T128: the same folder on the A500 core with Kickstart 2.05 or 3.x (B6) | The same disk; Kickstart 1.3 sees no hard disk, `.hdf` or folder |

## 4. Smaller

| # | Who | Step | What says it worked |
|---|---|---|---|
| D1 | A | C264: the Plus/4 model, a 1541, a cartridge | Only the C16 model booting BASIC V3.5 is proven (2026-10-14) |
| D2 | A | VC-20 | `.cor` on the stick since 2026-10-14, never started |

## Results of the session of 2026-10-16 (Alex at the board)

Green: the NES core runs Zelda; the Atari and NES core applications start with their core; the new UI
looks right; the Amiga OCS core has sound; both Amiga cores come up with an empty drive, as decided.

Found, each with its cause read out of the code, none of them guessed:

| What | Cause | Tracked in |
|---|---|---|
| The boot menu does not scroll past the 5th row | `apply_updates` takes the cheap path on a selection move and never calls `listwin_fix`, so the window cannot follow; past row 5 no row is highlighted at all (`hostui.cc:1596-1604`). The browse view has the handling (`:1605-1607`), the menu and the editor never got it | [T101](todo/T101.md) |
| The core application's menu answers a key only after 0.5-1 s | Every key goes through `ui_menu_render()` -> `trx_shell()` -> `lv_obj_clean(g_content)` (`trxview.cc:183`) and rebuilds every row as new LVGL objects. The frame cache spares the frame, not the pane. The host restyles two rows instead | [T104](todo/T104.md) |
| The A1200 boots nothing with Kickstart 3.1 (black, resets) | The ROM is genuine (both files byte-identical, `exec 40.10 (15.7.93)`, 40.68 = the A1200 3.1), but 40.68 is built for the 68EC020 and the core still has fx68k, a 68000 | [T97](todo/T97.md) |
| With 1.3 the AGA core is an A500 with more chip RAM | 1.3 does not know AGA at all: no AGA screen modes, no 8-plane Workbench. Only a game that writes the registers itself sees AGA. So the second core earns its place only with the 020 | [T97](todo/T97.md) |
| No sound on the A1200, sound on the A500 | Same gateware file, same audio path; the two cores differ in exactly three generics (`CHS_INIT=16`, `MEM_INIT=3`, `HDF_SECT=1`). So it is AGA or the 2 MB chip RAM, not the application | [T85](todo/T85.md) |
| The A500 sound still scratches slightly | Cannot be told apart from the board: `amiga_core.v:259` leaves `.st_under() .st_over() .st_fill()` unconnected, so no counter reaches a register. Underflow (a click) and pitch correction (a scratch) look the same from outside | [T98](todo/T98.md) |
| Inserting a disk does not start it, a real A500 boots it | `_disk_change` is only released by a step pulse for the selected drive with a disk already present (`paula_floppy.v:610`), and `disk_present` is set by the mount pulse alone (`:965`). The application's mount is a mount event, not a "drive door closed" event | [T85](todo/T85.md) |
| The NES picture sits in a wide border | The core already draws at the pixel layer's highest scale (3, `pix_layer.v:63`): 768 x 672 in 1920 x 1080 = 40 % x 62 %. At 720p the same pixels fill 93 % of the height. The side border is 4:3 on 16:9 and stays | [T107](todo/T107.md), [ADR-0057](adr/0057-every-application-owns-the-video-mode.md) |

Not reached in this session: the host update was not installed, so A1-A9 (scrolling, V key, EDID, the
browser page) were not tested on the new application.

## History

- **2026-10-16:** written for the session Alex announced ("Dann würde ich später gerne mal testen").
  The list before this one, `docs/board-tests-pending.md`, is the drop-in line's and stops at
  2026-10-07.
- **2026-10-16:** Alex: "Weg ja" - the stale `/Usb1/cores/vc20i.cor` of an older build is deleted, so
  the directory holds exactly the seven bundles of the release and the host menu is one entry shorter.
