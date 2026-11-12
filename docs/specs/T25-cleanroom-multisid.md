# T25 — Clean room: UltiSID, Copper, lp_filter (overview)

**2026-10-06 (Alex): the SID is now [T25.4-redip-ultisid.md](T25.4-redip-ultisid.md): Dag Lem's reDIP SID is extended to 8 SIDs in one pipeline instead of our own engine (T25.1 below stays as the record of the engine that was built).**

**2026-10-07 (Alex): the app's SID menus for our engine (name reSID8, reDIP filter model, UltiSID on Gideon's core) are [T25.5-resid8-menus.md](T25.5-resid8-menus.md).**

Status: overview, 2026-10-02. The draft of 2026-10-01 (time-multiplexed multi-SID idea) is now
worked out in T25.1; this file points at the three specs. Nothing built. Task: TODO.md T25.

| Spec | Block | Result in one line |
|---|---|---|
| [T25.1-sid-engine.md](T25.1-sid-engine.md) | UltiSID → own SID engine | one time-multiplexed pipeline, 8 SIDs / 24 voices / 8 filters, state in RAM, answers the app's UltiSID registers; ~1,350 LUT, 3 BRAM tiles, 3 DSP (reDIP today: 1,336 LUT, 6.5 tiles, 2 DSP for **two** SIDs) |
| [T25.2-copper.md](T25.2-copper.md) | Copper | no current app build compiles the Copper driver and the C64U app never uses it: keep `CAPAB_COPPER` = 0. **Won't do** (Alex, 2026-10-02) |
| [T25.3-lp-filter.md](T25.3-lp-filter.md) | lp_filter + closed mixer/resampler → own mixer and decimator | today one 1 MHz sample per 48 kHz frame is kept (aliasing); CIC /4 + fractional polyphase FIR on the exact 2400/49259 clock ratio, 10-source mixer with the app's gains and pans; ~830 LUT, 0.5 tile, 2 DSP |
| [T25.6-sampler.md](T25.6-sampler.md) | Sampler / Ultimate Audio (G14) | 8 voices, one time-multiplexed datapath, state in LUT RAM, samples from the REU's DDR2 port, mixer inputs Sampler L/R; decided 2026-10-07 (Alex: build it lean) |

## Clean-room rule

Gideon's UltiSID (`fpga/sid6581`), Copper (`fpga/io/copper`) and lp_filter
(`fpga/io/audio/lp_filter*.vhd`, `sys_to_aud.vhd`) VHDL are never opened, read, grepped or
quoted, not by us and not by any agent, locally (1541ultimate `fpga/`, `cores/gideon`) or on
the web. ue2emu's copper model is not read either until it is known not to derive from that
VHDL. Allowed: the app side of the interface (`software/ultimate-app`), Gideon's public user
documentation (no VHDL), data sheets, published analyses, open implementations with known
licences, our own measurements. Each spec lists its sources with licences: GPL sources are
fine for our GPL-3 project when they are GPL-3 or "or later"; CERN-OHL-S (reDIP) is a licence
mix still to settle; proprietary sources are behaviour only.

## Findings that changed the draft

- The app addresses **up to 8 UltiSID instances** (two UltiSIDs, each split in 2 or 4 by
  `C64_EMUSID_SPLIT`); there is **no model register**: the model follows the filter curve
  table and the combined-waveform setting (T25.1 §1).
- The app writes each UltiSID's **filter curve as a 1024 x 16-bit table** into RAM at
  0x10185000 / 0x10185800 (T25.1 §1.3); our engine uses it as its cutoff table.
- reDIP's 1,310-1,336 LUT are for **two** SIDs; the draft said one.
- **BRAM** (56.5 of 75 tiles, 75 %) is the tight resource, not LUT; T25.1 frees 3.5 tiles.
- The Copper has no user on the C64U (T25.2 §1.1).
- Our C64 clock and the audio MCLK come from the same 50 MHz crystal, so the decimator runs
  on a fixed, exact ratio without a rate servo (T25.3 §2).

## Combined budget

| | LUT | BRAM tiles | DSP |
|---|---|---|---|
| Today (report 2026-10-02 00:12; task figure ~23.7k LUT) | 22,317 | 56.5 | 14 |
| T25.1 replaces reDIP | ±0..+300 | -3.5 | +1 |
| T25.3 | +600 | +0.5 | +2 |
| T25.2 phase 1 (optional) | +300 | +1 | 0 |
| After T25.1 + T25.3 (from ~23.7k) | ~24.3-24.6k (75 %) | 53.5 (71 %) | 17 |
| 80 % ceiling of the 50T | 26,080 | 60 | 96 |

## Order

1. T25.3 first: it removes the aliasing of the SID we already have and gives T25.1 its output
   path (per-SID samples into the mixer).
2. T25.1: software reference harness (reSIDfp on register-write logs) → engine in Verilator,
   bit-compared → one SID replacing reDIP on the board → 8 SIDs with the app's addressing.
   reDIP stays a build option until the engine matches it.
3. Our own measurements on a U64/C64U with Gideon's firmware for the open questions (filter
   table scaling, resonance, digi level, base/mask decode, split panning): recordings of the
   audio stream, no VHDL.
4. T25.2: won't do (Alex, 2026-10-02).
