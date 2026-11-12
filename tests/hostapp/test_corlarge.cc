// Host test of the LARGE .cor (T115 section 7): host-app/software/hostapp/cor.cc + cfw.cc over packages made by tools/cor_pack.py
// pack-large (tests/hostapp/run.sh builds them). The staging calls of legacy.cc take bit_off/bit_len/app_data/app_len/map from the
// CfwInfo this test reads, so what is checked here is that they point at the right bytes of the file.
//   test_corlarge <large.cor> <bit.bin> <app.bin> <icon.bin> <large_gbl4.cor> <large100t.cor> <id_mismatch.cor> <map_mismatch.cor> <dfx.cor> <manifest50t_bit100t.cor>
#include "cfw.h"
#include "handover.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

typedef std::vector<uint8_t> Buf;

struct MemSrc : CorSource {
    Buf d;
    uint32_t size() { return d.size(); }
    bool read(uint32_t off, void *b, uint32_t n) {
        if ((uint64_t)off + n > d.size()) return false;
        memcpy(b, d.data() + off, n);
        return true;
    }
};

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static bool load(MemSrc &s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); fails++; return false; }
    uint8_t b[4096]; size_t n;
    s.d.clear();
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.d.insert(s.d.end(), b, b + n);
    fclose(f);
    return true;
}

#define ID_50T 0x0362C093u

int main(int argc, char **argv)
{
    if (argc < 11) { printf("usage: see the header\n"); return 2; }
    MemSrc cor, bit, app, icon;
    if (!load(cor, argv[1]) || !load(bit, argv[2]) || !load(app, argv[3]) || !load(icon, argv[4])) return 1;

    // 1. the good package: read as a full firmware, and the offsets point at the bitstream and the application as they were packed
    CfwInfo ci;
    CHECK(cfw_probe(cor, ID_50T, ci) == CFW_OK);
    CHECK(ci.from_cor && ci.idcode == ID_50T && ci.map == CFW_MAP_IDENTITY);
    CHECK(!strcmp(ci.name, "Amiga 1200 \"test\"") && !strcmp(ci.part, "xc7a50t"));
    CHECK(!strcmp(ci.id, "amiga1200"));                          // T115 7 I7: the manifest's id reaches the hand-over page
    {
        alignas(128) static uint8_t page[CFW_HANDOVER_BYTES];
        cfw_handover_page(ci, "/Usb0/cores/amiga1200.cor", page);
        Buf view(CFW_HANDOVER_BYTES);
        for (uint32_t o = 0; o < CFW_HANDOVER_BYTES; o++) view[o] = page[cfw_map_off(ci.map, o)];
        const Handover *hv = (const Handover *)view.data();
        CHECK(handover_valid(hv) && !strcmp(handover_core_id(hv), "amiga1200"));
        int y, mo, d, h, mi, s;
        CHECK(!handover_time(hv, &y, &mo, &d, &h, &mi, &s));      // T191 S9: a host without a clock passes no time
        const int now[6] = { 2026, 11, 8, 18, 30, 5 };
        cfw_handover_page(ci, "/Usb0/cores/amiga1200.cor", page, now);  // the large .cor start (legacy.cc) passes the host's time
        for (uint32_t o = 0; o < CFW_HANDOVER_BYTES; o++) view[o] = page[cfw_map_off(ci.map, o)];
        CHECK(handover_valid(hv) && handover_time(hv, &y, &mo, &d, &h, &mi, &s));
        CHECK(y == 2026 && mo == 11 && d == 8 && h == 18 && mi == 30 && s == 5);
    }
    CHECK(ci.bit_len == bit.d.size() && ci.bit_off + ci.bit_len <= cor.d.size());
    CHECK(Buf(cor.d.begin() + ci.bit_off, cor.d.begin() + ci.bit_off + ci.bit_len) == bit.d);
    CHECK(ci.app_len == app.d.size() && ci.app_data == ci.app_off);
    CHECK(Buf(cor.d.begin() + ci.app_data, cor.d.begin() + ci.app_data + ci.app_len) == app.d);
    CHECK(ci.app_load == CFW_APP_ADDR && ci.app_entry == CFW_APP_ADDR);
    // what legacy_stage stages: the application under the map, 128-byte padded; the identity map leaves every byte where it is
    {
        uint32_t n = (ci.app_len + 127) & ~127u;
        Buf in(n, 0), out(n, 0xEE);
        memcpy(in.data(), app.d.data(), ci.app_len);
        cfw_stage_copy(ci.map, out.data(), 0, in.data(), n);
        CHECK(out == in);
    }
    // the same file through cor_probe: LARGE, never OK (a layer-2 start must refuse it), manifest and record layout filled
    CorInfo c;
    CHECK(cor_probe(cor, c) == COR_ERR_LARGE);
    CHECK(c.large && c.size[COR_PART_PARTIAL] == 0 && c.size[COR_PART_FULL] == bit.d.size() && c.size[COR_PART_ICON] == 2048);
    CHECK(c.contract == 0 && c.host_key == 0 && !strcmp(c.id, "amiga1200") && c.idcode == ID_50T);
    CHECK(ci.bit_off == c.off[COR_PART_FULL] && !strcmp(ci.bit_sha, c.sha[COR_PART_FULL]));
    // the icon record is the one that was packed
    {
        Buf ic(2048);
        CHECK(cor_read_part(cor, c, COR_PART_ICON, ic.data(), 0, 0) == COR_OK && ic == icon.d);
    }
    // the hashes: the check that runs when the entry is added
    const char *why = 0;
    CHECK(cor_large_verify(cor, c, &why));
    for (int part = 0; part < 3; part++) {                    // one byte flipped in the app, the icon, the bitstream
        static const int p[3] = { COR_PART_APP, COR_PART_ICON, COR_PART_FULL };
        MemSrc bad = cor;
        bad.d[c.off[p[part]] + 7] ^= 1;
        CorInfo cb;
        CHECK(cor_probe(bad, cb) == COR_ERR_LARGE);          // a probe does not hash
        why = 0;
        CHECK(!cor_large_verify(bad, cb, &why) && why);
    }

    // 2. a different FPGA: refused by the running IDCODE, whichever of the manifest or the bitstream says so
    CHECK(cfw_probe(cor, 0x03631093u, ci) == CFW_ERR_NO_50T);            // this host would be a 100T
    CHECK(cfw_probe(cor, 0x1362C093u, ci) == CFW_OK);                    // the version nibble does not count
    MemSrc b100, idm, mapm;
    CHECK(load(b100, argv[6]) && load(idm, argv[7]) && load(mapm, argv[8]));
    CHECK(cfw_probe(b100, ID_50T, ci) == CFW_ERR_NO_50T);                // manifest and bitstream both 100T
    CHECK(cfw_probe(idm, ID_50T, ci) == CFW_ERR_NO_50T);                 // manifest says 100T, bitstream 50T
    MemSrc bb;
    CHECK(load(bb, argv[10]));
    CHECK(cfw_probe(bb, ID_50T, ci) == CFW_ERR_NO_50T);                  // manifest says 50T, the bitstream is the 100T's
    CHECK(cfw_probe(mapm, ID_50T, ci) == CFW_ERR_FORMAT);                // the manifest's map contradicts the AXSS write
    // (the packer refuses the first of these, test_cor_pack.py; these files were made around it)

    // 3. a bitstream without the AXSS write: the map is gideon_bl4
    MemSrc g;
    CHECK(load(g, argv[5]));
    CHECK(cfw_probe(g, ID_50T, ci) == CFW_OK && ci.map == CFW_MAP_GIDEON_BL4);

    // 4. damaged containers: every cut and a wrong magic are refused, never read past the end
    for (size_t cut = 1; cut < 60; cut += 7) {
        MemSrc t = cor;
        t.d.resize(t.d.size() - cut);
        CHECK(cfw_probe(t, ID_50T, ci) != CFW_OK);
    }
    {
        MemSrc t = cor;
        t.d.resize(100);
        CHECK(cfw_probe(t, ID_50T, ci) != CFW_OK);
        t = cor;
        t.d[3] ^= 0x20;
        CHECK(cfw_probe(t, ID_50T, ci) != CFW_OK);
        t = cor;
        uint32_t m = c.off[COR_PART_FULL] - 8;                   // the bitstream record's type: 5 -> 6
        t.d[m] = 6;
        CHECK(cor_probe(t, c) == COR_ERR_FORMAT);
    }

    // 5. a layer-2 .cor is not a full firmware, and a large one is not a layer-2 core
    MemSrc dfx;
    CHECK(load(dfx, argv[9]));
    CHECK(cfw_probe(dfx, ID_50T, ci) == CFW_ERR_FORMAT);
    CHECK(cor_probe(dfx, c) == COR_OK && !c.large);              // unchanged for the layer-2 reader
    // a layer-2 bundle with a record 5 stuck on: refused, whatever its manifest says
    {
        MemSrc x = dfx;
        uint8_t rec5[8 + 4] = { 5, 0, 0, 0, 4, 0, 0, 0, 1, 2, 3, 4 };
        x.d.insert(x.d.end(), rec5, rec5 + sizeof(rec5));
        uint32_t len = x.d.size() - 12;
        memcpy(&x.d[8], &len, 4);
        CHECK(cor_probe(x, c) == COR_ERR_FORMAT);
    }

    printf("test_corlarge: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
