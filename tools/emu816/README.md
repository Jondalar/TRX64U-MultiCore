# emu816 - W65C816 instruction-set interpreter + REU model (unit-test tool)

Python 3 stdlib only. Instruction set only: no SuperCPU `$D07x`, no timing, no VIC/SID/CIA, no booting.
I/O reads return what a fixture provides, I/O writes are logged.

    python3 -I -m unittest discover -s tools/emu816/tests -t tools      # 95 tests

## API

```python
import sys; sys.path.insert(0, "tools")
from emu816.cpu import CPU
from emu816.memory import Memory, IORegion
from emu816.reu import REU

mem = Memory()                                   # sparse 16 MiB, bank = addr>>16, unwritten = 0
IORegion(0xD000, 0xDEFF, reads={0xDC01: [0xEF, 0xFF]}, default=0).attach(mem)   # reads scripted, writes -> .writes
cpu = CPU(mem)                                   # 65816, native/emulation per cpu.E ; CPU(mem, mode6502=True) = 6502 mode
cpu.E = 0; cpu.fM = cpu.fX = 0                   # or cpu.set_state(E=0, M=0, XF=0, A=..., X=..., D_flag=1)
mem.load(0x8000, code)
res = cpu.run(0x8070, stop_on="rts", sentinel=0x00FFF0, max_steps=1_000_000)   # RunResult(reason, steps, pc)
```

`run(entry, stop_on=...)`: `"rts"` pushes a JSR-style return to `sentinel` (inside the current program bank) and stops when
PB:PC reaches it; `"rtl"` pushes a 3-byte JSL return and stops at the 24-bit sentinel; `"address"` stops at any of
`addresses=[...]`; `None` = only `max_steps` / WAI / STP. `max_steps` always applies. Reasons: `sentinel address max_steps WAI STP`.
Registers: `C` (16-bit accumulator; `A` low, `B` high), `X Y S D DB PB PC`, flags `fN fV fM fX fD fI fZ fC`, `E`. `cpu.P` is the P byte.
Trace: `cpu.format_trace(n)` (ring of the last 64 instructions, disassembled, with registers before the instruction).

Memory hooks: `mem.add_hook(lo, hi, read, write)`; `read(addr)->int|None` (None = fall through to RAM),
`write(addr, v)->True` consumes the write. `mem.write_log = []` logs every write; `mem.post_step` = callbacks run after every instruction.

## Implemented behaviour and sources

All 256 opcodes, all addressing modes (long, `[dp]`, `[dp],Y`, `sr,S`, `(sr,S),Y`, `(a,X)`, `[a]`, MVN/MVP, PEA/PEI/PER, BRL, JSL/RTL,
BRK/COP/RTI native + emulation, WDM, WAI/STP as stop reasons). Decisions where the datasheet is silent or subtle
(tests in `tests/test_core.py` pin each one):

| topic | rule used |
|---|---|
| 8/16-bit width | LDA/STA/ALU by M; index ops by X. TAX/TAY width of the destination (X flag, 16 bit even with M=1); TXA/TYA width of A (M). TCD/TDC/TCS/TSC always 16 bit. XBA: N,Z from the new low byte. |
| X=1 | X/Y high bytes forced 0 (SEP #$10, PLP, XCE). E=1: M=X=1 forced, S high = 1 (restored after every instruction). |
| direct page | Bank 0, 16-bit wrap. E=1 **and DL=0**: all dp EAs and pointer fetches (dp, dp,X, dp,Y, (dp), (dp,X), (dp),Y, [dp], PEI) wrap inside the page (including the 2nd/3rd pointer byte); E=1 with DL!=0 uses the native rule. |
| absolute | DB:abs (+X/Y) with a 24-bit add (bank carry), 16-bit data crossing `$xxFFFF` goes into the next bank. Long,X: 24-bit wrap. `(sr,S),Y`: DB:ptr+Y, 24-bit. |
| stack | `sr` wraps in bank 0. Native S is 16 bit, wraps in bank 0. E=1: PHA/PLA/PHP/PLP/PHB/PLB/PHK/JSR/RTS/BRK/RTI wrap in page 1; the 65816-new stack ops PEA/PEI/PER/PHD/PLD/JSL/RTL/JSR(a,X) use the full 16-bit S (may touch page 0/2) and S high is forced back to 1 after the instruction (WDC manual, emulation-mode stack note). |
| PC | PB:PC; operand fetch and branch targets wrap inside the program bank. JMP (a) pointer from bank 0 (16-bit wrap in bank 0), target in PB; JMP (a,X)/JSR (a,X) pointer from PB; JML [a] 3 bytes from bank 0. |
| MVN/MVP | Object code `op dst src` (destination bank first). A (full 16-bit C, regardless of M) = count-1, X source, Y destination, DB := dst. Runs byte by byte (overlap semantics fall out), X/Y wrap at 8 bit if X=1, ends when C wraps to `$FFFF`; C=`$FFFF` on entry copies 65536 bytes. |
| BRK/COP | PC+2 pushed; native pushes PBR first, clears D, sets I, PB:=0, vectors `$FFE6/$FFE4`; E=1 vectors `$FFFE/$FFF4`, pushed P has bit 4 (B) set for BRK **and** COP (datasheet leaves COP's B bit unstated; X is forced 1 in E=1). RTI native pulls PBR, E=1 does not. |
| RMW | 16-bit read-modify-write writes **high byte first, then low** (datasheet cycle table). |
| decimal ADC/SBC | 65816: nibble chain (BCD correction per nibble, 8 or 16 bit); N and Z from the decimal result, V from the sum with the top nibble not yet corrected, C from the corrected top nibble. For valid BCD the result and C are exact (swept in tests). **N/V for invalid BCD operands are not verified against silicon** (only against the published algorithm); the datasheet gives no table. |

Known-answer vectors in the tests (derived by hand from the definitions, not recorded from this code): binary ADC/SBC flag cases
(`$50+$50`, `$FF+1`, `$7FFF+1`, `$00-1`, `$80-1`...), decimal `$09+$01=$10`, `$99+$01=$00 C`, `$58+$46+C=$05 C`, `$46-$12=$34`,
`$00-$01=$99 C=0`, 16-bit `$9999+1=$0000 C`, `$0100-$0001=$0099`, an exhaustive 100x100x2 BCD sweep (8 bit) and a random 16-bit sweep
against integer arithmetic, MVN/MVP register end states, emulation/native wrap cases.

## 6502 mode (`CPU(mem, mode6502=True)`)

E=1 with D=DB=PB=0, **any opcode outside the 151 legal NMOS opcodes raises `IllegalOpcode6502`** (all 105 others, including the
undocumented NMOS ones, 65C02 and 65816 opcodes). Translated code is judged by NMOS 6510 rules; where an E=1 65816 differs, 6502 mode follows the 6510:

| legal opcode behaviour | E=1 65816 | 6502 mode (NMOS 6510) |
|---|---|---|
| decimal ADC | N,Z,V valid from the decimal result | A and C as 65816 for valid BCD; **Z from the binary sum**, N/V from the intermediate (Clark seq. 2) |
| decimal SBC | N,Z,V from the decimal result | **all flags from the binary subtraction**, only A decimal-corrected |
| `JMP ($xxFF)` | reads `$xxFF`,`$xx+1:00` | reads high byte from `$xx00` (bug). **Detected: raises `JmpIndirectBug`** (set `raise_on_jmp_bug=False` to emulate the bug and only record `cpu.events`) |
| BRK | clears D | does **not** clear D |
| RMW (INC/DEC/ASL/LSR/ROL/ROR, zp/zp,X/abs/abs,X) | one write (16-bit: hi then lo) | **two writes: old value, then new value** (visible in the I/O write log) |
| indexed addressing | `abs,X/Y`, `(zp),Y` add with bank carry (DB:abs+X may reach `$01:0000`) | **wrap at 64 KiB** |
| zero page,X / (zp,X) / (zp),Y pointer | page wrap only when DL=0 (it is) | page wrap (same) |

Not modelled (documented limits): bus-visible dummy reads (page-cross and indexed-store dummy reads), interrupts/IRQ/NMI/reset sequences,
the 6510 port at `$00/$01` (plain RAM unless a hook is installed), undocumented opcodes (rejected), all timing.

## REU (`emu816/reu.py`)

Commodore 1764/1750 REC at `$DF00-$DF0A`, mirrored through `$DFFF` (`& $1F`; registers `$0B+` read `$FF`). STASH/FETCH/SWAP/VERIFY, autoload,
fixed-address bits, length 0 = 64 KiB, status register (bits 7-5 clear on read), interrupt mask/IRQ line, `$FF00` trigger (command bit 4 = 0 arms,
a CPU write to `$FF00` fires), transfer executes at the instruction boundary after the write that starts it. Register semantics are a line-by-line port of
TRX64's `crates/trx64-core/src/reu.rs` (itself a port of VICE `reu.c`); cited per function in `reu.py`:
model options 109-137 (sizes 128/256/512/1024..16384 KiB, bank-register bits 3-7 stuck at 1 below 1 MiB), reset 308-322, register file 359-419,
live read 423-442 (status clear), live write 445-481, `$FF00` snoop 805-820, transfer entry 516-544 (length 0 = 64K 524-525, fixed bits 526-529),
`update_regs`/autoload with shadow registers 548-583 (Half-Autoload bug: shadows track every register write), stash 586-607, fetch 610-634,
swap 637-661, verify 666-710 (all three "weirdnesses"), 512K wraparound of `increment_reu` 348-354. Behaviours cross-checked against the gate tests in
TRX64's `crates/trx64-core/tests/reu_gate.rs`: round trip :123, swap :139, verify :150, status clear :175, autoload :191,
fixed C64 address :209, `$FF00` arming :242, trigger disabled :266, IRQ needs both mask bits :346.
**Wraparound bug (reu.rs:127-131, 348-354):** one transfer cannot cross a 512 KiB boundary: the address increment wraps within the low 19 bits,
so REU `$07:FFFF` + 1 = `$00:0000` (not `$08:0000`). Default size is 16384 KiB so REU address == original 24-bit address.
The host (C64) side goes through `Memory.read/write` of bank 0, i.e. through I/O hooks. Not modelled: cycle cost, VIC stealing, `DmaBus`.

## Files

`cpu.py` core + disassembler, `opcodes.py` table + legal-6502 set, `memory.py`, `reu.py`, `tests/` (`test_core.py`, `test_6502mode.py`, `test_reu.py`).
