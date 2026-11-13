# T156 — USB gamepads drive the machine's own controllers in every core, first NES, SNES and Neo Geo

**Status:** PROPOSED (2026-10-29), asked by Alex. Status check 2026-11-13: BUILT and merged (15f484139, 2026-11-01): NES, SNES, Neo Geo pads, board pass 11-02 (Alex); open: real pad dump.
**Repo:** TRX64U-MultiCore — shared base of the generic core application (`coreapp/src`: USB HID report parsing, a pad
service), per-core descriptors (button maps), per-core gateware only where a core needs a register for pad state
(`cores/nes`, `cores/snes`, `cores/neogeo`; large cores since T164, the register lives in the core's own `trx/` sources, not in the host copy). Host gateware is not changed.
**Number:** T156.
**Origin:** Alex, 2026-10-29, at the board with an 8BitDo SN30 Pro on USB (translated): "I think USB joysticks do not
exist in Gideon's FW app .. but we could build that ourselves." Then: "Yes. And then we build it for NES and SNES."
**Depends on:** T104 (generic app), T141 (USB keyboard into cores: same USB plumbing, separate parts), T142 (SNES pad
`snes_pad.v`), the NES pad glue (`nes_pad.v`, T95 mapping).
**Milestone:** M3.

---

## §1 The problem

- Read from the board console (TRX64 core with the Gideon-based app, 2026-10-29): the 8BitDo is enumerated and installed
  as a HID device (`2DC8:6001`, "8Bitdo SN30 Pro", interface class 3:0:0, HID report descriptor 114 bytes), but nothing
  reads its reports. The Gideon-based app parses keyboard and mouse only (`apps/ultimate/software/io/usb/usb_hid.cc`;
  `usb_hid_set_joy1_output` / `JoystickOutput::setUsbPort1` carry mouse buttons only).
- The generic app reads the USB keyboard for its own menus (`coreapp/src/run.cc`, `system_usb_keyboard`) and the mouse
  (`mousesink`); no gamepad.
- Console cores have more buttons than a C64 joystick: SNES 12 (B Y Select Start Up Down Left Right A X L R), NES 8.
  Today they come from the C64 joystick (D-pad + one button) plus C64 keys (`snes_pad.v`, `nes_pad.v`).
- Parts: **P1** generic HID gamepad parsing in the app base (from the report descriptor: buttons, hat switch, X/Y axes
  with a dead zone), up to two pads; **P2** a per-core button map declared in the core's descriptor/TOML, so a core says
  which pad button is which of its buttons; **P3** NES and SNES receive the full pad state (gateware register written by
  the app, OR-ed with today's joystick/key sources); **P4** C64-family cores: pad to C64 joystick port 1/2 (D-pad + fire,
  later), not built in this first step.

## §2 Scenarios

```gherkin
Scenario: S1 the 8BitDo is a pad
  Given an 8BitDo SN30 Pro (2DC8:6001) on USB
  When  the NES or SNES core runs
  Then  the app reports "pad 1: 8Bitdo SN30 Pro" and every button and the D-pad change the pad state (bench: the parser
        on the real 114-byte descriptor and recorded reports)

Scenario: S2 SNES all twelve buttons
  Given the SNES core and the pad
  When  each of B Y Select Start Up Down Left Right A X L R is pressed in turn
  Then  $4218/$4219 show exactly that bit (board: a test ROM or a game's input screen; bench: snes_pad with the register)

Scenario: S3 NES
  Given the NES core and the pad
  Then  A B Select Start and the D-pad work in a game (board)

Scenario: S3b Neo Geo
  Given the Neo Geo core and the pad
  When  each of Right Left Down Up A B C D Start Select Coin is pressed in turn
  Then  exactly that bit of the machine's joystick word changes, on each player (bench: neo_regs + neo_pad, tb_pad.v; board: the BIOS test menu)

Scenario: S4 both sources
  Given the pad and the C64 joystick/keys at the same time
  Then  the core sees the OR of both; releasing the pad releases only its bits

Scenario: S5 unplug
  When  the pad is pulled while a button is held
  Then  its bits go to 0 within one report period (no stuck button)

Scenario: S6 negative control
  Given the register write disabled in a bench build
  Then  S2 fails
```

## §3 Decision

- **D1 (Alex, 2026-10-29)** USB gamepads are our own feature, in the generic app; built first for NES and SNES.

- **D5 (Alex, 2026-10-29)** "Build a 'Map Button/Joystick' into it too .. and then one should be allowed to choose it
  and save it as core.cfg" (translated): a menu action "Map buttons" walks the core's buttons one by one ("press the pad
  button for A ..."), the user presses the pad button for each; the result can be chosen (default map or the user's) and
  is saved in the core's `.cfg` beside its `.cor` (T148 core folder), per core.
- **D5 wizard keys (Alex, 2026-10-29: "let's try it, yes", translated):** a pad press assigns the button and moves on;
  RETURN keeps the current entry; the left arrow goes back one; RUN/STOP cancels without saving; the walk ends with
  "Pad map" set to Custom.

Proposed:

- **D2** One pad service in the base: HID report descriptor parser (generic, not per vendor), state per pad as a 16-bit
  button word + hat + two axes; a core's descriptor names its map (`pad_map` in the TOML: pad button -> core button).
- **D3** The default map follows the SNES layout printed on most pads (A right, B bottom, X top, Y left, L/R shoulders,
  Select/Start); NES takes B and A from the SNES-layout B and A.
- **D4** NES/SNES/Neo Geo gateware: one register per player (`PAD1`, `PAD2`, 12/8 bits) in the core's register block, OR-ed into
  the existing pad source; the app writes it on every report.

## §3a Open questions

- **Q1** Answered 2026-10-29 (Alex: "ja"): two pads in plug order, the first plugged is player 1, the second player 2;
  the menu shows the order.
- **Q2** Answered 2026-10-29 (Alex: "ok"): the analog stick also acts as the D-pad; a direction counts from 40 % of the
  travel (dead zone below).

## §4 Out

- C64-family cores (pad to joystick ports), Atari, A1200: later, on the same service (P4). Neo Geo moved in (below).
- **Neo Geo in (Alex, 2026-11-01, at the board, NES without the USB pad):** "the USB gamepad is missing? That must go into the game
  consoles... also into the NES" (translated): NES, SNES and Neo Geo get the pads in this task.
- Rumble, XInput-only pads that are not HID class.

## §5 Build notes

Built 2026-10-29 on branch `t156-pads` (from `t142-snes`). Written to the code as it stands; nothing here was seen on the board.

### 5.1 Read, compare, plan (before the gateware)

| Signal / register | Written by | Read by | Clock | What it needs | What the change gives |
|---|---|---|---|---|---|
| `nes_regs` PAD1 `0F8`, PAD2 `0FA` (8 bit) | the application over the core window (`cw_*`) | `nes_pad.v` `app1/app2`, ORed into `p1/p2` (`q1/q2`) | `cpu_clk`, both | the shift register loads `q1/q2` while `strobe` is high | no crossing: registers, `nes_pad` and the machine are one clock |
| `snes_regs` PAD1 `0F4-0F5`, PAD2 `0F6-0F7` (16 bit; `0F8-0FB` when built, moved in 5.8) | same, low byte first | `snes_pad.v` `app1/app2`, ORed into `b1/b2` (`c1/c2`) | `cpu_clk`, both | a latch (JOY_STRB) must see a whole word | the low byte waits in a staging register, the high byte write takes the pair: a word is never half old |
| `0F4-0F7` in the SNES, `0F8`/`0FA` in the NES | - | - | - | free: the SNES used `0F0-0F3`, the NES `0E0-0EF`; reads of unused addresses were 0 | unchanged edge: no port of `layer2_*.v` changed, so the partition contract (`contract/layer2_ports.txt`) and the host are untouched |

Cost: 16 + 16 flip-flops and a few LUTs per core (see 5.6 for the measured figures). The synchroniser flip-flops of `joy`/`keys` stay; the application's bits need none.

### 5.2 What is built

- `coreapp/src/hidpad.{h,cc}` - HID report descriptor parser and report decoder (pure). Fields: Button page buttons (up to four runs), hat switch (8 or 4 positions, null state), X/Y with a 40 % dead zone (`HIDPAD_DEADZONE_256`), the D-pad usages 0x90-0x93, report IDs, signed and 8/16 bit axes. A report shorter than the last field used is refused.
- `coreapp/src/pad.{h,cc}`, `pad_ops.cc` - the pad service: two pads in plug order, a source id per pad input (0-31 Button n+1, 32-35 the four directions), a map per core button, the register pair per player written low byte first, only when the word changed, zero when the pad leaves (S5), the capture of the mapping wizard, the maps as hex text. Operations `pad.set_map` (the setting), `pad.poll` (NES service), `menu.pad_map_1/2`; states `pad.1`, `pad.2` ("name buttons=.. dpad=.." for REST and the board).
- `CoreDesc::pad` (`PadSpec`: the two registers, the buttons' names in register-bit order, the default map). This is the "pad_map in the core descriptor": the descriptors are C++ records (`coreapp/cores/*.cc`); `core.toml` holds no per-core application data. Set for `nes` and `snes`; checked by `desc_check`.
- `Settings::extra_get/extra_set` - keys that start with "Pad " in the core's section are kept and written with the settings ("Pad 1 map=<hex>", "Pad 2 map=<hex>", two hex digits per core button; 00-23 a source, FF none). The setting "Pad map" (Default / Custom, live) is an ordinary item.
- Menu: tasks "Map buttons, pad 1/2" (`MenuCtl::begin_map`, `map_tick`, `map_key`); the run loop calls `map_tick()` while mapping. See 5.3 for the keys.
- USB: `host-app/software/io/usb/usb_hid.{h,cc}` gains `UsbPadSink` / `usb_hid_set_pad_sink()` (provenance and patches updated). An interface that is neither keyboard nor mouse is offered to the sink; the driver then reads its interrupt endpoint (the same pipe code as the keyboard and mouse) and hands every report over. Without a sink (the host app, the Gideon-based app) nothing changes. `coreapp/src/run.cc` registers the service; it shares the mouse sink's critical section. `apps/ultimate` is not touched.
- Gateware: `nes_regs/nes_pad/nes_core`, `snes_regs/snes_pad/snes_core` as in 5.1.
- The descriptor of a pad is kept (256 bytes) and readable over REST as the states `pad.1.descriptor` / `pad.2.descriptor` ("name VID:PID n bytes: hex"), and printed to the console a second time 15 s after the first pace that saw the pad, so that the 4 KB console ring cannot lose it.
- Console (UART) lines for the board: `[pad] <name> VID:PID: report descriptor N bytes, ...` followed by the descriptor as hex (16 bytes a line), `[pad] pad N: ... (buttons, hat, X/Y, report bits)`, and the first 96 changes of a pad as `[pad] pad N report: <bytes> -> buttons <hex> dpad <hex>`.
- The core's service period is 16 ms (NES `pad.poll`, SNES `snes.service`; its save check keeps its own 5 s): one report period of a 62.5 Hz pad.

### 5.3 Decisions taken while building (Alex to confirm; none was asked)

1. **Default map numbering: per pad, from a table.** Board 2026-10-29 (SNES, 8BitDo SN30 Pro `2DC8:6001`, D-input): the pad is parsed, hat and buttons arrive, the wizard works and saved `Pad 1 map=01040A0B2021222300030607` (B = Button 2, Y = 5, Select = 11, Start = 12, A = 1, X = 4, L = 7, R = 8). The first build's generic numbering was wrong for it. The descriptors now name a button by its position (`PAD_ROLE_*`: right, bottom, left, top, L, R, Select, Start); `pad.cc` resolves the role with the numbering of the pad's VID:PID (`numberings[]`; first entry `2DC8:6001`: source ids right 0, bottom 1, left 4, top 3, L 6, R 7, Select 10, Start 11). Every other pad gets the generic numbering, which stays an **assumption** (`pad.h`: Button 1 bottom, 2 right, 3 left, 4 top, 5 L, 6 R, 7 Select, 8 Start, an Xbox-style pad). The console prints "default numbering: <name>" at the plug. A pad is added to the table after its map was recorded.
2. D5 asks for the walk; the keys are this build's reading: a press on the pad assigns and moves on; **RETURN keeps the entry the button has now** (the map's, or what an earlier pass of this walk gave it) and moves on; **the left arrow goes back one** (the C64 left-arrow key arrives as '`' in `keymap_normal`, DEL as KEY_BACK; both are taken); **RUN/STOP and ESC cancel** (nothing saved, the menu stays open). The T104.2 menu rules (keycap letter on the action, the declarative TOML for actions) are **not on this branch** (`t142-snes` predates them): the task is a `Task` record in the descriptor (`menu.pad_map_1/2`), and when the T104.2 rules arrive its label gets its keycap there.
3. When the walk ends the setting "Pad map" is set to **Custom** at once (otherwise the recording would change nothing visible). "Reset to host defaults" removes the maps with the rest of the file.
4. One task per player ("Map buttons, pad 1/2"), because two pads are possible; it answers "No pad is connected as pad N" when there is none (a pad that is pulled during the walk cancels it).
5. A pad that is plugged in while no core is running a pad record, or a third pad, is read and dropped (a log line says so).
6. Q1 (two pads in plug order, player 1 = first plugged, a pad that leaves frees its place) and Q2 (the analog stick is a D-pad too, 40 % dead zone) are built as the recommendations of the spec: **recommendation, Alex not yet asked**. The right stick (Z/Rz) is not a D-pad.

### 5.4 Benches and what they prove

| Bench | Where | What | Controls (each must fail) |
|---|---|---|---|
| `tests/coreapp` `hidpad` | native | the parser and decoder on five descriptors written to the HID spec (D-input pad with hat, SNES-style clone with X/Y as D-pad, report IDs with signed axes and a 1..8 hat, 16 bit axes with two button runs and D-pad usages), a mouse, a keyboard, a one-button joystick, truncated input | dead zone, Y direction, first button number, report ID, sign extension, hat minimum, short report, a hat direction, a D-pad usage (9) |
| `tests/coreapp` `pad` | native | plug order, third pad refused, the D3 map on the real SNES and NES descriptors and a model of their registers (every mapped button, hat, stick, the write order low/high, write only on change), unplug while held (S5), custom map and mode, map text, `.cfg` round trip incl. a damaged map and a later setting write, the ops, the capture | 14 |
| `tests/coreapp` `padmenu` | native | the real `MenuCtl`: no pad, the whole 12-button SNES walk with D-pad on the hat, left arrow back, RETURN keep, RUN/STOP and ESC cancel, a pad pulled mid-walk, NES player 2 with two pads, the `.cfg` text, the map in force afterwards, a restart reading it back | 6 |
| `cores/snes/sim/run.sh padmod` | iverilog | `tb_pad.v`: `snes_regs` + `snes_pad` with the S-CPU's latch and 16 shifts: each of the 12 buttons alone on each player, all twelve, unplug, OR with joystick and keys, low byte alone not taken, registers read back | `SNES_MUT_NOAPP` (30 red), `SNES_MUT_PADORDER` (4 red) |
| `cores/snes/sim/run.sh padapp` | Verilator, whole core | the registers written over the bus, the S-CPU reads `$4218..$421B` | `mut_noapp` (S6) |
| `cores/nes/sim/run.sh pad` | Verilator, whole core | phases 4-6 of the pad program: the application's registers alone, ORed with joystick 1, cleared (unplug); the program reads both ports | `NES_MUT_NOAPP` |

**Proven on the board (Alex, 2026-10-29, SNES): the 8BitDo SN30 Pro `2DC8:6001` is parsed, hat and buttons arrive, the wizard works and saves.** Not yet seen: S2 in a game (every button), S3 (NES), S5 (unplug), two pads. **Not proven:** every other pad. None of the descriptors above is the 8BitDo SN30 Pro's (its bytes are on the board, not yet in this repository: the state `pad.1.descriptor` gives them, and they belong in `test_hidpad.cc`); S1 is seen on the board for the SN30 Pro, not in a bench. The parser is written to the HID 1.11 rules; "typical" shapes are not a measurement of a device. The USB driver hook has no bench (the driver is firmware on the board); it was compiled into `coreapp.bin` and nothing else.

The whole `tests/coreapp/run.sh` on the builder (u64-build-env:ci): 65 test programs, 895 controls, all as they must (after the last app commit). 33 controls are this task's (9 + 18 + 6; the numbering table, the pid match, the second dump and the kept descriptor were added after the board run); the golden menu files of the NES and SNES gained the new setting and tasks.

### 5.5 Board steps (for Alex; one session, nothing else needed)

1. Flash `snes.cor` / the NES `.cor` with the new `coreapp.bin` as built (files and sha256 in the history of `docs/todo/T156.md`); plug the SN30 Pro (D-input or whichever mode gave `2DC8:6001`) into the C64U's USB port.
2. Read the console or `GET` the state `pad.1.descriptor`: the descriptor as hex. **Send it** - they are the only measurement of this pad; the parser and the default map are checked against them.
3. Press every button once and the D-pad: the `[pad] pad 1 report: ...` lines show which Button number each one is. The state is also `GET` as `pad.1` (the REST state).
4. Run a game: S2 (SNES, a game's input screen) and S3 (NES). If the buttons are wrong: menu, "Map buttons, pad 1", press the button asked for; then check "Pad map" = Custom and restart the core: the map must be back.
5. Pull the pad while holding a button: the button must be released at once (S5). Plug a second pad if there is one.

### 5.6 Vivado

Builder (`ssh builder`, Vivado 2024.1, `vivado-env:2024.1`, `/cache/heavy.lock`), run by hand as the CI stages run them (the lane scripts need Gitea's environment); sources of the branch at `e90e60b2` plus the `place` line of `cores/nes/core.toml`, nothing pushed. Baselines are the CI cache entries of the same cores before the change.

| | LUT | FF | BRAM tiles | DSP | WNS |
|---|---|---|---|---|---|
| SNES, flat (`cores/snes/mono/build.sh`) | **21,195** (65.02 % of 32,600) | **20,427** | 71 of 75 | 22 | **+0.244 ns**, route errors 0 |
| SNES baseline (CI `mono-snes-04b1615d`, commit 8a3ed798) | 21,144 | 20,357 | 71 | 22 | +0.221 ns |
| NES, `pb_l2`, against host `2208e1ad` (the host on the board) | **13,170** (78.39 % of 16,800) | **6,033** | 11 of 50 | 0 | **+0.144 ns** (the host's own figure), `pr_verify` passed |
| NES, `pb_l2`, against host `39bdd119` (newest host in the cache) | 13,211 (78.64 %) | 6,033 | 11 | 0 | +0.112 ns, `pr_verify` passed |
| NES baseline (CI `core-nes-be91f53a`, host `2208e1ad`) | 13,170 | 6,017 | 11 | 0 | +0.144 ns |

The change adds 16 flip-flops to the NES (two 8 bit registers) and 48 to the SNES (two 16 bit words and two 8 bit staging bytes) and a few LUTs; the LUT counts move by less than the placer's spread between runs (SNES +51; NES 0 and +41 against the two hosts). The SNES baseline is the CI build of an earlier commit (8a3ed798), so part of its +70 registers and +51 LUT is not this change.

**NES timing needed a placement knob.** The first run (host `39bdd119`, default directive `ExtraTimingOpt`) ended at **WNS -0.077 ns** (5 failing endpoints; worst path `l2/nes/flags_q_reg[2]` to `l2/nes/mem/pa_reg[16]/CE`, 12 LUT levels of the mapper logic, a path this change does not touch; `pb_l2` 13,179 LUT): no bitstream. A post-route `phys_opt_design` on the saved checkpoint crashed Vivado (segfault, rc 139); a second placement with `Explore` met timing on both hosts. `cores/nes/core.toml` now says `place = "Explore"` (the core's own knob, `tools/core_toml.py`; it moves the core's key, so the CI lane builds it anew). The evidence is these manual runs; the CI lane run is the proof.

Files for the board (scratchpad of the session, `/private/tmp/claude-501/-Users-alex-Development-TRX64U-MultiCore/0ca06245-af10-4231-a6a6-09d268feb49b/scratchpad/t156/`): `snes.cor` 2,852,497 bytes sha256 `7b111660e572c5a5efdfd5366e5a066bd1748ea5acdf7ed8ce9074a5f446de4f` (flat bitstream `snes_mono.bit` sha256 `d6ff5aa2767cee432be2838c7a12f16aea51d2f1b9e8c6b58ea676d718050af7`); `nes.cor` 2,246,391 bytes sha256 `4d8e5bf14e6f7035a994784271afe4995d273b557d345b694badbbc5ee3038ef` (the partial for host key `2208e1ad`, the host on the board, sha256 `dc0769b2b8e5cd2e4d17c2014f55ddf0a62066383e54f761680eeb5536c06089`); both carry `coreapp.bin` 1,004,976 bytes sha256 `b4e2b98df745d0013d1a9244a678acb160f05bd997dbb86f4d24ed31572abd47` (the generic application of this branch; the slot takes 1,966,068). `tools/cor_pack.py check` passes on both `.cor`.

### 5.8 Merged onto t164, Neo Geo (2026-11-01)

Branch `t164-pads` = `t164` + `t156-pads` + Neo Geo. Why: NES, SNES and Neo Geo are large cores on t164 (own host copy in `cores/<id>/mono/host/`, built by `build_mono.tcl`); the pad registers are core sources
(`cores/<id>/trx/*_regs.v`, `*_pad.v`, `*_core.v`), so the host copy and `host/` are untouched and the merge needed no gateware port change.

- **Registers** (code: `*_regs.v`, `coreapp/cores/*_regs.h`): NES `PAD1 0F8`, `PAD2 0FA` (8 bit, as built); **SNES moved to `PAD1 0F4-0F5`, `PAD2 0F6-0F7`** because t164 put the audio peak registers
  (`PEAK_DSP 0F8`, `PEAK_OUT 0FA`, `HANDS 0FC`) where the pads were; Neo Geo `PAD1 0F0-0F1`, `PAD2 0F2-0F3` (16 bit, low byte staged, the high byte commits, as the SNES).
- **Neo Geo gateware:** `neo_regs.v` holds the two words in `cpu_clk`; `neo_pad.v` takes them through two flip-flops into the machine's 48 MHz clock (buttons only, read once per frame) and ORs bits 10:0 into
  `pad0`/`pad1` (MiSTer layout: b0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D, 8 Start, 9 Select, 10 Coin). The port swap (`0A3`) moves the host's joysticks, not the pads (the first pad plugged is player 1).
- **Neo Geo map (Alex, 2026-11-01, asked "by position or by name": "position"):** the positional map of the MiSTer core (`neogeo.sv`, `jp`; the layout of the Neo Geo CD pad) with the D3 numbering: A = the pad's bottom button,
  B = right, C = left, D = top, Start, Select, Coin = L; stick and hat as the D-pad. The alternative, the name map (`jn`: A = SNES A (right), B = SNES B (bottom), C = X, D = Y), is a table change in
  `coreapp/cores/neogeo.cc` (`padbtn_def`). Application: setting "Pad map", tasks "Map buttons, pad 1/2", the core's service runs `pad_pace` every 16 ms (it ran every 500 ms; its clock, save and log keep their own timers).
- **Merge details:** `PadSpec` is the last field of `CoreDesc`; `place = "Explore"` (a DFX-lane knob, `build_mono.tcl` does not read it) is dropped from `cores/nes`; the golden export of the real image
  gets the base's new `pad.*` operations and states (`tests/coreapp/golden/ops_trx64u.txt`, added by hand from the native export, to be confirmed by `tests/ui/export.py --update` on an image); a pad map saved under
  "Auto Save Config" Off/Ask waits with the other changes for `save()`.
- **Benches:** `sh cores/neogeo/sim/run.sh pad mut_pad` (iverilog, 36 checks: eleven buttons on both players, all, pulled, OR with joystick and keys, swap, commit, read back; controls `NEO_MUT_NOAPP`
  and `NEO_MUT_PADBYTES` fail); SNES `padmod padapp mut_noapp` at the new addresses; `tests/coreapp` `pad` with the Neo Geo block and controls `pad_neo_abcd`, `pad_neo_coin`, `pad_neo_service`.
  Results: see the history line of T156.

### 5.7 Open

- S1 on the real pad (5.4), the default map's numbering (5.3, 1), Q1 and Q2 (asked of Alex), the T104.2 keycap letter for the action (5.3, 2).
- P4 (pad to the C64 joystick ports) and the other cores: not built (§4).


