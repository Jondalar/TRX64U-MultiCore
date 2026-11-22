// Host test of the .cfw reader and the DDR2 address map (T115 S1, S2): host-app/software/hostapp/cfw.cc over synthetic
// images that reproduce the two layouts measured on 2026-10-21 (Gideon's 3.15a and the TRX64U rc1 release), byte for byte
// where it matters: the offsets of the .bit files and of the application record, the header sizes, the IDCODEs, the decoys.
//   test_cfw                  the vectors
//   test_cfw --real <.cfw>... prints what the parser makes of real files (by hand, not in CI)
#include "cfw.h"
#include "handover.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

typedef std::vector<uint8_t> Buf;

struct MemSrc : CorSource {
    Buf d;
    unsigned reads = 0;
    uint32_t size() { return d.size(); }
    bool read(uint32_t off, void *b, uint32_t n) {
        reads++;
        if ((uint64_t)off + n > d.size()) return false;
        memcpy(b, d.data() + off, n);
        return true;
    }
};

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void be32(Buf &b, uint32_t v) { b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v); }
static void put_le32(Buf &b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = v >> (8 * i); }
static void put_be16(Buf &b, uint32_t v) { b.push_back(v >> 8); b.push_back(v); }
static void put_str(Buf &b, char key, const char *s)
{
    b.push_back(key);
    put_be16(b, strlen(s) + 1);
    b.insert(b.end(), s, s + strlen(s) + 1);
}

#define ID_50T  0x0362C093u
#define ID_100T 0x03631093u
#define AXSS_VAL 0x553525D3u

struct BitSpec {
    const char *a, *b, *c, *d;
    uint32_t idcode;
    bool axss;              // writes the AXSS register
    bool axss_late;         // ... after the frame data instead of before it
    bool fake_axss;         // the frame data holds a word that looks like an AXSS write (not a write)
    bool packed;            // a compressed stream: many small packets, no type-2 frame data
    uint32_t data_len;
};

static const char *A_GIDEON = "u64_mk2_artix;UserID=0XFFFFFFFF;Version=2024.1";
static const char *A_TRX = "top;COMPRESS=TRUE;UserID=0XFFFFFFFF;Version=2024.1";

// The .bit file: the header fields a-d, 'e' and the length, then the configuration packets (sync, IDCODE, ..., DESYNC).
static Buf make_bit(const BitSpec &s)
{
    static const uint8_t magic[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };
    Buf b(magic, magic + 13);
    put_str(b, 'a', s.a); put_str(b, 'b', s.b); put_str(b, 'c', s.c); put_str(b, 'd', s.d);
    b.push_back('e');
    be32(b, s.data_len);
    Buf w;                                          // the configuration words
    be32(w, 0xFFFFFFFF); be32(w, 0xFFFFFFFF); be32(w, 0x000000BB); be32(w, 0x11220044);
    be32(w, 0xFFFFFFFF); be32(w, 0xFFFFFFFF); be32(w, 0xAA995566); be32(w, 0x20000000);
    be32(w, 0x30008001); be32(w, 0x00000000);      // CMD := NULL
    be32(w, 0x30018001); be32(w, s.idcode);        // IDCODE
    if (s.axss && !s.axss_late) { be32(w, 0x3001A001); be32(w, AXSS_VAL); }
    size_t tail_words = 2 + 6;                      // DESYNC and the NOPs behind it
    size_t late = (s.axss && s.axss_late) ? 2 : 0;
    size_t total_words = s.data_len / 4;
    if (!s.packed) {
        be32(w, 0x30004000);                        // FDRI, count 0, then a type-2 packet with the frames
        size_t n = total_words - w.size() / 4 - 1 - late - tail_words;
        be32(w, 0x50000000u | (uint32_t)n);
        for (size_t i = 0; i < n; i++) {
            uint32_t v = (uint32_t)(i * 2654435761u);
            if (s.fake_axss && i == n / 2) v = 0x3001A001;      // the word, not a packet header
            if (s.fake_axss && i == n / 2 + 1) v = AXSS_VAL;
            be32(w, v == 0xAA995566 ? 0 : v);
        }
    } else {
        size_t pairs = (total_words - w.size() / 4 - late - tail_words) / 2;
        for (size_t i = 0; i < pairs; i++) { be32(w, 0x30002001); be32(w, (uint32_t)(i * 40503u)); }   // FAR := ...
    }
    if (late) { be32(w, 0x3001A001); be32(w, AXSS_VAL); }
    be32(w, 0x30008001); be32(w, 0x0000000D);      // CMD := DESYNC
    while (w.size() / 4 < total_words) be32(w, 0x20000000);
    b.insert(b.end(), w.begin(), w.end());
    return b;
}

static void put(Buf &img, size_t at, const Buf &b) { memcpy(img.data() + at, b.data(), b.size()); }

// filler that cannot form a header by accident
static Buf filler(size_t n) { Buf b(n); for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(0x41 + ((i * 7) % 23)); return b; }

static void put_record(Buf &img, size_t at, uint32_t load, uint32_t len, uint32_t start)
{
    put_le32(img, at, load); put_le32(img, at + 4, len); put_le32(img, at + 8, start);
    for (uint32_t i = 0; i < len && at + 12 + i < img.size(); i++) img[at + 12 + i] = (uint8_t)(i * 5);
}

static const uint8_t PRE[10] = { 0xDE, 0xAD, 0x15, 0x41, 'U', '6', '4', 'M', 'K', '2' };

// Gideon's 3.15a: 50T .bit at +0x2FCB0, the 100T .bit behind it, the application record after the LAST bitstream at
// +0x5ED0A0, blobs (ESP32, ROMs, HTML) behind the application.
static Buf gideon(bool swap_order = false)
{
    Buf img = filler(0x867B7C);
    memcpy(img.data(), PRE, sizeof(PRE));
    BitSpec b50 = { A_GIDEON, "7a50tfgg484", "2026/09/10", "11:00:57", ID_50T, false, false, false, false, 0x21728C };
    BitSpec b100 = { A_GIDEON, "7a100tfgg484", "2026/09/10", "11:05:11", ID_100T, false, false, false, false, 0x3A607C };
    if (!swap_order) {
        put(img, 0x2FCB0, make_bit(b50));
        put(img, 0x246FB0, make_bit(b100));
        put_record(img, 0x5ED0A0, 0x30000, 0x12C978, 0x30000);
    } else {                                        // the 100T first: the same sizes, shifted so the 50T ends where the app is
        put(img, 0x2FCB0, make_bit(b100));
        size_t e = 0x2FCB0 + make_bit(b100).size();
        e = (e + 7) & ~7u;
        put(img, e, make_bit(b50));
        size_t e2 = (e + make_bit(b50).size() + 3 + 4) & ~3u;
        put_record(img, e2, 0x30000, 0x12C978, 0x30000);
    }
    return img;
}

// TRX64U rc1 (692f643d): one compressed 50T .bit at +0x42AD8 behind a decoy record at +0x42AA8, a decoy of the .bit magic in
// the updater's code at +0x39B54, the application record at +0x20365C and the file ends with the application.
static Buf trx64u(void)
{
    Buf img = filler(0x35DF94);
    memcpy(img.data(), PRE, sizeof(PRE));
    static const uint8_t magic[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };
    memcpy(img.data() + 0x39B54, magic, 13);        // 'a' 00 00 ...: not a header
    img[0x39B54 + 13] = 'a'; img[0x39B54 + 14] = 0; img[0x39B54 + 15] = 0;
    put_record(img, 0x42AA8, 0x30000, 0x15A92C, 0x30000);
    BitSpec t = { A_TRX, "7a50tfgg484", "2026/10/10", "18:23:21", ID_50T, true, false, false, true, 0x1C0B10 };
    put(img, 0x42AD8, make_bit(t));
    put_record(img, 0x20365C, 0x30000, 0x15A92C, 0x30000);
    return img;
}

static unsigned last_reads;
static cfw_result_t probe(const Buf &img, CfwInfo &ci, uint32_t running = ID_50T)
{
    MemSrc s;
    s.d = img;
    cfw_result_t r = cfw_probe(s, running, ci);
    last_reads = s.reads;
    return r;
}

static void s1(void)
{
    CfwInfo ci;
    // ---- Gideon's layout
    Buf g = gideon();
    CHECK(probe(g, ci) == CFW_OK);
    CHECK(ci.bit_off == 0x2FCB0 && ci.bit_len == 0x2172F9);             // the measured 50T .bit: 2,192,121 bytes
    CHECK(ci.app_off == 0x5ED0A0 && ci.app_len == 0x12C978 && ci.app_load == 0x30000 && ci.app_entry == 0x30000);
    CHECK(ci.map == CFW_MAP_GIDEON_BL4);                                // no AXSS write
    CHECK(ci.idcode == ID_50T && !strcmp(ci.part, "7a50tfgg484"));
    CHECK(!strcmp(ci.name, "u64_mk2_artix 2026/09/10 11:00:57"));
    // the 100T first and the 50T second: the 50T is still the choice and the record follows the LAST bitstream
    {
        CfwInfo c2;
        Buf g2 = gideon(true);
        CHECK(probe(g2, c2) == CFW_OK && c2.idcode == ID_50T && c2.bit_off > 0x2FCB0 && c2.app_off > c2.bit_off + c2.bit_len);
    }
    // the same image on a 100T host picks the other .bit (the choice is the running IDCODE's, not "the first")
    {
        CfwInfo c3;
        CHECK(probe(g, c3, ID_100T) == CFW_OK && c3.idcode == ID_100T && c3.bit_off == 0x246FB0 && c3.bit_len == 0x3A60EA);
        CHECK(probe(g, c3, ID_50T | 0xA0000000u) == CFW_OK && c3.idcode == ID_50T);       // the version nibble does not count
    }
    // ---- TRX64U's layout
    Buf t = trx64u();
    CHECK(probe(t, ci) == CFW_OK);
    CHECK(ci.bit_off == 0x42AD8 && ci.bit_len == 0x203659 - 0x42AD8);
    CHECK(ci.app_off == 0x20365C && ci.app_len == 0x15A92C && ci.app_load == 0x30000);
    CHECK(ci.app_off != 0x42AA8);                                        // not the record before the bitstream
    CHECK(ci.map == CFW_MAP_IDENTITY);                                   // AXSS written
    CHECK(!strcmp(ci.name, "top 2026/10/10 18:23:21"));
    // a menu scan must stay cheap on a medium: the 1.8 MB compressed stream is not walked to its end once IDCODE and AXSS are known
    CHECK(last_reads < 400);
    probe(g, ci);
    CHECK(last_reads < 400);                                             // and the uncompressed one is crossed by its type-2 packets

    // ---- map by the AXSS register, on the packets and not on a bit pattern
    {
        Buf img = gideon();
        BitSpec late = { A_GIDEON, "7a50tfgg484", "2026/09/10", "11:00:57", ID_50T, true, true, false, false, 0x21728C };
        put(img, 0x2FCB0, make_bit(late));
        CHECK(probe(img, ci) == CFW_OK && ci.map == CFW_MAP_IDENTITY);   // AXSS behind the frame data: the type-2 packet is skipped
        BitSpec fake = { A_GIDEON, "7a50tfgg484", "2026/09/10", "11:00:57", ID_50T, false, false, true, false, 0x21728C };
        put(img, 0x2FCB0, make_bit(fake));
        CHECK(probe(img, ci) == CFW_OK && ci.map == CFW_MAP_GIDEON_BL4); // the word inside the frames is data, not a write
    }

    // ---- refusals
    {   // truncated inside the 50T .bit
        Buf img(g.begin(), g.begin() + 0x100000);
        CHECK(probe(img, ci) == CFW_ERR_TRUNCATED);
    }
    {   // truncated inside the application
        Buf img(g.begin(), g.begin() + 0x5ED0A0 + 12 + 0x12C978 - 1);
        CHECK(probe(img, ci) == CFW_ERR_TRUNCATED);
        Buf ok(g.begin(), g.begin() + 0x5ED0A0 + 12 + 0x12C978);          // the control: cut exactly at the end it is fine
        CHECK(probe(ok, ci) == CFW_OK);
    }
    {   // cut right behind the last bitstream: the record is not there
        Buf img(g.begin(), g.begin() + 0x5ED09A);
        CHECK(probe(img, ci) == CFW_ERR_TRUNCATED);
    }
    {   // only the 100T: nothing for a 50T host
        Buf img = filler(0x700000);
        memcpy(img.data(), PRE, sizeof(PRE));
        BitSpec b100 = { A_GIDEON, "7a100tfgg484", "2026/09/10", "11:05:11", ID_100T, false, false, false, false, 0x3A607C };
        put(img, 0x2FCB0, make_bit(b100));
        put_record(img, 0x2FCB0 + make_bit(b100).size() + 6, 0x30000, 0x12C978, 0x30000);
        CHECK(probe(img, ci) == CFW_ERR_NO_50T);
    }
    {   // the text says 7a50t, the IDCODE inside is the 100T's: refused (text is only a name) ...
        Buf img = gideon();
        BitSpec lie = { A_GIDEON, "7a50tfgg484", "2026/09/10", "11:00:57", ID_100T, false, false, false, false, 0x21728C };
        BitSpec b100 = { A_GIDEON, "7a100tfgg484", "2026/09/10", "11:05:11", ID_100T, false, false, false, false, 0x3A607C };
        put(img, 0x2FCB0, make_bit(lie));
        put(img, 0x246FB0, make_bit(b100));
        CHECK(probe(img, ci) == CFW_ERR_NO_50T);
        // ... and the text says 7a100t over the 50T's IDCODE: taken
        BitSpec lie2 = { A_GIDEON, "7a100tfgg484", "2026/09/10", "11:00:57", ID_50T, false, false, false, false, 0x21728C };
        put(img, 0x2FCB0, make_bit(lie2));
        CHECK(probe(img, ci) == CFW_OK && ci.idcode == ID_50T && !strcmp(ci.part, "7a100tfgg484"));
    }
    {   // wrong load / wrong start
        Buf img = gideon();
        put_le32(img, 0x5ED0A0, 0x40000);
        put_le32(img, 0x5ED0A0 + 8, 0x40000);
        CHECK(probe(img, ci) == CFW_ERR_LOAD);
        put_le32(img, 0x5ED0A0, 0x30000);
        put_le32(img, 0x5ED0A0 + 8, 0x30010);
        CHECK(probe(img, ci) == CFW_ERR_LOAD);
        put_le32(img, 0x5ED0A0 + 8, 0x30000);
        CHECK(probe(img, ci) == CFW_OK);                                  // the control: both right again
    }
    {   // an application the staging area cannot hold
        Buf img = gideon();
        put_le32(img, 0x5ED0A0 + 4, CFW_APP_MAX + 4);
        CHECK(probe(img, ci) == CFW_ERR_RANGE || probe(img, ci) == CFW_ERR_TRUNCATED);
        img.resize(0x5ED0A0 + 12 + CFW_APP_MAX + 4 + 16, 0x55);
        CHECK(probe(img, ci) == CFW_ERR_RANGE);
    }
    {   // no application record behind the bitstreams
        Buf img = gideon();
        memset(img.data() + 0x5ED09A, 0xEE, 0x40);
        CHECK(probe(img, ci) == CFW_ERR_NO_APP);
    }
    {   // not a firmware image / too small / no bitstream at all
        Buf img = gideon();
        img[3] ^= 1;
        CHECK(probe(img, ci) == CFW_ERR_FORMAT);
        Buf small(100, 0);
        CHECK(probe(small, ci) == CFW_ERR_FORMAT);
        Buf none = filler(0x10000);
        memcpy(none.data(), PRE, sizeof(PRE));
        CHECK(probe(none, ci) == CFW_ERR_NO_BIT);
        Buf decoy_only = trx64u();
        memset(decoy_only.data() + 0x42AD8, 0x41, 0x71);                  // the real header gone: only the decoy magic is left
        CHECK(probe(decoy_only, ci) == CFW_ERR_NO_BIT);
    }
    {   // alignment bytes between the last .bit and the record: up to 12 (the window is 16 bytes from the unaligned end) are fine, whatever they are. Zero padding in front of a
        // small application once made a window {0, load, length} look like a record that loads at 0 (found by the menu bench).
        for (int pad = 0; pad <= 20; pad += 4) {
            for (int fillv = 0; fillv < 2; fillv++) {
                Buf img = filler(0x4000);
                memcpy(img.data(), PRE, sizeof(PRE));
                BitSpec t = { A_TRX, "7a50tfgg484", "2026/10/10", "18:23:21", ID_50T, true, false, false, true, 0x200 };
                Buf bit = make_bit(t);
                put(img, 0x2000, bit);
                size_t at = 0x2000 + bit.size();
                at = (at + 3) & ~(size_t)3;
                for (int i = 0; i < pad; i++) img[at + i] = fillv ? 0xFF : 0x00;
                img.resize(at + pad + 12 + 0x10100);
                put_record(img, at + pad, 0x30000, 0x10100, 0x30000);
                cfw_result_t r = probe(img, ci);
                if (pad <= 12) {
                    CHECK(r == CFW_OK && ci.app_off == at + pad && ci.app_len == 0x10100);
                } else if (pad >= 20) {
                    CHECK(r == CFW_ERR_NO_APP);                                  // out of reach: nothing is found
                }
            }
        }
    }
    CHECK(cfw_is_name("x.cfw") && cfw_is_name("/Usb0/A.CFW") && !cfw_is_name(".cfw") && !cfw_is_name("x.cor") && !cfw_is_name("cfw"));
    CHECK(!strcmp(cfw_result_str(CFW_ERR_NO_50T), "no bitstream for this FPGA in the firmware image"));
}

static void s2(void)
{
    // The measured map (T115 section 3.3), written out bit by bit: ours[3] = theirs[6], ours[6:4] = theirs[5:3], every
    // other bit equal. Independent of the code under test (no shifts of groups).
    for (uint32_t g = 0; g < 8192; g++) {
        uint32_t o = 0;
        for (int bit = 0; bit < 32; bit++) {
            int from = bit;
            if (bit == 3) from = 6;
            else if (bit == 4) from = 3;
            else if (bit == 5) from = 4;
            else if (bit == 6) from = 5;
            o |= ((g >> from) & 1u) << bit;
        }
        CHECK(cfw_map_off(CFW_MAP_GIDEON_BL4, g) == o);
        CHECK(cfw_unmap_off(CFW_MAP_GIDEON_BL4, o) == g);
        CHECK(cfw_map_off(CFW_MAP_IDENTITY, g) == g && cfw_unmap_off(CFW_MAP_IDENTITY, g) == g);
        CHECK((cfw_map_off(CFW_MAP_GIDEON_BL4, g) >> 7) == (g >> 7));    // every 128-byte block stays where it is
        CHECK((cfw_map_off(CFW_MAP_GIDEON_BL4, g) & 7) == (g & 7));      // and the bytes of an 8-byte unit stay together
    }
    // a permutation of the 16 units of every block
    {
        bool seen[16] = { false };
        for (uint32_t u = 0; u < 16; u++) seen[cfw_map_off(CFW_MAP_GIDEON_BL4, u * 8) >> 3] = true;
        bool all = true;
        for (int i = 0; i < 16; i++) all &= seen[i];
        CHECK(all);
    }
    // measured points: 0x40 -> 0x08, 0x08 -> 0x10, 0x38 -> 0x70, 0x78 -> 0x78
    CHECK(cfw_map_off(CFW_MAP_GIDEON_BL4, 0x40) == 0x08 && cfw_map_off(CFW_MAP_GIDEON_BL4, 0x08) == 0x10);
    CHECK(cfw_map_off(CFW_MAP_GIDEON_BL4, 0x38) == 0x70 && cfw_map_off(CFW_MAP_GIDEON_BL4, 0x78) == 0x78);
    // the magic words map to themselves (bits 6:3 are all one at 0xFFF8-0xFFFF), under both maps
    for (uint32_t a = 0xFFF8; a < 0x10000; a++) {
        CHECK(cfw_map_off(CFW_MAP_GIDEON_BL4, a) == a && cfw_map_off(CFW_MAP_IDENTITY, a) == a);
    }
    CHECK(CFW_BOOT_JUMP_ADDR == 0xFFF8 && CFW_BOOT_MAGIC_ADDR == 0xFFFC && CFW_BOOT_MAGIC == 0x1571BABEu);
    // staging: what the full firmware's CPU reads at application offset o is stage[map(o)]; it must be app[o]
    {
        const uint32_t len = 1000, padded = 1024;
        Buf app(padded, 0);
        for (uint32_t i = 0; i < len; i++) app[i] = (uint8_t)(i * 31 + 7);
        for (int kind = 0; kind < 2; kind++) {
            alignas(128) static uint8_t stage[1024];
            memset(stage, 0xEE, sizeof(stage));
            // in two chunks, as the board stages it
            cfw_stage_copy(kind, stage, 0, app.data(), 512);
            cfw_stage_copy(kind, stage, 512, app.data() + 512, 512);
            bool ok = true, moved = false;
            for (uint32_t o = 0; o < padded; o++) {
                uint32_t at = kind == CFW_MAP_IDENTITY ? o : (o & ~0x78u) | (((o >> 6) & 1) << 3) | (((o >> 3) & 7) << 4);
                ok &= stage[at] == app[o];
                moved |= stage[o] != app[o];
            }
            CHECK(ok);
            CHECK(moved == (kind == CFW_MAP_GIDEON_BL4));               // the control: gideon_bl4 really permutes, identity does not
        }
    }
    // T112 Q1: the hand-over page of a legacy start, read the way the application's CPU reads it (view[o] = page[map(o)])
    for (int kind = 0; kind < 2; kind++) {
        for (int large = 0; large < 2; large++) {
            CfwInfo ci;
            memset(&ci, 0, sizeof(ci));
            ci.map = kind;
            ci.from_cor = large != 0;
            snprintf(ci.name, sizeof(ci.name), "Amiga 1200 (AGA, 68020)");
            snprintf(ci.id, sizeof(ci.id), "amiga1200");
            alignas(128) static uint8_t page[CFW_HANDOVER_BYTES];
            memset(page, 0xEE, sizeof(page));
            cfw_handover_page(ci, "/Usb0/cores/trx64/trx64.cor", page);
            Buf view(CFW_HANDOVER_BYTES);
            for (uint32_t o = 0; o < CFW_HANDOVER_BYTES; o++) view[o] = page[cfw_map_off(kind, o)];
            const Handover *h = (const Handover *)view.data();
            if (large) {
                CHECK(handover_valid(h));
                CHECK(!strcmp(h->path, "/Usb0/cores/trx64/trx64.cor"));
                CHECK(h->video == 0 && !strcmp(h->core_name, "Amiga 1200 (AGA, 68020)"));   // the name picks among descriptors sharing an ID
                CHECK(!strcmp(handover_core_id(h), "amiga1200"));                              // T115 7 I7: and the id first
            } else {
                // a .cfw: wiped, never a valid block (the control: the page was 0xEE before)
                bool zero = true;
                for (uint32_t o = 0; o < CFW_HANDOVER_BYTES; o++) zero &= view[o] == 0;
                CHECK(zero);
                CHECK(!handover_valid(h));
                CHECK(!strcmp(handover_core_id(h), ""));
            }
        }
    }
    // IPROG with WBSTAR = 0x411000 (the PoC's measured run): the bytes in file order
    {
        static const uint8_t want[CFW_IPROG_LEN] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x99, 0x55, 0x66, 0x20, 0x00, 0x00, 0x00, 0x30, 0x02, 0x00, 0x01,
            0x00, 0x41, 0x10, 0x00, 0x30, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00 };
        uint8_t got[CFW_IPROG_LEN];
        cfw_iprog_seq(0x411000, got);
        CHECK(!memcmp(got, want, sizeof(want)));
        cfw_iprog_seq(0x0A0B0C0D, got);
        CHECK(got[16] == 0x0A && got[17] == 0x0B && got[18] == 0x0C && got[19] == 0x0D);
    }
}

static int real(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        MemSrc s;
        FILE *f = fopen(argv[i], "rb");
        if (!f) { printf("cannot open %s\n", argv[i]); return 1; }
        uint8_t b[65536]; size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0) s.d.insert(s.d.end(), b, b + n);
        fclose(f);
        CfwInfo ci;
        cfw_result_t r = cfw_probe(s, ID_50T, ci);
        printf("%s: %s (%u reads of the source)\n", argv[i], cfw_result_str(r), s.reads);
        if (r == CFW_OK) {
            printf("  name '%s' part %s idcode %08x map %s\n  bit +0x%X len 0x%X  app +0x%X len 0x%X load 0x%X start 0x%X\n", ci.name, ci.part, ci.idcode,
                   ci.map == CFW_MAP_IDENTITY ? "identity" : "gideon_bl4", ci.bit_off, ci.bit_len, ci.app_off, ci.app_len, ci.app_load, ci.app_entry);
        }
    }
    return 0;
}

// T115 D9: a .ue2 is the .cfw without its 36-byte pre-header; it starts with the updater's record {0x30000, length, 0x30000}.
static void s_ue2(void)
{
    CfwInfo c, u;
    Buf t = trx64u();
    put_le32(t, 36, 0x30000); put_le32(t, 40, 0x25E788); put_le32(t, 44, 0x30000);   // the updater's record, as in the real file
    CHECK(probe(t, c) == CFW_OK);
    Buf ue2(t.begin() + 36, t.end());
    CHECK(probe(ue2, u) == CFW_OK);
    CHECK(u.bit_off == c.bit_off - 36 && u.bit_len == c.bit_len && u.app_off == c.app_off - 36 && u.app_len == c.app_len);
    CHECK(u.idcode == c.idcode && u.map == c.map && !strcmp(u.name, c.name));
    // the real files: the updater's record loads at 0x03000000 (Commodore 1.1.0, Gideon 3.14, vanilla 3.15a after its pre-header)
    Buf real_ue2 = ue2;
    put_le32(real_ue2, 0, 0x03000000); put_le32(real_ue2, 8, 0x03000000);
    CHECK(probe(real_ue2, u) == CFW_OK && u.bit_off == c.bit_off - 36);
    put_le32(real_ue2, 8, 0x03000010);                                  // load and start differ: not the updater's record
    CHECK(probe(real_ue2, u) == CFW_ERR_FORMAT);
    // neither the pre-header nor the record: not a firmware image
    Buf bad = ue2;
    put_le32(bad, 0, 0x40000);
    CHECK(probe(bad, u) == CFW_ERR_FORMAT);
    CHECK(cfw_is_name("x.ue2") && cfw_is_name("/USB1/C64U_V1.1.0.UE2") && !cfw_is_name(".ue2"));
}

// T254: the name and version a foreign firmware gives itself (cfw_read_self): the line of its version report in the application, read in the
// application's last 512 KiB; the distances from the end are the measured ones (cfw.cc).
static void put_text(Buf &b, size_t at, const char *s, size_t n = 0) { memcpy(b.data() + at, s, n ? n : strlen(s)); }

static bool self_of(const Buf &img, CfwInfo &ci, unsigned *reads = 0)
{
    MemSrc s;
    s.d = img;
    if (cfw_probe(s, ID_50T, ci) != CFW_OK) return false;
    s.reads = 0;
    bool r = cfw_read_self(s, ci);
    if (reads) *reads = s.reads;
    return r;
}

static void s_self(void)
{
    CfwInfo ci;
    unsigned reads = 0;
    Buf g = gideon();
    CHECK(probe(g, ci) == CFW_OK);
    const size_t gend = ci.app_data + ci.app_len;
    // the updater before the bitstreams holds both texts and a version: not the application's, not read
    put_text(g, 0x1000, "TRX64 Ultimate");
    put_text(g, 0x2000, "Software version:   9.9\n");
    // no line in the application: nothing named, the title stays the .bit header's
    CHECK(!self_of(g, ci) && !ci.self_name[0] && !ci.self_ver[0]);
    CHECK(!strcmp(cfw_title(ci), "u64_mk2_artix 2026/09/10 11:00:57"));
    // Gideon: "3.15", 250 KB before the end, as in 3.15a
    Buf g315 = g;
    put_text(g315, gend - 250000, "Software version:   3.15\n   Build date:");
    CHECK(self_of(g315, ci, &reads) && !strcmp(ci.self_ver, "3.15") && !strcmp(ci.self_name, "Gideon 3.15"));
    CHECK(!strcmp(cfw_title(ci), "Gideon 3.15") && !strcmp(ci.name, "u64_mk2_artix 2026/09/10 11:00:57"));   // the header's name stays (the icon reads it)
    CHECK(reads <= 33);                                                  // the last 512 KiB in 16 KiB pieces: a third of a 1.2 MB application
    // Commodore: "1.2.1"; any other first digit: "Firmware"
    Buf g121 = g;
    put_text(g121, gend - 249580, "Software version:   1.2.1\n");
    CHECK(self_of(g121, ci) && !strcmp(ci.self_name, "Commodore 1.2.1") && !strcmp(ci.self_ver, "1.2.1"));
    Buf g20 = g;
    put_text(g20, gend - 1000, "Software version:   2.0-beta\n");
    CHECK(self_of(g20, ci) && !strcmp(ci.self_name, "Firmware 2.0-beta"));
    // a key with no version after it is passed over; the first with one counts
    Buf gk = g;
    put_text(gk, gend - 300000, "Software version:\0\0\0", 20);
    put_text(gk, gend - 200000, "Software version:   3.15\n");
    put_text(gk, gend - 100000, "Software version:   3.99\n");
    CHECK(self_of(gk, ci) && !strcmp(ci.self_ver, "3.15"));
    // a key and a name across the edge of two 16 KiB pieces (the scan starts 512 KiB before the end)
    Buf ge = g;
    const size_t edge = gend - 512 * 1024 + 16384;
    put_text(ge, edge - 5, "Software version:   1.1.0\n");
    CHECK(self_of(ge, ci) && !strcmp(ci.self_name, "Commodore 1.1.0"));
    put_text(ge, edge + 16384 - 7, "TRX64 Ultimate");
    CHECK(self_of(ge, ci) && !strcmp(ci.self_name, "TRX64 Ultimate 1.1.0"));
    // more than 512 KiB before the end: not read (the File Browser's bound), the title stays the header's
    Buf gfar = g;
    put_text(gfar, gend - 600000, "Software version:   3.15\n");
    CHECK(!self_of(gfar, ci) && !strcmp(cfw_title(ci), "u64_mk2_artix 2026/09/10 11:00:57"));
    // TRX64-Ultimate: its name 389 and 353 KB before the end, the line 254 KB before it (0.3.1, measured)
    Buf t = trx64u();
    CHECK(probe(t, ci) == CFW_OK);
    const size_t tend = ci.app_data + ci.app_len;
    put_text(t, tend - 388785, "monitor (VICE-superset) on TRX64 Ultimate hardware (trxmon):");
    put_text(t, tend - 253896, "Software version:   0.3.1\n");
    CHECK(self_of(t, ci) && !strcmp(ci.self_name, "TRX64 Ultimate 0.3.1") && !strcmp(ci.self_ver, "0.3.1"));
    CHECK(!strcmp(ci.name, "top 2026/10/10 18:23:21"));
    // an application that does not fit the file: nothing read, nothing named
    CHECK(probe(g315, ci) == CFW_OK);
    {
        MemSrc s;
        s.d = g315;
        ci.app_len = (uint32_t)(s.d.size() - ci.app_data + 1);
        CHECK(!cfw_read_self(s, ci) && !ci.self_name[0]);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--real")) return real(argc, argv);
    s1();
    s2();
    s_ue2();
    s_self();
    printf("test_cfw: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
