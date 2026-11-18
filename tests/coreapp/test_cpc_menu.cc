// T92 #17 (D-17b): the F1 drive list of the CPC against the real descriptor: a mount into an EMPTY drive runs "Mount A: and reset" (the cold start
// clears every FDC lock), the replace of a FILLED drive runs the plain "Mount A:" (a multi-disk game keeps running). B: the same.
#include <string>
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "cpc_regs.h"
#include "menuctl.h"
#include "keyboard.h"
#include "ops_glue.h"

struct CpcIo : FakeIo {
    std::vector<uint8_t> bank[3];
    CpcIo() { for (auto &b : bank) b.assign(CPC_ROM_BYTES, 0); }
    void rom_write(uint32_t off, const void *p, uint32_t n) { int b = (regs[CPC_WIN_PAGE] >> 1) & 3; memcpy(&bank[b % 3].at(off), p, n); }
    void rom_read(uint32_t off, void *p, uint32_t n) { int b = (regs[CPC_WIN_PAGE] >> 1) & 3; memcpy(p, &bank[b % 3].at(off), n); }
    uint8_t rd(uint32_t reg) { if (reg == CPC_STATUS) return (uint8_t)((regs[CPC_MODEL] & 3) << 2); return FakeIo::rd(reg); }
};

struct Host : MenuHost {
    void info(int w, char *out, int n) { snprintf(out, n, "value %d", w); }
    bool return_to_host(char *, int) { return false; }
};

static std::vector<uint8_t> dsk(size_t n)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 31 + 7);
    const char *magic = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    memcpy(v.data(), magic, strlen(magic));
    return v;
}

struct Rig {
    CpcIo io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &cpc_desc; c.io = &io; c.fs = &fs;
        for (const char *n : { "os464.rom", "basic464.rom", "os664.rom", "basic664.rom", "os6128.rom", "basic6128.rom", "amsdos.rom" })
            fs.put(std::string("/usb0/roms/cpc/") + n, std::vector<uint8_t>(CPC_ROM_BYTES, 1));
        st = new Settings(cpc_desc.settings, cpc_desc.nsettings, cpc_desc.store, &fs, "/sd/cores/cpc.cfg", 0);
        st->load();
        vid = new Video(cpc_desc.video, &px, &io);
        c.settings = st; c.video = vid;
        MediumInfo mi; memset(&mi, 0, sizeof(mi)); mi.ready = true; mi.writable = true; fs.media["usb0"] = mi;
        fs.put("/usb0/g.dsk", dsk(40000));
        fs.put("/usb0/h.dsk", dsk(30000));
        m = new MenuCtl(&c, &host);
    }
    std::string left(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return l; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    void go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return; } while (m->selected() > r) m->key(KEY_UP); while (m->selected() < r) m->key(KEY_DOWN); }
    std::string foot;                                                       // the status line after the Library opened for the slot
    // F1, the drive's row, "<Mount|Replace> from Library...", the file on the stick; returns the number of cold starts it caused
    int from_list(const char *drive, const char *entry, const char *file)
    {
        m->key(KEY_F1);
        go_to(drive); m->key(KEY_RETURN);
        CHECK(find_row(entry) >= 0);
        go_to(entry); m->key(KEY_RETURN);
        foot = m->status();
        if (find_row("usb0/") >= 0) { go_to("usb0/"); m->key(KEY_RETURN); }      // the Library opens where it was left
        int asserts = io.reset_asserts;
        go_to(file); m->key(KEY_RETURN);
        return io.reset_asserts - asserts;
    }
};

int main()
{
    Rig r;
    struct U : BootUi { void message(const char *, const char *, bool) { } void clear() { } void tick() { } } ui;
    CHECK_EQ(boot_start(&r.c, &ui), BOOT_RUNNING);
    r.m->open();
    // empty A: and B: mount with the reset
    CHECK_EQ(r.from_list("A:", "Mount from Library...", "g.dsk"), 1);
    CHECK_STR(r.foot.c_str(), "Choose a file for A: - RETURN mounts it and resets");
    CHECK_STR(ops_state(&r.c, "cpc.a.image"), "g.dsk");
    CHECK_EQ(r.from_list("B:", "Mount from Library...", "g.dsk"), 1);
    CHECK_STR(r.foot.c_str(), "Choose a file for B: - RETURN mounts it and resets");
    CHECK_STR(ops_state(&r.c, "cpc.b.image"), "g.dsk");
    // filled: Replace swaps without a reset
    CHECK_EQ(r.from_list("A:", "Replace from Library...", "h.dsk"), 0);
    CHECK_STR(r.foot.c_str(), "Choose a file for A: - RETURN swaps the medium");
    CHECK_STR(ops_state(&r.c, "cpc.a.image"), "h.dsk");
    CHECK_EQ(r.from_list("B:", "Replace from Library...", "h.dsk"), 0);
    CHECK_STR(ops_state(&r.c, "cpc.b.image"), "h.dsk");
    // ejected again: the reset variant is back
    OpResult res;
    CHECK_EQ(ops_run_bound(&r.c, "cpc.eject", cpc_desc.media[0].eject_bind, cpc_desc.media[0].neject_bind, 0, &res), 0);
    CHECK_EQ(r.from_list("A:", "Mount from Library...", "g.dsk"), 1);
    CHECK_STR(r.foot.c_str(), "Choose a file for A: - RETURN mounts it and resets");
    DONE("test_cpc_menu");
}
