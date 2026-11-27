// T93 D10, S5, S6: the settings layers (descriptor default < flash < <name>.cfg), the one place that is written, and a medium
// that cannot be written.
#include <string>
#include <ctype.h>
#include "check.h"
#include "fakes.h"
#include "settings.h"
#include "cores.h"
extern const CoreDesc atari800_desc;          // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)

// The flash as the application sees it: a byte image that only get() reads. There is no write method to call - the test
// proves the image is the same after a whole session (S5, "read back and hashed" on the board).
struct FakeFlash : FlashLayer {
    std::map<std::string, std::string> kv;
    std::vector<uint8_t> image;
    int gets = 0;
    FakeFlash() : image(4096) { for (size_t i = 0; i < image.size(); i++) image[i] = (uint8_t)(i * 31 + 7); }
    bool get(const char *store, const char *key, char *val, int n)
    {
        gets++;
        auto it = kv.find(std::string(store) + "/" + key);
        if (it == kv.end()) return false;
        strncpy(val, it->second.c_str(), n - 1);
        val[n - 1] = 0;
        return true;
    }
    uint32_t hash() const { uint32_t h = 2166136261u; for (auto b : image) h = (h ^ b) * 16777619u; return h; }
};

#define CHECK_TXT(s, b) do { std::string _s = (s); CHECK_STR(_s.c_str(), b); } while (0)
#define CFG "/usb0/cores/atari800.cfg"
#define STORE "Atari 800 XL Settings"

int main()
{
    const Setting *it = atari800_desc.settings;
    int n = atari800_desc.nsettings;
    FakeFs fs;
    FakeFlash flash;
    flash.kv[STORE "/Video standard"] = "NTSC";
    flash.kv[STORE "/CPU speed"] = "2x";
    uint32_t h0 = flash.hash();

    Settings s(it, n, STORE, &fs, CFG, &flash);
    s.load();
    int ram = s.find("RAM size"), vid = s.find("Video standard"), joy = s.find("Joystick ports"), spd = s.find("CPU speed");
    CHECK(ram >= 0 && vid >= 0 && joy >= 0 && spd >= 0);
    // no .cfg: the flash's values where it has them, the descriptor's default elsewhere
    CHECK_EQ(s.value(vid), 1);                       // NTSC from the flash
    CHECK_EQ(s.layer(vid), Settings::L_HOST);
    CHECK_EQ(s.value(spd), 1);                       // 2x
    CHECK_EQ(s.value(ram), 0);
    CHECK_EQ(s.layer(ram), Settings::L_DEFAULT);
    CHECK(!fs.has(CFG));

    // S5: a change goes into the .cfg and nowhere else; only the changed item is written, so a later change of the flash
    // still shows through for the others
    CHECK(s.set(ram, 5));
    CHECK_TXT(fs.text(CFG), "[" STORE "]\nRAM size=576 KB Rambo\n\n");
    CHECK(s.set(joy, 1));
    CHECK_TXT(fs.text(CFG), "[" STORE "]\nRAM size=576 KB Rambo\nJoystick ports=Swapped\n\n");
    CHECK_EQ(s.value(ram), 5);
    CHECK_EQ(s.layer(ram), Settings::L_CORE);
    CHECK_EQ(s.value(vid), 1);                       // the flash's NTSC is still what runs
    CHECK_STR(s.take_notice(), "");
    // a fresh start reads it back
    {
        Settings t(it, n, STORE, &fs, CFG, &flash);
        t.load();
        CHECK_EQ(t.value(ram), 5);
        CHECK_EQ(t.value(joy), 1);
        CHECK_EQ(t.value(vid), 1);
        CHECK_EQ(t.value(spd), 1);
        CHECK_EQ(t.layer(joy), Settings::L_CORE);
        CHECK_EQ(t.layer(spd), Settings::L_HOST);
    }
    CHECK_EQ(flash.hash(), h0);                      // the flash is byte-identical after the whole session
    CHECK(flash.gets > 0);

    // "Reset to host defaults": the .cfg goes, the flash's values and the defaults come back
    CHECK(s.reset_to_host());
    CHECK(!fs.has(CFG));
    CHECK_EQ(s.value(ram), 0);
    CHECK_EQ(s.value(joy), 0);
    CHECK_EQ(s.value(vid), 1);
    CHECK_EQ(s.layer(vid), Settings::L_HOST);

    // S6: the card is write protected. The change holds for the session, the screen says so once, the flash value is not lost
    fs.read_only = true;
    CHECK(!s.set(spd, 3));
    CHECK_EQ(s.value(spd), 3);                       // 8x, in memory
    CHECK_EQ(s.value(vid), 1);                       // the flash default of another item is untouched
    const char *notice = s.take_notice();
    CHECK(notice[0] != 0 && strstr(notice, "cannot be saved") != 0);
    CHECK_STR(s.take_notice(), "");                  // once
    CHECK(!s.set(joy, 1));
    CHECK_STR(s.take_notice(), "");                  // still once while it keeps failing
    CHECK_EQ(s.value(joy), 1);
    CHECK(!fs.has(CFG));
    // the card is writable again: the next change saves everything the session set, including the earlier failed one
    fs.read_only = false;
    CHECK(s.set(ram, 2));
    CHECK_TXT(fs.text(CFG), "[" STORE "]\nRAM size=320 KB Compy Shop\nJoystick ports=Swapped\nCPU speed=8x\n\n");
    CHECK_EQ(flash.hash(), h0);

    // the stick is pulled: reads and writes fail, the session goes on from memory
    fs.gone = true;
    CHECK(!s.set(ram, 1));
    CHECK_EQ(s.value(ram), 1);
    CHECK(s.take_notice()[0] != 0);

    // a medium that cannot delete the .cfg: reset_to_host empties it instead; when nothing works it says so
    FakeFs fs2;
    fs2.put_text(CFG, "[" STORE "]\nRAM size=1088 KB Rambo\n\n");
    Settings u(it, n, STORE, &fs2, CFG, &flash);
    u.load();
    CHECK_EQ(u.value(ram), 6);
    fs2.read_only = true;
    CHECK(!u.reset_to_host());
    CHECK_EQ(u.value(ram), 0);                       // the session is back at the defaults
    CHECK(u.take_notice()[0] != 0);

    // no bundle path (JTAG / developer start): settings work in memory, nothing is written, the screen says why
    FakeFs fs3;
    Settings v(it, n, STORE, &fs3, "", &flash);
    v.load();
    CHECK(!v.has_path());
    CHECK(!v.set(ram, 4));
    CHECK_EQ(v.value(ram), 4);
    CHECK(strstr(v.take_notice(), "no bundle path") != 0);
    CHECK_EQ(fs3.writes, 0);

    // the reader: CRLF, comments, a foreign section first, an unknown key, padded and mixed-case labels, a bad label
    FakeFs fs4;
    fs4.put_text(CFG, "# a comment\r\n[Other Machine]\r\nRAM size=1088 KB Rambo\r\n\r\n[" STORE "]\r\n; another\r\nvideo standard=ntsc\r\n"
                      "Joystick ports =  Swapped \r\nCPU speed=3x\r\nNo Such Item=1\r\nRAM size=banana\r\n\r\n");
    Settings w(it, n, STORE, &fs4, CFG, 0);
    w.load();
    CHECK_EQ(w.value(ram), 0);                       // the foreign section's value, a bad label: ignored
    CHECK_EQ(w.value(spd), 0);                       // 3x is not a label of this item
    CHECK_EQ(w.value(joy), 1);                       // "Joystick ports =  Swapped " : spaces around key and value do not matter
    CHECK_EQ(w.value(vid), 1);                       // "video standard=ntsc": item names and labels are case-insensitive, as Gideon's
    FakeFs fs5;
    fs5.put_text(CFG, "[" STORE "]\nVideo standard=ntsc\nCPU speed= 4x \nJoystick ports=Swapped\n");
    Settings x(it, n, STORE, &fs5, CFG, 0);
    x.load();
    CHECK_EQ(x.value(vid), 1);                       // a label matches ignoring case and outer spaces (Gideon's reader does)
    CHECK_EQ(x.value(spd), 2);
    CHECK_EQ(x.value(joy), 1);                       // no trailing newline needed
    // T224 D2/D3: an override is in force for this run and is written nowhere: not by a later save of the .cfg, not by Save as config; a change of the person ends it for that item
    {
        FakeFs fs2;
        Settings o(it, n, STORE, &fs2, CFG, 0);
        o.load();
        CHECK(o.set(ram, 5));
        CHECK(o.set(joy, 1));
        CHECK_TXT(fs2.text(CFG), "[" STORE "]\nRAM size=576 KB Rambo\nJoystick ports=Swapped\n\n");
        o.override_value(ram, 0);
        o.override_value(spd, 3);                    // an item that is not in the file at all
        CHECK_EQ(o.value(ram), 0);
        CHECK_EQ(o.value(spd), 3);
        CHECK(o.set(joy, 0));                        // another item changes: the file is written again
        {
            std::string want = "[" STORE "]\nRAM size=576 KB Rambo\nJoystick ports=" + std::string(it[joy].values[0]) + "\n\n";
            CHECK_TXT(fs2.text(CFG), want.c_str());
        }
        CHECK(o.save());
        CHECK(fs2.text(CFG).find("RAM size=576 KB Rambo") != std::string::npos && fs2.text(CFG).find("CPU speed") == std::string::npos);
        int ns = 0;
        CHECK(o.export_config("/usb0/cores/atari800/configs/x.cfg", 0, 0, 0, &ns));
        CHECK(fs2.text("/usb0/cores/atari800/configs/x.cfg").find("RAM size=576 KB Rambo") != std::string::npos);
        CHECK(fs2.text("/usb0/cores/atari800/configs/x.cfg").find(std::string("CPU speed=") + it[spd].values[it[spd].def]) != std::string::npos);
        CHECK(o.set(ram, 2));                        // the person's own choice is stored and in force
        CHECK_EQ(o.value(ram), 2);
        CHECK(fs2.text(CFG).find(std::string("RAM size=") + it[ram].values[2]) != std::string::npos);
        Settings t(it, n, STORE, &fs2, CFG, 0);
        t.load();
        CHECK_EQ(t.value(spd), it[spd].def);         // the next start reads the stored values, the override is gone
    }

    // ---- find: the name-hash index (base 3.3). Every key is found at its own index in any case, a name that is none is -1, the first of two equal keys wins, a rebind keeps the answers,
    // and a lookup costs about one key comparison where the scan it replaced cost one per setting before the hit (the figures are printed: the linear count is the old algorithm's)
    {
        static char names[SETTINGS_MAX][20];
        static Setting many[SETTINGS_MAX];
        static const char *const two[] = { "A", "B" };
        for (int i = 0; i < SETTINGS_MAX; i++) {
            snprintf(names[i], sizeof(names[i]), i == 7 ? "Setting 5" : "Setting %d", i);      // the item 7 repeats the key of item 5
            many[i] = Setting{ names[i], two, 2, 0, WHEN_LIVE, 0, 0 };
        }
        FakeFs fsm;
        Settings m(many, SETTINGS_MAX, "Many", &fsm, "", 0);
        Settings::find_stats_reset();
        for (int i = 0; i < SETTINGS_MAX; i++) {
            int want = i == 7 ? 5 : i;
            CHECK_EQ(m.find(names[i]), want);
            char up[20];
            snprintf(up, sizeof(up), "%s", names[i]);
            for (char *c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
            CHECK_EQ(m.find(up), want);                                                       // the case does not matter
        }
        Settings::FindStats hit = Settings::find_stats();
        CHECK_EQ(hit.calls, 2u * SETTINGS_MAX);
        CHECK(hit.compares >= hit.calls && hit.compares <= hit.calls * 2);                    // about one comparison a call: the table holds a third of its slots
        CHECK(hit.linear > hit.compares * 20);                                                // the scan paid 170 comparisons a call on average
        Settings::find_stats_reset();
        CHECK_EQ(m.find("No such setting"), -1);
        CHECK_EQ(m.find("Setting 340"), -1);
        CHECK_EQ(m.find(""), -1);
        CHECK_EQ(m.find(0), -1);
        Settings::FindStats miss = Settings::find_stats();
        CHECK_EQ(miss.calls, 4u);
        CHECK(miss.compares <= 4u && miss.linear == 4u * SETTINGS_MAX);
        uint32_t g0 = m.generation();
        m.rebind("");
        CHECK(m.generation() != g0);
        CHECK_EQ(m.find("setting 339"), 339);
        CHECK_EQ(m.find("Setting 5"), 5);
        printf("find on %d settings, %d lookups: %u key comparisons (linear scan: %u), %.2f against %.1f a call; a miss: %u against %u\n", SETTINGS_MAX, (int)hit.calls, hit.compares, hit.linear,
               (double)hit.compares / hit.calls, (double)hit.linear / hit.calls, miss.compares / 4, miss.linear / 4);
        // a real descriptor, every key of it
        Settings a(it, n, STORE, &fs, CFG, 0);
        for (int i = 0; i < n; i++) CHECK_EQ(a.find(it[i].key), i);
    }
    DONE("test_settings");
}
