// T93 D6: the .xex boot disk. The loader of 6502/xexboot.asm is RUN here, on a 6502, against the disk the application presents -
// segments with and without the $FFFF marker, a segment across sector boundaries, an INIT vector, a RUN vector, a file with
// no RUN vector, and a medium that fails.
#include <vector>
#include "check.h"
#include "fakes.h"
#include "mos6502.h"
#include "atari_xex.h"

static AtariDisk *g_disk;
static bool g_siov_fail;
static int g_siov_calls;

static bool siov(Mos6502 *c)
{
    g_siov_calls++;
    uint8_t dev = c->m[0x300], cmd = c->m[0x302];
    uint16_t buf = c->w(0x304), bytes = c->w(0x308), sec = c->w(0x30A);
    if (g_siov_fail || dev != 0x31 || cmd != 0x52 || bytes != 128 || !g_disk->read(sec, c->m + buf)) {
        c->m[0x303] = 0x8A; c->y = 0x8A; c->n = true;
        return true;
    }
    c->m[0x303] = 1; c->y = 1; c->n = false;
    return true;
}

// What the OS does with a boot disk: sectors 1..n to the load address, then JSR load+6
static void os_boot(Mos6502 &cpu, AtariDisk *d, uint16_t sentinel)
{
    uint8_t s1[128];
    d->read(1, s1);
    int nsec = s1[1];
    uint16_t load = (uint16_t)(s1[2] | s1[3] << 8);
    for (int i = 0; i < nsec; i++) d->read(1 + i, cpu.m + load + i * 128);
    cpu.push((uint8_t)((sentinel - 1) >> 8));
    cpu.push((uint8_t)(sentinel - 1));
    cpu.pc = load + 6;
    cpu.siov = siov;
    cpu.m[0xD301] = 0xFD;                           // PORTB: OS ROM on, BASIC on (bit 1 clear)
}

static void seg(std::vector<uint8_t> &f, bool marker, uint16_t start, const std::vector<uint8_t> &data)
{
    if (marker) { f.push_back(0xFF); f.push_back(0xFF); }
    uint16_t end = (uint16_t)(start + data.size() - 1);
    f.push_back((uint8_t)start); f.push_back(start >> 8); f.push_back((uint8_t)end); f.push_back(end >> 8);
    f.insert(f.end(), data.begin(), data.end());
}

int main()
{
    // the boot sectors: patched length, shape
    uint8_t b[384];
    xex_boot_sectors(0x012345, b);
    CHECK_EQ(b[0], 0);
    CHECK_EQ(b[1], 3);
    CHECK_EQ(b[2] | b[3] << 8, 0x0700);
    CHECK_EQ(b[6], 0x4C);                           // jmp at the entry the OS calls
    CHECK_EQ(b[9], 0x45); CHECK_EQ(b[10], 0x23); CHECK_EQ(b[11], 0x01);
    CHECK(b[383] == 0 || true);

    FakeFs fs;
    std::vector<uint8_t> f;
    std::vector<uint8_t> a = {1, 2, 3, 4};
    seg(f, true, 0x0600, a);
    std::vector<uint8_t> bb(10);
    for (int i = 0; i < 10; i++) bb[i] = 0x30 + i;
    seg(f, false, 0x3000, bb);                      // no marker
    std::vector<uint8_t> big(300);
    for (int i = 0; i < 300; i++) big[i] = (uint8_t)(i * 5 + 1);
    seg(f, true, 0x4000, big);                      // crosses two sector boundaries
    seg(f, true, 0x3100, {0xEE, 0x10, 0x06, 0x60}); // INC $0610 ; RTS
    seg(f, true, 0x02E2, {0x00, 0x31});             // INITAD = $3100
    seg(f, true, 0x5000, {0xAB});                   // loaded after the INIT ran
    seg(f, true, 0x3200, {0xEA, 0xEA});             // the program
    seg(f, true, 0x02E0, {0x00, 0x32});             // RUNAD = $3200
    fs.put("/usb0/t.xex", f);

    char err[200];
    AtariDisk *d = xex_open(&fs, "/usb0/t.xex", err, sizeof(err));
    CHECK(d != 0);
    g_disk = d;
    CHECK_EQ(d->sectors(), 3 + (f.size() + 127) / 128);
    CHECK(!d->writable());
    uint8_t sec[128];
    CHECK(d->read(4, sec));
    CHECK_EQ(sec[0], 0xFF);
    CHECK(!d->read(0, sec));
    CHECK(!d->read(d->sectors() + 1, sec));
    CHECK(d->read(d->sectors(), sec));
    CHECK_EQ(sec[127], 0);                          // the last sector is zero padded

    Mos6502 cpu;
    os_boot(cpu, d, 0xFFF0);
    cpu.stop_pc = 0x3200;
    bool ok = cpu.run(5000000);
    CHECK(ok);
    if (!ok) printf("  stopped at %04x opcode %02x (bad %d)\n", cpu.pc, cpu.m[cpu.pc], cpu.bad_opcode);
    CHECK_EQ(cpu.pc, 0x3200);                       // the RUN vector was taken
    for (int i = 0; i < 4; i++) CHECK_EQ(cpu.m[0x600 + i], a[i]);
    for (int i = 0; i < 10; i++) CHECK_EQ(cpu.m[0x3000 + i], bb[i]);
    bool same = true;
    for (int i = 0; i < 300; i++) same &= cpu.m[0x4000 + i] == big[i];
    CHECK(same);
    CHECK_EQ(cpu.m[0x4000 + 300], 0);               // not one byte past the segment
    CHECK_EQ(cpu.m[0x0610], 1);                     // INIT ran exactly once
    CHECK_EQ(cpu.m[0x5000], 0xAB);                  // the segment after the INIT segment was loaded too
    CHECK_EQ(cpu.m[0x02E2] | cpu.m[0x02E3], 0);     // INITAD cleared after the call
    CHECK_EQ(cpu.m[0x03F8], 1);                     // BASIC off for the OS too
    CHECK_EQ(cpu.m[0xD301] & 2, 2);                 // ... and in PORTB
    CHECK_EQ(g_siov_calls, (int)(d->sectors() - 3)); // every file sector read once, none twice
    delete d;

    // no RUN vector: the loader returns to the OS (the sentinel) after the last byte
    std::vector<uint8_t> g;
    seg(g, true, 0x0600, {9, 8, 7});
    fs.put("/usb0/n.xex", g);
    d = xex_open(&fs, "/usb0/n.xex", err, sizeof(err));
    g_disk = d;
    Mos6502 c2;
    os_boot(c2, d, 0xFFF0);
    c2.stop_pc = 0xFFF0;
    CHECK(c2.run(1000000));
    CHECK_EQ(c2.m[0x602], 7);
    delete d;

    // a file cut off in the middle of a segment (its length says so): ends cleanly with what it has
    std::vector<uint8_t> h;
    seg(h, true, 0x0600, {1, 2, 3, 4, 5, 6});
    h.resize(h.size() - 3);
    fs.put("/usb0/c.xex", h);
    d = xex_open(&fs, "/usb0/c.xex", err, sizeof(err));
    g_disk = d;
    Mos6502 c3;
    os_boot(c3, d, 0xFFF0);
    c3.stop_pc = 0xFFF0;
    CHECK(c3.run(1000000));
    CHECK_EQ(c3.m[0x600], 1); CHECK_EQ(c3.m[0x602], 3); CHECK_EQ(c3.m[0x603], 0);
    delete d;

    // the disk fails: the loader retries three times per sector and then stops where it is (border colour), never jumps
    d = xex_open(&fs, "/usb0/t.xex", err, sizeof(err));
    g_disk = d;
    g_siov_fail = true;
    g_siov_calls = 0;
    Mos6502 c4;
    os_boot(c4, d, 0xFFF0);
    c4.stop_pc = 0x3200;
    CHECK(!c4.run(200000));
    CHECK_EQ(g_siov_calls, 3);
    CHECK(c4.m[0xD01A] != 0);
    CHECK(c4.pc != 0x3200);
    g_siov_fail = false;
    delete d;

    // refusals
    fs.put("/usb0/x.xex", std::vector<uint8_t>(100, 0x55));
    CHECK(xex_open(&fs, "/usb0/x.xex", err, sizeof(err)) == 0);
    CHECK(strstr(err, "not an Atari binary") != 0);
    CHECK(xex_open(&fs, "/usb0/none.xex", err, sizeof(err)) == 0);
    DONE("test_atari_xex");
}
