// Host test of the flash protection before a large core (host-app/software/hostapp/flashlock.cc; T152 S5, S6, S7, D6, D7) against a model of the
// W25Q128JV's status registers: volatile writes (0x50), the block-protect range, the power-supply lock-down (SRL with SRP = 0), the one-time program
// lock (SRP and SRL together) which the model records as OTP - the thing that must never happen - and a power cycle that returns the volatile state.
//   test_flashlock
#include "flashlock.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct ChipModel : FlashSpi {
    uint32_t jedec;
    uint8_t sr1, sr2;                  // the volatile registers
    bool vol_we, nv_we;                // a write enable is pending
    bool otp;                          // SRP and SRL both set: the one-time program lock (can never be undone on the real chip)
    bool stuck_srp;                    // status register 1 writes are ignored (a hardware-protected chip), SRP reads as 1
    bool ignore_writes;                // every status register write is ignored
    int sr_writes, nv_enables, vol_enables, overlong, reads;
    int gate_viol;                     // status register writes the host's flash gate (host/flash_gate.vhd, g_lock) would not pass
    std::vector<uint8_t> cmds;         // the first byte of every frame
    std::vector<uint8_t> frame;
    size_t out_pos;
    bool open;
    ChipModel(uint32_t id = FL_JEDEC_W25Q128) : jedec(id), sr1(0), sr2(0), vol_we(false), nv_we(false), otp(false), stuck_srp(false), ignore_writes(false),
                                                sr_writes(0), nv_enables(0), vol_enables(0), overlong(0), reads(0), gate_viol(0), out_pos(0), open(false) { }
    bool lockdown() const { return (sr2 & FL_SR2_SRL) && !(sr1 & FL_SR1_SRP); }
    void begin() { frame.clear(); out_pos = 0; open = true; }
    void send(uint8_t b) { frame.push_back(b); }
    uint8_t recv()
    {
        reads++;
        uint8_t cmd = frame.empty() ? 0 : frame[0];
        size_t i = out_pos++;
        if (cmd == 0x9F) { return i == 0 ? (uint8_t)(jedec >> 16) : (i == 1 ? (uint8_t)(jedec >> 8) : (uint8_t)jedec); }
        if (cmd == 0x05) { return sr1; }
        if (cmd == 0x35) { return sr2; }
        return 0xFF;
    }
    void end()
    {
        open = false;
        if (frame.empty()) { return; }
        cmds.push_back(frame[0]);
        switch (frame[0]) {
        case 0x50: vol_we = true; vol_enables++; if (frame.size() != 1) overlong++; break;
        case 0x06: nv_we = true; nv_enables++; if (frame.size() != 1) overlong++; break;
        case 0x01: {                                                     // write status register 1 (and 2 when a second byte follows)
            if (frame.size() != 2) overlong++;
            sr_writes++;
            if (frame.size() != 2 || (frame[1] & 0xFC) != 0x34 || cmds.size() < 2 || cmds[cmds.size() - 2] != 0x50) gate_viol++;
            bool allowed = (vol_we || nv_we) && !lockdown() && !ignore_writes && !stuck_srp;
            vol_we = nv_we = false;
            if (allowed && frame.size() >= 2) { sr1 = (uint8_t)(frame[1] & 0xFC); }
            break;
        }
        case 0x31: {
            if (frame.size() != 2) overlong++;
            sr_writes++;
            if (frame.size() != 2 || (frame[1] & 0xFC) != 0x00 || !(frame[1] & 0x01) || cmds.size() < 2 || cmds[cmds.size() - 2] != 0x50) gate_viol++;
            bool allowed = (vol_we || nv_we) && !lockdown() && !ignore_writes;
            vol_we = nv_we = false;
            if (allowed && frame.size() >= 2) { sr2 = (uint8_t)(frame[1] & 0x41); }
            break;
        }
        default: break;
        }
        if (stuck_srp) { sr1 |= FL_SR1_SRP; }
        if ((sr1 & FL_SR1_SRP) && (sr2 & FL_SR2_SRL)) { otp = true; }
    }
    bool wait_idle(int) { return true; }
    void power_cycle() { sr1 = stuck_srp ? FL_SR1_SRP : 0; sr2 = 0; vol_we = nv_we = false; }
    // is an erase of the sector at addr refused by the block protect? (TB = 1, SEC = 0, CMP = 0: the bottom 256 KiB << (BP - 1))
    bool protected_at(uint32_t addr) const
    {
        unsigned bp = (sr1 >> 2) & 7;
        if (bp == 0) { return false; }
        bool cmp = (sr2 & FL_SR2_CMP) != 0;
        uint32_t size = bp >= 7 ? 0x1000000u : (0x40000u << (bp - 1));
        bool in;
        if (sr1 & FL_SR1_SEC) { return false; }                          // the model knows the 4 KiB variants no further: not used by the host
        if (sr1 & FL_SR1_TB) { in = addr < size; } else { in = addr >= 0x1000000u - size; }
        return cmp ? !in : in;
    }
    int count_cmd(uint8_t c) const { int n = 0; for (size_t i = 0; i < cmds.size(); i++) n += cmds[i] == c; return n; }
};

static bool only_reads(const ChipModel &m)
{
    for (size_t i = 0; i < m.cmds.size(); i++) { if (m.cmds[i] != 0x9F && m.cmds[i] != 0x05 && m.cmds[i] != 0x35) return false; }
    return true;
}

// S5: the lock under a large core
static void t_lock()
{
    ChipModel m;
    FlashLockInfo info;
    FlashLockResult r = flashlock_apply(m, true, &info);
    CHECK(r == FLR_LOCKED);
    CHECK(info.jedec == FL_JEDEC_W25Q128);
    CHECK(flashlock_is_set(info.sr1, info.sr2));
    CHECK(info.sr1 == 0x34);                                              // TB, BP = 101, SRP = 0
    CHECK((info.sr2 & FL_SR2_SRL) != 0);
    CHECK(!m.otp);
    CHECK(m.overlong == 0);
    CHECK(m.gate_viol == 0);                                              // T152 D9: every status register write is one the host's gate passes
    // the range of D7: 0x000000-0x3FFFFF and not a byte more
    CHECK(m.protected_at(0x000000));
    CHECK(m.protected_at(0x220000));                                      // the application slot
    CHECK(m.protected_at(0x3FF000));                                      // the last sector of the range
    CHECK(!m.protected_at(0x400000));                                     // the flash drive starts here
    CHECK(!m.protected_at(0x580000));
    CHECK(!m.protected_at(0xFE8000));                                     // the config sectors (Gideon's settings)
    // only volatile writes: no non-volatile write enable, no write enable at all in front of anything else
    CHECK(m.count_cmd(0x06) == 0);
    CHECK(m.count_cmd(0x50) == 2);
    CHECK(m.count_cmd(0x01) == 1 && m.count_cmd(0x31) == 1);
    // the power-supply lock-down holds: a later write of status register 1 (a large core's protect_disable) changes nothing
    ChipModel &c = m;
    c.begin(); c.send(0x50); c.end(); c.begin(); c.send(0x01); c.send(0x00); c.end();
    c.begin(); c.send(0x50); c.end(); c.begin(); c.send(0x31); c.send(0x00); c.end();
    CHECK(c.sr1 == 0x34 && (c.sr2 & FL_SR2_SRL));
    CHECK(c.protected_at(0x000000));
    // S6: after the power cycle the host menu has an open flash
    c.power_cycle();
    CHECK(c.sr1 == 0 && c.sr2 == 0);
    CHECK(!c.protected_at(0x000000) && !c.protected_at(0x220000));
    CHECK(!c.otp);
}

// the SRP guard: the chip arrives with SRP = 1 in the volatile register
static void t_srp_set_volatile()
{
    ChipModel m;
    m.sr1 = FL_SR1_SRP;
    FlashLockInfo info;
    FlashLockResult r = flashlock_apply(m, true, &info);
    CHECK(r == FLR_LOCKED);
    CHECK(!m.otp);
    CHECK((m.sr1 & FL_SR1_SRP) == 0);
}

// SRP cannot be cleared (a chip whose status register 1 is write protected): SRL is never written
static void t_srp_stuck()
{
    ChipModel m;
    m.stuck_srp = true;
    m.sr1 = FL_SR1_SRP;
    FlashLockInfo info;
    FlashLockResult r = flashlock_apply(m, true, &info);
    CHECK(r == FLR_FAILED);
    CHECK(!m.otp);
    CHECK(m.count_cmd(0x31) == 0);
}

// a chip that takes no status register write: the start is refused
static void t_ignored()
{
    ChipModel m;
    m.ignore_writes = true;
    FlashLockInfo info;
    CHECK(flashlock_apply(m, true, &info) == FLR_FAILED);
    CHECK(!m.otp);
}

// a chip whose CMP bit is set would invert the range: it is cleared
static void t_cmp()
{
    ChipModel m;
    m.sr2 = FL_SR2_CMP;
    FlashLockInfo info;
    CHECK(flashlock_apply(m, true, &info) == FLR_LOCKED);
    CHECK((m.sr2 & FL_SR2_CMP) == 0);
    CHECK(m.protected_at(0x000000) && !m.protected_at(0x400000));
}

// S7: off touches nothing
static void t_off()
{
    ChipModel m;
    FlashLockInfo info;
    CHECK(flashlock_apply(m, false, &info) == FLR_OFF);
    CHECK(m.cmds.empty());
    CHECK(m.sr1 == 0 && m.sr2 == 0);
    CHECK(!m.protected_at(0x000000));
}

// another chip (the emulator's Infineon S25FL128L): only the ID is read
static void t_other_chip()
{
    ChipModel m(0x016018);
    FlashLockInfo info;
    CHECK(flashlock_apply(m, true, &info) == FLR_OTHER_CHIP);
    CHECK(only_reads(m) && m.count_cmd(0x9F) == 1 && m.cmds.size() == 1);
    CHECK(m.sr1 == 0 && m.sr2 == 0 && !m.otp);
    ChipModel w(0xEF8018);                                                // a W25Q128JW: same maker, another type
    CHECK(flashlock_apply(w, true, &info) == FLR_OTHER_CHIP);
    CHECK(w.cmds.size() == 1);
}

// S6: the F8 page reads the chip and its status registers (reads only); a chip that is not the W25Q128JV is asked for its ID and nothing else
static void t_status()
{
    ChipModel m;
    FlashLockInfo info;
    CHECK(flashlock_status(m, &info) && info.jedec == FL_JEDEC_W25Q128 && info.sr1 == 0 && info.sr2 == 0);
    CHECK(only_reads(m) && m.sr_writes == 0 && m.vol_enables == 0 && m.nv_enables == 0);
    CHECK(flashlock_apply(m, true, &info) == FLR_LOCKED);
    CHECK(flashlock_status(m, &info) && info.sr1 == 0x34 && (info.sr2 & FL_SR2_SRL));
    ChipModel o(0x016018);
    CHECK(!flashlock_status(o, &info) && info.jedec == 0x016018 && o.cmds.size() == 1 && o.count_cmd(0x9F) == 1);
}

int main()
{
    t_status();
    t_lock();
    t_srp_set_volatile();
    t_srp_stuck();
    t_ignored();
    t_cmp();
    t_off();
    t_other_chip();
    printf("%s (%d failed)\n", fails ? "test_flashlock FAILED" : "test_flashlock ok", fails);
    return fails ? 1 : 0;
}
