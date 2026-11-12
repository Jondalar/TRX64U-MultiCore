// Host test of the ESP32 flashing (host-app/software/hostapp/espflash.cc; T152 S8) against a model of the ESP32 ROM loader (esp_loader_model.h). Files from
// mkesptest.py. What is proven: the three images land at 0x0 / 0x8000 / 0x10000 byte for byte (the rest of the last block 0xFF), in the vendor updater's
// order of commands, at the right baud rate; the pins are armed for the write and free again after it, the chip is put back to run mode whatever happened;
// the version the module reports afterwards has to be the file's; every failure names where it stopped and whether the ESP32's flash was changed.
//   test_espflash <dir of mkesptest.py>
#include "espflash.h"
#include "esp_loader_model.h"
#include <stdio.h>
#include <string>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct FileSrc : CorSource {
    FILE *f;
    uint32_t sz;
    explicit FileSrc(const std::string &p) : f(fopen(p.c_str(), "rb")), sz(0)
    {
        if (f) { fseek(f, 0, SEEK_END); sz = (uint32_t)ftell(f); }
    }
    ~FileSrc() { if (f) fclose(f); }
    uint32_t size() { return sz; }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if (!f || (uint64_t)off + len > sz) return false;
        fseek(f, off, SEEK_SET);
        return fread(buf, 1, len, f) == len;
    }
};

static uint32_t g_last_done, g_last_total;
static int g_stage_max = -1;
static void prog(void *, int stage, uint32_t done, uint32_t total)
{
    g_last_done = done; g_last_total = total;
    if (stage > g_stage_max) g_stage_max = stage;
}

struct Run {
    FileSrc src;
    EspInfo info;
    EspResult insp;
    EspFlashResult res;
    bool ok;
    Run(const std::string &path, EspLoaderModel &m, bool do_flash = true) : src(path)
    {
        insp = esp_inspect(src, &info, 0, 0, 0);
        ok = false;
        g_last_done = g_last_total = 0; g_stage_max = -1;
        if (do_flash) ok = esp_flash(m, src, info, prog, 0, &res);
    }
};

// the file's bytes at [off, off+len) against the model's flash at addr, the rest of the last block 0xFF
static bool image_in_flash(FileSrc &s, EspLoaderModel &m, uint32_t off, uint32_t len, uint32_t addr)
{
    std::vector<uint8_t> b(len);
    if (!s.read(off, b.data(), len) || memcmp(&m.flash[addr], b.data(), len)) return false;
    uint32_t end = (len + ESPF_BLOCK - 1) / ESPF_BLOCK * ESPF_BLOCK;
    for (uint32_t i = len; i < end; i++) if (m.flash[addr + i] != 0xFF) return false;
    return true;
}

static bool flash_untouched(EspLoaderModel &m)
{
    for (size_t i = 0; i < m.flash.size(); i += 4096) if (m.flash[i] != 0xFF) return false;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_espflash <dir>\n"); return 2; }
    std::string dir = argv[1];
    // ---- the version text
    {
        uint16_t a = 9, b = 9;
        CHECK(esp_version_parse("V1.11", &a, &b) && a == 1 && b == 11);
        CHECK(esp_version_parse("V1.8", &a, &b) && a == 1 && b == 8);
        CHECK(esp_version_parse("V12.0", &a, &b) && a == 12 && b == 0);
        CHECK(!esp_version_parse("", &a, &b) && !esp_version_parse("V1", &a, &b) && !esp_version_parse("V1.", &a, &b) && !esp_version_parse("1.11", &a, &b) &&
              !esp_version_parse("V1.11x", &a, &b) && !esp_version_parse("V.5", &a, &b) && !esp_version_parse(0, &a, &b));
    }
    // ---- S8: the set of a .ue2 goes in, byte for byte, and the module reports the file's version
    for (int v = 0; v < 2; v++) {
        EspLoaderModel m;
        Run r(dir + (v ? "/v18.ue2" : "/v111.ue2"), m);
        CHECK(r.insp == ESP_FOUND && r.ok && r.res.ok && r.res.started && r.res.ident == EFI_MATCH);
        CHECK(r.res.major == 1 && r.res.minor == (v ? 8 : 11));
        CHECK(image_in_flash(r.src, m, r.info.boot_off, r.info.boot_len, 0x0000));
        CHECK(image_in_flash(r.src, m, r.info.ptable_off, r.info.ptable_len, 0x8000));
        CHECK(image_in_flash(r.src, m, r.info.app_off, r.info.app_len, 0x10000));
        CHECK(m.begin_addrs.size() == 3 && m.begin_addrs[0] == 0x0000 && m.begin_addrs[1] == 0x8000 && m.begin_addrs[2] == 0x10000);
        CHECK(!m.saw_bad_order && m.log.empty());
        CHECK(m.arm_on == 1 && m.arm_off == 1 && !m.armed);                           // armed for the write, free again after it
        CHECK(m.leave_calls == 1 && m.run_mode && m.baud == 115200 && m.identify_calls == 1);
        CHECK(m.op_count[0x0D] == 1 && m.op_count[0x0F] == 1 && m.op_count[0x0B] == 1 && m.op_count[0x02] == 3);
        CHECK(g_last_total > 0 && g_last_done == g_last_total && g_stage_max == EFS_RUN);
        CHECK(m.blocks_ok == g_last_total);
    }
    // ---- the loader's first tries to enter see no DOWNLOAD message: the module tries again (3 times), the fourth call is not made
    {
        EspLoaderModel m; m.enter_fail_first = 2;
        Run r(dir + "/v111.ue2", m);
        CHECK(r.ok && m.enter_calls == 3);
        EspLoaderModel m2; m2.enter_fail_first = 3;
        Run r2(dir + "/v111.ue2", m2);
        CHECK(!r2.ok && m2.enter_calls == 3 && r2.res.stage == EFS_ENTER && !r2.res.started);
    }
    // ---- failures before the first FLASH_BEGIN: nothing in the ESP32's flash changed, the chip is back in run mode and the pins free
    struct Early { const char *name; int op; int stage; bool no_dl, no_sync; const char *word; } early[] = {
        { "no DOWNLOAD message", -1, EFS_ENTER, true, false, "ROM loader" },
        { "no sync answer", -1, EFS_SYNC, false, true, "sync" },
        { "ATTACH_SPI refused", 0x0D, EFS_SETUP, false, false, "ATTACH_SPI" },
        { "CHANGE_BAUDRATE refused", 0x0F, EFS_SETUP, false, false, "CHANGE_BAUDRATE" },
        { "SET_FLASH_PARAMS refused", 0x0B, EFS_SETUP, false, false, "SET_FLASH_PARAMS" },
    };
    for (unsigned i = 0; i < sizeof(early) / sizeof(early[0]); i++) {
        EspLoaderModel m; m.no_download_msg = early[i].no_dl; m.no_sync = early[i].no_sync; m.fail_cmd_op = early[i].op; m.fail_cmd_nth = 1;
        Run r(dir + "/v111.ue2", m);
        if (r.ok || r.res.started || r.res.stage != early[i].stage || !strstr(r.res.why, early[i].word)) printf("  %s: ok %d started %d stage %d why '%s'\n", early[i].name, r.ok, r.res.started, r.res.stage, r.res.why);
        CHECK(!r.ok && !r.res.ok && !r.res.started && r.res.stage == early[i].stage && strstr(r.res.why, early[i].word));
        CHECK(flash_untouched(m) && m.begin_addrs.empty());
        CHECK(m.leave_calls == 1 && m.run_mode && !m.armed && m.arm_on == 1 && m.arm_off == 1 && m.identify_calls == 0);
    }
    // ---- failures inside the write: the page can say the ESP32's flash is changed
    {
        EspLoaderModel m; m.fail_cmd_op = 0x02; m.fail_cmd_nth = 2;                    // the partition table's FLASH_BEGIN
        Run r(dir + "/v111.ue2", m);
        CHECK(!r.ok && r.res.started && r.res.stage == EFS_PTABLE && strstr(r.res.why, "FLASH_BEGIN"));
        CHECK(image_in_flash(r.src, m, r.info.boot_off, r.info.boot_len, 0) && m.begin_addrs.size() == 1);
        CHECK(m.leave_calls == 1 && !m.armed && m.arm_off == 1 && m.identify_calls == 0);
    }
    {
        EspLoaderModel m; m.fail_cmd_op = 0x03; m.fail_cmd_nth = 5;
        Run r(dir + "/v111.ue2", m);
        CHECK(!r.ok && r.res.started && r.res.stage == EFS_BOOT && strstr(r.res.why, "FLASH_DATA") && m.blocks_ok == 4 && m.leave_calls == 1 && !m.armed);
    }
    {
        EspLoaderModel m; m.drop_data_seq = 2;                                         // no answer to a block: the time out is a failure, not a hang
        Run r(dir + "/v111.ue2", m);
        CHECK(!r.ok && r.res.started && strstr(r.res.why, "FLASH_DATA") && m.blocks_ok == 2);
    }
    // ---- the version afterwards
    {
        EspLoaderModel m; m.force_major = 1; m.force_minor = 8;                          // the module reports V1.8 after V1.11 was written
        Run r(dir + "/v111.ue2", m);
        CHECK(!r.ok && !r.res.ok && r.res.started && r.res.ident == EFI_DIFFERS && r.res.major == 1 && r.res.minor == 8 && strstr(r.res.why, "another version"));
    }
    {
        EspLoaderModel m; m.silent_identify = true;
        Run r(dir + "/v111.ue2", m);
        CHECK(r.ok && r.res.ok && r.res.ident == EFI_NO_ANSWER && strstr(r.res.why, "did not report"));
    }
    {
        EspLoaderModel m;                                                               // a set whose application carries no version: nothing to compare, nothing asked
        Run r(dir + "/noident.ue2", m);
        CHECK(r.insp == ESP_FOUND && r.ok && r.res.ident == EFI_NOT_ASKED && m.identify_calls == 0 && m.leave_calls == 1);
    }
    // ---- what is refused before anything is armed or sent
    {
        const char *files[] = { "app_only.ue2", "no_ptable.ue2", "damaged.ue2", "plain.cfw", "empty.cfw" };
        for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            EspLoaderModel m;
            Run r(dir + "/" + files[i], m);
            if (r.ok || m.arm_on || m.enter_calls) printf("  %s: ok %d arm_on %d enter %d\n", files[i], r.ok, m.arm_on, m.enter_calls);
            CHECK(r.insp != ESP_FOUND && !r.ok && !r.res.started && r.res.why[0] && m.arm_on == 0 && m.arm_off == 0 && m.enter_calls == 0 && m.leave_calls == 0 && flash_untouched(m));
        }
        EspLoaderModel m;                                                               // an info whose image reaches beyond the file
        Run r(dir + "/v111.ue2", m, false);
        EspInfo bad = r.info; bad.app_len = r.src.size();
        CHECK(!esp_flash(m, r.src, bad, 0, 0, &r.res) && m.arm_on == 0 && m.enter_calls == 0 && !r.res.started);
    }
    // ---- the file is only read (no write call exists in CorSource); a read error stops the write and says so
    {
        struct Bad : FileSrc { uint32_t from; Bad(const std::string &p, uint32_t f) : FileSrc(p), from(f) { }
            bool read(uint32_t off, void *buf, uint32_t len) { return off + len <= from && FileSrc::read(off, buf, len); } };
        EspLoaderModel m0;
        Run r0(dir + "/v111.ue2", m0, false);
        Bad b(dir + "/v111.ue2", r0.info.app_off + 2048);
        EspLoaderModel m;
        EspFlashResult res;
        CHECK(!esp_flash(m, b, r0.info, 0, 0, &res) && res.started && res.stage == EFS_APP && strstr(res.why, "could not be read") && m.leave_calls == 1 && !m.armed);
    }
    printf(fails ? "test_espflash: %d failed\n" : "test_espflash: 0 failed\n", fails);
    return fails ? 1 : 0;
}
