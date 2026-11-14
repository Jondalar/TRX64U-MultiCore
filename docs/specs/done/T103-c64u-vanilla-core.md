# T103 — A second C64 core that is Gideon's set, not ours

**Status:** PROPOSED (2026-10-15); analysis written 2026-10-16; Q1, Q4 answered 2026-10-16, Q3 2026-10-17; D1-D5 open. 2026-11-13 status check: DROPPED (row and todo: large cores start from the menu, a vanilla C64 on the MultiCore host is not needed); D1-D5 not built.
**Repo:** TRX64U-MultiCore — a new `cores/c64u/` (descriptor and `core_sources.tcl`), the blocks it
switches on come from `vendor/icepi` and `vendor/gideon`; the application side leans on the
**`trex/main` branch of `1541ultimate_fw`** (Alex, 2026-10-15). **Whether `host/` stays untouched is Q1/Q2 below.** Not because UCI or the streams belong to layer 1 - their
machine half is the core's and is in the contract (P4) - but because the *switch* that builds them is the host's,
and tape, ACIA and a second drive have no contract port at all (P5).
**Number:** T103 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-10-15, right after the A1200 agent reported (translated): "one more TODO: a
C64U core that does NOT have TRX64 with the ring, monitor and so on, but covers the normal C64U /
UE2 package as far as possible as seen from Gideon's firmware. So: Turbo, UCI, UltiSID (reSID8),
and so on — and NO ring, no TRX64, no trxdebug. Do you see what I mean? A second C64 core that is
closer to Gideon's vanilla in terms of the feature set." And: "there we would lean on the
`trex/main` branch of `1541Ultimate_fw`, please!"
**Depends on:** T94 (the core layout and the descriptors), T87 (the layer caches), T25.5 (reSID and
whose filter curves belong to which engine), T100 (the pipeline this core will be built by).
**Restated by Alex, 2026-10-16 (translated):** "I need a 2nd C64 core that must be able to do everything
Gideon's core can. You can use the gateware blocks from TRX64 for it, they are understood by us.
TRX64 USP functionality such as monitor, ring, debugger is not to be in it. And yes, this is a completely
separate configuration, you can simply reuse some of the gateware again."
**Milestone:** M3.

---

## §0 Scope as Alex set it, 2026-10-16

His words (translated): "Again, I want **2 cores: TRX64 and C64U-vanilla**, with **one host, ideally
WITHOUT having to touch it again**. The scope of the TRX64 core is fine as it is (reSID 2 and so on).
It is **only** about the scope of C64U-vanilla."

That closes Q1 and Q4 and turns the rest of this spec around: the host image on the board
(`0E8FB06E`, built from `9bf12877`) is a **given**, not a variable. Adding a core does not touch it -
`host/core.tcl:20-40` opens the host checkpoint, `lock_design -level routing`, and fills `l2`. So
what vanilla can be is decided by what that host already offers, and **two things in it draw the line,
neither of them area**:

1. **The capability word is a host generic.** `host/top.v:651`:
   `ultimate_sys #(.g_capabilities({2'b00, FPGA_TYPE, 28'h5E40202}), .g_uci(UCI_EN))`. Gideon's
   application gates whole menus on those bits (`apps/ultimate/software/system/itu.h:49-79`), so a
   block in the core that the word does not advertise stays invisible. Set in `0x5E40202`:
   `DRIVE_1541_1` (b1), `CARTRIDGE` (b9), `COMMAND_INTF` (b18), `SAMPLER` (b21), `EEPROM` (b22),
   `USB_HOST2` (b23), `ETH_RMII` (b24), `ULTIMATE64` (b26). **Clear:** `RAM_EXPANSION` (b10),
   `DRIVE_1541_2`, `DRIVE_SOUND`, `HARDWARE_GCR`, `HARDWARE_IEC` (b5), `C2N_STREAMER/RECORDER`,
   `MM_DRIVE`, `EXTENDED_REU`, `STEREO_SID`, `COPPER`, `ACIA`, `BUS_MEASURE`, `UART`.
2. **`NO_UCI` and `NO_STREAMS`** (`bitstream.yml:88`, T110): `uci_unit` is not instantiated
   (`ultimate_sys.vhd:633` `gen_no_uci`), the senders are absent.

Two consequences worth stating plainly, because they are not symmetric:

- **REU is advertised nowhere.** `CAPAB_RAM_EXPANSION` is clear, so REU gateware in vanilla would be
  there and the application would offer nothing for it. REU is a host-word question, not a core question.
- **UCI is advertised and absent.** `COMMAND_INTF` is set while `UCI_EN = 0`, so the application offers
  a command interface today that no gateware answers. That is a defect of the shipped host
  independent of this core (noted in [T110](../../todo/T110.md)).

So with the host untouched, vanilla's set is: the C64, the cartridge/expansion port, the 1541 drive A,
the IEC bus, the sampler, turbo, and the SID engine of its choice. Not REU, not UCI, not the streams,
not a second drive, not tape, not ACIA, not the stereo-SID menus. **Area is not the constraint here**: dropping the ring and the debug path pays for what comes in.

**Amended by Alex, 2026-10-17:** "No, wait. Extending the host once and then building it with trx64 (must-have reference core) is ok" (translated). §0's "host untouched" no longer holds: the host
gets **one** extension for this core (at least `CAPAB_MM_DRIVE` in the capability word, `host/top.v:651`) and is
rebuilt and routed with `trx64` as the reference core.
**And, same day, on new contract ports:** "Well yes. Rebuild everything once, as soon as trx64 + host build" (translated). So the extension may add ports to the contract
(`contract/VERSION` up); every core is rebuilt once against the new host after host + `trx64` route. Which of the
contract items (tape, drive B, SID sockets/paddles/mouse, bus/IEC streams) go in is refined item by item.
**Tape (Alex, 2026-10-17):** "Only the pins go into the API for the cores. The core brings the logic/gateware itself" (translated). The host passes the cassette port pins (`CAS_*`: read, write, motor, sense)
through new contract ports and nothing else; playback (`c64_tape.v`, yosys 283 LUT / 143 FF, 2026-10-17) and recording
live in the core, fed by the application through the core's register window.
**Drive B: in** ("Yes" (Alex, 2026-10-17, on drive B)). A second drive instance in the core (about +1,820 LUT by the 1541 of run 275); both drives
share the IEC bus inside the core, so one line pair leaves it. Whether the 24-bit `drv_io_*` window carries the second
drive without a new port is **not checked**. The host sets `CAPAB_DRIVE_1541_2` in its word.
**SID sockets and paddles/mouse: both in** (Alex, 2026-10-17: "Both. For the SID it only matters to the C64 cores anyway" (translated)). The host passes the socket bus pins and the POT X/Y
lines of both joystick ports as contract ports; the logic is the core's. Cores that do not use them tie them off.
How many sockets the board carries is **not checked**.
**UDP streams (Alex, 2026-10-17, on moving them into the core):** "Analyse and test that in any case" (translated). Proposal under analysis: the host keeps the
Ethernet MAC and offers one generic packet port in the contract (request/grant, byte, valid, end of packet, ready);
every source - video, audio, bus, IEC - with its mux and header template moves into the core. Gain: the host's
~528 LUT of `udp_stream`/`udp_audio` (run 275). To prove: whether the core sees the 50 MHz Ethernet clock or needs
a crossing FIFO, the port's width, and a bench that sends a video and an audio packet through it.
**UCI: in, and into the core** (Alex, 2026-10-17: "Then into the core, please" (translated)). `uci_unit` (211 LUT + 1 RAMB18, yosys T28) moves from the host
into the core; the application reaches it through the core's register window, the host loses the block and `NO_UCI`.
**Not checked:** whether the window has an interrupt to the RISC-V (`io_irq` today), and whether Gideon's application
finds UCI at its fixed IO address or the `trex/main` application has to learn the core window.
**Capability word, first choice (Alex, 2026-10-17: "Trying it would be good. And if it does not build, then the way you wrote down as the fallback" (translated)).** The host hands its own capability bits to the core as a
constant input port of the contract (up to 32 bits, host cost 0 LUT); the core ORs them with its own and reports the whole
word in its window, so the application talks to the core only. The bootloader, which runs before any core, keeps reading
the ITU. If this does not build, the split below is the fallback.
**Capability word split, fallback (Alex, 2026-10-17: "Yes. Let us try it" (translated)).** The host's ITU word (`ultimate_sys.vhd:731`, a generic today)
reports only what the host has (USB, Ethernet, EEPROM, ...) - the bootloader reads it before any core is loaded
(`bootloader_u64ii.c:152`). Each core reports its own blocks (1541 A/B, 1581, REU, sampler, tape, UCI, streams, SID) in
its register window, as `trx_io.v:334` already does with `caps3`; the host only forwards the window's address range
(router). The `trex/main` application reads the core word after the core is up (never while decoupled) and ORs the
two.
**Withdrawn, Alex 2026-10-17: "STOP! No reSID8 in trx64" (translated); `trx64` stays exactly as it is, `TRX_SID: redip2` included. The entry below is history.**
**`trx64` in the host rebuild (Alex, 2026-10-17):** first "a rebuild, but no new gateware and no more LUTs", then: "A few LUTs more are ok, it just has to build and the timing has to hold" (translated).
Acceptance: it routes, WNS >= 0, `pr_verify` compatible; a small LUT rise is accepted.
*(Withdrawn)* reSID8 in `trx64` with the rebuild (Alex, 2026-10-17: "Yes. If need be, jtag debug goes" (translated)). T110's `TRX_SID: redip2` goes; about +110 LUT in layer 2
(T25.4), `trx64` near 79 %. Fallback named by Alex: the JTAG debug. Note: `jtag_dbg` (482 LUT) is in the **host**, so it
frees host area, not the core region where reSID8 lands. Alex then: `trx64` keeps its scope; what gives way if it does
not route is decided when a run says so, not now.
**Rule for the host (Alex, 2026-10-17: "Yes. The window sounds more sustainable" (translated)).** The host gets pins, clocks and the packet port, nothing else; everything
the application sets up (stream templates, stream enable, stream mute, UCI, capability word of the core) goes through
the existing `cw_*` window (`ultimate_sys.vhd:904`, 0x1Axxxx, 64 KB, already in the contract) and is decoded by the core.
Host cost 0; `r_udp`/`stream_ena` in the host are retired.

**Withdrawn the same day, Alex: "Yes" (to reverting it): the registered port broke the host route (pipeline 876 cancelled at WNS -3.2 ns, TNS -465 ns, the router 1 h 50 min in) and did not help `nes`, whose window runs on `cpu_clk` while the host port is on `sys_clk`. `nes_win.v` now starts its access from its own registers and ignores the host's `p_*` (JTAG and text-layer reads of the cartridge were already unsupported); the host stays as built in 874. The entry below is history.**
**Second RAM port registered in the host (Alex, 2026-10-17: "Ok. Since you make commits, nothing can break. Try it with the
host change" (translated)).** Pipeline 874: `nes` misses at WNS -0.949 ns on `win/p_ram_reg` -> host port mux
(`host/top.v:1247-1257`) -> `port_addr` -> `win/s_addr_reg`, 90 % route; NES synthesis and constraints are identical to run 841
(+0.114 ns), only the host placement changed. The path is a combinational round trip core -> static -> core in one clock and
it is real (the host grants tx/ap/dbg in the clock `win_port_use` falls), so no false path. Fix: the host registers what it
hands back on the second port (`port_*`, and the colour RAM port alike if it has the same shape), every requester's read data
arrives one clock later and the host consumers (tx `c64_text`, APPLY `ap_*`, JTAG `dbg_*`) and the cores' windows take it
there. Proof: the benches of those consumers (`dma_bus`, `ring_core`, `slot`, `steal`, whichever drive the port) pass, with a
control that reads the data one clock early and has to fail; pipeline: `nes` routes, trx64 and the host hold timing.

### What it is and what it is not (2026-10-18, from T86)

T86 (2026-10-12, "U64 core as close as possible to Gideon's, without the TRX64 ring, monitor and so on") is merged
into this spec. **What it is:** Gideon's set as his application sees it, run by Gideon's application (`make C64U=1`):
the C64, the cartridge/expansion port, one drive (Gideon's `mm_drive`, 1541 or 1581 as the app selects), the IEC bus,
REU, sampler (Ultimate Audio), turbo, UCI, the UDP streams, tape, paddles/mouse, the two SID sockets, and 8 SIDs.
**What it is not:**
- not Gideon's gateware 1:1: the SIDs are the reDIP engine (8 SIDs), not UltiSID (clean room); the C64 itself is
  icepi's/ours, Gideon's VHDL only where vendored (drive, UCI, tape);
- no TRX64 additions: no monitor, no timewarp ring, no debugger (`trx_engine`, `trx_dbg`, `drv_dbg`);
- no drive B (Alex, 2026-10-17: "let drive B go .. 1541 and 1581 work, just as one drive each"), no 1571
  (Alex, 2026-10-17: "We drop the 1571");
- no ACIA/SwiftLink/modem, no user port (Alex, 2026-10-17: "7. can go as far as I am concerned");
- no Copper;
- not a monolith: it is a layer-2 core behind the host's contract, swapped through ICAP.

## §1 The problem

The reference core `trx64` is ours: ring, TRX64 engine, debug path, and it leaves out much of what
Gideon's firmware expects of a C64 Ultimate. Alex wants the other end of the trade as a core he can
pick in the boot menu: **everything Gideon's stock core does, minus the TRX64 unique selling points
(monitor, ring, debugger).**

**Two things in the first draft of this spec were wrong and are corrected here.**
(1) S1 said "reu, sampler, uci, turbo and streams are on" as a property of the core. `uci` and `streams`
are switched when the **host** is built, not by the core (P4 below); the core's half of both is already in the
contract. (2) "No change to `host/`" cannot hold for a full
Gideon set; tape, a second drive and more are not reachable through the 228-port contract either
(P5). `core.toml`'s `features = ["TRX_FEATURES=jtag"]` is validated by `tools/core_toml.py` but
**nothing reads it**; only the workflow environment (`.gitea/workflows/bitstream.yml:87-88`) reaches a
build.

### What Gideon's stock machine is (the yardstick)

From his own config tables (`apps/ultimate` is TRX's fork; upstream is `/path/to/Development/1541ultimate`,
`upstream/master`): drives 1541/1571/1581 from one engine, A and B (`software/drive/c1541.cc:24,47`; MFM side
`wd177x.cc`, `mfmdisk.cc`); REU 128 KB-16 MB with Disabled/Enabled/GeoRAM and preload (`software/io/c64/c64.cc:141`);
UCI with six software targets at $DF1C/$DFFC/$DE1C (`fpga/io/command_interface/`); about 28 cartridge types
(`c64_crt.cc`); tape playback and recording (`CAPAB_C2N_STREAMER 0x80`, `CAPAB_C2N_RECORDER 0x100`,
`itu.h:56-57`); an 11-input audio mixer plus speaker mixer; two UltiSID cores plus two SID sockets; turbo
($D030/$D031/$D07A/$D0BC); sampler/Ultimate Audio Cartridge; ACIA/SwiftLink; UDP video/audio/bus/IEC streams.

### Parts (MECE)

Every capability lands in exactly one part, by *where its gateware lives and who decides it*.

- **P1 In layer 2, exists, only to be switched on.** `reu`, `sampler`, `turbo`, the SID build, the cartridge
  slot/expansion model. Chosen per core by `core_sources.tcl` (`core_synth` defines `NO_REU`, `NO_SAMPLER`,
  `NO_TURBO`, `TRX_SID`, `SLOT_EN`: `cores/trx64/core_sources.tcl:71-86,99-105`).
- **P2 In the tree, but tied off.** Tape: `vendor/icepi/c64_top.v:914` instantiates `c64_tape.v`, but
  `cores/trx64/c64_core.v:567` ties `.tape_play_toggle(1'b0)` and leaves the outputs open; `host/top.v:19` ties it off.
- **P3 Not in the tree, or not vendored.** 1571/1581, WD177x/MFM, drive B: VHDL exists in
  `apps/ultimate/fpga/1541/vhdl_source/` (`c1581_drive.vhd`, `cpu_part_1581.vhd`, `c1571_drive.vhd`, `mm_drive.vhd`,
  `wd177x.vhd`), but `tools/vendor_gideon.sh` copies only the 14 1541 files. ACIA, drive-sound mixer sources,
  tape recorder: absent.
- **P4 Host build switch: the machine half is in the core, the switch is the host's.** `uci`, `jtag`, `streams` go to `top_synth`
  (`core_sources.tcl:99-105`) and are applied in `host/top.v:633,1140,1163`. `host/build.tcl:28` sources
  `cores/trx64/core_sources.tcl` **unconditionally** for the host; `host/core.tcl:29` sources the per-core recipe and
  reaches layer 2 only. The host in use is built with `TRX_FEATURES: jtag`, `TRX_SID: redip2`
  (`bitstream.yml:87-88`): **UCI off, streams off, REU/sampler/turbo off, two reDIP SIDs** in that image.
  `ci/lib.sh:196` keys the host on `uci jtag streams`, `:204` the core on `reu sampler turbo`.
  **This is a build-configuration state, not a layering problem.** The contract carries both blocks' machine halves
  (verified in `contract/layer2_ports.txt`): **UCI**, ten ports - into the core `uci_c64_irq` (:74), `uci_freeze` (:75),
  `uci_oe` (:76), `uci_dout` (:130); out of the core `uci_ev` (:183), `uci_ev_io` (:184), `uci_ev_we` (:185),
  `uci_bus_addr` (:198), `uci_ev_addr` (:199), `uci_ev_data` (:226). `host/uci_unit.vhd:33-52` shows the split: the C64 bus
  side (cycle events, answer byte, IRQ, freeze) faces the core, `io_req`/`io_resp`/`io_irq` (the RISC-V mailbox) the host. With
  `NO_UCI` the partition ports stay but nothing is behind them (`host/top.v:633`, `ultimate_sys.vhd` `gen_no_uci` :633 vs `gen_uci` :649):
  **no core has UCI today, not even `trx64`.** **Streams:** `udp_stream` takes `pix_color`/`pix_x`/`pix_y` and `udp_audio`
  takes `audio_l`/`audio_r` with `audio_sample_en` (`host/top.v:1163-1190`), all outputs/inputs the contract already has; only the senders and
  Ethernet are host. Same state: built off (`NO_STREAMS`), contract complete.
- **P5 Layer 1 by contract: no port exists.** `contract/layer2_ports.txt` (228 ports) has one IEC output pair
  (`drv_clk_o`, `drv_data_o`, one `drv_io_*` window, one `gcr_io_*`), `SLOT_*` for the expansion port, `uci_*` hooks,
  `audio_l/r`, `c64_joy1/2`, `c64_keys`, and **no** tape (C2N) pins, no ACIA/user-port, no SID-socket control, no
  paddles, no second drive window. These are `top.v`/`ultimate_sys` matters (gap audit G07-G08, G11, G13, G16-G19; T37, T47,
  T58). Changing the port list raises `contract/VERSION` and invalidates every published core (`contract/README.md`).
- **P6 Area.** 18,000 LUT / 50 BRAM, and the densest layout that ever routed is 78 %.
- **P7 Left out on purpose** (monitor, ring, debugger): §4.

### Gap table: Gideon's stock capability against our gateware

Status: **yes** = in the layer-2 tree and switchable; **tied off**; **not vendored**; **absent**; **layer 1** = lives in `host/`.

| Capability (Gideon) | Gateware here | Where | What it takes |
|---|---|---|---|
| 1541 drive A | yes | `cores/common/drive_unit.vhd`, `drive_l2.vhd`; `vendor/icepi/c64_c1541/` | nothing (already in every C64 build) |
| Drive B | absent | contract has one `drv_*` window and one IEC pair | second instance in the core + a second register window; the `drv_io_addr` is 24 bits, so whether the host routes a second range without a contract change is **not checked** (T56) |
| 1571 / 1581, MFM, `wd177x` | not vendored | `apps/ultimate/fpga/1541/vhdl_source/{c1571_drive,c1581_drive,cpu_part_1581,mm_drive,wd177x}.vhd` | extend `tools/vendor_gideon.sh`, new `mm_drive` wrapper in the style of `drive_l2.vhd`, WD177x IRQs to ITU high 1/2 (`itu.h:41-47`; **host side not checked**), area **not measured** (T56 "trial synthesis" never ran) |
| Hardware GCR (`CAPAB_HARDWARE_GCR`) | partly | `gcr_io_*` ports in the contract; capability bit clear | bit is in `host/top.v` (`28'h5C40222`) |
| Drive sound | absent | G12 | mixer sources are layer 1 (`audio_l/r` is already mixed) |
| REU 128 KB-16 MB, GeoRAM | yes | `vendor/icepi/c64_top.v` / `c64_reu.v`, `core_sources.tcl` `NO_REU`; `reu_*` contract ports | switch on; 16 MB path is T60 (`reu_cmd_addr[20:0]`, `top.v:315`); capability bit clear (layer 1) |
| Sampler, Ultimate Audio Cartridge | yes | `trx_sampler.v` (`c64_core.v:506`), `NO_SAMPLER`, `smp_*` ports | switch on; app side G14 (capability bit 21 clear in `top.v`) |
| Turbo ($D030/$D031/$D07A/$D0BC) | yes | `c64_turbo.v`, `NO_TURBO`, `turbo_*` ports | switch on; timing history in T29 (CI 335-354, 497-499) |
| UltiSID 1/2, reSID8 | yes | `cores/trx64/trx_sid/`, `TRX_SID` default `SID_MULTI` | default; T25.5 §0 menu rule |
| SID sockets, stereo SID | layer 1 | G19, T37 | no contract port; out of the core's reach |
| UCI hardware + 6 targets | core half in contract, host half off | 10 `uci_*` ports; `host/uci_unit.vhd`; `core_sources.tcl` `NO_UCI` (host) | the host must be built once with `uci` on (Q1); no core work |
| UDP video/audio streams | inputs in contract, senders off in host | `pix_*`, `audio_l/r`, `audio_sample_en`; `udp_stream`/`udp_audio` at `host/top.v:1163-1190` | host built with `streams` on (Q1); no core work |
| UDP bus/IEC streams (2, 3) | absent | G15 | only `stream_ena[0..1]` exist |
| Tape playback + recording | tied off + layer 1 | `c64_tape.v` (`c64_top.v:914`) tied off at `c64_core.v:567`; `C2N_*` pins absent from `top.v` | needs contract ports and `top.v` pins, plus `irq_in(3)`; T58, T35; recorder is absent |
| ACIA / SwiftLink / modem | absent | G13 (`0x1004A000` is `r_none`) | block + host region + capability bit 27 |
| Cartridges (~28 types) | partly | `cores/trx64/cart_slot.v`, `slot_port.v`, `SLOT_EN` | which of the 28 exist is **not audited here** (T16, T57) |
| Real expansion port | layer 1 | `SLOT_*` ports present; bus bridge a latch (T16 §13) | T16/T06 |
| User port, paddles, mouse | layer 1 | G08, T47 | no contract ports |
| HDMI scan lines, resolutions, analog video, LED strip, $D7FF | layer 1 | G09-G11, G16-G17 | host matters |
| Freeze | in our tree | `dma_win.v`, `trx_io.v`, `trx_sys.v` | Gideon's freeze needs the DMA aperture; whether stock freeze runs without `trx_engine`/`trx_dbg` is **not measured** (D3) |
| Capability word | host | `0x05C40222` (`docs/gap-audit-gideon-app.md:48`) | REU, sampler, tape, drive B, hardware GCR, stereo SID, ACIA clear; a core cannot set bits in the host's word |

**Reading the table:** of Gideon's surface, the part a second layer-2 core can deliver *alone* is
REU, sampler, turbo, SIDs, the 1541, and (after vendoring and area) 1571/1581 on drive A. UCI and streams need only the host to be **built** with them on (their core half is in the contract); tape, ACIA,
SID sockets, user port and drive B need the host to be **changed** (new contract ports).

### UCI and streams: a host build state, stated plainly

UCI's and the streams' machine halves are in the core through the contract; the host we ship is built with them off, so
**no core has them today**. The decision is **when the host is rebuilt with `uci` (and possibly `streams`) on**, not whether a core can have UCI.
That touches the pinned host (`contract/host_pin.txt`; Alex's rule "no core folder may touch the host"), so it needs Alex's word.
Options, **not chosen here**:

- **(a) Rebuild the host now with `uci` (and `streams`) on.** No RTL change, only `TRX_FEATURES`; new host image, new pin. Costs: the host is at 76.96 %
  LUT, 21 of 25 BRAM, WNS +0.030 ns (floorplan, run 35); `uci_unit` is 211 LUT + 1 RAMB18 (yosys, T28), the streams 528 LUT (run 275,
  `docs/lut-budget.md`), so the extra is ~740 LUT, **not measured on the current host**. UCI then exists for every core, including `trx64`.
- **(b) Fold it into T94's one host.** No extra host build; this core cannot be board-tested with UCI before that.
- **(c) Leave UCI/streams off.** Then the core is "Gideon's set minus UCI minus streams", not "everything". Only Alex can choose that.

Tape and drive B are different: there the host change is a **contract change** (new ports,
`VERSION` up, every published core invalidated), which is heavier than a feature flag.

### Area estimate

The reference core: **14,103 / 18,000 LUT (78.35 %), 39 / 50 BRAM, WNS +0.030 ns** (`floorplan.html:107`, run 35),
with ring, debug and `dma_win` in and `reu`, `sampler`, `turbo` out. No per-module report of the current tree is
in the repository; the figures below come from **different tools and dates and disagree**, so the result is a range.

| Item | Figure | Source | Tool |
|---|---:|---|---|
| ring engine (`eng`) | 931 LUT | `docs/lut-budget.md`, run 275 | Vivado, post-route |
| ring engine | 1,377 LUT | T80-lean-ring.md in TRX64-Ultimate/docs/specs:9` (M1) | Vivado |
| ring engine | 1,687 LUT, 4 RAMB36 | `SLIM-state-path.md` §6.1 table (slim-core) | yosys, +-20 % |
| `trx_dbg` | 962 LUT | `lut-budget.md`, run 275 | Vivado |
| `trx_dbg` | 531 LUT, 2 RAMB36 | `ARCH-lean-state-access.md:1150` | yosys |
| freeze responder / `dma_win` | 531 LUT (run 275); 127 LUT (slim) | `lut-budget.md`; `SLIM-state-path.md` | Vivado; yosys |
| `trx_sys` / `drv_dbg` | 442 LUT / 111 LUT | `SLIM-state-path.md`; `ARCH...:1151` | yosys |
| REU | 541 LUT | `lut-budget.md`, run 275 | Vivado |
| sampler | 475 LUT + mixer 81, 1 DSP; later delta +41 LUT (run 554) | `T25.6-sampler.md:135,215` | yosys; Vivado delta |
| turbo | 530 (430-680) stage 1, 660 stage 2 | `T29-turbo.md:507-526` | **estimate, never isolated in a report** |
| `jtag_dbg` | 482 LUT | `lut-budget.md` | Vivado; host, not this core |
| 1581 / WD177x / second drive | **not measured** | | |
| tape (`c64_tape.v`) | **not measured** | | |
| 1541 drive (reference for a second drive) | 1,820 LUT | `lut-budget.md`, run 275 | Vivado |

Arithmetic, as ranges and nothing finer: out with ring + dbg + freeze ~= 2,400 LUT (run 275 sum 931 + 962 + 531), up to
~3,000 when the yosys/slim figures are used; `trx_io`/`trx_sys`/`dma_win` partly stay (D3), so take the low end.
In with REU 541 + sampler ~560 + turbo ~530-660 = ~1,650-1,750. Net about **-650 to -1,350 LUT, ~12,750-13,450 LUT
(71-75 %)** for the P1 set alone. BRAM: ring + dbg free some of the 39 tiles (about 6 RAMB36 by the yosys figures); REU data is in DDR, so
BRAM is not the constraint. **Verdict: the P1 set plausibly fits and sits below the reference's 78 %.**

The risk is what is *not* in that sum: if 1571/1581 + drive B are built, a second drive of 1541 size would be +1,820 by analogy (a 1581 with
WD177x is **not measured**), taking the core to roughly 14,600-15,300 (81-85 %). The tree has one data point above 78 %: **88.6 % did not route**
(`floorplan.html:84`), and the highest that routed is ~78 % (run 722). So the full Gideon set is **not shown to fit**; only
trial synthesis can say. Timing: the reference core carries WNS +0.030 ns, and turbo moved paths onto a 2x clock with WNS -0.033
(CI 497) before it was repaired (CI 499, +0.198); this core would be implemented against the cached host (T87 D8), so the host's timing does
not move, but the core's own paths with turbo on were never closed in the 18,000-LUT region.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: the set is what was decided (P1)
  Given the core built from its descriptor
  When  the build log prints the blocks it switched on
  Then  reu, sampler and turbo are on and the SID build is SID_MULTI (reSID8)
  And   the log names every block of Gideon's set that is NOT in (tape, ... per Q2-Q3), and whether the host under test has uci/streams (Q1)
  And   the ring, the TRX64 engine and the debug path appear in no report of this core

Scenario: ours is really out (P7)
  Given the routed core
  When  the hierarchy utilisation report is read
  Then  no cell of trx_engine, trx_dbg, drv_dbg is in it
  And   contract_ports.py --check passes against the unchanged 228 ports (or the new version, Q2)

Scenario: it fits (P6)
  Given the core stage of the pipeline
  When  it implements this core against the current host
  Then  pb_l2 is below 18,000 LUT and 50 BRAM tiles, WNS is positive, pr_verify is compatible
  And   the host's own numbers do not move

Scenario: the blocks are alive, not just present (P1)
  Given the benches turbo, reu, reu_map back in ci/sims.txt with this core's key
  When  the simulation stage runs them
  Then  they pass
  And   the same benches against a build with NO_REU / NO_TURBO fail

Scenario: UCI and streams (P4), only if Q1 is (a) or (b)
  Given the host with uci and streams on
  When  the uci and udp_audio benches run, then a C64 program sends "identify" at $DF1C
  Then  the bench passes and the board answers ULTIMATE-II DOS V1.2
  And   on the host as built today (uci off) the same program reads open bus (negative control)

Scenario: 1571 and 1581 (P3), if built
  Given a 1581 image (D81) mounted on drive A with Drive Type 1581 on the board
  When  a LOAD"$",8 and a file load run
  Then  the directory and the file load; with Drive Type 1541 the D81 does not mount (negative control)

Scenario: a user sees the stock machine
  Given the core started from the boot menu with its application
  When  the menu is walked
  Then  every entry Gideon's firmware has for a built block is there and works
  And   no entry shows for a block that is not built (the capability word is honest)
```

| # | Solves | Scenario | Test (bench / board step) | Where it runs |
|---|---|---|---|---|
| S1 | P1 | the set is what was decided | core stage log line | CI `bitstream` |
| S2 | P7 | ours is really out | `c64u.utilization_hier_synth.rpt`, `contract_ports.py` | CI `bitstream` + `lint` |
| S3 | P6 | it fits | `c64u.utilization_pb_l2.rpt`, `.timing_neg.rpt`, `.pr_verify.rpt` | CI `bitstream` |
| S4 | P1 | the blocks are alive | `turbo`, `reu`, `reu_map` in `ci/sims.txt`, plus a with/without build | `ci/sims.txt` |
| S5 | P4 | UCI/streams | `uci`, `udp_audio` benches (they test `host/`, not this core) + board step | `ci/sims.txt`, by hand |
| S6 | P3 | 1571/1581 | new drive bench + board step with a D81 | by hand |
| S7 | all | a user sees the stock machine | board session with photos | by hand |

The negatives that have to bite: S4's with/without build (a bench that passes either way is decoration, which is why the
five benches were deleted on 2026-10-14, T100 D6), S5's host-without-UCI read, and S6's wrong Drive Type. `uci` and
`udp_audio` test blocks of the host: with this core the T100 D6 reason ("we do not build these blocks") is gone only for
`turbo`, `reu`, `reu_map`; for `uci`/`udp_audio` it depends on Q1.

## §3 Decision

D1-D5 stay; what is now known is added. Q1-Q7 are Alex's, to be taken one at a time.

- **D1 The name and the descriptor.** `cores/c64u/`, `id = c64u`, `experimental = true` until it routes. **Now known:**
  because the ring-free core needs its own `layer2_c64u.v` (§4), it shares `c64_core.v`, `c64_mem.v`, `cart_slot.v`,
  `slot_port.v` only if those files can lose `trx_dbg` (`c64_core.v:810`) without a flag: they have **no** `ifdef`
  switch for it, so either `c64_core.v` gets one (a change in the reference core's file, which is host-pinned
  territory: `contract/host_pin.txt` pins `cores/trx64/*`, check which) or this core carries its own copy.
- **D2 Which blocks.** Per core by `core_sources.tcl`: `reu`, `sampler`, `turbo`, SID. **Now known:** `jtag`, `uci`, `streams` are switched in the host build (P4); their core halves exist. `core.toml` `features`
  is not read by anything; either `ci/lib.sh` learns to read it or the core's recipe sets the defines itself.
- **D3 What of ours stays.** Contract needs `dma_win`/`trx_io`/`trx_sys`-side ports; stock freeze needs the DMA aperture
  (`dma_win`). **Not measured** whether they can be reduced to stubs, and `trx_sys`'s read mux is also what the app's
  registers hang on. A bench with the null stubs decides it.
- **D4 The application.** `app = "ultimate"` (Gideon's application, as the reference core); leaning on `trex/main` of
  `1541ultimate_fw` (Alex). Open: the build is not in this repository (T93). Which menu entries the application shows must follow the capability
  word the host gives, which a core cannot change (P5).
- **D5 Area.** The P1 set fits by estimate (71-75 %); 1571/1581/drive B is the item that may not. Fallback order to be set by Alex with
  measured costs; the order proposed by cost only: drive B before 1581 before tape.

**Open questions for Alex** (one at a time; each with options and the cost):

1. **Q1 When is the host rebuilt with `uci` (and `streams`) on?** Options (a) now, (b) with T94's single host, (c) never, as in §1. (a): no RTL, new host image and
   pin update, ~+740 LUT on a host at 77 % (not measured on the current tree), UCI on in every core. (b): no extra build, no UCI board test before then. (c): "everything" no longer true. *Asked first because it decides whether `host/` is touched.*
2. **Q2 Contract change for tape, drive B, ACIA.** Options: new `contract/VERSION` and host rebuild with ports for them (every published core invalidated; ports
   would also be useful to other cores); or leave them out and list them in §4 as decided omissions; or tape only (T58). Cost of the first: the
   largest of all options, a host change.
3. **Q3 1571/1581 on drive A in this core.** *Answered 2026-10-17 (Alex, translated): "We drop the 1571. The 1581
   would be nice."* So the 1571 is out (§4) and the 1581 is wanted but not required. **Found while recording it:** the
   application refuses every image that is not a 1541 one when `CAPAB_MM_DRIVE` (0x800) is clear
   (`filetype_d64.cc:88`, `dos.cc:362`), and the host word `0x5E40202` has it clear. A 1581 in the core alone stays
   unreachable; it needs either that bit in the host or the `trex/main` application taking it from the core.
   The question as it stood: Needs vendoring `mm_drive` and friends (no contract change if drive A's window can carry it, not checked);
   area not measured. Options: build as part of this core; build after a trial synthesis shows the headroom; leave out.
4. **Q4 `jtag` / `jtag_dbg`.** It is host. This core has no say; the question is only whether the host carrying this core is a debug host.
5. **Q5 The core's own `layer2_c64u.v`.** *Answered 2026-10-17 (Alex, translated): "1. Fully self-contained. Copy the gateware over too, and the app."* `cores/c64u/` carries its own copies of the layer-2 top, `c64_core.v` and every gateware file it reads, and its own copy of the application; nothing under `cores/trx64/` or `cores/common/` is shared or changed. The question as it stood: Copy of `layer2_trx64.v` with the six blocks removed (fast, two files to keep in step), or `ifdef` the blocks in
   the reference file (one file, but a change in the reference core and the host pin). Cost: the contract check and the pin lint either way.
6. **Q6 Cartridge types.** *Answered 2026-10-17 (Alex, translated): "Well, 1, but check what would be needed for the rest."* The core takes the reference core's set; an audit (no code) lists which of Gideon's types `cart_slot.v` lacks and what each would cost. The question as it stood: The 28 types are Gideon's; which ones exist in `cart_slot.v` has not been audited. Options: audit first (a research item, no code), or accept the
   reference core's set.
7. **Q7 Order of work.** *Answered 2026-10-17 (Alex, translated): "Try 2, and if it does not work, then one after the other."* The first build carries every block at once; if it does not route or hold timing, the blocks go in one at a time, each with its own build, in the fallback order of D5. The question as it stood: P1 set first as `experimental`, measured; then each of Q1-Q3 as its own spec section. *Only after Alex has answered Q1-Q3 does any code start.*

## §4 Out

Deliberately not built (Alex, 2026-10-16: monitor, ring, debugger not in it):

- **Monitor, timewarp ring, debugger**, with the files that stay out of this core's `layer2_c64u.v`:
  `cores/trx64/trx_engine.v` (instantiated `layer2_trx64.v:589`), `cores/trx64/trx_dbg.v` (`c64_core.v:810`),
  `cores/trx64/drv_dbg.v` (`:582`), and, as far as D3 allows, `trx_sys.v` (`:612`), `trx_io.v` (`:471`) and `dma_win.v` (`:488`). None of
  these has an `ifdef` (grep count 0), so **leaving them out means a new `layer2_<name>.v` and a new `core_sources.tcl`**, not a flag. It must
  present exactly the 228 contract ports (outputs driven idle for the removed blocks).
- **ACIA / SwiftLink / modem and the user port** (Alex, 2026-10-17, on the host refinement list item 7: "7. can go as far as I am concerned" (translated)).
- **The 1571** (Alex, 2026-10-17: "We drop the 1571", translated).
- **Drive B** (Alex, 2026-10-17: "Ok, let drive B go .. 1541 and 1581 work, just as one drive each", translated; §5).
- No change to the reference core's behaviour; `trx64` keeps the ring and the engine.
- No ROM, no Kickstart, no disk image in the repository or in a release (the Atari OS exception stands).
- Everything that Q1-Q3 leaves out is recorded here by Alex's answer, with its date, not by omission.

## §5 Decisions taken while building

### Host v-next - the plan (2026-10-17, written before the code; method: fpga-gateware skill §1)

Scope as the coordinator handed it down (Alex's rules of 2026-10-17 are in §0): the host gets pins, clocks and one packet port; every application setting goes
through the `cw_*` window; the cores decode it. `contract/VERSION` 1 -> 2, then every core is rebuilt once.

**trx64 (Alex, 2026-10-17, translated): "That is ok. A rebuild, but no new gateware and no more LUTs."** So `cores/trx64/layer2_trx64.v` gets port declarations and constant
tie-offs only, uses none of the new inputs and adds no logic; `bitstream.yml` and `cores/trx64/core.toml` are not touched (no reSID8, no `T79_GREYBOX` change, T110 stays
open as it is). Acceptance: trx64's `pb_l2` LUT count in the CI report does not rise above the last reported one. Baseline recorded: **13,643 LUT (75.79 %), 39 of 50 BRAM
tiles, reference core, run 813** (`docs/todo/T94.md:187,202`; run 35's 14,103 is the older floorplan figure) - the coordinator confirms it against the last green run before the
comparison. The UCI hooks of `c64_core.v` that `layer2_trx64.v` wired to the removed `uci_*` ports are tied off (`io_oe`, `io_dout`, `io_irq` = 0, events left open): that is what the
shipped host already presented (`NO_UCI`), so the behaviour does not change; the logic that only fed those events can only get smaller.

| port | width | dir (seen from the core) | consumer in the core | host side | change |
|---|---|---|---|---|---|
| `uci_bus_addr`, `uci_ev`, `uci_ev_addr`, `uci_ev_data`, `uci_ev_io`, `uci_ev_we` | 16/1/16/8/1/1 | out | - | `uci_unit` | **removed** (UCI is the core's, behind `cw_*`; its IRQ is `cw_irq`) |
| `uci_c64_irq`, `uci_dout`, `uci_freeze`, `uci_oe` | 1/8/1/1 | in | - | `uci_unit` | **removed** |
| `host_caps` | 32 | in | capability word of the core window | constant `{2'b00, FPGA_TYPE, 28'h5E40202}` | added (first choice of 2026-10-17: the core ORs it with its own bits) |
| `pp_req`, `pp_data`, `pp_valid`, `pp_eof` | 1/8/1/1 | out | stream sources (core's) | `pktport_host` (crossing FIFO, grant, frame counter) | added; as benched, `cpu_clk` side |
| `pp_grant`, `pp_ready` | 1/1 | in | stream sources | `pktport_host` | added |
| `c2n_motor_i`, `c2n_sense_i`, `c2n_read_i`, `c2n_write_i` | 1 each | in | tape (core's) | pin level straight from `C2N_*`, asynchronous | added |
| `c2n_motor_o`, `c2n_motor_en` | 1 each | out | tape | IOBUF, released unless the core enables it and during `core_decouple`; **direction of the pin is not known from any source** (Gideon's XDC gives only LVCMOS33 and the pin; on the C64U the FPGA is the C64 and may have to drive it), so the core decides | added (Alex's coordinator, 2026-10-17) |
| `c2n_sense_o` (1 = pull low), `c2n_read_o`, `c2n_read_en`, `c2n_write_o`, `c2n_write_en` | 1 each | out | tape | IOBUF (SENSE open drain; READ/WRITE tri-state, pull-ups in `pins.xdc` as Gideon's XDC); idle = released | added |
| `paddle_cmp_i` | 4 | in | POT logic (core's) | `PADDLE_CMP[3:0]` (1.8 V), asynchronous | added |
| `paddle_dis_o`, `paddle_dis_en`, `paddle_adj_o`, `paddle_adj_en` | 1 each | out | POT logic | IOBUF, idle = released (high-Z) | added; **what DIS and ADJ drive is hypothesis (T47 §4.6)**, so the host never drives them on its own |
| `sid_sock1_n`, `sid_sock2_n`, `sid_sock_addr` | 1 each | out | socket decode (core's) | `SLOT_SID1n/2n/_ADDR`, idle 1/1/0 as the host held them | added |
| `adc_sdo`, `adc_bclk`, `adc_lrck` | 1 each | in | socket audio into the mixer (core's) | `AUDIO_SDO` pin, the host's own I2S `bclk`/`lrck` (`mclk` domain) | added - found while planning: the sockets' analog output comes back through the codec ADC (T37 §2.5). **Decided by Alex, 2026-10-17: "ADC in, yes" (translated)** |

Host side without a port: `stream_ena`, `udp_we/addr/wdata` leave `ultimate_sys` (the register at page 2 0x0F stays as a plain latch, region `r_udp` acks and ignores, the app writes the
streams' setup into the core window); `g_uci`, `uci_*`, `c64_clock` leave `ultimate_sys`; the UCI region `0x044000` acks with 0 (`gen_no_uci`'s answer, now unconditional).
`g_capabilities` (the ITU word the bootloader reads) keeps its value `0x5E40202`: trx64's application reads it, and `COMMAND_INTF` stays advertised - which is the defect T110 already
names, now moved to the core's word. `NO_UCI`/`NO_STREAMS` stop meaning anything to `top.v`; `core_sources.tcl` of trx64 still defines them (not touched, harmless).
`udp_stream.v`, `udp_audio.v`, `stream_mux.v`, `uci_unit.vhd` stay where they are as files (trx64's `core_sources.tcl` reads `stream_mux.v` by path; the benches `udp_audio`, `uci`,
`ntsc` use them); the host no longer reads or instantiates them. The new core takes them from there.
During a core swap (`core_decouple`) the host forces the port idle and `pktport_host` closes a half-written frame with an eof so the MAC side never sees a frame without an end.

**Estimate (yosys `synth_xilinx`, not routed, 15-25 % above Vivado):** host removed `uci_unit` ~211 LUT + 1 RAMB18, the three stream blocks ~288 LUT + 16 RAM32M + 198 FF + 3 RAMB18;
added `pktport_host` ~70 LUT + 104 FF + 1 RAMB18 and ~5 LUT for the decouple closing. **Net host about -430 LUT, -4 RAMB18** (the shipped host was built with both off - `NO_UCI`,
`NO_STREAMS` - so against the *shipped* image the host grows by the FIFO, ~+75 LUT, +1 RAMB18; the saving is against a host that would have had them on). New pins cost IOBUFs,
no LUT. 32 constant bits through the partition boundary: planned as ties (risk: one LUT each or a GND-net conflict); built as 32 DONT_TOUCH flops instead, see "as built". If that does not build, the fallback is the capability split of §0.
**Proof, in this order:** (1) local: `tools/contract_ports.py --check`, yosys parse of `host/top.v` and the pktport bench (iverilog), the `dma_bus`/`input` benches' port lists (GHDL, CI only);
(2) CI host job: the `pb_l1` LUT line of `utilization_pb_l1.rpt` against 10,005 (run 813) and WNS against +0.030 ns; (3) CI core job of trx64: `pb_l2` LUT against 13,643, and
`pr_verify`. Not provable locally: any VHDL change in `ultimate_sys.vhd` (no GHDL on the Mac), the IOBUF/constraint effect, timing of the 100 MHz FIFO write side.

#### Host v-next - as built (2026-10-17, branch `t103-c64u`, not routed, not on a board)

Built as planned, with these differences and findings:
- **Constants at the boundary (T87 rule, `tools/vivado_lib.tcl:60`).** A constant on a core output that has loads in the static fails the carve-out ([Route 35-341]); `layer2_trx64.v` keeps
  such outputs real with a DONT_TOUCH `LUT1` per bit (`l2_keep`). Doing that for the 25 new tie-offs would have cost 25 LUT in `pb_l2` and broken "no more LUTs", so the tie-off is
  one DONT_TOUCH **flop** per bit (`l2_tie_ff`, D = its own power-up value, clocked by `cpu_clk`): 25 FF, 0 LUT. Same reason for `host_caps`: the host drives it from 32 DONT_TOUCH flops
  (`host_caps_q`, 32 FF, 0 LUT), not from a tie net. Both are unproved until a run says `pr_verify passed` and prints `0 layer-2 output pins on a constant net`. Other cores tie
  with plain `assign`, as before (they are carved against the cached static).
- **trx64 changes (Alex: "no new gateware and no more LUTs").** Only `layer2_trx64.v`: the 10 `uci_*` ports gone, the 29 new ports declared, 25 outputs on `l2_tie_ff`, the inputs unused;
  `c64_core`'s UCI hooks tied (`io_oe`, `io_dout`, `io_irq` = 0, events open) - the shipped host presented exactly that (`NO_UCI`); `uci_freeze` is gone from `cpu_pause_req`.
  `core_sources.tcl`, `core.toml`, `bitstream.yml` untouched; `c64_core.v` untouched, so the cached core synthesis key does not move. Effect on `pb_l2`: -(UCI hook logic
  that opt_design can now trim) +0 LUT; measured only by the run.
- **`pktport_host.v`** moved from the bench into `host/` (git mv, the real file), plus: `dc` input (a frame left half-written by a core swap is closed with an eof byte and no new grant is given),
  `ASYNC_REG` on the synchronisers. Bench `sim/pktport/run.sh` now runs against it: port bench PASS, three controls fail; new `tb_pk_dc.v` PASS, control `NEG_NODC` fails.
- **`udp_stream.v`, `udp_audio.v`, `stream_mux.v`, `uci_unit.vhd` stay in `host/` as files** (trx64's `core_sources.tcl` reads `stream_mux.v`/`gideon_vhdl.tcl` by path and may not change); the
  host does not read or instantiate them any more except `gideon_vhdl.tcl`'s list (shared with trx64's layer-2 synthesis, compiled and ignored). Moving them into `cores/common/` is for the core that uses them.
- **Contract number 1 -> 2 everywhere it is a literal** (found by the lint stage, which failed on it): `core.toml` of all seven cores (`trx64`'s too: one line, forced by `core_toml.py --check`; its SID note is untouched),
  `tools/cor_pack.py` `CONTRACT`, `host-app/software/hostapp/cor.h` `COR_HOST_CONTRACT` (the host application refuses a core of another contract), `tools/release_mc.sh` text, and the host-app/cor_pack tests, whose
  "bundle for another contract" is now contract 3. Local: `tests/hostapp/run.sh`, `tests/coreapp/run.sh`, `test_cor_pack.py`, `test_make_release_mc.py`, `test_release_check.py` pass.
- **C2N_MOTOR is an inout** (follow-up, 2026-10-17): direction not known from any source; `IOBUF` released (T=1) unless the core sets `c2n_motor_en` and during `core_decouple`; ports `c2n_motor_o`, `c2n_motor_en` added, contract stays version 2 (nothing published with v2). `l2_tie_ff` is W=25.
- **Pins**: `pins.xdc` gets `C2N_*` (LVCMOS33, pull-ups on READ/WRITE), `PADDLE_CMP[3:0]` (LVCMOS18), `PADDLE_DIS/ADJ` (LVCMOS33), `AUDIO_SDO` (LVCMOS18), all from Gideon's XDC. Cassette as his U2
  top does it (`CAS_SENSE` pulled low or released, READ/WRITE tri-state); DIS/ADJ released unless the core enables them (what they drive is hypothesis, T47).
  `c64u.xdc` (Gideon's whole file, not read by any build) already had all of them.
- **Not done / for the application and the cores** (none is a host matter any more): the application writes the stream templates, the stream enable and the mute into the core window instead of `r_udp`/page 2
  `0x0F` (`apps/ultimate` is a submodule, not touched); the UCI window `0x044000` acks 0 in the host, so a UCI core decodes it at a `cw_*` address (`cw_addr` is 17 bits: 0x044000 does not fit, the application has to
  learn the new address); `host_caps` is used by no core yet; the 1581 still needs `CAPAB_MM_DRIVE` (the core ORs it in via `host_caps`).
- **Local checks (Mac)**: `tools/contract_ports.py --check` (7 cores, 247 ports each), yosys parse of `top.v`, `pktport_host.v` and all seven `layer2_*.v` with `default_nettype none` (control: a misspelt wire is caught),
  `top.v`'s `layer2` instance against `contract/layer2_ports.txt` (no port missing, none extra), `sh host/sim/pktport/run.sh`. **Not checkable here**: every VHDL change (`ultimate_sys.vhd`, `tb_dma_bus.vhd`, `tb_input.vhd`,
  `tb_usb.vhd`; no GHDL on the Mac - CI benches `dma_bus`, `input`, `eth`, `usb`), Vivado (IOBUF/OBUFT, constraints, partition pins, DONT_TOUCH flops), `tools/host_pin.sh --check` (fails by design until the coordinator runs `--update`).

### UDP streams behind one generic packet port - analysis and bench (2026-10-17, asked by Alex: "Analyse and test that in any case")

Prototype in `host/sim/pktport/` (bench only; `host/` and `contract/` untouched). The blocks are the unchanged `host/udp_stream.v`,
`udp_audio.v`, `stream_mux.v`.

**Where the clocks are.** The sender side of both blocks and `stream_mux` run on `clk50` (`host/top.v:1178,1187,1192`: `.eth_clk(clk50)`,
`.clk(clk50)`; the arbiter `top.v:746`, the MAC `top.v:736`). `clk50` is **not** a layer-2 port (`contract/layer2_ports.txt` has `cpu_clk`,
`sys_clk`, `mclk`, `ui_clk`, `sid2x_clk`, `c64_gclk`, no 50 MHz clock), so a core cannot run the senders on it. But the blocks already cross
clocks themselves: capture in `sys_clk` (`vclk`, `top.v:1175`) and `mclk` (`snd_clk`, `top.v:1184`), template in `cpu_clk`, sender in `clk50`
(toggle synchronisers, `udp_stream.v:101,141`). In the core the sender clock becomes `cpu_clk` (100 MHz, `top.v:218-227`); the only new crossing is the
byte stream, **cpu_clk -> clk50, one asynchronous FIFO in the host**. Template RAM then has writer and reader on the same clock.

**The port (cpu_clk side, 13 signals).** Core -> host: `pp_req`, `pp_data[7:0]`, `pp_valid`, `pp_eof` (11 bits). Host -> core: `pp_grant`, `pp_ready` (2 bits).
Same handshake as today's `tx_*` of the blocks and `stream_mux`; core clock and reset are the existing `cpu_clk`/`cpu_rst`. Two rules the host must keep, both found
by the bench:
1. **`pp_grant` only when a whole frame fits** (the host FIFO reserves 1024 bytes; the longest frame is 822), `pp_ready` then never drops inside a frame.
2. **`pp_ready` at most every second clock.** `udp_stream`/`udp_audio` read their RAM through a register; with ready every clock the payload is skewed by one byte
   (control `NEG_FULLRATE`). On `clk50` the RMII transmitter's one byte per four clocks hid this. The host makes the strobe (one FF).
The host asks the MAC arbiter (`st_req`) only when a whole frame is in the FIFO (eof toggle across, a 3-bit frame counter), because
`rmii_transceiver` wants the data held from valid until ready, no gaps. The arbiter `top.v:744-760` stays as it is.

**What else the core needs besides the 13 signals** (not in the contract today, `grep` of `layer2_ports.txt`):
- template writes and enable: today the application writes region `r_udp` (`ultimate_sys.vhd:943,1250-1253`) and `stream_ena` (page 2 reg `0x0F`, `:1215`);
  both are host VHDL outputs in `cpu_clk` (`top.v:434-435,691`). New inputs `udp_we`, `udp_addr[7:0]`, `udp_wdata[7:0]`, `stream_ena[7:0]` (18 bits), or the same
  bytes routed through the existing `cw_*` window (`ultimate_sys.vhd:905,1011-1014`, 17-bit address, `r_cw` only decodes `0x04Cxxx`, `0x1Axxxx`, `0x0480xx`): then no
  new port but a new decode in `ultimate_sys.vhd` and the core must decode it. Both change the host.
  **Settled 2026-10-17 by the host rule (§0 amendments):** through `cw_*`, and with **no** host change: the application is
  ours to change, so it writes the template and the enable at new addresses inside the `0x1Axxxx` range that `r_cw` already
  forwards (`ultimate_sys.vhd:904`); only the core decodes them. A new decode is needed only to keep the old `r_udp` addresses.
- `v60`: derive from the existing `vmode_sys[1:0]` (`layer2_ports.txt:92`), no port.
- `c64_mute`: the host mutes the stream's audio before the senders (`top.v:931-936`); the core has no such input. Either a new bit or the stream carries unmuted audio.
  Under the host rule the application, which issues `C64_STOP` itself, writes the mute into the core window too.
- the debug counters `st_frames`, `as_pkts` (`top.v:1095,1097`): leave the host (a core register instead).

**Area (yosys 0.x `synth_xilinx -family xc7 -flatten`, per module, 2026-10-17; yosys is typically 15-25 % above Vivado, and these are not routed numbers).**
Command per module `M`: `yosys -q -p "read_verilog -sv host/udp_stream.v host/udp_audio.v host/stream_mux.v host/sim/pktport/pktport_core.v host/sim/pktport/pktport_host.v; hierarchy -top M; synth_xilinx -family xc7 -flatten; stat"`
(LUT = LUT2..LUT6, INV not counted; RAM32M is LUT-as-memory, 4 sites each).

| module | LUT | LUTRAM (RAM32M) | FF | RAMB18 | CARRY4 |
|---|---|---|---|---|---|
| `udp_stream` | 162 | 8 | 130 | 2 | 36 |
| `udp_audio` | 134 | 8 | 97 | 1 | 24 |
| `stream_mux` | 23 | 0 | 3 | 0 | 0 |
| sum of the three | 319 | 16 | 230 | 3 | 60 |
| the three together (`pktport_core`) | 288 | 16 | 198 | 3 | 52 |
| host remainder (`pktport_host`: FIFO 2048 x 9, grant, frame counter) | 70 | 0 | 104 | 1 | 13 |

Moving the blocks: the host loses about 288 LUT + 16 RAM32M + 198 FF + 3 RAMB18, gains 70 LUT + 104 FF + 1 RAMB18: **net about -218 LUT, -94 FF, -2 RAMB18**
(yosys). It does not reconcile with the 528 LUT of run 275 (`docs/lut-budget.md`) - that figure is Vivado's for the shipped host and was not re-derived here.
The core pays +288 LUT, +16 RAM32M, +198 FF, +3 RAMB18 **inside the partition**, and `trx64` (the reference core, which must keep the streams) pays it too, on
the tightest partition (WNS 0.030 ns). Net over host + core the area grows by the host remainder.

**Bench** `sh host/sim/pktport/run.sh` (iverilog, 52 s on the Mac): the same pixels (31.5 MHz), audio samples (12.29 MHz), template and MAC frames (a source on clk50) go into
(A) the blocks wired as in `top.v` and (B) core on `cpu_clk` -> `pktport_host` -> the same arbiter and a transmitter model taking a byte per four clocks. Every frame
at the transmitter is compared byte for byte per kind. Result: 505 frames (A) / 506 (B) in 26 ms; video 68/68 (822 bytes), audio 5/5 (812 bytes), MAC 327/327,
**0 differ, PASS**. Controls, each must fail and does: `NEG_CORRUPT` (bit 0 of port byte 1000 flipped): 1 video frame differs at byte 178; `NEG_NOREADY` (no reservation, no
back-pressure, 512-byte FIFO): every video and audio frame differs; `NEG_FULLRATE` (ready every clock): every video and audio frame differs from byte 55.
Added to `ci/sims.txt`, tier 1.

**Not proved:** no Vivado run, so no timing of the FIFO crossing or the 100 MHz sender (the blocks were only ever timed at 50 MHz); gray-pointer crossing is
not checked by a CDC tool; the bench has no second MAC-side stall pattern (inter-frame gap is 30 clocks, ready is periodic). Decision on moving the streams is Alex's and open.
### First build with every block in (pipeline 881, `t103-c64u` 39b554a3)

Synthesis passed; `pb_l2` holds **17,306 of 18,000 LUT (96.14 %)**, 14,437 FF, 35 of 50 BRAM tiles
(`utilization_pb_l2_opt.rpt`). By instance (`utilization_c64u_hier_synth.rpt`, the C64 itself is an
out-of-context checkpoint, about 10,000 LUT by difference): drive A 2,435, drive B 2,308, packet port 707,
cw window 557, tape 389, cart fill 235, `c64u_sys` 208, UCI 165, GCR 80. The router did not converge
(nodes with overlaps 9,540 -> 25,751 -> 17,152, WNS -3.456 ns): no route at this density.

### The 1571 is removed from the gateware (2026-10-17)

Alex (translated): "The 1571 out! I never wanted it .. 1541 + 1581"; and on how: "Not just a parameter,
really take the gateware code that is not needed out of the core". Both drive instances carried the 1571
mode of `mm_drive_cpu`. Removed in `cores/c64u/gideon/` (PROVENANCE.md names each change): the 1571
bundle of `mm_drive_cpu` (address decode, VIA1 port A, the parallel cable on the CIA, fast serial,
side and 2 MHz); `m(1)` is now the 1581. The drive type register keeps bit 0 at 0 (`drive_registers.vhd`),
the GCR floppy of `mm_drive.vhd` gets side 0 (double-sided GCR was only the 1571's). The app
(`t103-c64uapp` 184c01f6) offers 1541 and 1581 only: the setting, no 1571 ROM, no "Switch to 1571", and
`set_drive_type()` refuses the 1571, so a D71/G71 mount and the REST mode fail with
`SSRET_WRONG_DRIVE_TYPE`. Checked: ghdl analyses and elaborates `c64u_drive_l2`; both app variants compile.
The LUT saving is measured by the next build.

### Drive B out, the SID sockets next (2026-10-17)

Pipeline 882 (no 1571) synthesised at 17,286 LUT (96.03 %), 20 fewer than 881: Gideon's 1571 shares nearly all of its
hardware with the 1541 and the 1581. Alex then (translated): "Ok, let drive B go .. 1541 and 1581 work, just as one drive
each" - drive B and `c64u_dp_arb` are removed (`t103-c64u` 44da4ed6), `CAPAB_DRIVE_1541_2` is no longer set. And: "try to build
the core without drive B and then, if it works, including the two SID sockets" - the sockets (T37 §4.3/§7, about 170 LUT,
1 DSP, not measured) go in after a build without drive B has routed; host v-next already passes `sid_sock*` and `adc_*`.

### Timing without drive B, the voice levels at 0, and a board check (2026-10-18)

The build without drive B missed by a few hundred picoseconds; the cuts that got it through are in
`t103-c64u` (manual builds tim2-tim6 on builder, `core_tim.tcl` against the cached host checkpoint). The last
miss was `sid_lv`: the host reads the voice levels combinationally across the partition edge in one cpu_clk
(`ultimate_sys.vhd`), routed -1.36 ns in this floorplan, and the application reads them only for the LED strip,
which is out (ADR-0007). Alex (translated): "Yes. Set it to 0." - and on whether the SID is still heard: "the
SID is heard though, right? That was only for the LEDs". `c64_top_c64u.v` ties `sid_lv_data` to 0.

tim6: WNS +0.039, WHS +0.017, pb_l2 14,472 LUT (80.40 %), pr_verify passed. On the board (SRAM load, Alex:
"Darfst aufs Board" - you may use the board): a sawtooth on voice 1 at 421 Hz through the UDP audio stream,
RMS 395/395, peak 803 - the SID is heard.

### The two SID sockets (T37), 2026-10-18

Alex (translated): "Try it, into the VHDL and build." Built as T37 §4 describes, with these choices:

- **Register path:** the cw window, `SOCK_*` at 0x088-0x094 (base, mask, enable, split) and the mixer bytes 4-7
  at 0x0A4-0x0A7; the application maps Gideon's `C64_IO_BASE` socket registers onto them under `C64U_APP`.
- **Decode only in `$D400-$D7FF`.** Alex (translated): "We leave D400-D7FF as it is. But think of the app!!! It
  must not suggest anything else then." The application (`t103-c64uapp` ef33ee84, local) offers only the
  addresses the core decodes (`SID_BASE_LAST` 32 under `C64U_APP`) and refuses a base at or above 0x80.
- A socket answers a read before the UltiSID, a write goes to both; the turbo's fast SID path is closed for a
  socket address.
- **The port runs PHI2 while a socket is enabled** (T37 Q2 taken as "drive every cycle"); CS low in core ticks
  2-17 (still low when PHI2 falls), SID_ADDR from A5..A9 per `STEREO_ADDRSEL` 1-5; a pause parks PHI2.
- **Audio:** an I2S receiver on the codec's ADC data; left = socket 1, right = socket 2 is a hypothesis until
  the board shows it (T37 M5). One time-shared multiplier, a disabled socket adds nothing.
- Bench `tb_c64u_sock` (`run_sock.sh`, `ci/sims.txt`): "PASS c64u_sock (bench passes, all controls fail)", six
  controls. Not benched: the decode in `c64_top_c64u.v`. Not on the board: no chip here.

**The sockets did not fit at first.** tim7: pb_l2 15,070 LUT (83.7 %), post-place WNS -0.618, the route
diverged; tim8 (accumulators of the mixer and the sampler in DSP48s): 14,618 (81.2 %), placement met, the route
diverged again. The worst path in both was an old one - block RAM -> turbo `t_din` -> 6502 registers, a half
cycle of clk2x - stretched by density, not the socket logic. tim9 adds the UDP header templates as LUTRAM with
one read port instead of 512 FFs and a 64:1 mux: **pb_l2 14,248 LUT after opt (79.2 %), 14,029 routed, 30 DSP,
WNS +0.039, WHS +0.027, pr_verify passed** (`t103-c64u` c8186401). Every cut is behaviour-neutral and checked:
the DSP moves as netlists against the old RTL with random stimulus and biting controls (`use_dsp` broke the
socket audio silently, so that one shares one adder per side instead), the templates by `run_pktport.sh`
(byte-identical to the host's senders, both controls fail). Measured threshold: pb_l2 routes at about 80 %
and not at 81 %.

Pipeline run 887 (`t103-c64u` c8186401) built the same: "=== core c64u: WNS 0.039 ns", "pr_verify passed",
1,521,494 bytes partial, 1,746,377 full; the c64u stage took 28 minutes and the run finished inside its limit.

**On the board with Alex's KickSID (2026-10-18, tim9 in SRAM, the `t103-c64uapp` build, SRAM/JTAG only).**
reSID8 moved to $D500/$D520 and muted, a sawtooth on $D400, measured from the UDP audio stream (RMS L/R):
both sockets on 1162/1165; socket 1 only (Pan "Left 3") 293/880; socket 2 only (Pan "Right 3") 874/291; both off
9/9 (the control). The sockets play, the enables work, silence when off. Left and right come out the other way
round from the pan setting; Alex (translated): "Left and right have ALWAYS been exactly the other way round.
That's ok." Open: socket 2 at $D420 still plays a tone written to $D400 (the split or the KickSID's own address
decode, not checked yet); the boot-time detection reads FF FF and reports "None" (display only).

**Eight SIDs, turbo, and UCI on the board (2026-10-18, same load).** The Tuneful Eight (8 SIDs, left $D400/$D480/
$D500/$D580, right $D420/$D4A0/$D520/$D5A0 - reSID8 1 $D400, reSID8 2 $D420, split 1/4 (A7,A8)): all eight there,
Alex: "perfekter sound, auch besser als Gideons Sid" (perfect sound, better than Gideon's SID too); the same with
Turbo Manual at 64 MHz, where the picture flickers (a raster demo forced to 64 MHz). Before that each of the eight
SIDs and voices 1-3 were measured one by one from the stream (RMS 395 each) and OSC3 read back from all eight.
**UCI does not answer:** UltimateDemo2026 stops at "UCI: FAIL, not detected" with Command Interface enabled, and
$DF1C reads $08. An EF3 was in the port at the time; whether that alone explains it is open (UCI moved into the
core on 2026-10-17).

**A cartridge in the port switches the internal I/O off (Alex, 2026-10-18).** The original firmware keeps UCI and
UltiSID enabled with a cartridge in the port; since we allow EAPI on the original, our software/gateware switches them
off. The application already does it, for emulated and real
cartridges alike (T72, `c64.cc:407` `trx_io_devices_off`: REU, Ultimate Audio and UCI off). The finer rule is T73: a
cartridge with a definition protects only the ranges it uses, the rest of $DE00-$DFFF stays for the internal devices;
one without a definition keeps everything off (Alex, 2026-10-18, T73 history). The SIDs are not affected.

**The UCI was switched off by the sockets, fixed (2026-10-18).** The EF3 was not the cause: without it, with the sockets
enabled, $DF1D read $FF; with the sockets off, $C9 and UltimateDemo2026 found everything. An enabled socket arms the
port for PHI2, SLOT_STATUS b16 said "armed", and the application's policy (T72, `trx_io_devices_forced_off`,
`trx_slot_armed`) took that for a cartridge and cleared `CMD_IF_SLOT_ENABLE`. b16 now reports a cartridge only
(`t103-c64u` e1ab74c7, bench control `NEG_ARMED_SOCK`). Pipeline run 891: c64u WNS 0.039, pr_verify passed, and the
first CI build of the c64u application (`app_ref` ef33ee84, `C64U=1`) packed into `c64u.cor` (2,665,327 B).
On the board with run 891's bitstream and application, sockets enabled: $DF1D = $C9; the sockets play (RMS 1161/1165
with reSID8 muted); UltimateDemo2026 detects UCI, REU 16 MB, turbo 64 MHz, Ultimate Audio V01 and loads its music.
Heartbeat (8 SIDs, REU, samples) sounds good (Alex).

**Merged to main with the host v-next (Alex, 2026-10-18: "Ja").** Asked whether the merge may make the host of
`t103-c64u` (contract 2: pins and ADC for the sockets, the packet port, the new host pin) the host of every core on
main, Alex answered "Ja" (yes). Every core built green against it in runs 887 and 891; only c64u ran on the board.

