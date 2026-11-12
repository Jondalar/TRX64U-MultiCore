// T93 D10, S5, S6: the settings layers (descriptor default < flash < <name>.cfg), the one place that is written, and a medium
// that cannot be written.
#include <string>
#include "check.h"
#include "fakes.h"
#include "settings.h"
#include "cores.h"

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
    DONE("test_settings");
}
