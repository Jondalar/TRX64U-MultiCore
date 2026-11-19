# dis65816 - W65C816 flow-following disassembler

Python 3 stdlib only. Reads a PRG by path; contains no program data. Copied 2026-11-19 (with `tools/emu816`) from another of Alex's
projects, where it was built for a SuperCPU game; here it serves the debugging of SuperCPU programs on the C64SCPU core (T241, GitHub #108).
A listing is the program's data: write it outside the repository (`--out` into a scratch directory). `/analysis/`, the default output, is
ignored by git as a safety net.

    python3 -I tools/dis65816/dis65816.py PROGRAM.prg --entry 080D:e=1 --out SCRATCH/prog --verify
    python3 -I tools/dis65816/test_dis65816.py  # unit tests
    python3 -I tools/dis65816/rebuild_check.py LISTING.asm ORIGINAL.prg [KickAss.jar]

## Options
| option | meaning |
|---|---|
| `INPUT.prg` / config `input` | PRG, first 2 bytes = load address. Relative paths resolve against the project root. |
| `--entry ADDR[:e=1,m=8,x=16]` | start of flow following. Hex. `e=1` forces m8/x8. `e=0` needs `m`/`x` or they are unknown. Without `e` everything is unknown. |
| `--force ADDR:e=0,m=16,x=16` | user-declared state at an address; overrides whatever arrives there (listed in the summary as forced override). |
| `--alias VIRT=PHYS:LEN` | code that runs at VIRT but sits at PHYS in the file (relocation by a copy loop). Decoded and labelled at VIRT, emitted at PHYS. |
| `--stop ADDR` | declare data: flow is not followed into ADDR (listed in the summary). |
| `--track-php` | pair PHP/PLP on an abstract push stack (PLP restores the pushed M/X). Default off: PLP/RTI make M/X unknown. |
| `--out DIR`, `--name STEM` | output dir (default `analysis/dis65816/<name>/`), file stem. |
| `--verify [--kickass JAR]` | assemble with KickAssembler and compare with the whole input PRG; exit 2 on mismatch. The jar: `--kickass`, else `$KICKASS_JAR`, else `/Applications/KickAssembler/KickAss.jar`. |

Config JSON keys: `input`, `name`, `out`, `bank`, `track_php`, `entries` (`{"addr":"8000","e":1,"m":8,"x":8}`),
`force` (same shape), `alias` (`{"virt","phys","len"}` hex strings), `stop` (hex strings). A config describes one program, so it lives
with that program's analysis, never here.

## Outputs (per run, in `analysis/dis65816/<name>/`)
* `<name>.asm` - KickAssembler source. Every byte is `.byte`; comments carry address, 65816 mnemonic+operand,
  state `[e0 m16 x8]` (`?` = unknown) and I/O names. Labels `L_xxxx:` at every branch/jump/call/PER target.
  Labels for relocated (alias) addresses are `.label L_xxxx = $xxxx` at the top. Origin `*=$load`.
* `<name>.json` - every instruction (addr, bytes, mnemonic, mode, operand, `state_in`/`state_out`, flow, targets,
  external targets, `dest_bank`/`src_bank` for MVN/MVP), `xrefs` (branch/jump/call/per/operand), and `summary`.
* `summary.txt` - human summary.

Summary fields: code/data bytes, unresolved indirect jumps, state conflicts (`width_dependent`,
`joined_to_unknown`), benign merges, overlap conflicts, unknown-width stops, forced overrides, declared stops hit,
external targets, self-modifying writes into decoded instruction bytes, unreached ranges, call-return state notes.

## Semantics
* Table: all 256 W65C816 opcodes. MVN/MVP bytes are `op, DEST bank, SRC bank`; text is assembler order `MVN $src,$dest`;
  JSON has explicit `dest_bank`/`src_bank`.
* State (e, m, x) plus carry (for XCE) tracked through CLC/SEC+XCE, REP, SEP. XCE to/from emulation gives m8/x8.
  In emulation REP/SEP do not change M/X. SEP with unknown E gives 8 bit; REP with unknown E gives unknown.
  Carry is cleared to unknown by ADC/SBC/CMP/CPX/CPY/shifts/rotates/PLP.
* PLP/RTI: M/X unknown (unless e=1). An immediate of unknown width stops that path and is listed under
  "unknown-width stops"; the instruction is not emitted as code. Declare the state with `--force` (or use `--track-php`).
* A second arrival with different e/m/x is recorded. If the instruction's length depends on the differing bit, the first
  decode is kept (`width_dependent`). If the result state is identical for both (e.g. REP/SEP overwriting it, or a path-end
  instruction) it is a benign merge. Otherwise the states are joined (differing bits become unknown) and flow is re-propagated
  (`joined_to_unknown`).
* Flow: branches (also conditional fall-through), JMP/JML/BRA/BRL, JSR/JSL (target + fall-through). RTS/RTL/RTI/BRK/COP/STP
  and indirect JMP/JML end the path; indirect JMP/JML/JSR (abs,X) are listed as unresolved (JSR (abs,X) still falls through).
  PER/PEA are not followed (PER target gets an xref and a label). JSL/JML to another bank and targets outside the file are
  "external targets". Branch/BRL targets wrap inside the 64K bank.
* Bytes not reached = data. Overlapping decodes: first wins, the other is listed.

## Known limits
* JSR/JSL fall-through assumes the callee returns with the caller's e/m/x (and balanced stack). `call-return state notes`
  lists callees whose RTS/RTL state differs from the entry state (`changed` = known and different; `unknown_at_return`
  = unknown, usually PLP without `--track-php`). Not auto-corrected.
* A conditional branch whose fall-through is data (always-taken by the carry) is decoded as code; use `--stop`.
* Self-modifying code (operand patched at run time, e.g. `JSR $FFFF` placeholder) is only reported, not resolved.
* PEA/RTS-trick dispatch, jump tables and callbacks are not resolved: give more `--entry` addresses.
* Alias segments: no instruction may straddle a segment edge; no overlap between a virtual and a real address range.
* Single bank per run (default 0); no direct-page/DBR tracking, so `dp` operands are not named.
* I/O names are labels only: VIC/SID/CIA standard names; SuperCPU `$D070-$D07F`, `$D0B0-$D0BF` are `SCPU_xxxx`; the four
  names on `$D07A/$D07B/$D07E/$D07F` were written from memory of the SuperCPU docs and are NOT verified against a source here.
