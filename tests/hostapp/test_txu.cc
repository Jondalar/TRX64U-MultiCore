// Host test of the MultiCore update (host-app/software/hostapp/txu.cc, flashfw.cc fw_write_parts; T152 S1-S4): the package reader, the checks before the
// first erase, the partial writes against a fake flash that counts every erase and program per region, the survey of the cores. Packages come from
// tests/hostapp/mktxutest.py (tools/txu_pack.py, the real writer).
//   test_txu <dir of mktxutest.py>
#include "txu.h"
#include "flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
typedef std::vector<uint8_t> Bytes;

static std::string dir;
static Bytes load(const std::string &name)
{
    Bytes b;
    FILE *f = fopen((dir + "/" + name).c_str(), "rb");
    if (!f) { printf("FAIL cannot read %s\n", name.c_str()); fails++; return b; }
    fseek(f, 0, SEEK_END);
    b.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(b.data(), 1, b.size(), f) != b.size()) fails++;
    fclose(f);
    return b;
}

struct MemSrc : CorSource {
    Bytes b;
    MemSrc(const Bytes &x) : b(x) { }
    uint32_t size() { return (uint32_t)b.size(); }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if ((uint64_t)off + len > b.size()) return false;
        memcpy(buf, &b[off], len);
        return true;
    }
};

static std::map<std::string, std::string> facts;
static void load_facts()
{
    FILE *f = fopen((dir + "/facts.txt").c_str(), "r");
    char k[64], v[128];
    while (f && fscanf(f, "%63s %127s", k, v) == 2) facts[k] = v;
    if (f) fclose(f);
}

// ---- a flash with the real semantics that counts every erase and program per region (bitstream slot, application slot, the rest) -------------------------
class FakeFlash : public Flash {
public:
    Bytes mem;
    int er_bit, er_app, er_rest, pr_bit, pr_app, pr_rest;
    FakeFlash() : mem(16 << 20, 0xFF) { clear_counts(); }
    void clear_counts() { er_bit = er_app = er_rest = pr_bit = pr_app = pr_rest = 0; }
    const char *get_type_string(void) { return "Winbond W25Q128"; }
    int get_page_size(void) { return 256; }
    int get_sector_size(int) { return 4096; }
    int page_to_sector(int page) { return page / 16; }
    bool need_erase(void) { return true; }
    bool erase_sector(int s)
    {
        uint32_t a = (uint32_t)s * 4096;
        if (a < FW_BIT_SLOT) er_bit++; else if (a < FW_APP_ADDR + FW_APP_SLOT) er_app++; else er_rest++;
        memset(&mem[a], 0xFF, 4096);
        return true;
    }
    bool read_page(int p, void *b) { memcpy(b, &mem[(size_t)p * 256], 256); return true; }
    bool write_page(int p, const void *b)
    {
        uint32_t a = (uint32_t)p * 256;
        if (a < FW_BIT_SLOT) pr_bit++; else if (a < FW_APP_ADDR + FW_APP_SLOT) pr_app++; else pr_rest++;
        const uint8_t *s = (const uint8_t *)b;
        for (int i = 0; i < 256; i++) mem[(size_t)p * 256 + i] &= s[i];
        return true;
    }
    void fill(uint32_t addr, const Bytes &v) { memcpy(&mem[addr], v.data(), v.size()); }
    Bytes slice(uint32_t addr, uint32_t n) const { return Bytes(mem.begin() + addr, mem.begin() + addr + n); }
};

static Bytes pattern(size_t n, uint8_t seed) { Bytes v(n); for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 13 + seed); return v; }

static uint8_t bit_stage[FW_BIT_SLOT + 8192], app_stage[FW_APP_SLOT + 8192];
static uint32_t KEY, KEY2;
static TxuRunning running() { TxuRunning r; r.host_key = KEY; r.iface_provided = TXU_IFACE_PROVIDED; r.iface_needed = TXU_IFACE_NEEDED; return r; }

struct Loaded {
    Bytes file;
    TxuInfo ci;
    txu_result_t r;
    TxuCheck chk;
    TxuStaged st;
    FwImage img;
    bool staged;
};
// the whole path of the host: probe, the compatibility check, the staging (the same order as hostui.cc)
static Loaded open_txu(const char *name)
{
    Loaded l;
    memset(&l.chk, 0, sizeof(l.chk));
    memset(&l.st, 0, sizeof(l.st));
    memset(&l.img, 0, sizeof(l.img));
    l.file = load(name);
    MemSrc s(l.file);
    l.r = txu_probe(s, &l.ci);
    l.staged = false;
    if (l.r != TXU_OK) return l;
    txu_check(l.ci, running(), &l.chk);
    if (!l.chk.ok) return l;
    l.staged = txu_stage(s, l.ci, bit_stage, app_stage, 0, 0, 0, &l.st, &l.img);
    return l;
}

static Bytes want_bit, want_bit2, want_app, want_app2;

// S4: a .cfw/.ue2 is not a .txu, and no vendor-shaped file passes as one
static void t_recognise()
{
    CHECK(txu_is_name("update.txu") && txu_is_name("UPDATE.TXU") && !txu_is_name("update.cfw") && !txu_is_name("update.ue2") && !txu_is_name("txu") && !txu_is_name(0));
    const char *notxu[] = { "not_txu.cfw", "not_txu.ue2" };
    for (unsigned i = 0; i < 2; i++) {
        TxuInfo ci;
        MemSrc s(load(notxu[i]));
        CHECK(txu_probe(s, &ci) == TXU_ERR_NOT_TXU);
    }
    Loaded l = open_txu("both.txu");
    CHECK(l.r == TXU_OK);
    CHECK(!strcmp(l.ci.origin, "trx") && !strcmp(l.ci.version, "main-1a2b3c4d") && !strcmp(l.ci.commit, "1a2b3c4d5e6f708"));
    CHECK(l.ci.nparts == 2 && l.ci.host() && l.ci.app());
    CHECK(l.ci.host_key == KEY);
    CHECK(l.ci.host()->addr == FW_BIT_ADDR && l.ci.app()->addr == FW_APP_ADDR);
    CHECK(l.ci.host()->size == want_bit.size() && l.ci.app()->size == want_app.size());
    l = open_txu("app_only.txu");
    CHECK(l.r == TXU_OK && l.ci.nparts == 1 && !l.ci.host() && l.ci.app() && l.ci.host_key == 0 && l.ci.iface_provided == 0);
    l = open_txu("host_only.txu");
    CHECK(l.r == TXU_OK && l.ci.nparts == 1 && l.ci.host() && !l.ci.app() && l.ci.iface_needed == 0);
    // the header's own refusals
    CHECK(open_txu("crc.txu").r == TXU_ERR_CRC);
    CHECK(open_txu("trailing.txu").r == TXU_ERR_FORMAT);
    CHECK(open_txu("foreign_origin.txu").r == TXU_ERR_ORIGIN);
    CHECK(open_txu("addr_bad.txu").r == TXU_ERR_FORMAT);
    Bytes cut = load("both.txu");
    for (size_t n : { (size_t)3, (size_t)40, (size_t)100, cut.size() - 1 }) {
        Bytes c(cut.begin(), cut.begin() + n);
        TxuInfo ci;
        MemSrc s(c);
        CHECK(txu_probe(s, &ci) != TXU_OK);
    }
}

// S3: refused before the first erase, with the reason; the same package with the interface field fixed passes
static void t_check()
{
    Loaded bad = open_txu("iface_bad.txu");
    CHECK(bad.r == TXU_OK && !bad.chk.ok && !bad.staged);
    CHECK(strstr(bad.chk.why, "needs host interface 2") && strstr(bad.chk.why, "running host provides 1"));
    Loaded good = open_txu("iface_fixed.txu");
    CHECK(good.r == TXU_OK && good.chk.ok && good.staged && good.st.ok);
    // a host that provides another interface than the app that stays needs
    Loaded h2 = open_txu("host_iface2.txu");
    CHECK(h2.r == TXU_OK && !h2.chk.ok && strstr(h2.chk.why, "provides host interface 2") && strstr(h2.chk.why, "app that stays needs 1"));
    // a package whose own two parts disagree
    Loaded mm = open_txu("both_iface_mismatch.txu");
    CHECK(mm.r == TXU_OK && !mm.chk.ok && strstr(mm.chk.why, "needs host interface 1") && strstr(mm.chk.why, "provides 2"));
    // both parts, matching: ok; the running host does not matter for the app when the package brings its own host
    CHECK(open_txu("both.txu").chk.ok);
    TxuRunning r = running();
    r.iface_provided = 7;
    Loaded b = open_txu("both.txu");
    TxuCheck c;
    txu_check(b.ci, r, &c);
    CHECK(c.ok);
    txu_check(open_txu("app_only.txu").ci, r, &c);
    CHECK(!c.ok);
    // the host key
    CHECK(!open_txu("both.txu").chk.host_key_changes);
    CHECK(open_txu("newkey.txu").chk.host_key_changes);
    CHECK(!open_txu("app_only.txu").chk.host_key_changes);
}

// the check of every byte and of the host part (nothing is written; the image is not sealed on a failure)
static void t_stage()
{
    Loaded l = open_txu("both.txu");
    if (!l.staged) printf("stage said: %s\n", l.st.why);
    CHECK(l.staged && l.st.ok && l.img.sealed == FW_SEALED);
    CHECK(l.img.bit_len == want_bit.size() && l.img.app_len == want_app.size());
    CHECK(!memcmp(bit_stage, want_bit.data(), want_bit.size()) && !memcmp(app_stage, want_app.data(), want_app.size()));
    CHECK(l.st.host_axss == KEY && l.img.axss == KEY);
    l = open_txu("payload_app.txu");
    CHECK(l.r == TXU_OK && !l.staged && l.img.sealed != FW_SEALED && strstr(l.st.why, "app part"));
    l = open_txu("payload_host.txu");
    CHECK(l.r == TXU_OK && !l.staged && l.img.sealed != FW_SEALED && strstr(l.st.why, "host part"));
    l = open_txu("bitlen.txu");
    CHECK(l.r == TXU_OK && !l.staged && strstr(l.st.why, "header length"));
    l = open_txu("idcode100t.txu");
    CHECK(l.r == TXU_OK && !l.staged && strstr(l.st.why, "for this FPGA"));
    l = open_txu("keymismatch.txu");
    CHECK(l.r == TXU_OK && !l.staged && strstr(l.st.why, "host key"));
    l = open_txu("badrecord.txu");
    CHECK(l.r == TXU_OK && !l.staged && l.img.sealed != FW_SEALED);
    // the abort is polled
    {
        Loaded a;
        a.file = load("both.txu");
        MemSrc s(a.file);
        txu_probe(s, &a.ci);
        TxuStaged st;
        FwImage img;
        bool (*ab)(void *) = [](void *) { return true; };
        CHECK(!txu_stage(s, a.ci, bit_stage, app_stage, 0, 0, ab, &st, &img) && !strcmp(st.why, "cancelled") && img.sealed != FW_SEALED);
    }
}

// S1: an app-only update writes the app region and nothing else; the bitstream reads back equal to before
static void t_app_only()
{
    FakeFlash fl;
    Bytes old_bit = pattern(want_bit.size(), 5), old_app = pattern(want_app2.size() + 12, 9);
    fl.fill(FW_BIT_ADDR, old_bit);
    fl.fill(FW_APP_ADDR, old_app);
    Bytes before = fl.slice(0, FW_BIT_SLOT);
    Loaded l = open_txu("app_only.txu");
    CHECK(l.staged);
    CHECK(!fw_flash_equals_app(&fl, l.img));
    FwWriteResult wr;
    CHECK(fw_write_parts(&fl, l.img, FW_PART_APP, 0, 0, &wr));
    CHECK(wr.ok && wr.started);
    CHECK(fl.er_bit == 0 && fl.pr_bit == 0 && fl.er_rest == 0 && fl.pr_rest == 0);          // nothing outside the app region
    CHECK(fl.er_app > 0 && fl.pr_app > 0);
    CHECK(fl.slice(0, FW_BIT_SLOT) == before);                                                 // the bitstream slot is byte for byte what it was
    CHECK(fl.slice(FW_APP_ADDR, want_app.size()) == want_app);
    CHECK(fw_flash_equals_app(&fl, l.img));
    // writing it again: equal already, the caller skips it (the layer reports equal)
    CHECK(fw_flash_equals_app(&fl, l.img));
    // the write of a part the image does not hold is refused before any erase
    fl.clear_counts();
    CHECK(!fw_write_parts(&fl, l.img, FW_PART_BIT, 0, 0, &wr) && !wr.started && fl.er_bit == 0 && fl.er_app == 0 && fl.er_rest == 0);
    CHECK(!fw_write_parts(&fl, l.img, FW_PART_BIT | FW_PART_APP, 0, 0, &wr) && !wr.started && fl.er_app == 0);
    CHECK(!fw_write(&fl, l.img, 0, 0, &wr) && !wr.started && fl.er_app == 0);                // fw_write is for both regions
    CHECK(!fw_write_parts(&fl, l.img, 0, 0, 0, &wr) && !wr.started);
}

// a host-only update writes the bitstream slot and leaves the application alone
static void t_host_only()
{
    FakeFlash fl;
    Bytes old_bit = pattern(want_bit.size() + 40000, 5), old_app = pattern(want_app.size() + 12, 9);
    fl.fill(FW_BIT_ADDR, old_bit);
    fl.fill(FW_APP_ADDR, old_app);
    Bytes app_before = fl.slice(FW_APP_ADDR, FW_APP_SLOT);
    Loaded l = open_txu("host_only.txu");
    CHECK(l.staged);
    CHECK(!fw_flash_equals_bit(&fl, l.img));
    FwWriteResult wr;
    CHECK(fw_write_parts(&fl, l.img, FW_PART_BIT, 0, 0, &wr));
    CHECK(fl.er_app == 0 && fl.pr_app == 0 && fl.er_rest == 0 && fl.pr_rest == 0);
    CHECK(fl.er_bit > 0 && fl.pr_bit > 0);
    CHECK(fl.slice(FW_APP_ADDR, FW_APP_SLOT) == app_before);
    CHECK(fl.slice(0, want_bit.size()) == want_bit);
    for (uint32_t a = want_bit.size(); a < FW_BIT_SLOT; a += 4096) { CHECK(fl.mem[a] == 0xFF); }      // the rest of the slot is blank again
    CHECK(fw_flash_equals_bit(&fl, l.img));
}

// both parts, and the skip of a part the flash holds already
static void t_both_and_skip()
{
    FakeFlash fl;
    fl.fill(FW_BIT_ADDR, pattern(want_bit.size(), 5));
    fl.fill(FW_APP_ADDR, pattern(want_app.size() + 12, 9));
    Loaded l = open_txu("both.txu");
    CHECK(l.staged);
    FwWriteResult wr;
    CHECK(fw_write_parts(&fl, l.img, FW_PART_BIT | FW_PART_APP, 0, 0, &wr));
    CHECK(fl.slice(0, want_bit.size()) == want_bit && fl.slice(FW_APP_ADDR, want_app.size()) == want_app);
    CHECK(fl.er_rest == 0 && fl.pr_rest == 0);
    CHECK(fw_flash_equals(&fl, l.img));
    // the flash holds the bitstream already, the app differs: only the app is written (the decision hostui.cc takes from fw_flash_equals_bit/app)
    FakeFlash f2;
    f2.fill(FW_BIT_ADDR, want_bit);
    f2.fill(FW_APP_ADDR, pattern(want_app.size(), 3));
    CHECK(fw_flash_equals_bit(&f2, l.img) && !fw_flash_equals_app(&f2, l.img) && !fw_flash_equals(&f2, l.img));
    uint32_t parts = (fw_flash_equals_bit(&f2, l.img) ? 0 : FW_PART_BIT) | (fw_flash_equals_app(&f2, l.img) ? 0 : FW_PART_APP);
    CHECK(parts == FW_PART_APP);
    CHECK(fw_write_parts(&f2, l.img, parts, 0, 0, &wr));
    CHECK(f2.er_bit == 0 && f2.pr_bit == 0 && f2.er_app > 0);
    // and when both are equal nothing is left to write
    CHECK(fw_flash_equals(&f2, l.img));
}

// the write refuses to start with an unsealed image
static void t_unsealed()
{
    FakeFlash fl;
    FwImage img;
    memset(&img, 0, sizeof(img));
    img.bit = bit_stage; img.bit_len = 100; img.app = app_stage; img.app_len = 100;
    FwWriteResult wr;
    CHECK(!fw_write_parts(&fl, img, FW_PART_BIT | FW_PART_APP, 0, 0, &wr) && !wr.started);
    CHECK(fl.er_bit == 0 && fl.er_app == 0);
    const char *why = "";
    FwImage s;
    CHECK(!fw_image_seal_parts(&s, true, 0, 0, 0, 0, 0, &why) && s.sealed != FW_SEALED);
    CHECK(!fw_image_seal_parts(&s, false, bit_stage, 100, 0, 0, 0, &why) && s.sealed != FW_SEALED);
}

// S2: the survey of the cores on the media
static void t_survey()
{
    CorInfo ci;
    memset(&ci, 0, sizeof(ci));
    TxuSurvey s;
    memset(&s, 0, sizeof(s));
    // 3 partial cores for the running key, 2 large cores; a host with another key
    ci.host_key = KEY;
    for (int i = 0; i < 3; i++) txu_survey_add(&s, COR_OK, ci, KEY, KEY2);
    for (int i = 0; i < 2; i++) txu_survey_add(&s, COR_ERR_LARGE, ci, KEY, KEY2);
    CHECK(s.partial == 3 && s.broken == 3 && s.large == 2 && s.already_off == 0 && s.revived == 0);
    // the same cores, a host with the same key: nothing breaks
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 3; i++) txu_survey_add(&s, COR_OK, ci, KEY, KEY);
    for (int i = 0; i < 2; i++) txu_survey_add(&s, COR_ERR_LARGE, ci, KEY, KEY);
    CHECK(s.partial == 3 && s.broken == 0 && s.large == 2);
    // a core routed for the new key comes back; one routed for a third key stays off; a damaged file is nobody's
    memset(&s, 0, sizeof(s));
    ci.host_key = KEY2;
    txu_survey_add(&s, COR_ERR_HOST, ci, KEY, KEY2);
    ci.host_key = 0x11111111;
    txu_survey_add(&s, COR_ERR_HOST, ci, KEY, KEY2);
    txu_survey_add(&s, COR_ERR_FORMAT, ci, KEY, KEY2);
    txu_survey_add(&s, COR_ERR_HASH, ci, KEY, KEY2);
    CHECK(s.partial == 2 && s.revived == 1 && s.already_off == 1 && s.broken == 0 && s.large == 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_txu <dir>\n"); return 2; }
    dir = argv[1];
    load_facts();
    KEY = (uint32_t)strtoul(facts["key"].c_str(), 0, 16);
    KEY2 = (uint32_t)strtoul(facts["key2"].c_str(), 0, 16);
    want_bit = load("bit.bin");
    want_bit2 = load("bit2.bin");
    want_app = load("app.bin");
    want_app2 = load("app2.bin");
    t_recognise();
    t_check();
    t_stage();
    t_app_only();
    t_host_only();
    t_both_and_skip();
    t_unsealed();
    t_survey();
    printf("%s (%d failed)\n", fails ? "test_txu FAILED" : "test_txu ok", fails);
    return fails ? 1 : 0;
}
