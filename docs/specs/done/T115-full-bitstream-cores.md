# T115 — The host starts a full firmware from a .cfw: Gideon's original and the large TRX64 Ultimate

**Status:** DONE (2026-11-14, Alex: "Yes", translated). Closed: proven in daily board use (.cfw start). Earlier status: BUILT in the host app (2026-10-21), benched (S1-S3, menu, S10 logos); board S4-S8 not run. Q1-Q4 answered. The mechanism is proven on the board (PoC, §3); what was built and where it differs from D1-D6 is in §7. 2026-11-13 status check: merged to main (D12: trx64 core deleted, host drops it); board S8 and S10 passed 2026-10-28 (todo); S7 (refused .cfw) open; no board record found for S4-S6.
**Repo:** TRX64U-MultiCore — the host app only (`host-app/software/hostapp/`); no gateware (the host's ICAP loader can
already send IPROG, `coreapp/src/hw.cc:316`), no change to `.cor` or the generic application for part A.
**Number:** T115 (registry: `TODO.md`).
**Origin:** Alex, 2026-10-21, after the PoC started Gideon's 3.15 from a vanilla `.cfw` on the C64U: "Mega - make a softload
function for 'large cores' or 'legacy cores' or whatever out of the PoC ... that only concerns the host app, right? Write the
spec, coordinate with the MultiCore session, build" (translated). Before that: "Can we load Gideon's complete vanilla
bitstream through ICAP when it lies in the flash file system, and then start it?" and "it should take a real .cfw, everything
is in it" (translated).
**Depends on:** T84 (host, ICAP loader, boot ROM magic), T79.1 (host contract). ADR-0058 (proposed, coordinator) records
the decision. "T112" here means MultiCore's T112; TRX64U has its own T112 (ESP32 recovery).
**Amends:** ADR-0055 ("Foreign full bitstreams (Gideon's original) are not offered") and T84 D7/Q3 ("needs its own flash
layout"): the PoC shows it needs neither its own layout nor the ESP32.
**Milestone:** M3.

---

## §1 The problem

**Who:** Alex, and every C64U owner with our host in flash, who wants Gideon's original firmware (the full Ultimate, his
menu, his updates) without a PC, a JTAG cable or reflashing; later the same for our own cores that do not fit layer 2.

**What exists:**
- The host starts `.cor` bundles: a **partial** bitstream into layer 2 through ICAP, then the bundle's application
  (`coreboot.cc`). A core larger than `pb_l2` cannot be a `.cor`. (The Amiga 1200 was the example until run 914 routed it at
  78.52 % after the BRAM levers of T97 5.11; it is no longer the case.)
- A full bitstream replaces the host itself, so the host cannot stream it through ICAP. The FPGA can load one from SPI flash
  on its own: `WBSTAR` + `IPROG` through ICAP (UG470 MultiBoot). The host already does this with `WBSTAR = 0`
  (`hw_return_to_host`).
- Gideon's boot ROM (`bootloader_u64ii.c:168`) jumps to `[0xFFF8]` when `[0xFFFC] == 0x1571BABE`; otherwise it loads the
  application from flash 0x220000, which on our boards is the host app.

**Parts:**
- **P1 Gideon's original.** Start his complete firmware (bitstream + application) from a vanilla `.cfw` from the host menu;
  back to the host by a power cycle.
- **P2 The large TRX64.** The TRX64 Ultimate monolith (its release `.cfw`, `trx64_update_<commit>_<date>.cfw`) started the same
  way: a `.cfw` with the same layout (Q4). Its boot ROM checks the same magic first (TRX64U `software/bootrom/bootrom.c:372`,
  after a test of a few words at 0x10000) and its DDR2 controller is ours, so no address map.

## §2 Scenarios

```gherkin
Scenario: P1 Gideon's original from a .cfw
  Given the host in flash and Gideon's 3.15a .cfw on a stick
  When  Alex picks the .cfw in the host menu and confirms
  Then  the host checks the .cfw, puts its 50T bitstream into the flash drive as one contiguous file (first time only),
        writes his application into DDR2, sets the boot magic and sends IPROG
  And   Gideon's menu comes up with network and REST; a power cycle returns to the host

Scenario: P1 refused
  Given a .cfw for another FPGA, a damaged .cfw, a full flash drive or a fragmented file
  When  Alex picks it
  Then  the host says what is wrong and stays; nothing is loaded
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | `.cfw` parser: finds the 50T `.bit` and the application record; refuses 100T-only, truncated, wrong family | bench (`tests/hostapp`) |
| S2 | P1 | address map `gideon_bl4`: the measured permutation (§3) and its inverse; magic word at 0xFFF8 maps to itself | bench |
| S3 | P1 | flash file: FAT cluster chain contiguous → flash address; fragmented → refused | bench (fake FAT image) |
| S4 | P1 | board: pick the `.cfw`, Gideon's 3.15 boots, REST answers, power cycle → host | board |
| S5 | P2 | the TRX64 Ultimate release `.cfw` picked in the host menu boots the monolith (identity map); power cycle → host | board |
| S6 | R2 | flash dump 0x400000-0xFFFFFF before and after one session of Gideon's firmware (including a settings save); negative control: the same dump pair across a host-only power cycle shows no difference. Before S4 | board |
| S7 | P1 | one refusal on the board: a fragmented file, or a `.cfw` whose bitstream is for another part; nothing is loaded | board |
| S8 | R1, R2 | after S4, a power cycle returns to the host, and its settings (`boot.cfg`), the flash drive and the host app slot are intact | board |
| S9 | P1 | after S4, Gideon's picture is whole: in his VIC stream the background starts at x 32 and column 0 is visible; negative control: the start with our synthesizer setting (dd43af43) or the boot ROM's (run 921) puts the border 8 px right | board |
| S10 | D7 | a `.cfw` row shows a logo: Gideon's firmware and the TRX64 firmware their built-in logo, an unknown design the generic firmware icon, a `<name>.icon` beside the `.cfw` replaces it; negative control: a `.icon` of the wrong size is ignored (built-in logo stays) | bench (`tests/hostapp`, render) + board |

## §3 What the PoC measured (2026-10-21, board, MC host run 406 in flash)

PoC: `tools/legacy_boot/` (bare-metal RISC-V program loaded over JTAG, `.cfw` staged in DDR2 over JTAG).

1. **IPROG from a FAT file works.** Gideon's 50T `.bit` as `/Flash/G315A50T.BIT` (2,192,121 bytes, FTP) lay at flash 0x411000,
   0x218 contiguous 4 KiB clusters, byte-equal to the `.cfw`'s. `WBSTAR = 0x411000`, `IPROG`: `BOOTSTS = 0x105`
   (`VALID_0`, `IPROG_0`), no fallback.
2. **DDR2 survives the reconfiguration completely.** 2 MiB of xorshift pattern written by our host, read back through
   Gideon's USER4 chain after his boot: 16,384 of 16,384 words of the first 64 KiB and the last 16 KiB present, nothing lost.
3. **The two DDR2 controllers place addresses differently.** Gideon's moves 8-byte units (BL4), our `ddr2_lean` 16-byte (BL8,
   `OPT-ddr2.md` §7.2). Measured: only CPU address bits 6:3 differ, `ours[3] = gideon[6]`, `ours[6:4] = gideon[5:3]`, every
   other bit equal (bit by bit for bits 2-19, then 300 of 300 random addresses). The magic word 0xFFF8-0xFFFF maps to itself
   (bits 3-6 all one), which is why the boot ROM saw the magic before the map was known.
4. **Gideon's 3.15 starts.** With the application copied under that map: his console `*** Ultimate 64-II (V1.50) 3.15 ***`,
   USB, network; REST answers on 192.0.2.189.
5. Gideon's boot ROM writes 0x100 (calibration) and 0x10000-0x2FFFF (RAM test) before it checks the magic; nothing the
   handover needs lies there.
6. Tooling found on the way: Gideon's USER4 chain takes writes only when IR `USER4` is loaded again before each data scan,
   as `recover.py` does; `1541ultimate-tools/tooling/u64ii_jtag.py` skips that (reads work, writes are lost).

## §4 Design (part A = P1)

- **D1 Source.** A vanilla `.cfw` (Gideon's pre-header `DE AD 15 41`, family `U64MK2`; the record holds the updater image with
  the blobs). The host takes the `.bit` built for the running FPGA: the IDCODE in the bitstream's configuration words
  (register write to IDCODE) must equal the running IDCODE; the header's part text is only a name. It takes the application
  record after the last bitstream (`{load, length, start}`); a load or start other than 0x30000 is refused. S1 covers both
  refusals. As the softboot app of the 1541ultimate fork finds them (`apps/softboot/src/softboot.c` there). Every
  other blob (ESP32, ROMs, HTML) is ignored.
- **D2 Flash file.** The flash drive holds one scratch file, `/Flash/legacy/fullcore.bit`: the bitstream of the `.cfw`
  started last, a working copy and no installation (Q1). Starting a `.cfw` whose bitstream differs rewrites it; starting the
  same one again finds it equal and writes nothing. On **every** start the host checks
  it again, because any later write or delete on the drive can move it: size equal to the `.cfw`'s bitstream, then the cluster
  chain is one run (a gap → refused, "flash drive fragmented"; after the first write the file is deleted again), then SHA-256
  of the file against the `.cfw`'s bitstream (about 2 MB; a difference is refused and rewritten only after a confirm). The flash address
  is `0x400000 + (data start sector + (first cluster - 2) * sectors per cluster) * 4096` (`blockdev_flash.cc:17-28`: sectors
  are linear from the flash drive's start).
- **D3 Address map.** The application is staged permuted (`stage[map(off)] = app[off]`, the map keeps every 128-byte block in
  place, so load 0x30000 and the staging area being 128-byte aligned is enough). The map follows the bitstream: a bitstream
  whose configuration words write the AXSS register is a TRX64 build (T84 D5b; `tools/cor_pack.py hostkey` reads it) and gets
  `identity`; one without gets `gideon_bl4` (§3.3). Checked 2026-10-21: Gideon's 3.15a writes no AXSS, TRX64U rc1
  (692f643d) writes 0x553525D3.
- **D4 Handover.** The host app itself runs from 0x30000 (`hostapp/linker.x`: 0x30000, 0xE60000 bytes), where Gideon's
  application goes, so the final step runs from a trampoline (as `core_jump`, `coreboot.cc:217`). Staging (0x03000000,
  `STAGE_APP_ADDR`) and trampoline (0x03F00000) lie outside 0x30000 + the application's length and outside Gideon's boot-ROM
  scratch (0x100, 0x10000-0x2FFFF, §3.5); all three are 128-byte aligned, so the map's permutation never crosses into them.
  The trampoline copies the staged image to 0x30000, write `[0xFFF8] = 0x30000`, `[0xFFFC] = 0x1571BABE` (both through the map; they map to themselves), then the IPROG
  sequence to ICAP with `WBSTAR` = the file's flash address. Interrupts off, network off, pixel layer off before, as
  `core_jump`. No handover page (Gideon's application does not read one).
- **D5 Menu (Q1).** `.cfw` files lie beside the `.cor` bundles and are found and listed the same way (T101 discovery), marked
  as full firmware; picking one checks it, puts its bitstream into the scratch file (D2) and starts it. Confirm text: back only
  by switching off and on (nothing about Gideon's updater, Q2).
- **D6 Return.** A power cycle loads 0x0, the host. Nothing else returns (Gideon's bitstream has no way back).
- **D7 Logo (Alex, 2026-10-21: "yes, 2 and 1 optional", translated, on the choice built-in logos by design name /
  a sidecar file / a .cor wrapper).** A `.cfw` has no icon part. The host app picks a built-in 32 x 32 RGB565 logo by the
  design name of the `.cfw`'s `.bit` header (field a, as `cfw_probe` reads it): Gideon's firmware gets the standalone U64 mark,
  TRX64 gets `cores/trx64/logo/`, and any other design a generic firmware icon (T114). Optional: a file `<name>.icon`
  (the `.cfw`'s name with `.icon` for `.cfw`, 2,048 bytes, RGB565 little endian) beside the `.cfw` replaces the built-in one;
  another size is ignored. No manifest check for it (a `.cfw` has no manifest).

## §5 Risks

- **R1 Gideon's updater inside his firmware** writes 0x0 and 0x220000 (our host and its app) without our flash gate (his
  bitstream has none). After that the host is gone and cannot notice anything. Accepted, nothing is done against it and the
  confirm screen does not mention it (Q2, §7).
- **R2 Shared flash.** His application mounts a FAT drive and uses the config pages 0xFE8000-0xFFFFFF. On the C64U (50T) his
  stock drive starts at 0x400000 like ours (`docs/c64u-flash-layout.md` in TRX64U, read from his flash); on the 100T at
  0x580000 (T53 section 1). A drive he mounts at another base or geometry, a format, or a settings save can destroy our flash
  drive, the `.bit` file in it, or `boot.cfg`. Measured by S6 before S4.
- **R3 Other Gideon versions** may have another DDR2 controller and so another map; measured for 3.15a only. Accepted (Q3):
  every `.cfw` starts with `gideon_bl4`; a version with another map shows a black screen, a power cycle returns, and the map
  is measured again with `tools/legacy_boot` and added then.

## §6 Out

ESP32 involvement; signing; Gideon's 100T bitstream (no 100T board); a warm way back from Gideon's firmware.

## §7 Decisions taken while building

- **`cores/c64u` stays as a source tree, never built (Alex, 2026-10-25).** "lass doch einfach c64u verzeichnis drin aber BAU
  ihn nie" ("just leave the c64u directory in, but never build it", translated). *Recording error, corrected 2026-11-08: this sentence was
  about `c64u` only; the first version of this record extended it to `cores/trx64`, which Alex never said. `cores/trx64` was deleted
  (D12).* The C128 on another branch reads `cores/c64u`, tests read the `cores/trx64` files of D12. Both lost their `core.toml` (the pipeline finds cores by it, so neither gets a stage, a `.cor`, a release line or an app build;
  the `app_ref` variant build of c64u goes with it) and sit in `ignore:` of `host/layers.txt`; the five trx64 modules the host reads stay in `shared:`.
  The platform logos of both stay. TRX64 is used for its `.cfw`; since T114 D5, Gideon's `.cfw` uses the separate U64 mark and C64U remains its own platform identity. Supersedes the "still ordinary cores" wording of the c64ref entry.
- **The extended `.cor` for large cores comes first (Alex, 2026-10-25, late):** "Das .cor-Format, um die Large Cores zu erweitern,
  wäre viel essentieller, als die bereits gebauten Cores nochmal auf die SD zu kopieren ... und dann aber auch durchziehen und
  ausliefern, damit ICH es morgen testen kann" ("extending the .cor format for the large cores is far more essential than copying the
  cores already built onto the SD again ... and then see it through and deliver it, so that I can test it tomorrow", translated).
  Order: format + packer, host-app reader (start through the T115 path: flash file, application into DDR2, IPROG), the Amiga 1200 as
  the first large `.cor`, delivered for a board test.
- **Large cores are packages; checks only on adding (Alex, 2026-10-25).** A large core is a package like a DFX core (`.cor`). The full
  hash and the format checks run only when an entry is added to the boot menu; at every start the host app checks, for every entry,
  only whether the package is still intact or was modified, and for DFX cores the contract (API). Open: how a `.ue2`/`.cfw` is
  handled (possibly a `.cor` made from it when it is added, otherwise ignored). The per-start check reads the size and modification
  time of the directory entry (kept when the entry is added) and the package header (manifest with its SHA values, contract, host
  key); no full hash at start (Alex: "a+b"). Quotes in the T115 history.
- **C64 firmware comes from outside (Alex, 2026-10-25).** The C64U and TRX64 cores leave this repository: Gideon's 3.15,
  Commodore's 1.1.0 and the TRX64-Ultimate repository supply full C64 firmware as `.cfw`. The large cores built here are
  Amiga 500, Amiga 1200 (AGA), NES and C128. Supersedes the TRX64/C64U part of the next entry. The slim C64U reference of
  the next entry stays, as its own core `c64ref` (C64 Reference): built only to route the host against, never shipped (no
  `.cor`, no release set, no boot menu entry). Quotes in the T115 history.
- **Separation of concerns (Alex, 2026-10-25).** The host is routed with the null core as its reference (T94 D8 amended);
  TRX64, C64U (Gideon's set), Amiga 1200, Amiga 500 and NES become large cores (full bitstreams by IPROG), with the `.cor`
  format extended for them and the hand-over made faster (the check of a flash image runs once, not at every start); every
  other core is built against the new host after it routes; the pipeline builds all of it. The JTAG debug master stays in
  the host for now (Alex: "Erstmal JTAG rein", "JTAG in, first"). The reference core is not the null core but a slim C64U
  that only exists to route the host against (core 0, never started): without REU, turbo, sampler, datasette and UDP streams,
  the 8-SID reDIP kept (Alex, asked whether SID8 stays since two SIDs save nothing: "ja"). Quotes in the T115 history.
- **The four build deviations are accepted (Alex, 2026-10-21): "The deviations are ok."** (translated): byte compare against the
  SPI flash instead of SHA-256; a differing or fragmented file is rewritten without asking; the running IDCODE is the 50T constant;
  a countdown or network start of a `.cfw` asks nothing, RETURN/S asks.
- **Who builds (Alex, 2026-10-21): the MultiCore session, in the host app.** "Build it, yes, that removes a lot of pain and makes
  MultiCore 'perfect' in my opinion" (translated). TRX64-FW handed over after the spec; no code from there.
- **Q1 (Alex, 2026-10-21): beside the `.cor` files, picked like a core, the bitstream only transiently in flash.** "The .cfw
  should lie beside the .cor and be selectable, and then go through the loader transiently into flash and then be started"
  (translated). → D2 (one scratch file, rewritten only when the bitstream differs) and D5.
- **Q2 (Alex, 2026-10-21): nothing against Gideon's updater, no warning.** "We do nothing at all, whoever flashes over it has
  bad luck :-)" (translated). → R1 accepted.
- **Q3 (Alex, 2026-10-21): every `.cfw` starts, no version list, no question.** "A, if Gideon changes his layout with a 4.x
  we adapt" (translated). → R3.
- **Q4 (Alex, 2026-10-21): the large TRX64 is part of T115.** "The Gideon vanilla core and the 'large TRX64' would be nice to
  load as a 'large core' ... they are just .cfw with the same layout" (translated). Checked: TRX64U's `.cfw` has Gideon's
  pre-header, one 50T `.bit` (compressed) and the application record after it. → P2, D3 (map by AXSS), S5.

### Built (MultiCore session, 2026-10-21): what the host app does, and where it differs from the text above

Files in `host-app/software/hostapp/`: `cfw.cc/h` (parser, map, IPROG bytes), `fatwalk.cc/h` (FAT12/16 chain walker), `legacy.cc/h` (staging, flash
file, handover), `legacy_tramp.cc` (the code that runs from DDR2), plus the menu in `hostui.cc`; `discover.cc` lists `.cfw` beside `.cor`. All of the
pure code is in files that include no firmware header, so the benches build it natively. The code is the check; these are the points where it is
not the same as D1-D6 above, or where D1-D6 left a choice:

- **D1 parser.** The first `.bit` is looked for in the first 4 MiB; a hit counts only when the fields a-d, `e` and the length behind the magic make
  a header (TRX64U rc1 has the magic bytes inside the updater's code at +0x39B54, followed by `a` with length 0). Every `.bit` of the chain (the
  next one or the record must follow within 16 bytes) is read to its IDCODE (register write 0x0C in the configuration packets) and compared with the
  running one **without the version nibble** (mask 0x0FFFFFFF); the text of field b is only the name. Wrong-family test: a `.bit` that says `7a50t` over
  the 100T's IDCODE is refused, one that says `7a100t` over the 50T's is taken. The record: the exact one (load and start 0x30000) wins over the
  next `.bit`, which wins over a record that loads elsewhere (refused as `LOAD`); a window whose load is 0 is never a record (a zero word in front of
  the record shifts a window onto `{0, load, length}`; found by the menu bench with a small application). The walk of the packets stops once IDCODE and AXSS are known (a probe of the real rc1 file with its 1.8 MB compressed stream costs 75 reads of the medium, the real 3.15a 68, most of them the 4 KiB scan to the first `.bit`; a walk to the end would be thousands; bench counts the reads). Both real layouts reproduce the measured
  offsets exactly (Gideon 3.15a: `.bit` +0x2FCB0, 0x2172F9 B, record +0x5ED0A0, 0x12C978 B; TRX64U rc1: `.bit` +0x42AD8, record +0x20365C, 0x15A92C B,
  not the decoy record at +0x42AA8) and `test_cfw --real <.cfw>` gives the same on the real files (3.15a, rc1, and the TREX 20260925 firmware).
- **Running IDCODE is a constant** (`CFW_IDCODE_XC7A50T`, 0x0362C093): no register of the layer-1 ICAP loader reads it back and the host app is a
  50T program (ADR-0015). Measured in the real files: 50T 0x0362C093, 100T 0x03631093.
- **Menu name** is `<design> <date> <time>` from the header fields a, c, d, e.g. `u64_mk2_artix 2026/09/10 11:00:57` and `top 2026/10/10 18:23:21`.
  The row says `full firmware` (not a version), the tile `FW`, the badge `found` / `ready`; a `.cfw` the parser refuses is listed with `unusable`, and
  the browser's right panel says why (the parser's sentence).
- **D2 flash file.** The file is compared with the bitstream **byte for byte against the SPI flash** at the address the cluster chain gives, not by
  SHA-256 of the file (the same guarantee, since it reads what the FPGA will read, without hashing 2 MB twice). A file that differs in any way
  (missing, other size, other bytes, chain not one run) is **replaced without asking**: another `.cfw` of the same size is the normal cause (every
  uncompressed 50T bitstream has the same size), the file is ours and a scratch copy. After a write the check runs again; only then a gap refuses
  ("flash drive fragmented ... free space on the flash drive", the file deleted again) and nothing is loaded. An existing file found fragmented on a
  later start is rewritten once the same way (it can be laid down again contiguous) instead of refused at once. Flash address = drive start
  (`get_image_addresses(FLASH_ID_FLASHDRIVE)`, 0x400000 on the 50T) + the volume offset the walker returns; with the PoC's volume (4096-byte sectors, 1
  per cluster, 1 reserved, 1 FAT of 2, 512 root entries) cluster 12 is 0x411000, the address the PoC measured.
  **Fix 2026-10-24 (board: the rewrite was refused as fragmented three times in a row, on a drive emptied down to ~350 KB).** FatFS lays a file out
  from its last-allocated-cluster hint (`ff.c` `create_chain`: `last_clst`, reset to "none" at every mount, so the first write after a boot starts at
  the bottom of the drive and takes whatever clusters are free there). A 2.2 MB file therefore need not be one run; Alex saw that a second start
  right after the refusal always worked, which fits the hint (the failed attempt moved it behind its own chain) - consistent with the code, not
  measured. `write_scratch` no longer depends on the hint: after the delete and the create it reserves the run with `f_expand(fp, size, 1)` (one
  free run of the whole size, allocated at once) and writes over it; the check afterwards stays. `f_expand` needs `FF_USE_EXPAND 1`; the Ultimate
  application's `chan_fat/full/ffconf.h` (submodule) stays untouched, `host-app/software/hostapp/ffconf.h` includes it and switches the one option on
  (the Makefile's `-I software/hostapp` comes first; the core app keeps the original; the option adds a function, no structure). If no free run of that
  size exists (`FR_DENIED`) the start is refused with "flash drive: no free run of N KiB (M clusters) in one piece. Tidy the flash drive ... and start
  again" on the start panel and the console; nothing is deleted automatically. Bench `tests/hostapp/test_fatexpand.cc` (the real `ff.c` over a RAM
  disk): a plain write after a remount over scattered free clusters is not one run, the reserved one is, a drive with plenty of free space but no run
  of 101 clusters gives `FR_DENIED` and allocates nothing; control: without the reservation the bench goes red. Not run: the board.
- **D3 staging.** The application is read in 32 KiB chunks and copied in 8-byte units to `STAGE_APP_ADDR` under the map; the last chunk is padded with
  zeros to 128. Both real layouts decide the map the same way as the text: no AXSS write -> `gideon_bl4` (3.15a), AXSS written -> `identity` (rc1).
  The walk follows the packets and skips the frame data, so a bit pattern that looks like an AXSS write inside the frames does not count (bench).
- **D4 handover.** The boot magic (`[0xFFF8]` = 0x30000, `[0xFFFC]` = 0x1571BABE) is written **in C before the trampoline** and read back, not by
  the trampoline (fewer lines of code that nothing can test off the board); the trampoline (`legacy_tramp.cc`, 100 bytes, position independent, no
  stack) copies the staged application to 0x30000, reads the last word back, releases the decouple, enables the ICAP loader and sends the 36 bytes
  `cfw_iprog_seq()` built (the bench compares them with the PoC's sequence for WBSTAR 0x411000). Quiesce as `core_jump`: pixel layer, RMII, IRQs.
- **D5 menu.** The confirm panel says "<name> replaces the host. The way back is to switch the power off and on." and nothing about Gideon's updater (Q2).
  It is asked when a person presses RETURN or S; a countdown or a request from the network (nobody to ask) starts a `.cfw` entry at once, like a core.
  A `.cfw` can be added to the boot menu (A, or RETURN in the browser) and be the default; the entry carries no hash (the bitstream is compared at every
  start). Progress uses the start panel with its own step names.
- **D4 clocks (2026-10-21, after S4 and S9 on the board; never effective, replaced by D8).** Before the trampoline the host writes both clock synthesizers (CDCE913, 0xC8, 1V8 and
  HDMI bus) back to their **factory default** from register 0x14, `ED 02 01 01 00 40 02 08` (TI SCAS849I, 7.3.2 and table 9-4: PLL1 bypassed
  and powered down, every output the input clock), then waits 20 ms: the state a cold boot gives the firmware's bitstream until its boot ROM
  (`init_ext_pll()`) programs them. Why: with dd43af43 (synthesizers on our setting, `cdce_i2c.v`) Gideon's 3.15a came up with the leftmost
  character column hidden; `$D016` = 200 and `$D011` = 27 (typed `PEEK`s over his REST), screen RAM right, and one frame of his VIC stream
  showed the border 8 pixels right of the character grid (background x 35-354, column 0 at x 27), in the VIC's own output. On a cold boot Alex
  never saw that ("I worked with it for months", translated); the JTAG PoC shows it too, the TRX64 rc1 `.cfw` (own VIC) does not. Not the
  cause: his HDMI Scan Resolution and System Mode toggled over REST, a C64 reset, and the boot ROM's own values written before the IPROG
  (run 921, S9 failed). With the factory default (written over JTAG I2C, `tools/i2cx.py`, during the staging of dd43af43) the stream showed
  the background at x 32-352 and `READY.` whole; Alex: "It works!!!!!" (translated). UG470 (Chapter 7, IPROG): IPROG resets the FPGA like
  PROGRAM_B except WBSTAR, TIMER and BOOTSTS, so the difference lies outside the FPGA. Our gateware does not write the synthesizers back on
  its own here: `cdce_i2c` restarts only on a lock loss of `mmcm_sys`/`mmcm_ref`, which run from the crystal (`top.v:209`, `811`).
- **S9 again (2026-10-24).** With the host app that carries D4 clocks (6d9f3d2, writes `(0, 0)`) and once more with the bytes written by
  hand 4 s into the staging as on 2026-10-21, Gideon's 3.15 came up with column 0 hidden both times. D4 clocks does not fix S9; the
  whole picture of 2026-10-21 had another cause, not found yet. History in `docs/todo/T115.md`.
- **S9 series and boot delay (2026-10-24).** `tools/s9_series.py` (JPROGRAM back to the host, JTAG I2C dump, one frame of Gideon's
  VIC stream per start): 4 of 20 starts whole (background x 32), 16 shifted (x 35 or 36); the I2C dump (both CDCE913, 0x1A, 0x20,
  0x21) is byte-identical in all of them, so the host leaves the same outside state every time. Gideon's boot ROM
  (`bootloader_u64ii.c` `main`) programs the synthesizers (`init_ext_pll`, then 2 ms) and on a cold boot reads the application
  from flash before it jumps; on the boot magic it jumps at once. Test of that difference (Alex: "Mach das"): the magic jump goes
  to `tb_delay` (`legacy_tramp.cc`, copied to TRAMPOLINE_ADDR + 0x400), 50,000,000 turns of a two-instruction loop, then to
  0x30000. Measured with the same series and the host app over JTAG (`--app`).
  Result: 2 of 20 whole, 18 shifted; the firmware's REST came 28.9 s after the host's on average (30.5 s without), so no
  added wait shows and the loop's length on his CPU is unknown. No effect at this length; the change is not kept in the host app
  (a test with a delay that shows in the times, about 10 s, is the next step if the timing idea is pursued).
- **S9 with the 09/10 bitstream, cold against host (2026-10-24 evening).** The state-logging firmware with Gideon's 09/10 bitstream
  (`trex-s9state-bit0910.cfw`) flashed: 5 cold boots, all whole (x 32), the state file equal in all 5 apart from free-running values
  (uninitialised RAM at 0xFFF8, timers, `D012`, `U64_CLOCK_FREQ` +-1, PHY status bits). The same firmware from the host
  (`--app hostapp_expand.bin`): 1 of 10 whole. A whole and a shifted host start have the same state file bit for bit; after the
  firmware's own init (phase b) the host starts equal the cold ones except one output latch bit of expander 0x42 whose pin is an
  input. One difference that is not noise: his application reaches its first init step at 981 ms on a cold boot (5 of 5) and at
  307 ms from the host (all). Tests, each a series from the host:
  - wait block (`tb_wait`, 128 bytes behind the staged application, the magic jump goes there; it waits 674 ms on his
    millisecond timer, then jumps to 0x30000): the first init step at 981 ms as on a cold boot, 1 of 8 whole;
  - both CDCE913 powered down while the PLL bytes are written, register 0x01 to 0x00, 500 ms before the IPROG (host app):
    2 of 8 whole; register 0x01 of the HDMI-bus part still read 0x08 at his first init step, so that write did not take;
  - register 0x01 of both parts set to its cold value 0x00 over JTAG I2C after each host start (`--cdce-cold`): 0x00 at his
    first init step in 10 of 10, 2 of 10 whole.
  Every host series lands at about 1 in 7 whole (4/20, 2/20, 1/10, 1/8, 2/8, 2/10) whatever is changed; the border is 8 pixels,
  one phi cycle, off the character grid. Read: a phase between two clocks that his 09/10 design takes at its start and that is
  fixed on a cold boot, not a register value. In the Kawari VIC of TRX64 the border comparator (`border.v`, `xpos`) and the
  cycle counter (`clk_phi` from the `phi_gen` ring in `vicii.v`) run on one clock and one reset, so this cannot happen there.
- **S9 cause found (2026-10-25, night).** Measured, each line a series of one VIC-stream frame per start (x 32 whole, x 35/36 shifted):
  - vanilla 3.15a flashed at 0x0/0x220000 (bitstream and application from `update_v3.15a.cfw`, over JTAG), warm restart by JTAG
    JPROGRAM, Gideon's own state before: 15 of 15 whole. Gideon's 09/10 bitstream has no ICAP behind 0x10060600 (all reads 0) and
    `W25Q_Flash::reboot` sends the Spartan-6 sequence; his U64-II restarts are power cycles through the ESP32 (`wifi_machine_reboot`,
    `rpc_dispatch.c` cmd_reboot), so in the field this bitstream only ever starts at power-on;
  - our host (bitstream of `trx64host_update_54e63c47` loaded into SRAM, host app over JTAG) running first, then JPROGRAM into the same
    flashed vanilla 3.15a: 2 of 15 whole. The cause is a state our host leaves; IPROG, WBSTAR and the magic jump are not involved;
  - as before, but both CDCE913 set over JTAG I2C to the values Gideon's firmware runs with: 15 of 15 whole; only the video PLL
    (1V8 bus on this board, rev 0x17): 8 of 8; only the HDMI PLL: 3 of 8; the video PLL to the boot ROM's own bytes (`init_ntsc`,
    `57 00 03 03 02 D0 22 08`): 8 of 8;
  - the real handover (host from SRAM, countdown, `.cfw` of 3.15a, IPROG) with the video PLL set to the boot ROM's bytes over JTAG
    right after the host is up: 10 of 10 whole (without: 2 of 5 and 2 of 6 in the same set-up).
  Cause: our host gateware programs the video PLL with both output dividers at 1 (`cdce_i2c.v`, 0x16/0x17 = 01 01, the 148.87 MHz
  pixel clock straight from the VCO); Gideon's boot ROM moves them to 3 (`init_ext_pll`). Coming from 1, the 09/10 bitstream comes
  up with its border one phi cycle (8 pixels) off the character grid in about 6 of 7 starts; coming from 3 or 5 (his own runtime
  value, PAL) it is whole. The 09/29 bitstream (trex/main) and Commodore's 1.1.0 (02/28) are whole from the host either way (12 of 12,
  3 of 3). **D4 clocks never reached the chips:** `app_i2c.v` cuts every write the application addresses to 0x64 off the pins and
  ACKs it (read back over JTAG: 0x16 stayed 01 until the IPROG), so the factory default was never written and the whole picture of
  2026-10-21 was one of the 1 in 7. Fix: the host gateware writes the boot ROM's PLL bytes before the handover (a host change, frozen:
  decision open, see the history). Tools: `tools/s9_series.py` (`cdce_gideon`, `--cdce-cold`, `--cdce-pdn`), the scripts of the night
  in the session scratchpad.
- **D8 clocks, replaces D4 (2026-10-25, decided by Alex).** "Mach das so. Bau dann alle Cores neu nach Host+trx64" ("do it that way;
  then rebuild all cores after host + trx64", translated), to the proposal to put the S9 fix and the rvlite fix (T97 5.13) into one host
  build. `app_i2c.v` gets a CDCE-open bit (+8 b1 beside scan_enable; every write to +8, rst and soft reset set it from the written bit / clear it): with it set a write to 0x64 reaches the pins like any
  other, without it the old rule (cut off, ACKed). `legacy_clocks` (`legacy.cc`) sets it as its last I2C use before the hand-over and
  writes the boot ROM's own PLL bytes: `init_ntsc` `57 00 03 03 02 D0 22 08` to the video PLL, `init_hdmi_60` `57 00 01 01 55 F7 82 89`
  to the other one (video PLL on the HDMI bus on board rev 0x15, on the 1V8 bus otherwise, as `init_ext_pll`), then 20 ms. Why: S9 cause
  above; the same bytes written over JTAG gave 10 of 10 whole on the IPROG hand-over. Bench `tb_app_i2c` (CI `app_i2c`): a write to 0x64
  is blocked while closed, reaches the synthesizer while open, is blocked again after closing and after a soft reset; control (the
  open bit ignored): 4 errors. Host build with T97 patch 0007 in the same pin change; the cores are routed against it afterwards.
- **D8 build (2026-10-25).** CI run 960 (main 169a865ec, the bit at +9) did not route the host: the router oscillated (overlaps per
  global iteration 18k, 15.7k, 20k, 26k, never converging); trials on the builder with `AlternateCLBRouting`, with
  `AltSpreadLogic_high` and incremental from the last green host (placement fails, `Place 30-504`) did the same or worse; the exact
  `t97-rvlite` tree (0007 without D8) routes (WNS +0.056, overlaps from 12.4k to 0 in every iteration). pb_l1 78.24 % -> 78.25 %: two
  LUTs tip the host's routing. Alex: "Versuch 1. Danach können wir den floorplan und den contract anschauen. Weil wir Single/Full Cores
  laden können. Die großen, kritischen als Single bauen können" ("try 1 [the bit in the existing register]. Afterwards we can look at
  the floorplan and the contract, because we can load single/full cores; the big, critical ones can be built as single", translated).
  So the bit moved to +8 b1 (no new decode).
- **D7 logos (2026-10-21).** `cfwicon.cc/h` (pure) picks the logo, `hostui.cc` calls it for a `.cfw` row (boot menu entry or found bundle) once the
  parser accepted the file; a refused `.cfw` keeps the text tile. Built in by the design name `cfw_probe` made, the word before the first space
  (date and time change with every build, so they are not part of the rule). Measured in real files (`.bit` header field a, read with a scan for the
  header magic; the board was off, so the copies on disk, not the board's, see below): Gideon's firmware `u64_mk2_artix` (50T and 100T `.bit` both, date
  2026/09/10, from `vanilla-3.15a-ef9937a.cfw` and the TREX firmwares `trex-fw-20260925-3.15.cfw` and two `update.cfw`); the TRX64 firmware `top`
  (`trx64_update_692f643d_20261010.cfw`, 2026/10/10 18:23:21, and the CI host updates `trx64host_update_8c154e93_20261014.cfw`,
  `trx64host_update_5c299ad3_20261021.cfw`). Rule: `u64_mk2_artix` (prefix) -> `tools/core_logos/u64/icon.rgb565`; `top` (whole word) ->
  `cores/trx64/logo/icon.rgb565`; any other design -> `tools/core_logos/firmware/icon.rgb565`, a generic chip mark with "FIRMWARE .cfw" made by
  `tools/core_logos/build.py` in the style of T114 (`--check` covers it). The 2026-10-22 temporary mapping of Gideon's firmware to the C64U
  logo is superseded by Alex's 2026-10-26 decision that U64 and C64U each have their own mark (T114 D5). "top" is Vivado's
  default module name: a foreign design called `top` would get the TRX64 logo (a sidecar corrects it). Embedding: `tools/gen_cfw_icons.py` writes
  `host-app/software/hostapp/cfw_icons.h` (three 2048-byte arrays from the three files, checked in); `tests/hostapp/run.sh` runs its `--check` (the
  header must be what the files give now) and `test_cfwicon` compares the arrays with the files too; no build-system change besides the new source
  in the Makefile. Sidecar: `<name>.icon` beside the `.cfw` (the `.cfw` extension in any case replaced by `.icon`), read through `FileSource`; it counts
  only at exactly 2048 bytes and when it reads, any other size or none -> the built-in logo. No manifest, no hash. It is read at the scan, so a new
  `.icon` shows after R (rescan) or the next start of the menu. Bench: `test_cfwicon` (37 checks; names, arrays = files, sidecar 2048 wins, sizes
  2047/2049/4096/0/1 ignored, path rules) with controls `icon_sidecar_any_size`, `icon_gideon_unknown`, `icon_top_prefix`, `icon_sidecar_ignored`; the
  render bench (third tree `_ic`: Gideon with and without a sidecar, an unknown design, TRX64, TRX64 with a 2049-byte sidecar) finds each picture
  pixel for pixel in the frame and counts them (101 checks), controls `iconsize` and `noicon`. Board part of S10 (2026-10-22, host app ff984647, Alex's photo): `u64_mk2_artix` shows the C64U logo, `top` the TRX64 logo, as D7 says;
  the icons look poor on the board (Alex: "something is fundamentally off", translated): grey bars beside the square icon, uneven pixel widths
  from a non-integer scale, the text in the 32x32 logos unreadable. Being looked at with ue2emu (T101). Not verified: the board's own
  `gideon-3.15a.cfw` and the `trx64_update_692f643d_20261010.cfw` on the stick (FTP of the board refused the connection); the disk copies of the same
  name and the other 3.15a files carry the same design names.
- **Not built, as decided:** nothing of §6 and nothing for the 100T.

Bench lines (2026-10-21, Mac, clang and GCC 16; `tests/hostapp/run.sh` in `ci/lint.sh`, the render bench in `ci/trxapps.sh`):

```
test_cfw: 41026 checks, 0 failed           S1 (both layouts, refusals) and S2 (map, inverse, magic words, IPROG bytes)
test_fatwalk: 35 checks, 0 failed          S3 (PoC volume, fragmented, short/long chain, bad clusters, FAT16, bad boot sectors)
control cfw_first_bit: fails as it must (4 check(s) red)        the first .bit taken whatever its IDCODE says
control cfw_map_by_text: fails as it must (2 check(s) red)      map not decided by the AXSS write
control cfw_map_inverted: fails as it must (4 check(s) red)
control cfw_frames_as_packets: fails as it must (1 check(s) red)   frame data parsed as packets
control cfw_load_unchecked: fails as it must (2 check(s) red)
control cfw_walk_to_end: fails as it must (1 check(s) red)      the compressed stream walked to its end (reads counted)
control cfw_map_bits: fails as it must (4108 check(s) red)      the permutation moved by one bit
control fat_gap_allowed: fails as it must (5 check(s) red)
control fat_odd_entry: fails as it must (13 check(s) red)       the FAT12 odd-cluster nibble
control fat_long_chain: fails as it must (1 check(s) red)
render bench: 92 checks, 0 failed (menu: found rows, unusable row, confirm, cancel, start, add, web start; browser panel)
control noask / nolist / anybit: fail as they must (6, 17 and 3 checks red)
```

Not run, and why: S4-S8 are board scenarios (Alex power-cycles the board first; the coordinator runs them). The RISC-V build of the host app as a whole:
the Mac has no riscv32 newlib. What was compiled for it: `cfw.cc`, `fatwalk.cc` and `legacy_tramp.cc` for `rv32im` with `riscv64-elf-g++` (Homebrew; stub libc
headers; the trampoline assembles to 100 bytes). `legacy.cc` was compiled as C++ against the real firmware headers with clang `-fsyntax-only` (no error,
no warning of its own); `hostui.cc` runs natively in the render bench. Not done: the link, the code size, and the board behaviour of the flash write
(time, FatFs allocation, whether the drive hands out one run of clusters).

### The per-start check, as built (T101 S10, 2026-10-25)

The decision of section 7 ("a+b") is in the host app. `startcheck.cc/.h` (pure, benched natively): adding an entry (F3) is the one full check
(`cor_full_check`: every part read and hashed; a `.cfw` is parsed by `cfw_probe`); the entry then carries `size|mtime|header digest` beside
the name and the partial's hash (`core=<path>|<name>|<hash>|<size>|<mtime>|<digest hex>`). A start reads, per `.cor` entry, (a) the directory
entry (size, mtime) and (b) the package header (`cor_probe`: header, manifest with every part's SHA-256, contract, host key, record headers: under
2.2 KB), compares them with the stored values and the running host (contract, host key), and trusts the stored hash while they match; per `.cfw`
entry only (a). A difference marks the row "changed" (startable; the start then checks every part, `core_stage` uses the same rule), a missing
file "missing"; nothing is dropped. Entries of an older boot.cfg (no mtime) get the full check once at the first start. Nothing looks for
bundles or images any more (`scan_found` and `discover_cors` are gone; T101 D10): the long-run handling of `.ue2`/`.cfw` stays open as written
above, only the scan is removed.

### The large .cor (built 2026-10-25)

A large core is a `.cor`: `TRXCOR01`, length, records (`tools/cor_pack.py`, the docstring is the field list). What differs from a layer-2 `.cor`:

| | layer-2 (DFX) `.cor` | large `.cor` |
|---|---|---|
| manifest `kind` | absent | `"large"`; `format` 2 |
| records (type: part) | 1 manifest, 2 partial, 3 app, 4 icon | 1 manifest, 3 app, 4 icon (optional), **5 bitstream** (no partial) |
| bitstream | partial config data, `.bit` header stripped | the full `.bit` file as it is, header included (the flash file the IPROG reads, as in a `.cfw`) |
| checked against | `contract`, `host_key` (AXSS of the running host) | `idcode` (8 hex, the 50T `0362c093` without the version nibble); no contract, no host key |
| manifest keys | name, version, contract, fpga, host_key, partial_*, app_*, icon_* | format, kind, id, name, version, fpga, idcode, map, bit_size, bit_sha256, app_*, icon_*, bit_commit / app_commit (optional) |
| application | load/entry in the manifest | load = entry = 0x30000 (same rule as the `.cfw`), raw `coreapp.bin` (not the `.app`) |
| `map` | - | `identity` or `gideon_bl4` (D3), `auto` in the packer = identity when the bitstream writes the AXSS register |

- **Checks (Alex: only when adding).** `cor_pack.py` refuses at pack time: IDCODE not the 50T, no sync word / IDCODE write, an application that is a `.app`, sizes over
  the host's staging areas (cfw.h: bitstream 8 MiB, application 4 MiB). The host (`cfw_probe` takes a `.cor` too, `cor_probe` returns `COR_ERR_LARGE` with the
  manifest filled) checks at every probe: container, record order, sizes, the manifest IDCODE against the running FPGA **and** against the IDCODE found in the
  bitstream's configuration packets, and `map` against the AXSS write found there. The SHA-256 of the parts is checked when the entry is added and when it is
  started once from the browser (`legacy_stage(..., verify)`); a start from the boot menu trusts it (size and layout are checked, no hash).
- **Start.** The same path as a `.cfw`: `legacy_stage` copies the bitstream record to the staging area, compares / writes the flash file, stages the application
  record under `map`, `legacy_jump` writes the boot magic and sends IPROG. No second path. Logo: the icon record, else the generic firmware icon.
- **Map for the Amiga 1200.** Our host's DDR2 controller runs in it (flat build, `host/pins.xdc` sets `USR_ACCESS TIMESTAMP`, so the bitstream writes AXSS): `identity`,
  decided by the packer from the bitstream and checked again by the host.
- **Old and new readers.** A reader without this section reads record type 5 as an unknown type (`cor.cc`: type > 4 -> `COR_ERR_FORMAT`) and in any case
  finds no partial record and no `host_key`: "not a valid core bundle", nothing loaded. Measured 2026-10-25: the `cor.cc` of main compiled natively and
  given a large `.cor` returns `COR_ERR_FORMAT`, no crash. The reader of a layer-2 `.cor` is unchanged in behaviour (records 1-4, `kind` absent; the
  golden SHA-256 test in `tools/test_cor_pack.py` and `test_cor`); a record 5 in a layer-2 `.cor` is refused.
- **Host app (files).** `cor.cc/h`: `COR_PART_FULL`, `CorInfo.large/idcode/map/id`, `cor_probe` -> `COR_ERR_LARGE`, `cor_large_verify`; `cfw.cc/h`: `cfw_probe`
  reads a `.cor` too (`probe_large_cor`, `CfwInfo.from_cor/app_data/bit_sha/app_sha`); `legacy.cc`: `legacy_stage(..., verify)` hashes while it reads and
  uses `app_data` (a `.cfw` has the 12-byte record in front, a `.cor` does not); `coreboot.cc` unchanged, it refuses `COR_ERR_LARGE` through `cor_result_str`.
  `hostui.cc`: `is_full(path)` (a `.cfw`, or a `.cor` that probes as large) replaces `cfw_is_name` at the seven places that decide "full"; `add_full` runs
  `cor_large_verify` for a `.cor`; a start with no stored hash (browser, found row) passes `verify`; the logo is the icon record.
- **Packing.** `tools/pack_large_cor.sh --id --name --version --bit --app-bin [--icon --bit-commit --app-commit --out]` (python3 only, same inputs as
  `tools/pack_cfw_mc.sh`; the icon defaults to `cores/<id>/logo/icon.rgb565`); the flat build's `cores/<id>/mono/build.sh --pack-cor` forwards to it.
- **Benches.** `tests/hostapp/test_corlarge.cc` (offsets, map, staged bytes, IDCODE both ways, hashes, truncation, record 5 in a layer-2 `.cor`) with the
  controls `cl_*` in `run.sh`; the render bench `scenarios_lc` (found rows, refusal of the 100T and of the damaged package, confirm, start path
  `legacy_stage` with `verify`, add, boot-menu start without it) with the controls `nolarge`, `nohashadd`.

### D9 Our cores always as .cor; .cfw and .ue2 are foreign firmware (Alex, 2026-10-28)

Alex at the board, after seeing the CI's large cores as `.cfw` in the browser: "Von uns bereit gestellte Cores bitte immer (!) mit
.cor. Der Bootloader soll auch .cfw und .ue2 als "fremd core" akzeptieren. Aber .cor mit Logo, metadaten bitte immer für unsere."
("Cores we provide always (!) as .cor. The boot loader shall also accept .cfw and .ue2 as a 'foreign core'. But .cor with logo and
metadata always for ours.")

- **Ours:** every core this repository delivers is a `.cor`; a large core is a large `.cor` (section above: manifest, icon, IDCODE,
  SHA-256), packed by the CI's package stage with `tools/pack_large_cor.sh` from the mono bitstream, `coreapp.bin` and the icon. The
  CI no longer ships a large core as `.cfw` (until now `delivery = "cfw"` gave `build_update_core` -> `<core>.cfw`).
- **Foreign:** the host lists and starts a `.cfw` (Gideon's pre-header) and a `.ue2` (the same image without the 36-byte pre-header:
  it starts with the updater's record {0x30000, length, 0x30000}) the same way; no metadata beyond what the image carries.
- The flash update files of the host (`trx64host_update_*.cfw/.ue2`, T49) are not cores and stay as they are.

### D10 A menu start trusts the entry (Alex, 2026-10-28)

Alex at the board, after a menu start of amiga1200 ran "Check the firmware image", "Bitstream into the flash drive" and "Check the
flash file" and then stayed held: "Ja, wenn wir eine .cor haben mit SHA256 in den meta daten für den bitstream und dann einmal beim
import prüfen, ist es doch ok oder? Und bau das um bitte" ("Yes: with a .cor carrying the bitstream's SHA-256 in its metadata,
checked once at import, that is fine. Rebuild it that way.").

- A boot-menu entry is hashed when it is added (`add_full`, `cor_large_verify`); its start from the menu then takes the direct path of
  T135 D10: a scratch file whose stamp names the firmware is used as it is (no image check, no flash compare). A new or changed file is
  copied and checked after writing, as before. A file started once from the browser is still hashed (`verify`).
- With it (same board run): a firmware started as `.cfw` gives the core application no bundle name; for the shared Amiga ID the core
  application now asks the gateware (`AM_FEAT_FBWIN`: A1200), and a core that cannot be started offers RETURN back to the host.
- **Guard (Alex, 2026-10-28: "ja").** The same board session logged "the flash file differs from the bitstream at +0x00097000" on
  a menu start: the scratch file had changed after it was written (cause not found yet), and a start that trusts the stamp would jump
  into it (no picture, no HDMI). The stamp now carries the CRC-32 of the file as written; a start that trusts the stamp reads the file
  once and checks it, a mismatch copies the bitstream again. A stamp without the CRC (older host apps) is treated as no stamp.


### D11 No confirmation before our large cores; versioned stamp (Alex, 2026-10-28)
Alex (translated): "We don't need this requester before the start of a large core." RETURN on a large `.cor` starts it at
once; foreign firmware (`.cfw`/`.ue2`) keeps the "Start full firmware?" question. Board finding the same day: the C128's
scratch file held one wrong bit (file +0x10D38E, 0x88 for 0x08) from an unchecked SD read, and its stamp CRC was taken
from that same RAM copy, so the trusted start (D10) jumped into a broken bitstream (no HDMI). Since ea00f22d2 the copy is
checked against the manifest's SHA-256 before it is written; the stamp format gets a version (`cor2|`, `cfw2|`) so every
older stamp is invalid once and the next start copies again under the check.

### D12 The old DFX core `cores/trx64` is deleted (Alex, 2026-11-08)

Alex, 2026-11-08 (translated): "the old DFX trx64 core we don't do - take it out and delete it!!! So that it does not disturb us!!!"
It was a source tree that nothing built (T115 above), but its benches still ran and gated the host: `statebus` (host/sim/statebus) built
`cores/trx64/c64_core.v`, `trx_sys.v`, `trx_dbg.v`, `c64_mem.v` and the CIA/SID/VIC models, was filed as a host bench and has been hanging since 2026-11-04,
which blocked the host's Vivado. `tools/check_no_old_core.py` (lint stage, with its own selftest: the directory back, a path in a Tcl file, a path in a
bench script each turn it red) fails while `cores/trx64/` exists or a tracked file outside `docs/` names the path; board paths (`/SD/cores/trx64.cor`) and
`cores/trx64u` / `cores/trx64g` are not hits.

File map (old path -> new path; `git mv`, history kept; the gateware content is identical, only paths and comments changed):

| old | new |
|---|---|
| `cores/trx64/trx_io.v`, `trx_sys.v`, `dma_win.v`, `trx_engine.v`, `drv_dbg.v` (the five the host and the large cores read in place) | `cores/common/` (already in `shared:`; the five lines and the `ignore:` excludes left `host/layers.txt`) |
| `cores/trx64/logo/` (icon of the TRX64 `.cfw` identity, read by the release pins and `tools/gen_cfw_icons.py`) | `tools/core_logos/trx64/` (an extra identity of `tools/core_logos/build.py`, like `u64` and `firmware`) |
| `c64_core.v`, `c64_mem.v`, `cart_slot.v`, `slot_port.v`, `trx_dbg.v`, `layer2_trx64.v`, `core_sources.tcl`, `c64_core_ooc.xdc` | deleted (`c64u`, `c64ref`, `c64scpu` and `c128` carry their own `_c64u` / `_c64ref` copies since T103 Q5; none read these) |
| `trx_sid/` (engine RTL, tables, model) | deleted (`cores/c64u/trx_sid` is its own copy; `tools/fetch_libresidfp.sh` went with its model bench) |

Paths changed in: `host/build.tcl`, the twelve `cores/*/mono/build_mono.tcl` (`$trx64/` -> `$common/`), `host/layers.txt`, `ci/sims.deps`, `ci/sims.lane`,
`ci/sims.txt`, `ci/lanes.py`, `ci/test_plan.sh`, `ci/test_layers.sh`, `ci/test_layers_flow.sh` (the fixture's reference core is now a copy of c64ref named `refx`),
`contract/host_pin.txt` (re-pinned; the code diff of the pinned files is paths plus one log string, the rest comments), the bench scripts below, comments naming the path.
**The host key and every core key change once** (the five files moved, the pinned comments changed): one host Vivado run follows the merge, and the large cores rebuild once.

Benches (every one of `ci/sims.txt` and `host/sim/*` that touched an old-core file):

| bench | result | reason |
|---|---|---|
| `trx_io`, `ring_f4`, `ring_trace`, `pixlayer`, `pixscale`, `apply` (not in `sims.txt`) | kept, repointed to `cores/common/` | host blocks (`trx_io`, `trx_sys`, `trx_engine`); stand without the old core |
| `cart_unit` | kept, repointed | `cart_slot` is now `cores/c64ref/cart_slot_c64ref.v` (same ports; the diff to the old file was renames only); 198 checks pass |
| `input` | kept, half | the GHDL half (`ultimate_sys` with the BFM: matrix, joysticks, NMI, reset sense) stands; the Verilator half (old `c64_core` + KERNAL typing the matrix) went |
| `redip_new` | kept | needs `vendor/icepi` only; the dependency on `trx_sid/mem` was never read |
| `uci` (not in `sims.txt`) | kept, GHDL half | `run.sh` + `tb_uci.vhd` test `host/uci_unit.vhd` alone; the old-core half went |
| `statebus`, `ring_core`, `ultsys`, `dbgsess`, `steal`, `slot` | deleted | built the old core with `trx_dbg` (state bus, snapshots/scrub, debugger stop through `ultimate_sys`, DMA steal, cartridge slot); no live core has `trx_dbg` |
| `cart` (build, `tb_cart*`), `dbg`, `dma_io`, `drive`, `pause`, `reu`, `turbo`, `sampler`, `sid`, `lorenz`, `cart_equiv` (not in `sims.txt`) | deleted | old core only (`cart/carts.py` + `asm/` stay: `tests/cart` emits its CRTs from them) |
| `redip` equivalence and trx scripts (`run_equiv2/8`, `run_bypass`, `run_trx`, `run_all`, `build_ref/api/multi/trx/core`, `tb_ref/multi/trx/audio_top`, `mklog.py`) | deleted | compared the T25.1 engine of the old core with reDIP |

Coverage that is gone with the old core and has no replacement bench: the debugger stop as a freeze (T12 section 14.8), the state bus and ring snapshots
over a whole C64, DMA steal and the cartridge slot against a full C64. `drv_dbg.v` stays in the host build; its only bench was `dbg`.
Pre-existing, not touched: `python3 tools/core_logos/build.py --check` fails on `main` too (`diag` has no logo definition).

### Reference core c64ref (built 2026-10-25)

`host/layers.txt` `reference:` is `c64ref` (it was `trx64`); `host/build.tcl` reads that line and sources `cores/<reference>/core_sources.tcl`
(it sourced the trx64 recipe unconditionally). `cores/c64ref` is a copy of `cores/c64u` without `sim/` and `logo/`, self-contained, contract 2 (247
ports, `contract_ports.py --check`), module `layer2` (the name `host/top.v` instantiates), files and modules with the suffix `_c64ref`.
`core.toml`: id `c64ref`, name "C64 Reference", app `none`, order 0, `experimental = false`, `features = ["TRX_FEATURES=jtag"]`, and the new key
**`build_only = true`**: the core is built so that the host can be routed against it and is never shipped. `tools/core_toml.py list-cor` leaves it out
(so `ci/package.sh` and `tools/pack_cores_mc.sh` pack no `.cor` for it), `tools/test_make_release_mc.py` leaves it out of the notes check, and
`tools/core_logos/build.py` wants no logo for it. `flash_filler` (null core) is unchanged; the null core's image checks stay its own.

- **Removed, with the outputs at the null core's idle value (0):** the sampler (`trx_sampler`, its page 8, its DDR2 read: `smp_f_addr`, `smp_f_tgl`),
  the datasette (Gideon's `c2n_playback_io`, `c2n_record`, `tape_speed_control`, `spike_filter`, the `c2n_l2` wrapper, pages C/D, the `c2n_*` pins),
  the UDP streams (`pktport_core`, `udp_stream`, `udp_audio`, `stream_mux`, page 9, the packet port `pp_*`). Their outputs are
  tied by `c64ref_tie_ff` (one DONT_TOUCH flop per bit, the existing boundary rule of the c64u layer 2); their inputs are unused. The tape inputs of
  the C64 sit at idle high, `CORE_CAPS` loses the streamer, recorder, RAM expansion and extended REU bits, pages 8, 9, C, D ack and read 0.
- **Built off, by the defines that exist:** `NO_REU` and `NO_TURBO` in the C64 core's synthesis (the engine stays held at normal speed, as in
  trx64's `TRX_FEATURES` without turbo); the REU outputs reach the boundary through the keep LUTs of the c64u layer 2.
- **Kept:** VIC, CPU, CIAs, the 8-SID reDIP (`SID_MULTI`), the SID sockets, drive A (`mm_drive`), cart slot and expansion port, UCI, paddles.
  The SID stays the 8-SID reDIP: T25.4 measured the two-SID engine at 1,557 logic LUT against 1,501 for eight, so the swap would save
  nothing, and Alex kept SID8 (§7).
- **The host's `top_synth`** is computed in `cores/c64ref/core_sources.tcl` from `TRX_FEATURES` exactly as in `cores/trx64/core_sources.tcl`, so
  with `TRX_FEATURES=jtag` the host is built with `NO_UCI` and `NO_STREAMS` and the JTAG debug master in, as before. The five TRX modules the host's top reads
  from `cores/trx64` (`trx_io.v`, `trx_sys.v`, `dma_win.v`, `trx_engine.v`, `drv_dbg.v`) moved from the reference core's key to `shared:` of `layers.txt`
  (and out of `ignore:`), so a change to them still moves the host key and the pin.
- **Not measured:** the LUT count and timing of the host routed against c64ref - no Vivado run here; CI builds it. The pin
  `contract/host_pin.txt` was written with this change.

### Way back from the held page, and the shared-ID pick by id (T115 §7, asked 2026-10-26, BUILT and benched 2026-10-26; board B1 open)

Alex, board session of 2026-10-26: "It would be good if the app allowed us a 'Return to Host Menu' here." ("here" = the page "This core cannot
be started ... The core stays held.", `coreapp/src/coreapp.cc`). The same session found that a large `.cor` whose manifest name was "Amiga 1200
(AGA)" stayed held against the descriptor's "Amiga 1200 (AGA, 68020)": `start_identify` picks among descriptors that share an ID word (A500 and
A1200: AMIG) by comparing the hand-over's `core_name` with the descriptor's display name.

**What is known (read from the code, not from comments).** `board_detect` (`board.cc`) decides `can_return_host` from the ICAP ID byte at
0x10060600 (0x7A), not from "a host was there". The flat build of a large core (`cores/amiga1200/mono/build_mono.tcl`) instantiates the host's own
`host/top.v` unchanged, whose ICAP loader (`ICAPE2`, `top.v:628`) it therefore keeps: such a core reads 0x7A, is the MULTICORE profile
(with a valid hand-over, T112 Q1), and `hw_return_to_host` (IPROG, WBSTAR 0) is its way back; the SYSTEM menu already gets "Return to host menu"
(`menutree.cc` `return_host`). Gideon's bitstream and the TRX64 Ultimate monolith have no ICAP behind 0x10060600 (reads 0): MONOLITH profile,
no way back but a power cycle (D6). Offering a button there would be a lie, and giving them one needs gateware (host/ and the monolith are not
touched from here). Not measured on a board: that the A1200 `.cor` reads 0x7A (board step R2 below).

**Decisions (taken by the request, recorded here).**
- **H1. The held page offers the way back exactly where the SYSTEM entry does:** `board.can_return_host`. The text under the reason says
  "Press RETURN to return to the host menu."; RETURN calls the same `hw_return_to_host` as `EK_RETURN_HOST`. If it comes back (no loader, refused)
  the page stays and shows the reason. Without `can_return_host` the page is as before, with no hint and no key.
- **H2. The pick by id.** The hand-over carries the core's id (`core.toml` `id`, written by `tools/cor_pack.py pack-large` into the manifest as
  `"id"`; the host reads it as `CorInfo.id`). A descriptor names its id in the new field `CoreDesc.core_id` (set for `amiga` and `amiga1200`).
  `start_identify` compares per candidate: both sides have an id -> the ids decide (case-insensitive), the name is not looked at; otherwise the
  name decides as before. A hand-over id that fits no candidate is a mismatch (a `.cor` that says `nes` on the Amiga ID word is refused even if
  its name were right). No name and no id: ambiguous, as before.
- **H3. Hand-over layout stays version 1 and compatible both ways.** `core_id[24]` and `id_sum` are appended after `host_build`; `handover_sum`
  and `handover_valid` cover the version-1 bytes only, so an application built before this change still accepts the block, and a block written
  by an older host (which fills the old size and leaves what lies behind) is read by the new application without an id. `id_sum` is the id's
  hash mixed with the block's `sum`, so bytes left behind by an earlier start are not taken for this block's id: an id that fails its check reads as
  "" (the name decides), never as a wrong id. `handover_core_id()` is the only reader.
- **Out (decided with the request, to be told to Alex):** the layer-2 `.cor` manifest has no `id` (adding it changes the golden SHA-256 of every
  bundle); layer-2 shared-ID cores keep matching by name. A way back for the TRX64 Ultimate monolith and Gideon's bitstream (gateware).

**As built.** `start.cc`: `bundle_names` (id first, name fallback), `held_offers_return`, `held_text`, `held_key` (pure, benched); `run.cc`
`coreapp_held` (board code: the page, USB and matrix keys, `hw_return_to_host`); `coreapp.cc` calls it instead of the endless loop; the message
panel is 220 px high and its text buffer 400 bytes for the hint. `desc.h`: `CoreDesc.core_id` (last field, so positional initialisers stay valid),
set in `amiga.cc`. Host: `CfwInfo.id` from the manifest, `cfw_handover_page` and `core_jump` pass it to `handover_fill(..., core_id)`.
`handover.h`: `id_sum` + `core_id[24]` after `host_build`, `sizeof(Handover)` 360 -> 388, version stays 1.

**Scenarios** (bench = `tests/coreapp` and `tests/hostapp`; each has a control that breaks the product source and must turn the test red).

| # | Scenario | Proof | Control |
|---|---|---|---|
| R1 | MULTICORE profile (ICAP 0x7A), layer-2 core, descriptor missing: the held page offers the return, RETURN calls the loader once | `test_board` held page | offer always off; key ignored |
| R2 | MULTICORE profile with a valid hand-over (a large `.cor` of the flat build): same offer, and the SYSTEM menu has the entry | `test_board` | `can_return_host = false` |
| R3 | MONOLITH profile without hand-over (TRX64 Ultimate): no offer, RETURN does nothing, the loader is not called | `test_board` | offer always on |
| R4 | MONOLITH profile with a valid hand-over (large TRX64 started by the host, no ICAP): no offer | `test_board` | offer decided by `has_handover` |
| R5 | the return comes back false: the page stays and shows the reason; another key does nothing | `test_board` | result ignored |
| I1 | AMIG, manifest name "Amiga 1200 (AGA)" (not the display name), id `amiga1200`: the A1200 descriptor | `test_start` | id ignored (name only) |
| I2 | AMIG, name of the A1200 but id `amiga`: the id wins, the A500 descriptor | `test_start` | name wins over id |
| I3 | AMIG, id `nes`: refused (mismatch), also for a name that fits | `test_start` | unknown id falls back to the name |
| I4 | no id (older host, layer-2 `.cor`): the name decides, as before; no name and no id: ambiguous | `test_start` | (existing controls) |
| I5 | a non-shared descriptor with a `core_id`: id decides, an id that disagrees is refused | `test_start` | |
| I6 | hand-over: a version-1 reader accepts the new block; a flipped byte in the id or a stale id from another block reads as "" | `test_handover` | id sum not mixed with the block sum |
| I7 | host: the page filled for a large `.cor` carries the manifest id; a `.cfw` page carries none | `test_cfw` | id not copied |
| B1 | board (Alex's step): the A1200 `.cor` with a manifest name that is not the descriptor's name starts; a descriptor-less core shows the offer and RETURN brings the host menu back; on the TRX64 Ultimate the page shows no offer | board | |

## Open questions for Alex

1. ~~**Q1** Where the user picks it?~~ Answered, §7: beside the `.cor` files, transiently in flash.
2. ~~**Q2** Gideon's updater overwrites our host (R1)?~~ Answered, §7: nothing is done, no warning.
3. ~~**Q3** Which versions?~~ Answered, §7: every `.cfw`.
4. ~~**Q4** P2 in this spec?~~ Answered, §7: yes, the TRX64 Ultimate release `.cfw`.
