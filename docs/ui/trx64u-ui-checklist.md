# TRX64U UI checklist

Built from Gideon's inventory as written (gideon-menu-tree.md 4, 8, 13; UI-COVERAGE.md; NAVIGATION.md) with item names and choices read from the firmware source (u64_config.cc, c1541.cc, c64.cc, network_*.cc, userinterface.cc). Gideon's app was NOT walked in the emulator (Alex: skip). Our app was walked in ue2emu (app = the one inside trx64u.cor, commit d0d86101, screenshots in ours/). "In our app" is read from that run and from coreapp/cores/trx64u/menu.toml.

Hardware basis (what the rows below assume): ITU CAPS 25E40202 = 1541 drive 1 (bit 1), cartridge (bit 9), command interface/UCI (bit 18), sampler (21), EEPROM (22), USB host 2 (23), Ethernet RMII (24), Ultimate64 (26), FPGA type 2. NOT set: second drive (2), hardware IEC (5), C2N streamer/recorder (7, 8), RAM expansion (10), MM drive (11), RTC (12, 13), stereo SID (17), ACIA (27). Descriptor capabilities of the TRX2 core (coreapp/cores/c64.cc): iec drive_a cartridge reu ring uci sid_sockets; at runtime the board strikes "cartridge" (log: "struck by the board: cartridge"). Drive is 1541 only (ue2emu: "1541-only drives"). Every row in the main table is "on our hardware: yes"; rows where hardware says no are in the list at the end.

Rows: 246 in the main table. In our app: 36 yes, 16 partial, 192 no (missing). Total rows incl. hardware-no list: see end.

Target notation: Fn = area key, /k = tab number from NAVIGATION.md (F1 Home 1; F3 Library 1 Browse 2 Search 3 Manage; F5 C64 1 Summary 2 Machine 3 Drives 4 I/O; F7 Actions 1 Quick 2 C64 3 Timewarp 4 Library; F2 Timewarp 1; F4 Tools 1 Overview 2 Monitor 3 Diagnostics 4 Apps 5 Streams; F6 Settings 1 TRX64 2 Video 3 Audio 4 Input 5 Network; F8 System 1 Status 2 I/O 3 Expert 4 Page). Where docs are silent the target says "proposal". Providers: base = coreapp base/menu.toml/widgets; lib = T104.1 library (cbm1541, c64_cart, cbm_run); app = T104.3 application; "code exists" = source in ~/Development/TRX64-Ultimate to be copied and adapted (Alex 2026-10-28).

Documentation gaps found against UI-COVERAGE/gideon-menu-tree: (1) UI-COVERAGE places "Drives and peripherals", "System and expert" under Settings but NAVIGATION.md gives Settings only tabs 1-5 (TRX64 Video Audio Input Network) and System tabs Status/I-O/Expert/Page: so drive settings and the expert pages have no numbered Settings tab; I used F5/3 and F8/3 and F8/4. (2) The doc 13.5 counts items but does not list their values; values here are from source. (3) Doc says F-key mapping per UI style; our UI has one grammar (NAVIGATION), so "User Interface" (Automatic/Ultimate/Commodore) and "Navigation Style" have no target (see decisions).

### F1 Home

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 1 | Home / status | C64 running or paused | Running / Held | F1/1 status row | yes: F1 header chip RUNNING | base |
| 2 | Home / status | Drive A image + mode | image name, mode | F1/1 status row | yes: F1 Drive A row (No image, EMPTY) | base + lib cbm1541 |
| 3 | Home / status | Timewarp state | off / recording / N/A | F1/1 status row | partial: row shows "N/A, Not built yet" | missing (ring engine) |
| 4 | Home / status | Network state | link / no link, IP | F1/1 status row | partial: row shows "no link" fixed text | missing (network state op) |
| 5 | Home / status | Cartridge / tape active | name | F1/1 status row (proposal: add Cartridge row) | no | missing (state value from c64_cart.cc exists) |
| 6 | Home / status | Unsaved changes, warnings, running jobs | list | F1/1 status row / header chip | partial: SAVED chip only on F6 | base (job widget) + missing |
| 7 | Home / quick | Open Library | button | F1/1 head | yes | base |
| 8 | Home / quick | Pause / Resume | toggle | F1/1 session; F5/1 head | partial: button drawn greyed ("held while this menu is open") | missing (REST machine:pause op; Freeze holds the machine already) |
| 9 | Home / quick | Machine & Media | goto F5 | F1/1 session | yes | base |
| 10 | Home / quick | F7 Quick Actions | goto F7 | F1/1 foot | yes | base |
| 11 | Home / quick | Recent and frequent actions | list | F1/1 (proposal: lower half of the status column) | no | missing |

### F7 Actions (quick sheet and full catalogue)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 12 | Action Menu / C64 Machine | Reset C64 | action | F7/1 Quick | yes: "Reset" (machine.cold_reset) | base |
| 13 | Action Menu / C64 Machine | Reboot C64 | action (reload ROMs, restart) | F7/1 Quick | yes: "Reload ROMs" | base |
| 14 | Action Menu / C64 Machine | Reboot (Clr Mem) | action | F7/2 C64 | no | missing (op: clear RAM + reset via dma_win) |
| 15 | Action Menu / C64 Machine | Power Cycle | action (confirm) | F7/2 C64 | no | missing (board power register; host-app has none in coreapp) |
| 16 | Action Menu / C64 Machine | Power OFF | action (confirm) | F7/2 C64 | no | missing (same) |
| 17 | Action Menu / C64 Machine | Pause / Resume | action (REST-only in Gideon) | F5/2 Machine + F1 | no | missing (op over C64_STOP; hw.cc has it) |
| 18 | Action Menu / C64 Machine | Save C64 Memory | path | F5/2 Machine -> Save state; F7/2 | no | missing (op: write 64 KB through dma_win to a file) |
| 19 | Action Menu / C64 Machine | Save REU Memory | path | F5/2 Machine -> Save state | no | missing (op over the DDR2 REU window) |
| 20 | Action Menu / C64 Machine | Save Cartridge | path | F5/2 Machine -> Save state | no | missing (c64_cart.cc keeps the image; op to write) |
| 21 | Action Menu / C64 Machine | Timewarp (task) | opens timeline | F7/3 Timewarp -> F2/1 | no | BIG: ring engine and timeline UI |
| 22 | Action Menu / Configuration | Save to Flash / Save to File | action | F6 bar (every settings page) | partial: "Save to flash" drawn, disabled (writes go to .cfg at once) | base |
| 23 | Action Menu / Configuration | Reset from Flash | action | F6 bar | yes: "Reset from flash" | base |
| 24 | Action Menu / Configuration | Reset to Defaults | action | F6 bar / F8/3 Expert | yes: F8/3 "Reset to host defaults" | base |
| 25 | Action Menu / Configuration | Clear Flash Config | action (confirm: names what is lost) | F8/3 Expert | no | missing (op: delete the core .cfg) |
| 26 | Action Menu / Create | Directory | name | F7/4 Library, F3/3 Manage | no | missing (op over FileManager) |
| 27 | Action Menu / Create | D64 / G64 | name, folder | F7/4 Library, F3/3 Manage | yes: "Create blank D64", "Create blank G64" | base + lib cbm1541 |
| 28 | Action Menu / Developer | Debug Log: show / Clear / Save | C=L shows | F4/3 Diagnostics | no | missing (needs log ring in coreapp) |
| 29 | Action Menu / Developer | Save EDID to file | path | F4/3 Diagnostics | no | missing (U64 EDID read) |
| 30 | Action Menu / Developer | Machine Code Monitor | full screen | F4/2 Monitor | no | BIG: monitor |
| 31 | Action Menu / Streams | VIC Stream Start/Stop | toggle | F4/5 Streams | no | missing (data_streamer.cc exists in host-app/Makefile? not wired) |
| 32 | Action Menu / Streams | Audio Stream Start/Stop | toggle | F4/5 Streams | no | missing (same) |
| 33 | Action Menu / Streams | Debug Stream Start/Stop | toggle | F4/5 Streams | no | missing (same) |
| 34 | Action Menu | Category with one action runs it without submenu | rule | F7 tabs (a tab with one row) | n/a (our tabs are data from menu.toml) | base |
| 35 | Action Menu | Assembly 64 / CommoServe: New Search (F6) | form | F3/2 Search | no | BIG: search client |
| 36 | Action Menu | Remove Cartridge | action | F5/2 Machine (cartridge row); F7/2 | yes: task "Remove Cartridge" (group quick, not visible in F7 tabs) | base + lib c64_cart |
| 37 | Action Menu | Cartridge changed. Save it to ...? (popup when the menu opens) | Off / Ask / Auto | popup, setting in F6/1 | no | missing (hook on menu enter) |

### F3 Library

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 38 | Browser root | SD card | mount | F3/1 source chip | yes | base |
| 39 | Browser root | USB sticks | mounts | F3/1 source chip | partial: chip appears only with a stick (not seen without one) | base |
| 40 | Browser root | Flash | mount | F3/1 source chip | yes: chip FLASH | base |
| 41 | Browser root | Network interfaces and FTP hosts | mounts | F3/1 source chip | no | missing (FTP client filesystem; filesystem_ftp.cc not built) |
| 42 | Browser root | FTP host: New / Edit / Remove | form | F3/1 action sheet on host | no | missing |
| 43 | Browser root | Wi-Fi: Show APs, Connect, Connect to.., Forget APs, Disconnect, Enable, Disable | sheet on network entry | F3/1 sheet + F6/5 Network | no | BIG: ESP32 Wi-Fi manager |
| 44 | Browser | Recent files | list | F3/1 chip RECENT (UI-COVERAGE: Library sources) | no | missing |
| 45 | Browser | Favorites | list | F3/1 chip FAVORITES (UI-COVERAGE) | no | missing |
| 46 | Browser | Search (local and Assembly64) | text, ? | F3/2 Search | no | missing local (FileManager walk); BIG for Assembly64 |
| 47 | Browser | Quick-seek: type a name, ? as wildcard | typing | F3/1 | no | missing (fastseek.cc exists in src, not bound to the list) |
| 48 | Browser | Home directory: HOME, C=HOME sets it, Enter Home on Startup | key + setting | F3/1 + F8/3 Expert | no | missing |
| 49 | Browser | Path picker "Select Path" | dialog | modal layer (Copy, Move, Save) | partial: pick_dir used by Create blank D64/G64 | base |
| 50 | Browser | Multi-select: Space, C=A, C=N | keys | F3/3 Manage | no | missing |
| 51 | Browser | Clipboard: C=C copy, C=V paste | keys, progress, error question | F3/3 Manage | no | missing |
| 52 | Browser | Delete selection (INST) with confirm and progress | key | F3/3 Manage | no | missing |
| 53 | Context | Enter (directory / container) | action | F3/1 sheet | yes | base |
| 54 | Context | View (text viewer) | up to 256 KiB | F3/1 sheet; F4/3 viewers | no | missing (viewer widget) |
| 55 | Context | Hex View | up to 256 KiB | F3/1 sheet; F4/3 viewers | no | missing |
| 56 | Context | Copy to... | path picker | F3/3 Manage | no | missing |
| 57 | Context | Move to... | path picker | F3/3 Manage | no | missing |
| 58 | Context | Rename | text | F3/3 Manage | no | missing |
| 59 | Context | Delete (only writable paths) | confirm names file | F3/3 Manage | no | missing |
| 60 | Context | Run with App (/Flash/apps/<ext>.prg and U2A registry actions) | app | F3/1 sheet; F4/4 Apps | no | app (T104.3 registry) missing |
| 61 | File type PRG / P00 | Run PRG | action | F3/1 sheet | yes | base + lib cbm_run |
| 62 | File type PRG / P00 | Load PRG and start from | address | F3/1 sheet | yes | base |
| 63 | File type PRG | DMA (load without run) | action | F3/1 sheet | no | missing (c64.load_prg exists; bind without start) |
| 64 | File type D64 | Run Disk | action | F3/1 sheet | yes | base + lib cbm1541 |
| 65 | File type D64 | Mount Disk | action | F3/1 sheet | yes | base + lib |
| 66 | File type D64 | Mount Disk Read Only | action | F3/1 sheet | yes | base + lib |
| 67 | File type D64 | Mount Disk Unlinked | action | F3/1 sheet | yes | base + lib |
| 68 | File type D64 | Browse Disk -> Mount & Run / Real Run | inner PRG | F3/1 sheet | yes | base + lib |
| 69 | File type G64 | Run / Mount / Read Only / Unlinked | 4 actions | F3/1 sheet | yes | base + lib |
| 70 | File type CRT | Run Cartridge | action | F3/1 sheet | yes | base + lib c64_cart |
| 71 | File type SID/MUS/STR | Play Main Tune, Show Info, Play Song n | actions | F3/1 sheet | no | BIG: SID player |
| 72 | File type REU/MOD | Load into REU, Preload on Startup, Play MOD | actions | F3/1 sheet | no | missing (REU load op); Play MOD BIG |
| 73 | File type T64 | Enter | container | F3/1 sheet | no | missing (T64 reader) |
| 74 | File type CFG | Load Settings | action | F3/1 sheet | no | missing |
| 75 | File type VPL | Apply Palette, Set as Default | actions | F3/1 sheet (palette lives in F6/2 Video) | no | missing |
| 76 | File type U2A | Run App | action | F3/1 sheet; F4/4 Apps | no | app loader (T104.3) missing in coreapp |
| 77 | File type CFW/U2P | Run Update | action | F3/1 sheet; F8/1 | partial: ".cfw Run Update" listed, op interface only (T104.2 D9) | base (interface) + missing |
| 78 | File type BIN/ROM/64C | Set as Char / Kernal / Basic ROM, Load Kernal | by size | F3/1 sheet | no | missing |
| 79 | File type BIN/ROM | Set as 1541 ROM | 16 KB | F3/1 sheet | yes: ".rom Set as 1541 ROM" | base + lib |
| 80 | File type .c64re | Load Timewarp dump (gap in Gideon: REST only) | action | F3/1 sheet; F2/1 | no | BIG: ring engine |
| 81 | App manifest actions | softboot (cfw, ue2, u64), unzip (3), Dump Cartridge, Cart Inspector, Flash Cartridge, trxmon, Save .C64RE Dump | from manifests | F4/4 Apps and F3/1 sheet | no | app (T104.3) missing; cartridge tools BIG |

### F5 C64 (Summary 1, Machine 2, Drives 3, I/O 4)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 82 | Concept C64 | Summary: program, Drive A, cartridge, REU, machine panel | status rows | F5/1 | partial: Program, Drive A, Core/Network/Media only | base |
| 83 | Concept C64 | Machine: reset, pause, save state, power | page | F5/2 | no (tab absent) | missing |
| 84 | Concept C64 | I/O: cartridge, REU, UCI, SID sockets state and actions | page | F5/4 | no (tab absent) | missing |
| 85 | Drive A actions | Turn On / Turn Off | toggle | F5/3 (setting "Drive" + side button) | partial: setting "Drive" Enabled/Disabled | base + lib |
| 86 | Drive A actions | Reset drive | action | F5/3, F7/1 | yes | base + lib |
| 87 | Drive A actions | Remove Disk / Eject image | action | F5/3 | yes: "Eject image" | base + lib |
| 88 | Drive A actions | Insert Blank | action | F5/3 | no | missing (lib has drive.create; op to insert) |
| 89 | Drive A actions | Save as Binary (D64) | path | F7/2 C64 | yes: "Save Disk as D64" | base + lib |
| 90 | Drive A actions | Save as GCR (G64) | path | F7/2 C64 | yes: "Save Disk as G64" | base + lib |
| 91 | Drive A actions | Swap Disk (long menu press, Ctrl-D, REST) | action | F5/3 | no | missing (swap delay setting exists) |
| 92 | Drive A actions | Unlink | action (REST only) | F5/3 | no | missing |
| 93 | Drive A actions | Set drive ROM from file | 16/32 KB | F3/1 sheet | yes | base + lib |
| 94 | Drive A settings | Drive | Disabled / Enabled (default Enabled) | F5/3 | yes | base + lib |
| 95 | Drive A settings | Drive Bus ID | 8..11 (default 8) | F5/3 | yes | base + lib |
| 96 | Drive A settings | ROM for 1541 mode | file list (default 1541.rom) | F5/3 | no | missing (core_setting with file list) |
| 97 | Drive A settings | Extra RAM | Disabled / Enabled | F5/3 | yes | base + lib |
| 98 | Drive A settings | Disk swap delay | 100 ms .. 1000 ms (step 100) | F5/3 | yes | base + lib |
| 99 | Drive A settings | Resets when C64 resets | No / Yes (default Yes) | F5/3 | no | missing |
| 100 | Drive A settings | Freezes in menu | No / Yes (default Yes) | F5/3 | no | missing (our Freeze is the global F6 setting) |
| 101 | Drive A settings | GCR Save Align Tracks | No / Yes (default Yes) | F5/3 | no | missing |
| 102 | Drive A settings | Leave Menu on Mount | No / Yes (default Yes) | F5/3 | no | missing (closes_menu is fixed in menu.toml) |
| 103 | Drive A settings | D64 Geos Copy Protection | none / Gaps / Track 36 / Both | F5/3 | no | missing |
| 104 | Drive A settings | Track Twist | 0..40 (x100), default 27 | F5/3 | no | missing |
| 105 | Drive A settings | Parallel Cable to Drive A (SpeedDOS) | Disabled / Enabled | F5/3 (UI-COVERAGE: Machine Tweaks in F8/3; proposal F5/3) | no | missing (U64 register) |
| 106 | Cartridge | Cartridge (selected file) | file list | F5/4 I/O | partial: via Library Run Cartridge | base + lib |
| 107 | Cartridge | Save Changed Cartridge | Off / Ask / Auto (default Ask) | F6/1 TRX64 (section 13.6 target "Cartridge-Writeback") | no | missing |
| 108 | Cartridge | Cartridge Preference | 4 values (cartmodes) | F5/4 I/O | no | missing |
| 109 | Cartridge | Bus Operation Mode | 5 values (bus_modes) | F8/3 Expert | no | missing |
| 110 | Cartridge | Bus Sharing ROMs / I/O1 / I/O2 / Interrupts | Internal / External / Both (default Both) | F8/3 Expert | no | missing |
| 111 | Cartridge | GAME/EXROM | On / Off | F8/3 Expert | no | missing |
| 112 | Memory | Fast Reset | Disabled / Enabled | F5/2 Machine (proposal) | no | missing (fastresetPatch in Gideon) |
| 113 | Memory | Kernal ROM / Basic ROM / Char ROM | file lists (kernal.bin, basic.bin, chars.bin) | F5/2 Machine (proposal); concept: F8/3 | partial: ROMs fixed by name, shown in F8/1 Status | base + lib |
| 114 | Memory | RAM Expansion Unit | Disabled / Enabled / GeoRAM Mode | F5/4 I/O | no | missing (REU in DDR2, 16 MB) |
| 115 | Memory | REU Size | 8 values (default index 4) | F5/4 I/O | no | missing |
| 116 | Memory | REU Preload | Disabled / Enabled | F5/4 I/O | no | missing |
| 117 | Memory | REU Preload Image | path (default /Usb0/preload.reu) | F5/4 I/O | no | missing |
| 118 | Memory | REU Preload Offset | 9 values | F5/4 I/O | no | missing |
| 119 | Memory | Map Ultimate Audio $DF20-DFFF | Disabled / Enabled | F5/4 I/O | no | missing |
| 120 | Memory | DMA Load Mimics ID | 8..31 (default 8) | F5/2 Machine (proposal) | no | missing |
| 121 | Memory | Command Interface (UCI) | Disabled / Enabled | F5/4 I/O | no | missing |

### F2 Timewarp

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 122 | Timewarp | Timeline with frame preview | visual | F2/1 | no (area absent) | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 123 | Timewarp | Scrub, play, pause, rewind | keys | F2/1 | no | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 124 | Timewarp | Markers and checkpoints | list | F2/1 / F7/3 | no | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 125 | Timewarp | Start / stop the ring (REST timewarp:start, stop) | toggle | F2/1 | no | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 126 | Timewarp | Dump, ringdump, load .c64re | files | F2/1, F3/1 sheet | no | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 127 | Timewarp | Save / load state | files | F2/1 | no | code exists (TRX64-Ultimate trx_ring.cc, trx_timewarp_ui.cc): adapt |
| 128 | Timewarp | Ring settings (6 s after boot, no store today) | Off / On, depth | F6/1 TRX64 ("Ring und Timewarp") | no | missing (store to define) + code exists |

### F4 Tools

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 129 | Tools | Overview | page | F4/1 | no (area absent) | missing |
| 130 | Tools | Machine Code Monitor (C=O) | 80-column terminal | F4/2 | no | code exists: apps/trxmon (this repo vendor/gideon/apps/trxmon), terminal widget T104.3 |
| 131 | Tools | trxmon RPC 4312 (WebSocket) | service | F4/2 | no | app trxmon |
| 132 | Tools | Monitor Bookmarks | 11 items | F4/2 / F8/3 | no | missing |
| 133 | Tools | Trace | view | F4/3 | no | code exists (trace in TRX64-Ultimate); adapt |
| 134 | Tools | Text viewer, Hex viewer, Debug log, EDID | viewers | F4/3 | no | missing (viewer widgets) |
| 135 | Tools | System info (F4 in Gideon) | text | F8/1 Status + F4/3 | partial: F8/1 shows Application (name blank), ROMs, Libraries | base |
| 136 | Tools | Heap, debug registers (REST machine:heap, debugreg) | probes | F4/3 | no | missing |
| 137 | Tools | Read/write memory (REST machine:readmem, writemem) | monitor | F4/2 | no | part of monitor |
| 138 | Tools | Installed apps list and run | U2A | F4/4 | no | app loader T104.3 missing |
| 139 | Tools | Streams: VIC / Audio / Debug start-stop | toggles | F4/5 | no | missing (data_streamer) |
| 140 | Tools | Cartridge Dumper / Inspector / Flash Cartridge (trex_crt) | apps | F4/4 Apps | no | BIG: vendor/gideon/apps/trex_crt needs a new UI |

### F6 Settings

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 141 | TRX64 | Freeze mode | Whole machine / CPU only | F6/1 | yes | base |
| 142 | TRX64 | Overlay Transparency | 0 / 25 / 50 / 75 % | F6/1 | no | missing (our own pixel layer alpha) |
| 143 | TRX64 | Splash | On / Off | F6/1 | no | missing |
| 144 | TRX64 | Freeze and Pause behaviour | values | F6/1 | partial: Freeze mode only | base |
| 145 | TRX64 | Monitor and Trace settings | values | F6/1 | no | missing |
| 146 | TRX64 | Turbo (common values) | CPU Speed | F6/1 curated + F8/3 full | no | missing |
| 147 | Video | System Mode | PAL / NTSC / PAL-60 / NTSC-50 / PAL-60/L / NTSC-50/L | F6/2 | no (tab absent) | missing (U64 register write) |
| 148 | Video | HDMI Scan Resolution | SD (480p/576p), HD (720p), FullHD (1080p) ... | F6/2 | no | missing |
| 149 | Video | HDMI Scan lines | Disabled / Enabled | F6/2 | no | missing |
| 150 | Video | Palette Definition | file list (.vpl) | F6/2 | no | missing |
| 151 | Video | Analog Video Mode | CVBS + SVideo / RGB | F6/2 | no | missing |
| 152 | Video | Digital Video Mode | Auto / HDMI / DVI | F6/2 | no | missing |
| 153 | Video | Adjust Color Clock (no menu path) | -100..100 ppm | F6/2 (hidden group: proposal Hardware) | no | missing |

### F6 Settings: Audio and SID (tab 3)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 154 | Audio Mixer | Vol Master | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 155 | Audio Mixer | Vol UltiSid 1 | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 156 | Audio Mixer | Vol UltiSid 2 | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 157 | Audio Mixer | Vol Socket 1 | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 158 | Audio Mixer | Vol Socket 2 | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 159 | Audio Mixer | Vol Sampler L | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 160 | Audio Mixer | Vol Sampler R | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 161 | Audio Mixer | Vol Drive 1 | OFF, -42 dB .. +6 dB (31 steps; default 0 dB; drive default 11) | F6/3 | no | missing (U64 mixer registers, hot change) |
| 162 | Audio Mixer | Pan UltiSID 1 | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 163 | Audio Mixer | Pan UltiSID 2 | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 164 | Audio Mixer | Pan Socket 1 | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 165 | Audio Mixer | Pan Socket 2 | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 166 | Audio Mixer | Pan Sampler L | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 167 | Audio Mixer | Pan Sampler R | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 168 | Audio Mixer | Pan Drive 1 | Left 5 .. Center .. Right 5 (11 steps) | F6/3 | no | missing |
| 169 | Speaker Mixer | Speaker Enable | Disabled/Enabled or volume list | F6/3 | no | missing (U64-II / same board) |
| 170 | Speaker Mixer | Vol UltiSid 1/2, Socket 1/2, Sampler L/R, Drive 1 (speaker copy) | Disabled/Enabled or volume list | F6/3 | no | missing (U64-II / same board) |
| 171 | SID Sockets Configuration | SID Socket 1 / 2 | Disabled / Enabled | F6/3 | no | missing (sid_sockets cap; detect op) |
| 172 | SID Sockets Configuration | SID Detected Socket 1 / 2 | None 6581 8580 FPGASID SwinSID ARMSID ARM2SID SidFx FPGASID-Dukestah PDsid SIDKick-Teensy SIDKick-Pico | F6/3 | no | missing (sid_sockets cap; detect op) |
| 173 | SID Sockets Configuration | SID Socket 1 / 2 1K Ohm Resistor | Off / On | F6/3 | no | missing (sid_sockets cap; detect op) |
| 174 | SID Sockets Configuration | SID Socket 1 / 2 Capacitors | 470 pF / 22 nF | F6/3 | no | missing (sid_sockets cap; detect op) |
| 175 | SID Addressing | SID Socket 1 / 2 Address | 49 base addresses | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 176 | SID Addressing | Ext DualSID Range Split | Off, A5..A9 | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 177 | SID Addressing | UltiSID 1 / 2 Address | 49 base addresses | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 178 | SID Addressing | UltiSID Range Split | Off, 1/2 (A5..A8), 1/4 (3 variants) | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 179 | SID Addressing | Paddle Override | Disabled / Enabled | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 180 | SID Addressing | Auto Address Mirroring | Disabled / Enabled | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 181 | SID Addressing | Visual SID Address Editor | full-screen editor | F6/3 (editor: terminal bridge per UI-COVERAGE) | no | missing |
| 182 | UltiSID Configuration | UltiSID 1 / 2 Filter Curve | 8580 Lo, 8580 Hi, 6581, 6581 Alt, U2 Low, U2 Mid, U2 High | F6/3 | no | missing |
| 183 | UltiSID Configuration | UltiSID 1 / 2 Filter Resonance | Low / High | F6/3 | no | missing |
| 184 | UltiSID Configuration | UltiSID 1 / 2 Combined Waveforms | 6581 / 8580 | F6/3 | no | missing |
| 185 | UltiSID Configuration | UltiSID 1 / 2 Digis Level | Off / Low / Medium / High | F6/3 | no | missing |
| 186 | SID Player Behavior | SID Player Autoconfig / Allow Autoconfig uses UltiSid | Disabled/Enabled; No/Yes | F6/3 | no | missing |
| 187 | SID device pages | FPGASID (25), ARMSID (6), SwinSID (3), PDsid (1), SIDKick (1) | per detected device | F6/3 generated pages | no | missing |

### F6 Settings: Input (tab 4)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 188 | Joystick Settings | Joystick Input | Normal, Swapped, WASD Port 2, WASD Port 1, WASD P2 No KB, WASD P1 No KB | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 189 | Joystick Settings | Paddle Override | Disabled / Enabled | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 190 | Joystick Settings | Mouse Mode | Cursor, Mouse, Mouse + Cursor, Mouse + Wheel | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 191 | Joystick Settings | Mouse Sensitivity | 1..16 (8) | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 192 | Joystick Settings | Mouse Acceleration | Off / Adaptive | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 193 | Joystick Settings | Menu Mouse Navigation | Disabled / Enabled | F6/4 | no | out: keys only (T104.1; Alex 2026-10-29 "menumouse raus"); the driver is told 0 |
| 194 | Joystick Settings | Mouse Wheel Sensitivity | 1..16 (8) | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 195 | Joystick Settings | Mouse Wheel Direction | Normal / Reversed | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 196 | Joystick Settings | USB Mouse, USB Mouse HID Mode, USB Keyboard, USB Keyboard HID Mode | info rows | F6/4 | no | missing (mouse sink exists in lib; settings none) |
| 197 | Hotkeys | C=J swap joystick ports | key | F6/4 help | no | missing |

### F6 Settings: Network (tab 5)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 198 | Ethernet Settings | Use DHCP | Disabled / Enabled | F6/5 | no | missing (host-app builds network_interface.cc; no settings page) |
| 199 | Ethernet Settings | Static IP / Netmask / Gateway / DNS | strings | F6/5 | no | missing (host-app builds network_interface.cc; no settings page) |
| 200 | Ethernet Settings | Status, Active IP address, Interface MAC | info | F6/5 | no | missing (host-app builds network_interface.cc; no settings page) |
| 201 | WiFi Settings | Enable / Disable | function | F6/5 native AP page | no | BIG-ish: ESP32 Wi-Fi manager (code exists in TRX64-Ultimate network_esp32.cc) |
| 202 | WiFi Settings | Connect, Select AP, Enter AP, Forget | functions | F6/5 native AP page | no | BIG-ish: ESP32 Wi-Fi manager (code exists in TRX64-Ultimate network_esp32.cc) |
| 203 | WiFi Settings | Status, SSID, signal, IP, MAC | info | F6/5 native AP page | no | BIG-ish: ESP32 Wi-Fi manager (code exists in TRX64-Ultimate network_esp32.cc) |
| 204 | Network Settings | Host Name | string | F6/5 | no | missing |
| 205 | Network Settings | Unique ID | list | F6/5 | no | missing |
| 206 | Network Settings | Network Password | masked | F6/5 | no | missing |
| 207 | Network Settings | Ultimate Ident / DMA / Telnet Remote Menu / FTP File / Web Remote Control Service | Disabled / Enabled | F6/5 | no | missing |
| 208 | Network Settings | Log to Syslog Server | string | F6/5 | no | missing |
| 209 | Network Settings | SNTP Enable | Disabled / Enabled | F6/5 | no | missing |
| 210 | Network Settings | TimeZone | zone list (default idx 16) | F6/5 | no | missing |
| 211 | Network Settings | Time Server 1 / 2 / 3 | strings | F6/5 | no | missing |
| 212 | Data Streams | Stream VIC to / Audio to / Debug to | strings host:port | F6/5 and F4/5 | no | missing |
| 213 | Data Streams | Debug Stream Mode | 7 values | F6/5 and F4/5 | no | missing |

### F8 System (Status 1, I/O 2, Expert 3, Page 4)

| # | Gideon place | item | values | target in the concept | our app today | what provides it |
|---|---|---|---|---|---|---|
| 214 | System | Status: application, ROMs, libraries | text | F8/1 | partial: shown; Application name is empty (", built ...") | base |
| 215 | System | I/O: hardware, USB devices, SD, flash | list | F8/2 | no (tab absent) | missing |
| 216 | System | Reset to host defaults | action | F8/3 | yes | base |
| 217 | System | Restart core | action | F8/3 | yes | base |
| 218 | System | Page: any dynamic settings page | list | F8/4 | no | missing |
| 219 | User Interface Settings | Interface Type (Freeze / Overlay on HDMI) | 2 values | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 220 | User Interface Settings | Navigation Style | 2 | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 221 | User Interface Settings | Color Scheme | 7 | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 222 | User Interface Settings | Enter Home on Startup | Disabled / Enabled | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 223 | User Interface Settings | Home Directory | string | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 224 | User Interface Settings | Auto Save Config | 3 values (default 1) | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 225 | User Interface Settings | Filename overflow squeeze | 4 values | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 226 | User Interface Settings | Temp Auto Cleanup | Disabled / Enabled | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 227 | User Interface Settings | Temp Subfolders | Disabled / Enabled | F8/3 (Interface Type: proposal, our UI has one surface) | no | missing |
| 228 | Turbo Settings | Turbo Control | Off, Manual, U64 Turbo Registers, TurboEnable Bit | F6/1 curated (speed) + F8/3 | no | missing (feature turbo in bitstream) |
| 229 | Turbo Settings | CPU Speed | 1,2,3,4,6,8,10,12,14,16,20,24,32,40,48,64 MHz | F6/1 curated (speed) + F8/3 | no | missing (feature turbo in bitstream) |
| 230 | Turbo Settings | Badline Timing | Disabled / Enabled | F6/1 curated (speed) + F8/3 | no | missing (feature turbo in bitstream) |
| 231 | Turbo Settings | SuperCPU Detect (D0BC) | Disabled / Enabled | F6/1 curated (speed) + F8/3 | no | missing (feature turbo in bitstream) |
| 232 | Machine Tweaks | C64U Model | BASIC Beige, Starlight, Founders (greyed in Gideon, set by boot key) | F8/3 | no | missing |
| 233 | Machine Tweaks | Burst Mode Patch | Off / CIA1 / CIA2 | F8/3 | no | missing |
| 234 | Machine Tweaks | Serial Bus Mode | All Connected, C64 <-> Internal, Ext. <-> Int., C64 <-> External | F8/3 | no | missing |
| 235 | Power Settings | Power On After Power Loss | Off / On / Last State | F8/3 | no | missing |
| 236 | Power Settings | Wake On Wi-Fi | Disabled / Enabled | F8/3 | no | missing |
| 237 | LED Lighting | Power LED Output 1 / 2 | 16 selections (On, Off, Drive A Pwr, ... IRQ Line) | F8/3 | no | missing |
| 238 | LED Lighting | LED Strip Settings | hidden store | F8/3 | no | missing |
| 239 | LED Lighting | Keyboard Lighting | hidden store (C64U Bling board) | F8/3 | no | missing |
| 240 | C64U specific store | 33 hardware items (hidden) | various | F8/3 -> Hardware group (proposal F8/2 I/O) | no | missing |
| 241 | No menu path | UserPort Power Enable | Disabled / Enabled | F8/2 I/O (proposal) | no | missing |
| 242 | Keys | C=R reset, C=Z reboot, C=B power cycle, C=K power off (every menu) | hotkeys | F8 help page + F7 | no | missing |
| 243 | Keys | Menu button long press: swap disk; F10 / Scroll Lock opens menu; Ctrl-D swap | gestures | F8 help page | no | missing (rocker events exist) |
| 244 | Keys | Boot hotkeys Ctrl-P/N (PAL/NTSC), Ctrl-B/S/F (model) | hold at power on | F8 help page | n/a (firmware input) | Gideon boot code |
| 245 | Keys | Help text (F3 / F7 in Gideon) | text | F8 help page (proposal F8/1) | partial: footer hints only | base |
| 246 | Persistence | Auto Save Config | Off / Ask / Auto | F6 bar | no | missing |

## Rail / content mismatches and tab gaps in our UI, as seen in ue2emu (screenshots in ours/)

- Rail lists 6 areas: HOME F1, LIBRARY F3, C64 F5, ACTIONS F7 (group DIRECT) and SETTINGS F6, SYSTEM F8 (group SHIFTED). Concept has 8. Missing: Timewarp F2 and Tools F4. Pressing F2 or F4 does nothing (screen stays on Home / Library, identical PNG).
- Tab gaps (a number that is not shown and ignored when pressed): Library 2 Search, 3 Manage; C64 2 Machine, 4 I/O (C64 shows "1 SUMMARY, 3 DRIVES"); Actions 3 Timewarp (shows "1 QUICK, 2 C64, 4 LIBRARY"); Tools all; Settings 2-5 (only "1 TRX64"); System 2 I/O, 4 Page (shows "1 STATUS, 3 EXPERT"); Timewarp 1.
- Home status row "Timewarp: Not built yet / N/A" points at a function the rail has no area for.
- Home and C64 Summary "Pause" button is drawn but off ("The machine is held while this menu is open").
- F6 Settings has one row (Freeze mode), a "Save to flash" button that is always disabled.
- F8 Status: "Application" reads ", built 2026-10-28 18:23" (name empty); Core reads "TRX64U ? (TRX2)" (version shows "?").
- F7/4 Library and F7/2 C64 hold two rows each (create/save D64/G64); "Remove Cartridge" is a task in group "quick" but is not on the Quick tab (Quick shows Reset, Reload ROMs, Reset Drive).
- Library lists only SD and FLASH roots; no recent, favorites, search, manage.
- Footer says "1-5 TAB" while no area has more than 3 tabs.
- Our footer/hint line and the concept's: R/S CLOSE present; left-arrow back not verified.

## Open decisions for Alex (one line each)

1. User Interface (Automatic/Ultimate/Commodore), Navigation Style and Interface Type have no meaning in one UI grammar: drop them? (proposal: drop, record as deliberate exclusion)
2. Where do Drive settings and the expert pages live: F5/3 and F8/3-4 as built, or add Settings tabs 6+ (NAVIGATION gives Settings 1-5 only)?
3. "cartridge" is struck by the board at run time but Run Cartridge and Remove Cartridge exist: confirm the cartridge rows (Run, Save Changed Cartridge, Bus modes, GAME/EXROM) are targets, and whether the physical-port bus rows stay.
4. Power Cycle / Power Off: the board's power control is not in coreapp; build it as a host service op, or leave to the hardware button?
5. Where are Gideon's settings stored: one core .cfg per group (T148 folder) means Video/Audio/SID writes are U64 register writes from coreapp; confirm coreapp may write those registers (host-owned code).
6. Pause/Resume: Freeze already holds the machine under the menu; is a separate Pause (menu closed, machine held) wanted?
7. D71/G71/D81/DNP creation and the 1571/1581 mode: excluded because the drive is 1541 only: confirm.
8. Tape, Printer, Modem, SoftIEC, UltiCopy, 2nd drive, MP3 ramdrive: excluded by CAPS (bits absent): confirm.
9. Cartridge Dumper/Writer (trex_crt) needs its own UI: design first.
10. Timewarp ring settings (start delay, depth) and Splash/Monitor/Trace/Turbo settings have no store today: names and choices to be decided (T24 6.6 list only).
11. Wi-Fi/Assembly64 need the network stack in coreapp; the host-app Makefile builds network_interface.cc, but no link state exists in the UI: is network in scope for the first full build?

## Rows that do not exist on our hardware (not build targets)

| Gideon row | reason (hardware / descriptor) |
|---|---|
| Drive B (all rows: settings, actions, Mount on B, Drive 2 vol/pan, LED Drive B, DrvB LED selections) | CAPS bit 2 (second 1541) not set; descriptor has drive_a only |
| Switch to 1571 / 1581, Drive Type, ROM for 1571 / 1581 mode, 1581 drive page | drive is 1541 only |
| D71, G71, D81, D81 81 tracks, DNP: create, mount, run; File type D71/D81/DNP/G71 | no drive can mount them |
| Load into MP3 Drv A-D, Save MP3 Ramdrives, MM drive | CAPS bit 11 not set |
| Tape: Pause/Resume/Stop, Sample tape, Capture save, Finish rec., Tape Settings, TAP file type (Run/Start/Write/index), Vol Tape Read/Write, Tape pan | CAPS bits 7, 8 not set |
| Software IEC: On/Off/Reset, partitions (Show, Set here, Save, Add/Edit/Remove, IPR), SoftIEC Drive Settings | CAPS bit 5 (hardware IEC) not set |
| UltiCopy Drive 8-11, Ulticopy Uses disk name | needs hardware IEC (bit 5) |
| Printer: Flush/Eject, Reset, On/Off, Printer Settings (11 items) | software IEC device, bit 5 |
| Modem Settings (16 items) | CAPS bit 27 (ACIA) not set |
| Clock Settings | no RTC (bits 12, 13); Gideon's own u64ii build uses rtc_dummy |
| Stereo SID extras of the old 1541ultimate (Ext DualSID on stereo hardware) | bit 17 not set (sockets and UltiSID remain) |
| Speaker Enable / Speaker Volume (SpkDat) | U64 (first board) only; not our board |
| C64 and Cartridge timing items (CPU Addr valid after PHI1/PHI2, Force 2 MHz, Kernal Shadow RAM, PHI2 edge recovery) | only built for CLOCK_FREQ 62.5/50 MHz; ours is 100 MHz |
| Pause / Resume / Measure Cart Bus as menu rows in Gideon; VIC Test Colors; Poke, Detect SIDs, Disable/Enable ESP32, Enable ESP32 Boot; Write EEPROM | DEVELOPER build only |
| Copy to Flash (CRT) | writes Gideon's flash cartridge slot, not part of our flash layout (monolith: struck cartridge) |
| Telnet remote menu as third host | no second UI surface in our grammar (decision 1) |
| Extended REU sizes above 16 MB | REU is the 16 MB DDR2 window |
