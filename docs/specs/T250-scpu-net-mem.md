# T250 — The c64scpu has Ethernet and memory access over REST, so its tests run without a person at the screen

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: NO_NET out, read_mem/write_mem, Vivado on the builder; then T241.1 by REST
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
  Given the c64scpu of this build on the board, started from the menu
  When GET /v1/version is sent to the board's LAN address
  And PUT /v1/runners:run_prg runs scpu1_env from /SD/tests/scpu
  Then GET /v1/machine:readmem gives the program's screen, and its last line reads "n checks, m failed"

Scenario: S4 (P2) a write over REST
  When PUT /v1/machine:writemem writes 16 bytes to $01F000 and to $1000C000
  Then GET /v1/machine:readmem gives the same 16 bytes back from both addresses
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
- **D3 (P2):** a write into the 65816's RAM goes straight to the DDR2. The CPU's 8 KiB cache (`scpu_cache.v`) does not
  see it, so `machine.write_mem` flushes the cache after the write, with the flush the ROM load uses
  (`flush_cache`). Reads need nothing: the cache writes through (`scpu_cache.v`, every write is a byte job to the DDR2).
- **D4 (D24):** the two operations are code in the per-core file `coreapp/cores/c64scpu.cc`; the ledger gets an
  exception line with Alex's quotes of the origin as the decision.
- **D5 (P3):** Alex, 2026-11-21 (translated): "Just build it locally, directly with Vivado, first." The build runs on the
  builder, in `vivado-env:2024.1`, with `cores/c64scpu/mono/build.sh`, under `ci/lib.sh` `heavy_lock`, outside the
  pipeline. The pipeline builds the branch again when it is pushed.

## §4 Out

- No trace ring and no snapshot of the CPU's registers: Alex, "it does not have to be as lavish as the ring and the
  monitor access on trx64U".
- No change to WiFi.
- The ROM banks in the address map: D2.

## §5 Built

Nothing yet.
