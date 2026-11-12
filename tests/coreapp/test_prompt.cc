// T104.2 D6: the address prompt of "Load PRG and start from". A file action with `ask` shows one row - the prompt, the address in hex, a cursor -
// takes hex digits, BACK, RETURN and ESC, starts on the word the file itself holds, and hands the number to the operation through the
// binding. While it is up a digit is a digit, never a tab. The C64 descriptor and the C64 model are the real ones.
#include <string>
#include "check.h"
#include "c64_rig.h"
#include "menuctl.h"
#include "keyboard.h"

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, 0 };

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "x"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

static std::string row0(MenuCtl &m, std::string *sub = 0)
{
    char l[100], r[100], s[100];
    bool a;
    m.row(0, l, 100, r, 100, &a, s, 100);
    if (sub) *sub = s;
    return std::string(l) + "|" + r;
}

static std::vector<uint8_t> prg(uint16_t load, size_t n)
{
    std::vector<uint8_t> v(2 + n, 0xEA);
    v[0] = (uint8_t)load; v[1] = (uint8_t)(load >> 8);
    return v;
}

int main()
{
    C64Rig r;
    r.put_roms();
    CHECK(r.boot());
    r.c.started = true;
    r.fs.put("/usb0/x.prg", prg(0xC000, 64));
    std::vector<uint8_t> p00(0x1A, 0);
    std::vector<uint8_t> inner = prg(0x2000, 16);
    p00.insert(p00.end(), inner.begin(), inner.end());
    r.fs.put("/usb0/y.p00", p00);
    r.fs.put("/usb0/z.prg", prg(0x0000, 0x2000));
    Host host;
    MenuCtl m(&r.c, &host);
    m.open();
    m.key(KEY_F3);                                          // the browser: it opens on the media level (T104.2); the stick is /usb0
    { char l[100], r_[100]; bool a_; for (int i = 0; i < m.rows(); i++) { m.row(i, l, 100, r_, 100, &a_); if (!strcmp(l, "usb0/")) { while (m.selected() < i) m.key(KEY_DOWN); } } }
    m.key(KEY_RETURN);
    CHECK(m.rows() >= 3);

    auto select = [&](const char *name) {
        char l[100], rr[100]; bool a;
        for (int i = 0; i < m.rows(); i++) { m.row(i, l, 100, rr, 100, &a); if (!strcmp(l, name)) { while (m.selected() > i) m.key(KEY_UP); while (m.selected() < i) m.key(KEY_DOWN); return true; } }
        printf("FAIL no row %s\n", name); g_fails++;
        return false;
    };
    auto to_prompt = [&](const char *file) {
        select(file);
        CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);     // two actions: the chooser
        CHECK(m.chooser());
        m.key(KEY_DOWN);
        CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);     // "Load PRG and start from": the prompt, nothing run yet
        CHECK(m.entering());
    };

    // the chooser has both actions; Run PRG asks nothing
    select("x.prg");
    m.key(KEY_RETURN);
    CHECK(m.chooser());
    char l[100], rr[100]; bool act;
    m.row(0, l, 100, rr, 100, &act); CHECK_STR(l, "Run PRG");
    m.row(1, l, 100, rr, 100, &act); CHECK_STR(l, "Load PRG and start from");
    m.key(KEY_ESCAPE);
    CHECK(!m.chooser());

    // the prompt starts on the file's own load address
    to_prompt("x.prg");
    CHECK_EQ(m.rows(), 1);
    CHECK_EQ(m.selected(), 0);
    std::string sub;
    CHECK_STR(row0(m, &sub).c_str(), "Start address|$C000");
    CHECK(sub.find("RETURN") != std::string::npos);
    int area = m.area(), tab = m.tab();
    // digits are digits: the tab strip does not see them. The first one replaces the default.
    CHECK_EQ(m.key('1'), MenuCtl::R_REDRAW);
    CHECK_STR(row0(m).c_str(), "Start address|$1_");
    CHECK_EQ(m.area(), area); CHECK_EQ(m.tab(), tab);
    m.key('0'); m.key('0'); m.key('0');
    CHECK_STR(row0(m).c_str(), "Start address|$1000");
    m.key('F');                                              // a fifth digit has no room
    CHECK_STR(row0(m).c_str(), "Start address|$1000");
    m.key(KEY_BACK);
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('g'); m.key(' '); m.key(KEY_UP);                  // not hex: swallowed
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('c'); m.key('A');                                  // both cases, then the room is gone
    CHECK_STR(row0(m).c_str(), "Start address|$100C");
    m.key(KEY_BACK); m.key(KEY_BACK); m.key('0');
    CHECK_STR(row0(m).c_str(), "Start address|$100_");
    m.key('0');
    // nothing has run yet
    CHECK(r.io.cmds.empty());
    uint32_t releases = (uint32_t)r.io.release_t.size();
    // RETURN takes it: the operation gets $1000
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK(!m.entering());
    CHECK_EQ(r.io.exec_pc, 0x1000);
    CHECK_EQ(r.io.ram[0xC000], 0xEA);
    CHECK(r.io.release_t.size() == releases + 1);            // the machine was reset for it
    CHECK(strstr(m.status(), "Load PRG and start from") != 0);

    // ESC cancels: nothing happens
    to_prompt("x.prg");
    size_t cmds = r.io.cmds.size();
    uint16_t pc = r.io.exec_pc;
    m.key('4'); m.key('2');
    CHECK_EQ(m.key(KEY_ESCAPE), MenuCtl::R_REDRAW);
    CHECK(!m.entering());
    CHECK(m.chooser());                                      // back where it came from
    m.key(KEY_ESCAPE);
    CHECK(!m.chooser());
    CHECK_EQ(r.io.cmds.size(), cmds);
    CHECK_EQ(r.io.exec_pc, pc);

    // RETURN without a digit takes the default; so does a field typed empty again
    to_prompt("x.prg");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK_EQ(r.io.exec_pc, 0xC000);
    to_prompt("x.prg");
    m.key('7'); m.key(KEY_BACK);
    CHECK_STR(row0(m).c_str(), "Start address|$_");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK_EQ(r.io.exec_pc, 0xC000);

    // a .P00: the default is the word behind its header
    to_prompt("y.p00");
    CHECK_STR(row0(m).c_str(), "Start address|$2000");
    m.key(KEY_ESCAPE);
    m.key(KEY_ESCAPE);

    // a function key leaves the prompt and goes where it says; the closing key closes
    to_prompt("x.prg");
    m.key('1');
    CHECK_EQ(m.key(KEY_F1), MenuCtl::R_REDRAW);
    CHECK(!m.entering());
    CHECK_EQ(m.area(), 0);
    m.key(KEY_F3);
    to_prompt("x.prg");
    CHECK_EQ(m.key(KEY_MENU), MenuCtl::R_CLOSE);
    CHECK(!m.entering());
    m.key(KEY_F3);

    // Run PRG from the chooser runs at once
    select("x.prg");
    m.key(KEY_RETURN);
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK(!m.entering());
    CHECK(std::string((const char *)&r.io.ram[0x0277], r.io.ram[0xC6]) == "SYS49152\r");

    // a file over the zero page started at an address of its own
    to_prompt("z.prg");
    m.key('2'); m.key('0'); m.key('0'); m.key('0');
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK_EQ(r.io.exec_pc, 0x2000);

    // a start that fails later (the machine does not stop on an instruction boundary) is told on the screen of the next menu; the prompt
    // itself is long gone and the menu has closed
    r.io.pause_after_ms = -1;
    to_prompt("x.prg");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_CLOSE);
    r.menu_closed();
    CHECK(!m.entering());
    CHECK(r.c.notice_pending && strstr(r.c.notice, "instruction boundary") != 0);
    m.open();                                                // the next time the menu opens, it says so
    CHECK(m.status_bad());
    CHECK(strstr(m.status(), "instruction boundary") != 0);
    m.key(KEY_F3);
    // a file the machine cannot be started with at all is refused at the prompt's RETURN, the menu stays
    to_prompt("x.prg");
    r.fs.remove("/usb0/x.prg");
    CHECK_EQ(m.key(KEY_RETURN), MenuCtl::R_REDRAW);
    CHECK(m.status_bad());
    DONE("test_prompt");
}
