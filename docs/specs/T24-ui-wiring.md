# T24 wiring: how the LVGL UI reaches every function of the app

**Status:** concept 2026-10-03, nothing built. **Tasks:** TODO.md T24 (LVGL UI), T32 (own settings), T36 (own API).
**Builds on:** `docs/specs/T24-lvgl-ui.md` (phase 1 built; LVGL v9.6.0; pixel layer), the coverage check
`docs/gideon-menu-tree.md` §13 (what the UI must reach), `docs/specs/T21-monitor.md` §7.4 (the API contract).
**Shares with:** T12 (one owner of the pixel layer, T24 §7), T20 (ring), T21 (monitor).

**One sentence:** the LVGL UI is one more client of the interfaces the app already has (actions, config
stores, file manager, `trx_*` calls); it gets Gideon's blocking dialogs through a `UserInterface` subclass and
his character-grid screens through a terminal widget, so no behaviour is copied and every function is reachable
on day one.

Evidence base: `software/ultimate-app` on `trx64/main` 06de2ea3, build `target/u64ii/riscv/ultimate`. Paths
below are relative to `software/ultimate-app/software/` unless they start with `apps/` or `target/`.

---

## 1. Rule

T24 §2 already says: every action the UI offers is a thin call into a function the REST API or the
`SubsysCommand` path also reaches; the UI holds no behaviour. For the wiring this means each LVGL control ends in
exactly one of:

1. `SubsysCommand::execute()` on an existing `Action` or on a `(subsys, function, mode)` triple;
2. a read or write on a `ConfigItem`;
3. a `FileManager` call;
4. a `trx_*` C call (ring, `.c64re`, video, IO block).

Where a function lives only inside a Gideon UI class, it gets an adapter (§4) or a change listed in §9. The one
place logic is written again is the selection loops of the file browser (about 60 lines, §9 G3).

---

## 2. What is already free of UI classes

| Interface | What it gives | Evidence | LVGL binds by |
|---|---|---|---|
| Action catalogue | `TasksCollection`: categories (name, sort order) holding `Action`s (name, subsys/function/mode or direct function, `hidden`, `greyed`). Every `ObjectWithMenu` fills it, apps too | `userinterface/tasks_collection.h:16-97`; `infra/action.h:19-120`; `userinterface/menu.h:11-30`; `app_loader/app_registry.cc:349-376` | `TaskMenu::ensure_task_actions_created(writable)` (`task_menu.h:41-66`) builds and refreshes it; then iterate `getCategories()`; show `isShown()`, grey `!isEnabled()` |
| Run an action | `SubsysCommand(ui, action, path, filename)->execute()`; the command deletes itself; subsystem mutex with a 1,000-tick wait | `infra/subsys.h:91-110`; `infra/subsys.cc:4-34`; `userinterface/context_menu.cc:131-140` | one call; afterwards read `ui->menu_response_to_action` (set at `io/c64/c64_subsys.cc:787`, `io/c64/trx_timewarp_ui.cc:314`, `filetypes/filetype_prg.cc:196`, `drive/c1541.cc:1042`) to know whether the menu closes |
| Settings | `ConfigManager::getStores()`, `ConfigStore::items`, `ConfigItem` (definition: type, min, max, items, format; `getValue`, `getString`, `setValue`, `setString`, `next`), `ConfigGroupCollection::getGroups()`, `is_flash_stale()` | `components/config.h:87-300`; `userinterface/config_collection.h:16-106` | direct; lifecycle in §4.4 |
| Files | `FileManager`: `get_directory`, `fstat`, `fcopy`, `rename`, `delete_recursive`, `create_dir`, `is_path_writable`, observer queues | `filemanager/filemanager.h:215-290` | direct; none of these take a `UserInterface` |
| Tree and context items | `Browsable::getSubItems()`, `fetch_context_items()`: file type actions, then the generic file actions, then the app registry hook | `userinterface/browsable.h:70-75`; `userinterface/browsable_root.h:134-136,240-251`; `filetypes/filetypes.h:23` | direct; use `getName()` and `getInfo()` (size, attribute, extension), never `getDisplayString()` (it writes screen escape codes) |
| Assembly64 / CommoServe | `Assembly::get_presets`, `send_query`, `request_entries`, `request_binary` | `network/assembly.h:34-39` | direct |
| Frame ring (`.c64re` removed from M1, A13) | `trx_ring_*` | `io/c64/trx_ring.h:60-125`; `io/c64/trx_c64re.h:15-23` | direct |
| Drives | `C1541` objects (`c1541_A`, `c1541_B`), `get_last_mounted_drive()` | `api/route_drives.cc:42-90` builds its JSON from them | same accessors |
| Identity, capabilities | IO block ID/CAPS | `io/c64/trx_io.h`; `api/routes.cc:194` | direct |

The REST routes already wrap most of these. They call `SubsysCommand` with `user_interface = NULL`
(`api/route_machine.cc:76-82`; `route_streams.cc:56,61`) and `ConfigStore::at_close_config()` after a set
(`api/route_configs.cc:313`). The LVGL path is the same call with a real UI object.

---

## 3. Where a function is tied to Gideon's UI classes

| Seam | Evidence | Effect | Resolution |
|---|---|---|---|
| Blocking dialogs | `UserInterface::popup` (2), `choice`, `string_box` (4), `string_edit`, `show_progress`, `update_progress`, `hide_progress` are `virtual`: `userinterface/userinterface.h:116-129`. Call sites outside the monitor: 227 `popup(`, 39 `string_box(`, 25 `show_progress(` | every action asks its dialogs through the `UserInterface*` in the command | subclass (§4.1) |
| Non-virtual UI methods | `run_editor` (two), `run_hex_editor`, `run_machine_monitor`, `help`, `activate_uiobject`, `uiobject_modal`: `userinterface.h:84,140-147`; `pick_path()` is a free function: `userinterface/tree_browser.cc:884`, declared in `tree_browser.h:157` | text viewer, hex viewer, system information, debug log, monitor and the path chooser always draw on the character screen | make them `virtual` (§9 G1) |
| Character-grid objects | `UIObject` stack on `Screen`/`Window`; apps reach it through `s_current_ui->screen` | monitor, SID address editor, forms, Assembly64 search, UltiCopy window, apps' `screen_open` and `win_*` | terminal widget (§4.2) |
| Settings functions | `t_cfg_func(UserInterface *, ConfigItem *)`: `components/config.h:112`; called at `userinterface/config_menu.cc:126-131` (FUNC case) | Wi-Fi connect and AP choice, SID address editor | pass the LVGL `UserInterface` |
| Where the UI runs | `ultimate_main` loop calls `ui->run_once()`, which is `virtual` (`application/ultimate/ultimate.cc:189-230`; `userinterface.h:113`) | the UI is selected by one pointer | override `run_once()` (§7) |
| File selection ops | clipboard, paste, delete selection live in `TreeBrowser` (`userinterface/tree_browser.cc:641-735`) | not reachable without the class | adapter loop over `FileManager` (§9 G3) |
| Texts with control codes | action names `"\023 VIC Stream"` and leading blanks (`io/network/data_streamer.cc:261-266`); display strings with `\e` codes | would show as garbage | name sanitiser (§4.7) |
| Path context | save actions write into `cmd->path` (`io/c64/c64_subsys.cc:631-637`); the task menu of the Commodore interface passes no path, so they are greyed (`userinterface/task_menu.cc:37`; `userinterface/commodore_menu.cc:158`) | "Save REU / Memory / Cartridge / EDID / log / config to file", "Set Partition here", Create actions | UI supplies a target folder (§9 G4) |

---

## 4. The adapter layer

New directory `software/trxui/`, built only for the U64-II app (`#if U64 == 2`, like `io/c64/trx_*.cc`). Seven
pieces, none with behaviour of its own.

### 4.1 `LvglUserInterface : UserInterface`

One instance. It is what the LVGL path puts into `SubsysCommand::user_interface`, so Gideon's 227 popup and 39
string-box calls work unchanged. It overrides `popup` (both), `choice`, `string_box` (the four overloads forward to
the one with the edit policy), `string_edit`, `show_progress`, `update_progress`, `hide_progress`, `pollInactive`
and `run_once` (§7), and, after §9 G1, `run_editor`, `run_hex_editor`, `run_machine_monitor`, `help`.
A subclass of `UserInterface` already exists in the tests (`test/monitor/machine_monitor_test_support.cc:1117`).

Semantics stay Gideon's: `popup` returns the `BUTTON_*` code (`userinterface/ui_elements.h:15-19`) or 0 when the
menu key aborts; `string_box` returns the length or 0; one modal at a time; one progress box (as
`userinterface.cc:952-967`). The constructor registers the shared "User Interface Settings" store by name, as the
overlay, C64 and telnet instances already do (`userinterface.cc:247`; `components/config.cc:102-110`).

### 4.2 `LvglHost` and the terminal widget

`LvglHost : GenericHost` (`infra/host.h`) gives `getScreen()` a `Screen_MemMappedCharMatrix` of 40 x 25 (the class
the overlay uses: `io/overlay/overlay.h:60`; two 2,000-byte arrays) and `getKeyboard()` the LVGL keypad queue. A
`trx_term` LVGL widget draws that matrix with the C64 character set, from `copy_matrix` (`io/c64/screen.cc:110`,
the same call the REST `machine:menu_screen` uses: `api/route_machine.cc:438`).

Every character-grid object then runs unchanged inside LVGL: `MachineMonitor`, `SidEditor`, `Editor`,
`HexEditor`, `FormUI`, the Assembly64 search (`TreeBrowser` based), `pick_path`, and every app that calls
`ui->screen_open` (`app_loader/app_api_impl.cc:369-390`). This is also the migration bridge (§7): native LVGL
screens replace it one function at a time.

### 4.3 Action catalogue adapter

Flattens `TasksCollection` into `{category, label, key, shown, enabled}`. `key` is the text `category|label`; for
actions with a subsystem it also carries `(subsys, function, mode)` (`infra/action.h:30-31`), which are the
`MENU_*` constants and stable. `refresh(target_folder)` calls `ensure_task_actions_created(writable)` and must
be called at start, after every folder change and after `AppRegistry::register_app` (`app_registry.cc:263-300`).
Execution is §6.1.

### 4.4 Settings adapter

Pages come from `ConfigManager::getStores()` (not hidden) and `ConfigGroupCollection::getGroups()`; the
tree order is `sort_order` with the six header orders (`userinterface/config_menu.h:279-308`). Item kinds:
value, enum, string, function, separator, info, dropdown function, password (`components/config.h:73-80`).
Rules, all taken from what Gideon's browser and the REST route do:

| Step | Call | Evidence |
|---|---|---|
| open a page | `store->at_open_config()` (refreshes info items and calls `on_edit`) | `config.cc:268-273`; `config_menu.h:178-190,254-260` |
| change | `ConfigItem::setValue/next/setString`; dropdown choices from `t_cfg_strfunc` | `config.cc:850-910`; `config_menu.h:118-126` |
| leave a page | `store->at_close_config()`, which effectuates when stale | `config.h:206-212`; `api/route_configs.cc:313` |
| save | `store->write()` for every `is_flash_stale()` store; ask or not by `Auto Save Config` | `userinterface/config_menu.cc:169-203` |
| function item | `((t_cfg_func)item->definition->items)(lvglUi, item)` | `config_menu.cc:126-131` |

The UI shows the `is_flash_stale()` flag as its "unsaved" state; there is no Apply or Discard in the app (Gideon
effectuates when a page is left; "Reset from Flash" is the way back).

### 4.5 Library adapter

Wraps `BrowsableRoot` / `BrowsableDirEntry` and `FileManager`:
`list(path)` returns entries `{name, size, dir, extension, type}`; `actions(entry)` returns what
`fetch_context_items` appends (the caller deletes non-persistent actions, as `context_menu.cc:46-53` does);
`run(action, path, name)` is a `SubsysCommand`. File operations (view, copy, move, rename, delete, mkdir,
selection, clipboard) call `FileManager` and `UserFileInteraction::S_*` with progress through §4.1. Changes on
disk arrive through a `FileManager` observer queue (`filemanager.h:275-290`), as `TreeBrowser::checkFileManagerEvent`
uses it. The root entry list includes network interfaces and FTP hosts (`browsable_root.h:272-310`), so the Wi-Fi
actions (`io/network/network_esp32.cc:211-232`) come with `fetch_context_items`.

### 4.6 Status adapter (Home)

Read-only getters for the Home screen of the target picture: machine paused or running, drive A/B image and
modified state, cartridge name (`C64_CRT::get_source`, `is_valid`), network link (`NetworkInterface::DoWeHaveLink`),
ring state (`trx_ring_status`), heap (`xPortGetFreeHeapSize`). Which of them need new getters is §9 G5.

### 4.7 Name sanitiser

Drops bytes below 0x20, `\e` sequences and leading blanks from action names, config names and alt names, and turns
the leading `\023` of the stream actions into a state flag. Also adds the "why is it greyed" text, keyed by
category and label (the app has only a boolean).

---

## 5. Function by function

Function groups follow `docs/gideon-menu-tree.md` §13. "REST" is what the app already exposes for the same
function.

| Function group | Binds to | REST today | Adapter or change |
|---|---|---|---|
| Settings pages and groups (§13.5) | `ConfigManager`, `ConfigGroupCollection` | `/v1/configs*` (`api/route_configs.cc:178-473`): items only; no groups; no function or info items (`route_configs.cc:17-24`) | §4.4 |
| Save, reset, clear configuration | Actions in category "Configuration" (`userinterface/configio.cc:26-39`) | `configs:save_to_flash`, `load_from_flash`, `reset_to_default` | none |
| Reset, reboot, clear memory, power cycle, power off | `SubsysCommand(SUBSYSID_C64, MENU_C64_*)` | `machine:reset`, `reboot`, `poweroff` (`route_machine.cc:91-208`) | none |
| Pause, resume | `MENU_C64_PAUSE/RESUME`; not in the menu of this build | `machine:pause`, `resume` | catalogue lacks them: call by id |
| Save C64 memory, REU, cartridge, MP3 drives | `SubsysCommand` plus `string_box` and progress | none | §9 G4 (target folder) |
| Run, load, DMA of PRG, CRT, SID, MOD, REU | actions from `FileType::fetch_context_items` | `runners:*` (`route_runners.cc:24-272`) | none |
| Mount, run, load into MP3 drive | actions from `filetype_d64.cc:101-124`, `filetype_g64.cc:63-81` | `drives:mount` (`route_drives.cc:94-140`) | none |
| Drive on, off, reset, mode, remove, save, blank | actions of `drive/c1541.cc:240-260` | `drives:*` (`route_drives.cc:186-343`), no save, blank, swap | none |
| View, hex view, copy, move, rename, delete, mkdir, selection, clipboard | `FileManager`, `UserFileInteraction::S_*` (`user_file_interaction.cc:33-215`) | none (FTP only) | §4.5; viewers need §9 G1 or a native text view |
| Create disk images | `ImageCreator` actions (`drive/disk_image.h:167-180`) | `files:create_*` (`route_files.cc:95-247`) | none |
| Tape, printer | actions `io/tape/tape_*.cc`, `io/printer/iec_printer.cc:191-200` | none | none |
| Software IEC, partitions | actions `io/iec/iec_drive.cc:209-214`; `FormUI` in `io/iec/iec_ui.cc` | none | forms in the terminal widget |
| UltiCopy | actions `io/iec/iec_ulticopy.cc:46-49`; `UltiCopyWindow` | none | terminal widget |
| Streams | actions `io/network/data_streamer.cc:261-266` | `streams:start/stop` (`route_streams.cc:30-79`) | none |
| Wi-Fi, network | config function items; `NetworkLWIP_WiFi::fetch_context_items`; `BrowsableWifiAP` | none | §4.1 (function items need a UI object) |
| Assembly64 / CommoServe | `Assembly` class | none | library adapter variant |
| System information | `SystemInfo::generate` writes text into `run_editor` | `/v1/info` (part) | shown as text through §9 G1, structured data §9 G5 |
| Monitor | `run_machine_monitor(MemoryBackend*)` (`userinterface.h:147`) | none; `trxmon.u2a` RPC | terminal widget first; a native monitor view is a client of the trxmon verbs (T21 §7.4) |
| Timewarp | `trx_ring_*` (`trx_ring.h:60-125`) | `timewarp:*` (`route_timewarp.cc:54-330`) | §9 G8 |
| `.c64re` save and load | **removed from M1 (A13), T66 in M2** | was `timewarp:dump`, `ringdump`, `load` | §9 G7 |
| Apps: run, tasks, file actions | `AppRegistry`, app loader (`app_registry.cc:321-376`) | `apps:run`, `run_file` (`route_apps.cc:36-193`) | §9 G12 |
| Menu button, keys, disk swap | dispatcher in `ultimate_main` | `machine:menu_button`, `machine:input` | §6.2, §7 |
| Cartridge-changed popup | menu enter hook (`c64_subsys.cc:244-280`) | none | §9 G1 (hook access) |
| Boot hotkeys | `u64_config.cc:965-990` | none | not a UI function; stays |

---

## 6. Threads and ownership

### 6.1 Tasks of the app and who runs what

| Task | Priority | Stack (words) | Evidence | Role here |
|---|---|---|---|---|
| "U-II Main" | 0 | 1,600 | `portable/riscv/riscv_main.c:159`; `FreeRTOSConfig.h:6-23` | init, then the Gideon UI loop: poll button and keys, `ui->run_once()` blocking (`ultimate.cc:189-230`); menu actions run here today |
| "TRX UI" | 1 | 3,200 | `io/c64/trx_ui.cc:67-68,221` | owns every `lv_*` call (T24 §6) |
| "TRX Video" | 1 | 1,024 | `io/c64/trx_video.cc:802` | freeze renderer |
| "TRX Ring" | 1 | 3,200 | `io/c64/trx_ring.cc:1638` | polls the ring every 100 ms |
| "HTTP Listener" and request tasks | 1 | 1,600 | `network/httpd.cc:25` | REST; runs `SubsysCommand` with `user_interface = NULL` |
| "Socket Gui Task" | 0 | 1,600 | `network/socket_gui.cc:189,306` | telnet menu, own `UserInterface`, `run_remote` |
| "app_task" / "app_resident" | 0 | 25,600 | `app_loader/app_loader.cc:355`; `app_api_net.cc:234` | one app at a time |

**Decision.** A new task "TRX Cmd" (priority 0, 3,200 words) runs every action the LVGL UI starts. Reasons:

1. Gideon's actions block: dialogs, long copies, `vTaskDelay` loops, the subsystem mutex wait (`subsys.cc:20-25`). On the UI task they would stop LVGL.
2. They run on a 1,600-word stack today (U-II Main), so 3,200 is enough. The UI task (3,200) stays for drawing only.
3. REST already runs the same code from other tasks, so the actions are task-agnostic.

One command at a time (queue depth 1, a second request answers "busy"): `UserInterface` has one progress box and one `menu_response_to_action`, and the app loader runs one app.

### 6.2 Keyboard, button, mouse

| Source | Path | Evidence |
|---|---|---|
| C64 matrix and joystick (U64-II registers) | `Keyboard_C64(overlay, &U64II_KEYB_ROW, …)`; `getch()` falls through to the USB keyboard | `ultimate.cc:134-135`; `io/c64/keyboard_c64.cc:428-445` |
| USB HID keyboard and REST-injected keys | `system_usb_keyboard.getch()`; REST pushes with `push_head` | `io/usb/keyboard_usb.h:115-117`; `api/route_input.cc:383-421` |
| C64 keyboard through the C64's CIA | `Keyboard_C64(this, &CIA1_DPB, …)` of the C64 host, used while frozen | `io/c64/c64.cc:228`; `ultimate.cc:165-178` |
| Menu button | `Overlay::checkButton` on `ITU_BUTTON_REG`; REST `push_active_menu_button`; USB F10 and Scroll Lock | `io/overlay/overlay.h:111-131`; `ultimate.cc:61-68,195-198` |
| USB mouse | converted to keys by Gideon's path (`CFG_MENU_MOUSE_NAV`, `u64_config.cc:366`) | not examined; open question 9 |

`getch()` consumes, so there is exactly one reader. The LVGL UI task reads the overlay's keyboard object
(`overlay->getKeyboard()`, `overlay.h:138`) through an `lv_indev` keypad (T24 §9), and Gideon's loops never run at
the same time (§7). Two details keep REST and USB input correct:

- `ultimate_main` wraps `run_once()` in `system_usb_keyboard.enableMatrix(false/true)` (`ultimate.cc:221-223`);
  `LvglUserInterface::run_once()` is called inside that bracket.
- Gideon counts open menus (`set_available`, `userinterface.cc:267-285`, `683-690`); HID and REST ask
  `userinterface_any_menu_active()` to decide whether a key goes to the menu or to the C64 matrix
  (`io/usb/usb_hid.cc:187-190`; `api/route_input.cc:396`). The LVGL UI must raise that count while it is open
  (§9 G1: `set_available` is private).

Key codes: native screens take raw codes and map them to `LV_KEY_*`; they do not use Gideon's style-dependent
F-key mapping. Legacy objects in the terminal widget get `UserInterface::keymapper` (`userinterface.cc:1023-1049`).

### 6.3 Screen ownership

| Consumer | What it uses | Switch |
|---|---|---|
| C64 picture | scaler, HDMI | always |
| Gideon overlay UI | FPGA text layer, `overlay_regs->TRANSPARENCY` | `Overlay::take_ownership` (`overlay.h:91-94`) |
| Gideon freeze UI | machine pause, C64 screen RAM, T12 pixel layer renderer | `C64::freeze()` (`c64.cc:1288`) |
| LVGL | pixel layer, RGB565, two buffers (T24 §5) | `trx_lv_open/close`, `trx_lv_yield_layer` |

Rules:

1. The freeze wins (T24 §7): `trx_video_freeze_enter()` calls `trx_lv_yield_layer()`.
2. Gideon's overlay and LVGL are never on together; the dispatcher (§7) picks one per menu press.
3. LVGL opens over the running picture, with colour key and global alpha (T24 §5, phase 3). It does not freeze the machine, like Gideon's "Overlay on HDMI" interface type.
4. A function that needs a paused machine pauses it itself. Timewarp requires `c64->is_frozen()` today (`io/c64/trx_timewarp_ui.cc:291-299`); the LVGL page calls the freeze, then the T12 Timewarp bar (`trx_video_tw_begin`, `trx_video.h:58-62`) takes the layer. LVGL returns after `trx_video_freeze_leave` once T24 §7's "layer free again" call exists.

### 6.4 Dialogs, progress, messages

- **Dialog.** The worker calls `LvglUserInterface::popup`; it posts `{kind, text, buttons}` to the UI task's queue and waits on a binary semaphore; the UI task shows an `lv_msgbox` and gives the semaphore with the button code. LVGL stays single-threaded.
- **Progress.** `show/update/hide_progress` become three messages, at most 25 updates per second. A 10 MB copy does not block drawing (the cost is the copy).
- **Toasts.** Gideon's menus poll `UserInterface::getMessage()` (`userinterface.cc:1010-1019`, four slots) and show a popup; the LVGL tick polls the same queue and shows a toast.
- **Jobs started elsewhere** (REST, FTP, DMA) have no `UserInterface` and no progress; the LVGL UI cannot show them (§9 G6).

---

## 7. Replacement, not coexistence (Alex, 2026-10-03)

The LVGL UI replaces Gideon's menu; there is no selector and no way back to Gideon's UI.

**Dispatcher.** `ultimate_main` picks `ui` (`ultimate.cc:204-211`); a few lines set `ui = lvglUserInterface`.
`LvglUserInterface::run_once()` takes the layer, calls the menu enter hook, runs the LVGL screens until the menu
closes, and returns. The rest of the loop (button, `enableMatrix`, REST `push_active_menu_button`) is unchanged, so
`machine:menu_button` opens the LVGL UI. Gideon's overlay/C64 `UserInterface` roots are no longer created.

**Text screens.** Functions that draw on Gideon's character screen (monitor, forms, SID editor, Assembly64 search,
the apps' `win_*` API) run inside the terminal widget (§4.2) as part of the LVGL UI until a native screen exists.
That widget is a building block of our UI, not a route into Gideon's menu tree. Every function in
`docs/gideon-menu-tree.md` §13 must have an LVGL entry point before the switch.

**Failure handling.** If LVGL cannot start (no pixel layer or no buffers: `trx_lv_available()`/`trx_lv_open()`
false), the app logs it and the menu stays closed; recovery is the developer path (JTAG/dev bitstream), the same as
for any firmware fault. Open: what a machine without HDMI shows (T12/T35 menu on the VIC), and whether telnet
(`socket_gui.cc:189`) keeps a text menu. REST `machine:menu_screen` answers 404 while LVGL is open
(`route_machine.cc:429-445`); remote clients use T36.

---

## 8. API mapping (T36)

One command table in firmware (`software/trxui/cmd_*.cc`): the C++ functions of §4, with no `UserInterface`
parameter. LVGL calls them in process. The WebSocket JSON-RPC server (T21 §7.4) is a second front end that
marshals the same calls. T24 §2 (API first) holds by construction.

Three method families on the TRX64 runtime contract (WebSocket JSON-RPC 2.0, port 4312):

| Family | Methods | Our function | UI use |
|---|---|---|---|
| Contract, as specified in T21 §7.4 | `ping`, `session/state`, `session/read_memory` | IO block, `C64` state | status bar |
| | `media/open`, `mount`, `unmount`, `persist` | drive `SubsysCommand`s: mount (`filetype_d64.cc:101-124`), remove, save as binary / GCR (`c1541.cc:240-260`) | drive tiles, Library |
| | `session/load_prg`, `runtime/run_prg` | PRG actions (`filetype_prg.cc:70-72`) | Library: Run |
| | `session/type`, `key_down`, `key_up`, `joystick_set` | input API (`route_input.cc:726-821`) | none (the UI has its own keys) |
| | `session/reset`, `session/power` | `MENU_C64_RESET`, `MENU_C64_POWERCYCLE` | Home quick actions |
| | `checkpoint/*`, `snapshot/dump` | `trx_ring_*`, `trx_c64re_*` | Timewarp |
| | `monitor/exec`, `debug/*` | `trxmon.u2a` (`apps/trxmon/src/rpc.c:913-928`) | Tools: Monitor |
| Ours, `ult/*` (additive; a TRX64 daemon answers -32601, so nothing breaks) | `ult/config.groups`, `list`, `get`, `set`, `save`, `reset` | settings adapter, groups included | Settings |
| | `ult/task.list`, `ult/task.run` | catalogue, key `category|label` | Quick actions, Tools |
| | `ult/fs.list`, `copy`, `move`, `rename`, `delete`, `mkdir`, `stat` | `FileManager` | Library |
| | `ult/app.list`, `ult/app.run` | `AppRegistry`, app loader | Tools: Apps |
| | `ult/status` | status adapter | Home |
| Notifications | `debug/*` (trxmon) | | |
| | `ult/job` (progress), `ult/dialog` (an open popup, answered by `ult/dialog.answer`) | `LvglUserInterface` queue | web UI |

An API client has no `UserInterface`, so actions that need a dialog run as REST does today
(`user_interface = NULL`: the action skips the dialog or answers `SSRET_NO_USER_INTERFACE`). A web UI that wants the
dialogs gets a `WsUserInterface` built exactly like §4.1: the same virtuals, the dialog sent as `ult/dialog`.

Where the server lives is open question 1: today the WebSocket server is inside `trxmon.u2a`, a resident app that
only gets the ABI tables (`app_loader/app_api_net.cc:200-240`; `apps/trxmon/src/ws.c`), and `ult/*` needs the
firmware's own objects.

---

## 9. Gaps: no clean interface today

| # | Function | Problem | Minimal change |
|---|---|---|---|
| G1 | Viewer, hex viewer, system information, debug log, help, monitor, path chooser, menu-open count, enter hook | `run_editor` (2), `run_hex_editor`, `run_machine_monitor`, `help` are non-virtual (`userinterface.h:84,140-147`); `pick_path` is a free function (`tree_browser.cc:884`); `set_available` and `menu_enter_hook` are private (`userinterface.h:64-85`) | add `virtual` to those six, make `pick_path` a virtual method (four callers: `iec_ui.cc:78`, `user_file_interaction.cc:303,330`, `app_api_impl.cc:688`), make `set_available` protected, add a public `fire_menu_enter_hook()`; about 20 lines |
| G2 | Character-grid screens and apps | none reachable without a character screen | new code only (§4.2); no app change |
| G3 | Selection copy, paste, delete | loops inside `TreeBrowser` (`tree_browser.cc:641-735`) | adapter reimplements them over `FileManager::fcopy` and `delete_recursive` (about 60 lines); extraction into a shared function only if Gideon's UI should share it |
| G4 | Save to file, Set Partition here, Create | actions take `cmd->path`; the Commodore menu passes none (`task_menu.cc:37`; `commodore_menu.cc:158`) | none in the app: the UI keeps a target folder (home directory `CFG_USERIF_HOME_DIR`, else the last picked one) and passes it to `ensure_task_actions_created` and to the command |
| G5 | Home status, system information as data | `SystemInfo::generate` builds text (`system_info.cc:160-218`); drive state sits in `route_drives.cc:42-90`; no getters for tape, printer, SoftIEC | extract `drive_status(C1541 *, JSON_Object *)` from the route; add small getters for tape and printer state |
| G6 | Progress of jobs started by REST, FTP, DMA | `show_progress` needs a `UserInterface`; those callers have none | not for the first release; later a job registry fed from the copy loops |
| G7 | **Dropped for M1 (A13, T66 in M2).** Load `.c64re` | no file type (`gideon-menu-tree.md` §13.2); only `timewarp:load` | add `FileTypeC64re` (about 40 lines): "Load Snapshot" calls `trx_c64re_load` |
| G8 | Timewarp from LVGL | the action is a `UIObject` that needs `c64->is_frozen()` (`trx_timewarp_ui.cc:291-316`) | split into `trx_timewarp_open()` (freeze, `trx_video_tw_begin`) and `trx_timewarp_close()`; keep the `UIObject` as one client |
| G9 | Ring, splash, freeze look, write-back settings | no store; the ring starts 6 s after boot with no switch (`trx_ring.cc:1606-1612`); only REST can stop it | store "TRX64 Settings" (T32) with change hooks calling `trx_ring_start/stop`; items for splash and look |
| G10 | Config groups, function and info items over the API | `route_configs.cc` knows stores only and skips function and info items (`route_configs.cc:17-24`) | `ult/config.groups` and `ult/config.call` (§8); call the item function with the right `UserInterface` |
| G11 | Items with no menu path | UserPort Power Enable, Adjust Color Clock, VIC Test Colors, C64U Model (`gideon-menu-tree.md` §13.5) | list them in the "TRX64 Settings" or an "Expert" page of the LVGL UI; no app change |
| G12 | Apps in the catalogue | tasks and file actions of an app exist only after it ran once since boot (`app_registry.cc:150-151,267-271`) | boot scan of the app folder and `register_app` per manifest (`app_manifest.cc`), or accept it (open question 8) |
| G13 | Greyed reason, stable id, clean names | `Action` has a boolean only and a pointer; names carry control codes | adapter (§4.3, §4.7), no app change |
| G14 | Mouse in menus | `CFG_MENU_MOUSE_NAV` path not traced | open question 9 |

---

## 10. Open questions

1. **Where does the `ult/*` server live?** Firmware-resident (full access, one table for LVGL and WebSocket) or a resident app with a larger ABI (keeps the firmware minimal)? This spec assumes firmware for `ult/*`; `trxmon.u2a` stays the debug server.
   *Superseded for the generic application by T104.1 Q8 (Alex, 2026-10-18): the REST server, and later the WS
   adapter, run in the base, not as a loadable app.* **Decided (Alex, 2026-10-03): a loadable app.** Users who want the API load it; it must be startable by autostart. Consequences: the command table the LVGL UI uses in process is exposed to apps through the app ABI (one table, two consumers); autostart needs the app registry to persist its autostart list in /Flash (possible once T44's gate allows config/flash-drive writes; today the registry is RAM-only on u64ii).
2. **Does the LVGL menu freeze the machine when it opens?** Assumed no: live picture, freeze only for Timewarp and similar.
   **Decided (Alex, 2026-10-03): both, selectable.** Default: the machine keeps running (like Gideon's default "HDMI Overlay"). Option: freeze on open (Alex uses Gideon's Freeze mode). The setting reuses Gideon's existing "Interface Type" (Freeze / Overlay) item (or moves to the TRX64 settings, T32); Timewarp and the monitor pause on their own either way.
3. ~~How do you get back to Gideon's UI when LVGL fails?~~ **Void (Alex, 2026-10-03): the LVGL UI replaces Gideon's; no way back (§7).**
4. ~~Where is the selector?~~ **Void: no selector (§7).**
5. **Order of native screens?** Assumed: native screens for the ten flows of `gideon-menu-tree.md` §9 first; text screens (monitor, forms, SID editor, Assembly64 search, apps' `win_*`) in the terminal widget until each gets a native one. No Classic tile (§7). **Decided (Alex, 2026-10-03): yes.**
6. **One command at a time?** Assumed yes. Copying while browsing needs more than one worker and a job registry (G6).
   **Decided (Alex, 2026-10-03): yes, one at a time, keep it simple.** But the UI gets an **activity indicator**: a small always-visible status (icon + short text, e.g. "FTP: upload game.d64 45 %", "Copying 3/12", "Downloading …") for work that runs outside the menu's own command — FTP server transfers, REST uploads/run_* loads, Assembly64 downloads, file-system work (copy, delete, format, flash-drive writes). Needs one small activity registry in the app (begin/progress/end calls from the FTP server, `FileManager`, the HTTP routes and the downloaders; read by the LVGL status bar and by T36 as a notification). Gideon's UI shows none of this today.
7. **Language.** Gideon's labels are English and fixed in code. Does the LVGL UI show them as they are (adapter only) or does it own its terms (`gideon-menu-tree.md` §11.1)? A label table is a new piece in §4.7.
   **Decided (Alex, 2026-10-03): own terms in English only, from a label table (properties-style, common practice).** Gideon's fixed labels are legacy from the 1541U/U2+ era; the table maps config item / action / page ids to our labels and short help texts. No translations for now (the table keeps that door open).
8. **Apps in the catalogue.** Boot scan (G12) or "after the first run"?
   **Answered (2026-10-03): the app registry already persists apps and their actions (`/Flash/apps.json`, `.u2a` in `/Flash/apps`) and loads them at boot.** "After the first run" is only our `APP_REGISTRY_RAM_ONLY` switch on u64ii, set while flash writes were forbidden. Once T44 stage 2 has passed and `g_wren_pass` is on, take the switch out: apps show at boot, autostart (Q1) works through the same registry. No boot scan needed.
9. **USB mouse** in the LVGL menu: needed? (G14) **Decided (Alex, 2026-10-03): no mouse.**

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

### T32

Own TRX64U settings menu for our own features

Milestone M2, status ⏸. Open (one line, as in the TODO table): Not built

Decisions: [ADR-0002 Own API (TRX64 WS JSON-RPC) is the target; Gideon's REST is no longer extended](../adr/0002-own-api-gideon-rest-frozen.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Own "TRX64U" settings menu for our own features (overlay transparency, splash, freeze/pause behaviour, write-back, later ring/monitor/turbo) instead of adding items to Gideon's existing setting pages; later part of the LVGL UI (T24). Store, selector and gaps: docs/specs/T24-ui-wiring.md §7, §9 (G9, G11); today only Overlay Transparency, Interface Type and Save Changed Cartridge exist (gideon-menu-tree.md §13.6)

### T36

Decoupling stage 2: own UI and own API for everything

Milestone M2, status ⏸. Open (one line, as in the TODO table): Not built

Decisions: [ADR-0002 Own API (TRX64 WS JSON-RPC) is the target; Gideon's REST is no longer extended](../adr/0002-own-api-gideon-rest-frozen.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- Task text: Stage 2, decoupling from Gideon: own UI (T24, LVGL) and own API for everything (WS JSON-RPC per the TRX64 contract also for media/machine/input, web UI on it; Gideon's REST no longer extended, decision 2026-10-02 "option 1 as target"); Gideon's app remains only the base layer. Command table, `ult/*` methods and open questions: docs/specs/T24-ui-wiring.md §8, §10
