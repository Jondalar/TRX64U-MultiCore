// Host test of the ESP32 inspection (host-app/software/hostapp/espinspect.cc; T152 D8, S9): vendor-style files with an ESP32 image set (V1.8, V1.11, edge
// cases), a file without ESP32 part as the control, and - when they are on this machine - the real vendor files.
//   test_espinspect <dir of mkesptest.py> [real file expected-version]...
#include "espinspect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
typedef std::vector<uint8_t> Bytes;

struct FileSrc : CorSource {
    FILE *f;
    uint32_t sz;
    long reads;
    explicit FileSrc(const std::string &p) : f(fopen(p.c_str(), "rb")), sz(0), reads(0)
    {
        if (f) { fseek(f, 0, SEEK_END); sz = (uint32_t)ftell(f); }
    }
    ~FileSrc() { if (f) fclose(f); }
    uint32_t size() { return sz; }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        reads++;
        if (!f || (uint64_t)off + len > sz) return false;
        fseek(f, off, SEEK_SET);
        return fread(buf, 1, len, f) == len;
    }
};

static EspInfo inspect(const std::string &path, EspResult *r)
{
    FileSrc s(path);
    EspInfo e;
    CHECK(s.f != 0);
    *r = esp_inspect(s, &e, 0, 0, 0);
    return e;
}

static void expect(const std::string &dir, const char *name, EspResult want, const char *ver)
{
    EspResult r;
    EspInfo e = inspect(dir + "/" + name, &r);
    if (r != want || strcmp(e.version, ver)) {
        printf("  %s: result %d (want %d), version '%s' (want '%s')\n", name, (int)r, (int)want, e.version, ver);
    }
    CHECK(r == want);
    CHECK(!strcmp(e.version, ver));
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_espinspect <dir> [file version]...\n"); return 2; }
    std::string dir = argv[1];
    // S9: the two versions, and a set that is really a set
    expect(dir, "v18.ue2", ESP_FOUND, "V1.8");
    expect(dir, "v111.ue2", ESP_FOUND, "V1.11");
    EspResult r;
    EspInfo e = inspect(dir + "/v111.ue2", &r);
    CHECK(e.boot && e.ptable && e.app && e.digest_ok && !strcmp(e.project, "u64ctrl"));
    CHECK(e.boot_off < e.ptable_off && e.ptable_off < e.app_off);
    CHECK(e.boot_len > 32 && e.boot_off + e.boot_len <= e.ptable_off && e.ptable_len == 0xC00u && e.ptable_off + e.ptable_len <= e.app_off);       // T152 S8: what is flashed
    // the edges: a header over the scan block boundary, a version over the hash block boundary, a set without IDENT_STRING
    expect(dir, "boundary.ue2", ESP_FOUND, "V1.14");
    expect(dir, "ident_split.ue2", ESP_FOUND, "V1.11");
    expect(dir, "ident_split_digits.ue2", ESP_FOUND, "V1.11");
    expect(dir, "noident.ue2", ESP_FOUND, "");
    // not a set
    expect(dir, "app_only.ue2", ESP_INCOMPLETE, "V1.8");
    expect(dir, "no_ptable.ue2", ESP_INCOMPLETE, "V1.8");
    expect(dir, "damaged.ue2", ESP_DAMAGED, "V1.8");
    // the control: files without an ESP32 part say so
    expect(dir, "plain.cfw", ESP_NONE, "");
    expect(dir, "lookalike.cfw", ESP_NONE, "");
    expect(dir, "empty.cfw", ESP_NONE, "");
    expect(dir, "other_chip.cfw", ESP_NONE, "");
    // a file that does not exist / cannot be read
    {
        FileSrc s(dir + "/nothing.ue2");
        EspInfo x;
        CHECK(esp_inspect(s, &x, 0, 0, 0) == ESP_NONE);                                 // size 0: nothing in it
    }
    // the abort is polled
    {
        FileSrc s(dir + "/v111.ue2");
        EspInfo x;
        bool (*ab)(void *) = [](void *) { return true; };
        CHECK(esp_inspect(s, &x, 0, 0, ab) == ESP_ABORTED);
    }
    // the file is only read, never written: no write call exists in the interface; the scan stops at the application (the rest of the file is not read)
    {
        FileSrc s(dir + "/v111.ue2");
        EspInfo x;
        CHECK(esp_inspect(s, &x, 0, 0, 0) == ESP_FOUND);
        CHECK(x.app_off + x.app_len <= s.size());
    }
    // the real vendor files, when this machine has them (never committed)
    for (int i = 2; i + 1 < argc; i += 2) {
        EspResult rr;
        EspInfo re = inspect(argv[i], &rr);
        printf("real %s: result %d version '%s' app at %u\n", argv[i], (int)rr, re.version, (unsigned)re.app_off);
        CHECK(rr == ESP_FOUND);
        CHECK(!strcmp(re.version, argv[i + 1]));
        CHECK(re.boot && re.ptable && re.app && re.digest_ok);
    }
    printf("%s (%d failed)\n", fails ? "test_espinspect FAILED" : "test_espinspect ok", fails);
    return fails ? 1 : 0;
}
