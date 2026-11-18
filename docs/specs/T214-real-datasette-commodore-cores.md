# T214 — A real datasette works in every Commodore core that has a C2N port (C128, SuperCPU, VC-20, C16/Plus4), as it does on the PET

**Status:** PROPOSED (2026-11-12). Corrected 2026-11-13 (status check): BUILT, merged to main (fc77f0d8b, run 871 green); S4 board LOAD per core open; C16/Plus4 motor polarity not measured. 2026-11-15 sync: S1 holds only where the comments were fixed (`pet.cc`; the stale text is still in `layer2_c128.v:550`, `layer2_c64scpu.v:544`, `layer2_c64u.v:490`, `c64u_io.v:37` and `layer2_pet.v:283`, deliberately taken back, see §5); C16/Plus4 on main writes TAPE_PIN 0x11 (`c264.cc:53`, WRITE never driven); the WRITE drive, bench `c264_tape` and the tester report of 2026-11-14 are on branch t214-c264-tape (not in main); C16/Plus4 LOAD fails on the board (§5, 2026-11-14).
**Repo:** TRX64U-MultiCore — cores (`c128`, `c64scpu`, `vc20`, `c264` gateware), the core app (`coreapp/cores/*`), comments in `pet` and `c64u`.
**Number:** T214.
**Origin:** Alex, 2026-11-12, after the PET result (GitHub #26): "nobody needs to do that any more if it already works with the PET. Same device." / "Then fix the comment. Build the cores that way." (translated)
**Depends on:** nothing. Related: `docs/todo/T58.md` (tape port in the C64 core, file/tapecart side) stays as it is.
**Milestone:** M2.

---

## §1 The problem

- **P1 Direction unknown in the comments.** `coreapp/cores/pet.cc`, `cores/pet/layer2_pet.v`, `cores/c128/layer2_c128.v`, `cores/c64scpu/layer2_c64scpu.v`, `cores/c64u/layer2_c64u.v` and `cores/c64u/c64u_io.v` said the direction of the C2N pins was "not known from any source" / "measured on 2026-11-14". A real datasette LOADs on the PET on the board (GitHub #26, tester, 2026-11-12). The pins are the same board, the same pins in every core.
- **P2 C128 and SuperCPU.** The port is wired like the C64 (`PIN_CTRL` 0x078 of `c64u_io`, reset 0 = released), but no app code writes the register: a real datasette cannot be used.
- **P3 VC-20 and C16/Plus4.** All c2n outputs are tied to 0 (`cores/vc20/layer2_vc20.v`, `cores/c264/layer2_c264.v`), the machine's cassette lines are `open` / `'1'`: the port does not exist.
- **P4 trx64u.** Same gateware as c64u; its app is built in TREX/TRX64-Ultimate (handover contract), not here.

## §2 Scenarios

```gherkin
Scenario: S1 comments say what is known
  Given the six files of P1
  Then  each says MOTOR out, READ and SENSE in (proven by the PET LOAD, #26), WRITE out (not proven by a SAVE yet)   # grep, review
Scenario: S2 the setting exists in every core
  Given the core app of C128, SuperCPU, VC-20, C16/Plus4
  Then  the menu has "Datasette port": "Off (file only)" / "External datasette", and the golden menu dump shows it   # tests/coreapp/golden/menu_*.txt, tools/dev.sh test
Scenario: S3 External datasette writes the pin register
  Given "External datasette" is selected
  Then  C128/SCPU: PIN_CTRL (0x078) = 0x13; VC-20: TAPE_PIN (0x0B0) = 0x13; C16/Plus4: TAPE_PIN (0x0B2) = 0x11; "Off" writes 0   # board: register read back (trxdbg)
Scenario: S4 a real datasette LOADs
  Given a datasette on the C2N port and the setting on
  When  LOAD is typed (PRESS PLAY ON TAPE appears, PLAY is pressed)
  Then  the motor runs, the program loads   # board step per core (tester / Alex): C128 (C64 and C128 mode), SuperCPU, VC-20, C16/Plus4
Scenario: S5 off means released
  Given the setting is Off (reset default)
  Then  no C2N pin is driven; the file/streamer paths of the C64-style cores work as before   # board: existing tape tests unchanged
Scenario: S6 the gateware builds
  Then  the VC-20 sim (cores/vc20/sim/run.sh) still passes; Vivado through the pipeline closes timing   # ghdl in trx-ghdl6, pipeline run
```

## §3 Decision

**D1 (Alex, 2026-11-12):** "nobody needs to do that any more if it already works with the PET. Same device." / "Then fix the comment. Build the cores that way." The directions are settled by the PET result: MOTOR out, READ in, SENSE in; WRITE out, wired but not proven by a SAVE on real tape.

**D2 Bits.** C128/SCPU use the existing c64u `PIN_CTRL`: b0 MOTOR, b1 WRITE, b4 READ+SENSE inputs = 0x13. b2 (the streamer drives READ) and b3 (SENSE pulled) stay 0 with an external datasette. (PET and C16/Plus4 write 0x11: their WRITE bit stays 0 until a SAVE is proven - on the C16/Plus4 cassette WRITE shares P1 with the serial CLK out, so IEC traffic would reach the pin; C128, SCPU and VC-20 write 0x13.)

**D3 VC-20 and C16/Plus4 follow the PET pattern:** a `TAPE_PIN` register (VC-20 0x0B0, C264 0x0B2, same bits as PET_TAPE_PIN), inputs through two flip-flops, outputs registered in sys_clk, every pin released at 0. No file streamer in these two, so READ and SENSE are never driven. VC-20: `cass_read` -> VIA2 CA1, `cass_sw` -> VIA1 PA6, `cass_motor` = CA2 (low = on), `cass_write` = VIA2 PB3. C16/Plus4 (7501, sources: MJK 7501 page ist.uwaterloo.ca/~schepers/MJK/7501.html, zimmers 264memnio.txt): P3 MOTOR out (low = on, KERNAL convention, unmeasured), P4 READ in, cassette WRITE shares P1 with the serial CLK out (unproven), P5 not externally accessible; SENSE read at $FD10 b2 (6529 CST sense, low = pressed).

## §4 Out

- SAVE to real tape is unproven until someone saves; WRITE stays wired (D1), the scenario is not claimed.
- trx64u: its app is built in TREX/TRX64-Ultimate; it needs the same setting writing `c64u_io` 0x078 = 0x13 there. Not changed here.
- File/tapecart playback in the new cores (T58).

## §5 Built

- 2026-11-12: comments fixed (S1); C128/SCPU setting; VC-20 and C264 gateware and setting. Not measured: C16/Plus4 motor polarity (P3), the WRITE-on-P1 sharing and the $FD10 bit 2 sense come from the sources in D3, not from a measurement in this repo - S4 on the board decides.
- 2026-11-12 (Alex: "A comment should trigger NO build at all"): the comment-only fixes in gateware of cores whose gateware does not
  change otherwise (c128, c64scpu, c64u, pet layer2/io and the 13 `mono/host/top.v` copies except c264) are taken back, so the push
  builds only VC-20 and C264. Those stale "direction unknown" comments are corrected with the next real change of each file.
- 2026-11-14 board, C16/Plus4 (recorded observation, test build a050e995 from branch t214-c264-tape, not in main): SENSE ok, motor ok, READ pulses arrive, SAVE runs; LOAD: the screen goes blank and never returns. Cause open. Next: a KERNAL tape-load bench with a VICE TAP and a capture of the SAVE pulses.
- 2026-11-15, correction of the 11-14 board line (chat log of 2026-11-14 14:58-15:04 UTC, build a050e995): Alex typed `10 PRINT "HALLO"`, `SAVE "TEST"` (RECORD+PLAY), rewound and `LOAD`: the screen blanks and never returns. So the core cannot LOAD its own recording; the cassette had held VIC-20/C64 data before, which does not matter here. A short-lived entry of the same day that blamed the recording format was wrong and is replaced by this line. Read of the 264 KERNAL (kernal-318004-05, local): it calibrates on the leader (`$E939-$E98A`) and classifies each pulse by its low time against TED timer 2/3 (`$E5FD-$E68D`); the VIC-20/C64 KERNALs count only the falling edge. The READ path pin -> `$01` bit 4 is the same as the VC-20 one (`c264_core.v:119`, `:240`). The tester (GitHub #86) reports LOAD of a TAP written by the C64U TAP writer works and SAVE gives no usable data. `tb_c264_tapeload.v` proves only SEARCHING.
- 2026-11-14 tester (pre-release main-ef67ea93; recorded in this spec on branch t214-c264-tape, not in main): VC-20, C128, SCPU and PET "LOAD and SAVE"; C16/Plus4 "reads or writes nothing". S4 is therefore not closed for any core by a recorded per-core pass in main.
- 2026-11-15, C16/Plus4 parked (Alex, translated): a tester tried the READ-inverted test build (pre-release test-66f89900, branch
  t214-read-inv): it does not work either. "It goes to the back until we have a solution ... a C264 core with full debug read-along,
  and I borrow a datasette or so." Next step when resumed: a c264 debug build that records the cassette lines (READ, WRITE, MOTOR,
  SENSE, the 7501 port) into the trace ring, plus a borrowed datasette at the board. Nothing is built until then.
- **2026-11-18 - board steps closed as done (shipped and in use):** S3/S5 (Datasette port register) in the VC-20, the C264, the SCPU
  and the C128 - Alex 2026-11-15: "Under A there is nothing I would not class as DONE! That has been in the builds for days/weeks, we
  had bugs and re-tests in part"; Alex 2026-11-18, shown these steps as open: "that is old stuff that has been running for ages, we had
  this already 3-4 days ago" and "tick it off in the TODOs/specs, otherwise it shows up again with some agent" (translated).
