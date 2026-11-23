// T170 D2: the serial port's bus trace in the bug report (iectrace.cc) against a model of the register page of cores/c128/c128_iectrace.v: the page is
// found by its ID bytes and by nothing else, the report lists the newest events oldest first with their times and lines, the counters come from a
// snapshot, a wrapped ring is read at the right slots, and a core without the page gets no section.
#include <string>
#include <vector>
#include "check.h"
#include "io.h"
#include "bugreport.h"
#include "iectrace.h"

static bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

// the page, as the gateware answers it (offsets as in c128_iectrace.v)
struct PageIo : CoreIo {
    bool present = true;
    uint32_t hz = 31527778, total = 0, atn = 0, clk = 0, dat = 0, srq = 0;
    uint16_t live = 0;
    std::vector<uint32_t> ring = std::vector<uint32_t>(1024, 0);
    uint16_t idx = 0;
    int snaps = 0, busy_reads = 0;
    uint8_t byte(uint32_t v, uint32_t i) { return (uint8_t)(v >> (8 * i)); }
    uint8_t rd(uint32_t reg)
    {
        if (!present || reg < 0xF000u) return 0;
        uint32_t off = reg - 0xF000u;
        switch (off) {
        case 0: return 'I';
        case 1: return 'E';
        case 2: return 'C';
        case 3: return 'T';
        case 4: return 1;
        case 5: return 10;
        case 0x010: return busy_reads-- > 0 ? 1 : 0;
        case 0x030: return (uint8_t)idx;
        case 0x031: return (uint8_t)(idx >> 8);
        case 0x018: return (uint8_t)live;
        case 0x019: return (uint8_t)(live >> 8);
        }
        if (off >= 0x008 && off < 0x00C) return byte(hz, off - 0x008);
        if (off >= 0x014 && off < 0x018) return byte(total, off - 0x014);
        if (off >= 0x020 && off < 0x024) return byte(atn, off - 0x020);
        if (off >= 0x024 && off < 0x028) return byte(clk, off - 0x024);
        if (off >= 0x028 && off < 0x02C) return byte(dat, off - 0x028);
        if (off >= 0x02C && off < 0x030) return byte(srq, off - 0x02C);
        if (off >= 0x034 && off < 0x038) return byte(ring[idx & 1023], off - 0x034);
        return 0;
    }
    void wr(uint32_t reg, uint8_t v)
    {
        if (!present || reg < 0xF000u) return;
        uint32_t off = reg - 0xF000u;
        if (off == 0x010 && (v & 2)) { snaps++; busy_reads = 3; }
        if (off == 0x030) idx = (uint16_t)((idx & 0x300) | v);
        if (off == 0x031) idx = (uint16_t)((idx & 0xFF) | ((v & 3) << 8));
    }
    void ddr_write(uint32_t, const void *, uint32_t) { }
    void ddr_read(uint32_t, void *, uint32_t) { }
    void rom_write(uint32_t, const void *, uint32_t) { }
    void rom_read(uint32_t, void *, uint32_t) { }
    void ram_write(uint32_t, const void *, uint32_t) { }
    void ram_read(uint32_t, void *, uint32_t) { }
    void stop(bool) { }
    void mode_reset(bool) { }
    uint32_t now_ms() { return 0; }
    void delay_ms(uint32_t) { }
    void delay_us(uint32_t) { }
    // an event: delta ticks, 4 bus bits, 4 port bits, fs out, C128 mode
    void push(uint32_t dt, unsigned bus, unsigned port, bool fs, bool c128)
    {
        ring[total & 1023] = (dt << 16) | (c128 ? 0x200u : 0) | (fs ? 0x100u : 0) | (port << 4) | bus;
        total++;
    }
};

static std::string report(PageIo &io, int show)
{
    static char buf[16384];
    BrOut o;
    br_init(&o, buf, sizeof(buf));
    iectrace_report(&io, &o, show);
    br_finish(&o);
    return std::string(buf);
}

int main()
{
    {
        PageIo io;
        CHECK(iectrace_present(&io));
        io.present = false;
        CHECK(!iectrace_present(&io));
        io.present = true;
    }
    {
        PageIo io;                      // an empty ring
        std::string t = report(io, 96);
        CHECK(has(t, "Events since the start: 0"));
        CHECK(has(t, "No event recorded."));
        CHECK_EQ(io.snaps, 1);
    }
    {
        PageIo io;                      // a LOAD's start: ATN low, CLK low, then DATA
        io.push(65535, 0xE, 0xF, true, true);                // ATN low, the first event: time saturated
        io.push(315, 0xC, 0xF, true, true);                  // CLK low 315 ticks later = 10.0 us
        io.push(31527, 0xC, 0xB, false, true);               // DATA of the port low, FSDIR in, 1000.0 us
        io.atn = 1; io.clk = 1; io.dat = 1; io.srq = 4096;
        io.live = 0x2BC;                                     // C128 mode, fs in, port .. D? , bus ..
        std::string t = report(io, 96);
        CHECK(has(t, "Events since the start: 3"));
        CHECK(has(t, "Edges the core heard: ATN 1, CLK 1, DATA 1, SRQ 4096"));
        CHECK(has(t, "clock 31527778 Hz"));
        CHECK(has(t, "Newest 3 of 3 events, oldest first:"));
        size_t a = t.find("#0 "), b = t.find("#1 "), c = t.find("#2 ");
        CHECK(a != std::string::npos && b != std::string::npos && c != std::string::npos && a < b && b < c);
        CHECK(has(t, "#0 +>2078.6us bus .CDS port ACDS fs out mode 1"));   // saturated: 65535 * 10000 / 31527 kHz = 20786 tenths
        CHECK(has(t, "#1 +9.9us bus ..DS port ACDS fs out mode 1"));       // 315 * 10000 / 31527 = 99 tenths
        CHECK(has(t, "#2 +1000.0us bus ..DS port AC.S fs in mode 1"));   // 31527 ticks = 1000.0 us; port 0xB: DATA low
        CHECK(has(t, "Now: bus"));
    }
    {
        PageIo io;                      // a wrapped ring: 1500 events, the newest 96 of them, in order, from the right slots
        for (uint32_t i = 0; i < 1500; i++) io.push(100 + (i & 7), (i & 1) ? 0xF : 0xE, 0xF, true, false);
        std::string t = report(io, 96);
        CHECK(has(t, "Events since the start: 1500"));
        CHECK(has(t, "Newest 96 of 1024 events, oldest first:"));
        CHECK(has(t, "#1404 +"));
        CHECK(has(t, "#1499 +"));
        CHECK(!has(t, "#1403 +"));
        CHECK(!has(t, "#1500 +"));
        CHECK(has(t, "#1499 +3.2us bus ACDS port ACDS fs out mode 0"));   // 1499 & 7 = 3: 103 ticks = 32 tenths
        CHECK(has(t, "#1498 +3.2us bus .CDS port ACDS fs out mode 0"));       // 1498 & 7 = 2: 102 ticks = 32 tenths (3.23)
        CHECK_EQ(io.snaps, 1);
    }
    {
        PageIo io;                      // a ring not yet full: only the events there are
        io.push(10, 0xE, 0xF, true, true);
        io.push(20, 0xF, 0xF, true, true);
        std::string t = report(io, 96);
        CHECK(has(t, "Newest 2 of 2 events, oldest first:"));
    }
    DONE("iectrace");
}
