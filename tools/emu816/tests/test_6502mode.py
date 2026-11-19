import unittest
from .helpers import *
from emu816.cpu import IllegalOpcode6502, JmpIndirectBug, CPU
from emu816.opcodes import LEGAL_6502, TABLE


class Rejection(unittest.TestCase):
    def test_every_non_legal_opcode_rejected(self):
        bad = [op for op in range(256) if op not in LEGAL_6502]
        self.assertEqual(len(bad), 105)
        for op in bad:
            cpu = nmos([op, 0, 0, 0])
            with self.assertRaises(IllegalOpcode6502, msg="%02X" % op):
                cpu.step()

    def test_every_legal_opcode_accepted(self):
        for op in sorted(LEGAL_6502):
            cpu = nmos([op, 0x10, 0x00, 0x00])
            cpu.S = 0x01F0
            try:
                cpu.step()
            except IllegalOpcode6502:
                self.fail("%02X" % op)

    def test_specific_65c02_and_816_ops(self):
        for op in (0x64, 0x80, 0xDA, 0x1A, 0x89, 0xEB, 0xFB, 0xC2, 0xE2, 0x5C, 0x22, 0x54, 0x92, 0x7C):
            with self.assertRaises(IllegalOpcode6502):
                nmos([op, 0, 0, 0]).step()

    def test_undocumented_nmos_rejected(self):
        for op in (0xA7, 0x03, 0x0B, 0xEB, 0x1A, 0x80, 0xFF):
            with self.assertRaises(IllegalOpcode6502):
                nmos([op, 0, 0]).step()


class JmpIndirect(unittest.TestCase):
    def test_detected(self):
        cpu = nmos([0x6C, 0xFF, 0x30])
        with self.assertRaises(JmpIndirectBug):
            cpu.step()

    def test_nmos_bug_semantics_when_not_raising(self):
        cpu = nmos([0x6C, 0xFF, 0x30]); cpu.raise_on_jmp_bug = False
        cpu.mem.poke(0x30FF, 0x34); cpu.mem.poke(0x3000, 0x12); cpu.mem.poke(0x3100, 0x99); cpu.step()
        self.assertEqual(cpu.PC, 0x1234)                              # high byte from $3000, not $3100
        self.assertEqual(cpu.events[0][0], "jmp_ind_page_bug")

    def test_816_emulation_has_no_bug(self):
        cpu = emu([0x6C, 0xFF, 0x30]); cpu.mem.poke(0x30FF, 0x34); cpu.mem.poke(0x3000, 0x12); cpu.mem.poke(0x3100, 0x99); cpu.step()
        self.assertEqual(cpu.PC, 0x9934)

    def test_normal_case_ok(self):
        cpu = nmos([0x6C, 0x10, 0x30]); cpu.mem.load(0x3010, [0x00, 0x40]); cpu.step(); self.assertEqual(cpu.PC, 0x4000)


class Differences(unittest.TestCase):
    def test_brk_does_not_clear_D_on_nmos(self):
        cpu = nmos([0x00, 0x00]); cpu.mem.load(0xFFFE, [0, 0x80]); cpu.set_state(D_flag=1); cpu.step()
        self.assertEqual(cpu.fD, 1)
        cpu = emu([0x00, 0x00]); cpu.mem.load(0xFFFE, [0, 0x80]); cpu.set_state(D_flag=1); cpu.step()
        self.assertEqual(cpu.fD, 0)

    def test_rmw_double_write_nmos(self):
        for op, name in ((0xEE, "inc"), (0xCE, "dec"), (0x0E, "asl"), (0x4E, "lsr"), (0x2E, "rol"), (0x6E, "ror")):
            io = IORegion(0xD000, 0xD0FF, default=0x41)
            cpu = nmos([op, 0x00, 0xD0], CF=0); io.attach(cpu.mem); cpu.step()
            self.assertEqual(len(io.writes), 2, name)
            self.assertEqual(io.writes[0], (0xD000, 0x41), name)            # old value first
            self.assertNotEqual(io.writes[1][1], 0x41, name)
        io = IORegion(0xD000, 0xD0FF, default=0x41)
        cpu = emu([0xEE, 0x00, 0xD0]); io.attach(cpu.mem); cpu.step()
        self.assertEqual(io.writes, [(0xD000, 0x42)])                       # 65816 E=1 : single write

    def test_rmw_zp_and_indexed_double_write(self):
        cpu = nmos([0xF6, 0x10], xr=1); cpu.mem.write_log = []; cpu.mem.poke(0x11, 5); cpu.step()
        self.assertEqual(cpu.mem.write_log, [(0x11, 5), (0x11, 6)])

    def test_abs_index_wraps_at_64k_on_nmos_not_on_816(self):
        cpu = nmos([0xBD, 0xFF, 0xFF], xr=2); cpu.mem.poke(0x0001, 0x66); cpu.mem.poke(0x010001, 0x77); cpu.step()
        self.assertEqual(cpu.A, 0x66)
        cpu = emu([0xBD, 0xFF, 0xFF], xr=2); cpu.mem.poke(0x0001, 0x66); cpu.mem.poke(0x010001, 0x77); cpu.step()
        self.assertEqual(cpu.A, 0x77)
        cpu = nmos([0xB1, 0x10], yr=2); cpu.mem.load(0x10, [0xFF, 0xFF]); cpu.mem.poke(0x0001, 0x61); cpu.step()
        self.assertEqual(cpu.A, 0x61)

    def test_zp_indexed_page_wrap(self):
        cpu = nmos([0xB5, 0xF0], xr=0x20); cpu.mem.poke(0x10, 0x4C); cpu.step(); self.assertEqual(cpu.A, 0x4C)
        cpu = nmos([0xA1, 0xFE], xr=0); cpu.mem.poke(0xFE, 0x00); cpu.mem.poke(0xFF, 0x20); cpu.mem.poke(0x2000, 0x31); cpu.step()
        self.assertEqual(cpu.A, 0x31)

    def test_decimal_nmos_adc_vectors(self):
        # hand-evaluated from Bruce Clark "Decimal Mode" 6502 sequences 1 (Z) and 2 (N,V): A,B,C -> A,N,V,Z,C
        vec = [(0x79, 0x00, 1, 0x80, 1, 1, 0, 0), (0x24, 0x56, 0, 0x80, 1, 1, 0, 0),
               (0x93, 0x82, 0, 0x75, 0, 1, 0, 1), (0x89, 0x76, 0, 0x65, 0, 0, 0, 1),
               (0x89, 0x76, 1, 0x66, 0, 0, 1, 1), (0x80, 0x80, 0, 0x60, 0, 1, 1, 1),
               (0x80, 0xFA, 0, 0xE0, 1, 0, 0, 1), (0x2F, 0x4F, 0, 0x74, 0, 0, 0, 0),
               (0x6F, 0x00, 1, 0x76, 0, 0, 0, 0)]
        for a, b, c, ra, n, v, z, cf in vec:
            cpu = nmos([0x69, b], a=a, CF=c, D_flag=1); cpu.step()
            self.assertEqual((cpu.A, cpu.fN, cpu.fV, cpu.fZ, cpu.fC), (ra, n, v, z, cf), (hex(a), hex(b), c))

    def test_decimal_nmos_adc_Z_from_binary(self):
        cpu = nmos([0x69, 0x01], a=0x99, D_flag=1); cpu.step()      # decimal $00 but binary $9A -> Z=0 on NMOS
        self.assertEqual((cpu.A, cpu.fC, cpu.fZ), (0x00, 1, 0))
        cpu = emu([0x69, 0x01], a=0x99, D_flag=1); cpu.step()       # 65816: Z from decimal result
        self.assertEqual((cpu.A, cpu.fC, cpu.fZ), (0x00, 1, 1))

    def test_decimal_nmos_sbc_flags_are_binary(self):
        cpu = nmos([0xE9, 0x01], a=0x00, CF=1, D_flag=1); cpu.step()
        self.assertEqual((cpu.A, cpu.fC, cpu.fN, cpu.fZ, cpu.fV), (0x99, 0, 1, 0, 0))   # N from binary $FF
        cpu = nmos([0xE9, 0x40], a=0x40, CF=1, D_flag=1); cpu.step()
        self.assertEqual((cpu.A, cpu.fZ, cpu.fC), (0x00, 1, 1))
        cpu = emu([0xE9, 0x01], a=0x00, CF=1, D_flag=1); cpu.step()
        self.assertEqual((cpu.A, cpu.fC, cpu.fN), (0x99, 0, 1))

    def test_decimal_valid_bcd_a_and_c_agree_nmos_vs_816(self):
        for a in range(0, 100, 7):
            for b in range(0, 100, 11):
                for c in (0, 1):
                    A, B = (a // 10) << 4 | a % 10, (b // 10) << 4 | b % 10
                    r = []
                    for mk in (nmos, emu):
                        cpu = mk([0x69, B], a=A, CF=c, D_flag=1); cpu.step(); r.append((cpu.A, cpu.fC))
                        cpu = mk([0xE9, B], a=A, CF=c, D_flag=1); cpu.step(); r.append((cpu.A, cpu.fC))
                    self.assertEqual(r[0], r[2]); self.assertEqual(r[1], r[3])


class Misc6502(unittest.TestCase):
    def test_small_program_runs(self):
        # LDX #5 / LDA #0 / loop: CLC ADC #3 / DEX / BNE loop / RTS  -> A = 15
        cpu = nmos([0xA2, 0x05, 0xA9, 0x00, 0x18, 0x69, 0x03, 0xCA, 0xD0, 0xFA, 0x60])
        r = cpu.run(CODE)
        self.assertEqual((r.reason, cpu.A), ("sentinel", 15))

    def test_trace_tail_formats(self):
        cpu = nmos([0xA9, 0x01, 0x60]); cpu.run(CODE)
        t = cpu.format_trace(4)
        self.assertIn("LDA #$01", t[0]); self.assertIn("RTS", t[1])
