// Host test of the guard's application side (host-app/software/hostapp/guard.cc, flashfw.cc record + recovery slot; T265 S3-S6): the blocks the boot ROM and the
// recovery program leave, the provisioning of a host started from the flash, the write order application -> copy -> record and the repair after a recovery start,
// all against a flash with erase semantics and a RAM disk that BOTH lose their power at a chosen operation: for every possible cut the state that is left must boot
// a complete application or hand over to a recovery program that finds a complete one, never start a half-written one and never leave nothing.
// The boot ROM's rule is the model `boot_decide` (docs/specs/T265/p1-p3-guard-and-recovery.md 1.3); the recovery program's image rule is guardimg.h, the same
// header the program is built with.
//   test_guard
#include "guard.h"
#include "guardimg.h"
#include "flash.h"
#include "txu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
typedef std::vector<uint8_t> Bytes;

// ---- the power: every change of the flash or of the RAM disk is one operation; from the cut on, none has an effect -----------------------------------------------
struct Power {
    int64_t cut_at = -1;       // -1: never; otherwise the number of operations that still take effect
    int64_t ops = 0;
    bool dead = false;
    bool tick()
    {
        if (dead) return false;
        if (cut_at >= 0 && ops >= cut_at) { dead = true; return false; }
        ops++;
        return true;
    }
} pw;

static const uint32_t FLASH_SIZE = 0x400000;
class FakeFlash : public Flash {
public:
    Bytes mem;
    int erases, programs;
    FakeFlash() : mem(FLASH_SIZE, 0xFF), erases(0), programs(0) { }
    const char *get_type_string(void) { return "Winbond W25Q128"; }
    int get_page_size(void) { return 256; }
    int get_sector_size(int) { return 4096; }
    int page_to_sector(int page) { return page / 16; }
    bool need_erase(void) { return true; }
    bool erase_sector(int s)
    {
        if (!pw.tick()) return false;
        erases++;
        memset(&mem[(size_t)s * 4096], 0xFF, 4096);
        return true;
    }
    bool read_page(int p, void *b) { memcpy(b, &mem[(size_t)p * 256], 256); return true; }
    bool write_page(int p, const void *b)
    {
        if (!pw.tick()) return false;
        programs++;
        const uint8_t *s = (const uint8_t *)b;
        for (int i = 0; i < 256; i++) mem[(size_t)p * 256 + i] &= s[i];
        return true;
    }
    Bytes slice(uint32_t a, uint32_t n) const { return Bytes(mem.begin() + a, mem.begin() + a + n); }
    void put(uint32_t a, const Bytes &v) { memcpy(&mem[a], v.data(), v.size()); }
};

struct RamFs : FoFs {
    struct H { std::string path; size_t pos; bool w; };
    std::map<std::string, Bytes> files;
    std::map<std::string, bool> dirs;
    uint64_t capacity;
    int mutations, new_files;
    bool flip;                                        // flips one bit of what is written to GUARD_NEW (a medium that does not keep what it was given)
    RamFs() : capacity(12u << 20), mutations(0), new_files(0), flip(false) { }
    uint64_t used() const { uint64_t u = 0; for (auto &f : files) u += f.second.size(); return u; }
    bool stat(const char *path, FoStat *st)
    {
        std::string p = path;
        if (files.count(p)) { st->dir = false; st->size = files[p].size(); return true; }
        if (dirs.count(p)) { st->dir = true; st->size = 0; return true; }
        return false;
    }
    int dir_entries(const char *) { return 0; }
    bool free_bytes(const char *, uint64_t *b) { *b = capacity - used(); return true; }
    bool same_volume(const char *, const char *) { return true; }
    void *open_read(const char *path)
    {
        if (!files.count(path)) return 0;
        H *h = new H; h->path = path; h->pos = 0; h->w = false; return h;
    }
    void *open_write(const char *path)
    {
        if (!pw.tick()) return 0;
        mutations++;
        if (std::string(path) == GUARD_NEW_PATH) new_files++;
        files[path] = Bytes();
        H *h = new H; h->path = path; h->pos = 0; h->w = true; return h;
    }
    int read(void *hh, void *buf, uint32_t n)
    {
        H *h = (H *)hh;
        Bytes &b = files[h->path];
        size_t k = b.size() - h->pos < n ? b.size() - h->pos : n;
        memcpy(buf, b.data() + h->pos, k);
        h->pos += k;
        return (int)k;
    }
    bool write(void *hh, const void *buf, uint32_t n)
    {
        H *h = (H *)hh;
        if (!pw.tick()) return false;
        if (used() + n > capacity) return false;
        mutations++;
        Bytes &b = files[h->path];
        b.insert(b.end(), (const uint8_t *)buf, (const uint8_t *)buf + n);
        if (flip && h->path == GUARD_NEW_PATH && b.size() > 5000 && b.size() - n <= 5000) b[5000] ^= 0x10;
        return true;
    }
    bool close(void *hh) { delete (H *)hh; return true; }
    bool remove(const char *path)
    {
        if (!pw.tick()) return false;
        mutations++;
        return files.erase(path) > 0;
    }
    bool rename(const char *from, const char *to)
    {
        if (!pw.tick()) return false;
        mutations++;
        if (!files.count(from) || files.count(to)) return false;     // f_rename onto an existing file fails
        files[to] = files[from];
        files.erase(from);
        return true;
    }
    bool make_dir(const char *p) { dirs[p] = true; return true; }
};

struct Board { FakeFlash f; RamFs fs; };

// ---- images ---------------------------------------------------------------------------------------------------------------------------------------------
static void put32(Bytes &b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i)); }
static Bytes make_app(uint8_t seed, uint32_t dlen)                    // a host application: the record, the data padded to 4
{
    uint32_t padded = (dlen + 3) & ~3u;
    Bytes b(12 + padded, 0);
    put32(b, 0, 0x30000); put32(b, 4, dlen); put32(b, 8, 0x30000);
    for (uint32_t i = 0; i < padded; i++) b[12 + i] = (uint8_t)(i * 7 + seed + (i >> 8));
    return b;
}
static Bytes make_rec(uint8_t seed, uint32_t dlen)                    // a recovery program: record, data, CRC-32 trailer over both
{
    Bytes b(12 + ((dlen + 3) & ~3u), 0);
    put32(b, 0, 0x01800000); put32(b, 4, dlen); put32(b, 8, 0x01800000);
    for (size_t i = 12; i < b.size(); i++) b[i] = (uint8_t)(i * 11 + seed);
    uint32_t c = fw_crc32(b.data(), (uint32_t)b.size());
    b.resize(b.size() + 4);
    put32(b, b.size() - 4, c);
    return b;
}
static uint32_t crc_of(const Bytes &b) { return fw_crc32(b.data(), (uint32_t)b.size()); }

// ---- the boot ROM's rule (the model) and the recovery program's pick ---------------------------------------------------------------------------------------
enum Boot { B_APP, B_RECOVERY, B_DEAD };
static bool recovery_intact(FakeFlash &f)                              // the boot ROM's check of the slot: header in range, the trailer CRC
{
    Bytes h = f.slice(FW_RECOVERY_ADDR, 12);
    uint32_t dest = guardimg_le32(h.data()), len = guardimg_le32(h.data() + 4);
    if (len == 0 || len > FW_RECOVERY_SLOT - 16 || dest != 0x01800000u) return false;
    uint32_t total = 12 + ((len + 3) & ~3u);
    Bytes all = f.slice(FW_RECOVERY_ADDR, total + 4);
    return guardimg_le32(all.data() + total) == fw_crc32(all.data(), total);
}
static bool g_guard_on = true;
static Boot boot_decide(FakeFlash &f)
{
    uint32_t len = 0, crc = 0;
    bool app = fw_flash_app_crc(&f, &len, &crc);
    FwGuardRecord rec;
    if (!g_guard_on || !fw_read_record(&f, &rec)) return app ? B_APP : B_DEAD;     // no record: as before this host
    if (app && rec.len == len && rec.crc == crc) return B_APP;
    return recovery_intact(f) ? B_RECOVERY : B_DEAD;
}

struct Pick { bool found; int medium; Bytes img; };
static bool load_file(RamFs &fs, const char *path, Bytes *out)
{
    if (!fs.files.count(path)) return false;
    Bytes &b = fs.files[path];
    uint32_t total;
    if (b.size() < 12 || !guardimg_ok(b.data(), b.size(), &total)) return false;
    *out = b;
    return true;
}
static Pick recovery_pick(RamFs &fs, const Bytes *sd_root)                // flash copy, GUARD_NEW, SD root (USB roots would follow)
{
    Pick p; p.found = false; p.medium = 0;
    if (load_file(fs, GUARD_COPY_PATH, &p.img)) { p.found = true; p.medium = GM_FLASH_COPY; return p; }
    if (load_file(fs, GUARD_NEW_PATH, &p.img)) { p.found = true; p.medium = GM_FLASH_NEW; return p; }
    uint32_t total;
    if (sd_root && sd_root->size() >= 12 && guardimg_ok(sd_root->data(), sd_root->size(), &total)) { p.found = true; p.medium = GM_SD; p.img = *sd_root; return p; }
    return p;
}

// the host application, started by the recovery program: the check, the write unless the flash holds it, the copy and the record
static bool run_repair(Board &b, const Pick &p, uint32_t iface = TXU_IFACE_NEEDED)
{
    FakeFlash &f = b.f;
    guard_install_fs(&b.fs);
    GuardInfo gi;
    memset(&gi, 0, sizeof(gi));
    gi.start = GS_RECOVERY; gi.iface = TXU_IFACE_PROVIDED; gi.medium = p.medium;
    gi.stage_len = (uint32_t)p.img.size(); gi.stage_crc = crc_of(p.img);
    FwImage img;
    const char *why = "";
    if (!guard_repair_check(gi, iface, p.img.data(), &img, &why)) return false;
    if (!fw_flash_equals_app(&f, img)) {
        FwWriteResult r;
        if (!fw_write_parts(&f, img, FW_PART_APP, 0, 0, &r)) return false;
    }
    GuardOut o;
    guard_repair_finish(&f, gi, img, &o);
    return o.record_ok;
}

// ---- the state of a board -----------------------------------------------------------------------------------------------------------------------------
static void flash_app(FakeFlash &f, const Bytes &app) { memset(&f.mem[FW_APP_ADDR], 0xFF, 0x1E0000); f.put(FW_APP_ADDR, app); }

// the host started from the flash (verdict "no record" / "match"): the engine runs with the recovery program it carries
static GuardInfo flash_start(const Bytes &app) { GuardInfo g; memset(&g, 0, sizeof(g)); g.start = GS_FLASH; g.iface = TXU_IFACE_PROVIDED; g.flash_len = (uint32_t)app.size(); g.flash_crc = crc_of(app); return g; }
static bool provision(Board &b, const Bytes &app, const Bytes &rec, GuardProv *o)
{
    guard_install_fs(&b.fs);
    GuardEnv env = { &b.f, rec.data(), (uint32_t)rec.size(), 0 };
    return guard_provision(env, flash_start(app), o);
}
// the writer of the .txu (host_core_flow.cc): the application, then the copy, then the record
static bool update_app(Board &b, const Bytes &app, GuardOut *go)
{
    guard_install_fs(&b.fs);
    FwImage img;
    const char *why;
    if (!fw_image_seal_parts(&img, true, 0, 0, app.data(), (uint32_t)app.size(), 0, &why)) return false;
    FwWriteResult r;
    if (!fw_write_parts(&b.f, img, FW_PART_APP, 0, 0, &r)) return false;
    return guard_after_update(&b.f, app.data(), (uint32_t)app.size(), go);
}

static Bytes v1, v2, foreign, rec1, rec2;
static Board *base;                                                                // a provisioned board with v1

static void blocks()
{
    uint32_t w[GUARD_BLK_WORDS];
    GuardInfo g;
    memset(w, 0, sizeof(w));
    guard_decode(w, &g);
    CHECK(g.start == GS_OTHER);                                                   // nothing written: a JTAG load, the old boot ROM
    w[GUARD_W_RESULT] = GUARD_RES_VALID; w[GUARD_W_RESULT + 1] = GUARD_ST_BOOTING; w[GUARD_W_RESULT + 2] = GUARD_SRC_FLASH;
    guard_decode(w, &g);
    CHECK(g.start == GS_OTHER);                                                   // the result block alone, with no guard block: an older boot ROM
    w[GUARD_W_GUARD] = GUARD_BLOCK_MAGIC; w[GUARD_W_GUARD + 1] = GV_MATCH; w[GUARD_W_GUARD + 4] = 1234; w[GUARD_W_GUARD + 5] = 0xCAFEF00Du; w[GUARD_W_GUARD + 6] = 1;
    guard_decode(w, &g);
    CHECK(g.start == GS_FLASH && g.verdict == GV_MATCH && g.flash_len == 1234 && g.flash_crc == 0xCAFEF00Du && g.iface == 1);
    w[GUARD_W_RESULT + 2] = 2;                                                    // booted from a staged image
    guard_decode(w, &g);
    CHECK(g.start == GS_OTHER);
    // started by the recovery program: the status is the guard's and the recovery block is intact
    w[GUARD_W_RESULT + 1] = GUARD_ST_GUARD; w[GUARD_W_RESULT + 2] = GUARD_SRC_FLASH;
    w[GUARD_W_RECOVERY] = GUARD_RECOVERY_MAGIC; w[GUARD_W_RECOVERY + 1] = GM_SD; w[GUARD_W_RECOVERY + 2] = 0x03000000; w[GUARD_W_RECOVERY + 3] = 5000; w[GUARD_W_RECOVERY + 4] = 0x1234;
    w[GUARD_W_RECOVERY + 5] = 1;
    w[GUARD_W_RECOVERY + 7] = guard_recovery_chk(w + GUARD_W_RECOVERY);
    guard_decode(w, &g);
    CHECK(g.start == GS_RECOVERY && g.medium == GM_SD && g.stage_addr == 0x03000000 && g.stage_len == 5000 && g.stage_crc == 0x1234 && g.copy_matches_record == 1);
    w[GUARD_W_RECOVERY + 3] ^= 1;                                                 // a recovery block that does not agree with its check
    guard_decode(w, &g);
    CHECK(g.start == GS_OTHER);
    w[GUARD_W_RECOVERY + 3] ^= 1; w[GUARD_W_GUARD] = 0;                           // a stale block: the boot ROM clears its magic on every start
    guard_decode(w, &g);
    CHECK(g.start == GS_OTHER);
    printf("PASS blocks: flash start, recovery start, and every incomplete block reads as no guard\n");
}

static void image_rule()
{
    Bytes a = make_app(1, 100);
    uint32_t t;
    CHECK(guardimg_ok(a.data(), a.size(), &t) && t == a.size());
    CHECK(!guardimg_ok(a.data(), a.size() + 4, &t) && !guardimg_ok(a.data(), a.size() - 4, &t));      // bytes behind it, a truncated file
    Bytes b = a; put32(b, 8, 0x30000 + 400); CHECK(!guardimg_ok(b.data(), b.size(), &t));              // start outside the image
    b = a; put32(b, 0, 0x30002); CHECK(!guardimg_ok(b.data(), b.size(), &t));                          // unaligned load address
    b = a; put32(b, 4, 0); CHECK(!guardimg_ok(b.data(), 12, &t));
    printf("PASS image rule: only the record and its padded data, in the ranges the boot ROM takes\n");
}

static void record_and_recovery_slot()
{
    FakeFlash f;
    FwGuardRecord r;
    CHECK(!fw_read_record(&f, &r));                                               // erased: no record
    FwWriteResult w;
    CHECK(fw_write_record(&f, 1000, 0xDEADBEEF, &w) && fw_read_record(&f, &r) && r.len == 1000 && r.crc == 0xDEADBEEF);
    CHECK(f.mem[FW_RECORD_ADDR] == 'T' && f.mem[FW_RECORD_ADDR + 1] == 'R' && f.mem[FW_RECORD_ADDR + 2] == 'X' && f.mem[FW_RECORD_ADDR + 3] == 'G');   // "TRXG" in flash order
    CHECK(f.mem[FW_RECORD_ADDR + 16] == 0xFF && f.mem[FW_RECORD_ADDR + 4095] == 0xFF);                       // the rest of the sector stays erased
    CHECK(fw_write_record(&f, 2000, 0x12345678, &w) && fw_read_record(&f, &r) && r.len == 2000 && r.crc == 0x12345678);
    f.mem[FW_RECORD_ADDR + 13] &= 0x7F;                                           // a flipped chk bit: not a record
    CHECK(!fw_read_record(&f, &r));
    // the record is the zlib CRC-32 the boot ROM makes word by word
    Bytes a = make_app(3, 777);
    uint32_t len = 0, crc = 0;
    flash_app(f, a);
    CHECK(fw_flash_app_crc(&f, &len, &crc) && len == a.size() && crc == crc_of(a));
    // the recovery slot
    Bytes rec = make_rec(5, 20000);
    CHECK(!fw_recovery_equals(&f, rec.data(), (uint32_t)rec.size()));
    CHECK(fw_write_recovery(&f, rec.data(), (uint32_t)rec.size(), &w) && fw_recovery_equals(&f, rec.data(), (uint32_t)rec.size()) && recovery_intact(f));
    CHECK(f.mem[FW_RECORD_ADDR + 1] == 'R');                                      // the recovery write left the record sector alone
    Bytes big(FW_RECOVERY_SLOT + 4, 0);
    CHECK(!fw_write_recovery(&f, big.data(), (uint32_t)big.size(), &w));
    printf("PASS record and recovery slot: the 16-byte record (TRXG), its chk, the recovery image written and read back\n");
}

static void provision_fresh()
{
    // S6: a board whose boot ROM is new but has no record, no copy and no recovery program
    Board *b = new Board;
    flash_app(b->f, v1);
    GuardProv o;
    CHECK(provision(*b, v1, rec1, &o) && o.app_ok && o.rec_written && o.copy_written && o.record_written);
    FwGuardRecord r;
    CHECK(fw_read_record(&b->f, &r) && r.len == v1.size() && r.crc == crc_of(v1));
    CHECK(b->fs.files[GUARD_COPY_PATH] == v1 && guard_copy_current((uint32_t)v1.size(), crc_of(v1)));
    CHECK(fw_recovery_equals(&b->f, rec1.data(), (uint32_t)rec1.size()) && recovery_intact(b->f));
    CHECK(boot_decide(b->f) == B_APP);
    CHECK(!b->fs.files.count(GUARD_NEW_PATH));
    // started again: everything is current, nothing is written
    int er = b->f.erases, pr = b->f.programs, mu = b->fs.mutations;
    CHECK(provision(*b, v1, rec1, &o) && !o.rec_written && !o.copy_written && !o.record_written);
    CHECK(b->f.erases == er && b->f.programs == pr && b->fs.mutations == mu);
    // a new recovery program in a new app: only the recovery slot is written
    er = b->f.erases;
    CHECK(provision(*b, v1, rec2, &o) && o.rec_written && !o.copy_written && !o.record_written);
    CHECK(fw_recovery_equals(&b->f, rec2.data(), (uint32_t)rec2.size()) && b->f.erases > er);
    // the stamp is gone (a cut after the rename): the copy is written again, the record stays
    b->fs.files.erase(GUARD_STAMP_PATH);
    CHECK(provision(*b, v1, rec2, &o) && o.copy_written && !o.record_written);
    // a copy of another size under a valid stamp is not believed
    b->fs.files[GUARD_COPY_PATH].pop_back();
    CHECK(provision(*b, v1, rec2, &o) && o.copy_written && b->fs.files[GUARD_COPY_PATH] == v1);
    // the flash holds another application than the one the boot ROM measured: nothing is written
    FakeFlash &f = b->f;
    GuardEnv env = { &f, rec2.data(), (uint32_t)rec2.size(), 0 };
    GuardInfo gi = flash_start(v2);
    er = f.erases; pr = f.programs; mu = b->fs.mutations;
    CHECK(!guard_provision(env, gi, &o) && !o.app_ok && f.erases == er && f.programs == pr && b->fs.mutations == mu);
    // a boot ROM that read no CRC (0): the CRC is made here
    gi.flash_crc = 0;
    CHECK(guard_provision(env, gi, &o) && o.app_ok);
    // an erased application slot
    memset(&f.mem[FW_APP_ADDR], 0xFF, 4096);
    CHECK(!guard_provision(env, flash_start(v1), &o) && !o.app_ok);
    delete b;
    printf("PASS provisioning: S6 board gets recovery, copy and record; a second start writes nothing; a stale stamp or size is rewritten; a foreign flash app is refused\n");
}

// the cut sweep of the provisioning: the application is never touched, so the next power-on boots it, whatever was left; the next start finishes the job
static void provision_sweep()
{
    int cuts = 0;
    for (int64_t cut = 0;; cut++) {
        Board *b = new Board;
        flash_app(b->f, v1);
        pw = Power();
        pw.cut_at = cut;
        GuardProv o;
        provision(*b, v1, rec1, &o);
        bool finished = !pw.dead;
        pw.dead = false; pw.cut_at = -1;                                          // power on
        CHECK(boot_decide(b->f) == B_APP);
        FwGuardRecord r;
        CHECK(provision(*b, v1, rec1, &o));                                       // the next start brings it to the end
        CHECK(fw_read_record(&b->f, &r) && r.len == v1.size() && r.crc == crc_of(v1) && guard_copy_current((uint32_t)v1.size(), crc_of(v1)) && recovery_intact(b->f));
        CHECK(boot_decide(b->f) == B_APP);
        delete b;
        cuts++;
        if (finished) break;
    }
    pw = Power();
    CHECK(cuts > 100);
    printf("PASS provisioning cut sweep: %d cut points, the application boots after each and the next start completes it\n", cuts);
}

static void update_sweep()
{
    // S5: provisioned board with v1, update to v2: application, copy, record. Power lost at every operation.
    int cuts = 0, n_app = 0, n_rec = 0, n_v1 = 0, n_v2 = 0;
    for (int64_t cut = 0;; cut++) {
        Board *b = new Board(*base);
        pw = Power();
        pw.cut_at = cut;
        GuardOut go;
        update_app(*b, v2, &go);
        bool finished = !pw.dead;
        pw.dead = false; pw.cut_at = -1;
        Boot bt = boot_decide(b->f);
        CHECK(bt != B_DEAD);
        uint32_t len = 0, crc = 0;
        if (bt == B_APP) {
            n_app++;
            CHECK(fw_flash_app_crc(&b->f, &len, &crc));
            CHECK((len == v1.size() && crc == crc_of(v1)) || (len == v2.size() && crc == crc_of(v2)));       // a complete application, never a half one
        } else if (bt == B_RECOVERY) {
            n_rec++;
            Pick p = recovery_pick(b->fs, 0);
            CHECK(p.found && (p.img == v1 || p.img == v2));
            CHECK(p.found && run_repair(*b, p));
            CHECK(boot_decide(b->f) == B_APP);
            CHECK(fw_flash_app_crc(&b->f, &len, &crc) && crc == crc_of(p.img));
            CHECK(guard_copy_current((uint32_t)p.img.size(), crc_of(p.img)) || (b->fs.files.count(GUARD_COPY_PATH) && b->fs.files[GUARD_COPY_PATH] == p.img));
            (p.img == v1 ? n_v1 : n_v2)++;
        }
        if (finished) {
            CHECK(bt == B_APP && fw_flash_app_crc(&b->f, &len, &crc) && crc == crc_of(v2));
            CHECK(b->fs.files[GUARD_COPY_PATH] == v2 && guard_copy_current((uint32_t)v2.size(), crc_of(v2)));
        }
        delete b;
        cuts++;
        if (finished) break;
    }
    pw = Power();
    CHECK(n_rec > 0 && n_app > 0 && n_v1 > 0 && n_v2 > 0);                        // the windows exist: some cuts boot straight on, some recover the old, some the new
    printf("PASS update cut sweep: %d cut points: %d boot at once, %d recover (%d back to the old app, %d to the new), none dead, none half\n", cuts, n_app, n_rec, n_v1, n_v2);
}

static void foreign_overwrite()
{
    // S2 + S3 + S4: another firmware's updater wrote its application over ours; the next power-on runs the recovery
    Board *b = new Board(*base);
    flash_app(b->f, foreign);
    CHECK(boot_decide(b->f) == B_RECOVERY);
    Pick p = recovery_pick(b->fs, 0);
    CHECK(p.found && p.medium == GM_FLASH_COPY && p.img == v1);
    b->fs.new_files = 0;
    CHECK(run_repair(*b, p));
    CHECK(b->fs.new_files == 0);                                                  // the stage is the copy: it is not written again, only its stamp
    CHECK(boot_decide(b->f) == B_APP && b->f.slice(FW_APP_ADDR, (uint32_t)v1.size()) == v1);
    CHECK(b->fs.files[GUARD_COPY_PATH] == v1 && guard_copy_current((uint32_t)v1.size(), crc_of(v1)));
    // the flash copy is gone, a file in the root of the SD card or a stick takes its place (S4)
    flash_app(b->f, foreign);
    b->fs.files.clear();
    p = recovery_pick(b->fs, 0);
    CHECK(!p.found);                                                              // nothing anywhere: the boot ROM waits for JTAG as before
    p = recovery_pick(b->fs, &v2);
    CHECK(p.found && p.medium == GM_SD && p.img == v2);
    CHECK(run_repair(*b, p));
    CHECK(boot_decide(b->f) == B_APP && b->f.slice(FW_APP_ADDR, (uint32_t)v2.size()) == v2);
    CHECK(b->fs.files[GUARD_COPY_PATH] == v2);                                    // the copy is made again from the stage
    // the order: the flash copy before the SD card; GUARD_NEW (a cut between unlink and rename) before the SD card; a partial GUARD_NEW is not an image
    b->fs.files[GUARD_COPY_PATH] = v1;
    CHECK(recovery_pick(b->fs, &v2).medium == GM_FLASH_COPY);
    b->fs.files.erase(GUARD_COPY_PATH);
    b->fs.files[GUARD_NEW_PATH] = v1;
    p = recovery_pick(b->fs, &v2);
    CHECK(p.medium == GM_FLASH_NEW && p.img == v1);
    b->fs.files[GUARD_NEW_PATH].resize(5000);
    CHECK(recovery_pick(b->fs, &v2).medium == GM_SD);
    // a recovery program that is damaged hands over to nothing: the JTAG wait of today
    flash_app(b->f, foreign);
    b->f.mem[FW_RECOVERY_ADDR + 20] ^= 0x55;
    CHECK(boot_decide(b->f) == B_DEAD);
    delete b;
    printf("PASS recovery: a foreign app is replaced from the copy (S3), from the SD root when the copy is gone (S4), nothing found waits for JTAG\n");
}

static void repair_sweep()
{
    // the repair itself loses its power at every operation: the next power-on is the recovery again or the application, never dead, never half
    int cuts = 0;
    for (int64_t cut = 0;; cut++) {
        Board *b = new Board(*base);
        flash_app(b->f, foreign);
        guard_install_fs(&b->fs);
        Pick p = recovery_pick(b->fs, 0);
        pw = Power();
        pw.cut_at = cut;
        run_repair(*b, p);
        bool finished = !pw.dead;
        pw.dead = false; pw.cut_at = -1;
        Boot bt = boot_decide(b->f);
        CHECK(bt != B_DEAD);
        if (bt == B_APP) {
            uint32_t len, crc;
            CHECK(fw_flash_app_crc(&b->f, &len, &crc) && len == v1.size() && crc == crc_of(v1));
        } else {
            Pick q = recovery_pick(b->fs, 0);
            CHECK(q.found && q.img == v1 && run_repair(*b, q) && boot_decide(b->f) == B_APP);
        }
        delete b;
        cuts++;
        if (finished) break;
    }
    pw = Power();
    printf("PASS repair cut sweep: %d cut points, each ends in the application or in the recovery again\n", cuts);
}

static void copy_cannot_be_written()
{
    // the flash drive is full: the new copy cannot be written. The old one must not stay behind a new record (a later recovery would start the OLD application).
    Board *b = new Board(*base);
    b->fs.capacity = b->fs.used() + 100000;
    GuardOut go;
    CHECK(update_app(*b, v2, &go));
    CHECK(!go.copy_ok && go.copy_room && go.record_ok);
    CHECK(!b->fs.files.count(GUARD_COPY_PATH) && !b->fs.files.count(GUARD_NEW_PATH) && !b->fs.files.count(GUARD_STAMP_PATH));
    FwGuardRecord r;
    CHECK(fw_read_record(&b->f, &r) && r.crc == crc_of(v2) && boot_decide(b->f) == B_APP);
    flash_app(b->f, foreign);
    CHECK(boot_decide(b->f) == B_RECOVERY);
    CHECK(!recovery_pick(b->fs, 0).found);                                        // nothing: the SD or USB root, or JTAG - not the old application
    delete b;
    printf("PASS no room: the old copy is dropped before the new record, the recovery never starts an older application\n");
}

static void copy_is_verified()
{
    // the medium does not keep what it was given: the copy is read back, the old one stays, nothing is stamped
    Board *b = new Board(*base);
    guard_install_fs(&b->fs);
    b->fs.flip = true;
    GuardMem src(v2.data(), (uint32_t)v2.size());
    const char *why = "";
    bool room = false;
    CHECK(!guard_copy_write(src, (uint32_t)v2.size(), crc_of(v2), &why, &room) && !room && strstr(why, "read back"));
    CHECK(b->fs.files[GUARD_COPY_PATH] == v1 && !b->fs.files.count(GUARD_NEW_PATH) && guard_copy_current((uint32_t)v1.size(), crc_of(v1)));
    delete b;
    printf("PASS copy read-back: a copy that does not read back as written is not given its name, the old copy and its stamp stay\n");
}

static void bit_write_keeps_recovery()
{
    // a host write (bitstream + application) must not touch the recovery slot and the record: the bitstream slot ends at 0x1E0000
    Board *b = new Board(*base);
    FwGuardRecord rec_in;
    Bytes bit(0x1D0000, 0);
    for (size_t i = 0; i < bit.size(); i++) bit[i] = (uint8_t)(i * 3 + 1);
    FwImage img;
    const char *why;
    CHECK(fw_image_seal_parts(&img, true, bit.data(), (uint32_t)bit.size(), v2.data(), (uint32_t)v2.size(), 0, &why));
    Bytes rec_before = b->f.slice(FW_RECOVERY_ADDR, FW_RECOVERY_SLOT), recd_before = b->f.slice(FW_RECORD_ADDR, 4096);
    FwWriteResult r;
    CHECK(fw_write_parts(&b->f, img, FW_PART_BIT | FW_PART_APP, 0, 0, &r));
    CHECK(b->f.slice(FW_RECOVERY_ADDR, FW_RECOVERY_SLOT) == rec_before && b->f.slice(FW_RECORD_ADDR, 4096) == recd_before);
    CHECK(b->f.slice(0, (uint32_t)bit.size()) == bit);
    // the bitstream does not fit a slot that ends where the recovery begins
    Bytes huge(0x1E0000 + 4, 1);
    CHECK(!fw_image_seal_parts(&img, true, huge.data(), (uint32_t)huge.size(), 0, 0, 0, &why));
    // a whole firmware of Gideon or Commodore (a restore, fw_image_seal): its bitstream runs to 0x220000 and takes the recovery slot and the record with it - a clean install of that firmware
    Bytes stock(0x217300, 0);
    for (size_t i = 0; i < stock.size(); i++) stock[i] = (uint8_t)(i * 5 + 3);
    CHECK(fw_image_seal(&img, true, stock.data(), (uint32_t)stock.size(), foreign.data(), (uint32_t)foreign.size(), 0, &why) && img.bit_slot == FW_BIT_SLOT_FULL);
    CHECK(fw_write_parts(&b->f, img, FW_PART_BIT | FW_PART_APP, 0, 0, &r));
    CHECK(b->f.slice(0, (uint32_t)stock.size()) == stock && !fw_read_record(&b->f, &rec_in) && !recovery_intact(b->f));
    delete b;
    printf("PASS slots: a host write leaves the recovery program and the record alone; a bitstream past 0x1E0000 is refused; a whole stock firmware (restore) may take them\n");
}

static void repair_refusals()
{
    GuardInfo gi;
    memset(&gi, 0, sizeof(gi));
    gi.start = GS_RECOVERY; gi.iface = 1; gi.stage_len = (uint32_t)v1.size(); gi.stage_crc = crc_of(v1);
    FwImage img;
    const char *why = "";
    CHECK(guard_repair_check(gi, 1, v1.data(), &img, &why) && img.sealed == FW_SEALED && img.app_len == v1.size());
    CHECK(!guard_repair_check(gi, 2, v1.data(), &img, &why) && img.sealed != FW_SEALED && strstr(why, "interface"));        // an app for another host interface
    gi.stage_crc ^= 1;
    CHECK(!guard_repair_check(gi, 1, v1.data(), &img, &why) && strstr(why, "intact"));
    gi.stage_crc ^= 1;
    Bytes bad = v1; bad[40] ^= 1;
    CHECK(!guard_repair_check(gi, 1, bad.data(), &img, &why));
    gi.start = GS_FLASH;
    CHECK(!guard_repair_check(gi, 1, v1.data(), &img, &why));
    printf("PASS repair check: another interface, a damaged stage and a start that was not the recovery's are refused before a write\n");
}

int main()
{
    v1 = make_app(1, 40000);
    v2 = make_app(9, 52000);
    foreign = make_app(77, 61000);
    rec1 = make_rec(5, 20000);
    rec2 = make_rec(6, 21000);
    blocks();
    image_rule();
    record_and_recovery_slot();
    provision_fresh();
    base = new Board;
    flash_app(base->f, v1);
    GuardProv o;
    CHECK(provision(*base, v1, rec1, &o));
    CHECK(boot_decide(base->f) == B_APP);
    provision_sweep();
    update_sweep();
    foreign_overwrite();
    repair_sweep();
    copy_cannot_be_written();
    copy_is_verified();
    bit_write_keeps_recovery();
    repair_refusals();
    printf(fails ? "test_guard: %d FAILED\n" : "test_guard: ok (%d)\n", fails);
    return fails ? 1 : 0;
}
