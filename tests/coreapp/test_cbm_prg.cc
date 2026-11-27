// The CBM BASIC loader (T104 5.6 item 1): relink, the pointers after it, the plausibility window, the
// machine-language case and the keys typed into the buffer.
#include "cbm_prg.h"
#include "check.h"
#include "fakes.h"
#include <stdio.h>
#include <string.h>
#include <vector>

// The numbers of the machine this loader was read from (its core's own parameters).
static CbmPrgParams sample_machine(void)
{
    CbmPrgParams p;
    memset(&p, 0, sizeof(p));
    p.basic_starts[0] = 0x1001; p.basic_starts[1] = 0x0401; p.basic_starts[2] = 0x1201;
    p.nbasic_starts = 3;
    p.txttab = 0x2B; p.txttab_min = 0x0400; p.txttab_max = 0x2000;
    p.vartab = 0x2D; p.arytab = 0x2F; p.strend = 0x31;
    p.kbd_buffer = 0x0277; p.kbd_count = 0xC6;
    p.ready_ms = 2500; p.p00_header = 0x1A;
    p.min_size = 3; p.max_size = 0x10020;
    return p;
}

// "10 PRINT" twice, as a .prg at `load`: link, line number, one token, 0; then the end marker.
static std::vector<uint8_t> prg(uint16_t load)
{
    std::vector<uint8_t> v;
    v.push_back((uint8_t)(load & 0xFF));
    v.push_back((uint8_t)(load >> 8));
    uint16_t a = load;
    uint16_t l2 = (uint16_t)(a + 6);                 // first line is 6 bytes
    v.push_back((uint8_t)(l2 & 0xFF)); v.push_back((uint8_t)(l2 >> 8));
    v.push_back(10); v.push_back(0);
    v.push_back(0x99); v.push_back(0);
    uint16_t l3 = (uint16_t)(l2 + 6);
    v.push_back((uint8_t)(l3 & 0xFF)); v.push_back((uint8_t)(l3 >> 8));
    v.push_back(20); v.push_back(0);
    v.push_back(0x99); v.push_back(0);
    v.push_back(0); v.push_back(0);                   // end of chain
    return v;
}

int main()
{
    CbmPrgParams p = sample_machine();
    FakeIo io;
    char msg[160];

    // --- a BASIC program loaded at $1001 but the machine says BASIC starts at $1201 (+3K expansion)
    io.ram_write(p.txttab, "\x01\x12", 2);
    std::vector<uint8_t> f = prg(0x1001);
    CbmPrgResult r;
    CHECK_EQ(cbm_prg_place(&io, f.data(), (uint32_t)f.size(), p, false, &r, msg, sizeof(msg)), 0);
    CHECK(r.basic);
    CHECK_EQ(r.load, 0x1201);
    // every link moved by the same difference, and nothing else changed
    uint8_t out[16];
    io.ram_read(0x1201, out, 16);
    CHECK_EQ(out[0] | (out[1] << 8), 0x1207);
    CHECK_EQ(out[2], 10);
    CHECK_EQ(out[6] | (out[7] << 8), 0x120D);
    CHECK_EQ(out[8], 20);
    CHECK_EQ(out[12] | (out[13] << 8), 0);
    // the three pointers follow the program, all three the same
    uint8_t q[2];
    io.ram_read(p.vartab, q, 2); CHECK_EQ(q[0] | (q[1] << 8), r.end);
    io.ram_read(p.arytab, q, 2); CHECK_EQ(q[0] | (q[1] << 8), r.end);
    io.ram_read(p.strend, q, 2); CHECK_EQ(q[0] | (q[1] << 8), r.end);
    CHECK_EQ(r.end, 0x120F);

    // --- RUN is typed, with its length in the count byte
    cbm_prg_type_start(&io, p, r);
    uint8_t kb[8];
    io.ram_read(p.kbd_buffer, kb, 4);
    CHECK(kb[0] == 'R' && kb[1] == 'U' && kb[2] == 'N' && kb[3] == '\r');
    io.ram_read(p.kbd_count, kb, 1);
    CHECK_EQ(kb[0], 4);

    // --- a machine-language file goes where it says and is started with SYS
    std::vector<uint8_t> ml = { 0x00, 0xC0, 0x60, 0x60 };
    CHECK_EQ(cbm_prg_place(&io, ml.data(), (uint32_t)ml.size(), p, false, &r, msg, sizeof(msg)), 0);
    CHECK(!r.basic);
    CHECK_EQ(r.load, 0xC000);
    CHECK_EQ(r.start, 0xC000);
    CHECK_EQ(r.end, 0xC002);
    io.ram_read(0xC000, kb, 2);
    CHECK(kb[0] == 0x60 && kb[1] == 0x60);
    cbm_prg_type_start(&io, p, r);
    io.ram_read(p.kbd_buffer, kb, 8);
    CHECK(!memcmp(kb, "SYS49152\r", 8));

    // --- the machine has not finished its reset: the pointer is nonsense and the load is refused.
    // Both ends of the window, so a control that defeats one of them is still seen.
    io.ram_write(p.txttab, "\xFF\xFF", 2);                  // above the window
    CHECK(cbm_prg_place(&io, f.data(), (uint32_t)f.size(), p, false, &r, msg, sizeof(msg)) != 0);
    CHECK(strstr(msg, "cannot be right") != 0);
    io.ram_write(p.txttab, "\x00\x01", 2);                  // below it
    CHECK(cbm_prg_place(&io, f.data(), (uint32_t)f.size(), p, false, &r, msg, sizeof(msg)) != 0);
    CHECK(strstr(msg, "cannot be right") != 0);
    io.ram_write(p.txttab, "\x01\x10", 2);

    // --- a .P00 carries 0x1A bytes before the load address
    std::vector<uint8_t> p00(p.p00_header, 0);
    p00.insert(p00.end(), f.begin(), f.end());
    CHECK_EQ(cbm_prg_place(&io, p00.data(), (uint32_t)p00.size(), p, true, &r, msg, sizeof(msg)), 0);
    CHECK_EQ(r.load, 0x1001);
    CHECK(r.basic);

    // --- sizes outside the window are refused by both ends
    CHECK(cbm_prg_place(&io, f.data(), 2, p, false, &r, msg, sizeof(msg)) != 0);
    CHECK(cbm_prg_place(&io, f.data(), p.max_size + 1, p, false, &r, msg, sizeof(msg)) != 0);

    // --- T104.2: a file never wraps round to $0000. It ends at $FFFF at the latest; one byte more is refused, and nothing is written.
    {
        FakeIo wio;
        std::vector<uint8_t> fits(2 + 16, 0x60);
        fits[0] = 0xF0; fits[1] = 0xFF;                       // $FFF0 + 16 bytes = $10000
        CHECK_EQ(cbm_prg_place(&wio, fits.data(), (uint32_t)fits.size(), p, false, &r, msg, sizeof(msg)), 0);
        CHECK_EQ(wio.ram[0xFFFF], 0x60);
        CHECK_EQ(r.end, 0);                                   // the first byte after it is $10000, which a 16 bit end cannot say
        std::vector<uint8_t> wraps(2 + 17, 0x61);
        wraps[0] = 0xF0; wraps[1] = 0xFF;
        CHECK(cbm_prg_place(&wio, wraps.data(), (uint32_t)wraps.size(), p, false, &r, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "past $FFFF") != 0);
        CHECK_EQ(wio.ram[0x0000], 0);                         // nothing folded back into the zero page
        CHECK_EQ(wio.ram[0xFFF0], 0x60);                      // and the earlier file is untouched
        // a BASIC program that does not fit above the start the machine states
        wio.ram_write(p.txttab, "\xF0\x1F", 2);
        std::vector<uint8_t> big = prg(0x1001);
        big.resize(0xF000, 0);
        CHECK(cbm_prg_place(&wio, big.data(), (uint32_t)big.size(), p, false, &r, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "does not fit") != 0);
        CHECK(!wio.overrun);                                  // nothing was written or read past $FFFF
    }

    // --- the relink alone: a link that points outside the file stops it instead of writing past the end
    uint8_t bad[8] = { 0xFF, 0xFF, 10, 0, 0x99, 0, 0, 0 };
    uint16_t end = cbm_prg_relink(bad, sizeof(bad), 0x1001, 0x1201);
    CHECK_EQ(end, 0x1203);
    CHECK(bad[0] == 0xFF && bad[1] == 0xFF);          // untouched

    printf("test_cbm_prg: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
