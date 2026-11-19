# Alex's requirements for the generic app (consolidated 2026-11-12)

Sources: all of Alex's chat messages 2026-09-13..11-12 (TRX64U-MultiCore, TRX64-Ultimate, C64U-TRX-UI, 1541ultimate sessions), specs, TODO and commit messages, plus the D-lines and Alex decisions in
docs/specs/done/T104.1-contribution-model.md, T104.2-generic-app-on-trx64-ultimate.md, T93-core-app-blueprint.md.
Dates earliest first. Quotes are Alex's own (German); where a spec only keeps a translation, the English quote is marked (tr.).
"replaces" = an earlier decision of Alex that the current one overrides.

## A Architecture

**R1** Three layers: (a) bitstream API per core -> (b) service with modules -> (c) client (UI). (a) differs per core but uses one function language and naming; (b) is strongly generalised so the UI works only from declarations of bitstream + service; the UI never names a core.
2026-10-18, 2026-10-28, 2026-11-07, 2026-11-12 — "(a) wird für jeden Core anders sein, aber sollte die gleiche 'funktionssprache' und benamung haben. (b) sollte stark generalisiert sein, so dass das UI ... nur deklarativ beschrieben funktioniert"

**R2** The .toml is the glue between a bitstream and the app; the app is generic. Core-specific data (strings, info line, ROM names, short name, drive/task lists) lives in the core's .toml, never in C; new per-core C data is refused by a lint gate; existing per-core C (19 cores, table.cc/cores/*.cc) is migration debt.
2026-10-15, 2026-10-28, 2026-11-12 — "Bitstream <-> API zu App .toml == glue zwischen einem Bitstream und der App App = generisch"

**R3** Every capability (hotkeys, Run Disk, media, pad, zip, ...) lives in the generic app; a core only declares in its .toml what it has, so every core can get every capability.
2026-10-15, 2026-11-12 — "Das ist u.a. eine fähigkeit in dern Generic App. Und damit kann JEDER core das haben. Die frage ist nur, was in der .toml definiert ist"

**R4** The service holds C modules (1541/1581, formats, tape, machine modules, drives); the API TOML only chooses and parametrises them, TOML is no programming language (option B).
2026-11-12 — "2 bleibt weiter C, das sind doch MODule in dem Core-Service"

**R5** One app for everything: host app and core app are the same generic app, role and features from configuration.
2026-10-15, 2026-10-18, 2026-11-08 — "Am ende eine generische App die nur mit einer Config läuft"
replaces 2026-10-15 (T104 D4): "2 Binaries auf einer Basis."

**R6** The UI is a client of a headless service; until UI and UX work completely as an API client it is unfinished; the old path and its tests are deleted.
2026-11-07, 2026-11-09 — "bis das UI inkl. der UX nicht komplett wie api Client funktioniert ist es Murks"

**R7** The full API exists from the first build; REST/WS endpoints are derived from the declaration (no handler per function); since every UI action goes through the service, everything incl. update works via API.
2026-11-08, 2026-11-12 — "wenn doch jede UI Aktion durch den Service geht und wir den Service zu 100% exponieren, warum geht das nicht"

**R8** Remote access never bypasses confirmations; a question raised over the API looks the same as a local one (there is exactly one person). Auth: simple token stored in UI and filesystem, no encryption, WebSocket transport; a security spec follows step by step.
2026-11-07, 2026-11-08, 2026-11-10 — "simples Auth mit Token ... Keine Encryption"

**R9** C++ is the core, no Rust. From Gideon only convenience classes (I/O, FS, USB, network, FreeRTOS); his UI, menus, machine classes and file-type handlers are not kept; GPL code may be reused instead of reinvented.
2026-10-15, 2026-11-07 — "von gideon kommen nur die Convenience Klasse ... lvgl als Output technologie - so clean wie möglich"

**R10** Menus are not programmed: they result from (a) settings, (b) the declarative description, (c) libraries, (d) apps. Gideon's app is not the model.
2026-10-13, 2026-10-18 — "sich a) aus den Settings b) Deklarativ c) aus dem Libraries d) den Apps ergeben ... Gideons App ist dabei KEIN vorbild"

**R11** Open world: every contribution of a loaded library or app appears; the description may hide single nodes by id.
2026-10-18 (T104.1 Q3) — "1" (Q3)
replaces 2026-10-13 (T93 D8): the menu is a whitelist from three sources.

**R12** Declarations are TOML compiled at pack time into a binary record (no parsing on the board); static menu frames may be assembled/pre-rendered at build time, only apps render at run time; the packer refuses a node its template cannot draw.
2026-10-18, 2026-10-28, 2026-10-29 — "Nur Apps sollten zur laufzeit rendern"

**R13** Division of labour: graphic elements = LVGL from the concept's XMLs; grid (left rail, tab row, status line, Home) = C++; content = TOML (functions as ABI calls, parameters with values/defaults, order). The widget layer is internal; outside apps change content and operations, not widgets.
2026-10-29 — "Grafik elemente -> lvgl aus den .xmls ... Inhalt -> .toml"

**R14** Core-specific functions go into libraries/modules shared by the cores that need them (1541/GCR for C64/VC-20/C16, mouse, CRT hash matching, miniz/zip, pad), never into the generic app's core-specific code; no per-core code without proven need.
2026-10-15, 2026-10-18, 2026-10-20, 2026-10-29, 2026-11-09 — "kein per-Core Code ohne nachgewiesenen Bedarf; gemeinsames Verhalten lebt in Libraries"

**R15** Each core describes its need; the pipeline builds the core-specific app, linking in what the core always needs; rarely used tools (e.g. TRX-CRT) load on demand from the core folder's apps/.
2026-10-28 — "Jeder Core beschreibt seinen 'Bedarf', die Pipeline baut dann die Corespezifische App" / "im core-folder auch apps/ haben"

**R16** Apps are self-contained containers that hook into UI and menus and bring their UI declaratively; the generic app renders it. An app declares its residence: only while open, or permanently. Apps are only for our cores.
2026-10-18, 2026-10-25, 2026-10-26 — "die App bringt ihr UI deklarativ mit und der Core-Generic-App Teil rendert"

**R17** Library rules: a missing/too old required library or ROM -> the core does not start, error naming it, back to the host; an optional one degrades visibly; a library whose capability the core lacks is refused with a message; trust/integrity model and one deterministic winner for duplicate providers; rescue = holding C= plus a boot-attempt counter (rewritten atomically).
2026-10-18, 2026-10-19, 2026-10-20 — "Nicht starten. Fehler und zurück zum Host."

**R18** Contracts: state and events as atomic immutable snapshots; F-key and digit sequences stable regardless of library load or USB order; operation names and result contract defined once; board capabilities three-valued (present/absent/unknown, unknown is shown).
2026-10-20 — "Yes" (tr., to the list he set)

**R19** Third-party apps later: describe the API and ship an SDK; first a documented generic UI SDK/API/ABI, then a test-suite SDK.
2026-10-26, 2026-10-29 — "Generic UI SDK API und ABI klar beschrieben, dann Test suite SDK"

**R20** Scope: the generic app with service runs on every core of our line, incl. trx64u and c64scpu; Gideon's and Commodore's C64 cores and cores on their bitstream (c64u, trx64g, c64ref) are never touched.
2026-10-18, 2026-10-21, 2026-10-28, 2026-11-09, 2026-11-10 — "Nur trx64 U wird dieses Ui bekommen. Gideon und Commodore fassen wir nicht an"
replaces 2026-10-15: TRX64 core keeps the Gideon-based app; 2026-10-22: "bis auf trx64 und c64u" switch to generic.

**R21** Gideon's vanilla app never runs on our bitstream; only an app layer matching the bitstream may run.
2026-10-07, 2026-10-10 — "Ich will Gideons Vanilla ultimate App NIEMALS mit unserem Bitstream laufen haben"

**R22** A core is self-contained in one folder: bitstream, app incl. libs/modules, config, ROMs, saves, logo; cores share no files (copies, no links/symlinks).
2026-10-21, 2026-10-28, 2026-11-08 — "ein Core sollte self containig sein"

**R23** Input to a running core is just an input device: the core offers its device sets (joystick, mouse, keyboard) via API, derived from core config and key mapping.
2026-11-09 — "Core sollte seine per API anbieten und sein 'set' pro device Joy, mouse, keyboard"

**R24** A text UI / remote client is a further client of the same service, modelled on the trx64-cli TUI and the TRX monitor (80x40 cells); a Mac/Linux client may be a separate project.
2026-11-07 — "Schau die das TUI an von trx64-cli, evtl. kann man das als client-shell kopieren"

## B Navigation & keys

**R25** Keys only: no mouse control of the UI, no joystick navigation (the host is navigated by keyboard).
2026-10-03, 2026-10-04, 2026-10-20, 2026-10-29, 2026-11-11 — "Keine maussteuerung in unsrer App! Nur keys. Die ganze UX geht nur mit Tasten."

**R26** F-keys are the main navigation (F1-F8 areas), digits 1-8 the tabs; default key set = TRX64 grammar; our own key names (Gideon's naming translated at the edge).
2026-10-29, 2026-11-07, 2026-11-12 — "F-Tasten sind unsere Haupt nav Tool"

**R27** Only the F-keys and 1-8 are fixed; all other keys depend on the context and are declared on the UI element in the declaration.
2026-10-29 — "Die direkten Tasten sind vom Kontext abhängig. Das kann ja in der Deklaration an dem UI Element beschrieben werden"

**R28** Keys per context (boot menu, browser, editor, ...), configurable per context/tab; one shared default key table, per-context override via bundle.
2026-11-09, 2026-11-12 — "wir haben doch extra in das generic app framework eingebaut, dass wir tasten pro Kontext belegen"

**R29** The bottom hint line shows the current context's bindings incl. paging (+ -); every list area uses the same foot "UP/DOWN CHOOSE  RETURN DO IT  F1-F8 AREA  ESC BACK"; the line never wraps off-screen.
2026-10-21, 2026-11-11, 2026-11-12 — "Unten in der Zeile stehen dann die Belegungshinweise"

**R30** RUN/STOP and left arrow act as exit/escape; in the Library left arrow goes one folder up.
2026-10-12, 2026-10-28, 2026-10-29, 2026-11-09 — "LARROW sollte wie RUN/STOP eigentlich immer als Exit/Escape fungieren"

**R31** Buttons have cursor navigation plus a framed hotkey letter like the digits ((E)JECT DISK, (R)ESET DRIVE).
2026-10-29 — "die 1 2 3 4 haben einen Rahmen, können das D F S nicht auch haben"

**R32** Moving the cursor onto an option never triggers it; one RETURN confirms.
2026-10-16 — "Einmal return reicht"

**R33** Scrolling: one cursor step = one row, no full redraw, scrolling starts 2 rows before the edge; long lists page with + / - (SHIFT +/- in a core's F3) instead of jump-to-letter; the boot menu pages the same way.
2026-10-23, 2026-11-04, 2026-11-11, 2026-11-12 — "C= + letter does not work ... can we page? With + -?" (tr., T180 D7)

**R34** Key response and scrolling in core apps as fast as in the host app (0.5-1 s delay unacceptable).
2026-10-16, 2026-10-22 — "Scrolling/Cursor Key navigation in den Core Apps ist ultra langsam im Vergleich zur Host app"

**R35** Opening the menu = Freeze: the machine always halts, on every core (rocker like Gideon 1:1); leaving resumes exactly; no overlay mode.
2026-10-13, 2026-10-15, 2026-10-20, 2026-11-09 — "Core Menü auf == Freeze. Dieses Overlay zeugs will ich nicht!"
replaces 2026-10-01/10-03: both HDMI overlay (machine running, default) and Freeze.

**R36** The menu reopens at F1 Home by default; a setting offers "last open screen".
2026-11-09 — "Standard = F1, 'last open screen' ist dann der andere Fall"

**R37** A reset from the menu closes the menu and resets; "Reset" and "Reset to host defaults" do not ask.
2026-10-13, 2026-11-09 — "dann sollte das Menu sich beenden ... und der Reset passieren"

**R38** "Return to Host Menu" is always offered, also from a hung core.
2026-10-26, 2026-10-29 — "man kann auch immer noch nicht vom Core zurück zum Host wenn er hängt"

**R39** Hotkeys control the running core without the menu: menu open/close, volume, reset, swap joy ports, picture mode, plus core ones declared in .toml (e.g. C128 40/80 CTRL+D). Modifier CTRL (not C=), 1 or 2 keys; the core never sees an action key, the modifier alone passes through; per-core default map in the descriptor, changeable per core.
2026-11-04, 2026-11-09, 2026-11-12 — "Menu: CTRL+RESTORE reset = CTRL+R Vol+ = CTRL+ usw"

**R40** Hotkeys are learned by pressing the key; they appear as one "Hotkeys >" row/section with a sub-page (in F7).
2026-11-06, 2026-11-11, 2026-11-12 — "Hotkeys wollten wir 'anlernen'"

**R41** Interface bundles (key set, colours, text mode) are plain TOML: edit, copy to board, restart, see; a broken file never locks out; host sets the default, each core may override; safe mode C=+S resets them. No density choice.
2026-11-07 — "Per Editor in dein Toml rumturnen Auf board kopieren neustarten Ergebnis sehen"

**R42** Search: C= SPACE opens a search overlay that owns all keys while open; indexes settings, pages and actions.
2026-11-07 — "wenn die Suche im 'overlay' über dem Screen ist, dann gehen die Tasten in die Suche - KEINESFALLS ins UI darunter"

**R43** Text entry: cursor in every entry; the last 20-50 entries are kept in a volatile history, cleared on reboot or core change.
2026-11-04, 2026-11-07 — "die letzten 20-50 Eingaben könnte ja in einen flüchtigen Cache"

**R44** Boot menu keys: RETURN starts, RIGHT opens the config overlay; default core autostarts after 5 s, RUN/STOP aborts; editor: SPACE marks, cursor moves, SPACE/RETURN drops, C changes the display name.
2026-10-12, 2026-10-28, 2026-11-10 — "gut wäre wenn der Deftault core nach 5s von alleine startet" / "RunStop sollte das starten abbrechen!"

**R45** File browser keys: SPACE marks/unmarks, an operation applies to all marked (except rename); A adds the file to the boot menu for the right type; H sets home.
2026-10-26, 2026-11-04, 2026-11-11 — "in der App (F3) mit SPACE einfach den Markiere/Demarkiere"

**R46** Monitor keys come from the monitor, never from the UI; the monitor uses the TRX64 monitor verbs, 80 columns.
2026-10-04, 2026-10-28 — "die keys darin kommen aus dem Monitor. Keinesfalls vom UI."

## C Screens & content

**R47** Our own modern UI, nothing like Gideon's pseudo-C64: real resolution, more colours (RGB565, 960x540 canvas), English only, must look professional.
2026-09-30, 2026-10-02, 2026-10-03, 2026-10-28 — "Ich will das das UI professionell wirkt."

**R48** Laid out for 540p, centred, good at every multiple; the same menu looks the same on every core; headers centred; layout derived declaratively/programmatically, not hand-tuned per resolution.
2026-10-22, 2026-10-23, 2026-10-24, 2026-10-30, 2026-11-05 — "Unser Ui ist auf 540p ausgelegt. Das sollte da und in Vielfachen davon immer gut aussehen"
replaces 2026-10-20: "Nein, immer 1080p einfach".

**R49** UX goals: simple consistent structure; insert/change/eject medium, start, monitor, TimeWarp within 1-3 steps; no display and setting of the same thing on one screen; build only what exists (unsupported hidden, currently impossible greyed with reason).
2026-10-13, 2026-10-28 — "relevante Aktionen nur 1-3 schritte weit weg ... kein redundanzen von Anzeige und Setting auf dem selben Screen"

**R50** Areas: F1 Home = quick actions with the cursor (no status box) plus a machine-info line (model/version, memory, chipset, PAL/NTSC); F5 = machine (core) settings incl. Video/Audio/Input; F7 = settings of the menu app and what installs into it (libraries, zip; each registers its own; Hotkeys; no single "Settings" tab); F8 = System (build, ROMs, libraries, reset to host defaults last); F6 gone.
2026-11-06, 2026-11-07, 2026-11-11, 2026-11-12 — "F5: Machine (Core) Settings F7: Menu-App Settings F1: Quick Actions mit den Cursor Tasten statt Status box F8: Bleibt so"
replaces 2026-11-07 (T187 D7): home/persist mounts in F5 Storage; 2026-11-11: "3x settings" with F6.

**R51** The core menu is the same for every core (SCPU too, every analog-out core too) and holds only what fits the machine; reset rows read the same everywhere ("Reset", "Reload <ROM name>, reset").
2026-10-13, 2026-10-31, 2026-11-11, 2026-11-12 — "Weil das menu ist ja jetzt in JEDEM core"

**R52** In F1, RETURN on a drive row opens its action list (empty: mount from Library, create blank per format; mounted: eject, save as, reset drive); each core declares task-to-drive; drive-less tasks have fixed places (cart slot list, tape slot, F5, top of F1).
2026-11-12 — "yes" (tr., T212 D5, D8-D11)

**R53** Status and messages: exact word/details in the status line, cleared when the menu closes; short state (volume, picture mode) as a chip in the top bar next to the core name; nothing is drawn over the running core.
2026-11-06, 2026-11-09 — "Keine overlays über den laufen Core. Oben neben dem Core Namen ist genug Platz für Vol 100%"

**R54** Overlays/progress bars never show what is behind them while redrawing; one class for all (incl. updater/restore).
2026-10-31 — "Beim Zeichnen sieht man dann kurz das, was hinter dem Overlay liegt durch, .. das ist doof"

**R55** Lists: one line per row, tight spacing; the current value carries one "now"/* mark and is preselected; "auto" replaces "(default)" everywhere; settings that need a reset get a pictogram, not a second line.
2026-11-06, 2026-11-10, 2026-11-11, 2026-11-12 — "der aktuell gewählte modus sollte NOW oder ein * haben und beim öffnen bereits selektiert sein"

**R56** Boot menu look: logo left at full size, name, version right, smaller line under the name; no path (the editor shows it smaller under the name); a core without logo gets a text tile with the short name from its .toml; state as pictograms (green usable, yellow check, red unusable) with the exact word in the status line; the slim file list never changes the boot menu; the core list scrolls.
2026-10-16, 2026-11-12 — "In den mocks war links das Logo, der Name und rechts die Version"

**R57** Boot menu behaviour: ships empty, the user builds it; a .cor is checked (SHA256) only when added, never at each start, no "checking cores" wait; no requester before a large core; omit "countdown stopped" when Direct Boot is on; RETURN starts, a saves/config overlay allows "start with config"; our cores ship as .cor, .cfw/.ue2 accepted as foreign cores.
2026-10-12, 2026-10-14, 2026-10-25, 2026-10-26, 2026-10-28, 2026-11-04, 2026-11-10 — "Hash/Scan-Vorgang muss dann NUR noch beim hinzufügen stattfinden"

**R58** Start-up: no "building UI" delay (pre-render screens); a startup bar/splash while loading; core splashes stay; splashes fit at every resolution.
2026-10-02, 2026-10-23, 2026-10-28, 2026-10-29, 2026-11-04 — "Screens rendern und speichern bitte speccen und bau starten, das sind echt nervige UX killer"

**R59** One file browser implementation for host and all cores: rename, delete, copy/move, mkdir, marks, zip as folder and "Unzip here"; activating a medium shows the overview of all media; in the host .ue2/.cfw offer convert to .cor / restore FW backup.
2026-10-13, 2026-10-28, 2026-10-29, 2026-11-04, 2026-11-10 — "wir sollten 1 Implementierung des Dateibrowsers haben ... die verwenden der Host und alle Cores"

**R60** Home directory works the same in every core: set with H in F3 or a folder picker overlay, never typed; "Enter home on startup"; pickers remember the last path.
2026-09-23, 2026-11-06, 2026-11-11 — "das muss in jedem Core gleich sein"

**R61** Picture: the menu offers only modes the display's EDID supports, follows EDID changes, applies with an 8 s keep-confirmation visible in every resolution; resolution row reads "Auto (W x H from Host)".
2026-10-16, 2026-10-31, 2026-11-02, 2026-11-04, 2026-11-06 — "im Menü nichts anzubieten, dass der Monitor/TV nicht kann"

**R62** Volume adjustable in the menu up to 200 %, shown as chip in the top bar.
2026-10-29, 2026-11-06, 2026-11-07 — "SHIFT+ SHIFT- wäre ideal dafür, also im Menu, nicht im laufenden Core"

**R63** Activity: running FTP/filesystem jobs are indicated; case LED as floppy-like load indicator in every core, with Load Indicator on/off and LED colour in the menu; the app serves both boards.
2026-10-03, 2026-10-29, 2026-10-30, 2026-11-01 — "die Gehäuse LED als Floppy indikator ... in alle Cores"

**R64** Updater/maintenance UI (host side): the app updates itself and the host, restores original FW; dialogs with centred header, Cancel back to menu, WARNING / "ARE YOU SURE" / "REALLY?"; asks where backup and cores go (/cores at a medium root); F8 "cor & rom maintenance", inspect cfw/ue2 for ESP32 updates, protect flash; state dump for bug reports.
2026-10-14, 2026-10-28, 2026-10-29, 2026-10-30 — "unsere App muss auch Updates machen können. Da brauchen wir ein UI für."

**R65** The function list must include TimeWarp and Monitor (core-declared, TRX64 only), FTP client with URL and credentials, assembly, app loader.
2026-10-18, 2026-10-28, 2026-10-29 — "Die Liste ist ok, aber 'zu klein'. TimeWarp, und MOnitor müssen auch rein."

**R66** Logging: option in F8 of every app, written to SD/USB with rotation (10 files), only when explicitly enabled.
2026-11-04 — "nur wenn man ihn explizit in den F8 settings anmacht"

**R67** Core display names are set by Alex (e.g. "Amiga 1200"); no garbled subline under the name.
2026-11-11 — "Die ganze subzeile ist doch unnütz. Den Namen vergebe ich ..."

## D Media, drives, ROMs

**R68** The generic app always handles media swap, large cores included; mount on an occupied slot = unmount + mount; disks change without a cold start.
2026-10-20, 2026-10-29, 2026-11-04 — "Spec dass die generic Core app das mit dem Tausch immer macht"

**R69** Every core can create a blank formatted medium like Gideon's app: path selection, progress, name entry, no garbage prefilled name.
2026-10-22, 2026-10-23, 2026-10-24 — "immer die Option, ein medium zu erzeugen so wie in Gideons Core/App"

**R70** "Mount Disk and run" / "Run Disk" (and "mount and reset" where useful) wherever a disk is mounted, same names on every core (PET too); Play on tape closes the menu like load and run.
2026-10-22, 2026-11-04, 2026-11-11, 2026-11-12 — "immer auch 'Mount Disk and run' oder 'Run Disk'"

**R71** Drives: 1541 and 1581 only; no 1571, no drive B, no drive sounds; several drives only where the machine has them.
2026-10-17, 2026-10-22, 2026-10-28 — "1571 ... ruled out von anfang an! Laufwerk B brauchen wir nicht ... Mount & Run IST wichtig"

**R72** Media persistence: option to keep media across reset/restart plus "Reset + eject all media"; "Persist medium mounts" defaults to NO; named configs (full copy of settings + mounts) replace media sets, one-shot start with a config, default config in boot.cfg; mounts restored before core start; unmount/return-to-host finishes lazy writes.
2026-10-28, 2026-11-04, 2026-11-10 — "Ab werk auf NO und muss aktiv gesetzt werden ... eine 'media config' speichern"

**R73** "Restart core" is a cold boot with all lines at default (essential to simulate power-on).
2026-10-23, 2026-10-24 — "für die DFX-Cores sollte der Punkt dann 'reload core' heissen"

**R74** ROMs: search order (1) the .cor's folder (roms or ./roms), (2) the medium's ../roms, (3) /flash, (4) other media; boot default is always stock (never JiffyDOS), alternatives chosen explicitly and written to config; clear placement instructions; kickstart etc. selectable where the core has it.
2026-10-16, 2026-10-19, 2026-10-20, 2026-10-28, 2026-11-06 — "1. .cor ort -> unter ordner name des Cores ... 3. /flash 4. andere medien"
replaces 2026-10-19: core folder then search trees; 2026-10-20: "/flash/Roms gewinnt".

**R75** PRG start: "Run PRG" and "Load PRG and start from", large PRGs up to $FFFF via DMA.
2026-10-18 (T104.2 Q2) — "RUN PRG / LOAD PRG and start from:"

**R76** Each core has a saves folder (TRX64: .c64re/.c64ring for save/resume).
2026-10-28 — "Dann saves mit anlegen ... .c64re und .c64ring"

**R77** Input devices via libraries/modules: USB mouse for every core with a mouse; USB keyboard; USB gamepad as ONE optional module enabled by the core's pad capability (console cores), button mapping saved in the core config.
2026-10-20, 2026-10-28, 2026-10-29, 2026-11-01, 2026-11-11 — "EIN Modul für das Zeugs als Option zur App ... über die Core+App capabilities"

**R78** Cartridges: CRT hash matching in the app for C64, C128, C16/Plus4, VIC-20 (later consoles); cart tools are apps with their own modern UI (TRX-CRT on demand, new UI); battery/memory-card save and remove cartridge in the cart slot list.
2026-10-25, 2026-10-28, 2026-10-29, 2026-11-12 — "Also Code in die App, das brauchen C64, C128, C16/plus4, VICE20"

**R79** Hard disk from a folder (Amiga): folder and size set in the core menu, app provides the block device.
2026-10-26 — "dann im Menu der Cores einstellbar, also welcher Folder und wieviel GB"

**R80** FAT32 and exFAT with 64-bit sizes and offsets (images > 4 GiB).
2026-10-18 — (relayed by TRX64-FW, D16; no German quote kept)

## E Settings & persistence

**R81** Each core has its own config (<name>.cfg beside the .cor), never in flash (exceptions: host, trx64u's own file); where the host has the same setting, the host value is offered as default option; no implicit inheritance.
2026-10-13, 2026-11-06, 2026-11-12 — "Jeder Core hat immer seine eigene Config und die Option den Host als Default wert"
replaces 2026-10-13: "Host == default, core == overwrite local".

**R82** Every change is written at once, no separate save step; all host menu settings are saved.
2026-10-13 (T93 Q "a"), 2026-11-03 — "Speichern wir die settings des Host Menüs nicht alle?"

**R83** Host and core settings are decoupled from Gideon's config store (reading his once at first start is OK).
2026-10-07, 2026-10-30 — "unsere Host und Core settings MÜSSEN entkoppelt sein von seinen"

**R84** Every core gets the C64's settings groups (Video, Audio, Input); one group-to-area table.
2026-11-06 — "Ist ok"

**R85** HDMI default mode saved in the host config; each core has its own video settings.
2026-10-31 — "pro Core eigene settings"

**R86** WiFi: set and edited only in the host's F8; cores show it and offer on/off; LAN beats WiFi; scanning never blocks the UI.
2026-10-13, 2026-11-06 — "Wifi settings im Host unter F8 setzen und editieren. In den Cores nur anzeigen und an/aus"

## F Process rules for app work

**R87** Spec first; complete specs, no spikes/slices; nothing left out or deferred without Alex's OK.
2026-09-13, 2026-10-04, 2026-10-14, 2026-11-01, 2026-11-07 — "Nichts wird gesliced, nichts verschoben, Agents DÜRFEN NIEMALS etwas weglassen"

**R88** Emulator first: compile locally, test in ue2emu (LVGL, TRX64 bitstream), board last.
2026-10-15, 2026-10-25, 2026-10-26 — "lokal compilen, mit dem Emulator testen ... als letzter Schritt geht es aufs Board"
replaces 2026-10-01 (FW era): "erstmal muss alles klappen auf dem C64U, dann ziehen wir den EMulator nach".

**R89** Every UI change goes through the UX skill / ux-specialist agent with pictures from ue2emu only (no render benches); learnings go into the skill; the agent also checks structure (separation of concerns); otherwise the UI work is stopped.
2026-10-28, 2026-11-06, 2026-11-07, 2026-11-11, 2026-11-12 — "Alle UI Sachen werden zwingend im U64 Emulator mit dem UI Skill getestet bevor an den Menschen geht"

**R90** Read the whole UX concept (TRX64-Ultimate lvgl_ui_project) before UI work; deviations listed for Alex, never silent.
2026-10-28 — "Und das UX Konzept soll er gefälligst komplett lesen!"

**R91** Parity when moving onto the service: same texts, navigation, actions and states (not pixel-identical); the host UI keeps its look, a change needs a reason; all 49 scenarios reprogrammed and proven.
2026-11-07, 2026-11-08, 2026-11-09 — "das UI vom Host sollte Möglichste weiter so aussehen"

**R92** Build the UI completely, then look at it; questions to Alex one at a time, with screenshot/mock for look and feel; never assume, refine.
2026-10-28, 2026-11-12 — "Nicht einfach was annehmen. Refinen mit mir wenn was unklar ist"

**R93** Deliver fast: app-only builds bundled into test releases; tests once in CI; no new CI/test infrastructure unless asked.
2026-11-08, 2026-11-12 — "Alles was ich will ist schnell zu liefern. Auf Kosten der Qualität gerne weil Tester gibt es"
replaces 2026-10-09: BDD/Gherkin migration as M2 acceptance criterion.

**R94** Status per core from code/GitNexus, never guessed; no human-team time estimates.
2026-11-12 — "Pro Core eine Tabelle. NICHT raten, im Code/gitnexus nachlesen"

**R95** T213: introduce (a) core by core as child specs, A1200 first, same pictures and tests before and after.
2026-11-07, 2026-11-12 — "(a) einführen, und ja, subspec Core by core"

**R96** App, benches and board test are built in this repository; only the published bitstream is taken from TRX64-Ultimate.
2026-10-26 — "dann können wir doch auch hier den Boardtest einplanen ... und von da nur den Bitstream holen"

**R97** Document the generic app framework: own tab in the architecture page (boot and core menu as examples), wiki on building a host app/own cores, DFX vs large core, what the host provides, return to host.
2026-10-12, 2026-10-26, 2026-11-02 — "'generic App framework' auch eine Tab sein, und da dann das Boot Menu als Beispiel"

## Contradictions / unclear

Resolved with Alex 2026-11-12 (all 13):
- 1: per setting; every core stores its own value; where the host has the same setting the core offers "Host" as a value, and on it follows the host live (also later changes). No automatic inheritance. ("Ja")
- 2: at 720p the 540-line menu is stretched to fill 1280x720 with smoothing (pix_layer can; only the app limits to whole factors: coreapp/src/video.cc:105, hostui.cc:1551). ("b")
- 3: the foot shows only the keys that act in the area; no "F1-F8 AREA" (the rail shows it). T212 D16 amended. ("Ja bitte")
- 4: CRSR LEFT = one level up; LARROW = ESC = leave, everywhere incl. the Library (replaces T192.3 D27). ("CRSR LINKS = eine ebene Hoch, LARROW = ESC")
- 4b: cursor keys in every file browser and file-tree select popup: up/down = one entry, right = into folder or ZIP, left = one folder up / out of folder or ZIP.
- 5: "no overlays over the running core" is about the menus and the generic app (Freeze only). The monitor is its own application (trx64u pulled it into the core for interfaces/LUTs) and is not covered by that rule.
- 6: a key bundle may remap every key except F1-F8 (areas) and 1-8 (tabs); reason: new key-UX paths without a rebuild. ("Ja, weil damit kann man dann verschiedene Tasten-UX-PFade erfinden ohne neubau")
- 7: ROMs as built (T148 D2, coreapp/src/romfind.h:42-57): read only from <core>/roms; a missing file is copied in once from the old places (verified). ("so bleiben")
- 9: hotkeys act always (unless an app context does not fit because of its key bindings or state), incl. volume in the running core; volume also in the menu up to 200 %; NO overlays over the running core. ("Hotkeys greifen im Grunde immer ... es gibt KEINE overlays über dem laufenden Core")
- 10: subline (path + version) only in the boot-menu editor; boot menu one line (logo, name, status, version); core menu no sublines. ("Ja")
- 11: three rows stay: Home "Reset", Home "Reload <ROM>, reset", F8 System "Reload core" (bitstream + app). ("Ja")
- 12: "host immutable" = the host bitstream only. The host app becomes the same generic app as the core app, built with it; host-specific are only the boot menu and its editor, the rest is identical. ("Ja genau. Das Boot Menu ist Host spezifisch und der Editor ... der Rest ist deckungsgleich")
- 8: test release cancelled, D15 holds without exception.
- 13: "C" = the language; matches T213 D1 option B (C modules in the service).


1. Settings layering: 10-13 "Host == default, core == overwrite local" vs 11-06 "KEINE Host settings mit vererbung" vs 11-12 "Option den Host als Default wert" - is "host as default" a per-setting choice the user flips, or the initial value only?
2. UI scaling: 10-22 "echte Auflösung, zentriert aber nicht skaliert" vs 11-05 "scaled menu better than pixel mush" - is the 540p UI integer-scaled, non-integer scaled, or unscaled at 720p?
3. Foot line: 10-21 "was links in der Nav steht braucht unten nicht mehr zu stehen" vs T212 D16 foot listing "F1-F8 AREA" on every list.
4. Left arrow: 10-28/29 always exit/escape like RUN/STOP vs T192.3 D27 "one folder up" in the Library - in the browser, does LARROW go up or leave?
5. Overlays: 10-26 monitor as overlay over the running TRX64 core wanted vs 11-06 "Keine overlays über den laufen Core" and Freeze-only menu.
6. Key bundles: 10-29 F-keys and 1-8 are fixed vs 11-07/11-09 fully configurable key sets per context - may a bundle remap F-keys/digits?
7. ROM search order: chat 10-28 (.cor folder, medium ../roms, /flash, other media) vs T104.1/T104.2 point 8 as recorded (beside core, board's /flash/roms, then roots) - which is built/binding?
8. T212 D15 says the info line comes from .toml, T213 D3 ships T212 as built with per-core C data - acceptable for the test release only, or a lint-gate exception?
9. Volume: 10-29 "im Menu, nicht im laufenden Core" vs 11-04 hotkey volume in the running core (CTRL+) - is the 10-29 rule fully replaced?
10. Subline: 11-11 "Die ganze subzeile ist doch unnütz" (core menu) vs 11-12 boot menu "smaller line under the name as before" - which screens keep a subline?
11. Restart naming: 10-23 "Restart core" / "reload core" (DFX) vs 11-12 T212 D17 "Reset" / "Reload <ROM name>, reset" - is "Restart core" still a separate row?
12. One app for everything (T192 D20, 11-08) vs T104 D4 "the host must not move when a core app changes" and the host pin - does the host binary get rebuilt from the shared app?
13. 11-12 "2 bleibt weiter C" - recorded as "service modules stay in C (option C?)" while T213 D1 records option "B"; the letter mapping is unclear without the weekend question text.
