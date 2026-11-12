# T197 — A .zip can be unpacked from the host menu and from every core that has room, through one zip library any application can use

**Status:** PROPOSED (2026-11-09, Alex).
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
- **D2 (Alex, 2026-11-10, translated: "It's ok. Cleaning up memory when it gets too full would be good though"):** a loaded library stays resident
  for the core session (about 19 KB); when the loader lacks room for an application, it first unloads libraries no running application uses,
  then decides (a reference count per library). Built after P3.
- **D3 (Alex, 2026-11-10):** `unzip.u2a` and `miniz.u2a` are part of the release and installed to `/cores/apps`; the tester workaround for
  main-2bd67963 is on #43. Whether the `.txu` host update also copies them is open.
- Proposed: P1 as a format-3 library (T104.1 registry, `ops.h`) with a small call table (open zip, list, extract entry to a file, inflate
  buffer); `unzip.u2a` imports it. The host gets the same loader code (shared source, copied per the copies rule where it is gateware-free app
  code that both apps compile).

## §4 Out

- Writing zips (packing).
- Other archive formats (7z, rar, tar.gz) unless they come free with miniz.

## §5 Decisions taken while building

(empty)
