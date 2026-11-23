# Handover to TRX64-Ultimate: the pending calls and their final names (T239.3, base 3.3)

Source: `software/trx64u/{base,ring,monitor}/pending_api.h` of the TRX64-Ultimate branch `u18` (2d4c2dcc). Contracts: the headers in `coreapp/src/` named in each table. Map and reasons: `docs/dev/boundary-trx64u.md` (section 14 lists the same rows with the reading of the code).

Rules for all rows:
- No call takes a `CoreCtx *`. The base knows its context. Shared-library tables (c64cart, cbm1541, uci, sid, tempdisk) take none either.
- A `LibApi` member is a pointer to a table with `size` first. Members are only appended. Test `size` before you call a member that came later.
- `LibApi` members (3.3): `sync`, `settings`, `ctx`, `io`, `board`, appended after `lib`; `SvcSys` gets six members; `SvcFs` gets `list_long`. Exports of shared libraries come with `SvcLib::find(name, U2A3_VERSION(1, 0), &size)`: the second argument is a version, `major << 16 | minor`, so a bare 1 asks for major 0. From 3.3 an export is matched on its exact major.
- A library starts its engine in the core hook `start` (`core_hooks.h`), never in `lib_register`.
- The headers compile alone (`cc -fsyntax-only`, `c++ -fsyntax-only`). They are not in `libapi.h` yet; that is the 3.3 build.

Kinds: base = a call, state or operation of the base (also `io.*` register calls); export = a table of a shared library of this repository; theirs = your own code; removed = no call needed.

## base library (59 pending: base 24, export 29, theirs 2, removed 4)

| Pending call | Final name | Header | Signature | Note |
|---|---|---|---|---|
| `settings_store(key, index)` | `settings.find` + `settings.set` | `svc_settings.h` | `int find(const char *)`, `bool set(int ix, int value)` | `set` stores and marks the setting changed; it writes the `.cfg` only with Auto Save Config on. It does not apply. No member of its own. |
| `settings_override(key, index)` | `settings.find` + `settings.override_value` (+ `settings.apply`) | `svc_settings.h` | `void override_value(int ix, int value)` | This run only. Does not apply; call `apply` when the value must take effect. |
| `settings_extra_get(key, val, n)` | `settings.extra_get` | `svc_settings.h` | `bool (*)(const char *key, char *val, int n)` | Keys with the prefix `Pad ` only. |
| `settings_extra_set(key, val)` | `settings.extra_set` | `svc_settings.h` | `bool (*)(const char *key, const char *val)` | |
| `machine_started`, `machine_frozen`, `machine_paused`, `menu_is_open` | `ctx.state` | `svc_ctx.h` | `uint32_t (*)(void)` | Bits `CTX_STARTED` 1, `CTX_PAUSED` 2, `CTX_FROZEN` 4, `CTX_MENU_OPEN` 8. |
| `machine_set_paused(on)` | `ctx.set_paused` | `svc_ctx.h` | `void (*)(bool on)` | Sets the flag only. |
| `menu_media_dir()` | `ctx.media_dir` | `svc_ctx.h` | `const char *(*)(void)` | "" when none, never null. |
| `ui_notice_pending()` | removed | `svc_ctx.h` | | Use `ctx.notice(text, true)`: it writes only when none waits. |
| `ui_notice(text)` | `ctx.notice` | `svc_ctx.h` | `bool (*)(const char *text, bool only_if_free)` | `false` replaces a waiting notice. |
| `board_has_uci()` | `sys.has_capability("uci")` | `svctab.h` (exists) | `bool (*)(const char *)` | The same test over the descriptor struck with the board probes; the C64 descriptor sets `board_caps`, so the answer is the old one. |
| `corefolder_dir(out, n)` | `ctx.core_folder` | `svc_ctx.h` | `bool (*)(const char *sub, char *out, int n)` | `sub` null = the folder itself; `"roms"`, `"saves"`, `"configs"`. False without a `.cor`. |
| `io_ddr_write(off, p, n)`, `io_ddr_read` | `io.ddr_write`, `io.ddr_read` | `svc_io.h` | `void (*)(uint32_t off, const void *p, uint32_t n)` | The DDR2 window at `0x01000000`. A write is complete on return. |
| `io_keys_read(m, restore)` | `io.keys_read` | `svc_io.h` | `bool (*)(uint8_t m[8], bool *restore)` | False: cannot say. |
| `sys_delay_us(us)` | `sys.delay_us` | `svc_sys.h` | `void (*)(uint32_t us)` | Busy wait, 10 us steps. |
| `boot_cold_reset(msg, n)` | operation `machine.cold_reset` | `svc_board.h` | `ops->call("machine.cold_reset", ...)` | Exists. `OPE_FAILED` with the reason in `msg`. |
| `boot_put_builtin_rom(put_op, msg, n)` | operation `machine.put_builtin_rom` (new) | `svc_board.h` | argument `put_op` (text) | One registry entry in the base. |
| `hw_esp32_rpc(cmd, args, nargs, reply, max, timeout_ms)` | `board.esp32_rpc` | `svc_board.h` | `int (*)(uint8_t, const uint8_t *, int, uint8_t *, int, int)` | Same result codes: n, -1 timeout, -2, -3; `timeout_ms <= 0` does not wait. |
| `board_sid_socket(socket, regulator, shunt, filter_caps)` | `board.sid_socket` | `svc_board.h` | `int (*)(int socket, int state, bool shunt, bool filter_caps)` | `SID_SOCKET_OFF` 0, `_ON` 1, `_ON_TYPE1` 2. The base inverts `filter_caps`. 0 done, < 0 bad argument. |
| `SetVideoPll(mode, ppm)` | `board.video_set_pll` | `svc_board.h` | `void (*)(int sys, int ppm)` | Includes `ResetHdmiPll`. One library user; kept because no library touches a PLL register and the base's video path programs the same PLL. |
| `ResetHdmiPll()` | removed | `svc_board.h` | | Part of `board.video_set_pll`; the old code always called both. |
| `video_color_timing(mode, ...)` | theirs | | | Decided: you keep your own copy of the table. |
| `picture_set_system(sys)` | `board.video_set_system` | `svc_board.h` | `void (*)(int sys)` | |
| `video_sys_name(sys)` | theirs | | | Print the label of the `System Mode` setting (`settings.text`) or the number. |
| `mouse_sink_scaling(key, value)` | removed | | | The base image answers the USB mouse items itself, from five settings the base owns (`svc_settings.h`; see "From our side"). |
| `media_writeback(why, msg, n)` | operation `media.flush_for_core` | `svc_board.h` | `ops->call("media.flush_for_core", ...)` | Exists. `why` was ignored by the base. |
| `tempdisk_set(auto_cleanup, subfolders)` | export "tempdisk" `set` | `tempdisk_api.h` | `void (*set)(bool auto_cleanup, bool subfolders)` | |
| `c64_cart_active`, `_name`, `_path` | export "c64cart" `active`, `name`, `path` | `c64cart_api.h` | `bool (*)(void)`, `const char *(*)(void)` | |
| `c64_cart_save(path, msg, n)` | export "c64cart" `save` | `c64cart_api.h` | `int (*)(const char *path, char *msg, int msglen)` | |
| `c64_cart_ef_clear(bank, high)` | export "c64cart" `ef_clear` | `c64cart_api.h` | `bool (*)(uint8_t bank, bool high)` | |
| `c64_cart_run_image(name, img, msg, n)` | export "c64cart" `run_image` | `c64cart_api.h` | `int (*)(const char *name, const uint8_t *img, char *msg, int msglen)` | The machine stays held in reset. |
| `sid_parse`, `sid_info_text`, `sid_plan`, `sid_requests`, `sid_mus_stereo`, `sid_mus_header` | export "sid" `parse`, `info_text`, `plan`, `requests`, `mus_stereo`, `mus_header` | `sid_api.h` | pointer forms of `sidfile.h` | `struct SidTune`, `struct SidReq` as you declare them. |
| `uci_start(host)` | export "uci" `start` | `uci_api.h` | `void (*)(const UciHooks *hooks)` | The callbacks table is `UciHooks` in the export (not `UciHost`: that name stays the base's internal type): `size` first, no `CoreCtx *` in its callbacks. Call it from the `start` hook. |
| `cbm1541_drive()` | removed | | | The class does not cross the boundary; the calls below act on drive A. |
| `cbm_drive_set_twist`, `_set_align`, `_set_reset_with_c64`, `_set_freeze`, `_set_geos` | export "cbm1541" `set_options` | `cbm1541_api.h` | `bool (*)(const Cbm1541Options *)` | One call, five fields. False without a drive. |
| `cbm_drive_image_name`, `_last_dir`, `_mode_name`, `_swap_delay_ms`, `_type`, `_bus_id`, `_powered` | export "cbm1541" `drive_info` | `cbm1541_api.h` | `bool (*)(int slot, Cbm1541Info *)` | `slot` 0. `mode` is `CBM1541_MODE_RW`, `_RO`, `_UNLINKED` (the old `mode_name` text). |
| `cbm_drive_create(path, format, name, msg, n)` | export "cbm1541" `create` | `cbm1541_api.h` | `int (*)(const char *path, int format, const char *name, char *msg, int msglen)` | `CBM1541_FORMAT_D64`, `_G64`, `_D81`. |
| `cbm_drive_mount(path, mode, msg, n)` | export "cbm1541" `slot_mount` | `cbm1541_api.h` | `int (*)(const char *spec, char *msg, int msglen)` | `spec` is the path, with `ro ` or `unlinked ` in front. No reset. |
| `cbm_drive_eject(msg, n)` | export "cbm1541" `slot_eject` | `cbm1541_api.h` | `int (*)(char *msg, int msglen)` | The write-back happens first. |

## ring library (47 pending: base 27, export 20, theirs 0, removed 0)

| Pending call | Final name | Header | Signature | Note |
|---|---|---|---|---|
| `sync_mutex_create()` | `sync.mutex_new` | `svc_sync.h` | `struct SvcMutex *(*)(void)` | Null: no memory. Priority inheritance, not recursive. |
| `sync_mutex_lock(m, timeout_ms)` | `sync.mutex_lock` | `svc_sync.h` | `bool (*)(struct SvcMutex *, uint32_t)` | 0 tries once; `SYNC_FOREVER` waits. |
| `sync_mutex_unlock(m)` | `sync.mutex_unlock` | `svc_sync.h` | `void (*)(struct SvcMutex *)` | |
| `sync_enter()`, `sync_leave()` | `sync.enter`, `sync.leave` | `svc_sync.h` | `void (*)(void)` | Nests. No blocking call inside. |
| `sys_ms_timer()` | `sys.ms_timer` | `svc_sys.h` | `uint16_t (*)(void)` | 16 bit, as `getMsTimer`: it wraps every 65.536 s, so use differences. Your declaration says `uint32_t`: change it. |
| `c64_stop(on)` | `io.stop` | `svc_io.h` | `void (*)(bool on)` | |
| `c64_mode()` | `io.cart_rd(0)` | `svc_io.h` | `uint8_t (*)(uint32_t reg)` | `io` names no register: the offset is relative to the window and is yours (core API TOML). |
| `c64_cartridge_type()` | `io.cart_rd(5)` | `svc_io.h` | | |
| `c64_reu_enable()`, `c64_set_reu_enable(v)` | `io.cart_rd(8)`, `io.cart_wr(8, v)` | `svc_io.h` | `void (*cart_wr)(uint32_t reg, uint8_t v)` | |
| `c64_reu_size()`, `c64_set_reu_size(v)` | `io.cart_rd(9)`, `io.cart_wr(9, v)` | `svc_io.h` | | |
| `c64_sampler_enable()`, `c64_set_sampler_enable(v)` | `io.cart_rd(0xE)`, `io.cart_wr(0xE, v)` | `svc_io.h` | | |
| `c64_dma_memonly()` | `io.bus_rd(3)` | `svc_io.h` | `uint8_t (*)(uint32_t off)` | `bus_rd` is new in `CoreIo` (the read side of `bus_wr`; default 0, `HwIo` reads the byte in 3.3). |
| `c64_set_dma_memonly(v)` | `io.ram_memonly` | `svc_io.h` | `void (*)(bool on)` | |
| `c64_videoformat()` | `io.bus_rd(1)` | `svc_io.h` | | |
| `cmd_if_slot_enable()` | export "uci" `slot_enable` | `uci_api.h` | `uint8_t (*)(void)` | Reads 0 unless `sys.has_capability("uci")`: the descriptor lists it and the board probe allows it. |
| `cmd_if_set_slot_enable(v)`, `cmd_if_set_slot_base(v)` | export "uci" `set_slot_enable`, `set_slot_base` | `uci_api.h` | `void (*)(uint8_t v)` | Ignored without the capability "uci". |
| `c64_cart_ram()` | export "c64cart" `ram_size`, `ram_read`, `ram_write` | `c64cart_api.h` | `uint32_t (*)(void)`, `void (*)(uint32_t off, uint8_t *dst, uint32_t n)`, `void (*)(uint32_t off, const uint8_t *src, uint32_t n)` | The pointer goes; copy in blocks. Not `io.ddr_abs_*`: the bodies use those inside the shared library. |
| `drive_a_image()` | export "cbm1541" `image_size`, `image_read`, `image_write` | `cbm1541_api.h` | `uint32_t (*)(int slot)`, `void (*)(int slot, uint32_t off, uint8_t *dst, uint32_t n)`, `void (*)(int slot, uint32_t off, const uint8_t *src, uint32_t n)` | `slot` 0. The same: no `io.ddr_abs_*` from your libraries. |
| `fpga_capabilities()` | `board.fpga_caps` | `svc_board.h` | `uint32_t (*)(void)` | `CAPAB_*` constants stay yours. |
| `get_fattime()`, `rtc_time_valid()` | `sys.wall_time` | `svc_sys.h` | `bool (*)(uint8_t t[6])` | `t[0]` is the year - 2000. False: no clock or not set. |
| `app_version_string()` | `sys.app_version` (and `sys.app_git`) | `svc_sys.h` | `const char *(*)(void)` | The text of the application image. `app_git` is the short hash of the image's commit, build identity like the version; the ring and the monitor both read it. |
| `settings_override(c, key, value)` | `settings.find` + `settings.override_value` + `settings.apply` | `svc_settings.h` | | Compare the value yourself for your "0: already" answer. |
| `ops_job_step(c)` | `ctx.job_step` | `svc_ctx.h` | `bool (*)(void)` | |
| `ops_job_progress(c, pct, path)` | `ctx.job_progress` | `svc_ctx.h` | `void (*)(int pct, const char *path)` | |
| `ops_job_cancelled(c)` | `ctx.job_cancelled` | `svc_ctx.h` | `bool (*)(void)` | Read and cleared. |
| `core_eeprom_read(c, off, p, n)` | export "c64cart" `eeprom_get` | `c64cart_api.h` | `void (*)(uint8_t out[2048])` | The array, no offset. Leaves the dirty flag alone. |
| `core_eeprom_write(c, off, p, n)` | export "c64cart" `eeprom_put` | `c64cart_api.h` | `void (*)(const uint8_t data[2048])` | Writes the array and clears the dirty flag. |
| `trx_target_path(c, name, ext, out, n)` | `ctx.target_path` | `svc_ctx.h` | `bool (*)(const char *name, const char *ext, char *out, int n)` | |
| `c64_cart_active`, `_name`, `_path`, `_crt_type` | export "c64cart" `active`, `name`, `path`, `crt_type` | `c64cart_api.h` | | |
| `c64_cart_image_bytes()` | export "c64cart" `image_bytes` | `c64cart_api.h` | `uint32_t (*)(void)` | |
| `c64_cart_image_read(c, off, dst, n)`, `_image_write` | export "c64cart" `image_read`, `image_write` | `c64cart_api.h` | `void (*)(uint32_t off, uint8_t *dst, uint32_t n)` | |
| `c64_cart_for_state(c, path, msg, n)` | export "c64cart" `for_state` | `c64cart_api.h` | `int (*)(const char *path, char *msg, int msglen)` | `path` null empties the port. |
| `c64_cart_require()`, `c64_cart_prohibit()` | export "c64cart" `require`, `prohibit` | `c64cart_api.h` | `uint16_t (*)(void)` | The cartridge definition's flags as the cartridge in the port has them; 0 without a cartridge. Added to your list in `2d4c2dcc`. |
| `c64_cart_writable()` | export "c64cart" `writable` | `c64cart_api.h` | `bool (*)(void)` | The cartridge has flash or an EEPROM the C64 can change; false without a cartridge. Same commit. |
| `cbm1541_slot_mount(c, spec, msg, n)` | export "cbm1541" `slot_mount` | `cbm1541_api.h` | | |
| `cbm1541_drive_a(c, ...)` | export "cbm1541" `drive_info` | `cbm1541_api.h` | `bool (*)(int slot, Cbm1541Info *)` | Type text: map `CBM1541_TYPE_1581` to "1581", anything else to "1541". |

`ctx.media_source(slot, buf, len)` (`svc_ctx.h`) is in no pending list; the night session took it for the media paths of the ring and the monitor.

## monitor library (3 pending: base 3, export 0, theirs 0, removed 0)

| Pending call | Final name | Header | Signature | Note |
|---|---|---|---|---|
| `sys_yield()` | `sys.yield` | `svc_sys.h` | `void (*)(void)` | Not the same as `sleep_ms(0)`. |
| `sync_enter()`, `sync_leave()` | `sync.enter`, `sync.leave` | `svc_sync.h` | `void (*)(void)` | |

The screen of the monitor is not a pending call: the export "textgrid" (`textgrid.h`, major 1: `render`, `cols`, `rows`, `line`, `attr`, `blank_rows`, `cursor`, `sig`, `enter`, `tick`; `enter` and `tick` replace the base's calls of `monitor.open` and `monitor.tick` by name in `menuflows.cc`, so those operations become calls inside your library) that a node with layout "terminal" and `grid = "textgrid"` names.

## From our side into your code

| Call | Final form |
|---|---|
| `trx64u_uci_df1c` (from `c64_cart.cc`) | Gone. The cartridge library calls the export "uci": `set_slot_enable(1)`, `set_slot_base(0x47)`. |
| `u64_get_usb_hid_config_value` (weak in `usb_hid.cc`) | Gone. The base image defines it in `mousesink.cc` and reads five mouse settings by name. The base now owns and declares `Mouse Mode`, `Mouse Sensitivity`, `Mouse Acceleration`, `Mouse Wheel Sensitivity`, `Mouse Wheel Direction` (keys and values in `svc_settings.h`), like `Overlay Transparency`, `Color Scheme`, `Filename overflow squeeze`, `Navigation Style` (with its first-run default on the Commodore product) and `Joystick Input`: drop your copies of all ten from the menu TOML (today four of them carry `apply = "ui.fx"`). The stored values carry over because the keys stay. The base lists a row only where the code can do it (T228 R20): `Joystick Input` needs the capability `kbd_joystick` in the core's capability list and `Mouse Mode`, `Mouse Wheel Sensitivity` and `Mouse Wheel Direction` need `mouse_driver` (none of the cores of this tree names either: no WASD register is decoded, and the mouse sink consumes every report); `Mouse Sensitivity` and `Mouse Acceleration` are listed for a core with a mouse. Your menu TOML hides a row of the base by its key in the top-level list `hide` (the trx64u TOML hides `Color Scheme`, R7); a hidden or absent key has no setting (`find` returns -1, no `base_setting_changed`). |
| `trx64u_sid_service`, `trx64u_sid_cancel`, `trx64u_kernal_put`, `trx64u_basic_put`, `trx64u_load_assoc_cfg`, `u64cfg_power_on_keys` | Calls inside your library: `c64.cc` and `c64_go.cc` are yours. |
| Hooks | `CoreHooks` (`core_hooks.h`): `init`, `release`, `warm_reset`, `cold_start`, `media_flush`, `media_pending` as before, and new `start` and `menu_closed` (not `resume`). `menu_closed` runs on the main loop task without the core lock, after the menu is hidden and before the machine is let go: the ring's pending frame lands there (rules in `core_hooks.h`). |

## The key table of the UI TOML (base 3.3)

The rows of `menukeytab.cc` for the monitor and Timewarp scopes (`KS_MONITOR`, `KS_TIMELINE`) move into the core's UI TOML, all of
them, the declines and the swallow included. The base reads them from the menu record (`tools/descrec.py` takes `[[key]]` with base
3.3; until then the writer refuses it) and puts them where those generic rows stand today: after an application's screens, before
the menu's grammar. Rows are tried in the order of the file; the first that matches takes the key. Each field maps one part of
today's `KeyRow`:

```toml
[[key]]
layout = "terminal"      # the scope: while a node with this layout is shown; `node = "<id>"` names one node instead
key = "any"              # one key, or "any" (every key); `codes = [32, 126]` instead takes a range of key codes (the swallow)
do = "op"                # "op" (MI_OP, the default when `op` is given), "decline" (MI_DECLINE: the key goes on to the menu's
                         # grammar) or "swallow" (MI_SWALLOW: the key is spent, nothing happens)
op = "monitor.key"       # the operation (do = "op")
value = -10              # the operation's int argument "value" (KF_ARG); absent: none
pass_key = true          # the key goes to the operation as its int argument "key" (KF_KEYARG); default false
status = true            # the answer text is shown in the status line (KF_STATUS); default false
status_ok = true         # also when the operation succeeded (KF_STATUS_OK); default false
close = true             # the menu closes after the operation (KF_CLOSE); default false
directive = true         # the answer text is "close" or "goto SPEC" for the menu (KF_DIRECTIVE); default false
direct = true            # called on the menu task, not through the executor, without the core lock (KF_DIRECT); default false
when = "!monitor.wants_stop"   # the row counts only while this state holds (non-empty, not "0" or "no"); "!" negates
```

Key names are those of `uiapp.cc` `key_code` (`up`, `down`, `left`, `right`, `return`, `esc`, `del`, `runstop`, `space`, `home`,
`pgup`, `pgdn`, `tab`, `f1` .. `f8`) plus `f10`, `scrlock`, `menu`, `ctrl+<letter>` and one printable character; in the key table
"any" means every key. With the table read from the record, the generic `KS_MONITOR` and `KS_TIMELINE` rows of `menukeytab.cc` go.

## The windows of a core's API TOML (base 3.3)

Until the API TOML of T213.1 has its full format, it holds the register windows `io.window` hands out; `tools/descrec.py` compiles
them into the core's description record (D11):

```toml
[[window]]
name = "audio_mixer"     # the name io.window takes
address = 0xA0000500     # the CPU address of the block
size = 0x14              # bytes
```

The base keeps no hand-written table of the registers it drives itself (`basereg.def` is gone, T239.3 D30 point 4): its source is the base blocks of the TOML A the app is linked with (`coreapp/gateware/host.toml` for the shared app and the host app), handed to the app as a table generated at build time. A `[[window]]` is checked for name, address and size, and it is refused when it covers a byte of such a block, by both readers (`descrec.cc`, `tools/descrec.py`); the blocks of one TOML A may not overlap, and a core's block is checked only against the blocks of its own TOML A. The case LED select is reached
through `board.case_led_select`. `io.bus_wr` and `io.bus_rd` take every offset of the bus-side I/O block.

A core delivers its two TOML contracts as two files: `menu.toml` (UI TOML: menu, key table) and `desc.toml` (API TOML: the core's
data sections and `[[window]]`). With 3.3, `cor_pack.py pack` takes `--desc` more than once and compiles them into one record 6; a
table or key present in both is refused. Granted to trx64u (checked against the base's own writes): `u64_userport` (+0x05),
`u64_hdmi_connectors` (+0x08, +0x09), `u64_pwm_duty` (+0x0D) of the U64 block, `speaker_enable` (0xA000000C, write only: a read
returns the board revision), the mixers, the resampler and both palettes.

## Where a core's menu goes

A library's image carries a menu record of contributions only: nodes, file actions, settings, tabs and a poll (`u2a_pack.py
--menu` refuses areas, `hide` and `rename`, which only a core's description has). The core's own menu, your `menu.toml`, is the
core's description: record 6 of the `.cor`, packed by `tools/cor_pack.py pack --desc` together with the data sections of the
core's `desc.toml`. So your libraries drop `LIB_MENU = menu.toml`; `menu.toml` and `desc.toml` are delivered with the libraries
and packed into `trx64u.cor` by this repository's pipeline (the hand-over contract gets that line with P4).

## Long operations (base 3.3)

Work that needs the machine for long is a stepped operation: the menu calls it again while it answers `more`, each call holds the
core lock for one step, the progress line shows `ctx.job_progress`, and ← cancels (`ctx.job_cancelled`). A UI node takes it with
the attribute `job`; with 3.3 a `[[task]]` takes `job = true` as well (the menu's job path already runs a Task). Commands that
arrive on a task without the lock (a `direct` key row, as the monitor's terminal) are queued and run step by step by the
library's own poll (lock held), never inside the key handler.

## The core's code in its library: hooks, mount slots, the drive reset, the CBM loader (base 3.3, 2026-11-23)

These answers are for the cut of trx64u, in which `c64.cc` goes to the base library of TRX64-Ultimate.

- The library that holds a core's code: `hooks = "<library name>"` in the [core] section of the core's API TOML. `cor_pack` refuses the core in two cases: the named
  library is not in the bundle, or its file does not list the export `corehooks=1`. The base takes CoreHooks from that library. `descrec_core` accepts the control
  and [mount] sections of a record that names a hooks library, because the code comes with the library.
- Mount slots: `slot_spec`, `slot_mount` and `slot_eject` are appended to CoreHooks (`core_hooks.h`). `slot` is the position in the [mount] list. The meaning is
  that of MountSlots (`desc.h`). `ctx.state` gets `CTX_MEDIA_RESTORING`, and `ctx` gets `mount_notify(slot)` (`svc_ctx.h`).
- `cbm1541` gets `reset` (`cbm1541_api.h`). It resets drive A when there is one, as `c64.cc` did, and it never makes a drive (-1 when there is none).
- The CBM program loader stays here as the export "cbmprg" (`cbmprg_api.h`), because five cores use the same code. It is exported in base 3.3 (`libexports.cc`), so your copy can go.
- `menuvlist.cc` names `c64.run_t64` and `c64.dma_t64` in generic code. That breaks D24. The fix is a container row in the UI TOML (the extension, the lister, and its
  actions as [[filetype]] actions are written), built with the cut (P4). Until then the names stay, and they resolve to your operations of the same name. The
  T64 lister (`t64_list`) stays here.
- The first reset at power-on: the bundle's libraries are loaded and the groups are filled (`lib_board_load`, run.cc) before `boot_start` makes the first reset,
  so the `init` hook has io and settings there (CTRL+P, CTRL+N). 3.3 keeps this order, and a test holds it.
- Who notifies a slot change (advisor, 2026-11-23): the engine that makes the change. `ctx.mount_notify` takes the slot's name from [mount], as
  `ctx.media_source` does, and runs on any task, because it only marks the slot. The shared libraries notify every change they make to their own slot: "Drive A"
  for `cbm1541`, and "Cartridge" for `c64cart`, `for_state` included. A core's code that calls them does not notify again.
