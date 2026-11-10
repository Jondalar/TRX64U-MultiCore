# T197 — A .zip can be unpacked from the host menu and from every core that has room, through one zip library any application can use

**Status:** IN PROGRESS (2026-11-10, branch `t197-zip`: P1, P2, P4, P5 built and green natively and for RISC-V; P3 host open).
**Repo:** TRX64U-MultiCore — `apps/` (the zip library and the unzip application), the loader and service table of T104.1/T104.3
(`app/sdk/`, `coreapp/src/libload.cc`), the host app (`host-app/software/hostapp/`: an application loader it does not have today).
**Number:** T197.
**Origin:** Alex, 2026-11-09 (translated), on "does unzip work in the host menu now?" (it does not): "miniz should be a lib/app. Any kind of
application can use/include it. Host == yes. Cores with room == yes".
**Depends on:** T104.1 (format 3, the loader), T104.3 (ABI 3, the application runner, `apps/unzip` as built), T180 D9 (`/SD/cores/apps`).
**Milestone:** M2.

---

## §1 The problem

- **Who:** a person with a `.zip` (a game set, a disk collection) on the SD or a stick.
- **What they want:** unpack it where they are: in the host's file browser (F3) before any core runs, and in a core's Library.
- **What happens today (code):**
  - Core app: `apps/unzip` (miniz compiled into `unzip.u2a`, ABI 3) adds three actions on `.zip` (`app.toml`: to folder, here, one file),
    loaded from `<core folder>/apps` or `/SD/cores/apps` (`coreapp/src/appindex.h`). miniz lives inside that one application: another
    application that wants to read a zip has to carry its own copy.
  - Host app: no application loader, no zip code (`host-app/software/hostapp/`: no `.zip`, no miniz); F3 shows a `.zip` as a plain file.
  - Cores: whether a core can load an application at all depends on the RAM its core app leaves (the loader's window); not every core has it.
- **Why it matters:** collections come as zips; unpacking needs a PC today when no core runs.

Parts:
- **P1 Library:** miniz (inflate, zip directory) as one loadable library with a stable call table, which any application (unzip, a future
  disk-image or update reader) uses instead of a copy.
- **P2 Unzip on the library:** `apps/unzip` calls P1 instead of its built-in miniz.
- **P3 Host:** the host app loads the same applications (format 3 / ABI 3 loader and the file actions of `app.toml`) for its F3 browser,
  so the same `unzip.u2a` + library run there.
- **P4 Cores with room:** a core app loads them only when its loader window holds the library + the application (sizes known at build time);
  a core without room does not show the actions (no action that fails after the press).
- **P5 Packaging:** the library ships in `/SD/cores/apps` beside `unzip.u2a` (package and update), found by host and cores.

## §2 Scenarios

| # | Solves | Scenario | Test | Where |
|---|---|---|---|---|
| S1 | P1, P2 | unzip through the library unpacks a test zip (stored and deflated entries, folders, a long name) byte-identical to the original files | native loader test with the library and the application loaded as on the board | apptests |
| S2 | P1 | a second application (a test app) calls the same library: no second copy of miniz in it (its size shows it) | native loader test + size check | apptests |
| S3 | P3 | host F3: a `.zip` shows the three actions; "Unzip to folder" makes the folder with the files | host render bench / native host test with the loader | apptests |
| S4 | P4 | a core whose loader window is too small does not list the actions; a core with room does and unpacks | core app native test with both window sizes | apptests |
| S5 | P1-P5 | Board: unzip a real collection in the host F3 and in one core (A1200), files usable | by hand | board (Alex) |

## §3 Decision

- **D1 (Alex, 2026-11-09):** miniz becomes a library/app any application can use; host yes; cores with room yes.
- Proposed: P1 as a format-3 library (T104.1 registry, `ops.h`) with a small call table (open zip, list, extract entry to a file, inflate
  buffer); `unzip.u2a` imports it. The host gets the same loader code (shared source, copied per the copies rule where it is gateware-free app
  code that both apps compile).

## §4 Out

- Writing zips (packing).
- Other archive formats (7z, rar, tar.gz) unless they come free with miniz.

## §5 Decisions taken while building

- **D2 (built; how an application reaches a library):** a library's record (`LibRecord`, `libapi.h`) gets two last fields, `exports` / `exports_size`: a call table of
  function pointers in the library's image (relocated by the loader like any table). `LibApi` gets a last group `lib` (`SvcLib::find(name, min_version, &size)`, base
  **3.1**, `U2A_BASE_MINOR` 1): it returns the table of a *loaded* library of that name and version or newer, else null. Both are append-only (a library or application
  built against the old layout reads what it always read). The loader accepts a table only with a size of at least 4 bytes; the layout of a table is the library's own header
  (`apps/miniz/zip_lib.h`, `ZipLib`, append-only, `ZIPLIB_HAS(t, field)` checks the size the library reports).
- **D3 (built; the library):** `apps/miniz` = miniz moved out of `apps/unzip/src/miniz` (`git mv`, unchanged), `miniz_lib.cc` = `lib_register`, `zip_lib.h` = the table
  (reader init/end, number of files, file stat, extract to callback / to memory, locate, inflate to memory). Format 3, **resident**, `needs base>=3.1`. `app/lib/app.mk` got
  `APP_KIND = lib` and `APP_NEEDS`. `apps/unzip` keeps only `unzip.c`, includes `zip_lib.h`, maps the five miniz calls it uses onto the table (macros), `glue.cc` looks the
  table up and answers "The zip library (miniz.u2a) is not loaded." when it is not there; `unzip.u2a` has `needs miniz>=1.0`. Sizes on the builder (RISC-V, `-Os`):
  `unzip.u2a` 10269 bytes (it carried miniz before), `miniz.u2a` 18860 bytes, 8 relocations.
- **D4 (built; who loads the library):** a library is not an application, so `AppIndex::scan` no longer logs "is a library, not an application" for it: it keeps up to
  `LIBS_MAX` (4) `LibFile`s (name, version, path, image bytes) of the same apps folders (core's own first, then the shared one). `AppHost::open_now` calls
  `AppIndex::load_needs` before it loads an application: every non-base need that is not in memory is loaded from that list (version checked by the loader). A library
  stays loaded afterwards (libraries are resident, `libload.cc` refuses anything else): a core that unzipped once holds 18.9 KB of heap until it stops. Transient libraries
  are not built (open, see below).
- **D5 (built; P4, the loader window):** the window is the heap the board can still give: `LibHost::room()` (board: `xPortGetFreeHeapSize()`, the default is unlimited for
  hosts without a limit). `run.cc` hands it to `AppIndex::set_room` before every menu merge. `AppIndex::fits` = the application's image + bss + alignment, plus every needed
  library that is not in memory yet (image sizes read from the headers at scan time), plus `APP_WORK_BYTES` (64 KiB: miniz's inflate state and window, the zip directory,
  buffers - a constant of the base, not measured per application) must be at most the room; a needed library that is missing from the folders or too old does not fit.
  `contributors()` skips an application that does not fit, so the `.zip` sheet shows no action rather than one that fails after the press. The decision is made at merge time
  (core start and a stick that comes later), not at the press: the heap may have shrunk since (open).
- **D6 (built; P5):** `ci/trxapps.sh` builds `miniz.u2a` beside `unzip.u2a` (stage key and the `libapp` key cover `apps/miniz`; the stage fails if `unzip.u2a` is not
  smaller than the library, S2 on the real image), `ci/package.sh` copies it, `tools/release_mc.sh` requires it and passes it with `--u2a`; the installer
  (`update_trx.cc`, #43) copies every `.u2a` beside the update file into `<cores>/apps`, so no updater change was needed; `INSTALL.md` lists it and says Unzip needs it.
- **Evidence:** S1, S2, S4 in `tests/coreapp/test_unzip.cc` (71 checks; the real `LibLoader` loads `miniz.u2a` and `unzip.u2a` fixtures made by the real packer, the Mac builds
  of `apps/miniz/miniz_lib.cc`, miniz and `apps/unzip` run in place of the RISC-V image): the loader refuses `unzip` with "needs miniz" while the library is not loaded;
  `load_needs` loads it once; unzip extracts the synthetic zip byte-identical (stored, deflated, nested folders, 100/200 character names, 255 refused and counted); a test
  `ziplist` application lists the entries through the same table and loads no second copy; S4 room = need - 1 byte: no `.zip` action, room = need: three actions, library
  missing from the folders: none. `tools/dev.sh riscv` builds both images.
- **Open: P3, the host (S3).** The host app has no loader. What it needs, read from the tree: `libload.cc`, `libmenu.cc`, `descrec.cc`, `ops.cc` (the closure
  `tests/loader/run.sh` already builds on its own) into `hostapp` through the existing `VPATH += coreapp/src`; a `LibHost` for the host (FreeRTOS heap, the icache sweep of
  `libboard.cc`); a `CoreFs` over `hostfs`; the `dlg` group (message, progress, pick) over the host's own note / progress lines; the `.zip` rows in `host_core_rows.cc` /
  `hostdecl.cc` (a new action id, so `test_hostapi`, the goldens and the ue2emu F3 flow change; `ux-review` before any board test). Not started: it is the largest part and
  its screen can only be judged in ue2emu.
- **Open question for Alex (the .txu path):** the `.txu` host update still installs no `.u2a` (#43: INSTALL.md 3a says "copy by hand"). When P3 gives the host a loader, should
  the `.txu` flow copy `unzip.u2a` and `miniz.u2a` into `<cores>/apps` (the host would then be the installer of applications for cores it does not run)? Not built, no decision.
- **Open question for Alex (resident library):** the library stays in the heap once loaded. A transient library (freed when the last application using it ends) needs a
  reference count in `LibLoader` and a second residence rule in the format; not built.
