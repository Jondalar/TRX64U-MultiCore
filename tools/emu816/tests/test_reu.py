import unittest
from .helpers import *
from emu816.reu import REU, STATUS_EOB, STATUS_VERIFY, STATUS_IRQ


def prog_setup(host, reu, ln, cmd):
    """6502 program that programs the REU like reu_gate.rs setup()/transfer() (reu_gate.rs:44-55,114-121)."""
    code = []
    for r, v in ((2, host & 0xFF), (3, host >> 8), (4, reu & 0xFF), (5, (reu >> 8) & 0xFF), (6, reu >> 16),
                 (7, ln & 0xFF), (8, ln >> 8), (1, cmd)):
        code += [0xA9, v, 0x8D, r, 0xDF]
    return code + [0x60]


class REUTests(unittest.TestCase):
    def mk(self, size=16384):
        cpu = nmos([])
        reu = REU(cpu.mem, size_kb=size)
        return cpu, reu

    def go(self, cpu, host, reu_addr, ln, cmd):
        cpu.mem.load(CODE, prog_setup(host, reu_addr, ln, cmd | 0x90))   # EXECUTE | FF00 trigger disabled
        cpu.PC = CODE
        cpu.run(CODE)

    def test_reset_values(self):                      # reu.rs:308-322 / reu_gate.rs:893-900
        cpu, reu = self.mk(512)
        self.assertEqual((reu.command, reu.length, reu.bank, reu.int_mask, reu.addr_ctl, reu.status),
                         (0x10, 0xFFFF, 0xF8, 0x1F, 0x3F, 0x10))

    def test_register_read_unused_and_mirror(self):   # reu.rs:427-429, 776-780 (mirrors to $DFFF)
        cpu, reu = self.mk()
        self.assertEqual(cpu.mem.read(0xDF0B), 0xFF)
        reu.mem.write(0xDF22, 0x34)                    # mirror of $DF02
        self.assertEqual(cpu.mem.read(0xDF02), 0x34)

    def test_stash_then_fetch_roundtrip(self):        # reu_gate.rs:123
        cpu, reu = self.mk()
        cpu.mem.load(0x2000, bytes(range(16)))
        self.go(cpu, 0x2000, 0x050000 + 0x1234, 16, 0)
        self.assertEqual(reu.dump(0x051234, 16), bytes(range(16)))
        cpu.mem.load(0x2000, bytes(16))
        self.go(cpu, 0x2000, 0x051234, 16, 1)
        self.assertEqual(cpu.mem.dump(0x2000, 16), bytes(range(16)))
        self.assertTrue(reu.status & STATUS_EOB)

    def test_status_clears_on_read(self):             # reu_gate.rs:175 / reu.rs:432-436
        cpu, reu = self.mk()
        self.go(cpu, 0x2000, 0, 4, 0)
        self.assertEqual(cpu.mem.read(0xDF00) & 0x40, 0x40)
        self.assertEqual(cpu.mem.read(0xDF00) & 0xE0, 0)

    def test_registers_after_transfer_without_autoload(self):   # reu.rs:552-560 : address advances, length -> 1
        cpu, reu = self.mk()
        self.go(cpu, 0x2000, 0x010000, 8, 0)
        self.assertEqual((reu.base_c64, reu.base_reu, reu.bank, reu.length), (0x2008, 0x0008, 1, 1))

    def test_autoload(self):                           # reu_gate.rs:191 / reu.rs:561-566
        cpu, reu = self.mk()
        self.go(cpu, 0x2000, 0x010000, 8, 0x20)
        self.assertEqual((reu.base_c64, reu.base_reu, reu.bank, reu.length), (0x2000, 0x0000, 1, 8))

    def test_swap(self):                               # reu_gate.rs:139
        cpu, reu = self.mk()
        reu.load(0x030000, [1, 2, 3]); cpu.mem.load(0x2000, [9, 8, 7])
        self.go(cpu, 0x2000, 0x030000, 3, 2)
        self.assertEqual((reu.dump(0x030000, 3), cpu.mem.dump(0x2000, 3)), (bytes([9, 8, 7]), bytes([1, 2, 3])))

    def test_verify_equal_and_error(self):             # reu_gate.rs:150
        cpu, reu = self.mk()
        reu.load(0x040000, [5, 6, 7, 8]); cpu.mem.load(0x2000, [5, 6, 7, 8])
        self.go(cpu, 0x2000, 0x040000, 4, 3)
        self.assertEqual(reu.status & (STATUS_EOB | STATUS_VERIFY), STATUS_EOB)
        reu.status = 0
        cpu.mem.poke(0x2001, 0xEE)
        self.go(cpu, 0x2000, 0x040000, 4, 3)
        self.assertEqual(reu.status & (STATUS_EOB | STATUS_VERIFY), STATUS_VERIFY)       # failed mid-block: no EOB
        self.assertEqual(reu.length, 2)                                                    # bytes still to go after the mismatch byte
        reu.status = 0
        cpu.mem.poke(0x2001, 0x06); cpu.mem.poke(0x2003, 0xEE)
        self.go(cpu, 0x2000, 0x040000, 4, 3)
        self.assertEqual(reu.status & (STATUS_EOB | STATUS_VERIFY), STATUS_EOB | STATUS_VERIFY)   # weirdness 2

    def test_length_zero_is_64k(self):                 # reu.rs:524-525
        cpu, reu = self.mk()
        cpu.mem.poke(0x0000, 0x11); cpu.mem.poke(0xFFFF, 0x22)
        self.go(cpu, 0x0000, 0x020000, 0, 0)
        self.assertEqual((reu.ram_read(0x020000), reu.ram_read(0x02FFFF)), (0x11, 0x22))

    def test_fixed_c64_address(self):                  # reu_gate.rs:209 / reu.rs:526-529
        cpu, reu = self.mk()
        reu.load(0x010000, [1, 2, 3, 4])
        reu.mem.write(0xDF0A, 0x80)
        self.go(cpu, 0x2000, 0x010000, 4, 1)
        self.assertEqual(cpu.mem.peek(0x2000), 4)          # every byte lands on the same address; last wins
        self.assertEqual(reu.base_c64, 0x2000)

    def test_fixed_reu_address(self):
        cpu, reu = self.mk()
        cpu.mem.load(0x2000, [1, 2, 3, 4]); reu.mem.write(0xDF0A, 0x40)
        self.go(cpu, 0x2000, 0x010000, 4, 0)
        self.assertEqual(reu.dump(0x010000, 2), bytes([4, 0]))

    def test_ff00_trigger(self):                        # reu_gate.rs:242 : armed until a write to $FF00
        cpu, reu = self.mk()
        cpu.mem.poke(0x2000, 0x77)
        code = []
        for r, v in ((2, 0), (3, 0x20), (4, 0), (5, 0), (6, 1), (7, 1), (8, 0), (1, 0x80)):   # EXECUTE, trigger enabled
            code += [0xA9, v, 0x8D, r, 0xDF]
        code += [0xA9, 0x00, 0x8D, 0x00, 0xFF, 0x60]
        cpu.mem.load(CODE, code)
        cpu.step_count = 0
        for _ in range(16):
            cpu.step()
        self.assertEqual(reu.ram_read(0x010000), 0)                     # armed, not yet run
        cpu.step(); cpu.step()                                            # LDA #0 ; STA $FF00
        self.assertEqual(reu.ram_read(0x010000), 0x77)
        self.assertEqual(len(reu.transfers), 1)

    def test_trigger_disabled_ignores_ff00(self):
        cpu, reu = self.mk()
        cpu.mem.load(CODE, [0xA9, 0, 0x8D, 0, 0xFF, 0x60]); cpu.run(CODE)
        self.assertEqual(reu.transfers, [])

    def test_wraparound_bug_at_512k(self):             # reu.rs:127-131, 348-354 : increment wraps in the low 19 bits
        cpu, reu = self.mk(16384)
        cpu.mem.load(0x2000, [1, 2, 3, 4])
        self.go(cpu, 0x2000, 0x07FFFE, 4, 0)
        self.assertEqual((reu.ram_read(0x07FFFE), reu.ram_read(0x07FFFF), reu.ram_read(0x000000), reu.ram_read(0x000001)),
                         (1, 2, 3, 4))          # 07:FFFF + 1 wraps to 00:0000 (low 19 bits)
        self.assertEqual(reu.ram_read(0x080000), 0)

    def test_512k_unbacked_and_bank_bits(self):         # reu.rs:99,338-343 : stores above fitted DRAM drop; bank bits 3-7 stuck 1
        cpu, reu = self.mk(256)
        self.assertEqual(cpu.mem.read(0xDF06) & 0xF8, 0xF8)
        cpu.mem.poke(0x2000, 0x55)
        self.go(cpu, 0x2000, 0x040000, 1, 0)                 # 256K = banks 0-3 : bank 4 aliases to 4&7 > size -> dropped
        self.assertEqual(reu.ram_read(0x040000), 0)

    def test_fetch_into_io_is_logged(self):
        cpu, reu = self.mk()
        io = IORegion(0xD800, 0xDBFF).attach(cpu.mem)
        reu.load(0x010000, [3, 4])
        self.go(cpu, 0xD800, 0x010000, 2, 1)
        self.assertEqual(io.writes, [(0xD800, 3), (0xD801, 4)])

    def test_irq_line(self):                              # reu_gate.rs:346 : both mask bits needed
        cpu, reu = self.mk()
        reu.mem.write(0xDF09, 0xC0)
        self.go(cpu, 0x2000, 0, 1, 0)
        self.assertTrue(reu.irq); self.assertTrue(reu.status & STATUS_IRQ)
        cpu.mem.read(0xDF00); self.assertFalse(reu.irq)


if __name__ == "__main__":
    unittest.main()
