// Host test of the restore (host-app/software/hostapp/fwrestore.cc, flashfw.cc, fwconfirm.cc; T137 S2, S3): which file is offered, the check before the
// first erase, and the write against a fake flash with the real semantics (a program only clears bits, only an erase sets a sector to 0xFF, every
// operation is logged). Packages come from tests/hostapp/mkrestoretest.py (the real writers).
//   test_fwrestore <dir of mkrestoretest.py>
#include "fwrestore.h"
#include "fwconfirm.h"
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

// ---- a flash with the real semantics -------------------------------------------------------------------------------------------------
class FakeFlash : public Flash {
public:
    std::string type;
    Bytes mem;
    int erases, programs, reads;
    struct Op { char kind; int arg; };
    std::vector<Op> log;
    int drop_once_page;           // a program of this page does not take, once
    int bad_page;                 // a program of this page never takes
    int protect_sector;           // an erase of this sector changes nothing
    int page_size_v, sector_size_v;
    bool needs_erase;
    int disturb_after, disturb_addr;   // after this many programs one bit at disturb_addr changes (a program disturb): only a read of the whole region finds it
    FakeFlash() : type("Winbond W25Q128"), mem(16 << 20, 0xFF), erases(0), programs(0), reads(0), drop_once_page(-1), bad_page(-1), protect_sector(-1), page_size_v(256), sector_size_v(4096), needs_erase(true), disturb_after(-1), disturb_addr(0) { }
    const char *get_type_string(void) { return type.c_str(); }
    int get_page_size(void) { return page_size_v; }
    int get_sector_size(int) { return sector_size_v; }
    int page_to_sector(int page) { return page / 16; }
    bool need_erase(void) { return needs_erase; }
    bool erase_sector(int s)
    {
        Op o = { 'E', s };
        log.push_back(o);
        if (s < 0 || s >= (int)(mem.size() / 4096)) return false;
        erases++;
        if (s == protect_sector) return true;                 // it says yes, and nothing changes
        memset(&mem[(size_t)s * 4096], 0xFF, 4096);
        return true;
    }
    bool read_page(int p, void *b) { reads++; memcpy(b, &mem[(size_t)p * 256], 256); return true; }
    bool write_page(int p, const void *b)
    {
        Op o = { 'P', p };
        log.push_back(o);
        programs++;
        if (p == bad_page) return true;                       // accepted, never stored
        if (p == drop_once_page) { drop_once_page = -1; return true; }
        const uint8_t *s = (const uint8_t *)b;
        for (int i = 0; i < 256; i++) mem[(size_t)p * 256 + i] &= s[i];
        if (programs == disturb_after) mem[disturb_addr] ^= 1;
        return true;
    }
    void fill(uint32_t addr, const Bytes &v) { memcpy(&mem[addr], v.data(), v.size()); }
};

static Bytes pattern(size_t n, uint8_t seed) { Bytes v(n); for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + seed); return v; }

static const char *CHIP = "Winbond W25Q128";
static FwKnown official;            // the table row of vendor.cfw (the real table holds real files; this bench has no vendor file)

static uint8_t bit_stage[0x400000], app_stage[0x400000];

struct Staged {
    FwPkg p;
    FwCheck c;
    FwImage img;
    bool ok;
};
static bool is_official(const Bytes &file) { MemSrc s(file); char hex[65]; return fw_file_sha(s, hex, 0, 0, 0) && !strcmp(hex, official.sha256); }
// the whole path of the host: probe, the table, the check
static Staged stage(const Bytes &file, const char *chip = CHIP)
{
    static MemSrc *keep;
    delete keep;
    keep = new MemSrc(file);
    Staged s;
    memset(&s, 0, sizeof(s));
    fw_result_t r = fw_probe(*keep, CFW_IDCODE_XC7A50T, &s.p);
    if (r != FW_OK) { s.ok = false; s.c.why = "probe"; return s; }
    char hex[65];
    fw_file_sha(*keep, hex, 0, 0, 0);
    const FwKnown *k = !strcmp(hex, official.sha256) ? &official : fw_known_lookup(hex);
    s.ok = fw_restore_stage(*keep, s.p, k, chip, bit_stage, app_stage, 0, 0, 0, &s.c, &s.img);
    return s;
}

static bool le32_ok(const uint8_t *a) { return a[0] == 0 && a[1] == 0 && a[2] == 3 && a[3] == 0 && a[8] == 0 && a[10] == 3; }

// P2 recognise: what each file is
static void t_probe()
{
    struct { const char *f; FwKind k; } want[] = {
        { "vendor.cfw", FWK_FIRMWARE }, { "vendor.ue2", FWK_FIRMWARE }, { "mc_update.cfw", FWK_FIRMWARE }, { "tester.cfw", FWK_FIRMWARE },
        { "backup.cor", FWK_BACKUP_COR }, { "foreign.cor", FWK_FOREIGN_COR }, { "our.cor", FWK_OUR_COR } };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        MemSrc s(load(want[i].f));
        FwPkg p;
        fw_result_t r = fw_probe(s, CFW_IDCODE_XC7A50T, &p);
        if (r != FW_OK || p.kind != want[i].k) printf("  probe %s: r=%d kind=%s\n", want[i].f, r, fw_kind_name(p.kind));
        CHECK(r == FW_OK && p.kind == want[i].k);
    }
    CHECK(true);
    // a layer-2 core is not a firmware
    { MemSrc s(load("layer2.cor")); FwPkg p; CHECK(fw_probe(s, CFW_IDCODE_XC7A50T, &p) == FW_ERR_PROBE && p.kind == FWK_NONE); }
    // not for another FPGA
    { MemSrc s(load("vendor.cfw")); FwPkg p; CHECK(fw_probe(s, 0x03631093, &p) == FW_ERR_PROBE && p.kind == FWK_NONE); }
    // truncated
    { Bytes b = load("vendor.cfw"); b.resize(b.size() / 2); MemSrc s(b); FwPkg p; CHECK(fw_probe(s, CFW_IDCODE_XC7A50T, &p) != FW_OK && p.kind == FWK_NONE); }
}

// S2: only a recognised file is offered for restore
static void t_offers()
{
    struct { const char *f; bool known, unknown; } w[] = {
        { "vendor.cfw", true, false },         // a vendor image: restorable only when the whole file is in the table
        { "vendor.ue2", true, false },
        { "mc_update.cfw", true, false },      // (given a table row, anything with this shape would be: the row is what the caller looked up)
        { "tester.cfw", true, false },
        { "backup.cor", true, true },          // the backup of this board needs no table
        { "foreign.cor", false, false },       // a converted .cor can say anything about itself (Q5)
        { "our.cor", false, false } };
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
        MemSrc s(load(w[i].f));
        FwPkg p;
        fw_probe(s, CFW_IDCODE_XC7A50T, &p);
        bool b = fw_can_restore(p, &official, CHIP) == w[i].known, c = fw_can_restore(p, 0, CHIP) == w[i].unknown;
        if (!(b && c)) printf("  offers %s: known %d unknown %d\n", w[i].f, b, c);
        CHECK(b && c);
    }
    // a backup of another chip is not restored here (D2)
    { MemSrc s(load("backup_otherchip.cor")); FwPkg p; fw_probe(s, CFW_IDCODE_XC7A50T, &p); CHECK(p.kind == FWK_BACKUP_COR && !fw_can_restore(p, 0, CHIP) && !fw_can_restore(p, &official, CHIP)); }
    { MemSrc s(load("backup.cor")); FwPkg p; fw_probe(s, CFW_IDCODE_XC7A50T, &p); CHECK(!fw_can_restore(p, 0, "Winbond W25Q64") && !fw_can_restore(p, 0, 0)); }
    // the synthetic files are none of the table's rows; one digit off a real row is none either
    CHECK(fw_known_count() == 4);
    CHECK(fw_known_lookup("487a7d2104d6d32f61f33ca36a16439d947cd1b0eab75d5b1e78248de51bb58e") == 0);
    char hex[65];
    { MemSrc s(load("vendor.cfw")); CHECK(fw_file_sha(s, hex, 0, 0, 0) && fw_known_lookup(hex) == 0 && facts["vendor_sha"] == hex); }
    // the other test files the user may meet: a MultiCore update package (T152's) and a tester package are firmware files not in the table
    { MemSrc s(load("mc_update.cfw")); CHECK(fw_file_sha(s, hex, 0, 0, 0) && fw_known_lookup(hex) == 0); }
}

// S3: the check before the first erase
static void t_stage()
{
    const char *files[] = { "vendor.cfw", "vendor.ue2", "backup.cor" };
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        Bytes f = load(files[i]);
        Staged s = stage(f);
        // vendor files are in the table only through the bench's row; the .ue2 is another file: refused (not the same hash)
        bool expect = strcmp(files[i], "vendor.ue2") != 0;
        if (s.ok != expect) printf("  stage %s: %s\n", files[i], s.c.why);
        CHECK(s.ok == expect);
        if (!s.ok) { CHECK(s.img.sealed != FW_SEALED); continue; }
        CHECK(s.img.sealed == FW_SEALED);
        CHECK(!memcmp(bit_stage, &f[s.p.ci.bit_off], s.p.ci.bit_len));
        if (!s.p.ci.from_cor) CHECK(!memcmp(app_stage, &f[s.p.ci.app_off], FW_REC_HDR + s.p.ci.app_len));
        else CHECK(!memcmp(app_stage + FW_REC_HDR, &f[s.p.ci.app_data], s.p.ci.app_len) && le32_ok(app_stage));
    }
    // an unknown firmware file never gets an image, whatever it holds
    { Staged s = stage(load("vendor.ue2")); CHECK(!s.ok && s.img.sealed != FW_SEALED && !strstr(s.c.why, "cancelled")); }
    { Staged s = stage(load("mc_update.cfw")); CHECK(!s.ok && s.img.sealed != FW_SEALED); }
    // a converted .cor and a core of ours never get one
    { Staged s = stage(load("foreign.cor")); CHECK(!s.ok && s.img.sealed != FW_SEALED); }
    { Staged s = stage(load("our.cor")); CHECK(!s.ok && s.img.sealed != FW_SEALED); }
    // T135 B2: a backup that holds our own host (stamp + TRX64 in the application) is not the firmware that ran before; the reason is named
    { Staged s = stage(load("backup_hostfw.cor")); CHECK(!s.ok && s.img.sealed != FW_SEALED && strstr(s.c.why, "MultiCore's own firmware")); }
    // T135 B1: a stamp alone does not make a firmware ours: a self-built one is staged like any backup
    { Staged s = stage(load("backup_selfbuilt.cor")); CHECK(s.ok && s.img.sealed == FW_SEALED && s.c.bit_axss != 0); }
    // a backup of another chip, and a backup on a board with another chip
    { Staged s = stage(load("backup_otherchip.cor")); CHECK(!s.ok && s.img.sealed != FW_SEALED); }
    { Staged s = stage(load("backup.cor"), "Winbond W25Q64"); CHECK(!s.ok && s.img.sealed != FW_SEALED); }

    // one byte changed anywhere in a part of a backup: the SHA-256 of the manifest finds it, with a reason, and there is no image
    Bytes f = load("backup.cor");
    MemSrc pr(f);
    FwPkg p;
    fw_probe(pr, CFW_IDCODE_XC7A50T, &p);
    struct { const char *part; uint32_t off, len; } parts[2] = { { "bit", p.ci.bit_off, p.ci.bit_len }, { "app", p.ci.app_data, p.ci.app_len } };
    for (int j = 0; j < 2; j++) {
        uint32_t spots[3] = { parts[j].off, parts[j].off + parts[j].len / 2, parts[j].off + parts[j].len - 1 };
        for (int k = 0; k < 3; k++) {
            Bytes g = f;
            g[spots[k]] ^= 0x01;
            Staged s = stage(g);
            if (s.ok) printf("  backup %s byte %u: the check passed\n", parts[j].part, spots[k]);
            CHECK(!s.ok && s.c.why && s.c.why[0] && s.img.sealed != FW_SEALED);
        }
    }
    // an official file with one byte changed is not official any more: its whole-file hash is none of the table's (the bench's row stands in for a table row)
    { Bytes g = load("vendor.cfw"); MemSrc pr2(g); FwPkg p2; fw_probe(pr2, CFW_IDCODE_XC7A50T, &p2); g[p2.ci.app_off + 5000] ^= 1;
      CHECK(!is_official(g)); Staged s = stage(g); CHECK(!s.ok && s.img.sealed != FW_SEALED); }
    // sizes against the slots
    { Bytes g = load("backup.cor"); MemSrc q(g); FwPkg pp; fw_probe(q, CFW_IDCODE_XC7A50T, &pp); FwCheck c; FwImage im;
      pp.bit_flash_len = FW_BIT_SLOT + 4; CHECK(!fw_restore_stage(q, pp, 0, CHIP, bit_stage, app_stage, 0, 0, 0, &c, &im) && strstr(c.why, "larger"));
      fw_probe(q, CFW_IDCODE_XC7A50T, &pp); pp.app_flash_len = FW_APP_SLOT + 4; CHECK(!fw_restore_stage(q, pp, 0, CHIP, bit_stage, app_stage, 0, 0, 0, &c, &im) && strstr(c.why, "larger")); }
    // an application that loads elsewhere than the boot ROM takes: a vendor image whose record says so
    { Bytes g = load("vendor.cfw"); MemSrc pr3(g); FwPkg p3; fw_probe(pr3, CFW_IDCODE_XC7A50T, &p3); g[p3.ci.app_off + 8] = 0x10; g[p3.ci.app_off + 9] = 0; g[p3.ci.app_off + 10] = 0x01;
      MemSrc q(g); FwCheck c; FwImage im; char hex[65]; fw_file_sha(q, hex, 0, 0, 0); FwKnown k = { hex, "x", "x", "x" };
      CHECK(!fw_restore_stage(q, p3, &k, CHIP, bit_stage, app_stage, 0, 0, 0, &c, &im) && strstr(c.why, "0x30000") && im.sealed != FW_SEALED); }
    // a cancel between chunks leaves no image
    { Bytes g = load("backup.cor"); MemSrc q(g); FwPkg pp; fw_probe(q, CFW_IDCODE_XC7A50T, &pp); FwCheck c; FwImage im;
      CHECK(!fw_restore_stage(q, pp, 0, CHIP, bit_stage, app_stage, 0, 0, [](void *) { return true; }, &c, &im) && !strcmp(c.why, "cancelled") && im.sealed != FW_SEALED); }
}

// ---- the gate between the check and the write ------------------------------------------------------------------------------------------
static void t_gate()
{
    Staged s = stage(load("backup.cor"));
    CHECK(s.ok);
    FwWriteResult r;
    // an image that was not sealed (or was damaged after sealing) is refused before the first erase
    { FakeFlash f; FwImage im = s.img; im.sealed = 0;
      CHECK(!fw_write(&f, im, 0, 0, &r) && !r.started && f.erases == 0 && f.programs == 0 && f.log.empty()); }
    { FakeFlash f; FwImage im; memset(&im, 0, sizeof(im));
      CHECK(!fw_write(&f, im, 0, 0, &r) && !r.started && f.erases == 0 && f.programs == 0); }
    // the seal is refused for a package that did not pass its check, and for parts that do not fit or a record the boot ROM would not take
    const char *why;
    { FwImage im; CHECK(!fw_image_seal(&im, false, bit_stage, s.img.bit_len, app_stage, s.img.app_len, 0, &why) && im.sealed != FW_SEALED); }
    { FwImage im; CHECK(!fw_image_seal(&im, true, bit_stage, FW_BIT_SLOT + 4, app_stage, s.img.app_len, 0, &why) && im.sealed != FW_SEALED); }
    { FwImage im; CHECK(!fw_image_seal(&im, true, bit_stage, s.img.bit_len, app_stage, FW_APP_SLOT + 4, 0, &why) && im.sealed != FW_SEALED); }
    { std::vector<uint8_t> bad(app_stage, app_stage + s.img.app_len); bad[5]++;     // the length (+256) disagrees with the record
      FwImage im; CHECK(!fw_image_seal(&im, true, bit_stage, s.img.bit_len, bad.data(), s.img.app_len, 0, &why) && im.sealed != FW_SEALED); }
    { std::vector<uint8_t> bad(app_stage, app_stage + s.img.app_len); bad[10] ^= 0x01;     // the start address lies outside the image
      FwImage im; CHECK(!fw_image_seal(&im, true, bit_stage, s.img.bit_len, bad.data(), s.img.app_len, 0, &why) && im.sealed != FW_SEALED); }
    // a flash that is not the chip: refused before the first erase, nothing touched
    { FakeFlash f; f.type = "Winbond W25Q64"; CHECK(!fw_write(&f, s.img, 0, 0, &r) && !r.started && f.erases == 0 && f.programs == 0); }
    { FakeFlash f; f.page_size_v = 512; CHECK(!fw_write(&f, s.img, 0, 0, &r) && !r.started && f.erases == 0); }
}

// ---- the write -----------------------------------------------------------------------------------------------------------------------
static void t_write()
{
    Staged s = stage(load("vendor.cfw"));
    CHECK(s.ok);
    if (!s.ok) return;
    // an old firmware in the flash: a LONGER bitstream and application than the new ones, programmed over erased flash (bits only cleared)
    FakeFlash f;
    Bytes old_bit = pattern(s.img.bit_len + 20000, 0x21), old_app = pattern(s.img.app_len + 30000, 0x44), drive = pattern(0x20000, 0x77);
    f.fill(FW_BIT_ADDR, old_bit);
    f.fill(FW_APP_ADDR, old_app);
    f.fill(0x400000, drive);                      // the flash drive: boot.cfg and the legacy scratch file live there
    f.fill(0xFE8000, pattern(0x18000, 0x31));     // the config pages
    CHECK(!fw_flash_equals(&f, s.img));
    const char *type, *why;
    CHECK(fw_flash_check(&f, &type, &why) && !strcmp(type, "Winbond W25Q128"));

    int phases_seen[FWP_PHASES] = { 0 };
    FwWriteResult r;
    bool ok = fw_write(&f, s.img, [](void *ctx, int ph, uint32_t, uint32_t, const char *) { ((int *)ctx)[ph]++; }, phases_seen, &r);
    if (!ok) printf("  write failed: phase %d at 0x%x: %s\n", r.phase, r.sector, r.why);
    CHECK(ok && r.ok && r.started);
    CHECK(!memcmp(&f.mem[FW_APP_ADDR], s.img.app, s.img.app_len));
    CHECK(!memcmp(&f.mem[FW_BIT_ADDR], s.img.bit, s.img.bit_len));
    // the rest of the bitstream slot reads blank again (the old, longer image is gone)
    bool blank = true;
    for (uint32_t a = (s.img.bit_len + 255) & ~255u; a < FW_BIT_SLOT; a++) blank &= f.mem[a] == 0xFF;
    CHECK(blank);
    // nothing outside the two regions changed: the flash drive and the config pages are what they were
    CHECK(!memcmp(&f.mem[0x400000], drive.data(), drive.size()) && !memcmp(&f.mem[0xFE8000], pattern(0x18000, 0x31).data(), 0x18000));
    uint32_t app_sectors = (s.img.app_len + 4095) / 4096, bit_sectors = (s.img.bit_len + 4095) / 4096;
    int e_app = 0, e_bit = 0, e_tail = 0;
    for (size_t i = 0; i < f.log.size(); i++) if (f.log[i].kind == 'E') {
        uint32_t a = (uint32_t)f.log[i].arg * 4096;
        if (a >= FW_APP_ADDR && a < FW_APP_ADDR + FW_APP_SLOT) e_app++;
        else if (a < FW_BIT_SLOT && a < bit_sectors * 4096) e_bit++;
        else e_tail++;
    }
    int want_tail = (int)((s.img.bit_len + 20000 - 1) / 4096) - (int)bit_sectors + 1;
    CHECK(e_app == (int)app_sectors && e_bit == (int)bit_sectors && e_tail == want_tail && f.erases == e_app + e_bit + e_tail);
    CHECK(phases_seen[FWP_APP] > 0 && phases_seen[FWP_BIT] > 0 && phases_seen[FWP_READBACK] > 0);
    CHECK(fw_flash_equals(&f, s.img));

    // T49 5.1: the application's header (its first page) is the very last page programmed of the application; the first sector was erased first
    int first_app_erase = -1, last_app_prog = -1, hdr_prog = -1, first_bit_op = -1;
    for (size_t i = 0; i < f.log.size(); i++) {
        const FakeFlash::Op &o = f.log[i];
        uint32_t a = o.kind == 'E' ? (uint32_t)o.arg * 4096 : (uint32_t)o.arg * 256;
        bool in_app = a >= FW_APP_ADDR && a < FW_APP_ADDR + FW_APP_SLOT;
        if (o.kind == 'E' && in_app && first_app_erase < 0) first_app_erase = (int)i;
        if (o.kind == 'P' && in_app) { last_app_prog = (int)i; if (a == FW_APP_ADDR) hdr_prog = (int)i; }
        if (!in_app && first_bit_op < 0) first_bit_op = (int)i;
    }
    CHECK(f.log[first_app_erase].arg * 4096 == (int)FW_APP_ADDR);             // the header sector goes first
    CHECK(hdr_prog == last_app_prog && hdr_prog > 0);                          // and its first page is the last one programmed
    CHECK(first_bit_op > last_app_prog);                                       // the bitstream is touched only after the application is complete
    { std::vector<int> last16;
      for (int i = last_app_prog; i >= 0 && last16.size() < 16; i--) if (f.log[i].kind == 'P') last16.push_back(f.log[i].arg);
      bool asc = true;
      for (size_t i = 0; i + 1 < last16.size(); i++) asc &= last16[i] == last16[i + 1] - 1;
      CHECK(last16.size() == 16 && asc && last16.back() == (int)(FW_APP_ADDR / 256) + 15); }

    // a backup writes the same way, and a repeat over the same image writes the same bytes
    Staged b = stage(load("backup.cor"));
    FakeFlash g;
    CHECK(b.ok && fw_write(&g, b.img, 0, 0, &r) && fw_write(&g, b.img, 0, 0, &r) && fw_flash_equals(&g, b.img));
    CHECK(!memcmp(&g.mem[FW_APP_ADDR], b.img.app, b.img.app_len) && le32_ok(&g.mem[FW_APP_ADDR]));
}

static void t_write_faults()
{
    Staged s = stage(load("backup.cor"));
    FwWriteResult r;
    // a program that does not take once: the page is read back, found different and tried again
    { FakeFlash f; f.drop_once_page = (int)(FW_BIT_ADDR / 256) + 7;
      bool ok = fw_write(&f, s.img, 0, 0, &r);
      CHECK(ok && fw_flash_equals(&f, s.img));
      uint32_t pages = (s.img.bit_len + 255) / 256 + (s.img.app_len + 255) / 256;
      CHECK((uint32_t)f.programs == pages + 1); }
    // a page that never takes: three tries, then the failure names the page and says the flash was touched
    { FakeFlash f; f.bad_page = (int)(FW_BIT_ADDR / 256) + 20;
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && !r.ok && r.started && r.phase == FWP_BIT && r.sector == FW_BIT_ADDR + 20 * 256 && r.why[0]);
      int tries = 0;
      for (size_t i = 0; i < f.log.size(); i++) tries += f.log[i].kind == 'P' && f.log[i].arg == f.bad_page;
      CHECK(tries == 3);
      f.bad_page = -1;
      CHECK(fw_write(&f, s.img, 0, 0, &r) && fw_flash_equals(&f, s.img)); }
    // an application page that never takes: the bitstream is not touched (T49: the risky step comes last)
    { FakeFlash f; Bytes old_bit = pattern(s.img.bit_len, 0x55); f.fill(FW_BIT_ADDR, old_bit); f.bad_page = (int)(FW_APP_ADDR / 256) + 3;
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && r.phase == FWP_APP);
      CHECK(!memcmp(&f.mem[0], old_bit.data(), old_bit.size())); }
    // a protected sector: the erase says yes and nothing changes; found by the blank check, before any program of that sector
    { FakeFlash f; Bytes old_app = pattern(s.img.app_len, 0x66); f.fill(FW_APP_ADDR, old_app); f.protect_sector = (int)(FW_APP_ADDR / 4096) + 2;
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && r.phase == FWP_APP && strstr(r.why, "erase did not take") && r.sector == FW_APP_ADDR + 2 * 4096);
      bool programmed_there = false;
      for (size_t i = 0; i < f.log.size(); i++) programmed_there |= f.log[i].kind == 'P' && (uint32_t)f.log[i].arg * 256 >= FW_APP_ADDR + 2 * 4096 && (uint32_t)f.log[i].arg * 256 < FW_APP_ADDR + 3 * 4096;
      CHECK(!programmed_there); }
    // T152 (2026-11-05 history): a flash locked by Protect Flash refuses the FIRST erase and keeps its data: nothing was written, so no "do not switch off"
    // (started stays false), and the text names the lock and the cure; the same refusal one sector later (after an erase took) IS a half-written flash
    { FakeFlash f; Bytes old_app = pattern(s.img.app_len, 0x66); f.fill(FW_APP_ADDR, old_app); f.protect_sector = (int)(FW_APP_ADDR / 4096);
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && !r.ok && !r.started && r.phase == FWP_APP && strstr(r.why, "locked") && strstr(r.why, "power") && f.programs == 0);
      CHECK(!memcmp(&f.mem[FW_APP_ADDR], old_app.data(), old_app.size())); }
    { FakeFlash f; f.fill(FW_APP_ADDR, pattern(s.img.app_len, 0x66)); f.protect_sector = (int)(FW_APP_ADDR / 4096) + 1;
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && r.started && strstr(r.why, "erase did not take")); }
    // a program disturb: an earlier page changes after its own read-back; only the read-back of the whole write finds it
    { FakeFlash f; f.disturb_after = 60; f.disturb_addr = FW_APP_ADDR + 4096 + 300;
      CHECK(!fw_write(&f, s.img, 0, 0, &r) && r.phase == FWP_READBACK && strstr(r.why, "does not hold") && r.sector == FW_APP_ADDR + 4096 + 256); }
    // the stamp the walk finds in the flash is the one the image demands: an image that claims another one fails the read-back
    { Staged v = stage(load("vendor.cfw")); FakeFlash f; FwImage img = v.img; img.axss = 0x12345678;
      CHECK(!fw_write(&f, img, 0, 0, &r) && r.phase == FWP_READBACK && strstr(r.why, "stamp")); }
    // a stock image demands no stamp
    { Staged v = stage(load("vendor.cfw")); FakeFlash f; CHECK(v.ok && v.img.axss == 0 && fw_write(&f, v.img, 0, 0, &r) && fw_flash_equals(&f, v.img)); }
}

static void t_chip()
{
    FakeFlash f;
    const char *type, *why;
    CHECK(fw_flash_check(&f, &type, &why));
    f.type = "Winbond W25Q64"; CHECK(!fw_flash_check(&f, &type, &why) && strstr(why, "W25Q128") && !strcmp(type, "Winbond W25Q64"));
    f.type = "Spansion S25FL164K"; CHECK(!fw_flash_check(&f, &type, &why));
    f.type = "Winbond"; CHECK(!fw_flash_check(&f, &type, &why));
    CHECK(!fw_flash_check(0, &type, &why) && !strcmp(type, "none"));
    { FakeFlash g; g.page_size_v = 512; CHECK(!fw_flash_check(&g, &type, &why) && strstr(why, "geometry")); }
    { FakeFlash g; g.sector_size_v = 8192; CHECK(!fw_flash_check(&g, &type, &why)); }
    { FakeFlash g; g.needs_erase = false; CHECK(!fw_flash_check(&g, &type, &why)); }
#ifdef HOSTAPP_EMU_FLASH
    f.type = "Infineon S25FL128L"; CHECK(fw_flash_check(&f, &type, &why));
#else
    f.type = "Infineon S25FL128L"; CHECK(!fw_flash_check(&f, &type, &why));        // the emulator's chip is accepted by a test build only
#endif
}

// the two questions
static void t_confirm()
{
    FwConfirm c;
    // Y, then RETURN after the first second: go
    fwc_open(&c, 1000);
    CHECK(fwc_key(&c, FWCK_Y, 1500) == FWCE_REDRAW && c.state == FWC_ASK2);
    CHECK(fwc_key(&c, FWCK_RETURN, 1700) == FWCE_NOTHING && c.state == FWC_ASK2);          // inside the first second: the repeat of a held key
    CHECK(fwc_left(&c, 2000) == 10 || fwc_left(&c, 2000) == 9);
    CHECK(fwc_key(&c, FWCK_RETURN, 2600) == FWCE_GO && c.state == FWC_STARTED);
    // every other first answer is No: one confirmation is not enough, and RETURN is not Y
    int others[] = { FWCK_RETURN, FWCK_ESCAPE, FWCK_OTHER };
    for (int i = 0; i < 3; i++) { fwc_open(&c, 0); CHECK(fwc_key(&c, (FwcKey)others[i], 100) == FWCE_ABORT && c.state == FWC_CANCELLED); }
    // Y alone never starts
    fwc_open(&c, 0); CHECK(fwc_key(&c, FWCK_Y, 100) != FWCE_GO && c.state == FWC_ASK2);
    // second question: any other key, ESC, and the clock all cancel; a key inside the first second does not
    fwc_open(&c, 0); fwc_key(&c, FWCK_Y, 100);
    CHECK(fwc_key(&c, FWCK_Y, 300) == FWCE_NOTHING && c.state == FWC_ASK2);                // the repeat of the held Y
    CHECK(fwc_key(&c, FWCK_OTHER, 1500) == FWCE_ABORT);
    fwc_open(&c, 0); fwc_key(&c, FWCK_Y, 100); CHECK(fwc_key(&c, FWCK_ESCAPE, 1500) == FWCE_ABORT);
    fwc_open(&c, 0); fwc_key(&c, FWCK_Y, 100); CHECK(fwc_tick(&c, 5000) == FWCE_NOTHING && fwc_tick(&c, 10100) == FWCE_ABORT && c.state == FWC_CANCELLED);
    fwc_open(&c, 0); CHECK(fwc_tick(&c, 59000) == FWCE_NOTHING && fwc_tick(&c, 60000) == FWCE_ABORT);
    // a RETURN that comes after the 10 s window is not a go
    fwc_open(&c, 0); fwc_key(&c, FWCK_Y, 100); CHECK(fwc_tick(&c, 10200) == FWCE_ABORT && fwc_key(&c, FWCK_RETURN, 10300) == FWCE_NOTHING && c.state == FWC_CANCELLED);
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_fwrestore <dir>\n"); return 2; }
    dir = argv[1];
    load_facts();
    official.sha256 = strdup(facts["vendor_sha"].c_str());
    official.vendor = "Test";
    official.name = "Vendor firmware";
    official.version = "1.0";
    t_probe();
    t_offers();
    t_stage();
    t_gate();
    t_write();
    t_write_faults();
    t_chip();
    t_confirm();
    printf("test_fwrestore: %d failed\n", fails);
    return fails ? 1 : 0;
}
