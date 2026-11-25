# The API of the core application

## 1. Scope and conventions

### 1.1 What this document covers

1.1.1 This document describes what the core application (the base) offers to a library and to an application.
1.1.2 A library is a `.u2a` file of kind lib. It stays loaded and registers operations, state values and events.
1.1.3 An application is a `.u2a` file of kind app. It has an entry `app_main`, which the base runs on a task of its own.
1.1.4 A library and an application are both called an image.
1.1.5 Every contract in this document is read from the code of the base, not from the comments of the headers.
1.1.6 This document covers the contracts declared in `libapi.h`, `svctab.h`, `ops.h`, `fsif.h` and `libfmt.h`, and in the headers of the groups that base 3.3 added: `svc_sync.h`, `svc_settings.h`, `svc_ctx.h`, `svc_io.h`, `svc_board.h`, `svc_overlay.h` and `svc_fs.h`, and the table of the core hooks in `core_hooks.h` (9.5).
1.1.7 `libapi.h` is the root. `svctab.h`, `ops.h` and `fsif.h` are reached from it. `libfmt.h` is not: an image that needs its constants includes it as well. The other headers of `include/` (`glue.h`, `u2p_app.h`, `u2p_ui.h`, `hidden.h`) belong to the older application interface and the build, and are described in the README of the package.
1.1.8 The calls of the older application interface in `u2p_app.h` are an adapter. `u2p_compat.c` implements each of them with the calls of this document.
1.1.9 The older calls for a character screen and for keys do not exist. An application brings a description of its screens, and the base draws it.

### 1.2 Versions

1.2.1 The API has a major and a minor number. `U2A_ABI_MAJOR` is the major. It is 3.
1.2.2 `U2A_BASE_MINOR` is the minor of this base. It is 5.
1.2.3 `U2A3_VERSION(major, minor)` makes the number `major << 16 | minor`. A `needs` entry, the `version` of a library and the call `api->lib->find` use this form.
1.2.4 An image names the majors it knows (`abi_min` to `abi_max`). The loader refuses an image whose range does not hold 3.
1.2.5 An image states the lowest minor it needs as a `needs` entry named `base`. The loader refuses an image whose `base` entry is above the minor of the base.
1.2.6 `api->abi_major` and `api->abi_minor` report the version of the running base.
1.2.7 Every symbol of this document is part of base 3.0, unless its section says base 3.1, 3.2, 3.3, 3.4, 3.5 or 3.6. Two things came with 3.1: `api->lib` and the members `LibRecord.exports` and `LibRecord.exports_size`. Four things came with 3.2: the base serves the poll of a library that loads after the core has started (6.10), the budget record of a file and its refusals (section 8), the account of an image, which now also holds what the operations of the image take from `api->mem` later (4.2.2, 8.5), and the named exports of a library with the table `C64Api` (section 9). Base 3.3 added the groups `api->sync`, `api->settings`, `api->ctx`, `api->io` and `api->board` (4.12 to 4.16), the members `ms_timer`, `yield`, `delay_us`, `wall_time`, `app_version` and `app_git` of `api->sys` (4.1.8 to 4.1.13), `list_long` of `api->fs` (4.4.24), the states `set.<key>` and `net.gated` (4.13.5), the members `connect` and `resolve` of `api->net` (4.8.13, 4.8.14) and `need` of `api->lib` (4.10.3). Base 3.4 added the members `exit_to_machine` and `goto_screen` of `api->overlay` (4.17.18, 4.17.19) and the group `api->wb`, the write-back service for libraries (4.18). Base 3.5 (SDK 0.5.0, T262) added the member `ask_list` of `api->ctx` (4.14.14), the node kind `LN_LIST` (6.19), the sources of the F3 search (6.20) and, in the menu record, the node keys `pick = "@state"`, `busy`, `more`, `search`, `find` and `fields` (6.7.3), the tab key `show` (6.11.7) and the key row key `legend` (6.18.3). Base 3.6 (SDK 0.5.1, T262 D4) added the members `browse_at` and `browse_sheet` of `api->ctx` (4.14.15, 4.14.16), the value `SVCFS_SIZE_UNKNOWN` of `SvcFsEntryLong.size` (4.4.24.6) and, in the menu record of a library, the key table `[[key]]` (6.18.5). The groups `api->net` and `api->job` are in the table that a library sees when it registers; every allocation, file, socket and job is on the account of the image whose code makes the call (4.2.2.1, 4.6.3, 4.8.2); the jobs of a library do not see the stop of an application (4.6.5, 4.6.13.3); the poll of a library with `when_frozen` runs while the machine is frozen (6.10.9). A library that depends on one of the additions of 3.1 to 3.6 writes `needs base >= 3.1`, `needs base >= 3.2`, `needs base >= 3.3`, `needs base >= 3.4`, `needs base >= 3.5` or `needs base >= 3.6`. A library built against 3.4 or 3.5 loads and runs on 3.6 unchanged: every addition is a new member at the end of a table, a new value of an enum, or a new key of a text.
1.2.8 A section of a group or a type states its version in the line `Since base`.
1.2.9 There is one base minor per release of the SDK, not one per change. A minor that a release of the SDK has shipped never changes again. Until then everything that is added to the API stays under the minor of the coming release: SDK 0.5.0 is base 3.5. Base 3.4 (SDK 0.4.6 and 0.4.7), base 3.3 (SDK 0.4.0 to 0.4.5, in 0.4.5's state), base 3.2 (SDK 0.2.0) and base 3.1 are released.

| Symbol | Contract |
|---|---|
| `U2A_ABI_MAJOR` | The major number of the API, 3. A base of another major refuses the image. |
| `U2A_BASE_MINOR` | The minor number of the API this base offers, 5. |
| `U2A3_VERSION` | `U2A3_VERSION(major, minor)` is `major << 16 \| minor`. |
| `U2A3_NAME_MAX` | The size of a name field, 16 bytes. A name has 1 to 15 characters of `a-z`, `0-9` and `_`, and a NUL. |
| `U2A3_NEEDS_MAX` | The number of `needs` entries of an image, at most 4. |
| `U2A3_IMAGE_MAX` | The size of the image (payload and zeroed memory), at most 1 MiB. |
| `U2A3_ALIGN_MAX` | The largest alignment an image may ask for, 4096 bytes. |
| `U2A3_FLAG_BUDGET` | The header flag 2 of a file: the budget record stands directly behind the header (section 8). A base older than 3.2 refuses a file that sets it. |
| `U2A3_FLAG_EXPORTS` | The header flag 4 of a file: the list of the names the library exports stands behind the budget record, or directly behind the header when there is none (9.3). A base older than 3.2 refuses a file that sets it. |
| `U2A3_EXPORTS_MAX` | The number of names a library exports, at most 8 (also `LIB_EXPORTS_MAX` for the record). |
| `U2A3_BUDGET_NONE` | The value 0xFFFFFFFF of a figure in the budget record: the image declares no figure of its own (8.2). |
| `PRG_APP_NEED` | The name `standalone`. A `needs` entry with this name names no library. The base does not offer the application while the board runs under the MultiCore host. |
| `CART_APP_NEED` | The name `cart_port`. A `needs` entry with this name names no library. The base offers the application only where `api->c64` is not null. |

### 1.3 Tasks

1.3.1 The contracts name the task a call may be made from. The tasks are these.
1.3.2 The menu task is the base's own loop. It draws the menu and the dialogs, takes the keys, loads images when an application opens, and builds the screens of an application.
1.3.3 The executor task runs the operations that the menu, the REST door and the WebSocket door call. It holds the core lock while a handler runs.
1.3.4 The application task is the task `App`. It runs `app_main`. Its stack is 8192 words, 32 KiB.
1.3.5 A job task is a task `App job`, made by `api->job->start`.
1.3.6 A poll task is a task of its own for each poll that is served: the polls the record of the core and of the libraries loaded at start declare, and the polls of the libraries and kept applications that load later. It holds the core lock while the operation runs.
1.3.7 The start-up task runs before the menu loop. It loads the libraries of the `.cor` file and of the libraries folders.
1.3.8 The core lock serialises the registers, windows and state of the core.
1.3.9 A call that blocks the caller (`api->dlg`, `api->job->join`, `api->net->send`) is never made from the menu task or the executor task.

### 1.4 Return values and memory

1.4.1 A function that returns a pointer returns null when it fails, unless its section says otherwise.
1.4.2 A function that returns `bool` returns `false` when it fails.
1.4.3 The operation calls return a code of `OpErr`. The net calls return their own negative codes, named in their sections.
1.4.4 A string an image hands to the base (a name, a label, an argument list, a call table) is kept by pointer. It has to stay valid as long as the image is loaded, so it belongs in the image or in memory from `api->mem`. A string on the stack does not.
1.4.5 The base frees what an application still holds from `api->mem` and `api->fs` when it ends, and closes the sockets it holds. The base takes back the registrations of an application before it frees the image.

## 2. Entry points

### 2.1 `lib_register` (`lib_register_fn`)

Since base 3.0.

2.1.1 The entry of a library is `extern "C" int lib_register(const LibApi *api, LibRecord *out)`. The type is `lib_register_fn`.
2.1.2 The packer finds the entry by the symbol name `lib_register`. The symbol has to be visible. The example marks it with `visibility("default")`.
2.1.3 The base calls `lib_register` once, when it loads the image, on the task that loads it.
2.1.4 The start-up task loads the libraries of the core at start. The menu task loads a library an application needs, with the core lock held.
2.1.5 The base clears `*out` and sets `out->struct_size` before the call. A library leaves `struct_size` as it is.
2.1.6 The function fills `*out` and returns 0. Any other value refuses the load. The base then takes back everything the image registered and frees the image.
2.1.7 The base refuses the load too when `out->struct_size` changed, when a count is negative or has no list, when `exports` is set and `exports_size` is below 4, when the list `export_list` is malformed or lacks a name the file lists (9.2), and when the registry refuses a name.
2.1.8 The base copies `*out`. The lists it points to are not copied. They stay in the image.
2.1.9 The base registers `out->ops`, `out->states` and `out->events` under the name of the image. The menu merge reads `nodes`, `file_actions`, `settings`, `tabs` and `poll` (section 6).
2.1.10 Memory a library takes from `api->mem` inside `lib_register` belongs to the library. It is freed when the library is unloaded or when the load fails.
2.1.11 Not every group of `api` is there during `lib_register` of a library loaded at core start (section 3.2).
2.1.12 The loader runs no constructor and no destructor. It does not walk `.init_array` or `.fini_array`, and nothing calls a function registered with `atexit` or `__cxa_atexit` when the library is unloaded.
2.1.13 The packer refuses an image that would hold one: a non-empty `.init_array`, `.fini_array`, `.ctors` or `.dtors`, or an undefined reference to `__cxa_atexit` or `atexit`. The message names the section or the symbol and, where the relocations tell it, the function and the object it comes from. `lib.mk` and `app.mk` run the check on the objects in front of `ld` (`u2a_pack.py --check-objects`), because `ld` would stop on the undefined `__cxa_atexit` of a destructor before the packer saw the image; so `make` shows the packer's message, not `ld`'s.
2.1.14 A global object with a constructor or a destructor is therefore not allowed. Hold the state in a plain structure, set it up in `lib_register` and release it where the library ends its work.

### 2.2 `app_main` (`app_main_fn`)

Since base 3.0.

2.2.1 The entry of an application is `extern "C" int app_main(const LibApi *api, AppCtx *ctx)`. The type is `app_main_fn`.
2.2.2 The base calls `app_main` on the application task.
2.2.3 The application task starts when the person opens the application, after the base has loaded the image and, when the menu record has screens, has built them.
2.2.4 An application that has a menu record with screens may also have `lib_register`. The base calls it first, when it loads the image. The registrations go when the application is freed.
2.2.5 The return value is the result. 0 is success. Any other value marks the reply line as an error.
2.2.6 When `app_main` returns and no screen of the application is open, a transient application is freed with everything it still holds from `api->mem`.
2.2.7 When a screen is open, a transient application stays loaded until the person closes the screens. The base then asks the application's jobs to stop, answers an open question with "cancelled", waits for them and frees the image.
2.2.8 A resident application stays loaded when its screens close. Its task goes on. While the task runs, opening the application again shows the screens and does not call `app_main` again.
2.2.9 The base runs one application at a time. A request for another application is refused with `OPE_BUSY` and a sentence that names the open application and asks to close it first, while the first has screens open, or still runs and is not resident.
2.2.10 When the first application is resident and has no screens open, the base asks it to end and waits up to 4 seconds.
2.2.11 An application that has to stop when the person closes its screens polls `api->job->stop_requested`.

### 2.3 `AppCtx`

Since base 3.0.

`AppCtx` is what the base hands `app_main`. It is valid for the call.

| Member | Contract |
|---|---|
| `AppCtx.struct_size` | `sizeof(AppCtx)` of the base. |
| `AppCtx.action` | The action of the menu entry that opened the application, up to 31 characters. The empty string when there is none. The text can change when the person opens a resident application again, so copy it if it is needed later. |
| `AppCtx.file` | The path of the file the entry was on, up to 519 characters. The empty string when there is none. The same rule as for `action` holds. |
| `AppCtx.reply` | A buffer for one line, `reply_len` bytes. The base sets its first byte to 0 before the call. The line is the application's result: the status line of the screens, or the answer of a transport. |
| `AppCtx.reply_len` | The size of `reply` in bytes. It is 240 when the base opens the application. The base sets the last byte of the buffer to 0 after `app_main` returns. |

## 3. The root: `LibApi`

### 3.1 The members

Since base 3.0.

`LibApi` is the root a library or an application gets. The same object is handed to every image for the whole run, so an image may keep the pointer.

| Member | Contract |
|---|---|
| `api->struct_size` | `sizeof(LibApi)` of the base. A base older than 3.1 is smaller by the member `lib`, and a base older than 3.3 by the six groups after it (a development base of 3.3 before the overlay service by the last one: test `LIB_API_COVERS(api, overlay)`). A call that a group got later is tested with `SVC_HAS(table, call)`: the table is there, its `size` covers the call and the call is set, e.g. `SVC_HAS(api->overlay, goto_screen)` (`libapi.h`). |
| `api->abi_major` | `U2A_ABI_MAJOR` of the base. |
| `api->abi_minor` | `U2A_BASE_MINOR` of the base. |

#### 3.1.1 `api->log`

Since base 3.0.

3.1.1.1 `void log(const char *text)` writes the text and a line end to the console of the board with the prefix `[lib] `.
3.1.1.2 It may be called from any task.
3.1.1.3 `api->sys->log` is the same function.

### 3.2 The groups

3.2.1 Each of the other members of `LibApi` points to a call table. The sections of chapter 4 describe them.
3.2.2 Every table starts with a member `size`, the size of the table in bytes. A member is there when `size` covers it and its pointer is not null (section 4.11).
3.2.3 A group the core or the base does not have is a null pointer. Test it before the call.
3.2.4 The base fills the groups in two steps. When `lib_register` runs for a library loaded at core start, the following holds.
3.2.5 `api->ops` answers `OPE_UNSUPPORTED` to `call` and null to `state`. `api->fs` fails every call as if the core had no file system.
3.2.6 `api->dlg`, `api->trx` and `api->c64` are null. `api->sys->flash_read` and `api->sys->machine_handover` are null. `api->job` and `api->net` are there from base 3.3, but they have no machine behind them yet: `api->net->listen` returns -7 and `api->job->start` returns null until the base has given its own tables (3.2.7).
3.2.7 The base fills these members later in the same object. An image that keeps `api` reads the members at the time of the call.
3.2.8 A library or an application loaded after the start sees every group the core has.
3.2.9 `api->trx`, the ring table `api->trx->ring` and `api->c64` leave the base API. They come from libraries, through `api->lib->find` (4.10, section 9): the names `trx`, `ring` and `c64`, each at major 1. New code asks `api->lib->find`. The three pointers stay in `LibApi` and keep their meaning until the cut of `LibApi` that comes with the next change of its major; the base fills them as before.
3.2.10 The base itself answers `find("c64", ...)` with a `C64Api` table (9.4). It does not answer `trx` or `ring`: their layouts belong to the libraries of the core that has them.
3.2.11 The core is handed to the base later too. Until then `api->io` is null, and the calls of `api->settings` and `api->ctx` answer as for a bad index (4.13.1, 4.14). `api->sync` and `api->board` work, except `video_set_system` and `case_led_select`, which do nothing. `api->sys->delay_us` and `wall_time` work (4.1.10.3).
3.2.12 A loader that has no core does not fill the five groups of base 3.3: `api->sync`, `api->settings`, `api->ctx`, `api->io` and `api->board` are null, and it offers base 3.2. A file that needs `base>=3.3` is refused there (`LIB_NEEDS`). The zip reader of the host application is such a loader. A library of base 3.3 therefore always finds `api->sync` and `api->board` when the loader accepted it, and tests `api->io` only.

| Member | Group | Present |
|---|---|---|
| `api->sys` | `SvcSys` | Always. |
| `api->mem` | `SvcMem` | Always. |
| `api->ops` | `SvcOps` | Always. |
| `api->fs` | `SvcFs` | Always. |
| `api->dlg` | `SvcDlg` | Null when the base has no screen. |
| `api->job` | `SvcJob` | Always, after the start; a library loaded at core start sees the group at base 3.3 and later (3.2.6). |
| `api->trx` | `SvcTrx` | Null unless the ID block of the core reads `TRX2`. |
| `api->net` | `SvcNet` | Always, after the start; a library loaded at core start sees the group at base 3.3 and later (3.2.6). |
| `api->c64` | `SvcC64` | Null unless the descriptor of the core has the id `TRX2`. |
| `api->lib` | `SvcLib` | Always. |
| `api->sync` | `SvcSync` | Always. Since base 3.3. |
| `api->settings` | `SvcSettings` | Always. Since base 3.3. |
| `api->ctx` | `SvcCtx` | Always. Since base 3.3. |
| `api->io` | `SvcIo` | Null on a core without a register window. Since base 3.3. |
| `api->board` | `SvcBoard` | Always. Since base 3.3. |
| `api->overlay` | `SvcOverlay` | After the start (the loader that has a core), else null. Since base 3.3. |
| `api->wb` | `SvcWb` | After the start (the loader that has a core), else null. Since base 3.4. |

## 4. The call tables

### 4.1 `api->sys` (`SvcSys`)

Since base 3.0.

The system group. It is there for every image.

#### 4.1.1 `api->sys->log`

4.1.1.1 The same call as `api->log`.

#### 4.1.2 `api->sys->ms`

4.1.2.1 `uint32_t ms(void)` returns the milliseconds since the start of the base.
4.1.2.2 The clock is the tick of the RTOS. A tick is 5 ms, so the value moves in steps of 5.
4.1.2.3 The value wraps after 2^32 ms. Compare with a subtraction.
4.1.2.4 Any task may call it.

#### 4.1.3 `api->sys->sleep_ms`

4.1.3.1 `void sleep_ms(uint32_t ms)` suspends the calling task for at least `ms` milliseconds.
4.1.3.2 A value of 5 or more sleeps for the whole ticks of `ms` plus one tick. A value below 5, and 0, sleeps for one tick.
4.1.3.3 The call does not return early.

#### 4.1.4 `api->sys->core_name`

4.1.4.1 `const char *core_name(void)` returns the name of the running core, the name in the manifest of its `.cor` file.
4.1.4.2 The string belongs to the base. It stays valid for the run.

#### 4.1.5 `api->sys->has_capability`

4.1.5.1 `bool has_capability(const char *capability)` returns `true` when the descriptor of the core lists the capability.
4.1.5.2 The comparison is exact and case sensitive.
4.1.5.3 A null pointer gives `false`.

#### 4.1.6 `api->sys->flash_read`

4.1.6.1 `int flash_read(uint32_t addr, void *buf, int len)` reads `len` bytes of the flash the firmware boots from, starting at `addr`, into `buf`.
4.1.6.2 It returns `len` on success.
4.1.6.3 It returns -1 when `buf` is null, `len` is negative, or the range is not inside the flash.
4.1.6.4 It returns -6 when the board has no flash driver.
4.1.6.5 The member is null while a library loaded at core start registers (section 3.2).

#### 4.1.7 `api->sys->machine_handover`

4.1.7.1 `void machine_handover(void)` prepares the machine to jump into other firmware. It does not return control to the base.
4.1.7.2 The call logs a line, gives the pixel layer back, stops the link to the ESP32 that sends the rocker and button events, disables the interrupts and stops the network receive.
4.1.7.3 There is no way back. The caller jumps to the other code next.
4.1.7.4 The member is null while a library loaded at core start registers (section 3.2).

#### 4.1.8 `api->sys->ms_timer`

Since base 3.3.

4.1.8.1 `uint16_t ms_timer(void)` returns the 1 ms hardware counter of the ITU. It is free running and 16 bit wide, so it wraps every 65.536 s.
4.1.8.2 Use differences: take a span as `(uint16_t)(now - then)`. It is valid for spans below 65 s. Never compare two readings with `<` or `>`.
4.1.8.3 It is no replacement for `ms`, which counts in steps of 5 ms and is 32 bit wide.
4.1.8.4 Any task may call it.

#### 4.1.9 `api->sys->yield`

Since base 3.3.

4.1.9.1 `void yield(void)` gives the processor to a ready task of the same priority. It returns at once when none is ready.
4.1.9.2 `sleep_ms(0)` is no yield: it sleeps one tick of 5 ms.
4.1.9.3 Any task may call it, but not between `api->sync->enter` and `leave`.

#### 4.1.10 `api->sys->delay_us`

Since base 3.3.

4.1.10.1 `void delay_us(uint32_t us)` waits `us` microseconds by looping. The unit is 10 us, rounded down. Below 10 it returns at once.
4.1.10.2 The task does not sleep, and no other task of its priority runs meanwhile. The call is for a few microseconds up to a few milliseconds (the handshakes of the SID chips, SIO timing). A longer wait is `sleep_ms`.
4.1.10.3 It waits in `lib_register` of a library loaded at core start too: the base has the machine's `CoreIo` for this call from the start. It does nothing in a loader that has no machine, such as the zip reader of the host application (3.2.12). Any task may call it.

#### 4.1.11 `api->sys->wall_time`

Since base 3.3.

4.1.11.1 `bool wall_time(uint8_t t[6])` reads the wall clock of the board: `t[0]` is the year of 2000 plus `t[0]` (0 to 99), then month 1 to 12, day 1 to 31, hour, minute and second.
4.1.11.2 It returns `false` when the board has no clock, or when it was never set (the year reads before 2020). `t` is then not written.
4.1.11.3 It works in `lib_register` of a library loaded at core start, like `delay_us` (4.1.10.3). Any task may call it.

#### 4.1.12 `api->sys->app_version`

Since base 3.3.

4.1.12.1 `const char *app_version(void)` returns the version text of the application image that runs: what the version file of the application and the commit make.
4.1.12.2 The string ends with a NUL, belongs to the base, is valid for the life of the application and is never null. A library writes it into what it saves.

#### 4.1.13 `api->sys->app_git`

Since base 3.3.

4.1.13.1 `const char *app_git(void)` returns the short git hash of the commit of the application image. The same rules as for `app_version` hold.

### 4.2 `api->mem` (`SvcMem`)

Since base 3.0.

The memory group. It is there for every image.

4.2.1 The memory comes from the heap of the core.
4.2.2 The base records every block with its owner. The owner is the image whose code takes the block: the image whose `lib_register` is running, the image whose operation or state getter the registry is running (whoever called it), or the application that is open at the time of the call. With none of these, the block has no owner.
4.2.2.1 Since base 3.3 the code that makes the call comes first: when the return address of the call into `api->mem` lies in the image of a loaded library or application, that image is the owner, whoever called the function of the image. An export table found with `api->lib->find` is called through the function pointers of the library, so what the exported function allocates is the account of the library, not of its caller.
4.2.2.2 A tail call replaces the return address by the one of the caller. Both build paths of the SDK (`mk/lib.mk` and `mk/app.mk`, which also builds a library with `APP_KIND = lib`) turn tail calls off (`-fno-optimize-sibling-calls`). An image that is built otherwise may find what it takes on the account of its caller.
4.2.3 A block that has no owner is never freed by the base.
4.2.4 The base frees the blocks of a transient application when it is freed, and the blocks of a library when its load fails or it is unloaded. A resident application keeps its blocks. An image that declared a `heap_peak` figure holds at most that many bytes at once (8.1, 8.4).
4.2.5 Free a block only with `api->mem->free`. A block from another allocator is not accepted.
4.2.6 Every block has a header of 16 bytes. On the board the block is 16-byte aligned.
4.2.7 Any task may call the group.

#### 4.2.8 `api->mem->malloc`

4.2.8.1 `void *malloc(uint32_t n)` returns a block of `n` bytes, or null when the heap has no room, when `n` is above 0x7FFFFFE0, or when the blocks of the image with this one would pass the `heap_peak` figure of its budget (8.4.4).
4.2.8.2 The contents are not cleared.
4.2.8.3 A request for 0 bytes returns a block.

#### 4.2.9 `api->mem->free`

4.2.9.1 `void free(void *p)` gives a block back. A null pointer does nothing.
4.2.9.2 The block of another owner is freed the same way.

#### 4.2.10 `api->mem->realloc`

4.2.10.1 `void *realloc(void *p, uint32_t n)` makes a new block of `n` bytes, copies the smaller of the old size and `n`, and frees the old block.
4.2.10.2 A null `p` is `malloc(n)`.
4.2.10.3 An `n` of 0 frees `p` and returns null.
4.2.10.4 When no new block can be made, it returns null and `p` stays valid.
4.2.10.5 The new block belongs to the owner of the moment, not to the owner of `p`.

### 4.3 `api->ops` (`SvcOps`)

Since base 3.0.

The operation group. It calls an operation of the registry by name, and it reads a state value by name. The types are in chapter 5.

#### 4.3.1 `api->ops->call`

4.3.1.1 `int call(const char *name, const OpIn *in, int nin, OpResult *r)` calls the operation `name` with `nin` arguments from `in`.
4.3.1.2 `r` must not be null. The base fills `r->code`, `r->msg`, `r->more` and `r->ndata`, and `r->data` when the operation returns bytes.
4.3.1.3 The result is the code of `OpErr`, the same as `r->code`.
4.3.1.4 `OPE_NOT_FOUND` says that no operation of that name is registered. `OPE_ARG` says that an argument is missing, given twice, unknown, or outside its type. The reason is in `r->msg`, and the handler did not run.
4.3.1.5 `OPE_UNSUPPORTED` says that the base has no operation registry yet (section 3.2).
4.3.1.6 The handler runs on the calling task. The call takes neither the core lock nor a place in the executor's queue.
4.3.1.7 The call does not protect an operation whose handler relies on the core lock.
4.3.1.8 The strings of `in` have to stay valid for the call. The base does not keep them.

#### 4.3.2 `api->ops->state`

4.3.2.1 `const char *state(const char *name, uint32_t *rev)` returns the current text of the state value `name`.
4.3.2.2 It returns null when the state is unknown, has no value, or the registry is not there yet.
4.3.2.3 The getter of the state runs on the calling task and returns a pointer it owns. Copy the text before the next call.
4.3.2.4 `rev` may be null. When it is not, it receives the revision of the state. The revision counts the changes the registry has seen. It changes only when some caller reads the state and finds another text than the last reader did.

#### 4.3.3 The operations `menu.screen` and `app.open`

Since base 3.0 (T256 D13 documents them).

4.3.3.1 `menu.screen` (arguments `screen`, `unless`, both strings) asks the menu to show a screen. `screen` is a place of the menu, `"F1/timewarp"`: the area key, a slash and the id of a tab of the core's UI TOML; `"F3"` alone is the first tab of the area (the Library, when that tab is the browser). It is the operation behind the rows and hotkeys of the core that open a screen.
4.3.3.2 The operation does not move the menu. It stores the request (`CoreCtx::want_screen`) and returns 0; the menu takes it when the row or the task of the menu that ran the operation returns, or when the menu opens next (a hotkey pressed while the machine runs opens the menu on that screen). A call from another place (a library's task, REST) leaves the request standing until one of these two happens: it does not move an open menu by itself. One request waits: the last call counts. A place the core does not have leaves the menu where it is, without an answer to the call.
4.3.3.3 `unless` names a state value (empty: none). While that state has a text, the call fails with `OPE_FAILED` and that text as its message, and the request is not made.
4.3.3.4 A call made while the menu idles, open or closed, moves nothing until one of those two happens. A caller that has to open the menu and go there at once uses `api->overlay->goto_screen` (4.17.19).
4.3.3.5 `app.open` (arguments `app`, `action`, `file`, all strings; `action` and `file` may be empty) opens an application by name, as a menu row of an application does. The host takes the request at its next pass of the menu loop and loads the image there; the call does not wait for the application and returns 0 with the message `Opening <app> ...`.
4.3.3.6 Only while the menu is open does the host take the request up (`AppHost::service`): a call made while the menu is closed waits until the menu opens. One request waits; a second call before the first is taken returns `OPE_BUSY`. One application is open at a time: while the screens or the task of another transient application are up the call returns `OPE_BUSY` with `Close <name> first.`; a resident application that is open is asked to end when another one is opened.
4.3.3.7 `OPE_NOT_FOUND` says that the apps folder has no such application, `OPE_FAILED` that the core does not offer it, `OPE_UNSUPPORTED` that the build has no applications. The target `app:name[:action]` of `api->overlay->goto_screen` (4.17.19) makes the same request and opens the menu first.

### 4.4 `api->fs` (`SvcFs`)

Since base 3.0.

The file group. It is the file system of the core, `CoreFs`.

4.4.1 A path starts with `/`. The first element names a medium. The listing of `/` names the media that are mounted. The media of the board are `/flash`, `/sd` and `/usb0` to `/usb3`. On the board the listing of `/` can also hold the folders `FTP` (when an FTP host is configured and the app is linked with the FTP library) and `Online` (when an online search has results). A path under them reaches the network, not a medium of the board: do not rely on them in an image that has to run without a network.
4.4.2 A name on a FAT medium is matched without regard to case.
4.4.3 Under `/flash`, a write, a delete, a mkdir, a rename and a read-write open fail. A read-write open gives a read-only file. Three files are the exception: the standalone core's settings file `/flash/<core id>/<folder>.cfg`, its boot counter `/flash/<core id>/boot.cfg` and the host's `/Flash/boot.cfg`. The core id is the `core_id` of the core's description record (the `.cor` id of `core.toml`), the folder its `folder`; both are read at start, and a core with no valid id (A-Z a-z 0-9 `_` `-`, at most 31 characters) keeps its settings in memory and has no flash exception. For trx64u these are `/flash/trx64u/c64.cfg` and `/flash/trx64u/boot.cfg`.
4.4.4 On the monolith board profile the flash drive is writable.
4.4.5 A file an image leaves open is closed when its application ends.
4.4.6 Any task of the image may call the group.
4.4.7 The group fails every call while `lib_register` of a library loaded at core start runs (section 3.2).

#### 4.4.8 `SvcFile`

Since base 3.0.

`SvcFile` is the opaque handle of an open file. It belongs to the application that opened it. `open` makes it and `close` frees it.

#### 4.4.9 `SvcFsEntry`

Since base 3.0.

`SvcFsEntry` is one entry of a listing. Its size is 72 bytes.

| Member | Contract |
|---|---|
| `SvcFsEntry.name` | The name, at most 55 characters and a NUL. A longer name is cut. |
| `SvcFsEntry.fdate` | The FAT packed date of the last change. 0 when unknown. |
| `SvcFsEntry.ftime` | The FAT packed time of the last change. 0 when unknown. |
| `SvcFsEntry.attr` | Bit 0 hidden, bit 1 system, bit 2 read only. |
| `SvcFsEntry.dir` | `true` for a folder. |
| `SvcFsEntry.size` | The size in bytes. |

#### 4.4.10 `api->fs->open`

4.4.10.1 `SvcFile *open(const char *path, bool rw)` opens a file.
4.4.10.2 It returns null when the file is not there, the medium cannot be read, or `path` is null.
4.4.10.3 With `rw` the base tries to open for reading and writing. When that is refused (a read-only file, a read-only medium, `/flash`), it opens for reading. Ask `writable`.

#### 4.4.11 `api->fs->read`

4.4.11.1 `bool read(SvcFile *f, uint64_t off, void *buf, uint32_t len)` reads exactly `len` bytes at the offset `off`.
4.4.11.2 It returns `false` when the range is not inside the file. It does not read part of it.
4.4.11.3 `f` null gives `false`.

#### 4.4.12 `api->fs->write`

4.4.12.1 `bool write(SvcFile *f, uint64_t off, const void *buf, uint32_t len)` writes exactly `len` bytes at `off`.
4.4.12.2 It returns `false` when the file is not writable or a part could not be written. The file may then hold part of the data.
4.4.12.3 To append, use `fsize` as the offset.
4.4.12.4 A write at an offset past the end of the file grows the file to `off + len`. The bytes between the old end and `off` are undefined until they are written: the FAT medium leaves what its new clusters held, the network medium writes zeros.

#### 4.4.13 `api->fs->fsize`

4.4.13.1 `uint64_t fsize(SvcFile *f)` returns the size of the file in bytes. A null `f` gives 0.

#### 4.4.14 `api->fs->writable`

4.4.14.1 `bool writable(SvcFile *f)` returns whether the open file accepts writes.

#### 4.4.15 `api->fs->sync`

4.4.15.1 `bool sync(SvcFile *f)` writes what is cached for the file to the medium. It returns `false` on failure.

#### 4.4.16 `api->fs->close`

4.4.16.1 `void close(SvcFile *f)` closes the file and frees the handle. A null `f` does nothing.
4.4.16.2 The handle is not valid after the call.

#### 4.4.17 `api->fs->list`

4.4.17.1 `int list(const char *dir, SvcFsEntry *out, int max)` lists a directory into `out`.
4.4.17.2 It returns the number of entries written, folders first, at most `max`.
4.4.17.3 It returns -1 when `dir` cannot be read as a directory or is null.
4.4.17.4 With `max` of 0 or less it writes nothing and returns 0 for a directory that can be read. The call does not count the entries.
4.4.17.5 The call takes a temporary buffer of `max` long entries from the heap of the base and gives it back. A large `max` can fail with -1.

#### 4.4.18 `api->fs->stat`

4.4.18.1 `bool stat(const char *path, uint64_t *size, bool *dir)` asks for the size and the kind of a path.
4.4.18.2 It returns `false` when the path is not there. `size` and `dir` may be null.

#### 4.4.19 `api->fs->load`

4.4.19.1 `int load(const char *path, void *buf, uint32_t max)` reads a whole file into `buf`.
4.4.19.2 It returns the length. It returns -1 when the file cannot be read or is larger than `max`.

#### 4.4.20 `api->fs->save`

4.4.20.1 `bool save(const char *path, const void *buf, uint32_t len)` replaces the file with `len` bytes.
4.4.20.2 It returns `false` when the path is under `/flash` (see above), when the path has no folder part or the folder is the root, when the medium is read only or full, or when not all bytes were written.
4.4.20.3 The folder has to exist.

#### 4.4.21 `api->fs->remove`

4.4.21.1 `bool remove(const char *path)` deletes the file. It returns `false` when the path is under `/flash` (see above) or the file manager refuses.

#### 4.4.22 `api->fs->mkdir`

4.4.22.1 `bool mkdir(const char *path)` makes a folder.
4.4.22.2 A folder that exists is success. A file of that name is `false`.
4.4.22.3 A path under `/flash` is refused.

#### 4.4.23 `api->fs->rename`

4.4.23.1 `bool rename(const char *from, const char *to)` moves a file or a folder.
4.4.23.2 It returns `false` when either path is under `/flash`, or the file manager refuses.

#### 4.4.24 `api->fs->list_long`

Since base 3.3.

4.4.24.1 `int list_long(const char *dir, SvcFsEntryLong *out, int max)` lists `dir` into `out[0 .. max - 1]` with whole names. `list` cuts a name to 55 characters.
4.4.24.2 It returns the number of entries written, never more than `max`. It returns -1 when `dir` is not a folder, the medium cannot be read, or `fs` fails (4.4.7).
4.4.24.3 A `max` of 0 or less writes nothing and returns the number of entries the folder has. The base counts them in a block of the heap of 72 bytes an entry and returns -1 when the heap has no room for it.
4.4.24.4 `out` is the caller's, on the heap of the image or on its stack. An entry has 264 bytes, so a long listing is taken in a block of the heap and freed afterwards.
4.4.24.5 The folders come first, as in `list`. A task that may block calls it: the medium is read.

#### 4.4.25 `SvcFsEntryLong`

Since base 3.3.

`SvcFsEntryLong` is one entry of the listing of `list_long`, with the layout of `FsEntry` of the base. Its size is 272 bytes.

| Member | Contract |
|---|---|
| `SvcFsEntryLong.name` | The whole name, at most 255 characters and a NUL. |
| `SvcFsEntryLong.fdate` | The FAT packed date of the last change. 0 when unknown. |
| `SvcFsEntryLong.ftime` | The FAT packed time of the last change. 0 when unknown. |
| `SvcFsEntryLong.attr` | Bit 0 hidden, bit 1 system, bit 2 read only. |
| `SvcFsEntryLong.dir` | `true` for a folder. |
| `SvcFsEntryLong.size` | The size in bytes. |

### 4.5 `api->dlg` (`SvcDlg`)

Since base 3.0.

The dialog group. The base asks the person, in its own widgets and skin.

4.5.1 The group is null when the base has no screen.
4.5.2 A dialog is modal. It is answered with the cursor keys, RETURN, and the left arrow of the C64.
4.5.3 RUN/STOP cancels. ESC and the left arrow cancel too. In a file picker they go one folder up first, until the root is reached.
4.5.4 Every call except the three progress calls blocks the calling task until the person has answered.
4.5.5 Call the group from the application task or from a job. Never call it from the menu task: the menu task serves the dialog.
4.5.6 One question is open at a time. A second caller waits for the first.
4.5.7 When the application ends or its screens close, an open question is answered "cancelled", and the progress bar goes.
4.5.8 The strings the call receives (title, text, items, keys) have to stay valid until the call returns.
4.5.9 The cancelled answer is 0 for `buttons`, false for `confirm`, `text`, `pick_file` and `pick_folder`, and -1 for `choose` and `choose_keys`.
4.5.10 The constants of the buttons are these.

| Symbol | Contract |
|---|---|
| `SVC_BUTTON_OK` | The OK button, value 1. |
| `SVC_BUTTON_YES` | The Yes button, value 2. |
| `SVC_BUTTON_NO` | The No button, value 4. |

#### 4.5.11 `api->dlg->message`

4.5.11.1 `void message(const char *title, const char *text)` shows a message with an OK button and returns when the person has pressed RETURN, SPACE or a cancel key.
4.5.11.2 The title is shown up to 63 characters. The text is shown up to 519 characters.

#### 4.5.12 `api->dlg->buttons`

4.5.12.1 `int buttons(const char *title, const char *text, int buttons)` shows the buttons `buttons`, a combination of `SVC_BUTTON_OK`, `SVC_BUTTON_YES` and `SVC_BUTTON_NO`. A value of 0 shows OK.
4.5.12.2 It returns the value of the button the person pressed, or 0 when the question was cancelled.
4.5.12.3 With Yes and No, the keys `Y` and `N` press them, the cursor keys move the focus, and RETURN presses the focused button. Yes has the focus first.
4.5.12.4 With one button, RETURN presses it. A cancel key presses OK when OK is offered, and answers 0 otherwise.

#### 4.5.13 `api->dlg->confirm`

4.5.13.1 `bool confirm(const char *title, const char *text)` asks with Yes and No. It returns `true` only for Yes.

#### 4.5.14 `api->dlg->choose`

4.5.14.1 `int choose(const char *title, const char *const *items, int n, int sel)` shows a list of `n` items with the item `sel` selected, 14 rows at a time.
4.5.14.2 It returns the index of the item chosen with RETURN, or -1 when cancelled.
4.5.14.3 It returns -1 at once, without a dialog, when `n` is 0 or less or `items` is null.
4.5.14.4 A `sel` outside 0 to `n - 1` selects the first item.

#### 4.5.15 `api->dlg->choose_keys`

4.5.15.1 `int choose_keys(const char *title, const char *const *items, int n, const char *keys)` is `choose` with a key for each item.
4.5.15.2 `keys` holds one letter for each item, or is null. A letter chosen without regard to case returns the index of its item.
4.5.15.3 The first item is selected.

#### 4.5.16 `api->dlg->text`

4.5.16.1 `bool text(const char *title, char *buf, int max)` asks for a line of text. `buf` holds the start text.
4.5.16.2 The person types printable ASCII (0x20 to 0x7E). The line has at most `max - 1` characters, and at most 120.
4.5.16.3 RETURN accepts, also an empty line, and returns `true` with the text in `buf` and a NUL.
4.5.16.4 A cancel key returns `false` and leaves `buf` as it was. ESC clears a line that is not empty first, and cancels at the second press.
4.5.16.5 It returns `false` at once when `buf` is null or `max` is below 2.

#### 4.5.17 `api->dlg->pick_file`

4.5.17.1 `bool pick_file(const char *start, const char *ext, char *out, int outlen)` lets the person pick a file.
4.5.17.2 `start` is the folder to open, or the path of a file, whose folder is opened. A `start` that does not begin with `/` opens the root.
4.5.17.3 `ext` is a name ending such as `.crt`. The picker lists the folders and the files whose name ends with `ext`, without regard to case. A null or empty `ext` lists every file. Only the first 11 characters of `ext` count.
4.5.17.4 The picker reads the first 160 entries of a folder, and lists the folders first, then the files, each sorted by name.
4.5.17.5 It returns `true` with the full path in `out`. It returns `false` when cancelled or when `out` is null or `outlen` is below 2.
4.5.17.6 A file whose path does not fit in `outlen` bytes cannot be picked: the picker stays open.

#### 4.5.18 `api->dlg->pick_folder`

4.5.18.1 `bool pick_folder(const char *start, char *out, int outlen)` lets the person pick a folder.
4.5.18.2 The first row is "[ use this folder ]". It returns the folder that is open.
4.5.18.3 The other rules are those of `pick_file`.

#### 4.5.19 `api->dlg->progress_open`

4.5.19.1 `void *progress_open(const char *title, int total)` shows a bar with a caption. It returns at once. The bar stays until `progress_close`.
4.5.19.2 `total` is the value that fills the bar. A value of 0 or less counts as 1.
4.5.19.3 The base has one bar. A second open call replaces the first. The handle is the same for both.
4.5.19.4 The title is kept up to 63 characters.

#### 4.5.20 `api->dlg->progress_set`

4.5.20.1 `void progress_set(void *h, int value, const char *text)` adds `value` to the bar's position and shows `text` under the bar, up to 63 characters. A null `text` leaves the caption as it is.
4.5.20.2 The value is a step, not the new position. The position stops at `total`.
4.5.20.3 A handle that `progress_open` did not return is ignored.

#### 4.5.21 `api->dlg->progress_close`

4.5.21.1 `void progress_close(void *h)` takes the bar away. A handle that `progress_open` did not return is ignored.

### 4.6 `api->job` (`SvcJob`)

Since base 3.0.

The job group. A job is long work on a task of its own, so that `app_main` can answer the screens.

4.6.1 The base runs at most 4 jobs at a time, over all applications.
4.6.2 A job has a stack of at least 12 KiB.
4.6.3 The memory and the files a job takes belong to the image that started it, application or library. The function of the job runs with that image as the owner.
4.6.4 When the person closes the screens, the base marks every job of the application as cancelled and waits up to 4 seconds. When a job or the application task still runs, it refuses to close the screens.
4.6.5 When the application ends, the base marks every job of that application as cancelled, waits up to 5 seconds, and goes on. The jobs of a library are not touched, and the end or the closing of an application is no request for them: `cancelled` and `stop_requested` of a library's job answer for the job and the library alone. A job that has not ended by then keeps running. Make a job end when it is cancelled. A library whose job still runs is not unloaded to make room for an application: the base marks the job as cancelled and keeps the library until a later attempt finds no job running. When the base frees a library otherwise (a load that is refused), it waits up to 5 seconds for the jobs first.
4.6.6 A finished job keeps its handle until `join`. `start` may reuse the slot of a finished job that was not joined. Join a job before you start the next one when you need its result.
4.6.7 A job started while the base has no job table behind the group returns null (3.2.6).

#### 4.6.8 `SvcJobH`

Since base 3.0.

`SvcJobH` is the opaque handle of a job. `start` makes it and `join` frees it.

#### 4.6.9 `api->job->start`

4.6.9.1 `SvcJobH *start(int (*fn)(SvcJobH *j, void *user), void *user, uint32_t stack_bytes)` runs `fn(j, user)` on a new task.
4.6.9.2 It returns the handle, or null when `fn` is null, all four slots are in use, or the task cannot be made.
4.6.9.3 The stack is `stack_bytes` rounded down to words, and at least 12288 bytes.
4.6.9.4 `fn` returns an `int`. `join` returns it.

#### 4.6.10 `api->job->cancelled`

4.6.10.1 `bool cancelled(SvcJobH *j)` returns `true` when the job was asked to stop. The base asks every job of an application when its screens close or it ends (4.6.4, 4.6.5).
4.6.10.2 The job decides whether it can stop. Nothing stops a job that does not look.

#### 4.6.11 `api->job->join`

4.6.11.1 `int join(SvcJobH *j)` waits for the job to end and returns what `fn` returned. It frees the handle.
4.6.11.2 It returns -1 when `j` is null or is not a started job.
4.6.11.3 Any task of the application may call it. Never call it from the menu task.

#### 4.6.12 `api->job->running`

4.6.12.1 `bool running(SvcJobH *j)` returns `true` while the job has not ended.

#### 4.6.13 `api->job->stop_requested`

4.6.13.1 `bool stop_requested(void)` returns `true` when the base wants the application's own task to end because its screens are closing.
4.6.13.2 The application task polls it and returns from `app_main`.
4.6.13.3 Called by the code of a library it returns `false`: a library is not asked to end with an application.

### 4.7 `api->trx` (`SvcTrx`)

Since base 3.0.

The debug block of the TRX bitstream: the register window, the run control of the machine, and the frame ring.

4.7.0 Since base 3.2 this group leaves the base API (3.2.9): new code finds the table `trx` (major 1) with `api->lib->find`. The pointer stays until the cut.
4.7.1 The group is null unless the ID block of the core reads `TRX2`.
4.7.2 The base makes the table when it installs the application host at core start, before the first reset. `ring` is not null only while the base's frame ring is present: the base sets it when it starts the ring at core start, which it does for a core whose library has no `start` (9.7.6).
4.7.3 The base releases the debug block when the application ends (see `release`).

#### 4.7.4 `api->trx->regs`

4.7.4.1 `volatile uint8_t *regs` is the base address of the register block, byte wide. On the board it is 0x101A0000.
4.7.4.2 The registers are those of the bitstream. This document does not describe them.

#### 4.7.5 `api->trx->caps`

4.7.5.1 `uint32_t caps` is the value of the CAPS register at the time the table was made.

#### 4.7.6 `api->trx->acquire`

4.7.6.1 `int acquire(uint32_t flags)` holds the machine and takes run control: it sets the pause bit, then sets OWNER, so that `C64_STOP` no longer stops the machine. It returns 0.
4.7.6.2 `flags` is not used.

#### 4.7.7 `api->trx->release`

4.7.7.1 `void release(void)` gives run control back. It switches off the execution bitmap, both watch units and the drive comparator, disarms the cycle compare, clears OWNER and lets the machine run.
4.7.7.2 It does nothing when `acquire` was not called.

#### 4.7.8 `api->trx->announce`

4.7.8.1 `void announce(uint16_t rpc_port)` records the TCP port on which the application serves the runtime contract. 0 means none.
4.7.8.2 The base clears the port when the application ends.

#### 4.7.9 `api->trx->ring`

4.7.9.1 `const SvcRing *ring` points to the ring group. It is null without the engine.

#### 4.7.10 `api->trx->video`

4.7.10.1 `int video(u2p_video_t *v)` fills the video timing of the machine and returns 0.

#### 4.7.11 `api->trx->reset_c64`

4.7.11.1 `void reset_c64(void)` calls the operation `machine.warm_reset` of the core through the executor and ignores its result.
4.7.11.2 Nothing happens when the core has no such operation.

#### 4.7.12 `SvcRing`

Since base 3.0.

`SvcRing` holds the frame ring and the instruction trace of the engine. The layouts of its records are those of the application interface of the TRX64 line.

4.7.12.1 A call fails with a negative code. `api->trx->ring->error` turns a code of `land` into a sentence. The codes of `land` are -1 (no ring in the bitstream), -2 (no such frame), -3 (the frame has no whole chip block), -4 (the log no longer reaches the frame), -5 (the machine did not reach the snap position), -6 (the state bus refused the staged words), -7 (out of memory), -8 (the engine did not finish), -9 (the REU is enabled).

#### 4.7.13 `api->trx->ring->status`

4.7.13.1 `int status(u2p_ring_status_t *st)` fills the status. It returns 0 when the engine is present and -6 when it is not.

#### 4.7.14 `api->trx->ring->frame`

4.7.14.1 `int frame(int index, u2p_ring_frame_t *f)` copies the timeline entry `index` (0 is the oldest). It returns 0, or -2 when there is no such entry.

#### 4.7.15 `api->trx->ring->find`

4.7.15.1 `int find(uint32_t seq)` returns the index of the entry with the number `seq`, or -1 when it is gone. A number is never reused. An index moves when old entries drop.

#### 4.7.16 `api->trx->ring->land`

4.7.16.1 `int land(int index)` puts the machine on the entry `index` and leaves it paused on that frame, held by the debugger. It does not cut the timeline.
4.7.16.2 It returns 0, or a negative code (see 4.7.12.1).

#### 4.7.17 `api->trx->ring->commit`

4.7.17.1 `void commit(void)` lets the machine run on from where `land` left it. The frames after the entry are cut. It does nothing when nothing was landed.

#### 4.7.18 `api->trx->ring->error`

4.7.18.1 `const char *error(int code)` returns the sentence for a code of `land`. The string belongs to the base.

#### 4.7.19 `api->trx->ring->trace_ctl`

4.7.19.1 `int trace_ctl(int on)` switches the trace gate on (1), off (0), or only asks (-1). It returns the gate (1 or 0), or -1 when the bitstream has no trace.

#### 4.7.20 `api->trx->ring->trace_status`

4.7.20.1 `int trace_status(u2p_trace_status_t *st)` fills the status. It returns 0 when the trace is present, -6 when not.

#### 4.7.21 `api->trx->ring->trace_read`

4.7.21.1 `int trace_read(uint32_t first, uint32_t n, u2p_trace_rec_t *out)` copies `n` records of the history, starting at `first` (0 is the oldest).
4.7.21.2 It returns the number copied. It returns 0 when `n` is 0 or the trace is not there.

#### 4.7.22 `api->trx->ring->trace_whowrote`

4.7.22.1 `int trace_whowrote(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted)` finds the writes to `addr`, newest first, at most `limit`.
4.7.22.2 It returns the number found. It sets `*exhausted` to 1 when the search reached the oldest record, and 0 when it stopped at `limit`.
4.7.22.3 It returns 0 and sets `*exhausted` to 1 when `limit` is below 1 or the trace is not there.

#### 4.7.23 `api->trx->ring->reserved_files`, `api->trx->ring->reserved_trace_writes`

4.7.23.1 The two members are null in this base. Do not use them.

#### 4.7.24 `api->trx->ring->log_read`

4.7.24.1 The member is null in this base. Test it before a call.

#### 4.7.25 The record types

Since base 3.0.

The calls of the ring and of the video fill these records.

##### 4.7.25.1 `u2p_video_t`

`u2p_video_t` is the video timing.

| Member | Contract |
|---|---|
| `u2p_video_t.lines` | The lines of a frame: 312 for PAL and NTSC-50, 263 for NTSC and PAL-60. |
| `u2p_video_t.cycles_per_line` | The phi2 cycles of a line: 63, or 65 when `hz60` is 1. |
| `u2p_video_t.frame_cycles` | The phi2 cycles of a frame. |
| `u2p_video_t.cpu_hz` | The CPU clock: 985248, or 1022727 when `hz60` is 1. |
| `u2p_video_t.fps_x100` | The frames per second times 100. |
| `u2p_video_t.hz60` | 1 when the machine runs at 60 Hz. |
| `u2p_video_t.reserved` | Zero. |

##### 4.7.25.2 `u2p_ring_status_t`

`u2p_ring_status_t` is the status of the ring.

| Member | Contract |
|---|---|
| `u2p_ring_status_t.present` | 1 when the bitstream has the engine. |
| `u2p_ring_status_t.on` | 1 when the log runs. |
| `u2p_ring_status_t.landed` | 1 when a land is open. |
| `u2p_ring_status_t.reu_off` | Not filled by this base. It is always 0. |
| `u2p_ring_status_t.frames` | The number of timeline entries. |
| `u2p_ring_status_t.targets` | The number of entries `land` can go to. |
| `u2p_ring_status_t.head_seq` | The number of the newest entry. |
| `u2p_ring_status_t.oldest_seq` | The number of the oldest entry. |
| `u2p_ring_status_t.landed_seq` | The number of the entry the last `land` put the machine on. 0 when none is open. |

##### 4.7.25.3 `u2p_ring_frame_t`

`u2p_ring_frame_t` is one timeline entry, the snap that starts a frame.

| Member | Contract |
|---|---|
| `u2p_ring_frame_t.seq` | The number of the entry. It is never reused. |
| `u2p_ring_frame_t.c24` | The low 24 bits of the phi2 cycle count after the snap edge. |
| `u2p_ring_frame_t.cycles` | The phi2 cycles since the ring started. |
| `u2p_ring_frame_t.line` | The line of the snap edge. |
| `u2p_ring_frame_t.x` | The position on the line of the snap edge. |
| `u2p_ring_frame_t.flags` | The frame flags of the engine. |
| `u2p_ring_frame_t.target` | 1 when `land` can go to this entry. |
| `u2p_ring_frame_t.reserved` | Zero. |

##### 4.7.25.4 `u2p_trace_rec_t`

`u2p_trace_rec_t` is one instruction of the trace.

| Member | Contract |
|---|---|
| `u2p_trace_rec_t.cycle` | The timeline cycle count of the edge that ended the fetch of the opcode. |
| `u2p_trace_rec_t.write_start` | The first of the write records of the instruction. |
| `u2p_trace_rec_t.write_count` | The number of write records of the instruction. |
| `u2p_trace_rec_t.pc` | The program counter before the instruction. |
| `u2p_trace_rec_t.a` | The accumulator before the instruction. |
| `u2p_trace_rec_t.x` | The X register before the instruction. |
| `u2p_trace_rec_t.y` | The Y register before the instruction. |
| `u2p_trace_rec_t.sp` | The stack pointer before the instruction. |
| `u2p_trace_rec_t.p` | The status register before the instruction. |
| `u2p_trace_rec_t.opcode` | The opcode. |
| `u2p_trace_rec_t.b1` | The first operand byte, 0 when the opcode has none. |
| `u2p_trace_rec_t.b2` | The second operand byte, 0 when the opcode has none. |

##### 4.7.25.5 `u2p_trace_hit_t`

`u2p_trace_hit_t` is one write that `trace_whowrote` found.

| Member | Contract |
|---|---|
| `u2p_trace_hit_t.cycle` | The cycle of the writing instruction. |
| `u2p_trace_hit_t.addr` | The address that was asked for. |
| `u2p_trace_hit_t.pc` | The program counter of the writing instruction. |
| `u2p_trace_hit_t.old_value` | The byte before the write. |
| `u2p_trace_hit_t.new_value` | The byte after the write. |
| `u2p_trace_hit_t.flags` | This base does not write it. Clear the array before the call. |
| `u2p_trace_hit_t.index` | The index of the writing instruction in the history. |
| `U2P_TRACE_HIT_OLD_UNKNOWN` | The flag value 1: the old value is not known. This base never sets it. |
| `U2P_TRACE_HIT_NEW_UNKNOWN` | The flag value 2: the new value is not known. This base never sets it. |

##### 4.7.25.6 `u2p_trace_status_t`

`u2p_trace_status_t` is the status of the trace.

| Member | Contract |
|---|---|
| `u2p_trace_status_t.present` | 1 when the bitstream has the trace. |
| `u2p_trace_status_t.on` | 1 when the trace gate is on and the log runs. |
| `u2p_trace_status_t.gap` | 1 when the history has a gap. |
| `u2p_trace_status_t.reserved` | Zero. |
| `u2p_trace_status_t.records` | The records in the history. |
| `u2p_trace_status_t.capacity` | The records the area holds. |
| `u2p_trace_status_t.lost` | The records the engine dropped. |
| `u2p_trace_status_t.oldest_cycle` | The cycle of the oldest record. |
| `u2p_trace_status_t.newest_cycle` | The cycle of the newest record. |

### 4.8 `api->net` (`SvcNet`)

Since base 3.0.

The network group. It serves TCP over the sockets of the board. A handle is a socket number.

4.8.1 The base tracks every handle it hands out. At most 16 handles are open at a time, over all applications.
4.8.2 The base closes the handles that an application holds when the application ends, and the handles of a library when the library is unloaded. A handle belongs to the image whose code opened it (4.2.2.1). The end of one application does not close the sockets of another image.
4.8.3 A handle is non-blocking. `accept` and `recv` return at once.
4.8.4 Any task of the application may call the group.
4.8.5 A call before the base has a socket table behind the group fails: `listen`, `accept` and `connect` return -7, the others -1 (3.2.6).

#### 4.8.6 `api->net->listen`

4.8.6.1 `int listen(uint16_t port, int backlog)` opens a listening socket on all addresses with address reuse.
4.8.6.2 It returns the handle. It returns -5 when 16 handles are open or the port is taken, and -7 when no socket can be made.
4.8.6.3 A `backlog` of 0 or less counts as 2.

#### 4.8.7 `api->net->accept`

4.8.7.1 `int accept(int listener)` returns a handle for a waiting connection. The connection is non-blocking, with Nagle's algorithm off.
4.8.7.2 It returns -5 when none is waiting or 16 handles are open.

#### 4.8.8 `api->net->recv`

4.8.8.1 `int recv(int h, void *buf, int len)` reads up to `len` bytes that have arrived.
4.8.8.2 It returns the count. It returns 0 when the peer closed, -5 when nothing is waiting, and -3 on any other error.

#### 4.8.9 `api->net->send`

4.8.9.1 `int send(int h, const void *buf, int len)` sends all `len` bytes.
4.8.9.2 It returns `len`. It returns -3 when the peer is gone or the send buffer stayed full for 5000 ticks, about 25 seconds, without any progress.
4.8.9.3 It waits for the network task one tick at a time while the buffer is full.

#### 4.8.10 `api->net->close`

4.8.10.1 `int close(int h)` closes a handle. It returns 0. It returns -1 when the handle is not one the base handed out.

#### 4.8.11 `api->net->wait`

4.8.11.1 `int wait(const int *handles, int n, uint32_t timeout_ms)` waits until one of the handles can be read.
4.8.11.2 It returns a mask: bit i is set when `handles[i]` can be read. A listener can be read when a connection waits.
4.8.11.3 It returns 0 on timeout and on a select error. It returns -1 when `n` is below 0 or above 16.
4.8.11.4 A timeout below one tick (5 ms) is one tick. A negative handle is skipped.
4.8.11.5 When no handle is valid, the call sleeps for the timeout and returns 0.

#### 4.8.12 `api->net->peer`

4.8.12.1 `int peer(int h, char *buf, int len)` writes the address of the peer as `a.b.c.d:port` into `buf`.
4.8.12.2 It returns the length of the text. It returns -1 when `buf` is null, `len` is below 1, or the handle has no peer.

#### 4.8.13 `api->net->connect`

Since base 3.3.

4.8.13.1 `int connect(const char *host, uint16_t port, uint32_t timeout_ms)` opens a TCP connection to `host`. `host` is a name or a dotted address; the name is resolved as `resolve` does (4.8.14).
4.8.13.2 The call blocks until the connection stands or `timeout_ms` has passed (at least one tick, 5 ms). The caller is never the menu task or the executor task (1.3.9).
4.8.13.3 It returns the handle. The handle is non-blocking, with Nagle's algorithm off, counts among the 16 of 4.8.1 and belongs to the image whose code made the call (4.8.2). `close` closes it.
4.8.13.4 It returns -5 when 16 handles are open, -6 when the name does not resolve, -3 when the connection is refused or does not stand within `timeout_ms`, and -7 when no socket can be made or the base has no socket table behind the group (4.8.5). A refusal leaves no handle.

#### 4.8.14 `api->net->resolve`

Since base 3.3.

4.8.14.1 `int resolve(const char *name, char *buf, int len)` looks up the IPv4 address of `name` and writes it as `a.b.c.d` into `buf`. A dotted address is returned as it is.
4.8.14.2 It returns the length of the text. It returns -1 when `name` or `buf` is null, `len` is below 8, the text does not fit, the lookup fails, or the base has no socket table behind the group.
4.8.14.3 The call blocks the caller while the lookup runs (1.3.9). It takes no handle.

### 4.9 `api->c64` (`SvcC64`)

Since base 3.0.

The bus group of the C64, for the cartridge tools while the machine is stopped.

4.9.0 Since base 3.2 this group leaves the base API (3.2.9): new code finds the table `c64` (major 1, the type `C64Api` of section 9.4) with `api->lib->find`. The pointer stays until the cut.
4.9.1 The group is null unless the descriptor of the core has the id `TRX2`.
4.9.2 The group is not in `lib_register` of a library loaded at core start (section 3.2).
4.9.3 Every call goes through the memory window of the core. `dma_read` and `dma_write` do not wrap at 0xFFFF: a range that runs past it reads or writes past the window.

#### 4.9.4 `api->c64->peek`

4.9.4.1 `uint8_t peek(uint16_t addr)` reads a byte through the window.

#### 4.9.5 `api->c64->poke`

4.9.5.1 `void poke(uint16_t addr, uint8_t val)` writes a byte through the window.

#### 4.9.6 `api->c64->dma_read`

4.9.6.1 `int dma_read(uint16_t addr, void *dst, int len)` copies `len` bytes from the window into `dst`. It returns `len`. It does not fail.

#### 4.9.7 `api->c64->dma_write`

4.9.7.1 `int dma_write(uint16_t addr, const void *src, int len)` copies `len` bytes from `src` into the window. It returns `len`. It does not fail.

#### 4.9.8 `api->c64->cart_setup_bus`

4.9.8.1 `void cart_setup_bus(void)` saves the bus bridge, the internal and external bus registers, the serve-while-stopped bit, the CPU port and `C64_DMA_MEMONLY`.
4.9.8.2 It then sets the window to see the C64 memory map (I/O and cartridge), switches the external bus on, and sets the CPU port so that BASIC, KERNAL and I/O are mapped.
4.9.8.3 `C64_MODE` is left as it is.
4.9.8.4 One level is saved. A second call overwrites the saved values.

#### 4.9.9 `api->c64->cart_restore_bus`

4.9.9.1 `void cart_restore_bus(void)` puts back what `cart_setup_bus` saved. It does nothing when nothing is saved.

#### 4.9.10 `api->c64->cart_mode`

4.9.10.1 `void cart_mode(uint8_t mode)` writes `mode` to register 0 of the cartridge block.

#### 4.9.11 `api->c64->cart_detect`

4.9.11.1 `uint8_t cart_detect(void)` returns bits 0 and 1 of the cartridge detect register, the GAME line and the EXROM line as read from the expansion port.

### 4.10 `api->lib` (`SvcLib`)

Since base 3.1.

The library group. A library puts a table of function pointers in its record (`LibRecord.exports`), or several under names (`LibRecord.export_list`, since base 3.2). An image that needs the library finds the table here.

4.10.1 The group is there for every image. A base of version 3.0 has no `lib` member: test `api->struct_size` or `api->abi_minor`.

#### 4.10.2 `api->lib->find`

4.10.2.1 `const void *find(const char *name, uint32_t min_version, uint32_t *table_size)` returns a call table by name. Since base 3.2 it searches the export names first (section 9), then the names of the libraries, as it did before. Since base 3.3 an export is found at exactly the major of `min_version`.
4.10.2.2 It returns null when `name` is null, nothing is exported under that name at the major of `min_version` (the major must be equal: a table of a higher major is not given, its layout is not the caller's), no library of that name is loaded, its version is below `min_version`, or it has no table.
4.10.2.3 `min_version` is `U2A3_VERSION(major, minor)`: always write it so, never a bare number (`find("ring", 1, &size)` asks for major 0). For an export name only the major counts: an export has a major and no minor, and a table grows by appending within its major (9.1). A call at major 0 for a name that is an export finds nothing, and the base logs one line that names the call, the name and the number it was given.
4.10.2.4 `table_size` may be null. It receives the number of bytes of the table: `size` of the export, or `exports_size` of the library.
4.10.2.5 The layout of the table is the library's own header. The caller checks `table_size` before it uses a member.
4.10.2.6 The table stays valid while the library is loaded. List the library in the `needs` of the image: a library that no loaded image needs and that contributes nothing (4.10.2.7) may be unloaded when an application is opened and the heap is short.
4.10.2.7 A library that declares a poll, menu nodes, file actions, settings or tabs, in `lib_register` or in its menu record, is never unloaded this way: its poll and its menu entries point into its memory. When the heap is then too short, the base refuses to open the application and says so: `Not enough memory for <application> (needs <n> KB, <m> KB free).` Since base 3.2.

#### 4.10.3 `api->lib->need`

Since base 3.3.

4.10.3.1 `const void *need(uint32_t n, uint32_t *table_size)` returns the table of the entry number `n` of the `needs` list of the calling image. The entries are numbered from 0 in the order of the file, which is the order of `LIB_NEEDS` when the image is packed, so the number is fixed when the application is linked. No name is looked up.
4.10.3.2 The loader resolved the entry when it loaded the image: the table is the one the provider exports under that name (9.1) or, for a provider that exports one table of base 3.1, its `exports`. The entries `base`, `standalone` and `cart_port` take a number and have no table.
4.10.3.3 It returns null and sets `*table_size` to 0 when `n` is past the list, the entry has no table, or the caller has no list. The caller is found as in 4.2.2.1: the image that holds the return address, otherwise the image the base runs at that moment; a call from other code made while no image runs gets null. `table_size` may be null; otherwise it receives the size of the table as in 4.10.2.4.
4.10.3.4 The caller is the image whose code makes the call, as for `api->mem` (4.2.2.1): `lib_register`, `app_main`, the operations and the screens of the image all see their own list. The table stays valid while the image is loaded, because the image counts as a user of its providers (4.10.2.6).

### 4.11 The member `size`

Since base 3.0.

#### 4.11.1 `Svc*.size`

4.11.1.1 Every call table starts with `uint32_t size`, the size of the table in bytes.
4.11.1.2 A newer base adds members at the end. A member is there when `size` covers it and its pointer is not null.
4.11.1.3 `size` is `sizeof` of the table in the base that made it.

### 4.12 `api->sync` (`SvcSync`)

Since base 3.3.

The synchronisation group: a mutex and a short critical section, for an image that runs more than one task. The base fills it for every image of the core application.

4.12.1 The calls are for a task. No call is allowed from an interrupt.
4.12.2 No call takes a context. The base knows its own.
4.12.3 A mutex belongs to the image that made it, on the same account as the blocks of `api->mem`. When the image is unloaded or freed, the base frees every mutex it still holds, also one that is locked. An image must not leave a task waiting on a mutex past its own end.

#### 4.12.4 `SvcMutex`

Since base 3.3.

`SvcMutex` is the opaque handle of a mutex. `mutex_new` makes it and `mutex_free` frees it.

#### 4.12.5 `SYNC_FOREVER`

Since base 3.3.

`SYNC_FOREVER` is the value 0xFFFFFFFF for the `timeout_ms` of `mutex_lock`: wait without a limit.

#### 4.12.6 `api->sync->mutex_new`

4.12.6.1 `struct SvcMutex *mutex_new(void)` makes a mutex with priority inheritance. It is not recursive: a second lock by the task that holds it waits for its own unlock, or times out.
4.12.6.2 It returns null when the heap has no room. The memory is the base's, on the account of the image whose code runs.
4.12.6.3 Any task may call it.

#### 4.12.7 `api->sync->mutex_lock`

4.12.7.1 `bool mutex_lock(struct SvcMutex *m, uint32_t timeout_ms)` takes the mutex.
4.12.7.2 A `timeout_ms` of 0 tries once and returns at once. `SYNC_FOREVER` waits without a limit. Any other value waits that long, rounded up to the tick of 5 ms: `pdMS_TO_TICKS(timeout_ms) + 1` ticks.
4.12.7.3 It returns `true` when the mutex is taken, and `false` on a timeout or when `m` is null.
4.12.7.4 A task that may block calls it. A task between `enter` and `leave` does not.

#### 4.12.8 `api->sync->mutex_unlock`

4.12.8.1 `void mutex_unlock(struct SvcMutex *m)` gives the mutex back. Only the task that took it may give it back.
4.12.8.2 A mutex that is not held, and a null `m`, do nothing.

#### 4.12.9 `api->sync->mutex_free`

4.12.9.1 `void mutex_free(struct SvcMutex *m)` frees the mutex and takes it off the account of its image.
4.12.9.2 A null `m` does nothing. A mutex that is held, or that a task waits for, is not freed: the result is not defined.
4.12.9.3 Any task may call it.

#### 4.12.10 `api->sync->enter`

4.12.10.1 `void enter(void)` opens a critical section. It is the critical section of the RTOS (`taskENTER_CRITICAL`) and nests with a counter: every `enter` has a `leave` on the same task.
4.12.10.2 It is for a few register accesses: the four byte accesses of one 32 bit register of the TRX block, or a flag that is set and tested in one go.
4.12.10.3 Between `enter` and `leave` the task calls nothing that blocks: not `sleep_ms`, `yield`, a `delay_us` of more than a few microseconds, a mutex call, a file, socket, dialog or log call, and nothing that enters another library.
4.12.10.4 An image that ends with a section open is a defect the base cannot repair.

#### 4.12.11 `api->sync->leave`

4.12.11.1 `void leave(void)` closes the section `enter` opened. See 4.12.10.

### 4.13 `api->settings` (`SvcSettings`)

Since base 3.3.

The settings group: the settings of the running core by key, as the base holds them. A setting has three layers: the default of its declaration (0), the flash of the host (1) and the `.cfg` file of the core (2). The value in force is the one of the topmost layer that has one, and one place to write: the file.

4.13.1 No call takes a context. The group acts on the running core. Before the base has handed the core over (while `lib_register` of a library loaded at core start runs, section 3.2) there are no settings: every call answers as for a bad index.
4.13.2 `generation`, `find`, `value`, `text`, `layer`, `info`, `cfg_value`, `cfg_text` and `extra_get` read, and any task may call them. `set`, `override_value`, `apply` and `extra_set` change the state of the core. The task that calls them holds the core lock: a handler, a core hook or a poll. `apply` must not be called from a job.
4.13.3 An index (from `find`) stays valid while `generation` is the same. When the generation changed, find the keys again.
4.13.4 A bad index gives -1 (an `int`), `false` (a `bool`) or the empty string (a text). Nothing asserts.
4.13.5 Besides the group, `api->ops->state` answers two states of the base that belong here: `set.<key>`, the value in force of the setting `<key>` as text (a list setting gives its label, a text setting its text with the leading blanks; a space in the key may be written `_`, the case is folded; null when the core has no such setting), and `net.gated`, the text `yes` while the REST token gate is on and null otherwise.

#### 4.13.6 `api->settings->generation`

4.13.6.1 `uint32_t generation(void)` counts up whenever the set of settings, or its indexes, changed: a new settings object (the base makes one when it merges what the libraries contribute) and every loading of another configuration.
4.13.6.2 It returns 0 while the core has no settings.

#### 4.13.7 `api->settings->find`

4.13.7.1 `int find(const char *key)` returns the index of the setting named `key`, compared without regard to case, or -1 when the core has none or `key` is null.

#### 4.13.8 `api->settings->value`

4.13.8.1 `int value(int ix)` returns the value in force: for a list the position of its label, for a range setting the position too (the number the menu shows is `base` plus the position, see `info`).
4.13.8.2 An override of `override_value` wins over the stored value.
4.13.8.3 It returns -1 for a bad index and for a text setting, which has no value index. Use `text`.

#### 4.13.9 `api->settings->text`

4.13.9.1 `const char *text(int ix)` returns the text in force of a text setting: the text of the core layer, else the default, with the leading blanks kept.
4.13.9.2 It returns the empty string for a setting that is no text and for a bad index.
4.13.9.3 The pointer belongs to the base. It is valid until that setting is written again or `generation` changes. Copy it at once.

#### 4.13.10 `api->settings->layer`

4.13.10.1 `int layer(int ix)` returns where the value comes from: 0 the default of the declaration, 1 the flash of the host, 2 the file of the core. It returns -1 for a bad index.

#### 4.13.11 `api->settings->set`

4.13.11.1 `bool set(int ix, int value)` stores the list position `value` in the core layer and marks the setting changed. It ends an override of the same setting.
4.13.11.2 The file is written at once when Auto Save Config is on. With it off the value is held and the file waits for the next save, and the call still returns `true`.
4.13.11.3 It does not run the operation of the setting. `apply` does that.
4.13.11.4 It returns `false` when the index or the value is bad, and for a text setting (nothing changes). It returns `false` too when the core has no place for a file or the file could not be written. The value holds in memory then.

#### 4.13.12 `api->settings->override_value`

4.13.12.1 `void override_value(int ix, int value)` sets a value for this run only. It is never stored and never written into a `.cfg` or an exported configuration. `value` returns it from now on.
4.13.12.2 A bad index, a bad value and a text setting do nothing. The call does not run the operation of the setting: call `apply` after it.

#### 4.13.13 `api->settings->apply`

4.13.13.1 `void apply(int ix)` puts the value in force into the core, on the calling task. A setting with an operation calls it with the value plus `base`. A setting bound to register bits writes the register that holds them. A setting that selects a file does nothing here.
4.13.13.2 A bad index does nothing. The base logs the failure of an operation.

#### 4.13.14 `api->settings->info`

4.13.14.1 `bool info(int ix, int *nvalues, int *base)` returns the number of labels, or of range steps, and the base: the menu shows `base` plus the position, and the operation of a range setting receives it.
4.13.14.2 It returns `false` for a bad index. The two numbers are then not written. Either pointer may be null.

#### 4.13.15 `api->settings->extra_get`

4.13.15.1 `bool extra_get(const char *key, char *val, int n)` reads a key of the core's own that is no choice from a list: the keys with the prefix `Pad ` (the recorded pad maps). The settings file keeps them beside the settings. The match ignores the case.
4.13.15.2 It returns `false` when the key is not there, or when an argument is null or `n` is below 1. `val` is then not written. A value longer than `n - 1` characters is cut.

#### 4.13.16 `api->settings->extra_set`

4.13.16.1 `bool extra_set(const char *key, const char *val)` stores such a key. The core keeps at most 4 keys of at most 23 characters, with values of at most 79 characters.
4.13.16.2 It returns `false` for a key without the prefix `Pad `, a key or value that is too long, no room, or a file that could not be written (the value holds in memory). With Auto Save Config off the file waits for the next save, and the call returns `true`.

#### 4.13.17 `api->settings->cfg_value`

4.13.17.1 `int cfg_value(const char *key, int def)` returns the value of the setting named `key` as `value` does, and `def` when the core has no such setting, when `key` is null, and for a text setting.

#### 4.13.18 `api->settings->cfg_text`

4.13.18.1 `const char *cfg_text(const char *key)` returns the text of the text setting `key` with the leading blanks skipped. It never returns null: it returns the empty string when the core has no such setting or the setting is no text.
4.13.18.2 The pointer lives as long as that of `text`.

#### 4.13.19 The settings the base owns

Since base 3.3.

4.13.19.1 The base declares up to six settings for a core, as it does Picture and Volume (4.13.19.3 says which). A library reads them by key with `find`, `value` or `cfg_value`, and the keys are the stored keys of every `.cfg`. The base applies them itself; a library does not.
4.13.19.2 The keys, with the values in the order of their positions (the first is position 0):

| Key | Values | Default |
|---|---|---|
| `Joystick Input` | `Normal`, `Swapped`, `WASD Port 2`, `WASD Port 1`, `WASD P2 No KB`, `WASD P1 No KB` | position 0 |
| `Mouse Mode` | `Cursor keys`, `Wheel: cursor keys`, `Wheel: joystick` | position 0 |
| `Mouse Sensitivity` | range 1 to 16 | position 7 |
| `Mouse Acceleration` | `Off`, `Adaptive` | position 0 |
| `Mouse Wheel Sensitivity` | range 1 to 16 | position 7 |
| `Mouse Wheel Direction` | `Normal`, `Reversed` | position 0 |

4.13.19.3 The base adds a key to a core only where the code can do what the row says. `Mouse Sensitivity` and `Mouse Acceleration` for a core whose descriptor has a mouse (the USB driver scales each report with them before the core's mouse sink takes it), `Joystick Input` for a core whose capability list names `kbd_joystick` and `Mouse Mode`, `Mouse Wheel Sensitivity` and `Mouse Wheel Direction` for a core that names `mouse_driver`. No core of the base names either capability: the host decodes no WASD register, and the mouse sink consumes every report. A key that the description of the core hides (`hide` in its menu TOML, 6.9.7) is not added. The menu lists them on the page of the application (F7), under Input. A key that is not added is not in the settings: `find` returns -1 and `base_setting_changed` is not called for it.
4.13.19.4 `Joystick Input` is applied in part by the base: it sets the port that the keyboard joystick drives and the WASD mapping of the keyboard. Hardware of the core that follows the setting, such as the paddle swap of a C64, is the library's; it hears the change through `base_setting_changed` (9.5.5).
4.13.19.5 A descriptor, a menu TOML `core_setting` and a library must not declare one of these keys. The check of the descriptor in the build and the tests refuses it. On the board the base adds a key only when the descriptor does not declare it, so a core's own item would stay and the base would not apply it.

4.13.19.6 The look of the menu is no setting of the base. No setting of the base changes it. From the coming release on (base 3.4, 1.2.9) the page of the application has no Look group and no row for `Overlay Transparency`, `Color Scheme`, `Filename overflow squeeze` or `Navigation Style`. These keys in an older `.cfg` are ignored.

### 4.14 `api->ctx` (`SvcCtx`)

Since base 3.3.

The context group: the state and the fields of the running core that a handler, a hook or a job reads or writes. No call takes a context. Before the base has handed the core over (section 3.2) the calls fail: `state` returns 0, `notice`, `target_path`, `core_folder` and `media_source` return `false`, `media_dir` returns the empty string, and `set_paused`, `job_progress` do nothing.

4.14.1 Task classes: any task, a task that holds the core lock (an operation or state handler, a core hook, a poll), and the task of a job. A string the base returns belongs to the base and is valid until the call that changes it. Copy it at once.

#### 4.14.2 `CTX_STARTED`, `CTX_PAUSED`, `CTX_FROZEN`, `CTX_MENU_OPEN`, `CTX_MEDIA_RESTORING`

Since base 3.3.

| Symbol | Contract |
|---|---|
| `CTX_STARTED` | Bit 0 of `state`: the core has been released once. |
| `CTX_PAUSED` | Bit 1: Pause is on. The machine stays held after the menu closes, until Resume. |
| `CTX_FROZEN` | Bit 2: the machine is stopped for the menu. |
| `CTX_MENU_OPEN` | Bit 3: the menu is on the screen, frozen or not. |
| `CTX_MEDIA_RESTORING` | Bit 4: the base mounts what a start or a media set stores. The machine is held and the base releases it later, so a mount in a hook of the core neither resets nor releases it. |

#### 4.14.3 `api->ctx->state`

4.14.3.1 `uint32_t state(void)` returns the five flags as the bits of 4.14.2. It is a snapshot: the flags change on other tasks. Any task may call it.

#### 4.14.4 `api->ctx->set_paused`

4.14.4.1 `void set_paused(bool on)` sets or clears Pause. It sets the flag only. The machine is stopped when the menu closes.
4.14.4.2 The caller tests `CTX_STARTED` first. The task holds the core lock.

#### 4.14.5 `api->ctx->notice`

4.14.5.1 `bool notice(const char *text, bool only_if_free)` stores a line that the screen shows once, when the menu next opens. The text is cut to 199 characters.
4.14.5.2 With `only_if_free` true it stores the line only when no notice waits. With it false the line replaces a waiting one.
4.14.5.3 It returns `true` when the line was stored, and `false` when a notice waits and `only_if_free` is true, or when `text` is null. Any task may call it.

#### 4.14.6 `api->ctx->media_dir`

4.14.6.1 `const char *media_dir(void)` returns the folder the menu was on when a task asks for a name: where a new file goes. It is the empty string when there is none, and never null. Any task may call it.

#### 4.14.7 `api->ctx->target_path`

4.14.7.1 `bool target_path(const char *name, const char *ext, char *out, int n)` makes the full path of a new file `name` on the medium the menu saves to: the folder of `media_dir`, or else the first medium of the root except `/flash`.
4.14.7.2 `ext`, for example `.bin`, is added when `name` has no extension. A path that does not fit `n` is cut.
4.14.7.3 It returns `false` when there is no medium. `out` is then not written. Any task may call it.

#### 4.14.8 `api->ctx->core_folder`

4.14.8.1 `bool core_folder(const char *sub, char *out, int n)` writes the folder the `.cor` file of the core installed its data into: the folder of the `.cor` file, then the name of the file without the extension, with no slash at the end. With `sub` ("roms", "saves", "configs", any name) it writes that sub folder of it. A null or empty `sub` is the core folder itself.
4.14.8.2 It returns `false`, and `out` is the empty string, when the core was not started from a `.cor` file (JTAG, developer) or the path does not fit `n`.
4.14.8.3 The base makes the core folder, `roms`, `saves` and `configs` at the start, when the medium can be written. Another sub folder is the image's to make. Any task may call it.

#### 4.14.9 `api->ctx->job_step`

4.14.9.1 `bool job_step(void)` is the protocol of a long write in steps. It returns `true` when the menu steps this operation: do one chunk per call and answer `more` in the `OpResult` until done. It returns `false` when every chunk runs in this call.
4.14.9.2 The task holds the core lock, inside the handler of the operation.

#### 4.14.10 `api->ctx->job_cancelled`

4.14.10.1 `bool job_cancelled(void)` returns `true` when the person asked to drop the running write. The call clears the flag, so a second call returns `false`.
4.14.10.2 The operation then removes its partial file and answers done. The task holds the core lock, inside the handler.

#### 4.14.11 `api->ctx->job_progress`

4.14.11.1 `void job_progress(int pct, const char *path)` reports where the write is: `pct` from 0 to 100 (a value outside is clipped), and the file being written once it is placed. A null `path` keeps the one reported before. The menu reads it the next time it draws.
4.14.11.2 The task holds the core lock, inside the handler.

#### 4.14.12 `api->ctx->media_source`

4.14.12.1 `bool media_source(const char *slot, char *buf, int len)` writes what is in the media slot `slot`, as the engine's own spec: the path of the medium, with a word in front when the engine needs one (`ro /usb0/x.d64`, `unlinked /usb0/x.d64`, `folder /usb0/set`). A path starts with `/`.
4.14.12.2 `slot` is one of these names, with or without the colon at the end and without regard to case: a mount slot of the core's own engine (`D1`, `Drive A`, `Cartridge`), a window drive of the core (the prefix of its window media and the number: `DF0` to `DF3`), a sector unit (`HD0`, `HD1`) or a media slot of its description (`D1:`). The mount slots of the engine are searched first, then the window drives and the sector units, which the base mounts itself and answers with the spec its own engine keeps (the path of the image, or `folder <path>, <GB> GB` for a folder disk), then the media slots; a media slot gives its state text when that text is a spec. These are the slots the base stores for `Persist medium mounts`.
4.14.12.3 It returns `true` with the spec in `buf`, cut to `len - 1` characters. It returns `false`, with `buf` the empty string, for no such slot and for an empty one. Any task may call it.

#### 4.14.13 `api->ctx->mount_notify`

4.14.13.1 `void mount_notify(const char *slot)` tells the base that the medium in a slot changed by its engine's own doing. `slot` is the name of the slot in the `[mount]` list of the core's API TOML (`Drive A`, `Cartridge`), as `media_source` takes it: the colon at the end and the case do not count.
4.14.13.2 The call only marks the slot. The base reads the spec of the slot on its next pass, under the core lock, and stores it for the setting `Persist medium mounts` and for the media sets. The pass waits until the start has mounted what the `.cfg` file stores. A second call before the pass stores once. A name the core does not list does nothing.
4.14.13.3 The engine that makes the change calls it. A shared library notifies every change it makes to its own slot, so the code of a core that calls one of its members does not. Any task may call it.

#### 4.14.14 `api->ctx->ask_list`

Since base 3.5 (T262). A member after `mount_notify`; a library or an application checks the `size` member of the table before it uses it (`SVC_HAS(api->ctx, ask_list)`).

4.14.14.1 `void ask_list(const char *op, int phase, const char *prompt, const char *src)` asks the person to choose one of a list of rows. `src` is the name of a state whose text holds the rows (6.19.2). The call only stores the request. When the operation that called it has returned to the menu, the menu shows the rows under the title `prompt` as a list that scrolls and takes UP, DOWN, PAGE UP, PAGE DOWN, HOME and END.
4.14.14.2 RETURN takes the row under the cursor, `<-` and ESC leave without a choice. The base then calls `op` again with the arguments it declares by name: `phase` (an `AT_INT`, the number given here) and `answer` (an `AT_STRING`, the value of the row, which is the first field of its line). An operation of another shape is refused by the call, with the registry's sentence in the status row.
4.14.14.3 The list has no fixed number of rows. The base copies the text of the state, at most a quarter of the heap that is free then (the largest list that fits is shown whole; a longer one is cut at the end of a line and the status row says so), so 47 values and 5000 values are the same call. The rows are read again about every half second while the list is up: a list that a job fills grows under the cursor, and the cursor stays on its value.
4.14.14.4 Only the last call before the operation returns counts. A call with an empty `op` or an empty `src` does nothing. The call is for an operation that the menu calls (a node, a hotkey, a file action); an operation that another door calls (REST, a poll) shows no list. The task holds the core lock, inside the handler.
4.14.14.5 A node can ask for the same list without code: `pick = "@state"` in its `ui` (6.7.3, 6.19.3).

### 4.15 `api->io` (`SvcIo`)

Since base 3.3.

The io group: the hardware of the running core, as the base's own `CoreIo` has it. Each member is a window of the core with offsets relative to that window. The header names no register of any core: the registers are the core's own.

4.15.1 The group is null on a core without a register window: a core with no machine (the diag core), and a context with no `CoreIo`. A library tests `api->io` before the first call. It is also null while `lib_register` of a library loaded at core start runs (section 3.2).
4.15.2 No call takes a context. Every access is one byte wide per register: the windows take one byte per access. A register of 32 bit is four calls. The caller holds `api->sync->enter` and `leave` around the four when the bitstream needs them back to back.
4.15.3 The register calls (`rd`, `wr`, `cart_rd`, `cart_wr`, `bus_rd`, `bus_wr`, `drv_rd`, `drv_wr`, `ram_memonly`, `video_format`, `stop`, `mode_reset`), `ram_read` and `ram_write` belong to a task that holds the core lock. What the last two see depends on `ram_memonly` and on whether the machine is stopped. The bulk calls that depend on no mode (`ddr_read`, `ddr_write`, `ddr_abs_read`, `ddr_abs_write`, `rom_read`, `rom_write`, `eeprom_read`, `eeprom_write`) and `window` may also run on a job task. `keys_read` and `delay_us` are for any task.
4.15.4 Work that holds the machine for a long time, a dump for example, is a stepped operation (4.14.9): the menu calls it again while it answers `more`, and each call holds the lock for one step.
4.15.5 No call reports an error. A core or host without a window ignores a write and reads 0. A bulk read of a window the core does not have gives zeros.

#### 4.15.6 `api->io->rd`, `api->io->wr`

4.15.6.1 `uint8_t rd(uint32_t reg)` and `void wr(uint32_t reg, uint8_t v)` read and write the byte register `reg` of the register block of the core. The register numbers are the core's.

#### 4.15.7 `api->io->ddr_read`, `api->io->ddr_write`

4.15.7.1 `void ddr_read(uint32_t off, void *p, uint32_t n)` and `void ddr_write(uint32_t off, const void *p, uint32_t n)` read and write `n` bytes of the DDR2 window of the core, at `off` from its start (the address 0x01000000 of the address space).
4.15.7.2 A write is complete when the call returns: the base reads the last byte back. The caller keeps `off + n` inside the window of the core.

#### 4.15.8 `api->io->rom_read`, `api->io->rom_write`

4.15.8.1 `void rom_read(uint32_t off, void *p, uint32_t n)` and `void rom_write(uint32_t off, const void *p, uint32_t n)` access the ROM aperture, 32 KB, with `off` from its start. What lies at which offset is the core's.

#### 4.15.9 `api->io->ram_read`, `api->io->ram_write`, `api->io->ram_memonly`

4.15.9.1 `void ram_read(uint32_t addr, void *p, uint32_t n)` and `void ram_write(uint32_t addr, const void *p, uint32_t n)` access the base RAM window, 64 KB per page of the core. `addr` is the address of the machine. The access is byte by byte.
4.15.9.2 `void ram_memonly(bool on)` selects the path. With `on` every byte goes to the second port of the RAM, the I/O range included. Otherwise an access in the I/O range is a bus cycle through the decoding of the machine. The caller puts it back to `false`.

#### 4.15.10 `api->io->drv_rd`, `api->io->drv_wr`

4.15.10.1 `uint8_t drv_rd(uint32_t off)` and `void drv_wr(uint32_t off, uint8_t v)` access the byte registers of the window of the built-in drive, `off` from the window. A core without a drive window reads 0 and ignores the write.

#### 4.15.11 `api->io->ddr_abs_read`, `api->io->ddr_abs_write`

4.15.11.1 `void ddr_abs_read(uint32_t addr, void *p, uint32_t n)` and `void ddr_abs_write(uint32_t addr, const void *p, uint32_t n)` access the fixed DDR2 areas of the drive and the cartridge port. Only the absolute addresses 0x00EA8000 up to 0x00F00000 are served.
4.15.11.2 A range that is not wholly inside is ignored on a write and reads as zeros. A write is fenced and read back as `ddr_write` is.

#### 4.15.12 `api->io->cart_rd`, `api->io->cart_wr`

4.15.12.1 `uint8_t cart_rd(uint32_t reg)` and `void cart_wr(uint32_t reg, uint8_t v)` access the byte registers of the control block of the cartridge port, `reg` from the block. A core without it reads 0 and ignores the write.

#### 4.15.13 `api->io->bus_wr`, `api->io->bus_rd`

4.15.13.1 `void bus_wr(uint32_t off, uint8_t v)` and `uint8_t bus_rd(uint32_t off)` access the byte registers of the I/O block on the bus side of the core, `off` from the start of the block. `bus_rd` reads the byte that `bus_wr` writes.
4.15.13.2 A core or host without the block reads 0 and ignores the write.

#### 4.15.14 `api->io->eeprom_read`, `api->io->eeprom_write`

4.15.14.1 `void eeprom_read(uint32_t off, void *p, uint32_t n)` and `void eeprom_write(uint32_t off, const void *p, uint32_t n)` access the EEPROM window of the cartridge port. Byte 0 is the dirty flag: a read gives it, and any write of it clears it. The 2 KB of data begin at offset 2048. One byte per access. A core without the window reads 0.

#### 4.15.15 `api->io->keys_read`

4.15.15.1 `bool keys_read(uint8_t m[8], bool *restore)` reads the physical keys now: `m[col]` bit `row` is 1 for a key that is down, the matrix as the scan of the menu reads it, in one critical section, and the level of RESTORE in `*restore`.
4.15.15.2 It returns `false` when this host cannot say. `m` and `*restore` are then not written. Any task may call it.

#### 4.15.16 `api->io->stop`, `api->io->mode_reset`

4.15.16.1 `void stop(bool on)` holds the machine (`on`) or lets it run.
4.15.16.2 `void mode_reset(bool on)` holds the application of the core in reset (`on`) or releases it.

#### 4.15.17 `api->io->video_format`

4.15.17.1 `void video_format(uint8_t v)` writes the video format register of the host, one byte: the bits of the system mode for the mode controller of the host. A host or core without it ignores the write.

#### 4.15.18 `api->io->delay_us`

4.15.18.1 `void delay_us(uint32_t us)` is the same call as `api->sys->delay_us` (4.1.10). Any task may call it.

#### 4.15.19 `api->io->window`

4.15.19.1 `volatile uint8_t *window(const char *name, uint32_t *size)` returns the base address of a register window of the bitstream that the API description of the core declares by `name`, in the address space of the board (4.15.19.2), and its size in `*size`.
4.15.19.2 The windows are the ones of the API description of the core that is running (`[[window]]`, section 19 of its record). For a name that it declares the call returns the address of the window and writes its size to `*size`. For any other name, a null `name` and a core without a description it returns null and writes 0 to `*size`. A null `size` is allowed. A caller treats a null result as a window the core does not grant. The address of a `[[window]]` is the firmware's (the hand-over contract: `U2P_IO_BASE` 0xA0000000 with 1 MiB, `IOBASE` 0x04000000 with 16 MiB; any other address is one of the TOML A already); both readers translate it by the `[io]` bases (`iobase`, `u2p_io_base`) of the TOML A the app is linked with, and `io.window` returns the translated address. A window that starts in one of the two firmware spans (`U2P_IO_BASE` with 1 MiB, `IOBASE` with 16 MiB) and ends beyond it is refused by both readers, because its address would stay untranslated and on the board it is memory, not I/O. After the translation both readers refuse a `[[window]]` that is not wholly inside the I/O span `[iobase, iobase + 16 MiB)` of the `[io]` of the TOML A the app is linked with (for `host.toml` 0x10000000 .. 0x10FFFFFF): the host's bus converter sends an address with bit 28 set to the I/O bus as `addr[23:0]` and ignores bits 31..29 and 27..24, so an address outside the span is memory or an alias of a register (0x11100550 is `master_volume`, a block at 0x10FFFFF0 with size 0x20 wraps onto I/O 0). `io.window` returns null and writes 0 to `*size` for a block that is not wholly inside that span, whatever record it comes from. Each register has one spelling inside the span, so the comparison with the base blocks, which is literal, is exact. A `[[window]]` is checked for its name, address and size, and it is refused when it covers a byte of a block with `owner = "base"` in the TOML A the app is linked with, compared after the translation, so the firmware's and the board's spelling of one register are refused alike: `gateware/host.toml` for the shared app and the host app, the TOML A of the design for an app linked for another one (`--toml-a` of `descrec.py`, `cor_pack.py`; `app_build.py build` takes it from its own `--toml-a`). The app has those blocks as a table generated at build time (`basetab.h`, `descrec.py basetab`) and reads nothing at run time.
4.15.19.3 A core whose gateware is described by TOML A (6.14) is granted the blocks it owns by their names in the same way: the name of a block of the core is the name of a window. Blocks of one TOML A do not overlap (6.14.5); a block of the core is checked only against the blocks of its own TOML A; a `[[window]]` is checked against the base blocks (4.15.19.2). A block of the base is not a window: `window` returns null for its name, and a shared driver reaches it through its map, not through a grant.

### 4.16 `api->board` (`SvcBoard`)

Since base 3.3.

The board group: the base operations a library calls by name, and the SID sockets. No call takes a context. The task is the task of a handler, hook, poll or job, and a call that waits says so. No call is allowed from an interrupt.

#### 4.16.1 `SID_SOCKET_OFF`, `SID_SOCKET_ON`, `SID_SOCKET_ON_TYPE1`

Since base 3.3.

| Symbol | Contract |
|---|---|
| `SID_SOCKET_OFF` | The supply of a socket: the regulator is off (bits 1:0 of its nibble are 00). |
| `SID_SOCKET_ON` | The socket is supplied (bits 1:0 are 10). |
| `SID_SOCKET_ON_TYPE1` | The socket is supplied for a 6581, the type 1 of the detection (bits 1:0 are 11). |

#### 4.16.2 `api->board->fpga_caps`

4.16.2.1 `uint32_t fpga_caps(void)` returns the capability register of the bitstream, four bytes, as the FPGA reports it. The meaning of a bit is the bitstream's: a library keeps its own constants for the bits it tests. Any task may call it.

#### 4.16.3 `api->board->is_commodore`

4.16.3.1 `bool is_commodore(void)` returns `true` on the board sold under Commodore's name and `false` on the other badge of the same board. Any task may call it.

#### 4.16.4 `api->board->esp32_rpc`

4.16.4.1 `int esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)` sends one request to the ESP32 control module (power mode, wake on Wi-Fi, power off, power cycle, GET commands) over the one UART of the board.
4.16.4.2 `cmd` is the command, `args` are its `nargs` bytes, at most 100. With `timeout_ms` above 0 the call waits for the answer: it returns the number of bytes of the whole answer packet, copied to `reply`, at most `maxreply`. The first byte is the command, a header of 4 bytes and a 4 byte error code follow.
4.16.4.3 With `timeout_ms` of 0 or below it sends and does not wait. It returns 0 when sent (the power off commands).
4.16.4.4 It returns -1 when no answer came in time, -2 when `nargs` is out of range, and -3 when there is no link or no buffer. It waits up to `timeout_ms` for the answer and 1 s for a buffer.

#### 4.16.5 `api->board->video_set_system`

4.16.5.1 `void video_set_system(int sys)` gives the picture layer a new system mode: `sys` is the index of the colour timings (PAL, NTSC, ...) of the System Mode setting. The HDMI mode of the layer and the Picture row are picked again. The task holds the core lock.

#### 4.16.6 `api->board->video_set_pll`

4.16.6.1 `void video_set_pll(int sys, int ppm)` sets the video PLL for the system mode `sys` with the offset `ppm` of the colour clock, -100 to 100, and then resets the HDMI PLL. 0 is the nominal clock. The task holds the core lock.

#### 4.16.7 `api->board->sid_socket`

4.16.7.1 `int sid_socket(int socket, int state, bool shunt, bool filter_caps)` sets the supply of one SID socket. The expander at address 0x40 on the 1.8 V I2C channel takes one control byte for both sockets: bits 3:0 are socket 0 and bits 7:4 socket 1.
4.16.7.2 A nibble is the supply (bits 1:0: 00 for `SID_SOCKET_OFF`, 10 for `SID_SOCKET_ON`, 11 for `SID_SOCKET_ON_TYPE1`), the shunt of 1 kOhm in bit 2 (`shunt`) and the option of the filter capacitor in bit 3. `filter_caps` is the setting of the core as it holds it, 0 or 1, and the base inverts it into bit 3.
4.16.7.3 The base keeps the control byte. It starts at 0 when the application starts. A call replaces the nibble of `socket` and leaves the other one, then writes register 1 of the expander with the byte and register 3 with 0. The write holds the bus lock named `SID Configuration`, which the keyboard scan shares, and waits for it.
4.16.7.4 It returns 0 when done, and also when there is nothing to do because the board has no expander. It returns -1 for a `socket` outside 0 to 1 or a `state` outside the three values, and -2 when the bus lock was not obtained in time. Nothing is written then, and the control byte stays as it was. The task holds the core lock.

#### 4.16.8 `api->board->case_led_select`

4.16.8.1 `void case_led_select(uint8_t v)` writes the source select of the two case LEDs: the high nibble for LED 1, the low nibble for LED 0, each a source code of the board. The task holds the core lock.

### 4.17 `api->overlay` (`SvcOverlay`)

Since base 3.3.

The overlay service: a library shows pixels over the machine's picture, and only the base drives the pixel layer (T256). An element is a rectangle that a library paints itself in an RGB565 surface; its name, its place and its size are the core's UI TOML's. Any task may call. A call checks its arguments at once and answers with its result; its effect (a redraw, text in a surface, the menu hidden or shown) is taken over by the UI task at its next frame, in the order of the calls. No call takes a context, and none is allowed from an interrupt. A library built before the group never reads it: its `LibApi` is smaller (3.1), and a loader without a core leaves the member null (3.2.12).

4.17.1 The state decides whether an element is on the screen, not the call. The call is valid at any time.
4.17.1.1 The service is up from the moment the core's start is finished (the splash is closed). A call before that, for instance from `lib_register`, returns `OVL_E_NOSERVICE`: a library shows its elements from a poll, a hook or an operation.
4.17.2 While the menu is open no element shows. The still is the exception: it lies under the menu, and the base blends the menu over it in software with the transparency of the menu.
4.17.3 On a C64-type core (the picture goes through the host's own video path) elements show over the running picture while the menu is closed. On any other core they show only while the machine stands (Pause). Elements are opaque: only the pixels of the colour key of the layer, `0x0841` (RGB565), let the picture through. The transparency of the menu applies to the open menu only.
4.17.4 Several libraries may show elements at once. They are drawn in the order of the elements in the UI TOML. When a library is released its elements go at once, and the layer takes its state. A core is left by the return to the host (`hw_return_to_host`), which reconfigures the FPGA: the service is not told and nothing of the core runs afterwards.
4.17.5 The elements of a core are the `[[element]]` tables of its UI TOML over the base's (api.md 6.16). The base has two: `still` (the whole canvas, the still) and `paused` (the PAUSED tag).

#### 4.17.6 `SvcSurface`

Since base 3.3.

| Member | Contract |
|---|---|
| `SvcSurface` | A surface of RGB565 pixels with the stride `w`, in a buffer the library owns. The base reads it from the UI task until `remove`, the end of the library or the end of the core. |
| `SvcSurface.px` | The pixels. |
| `SvcSurface.bytes` | The size of the buffer in bytes. It has to be `w * h * 2`. |
| `SvcSurface.w`, `SvcSurface.h` | The size of the surface in pixels. It has to be the element's size in the TOML. |

#### 4.17.7 `OVL_E_NONE`, `OVL_E_NAME`, `OVL_E_SIZE`, `OVL_E_BYTES`, `OVL_E_TAKEN`, `OVL_E_HANDLE`, `OVL_E_NOSERVICE`, `OVL_E_NOFRAME`, `OVL_E_ARG`

Since base 3.3; `OVL_E_ARG` since base 3.4.

| Symbol | Contract |
|---|---|
| `OVL_E_NONE` | 0: done. |
| `OVL_E_NAME` | -1: the core's UI TOML has no element of that name. |
| `OVL_E_SIZE` | -2: the width and height of the surface are not the element's size. |
| `OVL_E_BYTES` | -3: the surface is null or has no buffer, or `bytes` is not `w * h * 2`. |
| `OVL_E_TAKEN` | -4: another library shows the element. |
| `OVL_E_HANDLE` | -5: not a handle of an element of this library. |
| `OVL_E_NOSERVICE` | -6: no core runs, or the service is not up. |
| `OVL_E_NOFRAME` | -7: there is no complete frame of the machine's picture to take (`frame`). The surface is unchanged. |
| `OVL_E_ARG` | -8: a flag that `exit_to_machine` does not know, or a target that `goto_screen` cannot read (null, empty, or `OVL_TARGET_LEN` (48) characters or more). |

#### 4.17.8 `api->overlay->show`

4.17.8.1 `int show(const char *name, const SvcSurface *surface)` shows the element `name` with `surface`. It returns the handle (greater than 0) or an `OVL_E_*` code. The place and the size are the TOML's.
4.17.8.2 The library that shows an element already may call `show` again with another surface. The element is drawn again at the next frame, and the old surface is the library's again when the call returns (4.17.10.2).
4.17.8.3 The still is shown like any element: `show("still", surface)` replaces the base's own picture of the stopped machine with the library's surface (Timewarp's frame). It is drawn only while the machine stands.

#### 4.17.9 `api->overlay->update`

4.17.9.1 `int update(int el)` says that the surface of the element was painted again: it is drawn again at the next frame. It returns 0 or `OVL_E_HANDLE`.

#### 4.17.10 `api->overlay->remove`

4.17.10.1 `int remove(int el)` takes the element away. It returns 0 or `OVL_E_HANDLE`.
4.17.10.2 The surface is the library's again when the call returns: the call waits for the UI task's next frame, 200 ms at most (the log says when that time ran out). A call from the UI task itself does not wait. The same holds for `show` with another surface than the element has now, and for the end of a library.
4.17.10.3 When the element is the still and the machine stands, the base takes its own still again from the frame the machine drew last, on the UI task. The picture under the menu is then the newest frame (a scrub position), not the frame of the menu stop. The same happens when a library that shows the still is released while the machine stands. While the menu closes (from the close key until the layer is the core's again, which includes the call of `menu_closed`) the base takes no new still: a still that goes then is retaken only if the machine stands when the menu is closed (Pause), and not at all when the machine runs again.

#### 4.17.11 `api->overlay->text`

4.17.11.1 `void text(const SvcSurface *surface, int x, int y, const char *s, uint16_t colour)` writes `s` into the surface with the base's fonts (the font of the menu's captions), its top left at `x`, `y`, in the RGB565 `colour`. The pixels outside the surface are not written. A string longer than 47 characters is cut.
4.17.11.2 The UI task writes it at the next frame, before the redraw. A shown element whose surface is written is drawn again then. A surface that is not valid is ignored, and so is a call while 16 others wait for the frame. `remove`, and `show` with another surface, drop the calls that wait for the old surface.

#### 4.17.12 `api->overlay->menu_hide`, `api->overlay->menu_show`

4.17.12.1 `void menu_hide(void)` acts as the person's close of the menu (the leave intent). `void menu_show(void)` acts as the menu key. The main loop takes the call over at its next pass. `menu_hide` while the menu is closed and `menu_show` while it is open do nothing. One request waits for the pass: when a library calls both in one frame, the last call counts.
4.17.12.2 `menu_hide` closes the menu only when no application's screens are open. While an application's session is open the leave intent goes to the session: `menu_hide` then closes the session, and the application with it unless it is resident, and the menu stays open (the person's RUN/STOP and the left arrow at the first tab do the same). A caller that has to leave to the machine whatever is open uses `exit_to_machine` (4.17.18).
4.17.12.3 The four requests `menu_hide`, `menu_show`, `exit_to_machine` and `goto_screen` share one slot: of the calls made before the main loop's next pass the last one counts.
4.17.12.4 While the menu is hidden, it is closed: the machine runs unless the library set Pause first (`api->ctx->set_paused`), the keys go to the machine, and the menu key brings the menu back.

#### 4.17.13 `api->overlay->error`

4.17.13.1 `const char *error(int code)` returns the words of an `OVL_E_*` code, for the log.

#### 4.17.14 `api->overlay->frame`

Since base 3.3, member after `error` (SDK 0.4.3). A library checks the `size` member of the table before it uses it.

4.17.14.1 `int frame(const char *name, const SvcSurface *surface)` paints the frame the machine drew last into `surface`, laid out as the element `name` lies on the canvas. It returns 0 or an `OVL_E_*` code.
4.17.14.2 The place of the element comes from the core's UI TOML, as for `show`. Only the part of the machine's picture that lies under the element's rectangle is painted, at the picture's place and Scale, in the colours of the still (4.17.15). The pixels of the surface that the picture does not cover are not written.
4.17.14.3 When the core has no picture rectangle (a C64-type core on no VIC scaler), the picture's place is the still element: the whole picture is scaled into the still element's rectangle, cut to the canvas, as the base does for its own still, and the part that lies under the requested element is painted. A table with no still element uses the requested element's own rectangle. When the picture does not lie under the element, nothing is painted and the call returns 0.
4.17.14.4 The source is the one of the base's own still: the newest complete frame of the host's still capture on a C64-type core, else the core's frame buffer.
4.17.14.5 The checks are those of `show`: `OVL_E_NOSERVICE` under the same condition, `OVL_E_NAME` when the core's table has no element of that name, `OVL_E_BYTES` when the surface or its `px` is null or `bytes` is not `w * h * 2`, and `OVL_E_SIZE` when `w` x `h` is not the element's size. `OVL_E_NOFRAME` is returned when there is no complete frame; every check is made before the first pixel is written, so the surface is then unchanged.
4.17.14.6 The library need not show the element first. The call does not take the element, so it returns no `OVL_E_TAKEN`.
4.17.14.7 The call paints in the calling task and never waits for the UI task, also when it is called on the UI task. The grab shares one capture buffer with the base's still: a lock orders the two, and the call may wait for it for the time of one grab. It is not allowed from an interrupt.
4.17.14.8 A library that changes the machine's frame while the machine stands (a scrub) paints the new frame with `frame` into the surface it shows as the still (4.17.8.3) and calls `update`.

#### 4.17.15 `api->overlay->still_palette`

Since base 3.3, member after `frame` (SDK 0.4.3). A library checks the `size` member of the table before it uses it.

4.17.15.1 `int still_palette(bool (*f)(uint8_t rgb[16][3]))` sets the source of the 16 colours the person sees. `f` writes the colours (RGB, 8 bits each) and returns true, or returns false when it has none.
4.17.15.2 The order of the sources is: the library's, the base's own (the person's palette on a C64-type core), Colodore. A source that returns false hands over to the next.
4.17.15.3 The base's own still and `frame` call `f` each time they take a frame, on the task that takes the frame. `f` runs inside the lock of the grab, so it must not call `api->overlay` (the call would wait for itself).
4.17.15.4 `f` null gives the source back to the base. There is one source per core: the last call counts.
4.17.15.5 The source belongs to the library that set it. When that library is released, the source goes, and `f` is not called again after the release returns: the release waits for a grab in flight.
4.17.15.6 The call returns 0, or `OVL_E_NOSERVICE` under the same condition as `show`.

#### 4.17.16 `api->overlay->canvas`

Since base 3.3, member after `still_palette` (SDK 0.4.5). A library checks the `size` member of the table before it uses it.

4.17.16.1 `int canvas(int *w, int *h)` writes the size of the canvas the elements are placed on now into `*w` and `*h`, and returns 0.
4.17.16.2 It is the geometry of the places, of `show` and of `frame`: the canvas of the video mode (640 x 480 at 480p, 720 x 576 at 576p, 800 x 600, 960 x 540 above, 6.16.4). It follows a change of the mode, so a library asks again when it lays out its surface. The size of an element stays its row's; the canvas is what the anchor and the offset count from.
4.17.16.3 It returns `OVL_E_NOSERVICE` under the same condition as `show`, or when `w` or `h` is null, and leaves `w` and `h` untouched.

#### 4.17.17 Patterns for an application's own screens (T256 D12, D13)

Since base 3.3, nothing new in the API: the base gives the means, and the application draws its own screen. Fullscreen, the picture of the machine and the update are the calls of 4.17.8 to 4.17.16 used in this way.

4.17.17.1 Fullscreen on and off: an element of the size of the canvas, shown and removed. The core's UI TOML declares an `[[element]]` that fills the canvas (6.16); `canvas` (4.17.16) says the canvas's size now, and the element's row is placed on it as the TOML says (an element of 960 x 540 is cut on a smaller canvas, so a core lists one element per canvas it supports or lays its own surface out with `canvas`). `show(name, surface)` turns the screen on, `remove(handle)` turns it off. The element shows while the menu is closed on a C64-type core, and while the machine stands on any other (4.17.3); the application keeps the menu closed with `exit_to_machine` (4.17.18).
4.17.17.2 The picture of the machine on and off: `frame(name, surface)` (4.17.14) paints the frame the machine drew last into the surface of an element, also while the machine runs. The picture is on while the application shows that surface, and off when it shows an opaque surface (4.17.17.1) or nothing; over a running C64-type machine the colour key `0x0841` lets the machine's own picture through, so an element that is all key colour is "picture on" without a copy.
4.17.17.3 Updating the picture: the application paints again (`frame` into the same surface, `text`, its own pixels) and calls `update(handle)` (4.17.9); the element is drawn at the next frame. A scrub that moves the machine's frame while the machine stands is the case of 4.17.14.8.
4.17.17.4 The base draws no bar, no time and no hint for an application (T256 D12): what stands on the element is the application's.

#### 4.17.18 `api->overlay->exit_to_machine`, `OVL_EXIT_PAUSE`, `OVL_EXIT_KEEP_APP`

Since base 3.4, member after `canvas` (SDK 0.4.6). A library or an application checks the `size` member of the table before it uses it.

4.17.18.1 `int exit_to_machine(uint32_t flags)` leaves for the machine. It files one request that the main loop takes at its next pass, and returns 0 or an `OVL_E_*` code (`OVL_E_ARG` for a flag it does not know, `OVL_E_NOSERVICE`). What the pass does is not an answer to the call.
4.17.18.2 In the one pass, in this order: the open application's session ends; the application ends too, unless it is resident and `OVL_EXIT_KEEP_APP` is set; Pause is set to `OVL_EXIT_PAUSE` (set: the machine stays stopped; not set: it runs, also when the person had paused it); the menu closes as the person's close does.
4.17.18.3 `OVL_EXIT_PAUSE` = 1: the machine stays stopped. `OVL_EXIT_KEEP_APP` = 2: a resident application stays loaded and runs in the background; without the flag it ends. A transient application ends in any case.
4.17.18.4 The call is refused as the person's close is when the core cannot run yet (the core's description names `needs_program` and no program is loaded): the menu stays, its status line says what the person's close says, and nothing ends and nothing is paused. A session that cannot let go (its task or a job does not stop within 4 s) is refused likewise, with the application's own sentence on the status line.
4.17.18.5 While the menu is closed there is no session and nothing to close: a resident application ends unless `OVL_EXIT_KEEP_APP`, and the machine is not touched (`OVL_EXIT_PAUSE` does nothing).
4.17.18.6 Return path of a transient application. The call only files the request. The base frees an application's image after the application's task and its jobs have returned, never at the call: the main loop waits for them for 4 s. The application therefore returns from its task entry (and ends its own jobs) after the call, and touches nothing of its own memory that it has handed to the base. A task that does not return keeps the image alive and the exit is refused (4.17.18.4). A call from an operation on the executor is safe too: the base takes the core lock before it frees the image, and the operation holds it.
4.17.18.7 The order against the other requests is 4.17.12.3.

#### 4.17.19 `api->overlay->goto_screen`

Since base 3.4, member after `exit_to_machine` (SDK 0.4.6). A library or an application checks the `size` member of the table before it uses it.

4.17.19.1 `int goto_screen(const char *target)` goes to a place of the menu or to an application. It files one request that the main loop takes at its next pass, and returns 0 or `OVL_E_ARG` (`target` null, empty or `OVL_TARGET_LEN` (48) characters or more) or `OVL_E_NOSERVICE`.
4.17.19.2 `target` is a place of the menu (`"F3"`, `"F3/online"`: the area key and a tab id, as `menu.screen` names them, 4.3.3.1) or an application (`"app:name"` or `"app:name:action"`, as `app.open` names them, 4.3.3.5).
4.17.19.3 In the one pass: the menu opens when it is closed; the session of another application ends (a resident application stays loaded, a transient one ends; the session of the application named by the target stays); the menu goes to the place, or the base files the `app.open` request, which the host takes at its next pass. When the Library is the place and an operation asked it to open on a folder (`lib.show`), the Library opens there.
4.17.19.4 A place the core does not have, or an application the apps folder does not have, is not an error of the call: the menu stays where it is and its status line says so. A session that cannot let go is refused as in 4.17.18.4, and the menu does not move.
4.17.19.5 Not while the core has not started and the menu is closed: nothing happens (the menu cannot open then).
4.17.19.6 A transient application that calls it to go to another application returns from its task entry afterwards (4.17.18.6). A resident one need not.
4.17.19.7 The order against the other requests is 4.17.12.3.
### 4.18 `api->wb` (`SvcWb`)

Since base 3.4.

The write-back service for libraries (T239.3 D51): what a core's game or program writes into a medium of the machine (a cartridge's flash, a battery RAM) goes back to the user's file, the way the base does it for its own drivers. A library registers one writable medium, reports its changes and asks for the write. Applications and the API ask for the write only: they register no medium. A library built before the group never reads it (its `LibApi` is smaller): test `LIB_API_COVERS(api, wb)` and the pointer.

4.18.1 The base has one writable medium per core. While another owner holds it (the base's cartridge driver, another library), `register_medium` refuses and changes nothing.
4.18.2 The person's setting "Save Changed Cartridge" decides, whoever asks: Off writes nothing, Ask asks at the next open of the menu, Auto writes (at reset, eject, core change, power-off, the menu, 2 s after the last reported change, and when `write_now` asks). A call never writes against the setting.
4.18.3 The write is the safe one: the new file under `NAME.tmp`, the file as the person first had it kept once as `NAME.bak`, then `NAME.tmp` becomes `NAME`.
4.18.4 The medium belongs to the image whose code registered it (as for `api->mem`). When the image is released the base writes what is pending (setting Auto), while the writer's code is still there, and drops the medium.
4.18.5 The operation `writeback.flush` (4.3.1, `SvcOps::call`, JSON-RPC) writes the registered medium of whoever holds it, as the setting says, and nothing else; `media.flush_for_core` writes every medium of the core. An application or the API uses these, not the group.
4.18.6 No call takes a `CoreCtx`. `register_medium`, `unregister_medium`, `write_now` and `pending` are for a task that may block. `mark` and `mark_aux` may also be called from an interrupt.

#### 4.18.7 `SvcWbFile`

Since base 3.4.

| Symbol | Contract |
|---|---|
| `SvcWbFile` | An opaque file of a writer: the old file or the new one (`NAME.tmp`). It is valid only during the writer's call. It is not an `SvcFile` of `api->fs`: do not hand it to `api->fs`, and do not close it. |

#### 4.18.8 `SvcWbDirty`

Since base 3.4.

| Member | Contract |
|---|---|
| `SvcWbDirty` | What has changed since the last write. |
| `SvcWbDirty.block_shift` | A block is `1 << block_shift` bytes of the image (13: 8 KiB). |
| `SvcWbDirty.nblocks` | The number of bits in `blocks` (1024). |
| `SvcWbDirty.blocks` | One bit for each block: the bit `b & 31` of `blocks[b >> 5]` says the block `b` changed. |
| `SvcWbDirty.aux` | Non-zero when the second part of the medium changed (the GMod2's EEPROM). |

#### 4.18.9 `SvcWbWriter`

Since base 3.4.

4.18.9.1 `int (*SvcWbWriter)(void *user, SvcWbFile *old, SvcWbFile *out, const SvcWbDirty *dirty, char *msg, int msglen)` rebuilds the file into `out` (`NAME.tmp`, empty) from `old` (the file now) and the image. It returns 0, or non-zero with the reason in `msg` (`msglen` bytes: the person sees it). The change stays pending on a failure.
4.18.9.2 `old` is null when the medium was registered with `may_create` and the file is not there. The writer calls nothing but `file_read`, `file_write` and `file_size` on the two files.
4.18.9.3 The base calls the writer on whatever task writes (the executor, the poll, the menu). The writer's image is the owner of what the writer takes from `api->mem`.

#### 4.18.10 `SvcWbMedium`

Since base 3.4.

| Member | Contract |
|---|---|
| `SvcWbMedium` | The medium a library registers. |
| `SvcWbMedium.size` | `sizeof(SvcWbMedium)`. A smaller size is refused with `SVCWB_E_ARG`. |
| `SvcWbMedium.path` | The file the user has: a full path of fewer than 520 characters (`FS_PATH_MAX`). Copied. |
| `SvcWbMedium.name` | What the question and the notes call the medium; at most 55 characters, cut after that. Copied. Null or empty: "the medium". |
| `SvcWbMedium.write` | The writer (4.18.9). Not null. |
| `SvcWbMedium.user` | The first argument of the writer and of `hold`. The library keeps what it points to for as long as the medium is registered. |
| `SvcWbMedium.may_create` | True: the file may not exist yet (a battery save the game has never written). The writer then gets a null `old`. |
| `SvcWbMedium.hold` | Optional. The machine must not change the medium while the writer reads it: the base calls `hold(user, true)` before the writer and `hold(user, false)` after it. |

#### 4.18.11 `SVCWB_OK`, `SVCWB_ASK`, `SVCWB_OFF`, `SVCWB_E_NOSERVICE`, `SVCWB_E_ARG`, `SVCWB_E_TAKEN`, `SVCWB_E_NOTMINE`, `SVCWB_E_FAILED`, `SVCWB_E_APP`

Since base 3.4.

| Symbol | Contract |
|---|---|
| `SVCWB_OK` | 0: done. |
| `SVCWB_ASK` | 1: `write_now` found a change and the setting is Ask: nothing was written, the person is asked at the next open of the menu. |
| `SVCWB_OFF` | 2: `write_now` found a change and the setting is Off: nothing was written. |
| `SVCWB_E_NOSERVICE` | -1: no core runs, or the service is not attached yet (the call came from `lib_register` before the core's start). |
| `SVCWB_E_ARG` | -2: a null or too long path, no writer, or a `size` that does not cover the members. |
| `SVCWB_E_TAKEN` | -3: another owner holds the medium. |
| `SVCWB_E_NOTMINE` | -4: the calling image has no registered medium. |
| `SVCWB_E_FAILED` | -5: the write failed (`msg` says why) or, for `register_medium` and `unregister_medium`, the pending change could not be written first. |
| `SVCWB_E_APP` | -6: `register_medium` was called by a caller that is no library: an application, or code that belongs to no loaded image. An application asks for the write (`write_now`); only an image of kind library registers a medium (T239.3 D51). |

#### 4.18.12 `api->wb->register_medium`

4.18.12.1 `int register_medium(const SvcWbMedium *m)` registers the calling image's writable medium. It returns 0, `SVCWB_E_NOSERVICE`, `SVCWB_E_ARG`, `SVCWB_E_TAKEN`, `SVCWB_E_APP` (the caller is no library) or `SVCWB_E_FAILED`. Nothing is changed unless it returns 0.
4.18.12.2 An image that registers again replaces its medium. What was pending for the old one is written first (setting Auto) and then dropped; when that write fails, the call returns `SVCWB_E_FAILED` and the old medium stays.
4.18.12.3 The call works from the core's start hook on. A call from `lib_register` of a library loaded at core start returns `SVCWB_E_NOSERVICE`.

#### 4.18.13 `api->wb->unregister_medium`

4.18.13.1 `int unregister_medium(void)` gives the medium back. What is pending is written first when the setting is Auto, else it is dropped. It returns 0, `SVCWB_E_NOTMINE`, or `SVCWB_E_FAILED` when the write failed (the medium is given back and the change is lost; the person sees a note).

#### 4.18.14 `api->wb->mark`, `api->wb->mark_aux`

4.18.14.1 `void mark(uint32_t off, uint32_t len)` reports that the bytes `[off, off + len)` of the image changed. `void mark_aux(void)` reports that the second part of the medium changed. Both are callable from an interrupt.
4.18.14.2 A call while no library's medium is registered does nothing. A report is never taken for a medium another owner holds.

#### 4.18.15 `api->wb->write_now`

4.18.15.1 `int write_now(char *msg, int msglen)` writes the pending change as the setting says (4.18.2). It returns 0 when the file was written or nothing is pending, `SVCWB_ASK`, `SVCWB_OFF`, `SVCWB_E_NOTMINE`, or `SVCWB_E_FAILED` with the reason in `msg`. `msg` may be null.
4.18.15.2 A file that cannot be written (write protected, a read-only mount) is `SVCWB_E_FAILED`; the change stays pending.

#### 4.18.16 `api->wb->pending`

4.18.16.1 `int pending(void)` returns 1 when the calling image's medium has a change the file does not have, else 0.

#### 4.18.17 `api->wb->file_read`, `api->wb->file_write`, `api->wb->file_size`

4.18.17.1 `bool file_read(SvcWbFile *f, uint64_t off, void *buf, uint32_t len)` and `bool file_write(SvcWbFile *f, uint64_t off, const void *buf, uint32_t len)` are all or nothing; a write at the end makes the file longer. `uint64_t file_size(SvcWbFile *f)` is the size of the file. They are for the writer, on the two files it is given.

#### 4.18.18 `api->wb->error`

4.18.18.1 `const char *error(int code)` returns the words of an `SVCWB_*` code, for the log.

## 5. Operation types

Since base 3.0.

The registry holds the operations, the state values and the events of the core and of the images. An operation has a name, up to four typed arguments and a handler.

### 5.1 Names

5.1.1 A name has the form `domain.verb`: letters `a-z`, digits, `_` and `.`, with at least one dot that is neither the first nor the last character.
5.1.2 A name has one owner. The first registration wins. A second one is refused, and the refusal names both owners.
5.1.3 An operation, a state value and an event may have the same name, but not two of one kind.
5.1.4 The owner of a registration is the name of the image.

### 5.2 `OpErr`

The result codes of an operation. A REST client and an application see the same code for the same failure.

| Symbol | Contract |
|---|---|
| `OPE_OK` | 0. The operation ran and succeeded. |
| `OPE_ARG` | -1. An argument is missing, unknown, given twice or out of range. The handler did not run. |
| `OPE_NOT_FOUND` | -2. No such operation, state value or event. |
| `OPE_IO` | -3. A read or write failed. |
| `OPE_FORMAT` | -4. The data has the wrong format. |
| `OPE_BUSY` | -5. The executor's queue is full, or the base is busy with another request. |
| `OPE_UNSUPPORTED` | -6. The call is not served here. |
| `OPE_FAILED` | -7. The handler ran and refused. The reason is in the message. |

### 5.3 `ArgType` and `ArgSpec`

`ArgType` names the types of an argument.

| Symbol | Contract |
|---|---|
| `AT_INT` | A number from `lo` to `hi`. |
| `AT_BOOL` | 0 or 1. The text `true`, `on`, `false` and `off` are accepted without regard to case. |
| `AT_ENUM` | An index from 0 to the number of labels minus 1. The label text is accepted without regard to case. |
| `AT_STRING` | Text of at most `maxlen` characters. It may be empty. |
| `AT_PATH` | A path of 1 to 519 characters. |
| `AT_SLOT` | An identifier of 1 to 31 characters of `a-z`, `0-9`, `_` and `:`. |
| `AT_BLOB` | Bytes the caller owns for the duration of the call. |

`ArgSpec` declares one argument.

| Member | Contract |
|---|---|
| `ArgSpec.name` | The name of the argument. Not empty, and unique in its operation. |
| `ArgSpec.type` | A value of `ArgType`. |
| `ArgSpec.lo` | `AT_INT`, `AT_ENUM`: the lowest value, inclusive. |
| `ArgSpec.hi` | `AT_INT`, `AT_ENUM`: the highest value, inclusive. It is not below `lo`. |
| `ArgSpec.labels` | `AT_ENUM`: the labels. Not null. |
| `ArgSpec.maxlen` | `AT_STRING`, `AT_PATH`, `AT_SLOT`: the longest text. |

The macros fill an `ArgSpec`.

| Symbol | Contract |
|---|---|
| `ARG_INT` | `ARG_INT(name, lo, hi)` declares an `AT_INT`. |
| `ARG_BOOL` | `ARG_BOOL(name)` declares an `AT_BOOL`. |
| `ARG_ENUM` | `ARG_ENUM(name, labels, count)` declares an `AT_ENUM` with `count` labels. |
| `ARG_STRING` | `ARG_STRING(name, max)` declares an `AT_STRING` of at most `max` characters. |
| `ARG_PATH` | `ARG_PATH(name)` declares an `AT_PATH`. |
| `ARG_SLOT` | `ARG_SLOT(name)` declares an `AT_SLOT`. |
| `ARG_BLOB` | `ARG_BLOB(name)` declares an `AT_BLOB`. |

5.3.1 An operation has at most `OP_ARGS_MAX` arguments. The argument list has to stay valid as long as the image is loaded.

### 5.4 The call

#### 5.4.1 `OpIn`

`OpIn` is one argument as the caller gives it.

| Member | Contract |
|---|---|
| `OpIn.name` | The name of the argument. A null name is refused with `OPE_ARG`. |
| `OpIn.s` | The text. An integer or enum argument parses it: a whole decimal number, or a label. |
| `OpIn.i` | The number. It is used when `is_int` is true. |
| `OpIn.is_int` | `true` when `i` is the value. |
| `OpIn.p` | The bytes of a blob argument. |
| `OpIn.n` | The number of bytes of `p`. |

5.4.1.1 The helpers make an `OpIn`: `in_int(name, v)`, `in_str(name, text)` and `in_blob(name, bytes, n)`.

| Symbol | Contract |
|---|---|
| `in_int` | Makes an `OpIn` with a number. |
| `in_str` | Makes an `OpIn` with text. |
| `in_blob` | Makes an `OpIn` with bytes. |

5.4.2 The registry checks a call before the handler runs. Every argument of the operation has to be given once, and no other. The value has to match the type: a text longer than `maxlen`, an empty path, a number outside `lo` to `hi`, an unknown label and a blob without bytes are `OPE_ARG`.

#### 5.4.3 `OpArgs`

`OpArgs` is what the handler receives: the checked arguments in the order of the `ArgSpec` list. An enum is its index.

| Member | Contract |
|---|---|
| `OpArgs.n` | The number of arguments. |
| `OpArgs.iv` | The values of the integer, boolean and enum arguments. |
| `OpArgs.sv` | The texts of the string, path and slot arguments. |
| `OpArgs.pv` | The bytes of the blob arguments. |
| `OpArgs.nv` | The lengths of the blob arguments. |
| `OpArgs.i` | `i(k)` is the integer value of argument `k`. |
| `OpArgs.s` | `s(k)` is the text of argument `k`. |
| `OpArgs.p` | `p(k)` is the bytes of argument `k`. |
| `OpArgs.len` | `len(k)` is the length of the bytes of argument `k`. |

The texts and bytes of `OpArgs` belong to the caller. They are valid for the call.

#### 5.4.4 `OpOut`

`OpOut` is what the handler fills.

| Member | Contract |
|---|---|
| `OpOut.msg` | The buffer for the reason or the answer, a text. |
| `OpOut.msglen` | The size of `msg`. It is `OP_MSG_MAX`. |
| `OpOut.more` | Set it to `true` when the handler had work and wants to be called again at once. Only a poll reads it. |
| `OpOut.data` | The buffer for returned bytes. |
| `OpOut.datacap` | The size of `data`. It is `OP_DATA_MAX`. |
| `OpOut.ndata` | The number of bytes the handler wrote into `data`. A value outside 0 to `datacap` is clamped. |

#### 5.4.5 `op_fn`

`op_fn` is the type of a handler: `int fn(CoreCtx *c, const OpArgs &a, OpOut &o)`.

5.4.5.1 `CoreCtx` is the context of the core. It is opaque to an image. Pass it on unchanged and do not read it.
5.4.5.2 The handler returns a code of `OpErr`.
5.4.5.3 The handler runs on the executor task with the core lock, when the menu or a transport calls it. It runs on the caller's task, without the lock, when `api->ops->call` calls it. A poll runs it on its poll task with the lock.
5.4.5.4 A handler is short, because it holds the core lock.

#### 5.4.6 `OpResult`

`OpResult` is what the caller gets.

| Member | Contract |
|---|---|
| `OpResult.code` | The code of `OpErr`. |
| `OpResult.more` | The `more` of the handler. |
| `OpResult.msg` | The message of the handler, or the reason of a refusal, at most 239 characters. |
| `OpResult.ndata` | The number of bytes in `data`, 0 when the operation returns none. |
| `OpResult.data` | The bytes the handler returned, at most 256. |

#### 5.4.7 `state_fn`

`state_fn` is the type of the getter of a state value: `const char *get(CoreCtx *c)`.

5.4.7.1 The getter returns the value as text. It returns null when there is no value (an empty slot).
5.4.7.2 The getter runs on the task that asks: the menu task, a transport, or an application. It does not take the core lock. It is short and does not block.
5.4.7.3 The text belongs to the getter. It stays valid until the next call of the getter.

### 5.5 Bindings

A node, a file action and a task call an operation with fixed arguments. `OpBind` says how each argument is filled.

#### 5.5.1 `BindKind`

`BindKind` names the kinds.

| Symbol | Contract |
|---|---|
| `BIND_FILE` | The path of the file the entry was on. The argument is text. Only a file action has a file. |
| `BIND_INT` | The number `i`. The argument is a number, and `i` is inside its range. |
| `BIND_STR` | The text `s`. A text argument takes it when it fits `maxlen`. An enum argument takes a label. |
| `BIND_ASK` | The number the person typed. This base does not accept it in a node or file action of an image. |
| `BIND_TEXT` | The text the person typed. A node takes it when its `attr` has `ask`. The argument is a string. |

#### 5.5.2 `OpBind`

`OpBind` is one binding.

| Member | Contract |
|---|---|
| `OpBind.arg` | The name of the argument of the operation. |
| `OpBind.kind` | A value of `BindKind`. |
| `OpBind.i` | The number of `BIND_INT`. |
| `OpBind.s` | The text of `BIND_STR`. |

The macros fill an `OpBind`.

| Symbol | Contract |
|---|---|
| `B_FILE` | `B_FILE(arg)` binds the file. |
| `B_INT` | `B_INT(arg, value)` binds a number. |
| `B_STR` | `B_STR(arg, text)` binds a text. |
| `B_ASK` | `B_ASK(arg)` binds the typed number. |
| `B_TEXT` | `B_TEXT(arg)` binds the typed text. |

5.5.3 The merge refuses an entry whose bindings name an argument the operation does not have, bind one argument twice, give a value of the wrong type or outside the range, or leave an argument of the operation unbound. The refusal names the entry.

### 5.6 Limits of the registry

| Symbol | Contract |
|---|---|
| `OP_ARGS_MAX` | The arguments of one operation, 4. |
| `OP_MSG_MAX` | The size of the message of a result, 240 bytes with the NUL. |
| `OP_DATA_MAX` | The bytes an operation may return, 256. |
| `OPS_MAX` | The operations of the registry over the base and every image: 400 in the shared app, `ops` of the `[sizes]` table of an app's TOML B otherwise. A registration past it is refused. |
| `STATES_MAX` | The state values of the registry: 320, or `states` of `[sizes]`. |
| `EVENTS_MAX` | The events of the registry: 8, or `events` of `[sizes]`. |

5.6.1 These three are the defaults of the headers. The app of a core is linked with the numbers of its own TOML B (`python3 tools/app_build.py table` lists every key of `[sizes]` with its default and its range), and the objects that depend on them are compiled for that core: the registry's arrays are part of the class. A registration past a maximum is refused with the message that names it (`OPS_MAX`, `STATES_MAX`, `EVENTS_MAX`).

## 6. The record

### 6.1 `LibRecord`

Since base 3.0.

6.1.1 `LibRecord` is what `lib_register` returns. Every list in it is a pointer and a count. A list that an image does not have is null with a count of 0.
6.1.2 The pointers point into the image. They stay valid as long as the image is loaded.
6.1.3 The base registers `ops`, `states` and `events` at load time. It reads the other lists when it merges the menu.
6.1.4 The menu merge runs at the start of the core and again when the media changed and the list of applications changed. It reads the record of every image loaded at that time, and the menu record of the image's file.
6.1.5 A contribution that cannot be placed is refused by name and left out. The other contributions are merged. The refusals are logged and shown on the System page.
6.1.6 The contributions are ordered by rank (the description of the core first, then libraries, then applications), then by the name of the image. The entries of one image keep their order.

| Member | Contract |
|---|---|
| `LibRecord.struct_size` | `sizeof(LibRecord)` of the base. The base sets it. The load is refused when it differs after `lib_register`. |
| `LibRecord.ops` | The operations the image registers (`LibOpDecl`). |
| `LibRecord.nops` | The count of `ops`. |
| `LibRecord.states` | The state values (`LibStateDecl`). |
| `LibRecord.nstates` | The count of `states`. |
| `LibRecord.events` | The events (`LibEventDecl`). |
| `LibRecord.nevents` | The count of `events`. |
| `LibRecord.nodes` | The menu nodes (`LibNodeDecl`). |
| `LibRecord.nnodes` | The count of `nodes`. |
| `LibRecord.file_actions` | The file actions (`LibFileActionDecl`). |
| `LibRecord.nfile_actions` | The count of `file_actions`. |
| `LibRecord.settings` | The settings (`LibSettingDecl`). |
| `LibRecord.nsettings` | The count of `settings`. |
| `LibRecord.poll` | The poll (`LibPollDecl`). |
| `LibRecord.tabs` | The tabs (`LibTabDecl`). |
| `LibRecord.ntabs` | The count of `tabs`. |
| `LibRecord.exports` | Since base 3.1. The call table of the library, found with `api->lib->find`. Null: none. |
| `LibRecord.exports_size` | Since base 3.1. The size of the table in bytes. At least 4 when `exports` is set. |
| `LibRecord.export_list` | Since base 3.2. The named tables of the library (`LibExportDecl`), found with `api->lib->find` by name and major (section 9). Null: none. A library built before 3.2 leaves it zero. |
| `LibRecord.nexport_list` | Since base 3.2. The count of `export_list`, at most 8. |
| `LibRecord.tab_layouts` | Since base 3.3. How the tabs of `tabs` are drawn: one string for each tab, in the order of `tabs`, written as a tab of the route writes it (`"split;title=Search;side=Results"`; 6.11.5). A null entry, or a null array, leaves the tab a flat list. A library built before 3.3 leaves it zero; one that must also run on a base before 3.3 writes it only when `LIB_RECORD_COVERS(out->struct_size, tab_layouts)` holds. |

### 6.2 `LibOpDecl`

Since base 3.0.

`LibOpDecl` declares an operation.

6.2.1 The base registers the operation under the name of the image.
6.2.2 The registration is refused when the name is not valid, the name is taken, `fn` is null, or the argument list is not valid (section 5.3).
6.2.3 When one registration of an image is refused, the base takes back all of its registrations and refuses the load.

| Member | Contract |
|---|---|
| `LibOpDecl.name` | The name of the operation, `domain.verb`. |
| `LibOpDecl.args` | The argument list. Null with 0 arguments. |
| `LibOpDecl.nargs` | The number of arguments, 0 to 4. |
| `LibOpDecl.fn` | The handler (`op_fn`). |

### 6.3 `LibStateDecl`

Since base 3.0.

`LibStateDecl` declares a state value.

6.3.1 The registration is refused when the name is not valid, the name is taken, or `get` is null.

| Member | Contract |
|---|---|
| `LibStateDecl.name` | The name of the state value, `domain.name`. |
| `LibStateDecl.get` | The getter (`state_fn`). |

### 6.4 `LibEventDecl`

Since base 3.0.

`LibEventDecl` declares an event.

6.4.1 The registry lists the event for the transports. `api->ops` has no call to emit an event or to subscribe to one.
6.4.2 The registration is refused when the name is not valid, the name is taken, the payload is not valid, or 8 events are registered.

| Member | Contract |
|---|---|
| `LibEventDecl.name` | The name of the event. |
| `LibEventDecl.payload` | The one argument the event carries. Null: none. |

### 6.5 `LibNodeKind`

The kind of a menu node.

| Symbol | Contract |
|---|---|
| `LN_OP` | A button. It calls `op` with `bind`. |
| `LN_SETTING` | Not accepted in a node. This base refuses it. Declare a setting with `LibSettingDecl`. |
| `LN_STATE` | A line that shows the state value `state`. |
| `LN_WIDGET` | Not accepted. This base refuses it. A tab declares its widget with `LibTabDecl`. |
| `LN_GROUP` | A heading that groups the nodes after it. |
| `LN_LIST` | Since base 3.5 (T262). A row that opens the rows of the state `state` as a list; `op` is called with the row the person takes (6.19). |

### 6.6 `LibNodeDecl`

Since base 3.0.

`LibNodeDecl` declares a node of the menu.

6.6.1 The merge refuses a node that has no label, that has a kind other than button, state line, group or list, or whose anchor does not name a tab of the route that takes nodes (a source of the F3 search, 6.20, stands at the tab `search`, which takes no other node).
6.6.2 A button needs an `op`, unless its `attr` has `goto` or `off`. A state line needs a `state`, unless its `attr` has `sub` or `chip`.
6.6.3 The operation of a button has to exist, and its bindings have to fit (section 5.5). A node cannot bind a file. `B_TEXT` is accepted when `attr` has `ask`.
6.6.4 The state of a state line has to exist.
6.6.5 The nodes of one anchor are ordered by rank, then `order`, then the name of the image, then the position in the list.
6.6.6 The id of a node is `id`, else `op`, else `state`, else `label`. The description of the core can hide a node or rename it by its id.
6.6.7 The menu holds at most 320 nodes. A node past that is refused.
6.6.8 A node whose text fields contain `$slot` is made once for each slot of the core (section 6.12).

| Member | Contract |
|---|---|
| `LibNodeDecl.anchor` | `area/tab` or `area/tab/group`. The area is the key of an area of the route (`F1`, `F3`, `F5`, `F7`, `F8`), the tab is the id of a tab. The group names a part of the tab's layout. Tabs with the widget info or browser take no nodes. |
| `LibNodeDecl.kind` | A value of `LibNodeKind`. |
| `LibNodeDecl.label` | The text of the row. |
| `LibNodeDecl.op` | The operation a button calls. |
| `LibNodeDecl.state` | The state value a line shows. |
| `LibNodeDecl.order` | The sort key inside the anchor. |
| `LibNodeDecl.when_state` | A condition. The row is offered only while it holds. Otherwise it is greyed with "Not available now" or the `why` text. The forms are `state`, `state == value` and `state != value`. The word `empty` stands for no value. |
| `LibNodeDecl.capability` | The capability the core needs. The node is refused when the core lacks it. |
| `LibNodeDecl.id` | The name a description can hide or rename the node by. A node without one has the operation or the state as its id. |
| `LibNodeDecl.bind` | The bindings of the operation, one for each of its arguments. |
| `LibNodeDecl.nbind` | The count of `bind`. |
| `LibNodeDecl.closes_menu` | `true` closes the menu after the operation succeeded. |
| `LibNodeDecl.attr` | How the node is shown and acts: `key;key=value;...` (section 6.7). Null: a plain row. |

### 6.7 The attribute text of a node

Since base 3.0.

6.7.1 The `attr` of a node is a list of words separated by `;`. A word is a key, or `key=value`. A key the base does not know is ignored.
6.7.2 A value that is a spec is resolved when the row is drawn. `@name` is the text of the state value `name`, empty when it has none. `@set.Key` is the value label of the setting `Key` of the core. `@info.Label` is a line of the information page of the host. `@session` is what the machine is doing. Any other text is the text itself. `spec|fallback` uses the part after `|` when the first part is empty.
6.7.3 The keys of a node are these.

| Key | Contract |
|---|---|
| `primary` | The button is drawn as the main action. |
| `link` | The row is drawn as a link. |
| `tone` | The colour: `muted`, `cyan`, `violet`, `amber` or `coral`. |
| `label` | A spec. When it resolves to text, the text replaces the label of the row. |
| `sub` | A spec. The second line of the row. A state line shows it as its value. |
| `chip` | A spec. A small text at the right of the row. A state line with a chip is a display row. |
| `none` | A spec. While it resolves to empty, the row is greyed and shows `empty`. |
| `empty` | The text a row with `none` shows while it is empty. The default is `none`. |
| `hide` | A spec. The row is not listed while it resolves to text. |
| `off` | The reason. The row is greyed, and RETURN shows the reason. |
| `why` | The reason shown while the `when_state` of a button does not hold. |
| `goto` | `area` or `area/tab`. RETURN goes there. A button with an operation runs it first. |
| `key` | One character: a letter hotkey that runs the node while the screens are open. `F1` to `F8`: a function key of the tab, listed in the key legend. |
| `nokey` | The key is not shown on the row. |
| `bkey` | One character. A hotkey of a button on its own page, shown before the label. |
| `ctrl` | One character. The Ctrl key with it runs the node. |
| `legend` | The short label of an `F` key in the key legend of the tab. |
| `ask` | A question. RETURN asks the person for a name, then calls the operation with the name bound by `B_TEXT`. |
| `ask_state` | A state value whose text is the start of the name `ask` offers (up to `ask_max` characters). |
| `ask_max` | Since 0.5.1. The most characters `ask` holds, 1 to 255; default 24 as before. |
| `ask_any` | Since 0.5.1. The text of `ask` is free (a search, a message): every printable character, not only those of a file name. Without it `ask` is a name and refuses `/ \ : * ? " < > \|` as before. |
| `confirm` | A question. RETURN asks Yes or No before the operation runs. |
| `job` | The operation runs as a job with a progress display. With `ask`, the person types the name first. The row shows the label of the node (or its `busy` text) while the job runs (since 0.5.1 on a screen, a tab with a layout, the screen stays and this row carries the words and the percentage; elsewhere the page of the job is one row, and its title says the words once, not as the title and the row), and a percentage with a bar only once the operation has reported one (`job_pct` above 0): a job that reports no progress shows its words alone. This holds for every node with `job`, also for one that a library built against base 3.4 declares: with base 3.5 such a node that never reports progress no longer shows `0%` and an empty bar. |
| `busy` | Since base 3.5 (T262). A spec (6.7.2). The words the row shows while the node's job runs, in place of the label; resolved at every draw, so a state can change it. Empty: the label. Without `job` it has no effect. |
| `pick` | A label: the person picks a folder before the job starts. Or `@state` (since base 3.5, T262): RETURN opens the rows of the state as a list (6.19.3) and runs the node's operation with the chosen value bound by `B_TEXT`. A value that starts with `@` is the list; any other text is the label of the folder pick. |
| `more` | A node of kind `list` (6.19). The operation that asks the library for more rows. |
| `search`, `find`, `fields` | A node of kind `list` at the tab `search` of F3 (6.20). The name of the source, the operation that searches, the labels of its fields. |
| `view` | One of `log`, `edid`, `trace`, `info`, `scan`, `help`. RETURN opens the viewer on text the application keeps in memory. |
| `rename` | RETURN renames the file the Library browser has under the cursor. |
| `open` | An operation name. The base calls it, without arguments, when the person opens the page the node is on. |

### 6.8 `LibFileActionDecl`

Since base 3.0.

`LibFileActionDecl` declares an action the Library offers for files of certain endings.

6.8.1 The merge refuses an action that has no label, no operation, no extension, or an extension that does not start with a dot.
6.8.2 Each extension gets one action. The extension is compared without regard to case. The actions of the image come after the actions the description has for that extension.
6.8.3 The operation has to exist. The bindings may be `B_FILE`, `B_INT` and `B_STR`, and have to fit (section 5.5). `B_ASK` and `B_TEXT` are refused.
6.8.4 The core holds at most 40 file types and 96 actions over all contributors. An action past that is refused.
6.8.5 The id of an action is its label. The description of the core can hide it or rename it.
6.8.6 The operation receives the path of the file for `B_FILE`.

| Member | Contract |
|---|---|
| `LibFileActionDecl.ext` | The list of endings, each with a leading dot: `.d64`, `.g64`. |
| `LibFileActionDecl.next` | The count of `ext`, at least 1. |
| `LibFileActionDecl.label` | The text of the action in the file menu. |
| `LibFileActionDecl.op` | The operation the action calls. |
| `LibFileActionDecl.bind` | The bindings of the operation. |
| `LibFileActionDecl.nbind` | The count of `bind`. |
| `LibFileActionDecl.size_in` | A list of sizes in bytes. When set, the action is offered only for files of exactly one of these sizes. |
| `LibFileActionDecl.nsize_in` | The count of `size_in`. 0: every size. |
| `LibFileActionDecl.closes_menu` | `true` closes the menu after the operation succeeded. |

### 6.9 `LibSettingDecl`

Since base 3.0.

`LibSettingDecl` declares a setting. It lands in the tab that lists its group, and is kept in the settings file of the core under its key.

6.9.1 The merge refuses a setting that has no key or no `apply`, whose key is taken (the settings file keys by name, without regard to case), or that has fewer than 2 or more than 16 values.
6.9.2 With `values` set, `nvalues` labels are offered, the label with the index `def` is the default, and the value passed to the operation is the index.
6.9.3 With `values` null, the range `lo` to `hi` is offered with the numbers as labels. `def` is the default value itself, inside the range. The value passed to the operation is the number.
6.9.4 `apply` has to be an operation with exactly one argument named `value`, of type `AT_INT` or `AT_ENUM`, whose range holds all values of the setting.
6.9.5 The base calls `apply` with `value` when the value of the setting changes.
6.9.6 The core holds at most 16 contributed settings. A setting past that is refused.
6.9.7 The id of a setting is its key. The description of the core can hide a setting or give it another key.

| Member | Contract |
|---|---|
| `LibSettingDecl.group` | The group of the setting. A tab that lists the group shows it. Null: the ungrouped settings. |
| `LibSettingDecl.key` | The key. |
| `LibSettingDecl.values` | The labels, or null for a range. |
| `LibSettingDecl.nvalues` | The count of `values`. |
| `LibSettingDecl.lo` | The lowest number of a range. |
| `LibSettingDecl.hi` | The highest number of a range. |
| `LibSettingDecl.def` | The default: an index with `values`, a number with a range. |
| `LibSettingDecl.apply` | The operation that takes `value`. |

### 6.10 `LibPollDecl`

Since base 3.0. The service of a library that loads after the core has started, the stop when the library goes and the refusal at load are base 3.2.

`LibPollDecl` declares an operation the base calls every period.

6.10.1 A poll is served from the moment its library has registered until the library is released or unloaded. The base stops the poll before it removes the operations of the library.
6.10.2 The operation has no arguments. It has to exist when the poll is added.
6.10.3 The task calls the operation with the core lock, every `period_ms`. The call waits until the core has started, and while the machine is frozen for the menu unless the poll declares `when_frozen` (6.10.9).
6.10.4 When the handler sets `more`, the task calls it again at once. After 20 ms in a row it sleeps one period.
6.10.5 A poll is declared in `LibRecord.poll` or in the `[poll]` of the menu record of the file, not in both. A library that declares both is refused at load.
6.10.6 A poll the base cannot serve is refused: no operation of that name, an operation with arguments, a period of 0, or no free slot. A library loaded after the core has started is then refused at load, and nothing of it stays. A library loaded before the core starts stays loaded; its poll is dropped and the base writes a line to the log.
6.10.7 The base serves 16 polls at once after the core has started. The polls of the libraries loaded before start come through the menu merge, which takes 12.
6.10.8 A library that has a poll is never unloaded to make room (4.10.2.7).
6.10.9 A poll that declares `when_frozen` keeps running with the menu open, beside the executor and the core hooks. It protects the state it shares with them by its own mutex (`api->sync`). The poll of the `[poll]` of a menu record has no such field and waits for the menu to close.

| Member | Contract |
|---|---|
| `LibPollDecl.op` | The operation. Null: no poll. |
| `LibPollDecl.period_ms` | The period in milliseconds. At least 1. |
| `LibPollDecl.when_frozen` | `true`: the poll also runs while the machine is frozen for the menu. `false`: it waits for the menu to close. Since base 3.3. |

### 6.11 `LibTabDecl`

Since base 3.0.

`LibTabDecl` adds a tab to an area of the route.

6.11.1 The tab is appended after the tabs the area has.
6.11.2 The merge refuses a tab that lacks an area, an id, a name or a valid widget, whose area the route does not have, whose id the area has already, whose number is taken, or when the area has 8 tabs.
6.11.3 The tabs of a record are merged before the nodes, so a node may anchor to a tab of its own image.

| Member | Contract |
|---|---|
| `LibTabDecl.area` | The key of the area, for example `F5`. |
| `LibTabDecl.id` | The id of the tab, `a-z`, `0-9` and `_`. |
| `LibTabDecl.name` | The text of the tab. |
| `LibTabDecl.widget` | What the tab shows: 0 information lines, 1 the file browser, 2 the media slots, 3 the settings, 4 the tasks, 5 the system entries, 6 only what is contributed. A value above 6 is refused. The widgets 0 and 1 take no nodes. |
| `LibTabDecl.groups` | The settings groups the tab lists, separated by commas. Null for another widget. |
| `LibTabDecl.n` | The printed number. 0: the next free number. |

6.11.4 A tab of a menu record may carry `layout` and `ui` as a tab of the route does (the keys of `tools/descrec.py`: `title`, `crumb`, `side`, ...). The record then holds the string in its table of contributed tabs, and the base reads it as `LibRecord.tab_layouts` of that record. `LibTabDecl` itself did not change.
6.11.5 The string is the layout name, then `;` and the parameters, as the description record writes a route's own tab. An empty name with parameters keeps the tab a flat list with those parameters.
6.11.6 Base 3.3 widened an entry of the table of contributed tabs from 24 to 28 bytes (the layout string), for every record version. The record version did not change, because base 3.3 is not released: a record with a `[[tab]]` that an older `descrec.py` compiled has to be compiled again with the tool of the SDK that carries this base.

6.11.7 A tab of a menu record may carry `show = "@state"` (since base 3.5, T262). The tab is on the strip only while the state `state` has a value; while the state is empty the tab is off the strip, its digit does nothing, a goto to it lands on the first tab of the area that is shown, and the area key returns to that tab. The base looks at the state with every key that ran an operation and every two seconds, as for `hide` (6.7), so a tab appears after the operation that sets the state and goes after the one that clears it. When the tab on screen goes, the screen moves to the first tab of the area that is shown.
6.11.8 A tab keeps its number while it is off: the digits of the other tabs do not change, so a strip may read `1 2 4` while tab 3 is off. `show` is written beside `layout` and `ui` in the `[[tab]]`; the record holds it as the parameter `show=@state` of the layout string (6.11.5), alone on a tab with no layout name (a flat list). A library built before this key sees an unknown parameter and shows the tab always.
6.11.9 A tab that is off is not counted for the legend "1-n TAB" of an application's screens either (the count is of the tabs that are shown).

### 6.12 The slot expansion

Since base 3.0.

6.12.1 A core names its drive slots with capabilities of the form `drive_a`, `drive_b`.
6.12.2 A node declaration with `$slot` in its anchor, label, operation, state, condition or id, a binding text with `$slot`, a file action with `$slot` in its label, operation or a binding text, or a setting with `$slot` in its group, key or operation is made once for each slot, with `$slot` replaced by the slot name.
6.12.3 A core with no slot gets none. A core has at most 8 slots.

### 6.13 `LibExportDecl`

Since base 3.2.

`LibExportDecl` is one table of a library under a name. The record lists them in `export_list`. Section 9 has the rules.

| Member | Contract |
|---|---|
| `LibExportDecl.name` | The name of the table: 1 to 15 characters of `a-z`, `0-9` and `_`. It is not the name of another loaded library. |
| `LibExportDecl.major` | The major version of the layout of the table, at least 1. |
| `LibExportDecl.table` | The table: a block that starts with a `uint32_t` `size`, in the image or in memory the library keeps. It is not null. |
| `LibExportDecl.size` | The size of the table in bytes, at least 4. `find` hands it back as `table_size`. |

### 6.14 TOML A: the description of the gateware

6.14.1 TOML A describes the gateware of a bitstream as capabilities. It is a description record of version 7. It has no menu table and no section 1 to 19. It has the three sections 20 (`[io]`), 21 (`[identity]`) and 22 (`[[block]]`), each once.
6.14.2 A TOML A file holds `[io]`, `[identity]` and at least one `[[block]]` and nothing else. A table or key that is not named here is refused. TOML A holds no logic: no sequence, no operation, no setting and no text for a person.
6.14.3 `[io]` holds the two base addresses of the I/O space as 32 bit numbers: `iobase` and `u2p_io_base`.
6.14.4 `[identity]` is the standard block of the bitstream. `id` is four printable ASCII characters. `block` names the block that holds the identity registers. That block is owned by the base and has the readable properties `id` and `build`. One TOML A names a block of the core instead: the host's own (`coreapp/gateware/host.toml`), whose standard registers are `trx_io` in layer 2 (block `core_trx_io`); the readers take an identity block with owner `core` and read no property of it (T239.3 D39; the start-up check reads the running core's record, never the host's). `build` is the build word of the bitstream. It is optional in the source file (a record that is decoded and compiled again carries it). `cor_pack` writes it when it packs the core: the AXSS word of the bitstream, which the gateware's BUILD register answers. A source that names another build than the bitstream's is refused, a bitstream without an AXSS word is refused, and `cor_pack check` refuses a `.cor` whose record carries another build than its bitstream.
6.14.5 A `[[block]]` is a capability. `name` is 1 to 31 characters of `a-z`, `0-9` and `_`, once per record. `module` is the gateware module, and so the driver library, with the same characters. `owner` is `"base"` or `"core"`. `address` is a 32 bit number. `size` is 1 to 0x100000 bytes, and the block ends below 4 GiB. Blocks of one TOML A do not overlap; a block of the core is checked only against the blocks of its own TOML A. There is no other table of the registers the base drives; the base's blocks are the ones of TOML A with `owner = "base"`, and a `[[window]]` that covers one is refused (4.15.19.2).
6.14.6 A module whose registers serve several drivers is several blocks, one per register range. Each block has its own map.
6.14.7 The map of a block is its `[[block.property]]` tables. `name` is once per block. `offset` is in bytes from the address of the block, and the register lies inside the block. `width` is 8, 16 or 32 bits. `dir` is `"r"`, `"w"` or `"rw"`. `count` is optional (1 to 0x10000, default 1): a property with a count is a run of `count` registers of its width, element `i` at `offset + i * stride`, and the whole run lies inside the block. `stride` is optional and only for a run: the bytes from one element to the next, from the width in bytes to 0x100000, default the width (consecutive registers). A stride above the width leaves room between the elements for other runs, so words that interleave are one run each: the 1541's parameter RAM is `param_addr` (offset 0x1000) and `param_len` (offset 0x1004), 256 elements each with the stride 8, and the curve registers of reSID8 are `curve_base` (offset 4) and `curve_offset` (offset 6), 2 elements each with the stride 4. Fields and states hold for every element. Two properties share a byte only when no direction is in both of them, and a run shares the bytes of its elements only: runs whose elements do not meet may share the range between their first and last byte. The driver table has `_COUNT` and `_STRIDE` for a run (the stride in bytes, the width when the map gives none) and takes the element's index in `_RD(i)` and `_WR(i, v)`, which add `i * _STRIDE` to `_ADDR`; `rtl_check` checks each element at `offset + i * stride`. A block has at most 64 properties.
6.14.8 A property has fields: `[[block.property.field]]` with `name` (once per property), `shift` and `bits`. A field lies inside the width and shares no bit with another field. A property has at most 32 fields.
6.14.9 The possible states are `[[block.property.state]]` for the whole register and `[[block.property.field.state]]` for a field: `name` and `value`. The value fits the width or the bits. A name and a value are once per owner. An owner has at most 64 states.
6.14.10 A record has at most 64 blocks, 512 properties, 512 fields and 1024 states.
6.14.11 The map of a module lives with its driver library. A map file is named `<name>.map.toml` and holds `map = "<name>"` and `[[property]]` tables as in 6.14.7. A map carries every register of that module that one of our gateware decodes, with the directions of the RTL; a gateware that has fewer registers lists its own properties in its TOML A in place of the map (a block takes a map whole or lists its own). The drive's WD177x window (+0x1800) is the map `wd177x` and the block `drive_wd` of its own, next to `drive_a` with `cbm1541`: a drive without the WD177x (`c1541_drive`: the VIC-20's) has no such block, and the WD part of the library links only where the TOML A has it. A block says `map = "<name>"` in place of its own properties. `tools/descrec.py compile desc.toml out.bin --maps DIR` reads the map files of `DIR` (repeat the option for more), and the record carries the finished map. A name that no map file has is refused. The SDK ships the maps of the shared drivers in `maps/`: `c64cart` (the cartridge page, with the 43 byte descriptor `desc` as a run), `c64cart_regs`, `cbm1541` (the drive block, with the run `dirty` of 128 bytes and the two runs `param_addr` and `param_len` of the parameter RAM), `dma` (the DMA window, the run `mem` of 65536 bytes), `gcr_coder`, `icap`, `sid` (the reSID8 filter control of the SID library: `fcsel`, `auto`, `bypass`, `curve_base` and `curve_offset` are runs of the two engines), `uci` and `wd177x` (the WD177x registers of the drive). The parameter words of the drive interleave as the gateware has them: per half track two, the address of the track (`param_addr`) and `(length - 1) | bit time << 16` (`param_len`, with the fields `length_m1` and `bit_time`), 128 half tracks per side, side 1 from element 128 (byte 0x400).
6.14.12 The reader refuses a record that breaks a rule of 6.14.2 to 6.14.10 with a sentence that names the section, the block and the property. `tools/descrec.py` and the application read the same rules.
6.14.13 The service compares the standard block with the record at start. It reads the properties `id` and `build` of the identity block and compares them with the `id` and the `build` of the record: the four characters of the ID, byte 0 first, and the build word. This is the one check that stays at run time. A pair that does not match is told in the start notice (it says which of the two differs and with what), and the core is not released: the menu and the settings open, the machine stays held. A block that cannot be read is refused the same way. A record without a build word (one that no packer has stamped) is refused the same way: ID plus build is the proof, pairing by packaging is none. "Not released" holds on every path: the release of a start, a reset, a reload and a media mount refuses with the same reason, and the service poll, the polls of the libraries and the write-back task do not start.
6.14.14 The addresses of the application's build come from TOML A as linker symbols. `tools/descrec.py ldfrag toml_a.toml out.ld [--maps DIR]` writes a linker-script fragment: `__iobase` and `__u2p_io_base` from `[io]`, and for each block `__blk_<block>` (its address) and for each property `__reg_<block>_<property>` (the block's address plus the offset). `IOBASE` and `U2P_IO_BASE` of the headers are these two symbols, and a driver library takes the address of a register from its symbol. No address is a `-D` option or a literal. The build links the fragment as an input file (the Makefile variable `IOFRAG`). An application built for RISC-V without it does not link. The check of `[io]` and `[[block]]` is the one of a record; a fragment without `[identity]` is accepted, and it is no record.
6.14.15 The shared application and the host application link the `ldfrag` of the host's TOML A, `coreapp/gateware/host.toml`: their Makefiles write it to `$(OUTPUT)/io/iofrag.ld` before any source is compiled, and rewrite it only when its content changes. The ICAP registers are the symbols of the blocks `icap_id`, `icap_data`, `icap_enable`, `icap_count`, and the partition control of the block `partition` (`__reg_partition_release`). A core's own application links the `ldfrag` of its TOML A in place of it (`make IOFRAG=<file>`).
6.14.16 `tools/rtl_check.py check --design DIR --toml-a FILE [--maps DIR]...` compares TOML A with the RTL of the bitstream, code to code. It reads the source files that the design's build script (`build.tcl`) names, parses the VHDL and Verilog of the modules the request reaches, and runs the request through them as a stimulus: the address, write or read, the data unknown. A guard that depends on the request is decided, a guard that depends on state is open and both ways are followed. A module is followed when a strobe from the request reaches it. Per block it checks that every byte lies in what the RTL decodes (a write, a read, a request forwarded to another module, or a module a strobe reaches for sure), and per property that every byte of its width is written (dir `w`) and read (dir `r`), `rw` both. A `dir` that is part of what the RTL does passes. The options `--top` (the file's top module, default `top`) and `--bus` (`module.signal` of the request, default `ultimate_sys.io_req`) name where the request is. A block with owner `core` is checked like a block with owner `base` (decoded, every property read and written as its `dir` says) wherever the design decodes its bytes itself (a design that holds layer 2 as well: `c64_basic`: `trx_sys.v`, `pix_layer.v`, `c64_text.v`); where the RTL hands its bytes to the core over a port, the block is a window and only the hand-over is checked. The options `--own-core` and `--core-in-design` of SDK 0.4.5 chose this per run; they are still accepted and change nothing. Exit 0: every block holds; otherwise one line per finding.
6.14.17 An RTL pattern the check does not know is an error with file and line, never a pass. A request that the RTL forwards over a handshake (a held address, write flag and data, and a toggle) is run through the module that reads those ports, with its own state unknown (the byte handshake of `dma_win`); the module behind that one is not followed, and a decode on a register copy of the held address (a pipeline) is refused as a pattern the check does not know. A module behind a handshake that passes the request through registers before it decodes it (`trx_sys.v`: `rq_addr`, `c_addr`, `op_a`) is run with that pipeline: a register that takes the request's bits on unchanged, under a guard that carries no taint of the request, has the request's value where the module decodes, and what another master loads into the same register is left out. A VHDL for-generate with a constant range (numbers, generics with a default) is unrolled, and a vector declared with an ascending range has its leftmost element as its most significant bit (the element `i` of `(0 to n)` is the bit `high - i`). A Verilog `generate for` with a named block (`ent[i].wtag`) is unrolled and a Verilog function of one assignment is replaced by its expression. A block of TOML A with a pattern the check does not know stops that block only: every such block is listed. A request that a module holds in its own registers and decides one clock later is followed (`flash_gate.vhd`: `p_valid`, `p_addr`, `p_data` and a state machine; a register that only copies the request is the check's rule for a held request, a flag that the request itself sets is not): a property there fails the check. A VHDL source file (`.vhd`, `.vhdl`) whose base name says ultisid, copper, lp_filter or sampler is never opened (clean room: Gideon's files; a directory name or a Verilog file does not count). The check needs no simulator, no synthesis and no board.
6.14.19 A partial core has no request record: the host's decode hands its window of the I/O space to it over plain ports. `rtl_check.py check --bus MODULE:write=P,read=P,address=P,data=P,response=P --window BASE:SIZE` runs the stimulus into the one instance of `MODULE` in the top file (`--top`, a Verilog file) through those five ports. `BASE:SIZE` is the part of the I/O space the host hands to them (the host's fact, from its decode); a block outside it is refused, and the address port gets the block's address minus `BASE`. `--list` takes the core's source list (`cores/<id>/core_sources.tcl`, relative to `--design`), and `--design` is the directory the paths in it start from (`host/`, the working directory of Vivado). The VIC-20's: `--design host --list ../cores/vc20/core_sources.tcl --top layer2_vc20 --bus vc20_core:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata --window 0x101A0000:0x10000 --toml-a cores/vc20/app/gateware.toml`; with `--list gideon_vhdl.tcl` and a second `--bus layer2_vc20:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata --window 0x10020000:0x4000:full` it also checks the block `drive_a` (the top module's own ports are the bus, `cores/vc20/layer2_vc20.v` holds them one register stage; `:full` puts the whole address on the port). The rest is 6.14.16 and 6.14.17.
6.14.20 A description (TOML B) names a register of the core's TOML A instead of writing its number. The top-level key `toml_a = "<file>"` of the description is the path of the TOML A from the description's directory; it is read when the description is compiled and is not part of the record. The keys `reg`, `reg_*` (also inside a table such as `reg_bind`), `fb_base` and `fb_stride` then take a string: `"block.property"` is the register number (the address of the property minus the address of the identity block, which is offset 0 of the core's window), `"block.property.state"` is the value of a state of that property. A name that is no property or state, a register below the identity block, a name without `toml_a`, and `toml_a` without a file name to look from are refused with a sentence. The record is the same bytes as with the numbers written out. `compile`, `compile_files`, `compile_checked` and `compile_text` (with `path`) do it; `cor_pack --desc` and `pack_cores_mc.sh` need no option.
6.14.21 The host's TOML A is `coreapp/gateware/host.toml`; it is checked with `check --design host --list build.tcl --list gideon_vhdl.tcl --toml-a coreapp/gateware/host.toml --maps app/sdkpkg/maps` (the host reads its Gideon VHDL list through the reference core's script, so the second list is named). `--list FILE` is repeatable, `--define NAME` names a `define symbol of the build (none for the release). A property byte counts as written when a data value of the request reaches a register, or when an assignment is guarded by the write flag and by the address of the request and the same address as a read does not run it (a strobe register: the write itself is the effect). A guard on the write flag alone is no write. A block with owner `core` that the host's RTL decodes itself is checked like a base block; the host hands its core blocks over (no option, D43). Hand-over is checked for every byte: the request must leave over an output port of the groups `cw_`, `dma_`, `drv_io_`, `gcr_io_` or `sidcv_` of the base (a strobe port that is 1 for this address, or a register driven under the address), and a data port of the same group must carry the request's data; its properties are the core's contract. A VHDL if-generate is decided with the generics' defaults, a Verilog one with the parameters' defaults, and an instance that gives such a generic another known value gets the module parsed again with that value (the generate branches follow it); the check fails only when the value is unknown (the parameter lists of the top-level Verilog file are read for this).
6.14.22 What `check` follows since V-1b: a concatenation target (`{a, b[3:0]} <= x`, one assignment per part), a Verilog `for (i = A; i <= B; i = i + 1)` (also `<`, and `i - 1` with `>=`/`>`; unrolled like the VHDL loop), an entity's attribute declarations, and a generic that an instance sets away from the default an if-generate was decided with (the module is parsed again with the instance's value, the generate branches follow it). The deny list is one function (`denied`): a VHDL file whose base name says `ultisid`, `copper`, `lp_filter` or `sampler` is refused by `read_source`, the one place that opens RTL; the check prints the denied files of the source list, and a block of such a module is checked at the port only (the hand-over rule).

6.14.23 What `check` follows since V-1c: Gideon's `io_bus_splitter` is modelled by the check itself (its architecture is not read): the request with write and read cleared goes to port i when the address bits `g_range_hi` downto `g_range_lo` are i (`reqs(i) => sig` is an indexed formal), and the answer is the OR of the answers of the ports the address can select. Behind it follow the ITU with its console UART, USB (nano's program RAM), Ethernet, the 1541 drive and the UCI (`io_bus_bridge2` and `synchronizer_gzw`, whose outputs are its inputs: the vector crosses unchanged; `command_protocol`). Also read now: a procedure declared in a process (inlined at its call, in-parameters only), aliases (in an architecture and in a process), a VHDL block statement (its signals and statements are the module's), `x <= e when rising_edge(clk);`, `x(y'range)`, a scalar constant of a package with an expression, a record constant with `(others => c)`, a function with a parameter list in a declaration, a text generic (unknown), a slice whose position is state (unknown), and the parameters and localparams of the top Verilog file (the value of `HOST_CAPS`). A RAM entity (`dpram`, `dpram_8x16`, `dpram_8x32`, `pseudo_dpram_8x32`; the vendor architecture has shared variables) is a write sink: its enable port is 1 for the request and its data port carries the data. The taint of a part of a vector (a slice or an index) is that of those bits: a concatenation, a bridge's synchronizer and an instance port keep the address, data, read and write of the request apart. A write is a register or a RAM that takes the request's data, an assignment the data decides, or an assignment guarded by the address and the write flag that the read of the same address does not run; an answer of the bus (a `t_io_resp` signal, `ack`, `nc`) is none of them. A RAM takes a write only on the request's address, and a run or block may not be larger than the RAM (`dpram_*`, `pseudo_dpram_8x32`: 2 KB; `dpram`: `g_depth_bits`, `g_width_bits`). A request that a module holds in registers it sets itself (`flash_gate.vhd`: `p_valid`, `p_read`, `p_addr`, `p_data`, assigned for sure under a guard that is no address decode) is decoded in the next clock: the registers take the request's value and taint, and their own assignments are no writes (the flash window is in the host's TOML A). A flag whose assignments set it to another value than it has now is, as response data, a register. `--bus MODULE:...` with MODULE the top module itself (`--top`) takes the ports of the top module as the request; a register stage of the top file (`always @(posedge clk) q <= expr;`, the reset released) holds it one clock; `--window BASE:SIZE:full` puts the whole address on the address port; `--bus` and `--window` may be repeated (a block goes to the bus whose window holds it). A property that is a run with a stride (`count`, `stride`) is checked element by element (the overlay palette). `RTL_CHECK_TRACE=1` raises the error with its traceback. Followed since V-1d: the trx_io pages of `trx_sys.v` (a pipeline with generate blocks and hierarchical names) and what it hands on over continuous assignments: a module behind a handshake that drives an output port from the held request (`p2_we = ost == O_RUN && op_we && op_a[15:12] == 4'd2`, `p2_addr`, `p2_wdata`) hands the write flag with the address decode to the module that reads that port (`trx_engine`, `trx_debug`), which is run on it; the write flag is the one-bit register that holds the request's known value, and only a port whose continuous assignment carries it is a strobe (a module that reads an address or data alone is not run). Followed since V-1e: a VHDL for-generate with a constant range and vectors declared ascending (`gcr_codec.vhd`, the GCR coder window of `c64_basic`, which the check now agrees with). Followed since V-1f: a module that a followed module behind a handshake holds (`trx_sys.v` hands the debugger's strobe, address and data to `c64_core.v`, which holds `trx_dbg.v`): at a level behind a handshake an instance is run when a one-bit port it reads carries the address and the held write flag, as a strobe from a continuous assignment does. A Verilog expression that one operand decides (`x && y` with x false, `x || y` with x true, a conditional with a known condition) is a constant and has no taint, so the data behind the compare of another address (`ev <= ... || (wcmd && cmd == 8'h01)`) is no write of this address; a select of a genvar (`{1'b1, 6'h02, gu[0]}`) is a constant. A register that a write sets or clears whatever the data is (`14'h0402: sb_err <= 1'b0` under `op_we`) is a write when its assignment lies in an arm of a case that the request's address selects, in a module behind a handshake with a register pipeline, and the read of the same address does not run it; what a read runs is collected in every module the request reaches. With TRX64-Ultimate's `c64_basic` and its TOML A of commit b57405a6 (33 blocks, 150 properties) all blocks agree, including byte 3 of `trx_dbg_run.owner`, `cyc_cmp`, `cmp_en`, `trx_statebus.status` and `trx_debug.ipc_set`. Not followed: a Verilog generate-for that is no counting loop, a Verilog function with more than its one assignment, the UCI window of the host (it acks and answers 0).

### 6.15 Where the rows of the base stand: `[[group_route]]` and `[[place]]`

Since base 3.3.

6.15.1 The base adds rows to the menu of every core: Picture, Scale, Smoothing, Frame follow, Volume, Tone, Persist medium mounts, Home directory, Enter home on startup, Menu opens at, the hotkeys, and the look, joystick and mouse rows. The tab, the heading, the setting group and the place among the other rows of each come from records, not from code.
6.15.2 The base's own record is `coreapp/base_ui.toml`. It holds one `[[group_route]]` per block (`group`, `area` F1 to F8, `tab`, `heading`, and for a page `label` and `page = true`) and one `[[place]]` per row and one `[[core_setting]]` per row: the row itself, with the same keys as a row of a core's UI TOML (`key`, `label`, `values`, `list`, `range`, `default`, `text`, `maxlen`, `show_state`, `apply`).
6.15.3 A `[[place]]` finds its row by `op` (the operation of the row, `Setting::apply`) and, when one operation serves several rows, by `key` (case does not matter). A place with a key wins over a place for the operation alone. Its `route` names the block, its `group` is the setting group the row gets, its `order` is the place among the placed rows.
6.15.4 The rows are visited in the order of the numbers; equal numbers keep the order in which the base adds the rows. The rows that are not placed keep their positions.
6.15.5 The UI TOML of a core (`menu.toml`) may contain `[[group_route]]` and `[[place]]` tables too (sections 23 and 24 of its record, which need no `[core]`). A route of an existing `group` replaces the base's route; another is added, at most 16 in all.
6.15.6 A place of the core names an operation and a key that the base's table has: it changes only what it names, and the rest stays as the base's table has it. A place of another operation or key is added to the table, at most 64 in all. A place without a key is a row of its own: it does not change the keyed rows of the same operation (picture.apply, scale.apply, smooth.apply, follow.apply and home.apply have only keyed places in the base's table), so a core names the key to move one of them.
6.15.7 A place that names a route which neither table has is refused when the menu is built: the log has the line `[menu] refused: place of "<op>": the route "<route>" is in neither the base's table nor the core's`, and the row stays where it was.
6.15.8 `cor_pack.py` refuses a place whose `op` the application does not export, with the `--ops` list. The menu simulator builds the menu from the same records, so a place changed in `menu.toml` shows at the next reload.
6.15.9 A row of the base has its label, key, kind, values and default in `coreapp/base_ui.toml`, not in code. The application's extend functions (`picture_extend`, `volume_extend`, `tone_extend`, `persist_extend`, `home_extend`, `hk_extend`, `baseset_extend`) take the row from the table `tools/gen_base_ui.py` makes of it (`coreapp/src/base_rows.inc`, `menu_base_row` in `menubuild.h`) and keep only what a row cannot say: whether a core gets the row, a default that the core's own map or the gateware sets (the hotkey map, the Scale default Fit on the VIC path), the values of a list that changes at run time, and the condition of a row that depends on another row.
6.15.10 A list that changes at run time names its `pick` in the row, in the base UI TOML as in a core's UI TOML (a text row's `pick` names the state that holds its list; a choice row's names the list that fills its values): `video.modes` (the resolutions the screen takes, Picture), `hotkeys.modifiers` and `hotkeys.keys` (the names of the modifier presets and of the keys the core's keyboard has). The `values` of such a row are what it shows until the list has filled it. The record carries the name as the last value string before the lock state (flag 16 of the row's time code), and the reader hands it to `Setting::pick`. A core's choice row may name `hotkeys.keys` or `hotkeys.modifiers`: the menu then shows that list as the row's values (the list of the base row that names it), and the row's default is an index into it. `video.modes` is the base's Picture row alone; `descrec.py` and the record reader refuse it, and any other name, in a core's row. The key `provider` does not exist.
6.15.11 The base takes the keys and the operations of Picture, Scale, Smoothing and Frame follow, which `descrec.py` refuses in a core's `[[core_setting]]`. The rows of a core's own function in the hotkey map and of its hotkey tasks are the core's: the base gives them the shape of its hotkey rows and the core names them.
6.15.12 A core whose TOML A holds only the blocks of its own gateware names, in the `[app]` table of its TOML B, `toml_a` (its file, beside the TOML B) and `base_blocks` (names of blocks of the base's TOML A: the package's `gateware/<base_toml_a>`, `gateware/shared.toml` when `base_toml_a` is absent, or `--base-toml-a`; a core that runs on the host bitstream names `base_toml_a = "host.toml"`, the host's TOML A). `python3 tools/app_build.py build` links the app against `descrec.merge_toml_a` of the two: the core's `[io]` and `[identity]`, the named base blocks and the core's blocks. A name the base's TOML A does not have, a name the core's file has too, and two different `[io]` tables are refused with a sentence; the overlap rule of a record applies to the result. Nothing else of the base's TOML A is in the driver table, the linker fragment or the link (`out/blocks.txt` lists the blocks).
6.15.13 The Volume row and the Tone row exist only where the TOML A of the link has the block `audio_master` (6.17.5.7): the host's master volume and tone bytes. `volume_extend` and `tone_extend` copy the descriptor unchanged when `DRV_HAS_audio_master` is not defined, so a core whose bitstream does not decode those two bytes shows neither row. A core that declares a Volume or a Tone of its own keeps it, as before.

### 6.16 The elements of the overlay service: `[[element]]`

Since base 3.3.

6.16.1 An element is a rectangle a library paints itself and shows with `api->overlay->show` (4.17). Its name, its place and its size are not the library's: they come from `[[element]]` tables of the core's UI TOML (section 25 of its record, which needs no `[core]`).
6.16.2 The base's own record, `coreapp/base_ui.toml`, holds two: `still` (the whole canvas, marked `still = true`) and `paused` (the PAUSED tag, top right).
6.16.3 An `[[element]]` has `name` (`a-z`, `0-9` and `_`, 1 to 15 characters, once), `anchor` (`top_left`, `top_mid`, `top_right`, `left_mid`, `center`, `right_mid`, `bottom_left`, `bottom_mid` or `bottom_right`), `dx` and `dy` (the offset from the anchor in canvas pixels, right and down positive, default 0), `w` and `h` (the size in canvas pixels, the size of the surface) and optionally `still = true` (at most one still in a table).
6.16.4 The canvas of the TOML is 960 x 540. The compiler (`tools/descrec.py`) and the reader of the record (`descrec.cc`) refuse an element whose rectangle is not inside it, with the element and its place in the sentence. On the board the place is made again at every frame on the canvas of the video mode (640 x 480 at 480p, 720 x 576 at 576p, 800 x 600, 960 x 540 above): the anchor and the offset count from that canvas's edges, the size stays the TOML's, and what lies beyond a smaller canvas is cut off. The base's capture of a stopped C64-type core is fitted to the still's size cut to the canvas and painted with the palette the person set; that of a core that draws its picture on the pixel layer has the place and the scale of the running picture on the canvas (the part that lies on it). A library's surface for the still is its row's size, cut off at the right and below on a smaller canvas.
6.16.5 An element of the core with the name of a base element replaces it (the core places the PAUSED tag elsewhere); another is added, at most 16 in all (`OVL_ELEMENTS_MAX`; a core's TOML B sizes the maximum at link time through its `[sizes]` table). One that does not fit is told in the log (`[menu] refused: element "<name>": <why>`) and is not applied.
6.16.6 The elements are drawn in the order of the table: the base's first, then the core's in the order of its file. The menu simulator draws the places of the elements from the same records.

### 6.18 The key table: `[[key]]`

Since base 3.3.

6.18.1 A menu record may carry a key table (section 18 of the record, descrec.h): rows that give a key an operation or take it back, tried in the order of the record on a core's tabs, where an application's screens end and the menu's own keys begin.
6.18.2 A row is a table `{ layout = ..., key = ..., op = ... }`. Exactly one of `layout` (the layout name of the tab that is shown) and `node` (the id of the tab that is shown) says where the row is for. Exactly one of `key` (a name, `ctrl+<letter>`, one printable character or `any`) and `codes = [lo, hi]` says which keys it takes. `do` is `op` (the default when `op` is given), `decline` (the key goes on to the next row) or `swallow`. `value`, `pass_key`, `status`, `status_ok`, `close`, `directive`, `direct` and `when` (a state; `!state`: while it is empty) are for an op row.
6.18.3 `legend = "Label"` (since base 3.5, T262; a record with a legend table is not read by a base before 3.5) puts the row into the key legend at the foot of the tab it is for: the key's name and the label in capitals (`CTRL+O CLEAR`, `X GO ON`). The label is 1 to 16 printable characters. Only an op row of one key can have it (not a range, not `any`, not a decline or a swallow). The row is in the legend only while its `when` holds. The record writes the labels in a table behind the rows; a record that has no `legend` is byte for byte the record it was. A base from before this key refuses a record with the table (the length of the section does not fit), so a record that must also load there leaves `legend` out.
6.18.4 The legend is that of a list tab or a screen of the core's menu. The file browser's own legend, and the legend of an application's screens, do not list the rows of this table.

### 6.17 The driver table: TOML A at build time

6.17.1 The application is linked against one TOML A. `python3 tools/descrec.py drivers desc.toml OUT_DIR [--maps DIR_OR_FILE ...]` checks the file by the rules of 6.14 and writes two files into OUT_DIR: `drvtab.h` and `drvtab.c`. The build runs it before any source is compiled and writes a file only when its text changes, so an unchanged TOML A recompiles nothing. The shared app is linked against `coreapp/gateware/shared.toml`, whose blocks are the addresses it has today.
6.17.2 `drvtab.h` holds, for each block `B`: `DRV_HAS_B` (1), `DRV_B_ADDR`, `DRV_B_SIZE` and `DRV_B_OWNER` (0 base, 1 core). It holds the blocks of the base and of the core alike.
6.17.3 For each property `P` of the block: `DRV_B_P_ADDR` is its address. `DRV_B_P_RD()` reads it, for `r` and `rw`; `DRV_B_P_WR(v)` writes it, for `w` and `rw`. Both are as wide as the property (8, 16 or 32 bits). A property with a count (6.14.7) also has `DRV_B_P_COUNT` and `DRV_B_P_STRIDE` (the bytes from one element to the next, for a driver that addresses an element through an interface of its own), and its macros take the index of the element: `DRV_B_P_RD(i)` and `DRV_B_P_WR(i, v)`; `DRV_B_P_ADDR` is element 0. A driver that writes a property its map calls read-only does not compile. The raw-offset calls of the CoreIo API (`cart_rd`, `cart_wr`, `drv_rd`, `drv_wr`, `hw_read_id`) address through `DRV_<block>_ADDR` plus the offset and carry no direction check.
6.17.4 For each field `F` of the property: `DRV_B_P_F_SHIFT` and `DRV_B_P_F_MASK`. For each possible state `S` of the property or the field: `DRV_B_P_S` and `DRV_B_P_F_S` are its value. A driver names a register, a bit and a value only by these names.
6.17.5 A block the TOML A lacks has no macro. A driver that serves a block that only some bitstreams have tests `#ifdef DRV_HAS_B`.
6.17.5.1 An application linked against a TOML A without the ICAP loader (the block `icap` of the shared app, or the blocks `icap_id`, `icap_data`, `icap_enable` and `icap_count` of the host's TOML A; `hw.cc` names the registers through `HW_ICAP_*` for either) has no ICAP access: no partial port (`hw_partial` returns null), no return to the host (`hw_return_to_host` refuses), no expansion port release, and the board probe's ICAP byte is not read, so `board_detect()` takes the profile of a standalone installation.
6.17.5.2 The ICAP loader is one block and the partition control is another. The block `icap` (0x10060600, 0x1C bytes, map `icap`) holds the loader: `id`, `data`, `enable` and the word counter `count0` to `count3`. The block `partition` (0x10060620, 2 bytes, map `partition`) holds `decouple` (field `request` in bit 0, field `effective` in bit 1) and `release` (field `port` in bit 0). Each of the two is a block with a map of its own, as 6.14.11 has it.
6.17.5.3 An application linked against a TOML A with `icap` and without `partition` can load the loader's ID and write the IPROG sequence, and nothing else: `hw_partial` returns null (a partial load needs both blocks), `hw_release_expansion_port` does nothing, and `hw_return_to_host` skips the decouple write before the IPROG.
6.17.5.4 The board probe has `partition_linked()`. It is a fact of the link, not a read of the board: `hw.cc` answers true exactly when `DRV_HAS_partition` is defined. The base class answers true, so a test probe that flips only the other reads keeps today's behaviour.
6.17.5.5 `board_detect()` takes the MultiCore profile when the ID byte of `icap` is 0x7A and (`partition_linked()` is true or the hand-over page is valid). Every other answer is the monolith profile. The MultiCore profile has `can_return_host` set, `flash_cfg` clear and the host's `boot.cfg`; the monolith profile has `can_return_host` clear, `flash_cfg` set and the ROM directory `/flash/roms`.
6.17.5.6 A loader without `partition` and without a valid hand-over page is a standalone installation: no host started it, so there is no way back and the settings live in the flash. The `why` text of the profile says `ICAP 0x7A, no partition, no hand-over: standalone`. With a valid page the same link is the MultiCore host's and `Return to host menu` exists.
6.17.5.7 An application linked against a TOML A without the block `audio_master` (0x10100550, 2 bytes, map `audio_master`: `volume` at +0, `tone` at +1, both write only) writes no master volume and no master tone (`CoreIo::master_volume` and `CoreIo::master_tone` do nothing), and the base adds no Volume row and no Tone row to any core (6.15.12).
6.17.5.8 A board probe runs only when the app's driver table has the block it asks of (`BoardProbe::blocks()`, `BB_*` of `board.h`): the ICAP loader, `drive_a`, `cmd_if`, `iec_status`, and for the cartridge, the ring and the REU the blocks `c64cart`, `c64_debug` and `dma`. A capability whose block is not there is struck without a read.
6.17.5.9 The standard block's property `caps` is read only for the probe of the cartridge and the ring (`BB_CART`, `BB_RING`): a TOML A whose standard block has no `caps` links, and `hw.cc` answers 0. An app linked without the block `cart_regs` of the base has no cartridge-port registers (0.5.1; before it, it did not compile): `cart_rd` reads 0, `cart_wr` goes nowhere, `stop` and `mode_reset` do nothing, so `hw.cc` neither stops nor holds the machine (`tools/core_move.py sdkgaps` compiles the base's sources against a table without each optional block, one at a time).
6.18.5 A library's menu record (`u2a_pack --menu`) may carry `[[key]]` rows as a core's record does (since base 3.6, T262 D4): `key` or `codes`, `op`, `do`, `value`, `when`, `legend` and the flags of 6.18.2, scoped by `layout` or `node`. While the library is loaded the menu merges its rows into the key table behind those of the core, in the order the libraries loaded, tried in that order for the tab that is shown; the rows go when the library is unloaded. `op` names an operation that is registered when the key is pressed (the library's own, normally). At most 32 rows of all libraries together are merged; one over that is refused with a line in the log. A library's record still carries no `[[window]]` and no route; an application's menu record carries no key table. A base before 3.6 refuses a file whose menu record has a key table, so a library that must also load there leaves it out.

6.17.6 Two names that make the same macro (the block `a` with the property `b_c`, and the block `a_b` with the property `c`) are refused when the table is generated.
6.17.7 `drvtab.c` holds the table of the blocks, `drv_blocks[]` with `drv_nblocks`: name, module, owner, address and size. A native test redirects every register access with `-DDRV_PTR_HEADER='"drv_fake.h"'` (`test/drv_fake.h` of the package, `tests/drv_fake.h` in this repository: one array for the address space), so a test reads back what a driver wrote.
6.17.8 The shared drivers take their blocks from the table: the cartridge port and the built-in drive A (`hw.cc`; without `drive_a` in the TOML A the app has no drive), the debug register (`dmasvc.cc`), the ICAP loader and the partition control (`hw.cc`) and the master volume and tone bytes (`hw.cc`).
6.17.9 Every source writes every block (D53; it replaces D50's rule that only the owner writes). `drvtab.h` gives the `_WR` macros of every block, the base's (`owner = "base"`) like the core's, to every source: a core's own library and its `[app]` sources, the SDK's shared libraries and a `.u2a` library built with `lib.mk`. The registers a core has are the ones its TOML A names. The c64cart table drops the cartridge's cached lines itself after `ram_write` and `image_write` (9.5.3).

### 6.19 The list node: `LN_LIST`, `pick`

Since base 3.5 (T262).

6.19.1 A node of kind `list` (`kind = "list"` in a menu record) shows the rows a library makes at run time, with no node declared for a row. In its tab it is one row: the label of the node and, as its value, how many rows the state holds now (0.5.1: the `+` row and the `!` notes are not counted; `None yet`, or the text of its `empty`, while there are none). RETURN opens the rows as a page of their own: a list that scrolls, with the title of the node, that takes UP, DOWN, PAGE UP, PAGE DOWN, HOME, END, RETURN and `<-`. It has the `when_state`, `hide`, `why` and `empty` of every node. The kind needs a `state` (the rows) and an `op` (what a row does), and takes no `bind`; the merge refuses it without them, and when the state, the operation or the operation `more` is not registered.
6.19.2 The state holds the rows as text, one row a line; the fields of a line are separated by TAB and every field after the first may be left out:

| Field | Contract |
|---|---|
| 1 `value` | What the row is. A choice passes it as `answer` (4.14.14); a list passes it as `answer` too. It may be empty. |
| 2 `label` | The text of the row, up to 61 characters. Empty: the value. |
| 3 `right` | The second column, up to 39 characters (a size, a signal strength, a year). |
| 4 `tone` | `muted`, `cyan`, `violet`, `amber` or `coral`. The row's dot and its right column are drawn in that tone (the same five tones as every status row); `list_tone` of the menu controller reads it. `disabled` (0.5.1) greys the whole row, name, dot and right column, as a row that cannot be taken is drawn; RETURN on it still calls `op`, the library decides. |
| 5 to 7 | Up to three lines the row says about itself. The list page shows the lines of the selected row, one under the other, in the SELECTED panel at the right of the rows (the panel of the F3 browser); the foot line does not repeat them. The rows stay one line each: name, the right column in its tone. |

A line that starts with `!` is a note: shown, never taken (a search that is still running). A line that starts with `+` is the last row, the one that asks for more: its text after the `+` is its label (empty: `More`). Empty lines are skipped. The text is read from the state again about every half second while the page is up, so a job that appends rows makes the list grow; the cursor stays on its row number.
6.19.3 RETURN on a row of a list calls `op` with the arguments the operation declares by name: `index` (`AT_INT`, the row, 0 for the first, a note or the `+` row counted) and `answer` (`AT_STRING`, the value of the row). The page stays open; the answer of the operation is the status row, a prompt or list the operation asks for (4.14.14) replaces the page. RETURN on the `+` row calls the operation named by the node's `more` (the same two arguments) and reads the rows again; a list with no `more` does nothing on that row. `<-` closes the page. For `pick = "@state"` the page is the same list, a row closes it, and the node's own operation runs with the value of the row bound by `B_TEXT` (a node that has the other `pick`, a label, is the folder pick of a job as before; the two are told apart by the `@`).
6.19.4 The rows cost the text only: the base copies the text of the state (at most a quarter of the free heap; a longer text is cut at the end of a line and the status row says so) and reads a row from its line when it is drawn. A list of 200 or of 5000 rows needs no node, no state for a row and no number fixed in the base. The older limit of 40 rows of a choice list is gone.
6.19.5 A tab may hold several list nodes; each opens its own state. A library built before base 3.5 declares none and loads and runs as it did: the kind, the keys and `ask_list` are additions.

### 6.20 A source of the F3 search: `search`

Since base 3.5 (T262).

6.20.1 A library offers itself as a source of the Library's search with a node of kind `list` anchored at `F3/search` (the tab with the id `search`) with `ui = { search = "Name", find = "domain.verb", fields = "A,B" }`. The node does not stand in the tab: the base reads it. `search` is the name the source has in the Source row, `find` the operation that searches, `fields` up to four labels of extra text fields (1 to 24 characters each, separated by commas). `state`, `op` and `more` are those of the list (6.19): the rows of the results, what a row does, what asks for more. The merge refuses a source without a name or without `find`, with more than four fields, or with an operation or a state nobody registered; at most 6 sources are kept (`CONTRIB_SEARCH_MAX`). A source is offered only while its `when_state` holds.
6.20.2 With no source the search tab is what it was: RETURN or `/` asks for a part of the name, and the search runs over the media. With at least one source, RETURN still asks for a part of the name (0.5.1; the name the form holds is offered and the idle tab shows it), and `/` opens one form instead. Its rows: `Source` (`This device`, then every source; RETURN or RIGHT takes the next one), `Name contains` (RETURN types it), one row for each field of the source (RETURN types it) and `Search`. `<-` closes the form. Since 0.5.1 `<-` on the results of a source (6.19) is not a cancel: it is the form again with its texts and a row `Results` before `Search`, the cursor on it; RETURN there opens the results on the row they were left on, `<-` leaves the form and gives the results back. A new search or a change of the tree gives them back too. For `This device` the search runs as before and its hits stand in the tab.
6.20.3 For a source the base calls `find` with one string argument named `query` (`AT_STRING`, up to 200 characters): the text of `Name contains`, then for each field of the source a TAB and the text of that field, in the order of `fields` (an empty field is an empty text). The operation answers with the rows in the state of the node. With `job` in the node's `ui` the base runs `find` as a job (the call repeats while it answers `more`, the busy words are the node's `busy` or `Searching <name>`, `<-` cancels); without it the call runs at once. When the call has returned, the rows open as the page of 6.19 under the title of the node, `op` takes the row that the person chooses. A failed call is a line in the status row and no page.
6.20.4 A source that is registered by a library built before base 3.5 does not exist: the base before 3.5 refuses a node of kind `list` ("a kind this base does not know").

## 7. Limits

| Limit | Value |
|---|---|
| Images loaded at once | 12 |
| Operations, state values, events | 400, 320, 8 |
| Arguments of an operation | 4 |
| Jobs at once | 4 |
| Sockets at once | 16 |
| Menu nodes | 320 |
| Rows of a list or a choice | the memory budget (a quarter of the free heap), no fixed number |
| Library sources of the F3 search, fields of one | 6, 4 |
| Contributed settings | 16 |
| Values of one setting | 16 |
| File types, file actions | 40, 96 |
| Polls | 16 after the core has started, 12 from the libraries loaded before |
| Tabs of one area | 8 |
| Named tables of one library | 8 |
| Entries of a folder in the file picker | 160 |
| Image size | 1 MiB |
| Heap an image holds through `api->mem` | the heap itself, or the `heap_peak` figure of its budget (section 8) |

## 8. The budget and the account of an image

### 8.1 Declaring a budget

8.1.1 An image may declare a budget: ceilings for its size, for the heap it holds and for what it registers.
8.1.2 The build declares it in one place, the makefile of the image: `LIB_BUDGET` for `lib.mk`, `APP_BUDGET` for `app.mk`. The value is a list of `key=number` words, for example `LIB_BUDGET = image=8192 heap_keep=2048 heap_peak=4096 ops=3 settings=2`. `u2a_pack.py --budget key=number` does the same by hand.
8.1.3 The packer writes the budget into the file: a record directly behind the header, and the header flag `U2A3_FLAG_BUDGET`. The dependencies, the image and the other parts follow at the offsets the header names.
8.1.4 A file with a budget needs base 3.2. The packer adds the dependency `base>=3.2` to the file, or raises a lower `base` dependency to it. A base older than 3.2 refuses the file.
8.1.5 The packer refuses an image that is over its own `image` figure, before it writes the file. It cannot know the registrations of `lib_register`: the loader checks them (8.4).
8.1.6 A figure is a number from 0 to 4294967294. The figure 0 is a figure: the image declares that it brings none of that kind. `heap_keep` above `heap_peak` is no budget: the packer and the loader refuse it.
8.1.7 These are the keys.

| Key | What it limits |
|---|---|
| `image` | The size of the image in bytes: payload and zeroed memory (bss), without the alignment. |
| `heap_keep` | The bytes the image holds through `api->mem` when it is idle, as a promise. The loader checks the image and this figure together against the room of the heap before it places the image (8.4.3). It is no ceiling and the base reserves nothing for it. |
| `heap_peak` | The most the image holds at once through `api->mem`, as a ceiling. The sizes asked for count, not the headers of the blocks. `realloc` holds the old and the new block together while it runs. The base reserves nothing for it: the memory is taken when it is asked for. |
| `ops`, `states`, `events` | What `lib_register` registers: operations, state values, events. |
| `nodes`, `file_actions`, `settings`, `tabs`, `polls` | What the record `lib_register` returns and the menu record of the file declare together. |

### 8.2 What an open figure means

8.2.1 A key left out has no figure of its own. The file holds `U2A3_BUDGET_NONE` for it.
8.2.2 The limit is then the limit of the platform: for the image, 1 MiB (`U2A3_IMAGE_MAX`); for the peak of the heap, the heap of the core; for the permanent heap, no figure (nothing is checked); for operations, state values and events, the room that is left in the registry (5.6); for nodes, file actions, settings and tabs, the room of the menu merge (section 7); for polls, the room of the poll slots (6.10).
8.2.3 A file without a budget loads as it did before base 3.2. Its figures are all open.
8.2.4 A budget record of a later minor may be longer. The loader reads the figures it knows and ignores the rest.

### 8.3 The limits of the tables

8.3.1 The loader checks what an image brings against the room that is left in the tables, with or without a budget. It counts what the images loaded already take.
8.3.2 The registry holds 400 operations, 320 state values and 8 events, over the base and every image. The loader checks `lib_register`'s counts against what is free.
8.3.3 The images together may contribute 16 settings, 96 file actions and 320 menu nodes. Of one area they may add tabs until the tab strip holds 8.
8.3.4 The loader does not see what the description of the core takes of these tables. The check is a necessary condition. A contribution the menu merge cannot place is refused by name at the merge (section 6), as before.

### 8.4 The refusal

8.4.1 An image over a figure of its budget, or over the room of a table, is refused at load. The loader returns the code `LIB_BUDGET` and a sentence that names the image, the figure and the limit.
8.4.2 Nothing of the image stays. Its registrations, its poll, its blocks and its memory are given back (8.5.4).
8.4.3 The loader checks in this order. The image size is checked before any memory is spent. The counts of the menu record are checked before any memory is spent. The permanent heap figure and the image are checked against the room of the heap before the image is placed. The peak is checked while `lib_register` runs. The counts of both records are checked after `lib_register` returns and before the first registration.
8.4.4 A request for memory that would pass the `heap_peak` figure returns null. While `lib_register` runs, the first such request also refuses the load, even if `lib_register` returns 0. After the load the request returns null and the base writes one line to the log for the image.
8.4.5 The sentence reaches the screen, the System page and the log like every other refusal of the loader. These are the forms:

| Over | The sentence |
|---|---|
| the image | `Library "mylib" is refused: its image is 5120 bytes with its bss, over its declared budget of 4096 bytes.` |
| a declared figure | `Library "mylib" is refused: it brings 7 operations, over its declared budget of 5.` |
| a table | `Library "mylib" is refused: it brings 5 settings, the table holds 16 and 12 are taken.` |
| a tab strip | `Library "mylib" is refused: it brings 3 tabs to area F5, the tab strip holds 8 and 6 are taken.` |
| the heap, at load | `Library "mylib" is refused: lib_register asked for 200 bytes of heap with 0 held, over its declared heap peak of 100 bytes.` |
| the room of the heap | `Library "mylib" is refused: it keeps 1500000 bytes of heap and its image takes 4112, the heap has room for 900000.` The code is `LIB_NO_ROOM`: a library that is evicted or an application that is released may make room. |

### 8.5 The account

8.5.1 Everything the heap gives for an image is on the account of that image: its own block (code, data, bss and the alignment), the blocks of `api->mem`, the handles of `api->fs` and the menu record of the file.
8.5.2 A block of `api->mem` goes to the image whose code takes it: the image whose `lib_register` runs, the image whose operation or state getter the registry runs (whoever called it), or the application that is open. An operation that calls an operation of another image takes its own blocks on its own account and the other's on the other's account. The calling task has its previous owner again when the call returns (8.5.8).
8.5.3 Unloading a library, or releasing an application, gives back its whole account: heap free before the load equals heap free after the unload.
8.5.4 A load that is refused gives back its whole account too.
8.5.5 The base reports the account at the start of the core in the log line `[lib] N loaded, M refused; images A bytes, accounts B bytes, heap free X -> Y`. `A` is the image and bss of the images that stay. `B` is everything the heap gave the images and has not got back, with the block headers of the base. `X - Y` is `B` and the block headers of the heap itself.
8.5.6 A poll is the one thing that is not on the account of its image. The poll slots keep the task of a slot for the next image that polls (1.3.6). An image with a poll leaves that task in the pool when it goes, and the heap free after the unload is lower by the task and its stack until a slot is used again.
8.5.7 A block taken while no image is current has no owner and is never freed (4.2.3).
8.5.8 The owner of a handler is kept per task. While the registry runs an operation or a state getter of an image on one task, that image is the owner for that task alone; the other tasks keep theirs, and the task gets back the owner it had when the handler returns. The application that is open is the owner of every task that is in no handler. A task whose application is released while it is in a handler of a library gets no owner when the handler returns: its blocks are nobody's (8.5.7).
8.5.9 The account covers what an image causes through `api->mem`, `api->fs`, `api->sync`, `api->net` and `api->job`: a call is on the account of the image whose code makes it (4.2.2.1), so a call through an export table (`api->lib->find`), a job function and a callback are on the account of the library behind them, and so are the sockets and jobs it starts (4.6.3, 4.8.2). What is not on it yet: the hooks of a core and the start hook, until the base calls them for the library; any engine a library starts there; the debug block.

## 9. Named exports and the C64 table

### 9.1 Exports and `find`

Since base 3.2.

9.1.1 A library may offer several call tables. Each is an entry of `LibRecord.export_list` (`LibExportDecl`): a name, a major, the table and its size. A library built before 3.2 offers none, or the one table of `LibRecord.exports` under its own name.
9.1.2 `api->lib->find` looks for the export `name` first. It takes the entry whose major is equal to the major of `min_version` (exactly: a caller of major 1 never gets a table of major 2), from any loaded library. A find at major 0 for an export name finds nothing and is logged (4.10.2.3). When there is none it looks for the library of that name, as in base 3.1.
9.1.3 A table starts with `uint32_t size`, its own size in bytes. Within a major a table is only appended to. A caller checks `size` before it reads a member, and a member is there when its pointer is not null. Removing or reordering a member makes the next major.
9.1.4 A provider that changes a layout can export the old and the new major together under one name. `find` gives each caller the major it asks for.
9.1.5 The loader refuses a library whose export is malformed, whose name and major another loaded library exports already, whose export has the name of another loaded library, or whose own name is an export name of a loaded library. The refusal names the library and the export.
9.1.6 The tables leave with the library. The base keeps no copy.
9.1.7 A library built against base 3.2 that must also run on a base of 3.1 writes `export_list` only when `LIB_RECORD_COVERS(out->struct_size, export_list)` holds. A library that lists `needs base >= 3.2` need not ask: the packer adds that dependency to every file with a list.

### 9.2 What the file lists

9.2.1 The packer writes the names and majors of the exports into the file (`--export name=major`, `LIB_EXPORTS` in `lib.mk`, `APP_EXPORTS` in `app.mk`). They are the static form of `export_list`: the base reads them before any image runs.
9.2.2 The loader refuses a library whose `lib_register` does not return every name and major its file lists.
9.2.3 A library may return more exports than its file lists. They are found by `find`, but they take no part in the load order (9.3).

### 9.3 Needs on an export name

9.3.1 A `needs` entry of a file resolves against export names first, then against library names. A library can need `ring>=1` whatever the library that provides `ring` is called. The major of the entry must be equal to the major of the export, and the minor is not read.
9.3.2 A need that is met by an export counts as a use of the library that exports it, as a need on its name does. The library stays loaded while an image that needs it is loaded.
9.3.3 The loader of a folder reads the lists of the files before it loads one, and puts a file in front of every file that needs a name it exports. A need on an export name that a folder does not list in any file is refused by the loader at load, with the sentence `"x" needs "ring" 1.0, which is not loaded.`
9.3.4 A need on an export of another major, lower or higher, is refused with both versions: `"x" needs "trx" 3.0, 2.0 is loaded.`
9.3.5 The own tables of the base (9.4, 9.5) are no loaded library. They cannot be listed in `needs`. Ask `api->lib->find` for them and test the result.

### 9.4 The C64 table

Since base 3.2.

9.4.1 The table `C64Api` is declared in the header `c64_api.h` of the package. The name is `C64_API_NAME`, `"c64"`, and the major is `C64_API_MAJOR`, 1. A library of a core of the C64 family exports it. Several cores can provide it, and the shared libraries use it.
9.4.2 The members, in this order: `size`, `peek`, `poke`, `dma_read`, `dma_write`, `cart_setup_bus`, `cart_restore_bus`, `cart_mode`, `cart_detect` (the members of `api->c64`, 4.9), then `dma_read_frozen`, `cart_preference` and `reset`.
9.4.3 `dma_read_frozen(addr, dst, len)` reads C64 memory as the tools of the machine see it while it is frozen: the screen it had, not the menu over it. It returns the number of bytes read.
9.4.4 `cart_preference()` returns the cartridge preference: 0 Auto, 1 Internal, 2 External, 3 Manual.
9.4.5 `reset()` resets the C64. It replaces `api->trx->reset_c64`, which calls the same operation.
9.4.6 The base answers `find("c64", ...)` with its own table, when the core is the C64: the members of `api->c64` and `reset`. `dma_read_frozen` and `cart_preference` are null in that table. The table is major 1 and competes with the libraries' exports like any export: a caller gets the table of exactly the major it asks for. A library of the core that has the two members exports a `c64` of major 1, which takes the place of the base's table. A `c64` of a higher major does not hide it from a caller that asks for major 1; that caller still gets the base's table. A caller that asks for the higher major gets the library's.

### 9.5 The tables of the shared libraries of the base

Since base 3.3.

9.5.1 The shared libraries that stay in the base (T239.3 D12) answer `api->lib->find` with a table each, at major 1: `c64cart` (`c64cart_api.h`), `cbm1541` (`cbm1541_api.h`), `sid` (`sid_api.h`), `tempdisk` (`tempdisk_api.h`) and `cbmprg` (`cbmprg_api.h`). An app answers only for the libraries its TOML B names (the file `coreapp/libs/shared.toml` for the shared app; `tools/libtab.py` makes the library table from it): a table of a library the app is not linked with is not found. Each library gives its table in its own source (`*_exports.cc`, the function its `bind` entry names). They compete with the exports of the libraries like the C64 table (9.4.6): each answers for exactly its major, and a library that exports the same name and major wins over the base. `table_size` is the `size` the table starts with.
9.5.2 No member takes a `CoreCtx`: the base calls the code behind it with the context of the core it runs. A member that takes `msg` and `msglen` answers 0 for done and non-zero with the reason in `msg`, cut to `msglen - 1`. With `msg` null or `msglen` 0 the call gives no reason and works as with one.
9.5.3 `c64cart` is the cartridge port of the C64 family. `ram_size` is 65536 on the core that has the cartridge port of the C64 family (the core the base gives the `c64` table of 9.4.6, and whose capabilities list `cartridge`) and 0 on every other core, also on a core that lists `cartridge` for a cartridge of its own; `ram_read` and `ram_write` reach the port's RAM window in DDR2. A range that is not wholly inside is ignored by a write and read as zeros. `eeprom_get` and `eeprom_put` reach the 2048 bytes of the GMod2's EEPROM on that core; `eeprom_put` clears the dirty flag, `eeprom_get` leaves it; on every other core `eeprom_get` gives zeros and `eeprom_put` does nothing. `writable` is true while a cartridge with flash or an EEPROM that the C64 can change is in the port. `require` and `prohibit` answer 0 for every cartridge, also with one in the port: the base has no cartridge definition, so 0 means "not known by the base", never "nothing is required". A library that needs the flags keeps the definition itself. `ram_write` and `image_write` drop the cartridge's cached lines themselves after every write that lands (the `inval` bit of the property `cmd` of the cartridge page, which only the base's driver writes under 6.17.9): a library does not write `cmd.inval` after a restore. Neither call reports anything to the base's write-back service, because a restore (a Timewarp rewind, a state or ring load) is no change of the cartridge: only the C64's own flash or EEPROM programming is. A library that changes the cartridge on purpose reports it through the write-back group of LibApi (D51).
9.5.4 `cbm1541` is the built-in drive A. `drive_info` fills the members that the `size` of the caller's `Cbm1541Info` covers and leaves the others and `size` as they are. It answers false for a slot other than 0 and on a core without a drive window. `image_size(0)` is 65536 on a core with a drive window and 0 on every other; `image_read` and `image_write` reach the drive's image in DDR2 with the same rule for a range that is not wholly inside. A member of task class `lock` makes the drive on its first call; `drive_info`, `image_size`, `image_read` and `image_write` (task class `any`) never make it: `drive_info` answers false until a call of class `lock` has made the drive and attached it to the hardware, and the three image members test the drive window of the core only.
9.5.5 `uci` is not a table of the base. The command interface at $DF1C is a library of the core that runs it (TRX64-Ultimate's trx64u), linked into that core's app; the base holds no UCI code and no `find("uci", ...)` answer (T239.3 D48, D49). The block `cmd_if` and the `gcr_coder` of the SDK's TOML A are core blocks. SDK 0.4.6 hands the server's source over once, see the folder `handover/` of the package.
9.5.6 `sid` is the header work of the SID player: pure functions of bytes. `plan` accepts a null `why`.
9.5.7 `tempdisk` sets the two switches of the file manager's temp folder. The base mounts the Temp disk itself.
9.5.8 `cbmprg` is the CBM BASIC program loader of the base (`app/loaders/cbm_prg.cc`), the one the VC-20, the C16 and Plus/4, the PET, the C128, the SuperCPU and the C64 share. A library finds it with `find("cbmprg", U2A3_VERSION(1, 0), &size)`. No call takes a `CoreIo`: `place`, `type_start`, `script_step`, `mem_write` and `mem_read` act on the memory of the running core, through its RAM window, or through the DDR2 window at `mem_ddr + address` as well when `mem_ddr` is not 0. They are task class `lock`; `script_check`, `relink` and `key_for` are class `any`.
9.5.9 The caller describes its machine with `CbmPrgMachine`: the start addresses of its BASIC, the pointer pair of the start of BASIC and the window its value has to fall in, the three end pointers, the keyboard buffer and its count byte, the time after a reset, the header of a `.P00`, the smallest and largest file, and `mem_ddr`. The base checks the table by `size`: a `size` that does not reach `max_size` is no machine (`place` answers 1 with the reason `No machine given.`, `type_start`, `script_step` and `mem_write` do nothing, `mem_read` gives zeros); a member past the `size` of the caller reads as 0.
9.5.10 `place(data, n, m, is_p00, out, msg, msglen)` writes the program as the file is on the medium (its two load-address bytes first, for a `.P00` the header before them) and gives where it went in `out`. A load address that is one of `basic_starts` is a BASIC program: it goes to the start of BASIC the machine states in `txttab` (refused when the value is outside `txttab_min` and `txttab_max`), every line is relinked and the three end pointers are set. Any other address is a machine-language file and stays where it says. It answers 0, or 1 with the reason in `msg`. `type_start` types `RUN` for a BASIC program and `SYS<start>` otherwise into the keyboard buffer; call it once the machine is at READY. `script_step` types the next piece of a script (`script_check`: pieces of at most ten keys, separated by `|`) when the buffer is empty and answers 1 once the whole script is in and taken.
9.5.11 The table `online` (`online_api.h`, major 1) belongs to no library of the base. The sub-app `online.u2a` (`apps/online`, a resident image that needs base 3.3) exports it with `export_list`, and registers the operations and state values `online.*` of the search form; its menu record adds the tab "Online" to F3 (6.11.4). The file manager reads the folder `/Online` through the table and calls nothing else of the search, so a core that carries no such sub-app has no `/Online`. `fetch` gives the bytes of a file of a result in a block of the allocator the caller hands it (`alloc`), not in a block of the sub-app's account; the base frees the block with its own `free`. The sub-app talks to the server `hackerswithstyle.se` over `api->net->connect` (4.8.13), `send` and `recv`, and makes those calls only on the task of a job it starts with `api->job->start` (4.6.9), never on the menu task or the executor task (1.3.9, 4.8.13.2). The operations `online.presets` and `online.search` are nodes of its menu record with the attribute `job` (6.7): the menu calls the operation again while it answers `more` (4.14.9), and the operation answers `more` for as long as its job waits for the server; the person's cancel (4.14.10) gives the request up and the job's answer is dropped. A caller that does not step the operation (REST, a hotkey, a poll) gets the answer "Asking the server" at once, and the state values show the result when the job has ended. The sub-app runs one request at a time: a second one is refused with a sentence while the first is in flight. The folder `/Online` is called on the menu task and never waits for the server: `list`, `stat` and `fetch` answer from what the sub-app keeps (the results of the search, the files of the result last opened, the file last downloaded); what is not kept is a prefetch, a job that the call starts, and the call itself fails at once (`list` returns -1, `stat` false, `fetch` null) with the reason in `error` ("Loading the files of this result", "Downloading the file"). The call after the job has ended finds the data, or the reason the job failed, which `error` tells once; the call after that asks the server again. The member `loading` (appended, T239.3 D41) says after a failed `list` or `fetch` that the job of that folder or file is still running or its result is not taken over yet: the file manager then shows the last line "Loading ..." instead of a failure and reads the folder again every 300 ms until the job has ended, and the member `loading_for` (appended, review of SDK 0.4.6) says the same for one file (`rel` as in `fetch`): the job that runs is the one of that file's entries or download, not another job. The base's file `open` of a file below `/Online` never waits: it calls `fetch` once, and when that fails while `loading_for` holds it returns null with `CoreFs::file_loading` true and the sub-app's words as `open_note`. The file manager keeps the action (view, mount, copy) pending, shows the words on the status row and opens again every 300 ms until the job has ended; the key that leaves the folder or closes the menu cancels it (a copy waits inside its job, the person's cancel ends it). A caller that is not the menu, an operation over JSON-RPC, asks again for up to 120 s on its own task. A failed `list` that is not loading puts `error` on that line and on the status row; the folder is not asked again until the person enters it again. The search itself, when its job has ended with results, calls `goto_screen` of `api->overlay` with the target `F3` (4.17.19) after `lib.show`, so the menu goes to the Library on the results; on a base without that member it stays on the tab. A quiet server therefore holds up the job only, never the menu: the connection gives up after 10 s, a quiet answer after 15 s, and a job that the base asks to end (`api->job->cancelled`, 4.6.5 and 4.6.10) gives its request up at the next quiet moment of the answer. The sub-app does not join its jobs: an ended job's slot is reused by the next `start` (4.6.6), and `join` is not for the menu task (4.6.11.3).

9.5.12 The shared libraries `mouse` (`mouse_api.h`), `pad` (`pad_api.h`), `padkeys` (`padkeys_api.h`) and `resample` ship in the package as the other shared libraries do: their sources are in `base/` and `libs/shared.toml` lists them, and the headers of their tables are in `include/`. They are linked statically (D31), and their tables are not found with `api->lib->find`: the entries `table` and `core` of the library entry (`tools/libtab.py`) hand the call table to the generic part of the application (`libtab_table`) and tell the library which core runs. `mouse_api.h` holds `MouseSinkSpec` and `MouseApi`, `pad_api.h` holds `PadSpec`, `PadApi` and the `PAD_*` constants, `padkeys_api.h` holds `PadkeysApi`; each names `CoreCtx` only by a pointer. `resample` (`u64_resample.cc`) has no call table; its header `u64_resample.h` ships with its source under `base/` (`base/inc`), and the file of a placed core (`u64cfg.cc`) calls it directly.
9.5.13 The entry `capability` of a library in the TOML B is a name or a list of names. The operations of the entry's `register_ops` are registered for a core that names any one of them: `newdisk` names `floppy` and `sio`, so that a core described by its record (the atari800 names `sio`) has "New disk image" as the compiled-in core has it.

### 9.6 The text grid of a terminal node

Since base 3.3.

9.6.1 A node of the core's menu with the layout `terminal` and the layout parameter `grid = "<export name>"` is drawn as a text screen of 80 columns on the whole layer. The base finds the table `TextGrid` (`textgrid.h`) with `api->lib->find` (the name, major 1) and draws the rows the table gives, one attribute per row and the cursor cell.
9.6.2 The base calls `render` and then `sig` on every draw pass of the screen, also on a pass that redraws nothing, and reads the rows only when `sig` changed (or the screen is drawn afresh); the other drawing calls come after `render`. A table whose `size` does not cover the members up to `sig`, or that has a null member among them, is no grid. Without a grid the screen is empty.
9.6.3 `enter` is called once when the node is entered; `tick` is called on every pass of the menu loop while the node is shown and no key is waiting, and a `true` answer redraws. Both are optional. They run on the task of the menu, one after the other with the drawing calls.
9.6.4 The base registers one grid itself, under the name `textgrid`: the built-in monitor, on a core that registers the monitor. The menu of that core names it with `grid = "textgrid"`. A library may export a table of the same name and major 1, which takes its place.

### 9.7 The core hooks

Since base 3.3. The header is `core_hooks.h`.

9.7.1 The code of a core is its library. A library that implements the hooks of its core exports the table `CoreHooks` under the name `corehooks` at major 1 (`LIB_EXPORTS = corehooks=1`, 9.2). the base asks `api->lib->find` for the name `corehooks` at major 1 once, after the libraries are loaded and before the core starts.
9.7.2 The table starts with `size`. The base calls a member only when `size` of the table and the size the export carries both cover it and its pointer is not null. A member that is null is as if there were no hook: the base makes its own call where it has one (9.5.6).
9.7.3 The base reads the export `corehooks` at exactly major 1: a library that lists it at another major only (2, say) has no hooks, and the base says so. Within major 1 the table is only appended to.
9.7.4 The members, in this order: `size`, `init`, `release`, `warm_reset`, `cold_start`, `media_flush`, `media_pending`, `start`, `menu_closed`, `base_setting_changed`, `slot_spec`, `slot_mount`, `slot_eject`, `menu_video`. The first six mean what the members of the same name of the core description mean. No call takes a context. A hook returns 0 for done, or non-zero with the reason in `msg` (at most `msglen - 1` characters).
9.7.5 The three members of base 3.3:
9.7.5.1 `int start(char *msg, int msglen)` is called once, after the groups of the API are installed and the libraries named in `needs` are loaded, and before the first poll. It is the place to start an engine (the ring's buffers, the monitor, `uci.start`); `lib_register` is not, because the groups do not exist when it runs. A non-zero result is logged and shown to the person as a notice, and the core runs on without what failed. Task: the start task, holding the core lock.
9.7.5.2 `void menu_closed(void)` is called once when the menu closes, by any way, and the machine is about to run. The hook gives back what the debugger of the core held and lands what the menu chose. It cannot refuse. Task: the main loop of the application. The core lock is not held: the hook protects state it shares with a poll declared `when_frozen`, or with the executor, by its own mutex (`api->sync`). `CTX_MENU_OPEN` and `CTX_FROZEN` are still set when it runs, the menu is hidden, and the machine is still stopped. It does not block for long.
9.7.5.3 `void base_setting_changed(const char *key)` is called after the base applied one of its own settings (4.13.19), with its key. It is called once for each of those settings the core has, in the order of the table in 4.13.19.2, after `start` returned; a change is not told before that. After that it is called after every change. Task: lock, like the operation of a setting.
9.7.6 A member the table does not have is a call the base does not make: the base has no frame ring and no monitor of its own, so with no `start` nothing starts and with no `menu_closed` nothing is released or unfrozen after the menu closes. A core with no table at all gets no such calls either. With no `menu_video` the base calls nothing: behind the menu is what the core's video shows.
9.7.7 A library that exports `corehooks` is never unloaded to make room for an application, whatever else it declares: the base keeps a pointer into the table for the whole run.
9.7.8 The order of the calls in one run: `start`; `base_setting_changed` for each base setting the core has; then, until the end, `menu_closed` for each close of the menu, `menu_video` for each change of the picture behind the menu, and `base_setting_changed` for each change.
9.7.9 The library that is the code of a core is named in the core's API TOML: `hooks = "<library name>"` in its `[core]` section (1 to 15 characters of a-z, 0-9 and _). The packer (`cor_pack`) refuses a core whose named library is not among the libraries of the bundle, or whose file does not list the export `corehooks=1` (`u2a_pack --export corehooks=1`). The base takes the table of that library, at exactly major 1 (a table of another major is no hooks, and the core runs without it, with a notice); with no name it takes the table of whatever library exports it, as in 9.7.1. Every call into the table is made on the account of that library (allocations and files it makes in a hook are its own).
9.7.10 For such a core the base calls the first six members where it calls the members of the core description, in the same order and with the same meaning: `init` and `release` hold and release the machine at every start, cold reset and reload (a library without one leaves the machine as it is: the core is then not released, and the person is told when the table is missing altogether); `warm_reset` is the warm reset, and a table without it makes a warm reset a cold one; `cold_start` runs before the restart's reload and stops it with its reason; `media_flush` and `media_pending` are the write-back of a core's own media engine (a table without them is a core with none). The first reset at power-on comes after the libraries are loaded and their API groups are filled: `init` has `api->io` and `api->settings` there.
9.7.11 `slot_spec`, `slot_mount` and `slot_eject` serve the media slots of the core: the names are the `[mount]` list of the core's API TOML, `slot` is the position in it, and the three members come together (a table with one of them missing takes the slots away, with a notice). The base gives `slot_spec` its own buffer of 200 bytes (`Settings::MOUNT_VAL_LEN`): the length 0 is an empty slot, -1 a spec that does not fit (the slot then reads as empty). A spec is the words the file uses for what is in the slot (`ro /usb0/x.d64`), and a path in it starts with `/`. `slot_mount` returns 0, or non-zero with the reason in `msg`, and a slot that cannot take the medium keeps what it held. `ctx.media_source("<slot name>")` answers with the spec, "Persist medium mounts" stores it in the core's settings file and mounts it again at the start, and the named configs hold it. While the base restores mounts, `ctx.state()` has `CTX_MEDIA_RESTORING`: the machine is held and the base releases it later, so `slot_mount` neither resets nor releases it. When a slot changes by the engine's own doing it calls `ctx.mount_notify("<slot name>")`.
9.7.12 A core that is described by its record and whose code is a library registers no operations of the base's own code. The base registers for it the groups that a built-in core registers in its own code: Diagnostics (`diag.*`, `media.*`, `usb.*`) for every such core, and the group of the 1541 (`drive.*`, `drive.put_rom` among them) when its capabilities name `drive_a` and the board did not strike it (6.12.1, the record's `board_caps`). The base's own monitor does not serve such a core: TCP port 4312 is left to the core's library.
9.7.13 `void menu_video(int behind)` is called when the base turns the picture of the machine behind its menu layer on (`behind` = 1) or off (0). It is on while a screen of the base shows the picture behind the menu, which is the still element: the base's still, or the surface a library shows as "still" (4.17.8.3). It is off when the menu covers it again. The page of a core's key table with the layout `timeline` has no UI of the base: it is transparent over the still, and the key colour without one, so a library paints its own bar as elements (4.17). A library that changes the machine's frame while the machine stands (a scrub) paints the new frame with `api->overlay->frame` and calls `update`: the base does not call `menu_video` again for it. The base calls it once for each change, never twice with the same value, and the first call is an on. It cannot refuse. Task: the main loop of the application, which draws the menu. The core lock is not held, as for `menu_closed`. It does not block for long: the screen waits for it.
