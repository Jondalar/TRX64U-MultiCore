# T250 — The c64scpu has Ethernet and memory access over REST, so its tests run without a person at the screen

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: S1-S4 PASS; open: the merge (integ-0.2.1, without T248)
**Repo:** TRX64U-MultiCore — the c64scpu build (`cores/c64scpu/mono/core.toml`) and its application part
(`coreapp/cores/c64scpu.cc`).
**Number:** T250.
**Origin:** Alex, 2026-11-21 (translated): "And if you want DMA read/write and so on - the tape space would be free
for that. It does not have to be as lavish as the ring and the monitor access on trx64U. Network is missing, in the
end." Then: "What was the problem with putting the network in? The benches? Just build it locally, directly with
Vivado, first. We have everything here, and then you can do the tests yourself by REST and read them, can't you?"
**Depends on:** T249 (the branch base: T241 S2, T248 and T249), T241.1 (the suite that runs over REST).
**Milestone:** M2.

---

## §1 The problem

- **Who:** Alex at the board, and the measuring work of T241 and T241.1.
- **What happens today:**
  - The c64scpu build defines `NO_NET` (`cores/c64scpu/mono/core.toml`, `mono_env`): no Ethernet interface, no RMII
    transceiver, no packet-port arbiter (`mono/top.v` under `NO_NET`), and the capability word without bit 24, so the
    application starts no Ethernet driver. With the c64scpu loaded, the board is not on the LAN. Only the ESP32's WiFi
    is left.
  - Where `NO_NET` comes from: T164 (2026-11-01) made it for the console flat builds. The Ethernet interface is
    1,231 LUT (T140, the C128 report `i_eth`). The C128 core took it over, and the c64scpu was forked from the C128
    core. Nobody decided it for the c64scpu itself.
  - The application registers no `machine.read_mem` / `machine.write_mem` for the c64scpu (ten other cores do, with
    `MEM_OPS`). So `GET /v1/machine:readmem` has no operation on this core.
  - A person reads every result of the T241.1 programs from the screen and sends a photo.
- **The parts:**
  - **P1** Ethernet in the c64scpu build.
  - **P2** `machine.read_mem` and `machine.write_mem` on the c64scpu: the 65816's RAM and the C64 RAM.
  - **P3** A build path for this work that does not wait for the pipeline: Vivado directly on the builder.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1, P3) the build
  Given cores/c64scpu/mono/core.toml without NO_NET
  When cores/c64scpu/mono/build.sh runs on the builder in vivado-env:2024.1 under the heavy lock
  Then utilization_hier.rpt lists the Ethernet interface, and the setup slack is not negative

Scenario: S2 (P2) the operations
  Given the c64scpu application
  When machine.read_mem and machine.write_mem are called with an address of each region of D2
  Then each reaches the window offset of D2, and a write into the 65816's RAM is followed by the cache flush

Scenario: S3 (P1, P2) the board
  Given the c64scpu of this build on the board, started from the menu, and the token of POST /v1/ui/grant
  When GET /v1/info is sent to the board's LAN address
  And POST /v1/ops/c64scpu.run_prg runs scpu1_env from /SD/tests/scpu
  Then POST /v1/ops/machine.read_mem gives the program's screen, and its last line reads "n checks, m failed"

Scenario: S4 (P2) a write over REST
  When POST /v1/ops/machine.write_mem writes 16 bytes to $020000 (SuperRAM) and to $100C000 (the C64 RAM at $C000)
  Then POST /v1/ops/machine.read_mem gives the same 16 bytes back from both addresses
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1, P3 | the Vivado reports of the build | builder |
| S2 | P2 | a unit test of the c64scpu application | builder (app tests) |
| S3 | P1, P2 | REST calls from the Mac | board (Alex starts the core once) |
| S4 | P2 | REST calls from the Mac | board |

## §3 Decision

- **D1 (P1):** `NO_NET` leaves `mono_env` (`TRX_DEFS=SOCK_AUDIO`). The Ethernet sources are in `host/gideon_vhdl.tcl`
  already; `mono/top.v` then passes `g_net(1)` and the capability word with bit 24 (`CAPAB_ETH_RMII`).
- **D2 (P2): the address map of the two operations.** The 65816's addresses as `scpu_mem.v` maps them, plus the C64
  RAM above them:
  - `$000000-$01FFFF` SRAM banks 0 and 1: window offset `0x02000000 + address` (`scpu_mem.v` `sram()`, DDR2
    `0x03000000`).
  - `$020000-$F5FFFF` SuperRAM: window offset `0x01000000 + address` (`scpu_mem.v`, banks `$02-$F5`).
  - `$F60000-$F7FFFF` the SIMM window, SuperRAM bytes `0-$1FFFF`: window offset `0x01000000 + (address - $F60000)`.
  - `$1000000-$100FFFF` the C64 RAM, through the RAM window with the RAM under the I/O (as the C64-family cores).
  - The ROM banks `$F8-$FF` are not mapped: they hold the person's ROM image, and a write would change the running
    firmware. Proposed; Alex confirms or adds them.
  - Bank 0 is the SRAM store, where the CPU writes. In the reset configuration the CPU reads `$A000-$BFFF` and
    `$E000-$FFFF` from bank 1 (the BASIC and KERNAL copies) and `$D000-$DFFF` from the I/O, so a read of bank 0 there
    gives the RAM below. A write to `$01A000-$01BFFF` or `$01E000-$01FFFF` changes the running BASIC or KERNAL.
  - A write over REST to bank 0 does not reach the C64 RAM: the CPU's own writes are mirrored there, these are not. A
    byte that the CPU and the VIC both see is written at both addresses (`$0400` and `$1000400`).
  - The operations take the address as a number (JSON, or decimal in a query string): `$0400` is 1024, `$100C000` is
    16826368.
- **D3 (P2):** a write into the 65816's RAM goes straight to the DDR2. The CPU's 8 KiB cache (`scpu_cache.v`) does not
  see it, so `machine.write_mem` flushes the cache after the write, with the flush the ROM load uses
  (`flush_cache`). Reads need nothing: the cache writes through (`scpu_cache.v`, every write is a byte job to the DDR2).
- **D4 (D24):** the two operations are code in the per-core file `coreapp/cores/c64scpu.cc`; the ledger gets an
  exception line with Alex's quotes of the origin as the decision.
- **D5 (P3):** Alex, 2026-11-21 (translated): "Just build it locally, directly with Vivado, first." The build runs on the
  builder, in `vivado-env:2024.1`, with `cores/c64scpu/mono/build.sh`, outside the pipeline, as the pipeline's build job
  runs it: `docker run --init`, and `ci/lib.sh` `heavy_lock required` inside the container on the pipeline's own lock
  files, mounted as `/cache/heavy.lock` and `/cache/multicore/heavy.slot1`. The pipeline builds the branch again when
  it is pushed.

## §4 Out

- No trace ring and no snapshot of the CPU's registers: Alex, "it does not have to be as lavish as the ring and the
  monitor access on trx64U".
- No change to WiFi.
- The ROM banks in the address map: D2.

## §5 Built

- **2026-11-21, branch `t250-scpu-net`** (6c86f802, on T249's ea2586e5): `mono_env` without `NO_NET`;
  `machine.read_mem` and `machine.write_mem` in `coreapp/cores/c64scpu.cc` with the regions of D2, the flush of D3
  after a write below `$1000000`; the ledger lines of D4.
- **S2 PASS:** `test_c64scpu: 367 checks, 0 failed`, one read per region of D2, a write with its flush, a write to the
  C64 RAM without one, a read of `$F80000` refused. The unit ran alone: `CA_GROUP=c64scpu tests/coreapp/run.sh` does not
  run it, because c64scpu is no group of `cores.txt`.
- **S1 PASS:** builder, 7.5 min. `utilization_hier.rpt`: `g_eth.i_eth` (ethernet_interface) 413 LUT, `rmii` 140, `pkh`
  (pktport_host) 20; top 23,649 LUT (72.5 %; T249's run 44880276432: 22,952, 70.4 %). WNS +0.174 ns.
- **The first local run hung:** 45 min after `synth_design` with no CPU time. The container had no init process: the
  synthesis helpers stayed zombies and `synth_design` waited for them. The pipeline's build job runs its container with
  `--init`. That run also had no lock: `heavy_lock` found no writable `/cache` on the host and ran without one. D5 now
  says how the second run was made.
- **Board:** `c64scpu_t250.cor` (3,544,191 bytes, sha256 `98d7a670...`) in `/SD/cores/`, with the ROMs and the settings
  of `c64scpu_t248`. Alex started it from the menu on 2026-11-21.
- **S3 and S4 changed (2026-11-21), before they ran:** the first text used the REST of Gideon's application. The core
  application has another one (`coreapp/src/rest.cc`): no `/v1/version` (it has `/v1/info`), a token on every route
  except `/` and `/v1/ui/hello`, and the alias `runners:run_prg` needs `prg.run` or `c64.run_prg`, while this core
  registers `c64scpu.run_prg`. S4's `$01F000` is in the running KERNAL copy (D2), and `$1000C000` was a typo outside
  the map.
- **S3 PASS (2026-11-21, board, over the LAN):** `GET /` answered "TRX64 core application". `POST /v1/ui/grant` gave
  the token (Alex: "ja bitte"). `GET /v1/info` gave the core "C64 with SuperCPU" with 111 operations.
  `POST /v1/ops/c64scpu.run_prg` ran all eight T241.1 programs one after the other, and `machine.read_mem` read each
  screen with its last line `n checks, m failed` (`tests/c64scpu/rest.py`; the screens are in T241.1 §5).
- **S4 PASS (2026-11-21):** the bytes `A0`-`AF` written to `$020000` (SuperRAM) and to `$100C000` (C64 RAM `$C000`)
  read back the same at both addresses. The old bytes were written back and read back the same.
- **Seen on the way, not changed here (the core application's REST, not this core's):**
  - `PUT /v1/runners:run_prg` answers code -6: "/v1/runners:run_prg needs the operation prg.run|c64.run_prg, which
    this core did not register".
  - `GET /v1/machine:readmem?address=1024&length=16` works with the token. `address=0400` reads `$0190`: the core
    application reads the address as decimal, Gideon's application as hex, so the same URL reads another address.
- **The picture (Alex, 2026-11-21, translated):** "the picture is black every now and then" - "but less often than with
  248" - "the dropout comes about every 5-6 or 2-3 seconds". Not looked at in this spec.
- **2026-11-21, `integ-0.2.1`:** T250's two commits (`713de049`, `c658cb1a`) are picked onto `t241-scpu-fix` without
  T248 (T241 §5); the branch `t250-scpu-net` that stacks on T248 is not merged. The c64scpu of the bundle was built twice
  on the builder: `t241fix` (`b7571876`, WNS +0.028 ns) and T253 S1 (`c64e74e3` of the local branch `t253-scpu-test`, WNS +0.008 ns), not from S1's tree here.
