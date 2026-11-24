# The boundary of the trx64u libraries (T239.3 P1)

> Historical (T239.3 P1, kept as the record of the boundary): the code it calls moving (`trx64u_ops`, `u64cfg`, `ring*`, `monitor`, `uci_api.h` and the
> others) left this repository with T239.3 D44 and D48-D50; TRX64-Ultimate builds trx64u from it. The base calls it names are in `app/sdkpkg/api.md`.

## Summary

What the base must add, in the order it can be built:

(a) Base 3.2 (built on another branch): named exports (`LibRecord` lists {name, major, table, size}; `SvcLib::find` and `LIB_NEEDS` match export names first), the library budgets, the D19 refusal (`lib.mk` stops a library with `.init_array`, `.fini_array` or `__cxa_atexit`), and the SDK header `C64Api`. Rows: 6.3, 7, 12.4.

(b) Base 3.3, the new groups and members, with the contracts of section 11 as decided:
- `sync` (six calls: `mutex_new`, `mutex_lock`, `mutex_unlock`, `mutex_free`, `enter`, `leave`; header `svc_sync.h`). A mutex with priority inheritance, and `enter` and `leave`: a short critical section that nests and holds no blocking call (D20 puts the 32-bit TRX register accesses into the TRX64-Ultimate library).
- `settings` (13 calls, `cfg_value` and `cfg_text` among them), `ctx` (10 calls, `media_source` among them), `io` (24 calls: the 23 of `CoreIo` named in 8.2, and `bus_rd`, which is new in `CoreIo` as the read side of `bus_wr`; `svc_io.h` names no register of any core). Headers `svc_settings.h`, `svc_ctx.h`, `svc_io.h`.
- `board` (6 calls, `svc_board.h`): `fpga_caps`, `is_commodore`, `esp32_rpc`, `video_set_system`, `video_set_pll`, `sid_socket(socket, state, shunt, filter_caps)`. Each has its second user named in 11.5. The calls for the UI look, the name squeeze, the keyboard joystick, the colour timings and the I2C bus are not in it (4.4).
- `sys` gets `ms_timer`, `yield`, `delay_us`, `wall_time`, `app_version`, `app_git` (11.6, `svc_sys.h`); `fs` gets `list_long` (`svc_fs.h`). `net.gated` is not a member: it is the state `net.gated`, read with `ops->state`.
- Base operations and settings: the operation `machine.put_builtin_rom`, the states `set.<key>` and `net.gated`, and ten settings of the generic application: the five of 4.4 (`Overlay Transparency`, `Color Scheme`, `Filename overflow squeeze`, `Navigation Style`, `Joystick Input`) and the five USB mouse settings (`Mouse Mode`, `Mouse Sensitivity`, `Mouse Acceleration`, `Mouse Wheel Sensitivity`, `Mouse Wheel Direction`; 6.2, 12.2). Keys and values are in `svc_settings.h`.
- Two core hooks, `start` and `menu_closed` (12.1, table `CoreHooks` of `core_hooks.h`), and a `grid` parameter for the layout `terminal` with the SDK header `textgrid.h` (12.2).
- The export interfaces of the shared libraries (6.1): `c64cart_api.h` ("c64cart"), `cbm1541_api.h` ("cbm1541"), `uci_api.h` ("uci"), `sid_api.h` ("sid"), `tempdisk_api.h` ("tempdisk"), all major 1, none takes a `CoreCtx`.
- The contract of every 3.3 call is in the headers of `coreapp/src` named above; section 11 stays as the index. The pending lists of the TRX64-Ultimate libraries with the final name of each call are in `boundary-trx64u-handover.md`.
- `SvcTrx`, `SvcRing` and `SvcC64` leave `LibApi` (D20) when the TRX64-Ultimate libraries export "trx", "ring" and "c64"; the tables in `appsvc.cc` go.

(c) Base fixes that are not API:
- The per-owner account of S14 (2.7): memory, files, sockets, jobs and the debug block; the owner is set per task around every entry into a library; `appsvc_end` closes only what the ended application holds (2.6).
- The `set.<key>` branch of `ops->state` (2.3).
- The poll passes `when_frozen` on (2.5); the start order of 2.4.
- A second listing call with the whole file name, because `SvcFsEntry` is frozen at 72 bytes (3.4). A create flag for `CoreFs::open(rw)` is not needed (3.4).
- The SDK runtime source of 13.1 and the headers of 13.2.
- The budget check of S13 with two heap figures per library, the permanent one (what stays after the load) and the peak (during load and start); the figures per library are measured by the TRX64-Ultimate session (R5, 13.4).
- `app/lib/lib.mk` force-includes `hidden.h` for C++ sources as well (`-include $(SDK_HIDDEN)` in `LIB_CXXFLAGS`), so that a C++ library's call into another file of its own stays direct; the SDK package's `mk/lib.mk` carries the same line.
- The base honours a poll's `when_frozen` (2.5): `run.cc:701` passes `false` for every library poll today, and `LibPollDecl` has the field.

What TRX64-Ultimate does:
- Moves `trx64u_ops`, `u64cfg`, `c64.cc` and `c64_go.cc` into the core's library, `ring`, `ringfile`, `ringops` into the library `ring`, `monitor`, `monsrv` and the trxmon engine into the library `monitor`; turns the data of `c64.cc` into TOML (D8).
- Exports "trx" and "ring" (major 1, the layouts of its `app_api.h`; "ring" adds `dump`, `save`, `load`, `trace_writes`, `load_entry`, `save_native`) and "c64" (major 1, `C64Api`); provides a text grid for the layout `terminal`.
- Puts the 32-bit TRX register accesses, the `resid8` byte writes and the `slim` read into its library, with `sync.enter` and `sync.leave` around the 32-bit ones.
- Drops `FileManager` for `api->fs`, which removes the constructors that D19 refuses (3.4); writes the monitor's `plat_*` over `api` (section 10); keeps its own copy of the colour timing table (4.4).
- Calls `ring` and `monitor` from the core hooks `start` and `menu_closed` through `SvcLib`; reads the settings named in 4.4 through `settings.value`.

## 1. General

1.1 This page maps every symbol that the moving trx64u code takes from outside the SDK to exactly one target. It maps the other direction as well: every place where the core application calls into the moving code.

1.2 The moving code is `trx64u_ops`, `u64cfg` (library `trx64u`), `ring`, `ringfile`, `ringops` (library `ring`), `monitor`, `monsrv` and the trxmon engine (library `monitor`), as the TRX64-Ultimate session lists them (U18 D5; T239.3 D2, D12). `c64.cc` and `c64_go.cc` move with them as the core's own code (2.11).

1.3 Basis of the symbol lists: `software/trx64u/BOUNDARY.md` of the TRX64-Ultimate repository, measured on commit `3b8e2955` with `riscv64-elf-nm` over the objects of the moving files. That measurement lists symbols. It does not list the fields of `CoreCtx` and `CoreIo` that the code reaches, because those are types and inline code. Section 8 lists them. Section 9 lists the headers.

1.4 Basis of every row: the definition in this tree (`t239.3-boundary`, base `de3fbfc2`), read from the code. A row says what the code does. A comment is not evidence. Line numbers are those of this tree.

1.5 The kinds of target:

| Kind | Meaning |
|---|---|
| `api` | An existing call of `LibApi` (`group.member`), or a record of `lib_register`. The row says whether it does the same as the symbol and what differs. |
| `new-api` | A new call of `LibApi`. Section 11 gives group, name, signature and contract. The row names the other users. |
| `sdk-runtime` | Provided inside the library by the SDK build: libgcc, the pure parts of the C library, `printf` and `puts` over the log call, `operator new` and `delete` over `SvcMem`. |
| `shared-lib` | An export of a shared library of this repository (D12; T104.1 D15 for `cbm1541`). The row names the library and its call table. |
| `theirs` | Code inside the TRX64-Ultimate libraries, or a call between them through `SvcLib`. |
| `decided` | Depends on U18 R3 or R4, which are decided (D19, D20). The row states the answer. |
| `base` | The code stays in the core application, or its definition moves into it. The library does not call it any more, or calls it as a `settings` or `ctx` member (section 12). |

1.6 The two decisions that this page waited for are taken (Alex, 2026-11-22). Every row that depended on them is a `decided` row (section 7):

| Id | Decision | Rows |
|---|---|---|
| R3 | D19: the build refuses a library with constructors or destructors. `lib.mk` stops `.init_array`, `.fini_array` and `__cxa_atexit`; the loader runs none. | `__cxa_atexit`, `__dso_handle` |
| R4 | D20: `SvcTrx`, `SvcRing` and `SvcC64` leave the base API. They come from libraries through `SvcLib`: the exports "trx" major 1 and "ring" major 1 with the layouts of the TRX64-Ultimate `app_api.h`, "c64" major 1 as the SDK header `C64Api`. | `trx_io_*`, the `SvcRing` rows of section 12 |

1.7 Abbreviations in the column "Users": `ops` = `trx64u_ops.cc`, `cfg` = `u64cfg.cc`, `ring` = `ring.cc`, `rfile` = `ringfile.cc`, `rops` = `ringops.cc`, `mon` = `monitor.cc`, `msrv` = `monsrv.cc`.

1.8 The rules of this page, taken by the coordinator under Alex's delegation on the night of 2026-11-22 (spec section 5) and within D6-D20:
- `c64.cc` is the trx64u core's own code: its data become TOML (D8), its hooks come from the TRX64-Ultimate library (D10). `c64_go.cc` follows it. `c64_cart.cc` is the shared cartridge library (D12); `cbm_run.cc` the shared program-loading library (T104.1).
- The base API takes a call only when more than one core or library uses it, or when it belongs to the generic application by nature. A call that has one user is a setting or an operation of the base, or it falls away.
- `sync.enter` and `sync.leave` stay (D20). `net.gated` is a state. `sys.yield` and a 1 ms `sys.ms_timer` are new. `ops->state` gets `set.<key>`.
- S14 covers everything a library holds (2.7).

## 2. What reading the code changed

2.1 The handlers get `CoreCtx *`. In the package, `CoreCtx` is an opaque forward declaration (`ops.h:10`). The moving code reaches 18 fields of it and 18 methods of `CoreIo` (section 8). Without a call for each of them the code cannot compile against the package. This is the largest part of the boundary. BOUNDARY.md does not show it.

2.2 `cfg_value` and `cfg_text` are not library code. They are generic settings helpers that sit in `trx64u_ops.cc:27-62`. So is `trx_target_path` (`trx64u_ops.cc:753`): it reads `c->media_dir` and lists `/`. Six base files call them (`menuflows`, `netfs`, `netops`, `diag`, `run`, `c64`). They move into the core application (section 12).

2.3 The registry call is not the executor call. `ops_call` queues on the executor when one exists (`ops_glue.cc`: `c->exec->call`). `api->ops->call` runs `reg_->call` on the calling task, with no executor and no core lock (`libload.cc:158`). `ops_state` answers `set.<key>` from the settings (`ops_glue.cc`). `api->ops->state` does not (`libload.cc:173-177`). The monitor reads `set.<item>` (`monitor.cc:261-266`). So `api->ops->state` returns nothing for it today. Decided: `svc_state` gets the `set.<key>` branch, with the key folding and the labels of `setting_state` (3.1, `ops_state`).

2.4 The groups are installed late. `lib_board_load` (`run.cc:607`) runs `lib_register` before the application host exists. The host sets `ctx_`, `api->dlg` and `api->job` in `install()` (`apphost.cc:66-68`), and `api->trx`, `api->net`, `api->c64` in `set_machine_groups` (`run.cc:707-711`). Until then `api->ops->call` answers "No operations here" (`libload.cc:150`) and every `api->fs` call fails (`fs_of()` is null). `trx_ring_engine_init()` and `ring_autostart` run at `run.cc:691-692`, before that point. A library init that needs `ops`, `fs`, `job`, `trx` or `net` cannot run inside `lib_register`. It needs the core hook `start` (12.1).

2.5 The poll is served in this tree. `run.cc:698-705` starts a task for each declared `LibPollDecl`. The statement "declared and kept, and not served yet" (`libapi.h:21`) and the rows R2 and S3 of the spec are out of date. The served poll has three limits that matter for the monitor and the ring. It holds the core lock for every call (`PollTask`, `service.cc`). It never runs before `started`, and not while the menu is open: `poll_allowed` is `started && (!frozen || when_frozen)` (`service.cc:6`), and `run.cc:701` passes `false` for `when_frozen`, so the field `LibPollDecl::when_frozen` is not passed on. Its task runs at idle+2 (`above_ui` 1, `run.cc:702`).

2.6 `appsvc_end()` is the end hook of every sub-app (`apphost.cc:392`). It closes every socket in `s_socks` and releases the debug block (`appsvc.cc:439-444`). The list has no owner. A sub-app that ends (trex_crt) would close the listener of the resident monitor library and drop its run control. `stop_jobs()` (`apphost.cc:391`) cancels every job in the same way. The job table has `APPJOB_MAX` = 4 slots shared with the sub-apps (`apphost.h:23`). Decided (S14): the end of one application closes only the sockets, jobs and the debug block that this application holds, so the listener of a resident library survives it (2.7).

2.7 The account is the library's only during `lib_register` and `app_main`. `cur_` is one field of the loader. It is set at `libload.cc:671` and `libload.cc:749` and cleared after. Four kinds of holding fall outside it:
- Memory. A block that a handler allocates later, on the executor or on a poll task, goes to "nobody's" account (`list_of(-1)`).
- Files. `svc_open` files a handle under `cur_`, and `cur_` is -1 outside `lib_register` and `app_main` (`libload.cc:207`). `free_account` closes the files of the account it frees (`libload.cc:312`), so a file that a handler opened is on nobody's account and stays open when the library is freed.
- Sockets. `s_socks` has no owner (`appsvc.cc:256`).
- Jobs. The job table has no owner (`apphost.h:121`).

Decided (spec section 5, S14): the account covers everything a library holds, memory allocated in its handlers, files, sockets and jobs, and the debug block of 2.6. The end of one application closes only what that application holds. The base change: the owner is set around every entry into a library (the handlers of operations, states, events and polls, the core hooks, the job function), per task, because several tasks enter libraries and `cur_` is one field; every holding records the owner. The test S14 ("freeing the library gives all of it back") has to cover the four kinds. `operator new` and `malloc` of the SDK runtime map to `SvcMem` as asked; the claim holds only after this change.

2.8 The clocks differ. `api->sys->ms` is `xTaskGetTickCount() * portTICK_PERIOD_MS` (`libload.cc:78`, `libboard.cc:51`) and the tick is 5 ms (`configTICK_RATE_HZ` 200). `getMsTimer` is the 1 ms hardware counter of the ITU. The ring measures with it (`st->ms_stop`, timeouts of 10 ms and 60 ms: `ring.cc:1028`, `1546`). `CoreIo::now_ms` extends the same counter (`hw.cc:207`). So `sys.ms` is not a replacement for `getMsTimer`. Decided: `sys.ms_timer` (11.6).

2.9 `FileManager::getFileManager()` is a function-local static in an inline function (`filemanager.h:167-174`). A library that calls it builds its own `FileManager` with its own constructor and its own `__cxa_atexit`. That is a second file manager, not the board's. So the rows of `ringfile` and `ringops` that name `__cxa_atexit`, `__dso_handle`, `FileSystem_Root`, `mstring`, `pattern_match` and the critical sections come from this one source. They disappear when the file calls go through `SvcFs` (section 3.4). R3 is decided: the build refuses a library with constructors or destructors (D19), so a remaining `.init_array` stops the build and does not surprise the loader. Whether any file has a global constructor is not measured. BOUNDARY.md lists symbols only. The check is `riscv64-elf-readelf -S` on the seven objects.

2.10 `printf` formats in the moving code use `%d %u %x %s %c` and widths. None uses `%ll`, `%f` or `%p`; 64-bit values go through `u64dec` (`ringfile.cc`). `u2p_vsnprintf` of `u2p_compat.c` is not enough for `snprintf`: it knows `d u x X s c`, and `%%` prints two characters. The SDK `snprintf` has to be written for this list. `ringfile.cc:37` says the board's `printf` has no precision, which is why `nf_copy` exists; an SDK `snprintf` with `%.*s` makes `nf_copy` unneeded.

2.11 `c64.cc`, `c64_go.cc`, `c64_cart.cc` and `cbm_run.cc` are named by neither U18 D5 nor T239.3 D12. The night decision places them (1.8):
- `c64.cc` holds the TRX2 descriptor and its hooks: `c64_init`, `c64_release`, `c64.service`, the ROM, PRG, CRT and T64 operations (`c64.cc:60-110`, `695-770`). It is the trx64u core's own code and moves with the core: its data become TOML (D8), its hooks are code of the TRX64-Ultimate library (D10). Every row of section 12 that named a call from `c64.cc` into the moving code is then a call inside one library (12.2).
- `c64_go.cc` has one user: `c64.cc` (`c64_go_available`, `c64_go`, `c64.cc:297`, `352`). It follows `c64.cc`.
- `c64_cart.cc` becomes the shared cartridge library here (D12). Its users today are `trx64u_ops`, `ringops` and `c64.cc` (and its tests); D12 names C64SCPU and C128 as the next. Rows in 6.1.
- `cbm_run.cc` becomes the shared program-loading library here (T104.1). Its users today are `vc20.cc`, `c264.cc`, `c128.cc`, `c64scpu.cc` and `pet.cc`; `c64.cc` does not (no `cbm_run` symbol in it; it has its own PRG job, `c64.cc:290-365`). It is not a call of the moving code.
- The reach of `c64.cc` itself (it includes `memops`, `boot`, `cbm_prg`, `t64`, `cbm1541`, `online`, `diag`, `netops`, `mousesink`, `newfile`, `hk_defaults`) is a boundary of its own. It is not measured: no `nm` over its object exists. 12.5 lists what is known.

2.12 `app_version_string()` is generated per build of the application image (`host-app/target/common/rules.mk:37-60`). The ring file writes it as `appVersion` into every `.c64re` (`ringfile.cc:1158`). A library has no such function. Row in section 6.

## 3. Kind `api`

### 3.1 Registration and operations

| Symbol | Users | Target | Note |
|---|---|---|---|
| `OpRegistry::add_op` | ops, cfg, rops, mon | `LibRecord.ops` (`LibOpDecl`) | Same four fields: name, `ArgSpec` list, count, `op_fn`. 70 operations: ops 32, cfg 17, rops 17, mon 4. All registrations are unconditional and static (`trx64u_register`, `u64cfg_register`, `ringops_register`, `monitor_register`), so they fit the static `LibOpDecl` arrays of the image. `r.contributor(...)` falls away: the loader sets the owner from the library name, so the names of the owners change from `trx64u`, `u64cfg`, `timewarp`, `monitor` to the library names. The table limit is `OPS_MAX` 400 (`ops.h:151`). |
| `OpRegistry::add_state` | ops, cfg, rops, mon | `LibRecord.states` (`LibStateDecl`) | Same two fields: name, `state_fn`. 69 states: ops 24, cfg 28, rops 16, mon 1. Limit `STATES_MAX` 320. |
| `ops_call` | ops, cfg | `api->ops->call` | Same arguments (`name`, `OpIn`, `nin`, `OpResult`). Differs by the task that calls. `ops_call` queues on the executor when one exists (`ops_glue.cc:95-100`), and `Executor::call` runs the handler at once only on the executor's own task, where the core lock is held (`exec.cc:86`). `api->ops->call` runs `reg_->call` on the calling task and takes no lock (`libload.cc:158`). From a handler (the executor's task) the two are the same. From a poll task they are not: the poll holds the core lock (`service.cc:31`), so `ops_call` would queue on the executor, which waits for that lock, and the call would not return; `api->ops->call` runs the handler at once under the poll's lock. From a job task or any other task `ops_call` queues and the handler runs under the core lock; `api->ops->call` runs it at once with no lock. Needs the `ctx_` of 2.4. The code calls these names: `settings.set`, `settings.load_file`, `drive.mount_run`, `drive.set_rom`, `drive.mount`, `drive.eject`, `drive.swap`, `drive.set_power`, `c64.put_kernal` and the `put_op` of a ROM role. A library that calls them needs the library or the core that registers them. |
| `ops_state` | mon | `api->ops->state` | Same for registered states. Differs for `set.<key>` (2.3). Decided: `LibLoader::svc_state` (`libload.cc:173`) gets the `set.<key>` branch of `ops_state` (`ops_glue.cc:174-178`), with the key folding and the labels of `setting_state` (`ops_glue.cc:136-157`): a space and `_` compare equal, case is folded, an enum setting answers the label of its value, a text setting its text (leading blanks kept), and the answer is null when the core has no such setting. `plat_config` (section 10) maps to it. `settings.cfg_text` is not an alternative: it answers text settings only, trimmed, and an enum gives an empty string (`trx64u_ops.cc:54-62`, `Settings::text` `settings.cc:289`); it matches the key by `strcasecmp` without the `_` folding. |
| `boot_cold_reset` | ops | `api->ops->call("machine.cold_reset")` | `machine.cold_reset` is `op_hook<boot_cold_reset>` (`boot.cc:376`). The same function runs. The call returns `OpResult.code` (`OPE_FAILED`) and the reason in `msg`; the function returns 1 and writes `msg`. Other cores of this repository call `boot_cold_reset` directly (`c264`, `zxnext`, `apple2`, `amiga`, `cpc`, `atari_media`, `pet`, `cbm_run`). |
| `boot_put_builtin_rom` | ops | `api->ops->call` of a new base operation | No operation exists for it. `machine.reload_roms` reloads every ROM, this puts one role (`put_op`). The base registers `machine.put_builtin_rom(put_op)` next to `machine.cold_reset` (`boot.cc:375-378`). That is a registry entry, not a `LibApi` member. Only `ops` calls it (3 sites). |
| `media_writeback` | cfg | `api->ops->call("media.flush_for_core")` | `op_flush_for_core` calls `media_writeback(c, WB_WHY_CORE)` (`engines.cc`). The code calls it with `WB_WHY_POWER`. `wb_flush` ignores `why` (`writeback.cc:332`, `(void)why`), and `media_writeback` passes it nowhere else, so the result is the same. |
| `desc_has_capability(c->desc, "uci")` and `c->board->probes & BC_UCI` | ops | `api->sys->has_capability("uci")` | Inline code, not a symbol. `has_capability` reads `desc_has_capability(d_, ...)` (`libscan.h:62`) over the descriptor that `run.cc:465-475` has already struck with the board probes (`board_effective_caps`). For a descriptor with `board_caps` set the two tests give the same answer. For one without `board_caps` the probe test is not made. The one descriptor that lists `uci` is `c64.cc` and it sets `board_caps` (`run.cc:468`), so the test of `trx64u_ops.cc:289` and this one agree today; the export "uci" uses this test for its slot calls (`uci_api.h`). |

### 3.2 Tasks and time

| Symbol | Users | Target | Note |
|---|---|---|---|
| `xTaskCreate` (`ring_task`, `ring.cc:2817`) | ring | `LibRecord.poll` (`LibPollDecl`, S3) | The task loops `trx_ring_poll()` every 100 ms, and starts the log 6 s after the start. As a declared poll it runs under the core lock, never while the menu is open (2.5) and at idle+2 (now idle+1). While the menu is open the machine is stopped, so no marker comes; the scrub and the restore call `trx_ring_poll` themselves. The 6 s start delay becomes state in the poll operation. The core lock now excludes every operation during a poll: one poll reads up to `BOUNCE` (64 KB) through the mover. |
| `xTaskCreate` (`dump_task`, `ringops.cc:365`) | rops | `api->job->start` | One shot, 12 KB stack, ends itself. Same: a task of its own at idle+1. Differs: the stack is given in bytes and is at least 12 KB (`apphost.cc:495`); 4 slots shared with the sub-apps; `stop_jobs()` cancels every job when a sub-app ends; under the owner rule of 2.6 and 2.7 it cancels only the jobs of that sub-app. The dump reads the menu's `c->job_cancel`, not `SvcJob.cancelled`. |
| `vTaskDelete` | rops | the return of the job function | `vTaskDelete(NULL)` ends the task. The job ends when `fn` returns (`apphost.cc:506-512`). |
| `vTaskDelay` (`ring_task`) | ring | `LibRecord.poll` | The delay is the poll period and the 6 s start delay (above). |
| `vTaskDelay` (`plat_sleep`) | mon | `api->sys->sleep_ms` | `plat_sleep` clamps to `PLAT_SLEEP_MIN_MS`, then `vTaskDelay(pdMS_TO_TICKS(ms))`. `sleep_ms` rounds up to the tick plus one (`libboard.cc:52`). It is one tick longer. |
| `vTaskDelay` (`net_send`, `monsrv.cc:98`) | msrv | `api->net->send` | `SvcNet.send` has the same wait loop (`appsvc.cc:355`). The library has no delay of its own. |
| `xTaskGetTickCount` | mon, msrv | `api->sys->ms` | The same product (`tick * portTICK_PERIOD_MS`). |

### 3.3 Network

| Symbol | Users | Target | Note |
|---|---|---|---|
| `lwip_socket`, `lwip_setsockopt` (`SO_REUSEADDR`), `lwip_bind`, `lwip_listen`, `lwip_fcntl` (`O_NONBLOCK`) | msrv | `api->net->listen(4312, 2)` | `net_listen` (`appsvc.cc:284`) does the same sequence with the same backlog (`monsrv.cc:505` uses 2). A taken port gives -5, which is `s_taken` today. |
| `lwip_accept`, `lwip_setsockopt` (`TCP_NODELAY`), `lwip_fcntl` | msrv | `api->net->accept` | Same, `TCP_NODELAY` and non-blocking are set inside. Differs: a free socket slot is needed (`NET_MAX` 16, one array that the sub-apps share, `appsvc.cc:256`). Past it `accept` returns -5 and the connection stays in the backlog. `monsrv` accepts every connection (`accept_one`, `monsrv.cc:430-453`): the first becomes the client `C`, up to `MAX_REFUSE` (2) more get the refusal answer (`refuse_start`, `monsrv.cc:417-428`), and it closes any beyond. The refusal sockets are `SvcNet` handles too, so they count against the 16, with the listener and the client: 4 of them at most. |
| `lwip_recv` | msrv | `api->net->recv` | Same call (`MSG_DONTWAIT`). Return codes differ: nothing waiting -5 (now -1), broke -3 (now -2), closed 0. |
| `lwip_send` | msrv | `api->net->send` | Same loop with the same limit (5000 ticks idle). Failure -3 (now -2). |
| `lwip_close` | msrv | `api->net->close` | A handle that was not made by `listen` or `accept` gives -1. |
| `lwip_getpeername`, `ip4addr_ntoa_r`, `lwip_htons` | msrv | `api->net->peer` | The same text `"ip:port"` (`appsvc.cc:422`). `htons` is inside `listen`. |
| `__errno` | msrv | `api->net` | Used only for `EAGAIN` after `lwip_recv` and `lwip_send`. `SvcNet` returns codes instead. |
| `rest_uic` | msrv | `api->ops->state("net.gated")` | `gate_refused` (`monsrv.cc:171`) refuses the methods of `monsrv_gated` while the REST service has its token: the test is a null check of the pointer `rest_uic` (`rest.cc:416`, `543`), and the monitor socket has no token. The base registers the state `net.gated` next to the REST service; it answers "yes" while `rest_uic` is set and null otherwise, as `monitor.wants_stop` answers (`monitor.cc:524`). It is a state, not a `SvcNet` member. The sub-app trxmon serves the same methods through `api->net` and reads the same state. |

### 3.4 Files

The moving code opens files through `FileManager::getFileManager()` in `ringfile.cc` and `ringops.cc`, and through `c->fs` (`CoreFs`) in `trx64u_ops.cc`, `u64cfg.cc` and `monitor.cc`. Both go through `api->fs`.

| Symbol | Users | Target | Note |
|---|---|---|---|
| `FileManager::fopen(path, FA_READ)` | rfile | `api->fs->open(path, false)` | The board's `CoreFs::open` calls the same `FileManager::fopen` (`fm_fs.cc`). `SvcFs` also applies the `/flash` rule (`fsif.h:44`). A handle that a handler opens is on the library's account only with the owner rule of 2.7. |
| `FileManager::fopen(path, FA_WRITE \| FA_CREATE_ALWAYS)` | rfile | `api->fs->save(path, "", 0)`, then `open(path, true)` and `writable` | `CoreFs::open` has no create (`fm_fs.cc:245`: `FA_READ \| FA_WRITE`). `trx64u_ops.cc:779-790` already does the two steps for its own writes. A `create` call would make it one call; it is not needed. A medium that cannot be written gives a read-only file; the code must test `writable`. |
| `File::read`, `File::write`, `File::seek`, `File::get_size` | rfile | `api->fs->read`, `write`, `fsize` | Every call in `ringfile.cc` is all-or-nothing (`got != want` is an error), which is the `SvcFs` rule. A seek is an offset. A write at offset 9 into the open file (digest, `ringfile.cc:1346`) is `write(f, 9, ...)`. A write at the end makes the file longer. |
| `FileManager::fclose` | rfile | `api->fs->close` | |
| `FileManager::release_mount_point`, `FileSystem_Root::FileSystem_Root`, `mstring::~mstring`, `pattern_match`, `vTaskEnterCritical`, `vTaskExitCritical`, `vQueueDelete`, and the `xQueueCreateMutex` uses of `rfile` and `rops` | rfile, rops | `api->fs` (falls away) | None is called by the moving source. They come from the inline code of `filemanager.h` (the destructor of the static `FileManager`, 2.9), `managed_array.h` (`ENTER_SAFE_SECTION`) and `mystring.h`. They disappear with the `FileManager` calls above. |
| `FileManager::delete_file` | rfile, rops | `api->fs->remove` | `remove` refuses a path under `/flash`; the ring files go to a medium. |
| `c->fs->open`, `close`, `stat`, `load`, `save`, `remove`, `mkdir` | ops, cfg, mon | `api->fs->open`, `close`, `stat`, `load`, `save`, `remove`, `mkdir` | Same. `CoreFile *` becomes `SvcFile *`. `c->fs->mkdir("/flash/roms")` and `save` into `/flash/roms` (`trx64u_ops.cc:548`) keep the same rule: the write is refused unless the board profile makes the flash drive writable. |
| `c->fs->list_short` | ops, cfg, mon | `api->fs->list` | `SvcFsEntry` has the layout of `FsEntryShort` without `cut` (56 bytes of name, 72 bytes). Same names as `list_short`. |
| `c->fs->list` with `FsEntry` | ops | `api->fs->list`, and a second call with the whole name | `trx64u_ops.cc:589` and `759` list `/` (names of media, short). `646` lists the folder of the mounted image and builds the next path from the listed name (`swap_next`, `trx64u_ops.cc:617`). `SvcFsEntry` cuts a name to 55 characters, so a longer name gives a wrong path. `SvcFsEntry` is frozen at 72 bytes (`libapi.h:46`), so it cannot carry the name: the base adds a listing call whose entry holds the whole name (base fix, section 13.3). |

### 3.5 Memory

The calls of the C library that allocate go to `SvcMem` through the SDK runtime (section 5). `SvcMem` takes the block from the library's account (2.7).

## 4. Kind `new-api`

The calls are defined in section 11. This table names the symbol, the call and the other users.

### 4.1 Settings

| Symbol | Users | Target | Note |
|---|---|---|---|
| `Settings::find` | ops, cfg, rops | `settings.find` | Same: index or -1, case-insensitive. The index is valid while `generation` does not change, because `run.cc:646` makes a new `Settings` object after the merge; `cfg` caches indexes per object (`u64cfg.cc:79`, `696`, `866`). Other users: every library of a core that reads its own settings; the cbm1541 library when it leaves the image. |
| `Settings::value` | ops, cfg, rops | `settings.value` | Same. The base cache of `cfg_value` is keyed by the address of the key literal; with a library image that can leave, the key must be compared by content. `cfg_value` itself is the forward row of 6.2. |
| `Settings::text` | ops, cfg | `settings.text` | Same. `cfg_text` skips leading blanks, `text` does not (`trx64u_ops.cc:54-62`); `cfg_text` is the forward row of 6.2. |
| `Settings::layer` | ops | `settings.layer` | 0 default, 1 host, 2 core. Used once (`Navigation Style`, `trx64u_ops.cc:71`). The settings group is the generic application's by nature (1.8). |
| `Settings::override_value` | ops, cfg, rops | `settings.override_value` | This run only. Three callers: look and navigation (`ops`), power-on keys (`cfg`), the system mode of a loaded `.c64re` (`rops`). |
| `Settings::set` | cfg | `settings.set` | Writes the core layer and marks the setting changed; the `.cfg` is written at once only with Auto Save Config on (`settings.cc:283-286`), else it waits for the next save. The index form; `settings.set` of the registry takes a label. |
| `Settings::extra_get`, `Settings::extra_set` | cfg | `settings.extra_get`, `settings.extra_set` | Keys with the prefix `Pad ` only (`settings.cc`). One user today (`Pad SID Detected Socket`); the keys are kept by the generic settings file, which only the base writes. |
| `regset_apply_setting` | rops | `settings.apply` | Applies setting `i` through its operation or its register commit (`regset.cc:32`). D8 makes the register table TOML that the SDK implements, so the call stays in the base. |
| `c->desc->settings[ix].base`, `.nvalues` | cfg | `settings.info` | Inline code (`u64cfg.cc:706`, `883`). |

### 4.2 Sync

| Symbol | Users | Target | Note |
|---|---|---|---|
| `xQueueCreateMutex` (`xSemaphoreCreateMutex`) | ring | `sync.mutex_new` | `ring.cc:2811`. The uses in `rfile` and `rops` fall away (3.4). Priority inheritance is part of the contract. |
| `xQueueSemaphoreTake` (`xSemaphoreTake`) | ring | `sync.mutex_lock` | `portMAX_DELAY` (`ring.cc:180`) and 0 (`ring.cc:1949`). Both timeouts are needed. |
| `xQueueGenericSend` (`xSemaphoreGive`) | ring | `sync.mutex_unlock` | |
| `xQueueSemaphoreTake`, `xQueueGenericSend` (use in `cfg`) | cfg | falls away | Inline code of `I2C_Driver::i2c_lock` (`i2c_drv.h:46-62`, `xSemaphoreTake(mutex, 5000)`). The lock is inside the base call `board.sid_socket` (4.4). |

`vQueueDelete` has no use in the moving source (3.4). `sync.mutex_free` is in the group only so that a library can free a mutex it made.

`sync.enter` and `sync.leave` have no user in the moving source today (`vTaskEnterCritical` in `rfile` and `rops` falls away, 3.4). They stay for D20: the 32-bit TRX register accesses are code of the TRX64-Ultimate library, and the four byte accesses of one 32-bit access must not be split (`trx_io.cc:110-129`). Contract: the section is short and holds no blocking call (no `sleep_ms`, no mutex, no file, no socket); it nests.

### 4.3 Time and clock

| Symbol | Users | Target | Note |
|---|---|---|---|
| `getMsTimer` | ring, rfile | `sys.ms_timer` | The 1 ms counter, 16 bit (2.8). 31 lines in `ring.cc` and `ringfile.cc`, all differences of two reads. |
| `CoreIo::delay_us` | cfg | `sys.delay_us` | Busy wait (`hw.cc:229`, `wait_10us`). 9 uses in `u64cfg.cc` with 10 us. Other users: `atari_sio.cc`, `apple2_disk.cc`, `apple2_hdd.cc`. |
| `get_fattime`, `rtc_time_valid` | rfile | `sys.wall_time` | `CoreIo::wall_time` gives `y m d h mi s` (year 0-99) and false when the board has no clock (`io.h:118`, `hw.cc:218`). The file code wants the year to be at least 2024 and a valid time: the same. Other users: `amiga.cc:271`, `neogeo.cc:560`, `snes.cc:379`, `zxnext.cc:172`, `update.cc:39`. |
| `CoreIo::delay_ms` | ops, cfg | `api->sys->sleep_ms` | In `api`. `delay_ms` is `vTaskDelay(pdMS_TO_TICKS(ms) + 1)` (`hw.cc:228`). `sleep_ms` is `ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1` (`libboard.cc:52`). Equal for 1-4 ms (one tick) and for 5 ms and more. |
| `CoreIo::now_ms` | ops, cfg | `api->sys->ms` | In `api`. The uses are 500 ms timeouts (`u64cfg.cc:756`, `1189`, `1194`) and a timestamp (`trx64u_ops.cc:1488`). The 5 ms resolution is enough there. |
| `plat_yield` (`taskYIELD`, `monitor.cc:154`) | mon | `sys.yield` | The call gives the processor to a ready task of the same priority and returns at once when none is ready. `api->sys->sleep_ms(0)` is not that: `svc_sleep` calls `delay_ms`, which is `vTaskDelay(ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1)` (`libboard.cc:52`), so 0 sleeps one tick of 5 ms. The comment of `sleep_ms` in `svctab.h` said "0 only yields"; it is corrected in the commit of this page. Other users: any library that polls in a loop and must not hold off a task of its own priority. |

### 4.4 Board, video and UI

A call enters the group only when a second user is named or when the service belongs to the generic application by nature (1.8). Everything else is a setting or an operation of the base, or falls away.

| Symbol | Users | Target | Note |
|---|---|---|---|
| `hw_esp32_rpc` | ops, cfg | `board.esp32_rpc` | Power mode, wake on Wi-Fi, power off, power cycle. One shared UART with one reply table (`hw.cc:444`); a library cannot own it. Second user: `rocker_report_task` of the base (`hw.cc:409`). |
| `i2c`, `I2C_Driver::i2c_write_byte` | cfg | `board.sid_socket(socket, state, shunt, filter_caps)` | What the code does with I2C: it writes the SID socket expander (device 0x40 on the 1.8 V channel): lock, channel, two byte writes (register 1 gets `ctrl`, register 3 gets 0), unlock (`u64cfg.cc:1250-1256`). `ctrl` is built in the library from the enable, type, shunt and capacitor settings of the two sockets (`u64cfg.cc:1236-1249`). No second user: the other I2C code of the base is the keyboard expander at 0x42 (`coreapp.cc:87-92`), which no library touches. So there is no raw `i2c_write` in the API. Final form: a call `board.sid_socket(socket, state, shunt, filter_caps)`, `state` one of `SID_SOCKET_OFF` 0, `SID_SOCKET_ON` 1, `SID_SOCKET_ON_TYPE1` 2 (regulator bits 00, 10, 11 of the socket's nibble; the old code wrote `typ == 1 ? 3 : 2`, `u64cfg.cc:1245`); `shunt` is bit 2, `filter_caps` is the value of the setting and the base inverts it into bit 3 (`1 - V(...)`, `u64cfg.cc:1242`). The base keeps the control byte of both sockets, replaces the nibble of the one named, and writes registers 1 and 3 under the "SID Configuration" lock. |
| `getFpgaCapabilities` | ops, rfile | `board.fpga_caps` | The ITU capability register, four bytes (`itu.c:19`). Not the TRX `CAPS`. Second users: the library `uci` (`uci.cc:520`, `1388`), and the base (`hw.cc`, `board.cc`). |
| `isCommodoreBoard` | ops | `board.is_commodore` | `product.cc:92`. One library user (the first-run default of `Navigation Style`, `trx64u_ops.cc:71`). Kept because the product is a base fact that no library can derive; the base uses it too (`run.cc`, `board_set_port`). |
| `picture_set_system` | cfg | `board.video_set_system` | `picture.cc:543`. Second users: `c128.cc:433`, `c64scpu.cc:105`, `portdiag.cc:63`, each in the operation that follows a change of the Picture; they are libraries or cores of their own once D9 and D10 apply. |
| `SetVideoPll`, `ResetHdmiPll` | cfg | `board.video_set_pll` | `fx_video` calls them as a pair (`u64cfg.cc:275-276`) to put the colour clock's offset in ppm on the PLL. One call. No second library user. Kept because the PLL is a resource of the base's video path: the base programs it for every mode with offset 0 (`videomode.cc:374-376`), and no library may write a PLL register. |
| `color_timings` | cfg | not a call: the table moves into the library | A table of the vendor firmware (`color_timings.h`, 6 entries). The code reads `phase_inc`, `mode_bits`, `audio_div` (`u64cfg.cc:241-263`). The only other user is the resampling filter, and `u64cfg` is its only user (6.1). No second user, so no call: the library carries its own copy of the table as constants (8.3, D8). |
| `ui_set_look`, `ui_set_name_squeeze` | ops | settings of the generic application | The code calls them with `Overlay Transparency`, `Color Scheme` and `Filename overflow squeeze` (`trx64u_ops.cc:74-75`, `ui.cc:1261`, `1269`). The base owns the three settings as it owns Picture and Volume (`run.cc:464`), applies them itself, and any core's menu can show them. `Navigation Style` with its first-run default on the Commodore product moves to the base too (`baseset.cc`, built in 3.3); `fx_ui` keeps `tempdisk_set`. |
| `keyb_joy_select`, `wasd_to_joy` | cfg | a setting of the generic application | Two globals of the host keyboard driver (`keyboard_c64.cc:15`, `19`) that `menukeys.cc:27`, `45` write back into the registers when the menu closes. `fx_misc` derived them from the setting `Joystick Input` (`keyb_joy_select` = value & 1, `wasd_to_joy` = `wasd_settings[value]`). The base owns that setting and writes both globals and the registers `U64II_KEYB_JOY` and `MATRIX_WASD_TO_JOY` itself (`hw_keyboard_joystick`, `hw.cc`, called by `baseset.cc`); any core's menu can show it. Only `C64_PADDLE_SWAP` stays with the core library, which sets it in its `base_setting_changed` hook (`core_hooks.h`) from the value of `Joystick Input`. |

### 4.5 Context of the running core

| Symbol | Users | Target | Note |
|---|---|---|---|
| `cfg_value`, `cfg_text` | rops, base | see 6.2 | The forward row is 6.2 (the definition moves into the core application). The calls are `settings.cfg_value` and `settings.cfg_text` (11.2). |
| `trx_target_path` | rops, diag | see 6.2 | The forward row is 6.2. The call is `ctx.target_path` (11.3). |
| `corefolder_dir` | ops | `ctx.core_folder` | The path of the core folder from `c->cor_path` (`corefolder.cc`). Three sites for the SID player files. The sub folders `roms`, `saves`, `configs` are the same call with a name. Other cores need `roms` and `saves`. |
| `c->started`, `c->paused`, `c->frozen`, `c->menu_open` | ops, cfg, rops | `ctx.state`, `ctx.set_paused` | `trx64u_ops.cc:723-733` toggles `paused`. |
| `c->notice`, `c->notice_pending` | ops, cfg | `ctx.notice` | `u64cfg.cc:1229` writes only when none is pending; `trx64u_ops.cc:1501` writes always. |
| `c->media_dir` | ops | `ctx.media_dir` | |
| `c->job_step`, `c->job_cancel`, `c->job_pct`, `c->job_path` | rops | `ctx.job_step`, `ctx.job_cancelled`, `ctx.job_progress` | The progress protocol of the menu task (`menuctl.cc` `start_job`). `ringops.cc:365` and the dump. Other operations that write a file in steps. |
| `c->io->...` | ops, cfg, rops | `io.*` | Section 8. |

## 5. Kind `sdk-runtime`

| Symbol | Users | Target | Note |
|---|---|---|---|
| `__udivdi3`, `__umoddi3`, `__divdi3`, `__moddi3` | rfile, mon (trxmon) | already in `app/sdk/libc.c:246-296` | They exist today. A library takes them with `LIB_LIBC = 1` (`lib.mk:34`). BOUNDARY.md leaves out the names up to `__ashrdi3` only; these four are in the file. |
| `malloc`, `free`, `realloc` | ops, ring, rfile, mon, msrv | `SvcMem.malloc`, `free`, `realloc` | Same. `SvcMem.malloc` takes `uint32_t`. The block carries a 16-byte header and the base returns it 16-byte aligned. `ring.cc:166` aligns by hand anyway (the mover writes into it). `ring` takes about 1.5 MB (`tl` is 4096 entries of 260 bytes). 2.7 on the account. |
| `calloc` | rfile, mon, msrv | malloc and `memset` | `SvcMem` has no `calloc`. |
| `operator new`, `operator delete`, `operator new[]`, `operator delete[]` | ops, rfile, rops | `SvcMem` | `-fno-exceptions`: a failing `new` returns null in the SDK. `trx64u_ops.cc:1044` allocates an `OpResult` with `new` (about 500 bytes). |
| `printf`, `puts` | all | `api->log` | The SDK formats into a line buffer and calls `log`. `lib_log` prints `"[lib] %s\n"` (`libboard.cc:62`), so a trailing newline of the format has to be dropped, and a line built by several `printf` calls has to be joined. 243 lines with `printf(`, `snprintf(` included: ring 18, rfile 33, rops 42, ops 103, cfg 32, mon 8, msrv 7. |
| `snprintf`, `sprintf` | ops, cfg, rfile, rops, mon, msrv | the SDK's own | See 2.10. |
| `sscanf` | cfg | the SDK's own | One format, `"%x %x %x"` (`u64cfg.cc:198`). |
| `atoi`, `strtoll` | cfg, rfile | the SDK's own | `strtoll` for the JSON numbers of the ring file. |
| `strcasecmp`, `strspn`, `strstr`, `isalpha`, `toupper` | ops, cfg, rfile, rops | the SDK's own | `libc.c` has none of them. |

## 6. Kinds `shared-lib`, `base` and `theirs`

### 6.1 `shared-lib` (D12; T104.1 D15)

A shared library has one call table. A moving library reaches it with `api->lib->find(name, version, &size)`; base 3.2 matches export names first, so one library can export several tables. `cbm1541` is a shared library by T104.1 D15 (source `app/lib/cbm1541`; its contributor `coreapp/cores/cbm1541.cc` is still in the image); the cartridge, `sid`, `uci`, `tempdisk` and `resample` libraries are shared by D12. A table is a structure of function pointers in the layout of the library's own header. The functions of these libraries take `CoreCtx *`; the pointer passes through unchanged.

| Symbol | Users | Library, table | Note |
|---|---|---|---|
| `c64_cart_active`, `c64_cart_name`, `c64_cart_path`, `c64_cart_crt_type` | ops, rops | `c64_cart`, `active`, `name`, `path`, `crt_type` | Pure reads of the state. Other users: `c64.cc:537`, `625`, which moves with the core (2.11). |
| `c64_cart_image_bytes`, `c64_cart_image_read`, `c64_cart_image_write` | rops | `c64_cart` | The cartridge image in the DDR2 window. |
| `c64_cart_for_state` | rops | `c64_cart` | Puts the cartridge of a loaded `.c64re` in place and resets. |
| `c64_cart_ram` (a pointer), `c64_cart_require`, `c64_cart_prohibit`, `c64_cart_writable` | rops (the three from `u18` `2d4c2dcc`, the pointer from the ring's pending list) | `c64_cart`, `ram_size`, `ram_read`, `ram_write`, `require`, `prohibit`, `writable` | The cartridge RAM is not reached through `io.ddr_abs_*` by the TRX64-Ultimate libraries; the bodies of `ram_*` use those calls inside the shared library. `require` and `prohibit` are the cartridge definition's flags, `writable` says the C64 can change the cartridge's flash or EEPROM. |
| `c64_cart_save`, `c64_cart_ef_clear`, `c64_cart_run_image` | ops | `c64_cart` | |
| `c64_cart_run`, `c64_cart_remove`, `c64_cart_player_end` | `c64.cc` | `c64_cart` | Not in the measured list, because `c64.cc` was not measured (2.11). `c64.cc:111`, `601`, `610`, `632`, `644`. |
| `c64_cart_run_modplayer` | ops | none | Falls away (T239.3 D5). |
| `sid_parse`, `sid_plan`, `sid_requests`, `sid_info_text`, `sid_mus_header`, `sid_mus_stereo` | ops | `sid`, six members | Pure functions over `SidTune` (`sidfile.h`). The structures `SidTune` and `SidReq` are defined once, in the SDK header `sid_api.h` (same layout as before); `sidfile.h` includes it. |
| `uci_start` | ops | `uci`, `start(const UciHooks *)` | The callbacks table is `UciHooks` in the export (`uci_api.h`; `UciHost` stays the name of the internal type of `uci.h` until 3.3 converts it). It is a table of callbacks that the caller (trx64u) supplies: REU, drive info, drive operation, SID map, EF clear, RAM drive, palette (`trx64u_ops.cc:1051-1057`). The callbacks stay in the trx64u library. `uci` also needs `board.fpga_caps`. |
| `tempdisk_set` | ops | `tempdisk`, `set(auto_cleanup, subfolders)` | |
| `cbm1541_drive` | ops | `cbm1541`, `drive(c)` | Returns a `CbmDrive *`. T104.1 D15. |
| `CbmDrive::create`, `mount`, `eject`, `mode_name`, `set_reset_with_c64` | ops | `cbm1541` | T104.1 D15. BOUNDARY.md counts these five, because the others are inline. The class is used with these further members: `set_twist`, `set_align`, `set_freeze`, `set_geos`, `image_name`, `last_dir`, `swap_delay_ms`, `type`, `bus_id`, `powered` (`trx64u_ops.cc:463-715`, `1029-1035`), and `image_path`, `mount_mode` in `ringops.cc:255-261`. A call table of a class needs one entry for each. `c->drive` (`ringops.cc:255`) becomes `cbm1541.drive`. |
| `cbm1541_slot_mount` | rops | `cbm1541`, `slot_mount` | T104.1 D15. `c64.cc` calls it too (`c64.cc:630`). |
| `drive_a_image` (a pointer) | rops | `cbm1541`, `image_size`, `image_read`, `image_write` | The 64 KB image of drive A in DDR2; the bodies use `io.ddr_abs_*` inside the shared library. |
| `u64_resample.inc` | cfg | `resample`, `program(audio_div)` | Two coefficient tables (675 and 512 taps) and the register loop `set_resample_filter` (`u64cfg.cc:239-253`). The caller passes `audio_div` from its own colour timing table (4.4): 77 selects the 675 taps, any other value the 512. Only `cfg` uses it. |

Today the users of these libraries are the trx64u files and `c64.cc` only (the drive library has more: `c128.cc`, `c264.cc`, `c64scpu.cc`, `vc20.cc` use it). D12 names the later users: C64SCPU and C128 for the cartridges, the Amiga mouse. Each library image is at most `U2A3_IMAGE_MAX`, 1 MiB (`libfmt.h:40`). R5 (the size of each library) is not measured here.

### 6.2 `base`

| Symbol | Users | Target | Note |
|---|---|---|---|
| `mouse_sink_scaling` | cfg | kept in the mouse library | It is the mouse table's `scaling` (`mouse_exports.cc`). `u64_get_usb_hid_config_value` in `baseset.cc` (section 12, row `usb_hid`) asks it first (the Amiga's `mouse.sens`, `mouse.accel` set it) and then calls `baseset_usb_config`, which answers the USB items from settings. The five items (`Mouse Mode`, `Mouse Sensitivity`, `Mouse Acceleration`, `Mouse Wheel Sensitivity`, `Mouse Wheel Direction`) are settings the base owns and declares for every core with a mouse; the two cores that declared them, trx64u's `menu.toml` and `amiga.cc`, dropped their copies, and the stored values carry over because the keys stayed (`svc_settings.h`). |
| `cfg_value`, `cfg_text` | rops, base | `base` | Defined in `trx64u_ops.cc`. They move into the core application and are offered as `settings.cfg_value` and `settings.cfg_text` (4.1, 11.2). |
| `trx_target_path` | rops, diag | `base` | Defined in `trx64u_ops.cc`. It moves into the core application and is offered as `ctx.target_path` (4.5, 11.3). |

### 6.3 `theirs`

| Symbol | Users | Target | Note |
|---|---|---|---|
| `trx_ring_commit`, `error`, `find`, `fps`, `fps_x100`, `frame`, `frame_cycles`, `land`, `landed`, `log_read`, `reachable`, `status`, `trace_ctl`, `trace_read`, `trace_status`, `trace_whowrote` (16) | mon | the export "ring" (major 1) of the ring library, found with `api->lib->find` | A call between their libraries (D20). `fps`, `fps_x100` and `frame_cycles` are three functions over `C64_VIDEOFORMAT`; the monitor can also take them from the export "trx" (`video`). |
| `uic_json_get` | msrv | `theirs` | `gate_refused` reads `method` and `id` of a JSON-RPC frame. trxmon has `json_parse`, `json_get`, `json_str` (`trxmon/json.h`), which the monitor library already links. |
| `video_sys_name` | cfg | `theirs` | One log line (`u64cfg.cc:268`). The library prints the label of the `System Mode` setting (`settings.text`) or the number. |
| `app_version_string` | rfile | `new-api` `sys.app_version`, `sys.app_git` | Final form (spec section 5): the base gives the version text of the application image and its short commit hash, and the library writes them into the `appVersion` field it makes (2.12). The ring's `LIB_VERSION` is not used for it. |
| `nf_copy` | rfile | `theirs` | Eight lines (`newfile.cc`) that copy at most `n` characters. With a full `snprintf` (`%.*s`, 2.10) it is not needed. |
| plat glue of the monitor (`plat_*`) | mon | theirs, over `api` | Section 10. |
| engine and screen of trxmon | mon | theirs | Their own `apps/trxmon/src` (D4). |

## 7. Kind `decided`

R3 and R4 are decided (1.6). The table gives the answer for each row.

| Symbol | Users | R | Answer | Note |
|---|---|---|---|---|
| `__cxa_atexit`, `__dso_handle` | rfile, rops | R3 (D19) | The build refuses: `lib.mk` stops a library that has `.init_array`, `.fini_array` or `__cxa_atexit`; the loader runs none. | The only known source is the static `FileManager` (2.9). With 3.4 the symbols are gone. The rows cost the libraries nothing. `lib.ld` keeps `.text`, `.data.rel.ro`, `.data` and `.bss` only. |
| `trx_io_caps` | ring, rfile, mon | R4 (D20) | The library reads the `CAPS` byte of the ID block at `IOBASE + 0x1A0004` itself, or takes `caps` from the export "trx". | The export is theirs: "trx" major 1 in the layout of their `app_api.h`. It is null unless the ID block reads `TRX2`. |
| `trx_io_read32`, `trx_io_write32` | ring, rfile, mon | R4 (D20) | The library writes the four bytes at the fixed address inside `sync.enter` and `sync.leave`. | The 32-bit rule needs the four byte accesses in one critical section (`trx_io.cc:110-129`). The base keeps its own `trx_io_*` for `appsvc.cc` and the pixel layer. Contract of the section: 4.2. |
| `trx_io_slim` | ring | R4 (D20) | The library reads `TX_CTRL` (`+0x60C0`) itself and caches (`trx_io.cc:95-105`). | |
| `trx_io_resid8_filter`, `trx_io_resid8_bypass` | cfg | R4 (D20) | The library does it with byte writes (`ioWrite8` to `TRX_RS8_*`, `trx_io.cc:72-93`) guarded by `CAPS3` bit 0, using the header of constants of 8.3. | Plain byte writes: no call and no member in any table. |

The base has no `SvcTrx` member for any of these. `SvcTrx`, `SvcRing` and `SvcC64` leave `LibApi` when the libraries export the tables (12.4).

## 8. Fields of `CoreCtx` and methods of `CoreIo` that the code reaches

8.1 The 18 fields of `CoreCtx` (`ctx.h`) that the moving code reads or writes:

| Field | Users | Target | Note |
|---|---|---|---|
| `fs` | ops, cfg, mon | `api->fs` | 3.4. |
| `io` | ops, cfg, rops | `io.*` | 8.2. |
| `settings` | ops, cfg, rops | `settings.*` | 4.1. |
| `desc` | ops, cfg | `sys.has_capability`, `settings.info` | 3.1, 4.1. |
| `board` (`probes`) | ops | `sys.has_capability` | 3.1. |
| `started`, `paused`, `frozen`, `menu_open` | ops, cfg, rops | `ctx.state`, `ctx.set_paused` | 4.5. |
| `notice`, `notice_pending` | ops, cfg | `ctx.notice` | |
| `media_dir` | ops | `ctx.media_dir` | |
| `cor_path` | ops | `ctx.core_folder` | |
| `drive` | rops | `cbm1541.drive` | 6.1. |
| `job_step`, `job_cancel`, `job_pct`, `job_path` | rops | `ctx.job_*` | |

8.2 The 23 methods of `CoreIo` (`io.h`) that `svc_io.h` hands to libraries: `rd`, `wr`, `ddr_read`, `ddr_write`, `rom_read`, `rom_write`, `ram_read`, `ram_write`, `ram_memonly`, `drv_rd`, `drv_wr`, `ddr_abs_read`, `ddr_abs_write`, `cart_rd`, `cart_wr`, `bus_wr`, `eeprom_read`, `eeprom_write`, `keys_read`, `stop`, `mode_reset`, `video_format`, `delay_us`; the 24th member is `bus_rd`, new in `CoreIo` (default 0; `HwIo` reads the byte at the address when 3.3 is built). The moving code reaches 15 of them today (`rd`, `wr`, `bus_wr`, `cart_wr`, `ddr_read`, `ddr_write`, `ram_read`, `ram_write`, `ram_memonly`, `rom_write`, `stop`, `mode_reset`, `eeprom_read`, `eeprom_write`, `keys_read`); `now_ms` and `delay_ms` are in `api` (4.3) and `delay_us` is also `sys.delay_us`. The group is the base's own hardware abstraction: offsets are relative to each window and a core's API TOML names its registers (D8). The register names of the moving code (`C64_CARTREGS_BASE`, `C64_IO_BASE`, `U64_ROMS_BASE`, `C64_MEMORY_BASE`, `MODE`, `STOP`, `CARTRIDGE_TYPE`, `REU`, `SAMPLER`, `VIDEOFORMAT`, `DMA_MEMONLY`) are therefore not in `svc_io.h`. `SvcC64` (`svctab.h:197`) has `peek`, `poke`, `dma_read`, `dma_write` on the same window `C64_MEMORY_BASE` (`appsvc.cc:30-49`). It does not touch `C64_DMA_MEMONLY`. `ram_memonly` is called 12 times in `trx64u_ops.cc` and `u64cfg.cc` to reach RAM under I/O; so `ram_read` and `ram_write` need the `io` group, not the C64 table. `SvcC64` leaves `LibApi` (D20, 12.4); its members are the first of `C64Api`.

8.3 The register macros. The code writes registers directly with `ioWrite8` and the macros of `u64.h` (`C64_*`, `U64_*`), `iomap.h` (`IOBASE`) and `trx_io.h` (`TRX_*`). They are constants, not calls. D8 makes the register map TOML that the SDK implements once. Until then a header of constants in the package serves (section 13).

## 9. The headers outside the SDK

BOUNDARY.md lists 132 headers from the `.d` files. Most come through `ctx.h` and `desc.h` and are not used by the moving files. This table groups them by what the code takes from them.

| Group | Headers | Target |
|---|---|---|
| Firmware types: file system, containers, lwIP, FreeRTOS, I2C, flash, config, hand-over | `software/components/*`, `software/filemanager/*`, `software/filesystem/*`, `software/infra/*`, `software/lwip/*`, `software/network/*`, `software/FreeRTOS/*`, `software/io/i2c/*`, `software/io/flash/*`, `software/io/c64/{c64,keyboard,screen,trx_cart}.h`, `software/userinterface/*`, `software/hostapp/{configlist,cor,handover}.h`, `software/api/jsmn.h`, `software/system/{integer,itu,product,profiler,u2p}.h`, `output/gitinfo.h` | Not needed after sections 3 and 4. `jsmn.h` (JSON parser for the `.c64re` header, `ringfile.cc:36`) comes in with the library as source. `keyboard.h`: the key codes of `mon_key` (`monitor.cc`); they go into the SDK as a header of constants (the keys of the UI TOML, D7). |
| Register maps | `software/system/u64.h`, `software/system/iomap.h`, `software/io/c64/trx_io.h` (macros only), `software/u64/color_timings.h` (types), `coreapp/cores/c64_regs.h`, `app/video/videomode.h` (`VID_SYS_*`) | A header of constants in the package (D8). The functions of `trx_io.h` and `color_timings` are in sections 4 and 7. |
| Core application, internal | `ctx.h`, `desc.h`, `io.h`, `board.h`, `settings.h`, `ops_glue.h`, `engines.h`, `writeback.h`, `regset.h`, `boot.h`, `hw.h`, `picture.h`, `ui.h`, `rest.h`, `uic.h`, `svcjson.h`, `httpreq.h`, `partial.h`, `mousesink.h`, `newfile.h`, `corefolder.h`, `tempdisk.h`, `picmodes.h` (the rows of the Picture page, included through `picture.h`; none of its functions is called by the seven files), and the menu headers (`menuctl.h`, `menuflows.h`, `menuintent.h`, `menutree.h`, `menuvlist.h`, `picklist.h`, `hotkeys.h`, `rawkeys.h`, `romfind.h`, `romset.h`, `sectors.h`, `winslot.h`, `fbwin.h`, `ledload.h`, `mediamounts.h`, `filemgr.h`) | Not in the package. Their reach is sections 4 and 8. `sid_decide.h`, `resid8_filter.h` (a copy of the firmware header) and the inline `desc_has_capability` are used for real; `sid_decide.h` and `resid8_filter.h` move with `u64cfg`. |
| Shared libraries | `c64_cart.h`, `cbm1541.h`, `d1541_drive.h`, `d1541_gcr.h`, `d1541_wd.h`, `sidfile.h`, `uci.h`, `u64_resample.inc` | The header of each library, shipped with it (6.1). |
| Application ABI 2 | `vendor/gideon/software/appsdk/include/u2p_app_api.h`, `.../app_loader/app_api.h` | `app/sdk/u2p_app.h` of the package for the tm_* engine; their own copy for trxmon (the layouts differ, 12.4). |
| Not used | `app/edit/lineedit.h`, `app/loaders/kickstart.h`, `app/video/edid.h` | Transitive. No use found in the seven files. |

## 10. The monitor's platform glue

`monitor.cc` supplies `plat_*` to the trxmon engine. All of it moves with the monitor library and runs over `api`.

| Function | Replaced by |
|---|---|
| `plat_ms` | `api->sys->ms` |
| `plat_yield` | `api->sys->yield` (new, 4.3; `sleep_ms(0)` sleeps one tick) |
| `plat_sleep` | `api->sys->sleep_ms` (one tick longer, 3.2) |
| `plat_fopen`, `plat_fread`, `plat_fwrite`, `plat_fclose` | `api->fs->load`, `save` (a file is held whole in memory and written at close, up to 64 KB, `monitor.cc:179`) |
| `plat_mkdir`, `plat_rmdir` | `api->fs->mkdir`, `remove` |
| `plat_exists` | `api->fs->stat` |
| `plat_dir` | `api->fs->list` (64 entries) |
| `plat_config` | `api->ops->state("set.<item>")`. The base answers it with the folding and the labels of 3.1 (`ops_state`) |
| `plat_media`, `plat_firmware_*` | constants |
| `api_trx_acquire`, `api_trx_release` | `api->trx->acquire`, `release`. The two are the same code as `appsvc.cc:123-145`. `api_trx_announce` is empty; `api->trx->announce(4312)` also puts the port into the ident reply (`appsvc_rpc_port`). 2.6 for the release at the end of a sub-app. |
| `api_video` | `api->trx->video` (same code, `appsvc.cc:214`) |
| `s_ring` (`u2p_ring_api_t`) | the table of the ring library (12.4) |

## 11. The new API groups

The groups are appended to `LibApi` after `lib`, each with its own `size`, as the existing groups are. A group the core does not have is a null pointer. A call without a context argument acts on the running core (as `api->ops->call` does). The groups come with base 3.3; base 3.2 adds the named exports (summary, a). The tables `trx`, `ring` and `c64` are not groups of this section: they leave `LibApi` (D20, 12.4).

### 11.1 `sync` (`SvcSync`)

A mutex and a critical section for libraries that run more than one task. Both are unconditional. The mutex has priority inheritance. It is freed with the library (the account of 2.7 has to hold).

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `mutex_new` | `struct SvcMutex *(*)(void)` | `xSemaphoreCreateMutex` | The ring (one). A library with a job and an operation that share state. |
| `mutex_lock` | `bool (*)(struct SvcMutex *m, uint32_t timeout_ms)` | `xSemaphoreTake` | `0xFFFFFFFF` waits for ever, 0 tries. |
| `mutex_unlock` | `void (*)(struct SvcMutex *m)` | `xSemaphoreGive` | |
| `mutex_free` | `void (*)(struct SvcMutex *m)` | `vQueueDelete` | |
| `enter` | `void (*)(void)` | `portENTER_CRITICAL` | D20: the 32-bit TRX register accesses in the TRX64-Ultimate library (section 7). Contract: the section is short and holds no blocking call (no sleep, mutex, file or socket call); it nests. |
| `leave` | `void (*)(void)` | `portEXIT_CRITICAL` | Pairs with `enter`. |

### 11.2 `settings` (`SvcSettings`)

The settings of the running core, by key. An index from `find` is valid while `generation` is unchanged. The base owns the cache. The whole group belongs to the generic application by nature (1.8). `cfg_value`, `cfg_text` are of kind `base` (6.2): their definition moves from `trx64u_ops.cc` into the core application.

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `generation` | `uint32_t (*)(void)` | the comparison of the `Settings *` (`trx64u_ops.cc:35`, `u64cfg.cc:79`) | every library that keeps indexes |
| `find` | `int (*)(const char *key)` | `Settings::find` | |
| `value` | `int (*)(int ix)` | `Settings::value` | |
| `text` | `const char *(*)(int ix)` | `Settings::text` | |
| `layer` | `int (*)(int ix)` | `Settings::layer` | |
| `set` | `bool (*)(int ix, int value)` | `Settings::set` | |
| `override_value` | `void (*)(int ix, int value)` | `Settings::override_value` | the cores with a `.c64re`-like load |
| `apply` | `void (*)(int ix)` | `regset_apply_setting` | |
| `info` | `bool (*)(int ix, int *nvalues, int *base)` | `c->desc->settings[ix].nvalues`, `.base` | |
| `extra_get` | `bool (*)(const char *key, char *val, int n)` | `Settings::extra_get` | |
| `extra_set` | `bool (*)(const char *key, const char *val)` | `Settings::extra_set` | |
| `cfg_value` | `int (*)(const char *key, int def)` | `cfg_value` | `menuflows`, `netops` of the base; the libraries of the core |
| `cfg_text` | `const char *(*)(const char *key)` | `cfg_text` | `netfs`, `netops` of the base |

### 11.3 `ctx` (`SvcCtx`)

The state and the fields of the running core that a handler reads or writes.

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `state` | `uint32_t (*)(void)` | `c->started`, `paused`, `frozen`, `menu_open` as bits 0-3 | |
| `set_paused` | `void (*)(bool on)` | `c->paused = ...` | |
| `notice` | `bool (*)(const char *text, bool only_if_free)` | `c->notice`, `c->notice_pending` | `c64.cc:371` (it moves with the core), any library that tells the person once |
| `media_dir` | `const char *(*)(void)` | `c->media_dir` | |
| `target_path` | `bool (*)(const char *name, const char *ext, char *out, int n)` | `trx_target_path` | `diag.cc:185` |
| `core_folder` | `bool (*)(const char *sub, char *out, int n)` | `corefolder_dir`, `corefolder_sub`; `sub` null, `roms`, `saves`, `configs` | every core with saves or ROM folders |
| `job_step` | `bool (*)(void)` | `c->job_step` | `newdisk.cc` and every operation that writes a file in steps |
| `job_cancelled` | `bool (*)(void)` | `c->job_cancel` | |
| `job_progress` | `void (*)(int pct, const char *path)` | `c->job_pct`, `c->job_path` | |
| `media_source` | `bool (*)(const char *slot, char *buf, int len)` | the path of the mounted medium of a media slot, by the slot's name in the descriptor (`MountSlots::names`, `MediaSlot::name`) | the ring and monitor libraries; any library that names the medium a core has in a slot |

### 11.4 `io` (`SvcIo`)

The windows of the running core: the base's own hardware abstraction, `CoreIo`, handed to libraries. The signatures are the ones of `io.h`; offsets are relative to each window and no register of any core is named in `svc_io.h`. All core libraries of this repository use `CoreIo` today.

| Call | Replaces | Other users in this repository |
|---|---|---|
| `rd`, `wr` | `CoreIo::rd`, `wr` | all cores |
| `ddr_read`, `ddr_write` | `CoreIo::ddr_*` | `snes.cc`, `c128.cc` |
| `rom_read`, `rom_write` | `CoreIo::rom_read`, `rom_write` | `c64.cc`, `c128.cc` |
| `ram_read`, `ram_write`, `ram_memonly` | `CoreIo::ram_*` | all cores |
| `drv_rd`, `drv_wr` | `CoreIo::drv_rd`, `drv_wr` | the drive library `cbm1541` |
| `ddr_abs_read`, `ddr_abs_write` | `CoreIo::ddr_abs_*` | the shared libraries (cartridge, drive engines); the TRX64-Ultimate libraries reach the cartridge RAM and the drive image through `c64cart` and `cbm1541` |
| `cart_rd`, `cart_wr` | `CoreIo::cart_rd`, `cart_wr` | the C64 line |
| `bus_wr`, `bus_rd` | `CoreIo::bus_wr`; `bus_rd` is new, the read side of the same window | the C64 line; `bus_rd` has one user (the ring reads back the video format and the DMA mode it wrote), kept because it is the generic pair of an existing window |
| `eeprom_read`, `eeprom_write` | `CoreIo::eeprom_*` | `c64_cart.cc:363` |
| `keys_read` | `CoreIo::keys_read` | `hotkeys.cc:780`, `run.cc:546` |
| `stop`, `mode_reset` | `CoreIo::stop`, `mode_reset` | all cores |
| `video_format` | `CoreIo::video_format` | `c128.cc` |
| `delay_us` | `CoreIo::delay_us` | the same call as `sys.delay_us` (11.6) |

### 11.5 `board` (`SvcBoard`)

The services of the board and of the video of the application. Each call is a base function today. None is a register: the code that needs the register writes it with the header of 8.3. A call is here only with its second user named (1.8, 4.4). Not in the group: `ui_set_look`, `ui_set_name_squeeze` and `keyboard_joystick` (settings of the generic application), `video_color_timing` (a table the library carries), `i2c_write` (the call `board.sid_socket` of the same group, 11.5).

| Call | Signature | Replaces | Second user |
|---|---|---|---|
| `fpga_caps` | `uint32_t (*)(void)` | `getFpgaCapabilities` | the library `uci` (`uci.cc:520`, `1388`); the base |
| `is_commodore` | `bool (*)(void)` | `isCommodoreBoard` | the base (`run.cc`, `board_set_port`); board identity like `fpga_caps` |
| `esp32_rpc` | `int (*)(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)` | `hw_esp32_rpc` | `rocker_report_task` of the base (`hw.cc:409`) |
| `video_set_system` | `void (*)(int sys)` | `picture_set_system` | `c128.cc:433`, `c64scpu.cc:105`, `portdiag.cc:63` |
| `video_set_pll` | `void (*)(int sys, int ppm)` | `SetVideoPll`, `ResetHdmiPll` | no second library; kept because no library touches a PLL register and the base's video path programs the same PLL (`videomode.cc:374-376`, offset 0) |
| `sid_socket` | `int (*)(int socket, int state, bool shunt, bool filter_caps)` | `I2C_Driver::i2c_write_byte` of `write_sid_sockets` | c64scpu and c128 can drive the same sockets; the encoding is board knowledge (spec section 5). 0 done, < 0 a bad argument |

### 11.6 Members added to `SvcSys`

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `sys.ms_timer` | `uint16_t (*)(void)` | `getMsTimer` | the 1 ms measurements of any library. A 1 ms hardware counter, `uint16_t` (`itu.h:122`), wrapping every 65.536 s: use differences. |
| `sys.yield` | `void (*)(void)` | `plat_yield`, `taskYIELD` | any library that loops and waits for a task. Gives the processor to a ready task of the same priority; returns at once when none is. `sleep_ms(0)` is no yield (4.3). |
| `sys.delay_us` | `void (*)(uint32_t us)` | `CoreIo::delay_us` | `atari_sio.cc`, `apple2_disk.cc`, `apple2_hdd.cc` |
| `sys.wall_time` | `bool (*)(uint8_t t[6])` | `get_fattime`, `rtc_time_valid`, `CoreIo::wall_time` | `amiga.cc`, `neogeo.cc`, `snes.cc`, `zxnext.cc`, `update.cc` |
| `sys.app_version` | `const char *(*)(void)` | `app_version_string` | any library that writes the version of the application into a file it makes (the ring's `appVersion`) |
| `sys.app_git` | `const char *(*)(void)` | `app_version_hash` (`gitinfo.c`) | the ring and the monitor both read it; the short hash of the commit of the application image, build identity like `app_version` |

`SvcNet` gets no member. The REST gate is the state `net.gated` of the registry (3.3).

## 12. The other direction: the core application calls the moving code

12.1 The core hooks. The descriptor already has the control hooks `init`, `release`, `warm_reset`, `cold_start` and `media_flush` and one poll operation (`desc.h:262-339`), named in the core's data and implemented by the core's code. `c64.cc` moves with the core (2.11), so three of the five hooks that an earlier draft of this page asked for are calls inside one library and are no base hook:

| Earlier id | Call | Now |
|---|---|---|
| H2 `reset` | `trx64u_sid_cancel`, `u64cfg_power_on_keys` from `c64_init` (`c64.cc:103`, `110`) | `c64_init` is the descriptor's `init` hook in the core's library and calls them there. |
| H3 `rom_put` | `trx64u_kernal_put`, `trx64u_basic_put` from `op_put_kernal`, `op_put_basic` (`c64.cc:72`, `80`) | The two operations move with `c64.cc`; the descriptor names them as the `put_op` of the ROM roles (`roms[]`, `c64.cc:89-93`). The fast-reset patch is made against the KERNAL copy that the operation receives. |
| H4 `before_run` | `trx64u_load_assoc_cfg` from `queue_prg` and `op_run_crt` (`c64.cc:295`, `600`) | The operations move with `c64.cc`; the call is inside the library. |

Two hooks remain, because the generic application calls them. Both are members of the table `CoreHooks` (`core_hooks.h`, the export "corehooks"), named in the core's data (D8) and implemented in the core's library; they are not members of `LibRecord`. A core that leaves them null is as before.

| Id | Hook | Called when | Used for |
|---|---|---|---|
| H1 | `start` | once, after the groups of 2.4 are installed and before the polls start (`run.cc:691-692` today) | The core's library calls the export of the ring library (its init, which reads `Timewarp Ring`; `ring_autostart` becomes a variable of that library) and of the monitor library. The library names both in `LIB_NEEDS`. |
| H5 | `menu_closed` | the menu closes and the machine is about to run, after the menu is hidden and before the machine is let go (`run.cc:795-800`) | The core's library runs its own operations `monitor.release` and then `ring.unfreeze` (calls between its own libraries; no generic code names them). It has no result. Task rules (`core_hooks.h`): it runs on the main loop task from `close_menu` (`run.cc:792`), once per close, without the core lock; `open_menu` has set `frozen` and passed the lock once, so no ordinary poll is in the core while it runs (when the core had started); a poll declared `when_frozen` and an executor operation may still run beside it, as beside `mon_release` today. `ctx.state` still shows `CTX_MENU_OPEN` and `CTX_FROZEN`. It takes the place of the call of `mon_release` (`run.cc:795`) and of the ring calls (`run.cc:797-800`) and runs where the ring calls run, so the monitor's release comes a few lines later than today. |

12.2 Calls and their replacements:

| From | Calls | Replaced by |
|---|---|---|
| `c64` | `trx64u_register`, `u64cfg_register`, `ringops_register`, `monitor_register` (`c64.cc:704-709`) | `lib_register` of each library (3.1). `c64_register` moves with `c64.cc`. |
| `c64` | `trx64u_sid_service` (`c64.cc:378`, in `c64.service`, every 20 ms) | Unchanged in kind: `c64.service` moves with `c64.cc`, the descriptor keeps naming it as `service_op` (20 ms), and it still runs in sequence with `op_service_job` and `cbm1541_service`. The call is inside the core's library. |
| `c64` | `trx64u_sid_cancel`, `u64cfg_power_on_keys`, `trx64u_kernal_put`, `trx64u_basic_put`, `trx64u_load_assoc_cfg`, `cfg_value` (`c64.cc:72-110`, `270`, `295`, `600`) | Calls inside the core's library (12.1). `cfg_value` is `settings.cfg_value` (6.2). |
| `run` | `cfg_value`, `ring_autostart` (`run.cc:691`), `trx_ring_engine_init` (`run.cc:692`) | Hook H1. |
| `run` | `mon_serve` (`run.cc:1078`, every pass of the main loop, menu open or not) | `LibRecord.poll` of the monitor library with `when_frozen` true and a short `period_ms` (the loop pass). The poll as served ignores `when_frozen` (2.5), so the base passes it on (13.3). The descriptor's own poll cannot serve it: it is `c64.service`. Another way: a job with a loop and `net.wait`. |
| `run` | `mon_release` (`run.cc:795`), `trx_ring_pending`, `trx_ring_unfreeze` (`run.cc:799-800`) | Hook H5. |
| `screens` | `mon_render`, `mon_rows`, `mon_line`, `mon_attr`, `mon_cursor`, `mon_sig` (`screens.cc:867-885`, `1497-1498`) | The generic UI need is a text grid. The layout `terminal` exists already (`menu.toml:40`, drawn by `build_terminal`, `screens.cc:1496`). It needs no new node kind and no new `TabWidget`. It needs a layout parameter `grid` (read with `layout_param`, as `lines` and `input` are, `screens.cc:1405-1410`) that names an export, and an SDK header `textgrid.h` with the table of that export: `render`, `rows`, `line(r)`, `attr(r)`, `cursor(*x, *y)`, `sig`. The grid has 80 columns and `MON_ROWS_MAX` rows, an attribute per row and a cursor; the screen redraws when `sig` changes. Keys go through the operations that exist (`monitor.key`). The sub-app trxmon draws the same grid through ABI 2. |
| `menuctl` | `monitor.key`, `monitor.open`, `monitor.tick`, `monitor.wants_stop` | `monitor.key` and the state `monitor.wants_stop` are operations of the registry (`monitor.cc:318-325`) and stay. `monitor.open` and `monitor.tick` are no longer called by menuctl: the text grid's `enter` (the operation `monitor.open`, through the executor under the core lock) and `tick` (`mon_tick`, on the menu task) take their place (`monitor_grid.cc`, api.md 9.6). |
| `diag` | `trx_ring_trace_status`, `trx_ring_trace_read` (`diag.cc:120-126`, `246`) | The export "ring" of `api->lib->find` on the base side (`LibLoader::svc_lib_find`). The diag core shows "no trace" when the library is not loaded. |
| `diag` | `trx_target_path` (`diag.cc:185`) | `ctx.target_path`, moved into the core application (6.2). |
| `menuflows`, `netfs`, `netops` | `cfg_value`, `cfg_text` | `settings.cfg_value`, `settings.cfg_text`, moved into the core application (6.2). `menuflows.cc:44` reads the key `Leave Menu on Mount` and `netops.cc` reads keys of the trx64u menu (`Use DHCP`, `Debug Stream Mode`, `Network Password`, the host names); a key of one core's menu in generic code is a point for the ledger (D24). |
| `c64_cart` | `trx64u_uci_df1c` (`c64_cart.cc:656`) | Decided: the shared UCI library owns the `$DF1C` handling. `c64_cart.cc` calls the export "uci" (`uci_api.h`): `set_slot_enable(1)` then `set_slot_base(0x47)`, the order `trx64u_uci_df1c` wrote (`trx64u_ops.cc:290-291`). The capability test of that function (`desc_has_capability("uci")` and the board probe, `trx64u_ops.cc:289`) is made inside the slot calls of the export with `sys.has_capability("uci")` (3.1: the same test over the descriptor struck with the probes). Read: `run.cc:468` strikes the capabilities with the probes when `board_caps` is set, and the one descriptor that lists `uci` (`c64.cc`: caps with `uci`, `board_caps` true) sets it, so the answer is the old one; the run loop always has a board profile, so the `!c->board` case never arises. The rule of the export is therefore: writes ignored and `slot_enable` reads 0 unless `sys.has_capability("uci")`; a descriptor that lists `uci` sets `board_caps`. The registers are the UCI's own (`command_intf.h:15-16`). No call into the TRX64-Ultimate library remains, and the ring uses the same three calls for its `cmd_if_*`. |
| `usb_hid` | `u64_get_usb_hid_config_value` (weak in `host-app/software/io/usb/usb_hid.cc:418`) | Decided and built: the base image defines the function, in `baseset.cc` (L-1: it was in `mousesink.cc`, which is a library now), and it calls `baseset_usb_config` (`baseset.cc`); no call into the TRX64-Ultimate library remains. A library cannot define the strong symbol that the image's weak reference binds at link time. The body reads the five items 0x55, 0x56, 0x57, 0x5D, 0x5E from the settings by key (`Mouse Mode`, `Mouse Wheel Sensitivity`, `Mouse Wheel Direction`, `Mouse Sensitivity`, `Mouse Acceleration`); 0x58 answers 0; the driver's default for a core without the setting. It asks the mouse table's `scaling` (`mouse_sink_scaling`, set by the Amiga's `mouse.sens` and `mouse.accel`) first and calls `baseset_usb_config` when the table has no answer. The five names are settings the base owns and declares for every core with a mouse (`svc_settings.h`), like the UI look and `Joystick Input`; trx64u's menu drops its copies (the stored values carry over because the keys stay) and so does `amiga.cc` for its two. So no key of one core's menu is in generic code and the D24 ledger gets no line. No `LibApi` member. |
| `appsvc` | the 15 `trx_ring_*` of `g_ring`, `trx_ring_landed`, `trx_ring_frame_cycles`, `trx_ring_fps`, `trx_ring_fps_x100`, `trx_ring_reachable` (`appsvc.cc:150-251`) | D20: the table leaves the base. The exports "ring" and "trx" of the libraries replace `g_ring` and `g_trx` (12.4). |
| `tests` | `board_stubs.cc` stubs the symbols of the moving code; `trx64u_menu.h` and `render.cc` read the menu record | The tests of the moving code go with it (R9). The stubs go away when the symbols do. |

12.3 The poll of 2.5 (`when_frozen`), the hooks H1 and H5 and the `grid` parameter are the base changes on which the move depends most. R2 is built for the poll; H1, H5 and the grid are not in the spec yet.

12.4 The three tables under D20 (agreed with the TRX64-Ultimate session, spec section 5). The tables leave `LibApi`: `SvcTrx` (`regs`, `caps`, `acquire`, `release`, `announce`, `ring`, `video`, `reset_c64`), `SvcRing` and `SvcC64`. Each is an export of a library, found with `lib->find(name, major, &size)` (base 3.2):

| Export | Major | Layout | Provided by |
|---|---|---|---|
| "trx" | 1 | `u2p_trx_api_t`, the layout of the TRX64-Ultimate `app_api.h` | their library |
| "ring" | 1 | `u2p_ring_api_t`, the layout of the same header (`app_api.h:458-520`). It has what `SvcRing` lacks: `dump`, `save`, `load`, `trace_writes`, `load_entry`, `save_native`. | the ring library |
| "c64" | 1 | `C64Api`, an SDK header: today's `SvcC64` members in their order, then `dma_read_frozen`, `cart_preference` and `reset` (which replaces `SvcTrx.reset_c64`) | the core's library; any C64-family core may provide it |

Rule: `size` first; within a major, members are only appended; removing or reordering a member makes the next major.

What changes in the base: `g_ring` (`appsvc.cc:226`), `g_trx` (`appsvc.cc:230`), `g_c64` (`appsvc.cc:95`) and `set_machine_groups` (`run.cc:707-711`) for the three groups go. A sub-app (trxmon, trex_crt) reads the tables with `lib->find`; with no such library loaded it reads null, as it reads null for `trx` and `c64` on a core without the TRX ID block today. The six members of `u2p_ring_api_t` that `SvcRing` lacks (`dump`, `save`, `load`, `trace_writes`, `load_entry`, `save_native`) are in the export; their functions are in `ringfile.cc` (`trx_c64re_dump_ex`, `trx_c64rering_dump_ex`, `trx_c64re_load`, `trx_trxring_load`) and `ring.cc` (`trx_ring_trace_writes`).

The debug block has no owner today (`s_owned`, `appsvc.cc:120`), and `appsvc_end` releases it at the end of every application (2.6). With "trx" in their library, `acquire` and `release` are their code, so the base cannot release what an ended sub-app took unless the library is told. How the end of an application reaches the library that holds the block is not decided here (13.4).

12.5 What `c64.cc` itself reaches. It is not measured (2.11); this is what its source names, and none of it is mapped yet:
- `boot_cold_reset` (`c64.cc:307`, `454`, `586`): `api->ops->call("machine.cold_reset")` (3.1).
- `cbm1541_register`, `online_register`, `diag_register` from `c64_register` (`c64.cc:703-709`): each registers for its own library or for the base, and the descriptor stops calling them.
- `cbm1541_service`, `cbm1541_slot_spec`, `cbm1541_slot_mount`, `cbm1541_slot_eject` (`c64.cc:380`, `623-642`), `cbm1541_media_flush`, `cbm1541_media_pending` (the descriptor, `c64.cc:761`) and the macro `CBM1541_ROM`: the `cbm1541` table (6.1).
- `c64_cart_run`, `c64_cart_remove`, `c64_cart_player_end`, `c64_cart_active`, `c64_cart_path`: the `c64_cart` table (6.1).
- `cbm_prg_place`, `cbm_prg_type_start`, `cbm_prg_script_step`, `cbm_prg_script_check` (`cbm_prg.h`), `t64_load`, `memops`: shared code of the base or its libraries.
- `nf_copy` (`newfile.h`), `ops_call` with `settings.set` and `c64.fx_roms` (`c64.cc:506`, `511`), the `mouse` record and `hk_c64`: base data and calls.
- `c64_go.cc` has one user, `c64.cc`, and no call into the base beyond `CoreIo` (`io.*`, 11.4).

## 13. What the build of the package needs next

13.1 The SDK runtime source: `snprintf`, `sscanf`, `atoi`, `strtoll`, `strcasecmp`, `strspn`, `strstr`, `isalpha`, `toupper`, `calloc`, `new`, `delete`, `printf`, `puts` (section 5). `libc.c` has `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strchr`, `strrchr`, the 32-bit and 64-bit division and the 64-bit shifts.

13.2 The headers of constants of 8.3 and 9, the keys header, `C64Api` and `textgrid.h`.

13.3 The changes of the base that the rows name:
- `svc_state` for `set.<key>` (2.3, 3.1).
- The start order of 2.4 and the core hooks H1 and H5 (12.1); the `grid` parameter of the layout `terminal` (12.2).
- The poll's `when_frozen` and its lock (2.5).
- The per-owner account of S14 for memory, files, sockets, jobs and the debug block, the owner set per task around every entry into a library, and `appsvc_end` and `stop_jobs` closing only what the ended application holds (2.6, 2.7).
- The base operations `machine.put_builtin_rom` (3.1) and `board.sid_socket(socket, state, shunt, filter_caps)` (4.4), the state `net.gated` (3.3), and the ten settings of the generic application (4.4: `Overlay Transparency`, `Color Scheme`, `Filename overflow squeeze`, `Navigation Style`, `Joystick Input`; 6.2: the five USB mouse settings).
- The S13 budget check with a permanent and a peak heap figure per library (R5), `lib.mk` forcing `hidden.h` for C++, and the base honouring a poll's `when_frozen` (2.5): the same three items as in the summary (c).
- `fs.list` with full names, as a second call, because `SvcFsEntry` is frozen (3.4). A create flag for `CoreFs::open(rw)` is not needed: `save(path, "", 0)` then `open(path, true)` does it (3.4).
- The new groups and members of section 11; the removal of `SvcTrx`, `SvcRing` and `SvcC64` from `LibApi` (12.4).
- The comment of `sleep_ms` in `svctab.h` (done in the commit of this page); the comment at `libapi.h:21` that says the poll is "not served yet" is stale (2.5) and is corrected with the poll change.

13.4 Not measured or not decided: `.init_array` in the seven objects (2.9); the size of each library against `U2A3_IMAGE_MAX` (R5, measured by the TRX64-Ultimate session, spec section 5); the time of a poll under the core lock; the symbols of `c64.cc` and `c64_go.cc` (2.11, 12.5); how the end of an application reaches the library that holds a debug block (12.4).

## 14. The pending lists of the TRX64-Ultimate libraries (U18 `pending_api.h`, branch `u18`)

14.1 The three libraries declare the calls that base 3.1 lacks and link against none of them. The count is base 59, ring 47 (44 and the three cartridge calls `c64_cart_require`, `c64_cart_prohibit`, `c64_cart_writable` that `u18` `2d4c2dcc` added), monitor 3. The table of each library with the final name, header, signature and note is `docs/dev/boundary-trx64u-handover.md`. This section adds the rows that sections 3 to 12 did not have (the names the libraries use, the members of `CoreCtx` and `CoreIo` that `nm` does not show) and records the decisions of the reading.

| Pending name (library) | Final form | Note |
|---|---|---|
| `settings_store` (base) | `settings.find` + `settings.set` | `Settings::set` stores and marks the setting changed, writes the `.cfg` only with Auto Save Config on, and does not apply (`settings.cc:270-290`); the call is a composition, no member of its own. |
| `settings_override` (base, ring) | `settings.find` + `settings.override_value` + `settings.apply` | `override_value` does not apply (`settings.cc:148`). The ring's `settings_override(c, key, value)` returned 0 when the setting had the value: `value` is compared by the library. No member of its own. |
| `settings_extra_get`, `settings_extra_set` (base) | `settings.extra_get`, `settings.extra_set` | 4.1. |
| `machine_started`, `machine_frozen`, `machine_paused`, `menu_is_open` (base) | `ctx.state` | Bits `CTX_STARTED`, `CTX_PAUSED`, `CTX_FROZEN`, `CTX_MENU_OPEN`. |
| `machine_set_paused`, `menu_media_dir`, `ui_notice` (base) | `ctx.set_paused`, `ctx.media_dir`, `ctx.notice` | 4.5. |
| `ui_notice_pending` (base) | removed: `ctx.notice(text, only_if_free)` | The only reason to ask was the rule of `u64cfg.cc:1229` (write only when none waits). |
| `board_has_uci` (base) | `sys.has_capability("uci")` | 3.1: the same test over the struck descriptor. |
| `corefolder_dir` (base) | `ctx.core_folder(NULL, ...)` | 4.5. |
| `io_ddr_read`, `io_ddr_write`, `io_keys_read` (base) | `io.ddr_read`, `io.ddr_write`, `io.keys_read` | 8.2. |
| `sys_delay_us` (base) | `sys.delay_us` | 4.3. |
| `boot_cold_reset`, `boot_put_builtin_rom`, `media_writeback` (base) | operations `machine.cold_reset`, `machine.put_builtin_rom` (new), `media.flush_for_core` | 3.1. |
| `hw_esp32_rpc`, `SetVideoPll` + `ResetHdmiPll`, `picture_set_system` (base) | `board.esp32_rpc`, `board.video_set_pll`, `board.video_set_system` | 4.4. |
| `video_color_timing`, `video_sys_name` (base) | theirs | 4.4, 6.3. |
| `mouse_sink_scaling` (base) | the mouse library's table `scaling`, asked first by `u64_get_usb_hid_config_value` | 6.2, 12.2 (`usb_hid`). |
| `tempdisk_set` (base) | export "tempdisk" (`tempdisk_api.h`) | D12: the temp RAM disk stays central. The only caller is `fx_ui` (`trx64u_ops.cc:76`); the base already mounts the disk (`coreapp.cc:101`). |
| `c64_cart_*` (base 6, ring 11) | export "c64cart" (`c64cart_api.h`) | 6.1. The ring's `CoreCtx *` arguments go. The ring's 11: `active`, `name`, `path`, `crt_type`, `image_bytes`, `image_read`, `image_write`, `for_state`, and `require`, `prohibit`, `writable` (new in the export, same meaning as the pending lines of `u18` `2d4c2dcc`). |
| `sid_*` (base 6) | export "sid" (`sid_api.h`) | 6.1. |
| `uci_start` and `struct UciHost` (base) | export "uci" (`uci_api.h`), `start(const UciHooks *)` | 6.1. The callbacks lose the `CoreCtx *`; the export's type is `UciHooks`, because an export header's types carry the prefix of their table and never reuse a name of an internal header (`uci.h` keeps `UciHost`). |
| `cbm1541_drive` and the 15 `cbm_drive_*` (base) | export "cbm1541" (`cbm1541_api.h`) | 6.1. The class does not cross the library boundary: `set_options` (the five `set_*`), `drive_info` (`type`, `bus_id`, `powered`, `image_name`, `last_dir`, `mode`, `swap_delay_ms`), `create`, `slot_mount`, `slot_eject`. `mount` and `eject` are `slot_mount` and `slot_eject` with a spec (`cbm1541.cc:drive_mount`: a leading `ro ` or `unlinked ` is the mode). |
| `sync_mutex_create`, `sync_mutex_lock`, `sync_mutex_unlock`, `sync_enter`, `sync_leave` (ring, monitor) | `sync.mutex_new`, `sync.mutex_lock`, `sync.mutex_unlock`, `sync.enter`, `sync.leave` | 4.2. `SYNC_FOREVER` is `0xFFFFFFFF`. |
| `sys_ms_timer`, `sys_yield` (ring, monitor) | `sys.ms_timer`, `sys.yield` | 4.3. `getMsTimer` returns `uint16_t` (`itu.h:122`) and wraps every 65.536 s: use differences. The ring declares `uint32_t`, which has to follow. |
| `c64_stop` (ring) | `io.stop` | 8.2. |
| `c64_mode`, `c64_cartridge_type`, `c64_reu_enable`, `c64_reu_size`, `c64_sampler_enable` and their setters (ring) | `io.cart_rd`, `io.cart_wr` at `C64_CARTREGS_BASE` + 0, 5, 8, 9, 0xE | The offsets are the library's own constants (core API TOML, D8). A write of MODE for the cartridge mapping stays `c64.cart_mode` of the `C64Api`. |
| `c64_dma_memonly`, `c64_set_dma_memonly`, `c64_videoformat` (ring) | `io.bus_rd(3)`, `io.ram_memonly`, `io.bus_rd(1)` | `bus_rd` is new in `CoreIo` (a default that returns 0, as `cart_rd` has): no read of the bus bridge's block exists (`io.h`). The offsets 3 and 1 are the ring's own (core API TOML). |
| `cmd_if_slot_enable`, `cmd_if_set_slot_enable`, `cmd_if_set_slot_base` (ring) | export "uci": `slot_enable`, `set_slot_enable`, `set_slot_base` | The same three calls as `trx64u_uci_df1c` (12.2). |
| `c64_cart_ram`, `drive_a_image` (ring) | export "c64cart" `ram_size`, `ram_read`, `ram_write`; export "cbm1541" `image_size`, `image_read`, `image_write` | The ring's libraries do not reach DDR2 through `io.ddr_abs_*` (spec section 5): the bodies of these calls do, inside the shared libraries (both areas lie in the range the board allows, `hw.cc:116-139`: `0x00EA8000` to `0x00F00000`; the cartridge RAM at `0x00EF0000`, the drive A image at `0x00EE0000`). A pointer is not a call; the library copies in blocks. |
| `core_eeprom_read`, `core_eeprom_write` (ring) | export "c64cart": `eeprom_get`, `eeprom_put` | `ringops.cc:241-246` and `c64_cart.cc:471-493` write the 2048 bytes at +2048 and a byte at 0. The offsets stay in the cartridge library; `io.eeprom_read` and `io.eeprom_write` exist for it. |
| `fpga_capabilities` (ring) | `board.fpga_caps` | 4.4. |
| `get_fattime`, `rtc_time_valid` (ring) | `sys.wall_time` | 4.3. The year is `2000 + t[0]`. |
| `app_version_string` (ring) | `sys.app_version` | 6.3. |
| `ops_job_step`, `ops_job_progress`, `ops_job_cancelled` (ring) | `ctx.job_step`, `ctx.job_progress`, `ctx.job_cancelled` | 4.5. `job_cancelled` reads and clears (the operation sets `job_cancel` back itself, `cbm1541.cc:127`). |
| `trx_target_path` (ring) | `ctx.target_path` | 6.2. |
| `cbm1541_slot_mount`, `cbm1541_drive_a` (ring) | export "cbm1541": `slot_mount`, `drive_info(0, ...)` | `src_fill` (`ringops.cc:248-262`) reads exactly type, power, unit, image path and mode. |
| `trx64u_uci_df1c` (from `c64_cart.cc`) | export "uci" | 12.2. |
| `u64_get_usb_hid_config_value` (weak in `usb_hid.cc`) | base image, `baseset.cc` | 12.2. |
| `trx64u_sid_service`, `trx64u_sid_cancel`, `trx64u_kernal_put`, `trx64u_basic_put`, `trx64u_load_assoc_cfg`, `u64cfg_power_on_keys` (from `c64.cc`, `c64_go.cc`) | calls inside the TRX64-Ultimate library | 12.1, 12.2: `c64.cc` and `c64_go.cc` move with the core. |

