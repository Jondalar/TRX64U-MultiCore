# T137 — The host app turns a firmware file into a .cor and puts the original firmware back

**Status:** BUILT and merged (convert via t137-convert, restore via t137 step 2, both through w1-t152); BUILT, board partly proven (2026-11-15 sync; the old header said PROPOSED and was garbled by a status-check insertion). Asked by Alex 2026-10-28; Q1 answered. Convert (P1, S1, D1, D2 reader, D3 table, D9) built 2026-10-29; restore (P2, P3, S2, S3, D4-D8) built 2026-10-30, see §5; the MultiCore update (P4, S4) is T152 and not built here. Board: S3 restore passed (Alex 2026-10-31, host app main-76817bbe, commodore-110.cor, §5); D11 look of the update pages passed 2026-11-02. Open: the D10 restart as a board step, S1 with Gideon's 3.15a on the board, S4 (T152).
**Repo:** TRX64U-MultiCore — host application (`host-app/software/hostapp/`), the large `.cor` manifest
(`tools/pack_large_cor.sh`, `cor.h`), the installer's backup (T135 D8, `apps/ultimate` updater: built there, not changed
by this spec without Alex).
**Number:** T137.
**Origin:** Alex, 2026-10-28 (translated): "As an open todo for the host app: in perspective we have to allow updates
and the restore of the original firmware from there too." Later the same day, at the board: ".ue2/.cfw should then have
options for: convert to .cor, and CAREFUL - depending on what kind of .cfw it is also 'restore your FW backup' -> so a
marker has to go into our 'restore package' that we restore with." On the proposal (foreign marker in a converted
`.cor`, recognition by our backup's marker and by a table of known official hashes): "passt alles" ("all fine").
**Depends on:** T115 (large `.cor`, `.cfw`/`.ue2` start), T135 D8 (the installer packs the backup as a large `.cor`),
T139 (file browser actions).
**Milestone:** M3.

---

## §1 The problem

- **P1 Convert.** A `.cfw`/`.ue2` in the browser can be started (T115) but has no icon, no metadata and no SHA-256 of
  its parts, so it never gets the trusted fast start of T115 D10. Turning it into a large `.cor` gives it all three.
  T115 D9 says a `.cor` is ours: a converted one must say that it is not.
- **P2 Recognise.** Only some files may be offered for "restore": the backup the installer made of this board's
  original firmware (T135 D8: `commodore-110.cor`, `gideon-315a.cor`, ...), and official firmware releases. Any other
  `.cfw` (a test build, someone's image) must never be written to the flash as the machine's firmware.
- **P3 Restore.** Writing the original firmware back (flash 0x000000 bitstream, 0x220000 application) replaces the
  MultiCore host. It must not leave a board that boots nothing: the whole package checked before the first erase,
  two confirmations (as T135 D9), and a way back to MultiCore afterwards (the MultiCore update file, as today).
- **P4 Update.** The MultiCore firmware itself updated from the host (a MultiCore update package recognised by the same
  marker), instead of starting the update file from another firmware's menu.

## §2 Scenarios

```gherkin
Scenario: S1 convert a .cfw
  Given gideon-3.15a.cfw selected in the browser
  When  "convert to .cor" is chosen
  Then  gideon-3.15a.cor appears in <medium>/cores/ (T148 D1); its manifest carries origin "foreign", name and version from the .cfw,
        SHA-256 of bitstream and application; the host starts it on the trusted path (T115 D10)
  And   the menu and the browser mark it as foreign (badge), not as one of ours

Scenario: S2 restore is offered only for a recognised package
  Given the installer's backup commodore-110.cor, an official Gideon .ue2 whose SHA-256 is in the table, and a test .cfw
  When  each is selected in the browser
  Then  the first two offer "restore as firmware", the test .cfw does not (bench: the recognition function on the three)

Scenario: S3 restore
  Given the backup .cor selected and "restore" confirmed twice
  When  the host writes it
  Then  every part is checked against its SHA-256 before the first erase; after the write the flash is read back and
        compared; after Alex's power cycle the original firmware runs
  And   a package whose check fails: nothing erased (control: one byte changed in a copy)

Scenario: S4 update MultiCore
  Given a MultiCore update package on the card
  When  "update" is chosen and confirmed
  Then  host bitstream and application are replaced, the cores and boot.cfg stay; after the power cycle the new host runs
```

| # | Solves | Test | Where |
|---|---|---|---|
| S1 | P1 | `tests/hostapp/test_fwconvert.cc` + render bench `scenarios_t137` (synthetic .cfw/.ue2), board step with Gideon's 3.15a | CI, board |
| S2 | P2 | `tests/hostapp` recognition test | CI |
| S3 | P3 | board session with a backup .cor; control on the bench | board |
| S4 | P4 | board session | board |

## §3 Decision

- **D1 Origin in the manifest.** The large `.cor` manifest gets `origin`: `trx` (ours, `pack_large_cor.sh`), `foreign`
  (converted by the host), `backup` (written by the installer from this board's flash). A manifest without the field is
  `trx` (every `.cor` so far is ours). The host shows a badge for `foreign` and `backup`.
- **D2 Restore marker.** A `backup` `.cor` additionally records where its parts came from (`flash_bit` 0x000000,
  `flash_app` 0x220000) and the board's flash ID, so it is restored only to the layout it was read from.
- **D3 Known official releases.** A table in the host (`known_fw.h`: SHA-256 of the whole file, name, vendor) of
  official Gideon and Commodore firmware files. A `.cfw`/`.ue2` in the table is offered "restore"; its conversion
  gets origin `foreign` and the name from the table.
- **D4 The host writes the flash itself** (Q1), under the checks of P3: every part's SHA-256 before the first erase, two
  confirmations, read-back and compare after the write. One code path for a backup `.cor` and an official `.ue2`/`.cfw`.

- **D9 Convert (S1), as built on main.** `P` in the File Browser on a selected `.cfw`/`.ue2` (P as in Package, Q12) writes `<medium>/cores/<name>.cor` (T148 D1): the medium is the first component of the firmware's path, `cores` is
  made when it is missing, `<name>` is the file name without `.cfw`/`.ue2`. A firmware on the internal drive (`/Flash`) is refused
  (a 3 MiB file does not go there; copy it with C first). An existing file of that name is the T139 question "RETURN replaces"
  and is replaced only after the new file is complete and read back. The large `.cor` has `origin` `foreign`, `id`
  `fw-<first 8 hex of the bitstream SHA-256>`, `name` and `version` from `known_fw.h` when the whole file's SHA-256 is in it, else
  the `.bit` header's design and build date ("bit 2026/09/10"; the file name is never used), the icon `cfwicon` shows (built-in logo
  by design name, sidecar `.icon`; Q13), the map from the AXSS stamp, the SHA-256 of both parts; the bytes are those of
  `tools/cor_pack.py pack_large` (the same JSON with sorted keys). Written to `<name>.cor.part`, read back and compared by SHA-256,
  then renamed; ESC, RUN/STOP or LEFT cancel while the firmware is read and while the file is written and leave nothing behind.
  Nothing is written to the flash and the firmware file is not touched. The reader (`cor.cc`) takes `origin` (no key = ours) and a
  backup's `flash_bit`/`flash_app`/`flash_id` as D1/D2 say; the menu and the browser show the badge `foreign` or `backup`.
  `cfw_probe` takes the updater record of the real `.ue2` files (load and start 0x03000000). The manifest keys join
  `cor_pack.py pack-large` and `pack_large_cor.sh` (`--origin`, `--flash-id`).

- **D5 Restore at the UI (R).** `R` in the File Browser on a selected `.cfw`/`.ue2` or on a large `.cor` of origin `backup` (the footer says "R restore" only
  there; on any other file the key does nothing). One modal loop like P: a check page (the whole file is hashed for a `.cfw`/`.ue2`, then both parts are read
  into DDR2 and checked), the first question (details, "Y continues, any other key cancels"), the second ("This is the last question", RETURN within 10 s, not in
  the first second: the repeat of a held Y is not an answer), the progress page, the result page. Refusals are amber pages that say why and "Nothing was written.".
- **D6 What may be restored (P2).** `fw_can_restore` (`fwrestore.cc`): a backup `.cor` whose flash ID is this board's chip, or a `.cfw`/`.ue2` whose whole-file
  SHA-256 is in `known_fw.h`; never a converted (foreign) `.cor`, a `.cor` of ours, a MultiCore update package or any other `.cfw`. `fw_restore_stage` refuses what
  `fw_can_restore` refuses (a caller cannot forget the gate), reads both parts, compares a `.cor`'s parts with the SHA-256 of its manifest, checks the application
  record the boot ROM reads and the sizes against the slots, and only then seals an image. `fw_write` takes only a sealed image (T152 reuses `flashfw`).
- **D7 The write (`flashfw.cc`).** Application first, its first sector erased first and programmed last; then the bitstream front to back and the rest of its slot
  erased; every page read back (3 tries); both regions read back whole; the AXSS stamp walked in the flash. No key stops it once the first erase is issued (ESC,
  RUN/STOP and left-arrow say so on the page). A failure after the first erase offers the write again from the staged copy in DDR2. A chip other than the board's
  (Winbond W25Q128; `HOSTAPP_EMU_FLASH` adds the emulator's) and a geometry other than 256/4096 are refused before the first erase.
- **D8 The way back.** The restored firmware does not know the host. The question says it: the way back is the MultiCore update file, run from the restored
  firmware's update menu. Cores and `boot.cfg` on the flash drive are not touched.

- **D11 The look of the firmware pages (Alex, 2026-11-01).** The UX pass on `t137-updater` (commits bcf49a69e, 95a0fc7d4: scrim behind the
  page, pages sized to their lines, only the keys that work in the footer, one progress number with one bar, amber refusals, LEFT cancels, eight
  lines per page, plain wording) is taken onto main's update (T152), restore and convert pages. Alex, after the side-by-side screenshots: "The
  old draft is much better ... please take that one." The branch's code is not merged (main's code was rewritten); the look is ported.

## §3a Open questions

- **Q1** Answered 2026-10-28 (Alex: "Ja, selbst schreiben" - "Yes, write it ourselves"): the host writes the flash
  itself, not the package's own updater -> D4.

- **Convert first (Alex, 2026-10-29: "yes"):** the "convert .cfw/.ue2 to .cor" part (P1, `fwconvert.cc` on branch
  `t137-updater`) is taken to main on its own, ahead of the T152 split, and ships in the next tester release; restore and the
  update part wait for the split.

- **Q12 The key for convert.** Answered twice on 2026-10-29. First (Alex, "T wie Transform?", translated "T as in Transform?"): T, worded Transform. Then, superseding
  it (Alex, "Können wir statt T Transform nicht P Package machen?", translated "Instead of T Transform, can we make it P Package?"): the key is **P** and the
  wording says Package. The GitHub issue #3 had asked for C; C is copy since T139 D1; T and nothing else does nothing now. Built: footer "P package", panel line
  "P packages it as a .cor in the cores folder", pages "Cannot package <file>", "Packaging <name>.cor", "Packaged as .cor", "Package cancelled", "Packaging failed",
  "Nothing was packaged.", panel origin "packaged firmware"; the manifest field stays `origin` "foreign" and the menu badge `foreign`.
- **Q13 The icon of a converted `.cor`.** Built: the logo `cfwicon` shows for the same `.cfw` (Gideon's logo for Gideon's firmware, the
  sidecar `<name>.icon` when there is one), as T137 D9 on `t137-updater` says. T135 D8b gives the installer's backup `.cor` one fixed
  recovery icon instead (a way back). Recommendation: keep the logo for a converted firmware; a backup is the recovery entry.
  **Answered 2026-10-31 (Alex, screenshot of restore pictograms, "roughly like that is good", translated):** the backup keeps its own recovery
  icon, drawn as the counter-clockwise "restore" arrow (T135 D8b); a converted firmware keeps its logo.

- **Board, restore (S2/S3), 2026-10-31 (Alex, host app main-76817bbe):** `R` on `/SD/cores/commodore-110.cor` (backup, made by the installer in the
  plain firmware): question "Restore Commodore 1.1.0? ... 2.0 MiB + 1002 KiB, the backup is complete and intact", written and verified in 30 s;
  after the power cycle Commodore 64 Ultimate 1.1.0 booted (BASIC, its menu). Passed.
- **D10 (Alex, 2026-10-31, translated): "Better not to go back into the host menu afterwards but to restart by a key"; "after an update too".**
  The result page of a restore (T137) and of a `.txu` update (T152) says "any key restarts" and the key reloads the FPGA from flash address 0
  (ICAP IPROG), as a power cycle would; no way back into the old host menu.
  **As built (T152 D11, 2026-11-04; chosen over the IPROG reload, which was never built):** the result page counts down 5 s and the board asks the ESP32 control module to power-cycle it
  (`restart_page` in `hostui.cc`, `espreboot.cc`); a key does it at once; when the module does not answer, or the board stays powered, the page says so and keeps "Switch the power off and on ...".
  There is no way back into the host menu.

## §4 Out

- Shipping converted files: a `foreign` `.cor` is made on the board only and never goes into a release (licences).
- The analogue picture (T139 D5).
- The MultiCore update (U, P4, S4, `fwupdate.cc`): T152. A MultiCore update package is refused by R ("not restorable") and converted like any other `.cfw` by P.
- A cancel once the first erase is issued (D7); a restore from a medium other than the one the host reads (network, ESP32).

## §5 Build notes

**Convert built 2026-10-29 on `t137-convert`** (from main `beb6d5975`; the first build of it is on `t137-updater`, which has the update and restore code
and was not brought over).
- Code: `fwconvert.cc` (the `.cor` writer: plan, hashes, `<name>.cor.part`, read-back, rename), `fwknown.cc` + `known_fw.h` (whole-file SHA-256 against four
  official files: Commodore 1.1.0, Gideon 3.14, 3.14d, 3.12a-15; where each lay on the build machine is in the table's header and in
  the `t137-updater` spec §5; a row is a recognition by name and place, not a signature), `FoFs::make_dir` (`hostfs.cc`: `create_dir`), `cor.cc`/`cor.h`
  (`origin`, D1/D2), `cfw.cc` (the updater record at 0x03000000), `hostui.cc` (key P, `convert_flow`, the badge, the footer, the panel's last line), `tools/cor_pack.py`
  and `pack_large_cor.sh` (`--origin`, `--flash-id`). The host gateware is unchanged (`tools/host_pin.sh --check`).
- Benches, run on the builder (`trx64-sim-env`, `nice -n 15`, `docker --rm`): `tests/hostapp/run_fwconvert.sh` (`test_fwconvert`: the bytes equal what `cor_pack.py
  pack_large` writes for the same parts, the packer's own reader accepts the file, a `.ue2` gives the same `.cor`, the folder made once or refused, no room, a
  write that fails half way, a bit flipped in the `.part`, a cancel, the table, the origin reader) with 20 controls (each takes one decision out of `fwconvert.cc`,
  `fwknown.cc` or `cor.cc`: no read-back, replace unasked, room not checked, `origin` missing, map always identity, the file named early, no `.part`, not in `cores`,
  the internal drive allowed, the folder not made / not seen / a file taken for it, the table's name ignored, a prefix match, an abort or a read error ignored,
  an unknown origin taken, the backup's flash keys unchecked, flash keys on a foreign `.cor`); control `cfw_ue2_real_record` in `run.sh`; the render bench's
  `scenarios_t137` (the real `hostui.cc` over a tree on disk: the key goes to .cfw/.ue2 only, the refusal page, the folder made, the page, the replace question with
  both answers, a `.ue2` named from the table, the badge `foreign` in the menu and "packaged firmware" in the panel, the internal drive, no room, ESC while the file is
  read and while it is written) with 9 controls (`t137_*`). Result: `tests/hostapp/run.sh` exit 0 (107 control lines); the render bench 434 checks, 0 failed, 63
  controls. Pictures: `convert_done`, `convert_replace`, `convert_refused`, `menu_foreign`.
- **Not run:** the host application for the RISC-V board (the CI image of the trxapps stage has `apps/ultimate`; it is not on the builder and was not copied there),
  and with it `test_flashwrite`, `test_fatexpand` and the T111 benches of `run.sh`, which skip out loud without the submodule; the render bench ran against
  the `apps/ultimate/software` of another session's temporary checkout on the builder, mounted read-only (its `lvgl` is byte-identical to the pinned one, git tree
  61fdb212; its `io`, `components` and `system` headers were not compared with the pin). Not on the board either: the real speed of the whole-file hash and
  of writing 3.5 MiB to an SD card or stick, and the picture on a real screen.
- **Open for the board** (Alex): B1 on a stick with Gideon's 3.15a `.cfw` and Commodore's 1.1.0 `.ue2`: P on each, the `.cor` appears in `cores/`, S starts it,
  the badge says `foreign`; the name of the 1.1.0 file is "Commodore 64 Ultimate 1.1.0" (the table row). Q13 above.
- Found on the way (same as on `t137-updater`): the real `.ue2` files carry the updater's record at 0x03000000, so `cfw_probe` refused all of them; the firmware's
  `printf` has no `%%`, ignores a precision and prints `%x` in capitals, so `fwconvert.cc` writes its manifest without them (`run_fwconvert.sh` greps for them).

**Restore built 2026-10-30 (T137 step 2, branch `worktree-agent-ab3bbec775e48c870` from main `4ccab5b85`; code from `t137-updater`, rewritten, not merged).**
- Code: `flashfw.cc/.h` (the shared flash layer: checksums, the AXSS walk, `fw_image_seal`, `fw_write`; T152 reuses it), `fwrestore.cc/.h` (`fw_probe`, `fw_can_restore`,
  `fw_restore_stage`), `fwconfirm.cc/.h` (the two questions as pure code), `hostui.cc` (key R, `restore_flow`, the pages, the footers "R restore"), `known_fw.h` unchanged.
- Benches: `tests/hostapp/run_fwrestore.sh` (`test_fwrestore.cc` over packages from `mkrestoretest.py`, a fake flash with erase semantics; run.sh calls it) with 28 controls
  (who may be restored x4, the check before the first erase x6, the flash layer x11: order, retry, read-back, tail, stamp, chip, geometry, the two questions x5); the render
  bench's `scenarios_t137r` (the real `hostui.cc` over a tree of every kind of file, a fake flash: the key goes to .cfw/.ue2 and to a backup only, an unknown .cfw and a
  backup of another chip are refused with the flash untouched, "No" at either question writes nothing, a held Y is not an answer, Y and RETURN a second later write it and
  the flash holds the application record, a flash that already holds the firmware says so) with 5 controls (`t137r_*`).
- RISC-V: `ci/trxapps.sh` in `u64-build-env:ci` on the builder builds hostapp and coreapp (run of 2026-10-30, rc 0); `apptests` hostapp, coreapp, loader PASS; render bench 462
  checks, 0 failed. `run_fwrestore.sh` has no `-ftrivial-auto-var-init` (the image's gcc does not know it).
- In ue2emu (a build with `HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH`, a backup `.cor` whose flash ID is the emulator's Infineon S25FL128L): a real restore ran to the end: check, both
  questions, 18 erases and 275 programs in 1.6 s emulated, read-back, "Firmware restored". Refusals seen: another chip, an unknown `.cfw`; R does nothing on a foreign `.cor` and
  on a `.cor` of ours. The emulator's flash image is a copy that the run drops, so nothing persists.
- The panel has eight lines (`FM_LINES`, D11; six before): the first question of a restore uses all eight, a long sentence is two lines. Not run: a real restore on the board (Alex's step), a real official
  `.ue2` in the table (`known_fw.h` rows are the four files of D3; the render bench restores a backup, the official path is benched in `test_fwrestore.cc`).
- **Open for the board** (Alex): B2 a backup `.cor` of the board made by the installer (T135 D8): R, Y, RETURN, power cycle, the original firmware runs; then the MultiCore update
  file from its menu gets MultiCore back.

**D11 built 2026-11-01 on `t164-d11`** (the look of the update, restore and convert pages; the decision D11 is on `main`). The look of the old draft is ported onto main's pages,
not its code:
- One panel for every modal page of the host app (the format, convert, restore, update, inspect pages go through `fm_draw`): `fm_root` is a scrim over the content area (solid
  background, so the browser and its note do not show), `fm_box` is the page, as high as its lines (140 px at least, up to eight lines) and centred. A page change is still one
  `TrxPage` on `fm_root` (render bench: "page areas: start step change 1, fm page change 1"); the footer is set before it as a refresh of its own.
- The keys of the page are the footer while it is up (the saved browser footer comes back at `fm_close`); the line inside the page is gone. The progress pages have one number
  (`pct`, right-aligned on the row of what is being done, with a blank row above) and one bar; the message under the bar is only "Cannot stop now. Wait for the result.".
- Wording: question 2 is "Last question." + the erase sentence + "no key stops the write" + "power must stay on" (one line each, the RETURN key only in the footer);
  the update question names the version and the sizes, never the commit, and says "The package is complete and intact."; the restore question splits its two long
  sentences over two lines each and says "The restored firmware does not use them."; the update result has a blank row before "Switch the power off and on ..." and no
  "host key" wording; the failed-write page says "If you leave, the board needs to be repaired with a JTAG cable."; a package too big for its slot is not called damaged;
  the replace question says "RETURN replaces it    any other key cancels"; the convert result says `Marked "foreign" in the boot menu. A adds it, S starts it.`
- Bench (`tests/hostapp/render/render.cc`): `fw_geom_check` on every firmware page (44 pages): scrim = the content area, page inside it and centred, footer == the page's hint;
  heights 140..306 px. Controls `nofoot` (44 checks red), `fixedh` (1), `noscrim` (132). The format page's foreign-pixel probe follows the page rectangle.
- **Review items kept as built (spec wins):** the update question keeps "The host key changes: <n> partial cores need a rebuild for the new host." and "The host key stays the
  same: no core is affected." (T152 S2 names the cores and the manual 9.3.5 quotes them); "host" and "host app" stay the names of the parts (T152 D1), not "MultiCore firmware";
  the version stays `main-<id>` of the package. **Not in this pass:** the browser panel rows, footers and Settings row (review 11, 15-19, 23, 24: main's panels are not the
  t137-updater's), the "[EMULATOR BUILD]" tag (21: main has no such build flag), the key rule of D10 (built later: see the D10 entry in section 5 below).

**D10 for the restore result page, built 2026-11-07 (branch `w1-t152`).** `fw_result` calls `restart_page` with the same countdown as the update page (T152 D11). Proof in the render bench
(`scenarios_t137r`, restore of a `.cfw` after the countdown scenarios): key at once = one request in under 1 s; no key = one request after 5000 ms; module silent = the row says
"The restart did not happen: the ESP32 control module did not answer." and the old "Switch the power off and on to start the restored firmware." stays. Control
`t137_d10_restore_no_restart` (the old fixed text) fails. Pictures: `t137_d10_key`, `t137_d10_countdown`, `t137_d10_noanswer`. **Board step (Alex):** restore a backup `.cor`, watch the
countdown and the restart; the real power cycle through the module is not run from the host app yet (same step as T152 D11).
