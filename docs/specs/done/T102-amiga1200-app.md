# T102 — The Amiga 1200 core has an application, and it stays AGA with 2 MB chip RAM

**Status:** BUILT, bench-proven, not on the board (2026-10-15). Open: S4 (board), upload of `amiga1200.app` (coordinator). 2026-11-13 status check: MERGED INTO T97 (row dropped 2026-10-18); its board steps are T97's (done, Alex 11-08).
**Repo:** TRX64U-MultiCore — `cores/amiga1200/` (contract file), `cores/amiga/sim/` (benches), `cores/amiga/rtl/trx/` (the
feature register and the size generic). The application source is in the 1541ultimate line, worktree
`TREX/1541ultimate branch `t85-amigaapp` (the worktree `1541ultimate-wt-amigaapp` was removed 2026-10-22)`, branch `t85-amigaapp` (T93: still built outside this repository).
**Number:** T102 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-14 evening: "send an agent off in parallel for Amiga 1200 + app" (translated).
**Depends on:** T85 §6 (the A1200 core), T93 (where applications come from), T100 (the `package` stage packs the `.cor`).
**Milestone:** M3.

---

## §1 The problem

The A1200 core routes (run 35: `amiga1200_partial.bit`, 1,405,722 bytes) but has no `.cor`, because a `.cor` is the partial plus a
core application and no `amiga1200.app` exists. The Amiga application cannot stand in for it, measured on 2026-10-15:

- Its chipset setting offers OCS and ECS only (`amiga.cc`: `chipset_names[] = { "OCS", "ECS" }`; the byte string `AGA\0` occurs
  0 times in `dist/coreapps/amiga.app`, `ECS\0` once). It writes `CHIPSET_CFG` = 0x00 or 0x08.
- Its defaults are chip 512K + slow 512K (`MEM_CFG` = 0x04).
- `CHS_INIT=16` / `MEM_INIT=3` of the A1200 core are only power-up values: `amiga_core.v` copies `r_chipset` / `r_mem` over `chs_b` /
  `mem_b` at every configuration commit (`cfg_tgl`). The first commit of the existing application therefore turns the A1200 into an
  OCS machine with 512 KB.

**Parts (MECE):**

- **P1 The application.** An `amiga1200.app` built from source, whose settings have an AGA value and whose defaults are the A1200's.
- **P2 The machine stays what the core is.** After the application's commit the chipset is AGA and chip RAM is 2 MB.
- **P3 The bundle.** The application and the run-35 partial pack to a `.cor` that `cor_pack.py check` accepts for the host.
- **P4 On the machine.** The core started from the boot menu with this application runs Kickstart 3.1 in AGA with 2 MB chip RAM.
- **P5 The hard disk size field travels with it.** The application half of T97 D3 (images of 4 GiB and more); the decision itself
  is in T97 §5.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: the application's defaults are AGA and 2 MB (P1)
  Given the application built with AMIGA1200=1
  When  the register bytes of its defaults are computed (the code the image is built from, compiled for the host)
  Then  CHIPSET_CFG = 0x10 (bit 4) and MEM_CFG = 0x03 (chip 2 MB, slow none)
  And   the Amiga build of the same source still gives 0x00 and 0x04 (nothing moved for the A500 core)

Scenario: the core stays AGA after the commit (P2)
  Given the core built as cores/amiga1200 builds it (CHS_INIT=16 MEM_INIT=3 HDF_SECT=1) behind the real mig_arb and a DDR2 model
  When  the bench writes the bytes of S1 into the register window, in the order of the application's apply()
  Then  Denise's and Agnus's aga input is 1, chipset_config[4] is 1, the bank mapper's chip config is 3
  And   the same bench fed with what the existing amiga.app commits (0x00 / 0x04) FAILS on all three
  And   the same bench fed with the A1200 bytes but CHIPSET_CFG = 0x00 FAILS on AGA and still passes the 2 MB check

Scenario: the bundle packs (P3)
  Given amiga1200.app and the amiga1200 partial of package main-9bf12877
  When  tools/pack_cores_mc.sh packs it
  Then  tools/cor_pack.py check --host-key 0e8fb06e accepts it and prints the application's SHA-256

Scenario: the machine on the board (P4)
  Given the .cor on the stick, Kickstart 3.1 in /usb1/roms/amiga
  When  Alex starts "Amiga 1200" from the boot menu
  Then  the console line reads "chip 2M slow None fast None AGA ... CHIPSET 10", and Kickstart 3.1 shows its AGA screen
  And   a system information tool reports AGA and 2048 KB chip RAM

Scenario: an 8.4 GiB image is announced (P5)
  Given T97 S3
  Then  see T97 §5.1: the application writes sectors only if FEATURES bit 0 is set, otherwise it refuses an image of 4 GiB and more
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | defaults are AGA and 2 MB | `software/io/amiga/tests/run_cfgdump.sh` (1541ultimate worktree), contract `cores/amiga1200/app_regs.txt` | by hand; the contract file in CI |
| S2 | P2 | stays AGA after the commit, two negative controls | `cores/amiga/sim/run_cfg.sh` | `ci/sims.txt` tier 2 `amiga_cfg` |
| S3 | P3 | the bundle packs | `tools/pack_cores_mc.sh --pkg main-9bf12877 --core amiga1200` + `cor_pack.py check` | by hand, then the `package` stage |
| S4 | P4 | the machine on the board | board session with Alex, photo | by hand |
| S5 | P5 | size field | `cores/amiga/sim/run_sdc.sh` (T97 S3) | `ci/sims.txt` tier 1 `amiga_sdc` |

## §3 Decision

- **D1 One source, two builds.** The A1200 application is the Amiga application compiled with `make AMIGA=1 AMIGA1200=1`
  (`-DAMIGA1200_APP=1`); no second code base. What differs is in one header, `software/io/amiga/amiga_cfg.h`: the third chipset value,
  the defaults, the fast RAM choices. The same header is compiled for the host (`tests/cfgdump.cc`), so the bytes the bench is fed are the
  bytes of the image, not a copy of them. *Lost:* a separate fork of `amiga.cc` (would drift on the next fix).
- **D2 Defaults of the A1200 build.** AGA, chip 2 MB, slow none, fast none, two floppy drives. The A1200 has no slow RAM; fast RAM stays a
  choice. Chipset value 2 writes 0x10: `minimig.v` takes `.aga(chipset_config[4])` and `.ecs(|chipset_config[4:3])`, so bit 4 alone is AGA
  and ECS at once, which is what `CHS_INIT=16` already relies on.
- **D3 Its own settings store.** `A12G` / "Amiga 1200 Settings", so the saved settings of the Amiga application (OCS defaults) cannot
  become the defaults of this machine.
- **D4 The bytes are a contract file.** `cores/amiga1200/app_regs.txt` holds what each build writes. `run_cfg.sh` regenerates it from the
  application when the worktree is there and fails if it differs, and uses the file when it is not (CI). Changing the application's
  defaults therefore shows up in this repository's diff.
- **D5 The size field is announced by the gateware.** T97 D3 (decided in T97 §5.1); the application reads FEATURES bit 0 and refuses what it cannot announce.
- **D6 Where the file lies.** The built image is `ultimate.bin` of the build (the same file that rides in the other `.cor`); it is named
  `amiga1200.app`. The coordinator uploads it to `trx64-coreapps/1`; this task does not.

## §4 Out

- **The 68020** (T97 P1). `CPU_CFG` stays 0; the core is still an AGA chipset on a 68000. The application writes 3 when the gateware has the
  020 (one constant in `amiga_regs()`); the bench grows its check then.
- **AGA colour depth.** `nanomig.v` hands out `red[7:4]`, i.e. 4 bits per channel (`r_in = red[7:4]`); the AGA palette is 24 bit. The picture
  of this core is AGA's bitplane depth and palette banks at 12-bit colour. Not widened here; whether it matters is a question for the board
  photo (S4) and a separate spec if it does.
- **The Amiga (OCS) application gets no AGA value.** Its core is the A500; nothing about it moves.
- **Fast RAM 5.5M is read, not benched.** `fast = 3` autoconfigures three Zorro II cards (4 MB + 1 MB + 0.5 MB, `cpu_wrapper.v`, no
  `ENABLE_RAM32`) and takes the slow RAM; no bench writes it. The default is None.
- **Board.** S4 is by hand; nothing was run on the board, nothing flashed.
- **No Kickstart, no HDF** in the repository or the bundle.

## §5 Decisions taken while building

### 5.1 What was built (2026-10-15)

- Application, worktree `1541ultimate-wt-amigaapp` (removed 2026-10-22; branch `t85-amigaapp` on Gitea), branch `t85-amigaapp`: `amiga_cfg.h` (new), `amiga.cc`, `tests/cfgdump.cc`,
  `tests/run_cfgdump.sh`, `build/build_amiga1200.sh`, `Makefile` (`AMIGA1200`), `doc/AMIGA.md`; 64-bit `File::seek64` / `get_size64` in
  `filesystem/` (FatFs `FSIZE_t`; `FileInfo.size` is 32 bit and silently truncates an HDF of 4 GiB and more).
- Gateware (`cores/amiga`): FEATURES register `0x00C` (`amiga_regs.v`), `HDF_SECT` generic through `layer2_amiga` / `amiga_core` /
  `amiga_regs` / `amiga_sdc`; `cores/amiga1200/core_sources.tcl` passes `-generic HDF_SECT=1`.
- Benches: `run_cfg.sh` (new, `tb_l2.sv` mode `CFGCHK`), `run_sdc.sh` / `tb_sdc.sv` with sectors and a negative control.
- Image: `target/u64ii/riscv/ultimate/result_amiga1200/ultimate.bin`, 1,377,676 bytes, SHA-256
  `5fb4348c1940ec2b725102736988f2affaa31ca20fae39c571b88dee1c1edf48`, copied to `dist/coreapps/amiga1200.app`. It contains the string
  run `OCS\0ECS\0AGA\0`, "Amiga 1200 Settings" and the HDF refusal text; `strings` finds none of them in `dist/coreapps/amiga.app`.

### 5.2 Measurements

S1, `run_cfgdump.sh`:

```
== A1200 build            MEM=03 FAST=00 CPU=00 CHIPSET=10 FLOPPY=04 IDE=21
== Amiga (OCS/ECS) build  MEM=04 FAST=00 CPU=00 CHIPSET=00 FLOPPY=04 IDE=21
```

S2, `run_cfg.sh` (Verilator, 9 s per run after the build):

```
== 1: the A1200 application's defaults
CFGCHK: after the commit chipset_config 10 memory_config 03, Denise aga 1 Agnus aga 1, bank mapper chip config 3, HDF size 9008283648 bytes
ok  : after commit: Denise and Agnus aga input still set (AGA) / chipset_config bit 4 / chip RAM is 2 MB      PASS tb_l2 cfgchk errors=0
== 2: negative control, the Amiga application's defaults (what amiga.app commits)
CFGCHK: after the commit chipset_config 00 memory_config 04, Denise aga 0 Agnus aga 0, bank mapper chip config 0
FAIL: after commit: Denise and Agnus aga input still set (AGA) / chipset_config bit 4 / chip RAM is 2 MB    FAIL tb_l2 cfgchk errors=3
== 3: negative control, the A1200 defaults with the OCS chipset byte
CFGCHK: after the commit chipset_config 00 memory_config 03, Denise aga 0 Agnus aga 0, bank mapper chip config 3
FAIL: after commit: Denise and Agnus aga input still set (AGA) / chipset_config bit 4                       FAIL tb_l2 cfgchk errors=2
```

The power-up checks of the same bench (`chs_b` / `mem_b` hold the generics, Denise's `aga` follows `CHS_INIT`) pass in all three runs, so the
defect the controls show is the commit, not the start.

S3: `pack_cores_mc.sh --pkg main-9bf12877 --core amiga1200` gives a 2,783,655-byte `amiga1200.cor`, `cor_pack.py check --host-key 0e8fb06e`
prints `host_key 0e8fb06e`, `app_size 1377676`, `app_sha256 5fb4348c...`, `partial_size 1405596`. This partial is run 35's, built without
`HDF_SECT`: against it the application announces sizes in bytes and refuses 4 GiB and more (FEATURES reads 0).

### 5.3 Found on the way

- The register `0x00C` was free (`amiga_regs.v` case `10'h003`); the A500 core reports 0 there, as every older bitstream does.
- `tb_sdc` prints `PASS tb_sdc errors=N` whatever N is; only the runner's `grep errors=0` decides. The negative control relies on that grep
  and on the named `FAIL:` line, not on the word PASS.
