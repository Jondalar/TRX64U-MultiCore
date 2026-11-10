# Contract: TRX64 Ultimate firmware between TRX64-Ultimate and TRX64U-MultiCore

**Status:** agreed 2026-10-30 between the TRX64-Ultimate maintainer session and the MultiCore session, on Alex's
instruction; §1 and §2 amended 2026-10-31 (the package namespace, both sides and Alex). Changing it needs both
sides. The same text is in `TREX/TRX64-Ultimate` at the same path.

## 1. Who builds what

- TRX64-Ultimate builds the gateware and Gideon's application, and publishes the Gitea generic package `trx64-ultimate`, version `<branch>-<sha8>`: `c64_basic.bit`, `ultimate.app`, `manifest.json`, `SHA256SUMS`, plus the standalone updater files `trx64_update_<commit8>_<date>.cfw/.ue2`.
- TRX64U-MultiCore packs the `.cor` files. It does not build the application; it takes the published files.
- Neither side pushes into the other's CI. ADR-0058 (TRX64-Ultimate) stands: MultiCore loads TRX64 Ultimate's firmware, it does not build it.
- **The package namespace is split** (Alex, 2026-10-31). `trx64-ultimate` is written by TRX64-Ultimate's CI
  alone; MultiCore never publishes into it. MultiCore publishes its own generic package `trx64u-multicore`,
  version `<branch>-<sha8>` and `<branch>-latest`. Each side owns the `<branch>-latest` alias of its own
  package; a state of the other side is read as `<branch>-<sha8>`, never as its `-latest`.
  Why: until that day both CIs wrote `trx64-ultimate`, and the one mutable name changed owner — on 2026-10-31
  `main-latest` was MultiCore's `main-af5306d4` at 16:34 and TRX64-Ultimate's `main-3a9d10c2` at 16:36. The
  file names are the same in both packages and mean different content: `c64_basic.bit` is the monolith here
  (1 783 913 B) and the host layer there (1 739 801 B).

## 2. How a state moves over

- MultiCore pins per file in `release/imports/*.pin`: the package version plus the SHA-256 of each file it takes. Its `ci/package.sh` fetches, checks the hash and packs.
- A pin moves only by a commit in MultiCore, when Alex wants a new state there. There is no notification channel; TRX64-Ultimate names the package version it considers good.
- The two cores:
  - `trx64u` — NAME "TRX64U", file `trx64u.cor`: TRX64-Ultimate's bitstream with MultiCore's generic coreapp. Pins `BIT_FILE=c64_basic.bit` and `BIT_SHA256`.
  - `trx64g` — NAME "TRX64 Ultimate (Gideon app)", file `trx64g.cor`: TRX64-Ultimate's bitstream with its own application. Pins `BIT_FILE`, `APP_FILE=ultimate.app` and both hashes.
- The split of §1 leaves this path untouched: a pin names `trx64-ultimate/<branch>-<sha8>` and the SHA-256 of
  every file, so MultiCore keeps reading this package and the hash catches a file that is not the pinned one.

## 3. The hand-over block

- MultiCore's host fills one 4 KiB page in DDR2 at `HANDOVER_ADDR` `0x03EFF000` before every core start: magic `TRXH` `0x48585254`, version 1, a checksum, `path[256]` (the `.cor` path as the host opened it), `core_name`, `core_version`, `host_build`, then `id_sum` and `core_id`. Reference: MultiCore `host-app/software/hostapp/handover.h` / `.cc`, which TRX64-Ultimate may copy.
- **Version 1 tail rule.** `handover_sum()` and `handover_valid()` stop at `id_sum`, so a reader built before that field still accepts a block, and a block from an older host leaves `id_sum`/`core_id` unset. Read the id only through `handover_core_id()`, which answers `""` in that case and never a wrong id.
- **The address stays.** TRX64-Ultimate verified on 2026-10-30 that nothing in its boot path writes it before `main`: the boot ROM's `test_access()` is defined but never called, the DDR2 calibrator writes only `0x00000100` and `0x00010000`, the application's linker region is `ORIGIN 0x00030000, LENGTH 0xE60000`, and `__updater_limit` is `0x03C00000`. The only later writer is a cartridge load into `0x03C00000..0x04000000`, harmless once the page has been copied.
- **Rule for a reader:** copy the page once, as early as possible, validate the copy, then never read the DDR2 page again.
- **A valid block may be stale.** The page is one-shot per core start and is not cleared when control returns to the host, and after a JTAG/SRAM start of a bitstream it still holds the previous core's block. `handover_valid()` tells noise from a block, not a fresh block from a stale one. A reader that also runs on the host — the host app links Gideon's `ConfigManager` — must therefore check `core_id` against its own (and `core_name` as the fallback for a block without an id), not `handover_valid()` alone.

## 4. Where the settings go (T148 D1/D6)

- With a valid block **for this core**, TRX64-Ultimate's application writes its settings to `<dir>/<name>/<name>.cfg`, derived from the base name of `path[]`, creating the folder as MultiCore's `corefolder_ensure()` does. `/USB1/cores/trx64g.cor` gives `/USB1/cores/trx64g/trx64g.cfg`.
- Without one it uses the flash config pages, as Gideon's firmware does, and touches nothing MultiCore owns.
- The folder, not the old place beside the `.cor` (T93 S6), is the rule; MultiCore's `corefolder_move_cfg()` migrates an old file into it once.
- **One application file, not two.** The decision is made at run time from the block, not by a build flag. Two applications would need a second `APP_FILE` pin and two `.cor` for the same firmware, kept in step for nothing.
- The content of that `.cfg` belongs to whoever wrote it. MultiCore's generic coreapp uses its own `[section]` text format; `trx64g` runs Gideon's application and owns its file. Neither side parses the other's.

## 5. Foreign firmware as a core entry

- Converting a foreign `.cfw`/`.ue2` into a core entry is owned by MultiCore (its host on the board, T137; `tools/cor_pack.py` and the CI-side helper `tools/cfw_core.py`). In `cor_pack.py`, `ORIGINS = ("trx", "foreign", "backup")`: "foreign" means converted by the host from a `.cfw`/`.ue2`, "backup" means read from this board's flash.
- Its purpose is firmware that exists only as a `.cfw` (Gideon 3.15a, Commodore 1.1.0), so the core list has one kind of entry instead of two.
- It is **not** the path for `trx64g`/`trx64u`. Those are packed from `c64_basic.bit` + `ultimate.app`, which are published beside the `.cfw` and are byte-equal to what it contains — `tools/trx_pkg_check.py` proves that on every build.

## 6. What each side may change alone

- TRX64-Ultimate: the gateware, Gideon's application including its config code, the update packages, its own CI.
- MultiCore: `cor_pack.py`, `core.toml`, the `.cfw` container rules, the host, the generic coreapp, the pins.
- A change that moves the other's ground — the hand-over layout or address, the core folder rule, the package's file names, the application's address map — is announced before it is pushed.
