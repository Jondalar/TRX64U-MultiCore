// T260 S3 (the app reads the pin table of the bitstream), S4 (the reader of the capture ring), the v2 step words. The model is a register block that serves what the gateware's spec fixes
// (docs/specs/T260 §5 "Register map v2"): the pin ROM of the bank (cores/diag/rtl/pins.toml itself) and the DDR2 ring behind CAP_RADDR / CAP_RCTRL / CAP_RDATA.
#include "check.h"
#include "fakes.h"
#include "dtv2.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

struct Entry { std::string name; int dir, cls, flags, group, idle; };

static int word_of(const std::string &s, const char *key, const char *const *vals, int nv, int def)
{
    size_t p = s.find(std::string(key) + " = \"");
    if (p == std::string::npos) { return def; }
    p += strlen(key) + 4;
    for (int i = 0; i < nv; i++) { if (!s.compare(p, strlen(vals[i]), vals[i]) && s[p + strlen(vals[i])] == '"') { return i; } }
    return def;
}
static bool flag(const std::string &s, const char *key) { return s.find(std::string(key) + " = true") != std::string::npos; }

static std::vector<Entry> load_pins()
{
    std::ifstream f("cores/diag/rtl/pins.toml");        // the one source of the table (the tests run in the repository root, as diag_examples.h assumes)
    std::string line;
    std::vector<Entry> v;
    static const char *const dirs[] = { "in", "out", "inout" };
    static const char *const cls[] = { "none", "od", "pull", "push", "pz", "pzh" };
    static const char *const grp[] = { "slot", "joy", "paddle", "c2n", "iec", "ui", "led", "av", "misc" };
    while (std::getline(f, line)) {
        size_t p = line.find("{ index = ");
        if (p == std::string::npos) { continue; }
        Entry e;
        int idx = atoi(line.c_str() + p + 10);
        size_t q = line.find("name = \"", p) + 8;
        e.name = line.substr(q, line.find('"', q) - q);
        e.dir = word_of(line, "dir", dirs, 3, 0);
        e.cls = word_of(line, "class", cls, 6, 0);
        e.group = word_of(line, "group", grp, 9, 8);
        e.flags = (flag(line, "guard") ? 1 : 0) | (flag(line, "inv") ? 2 : 0) | (flag(line, "buf") ? 4 : 0) | (flag(line, "pwr") ? 8 : 0) | (flag(line, "gen") ? 16 : 0) | (flag(line, "host") ? 32 : 0);
        size_t ip = line.find("idle = ");
        e.idle = ip == std::string::npos ? 2 : atoi(line.c_str() + ip + 7);
        CHECK_EQ(idx, (int)v.size());
        v.push_back(e);
    }
    return v;
}

struct V2Model : FakeIo {
    std::vector<Entry> pins;
    std::vector<uint8_t> ring;                       // the DDR2 ring: byte k of line l at 16 * l + k
    uint32_t raddr = 0;
    int reads = 0, stale_polls = 0;
    V2Model() { regs[DV2_PIN_COUNT] = 0; }
    void on_wr(uint32_t r, uint8_t v) override
    {
        if (r == DV2_PIN_IDX) {
            memset(&regs[DV2_PIN_ROM], 0, 32);
            if (v < pins.size()) {
                const Entry &e = pins[v];
                memcpy(&regs[DV2_PIN_ROM], e.name.c_str(), e.name.size());
                regs[DV2_PIN_ROM + 16] = (uint8_t)e.dir; regs[DV2_PIN_ROM + 17] = (uint8_t)e.cls; regs[DV2_PIN_ROM + 18] = (uint8_t)e.flags;
                regs[DV2_PIN_ROM + 19] = (uint8_t)e.group; regs[DV2_PIN_ROM + 20] = (uint8_t)e.idle;
            }
        } else if (r >= DV2_CAP_RADDR && r < DV2_CAP_RADDR + 4) {
            int b = (int)(r - DV2_CAP_RADDR);
            raddr = (raddr & ~(0xFFu << (8 * b))) | ((uint32_t)v << (8 * b));
            raddr &= ~15u;
        } else if (r == DV2_CAP_RCTRL && (v & 1)) {
            regs[DV2_CAP_STATUS] &= (uint8_t)~DV2_CAP_ST_VALID;
            reads++;
            for (int k = 0; k < 16; k++) { regs[DV2_CAP_RDATA + k] = raddr + k < ring.size() ? ring[raddr + k] : 0; }
            raddr += 16;
            regs[DV2_CAP_STATUS] |= DV2_CAP_ST_VALID;
        }
    }
};

int main()
{
    V2Model m;
    m.pins = load_pins();
    CHECK_EQ(m.pins.size(), (size_t)121);
    m.regs[DV2_PIN_COUNT] = (uint8_t)m.pins.size();

    // S3: the app gets every pin with its name, direction and flags, and finds a pin by name
    DtPins t;
    CHECK_EQ(dtv2_pins_read(&m, &t), 0);
    CHECK_EQ(t.n, 121);
    for (int i = 0; i < t.n; i++) {
        CHECK_STR(t.p[i].name, m.pins[i].name.c_str());
        CHECK(t.p[i].dir == m.pins[i].dir && t.p[i].cls == m.pins[i].cls && t.p[i].flags == m.pins[i].flags && t.p[i].group == m.pins[i].group);
    }
    CHECK_EQ(dtv2_pin_find(t, "KBJOY_1_JOY0"), 50);
    CHECK_EQ(dtv2_pin_find(t, "PADDLE_CMP3"), 66);
    CHECK_EQ(dtv2_pin_find(t, "SLOT_ADDR12"), 12);
    CHECK_EQ(dtv2_pin_find(t, "kbjoy_1_joy0"), -1);            // names are exact
    CHECK_EQ(dtv2_pin_find(t, "NOT_A_PIN"), -1);
    CHECK(t.p[dtv2_pin_find(t, "SLOT_ADDR12")].flags & 1);       // a cartridge port pin is guarded
    CHECK_EQ(t.p[dtv2_pin_find(t, "KBJOY_1_JOY0")].cls, 1);      // open drain
    m.regs[DV2_PIN_COUNT] = 0;
    CHECK_EQ(dtv2_pins_read(&m, &t), -1);                         // a bitstream with no table (v1) is refused

    // the step words (spec §5 "Step word"): fields in place
    CHECK_EQ(dv2_drive(DV2_LOW, 50), (1u << 28) | (1u << 26) | 50);
    CHECK_EQ(dv2_drive(DV2_HIZ, 12), (1u << 28) | (3u << 26) | 12);
    CHECK_EQ(dv2_wait(0x123456), (2u << 28) | 0x123456);
    CHECK_EQ(dv2_sample(), 3u << 28);
    CHECK_EQ(dv2_edges(DV2_FALLING, 66), (4u << 28) | (2u << 26) | 66);
    CHECK_EQ(dv2_tte(DV2_ANY, 63), (5u << 28) | 63);
    CHECK_EQ(dv2_loop(300, 7), (7u << 28) | (300u << 8) | 7);
    CHECK_EQ(dv2_write_reg(0x050, 0x01), (9u << 28) | (0x050u << 16) | 1);
    CHECK_EQ(dv2_capture(1), (10u << 28) | 1);
    CHECK_EQ(dv2_capwait(), 11u << 28);

    // S4 reader: a ring a capture of 4 pins every 1 tick for 4096 samples would have written (a known pattern in the lane bits), then a second capture of 6 samples that starts on the next line
    const uint32_t N = 4096, N2 = 6;
    auto pat = [](uint32_t i) { return (uint32_t)((i * 5u + (i >> 3)) & 0xF) | ((i % 7 == 0) ? 0x80000000u : 0); };
    m.ring.assign(16 * (dtv2_cap_lines(N) + dtv2_cap_lines(N2)), 0);
    for (uint32_t i = 0; i < N; i++) { for (int b = 0; b < 4; b++) { m.ring[4 * i + b] = (uint8_t)(pat(i) >> (8 * b)); } }
    uint32_t second = dtv2_cap_lines(N);
    for (uint32_t i = 0; i < N2; i++) { for (int b = 0; b < 4; b++) { m.ring[16 * second + 4 * i + b] = (uint8_t)(~pat(i) >> (8 * b)); } }
    static uint32_t got[4096];
    CHECK(dtv2_cap_read(&m, 0, N, got));
    int bad = 0;
    for (uint32_t i = 0; i < N; i++) { bad += got[i] != pat(i); }
    CHECK_EQ(bad, 0);
    CHECK_EQ(m.reads, 1024);                                      // four samples per 16 byte line
    CHECK_EQ(m.raddr, 16u * 1024);
    uint32_t g2[6];
    CHECK(dtv2_cap_read(&m, second, N2, g2));                      // the second capture is where the first one ended
    for (uint32_t i = 0; i < N2; i++) { CHECK_EQ(g2[i], ~pat(i)); }
    CHECK_EQ(dtv2_cap_lines(1), 1u);
    CHECK_EQ(dtv2_cap_lines(4), 1u);
    CHECK_EQ(dtv2_cap_lines(5), 2u);
    // a reader that never says "valid" is given up on, not waited for
    struct Dead : V2Model { void on_wr(uint32_t r, uint8_t v) override { if (r != DV2_CAP_RCTRL) V2Model::on_wr(r, v); } } dead;
    uint32_t one[1];
    CHECK(!dtv2_cap_read(&dead, 0, 1, one));
    DONE("test_dtv2");
}
