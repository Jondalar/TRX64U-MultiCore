# T157 — every core can swap the two joystick ports from its menu, one setting, kept per core

**Status:** ACCEPTED (2026-10-29), asked by Alex; D1 decided. Status check 2026-11-13: BUILT in amiga, a1200, nes, c128 (merged 51a88f3cd, 10-29); vc20 none; board S5 closed as done 2026-11-18 (shipped and in use, Alex; §5), CI bitstream proof not recorded. 2026-11-15 sync: SNES and Neo Geo were to get the swap 'on their merge' (§3, §5): Neo Geo has it (`coreapp/cores/neogeo.cc:77`, `NEO_PAD` -> `pad_swap` in neo_regs.v:55,132, from T143; no T157 bench or board record); SNES has none (`coreapp/cores/snes.cc` has no Joystick ports setting, no swap in cores/snes) - not built, not decided out.
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src`: one "Joystick ports" setting for every core) and
the swap bit in each core's own registers (D1). The host is not changed.
**Number:** T157.
**Origin:** GitHub issue #9 (a tester, 2026-10-29): "Option to switch 9pin port configuration. I.e joystick in Port2 is fine
for C64 core. However, Amiga expects it in Port1. It would be nice if we can avoid physically swapping ports between cores".
Alex, 2026-10-29 (translated): "make a spec that the generic core app always does this swap".
**Depends on:** T104 (generic app), T38 (`app_input`: physical and app joysticks merged in the host), T156 (USB pads land on
a port too).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a person with one joystick who switches between cores. The C64 convention is port 2; the Amiga, the Atari and others
  expect the joystick in their port 1. Today the person unplugs and replugs the joystick at every core change.
- **What happens today** (read from the code): the two 9-pin ports reach every core as `c64_joy1` / `c64_joy2`
  (`contract/layer2_ports.txt:116-117`), merged in the host from the pins and the application's injection
  (`host/top.v:1223-1229`, `app_input`: `phys_joy1 = pins_s2[4:0]`, `phys_joy2 = pins_s2[9:5]`). No host-side swap.
  Most generic-app cores already have the swap, each in its own core (a "Joystick ports" setting, `WHEN_LIVE`, an op that writes the
  core's register): `apple2.cc:63`, `atari800.cc:45`, `c264.cc:40-46` (`C264_JOY_SWAP`, at next reset), `cpc.cc:124`, `zxnext.cc:58`,
  `zx.cc:112-113` (a choice per port), and the C64 menu (`trx64u/menu.toml:1657`, `joyswaps`). **Missing:** `amiga`, `nes`, `vc20`
  (they wire `c64_joy1/2` straight in: `layer2_amiga.v:276`, `layer2_nes.v:277`, `layer2_vc20.v:299`), and whatever other core on
  the generic app has no entry.
- **Parts (MECE):**
  - **P1 The setting exists** in every generic-app core, same name and place ("Joystick ports": Normal / Swapped), without
    each core declaring it.
  - **P2 The swap is applied** live, to both directions of what reaches the core: physical ports, the application's
    injected joystick (keyboard-to-joystick, T156 pads).
  - **P3 Kept per core** like every other setting (`.cfg` beside the `.cor`); the default per core can be Swapped (a core
    whose machine expects its joystick in port 1 may ship that default).
  - **P4 One name:** the existing per-core entries keep working and look the same (name, values, place in the menu).
  - **P5 The C64 cores** (`trx64u`/`trx64g`, Gideon-based app) are out unless decided: their swap is `U64II_KEYB_JOY` in the
    closed core (T47).

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 the swap reaches the core
  Given a core running with "Joystick ports" = Swapped
  When  fire is pressed on physical port 2
  Then  the core sees fire on its port 1 (c64_joy1 bit 4 low) and nothing on port 2
  # bench: the core's own swap bit (iverilog / GHDL bench of the core's input glue), control: swap bit ignored -> FAIL

Scenario: S2 the application's joystick is swapped the same way
  Given Swapped and the application injecting joystick 1 (keyboard-to-joystick or a USB pad, T156)
  Then  the core sees it on port 2
  # same bench, app_joy path; control: only the physical pins swapped -> FAIL

Scenario: S3 every core has the setting and keeps it
  Given each core on the generic app (`app = "blueprint"` in its core.toml)
  When  the settings are listed / the value is changed and the core restarted
  Then  "Joystick ports" is there once, and the chosen value is back after the restart (.cfg)
  # tests/coreapp (menu golden per core, cfg round-trip); control: setting not merged -> FAIL

Scenario: S4 the existing swaps still work
  Given apple2, atari800, c264, cpc, zx, zxnext with Swapped
  Then  the core's swap register is written as before (unchanged behaviour)
  # tests/coreapp register trace (existing tests stay green)

Scenario: S5 board
  Given the Amiga core, joystick in physical port 2, "Joystick ports" = Swapped
  Then  the game in the Amiga's joystick port moves; switching back to the C64 core, port 2 still works without replugging
```

## §3 Decision

- **D1 (2026-10-29, Alex: "we already have it partly .. I swear I saw it today already, not only in a Gideon core", translated):**
  follow the existing pattern, per core: every generic-app core gets the same "Joystick ports" setting (Normal / Swapped), live
  where the core can, written to a swap bit in the core's own registers; the host is not changed. Q1 (a), the host-side swap, is
  dropped. The missing cores (`amiga`, `nes`, `vc20`, and any other generic-app core without it) are added; the name and the
  values are made the same everywhere ("Joystick ports", Normal / Swapped; `zx` keeps its per-port choice, `c264` its
  "at next reset" because its register is taken in reset).
- **D2 (2026-10-29, Alex: "plan it for tonight, also in the large cores", translated):** the large cores on `main` get it too
  (`amiga1200/mono`, `c128/mono`; `zxnext/mono` has it already). SNES and Neo Geo get it on their merge to `main`. The bitstreams
  are built by the CI run of the night after the push; the branch is pushed only with green benches and app tests.

## §4 Out

- The C64 cores on the Gideon-based app (P5), unless decided otherwise.

## §5 Decisions taken while building

- **Scope change (coordinator for Alex, 2026-10-29):** the large cores on `main` are in: `amiga1200` (same sources as `amiga`, plus its own forks of
  `amiga_core.v` / `amiga_regs.v`, whose `.diff` files are regenerated) and `c128` (`cores/c128`, `mono`). `zxnext` already has the swap
  (`zxnext.cc:58`, `ZXN_JOY` -> `zxn_keys.v:61-62` `jl`/`jr`) and is verified only. SNES (branch `t142-snes`) and Neo Geo (branch `t143-neogeo`) are not on `main`:
  they get "Joystick ports" on their merge. The Vivado lane is not run by the building session: the bitstreams are built by CI after the push.
- **Where each swap is** (the host is not changed, `tools/host_pin.sh --check` green, no port of `layer2_*` changed, so `contract/` is untouched):

  | Core | Setting (`coreapp/cores`) | Register | Swap in the gateware |
  |---|---|---|---|
  | amiga, amiga1200 | `amiga.cc` `s[7]`, `RegBind b_joy`, `WHEN_LIVE` | `AM_JOY_CFG` 0x0A7 b0 (`amiga_regs.v`, `joy_cfg`; no commit toggle: quasi static) | `amiga_input.v` `ja`/`jb` (the host's joy1/joy2 into `joystick0`/`joystick1`); `amiga_core.v` takes `r_joy` through two flip-flops into `sys_clk`; A1200 forks + `.diff` follow |
  | nes | `nes.cc` `settings[1]`, op `nes.set_joy`, `WHEN_LIVE` | `NES_JOY` 0x0A2 b0 (`nes_regs.v`) | `nes_pad.v` (joystick part of both controller ports; B/Select/Start stay with the port's keys); `nes_core.v` wires it |
  | c128 | `c128.cc`, op `c128.set_joy`, `WHEN_LIVE` | `C128_JOY` page E 0x024 b0 (`c128_regs.v`; COLD puts it back to 0, the restart writes the setting again) | new `c128_joyswap.v` (two flip-flops, then the exchange) between `layer2_c128.v` and the core |

- **vc20 is not done, and needs a decision from Alex.** `vc20_core.vhd:203-204` ORs `joy1` and `joy2` (`joy_n <= not (joy1(3 downto 0) or joy2(3 downto 0))`,
  `fire_n <= not (joy1(4) or joy2(4))`): the VIC-20 has one joystick port and the joystick works in either physical port today. A swap bit
  exchanged in front of an OR changes nothing, so the setting would do nothing. Options: (a) no setting on vc20 (the person needs no swap there);
  (b) a "Joystick port" choice (Both / Port 1 / Port 2), which changes the behaviour of the ports not chosen. Nothing was built for it.
- **Reports** (builder, `trx64-sim-env:4`, `nice`, 2026-10-29):
  - `cores/amiga/sim/run_input.sh`: `PASS tb_input errors=0` with four new checks (swap off: port 2 fire is Amiga port 2 only; swap on: it is Amiga port 1 and port 2 stays empty; swap on: host port 1 up is Amiga port 2; off again); control `AMIGA_MUT_NOSWAP` (the bit ignored): `ok NOSWAP mutant failed the input bench (2 FAIL lines)`.
  - `cores/nes/sim/run_padswap.sh` (`tb_padswap.v`, the serial read of `$4016`/`$4017` through `nes_pad`): `PASS tb_padswap` (swap off: port 1 = `10`, port 2 = `01`; swap on: port 1 = `01`, port 2 = `10`; off again `10`); control `NES_MUT_NOSWAP`: 3 FAIL lines. `cores/nes/sim/run_l2.sh` (Verilator, the whole `layer2_nes`): `PASS run_l2`.
  - `cores/c128/sim/run_regs.sh` (`tb_regs.v` with `c128_joyswap.v`): `PASS tb_regs` (JOY_SWAP reads 0, set -> ports exchanged, COLD -> 0 and not exchanged); new controls `REGS_MUT_NOJOY` (3 FAIL lines, first `JOY_SWAP set = 00, expected 01`) and `C128_MUT_NOSWAP` (2 FAIL lines, first `swap on: ports 00001 10000`); the three old controls still fail as they must.
  - `tests/coreapp`: new `test_joyswap.cc`, 77 checks, 0 failed (S3 and S4: the key once and `Normal`/`Swapped`, `WHEN_LIVE`; the register gets the bit on Swapped and 0 on Normal; `.cfg` has `Joystick ports=Swapped` and a new session reads it back and `regset_apply` writes the bit; for amiga, amiga1200, nes, c128 and, unchanged, cpc, zxnext; apple2, atari800, c264 and zx are checked for their key); controls `joy_nes_renamed`, `joy_nes_op`, `joy_c128_renamed`, `joy_c128_op`, `joy_amiga_renamed`, `joy_amiga_reg`. Menu goldens `menu_amiga`, `menu_amiga1200`, `menu_c128`, `menu_nes` each gain the one line `setting Joystick ports = Normal | Swapped (default Normal, live)`.
  - `cores/amiga1200/tools/check_wrapper_fork.sh`: all forks `ok` (the two `.diff` regenerated).
  - Whole-core runs with the changed sources: `cores/amiga/sim/run_l2.sh` (Verilator, `amiga_core` with the new register and sync): `PASS tb_l2 errors=0`; `cores/nes/sim/run_l2.sh`: `PASS run_l2`. `layer2_c128.v` has no simulation bench that elaborates it (the machine benches build `c64_core_c128` only); its three changed lines are compiled first by the CI bitstream. `core_sources.tcl` of the C128 lists its files by name, so `c128_joyswap.v` is added there; `ci/sims.txt` / `.lane` / `.deps` get `nes_padswap` (`amiga_input` and `c128_regs` were in already).
  - `tests/coreapp/run.sh` whole (builder): exit 0, 1011 controls all red as they must (6 new); `tests/hostapp/run.sh` exit 0; `tests/loader/run.sh`: 1 program, 40 controls, all as they must; `tools/host_pin.sh --check`: the host is unchanged (315 files).
- **Open:** the bitstreams of amiga, amiga1200, nes, c128 from CI (WNS, `pr_verify`; the Vivado lane of T89.1 §5 S11 is run by the coordinator's push). Board S5 (Amiga with a joystick in physical port 2): closed as done 2026-11-18 (last entry).
- **2026-10-29, vc20 decided (Alex: "a"):** no setting on the VC-20. Its one joystick port takes both physical ports (OR), so a
  joystick works in either port already; S3 holds for every core with two ports.
- **2026-11-18 - board steps closed as done (shipped and in use):** S5 (joystick port swap, then trx64u) - Alex 2026-11-15: "Under A
  there is nothing I would not class as DONE! That has been in the builds for days/weeks, we had bugs and re-tests in part"; Alex
  2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had this already 3-4 days ago" and "tick
  it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
