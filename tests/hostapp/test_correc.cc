// Host test of records 6 and 7 of the .cor (T104.1 D9, D12) in the boot app's reader (host-app/software/hostapp/cor.cc, startcheck.cc): the bundles are made
// by tools/cor_pack.py. A bundle with the records is probed with their offsets and hashes, a bundle without them reads as it always did, the records are
// hashed on a full check (the host verifies a bundle before it starts it), and every way the manifest and the records can disagree is a format error.
#include "cor.h"
#include "startcheck.h"
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

static bool load(MemSrc &s, const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { printf("cannot open %s\n", path.c_str()); fails++; return false; }
    uint8_t b[4096]; size_t n;
    s.d.clear();
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.d.insert(s.d.end(), b, b + n);
    fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);                                 // a mutant may crash: what it said before has to be out
    // argv[1]: the directory with rec.cor (a layer-2 bundle with both records), recl.cor (a large core with both), old.cor (neither),
    // nomanifest.cor (records the manifest does not announce), v2.cor ("records": 2), nodesc.cor (the manifest announces a description, there is none),
    // libsonly.cor, desc.bin and libs.bin (the two records as the packer wrote them)
    std::string dir = argv[1];
    cor_host_key = 0x820eded2;
    MemSrc s, o;
    CorInfo ci;
    CHECK(load(s, dir + "/rec.cor") && load(o, dir + "/old.cor"));

    CHECK(cor_probe(o, ci) == COR_OK);
    CHECK(ci.size[COR_PART_DESC] == 0 && ci.size[COR_PART_LIBS] == 0);          // a bundle of before: nothing there

    CHECK(cor_probe(s, ci) == COR_OK);
    MemSrc desc, libs;
    load(desc, dir + "/desc.bin"); load(libs, dir + "/libs.bin");
    CHECK(ci.size[COR_PART_DESC] == desc.d.size() && ci.size[COR_PART_LIBS] == libs.d.size());
    CHECK(!memcmp(s.d.data() + ci.off[COR_PART_DESC], desc.d.data(), desc.d.size()));
    CHECK(!memcmp(s.d.data() + ci.off[COR_PART_LIBS], libs.d.data(), libs.d.size()));
    CHECK(strlen(ci.sha[COR_PART_DESC]) == 64 && strlen(ci.sha[COR_PART_LIBS]) == 64);
    CHECK(ci.size[COR_PART_PARTIAL] > 0 && ci.size[COR_PART_APP] > 0);            // the others are where they were
    // the records come out whole and hashed
    std::vector<uint8_t> buf(ci.size[COR_PART_LIBS]);
    CHECK(cor_read_part(s, ci, COR_PART_LIBS, buf.data(), 0, 0) == COR_OK);
    CHECK(buf == libs.d);
    CHECK(cor_verify_part(s, ci, COR_PART_DESC) == COR_OK);
    CHECK(cor_verify_part(s, ci, COR_PART_ICON) == COR_ERR_FORMAT || ci.size[COR_PART_ICON] > 0);
    // a damaged record: probe still reads the layout, the hash says no
    MemSrc bad = s;
    bad.d[ci.off[COR_PART_DESC] + 9] ^= 1;
    CHECK(cor_probe(bad, ci) == COR_OK);
    CHECK(cor_verify_part(bad, ci, COR_PART_DESC) == COR_ERR_HASH);
    CHECK(cor_read_part(bad, ci, COR_PART_DESC, buf.data(), 0, 0) == COR_ERR_HASH || ci.size[COR_PART_DESC] > buf.size());
    // the full check of the start (the host verifies every part before it starts a bundle) sees a damaged library record, which is larger than
    // the partial and the application together
    cor_probe(s, ci);
    CHECK(cor_full_check(s, ci) == COR_OK);
    MemSrc badl = s;
    cor_probe(badl, ci);
    badl.d[ci.off[COR_PART_LIBS] + ci.size[COR_PART_LIBS] - 40] ^= 1;
    CHECK(cor_probe(badl, ci) == COR_OK);
    CHECK(cor_full_check(badl, ci) == COR_ERR_HASH);

    // a large core carries them too, and stays a large core
    MemSrc l;
    CHECK(load(l, dir + "/recl.cor"));
    CHECK(cor_probe(l, ci) == COR_ERR_LARGE);
    CHECK(ci.large && ci.size[COR_PART_FULL] > 0 && ci.size[COR_PART_DESC] == desc.d.size() && ci.size[COR_PART_LIBS] > 0);
    const char *why = 0;
    CHECK(cor_large_verify(l, ci, &why));
    MemSrc badlarge = l;
    badlarge.d[ci.off[COR_PART_DESC] + 3] ^= 1;
    CHECK(cor_large_verify(badlarge, ci, &why) == false && why && strstr(why, "description"));

    // disagreement between the manifest and the records
    MemSrc x;
    CHECK(load(x, dir + "/nomanifest.cor"));
    CHECK(cor_probe(x, ci) == COR_ERR_FORMAT);
    CHECK(load(x, dir + "/v2.cor"));
    CHECK(cor_probe(x, ci) == COR_ERR_FORMAT);
    CHECK(load(x, dir + "/nodesc.cor"));
    CHECK(cor_probe(x, ci) == COR_ERR_FORMAT);
    CHECK(load(x, dir + "/libsonly.cor"));
    CHECK(cor_probe(x, ci) == COR_OK && ci.size[COR_PART_DESC] == 0 && ci.size[COR_PART_LIBS] > 0);
    // a record type nobody knows
    MemSrc t = s;
    uint32_t total = t.d.size() - 12;
    uint8_t extra[8] = { 8, 0, 0, 0, 0, 0, 0, 0 };
    t.d.insert(t.d.end(), extra, extra + 8);
    total += 8;
    for (int i = 0; i < 4; i++) t.d[8 + i] = (uint8_t)(total >> (8 * i));
    CHECK(cor_probe(t, ci) == COR_ERR_FORMAT);
    printf(fails ? "FAILED %d\n" : "test_correc: all ok\n", fails);
    return fails != 0;
}
