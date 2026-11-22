// T115 D7 S10: which logo a .cfw row gets (built in by the design name, a sidecar replaces it only at exactly 2048 bytes).
// test_cfwicon <c64u icon.rgb565> <trx64 icon.rgb565> <firmware icon.rgb565>: the logo files the built-in arrays come from.
#include "cfwicon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

class MemSource : public CorSource {
    std::vector<uint8_t> d;
public:
    explicit MemSource(size_t n, uint8_t seed) : d(n) { for (size_t i = 0; i < n; i++) d[i] = (uint8_t)(seed + i * 7); }
    uint32_t size() { return (uint32_t)d.size(); }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if (off > d.size() || len > d.size() - off) return false;
        memcpy(buf, d.data() + off, len);
        return true;
    }
    const uint8_t *data() const { return d.data(); }
};

static std::vector<uint8_t> slurp(const char *path)
{
    std::vector<uint8_t> v;
    FILE *f = fopen(path, "rb");
    if (!f) return v;
    uint8_t b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
    fclose(f);
    return v;
}

int main(int argc, char **argv)
{
    if (argc != 4) { printf("usage: test_cfwicon <gideon.rgb565> <trx64.rgb565> <generic.rgb565>\n"); return 2; }
    // the names are the ones cfw_probe made from the real files (T115 §7)
    const char *gideon = "u64_mk2_artix 2026/09/10 11:00:57";
    const char *trx = "top 2026/10/10 18:23:21";
    CHECK(cfw_icon_kind(gideon) == CFW_ICON_KIND_GIDEON);
    CHECK(cfw_icon_kind("u64_mk2_artix 2026/09/10 11:05:11") == CFW_ICON_KIND_GIDEON);   // the date and time are not part of the rule
    CHECK(cfw_icon_kind(trx) == CFW_ICON_KIND_TRX64);
    CHECK(cfw_icon_kind("top 2026/10/17 09:04:28") == CFW_ICON_KIND_TRX64);
    CHECK(cfw_icon_kind("some_design 2026/01/01 00:00:00") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("topaz 2026/01/01 00:00:00") == CFW_ICON_KIND_GENERIC);          // "top" is the whole word
    CHECK(cfw_icon_kind("tops") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("u64 2026/01/01 00:00:00") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("") == CFW_ICON_KIND_GENERIC);
    // T254: an entry added with A carries the name the firmware gives itself; the vendor word picks the icon the header's name picked
    CHECK(cfw_icon_kind("Gideon 3.15") == CFW_ICON_KIND_GIDEON);
    CHECK(cfw_icon_kind("Commodore 1.2.1") == CFW_ICON_KIND_GIDEON);
    CHECK(cfw_icon_kind("TRX64 Ultimate 0.3.1") == CFW_ICON_KIND_TRX64);
    CHECK(cfw_icon_kind("Firmware 2.0") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("Gideons 3.15") == CFW_ICON_KIND_GENERIC && cfw_icon_kind("TRX64U 1.0") == CFW_ICON_KIND_GENERIC);   // whole words
    // only the shape cfw_read_self gives: a large core's own name keeps the generic icon when its .cor has none
    CHECK(cfw_icon_kind("Commodore 128") == CFW_ICON_KIND_GENERIC && cfw_icon_kind("Commodore PET") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("Commodore 264 (C16, Plus/4)") == CFW_ICON_KIND_GENERIC && cfw_icon_kind("TRX64 Ultimate (Gideon app)") == CFW_ICON_KIND_GENERIC);
    CHECK(cfw_icon_kind("Gideon") == CFW_ICON_KIND_GENERIC && cfw_icon_kind("Commodore 1") == CFW_ICON_KIND_GENERIC);

    // the built-in arrays are the bytes of the logo files, and three different pictures
    const char *srcs[3] = { argv[1], argv[2], argv[3] };
    for (int k = 0; k < 3; k++) {
        std::vector<uint8_t> f = slurp(srcs[k]);
        CHECK(f.size() == CFW_ICON_BYTES && !memcmp(f.data(), cfw_icon_builtin(k), CFW_ICON_BYTES));
    }
    CHECK(memcmp(cfw_icon_builtin(0), cfw_icon_builtin(1), CFW_ICON_BYTES) && memcmp(cfw_icon_builtin(1), cfw_icon_builtin(2), CFW_ICON_BYTES) &&
          memcmp(cfw_icon_builtin(0), cfw_icon_builtin(2), CFW_ICON_BYTES));

    // no sidecar: the built-in one by name
    uint8_t out[CFW_ICON_BYTES];
    CHECK(!cfw_icon_load(gideon, 0, out) && !memcmp(out, cfw_icon_builtin(CFW_ICON_KIND_GIDEON), CFW_ICON_BYTES));
    CHECK(!cfw_icon_load(trx, 0, out) && !memcmp(out, cfw_icon_builtin(CFW_ICON_KIND_TRX64), CFW_ICON_BYTES));
    CHECK(!cfw_icon_load("other 2026/01/01 00:00:00", 0, out) && !memcmp(out, cfw_icon_builtin(CFW_ICON_KIND_GENERIC), CFW_ICON_BYTES));

    // a sidecar of exactly 2048 bytes replaces it, for every kind
    MemSource ok(CFW_ICON_BYTES, 3);
    CHECK(cfw_icon_load(gideon, &ok, out) && !memcmp(out, ok.data(), CFW_ICON_BYTES));
    CHECK(cfw_icon_load(trx, &ok, out) && !memcmp(out, ok.data(), CFW_ICON_BYTES));
    CHECK(cfw_icon_load("other 2026/01/01 00:00:00", &ok, out) && !memcmp(out, ok.data(), CFW_ICON_BYTES));

    // negative control: a sidecar of another size is ignored (shorter, longer, empty): the built-in one stays
    const size_t bad[] = { CFW_ICON_BYTES - 1, CFW_ICON_BYTES + 1, 2 * CFW_ICON_BYTES, 0, 1 };
    for (size_t n : bad) {
        MemSource s(n, 9);
        memset(out, 0xEE, sizeof(out));
        CHECK(!cfw_icon_load(trx, &s, out) && !memcmp(out, cfw_icon_builtin(CFW_ICON_KIND_TRX64), CFW_ICON_BYTES));
        CHECK(!cfw_icon_load(gideon, &s, out) && !memcmp(out, cfw_icon_builtin(CFW_ICON_KIND_GIDEON), CFW_ICON_BYTES));
    }

    // the sidecar path: the extension .cfw (any case) becomes .icon
    char p[96];
    CHECK(cfw_icon_sidecar_path("/Usb0/cores/gideon-3.15a.cfw", p, sizeof(p)) && !strcmp(p, "/Usb0/cores/gideon-3.15a.icon"));
    CHECK(cfw_icon_sidecar_path("/SD/x.CFW", p, sizeof(p)) && !strcmp(p, "/SD/x.icon"));
    CHECK(cfw_icon_sidecar_path("/SD/x.Cfw", p, sizeof(p)) && !strcmp(p, "/SD/x.icon"));
    CHECK(!cfw_icon_sidecar_path("/SD/x.cor", p, sizeof(p)));
    CHECK(!cfw_icon_sidecar_path("/SD/x.cfw.bak", p, sizeof(p)));
    CHECK(!cfw_icon_sidecar_path(".cfw", p, sizeof(p)));
    CHECK(!cfw_icon_sidecar_path("/SD/x.cfw", p, 10));                     // ".icon" is one byte longer than ".cfw": 10 bytes + NUL need 11
    CHECK(cfw_icon_sidecar_path("/SD/x.cfw", p, 11) && !strcmp(p, "/SD/x.icon"));
    CHECK(cfw_icon_sidecar_path("/SD/x.ue2", p, 11) && !strcmp(p, "/SD/x.icon"));         // T115 D9

    printf("test_cfwicon: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
