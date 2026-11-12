# T104 — the Amiga and the A1200 on the generic application: build notes

**Status:** BUILT on branch `t104-amiga` (2026-10-20), benched on the Mac, **not built for RISC-V and not run on a board.**
**Why this file:** a docs-only consolidation of T104 / T104.1 / T104.2 runs in parallel (coordinator, 2026-10-20), so the text that would
have been T104 §5.11 lives here; the consolidation can fold it in. **Origin:** Alex, 2026-10-20: "Yes, but please follow the architecture!
So library for what is core specific, app for what makes sense as an app, and so on" (translated: the Amiga and the A1200 move onto the
generic application, complete per T104 §5.1-5.7, only the diagnostics dropped as §5.7 says). Same day: "Core menu open == Freeze. I do not
want this overlay stuff!" - so no Overlay path was built and the menu-open service question (§5.3) is settled.

## What was built where

Rule applied: a machine's behaviour that another core can use is a **record kind in the base** (`coreapp/src/`), a file format is a **loader
library** (`app/loaders/`), and the Amiga's own file holds registers, values and names only (`coreapp/cores/amiga.cc`).

| kind of entry (T104 §5.2) | where it lives | the Amiga's record |
|---|---|---|
| `setting` extended: bits of a shared byte, value-to-byte table, `reset_write` | `coreapp/src/regset.{h,cc}`, `RegBind` / `RegWrite` / `Setting::bind` in `desc.h`; `boot_apply_settings`, the menu's live path and "Reset to host" go through it | MEM_CFG (chip [1:0] + slow [3:2]), FAST_CFG, CHIPSET_CFG (0x00/0x08/0x10), FLOPPY_CFG (write protect [1] + drives-1 [3:2]); CPU_CFG = 0 and IDE_CFG = 0x21 as fixed writes |
| `rom_set` (scan, choice setting, Auto by version, window placement, mirror, RAM clear, reload on change) | `coreapp/src/romset.{h,cc}`; the Kickstart **format** (version word at 0x0C, `ks_pick`) in `app/loaders/kickstart.{h,cc}` | `.rom`, 256 KB / 512 KB, at most 8, window +0x780000, 256 KB written twice (+0x7C0000), RAM below cleared |
| `media_slot: window` (zero-filled slot, mount register, 160-track dirty map, DLOG pop with settle, flush at 1 s idle / 5 s dirty, overflow = all tracks, write-back failure told once) | `coreapp/src/winslot.{h,cc}` | DF0-DF3, slots at +0x900000 + n * 0xE0000, 5632-byte tracks, DLOG 0x1F0-0x1F3 |
| `media_slot: sectors` (mailbox sector server, 64 bit offsets, size register in sectors or bytes as the core's FEATURES bit says, refusals with the reason) | `coreapp/src/sectors.{h,cc}` | HD0/HD1, size register 0x184 + 8u, mailbox 0x1A0-0x1A8, staging +0xC80000 |
| `mouse_sink` (accumulate clamp 4000, one pair per 2 ms of at most 127, remainder kept, buttons on change, DX then DY) | `coreapp/src/mousesink.{h,cc}` | 0x1C0-0x1C2 |
| the engines' state, the base operations, the one poll | `coreapp/src/engines.{h,cc}`: `window.mount/eject`, `sectors.mount/eject`, `romset.put`, `media.poll` (the core's `service_op`) | tasks and file actions only *bind* these by name |
| two records from one source, §5.5 override list | `coreapp/cores/amiga.cc`: `variants[2]` is the list (identity, store, Auto text and wanted version, chipset values and default, fast-RAM values, chip and slow defaults); everything else is built once | `amiga_desc`, `amiga1200_desc` |
| shared ID word | `CoreDesc::id_shared`; `start.cc` picks the descriptor by the bundle's name; `desc_check_table` allows a shared ID only when both say so | `AMIG` for both |
| video without FB_BASE/FB_STRIDE registers, `nbuf` honoured | `video.cc` writes the two registers only when the descriptor names them and ignores a reported buffer >= `nbuf` (T104 §5.3 gaps) | 720 x 540, scale 2, x0 240, buffers 0x01D00000 + n * 1 MiB, 3 buffers |

`desc_check` was extended for every new record (exactly one of apply / bind / rom_set choice per setting, register bits fit the byte and do
not overlap, registers inside the block, window/sector/mouse fields, **no two records claim the same DDR2 window range**, shared-ID rule).

## Decisions taken while building

- **Freeze only.** The descriptor does not touch `poll_when_frozen` (Alex dropped it, T104.1: no poll runs while frozen); an open menu holds the machine, so the dirty log, the mailbox and the mouse are served while
  the menu is closed. Pending write-backs are flushed by every hold (reset), eject and remount, and by the idle policy once the menu closes.
  The reference's `PX_GALPHA 70` under the menu is not built (no Overlay).
- **The service is one poll** (`media.poll`, 5 ms period). A pass that served a mailbox request asks to be called again at once, as the
  reference's 3 ms yield burst did; the base's cap of 20 ms of consecutive passes (T104.1 §5.2, Poll) now also applies to HDF reads and costs
  a 5 ms gap per 20 ms. Not measured; if a sequential HDF read is slower than the fork's, that cap is the first suspect.
- **Mount runs inside the executor under the core lock** with the machine held, instead of the reference's separate lock dance. The ADF is read
  after the old disk is flushed and ejected, as in the reference; a failure leaves the slot empty (an earlier refusal, e.g. size, leaves the old disk in).
- **A failed write-back clears the dirty marks** (the reference does too: the tracks were written, only the sync failed). The notice is told once
  until a write-back succeeds again.
- **The first directory with any `.rom` decides** (reference behaviour, kept and benched): `/flash/roms/amiga/odd.rom` of the wrong size
  hides good files on a stick; the message names the file and the directory. A refused set is looked at again after 5 s, a missing one after
  0.5 s and told after 10 s (T104 §5.1 "two retry delays").
- **Auto is the first candidate of the machine's version** (`KS_VER_13` for the A500, `KS_VER_31` for the A1200), as ADR-0056. The
  choice setting's value list is dynamic (labels live in the descriptor's storage); the settings are read again after a scan whose labels
  changed so that a `.cfg` naming a file resolves.
- **`machine.cold_reset` reloads the Kickstart when the choice names another file** (`reload_on_change`); `machine.reload_roms` always does and
  rescans. On a reload of a running machine the RAM below the ROM is cleared; at the first start it always is.
- **Ambiguous ID without a bundle name is refused** (`START_AMBIGUOUS`, the core stays held, the message lists the candidates). This is not
  the §5.7 ID-mismatch policy: a bundle whose name matches no candidate is refused exactly as a mismatching name always was.
- **Counts in §5.1's table do not match the reference code** (it says Amiga 8 settings / 4 tasks / 2 file types / 5 media, A1200 10 / 4 / 4 / 7).
  The code has 7 settings (Kickstart, chip, slow, fast, chipset, floppies, write protect), 8 tasks (reset, reload, eject DF0-3, eject HD0-1),
  2 file types (`.adf` with 4 actions, `.hdf` with 2) and 6 slots, identical for both machines apart from the five override fields. The
  descriptors follow the code.
- **Dropped, as §5.7 says or by the Freeze decision:** the eight performance counters and stall info (`diag_poll`), the overlay fade, the
  memory read/write of `C64_DMA_RAW_*` (REST, not built yet), the fork's menu pruning lists (the menu is a whitelist here).

## Not done

- **The USB mouse feed - done on branch `t104-mouse` (T104.1 5.6), the text below is the state before it.** The sink is built and benched, and the base sets its critical section, but nothing calls `mouse_sink_motion()`:
  the only place that sees the HID reports is `process_mouse_report()` in `host-app/software/io/usb/usb_hid.cc`, a byte-for-byte vendored copy
  of the fork, whose "U64" branch drives the 1351 port (`usb_hid_set_mouse1_position`). The fork's Amiga build patched exactly that spot
  (`#ifdef AMIGA_APP amiga_mouse_motion(...)`). A hook there is a change to `host-app/` and was not made. **Needs Alex / the coordinator.**
- **No RISC-V build.** The Makefile lists the new sources (`regset engines winslot sectors romset mousesink kickstart amiga`), but the
  build image is not on this Mac; `run.cc` (mouse critical section, ROM-set name for the Home line) was not compiled.
- **No board run.** Nothing of the Amiga ran with this application: Kickstart in the window, the picture, floppies, HDF, Freeze.
- **CI ships the generic image for both Amiga cores** (A1200 since 2026-10-20, A500 since 2026-10-24, T104 5.15): `amiga` / `amiga1200` are in `ci/trxapps.sh` `BLUEPRINT_CORES`; the stage's image wins over the uploaded `amiga.app` / `amiga1200.app`.
- The `.cor` records of T104.1 (description, libraries) do not exist yet, so the record lives in `amiga.cc` like the Atari's and the NES's.

## Open questions (listed, not decided)

1. **T104.1 §5.1 (the lines on the RAM disk and `wb_idx`).** The Amiga's buffers are 0x01D00000 + n * 1 MiB, n = 0..2, i.e. up to
   0x01FFFFFF: they end exactly at the window's end and the descriptor check refuses anything that overlaps another record. Whether the core
   application uses a RAM disk at all, and what the gateware's `wb_idx` register means for an application that never reads it, is open;
   nothing here forecloses either (`nbuf` is honoured by `Video::poll`; the descriptor check refuses a record that overlaps the buffers' range).
2. **T104 §5.7, the ID-mismatch policy** (S2 refuses, the forks report and carry on, S7 compares against the fork). Kept as it was: a
   bundle name that disagrees with the descriptor refuses. The new case, several descriptors on one ID with no bundle name (JTAG, developer
   start), refuses with the candidates listed; a default machine for that case would be Alex's choice.
3. **T104 §5.3, service while the menu is open** - settled by Alex (Freeze for every core, no poll while frozen); nothing to build.
4. The mouse feed (above) and the CI switch (above).
