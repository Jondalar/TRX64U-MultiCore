# OPT-bling: LED effects out of bling_io

Status: decided (A04, docs/done-audit-2026-10-04.md). Before removal: tag `park/led-effects`.
Measured on main (CI): `bio` 620 LUT, of which `u_bling` 284 and `u_case` 276 (led_engine).

## Goes

- both `led_engine` instances (keyboard LED chain 0x900-0x9FF, case LED strip 0x600-0x6FF):
  colour RAM, map RAM, ring/intensity/shift-lock-override logic, WS2812 and APA102 senders.
- `designs/c64_basic/led_engine.v` (deleted; build.tcl and PROVENANCE.md updated).
- the case strip outputs `LED_DATA` / `LED_CLOCK` are driven low in top.v (idle for both
  WS2812 and APA102). The ports of bling_io for them are gone.

## Stays

- `bling_keyboard` and the Bling RX block of bling_io (0x800-0x803): FIFO, FLAGS, IRQEN, GET,
  `lock_off`, IRQ 4. Unchanged, bit-identical logic.
- SHIFT LOCK state (`locked` from bling_keyboard) and its LED: a minimal fixed WS2812
  sender replaces the engine on `BLING_LED`. It refreshes continuously (about 2.4 ms frame, 310 us
  reset): 70 LEDs dark, the 71st amber while locked, dark otherwise. Amber is what the app writes
  (R 0xC0, G 0xA0, B 0) scaled by the app's default intensity 24/128: R 0x24, G 0x1E, B 0x00 on
  the wire, GRB order. The app's own colour and intensity are no longer honoured.
- case LED on/off and polarity (T08) is `case_leds.v` (`LED_CASE1n/2n`), not part of bling_io; untouched.

## Registers after removal

| Range | Write | Read |
|---|---|---|
| 0x600-0x6FF (case strip) | ignored | 0x00 |
| 0x900-0x9FF (keyboard LEDs) | ignored | 0x00 |
| 0x800-0x803 | as before | as before |

The app side (software/ultimate-app/software/u64/led_strip.cc, bling_board.cc) only writes
LEDSTRIP_DATA / MAP / FROM / INTENSITY / START (0xFF); it never reads any of them and never polls
the busy bit (0xFF read) nor a read-modify-write. So neutral 0x00 reads cannot hang or spin it.
Boot patterns and effect modes run against registers that go nowhere; their vTaskDelay loops are
unchanged. Consequences visible to the user: no keyboard lighting effects, no SID-reactive modes,
no case strip, the SHIFT LOCK LED is fixed amber/off. The "LedStrip" / "Keyboard Lighting"
menu items still exist and do nothing (app change is out of scope; firmware stays unchanged).

## Verification

- tb_bling_io: Bling RX checks kept; keyboard-LED and case-strip engine checks dropped (the
  engines are gone); new check of the SHIFT LOCK LED frame (timing, 70 dark LEDs, 71st amber when
  locked / dark when not) and of the 0x00 reads.
- tb_bling_io was failing on main: its end check expected SHIFT LOCK released by byte 0xC1, but since
  13b8357 SHIFT LOCK toggles on each 0x41 and 0xC1 is ignored. The bench is fixed to toggle with 0x41.
- CI build: compare package `opt-bling-latest` with `main-latest` (27,644 LUT, `bio` 620).

## As built

Branch opt-bling, CI run 412 (head aff7d7db), package `opt-bling-latest` against `main-latest`
(note: `main-latest` is the build of 9a42a01, older than the branch base 2967eff, so the totals
differ by more than this change; the `bio` rows are the clean comparison):

| | main-latest | opt-bling |
|---|---|---|
| `bio` (bling_io) LUT / FF | 620 / 270 | 69 / 55 (63 logic + 6 LUTRAM) |
| design Slice LUTs | 27,644 | 27,171 |
| design Slice Registers | 29,658 | 29,658 |
| Slices | 99.96 % | 99.90 % |
| WNS setup | +0.010 ns | +0.170 ns |

The FF total did not fall by the 215 FF of `bio` because main-latest is older than the base:
the other modules differ between the two builds (pix_layer/udp_audio from the merges in between).
`bkb`, `cled` unchanged (101/100, 57).

Further slimming of the remaining bling_io: not worth it. What is left is the 16 x 8 FIFO
(2 RAM32M), the read mux, and the SHIFT LOCK LED sender (15-bit gap/bit counter, 11-bit bit counter,
about 35 LUT); a smaller sender would save under 15 LUT.

Sims: tb_bling_io PASS (back in ci/sims.txt, tier 1), tb_bling, tb_keyboard, tb_case_leds PASS.
Dropped from tb_bling_io: the keyboard-LED section (WS2812 frame from colour RAM, map, ring start,
intensity, saturation, shift-lock override, queued second send, busy bit) and the case-strip section
(APA102 frame, WS2812 select, map register readback), because led_engine is gone. Added: SHIFT LOCK
LED frame (1704 bits, timing 1.25 us / 0.35 / 0.8 us, 70 dark, 71st amber/dark, three toggles) and
reads of 0x600-0x6FF/0x900-0x9FF = 0.
Why bling_io failed on main: the end check sent byte 0xC1 and expected SHIFT LOCK released
(flags 0x84), but since 13b8357 bling_keyboard toggles SHIFT LOCK on each 0x41 and ignores 0xC1, so
the lock stayed down (flags 0x86). The bench was stale, the design was right; the check now expects 0x86
after 0xC1 and the toggling is tested with 0x41.
Not tested on the board (no board access): the amber on the keyboard board's 71st LED.

## Status and history

The TODO.md table is an overview only; the row detail lives here (convention at the top of TODO.md). Decisions of Alex that apply project-wide are in docs/adr/.

### T08

Case and board LEDs

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0007 LED strip and lighting effects out of the bitstream (A04)](../adr/0007-led-effects-out-of-bitstream.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED (LED)** SHIFT LOCK LED amber when locked, dark when released; case LEDs ok. Board items of T08/T09 done.
- Task text: Case and board LEDs
- **In main 2026-10-06 (ba060ae, with opt-bling). Open: the case LEDs are not in Alex's board report of 2026-10-06.**
- **Alex 2026-10-04 (A04), branch opt-bling:** the case LED strip (LED_DATA/LED_CLOCK) leaves the bitstream, pins held low; case LED on/off and polarity (case_leds, LED_CASE1n/2n) unchanged. Spec docs/specs/OPT-bling.md.

### T09

Keyboard LEDs (Bling), SHIFT LOCK state and its LED

Milestone M1, status ✅. Open (one line, as in the TODO table): –

Decisions: [ADR-0007 LED strip and lighting effects out of the bitstream (A04)](../adr/0007-led-effects-out-of-bitstream.md).

Moved from the TODO.md row on 2026-10-07, wording unchanged. Dated entries, newest first; undated text stays attached to the entry before it.

- **Board 2026-10-07 evening (Alex, Freeze UI; CI run 514, package integ-m1-e2b2ca47, app 301ef8c3, trxmon.u2a built at app 9a1b996b = same trxmon source as integ-m1-app): PASSED (LED)** SHIFT LOCK LED amber when locked, dark when released; case LEDs ok. Board items of T08/T09 done.
- Task text: Keyboard LEDs (Bling), SHIFT LOCK state and its LED (top right)
- **In main 2026-10-06 (ba060ae, with opt-bling). Open: keyboard LEDs and the SHIFT LOCK LED are not in Alex's board report of 2026-10-06.**
- **Alex 2026-10-04 (A04), branch opt-bling:** keyboard lighting effects out (led_engine, parked at tag park/led-effects); SHIFT LOCK LED is a fixed WS2812 frame (amber 1E/24/00 GRB while locked, else dark), the app's LED registers 0x600-0x6FF/0x900-0x9FF ignore writes and read 0; keyboard data path unchanged. CI run 412: bling_io 620 -> 69 LUT, 270 -> 55 FF, WNS +0.170 ns. tb_bling_io back in ci/sims.txt (stale 0xC1 check fixed). The app's LedStrip / Keyboard Lighting menus remain and do nothing (app change not made).
