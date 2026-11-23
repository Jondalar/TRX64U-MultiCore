// T104.1 5.5, Alex's decision of 2026-10-18 (the rescue from a crashing library): holding the Commodore key while a core starts loads no
// library, and a boot-attempt counter - set before the libraries load, cleared once the core has started - makes the start after one that
// never finished skip them. The counter is one key of the profile's boot.cfg, written only when its value changes. The file system is the
// board's flash guard over a map: a write the CoreFs would refuse is refused here too, so the paths the guard uses are the allowed ones.
#include <string>
#include "check.h"
#include "fakes.h"
#include "libguard.h"
#include "board.h"

struct FlashFs : FakeFs {
    int refused_writes = 0;
    FlashFs() { put_text("/flash/volume.mark", ""); }    // the flash disk is mounted: its root lists
    bool save(const char *path, const void *buf, uint32_t len)
    {
        if (fs_write_refused(path)) { refused_writes++; return false; }
        return FakeFs::save(path, buf, len);
    }
};

static const char *MONO = "/flash/trx64u/boot.cfg", *MC = FS_FLASH_BOOT_MULTICORE;

static bool has_attempt(FlashFs &fs, const char *path)
{
    return fs.text(path).find("lib_attempt=1") != std::string::npos;
}

int main()
{
    CHECK(fs_flash_set_exceptions(MONO, 0));                            // what board_set_core registers for the core "trx64u"
    CHECK_STR(MC, "/Flash/boot.cfg");
    for (const char *path : { MONO, MC }) {
        // ---- an undisturbed start: the counter is set before the libraries and cleared after the start; two writes, no more
        {
            FlashFs fs;
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            CHECK_EQ(p.skip, LIBSKIP_NONE); CHECK(!p.armed); CHECK_STR(p.why, ""); CHECK_EQ(fs.writes, 0);   // nothing written yet
            g.arm(&p);
            CHECK(p.armed); CHECK(has_attempt(fs, path)); CHECK_EQ(fs.writes, 1);        // set just before the first library loads
            g.arm(&p); CHECK_EQ(fs.writes, 1);                                            // once
            CHECK(g.finished());
            CHECK(!has_attempt(fs, path)); CHECK_EQ(fs.writes, 2);
            CHECK(g.finished()); CHECK_EQ(fs.writes, 2);                  // nothing to clear: no write
            CHECK_EQ(fs.refused_writes, 0);
        }
        // ---- a start that never finishes (the crash): the next start skips the libraries, says why, writes nothing to set; the one after loads again
        {
            FlashFs fs;
            { LibGuard g(&fs, path); LibGuardPlan p; g.begin(false, &p); CHECK_EQ(p.skip, LIBSKIP_NONE); g.arm(&p); }       // ... and the machine hangs here
            CHECK(has_attempt(fs, path)); int w = fs.writes;
            LibGuard g2(&fs, path);
            LibGuardPlan p2;
            g2.begin(false, &p2);
            CHECK_EQ(p2.skip, LIBSKIP_COUNTER); CHECK_EQ(fs.writes, w);
            CHECK(strstr(p2.why, "not loaded") && strstr(p2.why, "did not finish"));
            CHECK(g2.finished());
            CHECK(!has_attempt(fs, path)); CHECK_EQ(fs.writes, w + 1);
            LibGuard g3(&fs, path);
            LibGuardPlan p3;
            g3.begin(false, &p3);
            CHECK_EQ(p3.skip, LIBSKIP_NONE);                              // the libraries get another try
            g3.arm(&p3);
            CHECK(g3.finished());
        }
        // ---- the Commodore key: skipped, said so, no write when the counter was 0; with a counter left from a crash the start clears it
        {
            FlashFs fs;
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(true, &p);
            CHECK_EQ(p.skip, LIBSKIP_KEY); CHECK_EQ(fs.writes, 0);
            CHECK(strstr(p.why, "Commodore key") && strstr(p.why, "not loaded"));
            CHECK(g.finished()); CHECK_EQ(fs.writes, 0);
            FlashFs fs2;
            fs2.put_text(path, "countdown=5\nlib_attempt=1\n");
            LibGuard g2(&fs2, path);
            g2.begin(true, &p);
            CHECK_EQ(p.skip, LIBSKIP_KEY); CHECK_EQ(fs2.writes, 0);
            CHECK(g2.finished()); CHECK(!has_attempt(fs2, path)); CHECK_EQ(fs2.writes, 1);
        }
        // ---- the file is the host's boot menu file: what it holds survives both writes
        {
            FlashFs fs;
            const char *menu = "# TRX64 boot menu\ncountdown=7\nautostart_s=7\ndirect=1\ndefault=1\nvideo=1,4\ncore=/usb1/a.cor|Alpha|aa|1234|1700000000|0badf00d\ncore=/usb1/b.cor|Beta|bb|0|0|00000000\n";
            fs.put_text(path, menu);
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            g.arm(&p);
            CHECK(has_attempt(fs, path));
            std::string armed = fs.text(path);
            CHECK(armed.find("countdown=7") != std::string::npos && armed.find("default=1") != std::string::npos && armed.find("video=1,4") != std::string::npos);
            CHECK(armed.find("core=/usb1/a.cor|Alpha|aa|1234|1700000000|0badf00d") != std::string::npos && armed.find("core=/usb1/b.cor|Beta|bb|0|0|00000000") != std::string::npos);   // T101 S10: size, mtime and header digest ride along
            CHECK(g.finished());
            CHECK(fs.text(path) == menu);                        // byte for byte the file the host wrote
        }
        // ---- a file that is there and cannot be read is not overwritten (the host's cores are in it): the libraries load unguarded, and it is said
        {
            FlashFs fs;
            fs.put_text(path, "countdown=7\ncore=/usb1/a.cor|Alpha|aa|1234\n");
            std::string before = fs.text(path);
            fs.fail_loads = 1;
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            g.arm(&p);
            CHECK_EQ(p.skip, LIBSKIP_NONE); CHECK(!p.armed); CHECK(strstr(p.why, "could not read"));
            CHECK(fs.text(path) == before); CHECK_EQ(fs.writes, 0);
            CHECK(g.finished()); CHECK_EQ(fs.writes, 0);
        }
        // ---- a medium that does not take the write: unguarded, said, the start goes on
        {
            FlashFs fs;
            fs.read_only = true;
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            g.arm(&p);
            CHECK_EQ(p.skip, LIBSKIP_NONE); CHECK(!p.armed); CHECK(strstr(p.why, "could not write"));
        }
        // ---- a start that finds no library: begin() and finished() alone write no flash
        {
            FlashFs fs;
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            CHECK(g.finished()); CHECK_EQ(fs.writes, 0);
        }
        // ---- review 2 (2026-10-18): the flash disk is not mounted yet, so stat() fails; boot.cfg is NOT overwritten by a default
        {
            FakeFs bare;                                                   // no /flash volume at all: its root does not list
            LibGuard g(&bare, path);
            LibGuardPlan p;
            g.begin(false, &p);
            g.arm(&p);
            CHECK(!p.armed); CHECK(strstr(p.why, "could not read")); CHECK_EQ(bare.writes, 0);
            CHECK(g.finished()); CHECK_EQ(bare.writes, 0);
        }
        // ---- the file is listed in its folder but stat() fails (a transient FR_ error): not absent, not overwritten
        {
            struct StatFails : FlashFs { bool stat(const char *, uint64_t *, bool *) { return false; } } fs;
            fs.put_text(path, "countdown=7\ncore=/usb1/a.cor|Alpha|aa|1234\n");
            std::string before = fs.text(path);
            LibGuard g(&fs, path);
            LibGuardPlan p;
            g.begin(false, &p);
            g.arm(&p);
            CHECK(!p.armed); CHECK(fs.text(path) == before); CHECK_EQ(fs.writes, 0);
        }
    }
    // ---- the paths: the flash guard lets exactly these through, and nothing next to them
    {
        FlashFs fs;
        LibGuard g(&fs, "/flash/trx64u/boot.cfg.bak");
        LibGuardPlan p;
        g.begin(false, &p);
        g.arm(&p);
        CHECK(!p.armed); CHECK_EQ(fs.refused_writes, 1);
        LibGuard g2(&fs, "/flash/boot.cfg2");
        g2.begin(false, &p);
        g2.arm(&p);
        CHECK(!p.armed); CHECK_EQ(fs.refused_writes, 2);
    }
    DONE("test_libguard");
}
