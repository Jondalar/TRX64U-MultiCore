// Host test of the boot app's .cor reader (host-app/software/hostapp/cor.cc) against bundles
// made by tools/cor_pack.py: tests/hostapp/run.sh builds the bundles, compiles this natively and runs it.
#include "cor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

struct MemSrc : CorSource {
    std::vector<uint8_t> d;
    uint32_t size() { return d.size(); }
    bool read(uint32_t off, void *b, uint32_t n) {
        if ((uint64_t)off + n > d.size()) return false;
        memcpy(b, d.data() + off, n);
        return true;
    }
};

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

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

int main(int argc, char **argv)
{
    // argv[1] good.cor, argv[2] contract2.cor, argv[3] app.bin the bundle was made with,
    // argv[4] a bundle routed against another host (T84 D5b)
    MemSrc s;
    CorInfo ci;
    if (!load(s, argv[1])) return 1;
    cor_host_key = 0x820eded2;                 // what the running bitstream would say
    CHECK(cor_probe(s, ci) == COR_OK);
    CHECK(ci.host_key == 0x820eded2);
    CHECK(!strcmp(ci.name, "VIC-20 \"test\"") );
    CHECK(!strcmp(ci.version, "0.1"));
    CHECK(ci.app_load == 0x30000 && ci.app_entry == 0x30010 && ci.size[COR_PART_ICON] == 2048);
    std::vector<uint8_t> buf(ci.size[COR_PART_PARTIAL]);
    CHECK(cor_read_part(s, ci, COR_PART_PARTIAL, buf.data(), 0, 0) == COR_OK);
    std::vector<uint8_t> app(ci.size[COR_PART_APP]);
    CHECK(cor_read_part(s, ci, COR_PART_APP, app.data(), 0, 0) == COR_OK);
    MemSrc a;
    load(a, argv[3]);
    CHECK(a.d == app);
    // damage the app part: hash refused, probe still fine
    MemSrc bad = s;
    bad.d[ci.off[COR_PART_APP] + 5] ^= 1;
    CHECK(cor_probe(bad, ci) == COR_OK);
    CHECK(cor_read_part(bad, ci, COR_PART_APP, app.data(), 0, 0) == COR_ERR_HASH);
    // truncated, wrong magic
    MemSrc t = s; t.d.resize(t.d.size() - 3);
    CHECK(cor_probe(t, ci) == COR_ERR_FORMAT);
    MemSrc m = s; m.d[0] = 'X';
    CHECK(cor_probe(m, ci) == COR_ERR_FORMAT);
    // wrong contract
    MemSrc c2;
    if (load(c2, argv[2])) CHECK(cor_probe(c2, ci) == COR_ERR_CONTRACT);
    CHECK(ci.contract == 3);
    // T84 D5b: another host's bundle is refused, and ci still says which host it wants
    MemSrc h2;
    if (argc > 4 && load(h2, argv[4])) {
        CHECK(cor_probe(h2, ci) == COR_ERR_HOST);
        CHECK(ci.host_key == 0x9ae17385);
        // a host that names no key refuses nothing
        cor_host_key = 0;
        CHECK(cor_probe(h2, ci) == COR_OK);
        cor_host_key = 0x820eded2;
    }
    // a manifest without the field is not a bundle any more
    {
        MemSrc nk = s;
        const char *k = "\"host_key\"";
        std::string all((const char *)nk.d.data(), nk.d.size());
        size_t at = all.find(k);
        CHECK(at != std::string::npos);
        nk.d[at + 2] = 'X';                    // "hXst_key": the key is gone
        CHECK(cor_probe(nk, ci) == COR_ERR_FORMAT);
    }
    printf(fails ? "FAILED %d\n" : "test_cor: all ok\n", fails);
    return fails != 0;
}
