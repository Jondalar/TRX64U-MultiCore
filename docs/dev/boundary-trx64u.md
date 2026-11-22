# The boundary of the trx64u libraries (T239.3 P1)

## 1. General

1.1 This page maps every symbol that the moving trx64u code takes from outside the SDK to exactly one target. It maps the other direction as well: every place where the core application calls into the moving code.

1.2 The moving code is `trx64u_ops`, `u64cfg` (library `trx64u`), `ring`, `ringfile`, `ringops` (library `ring`), `monitor`, `monsrv` and the trxmon engine (library `monitor`), as the TRX64-Ultimate session lists them (U18 D5; T239.3 D2, D12).

1.3 Basis of the symbol lists: `software/trx64u/BOUNDARY.md` of the TRX64-Ultimate repository, measured on commit `3b8e2955` with `riscv64-elf-nm` over the objects of the moving files. That measurement lists symbols. It does not list the fields of `CoreCtx` and `CoreIo` that the code reaches, because those are types and inline code. Section 8 lists them. Section 9 lists the headers.

1.4 Basis of every row: the definition in this tree (`t239.3-boundary`, base `de3fbfc2`), read from the code. A row says what the code does. A comment is not evidence. Line numbers are those of this tree.

1.5 The kinds of target:

| Kind | Meaning |
|---|---|
| `api` | An existing call of `LibApi` (`group.member`), or a record of `lib_register`. The row says whether it does the same as the symbol and what differs. |
| `new-api` | A new call of `LibApi`. Section 11 gives group, name, signature and contract. The row names the other users. |
| `sdk-runtime` | Provided inside the library by the SDK build: libgcc, the pure parts of the C library, `printf` and `puts` over the log call, `operator new` and `delete` over `SvcMem`. |
| `shared-lib` | An export of a D12 shared library of this repository. The row names the library and its call table. |
| `theirs` | Code inside the TRX64-Ultimate libraries, or a call between them through `SvcLib`. |
| `decision` | Depends on U18 R3 or R4. The row says what each answer means. |
| `base` | The code stays in the core application. The library does not call it any more, because the caller moves into the core application (section 12). |

1.6 Open decisions of the maintainer. This page does not decide them:

| Id | Question | Rows marked |
|---|---|---|
| R3 | Constructors (`.init_array`) and `atexit` in a library: the loader runs them, or the build refuses them. | `__cxa_atexit`, `__dso_handle` |
| R4 | The `trx`, `ring` and `c64` tables leave the base API and come from the TRX64-Ultimate libraries through `SvcLib`. | `trx_io_*`, the `SvcRing` rows of section 12 |

Interim rule given to the other session: the ring library exports its own table through `SvcLib` with the layout of its own header. The `trx` table stays `api->trx` until R4.

1.7 Abbreviations in the column "Users": `ops` = `trx64u_ops.cc`, `cfg` = `u64cfg.cc`, `ring` = `ring.cc`, `rfile` = `ringfile.cc`, `rops` = `ringops.cc`, `mon` = `monitor.cc`, `msrv` = `monsrv.cc`.

## 2. What reading the code changed

2.1 The handlers get `CoreCtx *`. In the package, `CoreCtx` is an opaque forward declaration (`ops.h:10`). The moving code reaches 18 fields of it and 18 methods of `CoreIo` (section 8). Without a call for each of them the code cannot compile against the package. This is the largest part of the boundary. BOUNDARY.md does not show it.

2.2 `cfg_value` and `cfg_text` are not library code. They are generic settings helpers that sit in `trx64u_ops.cc:27-62`. So is `trx_target_path` (`trx64u_ops.cc:753`): it reads `c->media_dir` and lists `/`. Six base files call them (`menuflows`, `netfs`, `netops`, `diag`, `run`, `c64`). They move into the core application (section 12).

2.3 The registry call is not the executor call. `ops_call` queues on the executor when one exists (`ops_glue.cc`: `c->exec->call`). `api->ops->call` runs `reg_->call` on the calling task, with no executor and no core lock (`libload.cc:158`). `ops_state` answers `set.<key>` from the settings (`ops_glue.cc`). `api->ops->state` does not (`libload.cc:176`). The monitor reads `set.<item>` (`monitor.cc:264`). So `api->ops->state` returns nothing for it today.

2.4 The groups are installed late. `lib_board_load` (`run.cc:607`) runs `lib_register` before the application host exists. The host sets `ctx_`, `api->dlg` and `api->job` in `install()` (`apphost.cc:66-68`), and `api->trx`, `api->net`, `api->c64` in `set_machine_groups` (`run.cc:707-711`). Until then `api->ops->call` answers "No operations here" (`libload.cc:150`) and every `api->fs` call fails (`fs_of()` is null). `trx_ring_engine_init()` and `ring_autostart` run at `run.cc:691-692`, before that point. A library init that needs `ops`, `fs`, `job`, `trx` or `net` cannot run inside `lib_register`. It needs a start hook (section 12, hook H1).

2.5 The poll is served in this tree. `run.cc:698-705` starts a task for each declared `LibPollDecl`. The statement "declared and kept, and not served yet" (`libapi.h:21`) and the rows R2 and S3 of the spec are out of date. The served poll has three limits that matter for the monitor and the ring. It holds the core lock for every call (`PollTask`, `service.cc`). It never runs before `started`, and not while the menu is open: `poll_allowed` is `started && (!frozen || when_frozen)` (`service.cc:6`), and `run.cc:701` passes `false` for `when_frozen`, so the field `LibPollDecl::when_frozen` is not passed on. Its task runs at idle+2 (`above_ui` 1, `run.cc:702`).

2.6 `appsvc_end()` is the end hook of every sub-app (`apphost.cc:392`). It closes every socket in `s_socks` and releases the debug block (`appsvc.cc:439-444`). The list has no owner. A sub-app that ends (trex_crt) would close the listener of the resident monitor library and drop its run control. `stop_jobs()` (`apphost.cc:391`) cancels every job in the same way. The job table has `APPJOB_MAX` = 4 slots shared with the sub-apps (`apphost.h:23`).

2.7 The memory account is only the library's during `lib_register` and `app_main`. `cur_` is set at `libload.cc:671` and `libload.cc:749` and cleared after. A block that a handler allocates later, on the executor or on a poll task, goes to "nobody's" account (`list_of(-1)`). The test S14 ("freeing the library gives all of it back") does not see it. `operator new` and `malloc` of the SDK runtime map to `SvcMem` as asked; the account rule needs a base change before the claim holds.

2.8 The clocks differ. `api->sys->ms` is `xTaskGetTickCount() * portTICK_PERIOD_MS` (`libload.cc:78`, `libboard.cc:51`) and the tick is 5 ms (`configTICK_RATE_HZ` 200). `getMsTimer` is the 1 ms hardware counter of the ITU. The ring measures with it (`st->ms_stop`, timeouts of 10 ms and 60 ms: `ring.cc:1028`, `1546`). `CoreIo::now_ms` extends the same counter (`hw.cc:207`). So `sys.ms` is not a replacement for `getMsTimer`.

2.9 `FileManager::getFileManager()` is a function-local static in an inline function (`filemanager.h:167-174`). A library that calls it builds its own `FileManager` with its own constructor and its own `__cxa_atexit`. That is a second file manager, not the board's. So the rows of `ringfile` and `ringops` that name `__cxa_atexit`, `__dso_handle`, `FileSystem_Root`, `mstring`, `pattern_match` and the critical sections come from this one source. They disappear when the file calls go through `SvcFs` (section 3.4). R3 then has no known user among the seven files. Whether any file has a global constructor (`.init_array`) is not measured. BOUNDARY.md lists symbols only. The check is `riscv64-elf-readelf -S` on the seven objects.

2.10 `printf` formats in the moving code use `%d %u %x %s %c` and widths. None uses `%ll`, `%f` or `%p`; 64-bit values go through `u64dec` (`ringfile.cc`). `u2p_vsnprintf` of `u2p_compat.c` is not enough for `snprintf`: it knows `d u x X s c`, and `%%` prints two characters. The SDK `snprintf` has to be written for this list. `ringfile.cc:37` says the board's `printf` has no precision, which is why `nf_copy` exists; an SDK `snprintf` with `%.*s` makes `nf_copy` unneeded.

2.11 `c64.cc`, `c64_go.cc`, `c64_cart.cc` and `cbm_run.cc` are named by neither U18 D5 nor T239.3 D12. This page assumes that `c64.cc` stays in this repository as the C64 machine code and calls the trx64u library through hooks (section 12, item H1). If the maintainer places `c64.cc` in the trx64u library, every row of section 12 for `c64` becomes a call inside one library and disappears.

2.12 `app_version_string()` is generated per build of the application image (`host-app/target/common/rules.mk:37-60`). The ring file writes it as `appVersion` into every `.c64re` (`ringfile.cc:1158`). A library has no such function. Row in section 6.

## 3. Kind `api`

### 3.1 Registration and operations

| Symbol | Users | Target | Note |
|---|---|---|---|
| `OpRegistry::add_op` | ops, cfg, rops, mon | `LibRecord.ops` (`LibOpDecl`) | Same four fields: name, `ArgSpec` list, count, `op_fn`. 70 operations: ops 32, cfg 17, rops 17, mon 4. All registrations are unconditional and static (`trx64u_register`, `u64cfg_register`, `ringops_register`, `monitor_register`), so they fit the static `LibOpDecl` arrays of the image. `r.contributor(...)` falls away: the loader sets the owner from the library name, so the names of the owners change from `trx64u`, `u64cfg`, `timewarp`, `monitor` to the library names. The table limit is `OPS_MAX` 400 (`ops.h:151`). |
| `OpRegistry::add_state` | ops, cfg, rops, mon | `LibRecord.states` (`LibStateDecl`) | Same two fields: name, `state_fn`. 69 states: ops 24, cfg 28, rops 16, mon 1. Limit `STATES_MAX` 320. |
| `ops_call` | ops, cfg | `api->ops->call` | Same arguments (`name`, `OpIn`, `nin`, `OpResult`). Differs: it runs on the calling task, never through the executor queue, and takes no lock (2.3). `Executor::call` does the same on the executor's own task (`exec.cc`: the lock is held already), so a call from a handler is unchanged. A call from a poll task is unchanged too (it holds the core lock; a queued call would wait for it). A call from a job task or any other task runs unlocked, where `ops_call` queues it. Needs the `ctx_` of 2.4. The code calls these names: `settings.set`, `settings.load_file`, `drive.mount_run`, `drive.set_rom`, `drive.mount`, `drive.eject`, `drive.swap`, `drive.set_power`, `c64.put_kernal` and the `put_op` of a ROM role. A library that calls them needs the library or the core that registers them. |
| `ops_state` | mon | `api->ops->state` | Same for registered states. Differs for `set.<key>` (2.3). One line in `LibLoader::svc_state` (call `setting_state` as `ops_state` does) closes it. The other way is the `settings` group (`settings.cfg_text`). |
| `boot_cold_reset` | ops | `api->ops->call("machine.cold_reset")` | `machine.cold_reset` is `op_hook<boot_cold_reset>` (`boot.cc:376`). The same function runs. The call returns `OpResult.code` (`OPE_FAILED`) and the reason in `msg`; the function returns 1 and writes `msg`. Other cores of this repository call `boot_cold_reset` directly (`c264`, `zxnext`, `apple2`, `amiga`, `cpc`, `atari_media`, `pet`, `cbm_run`). |
| `boot_put_builtin_rom` | ops | `api->ops->call` of a new base operation | No operation exists for it. `machine.reload_roms` reloads every ROM, this puts one role (`put_op`). The base registers `machine.put_builtin_rom(put_op)` next to `machine.cold_reset` (`boot.cc:375-378`). That is a registry entry, not a `LibApi` member. Only `ops` calls it (3 sites). |
| `media_writeback` | cfg | `api->ops->call("media.flush_for_core")` | `op_flush_for_core` calls `media_writeback(c, WB_WHY_CORE)` (`engines.cc`). The code calls it with `WB_WHY_POWER`. `wb_flush` ignores `why` (`writeback.cc:332`, `(void)why`), and `media_writeback` passes it nowhere else, so the result is the same. |
| `desc_has_capability(c->desc, "uci")` and `c->board->probes & BC_UCI` | ops | `api->sys->has_capability("uci")` | Inline code, not a symbol. `has_capability` reads `desc_has_capability(d_, ...)` (`libscan.h:62`) over the descriptor that `run.cc:465-475` has already struck with the board probes (`board_effective_caps`). For a descriptor with `board_caps` set the two tests give the same answer. For one without `board_caps` the probe test is not made. |

### 3.2 Tasks and time

| Symbol | Users | Target | Note |
|---|---|---|---|
| `xTaskCreate` (`ring_task`, `ring.cc:2817`) | ring | `LibRecord.poll` (`LibPollDecl`, S3) | The task loops `trx_ring_poll()` every 100 ms, and starts the log 6 s after the start. As a declared poll it runs under the core lock, never while the menu is open (2.5) and at idle+2 (now idle+1). While the menu is open the machine is stopped, so no marker comes; the scrub and the restore call `trx_ring_poll` themselves. The 6 s start delay becomes state in the poll operation. The core lock now excludes every operation during a poll: one poll reads up to `BOUNCE` (64 KB) through the mover. |
| `xTaskCreate` (`dump_task`, `ringops.cc:365`) | rops | `api->job->start` | One shot, 12 KB stack, ends itself. Same: a task of its own at idle+1. Differs: the stack is given in bytes and is at least 12 KB (`apphost.cc:495`); 4 slots shared with the sub-apps; `stop_jobs()` cancels it when a sub-app ends (2.6). The dump reads the menu's `c->job_cancel`, not `SvcJob.cancelled`. |
| `vTaskDelete` | rops | the return of the job function | `vTaskDelete(NULL)` ends the task. The job ends when `fn` returns (`apphost.cc:506-512`). |
| `vTaskDelay` (`ring_task`) | ring | `LibRecord.poll` | The delay is the poll period and the 6 s start delay (above). |
| `vTaskDelay` (`plat_sleep`) | mon | `api->sys->sleep_ms` | `plat_sleep` clamps to `PLAT_SLEEP_MIN_MS`, then `vTaskDelay(pdMS_TO_TICKS(ms))`. `sleep_ms` rounds up to the tick plus one (`libboard.cc:52`). It is one tick longer. |
| `vTaskDelay` (`net_send`, `monsrv.cc:98`) | msrv | `api->net->send` | `SvcNet.send` has the same wait loop (`appsvc.cc:355`). The library has no delay of its own. |
| `xTaskGetTickCount` | mon, msrv | `api->sys->ms` | The same product (`tick * portTICK_PERIOD_MS`). |
| `plat_yield` (`taskYIELD`) | mon | `api->sys->sleep_ms(0)` | `sleep_ms(0)` "only yields" (`svctab.h:25`). |

### 3.3 Network

| Symbol | Users | Target | Note |
|---|---|---|---|
| `lwip_socket`, `lwip_setsockopt` (`SO_REUSEADDR`), `lwip_bind`, `lwip_listen`, `lwip_fcntl` (`O_NONBLOCK`) | msrv | `api->net->listen(4312, 2)` | `net_listen` (`appsvc.cc:284`) does the same sequence with the same backlog (`monsrv.cc:505` uses 2). A taken port gives -5, which is `s_taken` today. |
| `lwip_accept`, `lwip_setsockopt` (`TCP_NODELAY`), `lwip_fcntl` | msrv | `api->net->accept` | Same, `TCP_NODELAY` and non-blocking are set inside. Differs: a free socket slot is needed (`NET_MAX` 16). Past it `accept` returns -5 and the connection stays in the backlog. `monsrv.cc:427` accepts and closes the extra ones. |
| `lwip_recv` | msrv | `api->net->recv` | Same call (`MSG_DONTWAIT`). Return codes differ: nothing waiting -5 (now -1), broke -3 (now -2), closed 0. |
| `lwip_send` | msrv | `api->net->send` | Same loop with the same limit (5000 ticks idle). Failure -3 (now -2). |
| `lwip_close` | msrv | `api->net->close` | A handle that was not made by `listen` or `accept` gives -1. |
| `lwip_getpeername`, `ip4addr_ntoa_r`, `lwip_htons` | msrv | `api->net->peer` | The same text `"ip:port"` (`appsvc.cc:422`). `htons` is inside `listen`. |
| `__errno` | msrv | `api->net` | Used only for `EAGAIN` after `lwip_recv` and `lwip_send`. `SvcNet` returns codes instead. |

### 3.4 Files

The moving code opens files through `FileManager::getFileManager()` in `ringfile.cc` and `ringops.cc`, and through `c->fs` (`CoreFs`) in `trx64u_ops.cc`, `u64cfg.cc` and `monitor.cc`. Both go through `api->fs`.

| Symbol | Users | Target | Note |
|---|---|---|---|
| `FileManager::fopen(path, FA_READ)` | rfile | `api->fs->open(path, false)` | The board's `CoreFs::open` calls the same `FileManager::fopen` (`fm_fs.cc`). `SvcFs` also applies the `/flash` rule (`fsif.h:44`). |
| `FileManager::fopen(path, FA_WRITE \| FA_CREATE_ALWAYS)` | rfile | `api->fs->save(path, "", 0)`, then `open(path, true)` and `writable` | `CoreFs::open` has no create (`fm_fs.cc:245`: `FA_READ \| FA_WRITE`). `trx64u_ops.cc:779-790` already does the two steps for its own writes. A `create` call would make it one call; it is not needed. A medium that cannot be written gives a read-only file; the code must test `writable`. |
| `File::read`, `File::write`, `File::seek`, `File::get_size` | rfile | `api->fs->read`, `write`, `fsize` | Every call in `ringfile.cc` is all-or-nothing (`got != want` is an error), which is the `SvcFs` rule. A seek is an offset. A write at offset 9 into the open file (digest, `ringfile.cc:1346`) is `write(f, 9, ...)`. A write at the end makes the file longer. |
| `FileManager::fclose` | rfile | `api->fs->close` | |
| `FileManager::release_mount_point`, `FileSystem_Root::FileSystem_Root`, `mstring::~mstring`, `pattern_match`, `vTaskEnterCritical`, `vTaskExitCritical`, `vQueueDelete`, and the `xQueueCreateMutex` uses of `rfile` and `rops` | rfile, rops | `api->fs` (falls away) | None is called by the moving source. They come from the inline code of `filemanager.h` (the destructor of the static `FileManager`, 2.9), `managed_array.h` (`ENTER_SAFE_SECTION`) and `mystring.h`. They disappear with the `FileManager` calls above. |
| `FileManager::delete_file` | rfile, rops | `api->fs->remove` | `remove` refuses a path under `/flash`; the ring files go to a medium. |
| `c->fs->open`, `close`, `stat`, `load`, `save`, `remove`, `mkdir` | ops, cfg, mon | `api->fs->open`, `close`, `stat`, `load`, `save`, `remove`, `mkdir` | Same. `CoreFile *` becomes `SvcFile *`. `c->fs->mkdir("/flash/roms")` and `save` into `/flash/roms` (`trx64u_ops.cc:548`) keep the same rule: the write is refused unless the board profile makes the flash drive writable. |
| `c->fs->list_short` | ops, cfg, mon | `api->fs->list` | `SvcFsEntry` has the layout of `FsEntryShort` without `cut` (56 bytes of name, 72 bytes). Same names as `list_short`. |
| `c->fs->list` with `FsEntry` | ops | `api->fs->list` | `trx64u_ops.cc:589` and `759` list `/` (names of media, short). `646` lists the folder of the mounted image and builds the next path from the listed name (`swap_next`). `SvcFsEntry` cuts a name to 55 characters, so a longer name gives a wrong path. This needs a `list` with full names before the move (open point, not decided here). |

### 3.5 Memory

The calls of the C library that allocate go to `SvcMem` through the SDK runtime (section 5). `SvcMem` takes the block from the library's account (2.7).

## 4. Kind `new-api`

The calls are defined in section 11. This table names the symbol, the call and the other users.

### 4.1 Settings

| Symbol | Users | Target | Note |
|---|---|---|---|
| `Settings::find` | ops, cfg, rops | `settings.find` | Same: index or -1, case-insensitive. The index is valid while `generation` does not change, because `run.cc:646` makes a new `Settings` object after the merge; `cfg` caches indexes per object (`u64cfg.cc:79`, `696`, `866`). Other users: every library of a core that reads its own settings; the cbm1541 library when it leaves the image. |
| `Settings::value` | ops, cfg, rops | `settings.value`, `settings.cfg_value` | Same. `cfg_value` is the helper of `trx64u_ops.cc:27` made generic. The base cache is keyed by the address of the key literal. With a library image that can leave, the key must be compared by content. |
| `Settings::text` | ops, cfg | `settings.text`, `settings.cfg_text` | `cfg_text` skips leading blanks, `text` does not (`trx64u_ops.cc:54-62`). |
| `Settings::layer` | ops | `settings.layer` | 0 default, 1 host, 2 core. Used once (`Navigation Style`). |
| `Settings::override_value` | ops, cfg, rops | `settings.override_value` | This run only. Three callers: look and navigation (`ops`), power-on keys (`cfg`), the system mode of a loaded `.c64re` (`rops`). |
| `Settings::set` | cfg | `settings.set` | Writes the core layer and the `.cfg`. The index form; `settings.set` of the registry takes a label. |
| `Settings::extra_get`, `Settings::extra_set` | cfg | `settings.extra_get`, `settings.extra_set` | Keys with the prefix `Pad ` only (`settings.cc`). One user today (`Pad SID Detected Socket`). |
| `regset_apply_setting` | rops | `settings.apply` | Applies setting `i` through its operation or its register commit (`regset.cc:32`). D8 makes the register table TOML that the SDK implements, so the call stays in the base. |
| `c->desc->settings[ix].base`, `.nvalues` | cfg | `settings.info` | Inline code (`u64cfg.cc:706`, `883`). |

### 4.2 Sync

| Symbol | Users | Target | Note |
|---|---|---|---|
| `xQueueCreateMutex` (`xSemaphoreCreateMutex`) | ring | `sync.mutex_new` | `ring.cc:2811`. The uses in `rfile` and `rops` fall away (3.4). Priority inheritance is part of the contract. |
| `xQueueSemaphoreTake` (`xSemaphoreTake`) | ring | `sync.mutex_lock` | `portMAX_DELAY` (`ring.cc:180`) and 0 (`ring.cc:1949`). Both timeouts are needed. |
| `xQueueGenericSend` (`xSemaphoreGive`) | ring | `sync.mutex_unlock` | |
| `xQueueSemaphoreTake`, `xQueueGenericSend` (use in `cfg`) | cfg | `board.i2c_write` (the lock is inside it) | Inline code of `I2C_Driver::i2c_lock` (`i2c_drv.h:46-62`, `xSemaphoreTake(mutex, 5000)`). |

`vQueueDelete` has no use in the moving source (3.4). `sync.mutex_free` is in the group only so that a library can free a mutex it made.

### 4.3 Time and clock

| Symbol | Users | Target | Note |
|---|---|---|---|
| `getMsTimer` | ring, rfile | `sys.ms_timer` | The 1 ms counter, 16 bit (2.8). 31 lines in `ring.cc` and `ringfile.cc`, all differences of two reads. |
| `CoreIo::delay_us` | cfg | `sys.delay_us` | Busy wait (`hw.cc:229`, `wait_10us`). 9 uses in `u64cfg.cc` with 10 us. Other users: `atari_sio.cc`, `apple2_disk.cc`, `apple2_hdd.cc`. |
| `get_fattime`, `rtc_time_valid` | rfile | `sys.wall_time` | `CoreIo::wall_time` gives `y m d h mi s` (year 0-99) and false when the board has no clock (`io.h:118`, `hw.cc:218`). The file code wants the year to be at least 2024 and a valid time: the same. Other users: `amiga.cc:271`, `neogeo.cc:560`, `snes.cc:379`, `zxnext.cc:172`, `update.cc:39`. |
| `CoreIo::delay_ms` | ops, cfg | `api->sys->sleep_ms` | In `api`. `delay_ms` is `vTaskDelay(pdMS_TO_TICKS(ms) + 1)` (`hw.cc:228`). `sleep_ms` is `ms >= 5 ? pdMS_TO_TICKS(ms) + 1 : 1` (`libboard.cc:52`). Equal for 1-4 ms (one tick) and for 5 ms and more. |
| `CoreIo::now_ms` | ops, cfg | `api->sys->ms` | In `api`. The uses are 500 ms timeouts (`u64cfg.cc:756`, `1189`, `1194`) and a timestamp (`trx64u_ops.cc:1488`). The 5 ms resolution is enough there. |

### 4.4 Board, video and UI

| Symbol | Users | Target | Note |
|---|---|---|---|
| `hw_esp32_rpc` | ops, cfg | `board.esp32_rpc` | Power mode, wake on Wi-Fi, power off, power cycle. One shared UART with one reply table (`hw.cc:444`); a library cannot own it. Other users: `rocker_report_task` of the base (`hw.cc:409`). Other cores that offer power off or Wi-Fi wake. |
| `i2c`, `I2C_Driver::i2c_write_byte` | cfg | `board.i2c_write` | The SID socket expander on the 1.8 V channel: lock, channel, two byte writes, unlock (`u64cfg.cc:1250-1256`). The lock belongs to the base driver, which other base code shares. |
| `getFpgaCapabilities` | ops, rfile | `board.fpga_caps` | The ITU capability register, four bytes (`itu.c:19`). Not the TRX `CAPS`. Other users: `uci.cc` (`520`, `1388`), `hw.cc`, `board.cc`. |
| `isCommodoreBoard` | ops | `board.is_commodore` | `product.cc:92`. Used once (`Navigation Style` default). |
| `picture_set_system` | cfg | `board.video_set_system` | `picture.cc:543`. Other users: `c128.cc:433`, `c64scpu.cc:105`, `portdiag.cc:63`. |
| `SetVideoPll`, `ResetHdmiPll` | cfg | `board.video_set_pll` | `fx_video` calls them as a pair (`u64cfg.cc:275-276`). One call. |
| `color_timings` | cfg | `board.video_color_timing` | A table of the vendor firmware (`color_timings.h`). The code reads `phase_inc`, `mode_bits`, `audio_div`. A copy-out call keeps the table in the base. |
| `ui_set_look`, `ui_set_name_squeeze` | ops | `board.ui_set_look`, `board.ui_set_name_squeeze` | `ui.cc:1261`, `1269`. One user today. The alternative is that the base owns the three settings (`Overlay Transparency`, `Color Scheme`, `Filename overflow squeeze`) as it owns Picture and Volume (`run.cc:464`). The maintainer has not placed them on the capability map. |
| `keyb_joy_select`, `wasd_to_joy` | cfg | `board.keyboard_joystick` | Two globals of the keyboard driver that `fx_misc` sets together with the registers `U64II_KEYB_JOY` and `MATRIX_WASD_TO_JOY` (`u64cfg.cc:1643-1644`). One user today. |

### 4.5 Context of the running core

| Symbol | Users | Target | Note |
|---|---|---|---|
| `cfg_value`, `cfg_text` | rops, base | `settings.cfg_value`, `settings.cfg_text` | See 2.2 and section 12. |
| `trx_target_path` | rops, diag | `ctx.target_path` | See 2.2. |
| `corefolder_dir` | ops | `ctx.core_folder` | The path of the core folder from `c->cor_path` (`corefolder.cc`). Three sites for the SID player files. The sub folders `roms`, `saves`, `configs` are the same call with a name. Other cores need `roms` and `saves`. |
| `c->started`, `c->paused`, `c->frozen`, `c->menu_open` | ops, cfg, rops | `ctx.state`, `ctx.set_paused` | `trx64u_ops.cc:723-733` toggles `paused`. |
| `c->notice`, `c->notice_pending` | ops, cfg | `ctx.notice` | `u64cfg.cc:1229` writes only when none is pending; `trx64u_ops.cc:1501` writes always. |
| `c->media_dir` | ops | `ctx.media_dir` | |
| `c->job_step`, `c->job_cancel`, `c->job_pct`, `c->job_path` | rops | `ctx.job_step`, `ctx.job_cancelled`, `ctx.job_progress` | The progress protocol of the menu task (`menuctl.cc` `start_job`). `ringops.cc:365` and the dump. Other operations that write a file in steps. |
| `c->io->...` | ops, cfg, rops | `io.*` | Section 8. |

### 4.6 Network

| Symbol | Users | Target | Note |
|---|---|---|---|
| `rest_uic` | msrv | `net.gated` | `gate_refused` (`monsrv.cc:172`) refuses the methods of `monsrv_gated` while the REST service has its token (`rest.cc:416`, `543`): the monitor socket has no token. The test is a null check of one pointer. The sub-app trxmon serves the same methods through `api->net` and needs the same answer. |

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

### 6.1 `shared-lib` (D12)

A shared library has one call table. A moving library reaches it with `api->lib->find(name, version, &size)`. A table is a structure of function pointers in the layout of the library's own header. The functions of these libraries take `CoreCtx *`; the pointer passes through unchanged.

| Symbol | Users | Library, table | Note |
|---|---|---|---|
| `c64_cart_active`, `c64_cart_name`, `c64_cart_path`, `c64_cart_crt_type` | ops, rops | `c64_cart`, `active`, `name`, `path`, `crt_type` | Pure reads of the state. Other users in this repository: `c64.cc:537`, `625`. |
| `c64_cart_image_bytes`, `c64_cart_image_read`, `c64_cart_image_write` | rops | `c64_cart` | The cartridge image in the DDR2 window. |
| `c64_cart_for_state` | rops | `c64_cart` | Puts the cartridge of a loaded `.c64re` in place and resets. |
| `c64_cart_save`, `c64_cart_ef_clear`, `c64_cart_run_image` | ops | `c64_cart` | |
| `c64_cart_run_modplayer` | ops | none | Falls away (T239.3 D5). |
| `sid_parse`, `sid_plan`, `sid_requests`, `sid_info_text`, `sid_mus_header`, `sid_mus_stereo` | ops | `sid`, six members | Pure functions over `SidTune` (`sidfile.h`). The structure `SidTune` is the library's header. |
| `uci_start` | ops | `uci`, `start(c, UciHost *)` | The `UciHost` is a table of callbacks that the caller (trx64u) supplies: REU, drive info, drive operation, SID map, EF clear, RAM drive, palette (`trx64u_ops.cc:1051-1057`). The callbacks stay in the trx64u library. `uci` also needs `board.fpga_caps`. |
| `tempdisk_set` | ops | `tempdisk`, `set(auto_cleanup, subfolders)` | |
| `cbm1541_drive` | ops | `cbm1541`, `drive(c)` | Returns a `CbmDrive *`. |
| `CbmDrive::create`, `mount`, `eject`, `mode_name`, `set_reset_with_c64` | ops | `cbm1541` | BOUNDARY.md counts these five, because the others are inline. The class is used with these further members: `set_twist`, `set_align`, `set_freeze`, `set_geos`, `image_name`, `last_dir`, `swap_delay_ms`, `type`, `bus_id`, `powered` (`trx64u_ops.cc:463-715`, `1029-1035`), and `image_path`, `mount_mode` in `ringops.cc:255-261`. A call table of a class needs one entry for each. `c->drive` (`ringops.cc:255`) becomes `cbm1541.drive`. |
| `cbm1541_slot_mount` | rops | `cbm1541`, `slot_mount` | |
| `u64_resample.inc` | cfg | `resample`, `program(mode)` | Two coefficient tables (675 and 512 taps) and the register loop `set_resample_filter` (`u64cfg.cc:239-253`). `program` takes `audio_div` from `board.video_color_timing`. Only `cfg` uses it. |

Today the users of these libraries are the trx64u files only. D12 names the later users: C64SCPU and C128 for the cartridges, the Amiga mouse. Each library image is at most `U2A3_IMAGE_MAX`, 1 MiB (`libfmt.h:40`). R5 (the size of each library) is not measured here.

### 6.2 `base`

| Symbol | Users | Target | Note |
|---|---|---|---|
| `mouse_sink_scaling` | cfg | `base` | Called only from `u64_get_usb_hid_config_value` (`u64cfg.cc:1674`). That function moves into the core application (section 12, row `usb_hid`), where `mouse_sink_scaling` is already (`mousesink.cc:58`). The library no longer calls it. The Amiga uses the same path today. |
| `cfg_value`, `cfg_text` | rops, base | `base` | Defined in `trx64u_ops.cc`. They move into the core application and are offered as `settings.cfg_value` and `settings.cfg_text` (4.1, 11.2). |
| `trx_target_path` | rops, diag | `base` | Defined in `trx64u_ops.cc`. It moves into the core application and is offered as `ctx.target_path` (4.5, 11.3). |

### 6.3 `theirs`

| Symbol | Users | Target | Note |
|---|---|---|---|
| `trx_ring_commit`, `error`, `find`, `fps`, `fps_x100`, `frame`, `frame_cycles`, `land`, `landed`, `log_read`, `reachable`, `status`, `trace_ctl`, `trace_read`, `trace_status`, `trace_whowrote` (16) | mon | `SvcLib` table `ring` | A call between their libraries (12.4). `fps`, `fps_x100` and `frame_cycles` are three functions over `C64_VIDEOFORMAT`; the monitor can also take them from `api->trx->video`. |
| `uic_json_get` | msrv | `theirs` | `gate_refused` reads `method` and `id` of a JSON-RPC frame. trxmon has `json_parse`, `json_get`, `json_str` (`trxmon/json.h`), which the monitor library already links. |
| `video_sys_name` | cfg | `theirs` | One log line (`u64cfg.cc:268`). The library prints the label of the `System Mode` setting (`settings.text`) or the number. |
| `app_version_string` | rfile | `theirs` | The library writes its own `LIB_VERSION`. The field `appVersion` of every `.c64re` then names the ring library, not the application image (2.12). Any reader of that field sees a different string. |
| `nf_copy` | rfile | `theirs` | Eight lines (`newfile.cc`) that copy at most `n` characters. With a full `snprintf` (`%.*s`, 2.10) it is not needed. |
| plat glue of the monitor (`plat_*`) | mon | theirs, over `api` | Section 10. |
| engine and screen of trxmon | mon | theirs | Their own `apps/trxmon/src` (D4). |

## 7. Kind `decision`

| Symbol | Users | R | If R3 or R4 is answered | Note |
|---|---|---|---|---|
| `__cxa_atexit`, `__dso_handle` | rfile, rops | R3 | Run: the loader runs `.init_array` and registers `atexit` handlers at `release`. Refuse: `lib.mk` stops a library that has the section. | The only known source is the static `FileManager` (2.9). With 3.4 the symbols are gone. Then "refuse" costs these libraries nothing. Without the `readelf` check of 2.9 a refusal can still surprise. The "run" answer needs a loader that calls the constructors at load and the destructors at `release`; today it calls neither, and `lib.ld` names neither `.init_array` nor `.fini_array`. |
| `trx_io_caps` | ring, rfile, mon | R4 | No: `api->trx->caps`, the same value (`appsvc.cc:241` stores `trx_io_caps()`). Yes: the library reads the byte at the fixed address `IOBASE + 0x1A0004`. | The table is null unless the ID block reads `TRX2`. |
| `trx_io_read32`, `trx_io_write32` | ring, rfile, mon | R4 | No: two new members of `SvcTrx` (`read32`, `write32`), because the 32-bit rule needs the four byte accesses in one critical section (`trx_io.cc:110-129`). Yes: their libraries write the four bytes at the fixed address inside `sync.enter` and `sync.leave`. | The base keeps its own use (`appsvc.cc`, the pixel layer). |
| `trx_io_slim` | ring | R4 | No: a new member `slim`. Yes: the library reads `TX_CTRL` (`+0x60C0`) itself and caches. | `trx_io.cc:95-105`. |
| `trx_io_resid8_filter`, `trx_io_resid8_bypass` | cfg | R4 | No: two members, or none, because they are plain byte writes (`trx_io.cc:72-93`: `ioWrite8` to `TRX_RS8_*`) guarded by `CAPS3` bit 0. Yes: the library does it with `trx->regs` or the fixed address. | With the table `api->trx->regs` (byte-wide) the library can do this without a call. |

Until R4 the `trx` table stays in the base (1.6). For the build in the meantime `SvcTrx` gets `read32`, `write32` and `slim`; `resid8_filter` and `resid8_bypass` need no member (`regs`). If R4 is yes, the three members are removed again with the group, and `sync.enter` and `sync.leave` carry the 32-bit rule in the TRX64-Ultimate library.

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

8.2 The 15 methods of `CoreIo` (`io.h`): `rd`, `wr`, `bus_wr`, `cart_wr`, `ddr_read`, `ddr_write`, `ram_read`, `ram_write`, `ram_memonly`, `rom_write`, `stop`, `mode_reset`, `eeprom_read`, `eeprom_write`, `keys_read`. Two more are in `api` (4.3: `now_ms`, `delay_ms`) and one is new (`delay_us`). Target of the 15: `io.*`. `SvcC64` (`svctab.h:197`) has `peek`, `poke`, `dma_read`, `dma_write` on the same window `C64_MEMORY_BASE` (`appsvc.cc:30-49`). It does not touch `C64_DMA_MEMONLY`. `ram_memonly` is called 12 times in `trx64u_ops.cc` and `u64cfg.cc` to reach RAM under I/O; so `ram_read` and `ram_write` need the `io` group, not `SvcC64`. `SvcC64` is null unless the ID reads `TRX2`.

8.3 The register macros. The code writes registers directly with `ioWrite8` and the macros of `u64.h` (`C64_*`, `U64_*`), `iomap.h` (`IOBASE`) and `trx_io.h` (`TRX_*`). They are constants, not calls. D8 makes the register map TOML that the SDK implements once. Until then a header of constants in the package serves (section 13).

## 9. The headers outside the SDK

BOUNDARY.md lists 132 headers from the `.d` files. Most come through `ctx.h` and `desc.h` and are not used by the moving files. This table groups them by what the code takes from them.

| Group | Headers | Target |
|---|---|---|
| Firmware types: file system, containers, lwIP, FreeRTOS, I2C, flash, config, hand-over | `software/components/*`, `software/filemanager/*`, `software/filesystem/*`, `software/infra/*`, `software/lwip/*`, `software/network/*`, `software/FreeRTOS/*`, `software/io/i2c/*`, `software/io/flash/*`, `software/io/c64/{c64,keyboard,screen,trx_cart}.h`, `software/userinterface/*`, `software/hostapp/{configlist,cor,handover}.h`, `software/api/jsmn.h`, `software/system/{integer,itu,product,profiler,u2p}.h`, `output/gitinfo.h` | Not needed after sections 3 and 4. `jsmn.h` (JSON parser for the `.c64re` header, `ringfile.cc:36`) comes in with the library as source. `keyboard.h`: the key codes of `mon_key` (`monitor.cc`); they go into the SDK as a header of constants (the keys of the UI TOML, D7). |
| Register maps | `software/system/u64.h`, `software/system/iomap.h`, `software/io/c64/trx_io.h` (macros only), `software/u64/color_timings.h` (types), `coreapp/cores/c64_regs.h`, `app/video/videomode.h` (`VID_SYS_*`) | A header of constants in the package (D8). The functions of `trx_io.h` and `color_timings` are in sections 4 and 7. |
| Core application, internal | `ctx.h`, `desc.h`, `io.h`, `board.h`, `settings.h`, `ops_glue.h`, `engines.h`, `writeback.h`, `regset.h`, `boot.h`, `hw.h`, `picture.h`, `ui.h`, `rest.h`, `uic.h`, `svcjson.h`, `httpreq.h`, `partial.h`, `mousesink.h`, `newfile.h`, `corefolder.h`, `tempdisk.h`, and the menu headers (`menuctl.h`, `menuflows.h`, `menuintent.h`, `menutree.h`, `menuvlist.h`, `picklist.h`, `hotkeys.h`, `rawkeys.h`, `romfind.h`, `romset.h`, `sectors.h`, `winslot.h`, `fbwin.h`, `ledload.h`, `mediamounts.h`, `filemgr.h`) | Not in the package. Their reach is sections 4 and 8. `sid_decide.h`, `resid8_filter.h` (a copy of the firmware header) and the inline `desc_has_capability` are used for real; `sid_decide.h` and `resid8_filter.h` move with `u64cfg`. |
| Shared libraries | `c64_cart.h`, `cbm1541.h`, `d1541_drive.h`, `d1541_gcr.h`, `d1541_wd.h`, `sidfile.h`, `uci.h`, `u64_resample.inc` | The header of each library, shipped with it (6.1). |
| Application ABI 2 | `vendor/gideon/software/appsdk/include/u2p_app_api.h`, `.../app_loader/app_api.h` | `app/sdk/u2p_app.h` of the package for the tm_* engine; their own copy for trxmon (the layouts differ, 12.4). |
| Not used | `app/edit/lineedit.h`, `app/loaders/kickstart.h`, `app/video/edid.h` | Transitive. No use found in the seven files. |

## 10. The monitor's platform glue

`monitor.cc` supplies `plat_*` to the trxmon engine. All of it moves with the monitor library and runs over `api`.

| Function | Replaced by |
|---|---|
| `plat_ms` | `api->sys->ms` |
| `plat_yield` | `api->sys->sleep_ms(0)` |
| `plat_sleep` | `api->sys->sleep_ms` (one tick longer, 3.2) |
| `plat_fopen`, `plat_fread`, `plat_fwrite`, `plat_fclose` | `api->fs->load`, `save` (a file is held whole in memory and written at close, up to 64 KB, `monitor.cc:179`) |
| `plat_mkdir`, `plat_rmdir` | `api->fs->mkdir`, `remove` |
| `plat_exists` | `api->fs->stat` |
| `plat_dir` | `api->fs->list` (64 entries) |
| `plat_config` | `api->ops->state("set.<item>")` (2.3), or `settings.cfg_text` |
| `plat_media`, `plat_firmware_*` | constants |
| `api_trx_acquire`, `api_trx_release` | `api->trx->acquire`, `release`. The two are the same code as `appsvc.cc:123-145`. `api_trx_announce` is empty; `api->trx->announce(4312)` also puts the port into the ident reply (`appsvc_rpc_port`). 2.6 for the release at the end of a sub-app. |
| `api_video` | `api->trx->video` (same code, `appsvc.cc:214`) |
| `s_ring` (`u2p_ring_api_t`) | the table of the ring library (12.4) |

## 11. The proposed new API groups

The groups are appended to `LibApi` after `lib`, each with its own `size`, as the existing groups are. A group the core does not have is a null pointer. A call without a context argument acts on the running core (as `api->ops->call` does). The base minor goes from 1 to 2.

### 11.1 `sync` (`SvcSync`)

A mutex and a critical section for libraries that run more than one task. Both are unconditional. The mutex has priority inheritance. It is freed with the library (the account of 2.7 has to hold).

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `mutex_new` | `struct SvcMutex *(*)(void)` | `xSemaphoreCreateMutex` | The ring (one). A library with a job and an operation that share state. |
| `mutex_lock` | `bool (*)(struct SvcMutex *m, uint32_t timeout_ms)` | `xSemaphoreTake` | `0xFFFFFFFF` waits for ever, 0 tries. |
| `mutex_unlock` | `void (*)(struct SvcMutex *m)` | `xSemaphoreGive` | |
| `mutex_free` | `void (*)(struct SvcMutex *m)` | `vQueueDelete` | |
| `enter` | `void (*)(void)` | `portENTER_CRITICAL` | R4: the 32-bit register rule. Short sections only. It nests. |
| `leave` | `void (*)(void)` | `portEXIT_CRITICAL` | |

### 11.2 `settings` (`SvcSettings`)

The settings of the running core, by key. An index from `find` is valid while `generation` is unchanged. The base owns the cache.

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
| `cfg_value` | `int (*)(const char *key, int def)` | `cfg_value` | `menuflows`, `netops`, `c64` today |
| `cfg_text` | `const char *(*)(const char *key)` | `cfg_text` | `netfs`, `netops` today |

### 11.3 `ctx` (`SvcCtx`)

The state and the fields of the running core that a handler reads or writes.

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `state` | `uint32_t (*)(void)` | `c->started`, `paused`, `frozen`, `menu_open` as bits 0-3 | |
| `set_paused` | `void (*)(bool on)` | `c->paused = ...` | |
| `notice` | `bool (*)(const char *text, bool only_if_free)` | `c->notice`, `c->notice_pending` | `c64.cc:297`, any library that tells the person once |
| `media_dir` | `const char *(*)(void)` | `c->media_dir` | |
| `target_path` | `bool (*)(const char *name, const char *ext, char *out, int n)` | `trx_target_path` | `diag.cc:185` |
| `core_folder` | `bool (*)(const char *sub, char *out, int n)` | `corefolder_dir`, `corefolder_sub`; `sub` null, `roms`, `saves`, `configs` | every core with saves or ROM folders |
| `job_step` | `bool (*)(void)` | `c->job_step` | `newdisk.cc` and every operation that writes a file in steps |
| `job_cancelled` | `bool (*)(void)` | `c->job_cancel` | |
| `job_progress` | `void (*)(int pct, const char *path)` | `c->job_pct`, `c->job_path` | |

### 11.4 `io` (`SvcIo`)

The windows of the running core. The calls are those of `CoreIo`. The signatures are the ones of `io.h`. All core libraries of this repository use `CoreIo` today.

| Call | Replaces | Other users in this repository |
|---|---|---|
| `rd`, `wr` | `CoreIo::rd`, `wr` | all cores |
| `bus_wr`, `cart_wr` | `CoreIo::bus_wr`, `cart_wr` | the C64 line |
| `ddr_read`, `ddr_write` | `CoreIo::ddr_*` | `snes.cc`, `c128.cc` |
| `ram_read`, `ram_write`, `ram_memonly` | `CoreIo::ram_*` | all cores |
| `rom_write` | `CoreIo::rom_write` | `c64.cc`, `c128.cc` |
| `stop`, `mode_reset` | `CoreIo::stop`, `mode_reset` | all cores |
| `eeprom_read`, `eeprom_write` | `CoreIo::eeprom_*` | `c64_cart.cc:363` |
| `keys_read` | `CoreIo::keys_read` | `hotkeys.cc:780`, `run.cc:546` |

### 11.5 `board` (`SvcBoard`)

The services of the board and of the video and look of the application. Each call is a base function today. None is a register: the code that needs the register writes it with the header of 8.3.

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `fpga_caps` | `uint32_t (*)(void)` | `getFpgaCapabilities` | `uci` |
| `is_commodore` | `bool (*)(void)` | `isCommodoreBoard` | |
| `esp32_rpc` | `int (*)(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)` | `hw_esp32_rpc` | any core with a power or Wi-Fi item |
| `i2c_write` | `int (*)(int channel, uint8_t dev, uint8_t reg, uint8_t val)` | `i2c->i2c_lock`, `set_channel`, `i2c_write_byte`, `i2c_unlock` | |
| `video_set_system` | `void (*)(int sys)` | `picture_set_system` | `c128`, `c64scpu`, `portdiag` |
| `video_set_pll` | `void (*)(int sys, int ppm)` | `SetVideoPll`, `ResetHdmiPll` | |
| `video_color_timing` | `bool (*)(int sys, SvcColorTiming *out)` | `color_timings[sys]` | `resample` |
| `ui_set_look` | `void (*)(int transparency, int scheme)` | `ui_set_look` | |
| `ui_set_name_squeeze` | `void (*)(int quarter)` | `ui_set_name_squeeze` | |
| `keyboard_joystick` | `void (*)(uint8_t select, uint8_t wasd)` | `keyb_joy_select`, `wasd_to_joy` | |

### 11.6 Members added to `SvcSys` and `SvcNet`

| Call | Signature | Replaces | Other users |
|---|---|---|---|
| `sys.ms_timer` | `uint16_t (*)(void)` | `getMsTimer` | the 1 ms measurements of any library |
| `sys.delay_us` | `void (*)(uint32_t us)` | `CoreIo::delay_us` | `atari_sio.cc`, `apple2_disk.cc`, `apple2_hdd.cc` |
| `sys.wall_time` | `bool (*)(uint8_t t[6])` | `get_fattime`, `rtc_time_valid`, `CoreIo::wall_time` | `amiga.cc`, `neogeo.cc`, `snes.cc`, `zxnext.cc`, `update.cc` |
| `net.gated` | `bool (*)(void)` | `rest_uic != null` | the monitor library, the sub-app trxmon (the same server code) and any socket that serves a method that changes the machine. True while the REST service has its confirmation token (`rest.cc:543`). |

The group `trx` and `ring` stay as they are until R4.

## 12. The other direction: the core application calls the moving code

12.1 Hooks that the library record does not have. `LibRecord` has `ops`, `states`, `events`, `nodes`, `file_actions`, `settings`, `poll`, `tabs`, `exports`. The calls below need five hooks and one widget that it does not have. The hooks are members at the end of `LibRecord`, as `exports` was added (`libapi.h:144`): a library built before them reads them as zero.

| Id | Hook | Called when | Used for |
|---|---|---|---|
| H1 | `start` | once, after the groups of 2.4 are installed and before the polls start | init of ring, monitor, trx64u; the first read of the settings |
| H2 | `reset` | the cold reset of the core (`c64_init`) | `trx64u_sid_cancel`, `u64cfg_power_on_keys` |
| H3 | `rom_put(role, data, n)` | after `c64.put_kernal`, `c64.put_basic` | `trx64u_kernal_put`, `trx64u_basic_put` |
| H4 | `before_run(path, kind)` | before a PRG or CRT runs | `trx64u_load_assoc_cfg` |
| H5 | `unfreeze` | the menu closes and the machine is about to run | `mon_release`, `trx_ring_pending`, `trx_ring_unfreeze` |

12.2 Calls and their replacements:

| From | Calls | Replaced by |
|---|---|---|
| `c64` | `trx64u_register`, `u64cfg_register`, `ringops_register`, `monitor_register` (`c64.cc:704-709`) | `lib_register` of each library (3.1). |
| `c64` | `trx64u_sid_service` (`c64.cc:378`, in `c64.service`, every 20 ms) | `LibRecord.poll` of the trx64u library, `period_ms` 20. Differs: its own task, under the core lock, no longer in sequence with `op_service_job` and the drive's `cbm1541_service`. |
| `c64` | `trx64u_sid_cancel` (`c64.cc:110`), `u64cfg_power_on_keys` (`c64.cc:103`) | hook H2. `power_on_keys` must run before the settings are applied; the hook runs in `c64_init`. |
| `c64` | `trx64u_kernal_put`, `trx64u_basic_put` (`c64.cc:72`, `80`) | hook H3. The fast-reset patch is made against the KERNAL copy the hook receives. |
| `c64` | `trx64u_load_assoc_cfg` (`c64.cc:295`, `600`) | hook H4. |
| `c64` | `cfg_value` (`c64.cc:105`, `270`) | `settings.cfg_value`, moved into the core application. |
| `run` | `cfg_value`, `ring_autostart` (`run.cc:691`) | the ring library reads `Timewarp Ring` in hook H1. `ring_autostart` becomes a variable of the library. |
| `run` | `trx_ring_engine_init` (`run.cc:692`) | hook H1. |
| `run` | `mon_serve` (`run.cc:1078`, every pass of the main loop, menu open or not) | `LibRecord.poll` with `when_frozen` true, a short `period_ms` (the loop pass). The poll as served ignores `when_frozen` (2.5). Another way: a job with a loop and `net.wait`. |
| `run` | `mon_release` (`run.cc:795`) | hook H5. |
| `run` | `trx_ring_pending`, `trx_ring_unfreeze` (`run.cc:799-800`) | hook H5. |
| `screens` | `mon_render`, `mon_rows`, `mon_line`, `mon_attr`, `mon_cursor`, `mon_sig` (`screens.cc:867-885`, `1497-1498`) | a generic text-grid widget of the UI TOML (a tab with `widget = "textgrid"`) and a record `LibGridDecl { render, rows, line, attr, cursor, sig, key }` in `LibRecord`. The grid has 80 columns and `MON_ROWS_MAX` rows, an attribute per row and a cursor; the screen redraws when `sig` changes. Other users: the sub-app trxmon draws the same grid through ABI 2. |
| `menuctl` | `monitor.key`, `monitor.open`, `monitor.tick`, `monitor.wants_stop` | Already operations and a state of the registry (`monitor.cc:318-325`). No change. |
| `diag` | `trx_ring_trace_status`, `trx_ring_trace_read` (`diag.cc:120-126`, `246`) | the table `ring` of `api->lib->find` on the base side (`LibLoader::svc_lib_find`). The diag core shows "no trace" when the library is not loaded. |
| `diag` | `trx_target_path` (`diag.cc:185`) | `ctx.target_path`, moved into the core application. |
| `menuflows`, `netfs`, `netops` | `cfg_value`, `cfg_text` | `settings.cfg_value`, `settings.cfg_text`, moved into the core application. `menuflows.cc:44` reads the key `Leave Menu on Mount` and `netops.cc` reads keys of the trx64u menu (`Use DHCP`, `Debug Stream Mode`, `Network Password`, the host names); a key of one core's menu in generic code is a point for the ledger (D24). |
| `c64_cart` | `trx64u_uci_df1c` (`c64_cart.cc:656`) | the table `uci`, a member `slot_enable` that writes `CMD_IF_SLOT_ENABLE_REG` and `CMD_IF_SLOT_BASE_REG` after the same capability test. The registers are the UCI's own. |
| `usb_hid` | `u64_get_usb_hid_config_value` (weak in `host-app/software/io/usb/usb_hid.cc:418`, defined at `u64cfg.cc:1671`) | a generic function of the core application. It answers `mouse_sink_scaling` first, then reads settings by keys that the core's API TOML names (mouse mode, wheel, sensitivity, acceleration: `u64cfg.cc:1676-1693`). The row `mouse_sink_scaling` has the kind `base`. |
| `appsvc` | the 15 `trx_ring_*` of `g_ring`, `trx_ring_landed`, `trx_ring_frame_cycles`, `trx_ring_fps`, `trx_ring_fps_x100`, `trx_ring_reachable` (`appsvc.cc:150-251`) | R4. Interim: the table `ring` of the library, found with `lib->find` when `api->trx` is installed. `trx->video` is three functions over `C64_VIDEOFORMAT` (`ring.cc` reads the register each call) and can stay in the base. |
| `tests` | `board_stubs.cc` stubs the symbols of the moving code; `trx64u_menu.h` and `render.cc` read the menu record | The tests of the moving code go with it (R9). The stubs go away when the symbols do. |

12.3 The poll of 2.5 and the hooks of 12.1 are the two changes of the base on which the move depends most. R2 is built for the poll; H1 to H5 and the grid widget are not in the spec yet.

12.4 The `SvcTrx` and `SvcRing` rows:

The trx_io_* symbols that `SvcTrx` lacks today:

| Symbol | `SvcTrx` today |
|---|---|
| `trx_io_caps` | has it: the field `caps` |
| `trx_io_read32` | lacks it |
| `trx_io_write32` | lacks it |
| `trx_io_slim` | lacks it |
| `trx_io_resid8_filter` | lacks it |
| `trx_io_resid8_bypass` | lacks it |

`SvcTrx` has `regs` (byte wide), `caps`, `acquire`, `release`, `announce`, `ring`, `video`, `reset_c64`. With `regs` a library can do `resid8_filter` and `resid8_bypass` without a call (7).

The six members of the TRX64-Ultimate `u2p_ring_api_t` (`software/ultimate-app/software/app_loader/app_api.h:458-520`) that our `SvcRing` (`svctab.h:166-181`) lacks:

| Member | Theirs | Ours |
|---|---|---|
| `dump(path, index, *bytes)` | slot after `error` | `reserved_files[0]`, always 0 |
| `save(path, seconds, stride, *bytes, *entries)` | next slot | `reserved_files[1]`, always 0 |
| `load(path)` | next slot | `reserved_files[2]`, always 0 |
| `trace_writes(start, n, u2p_trace_write_t *out)` | after `trace_read` | `reserved_trace_writes`, always 0; the type `u2p_trace_write_t` is not in `svctab.h` (`TrxWriteRec` in `trx_ring.h`) |
| `load_entry(path, entry)` | after `log_read` | not there |
| `save_native(path, *bytes)` | after `load_entry` | not there |

The offsets of the first four are the same in both layouts (pointer-sized slots), so a table that fills the slots and appends the last two is binary compatible with ours. The `ring` library exports its table with its own layout (1.6). The six functions are in `ringfile.cc`: `trx_c64re_dump_ex`, `trx_c64rering_dump_ex`, `trx_c64re_load`, `trx_trxring_load`; `trace_writes` is `trx_ring_trace_writes` (`ring.cc`). The base `SvcRing` keeps the zero slots for a sub-app that reads `api->trx->ring` when no library is loaded.

## 13. What the build of the package needs next

13.1 The SDK runtime source: `snprintf`, `sscanf`, `atoi`, `strtoll`, `strcasecmp`, `strspn`, `strstr`, `isalpha`, `toupper`, `calloc`, `new`, `delete`, `printf`, `puts` (section 5). `libc.c` has `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strchr`, `strrchr`, the 32-bit and 64-bit division and the 64-bit shifts.

13.2 The headers of constants of 8.3 and 9, and the keys header.

13.3 The changes of the base that the rows name: `svc_state` for `set.`; the start order of 2.4; the poll's `when_frozen` and its lock; the owner of sockets and of the debug block (2.6); the account of 2.7; the base operations `machine.put_builtin_rom`; `LibRecord` hooks H1-H5 and the grid; `fs.list` with full names; the new groups of section 11.

13.4 Not measured: `.init_array` in the seven objects (2.9); the size of each library against `U2A3_IMAGE_MAX` (R5); the time of a poll under the core lock.
