# Contract: TRX64 Ultimate firmware between TRX64-Ultimate and TRX64U-MultiCore

**Status:** agreed 2026-10-30 between the TRX64-Ultimate maintainer session and the MultiCore session, on Alex's
instruction; §1 and §2 amended 2026-10-31 (the package namespace, both sides and Alex); §3's page tail added by
MultiCore (T191, T198); MultiCore T230, proposed by both sides 2026-11-16, waiting for Alex's go: §1 the ownership
table, §2 the version, §7 the start file, §8 what the bitstream offers an application, §10 the target split, and the two copies
made the same text again. §7 and §8 amended 2026-11-24 by both sides on Alex's go (MultiCore T239.3 D37: the way back
from a large core).
Changing it needs both sides and Alex. The same text, byte for byte, is in `TREX/TRX64-Ultimate` and
`TREX/TRX64U-MultiCore` at `docs/contracts/trx64u-firmware-handover.md`; a `diff` of the two is the check.

## 1. Who owns what

| Area | TRX64-Ultimate | TRX64U-MultiCore |
|---|---|---|
| Bitstream (`c64_basic.bit`, the monolith) | builds it; what it offers an application is §8 | reads it from the package, never builds it |
| Gideon's application (`ultimate.app`, the core `trx64g`) | builds it, its settings (§4), its behaviour | packs it into `trx64g.cor` |
| The firmware's version | `APPL_VERSION_NUMBER` (`software/application/versions.h` of the application), set by hand per release; the releases | shows it in the boot menu (§2) |
| Package `trx64-ultimate` (§1, §2) | its CI alone publishes it, with `manifest.json` `"version"` | reads `<branch>-<sha8>` |
| Package `trx64u-multicore` | never writes it | its CI alone publishes it |
| Pins, `.cor` packing, the `SEMVER=` rule (§2) | names the package version it considers good | owns them |
| Host, boot menu, core start | - | owns them |
| Hand-over page (§3) | reads it (Gideon's application, §4) | its layout and the host that writes it |
| Start file (§7) | Gideon's application ignores it | writes it (host) and reads it (generic coreapp) |
| The way back (§7) | the ICAP loader in `c64_basic` (§8) | the menu marker and the IPROG (generic coreapp), the host's menu |
| Generic coreapp on `trx64u` (config at start, persist, autostart, `menu.toml`) | - | owns it |
| Foreign firmware as a core entry (§5) | - | owns it |
| DDR2 windows (§6) | `0x03800000..0x03A00000` (debug service) | `STAGE_APP`, `STAGE_PART`, the update window of the coreapp |
| This text | both sides, with Alex | both sides, with Alex |

- TRX64-Ultimate publishes the Gitea generic package `trx64-ultimate`, version `<branch>-<sha8>`:
  `c64_basic.bit`, `ultimate.app`, `manifest.json`, `SHA256SUMS`, plus the standalone updater files
  `trx64_update_<commit8>_<date>.cfw/.ue2`.
- `manifest.json` carries `"version"`: `x.y.z`, the `APPL_VERSION_NUMBER` of the application commit in the package
  (packages built before that commit have no such field). A package between two releases repeats the last
  release's number: the field names the release line, not this package; the release is the Gitea release
  `trx64-<branch>-<sha8>` of that package version.
- TRX64U-MultiCore packs the `.cor` files. It does not build the application; it takes the published files.
- Neither side pushes into the other's CI. ADR-0058 of TRX64-Ultimate stands: MultiCore loads TRX64 Ultimate's
  firmware, it does not build it.
- **The package namespace is split** (Alex, 2026-10-31). `trx64-ultimate` is written by TRX64-Ultimate's CI
  alone; MultiCore never publishes into it. MultiCore publishes its own generic package `trx64u-multicore`,
  version `<branch>-<sha8>` and `<branch>-latest`. Each side owns the `<branch>-latest` alias of its own
  package; a state of the other side is read as `<branch>-<sha8>`, never as its `-latest`.
  Why: until that day both CIs wrote `trx64-ultimate`, and the one mutable name changed owner — on 2026-10-31
  `main-latest` was MultiCore's `main-af5306d4` at 16:34 and TRX64-Ultimate's `main-3a9d10c2` at 16:36. The
  file names are the same in both packages and mean different content: `c64_basic.bit` is the monolith here
  (1 783 913 B) and the host layer there (1 739 801 B).

## 2. How a state moves over

- MultiCore pins per file in `release/imports/*.pin`: the package version plus the SHA-256 of each file it takes.
  Its `ci/package.sh` fetches, checks the hash and packs.
- A pin moves only by a commit in MultiCore, when Alex wants a new state there. There is no notification channel;
  TRX64-Ultimate names the package version it considers good.
- The split of §1 leaves this path untouched: a pin names `trx64-ultimate/<branch>-<sha8>` and the SHA-256 of
  every file, so MultiCore keeps reading this package and the hash catches a file that is not the pinned one.
- **The version shown** (MultiCore T230): a pin carries `SEMVER=x.y.z`, the manifest's `"version"`, only when its
  package version is a release (the Gitea release `trx64-<PKG_VERSION>` exists when the pin is reviewed); any
  other pin carries `x.y.z-dev+<sha8>` (the sha8 of the package version). MultiCore's packer accepts both forms and
  fails when the `x.y.z` part differs from the manifest. A package without `"version"` is pinned with the version
  taken by hand from the release.
- The two cores:
  - `trx64u` — NAME "TRX64U", file `trx64u.cor`: TRX64-Ultimate's bitstream with MultiCore's generic coreapp.
    Pins `BIT_FILE=c64_basic.bit` and `BIT_SHA256`.
  - `trx64g` — NAME "TRX64 Ultimate (Gideon app)", file `trx64g.cor`: TRX64-Ultimate's bitstream with its own
    application. Pins `BIT_FILE`, `APP_FILE=ultimate.app` and both hashes.

## 3. The hand-over block

- MultiCore's host fills one 4 KiB page in DDR2 at `HANDOVER_ADDR` `0x03EFF000` before every core start: magic
  `TRXH` `0x48585254`, version 1, a checksum, `path[256]` (the `.cor` path as the host opened it), `core_name`,
  `core_version`, `host_build`, then `id_sum` and `core_id`. Reference: MultiCore
  `host-app/software/hostapp/handover.h` / `.cc`, which TRX64-Ultimate may copy.
- **Version 1 tail rule.** `handover_sum()` and `handover_valid()` stop at `id_sum`, so a reader built before that
  field still accepts a block, and a block from an older host leaves `id_sum`/`core_id` unset. Read the id only
  through `handover_core_id()`, which answers `""` in that case and never a wrong id.
- **Page tail after `id_sum`/`core_id`** (all outside `handover_sum()`, each with its own check word; offsets as in
  `handover.h`): `time_sum` (T191: `handover_time_sum()`, date and time mixed with `sum`; 0 = no time),
  `time_date` (local calendar year << 16 | month << 8 | day), `time_time` (hour << 16 | minute << 8 | second);
  then `config_sum` (T198 D7: `handover_config_sum()`, the name mixed with `sum` and `id_sum`; 0 = no config) and
  `config[48]` (the name of a config of the core, `<core folder>/configs/<name>.cfg` without the extension, at
  most 32 characters, "" = plain start). Read them only through `handover_time()` / `handover_config()`, which
  answer "none" for a block of an older host or a failed check word.
  **Older readers ignore the tail:** they check only `sum` and `id_sum`, so a block with a time or a config is
  still valid for them and they start plain. Further fields follow the same rule: `safe_sum` (T224 D2),
  `hostver_sum` / `host_version` (T221), and the "menu requested" marker at `HANDOVER_MENU_OFFSET` `0xFF8` (T135),
  outside the struct; all in `handover.h`.
- **The address stays.** TRX64-Ultimate verified on 2026-10-30 that nothing in its boot path writes it before
  `main`: the boot ROM's `test_access()` is defined but never called, the DDR2 calibrator writes only
  `0x00000100` and `0x00010000`, the application's linker region is `ORIGIN 0x00030000, LENGTH 0xE60000`, and
  `__updater_limit` is `0x03C00000`. The only later writer is a cartridge load into
  `0x03C00000..0x04000000`, harmless once the page has been copied.
- **Rule for a reader:** copy the page once, as early as possible, validate the copy, then never read the DDR2
  page again.
- **A valid block may be stale.** The page is one-shot per core start and is not cleared when control returns to
  the host, and after a JTAG/SRAM start of a bitstream it still holds the previous core's block.
  `handover_valid()` tells noise from a block, not a fresh block from a stale one. A reader that also runs on the
  host — the host app links Gideon's `ConfigManager` — must therefore check `core_id` against its own
  (and `core_name` as the fallback for a block without an id), not `handover_valid()` alone.

## 4. Where the settings go (T148 D1/D6)

- With a valid block **for this core**, TRX64-Ultimate's application writes its settings to
  `<dir>/<name>/<name>.cfg`, derived from the base name of `path[]`, creating the folder as MultiCore's
  `corefolder_ensure()` does. `/USB1/cores/trx64g.cor` gives `/USB1/cores/trx64g/trx64g.cfg`.
- Without one it uses the flash config pages, as Gideon's firmware does, and touches nothing MultiCore owns.
- The folder, not the old place beside the `.cor` (T93 S6), is the rule; MultiCore's `corefolder_move_cfg()`
  migrates an old file into it once.
- **One application file, not two.** The decision is made at run time from the block, not by a build flag. Two
  applications would need a second `APP_FILE` pin and two `.cor` for the same firmware, kept in step for nothing.
- The content of that `.cfg` belongs to whoever wrote it. MultiCore's generic coreapp uses its own `[section]`
  text format; `trx64g` runs Gideon's application and owns its file. Neither side parses the other's.

## 5. Foreign firmware as a core entry

- Converting a foreign `.cfw`/`.ue2` into a core entry is owned by MultiCore (its host on the board, T137;
  `tools/cor_pack.py` and the CI-side helper `tools/cfw_core.py`). In `cor_pack.py`,
  `ORIGINS = ("trx", "foreign", "backup")`: "foreign" means converted by the host from a `.cfw`/`.ue2`, "backup"
  means read from this board's flash.
- Its purpose is firmware that exists only as a `.cfw` (Gideon 3.15a, Commodore 1.1.0), so the core list has one
  kind of entry instead of two.
- It is **not** the path for `trx64g`/`trx64u`. Those are packed from `c64_basic.bit` + `ultimate.app`, which are
  published beside the `.cfw` and are byte-equal to what it contains — `tools/trx_pkg_check.py` proves that on
  every build.

## 6. DDR2 windows

- The hand-over page at `0x03EFF000` and the cartridge ROM area `0x03C00000..0x04000000` stay as §3 describes
  them.
- **`0x03800000 .. 0x03A00000` (2 MB) belongs to TRX64-Ultimate**, for the buffers of its debug service (U5).
  It lies directly below the app area (`app_format.h`, `U2A_LOAD_ADDR 0x03A00000`, 2 MB) and inside the window
  the updater loads into (`update_trx.cc`, `IMAGE_LOAD 0x03000000`).
- MultiCore's host writes `STAGE_APP` `0x03000000..0x033FFFFF` and `STAGE_PART` `0x03400000..0x03BFFFFF`
  (`host-app/software/hostapp/coreboot.h`) **only before the jump into a core**; nothing of the host lives
  there afterwards. Its generic coreapp uses `0x03000000..0x03C00000` only while updating (`coreapp/src/update.h`,
  `UPD_WIN_LO/HI`) and `0x03C00000+` for cartridge ROMs.
- Two rules follow, and they are the window owner's: it **zeroes the window when the service starts** — after a
  MultiCore boot it holds the remains of a partial bitstream — and it **gives the window up when an update
  starts**. The updater's claim wins; an update reboots anyway.
- Agreed between the two sessions on 2026-10-30.

## 7. The start file (MultiCore T223 D4) and the way back (MultiCore T239.3 D37)

- A large core is started as full firmware, and its hand-over page may not survive the bitstream change. So the
  host also writes `<medium of the .cor>/trx64-start.txt` (`startfile_path()`: the first path element of the
  `.cor` path, `/SD/cores/x.cor` gives `/SD/trx64-start.txt`) just before the jump, for every large `.cor`
  (`trx64u` and `trx64g` included), never for a `.cfw`/`.ue2` (`cfw_is_name()`).
- Text, one item per line: `TRXSTART1`, `cor=<the .cor path>`, `config=<name>` ("" = plain start, at most
  `HANDOVER_CONFIG_MAX` characters), `safe=0|1` (`startfile_format()` / `startfile_parse()` in MultiCore's
  `host-app/software/hostapp/handover.cc`).
- The generic coreapp (`coreapp/src/start.cc` `start_file_take()`) reads it once at its start. It takes it only
  for its own start. With a valid hand-over page, `cor=` must be the page's `path[]`. Without one it looks for the
  file on `/SD`, `/USB0`, `/USB1` and takes it only when `cor=` names a `.cor` whose core folder is its own
  (MultiCore T230 P-2); any other file it drops. It deletes every start file it finds.
- TRX64-Ultimate keeps this: Gideon's application (`trx64g`) does not read it and does not delete it: after a `trx64g` start the file stays
  on the medium, with `cor=` naming `trx64g.cor`, until the next large-core start overwrites it or a generic
  coreapp start drops it. Gideon's application treats it as any file on the medium.
- **The way back.** A large core whose bitstream has the ICAP loader (§8) returns to the host with an IPROG and
  WBSTAR 0. The host's bitstream lies at flash `0x0` (ADR-0055).
  - Just before the IPROG, the generic coreapp writes the menu marker into the hand-over page:
    `handover_menu_request()` puts "MENU" at `HANDOVER_ADDR + 0xFF8` (MultiCore T135 D3).
  - DDR2 keeps the page across the IPROG. The host takes the marker (`handover_menu_take()`) and shows its menu instead
    of starting its default core again.
  - The sequence (MultiCore `coreapp/src/hw.cc` `hw_return_to_host()`): enable on; then into `data`, in file order,
    `FF FF FF FF AA 99 55 66 20 00 00 00 30 02 00 01 00 00 00 00 30 00 80 01 00 00 00 0F 20 00 00 00 20 00 00 00`
    (dummy, sync, NOP, WBSTAR := 0, CMD := IPROG, NOP, NOP); then enable off.
- **Started by the host** means a valid hand-over page (magic, version and sum, §3).
  - The host fills the page before every start of a core. The generic coreapp never wipes it. A `.cfw` start leaves
    a wiped page, and a power-on leaves none.
  - A large core offers "Return to host menu" only when the ICAP loader answers and the page is valid.
  - Without a valid page, the core's bitstream runs at flash `0x0` as a standalone installation. The core then offers
    no way back, keeps its settings in the flash (MultiCore T228 DC13), and runs a `.cfw` itself.
  - Under the host it does not run a `.cfw`: the host's menu runs updates.
- A large core's loader has no partition control. Its TOML A names the block `icap` (ID, data, enable, word count)
  and no block `partition`. So the generic coreapp never tries a partial load there and writes no decouple before
  the IPROG.
- MultiCore's host has both blocks. Its layer-2 cores run inside the host's gateware, so the loader alone makes them
  the host's, with or without a page.

## 8. What the bitstream offers an application

TRX64-Ultimate announces a change to any item of this list before it is pushed (§9). Read from TRX64-Ultimate's
code; the generic coreapp's use of each is MultiCore T104.2.

- **Application load:** the boot ROM copies the flash record at `0x220000` (`{dest, length, run}`, little endian)
  and refuses a length above `MAX_APPL_SIZE` = `0x1E0000 - 12` (`software/bootrom/bootrom.c`). Gideon's
  application links to `ORIGIN 0x00030000, LENGTH 0xE60000` (`target/u64ii/riscv/ultimate/linker.x`).
- **ITU capability word:** `0x25E40202` on the C64U (50T) build (`top.v`: `{2'b00, FPGA_TYPE, 28'h5E40202}`; bits
  29:28 are the FPGA type).
- **TRX register block** at `TRX_IO_BASE`, ID "TRX2": `CAPS` (+0x004) `0xFF`, `CAPS2` (+0x005) `0xF7`, `CAPS3`
  (+0x006) (`trx_io.v`).
- **IEC stub:** the IEC processor region answers version `0x25` at offset 0 and `0x01` at offsets 1 and 2 (both
  FIFOs empty) (`ultimate_sys.vhd`).
- **ICAP loader** (MultiCore T239.3 D37; from the first `c64_basic` bitstream that has it; before it, nothing is
  decoded there and it reads 0). It is a port of MultiCore's host loader (`host/ultimate_sys.vhd` region `r_icap`):
  - The region is `0x10060600`-`0x100606FF`.
  - ID `0x7A` at +0x00.
  - Data at +0x10: the bytes as the file has them. The loader reverses the bits, as `host/top.v` `icap_swap` does.
  - Enable at +0x14. A rising edge clears the word counter.
  - The word counter at +0x18..+0x1B.
  - ICAPE2 X32.
  - There is no decouple at +0x20 and no release at +0x21.
  - The generic coreapp takes the way back of §7 from it. The ID alone no longer tells the monolith from MultiCore's
    host: §7 says what does.
- **HDMI mode request** (TRX64-Ultimate U16, its D7): in the overlay window at `0x10140000` + `0x4F00`: write
  `0x4F00` the row (family * 6 + hdmi, the family part replaced by the System Mode's; a row that does not lock is
  refused), write `0x4F01` the scale (0 Integer, 1 Fit, 2 Fill); read `0x4F04` magic `0x48`, `0x4F05` version 1,
  `0x4F06` flags (b7 the last request was refused), `0x4F07` the row that runs, `0x4F08` / `0x4F09` requests
  written / processed, `0x4F0A` the scale in use (`mode_req.v`). Power-up: row 2 (1080p, 1080p60 in the 60 Hz
  family), Fit. An accepted request restarts the synthesizer and the transmitters.
- **Pixel layer:** `PX_POS` counts from the running row's active area, and the layer is clipped to it
  (`pix_layer.v`; at 1080p the same as before U16).
- **Drive and memory windows** the generic coreapp uses (MultiCore T104.2 §1 P3): `TRX_IO_BASE`, the DDR2 window
  at `0x01000000` (the REU's 16 MB), the ROM aperture `U64_ROMS_BASE` (32 KB: BASIC +0, KERNAL +0x2000, CHAR
  +0x4000, colour RAM +0x7000), the RAM window `C64_MEMORY_BASE`, `C64_STOP` / `C64_MODE` (reset 0x04,
  unreset 0x08), the ITU buttons, `uart_dma` at `0x10060900`, the pixel layer (page 6, `PX_*`).

## 9. What each side may change alone

- TRX64-Ultimate: the gateware, Gideon's application including its config code, the update packages, its own CI.
- MultiCore: `cor_pack.py`, `core.toml`, the `.cfw` container rules, the host, the generic coreapp, the pins.
- A change that moves the other's ground — the hand-over layout or address, the core folder rule, the package's
  file names, the manifest's fields, the start file (including Gideon's application leaving it alone, §7), the
  application's address map or any other item of §8 — is announced before it is pushed.

## 10. Target split (after the next release, MultiCore T230 D3 / D4)

§1 to §9 are the interim state. Alex, 2026-11-16: separating concerns is not enough while a feature change in
TRX64-Ultimate breaks the contract because MultiCore owns the application's menu. The target follows MultiCore
T192 D24 and T213 D1 (option B): the bitstream is described by its API TOML, a service with generic C modules
does the work, chosen and parametrised by that TOML (which is no programming language), and the UI comes only
from declarations (the UI TOML). Alex: *"Let's try it that way then. But first close all loose ends and a
release."*

- **TRX64-Ultimate owns** the bitstream and `trx64u`'s API TOML and UI TOML (the menu; today MultiCore's
  `coreapp/cores/trx64u/menu.toml`).
- **MultiCore owns** the host, the service with its generic C modules, the TOML schemas and the generic op list,
  each versioned. The feature logic of `trx64u` (the Timewarp ring, the monitor, the streams, the U64 registers;
  today in MultiCore's `coreapp/src`: `trx64u_ops`, `ring*`, `monitor`, `monsrv`, `u64cfg` and the stream code)
  becomes such modules.
- A feature change on TRX64-Ultimate's side is a change of its TOML; a feature that needs new logic is a module
  request to MultiCore.
- The contract then covers only these interfaces (the schemas, the op list, the modules' parameters) and their
  versions, not the features behind them.
- Built after the next release. Until the move, a `trx64u` feature change on either side goes through the other
  side as today (§9).
