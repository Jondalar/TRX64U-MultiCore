# T216 — The PET core works on every model with the right keyboard and menu, and its drive ROMs are documented

**Status:** PROPOSED (2026-11-12).
**Repo:** TRX64U-MultiCore — `cores/pet/` (keys, model select), `coreapp/cores/pet.cc` (menu), `INSTALL.md` (docs).
**Number:** T216 (registry: the table in `TODO.md`).
**Origin:** Alex, 2026-11-12, after a tester's report (2026-11-12): "spec, plan it in, let's get the PET round, more or less" (translated). Reopens T134 (closed "as is" 11-11).
**Depends on:** T134 (PET core), decision 6 in `docs/generic-app-requirements.md` (key bundles) for Q3.
**Milestone:** M2.

---

## §1 The problem

A tester (2026-11-12) ran main-736c16d9 on the board. Working, to keep: internal and external datasette, disk through the 4040 once the ROMs are set up. Findings:

- **P1 Business keyboard wrong on 8032 and above:** Q types S. `cores/pet/rtl/trx/pet_keys.v` (line 4: matrix taken from VICE `gtk3_buuk_pos.vkm`) is selected in `pet_core.v:280` by `.bus(m_crtc)`, i.e. for every CRTC model, the 4032 included. Which models really have the business keyboard is not settled by the code (a 4032 exists with the graphics keyboard, the 8032 is business): open question Q1.
- **P2 "SuperPET CPU" setting offered on every model:** `coreapp/cores/pet.cc:75` shows the row everywhere (a 4032 shows 6809); only the SuperPET uses it.
- **P3 The 4040 needs hand-made ROM files:** `901466-04.bin` doubled into `ctl4040.bin`, plus `dos4040.bin`; no guide.
- **P4 Wish:** a game mode mapping the direction keys to the number keys (games use the digits).
- **P5 Wish:** 60 Hz editor ROMs and the character ROM as in VICE.

## §2 Scenarios

| # | Solves | Scenario | Test | Where it runs |
|---|---|---|---|---|
| S1 | P1 | On 8032, 8096 and SuperPET (and any model Q1 adds) every key types its PET key (Q types Q) | bench `tb_pet_models` with the business matrix; board: type the alphabet | `ci/sims.txt`; board session |
| S2 | P1 | Graphics-keyboard models keep their matrix unchanged | `tb_pet_models` (graphics models) | `ci/sims.txt` |
| S3 | P2 | The "SuperPET CPU" row appears only on the SuperPET | golden `menu_pet.txt` per model | app tests |
| S4 | P3 | following the INSTALL.md §5 guide (D3), the joined 4040 ROMs mount a disk | board: build the files by the guide, mount a d80 | board session |
| S5 | P3 | `INSTALL.md` §5 lists the PET ROM files, names and how to get them | read in review | review |
| S6 | P4, P5 | Per Q3 and Q4 (scenarios are added when decided) | open | open |

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
  `pet_models.h` and the 4040 guide (`cat` / `copy /b`). **Open:** D3 says `901466-04.bin` is 2 KiB, written twice; the core takes `ctl4040.bin` only at exactly
  2,048 bytes (`pet.cc` `op_put_ctl4040`, `rom_scan` matches sizes exactly), so a 2 KiB file doubled (4 KiB) would be refused. The guide says the result must be 2,048 bytes;
  Alex or the tester to confirm the size of the source file (S4 board).
