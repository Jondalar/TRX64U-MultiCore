#!/usr/bin/env python3
"""Unit tests for dis65816 (synthetic byte sequences only; no game data)."""
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dis65816 as D  # noqa: E402


def img(data, load=0x8000, aliases=None):
    return D.Image(bytes(data), load, 0, aliases)


def st(spec):
    return D.parse_state(spec)


def dec(data, state="e=0,m=8,x=8", addr=0x8000, load=0x8000):
    ins, nxt = D.decode_one(img(data, load), addr, st(state))
    return ins


def run(data, entries, load=0x8000, forced=None, track=False, aliases=None, stops=None):
    ents = [(a, st(s)) for a, s in entries]
    f = {a: st(s) for a, s in (forced or {}).items()}
    return D.Disassembler(img(data, load, aliases), ents, f, track, stops).run()


class TestTable(unittest.TestCase):
    def test_256_entries_and_modes(self):
        self.assertEqual(len(D.OPCODES), 256)
        for mn, mode in D.OPCODES:
            self.assertTrue(mode in D.FIXED_LEN or mode in ("imm", "immx"), mode)

    def test_known_opcodes(self):
        exp = {0x00: "BRK", 0x02: "COP", 0x42: "WDM", 0x44: "MVP", 0x54: "MVN",
               0x5C: "JML", 0x22: "JSL", 0x6B: "RTL", 0x62: "PER", 0x82: "BRL",
               0xF4: "PEA", 0xD4: "PEI", 0xFB: "XCE", 0xEB: "XBA", 0xDB: "STP",
               0xCB: "WAI", 0xDC: "JML", 0x7C: "JMP", 0xFC: "JSR", 0x6C: "JMP",
               0x1B: "TCS", 0x3B: "TSC", 0x5B: "TCD", 0x7B: "TDC", 0x9B: "TXY",
               0xBB: "TYX", 0x0B: "PHD", 0x2B: "PLD", 0x4B: "PHK", 0x8B: "PHB",
               0xAB: "PLB", 0x89: "BIT", 0xE2: "SEP", 0xC2: "REP"}
        for op, mn in exp.items():
            self.assertEqual(D.OPCODES[op][0], mn, hex(op))

    def test_every_opcode_length(self):
        for op in range(256):
            mn, mode = D.OPCODES[op]
            for m in (0, 1):
                for x in (0, 1):
                    e = 0
                    ins = dec([op] + [0x11] * 5, "e=0,m=%d,x=%d" % (8 if m else 16, 8 if x else 16))
                    if mode == "imm":
                        want = 2 if m else 3
                    elif mode == "immx":
                        want = 2 if x else 3
                    else:
                        want = D.FIXED_LEN[mode]
                    self.assertEqual(ins.length, want, "%02X %s %s" % (op, mn, mode))


class TestModes(unittest.TestCase):
    def check(self, data, text, length, state="e=0,m=8,x=8"):
        ins = dec(data + [0xEA] * 4, state)
        self.assertEqual(ins.length, length, text)
        self.assertEqual(D.fmt_insn(ins, set()), text)

    def test_modes(self):
        self.check([0xA9, 0x12], "LDA #$12", 2)
        self.check([0xA9, 0x34, 0x12], "LDA #$1234", 3, "e=0,m=16,x=8")
        self.check([0xA2, 0x34, 0x12], "LDX #$1234", 3, "e=0,m=8,x=16")
        self.check([0xA0, 0x34, 0x12], "LDY #$1234", 3, "e=0,m=8,x=16")
        self.check([0xA9, 0x34], "LDA #$34", 2, "e=0,m=8,x=16")
        self.check([0xE0, 0x34], "CPX #$34", 2, "e=0,m=16,x=8")
        self.check([0xC0, 0x34], "CPY #$34", 2, "e=0,m=16,x=8")
        self.check([0x89, 0x34, 0x12], "BIT #$1234", 3, "e=0,m=16,x=8")
        self.check([0xE2, 0x30], "SEP #$30", 2, "e=0,m=16,x=16")
        self.check([0xC2, 0x30], "REP #$30", 2, "e=0,m=16,x=16")
        self.check([0xA5, 0x12], "LDA $12", 2)
        self.check([0xB5, 0x12], "LDA $12,X", 2)
        self.check([0xB6, 0x12], "LDX $12,Y", 2)
        self.check([0xAD, 0x34, 0x12], "LDA $1234", 3)
        self.check([0xBD, 0x34, 0x12], "LDA $1234,X", 3)
        self.check([0xB9, 0x34, 0x12], "LDA $1234,Y", 3)
        self.check([0xAF, 0x56, 0x34, 0x12], "LDA $123456", 4)
        self.check([0xBF, 0x56, 0x34, 0x12], "LDA $123456,X", 4)
        self.check([0xA1, 0x12], "LDA ($12,X)", 2)
        self.check([0xB1, 0x12], "LDA ($12),Y", 2)
        self.check([0xB2, 0x12], "LDA ($12)", 2)
        self.check([0xA7, 0x12], "LDA [$12]", 2)
        self.check([0xB7, 0x12], "LDA [$12],Y", 2)
        self.check([0xA3, 0x12], "LDA $12,S", 2)
        self.check([0xB3, 0x12], "LDA ($12,S),Y", 2)
        self.check([0x6C, 0x34, 0x12], "JMP ($1234)", 3)
        self.check([0x7C, 0x34, 0x12], "JMP ($1234,X)", 3)
        self.check([0xFC, 0x34, 0x12], "JSR ($1234,X)", 3)
        self.check([0xDC, 0x34, 0x12], "JML [$1234]", 3)
        self.check([0xF4, 0x34, 0x12], "PEA $1234", 3)
        self.check([0xD4, 0x12], "PEI ($12)", 2)
        self.check([0x42, 0x77], "WDM #$77", 2)
        self.check([0x02, 0x55], "COP #$55", 2)
        self.check([0x00, 0x01], "BRK #$01", 2)
        self.check([0x0A], "ASL A", 1)
        self.check([0xEA], "NOP", 1)
        self.check([0x6B], "RTL", 1)

    def test_jumps_text_and_targets(self):
        i = dec([0x4C, 0x34, 0x12])
        self.assertEqual(i.operand_target, 0x1234)
        i = dec([0x5C, 0x56, 0x34, 0x12])
        self.assertEqual(i.operand_target, 0x123456)
        i = dec([0x22, 0x56, 0x34, 0x12])
        self.assertEqual((i.mnemonic, i.flow, i.operand_target), ("JSL", "call", 0x123456))


class TestBlockMove(unittest.TestCase):
    def test_mvn_operand_order(self):
        # bytes: opcode, DEST bank, SRC bank
        i = dec([0x54, 0xAA, 0xBB])
        self.assertEqual((i.dest_bank, i.src_bank), (0xAA, 0xBB))
        self.assertEqual(D.fmt_insn(i, set()), "MVN $BB,$AA")   # assembler order src,dest
        j = dec([0x44, 0x01, 0x02])
        self.assertEqual((j.mnemonic, j.dest_bank, j.src_bank), ("MVP", 1, 2))
        self.assertEqual(D.fmt_insn(j, set()), "MVP $02,$01")
        self.assertEqual(j.length, 3)


class TestBranchTargets(unittest.TestCase):
    def test_bne_forward_backward(self):
        self.assertEqual(dec([0xD0, 0x05]).operand_target, 0x8007)
        self.assertEqual(dec([0xD0, 0xFE]).operand_target, 0x8000)
        self.assertEqual(dec([0xD0, 0x80]).operand_target, 0x8002 - 128)

    def test_brl(self):
        i = dec([0x82, 0x10, 0x00])
        self.assertEqual(i.operand_target, 0x8013)
        i = dec([0x82, 0xFD, 0xFF])
        self.assertEqual(i.operand_target, 0x8000)
        self.assertEqual(i.flow, "jump")

    def test_wraparound_in_bank(self):
        i = dec([0x82, 0xFF, 0x7F], addr=0x8000)   # +$7FFF
        self.assertEqual(i.operand_target, (0x8003 + 0x7FFF) & 0xFFFF)
        i = dec([0x80, 0x20, 0, 0], addr=0xFFF0, load=0xFFF0)
        self.assertEqual(i.operand_target, 0x0012)        # wraps, stays in bank 0
        i = dec([0x82, 0x00, 0x01], addr=0xFFFD, load=0xFFFD)
        self.assertEqual(i.operand_target, 0x0100)
        i = dec([0x82, 0xF0, 0xFF], addr=0x0000, load=0x0000)
        self.assertEqual(i.operand_target, 0xFFF3)

    def test_per_target_not_followed(self):
        d = run([0x62, 0x02, 0x00, 0x60, 0xEA, 0xEA], [(0x8000, "e=1")])
        self.assertNotIn(0x8005, d.insns)
        self.assertIn(0x8005, d.xrefs)


class TestStateTracking(unittest.TestCase):
    def test_rep_sep_widths(self):
        code = [0xC2, 0x20,            # REP #$20
                0xA9, 0x34, 0x12,      # LDA #$1234
                0xE2, 0x20,            # SEP #$20
                0xA9, 0x12,            # LDA #$12
                0xC2, 0x10,            # REP #$10
                0xA2, 0x34, 0x12,      # LDX #$1234
                0xE2, 0x10,            # SEP #$10
                0xA2, 0x12,            # LDX #$12
                0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")])
        a = sorted(d.insns)
        self.assertEqual(a, [0x8000, 0x8002, 0x8005, 0x8007, 0x8009, 0x800B,
                             0x800E, 0x8010, 0x8012])
        self.assertEqual(d.insns[0x8002].length, 3)
        self.assertEqual(d.insns[0x8007].length, 2)
        self.assertEqual(d.insns[0x800B].length, 3)
        self.assertEqual(d.insns[0x8010].length, 2)
        self.assertEqual(d.stops, [])

    def test_xce_forces_8bit(self):
        # CLC XCE -> native with m8 x8 ; REP #$30 ; LDA #16 ; SEC XCE -> LDA #8
        code = [0x18, 0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12, 0x38, 0xFB, 0xA9, 0x56, 0x60]
        d = run(code, [(0x8000, "e=1")])
        self.assertEqual(d.insns[0x8004].length, 3)
        self.assertEqual(d.insns[0x8004].state_in[:3], (0, 0, 0))
        self.assertEqual(d.insns[0x8009].length, 2)
        self.assertEqual(d.insns[0x8009].state_in[:3], (1, 1, 1))
        self.assertEqual(d.insns[0x8002].state_in[:3], (0, 1, 1))
        self.assertEqual(d.stops, [])

    def test_emulation_ignores_rep(self):
        d = run([0xC2, 0x30, 0xA9, 0x01, 0x60], [(0x8000, "e=1")])
        self.assertEqual(d.insns[0x8002].length, 2)

    def test_unknown_e_entry_stops(self):
        d = run([0xA9, 0x01, 0x60], [(0x8000, "")])
        self.assertEqual(len(d.stops), 1)
        self.assertEqual(d.stops[0]["addr"], 0x8000)
        self.assertEqual(d.insns, {})

    def test_sep_with_unknown_e_gives_8bit(self):
        d = run([0xE2, 0x20, 0xA9, 0x01, 0x60], [(0x8000, "")])
        self.assertEqual(d.insns[0x8002].length, 2)

    def test_plp_unknown_and_stop(self):
        code = [0x08, 0xC2, 0x30, 0x28, 0xA9, 0x01, 0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")])
        self.assertEqual(len(d.stops), 1)
        self.assertEqual(d.stops[0]["addr"], 0x8004)
        self.assertEqual(d.insns[0x8003].state_out[:3], (0, None, None))
        self.assertNotIn(0x8004, d.insns)

    def test_plp_forced_by_user(self):
        code = [0x08, 0xC2, 0x30, 0x28, 0xA9, 0x01, 0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")], forced={0x8004: "e=0,m=8,x=8"})
        self.assertEqual(d.stops, [])
        self.assertEqual(d.insns[0x8004].length, 2)
        self.assertEqual(len(d.forced_used), 1)

    def test_plp_in_emulation_is_8bit(self):
        d = run([0x28, 0xA9, 0x01, 0x60], [(0x8000, "e=1")])
        self.assertEqual(d.stops, [])

    def test_track_php_pairs(self):
        code = [0x08, 0xC2, 0x30, 0x28, 0xA9, 0x01, 0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")], track=True)
        self.assertEqual(d.stops, [])
        self.assertEqual(d.insns[0x8004].length, 2)
        self.assertEqual(d.insns[0x8004].state_in[:3], (0, 1, 1))

    def test_track_php_with_other_pushes(self):
        code = [0x08, 0x48, 0xC2, 0x30, 0x68, 0x28, 0xA9, 0x01, 0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")], track=True)
        self.assertEqual(d.stops, [])
        code = [0x08, 0x48, 0xC2, 0x30, 0x28, 0xA9, 0x01, 0x60]   # PLP pops a PHA item
        d = run(code, [(0x8000, "e=0,m=8,x=8")], track=True)
        self.assertEqual(len(d.stops), 1)

    def test_rti_unknown(self):
        self.assertEqual(D.apply_state("RTI", None, st("e=0,m=8,x=8"))[:3], (0, None, None))


class TestConflicts(unittest.TestCase):
    def test_conflict_width_dependent(self):
        d = run([0xA9, 0x01, 0x60, 0x00], [(0x8000, "e=0,m=8,x=8"), (0x8000, "e=0,m=16,x=8")])
        self.assertEqual(len(d.conflicts), 1)
        self.assertEqual(d.conflicts[0]["kind"], "width_dependent")
        self.assertEqual(d.insns[0x8000].length, 2)      # first wins

    def test_conflict_via_branch(self):
        # BEQ to LDA #imm; fallthrough path executed REP #$20 first
        code = [0xF0, 0x03,            # 8000 BEQ 8005
                0xC2, 0x20,            # 8002 REP #$20
                0xEA,                  # 8004 NOP
                0xA9, 0x01, 0x00,      # 8005 LDA #
                0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")])
        self.assertEqual([c["addr"] for c in d.conflicts], [0x8005])

    def test_benign_merge(self):
        # two paths with different X reach REP #$10: result identical
        code = [0xF0, 0x03,            # BEQ 8005
                0xC2, 0x10,            # REP #$10 (x16)
                0xEA,                  # NOP  8004
                0xC2, 0x10,            # 8005 REP #$10
                0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")])
        self.assertEqual(d.conflicts, [])
        # path A reaches 8005 with x8; path B (fallthrough) with x16 -> benign
        self.assertEqual(len(d.benign), 1)

    def test_overlap_reported(self):
        # 8000: LDA #$4C(2 bytes) ; BRA into the middle of it
        code = [0xA9, 0x4C, 0x80, 0xFD]
        d = run(code, [(0x8000, "e=1")])
        # BRA at 8002 -> 8002+2-3 = 8001 : inside LDA -> overlap
        self.assertEqual(len(d.overlaps), 1)

    def test_same_state_no_conflict(self):
        code = [0xD0, 0x00, 0x60]
        d = run(code, [(0x8000, "e=1")])
        self.assertEqual(d.conflicts, [])


class TestFlow(unittest.TestCase):
    def test_jsr_call_and_fallthrough(self):
        code = [0x20, 0x05, 0x80, 0x60, 0xEA, 0xEA, 0x60]
        d = run(code, [(0x8000, "e=1")])
        self.assertEqual(sorted(d.insns), [0x8000, 0x8003, 0x8005, 0x8006])
        self.assertIn(0x8005, d.labels)

    def test_rts_ends(self):
        d = run([0x60, 0xEA, 0xEA], [(0x8000, "e=1")])
        self.assertEqual(sorted(d.insns), [0x8000])

    def test_jmp_indirect_unresolved(self):
        d = run([0x6C, 0x00, 0x30, 0xEA], [(0x8000, "e=1")])
        self.assertEqual(d.unresolved, [0x8000])
        self.assertEqual(sorted(d.insns), [0x8000])

    def test_jsr_indexed_indirect_unresolved_and_falls_through(self):
        d = run([0xFC, 0x00, 0x30, 0x60], [(0x8000, "e=1")])
        self.assertEqual(d.unresolved, [0x8000])
        self.assertEqual(sorted(d.insns), [0x8000, 0x8003])

    def test_jml_other_bank_external(self):
        d = run([0x5C, 0x00, 0x00, 0x02], [(0x8000, "e=1")])
        self.assertEqual(list(d.external), [0x020000])

    def test_jsl_same_bank_followed(self):
        d = run([0x22, 0x05, 0x80, 0x00, 0x60, 0x6B], [(0x8000, "e=1")])
        self.assertIn(0x8005, d.insns)
        self.assertEqual(d.insns[0x8005].mnemonic, "RTL")

    def test_stp_brk_end(self):
        for op in (0xDB, 0x00, 0x40):
            d = run([op, 0x00, 0xEA], [(0x8000, "e=1")])
            self.assertEqual(len(d.insns), 1)

    def test_bra_not_fallthrough(self):
        d = run([0x80, 0x01, 0xEA, 0x60], [(0x8000, "e=1")])
        self.assertEqual(sorted(d.insns), [0x8000, 0x8003])

    def test_unreached_is_data_and_asm_rebuilds(self):
        code = [0xD0, 0x02, 0x60, 0x00, 0xDE, 0xAD, 0xBE, 0xEF] + list(range(40))
        d = run(code, [(0x8000, "e=1")])
        asm = d.to_asm("t")
        got = []
        for line in asm.splitlines():
            m = re.match(r"\s+\.byte ([^/]*?)\s*//", line)
            if m:
                got += [int(t.strip()[1:], 16) for t in m.group(1).split(",")]
        self.assertEqual(got, code)
        s = d.summary()
        self.assertEqual(s["code_bytes"] + s["data_bytes"], len(code))

    def test_entry_outside_image(self):
        d = run([0x60], [(0x9000, "e=1")])
        self.assertEqual(len(d.out_of_image), 1)

    def test_truncated_instruction_stop(self):
        d = run([0xAD, 0x00], [(0x8000, "e=1")])
        self.assertEqual(len(d.stops), 1)

    def test_call_state_note(self):
        # callee does REP #$30 and returns -> note "changed"
        code = [0x20, 0x04, 0x80, 0x60, 0xC2, 0x30, 0x60]
        d = run(code, [(0x8000, "e=0,m=8,x=8")])
        self.assertEqual(len(d.call_notes), 1)
        self.assertEqual(d.call_notes[0]["kind"], "changed")


class TestAliasAndMisc(unittest.TestCase):
    def test_alias_relocated_code(self):
        # code at file 8005.. runs at virtual 0100.. ; JMP $0100 from 8000
        code = [0x4C, 0x00, 0x01, 0xEA, 0xEA, 0xA9, 0x01, 0x60]
        d = run(code, [(0x8000, "e=1")], aliases=[(0x0100, 0x8005, 3)])
        self.assertIn(0x0100, d.insns)
        self.assertEqual(d.insns[0x0100].phys, 5)
        asm = d.to_asm("t")
        self.assertIn(".label L_0100 = $0100", asm)

    def test_declared_stop(self):
        d = run([0xEA, 0xEA, 0x60], [(0x8000, "e=1")], stops=[0x8001])
        self.assertEqual(sorted(d.insns), [0x8000])

    def test_io_names(self):
        self.assertEqual(D.io_name(0xD020), "VIC_EXTCOL")
        self.assertTrue(D.io_name(0xD07B).startswith("SCPU_D07B"))
        self.assertTrue(D.io_name(0xD0B3).startswith("SCPU_"))
        self.assertEqual(D.io_name(0xDC0D), "CIA1_ICR")
        self.assertEqual(D.io_name(0xD418), "SID_MODE_VOL")
        self.assertIsNone(D.io_name(0x1234))

    def test_io_name_in_asm(self):
        d = run([0x8D, 0x7B, 0xD0, 0x60], [(0x8000, "e=1")])
        self.assertIn("SCPU_D07B", d.to_asm("t"))

    def test_self_modifying_detected(self):
        code = [0x8D, 0x05, 0x80, 0xEA, 0x4C, 0x00, 0x00]  # STA $8005 -> operand of JMP
        d = run(code, [(0x8000, "e=1")])
        self.assertEqual(len(d.selfmod), 1)
        self.assertEqual((d.selfmod[0]["instruction"], d.selfmod[0]["operand_byte"]), (0x8004, 1))

    def test_xrefs_branch(self):
        d = run([0xD0, 0x00, 0x60], [(0x8000, "e=1")])
        self.assertEqual(d.xrefs[0x8002][0]["kind"], "branch")

    def test_json_roundtrip(self):
        import json
        d = run([0xC2, 0x30, 0xA9, 0x34, 0x12, 0x60], [(0x8000, "e=0,m=8,x=8")])
        j = json.loads(json.dumps(d.to_json({})))
        self.assertEqual(j["instructions"][1]["text"], "LDA #$1234")
        self.assertEqual(j["instructions"][1]["state_in"], {"e": 0, "m": 16, "x": 16})


if __name__ == "__main__":
    unittest.main(verbosity=1)
