# T181 — Hotkeys while a core runs: act without opening the menu

**Status:** PROPOSED (2026-11-04, Alex).
**Repo:** TRX64U-MultiCore — the core app (`coreapp/src/`), possibly `host/app_input.v` (see §1, P3) and then every large core's host copy.
**Number:** T181.
**Origin:** Alex, 2026-11-04: "that is worth its own spec. Create it. Then we should also talk about 'what do we make controllable by
hotkey' and 'which hotkeys'" (translated), after a tester asked in GitHub issue #10 (2026-11-03) whether the volume keys work "while the
core runs" and wrote "C= + RESTORE would be great to launch the menu - same as the C64U firmware".
**Depends on:** T160 (master volume, SHIFT+/- in the menu), T177 (RESTORE + F1 at power-on), T38 (`app_input.v`).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a user in a running core who wants to change something (volume, the menu, ...) without opening the menu first.
- **What happens today:** the core app acts on keys only while its menu is open; the case Menu button (ESP32) opens the menu.
- **What the hardware gives the app (read 2026-11-04):**
  - The core app runs on the host's RISC-V also while the core runs.
  - It sees the whole 64-key state of the C64 keyboard and the C64U keyboard at any time (`host/top.v` l.952 `keys_cpu_s2` -> `ultimate_sys`
    l.680), and the RESTORE level (`U64_RESTORE_REG`, `host/u64.h` l.68, fed by `top.v` l.946). Reading them does not disturb the core.
  - The core sees the same keys: `host/app_input.v` blocks the physical keys only while the menu owns the keyboard (`ovl_own`), and never
    blocks RESTORE. So a hotkey reaches the core as well (RESTORE = an NMI in the C64-type cores; SHIFT + "+" types a "+").
- **Parts:**
  - **P1 Detection** in the core app while the core runs (poll rate, debounce, the combination held vs pressed).
  - **P2 Actions:** what is reachable by hotkey (to decide, §3a Q1).
  - **P3 Keys reaching the core:** accept that the core sees the combination, or hide it in `app_input.v` (a host gateware change: the host,
    every large core with its own host copy, the DFX cores against the new host; the contract's port list unchanged).
  - **P4 Keys:** which combinations (to decide, §3a Q2), so that no core's normal use is hit (C64 BASIC, Amiga, Atari, ZX, NES pads mapped to keys).
  - **P5 Feedback:** a short card on the picture (as the T160 volume card) so a hotkey is seen to have acted.

## §2 Scenarios (draft; proofs fixed once §3 is decided)

- S1 With a core running, each decided hotkey performs its action within 100 ms of the press; the menu stays closed unless the action opens it.
- S2 A key that is not a hotkey changes nothing (negative control: a bench that maps every key to an action must fail).
- S3 Holding a combination acts once (no repeat), except where repeat is decided (volume).
- S4 (if P3 hides keys) the running core does not see the combination; without the mask the bench sees it.
- S5 Every core: the hotkeys do not collide with that core's own key use (a table per core).

## §3 Decision

- **D1 Actions (Q1, Alex 2026-11-04):** menu open/close, volume up/down, reset, swap joystick ports, picture mode. Snapshots out (§4).
- **D2 The core never sees an action key (Alex 2026-11-04).** On C64, VC-20 and C128 every CTRL + key is a function (colours, RVS, PETSCII
  control codes), so a free combination does not exist; the host masks it. Mechanism (Alex: "that makes sense ... into the spec with it"):
  - The modifier always reaches the core: it is a normal key of the core, and at its press the gateware cannot know whether an action key
    follows. Only the action keys are held back, and only while the whole modifier is held.
  - The modifier is 1 key or 2 keys held together (Alex: "modifier can be 1 key OR 2 keys"), so a hotkey is up to 3 keys.
  - The app writes the modifier (two key indices of the 64-key matrix, plus RESTORE; "none" for the second) and a mask of the action keys
    into a host register; `host/app_input.v` drops the masked keys from the core's key vector while every modifier key is down. The app
    reads the unmasked keys (as today) and acts. No contract port changes; one host build (and the cores against it), then all app work.
  - The cost of a combination: in that core the combination cannot be typed any more (CTRL + R as reset means no CTRL + R in that core).
- **D3 Configurable per core (Alex 2026-11-04):** each core's descriptor brings a default map that is harmless in that core; the core's F8
  menu can change modifier and action keys; stored in the core's `.cfg`. Default for the C64-type cores: modifier CTRL; menu CTRL + RESTORE,
  reset CTRL + R, volume CTRL + "+" / CTRL + "-" (Alex). A USB keyboard's keys go through the app before the core, so the app filters
  them itself, no gateware.
- **D4 (Alex, 2026-11-06, board, translated: "big bug ... out at once. No overlays over the running core"):** a hotkey action shows
  nothing over the running picture (on the A1200 the volume toast turned the whole screen into a garbled double picture); the action is
  done, the console says what happened. Supersedes the "toast" of §5.

## §3a Open questions (one at a time)

1. What is controllable by hotkey (e.g. open the menu, volume up/down, mute, reset, swap joystick ports, next disk, freeze/pause, save a snapshot,
   picture mode)?
   **Answered (Alex, 2026-11-04):** "1. menu open/close, 2. volume, 3. reset, 4. swap joy ports, 5. switch picture mode" (translated).
   Snapshot (Alex: "exciting, but that does not work unless we tap the machine's state in the gateware, does it?"): right - the app reaches
   the machine RAM (DMA window) but not the CPU and chip registers; a snapshot is gateware per core. The MiSTer NES code we carry already
   has save states (`cores/nes/gen/nes.v` l.144-161, `regs_savestates`), not wired in `layer2_nes.v`; so for the NES it would be a core
   rebuild, not new logic. Snapshot is out of T181; a spec of its own if wanted.
2. Which keys (e.g. C= + RESTORE as the original firmware, C= + other keys, a key held at the same time as RESTORE)?
   **Alex, 2026-11-04 (translated):** C= + something for everything is "really dumb"; proposed: C= + SHIFT + a key ("that this comes up for
   real is very unlikely in my view"), or configurable per guest (core). Open: C= + SHIFT alone is a function of the C64-type cores (the
   KERNAL toggles upper/lower case on C= + SHIFT), so without the host mask (P3) the first two keys of the combination already act in the core.
3. P3: may the running core see the combination, or is the host mask wanted (host rebuild)?

## §4 Out

- Snapshots (save states): gateware per core, its own spec if wanted (Alex, 2026-11-04).

## §5 Build notes

**Host part (2026-11-05, Alex's go; coordinator proposal for the register map, built on branch `t181-host`):**

Register block at `0x10100560` (the T160 master-volume page, `ultimate_sys.vhd`; byte writes, write only, reset = feature off):

| Offset | Meaning |
|---|---|
| `0x60` MOD_A | b7 = used, b6..b0 = key: 0-63 = matrix index `col*8+row` (as MATRIX_KEYB), 64 = RESTORE. b7 = 0: the whole feature is off |
| `0x61` MOD_B | same; b7 = 0: the modifier is MOD_A alone; MOD_B used with MOD_A unused does nothing |
| `0x62..0x69` | action-key mask, byte n = keys 8n..8n+7 (bit = key `col*8+row`) |
| `0x6A` | b0 = RESTORE is an action key |

`host/app_input.v` (sys clock, the register crosses by two flip-flops like the app's keys): while every used modifier key is physically down,
masked physical keys (and a masked physical RESTORE) are dropped from the core's key vector. Details that the app must know:
- A hidden key stays hidden until it is physically released: releasing the modifier first does not show the core a key press.
- A masked key that is already down when the modifier completes drops out of the core's vector (the core sees a release).
- The modifier keys pass unless the app puts them in the mask (it must not). The app's own keys (`app_keys`, USB keyboard through the app)
  and `app_restore` (the app's NMI) are never masked; the app's reads of the physical keys (`keys_cpu_s2`, `U64_RESTORE_REG`) are unmasked.
- A key index above 64 never reads as down. With the modifier off, the vector is bit-identical to the old wiring (S7 below).
- The app writes the whole map (a few bytes) at core start and when the map changes; a write is quasi static (no atomic update of all 11 bytes).
- Proof: `host/sim/hotkey` (`sims.txt` `hotkey`, owner host): 335 checks (one- and two-key modifier, only A or only B down, release order
  both ways, action first / modifier second, RESTORE as modifier, app keys, `ovl_own`, 300 random vectors with the feature off, index out
  of range); 8 controls, each the RTL with one change (no mask, B ignored, no latch, modifier always down, everything hidden, modifier
  hidden, app keys hidden, RESTORE unmasked) must fail.
- No contract port changes; `contract/host_pin.txt` is updated (the host's content changed).

**Vivado, host + c64ref (2026-11-05, builder, `vivado-env:2024.1`, the CI's host Tcl and env: TRX_SID=redip2, TRX_FEATURES=jtag, T79_GREYBOX=1, T79_FLOOR=B; baseline = cache entry `host-96f6ab08` of main, whose host sources equal main's):**

| | baseline (main) | with the mask |
|---|---|---|
| `=== T79 pb_l1 \| Slice LUTs*` | 12794 | 12805 (+11) |
| Slice LUTs, full design | 21401 | 21391 |
| Slice Registers, full design | 24190 | 24492 (+302: 3 x 81 + 65 hid + register file) |
| WNS / TNS / WHS | +0.101 / 0 / +0.037 | **-0.062 / -2.359 (56 endpoints) / +0.055** |

The run is red on setup: all 56 endpoints are in `pix_mmcm` (HDMI), worst `hout/mtim/yact0_reg[3]` -> `hout/hdmi_enc/.../audio_sample_word_packet_reg[*]/CE`
(6.481 ns, 9 levels, route 59 %); `cpu_mmcm` +0.198, `pll_75` +1.779, the sys/C64 clock domains positive. The path is not touched by this change (the
netlist change moved the placement). First place+route iteration -0.108, the second pass (`routed_alt`) -0.062.


**Timing fix in the HDMI encoder (2026-11-05, Alex approved a register stage on the path):** `host/hdmi_enc/hdmi.v` took `video_field_end = cx == fw_m1 && cy == (ey_e - 1)`
with `ey_e = yact0 + vact` (adder, decrement, 11-bit compare) straight into the CE of `audio_sample_word_packet` (via `packet_picker`'s `else if (packet_enable)` chain).
Now `ey_m1 <= ey_e - 1` is a register (same `ce` block as `sx_m1` / `cy_in`) and the compare is `cy == ey_m1`. `ey_e` is the mode's, constant between mode changes, so
`video_field_end` and with it the packet slot (data island at cx 14.. of the line after the last active line) are the same pixel as before; the only difference is one
`ce` after a mode change, while the frame is realigning anyway. Proof: `host/sim/hdmi_enc`, `host/sim/hdmi_out` (decoded packets in every mode) unchanged PASS.

**Vivado after the register stage (2026-11-05, builder, `vivado-env:2024.1`, same env as above, `build.tcl` TRX_STAGE=host, ooc key 177f13217d720606 from the cache; the run's bitgen
stopped on a dummy `TRX_HOST_KEY8`, so numbers are from `routed.dcp`: `=== impl pass 1 (ExtraTimingOpt): WNS 0.091 ns`, `Post Routing Timing Summary | WNS=0.091 | TNS=0.000 | WHS=0.051`):**

| | with the mask, before | with the register stage |
|---|---|---|
| `=== T79 pb_l1 \| Slice LUTs*` | 12805 | 12802 |
| Slice LUTs / Registers, full design | 21391 / 24492 | 21399 / 24488 |
| WNS / TNS / WHS | -0.062 / -2.359 / +0.055 | **+0.091 / 0 / +0.051** (0 failing endpoints) |
| `pix_mmcm` | -0.062 | +0.218 |
| `cpu_mmcm` / `pll_75` / `sid2x_mmcm` | +0.198 / +1.779 / - | +0.310 / +1.883 / +0.091 (worst path of the design: C64 CPU `pcl_reg` -> `mem/ram_lo` RAM, 3 levels, not touched) |

Benches (builder, `trx64-sim-env:4`, before = `6641b67d8`, after = this commit): `hdmi_enc` PASS before and after ("9 rows decoded, 4 controls failed as they must"),
`hotkey` PASS before and after (all controls). `hdmi_out` (long: over 47 min single-threaded) ran on the new RTL (builder, 2026-11-06, merge 95d3c4a9c): both modes PASS, 9 of 11 controls failed as they must in the full run (8696 s, beside the CI nightly); `vicsel` and `gear1` hit the 7000 s run cap there and were rerun alone (`ONLY="vicsel gear1"`, about 80 min): "control vicsel: failed as it must (the VIC lock never came)", "control gear1: failed as it must (FAIL)", "PASS hdmi_out (... 11 controls failed as they must)".

**App part (2026-11-06, built on `t181-host`; read from the code, not from the plan):**

- Files: `coreapp/src/hotkeys.{h,cc}` (map, registers, detection, USB filter, actions, the settings items, operation `hotkeys.apply`), `coreapp/cores/hk_defaults.h` (the six default
  maps), `CoreDesc::hotkeys` (`desc.h`, a pointer to an `HkSpec`; null = no hotkeys, no items, registers stay off), `CoreIo::hotkey_regs` / `keys_read` (`io.h`, `hw.cc`),
  the loop in `run.cc` (`HkLoop`), `rawkeys.cc` and `host-app/software/io/usb/keyboard_usb.{cc,h}` (USB filter).
- Poll: `HK_POLL_MS` 10 ms, from the running loop (menu closed) and from the open menu (only the menu hotkey is looked at there: it closes the menu again, not while the host app has a
  session). Physical keys are read with `CoreIo::keys_read` (unmasked), the USB keyboard's raw state from `hk_usb_keys()`.
- Detection (`HkDetect`): an action fires once, when its key goes down while the whole modifier is held, or when the modifier completes while the key is down. Holding does not
  repeat, except the two volume keys: first repeat after `HK_REPEAT_FIRST_MS` 400 ms, then every `HK_REPEAT_MS` 120 ms. The modifier keys are never actions (`is_mod`).
- Feedback (D4 replaces the toast): the line of the action (`hk_step_volume`, `hk_swap_joystick`, `hk_next_picture`, `hk_reset` fill it; a core without that setting says so)
  goes to the console only; nothing is drawn over the running picture. `tools/check_hotkey_nooverlay.py` (in `tests/coreapp/run.sh`, with its selftest as the negative control) fails when the hotkey path names a UI call.
- USB filter: a USB keyboard's keys go through the app before the core, so the app does what the host does: `HkFilter::apply` removes the action keys from the matrix (hook
  `keyboard_usb_matrix_filter` in `keyboard_usb.cc`) while the whole modifier is held and keeps them removed until released; cores that take the USB keyboard as raw key events
  (`rawkeys.cc`) get the report through `hk_usb_report_filter` (usages of the action keys zeroed).
- Settings (F5 tab; D3's "F8 menu" is the core's menu, and since 2026-11-05 every core's settings sit on F5, T182 D5 amendment): for a core with `hotkeys`, `hk_extend` appends 7 items to the core's settings (group `input`, `WHEN_LIVE`, apply = `hotkeys.apply`): "Hotkey modifier" (presets Off, CTRL, C=,
  C= + CTRL, C= + L SHIFT, C= + RUN/STOP, CTRL + L SHIFT, CTRL + RUN/STOP) and one key per action (menu, volume up, volume down, reset, swap joystick, picture mode; 65 keys or none). Defaults
  come from the descriptor, the values live in the core's `.cfg` like every setting. `hk_apply` writes the 11 registers at core start and on every change; an action key equal to
  a modifier key is dropped from the mask.
- Volume steps the setting `VOLUME_KEY` ("Vol Master" in the TRX64U C64), swap toggles the joystick setting between 0 and 1 ("Joystick Input" in the C64; "Joystick ports (at next reset)" in
  the C16 / Plus/4; "Joystick ports" otherwise), picture mode steps `PICTURE_KEY` and wraps; reset is the operation `machine.cold_reset`.

**Default map per core (coordinator proposal, read from `hk_defaults.h` and the `hotkeys` field of each `coreapp/cores/*.cc`; Alex decided only the C64-type column: CTRL + RESTORE, R, +/-):**
Action keys in all maps: RESTORE = menu open/close, R = reset, "+" = volume up, "-" = volume down, J = swap joystick ports, P = picture mode.

| Map | Modifier | Cores | Why |
|---|---|---|---|
| `hk_c64` | CTRL | c64 | as `hk_cbm`, volume = "Vol Master", joystick = "Joystick Input" |
| `hk_cbm` | CTRL | c64scpu, vc20, c128 | CTRL + 1..8 / 9 / 0 are colours and RVS (`HKO_DIGITS`), the action keys are none of them |
| `hk_c264` | CTRL | c264 (C16 / Plus/4) | as `hk_cbm`; the joystick swap is a reset-time item |
| `hk_typing` | C= + CTRL | apple2, atari800, zx, zxnext, cpc | CTRL is a typing key there; C= + CTRL has no function in these cores (not verified on the board) |
| `hk_amiga` | C= + RUN/STOP | amiga | CTRL + Amiga + Amiga is the Amiga's reset |
| `hk_console` | CTRL | nes, snes, neogeo | the consoles take no key of the keyboard |
| none | - | ddt_mandel, ddt_novic, portdiag | no `hotkeys` in the descriptor: no items, registers off |

**Tests (2026-11-06):** `tests/coreapp/test_hotkeys.cc` 766 checks, 0 failed (S1 action within a poll, S2 other keys change nothing, S3 no repeat except volume, S5 every core's default
against its own keys, the 11 registers per map, the USB filter and the raw-key report filter, settings items per core). 13 controls, each the source with one change, must fail and do
(`tests/coreapp/run.sh`):

| Control | Change | What it proves |
|---|---|---|
| `hk_everykey` | any key fires | S2: a non-hotkey changes nothing |
| `hk_selfmod` | the modifier counts as an action | a modifier key is no action |
| `hk_nomod` | action without the modifier | the modifier is needed |
| `hk_repeat_all` | every action repeats | S3: no repeat while held |
| `hk_norepeat` | volume does not repeat | S3: the volume repeat |
| `hk_nowrite` | registers not written | the host gets the map |
| `hk_nolatch` | USB: hidden key not latched | a hidden key stays hidden until released |
| `hk_nohide` | USB: nothing hidden | S4 for the USB keyboard |
| `hk_modreg` | MOD_B register 0 | two-key modifier reaches the host |
| `hk_own_digit` | default key J moved onto a digit | S5: a default on a core function is caught |
| `hk_actmod` | action key equal to a modifier key stays in the mask | the modifier must pass |
| `hk_late` | modifier completing late does not fire reset | action first, modifier second |
| `hk_report_nolatch` | raw-key report: hidden usage not latched | as `hk_nolatch` for raw-key cores |

**Render (2026-11-06, builder, `u64-build-env:ci`, `tests/coreapp/render/run.sh`):** PASS, 253 checks, all controls fail as they must. New case in `render.cc`: Atari with `hk_extend` against the same core without it; the 7 hotkey rows (modifier "C= + CTRL", menu RESTORE, volume + / -, reset R, swap J, picture P) are on F5 Settings (tab 2), 5864 px differ; F8 System is identical (0 px), the rows are not on F8. The bench has no PNG goldens (pictures go to the out dir), so nothing was re-baselined.

Also: `ci/apptests.sh hostapp coreapp loader` PASS, RISC-V build of host app and core app OK.

**Open (not built, not verified):**
- Physical modifier held with an action key on the USB keyboard: that action key is not hidden from the core (the host mask sees only the physical keys, the USB filter only the USB keys).
- C= + CTRL as the modifier on Apple II, Atari, ZX, CPC is unverified on the board (no function of that pair read in the cores' key tables; nobody has pressed it).
- Board test of the hotkeys (no overlay, the volume changes; reset hotkey) is open (not run on hardware).
