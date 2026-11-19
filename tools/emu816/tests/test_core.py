import unittest
from .helpers import *
from emu816.opcodes import TABLE, MODE_LEN, LEGAL_6502


class KnownAnswerFlags(unittest.TestCase):
    """Known-answer vectors derived by hand from the datasheet/programming-manual flag definitions."""

    def test_adc_binary_8(self):
        # (A, operand, carry_in) -> (A, N, V, Z, C)   binary, M=1
        vec = [(0x50, 0x50, 0, 0xA0, 1, 1, 0, 0), (0xFF, 0x01, 0, 0x00, 0, 0, 1, 1),
               (0x7F, 0x00, 1, 0x80, 1, 1, 0, 0), (0x80, 0x80, 0, 0x00, 0, 1, 1, 1),
               (0x00, 0x00, 1, 0x01, 0, 0, 0, 0), (0xD0, 0x90, 0, 0x60, 0, 1, 0, 1)]
        for a, d, c, ra, n, v, z, cf in vec:
            cpu = native([0x69, d], a=a, CF=c)
            cpu.step()
            self.assertEqual((cpu.A, cpu.fN, cpu.fV, cpu.fZ, cpu.fC), (ra, n, v, z, cf), (a, d, c))

    def test_adc_binary_16(self):
        vec = [(0x7FFF, 0x0001, 0, 0x8000, 1, 1, 0, 0), (0xFFFF, 0x0001, 0, 0, 0, 0, 1, 1),
               (0x1234, 0x4321, 1, 0x5556, 0, 0, 0, 0)]
        for a, d, c, ra, n, v, z, cf in vec:
            cpu = native([0x69, d & 0xFF, d >> 8], m=0, a=a, CF=c)
            cpu.step()
            self.assertEqual((cpu.C, cpu.fN, cpu.fV, cpu.fZ, cpu.fC), (ra, n, v, z, cf))

    def test_adc_8bit_keeps_B(self):
        cpu = native([0x69, 0x01], a=0xAB7F)
        cpu.step()
        self.assertEqual(cpu.C, 0xAB80)

    def test_sbc_binary(self):
        vec = [(0x00, 0x01, 1, 0xFF, 1, 0, 0, 0), (0x80, 0x01, 1, 0x7F, 0, 1, 0, 1),
               (0x05, 0x05, 1, 0x00, 0, 0, 1, 1), (0x05, 0x05, 0, 0xFF, 1, 0, 0, 0)]
        for a, d, c, ra, n, v, z, cf in vec:
            cpu = native([0xE9, d], a=a, CF=c)
            cpu.step()
            self.assertEqual((cpu.A, cpu.fN, cpu.fV, cpu.fZ, cpu.fC), (ra, n, v, z, cf), (a, d, c))
        cpu = native([0xE9, 0x01, 0x00], m=0, a=0x0000, CF=1)
        cpu.step()
        self.assertEqual((cpu.C, cpu.fN, cpu.fC), (0xFFFF, 1, 0))

    def test_decimal_8_vectors(self):
        vec = [("ADC", 0x09, 0x01, 0, 0x10, 0), ("ADC", 0x99, 0x01, 0, 0x00, 1),
               ("ADC", 0x58, 0x46, 1, 0x05, 1), ("ADC", 0x12, 0x34, 0, 0x46, 0),
               ("SBC", 0x46, 0x12, 1, 0x34, 1), ("SBC", 0x40, 0x13, 1, 0x27, 1),
               ("SBC", 0x00, 0x01, 1, 0x99, 0), ("SBC", 0x32, 0x02, 0, 0x29, 1)]
        for mn, a, d, c, ra, cf in vec:
            cpu = native([0x69 if mn == "ADC" else 0xE9, d], a=a, CF=c, D_flag=1)
            cpu.step()
            self.assertEqual((cpu.A, cpu.fC), (ra, cf), (mn, hex(a), hex(d), c))
            self.assertEqual(cpu.fZ, int(ra == 0))                       # 65816: Z from decimal result
            self.assertEqual(cpu.fN, ra >> 7)

    def test_decimal_flag_vectors(self):
        # derived from the valid-flag decimal algorithm of the 65816 (N,Z from result, V from pre-correction top nibble)
        cpu = native([0x69, 0x50], a=0x50, D_flag=1); cpu.step()
        self.assertEqual((cpu.A, cpu.fC, cpu.fV, cpu.fN, cpu.fZ), (0x00, 1, 1, 0, 1))
        cpu = native([0x69, 0x01], a=0x79, CF=1, D_flag=1); cpu.step()
        self.assertEqual((cpu.A, cpu.fC, cpu.fV, cpu.fN, cpu.fZ), (0x81, 0, 1, 1, 0))

    def test_decimal_8_sweep_vs_integer(self):
        for a in range(100):
            for b in range(100):
                for c in (0, 1):
                    A, B = (a // 10) << 4 | a % 10, (b // 10) << 4 | b % 10
                    cpu = native([0x69, B], a=A, CF=c, D_flag=1); cpu.step()
                    s = a + b + c
                    self.assertEqual((cpu.A, cpu.fC), ((((s % 100) // 10) << 4) | (s % 10), int(s > 99)))
                    cpu = native([0xE9, B], a=A, CF=c, D_flag=1); cpu.step()
                    d = a - b - (1 - c)
                    r = d % 100
                    self.assertEqual((cpu.A, cpu.fC), (((r // 10) << 4) | (r % 10), int(d >= 0)))

    def test_decimal_16_vectors(self):
        vec = [("ADC", 0x1234, 0x4321, 0, 0x5555, 0), ("ADC", 0x9999, 0x0001, 0, 0x0000, 1),
               ("ADC", 0x0999, 0x0001, 0, 0x1000, 0), ("SBC", 0x0100, 0x0001, 1, 0x0099, 1),
               ("SBC", 0x0000, 0x0001, 1, 0x9999, 0), ("SBC", 0x5000, 0x2345, 1, 0x2655, 1)]
        for mn, a, d, c, ra, cf in vec:
            cpu = native([0x69 if mn == "ADC" else 0xE9, d & 0xFF, d >> 8], m=0, a=a, CF=c, D_flag=1)
            cpu.step()
            self.assertEqual((cpu.C, cpu.fC, cpu.fZ, cpu.fN), (ra, cf, int(ra == 0), ra >> 15), (mn, hex(a), hex(d)))

    def test_decimal_16_sweep_sample(self):
        import random
        rnd = random.Random(7)
        for _ in range(1500):
            a, b, c = rnd.randrange(10000), rnd.randrange(10000), rnd.randrange(2)
            bcd = lambda v: int("%04d" % v, 16)
            cpu = native([0x69, bcd(b) & 0xFF, bcd(b) >> 8], m=0, a=bcd(a), CF=c, D_flag=1); cpu.step()
            s = a + b + c
            self.assertEqual((cpu.C, cpu.fC), (bcd(s % 10000), int(s > 9999)))
            cpu = native([0xE9, bcd(b) & 0xFF, bcd(b) >> 8], m=0, a=bcd(a), CF=c, D_flag=1); cpu.step()
            d = a - b - (1 - c)
            self.assertEqual((cpu.C, cpu.fC), (bcd(d % 10000), int(d >= 0)))


class Registers(unittest.TestCase):
    def test_rep_sep_xce(self):
        cpu = emu([0x18, 0xFB, 0xC2, 0x30, 0xE2, 0x10, 0x38, 0xFB])   # CLC XCE REP SEP SEC XCE
        steps(cpu, 2)
        self.assertEqual((cpu.E, cpu.fC, cpu.fM, cpu.fX), (0, 1, 1, 1))   # old E=1 went to C
        steps(cpu)
        self.assertEqual((cpu.fM, cpu.fX), (0, 0))
        cpu.X = 0x1234; cpu.Y = 0xABCD
        steps(cpu)                                                    # SEP #$10 clears X/Y high bytes
        self.assertEqual((cpu.fX, cpu.X, cpu.Y), (1, 0x34, 0xCD))
        steps(cpu, 2)                                                 # SEC XCE -> back to emulation
        self.assertEqual((cpu.E, cpu.fM, cpu.fX, cpu.S >> 8), (1, 1, 1, 1))

    def test_emulation_mx_cannot_be_cleared(self):
        cpu = emu([0xC2, 0x30])
        cpu.step()
        self.assertEqual((cpu.fM, cpu.fX, cpu.P & 0x30), (1, 1, 0x30))

    def test_xba_flags_from_new_low(self):
        cpu = native([0xEB], m=0, a=0x8001); cpu.step()
        self.assertEqual((cpu.C, cpu.fN, cpu.fZ), (0x0180, 1, 0))
        cpu = native([0xEB], a=0x0080); cpu.step()
        self.assertEqual((cpu.C, cpu.fN, cpu.fZ), (0x8000, 0, 1))

    def test_transfers_width(self):
        cpu = native([0xAA], m=1, x=0, a=0x1234); cpu.step()           # TAX x=0 : 16 bit even with m=1
        self.assertEqual(cpu.X, 0x1234)
        cpu = native([0x8A], m=1, x=0, a=0xAA00, xr=0x1234); cpu.step()  # TXA m=1 : low byte only
        self.assertEqual(cpu.C, 0xAA34)
        cpu = native([0xA8], m=0, x=1, a=0x1234); cpu.step()           # TAY x=1 : 8 bit
        self.assertEqual(cpu.Y, 0x34)
        cpu = native([0x5B], a=0x8000); cpu.step()                      # TCD 16-bit always
        self.assertEqual((cpu.D, cpu.fN), (0x8000, 1))
        cpu = native([0x7B], a=0); cpu.D = 0x1234; cpu.step()
        self.assertEqual(cpu.C, 0x1234)

    def test_tcs_tsx_txs_emulation(self):
        cpu = emu([0x1B, 0xBA, 0xA2, 0x55, 0x9A], a=0x7FA0); cpu.step()
        self.assertEqual(cpu.S, 0x01A0)
        cpu.step(); self.assertEqual(cpu.X, 0xA0)
        steps(cpu, 2); self.assertEqual(cpu.S, 0x0155)

    def test_inc_dec_a_and_index_wrap(self):
        cpu = native([0x1A], a=0x12FF); cpu.step(); self.assertEqual((cpu.C, cpu.fZ), (0x1200, 1))
        cpu = native([0x3A], m=0, a=0x0000); cpu.step(); self.assertEqual((cpu.C, cpu.fN), (0xFFFF, 1))
        cpu = native([0xE8], x=1, xr=0xFF); cpu.step(); self.assertEqual((cpu.X, cpu.fZ), (0, 1))
        cpu = native([0xCA], x=0, xr=0); cpu.step(); self.assertEqual((cpu.X, cpu.fN), (0xFFFF, 1))

    def test_flags_cmp(self):
        cpu = native([0xC9, 0x10], a=0x10); cpu.step(); self.assertEqual(flags(cpu), dict(N=0, V=0, Z=1, C=1))
        cpu = native([0xC9, 0x11], a=0x10); cpu.step(); self.assertEqual(flags(cpu), dict(N=1, V=0, Z=0, C=0))
        cpu = native([0xC9, 0x00, 0x80], m=0, a=0x7FFF); cpu.step(); self.assertEqual(flags(cpu), dict(N=1, V=0, Z=0, C=0))
        cpu = native([0xE0, 0x00, 0x01], x=0, xr=0x0100); cpu.step(); self.assertEqual(flags(cpu), dict(N=0, V=0, Z=1, C=1))

    def test_bit_and_tsb_trb(self):
        cpu = native([0x89, 0x80], a=0x01); cpu.fN = 0; cpu.fV = 1; cpu.step()       # BIT #: Z only
        self.assertEqual((cpu.fZ, cpu.fN, cpu.fV), (1, 0, 1))
        cpu = native([0x24, 0x10], a=0x01); cpu.mem.poke(0x10, 0xC1); cpu.step()
        self.assertEqual((cpu.fZ, cpu.fN, cpu.fV), (0, 1, 1))
        cpu = native([0x04, 0x10], a=0x0F); cpu.mem.poke(0x10, 0xF0); cpu.step()
        self.assertEqual((cpu.mem.peek(0x10), cpu.fZ), (0xFF, 1))
        cpu = native([0x14, 0x10], a=0x0F); cpu.mem.poke(0x10, 0xFF); cpu.step()
        self.assertEqual((cpu.mem.peek(0x10), cpu.fZ), (0xF0, 0))

    def test_shifts_16(self):
        cpu = native([0x0A], m=0, a=0x8001); cpu.step(); self.assertEqual((cpu.C, cpu.fC, cpu.fN), (0x0002, 1, 0))
        cpu = native([0x6A], m=0, a=0x0001, CF=1); cpu.step(); self.assertEqual((cpu.C, cpu.fC, cpu.fN), (0x8000, 1, 1))
        cpu = native([0x4A], a=0x0101); cpu.step(); self.assertEqual((cpu.C, cpu.fC), (0x0100, 1))
        cpu = native([0x2A], a=0x80); cpu.step(); self.assertEqual((cpu.A, cpu.fC, cpu.fZ), (0, 1, 1))


class Addressing(unittest.TestCase):
    def test_dp_with_D_and_wrap(self):
        cpu = native([0xA5, 0x10]); cpu.D = 0x0200; cpu.mem.poke(0x0210, 0x77); cpu.step()
        self.assertEqual(cpu.A, 0x77)
        cpu = native([0xB5, 0xFF], xr=2); cpu.mem.poke(0x0201, 0x11); cpu.mem.poke(0x0101, 0x22); cpu.D = 0x0100
        cpu.step(); self.assertEqual(cpu.A, 0x11)                      # native: D+$FF+2 = $0201 (no page wrap)

    def test_dp_x_wrap_emulation(self):
        cpu = emu([0xB5, 0xFF], xr=2); cpu.mem.poke(0x0001, 0x22); cpu.mem.poke(0x0101, 0x99); cpu.step()
        self.assertEqual(cpu.A, 0x22)                                   # page wrap with DL=0
        cpu = emu([0xB5, 0xFF], xr=2); cpu.D = 0x0100; cpu.mem.poke(0x0101, 0x33); cpu.mem.poke(0x0201, 0x44); cpu.step()
        self.assertEqual(cpu.A, 0x33)                                   # DL=0 : wrap inside D's page
        cpu = emu([0xB5, 0xFF], xr=2); cpu.D = 0x0080; cpu.mem.poke(0x0181, 0x55); cpu.step()
        self.assertEqual(cpu.A, 0x55)                                   # DL!=0 : native rule, no page wrap

    def test_dp_x_native_wrap_bank0(self):
        cpu = native([0xB5, 0xFF], xr=2); cpu.mem.poke(0x0101, 0x42); cpu.step()
        self.assertEqual(cpu.A, 0x42)
        cpu = native([0xB5, 0x10], xr=0x20, x=1); cpu.D = 0xFFF0; cpu.mem.poke(0x0020, 0x6B); cpu.step()
        self.assertEqual(cpu.A, 0x6B)                                   # D+off+X = $10020 -> bank-0 wrap

    def test_dp_indirect_pointer_page_wrap_emulation(self):
        cpu = emu([0xB1, 0xFF], yr=0)   # LDA ($FF),Y
        cpu.mem.poke(0x00FF, 0x00); cpu.mem.poke(0x0000, 0x20); cpu.mem.poke(0x0100, 0x30)
        cpu.mem.poke(0x2000, 0xAA); cpu.mem.poke(0x3000, 0xBB); cpu.step()
        self.assertEqual(cpu.A, 0xAA)                                   # high byte from $0000, not $0100

    def test_dp_indirect_native_no_wrap(self):
        cpu = native([0xB2, 0xFF]); cpu.mem.poke(0x00FF, 0x00); cpu.mem.poke(0x0100, 0x30); cpu.mem.poke(0x3000, 0xBB); cpu.step()
        self.assertEqual(cpu.A, 0xBB)

    def test_long_indirect_Y(self):
        cpu = native([0xB7, 0x20], yr=0x10)
        cpu.mem.load(0x20, [0xF0, 0xFF, 0x05])                          # ptr $05:FFF0
        cpu.mem.poke(0x060000, 0x5A); cpu.step()                         # +$10 carries into bank 6
        self.assertEqual(cpu.A, 0x5A)

    def test_long_indirect_store_and_dp_wrap(self):
        cpu = emu([0x87, 0xFE]); cpu.C = 0x1C
        cpu.mem.poke(0x00FE, 0x00); cpu.mem.poke(0x00FF, 0x40); cpu.mem.poke(0x0000, 0x03)  # third byte wraps to $0000
        cpu.step(); self.assertEqual(cpu.mem.peek(0x034000), 0x1C)

    def test_abs_indexed_bank_carry(self):
        cpu = native([0xBD, 0xFF, 0xFF], xr=1); cpu.mem.poke(0x010000, 0x99); cpu.step()
        self.assertEqual(cpu.A, 0x99)
        cpu = native([0xBD, 0xFF, 0xFF], xr=1); cpu.DB = 0x7F; cpu.mem.poke(0x800000, 0x98); cpu.step()
        self.assertEqual(cpu.A, 0x98)

    def test_data_bank_and_16bit_cross_bank(self):
        cpu = native([0xAD, 0xFF, 0xFF], m=0); cpu.DB = 2; cpu.mem.poke(0x02FFFF, 0x34); cpu.mem.poke(0x030000, 0x12); cpu.step()
        self.assertEqual(cpu.C, 0x1234)

    def test_long_and_long_x(self):
        cpu = native([0xAF, 0x34, 0x12, 0x7E]); cpu.mem.poke(0x7E1234, 0x5D); cpu.step()
        self.assertEqual(cpu.A, 0x5D)
        cpu = native([0xBF, 0xFF, 0xFF, 0x01], xr=1); cpu.mem.poke(0x020000, 0x6E); cpu.step()
        self.assertEqual(cpu.A, 0x6E)
        cpu = native([0x8F, 0x00, 0x00, 0x12], a=0x77); cpu.step(); self.assertEqual(cpu.mem.peek(0x120000), 0x77)

    def test_stack_relative(self):
        cpu = native([0xA3, 0x03]); cpu.S = 0x1FA; cpu.mem.poke(0x01FD, 0x71); cpu.step()
        self.assertEqual(cpu.A, 0x71)
        cpu = native([0xB3, 0x01], yr=3); cpu.S = 0x10; cpu.DB = 1
        cpu.mem.poke(0x11, 0x00); cpu.mem.poke(0x12, 0x80); cpu.mem.poke(0x018003, 0x66); cpu.step()
        self.assertEqual(cpu.A, 0x66)
        cpu = native([0xA3, 0x10]); cpu.S = 0xFFF8; cpu.mem.poke(0x0008, 0x4E); cpu.step()   # wraps in bank 0
        self.assertEqual(cpu.A, 0x4E)

    def test_indx_indy_ind(self):
        cpu = native([0xA1, 0x10], xr=4); cpu.mem.load(0x14, [0x00, 0x30]); cpu.mem.poke(0x3000, 0x21); cpu.step()
        self.assertEqual(cpu.A, 0x21)
        cpu = native([0xB1, 0x10], yr=5); cpu.mem.load(0x10, [0x00, 0x30]); cpu.mem.poke(0x3005, 0x22); cpu.step()
        self.assertEqual(cpu.A, 0x22)
        cpu = native([0x92, 0x10], a=0x5E); cpu.mem.load(0x10, [0x00, 0x30]); cpu.DB = 4; cpu.step()
        self.assertEqual(cpu.mem.peek(0x043000), 0x5E)

    def test_stz_widths(self):
        cpu = native([0x9C, 0x00, 0x20], m=0); cpu.mem.load(0x2000, [0xFF, 0xFF, 0xFF]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x2000, 3), bytes([0, 0, 0xFF]))

    def test_immediate_widths(self):
        cpu = native([0xA9, 0x34, 0x12, 0xA2, 0x78, 0x56], m=0, x=0); steps(cpu, 2)
        self.assertEqual((cpu.C, cpu.X, cpu.PC), (0x1234, 0x5678, CODE + 6))
        cpu = native([0xA9, 0x34, 0xA2, 0x78], m=1, x=1); steps(cpu, 2)
        self.assertEqual(cpu.PC, CODE + 4)

    def test_pc_wraps_in_program_bank(self):
        cpu = native([]); cpu.PB, cpu.PC = 5, 0xFFFF
        cpu.mem.poke(0x05FFFF, 0xA9); cpu.mem.poke(0x050000, 0x33); cpu.step()   # operand fetch wraps in bank 5
        self.assertEqual((cpu.A, cpu.PB, cpu.PC), (0x33, 5, 1))

    def test_branches(self):
        cpu = native([0x80, 0xFE]); cpu.step(); self.assertEqual(cpu.PC, CODE)         # BRA -2 : loop on itself
        cpu = native([0xF0, 0x05], Z=1); cpu.step(); self.assertEqual(cpu.PC, CODE + 7)
        cpu = native([0xD0, 0x05], Z=1); cpu.step(); self.assertEqual(cpu.PC, CODE + 2)
        cpu = native([0x82, 0xFD, 0xFF]); cpu.step(); self.assertEqual(cpu.PC, CODE)  # BRL -3
        cpu = native([]); cpu.PB, cpu.PC = 3, 0xFFFE; cpu.mem.load(0x03FFFE, [0x80, 0x04]); cpu.step()
        self.assertEqual((cpu.PB, cpu.PC), (3, 4))                                      # wraps in bank


class Stack(unittest.TestCase):
    def test_pha_pla_widths(self):
        cpu = native([0x48, 0x68], m=0, a=0x1234); cpu.step()
        self.assertEqual((cpu.S, cpu.mem.peek(0x1FF), cpu.mem.peek(0x1FE)), (0x1FD, 0x12, 0x34))
        cpu.C = 0; cpu.step(); self.assertEqual((cpu.C, cpu.S), (0x1234, 0x1FF))

    def test_phx_pla_mismatch_widths(self):
        cpu = native([0xDA, 0xFA], x=0, xr=0xBEEF); steps(cpu, 2); self.assertEqual(cpu.X, 0xBEEF)

    def test_emulation_stack_wraps_page1(self):
        cpu = emu([0x48], a=0x5A); cpu.S = 0x0100; cpu.step()
        self.assertEqual((cpu.mem.peek(0x0100), cpu.S), (0x5A, 0x01FF))
        cpu = emu([0x68]); cpu.S = 0x01FF; cpu.mem.poke(0x0100, 0x66); cpu.step()
        self.assertEqual((cpu.A, cpu.S), (0x66, 0x0100))

    def test_native_stack_wraps_bank0(self):
        cpu = native([0x48], a=0x5A); cpu.S = 0x0000; cpu.step()
        self.assertEqual((cpu.mem.peek(0), cpu.S), (0x5A, 0xFFFF))

    def test_jsr_rts_jsl_rtl(self):
        cpu = native([0x20, 0x00, 0x20]); cpu.mem.poke(0x2000, 0x60); cpu.step()
        self.assertEqual((cpu.PC, cpu.mem.peek(0x1FF), cpu.mem.peek(0x1FE)), (0x2000, 0x10, 0x02))
        cpu.step(); self.assertEqual((cpu.PC, cpu.S), (CODE + 3, 0x1FF))
        cpu = native([0x22, 0x00, 0x30, 0x04]); cpu.mem.poke(0x043000, 0x6B); cpu.step()
        self.assertEqual((cpu.PB, cpu.PC, cpu.S), (4, 0x3000, 0x1FC))
        self.assertEqual(cpu.mem.dump(0x1FD, 3), bytes([0x03, 0x10, 0x00]))
        cpu.step(); self.assertEqual((cpu.PB, cpu.PC, cpu.S), (0, CODE + 4, 0x1FF))

    def test_jsr_abs_x_and_jmp_variants(self):
        cpu = native([0xFC, 0x00, 0x20], xr=2); cpu.mem.load(0x2002, [0x00, 0x30]); cpu.step()
        self.assertEqual((cpu.PC, cpu.mem.peek(0x1FF), cpu.mem.peek(0x1FE)), (0x3000, 0x10, 0x02))
        cpu = native([0x7C, 0xFF, 0xFF], xr=1); cpu.mem.poke(0x0000, 0x34); cpu.mem.poke(0x0001, 0x12)
        cpu.PB = 0
        cpu.mem.poke(0x00FFFF, 0)  # (a,X) wraps in program bank: reads $0000/$0001 of bank PB
        cpu.mem.poke(0x0000, 0x00); cpu.mem.poke(0x0001, 0x40); cpu.step()
        self.assertEqual(cpu.PC, 0x4000)
        cpu = native([0x6C, 0x00, 0x30]); cpu.PB = 1; cpu.mem.load(0x013000, [0xEE, 0xEE]); cpu.mem.load(0x3000, [0x00, 0x50]);
        cpu.mem.load(0x011000, [0x6C, 0x00, 0x30]); cpu.PC = 0x1000; cpu.step()
        self.assertEqual((cpu.PB, cpu.PC), (1, 0x5000))                 # pointer from bank 0, target stays in PB
        cpu = native([0xDC, 0x00, 0x30]); cpu.mem.load(0x3000, [0x00, 0x60, 0x07]); cpu.step()
        self.assertEqual((cpu.PB, cpu.PC), (7, 0x6000))
        cpu = native([0x5C, 0x00, 0x60, 0x09]); cpu.step(); self.assertEqual((cpu.PB, cpu.PC), (9, 0x6000))

    def test_pea_pei_per(self):
        cpu = native([0xF4, 0x34, 0x12]); cpu.step(); self.assertEqual((cpu.S, cpu.mem.dump(0x1FE, 2)), (0x1FD, bytes([0x34, 0x12])))
        cpu = native([0xD4, 0x20]); cpu.D = 0x100; cpu.mem.load(0x120, [0x78, 0x56]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x1FE, 2), bytes([0x78, 0x56]))
        cpu = native([0x62, 0x10, 0x00]); cpu.step()                      # PER: PC(after operand)+$10
        self.assertEqual(cpu.mem.dump(0x1FE, 2), bytes([(CODE + 3 + 0x10) & 0xFF, (CODE + 3 + 0x10) >> 8]))

    def test_emulation_new_stack_ops_leave_page_but_S_restored(self):
        cpu = emu([0xF4, 0x34, 0x12]); cpu.S = 0x0100; cpu.step()
        self.assertEqual(cpu.mem.peek(0x0100), 0x12); self.assertEqual(cpu.mem.peek(0x00FF), 0x34)
        self.assertEqual(cpu.S, 0x01FE)

    def test_phd_pld_phb_plb_phk(self):
        cpu = native([0x0B, 0x2B]); cpu.D = 0xABCD; cpu.step(); cpu.D = 0; cpu.step()
        self.assertEqual((cpu.D, cpu.fN), (0xABCD, 1))
        cpu = native([0x8B, 0xAB]); cpu.DB = 0x80; cpu.step(); cpu.DB = 0; cpu.step(); self.assertEqual((cpu.DB, cpu.fN), (0x80, 1))
        cpu = native([0x4B]); cpu.PB = 0; cpu.step(); self.assertEqual(cpu.mem.peek(0x1FF), 0)

    def test_php_plp(self):
        cpu = native([0x08, 0x28], m=0, x=1, N=1, CF=1); cpu.step()
        self.assertEqual(cpu.mem.peek(0x1FF), 0x91)        # N, X, C ; M=0
        cpu.fN = 0; cpu.fC = 0; cpu.step(); self.assertEqual((cpu.fN, cpu.fC, cpu.fM, cpu.fX), (1, 1, 0, 1))
        cpu = emu([0x28]); cpu.S = 0x01FE; cpu.mem.poke(0x01FF, 0x00); cpu.step()    # PLP in E=1: M/X forced
        self.assertEqual((cpu.fM, cpu.fX), (1, 1))
        cpu = native([0x28], m=1, x=0, xr=0x1234); cpu.S = 0x1FE; cpu.mem.poke(0x1FF, 0x10); cpu.step()
        self.assertEqual((cpu.fX, cpu.X), (1, 0x34))


class Interrupts(unittest.TestCase):
    def test_brk_native_rti(self):
        cpu = native([0x00, 0xEE]); cpu.mem.load(0xFFE6, [0x00, 0x90]); cpu.PB = 0; cpu.set_state(D_flag=1)
        cpu.step()
        self.assertEqual((cpu.PB, cpu.PC, cpu.fI, cpu.fD), (0, 0x9000, 1, 0))
        self.assertEqual(cpu.mem.peek(0x1FF), 0)                            # PBR pushed first
        self.assertEqual((cpu.mem.peek(0x1FE), cpu.mem.peek(0x1FD)), (0x10, 0x02))   # PC+2
        self.assertEqual(cpu.mem.peek(0x1FC) & 0x08, 0x08)                  # P pushed with D set
        cpu.mem.poke(0x9000, 0x40); cpu.step()
        self.assertEqual((cpu.PB, cpu.PC, cpu.fD, cpu.S), (0, CODE + 2, 1, 0x1FF))

    def test_cop_native_vector(self):
        cpu = native([0x02, 0x11]); cpu.mem.load(0xFFE4, [0x34, 0x12]); cpu.step()
        self.assertEqual((cpu.PC, cpu.fI), (0x1234, 1))

    def test_brk_emulation(self):
        cpu = emu([0x00, 0x77]); cpu.mem.load(0xFFFE, [0x00, 0x80]); cpu.set_state(D_flag=1); cpu.step()
        self.assertEqual((cpu.PC, cpu.fI, cpu.fD, cpu.S), (0x8000, 1, 0, 0x01FC))
        self.assertEqual(cpu.mem.peek(0x1FD) & 0x38, 0x38)                  # B and bit 5 and D as pushed
        self.assertEqual((cpu.mem.peek(0x1FF), cpu.mem.peek(0x1FE)), (0x10, 0x02))
        cpu.mem.poke(0x8000, 0x40); cpu.step()
        self.assertEqual((cpu.PC, cpu.S, cpu.fD), (CODE + 2, 0x01FF, 1))
        self.assertEqual(cpu.PB, 0)

    def test_cop_emulation_vector(self):
        cpu = emu([0x02, 0x00]); cpu.mem.load(0xFFF4, [0x00, 0x70]); cpu.step(); self.assertEqual(cpu.PC, 0x7000)


class BlockMove(unittest.TestCase):
    def test_mvn_basic_operand_order_and_regs(self):
        cpu = native([0x54, 0x02, 0x01], m=0, x=0, a=3, xr=0x2000, yr=0x3000)    # dst=$02 src=$01
        cpu.mem.load(0x012000, [1, 2, 3, 4]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x023000, 5), bytes([1, 2, 3, 4, 0]))
        self.assertEqual((cpu.C, cpu.X, cpu.Y, cpu.DB), (0xFFFF, 0x2004, 0x3004, 2))
        self.assertEqual(cpu.PC, CODE + 3)

    def test_mvp_descending(self):
        cpu = native([0x44, 0x00, 0x00], m=0, x=0, a=2, xr=0x2002, yr=0x3002)
        cpu.mem.load(0x2000, [7, 8, 9]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x3000, 3), bytes([7, 8, 9]))
        self.assertEqual((cpu.X, cpu.Y, cpu.C), (0x1FFF, 0x2FFF, 0xFFFF))

    def test_mvn_overlap_propagates(self):
        cpu = native([0x54, 0, 0], m=0, x=0, a=5, xr=0x2000, yr=0x2001); cpu.mem.poke(0x2000, 0xAB); cpu.step()
        self.assertEqual(cpu.mem.dump(0x2000, 7), bytes([0xAB] * 7))        # byte-by-byte smear
        cpu = native([0x44, 0, 0], m=0, x=0, a=2, xr=0x2000, yr=0x2001); cpu.mem.load(0x2000, [1, 2, 3, 4]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x2000, 5), bytes([0, 1, 3, 4, 0]))   # MVP descends: [2000]->[2001]=1, then [1FFF](0)->[2000], ...  (hand-derived)

    def test_mvp_overlap_forward_safe(self):
        cpu = native([0x44, 0, 0], m=0, x=0, a=3, xr=0x2003, yr=0x2004); cpu.mem.load(0x2000, [1, 2, 3, 4]); cpu.step()
        self.assertEqual(cpu.mem.dump(0x2000, 5), bytes([1, 1, 2, 3, 4]))

    def test_mvn_8bit_index_wraps_and_B_ignored(self):
        cpu = native([0x54, 0, 0], m=1, x=1, a=0x0101, xr=0xFE, yr=0xFF)    # A is 16 bit count even with m=1 : $0101
        cpu.mem.load(0xFE, [5, 6]); cpu.step()
        self.assertEqual(cpu.C, 0xFFFF); self.assertEqual((cpu.X, cpu.Y), ((0xFE + 0x102) & 0xFF, (0xFF + 0x102) & 0xFF))

    def test_mvn_C_zero_is_64k(self):
        cpu = native([0x54, 1, 0], m=0, x=0, a=0xFFFF, xr=0, yr=0); cpu.mem.poke(0, 0x5A); cpu.step()
        self.assertEqual(cpu.C, 0xFFFF); self.assertEqual(cpu.X, 0)       # 65536 bytes, X wrapped fully
        self.assertEqual(cpu.mem.peek(0x010000), 0x5A)


class Decode(unittest.TestCase):
    def test_table_complete_and_lengths(self):
        self.assertEqual(len(TABLE), 256)
        control = {"BRK", "COP", "JMP", "JSR", "JSL", "JML", "RTS", "RTL", "RTI", "WAI", "STP"}
        for op in range(256):
            mn, mode = TABLE[op]
            if mn in control or mode in ("rel", "rell") or mn == "MVN" or mn == "MVP":
                continue
            for (m, x) in ((1, 1), (0, 0)):
                cpu = native([op, 1, 2, 3], m=m, x=x)
                cpu.S = 0x1F0; cpu.D = 0
                cpu.step()
                ln = MODE_LEN[mode]
                if ln is None:
                    ln = 1 if (m if mode == "immM" else x) else 2
                self.assertEqual(cpu.PC - CODE, 1 + ln, "%02X %s %s" % (op, mn, mode))

    def test_mnemonic_spotcheck(self):
        for op, mn in {0x00: "BRK", 0x42: "WDM", 0x44: "MVP", 0x54: "MVN", 0x5C: "JML", 0x6B: "RTL", 0x22: "JSL",
                       0xEB: "XBA", 0xFB: "XCE", 0xDC: "JML", 0xFC: "JSR", 0x7C: "JMP", 0xF4: "PEA",
                       0xD4: "PEI", 0x62: "PER", 0x82: "BRL", 0xAF: "LDA", 0x8F: "STA", 0xCB: "WAI", 0xDB: "STP"}.items():
            self.assertEqual(TABLE[op][0], mn)

    def test_wai_stp_halt(self):
        cpu = native([0xCB]); r = cpu.run(CODE, stop_on=None, max_steps=10); self.assertEqual(r.reason, "WAI")
        cpu = native([0xDB]); r = cpu.run(CODE, stop_on=None, max_steps=10); self.assertEqual(r.reason, "STP")

    def test_run_modes(self):
        cpu = native([0xEA, 0xEA, 0x60]); r = cpu.run(CODE); self.assertEqual((r.reason, r.steps), ("sentinel", 3))
        cpu = native([0xEA, 0xEA, 0x6B]); cpu.mem.poke(0, 0)
        r = cpu.run(CODE, stop_on="rtl", sentinel=0x050000); self.assertEqual((r.reason, cpu.pc24()), ("sentinel", 0x050000))
        cpu = native([0xEA, 0xEA, 0xEA]); r = cpu.run(CODE, stop_on="address", addresses=[CODE + 2]); self.assertEqual((r.reason, r.steps), ("address", 2))
        cpu = native([0x80, 0xFE]); r = cpu.run(CODE, stop_on=None, max_steps=50); self.assertEqual((r.reason, r.steps), ("max_steps", 50))

    def test_rmw_16bit_writes_high_first(self):
        io = IORegion(0xD000, 0xD0FF, default=0x12)
        cpu = native([0xEE, 0x00, 0xD0], m=0); io.attach(cpu.mem); cpu.step()
        self.assertEqual([a for a, _ in io.writes], [0xD001, 0xD000])


if __name__ == "__main__":
    unittest.main()
