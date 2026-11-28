// T91 D9 / T92 D9: a tape file as the shared engine's pulse stream. The expected words are made here from the TZX specification's timings
// (hardware facts), and every stream is also decoded back to the bytes the ROM loader would read.
#include <vector>
#include "check.h"
#include "tape_pulses.h"

typedef std::vector<uint8_t> Bytes;
typedef std::vector<uint16_t> Words;

class VecFile : public CoreFile {
public:
    Bytes d;
    bool fail = false;
    uint64_t size() { return d.size(); }
    bool writable() { return false; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        if (fail || off > d.size() || len > d.size() - off) return false;
        memcpy(buf, d.data() + off, len);
        return true;
    }
    bool write(uint64_t, const void *, uint32_t) { return false; }
    bool sync() { return true; }
};

class VecOut : public TapeOut {
public:
    Words w;
    bool refuse = false;
    bool put(const uint16_t *p, uint32_t n) { if (refuse) return false; w.insert(w.end(), p, p + n); return true; }
};

static void u16(Bytes &b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void u24(Bytes &b, unsigned v) { u16(b, v & 0xFFFF); b.push_back(v >> 16); }
static void u32(Bytes &b, unsigned v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
static void app(Bytes &b, const Bytes &x) { b.insert(b.end(), x.begin(), x.end()); }

static Bytes tzx_head() { Bytes b = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20 }; return b; }
static Bytes tap_block(const Bytes &data) { Bytes b; u16(b, data.size()); app(b, data); return b; }

// the words of the spec, built a second way: the pairs of one byte
static void bits(Words &w, unsigned byte, int nbits, unsigned t0, unsigned t1)
{
    for (int k = 0; k < nbits; k++) { unsigned t = (byte >> (7 - k)) & 1 ? t1 : t0; w.push_back(t); w.push_back(t); }
}
static void gapw(Words &w, uint32_t t) { w.push_back(0xFFFF); w.push_back(t & 0xFFFF); w.push_back(t >> 16); }
static void lead(Words &w, unsigned n, unsigned pilot, unsigned s1, unsigned s2)
{
    for (unsigned i = 0; i < n; i++) w.push_back(pilot);
    w.push_back(s1); w.push_back(s2);
}

// what the ROM loader's view would read: pilot run, the two sync pulses, then bit pairs until a pair is not 855/1710 pulses
static std::vector<Bytes> decode(const Words &w)
{
    std::vector<Bytes> out;
    size_t i = 0;
    while (i < w.size()) {
        size_t n = 0;
        while (i < w.size() && w[i] == 2168) { n++; i++; }
        if (n < 3000) { i++; continue; }
        if (i + 1 >= w.size() || w[i] != 667 || w[i + 1] != 735) { i++; continue; }
        i += 2;
        Bytes blk;
        unsigned cur = 0, nb = 0;
        while (i + 1 < w.size() && w[i] == w[i + 1] && (w[i] == 855 || w[i] == 1710)) {
            cur = (cur << 1) | (w[i] == 1710); i += 2;
            if (++nb == 8) { blk.push_back(cur); cur = 0; nb = 0; }
        }
        out.push_back(blk);
    }
    return out;
}

struct Result { int rc; Words w; TapeIndex idx; char msg[200]; };
static Result conv(const Bytes &file, TapeFormat fmt, bool stop48 = true, bool refuse = false, bool fail = false)
{
    VecFile f; f.d = file; f.fail = fail;
    VecOut o; o.refuse = refuse;
    Result r; memset(&r.idx, 0, sizeof(r.idx)); r.msg[0] = 0;
    r.rc = tape_convert(&f, fmt, stop48, &o, &r.idx, r.msg, sizeof(r.msg));
    r.w = o.w;
    return r;
}

int main()
{
    // (1) TAP: a header block (flag 0) and a data block (flag FF), the ROM loader's timings, 1000 ms after each
    Bytes hdr = { 0x00, 0x03, 'A', 'B', 0x5A, 0x00 }, dat = { 0xFF, 0x80, 0x01, 0x7E };
    Bytes tap = tap_block(hdr);
    app(tap, tap_block(dat));
    Result r = conv(tap, TAPE_TAP);
    CHECK_EQ(r.rc, 0);
    Words exp;
    size_t second = 0;
    for (int blk = 0; blk < 2; blk++) {
        const Bytes &d = blk ? dat : hdr;
        if (blk) second = exp.size();
        lead(exp, d[0] < 128 ? 8063 : 3223, 2168, 667, 735);
        for (uint8_t c : d) bits(exp, c, 8, 855, 1710);
        exp.push_back(3500);
        gapw(exp, 999 * 3500);
    }
    size_t used = exp.size();
    exp.push_back(0);
    while (exp.size() % 8) exp.push_back(0);
    CHECK(r.w == exp);
    CHECK_EQ(r.idx.words, used);
    CHECK_EQ(r.idx.nblocks, 2);
    CHECK_EQ(r.idx.start[0], 0);
    CHECK_EQ(r.idx.start[1], second);
    CHECK_EQ(r.w.size() % 8, 0);
    std::vector<Bytes> back = decode(r.w);
    CHECK_EQ(back.size(), 2);
    if (back.size() == 2) { CHECK(back[0] == hdr); CHECK(back[1] == dat); }
    CHECK_EQ(tape_block_at(&r.idx, 0), 0);
    CHECK_EQ(tape_block_at(&r.idx, second - 1), 0);
    CHECK_EQ(tape_block_at(&r.idx, second), 1);
    CHECK_EQ(tape_block_at(&r.idx, used + 5), 1);

    // (2) a TAP that stops inside a block is refused with that reason; an empty tape too
    Bytes cut = tap; cut.resize(cut.size() - 2);
    r = conv(cut, TAPE_TAP);
    CHECK(r.rc != 0 && strstr(r.msg, "ends inside"));
    r = conv(Bytes(), TAPE_TAP);
    CHECK(r.rc != 0 && strstr(r.msg, "no data"));

    // (3) TZX: standard block with a pause of 0 (no terminator), a turbo block with 3 used bits, a pure tone, pulses, pure data
    Bytes z = tzx_head();
    z.push_back(0x10); u16(z, 0); app(z, tap_block(dat));                       // pause 0
    z.push_back(0x11); u16(z, 1000); u16(z, 300); u16(z, 400); u16(z, 500); u16(z, 600); u16(z, 20); z.push_back(3); u16(z, 2); u24(z, 2);
    z.push_back(0xA0); z.push_back(0xE0);                                       // 2 bytes, the last holds 3 bits: 111
    z.push_back(0x12); u16(z, 1234); u16(z, 3);
    z.push_back(0x13); z.push_back(2); u16(z, 700); u16(z, 800);
    z.push_back(0x14); u16(z, 111); u16(z, 222); z.push_back(8); u16(z, 2); u24(z, 1); z.push_back(0x81);   // pause 2 ms
    r = conv(z, TAPE_TZX);
    CHECK_EQ(r.rc, 0);
    exp.clear();
    lead(exp, 3223, 2168, 667, 735);
    for (uint8_t c : dat) bits(exp, c, 8, 855, 1710);
    size_t b1 = exp.size();
    lead(exp, 20, 1000, 300, 400);
    bits(exp, 0xA0, 8, 500, 600);
    bits(exp, 0xE0, 3, 500, 600);
    exp.push_back(3500);                                                        // the turbo block's pause of 2 ms
    gapw(exp, 3500);
    size_t b2 = exp.size();
    for (int i = 0; i < 3; i++) exp.push_back(1234);
    size_t b3 = exp.size();
    exp.push_back(700); exp.push_back(800);
    size_t b4 = exp.size();
    bits(exp, 0x81, 8, 111, 222);
    exp.push_back(3500); gapw(exp, 3500);                                       // 2 ms: the terminator and 1 ms of silence
    used = exp.size();
    exp.push_back(0);
    while (exp.size() % 8) exp.push_back(0);
    CHECK(r.w == exp);
    CHECK_EQ(r.idx.nblocks, 5);
    CHECK_EQ(r.idx.start[0], 0);
    CHECK_EQ(r.idx.start[1], b1);
    CHECK_EQ(r.idx.start[2], b2);
    CHECK_EQ(r.idx.start[3], b3);
    CHECK_EQ(r.idx.start[4], b4);

    // (4) pauses and the stop marker: 0x20 with 500 ms is a gap, with 0 it stops the tape; 0x2A stops on the 48K only; groups and text are skipped
    z = tzx_head();
    z.push_back(0x21); z.push_back(3); z.push_back('a'); z.push_back('b'); z.push_back('c');
    z.push_back(0x30); z.push_back(2); z.push_back('h'); z.push_back('i');
    z.push_back(0x32); u16(z, 4); z.push_back(0); z.push_back(1); z.push_back(2); z.push_back(3);
    z.push_back(0x12); u16(z, 100); u16(z, 1);
    z.push_back(0x20); u16(z, 500);
    z.push_back(0x20); u16(z, 0);
    z.push_back(0x2A); u32(z, 0);
    z.push_back(0x2B); u32(z, 1); z.push_back(1);
    z.push_back(0x22);
    z.push_back(0x12); u16(z, 100); u16(z, 1);
    r = conv(z, TAPE_TZX, true);
    CHECK_EQ(r.rc, 0);
    exp = { 100 };
    gapw(exp, 500 * 3500);
    exp.push_back(0xFFFE);                                                      // the pause of 0
    exp.push_back(0xFFFE);                                                      // block 2A on a 48K
    exp.push_back(100);
    exp.push_back(0);
    while (exp.size() % 8) exp.push_back(0);
    CHECK(r.w == exp);
    r = conv(z, TAPE_TZX, false);
    exp = { 100 };
    gapw(exp, 500 * 3500);
    exp.push_back(0xFFFE);
    exp.push_back(100);
    exp.push_back(0);
    while (exp.size() % 8) exp.push_back(0);
    CHECK(r.rc == 0 && r.w == exp);

    // (5) a loop: block 24 with 3 repeats around two pulses
    z = tzx_head();
    z.push_back(0x24); u16(z, 3);
    z.push_back(0x13); z.push_back(2); u16(z, 50); u16(z, 60);
    z.push_back(0x25);
    r = conv(z, TAPE_TZX);
    exp = { 50, 60, 50, 60, 50, 60, 0, 0 };
    CHECK(r.rc == 0 && r.w == exp);
    CHECK_EQ(r.idx.nblocks, 3);                                                 // one entry per pass through the block
    z = tzx_head();
    z.push_back(0x24); u16(z, 2); z.push_back(0x24); u16(z, 2);
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "loops inside"));
    z = tzx_head(); z.push_back(0x25);
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "without a loop start"));
    z = tzx_head(); z.push_back(0x24); u16(z, 2); z.push_back(0x12); u16(z, 100); u16(z, 1);
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "inside a loop"));

    // (6) direct recording at 100 ticks a sample: 1100 0011 | 1 -> runs 2, 4, 3 (the last byte holds 1 bit)
    z = tzx_head();
    z.push_back(0x15); u16(z, 100); u16(z, 0); z.push_back(1); u24(z, 2); z.push_back(0xC3); z.push_back(0x80);
    r = conv(z, TAPE_TZX);
    // 1100 0011 1: ones(2) zeros(4) ones(2+1=3): the pulses are 200, 400, 300
    exp = { 200, 400, 300, 0, 0, 0, 0, 0 };
    CHECK(r.rc == 0 && r.w == exp);

    // (7) a pulse longer than a word: 70000 ticks are a gap of 69984 and a pulse of 16; 65533 is still one word
    z = tzx_head();
    z.push_back(0x13); z.push_back(2); u16(z, 65533); u16(z, 30000);
    r = conv(z, TAPE_TZX);
    exp = { 65533, 30000, 0, 0, 0, 0, 0, 0 };
    CHECK(r.rc == 0 && r.w == exp);
    z = tzx_head();
    z.push_back(0x15); u16(z, 70000 & 0xFFFF); u16(z, 0); z.push_back(8); u24(z, 1); z.push_back(0x00);   // 8 samples of 4464 ticks = 35712: one word
    r = conv(z, TAPE_TZX);
    CHECK(r.rc == 0 && r.w.size() == 8 && r.w[0] == 35712);
    z = tzx_head();
    z.push_back(0x15); u16(z, 20000); u16(z, 0); z.push_back(8); u24(z, 1); z.push_back(0x00);               // 160000 ticks
    r = conv(z, TAPE_TZX);
    exp = { 0xFFFF, (uint16_t)((160000 - 16) & 0xFFFF), (uint16_t)((160000 - 16) >> 16), 16, 0, 0, 0, 0 };
    CHECK(r.rc == 0 && r.w == exp);

    // the end marker is a word of its own even when the stream fills its last line exactly
    z = tzx_head();
    z.push_back(0x12); u16(z, 100); u16(z, 8);
    r = conv(z, TAPE_TZX);
    CHECK(r.rc == 0 && r.w.size() == 16 && r.w[7] == 100 && r.w[8] == 0);

    // (8) what the engine cannot play is refused, each with its reason
    struct Bad { uint8_t id; const char *text; } bad[] = {
        { 0x16, "C64" }, { 0x18, "CSW" }, { 0x19, "generalized" }, { 0x23, "jump" }, { 0x26, "call" }, { 0x27, "return" },
        { 0x28, "selection" }, { 0x99, "not known" },
    };
    for (auto &b : bad) {
        z = tzx_head();
        z.push_back(b.id);
        r = conv(z, TAPE_TZX);
        CHECK(r.rc != 0 && strstr(r.msg, b.text));
    }
    z = tzx_head(); z.push_back(0x12); u16(z, 10); u16(z, 5);                    // a pulse of 10 ticks: a turbo loader
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "shorter than 16"));
    z = tzx_head(); z.push_back(0x12); u16(z, 16); u16(z, 1);                    // 16 is the shortest there is
    r = conv(z, TAPE_TZX);
    CHECK_EQ(r.rc, 0);
    Bytes nosig = z; nosig[0] = 'Y';
    r = conv(nosig, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "not a TZX"));
    z = tzx_head(); z.push_back(0x10); u16(z, 0); u16(z, 50); z.push_back(0xFF);   // block says 50 bytes, the file has 1
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "ends inside"));
    z = tzx_head(); z.push_back(0x21); z.push_back(9);                           // a group name that runs off the end
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0);

    // (9) more than 4 MB of pulses is refused; the read and write failures say so
    z = tzx_head();
    for (int i = 0; i < 33; i++) { z.push_back(0x12); u16(z, 100); u16(z, 65535); }
    r = conv(z, TAPE_TZX);
    CHECK(r.rc != 0 && strstr(r.msg, "4 MB"));
    r = conv(tap, TAPE_TAP, true, true);
    CHECK(r.rc != 0 && strstr(r.msg, "could not be written"));
    r = conv(tap, TAPE_TAP, true, false, true);
    CHECK(r.rc != 0 && strstr(r.msg, "cannot read"));

    // (10) a tape that has blocks beyond the index keeps the first TAPE_BLOCKS_MAX and says so
    z = tzx_head();
    for (int i = 0; i < TAPE_BLOCKS_MAX + 4; i++) { z.push_back(0x12); u16(z, 100); u16(z, 1); }
    r = conv(z, TAPE_TZX);
    CHECK(r.rc == 0 && r.idx.nblocks == TAPE_BLOCKS_MAX && r.idx.truncated);
    CHECK_EQ(r.idx.start[TAPE_BLOCKS_MAX - 1], TAPE_BLOCKS_MAX - 1);

    // (10b) T134: a Commodore .tap (version 1): values of 8 cycles, 0 + 24 bit = a cycle count; each pulse is 100 us low, then high to the falling edge
    {
        Bytes c64 = { 'C','6','4','-','T','A','P','E','-','R','A','W', 1, 0, 0, 0 };
        u32(c64, 8);                                              // data size: 0x30, 0x56, 0 + 70000 cycles (a pause longer than one word)
        c64.push_back(0x30); c64.push_back(0x56); c64.push_back(0); u24(c64, 70000);
        Result t = conv(c64, TAPE_C64);
        CHECK_EQ(t.rc, 0);
        Words e = { 100, 0x30 * 8 - 100, 100, 0x56 * 8 - 100, 100, 0xFFFF, (70000 - 100 - 16) & 0xFFFF, (70000 - 100 - 16) >> 16, 16 };
        e.push_back(0);                                           // the end marker, then zeros to a whole line
        while (e.size() % 8) e.push_back(0);
        CHECK(t.w == e);
        CHECK_EQ(t.idx.words, 9);
        CHECK_EQ(t.idx.nblocks, 1);
        Bytes bad = c64; bad[0] = 'X';
        t = conv(bad, TAPE_C64);
        CHECK(t.rc != 0 && strstr(t.msg, "signature"));
        Bytes v2 = c64; v2[12] = 2;
        t = conv(v2, TAPE_C64);
        CHECK(t.rc != 0 && strstr(t.msg, "version"));
        Bytes shortp = { 'C','6','4','-','T','A','P','E','-','R','A','W', 1, 0, 0, 0 };
        u32(shortp, 1); shortp.push_back(0x05);
        t = conv(shortp, TAPE_C64);
        CHECK(t.rc != 0);
    }

    // (11) the extension decides
    TapeFormat f;
    CHECK(tape_format_of("/usb0/Game.TAP", &f) && f == TAPE_TAP);
    CHECK(tape_format_of("/usb0/game.tzx", &f) && f == TAPE_TZX);
    CHECK(tape_format_of("/usb0/game.CDT", &f) && f == TAPE_TZX);
    CHECK(!tape_format_of("/usb0/game.sna", &f));
    CHECK(!tape_format_of("/usb0/game", &f));
    DONE("test_tape");
}
