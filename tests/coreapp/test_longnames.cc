// T180 #38 (GitHub #38, docs/specs/done/T180-media-housekeeping.md): the pieces under the F3 Library and the media picks that a FAT long name (up to 255 characters) goes through.
// What the Library, the Amiga DF0 pick and the drive pick do with such a file is in test_f3.cc, test_media_mounts.cc and test_cbm1541.cc; here are the parts that have no screen:
//  - fs_short_name: a name for a place that only shows it keeps its start and its end and says that it was cut,
//  - the listing a caller with thousands of entries uses (CoreFs::list_short): 72 byte entries, the name cut at 55 characters and marked, the whole name kept by list(),
//  - the write-back of a medium whose file name is 255 characters: NAME.tmp and NAME.bak would be 259 characters, which no FAT holds, so the name part is cut.
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "writeback.h"

const CoreDesc *const core_table[] = { 0 };

struct Port : WbPort {
    uint32_t t = 0;
    uint32_t ticks_ms() { return t; }
    void enter() { }
    void leave() { }
    void irq_attach(uint8_t, uint8_t (*)(uint8_t)) { }
    void irq_enable(uint8_t, bool) { }
};

static std::string last_component(const std::string &p)
{
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    std::string n80, n255;
    for (int i = 0; i < 76; i++) n80 += (char)('a' + i % 26);
    n80 += ".adf";
    for (int i = 0; i < 251; i++) n255 += (char)('a' + (i * 7) % 26);
    n255 += ".sav";

    // ---- fs_short_name
    {
        char b[64];
        fs_short_name(b, sizeof(b), "short.adf");
        CHECK_STR(b, "short.adf");                                           // fits: unchanged
        std::string fits(63, 'x');
        fs_short_name(b, sizeof(b), fits.c_str());
        CHECK_STR(b, fits.c_str());                                          // exactly the room: unchanged
        fs_short_name(b, sizeof(b), n80.c_str());
        CHECK_EQ(strlen(b), 63u);
        CHECK(strstr(b, "..") != 0);
        CHECK_STR(b + strlen(b) - 6, n80.substr(n80.size() - 6).c_str());    // the end of the name, extension included
        CHECK(!strncmp(b, n80.c_str(), 55));                                 // the start
        fs_short_name(b, sizeof(b), n255.c_str());
        CHECK_EQ(strlen(b), 63u);
        CHECK(strstr(b, "..") != 0 && !strncmp(b, n255.c_str(), 55));
        char tiny[6];
        fs_short_name(tiny, sizeof(tiny), n80.c_str());
        CHECK_EQ(strlen(tiny), 5u);                                          // no room for "..": the start, cut
        CHECK(!strncmp(tiny, n80.c_str(), 5));
    }

    // ---- the listing: list() has the whole name, list_short() 55 characters and the mark
    {
        FakeFs fs;
        fs.put(("/usb0/d/" + n80).c_str(), std::vector<uint8_t>(10, 1));
        fs.put(("/usb0/d/" + n255).c_str(), std::vector<uint8_t>(20, 2));
        fs.put("/usb0/d/short.adf", std::vector<uint8_t>(30, 3));
        FsEntry e[8];
        int n = fs.list("/usb0/d", e, 8);
        CHECK_EQ(n, 3);
        bool got80 = false, got255 = false;
        for (int i = 0; i < n; i++) {
            got80 |= n80 == e[i].name;
            got255 |= n255 == e[i].name;
        }
        CHECK(got80 && got255);                                              // the whole names
        FsEntryShort s[8];
        n = fs.list_short("/usb0/d", s, 8);
        CHECK_EQ(n, 3);
        int cut = 0;
        for (int i = 0; i < n; i++) {
            if (!strcmp(s[i].name, "short.adf")) {
                CHECK(!s[i].cut && s[i].size == 30);
            } else {
                cut += s[i].cut;
                CHECK_EQ(strlen(s[i].name), 55u);
                CHECK(!strncmp(s[i].name, n80.c_str(), 55) || !strncmp(s[i].name, n255.c_str(), 55));
            }
        }
        CHECK_EQ(cut, 2);
        CHECK_EQ(sizeof(FsEntryShort), 72u);
        CHECK_EQ(fs.list_short("/usb0/nowhere", s, 8), -1);
    }

    // ---- the write-back of a medium with a 255 character file name: .tmp and .bak stay FAT names, the old file is kept once, nothing is left behind
    for (int pass = 0; pass < 2; pass++) {
        const std::string &nm = pass ? n255 : n80;
        FakeFs fs;
        Port port;
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.fs = &fs;
        wb_attach(&c, &port);
        wb_unregister();
        wb_set_mode(WB_AUTO);
        std::vector<uint8_t> ram(2048, 0);
        WbRaw raw;
        raw.read = [](void *u, uint32_t off, void *buf, uint32_t n) { memcpy(buf, (uint8_t *)u + off, n); return 0; };
        raw.user = ram.data();
        raw.size = (uint32_t)ram.size();
        std::string path = "/usb0/saves/" + nm;
        fs.put(path.c_str(), std::vector<uint8_t>(2048, 0xEE));              // the file as the user has it
        WbMedium w;
        memset(&w, 0, sizeof(w));
        snprintf(w.path, sizeof(w.path), "%s", path.c_str());
        snprintf(w.name, sizeof(w.name), "game");
        w.write = wb_raw_write;
        w.user = &raw;
        w.may_create = true;
        wb_register(w);
        for (int round = 0; round < 2; round++) {                            // twice: the .bak is taken once, the second write replaces the file only
            ram[5] = (uint8_t)(round + 1);
            wb_mark(5, 1);
            port.t += 10;
            char msg[200] = "";
            CHECK_EQ(wb_flush(&c, WB_WHY_MENU, msg, sizeof(msg)), 0);
            CHECK(!wb_pending());
            CHECK_EQ(fs.files[lower(path)].size(), 2048u);
            CHECK_EQ(fs.files[lower(path)][5], round + 1);
        }
        int files = 0, bak = 0;
        for (auto &kv : fs.files) {
            std::string spelled = fs.names.count(kv.first) ? fs.names[kv.first] : kv.first;
            CHECK(last_component(spelled).size() <= 255u);                   // no name a FAT cannot hold
            files++;
            bak += spelled.find(".bak") != std::string::npos;
            CHECK(spelled.find(".tmp") == std::string::npos);                // the temp file became the file
        }
        CHECK_EQ(files, 2);
        CHECK_EQ(bak, 1);                                                    // the file as the user had it, once
        for (auto &kv : fs.files) {
            if (kv.first.find(".bak") != std::string::npos) {
                CHECK_EQ(kv.second[5], 0xEE);                                // ... with its old content
            }
        }
        wb_unregister();
    }
    DONE("test_longnames");
}
