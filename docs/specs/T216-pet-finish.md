# T216 — The PET core works on every model with the right keyboard and menu, and its drive ROMs are documented

**Status:** BUILT except P9 (waits for T214, D-Q6); open: 5 UX questions for Alex, S12 wrong-content question, board steps (2026-11-15 night, merged with main). PROPOSED (2026-11-12). Reopened 2026-11-15 as the one PET spec (Alex: "bundle all PET bugs/enhancements into one spec", translated): GitHub #78, #79, #80, #82, #87 and the PET part of #86 are parts P6-P10 below; the parts built so far (P1-P3, P5 and their S1-S5) stay recorded as built, P4 stays decided (D4). Corrected 2026-11-13 (status check): merged to main (81fdcf008); S1, S2 bench, S3 golden, S5 guide built; S4 board and the board cause of S1 open. 2026-11-15 sync: verified in main - S1/S2 bench `tb_pet_models` runs in CI through `run_boot.sh` (`pet_boot`, `ci/sims.txt:48`), S3 row `pet.superpet` (`pet.cc:75`), S5 guide in `tools/release_notes_mc.py:165-171`; the stale 'Open: 2 KiB' note in §5 is removed (D3 says 1 KiB since cc1b7c55f).
**Repo:** TRX64U-MultiCore — `cores/pet/` (keys, model select), `coreapp/cores/pet.cc` (menu), `INSTALL.md` (docs).
**Number:** T216 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-11-12, after a tester's report (2026-11-12): "spec, plan it in, let's get the PET round, more or less" (translated). Reopens T134 (closed "as is" 11-11).
**Depends on:** T134 (PET core), decision 6 in `docs/generic-app-requirements.md` (key bundles) for Q3.
**Milestone:** M2.

---

## §1 The problem

A tester (2026-11-12) ran main-736c16d9 on the board. Working, to keep: internal and external datasette, disk through the 4040 once the ROMs are set up. Findings:

- **P1 Business keyboard wrong on 8032 and above (GitHub #79, #18):** Q types S. `cores/pet/rtl/trx/pet_keys.v` (line 4: matrix taken from VICE `gtk3_buuk_pos.vkm`) is selected in `pet_core.v:280` by `.bus(m_crtc)`, i.e. for every CRTC model, the 4032 included. Which models really have the business keyboard is not settled by the code (a 4032 exists with the graphics keyboard, the 8032 is business): open question Q1.
- **P2 "SuperPET CPU" setting offered on every model:** `coreapp/cores/pet.cc:75` shows the row everywhere (a 4032 shows 6809); only the SuperPET uses it.
- **P3 The 4040 needs hand-made ROM files:** `901466-04.bin` doubled into `ctl4040.bin`, plus `dos4040.bin`; no guide.
- **P4 Wish (GitHub #80):** a game mode mapping the direction keys to the number keys (games use the digits).
- **P5 Wish:** 60 Hz editor ROMs and the character ROM as in VICE.

Reported on GitHub after the first test release (2026-11-13/14), bundled here 2026-11-15. Code state read from main; "inference" = not proven by a measurement.

- **P6 SuperPET in 6809 mode shows only PETSCII (GitHub #78, also in #18, #79 comments); 96 KB RAM question.** Reports: with the CPU set to 6809 the screen draws only PETSCII, "the 40-column graphics set, not the 80-column business set"; with the 6502 it is fine; the RAM row never offers more than 32 KB in any mode (a tester suggested a 96 KB option; VICE's SuperPET preset also sits at 32 KB).
  - Code: the 6809 side is `pet_core.v:194-202` (`mc6809is`, `act809` from `pet_spet.v`, `cpu_set` taken at reset); the 6809 runs the Waterloo ROMs, which the app writes only if the files exist (`pet.cc:186-192` marks all six optional `false`, "the 6502 runs without them"), the 4 KiB bank RAM behind `$9000` is `pet_ram64` (`pet_core.v:213-222`). The picture takes `gfx` from the VIA (`pet_vid.v:87` `c_a <= {gfx, code, scan}`), i.e. whichever character set the running program selects.
  - Cause (inference): either the Waterloo ROMs are not in `pet/roms` (an empty or partly empty `$A000-$FFFF` leaves the 6809 in the PET editor, which prints PETSCII) or the 6809 program does not switch the character set. Not verified; the tester's ROM folder is unknown. Whether any Waterloo bench exists is not checked.
  - RAM: `pet.cc:72` has three entries (8/16/32 KiB) and says "the CRTC models always have 32 KiB"; `pet_core.v:229` `ram_ok = m_crtc | ...` ignores the row for 8032/8096/SuperPET (verified). The SuperPET's 64 KiB bank RAM exists but is not a menu value (verified); "96 KB" is 32 KiB + that 64 KiB.
- **P7 80-column mode reverts to 40 columns after the four beeps on 8032, 8096 and SuperPET (GitHub #82; the same finding in #18).** Reported: the READY screen is first 80 columns, then 4 beeps, then 40 columns, permanently, after F7 reload ROMs and reset.
  - Code: the 80-column state is only what the program programs into CRTC R1: `pet_core.v:374` `col80 = m_crtc && (r1 >= 60)`, the picture follows `r1` (`pet_core.v:333`); the core has no switch of its own. So the editor ROM that runs writes R1 = 40 (verified by reading: nothing else changes R1).
  - Cause (measured 2026-11-15, §5): not a wrong editor file. The real editors program R1 = 40 (two columns per CRTC character on the 80 column boards); the core took `r1 >= 60` for 80 columns, so the reset value showed 80 columns until the editor's CRTC table was written after the four beeps (about 580 ms). Fixed `col80 = r1 >= 30`; real 8032 editor bench PASS.
- **P8 PET 2001 with BASIC 1 and disks (GitHub #87).** Reported: the original BASIC 1 cannot use the 4040/IEEE bus (a Commodore bug, the core reproduces it); the fix on real machines is BASIC 2. A BASIC 2 ROM under the BASIC 1 file name gives a black screen.
  - Code: for model 2001 the BASIC role is `basic-1.901439-09-05-02-06.bin` or `basic1.bin`, `basic.bin` (`pet_models.h:21-24`); the editor `edit-1-n.901439-03.bin` and KERNAL 1 are fixed per model (`pet_models.h:31-45`); BASIC 1 uses other zero-page pointers (`pet_models.h:57-69`). A BASIC 2 image is the same 8 KiB, so the core accepts it.
  - Cause of the black screen (inference): BASIC 2 expects the KERNAL 2 / editor 2 entry points and zero page; with KERNAL 1 and editor 1 around it the start fails. Not measured.
- **P9 Real datasette: SAVE writes no data on the PET (GitHub #86, PET part; the issue itself is the C16).** Reported (2026-11-14): PET and C16 read tapes (written by the C64U), but do not write; C64, C128, VIC-20 read and write.
  - Code: the PET's datasette setting drives the motor and the READ/SENSE pins only; `pet.cc:59` `dset_bytes = {0x00, 0x11}` and the comment "b1 WRITE out stays 0 until a SAVE on real tape is proven (T214)" (verified: the write output is off by design). The core's write line is `cass_write` (`pet_core.v:252,277,394`). The PET LOAD from real tape is proven (GitHub #26). The T214 spec covers the other cores; the PET is not in it.
  - Open: whether the PET's write line is wired to the pin and with which polarity is not read yet; no hardware measurement here (no datasette on the project's side; a tester has one).
- **P10 Graphics keyboard (2001, 3032, 4032): number keys give the shifted symbols without C= (GitHub #79 comment, "4016 mode").** Reported: with the 4016 model the digits need the C= key, unshifted they print their shifted special characters; some special characters of the C64 keyboard are missing or misplaced.
  - Code: the graphics matrix is `pet_keys.v` (selected for `~m_crtc`); S2 only proves letters, digits, RETURN and SPACE against VICE's `gtk3_grus_pos.vkm` (§5), so the digits are claimed equal to VICE's positional map. How the host's C64 keys are turned into PET matrix bits (the shift handling of the host keyboard) is not read yet.
  - Cause: unknown. A positional map gives the PET shifted symbol for an unshifted C64 digit key on the C64 keyboard (the PET 2001 legend differs from the C64's): inference. A "4016" is not a model of this core (the graphics models are 2001, 3032, 4032; the tester likely means the 4032 with 16 KB RAM, or a 4032 with the RAM row on 16 KiB).

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | On 8032, 8096 and SuperPET (and any model Q1 adds) every key types its PET key (Q types Q) | bench `tb_pet_models` with the business matrix; board: type the alphabet | `ci/sims.txt`; board session |
| S2 | P1 | Graphics-keyboard models keep their matrix unchanged | `tb_pet_models` (graphics models) | `ci/sims.txt` |
| S3 | P2 | The "SuperPET CPU" row appears only on the SuperPET | golden `menu_pet.txt` per model | app tests |
| S4 | P3 | following the INSTALL.md §5 guide (D3), the joined 4040 ROMs mount a disk | board: build the files by the guide, mount a d80 | board session |
| S5 | P3 | `INSTALL.md` §5 lists the PET ROM files, names and how to get them | read in review | review |
| S6 | P5 | Per Q4 (scenarios are added when decided) | open | open |
| S7 | P6 | Given SuperPET, 6809, all six Waterloo files in `pet/roms`, When reset, Then the 6809 menu/prompt is on screen in the set the ROM selects (not the PET editor screen); with a file missing the app says which | board session; a bench with the Waterloo ROMs only if Alex allows them in `sim/media` | board session, bench by decision |
| S8 | P6 | Per the answer to Q1, the RAM row offers the SuperPET's 96 KiB (or documents why not) and the machine reports it | board: SuperPET memory test in 6809 mode | board session |
| S9 | P7 | Given 8032 with `edit-4-80-b-60Hz.901474-03.bin`, When reset, Then the screen stays 80 columns after the beeps (CRTC R1 stays 80, `status` bit `col80` stays 1); same for 8096 and SuperPET | `tb_pet_models` extended to run past the beeps and read R1; board: type text and see 80 columns | `ci/sims.txt`; board session |
| S10 | P7 | Given the 80-column model and only a 40-column editor file, When reset, Then the app refuses or tells the user which editor is missing, no silent 40 columns | app test of the ROM scan for the 8032 | app tests |
| S11 | P8 | Given 2001, BASIC 1 + its editor and KERNAL, When a disk is mounted, Then the behaviour is exactly the original's (the bug stays reproduced) and the guide says so; given Q3's answer, a BASIC 2 choice for the 2001 mounts and lists a disk | board: mount a d64 on the 2001 and `LOAD"$",8` | board session |
| S12 | P8 | Given the 2001 with a mismatched BASIC file, When reset, Then the app tells which file does not belong (no black screen without a message) | app test of the ROM check | app tests |
| S13 | P9 | Given an external datasette with a tape, When `SAVE"X"` runs on the PET, Then a `.tap`/real tape written by it loads back (also on the C64U's tape reader); with the write line off nothing is written | board: save on a real datasette, load it on the PET and the C64; bench `tb_pet_tape` for the write-line level and polarity if it exists | board session; negative control only here (tape write is a persistence operation) |
| S14 | P10 | Given 4032 (graphics keyboard), When the host keys 0-9 and the C64 special characters are typed, Then each key gives the PET character at the position Q4's answer decides (no C= needed for digits) | `tb_pet_models` mode 0 (digits) plus the host keyboard map test; board: type the digits | `ci/sims.txt`, app tests, board session |
| S15 | P4 | Per Q5 (direction keys, joystick, USB pad on the digits), the PET game mode | per the answer | open |

## §3 Decision

- **D1 (Alex, 2026-11-12):** P1 and P2 are fixed ("spec, plan it in, let's get the PET round, more or less", translated).
- **D2 (Alex, 2026-11-12, "ja" to the proposal; answers Q1):** graphics keyboard on 2001, 3032 and 4032; business keyboard on 8032,
  8096 and SuperPET. The keyboard follows the model, not the CRTC (`pet_core.v:280` `.bus(m_crtc)` changes).
- **D3 (Alex, 2026-11-12, "answer 2" = documentation only; answers Q2):** the app does not join or double the 4040 ROMs; INSTALL.md
  §5 gets a guide: `901466-04.bin` (1 KiB: doubled it gives the 2,048 bytes `coreapp/cores/pet.cc` asks for, as the tester built it) written twice into `ctl4040.bin`, plus `dos4040.bin` (S4 becomes the doc check, S5).
- **D4 (Alex, 2026-11-12, "b"; answers Q3):** P4 (game mode, direction keys on the digits) is not built in the PET; it comes with the
  key bundles of the generic app (decision 6 of `docs/generic-app-requirements.md`), built with the UI epic, last. Out of T216 (§4).
- **D5 (Alex, 2026-11-12, "passt"; answers Q4):** P5 needs no code: the app already picks the editor by model and picture rate and the
  character ROM by the VICE names (`coreapp/cores/pet_models.h:26-48`). INSTALL.md §5 lists which VICE PET files go into `pet/roms`.


### Questions for Alex (2026-11-15), all seven decided the same day

1. **P6 RAM:** offer a "96 KiB" value for the SuperPET (32 KiB + 64 KiB bank RAM), or keep 32 KiB as VICE does and document it? **Decided 2026-11-15 (Alex): offer 96 KiB** for the SuperPET (32 KiB + 64 KiB bank RAM); RAM/BRAM cost to be checked when built.
2. **P6/P7 ROM handling:** when a model's own ROM file is missing and a generic or other-model file is taken, should the app refuse with a message naming the file (options: refuse; warn and take it; as now)? **Decided 2026-11-15 (Alex): refuse and name the missing file** (the model does not start on a substitute ROM).
3. **P8:** the 2001 keeps the original BASIC 1 disk bug (documented), or the app offers "BASIC 2 on the 2001" (which needs KERNAL 2 and editor 2 with it, so it is a different model setting), or a warning when a drive is on with BASIC 1? **Decided 2026-11-15 (Alex): a model setting "2001 with BASIC 2"** (BASIC 2 + KERNAL 2 + editor 2 together); BASIC 1 keeps its original behaviour.
4. **P10:** are the PET legends the target (a C64 key gives the PET key at its position, as VICE's positional map) or the C64 legends (an unshifted C64 digit gives the digit; options: positional, symbolic, both as a setting)? **Decided 2026-11-15 (Alex): both, as a menu setting** (symbolic: the key legend counts; positional: the PET key at the same place).
5. **P4 (game mode, #80):** D4 puts it into the generic key bundles; does the reported USB gamepad (the PET has no joystick port) belong to the same bundle decision, and does the PET wait for it? **Decided 2026-11-15 (Alex): generic, PET first** (pad/joystick -> keys as a generic core-app service; the PET is its first user and drives the build).
6. **P9 (tape SAVE):** enable the write pin (`dset_bytes` b1) in a test core now and let a tester with a datasette report, as T214 does for the C16, or wait for T214's result first? **Decided 2026-11-15 (Alex): wait for T214 first** (the C16 SAVE is being measured); the PET write pin follows its result.
7. **P6 bench:** may the Waterloo ROMs be checked in under `cores/pet/sim/media/` (as the Atari OS ROM was) so a bench can prove the 6809 screen? **Decided 2026-11-15 (Alex): yes, under `cores/pet/sim/media/`** (Gitea only; `tools/github_snapshot.py` drops sim/media, never in a .cor, package or release; CLAUDE.md third exception).

### UX decisions on the PET menu (Alex, 2026-11-15, after the ue2emu review)

- **D6 (Alex: "Ja"):** "Picture rate" and "Phosphor" move from the PET tab to the Video tab (shorter F5 list): group `video` on both rows, `coreapp/cores/pet.cc` settings table.
- **D7 (Alex: "Key 8"):** the pad rows show the chosen key as "Key 8", "Key Space" (all keys, "None" stays): `coreapp/src/padkeys.h` `padkey_names` (generic service; the stored value text changes with it).
- **D8 (Alex: "Ja bitte alles ins Input tab"):** every pad/keyboard row is in the Input tab, including "Map buttons, pad 1/2" (was F7): the tasks carry group `input`; generic code: `coreapp/src/menutree.cc` lists a task in a settings tab that names its group (below the rows) and no longer in the tasks tab (`task_in_settings_tab`).
- **D9 (Alex: "Ja"):** the missing-ROM message lists every missing file of the model at once, one per line (`list_missing_roms`, `coreapp/cores/pet.cc`), not only the first.
- **D10 (Alex 2026-11-15, open UX Q1 "b", after q1a-c):** the foot shows one line: while a row note is shown (e.g. "Model: ... takes effect at the next reset"), the ring legend is left out (generic, `MenuCtl::foot_line`, every core).

## §4 Out

P4 (game mode): moved to the generic app's key bundles (D4). P5: documentation only (D5).

## §5 Decisions taken while building

- **Keyboard select already follows the model (2026-11-12).** `m_crtc` is `model_l >= 3`, i.e. 8032, 8096, SuperPET; the 4032 is model 2 and already reads the
  graphics matrix. So §1's "the 4032 included" does not hold in the code and D2 needs no RTL change; `.bus(m_crtc)` stays.
- **The business matrix is right (S1).** Checked against VICE `data/PET/gtk3_buuk_pos.vkm` (fetched from the VICE svn mirror on GitHub,
  `VICE-Team/svn-mirror`, `vice/data/PET/`) and, independently, against the key-decode table inside the VICE editor ROMs `edit-4-80-b-60Hz.901474-03.bin` and
  `-50Hz.901474-04.bin` (rows 0-9 at $06F5 descending, 8 bytes per row, stored right to left): Q is row 5 column 0 in all three, S is row 2 column 1, so the
  matrix cannot make Q type S. In the graphics matrix S is row 5 column 0, the position of the business Q: "Q types S" is what a business-ROM machine
  reads through the graphics matrix. The cause on the board is therefore not found by reading; open for the board session (S1 board line).
- **S1/S2 bench:** `sim/gen_key_expect.py` writes `sim/keys_expect.vh` from copies of VICE's `gtk3_buuk_pos.vkm` / `gtk3_grus_pos.vkm` (`sim/vice/`); `tb_pet_models`
  mode 0 presses every business key (letters, digits, keypad digits via C=, punctuation, cursor, ESC/TAB/REPEAT/RVS-off stand-ins, both shifts) and the graphics
  letters/digits/RETURN/SPACE and requires exactly one bit at VICE's row and column, nothing on the other rows.
- **P2 (S3):** the row uses the existing `Setting::show_state` (the REU rows of the TRX64U use it); the PET registers the state `pet.superpet`, which has a
  value only while the Model setting is SuperPET, so the row appears as soon as the model is chosen, not after the reset. The menu dump (`menutree.cc`) now
  prints `[listed while <state>]` for such rows, so the golden `menu_pet.txt` records the condition; `menu_c64.txt` changed with it (its REU and static-IP rows).
- **P3/P5 (S5):** INSTALL.md is generated; the PET text is `tools/release_notes_mc.py` (section 5). It lists the VICE files per model and picture rate from
  `pet_models.h` and the 4040 guide (`cat` / `copy /b`). The size question is settled (cc1b7c55f, 2026-11-13): `901466-04.bin` is 1 KiB, written twice it gives the 2,048 bytes `pet.cc` (`op_put_ctl4040`) accepts; the guide says the result must be 2,048 bytes. Board check of the guide stays S4.
- **2026-11-15 bundle (Alex, one spec for all PET bugs and enhancements).** GitHub #78, #79 (comments), #80, #82, #87 and the PET part of #86 read with all comments and checked against `cores/pet/` and `coreapp/cores/pet*.{cc,h}`: P6-P10 and S7-S15 added, causes marked verified or inference in §1, design questions in the list under §3. Nothing decided, no code changed. Status of the issues: all open.

### 2026-11-15 build (agent, branch t216-pet): causes found by reading VICE's source and running the real ROMs
- **#82 (P7) is a core defect, not a wrong editor file.** The real 8032 editor ROMs (`edit-4-80-b-60Hz.901474-03.bin` at `$E72A`, also the 40 column `edit-4-40-b-60Hz.ts.bin`) program CRTC R1 = 40:
  on the 80 column boards a CRTC character is two columns (VICE `hwflag` DOUBLE_CHARS, `hw_cols` 2). The core took `r1 >= 60` for 80 columns, so the reset value (80) showed 80 columns until the editor wrote 40.
  Fixed: `col80 = m_crtc && r1 >= 30` (`pet_core.v`, `pet_vid.v`), reset value R1 = 40.
- **#78 (P6) cause: the character set.** The Waterloo ROM programs CRTC R12 = `$30`; VICE (`petrom.c`: "Selecting between the 2 is done with CRTC register $0C: $30 for the Waterloo fonts, $10 for the Commodore fonts", `crtc.c`: MA13 = `vaddr_charswitch`) shows
  R12 bit 5 picks the upper half of the 4 KiB character ROM `characters.901640-01.bin`. The core had a 2 KiB character ROM, ignored R12 bit 5, and the app wrote only the first 2 KiB. Fixed: `pet_ram4k` as character ROM (+1 BRAM18), `alt = r12[5]` into the character address (`pet_vid.v`), the app writes 4 KiB (a 2 KiB file leaves the upper half zero; the SuperPET refuses a 2 KiB file).
- **96 KiB (P6, D-Q1):** the 64 KiB bank RAM already was in the core (`pet_ram64`, shared with the 8096's extra RAM; the models are exclusive), so 96 KiB costs no BRAM or LUT beyond a flag: `SuperPET RAM` row (32 KiB = ROM behind `$9000`, 96 KiB = bank RAM; default 96), register `PET_SP_RAM`, taken at reset. The old `RAM` row is listed for the 2001, 3032, 4032 only.
- **ROM refusal (P6/P7, D-Q2):** `pet_rom_names` lists only the model's own VICE names (no `basic.bin`, `edit.bin`, `kernal.bin`, `chargen.bin`, no other model's file); `put_model_rom` fails with "<role> of the <model> is missing: <file>", the boot loop then holds the machine and shows it (boot.cc). Generic names no longer work (manual 11.19.1 and INSTALL text changed).
- **P8 (D-Q3):** model value 6 "2001 + BASIC 2" (appended, stored settings keep their index): BASIC 2, KERNAL 2, editor 2 (3032's files), characters-1; `PET_MODEL` gets 0 (`model_bytes`), BASIC 2 zero page for the PRG loader. BASIC 1 on the plain 2001 is untouched.
- **P10 (D-Q4):** `pet_keys` has two mappings, `sym` from the app row `Keyboard mapping` (default Symbolic). Symbolic is built per character: graphics board - C64 digits type the keypad digits, shift + digit the `! " # $ % & ' ( )` keys unshifted, `+ - * = /` `.` the keypad keys, `?` `<` `>` `[` `]` on the C64's shifted `/` `,` `.` `:` `;`; business board - `+ * =` are shifted PET keys, shifted `:` `;` give `[` `]`. The test expects by the character found on the PET's own legends (gen_key_expect.py), not by the generator's table.
- **P4 (D-Q5):** generic service `coreapp/src/padkeys.h` (key list, register binding and setting-row macros; any core with `c64_keys` can use it) + the core's block `pet_padkeys.v` (copy per core): five buttons (up, down, left, right, fire) of both USB pads (T156 pad service, `PadSpec`) and both joystick ports each press one C64 key before the keyboard translation; rows `Pad / joystick up..fire`, defaults 8 2 4 6 and SPACE; `Pad map` and `Map buttons` as in NES/SNES.
- **Waterloo ROMs (D-Q7):** copied from VICE `data/PET` into `cores/pet/sim/media/` (Gitea only; `tools/github_snapshot.py` DROP_RE `cores/[^/]+/sim/media/.*` drops them, never in a .cor).
- **Not done: P9 (tape write), waits for T214 (D-Q6).**
- **UX review (ue2emu, agent, 2026-11-15): FAIL on R3/R6/R2, partly fixed.** Fixed: labels "SuperPET CPU" / "SuperPET RAM", SuperPET RAM values "32 KiB" / "96 KiB", Keyboard mapping + pad rows + Pad map moved to the Input tab (`group "input"`), the missing-ROM message names the folder. Open (proposals to Alex): Picture rate / Phosphor to the Video tab; pad values "8" vs "Key 8"; F7 "Map buttons" beside "Pad map"; the missing-ROM screen names only the first missing file; the foot note of a changed row is clipped at 480p/720p (generic app code, ring legend); the emulator cannot show the conditional rows (pet.ramrow / pet.superpet).
- **Real-ROM finding, closed (agent, 2026-11-15): no core defect, the 350 ms run was too short.** The 8032 editor's reset code (`LE60F`, called from `LE04B`) ends with the bell: `LDA #$10 / STA $E7` (note length), `JSR $E6A4`; `$E6A4` is `JSR $E6A7` falling into `$E6A7`, and the `BEQ $E6A4` after the first call is taken (Z from the last `DEX`), so the routine runs twice per call = four beeps of seven notes each. The note timing is pure software (`DEY` loop at `$E6BF`, `A = $E7` outer count, T2 at `$E848` only sets the pitch of the shift register on CB2; no flag or counter of the VIA is waited for): 4 x 7 x 16 x 256 x 5 cycles, about 570 ms at 1 MHz. Only after that does `LE04B` call `LE07A`, which writes the CRTC table `$E72A` (R1 = `$28`, R12 = `$10`). The bench stopped at 350 ms, inside the second beep; the 6502, the VIA and the shift register behave as the editor expects. The tester's sequence "80 columns, four beeps, then 40 columns" is the old `col80 = r1 >= 60` (reset R1 = 80 shows 80 columns until `LE07A` writes R1 = 40): fixed with the R1 part above. `tb_pet_real` now prints every 50 ms, checks READY on the screen, R12 = `$10` for CRTC boards, and has `+fastbeep` (the editors' note length `$10` patched to 1 in the loaded ROM copy: 120 instead of 2304 CB2 edges).

### 2026-11-15 scenario evidence (builder `tools/benches.sh pet -f`: pet_boot 360 s, pet_spet 10 s, pet_keys, pet_tape PASS; pet_wl 440 s PASS)
| # | Evidence |
|---|---|
| S1, S2 | `pet_keys` PASS: 312 key checks, positional against VICE's two .vkm, business and graphics; `tb_pet_models` mode 0 PASS. Board (Q types Q): open |
| S3 | golden `menu_pet.txt` updated by hand (rows by model: `pet.ramrow`, `pet.superpet`); app test not run (pipeline) |
| S4, S5 | unchanged: board, open / INSTALL text updated |
| S6 | not a scenario (P5 documentation only) |
| S7 | `pet_wl` PASS: the six Waterloo files run the 6809 to "Waterloo microSystems / Select :" menu on row 0 in 80 columns with R12 = `$30`; missing-file message from the app: `put_waterloo`, unchanged; board, open |
| S8 | `pet_spet` modes 2 (sixteen banks keep first and last byte) and 3 (32 KiB: ROM behind `$9000`) PASS; board memory test in 6809 mode: open |
| S9 | `tb_pet_models` mode 0 PASS; `tb_pet_real +model=3 +ms=2300` (VICE ROMs `data/PET`, real timing, builder, not in CI: needs the ROMs): beeps end at about 580 ms (CB2 edges 2304), R12 = `$10`, R1 = 40, 80 columns, `*** COMMODORE BASIC 4.0 ***  31743 BYTES FREE  READY.` on the screen, PASS. 8096 and SuperPET: not run with the real editor (same ROM, same CRTC path): board, open |
| S10 | `pet_models.h` lists only own names, `test_pet_models.cc` extended; message text rendered in ue2emu (4032 BASIC variant); app test not run |
| S11 | model "2001 + BASIC 2" built (app test `test_pet_models.cc`); `tb_pet_real +model=0 +ms=2300 +fastbeep +basic=basic-2.901465-01-02.bin +edit=edit-2-n.901447-24.bin +kernal=kernal-2.901465-03.bin +chars=characters-1.901447-08.bin`: `### COMMODORE BASIC ###  31743 BYTES FREE  READY.` PASS; the same bench for the 4032 (`+model=2`, `edit-4-40-n-60Hz.901499-01.bin`): `*** COMMODORE BASIC 4.0 ***` READY PASS; board, open |
| S12 | app refuses a missing own file by name (S10 path); a wrong-content file is not detected (size only): open |
| S13 | P9, waits for T214: not touched |
| S14 | `pet_keys` PASS: symbolic mapping checked by character against the PET legends, both boards |
| S15 | `pet_keys` PASS: pad / joystick keys (5 buttons, 2 pads, 2 joystick ports, SHIFT code); app side built (riscv build ok, `padkeys.h`); USB pad on the board: open |
LUT/BRAM: +1 BRAM18 (character ROM 4 KiB); no BRAM for 96 KiB (`pet_ram64` already existed); `pet_padkeys` about 350 LUT (64-bit decode of five key codes), `pet_keys` symbolic matrices about 150 LUT, registers ~60 FF: estimates, not synthesised.

### 2026-11-15 UX decisions applied (agent, branch t216-pet): D6-D9, riscv builds, ue2emu pictures, ux-specialist FAIL on one generic defect
- Built: D6-D9 in §3; the earlier labels "SuperPET CPU"/"SuperPET RAM" and the Keyboard mapping row in Input are in. Extra wording: "Picture rate" is labelled "Refresh rate" (beside the generic "Picture" row); the missing-ROM message ends with the action. `tools/dev.sh riscv` ok. `tests/ui/fixtures.py` gives the PET stick the VICE-named files (`PET_ROMS_DIR`, default the local VICE `data/PET`, plus `cores/pet/sim/media`, never committed elsewhere), `UI_PET_DROP` leaves files out; `tests/ui/steps.py` got `I wait N ms`; features `ux_t216_pet.feature`, `ux_t216_pet_missing.feature`.
- Pictures (480p/720p/1080p, not committed): scratchpad `pet-shots/<res>/ux-pet-*.png`.
- ux-specialist: FAIL. The two-line foot ("Model: X (takes effect at the next reset)   A ring beside a name: ...") is clipped at 480p, 720p and 1080p (`MenuCtl::foot_line`, generic; the foot box holds one line): not fixed, needs a decision (drop the ring legend while a row note shows, or a second foot line).
- Open for Alex: (1) the foot above; (2) "Map buttons, pad 1/2" on Input have no value and no ">" like Hotkeys/Picture: add the chevron?; (3) Input order puts the generic Hotkeys row between Pad map and Map buttons: Hotkeys last?; (4) the emulator shows RAM/SuperPET rows for every model (conditional rows are board-only); (5) "Diag" wording; (6) a long missing list (SuperPET + Waterloo) not rendered.

### 2026-11-15 night: merge with main, scenario check, open questions (agent, branch t216-pet)
- `git merge main` (a7998ed75): one conflict, `coreapp/src/menutree.cc`: main had built the same generic rule (T226 S2, `task_in_settings_tab` with a settings tab and a tasks-tab check); main's version kept, this branch's copy dropped. `tools/dev.sh riscv` passes on the merged tree (host app + core app). Goldens not regenerated: no change of a menu dump came from the merge (the PET golden was edited by hand on 11-15, see S3; no generator run here).
- Nothing decided in §3 is left unbuilt except P9. Checked in the code: P4 end to end in the menu = Input tab rows `Pad / joystick up..fire` (value "Key 8"), `Pad map`, `Map buttons, pad 1/2` (group `input`, rendered below the rows by main's rule), 5 registers `PET_PADKEY0..4`, `pet_pad` words via `pad_pace`; pictures `review/*/ux-pet-f5-input*.png`.
- S12 stays as built (a missing own file is refused by name). A WRONG file (right size, other content) is not detected; not decided, no CRC check invented: open question for Alex.
- ue2emu pictures (480p/720p/1080p, real core app) of every changed PET screen: PET tab top/end, SuperPET, 2001 + BASIC 2, Video, Input (4 positions, positional), F7, F1, missing-ROM message. ux-review: PASS for placement and wording (Refresh rate / Phosphor on Video, pad rows on Input, "Key 8"); R2 FAIL stays for the clipped two-part foot note until Q1 is answered; the emulator still lists the RAM / SuperPET rows for every model (conditional rows are board-only).
- Question pictures, one per option, `questions.md` with the German sentences (scratchpad t216/q): Q1 foot note, Q2 chevron on Map buttons, Q3 Hotkeys position, Q4 "Diag" wording, Q5 long missing list (mock-ups are scratch commits, dropped). Found while making Q5: a message is at most `OP_MSG_MAX` 240 bytes; the four-file list of every model fits (largest about 195), ten files (SuperPET + Waterloo) would be cut.
- Gateware: `cores/pet/rtl` changed on this branch (R1/col80, 4 KiB character ROM with R12 bit 5, `pet_padkeys.v`, symbolic `pet_keys`, SuperPET RAM flag) and never went through Vivado: a Vivado run of `pet` is needed. `cores/common`, `host`, `contract` unchanged by this work.

| # | State after the night |
|---|---|
| S1 | bench PASS (`pet_keys`, 312 checks); board (type the alphabet on 8032): open |
| S2 | bench PASS |
| S3 | golden by hand; app test: pipeline |
| S4 | board: build ctl4040/dos4040 by the guide, mount a d80, open |
| S5 | built (INSTALL text) |
| S6 | not a scenario (D5) |
| S7 | `pet_wl` PASS; board open |
| S8 | `pet_spet` PASS; board memory test 6809: open |
| S9 | `tb_pet_real` 8032 PASS (80 columns after the beeps); 8096 / SuperPET with real editor: board, open |
| S10 | built, listing of all missing files rendered (`ux-pet-missing`); app test: pipeline |
| S11 | `tb_pet_real` 2001 + BASIC 2 READY PASS; disk on the 2001: board, open |
| S12 | missing file: built; wrong content: open question (not decided) |
| S13 | not touched, waits for T214 (D-Q6) |
| S14 | `pet_keys` PASS, symbolic and positional; board: type the digits, open |
| S15 | `pet_keys` PASS, menu built end to end, riscv ok; USB pad on the board: open |
