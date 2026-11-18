# T202 — A .zip opens like a folder: the person browses its content and uses a file inside without unpacking the whole archive

**Status:** PROPOSED (2026-11-11, Alex, GitHub issue #72; "own spec", translated).
**Repo:** TRX64U-MultiCore — host app F3 browser (`host-app/software/hostapp/`), the zip library of T197 (`apps/miniz`, `miniz.u2a`); cores: to be decided.
**Number:** T202 (registry: `TODO.md`).
**Origin:** GitHub issue #72 (Alex, 2026-11-10): "zip files should 'open' like a folder ... the UI should treat the content of a zip like a
folder". Alex, 2026-11-11: own spec, apart from T197 (T197 unpacks a whole archive with `Z`).
**Depends on:** T197 (miniz as a format-3 library, the host's library loader, P3).
**Milestone:** M2.
**Also carries:** T228 D17 (Alex 2026-11-17, "D17 zu T202"): in the core application's Library, CURSOR RIGHT enters a .zip like a
folder and its Library menu (RETURN) offers Unzip, as on the host.

---

## §1 The problem

- **Who:** a person in the host's F3 browser (and maybe a core's file browser) with a `.zip` of disk images, cores or updates.
- **What they want:** RETURN on a `.zip` opens it like a folder; they browse it (subfolders, `..` back out) and pick one file, which then
  behaves as the same file outside an archive.
- **What happens today (code):** F3 shows a `.zip` as a file; with T197 (branch `t197-zip`) `Z` unpacks the whole archive into the folder.
  There is no way to look inside without writing every entry to the medium.
- **Why it matters:** collections ship as `.zip`; unpacking all of it to pick one image costs time and space and leaves folders behind.

Parts:
- **P1 Browse:** the archive's central directory is read and shown as a folder tree (sizes, folders, `..`), nothing is written.
- **P2 Use a file:** picking an entry makes that one file available to every action the same file has outside an archive.
- **P3 Where:** host F3 and the cores' F3 Library (D2).

## §2 Scenarios

(to be written after the decisions)

## §3 Decision

- **D1 (Alex, 2026-11-11):** own spec, not a part of T197.
- **D2 (Alex, 2026-11-11):** "also in the cores, yes, so generally in the browser module, that should ideally be the same everywhere" (translated): host F3 and every core's F3 Library. Fact at the time: two browsers (core app `coreapp/src/filemgr.*`, host `host_core_*`), both over Gideon's FileManager.

- **Open (Alex 2026-11-12):** what using a file inside a zip does (unpack one file to a cache folder / into RAM / beside the zip) should be the person's choice in app settings, the same in every core (not Gideon's UI) - "that should actually be decided by the user in something like app settings" (translated). Where: a fifth F5 tab "App" (fits T205 D1) or F8 (needs T205 D1 reopened) - open with Alex; then the default.

## §4 Out

(to be decided)

## §5 Build notes

(empty)
