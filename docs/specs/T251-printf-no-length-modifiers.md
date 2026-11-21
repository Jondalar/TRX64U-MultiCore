# T251 — The core application's texts show their numbers on the board: no format with a length modifier

**Status:** IN PROGRESS (2026-11-21). Asked by Alex, 2026-11-21.
Open (one line, as in the TODO table): IN PROGRESS 11-21: S1 PASS, review fixes in; open: S2 on the bundle's image, PR
**Repo:** TRX64U-MultiCore — the core application (`coreapp/src`, `coreapp/cores`).
**Number:** T251.
**Origin:** GitHub issue #121 (a tester, release 0.2.0, C64 Ultimate): "Browsing for ROM shows "%llu" as file size" (the C128
core, the JiffyDOS ROM). Alex, 2026-11-21 (translated): "Could you look through the issues that carry the tag 0.2.1? I would like
to get them done, and then we make 0.2.1."
**Depends on:** nothing.
**Milestone:** M2 (release 0.2.1).

---

## §1 The problem

- **Who:** a person who browses for a file in a core's menu, and everyone who reads a core's report file or its messages.
- **What happens today:** the file browser shows "%llu" where the size of a file belongs (#121, photo of release 0.2.0).
- **Why, read from the code:** the board's `snprintf` is `host-app/software/system/small_printf.cc` (`_my_vnprintf`; the core
  application links it, `host-app/target/u64ii/riscv/coreapp/Makefile`). It reads flags, a width and one letter (`d u i`, `x X`,
  `s`, `c`, `p`, `f`). A length modifier (`l`, `ll`) reaches `default:`, which writes `%` and the letter and takes no argument
  from the list. So `"%llu bytes"` prints "%llu bytes", and every conversion after such a modifier reads the argument meant for
  the one before. `coreapp/src/ops.cc:250` already says so in a comment ("the firmware's printf has no %ld"). The tests on the
  Mac use the system's printf, which knows `%llu`: no test shows it.
- **The parts:**
  - **P1** the file browser's size column (`coreapp/src/menuctl.cc:831`, #121).
  - **P2** the eight other formats with a length modifier in the core application:
    - `coreapp/src/corereport.cc:83`, the report file's media line `"%s, %llu bytes (%s)"`: the `%s` after the modifier reads
      the size as a pointer;
    - `coreapp/cores/apple2_disk.cc:483` and `:485`, `coreapp/cores/apple2_hdd.cc:143`: the messages of an image of the wrong
      size;
    - `coreapp/src/diagtest.cc:986`: the message of a wait outside the engine's range;
    - `coreapp/cores/c128.cc:367` and `:369`: the C128's diagnostic line (`%ld` three times: the conversions after them are
      shifted);
    - `coreapp/src/ringfile.cc:2563`: the console line of a loaded `.trxring`.

## §2 Scenarios — what proves it is solved

```gherkin
Scenario: S1 (P1, P2) no length modifier left
  Given the core application's sources (coreapp/src, coreapp/cores)
  When their format strings are searched for a conversion with a length modifier (l, ll, h, hh, z, j)
  Then none is found

Scenario: S2 (P1) the size in the file browser
  Given the core application's RISC-V image in ue2emu, a core's file browser on a folder with a file
  When the browser shows the file
  Then its row shows "<n> bytes" with the file's size, not "%llu bytes"
```

| # | Solves | Test | Where it runs |
|---|---|---|---|
| S1 | P1, P2 | `git grep` with the pattern of §5 | Mac |
| S2 | P1 | `tests/ui/features_cores/t251_c128_pick.feature`: the RISC-V image (so the board's printf) in ue2emu | Mac (`tests/ui/run.sh`) |

## §3 Decision

- **D1:** a 64 bit size a person reads goes through `u64_dec()` (`host-app/software/components/u64_str.h`, the helper of
  the FTP server) and `%s`: a file's size in the file browser (`coreapp/src/menuctl.cc`) and a medium's size in the report
  (`coreapp/src/corereport.cc`). exFAT is on (`FF_FS_EXFAT 1` in `chan_fat/*/ffconf.h`), so a file can be larger than
  4 GiB. A 32 bit value takes `%u` or `%d` with a cast. In the three files that D24 holds at their line count
  (`coreapp/src/diagtest.cc`, `coreapp/cores/apple2_disk.cc`, `coreapp/cores/apple2_hdd.cc`) a 64 bit value says
  "more than 4294967295" above 32 bits; the 3.5" message of `apple2_disk.cc` only prints 409600 or 819200.
- **D2:** `small_printf.cc` is not changed: it is the host tree's file, and the host application links it too. The host
  application's own code (`host-app/software/hostapp`) has no format with a length modifier. `sd_card.cc:204`, which both
  applications link, has `%lu` inside a comment block: it is not compiled.

## §4 Out

Nothing is left out.

## §5 Built

- **2026-11-21, branch `t251-printf-sizes`:** the nine formats of §1 (D1). `tools/check_d24.py` ok (the diag message stays one line).
- **S1 PASS (2026-11-21):** `git grep -nE '"[^"]*%[-+ 0#]*[0-9*]*(\.[0-9*]+)?(hh|h|ll|l|z|j|t|L)[diouxXcsfeg]' -- coreapp/src
  coreapp/cores` finds only the comment of `coreapp/src/ops.cc:250` ("the firmware's printf has no %ld").
- **S2 PASS (2026-11-21):** `COREAPP=.ci/dev/t251-printf-sizes/coreapp.app tests/ui/run.sh features_cores/t251_c128_pick.feature`:
  "1 passed, 0 failed". The C128's "JiffyDOS C128 ROM" row opens the picker; in `games/` the row of `alpha.d64` shows
  "174848 bytes" and `hello.prg` "14 bytes" (picture `t251-c128-pick`: the `.adf` files show "901120 bytes").
- **The review's fixes (2026-11-21):** D1 and D2 as above; before, D1 cut every size to 32 bits ("a file is below 4 GiB
  (FAT32)"), which exFAT disproves. `menuctl.cc` and `corereport.cc` include `u64_str.h` by its path, as `apple2_hdd.cc`
  includes `hdd_rom.h`. `tools/check_d24.py` ok. The four changed files compile with the flags of `tests/coreapp/run.sh`
  (`c++ -fsyntax-only`, no format warning). S1's search finds only the comment of `coreapp/src/ops.cc:250` again.
- **S2 again:** the file browser's line changed, so S2 runs again on the RISC-V image of the bundle (T253).

