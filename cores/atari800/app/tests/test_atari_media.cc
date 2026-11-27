// T93 D6: the Atari descriptor's media hooks - cartridges (type table, image into the DDR2 window, CART1 while held), D1:-D4:
// mount and eject, .xex as D1:, and the service that answers SIO from the descriptor's own state.
#include "check.h"
#include "sio_model.h"
#include "boot.h"
#include "cores.h"
#include "atari_cart.h"
#include "atari_media.h"
#include "engines.h"
#include "ops_glue.h"
extern const CoreDesc atari800_desc;          // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)

static std::vector<uint8_t> car(uint32_t type, uint32_t kb, bool good_sum = true)
{
    std::vector<uint8_t> d(kb * 1024);
    uint32_t sum = 0;
    for (size_t i = 0; i < d.size(); i++) { d[i] = (uint8_t)(i * 3 + 1); sum += d[i]; }
    std::vector<uint8_t> v = { 'C', 'A', 'R', 'T', (uint8_t)(type >> 24), (uint8_t)(type >> 16), (uint8_t)(type >> 8), (uint8_t)type,
                               (uint8_t)(sum >> 24), (uint8_t)(sum >> 16), (uint8_t)(sum >> 8), (uint8_t)(sum + (good_sum ? 0 : 1)), 0, 0, 0, 0 };
    v.insert(v.end(), d.begin(), d.end());
    return v;
}

static void head16(const std::vector<uint8_t> &v, uint8_t h[16]) { memset(h, 0, 16); memcpy(h, v.data(), v.size() < 16 ? v.size() : 16); }

int main()
{
    // --- the type table and raw images
    uint8_t h[16];
    memset(h, 0, 16);
    CHECK(cart_probe(h, 8192).ok && cart_probe(h, 8192).mode == 0x01);
    CHECK(cart_probe(h, 16384).ok && cart_probe(h, 16384).mode == 0x21);
    CHECK(cart_probe(h, 2048).mode == 0x16 && cart_probe(h, 4096).mode == 0x17);
    CartInfo ci = cart_probe(h, 12288);
    CHECK(!ci.ok && strstr(ci.err, "12288") != 0);
    CHECK(!cart_probe(h, 32768).ok);                                  // a raw 32 KB image is not one type
    auto v = car(2, 16); head16(v, h);
    ci = cart_probe(h, (uint32_t)v.size());
    CHECK(ci.ok && ci.mode == 0x21 && ci.size == 16384 && ci.data_off == 16 && ci.car_type == 2);
    v = car(42, 1024); head16(v, h);
    ci = cart_probe(h, (uint32_t)v.size());
    CHECK(ci.ok && ci.mode == 0x03 && ci.size == 1024 * 1024);
    v = car(41, 128); head16(v, h);
    CHECK(cart_probe(h, (uint32_t)v.size()).mode == 0x02);            // AtariMax 128K = cart_mode_atarimax1
    v = car(2, 8); head16(v, h);                                      // type 2 is 16 KB; the file holds 8
    ci = cart_probe(h, (uint32_t)v.size());
    CHECK(!ci.ok && strstr(ci.err, "16 KB") != 0 && strstr(ci.err, "8192") != 0);
    v = car(999, 8); head16(v, h);
    ci = cart_probe(h, (uint32_t)v.size());
    CHECK(!ci.ok && strstr(ci.err, "999") != 0);
    v = car(4, 32); head16(v, h);                                     // 4 = a 5200 type: not an XL cartridge
    CHECK(!cart_probe(h, (uint32_t)v.size()).ok);

    // --- hooks against the descriptor
    SioModel io; FakeFs fs; PxRec px;
    CoreCtx c; memset(&c, 0, sizeof(c));
    c.desc = &atari800_desc; c.io = &io; c.fs = &fs;
    Settings st(atari800_desc.settings, atari800_desc.nsettings, atari800_desc.store, &fs, "", 0);
    c.settings = &st;
    Video vid(atari800_desc.video, &px, &io);
    c.video = &vid;
    char msg[300];

    // a .car goes into the window, CART1 is written while the core is held, then a cold start
    fs.put("/usb0/g.car", car(1, 8));
    size_t n0 = io.log.size();
    io.events.clear();
    CHECK_EQ(a8_insert_cart(&c, "/usb0/g.car", 0, msg, sizeof(msg)), 0);
    bool same = true;
    auto data = car(1, 8);
    for (int i = 0; i < 8192; i++) same &= io.ddr[A8_W_CART + i] == data[16 + i];
    CHECK(same);
    CHECK_EQ(io.regs[A8_CART1], 0x01);
    int cart_at = -1, last_ctrl = -1;
    for (size_t i = n0; i < io.log.size(); i++) {
        if (io.log[i].reg == A8_CART1 && io.log[i].v == 1) cart_at = (int)i;
        if (io.log[i].reg == A8_CTRL) last_ctrl = (int)i;
    }
    CHECK(cart_at > 0 && cart_at < last_ctrl);                        // before the release
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    CHECK_STR(io.events.back().c_str(), "mode_unreset");
    CHECK(!c.notice_pending);
    CHECK_STR(a8_state(&c)->cart_name, "g.car");

    // the checksum in the header does not match: loaded anyway, the user is told
    fs.put("/usb0/b.car", car(2, 16, false));
    CHECK_EQ(a8_insert_cart(&c, "/usb0/b.car", 0, msg, sizeof(msg)), 0);
    CHECK(c.notice_pending && strstr(c.notice, "checksum") != 0);
    CHECK_EQ(io.regs[A8_CART1], 0x21);
    // the biggest one: 4 MB at the top of the cartridge region
    fs.put("/usb0/big.car", car(63, 4096));
    CHECK_EQ(a8_insert_cart(&c, "/usb0/big.car", 0, msg, sizeof(msg)), 0);
    CHECK_EQ(io.regs[A8_CART1], 0x20);
    CHECK_EQ(io.ddr[A8_W_CART + 0x3FFFFF], (uint8_t)(0x3FFFFF * 3 + 1));
    // refusals leave the machine alone (no hold)
    io.events.clear();
    fs.put("/usb0/odd.rom", std::vector<uint8_t>(12288, 1));
    CHECK_EQ(a8_insert_cart(&c, "/usb0/odd.rom", 0, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "12288") != 0);
    CHECK_EQ(a8_insert_cart(&c, "/usb0/none.car", 0, msg, sizeof(msg)), 1);
    CHECK(io.events.empty());
    CHECK_EQ(io.regs[A8_CART1], 0x20);                                // still the 4 MB one
    // remove: type 0
    CHECK_EQ(a8_remove_cart(&c, msg, sizeof(msg)), 0);
    CHECK_EQ(io.regs[A8_CART1], 0);
    CHECK(a8_state(&c)->cart_name[0] == 0);

    // --- mount, eject, and the service answering from that state
    std::vector<uint8_t> atr(16, 0);
    atr[0] = 0x96; atr[1] = 2; atr[2] = 0x80; atr[3] = 0x02;          // 0x280 paragraphs = 10240 bytes = 80 sectors
    atr[4] = 128;
    for (int n = 1; n <= 80; n++) atr.insert(atr.end(), 128, (uint8_t)n);
    fs.put("/usb0/d.atr", atr);
    // T108 D2: D2: is a mount and nothing else - no reset, the machine keeps running
    size_t ev_before = io.events.size();
    CHECK_EQ(a8_mount(&c, "/usb0/d.atr", 1, false, msg, sizeof(msg)), 0);
    CHECK(a8_state(&c)->disk[1] != 0 && a8_state(&c)->disk[0] == 0);
    CHECK_EQ(io.events.size(), ev_before);
    CHECK_STR(ops_state(&c, atari800_desc.media[1].state), "d.atr");
    CHECK(ops_state(&c, atari800_desc.media[0].state) == 0);
    io.command(0x32, 0x52, 9, 0);
    a8_service(&c);
    auto w = io.take();
    CHECK(w.size() == 131 && w[0] == 'A' && w[1] == 'C' && w[2] == 9);
    CHECK_EQ(a8_mount(&c, "/usb0/d.atr", 7, false, msg, sizeof(msg)), 1);   // no such drive
    fs.put("/usb0/junk.atr", std::vector<uint8_t>(50, 0));
    CHECK_EQ(a8_mount(&c, "/usb0/junk.atr", 2, false, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "not an ATR") != 0);
    CHECK(a8_state(&c)->disk[2] == 0);
    CHECK_EQ(a8_eject(&c, 1, msg, sizeof(msg)), 0);
    io.command(0x32, 0x53, 0, 0);
    a8_service(&c);
    CHECK_EQ(io.take().size(), 0);                                    // ejected: silent again
    // ... while D1: is the drive the machine boots from: the disk goes in and the machine cold starts
    // (T108 D6: "Mount D1:" swaps without a reset, "Mount D1: and cold start" is the boot; the file actions bind the flag)
    io.events.clear();
    CHECK_EQ(a8_mount(&c, "/usb0/d.atr", 0, true, msg, sizeof(msg)), 0);
    CHECK(a8_state(&c)->disk[0] != 0);
    CHECK(io.events.size() >= 2 && io.events.front() == "mode_reset" && io.events.back() == "mode_unreset");
    io.events.clear();
    CHECK_EQ(a8_mount(&c, "/usb0/d.atr", 0, false, msg, sizeof(msg)), 0);   // the swap: the machine keeps running
    CHECK(a8_state(&c)->disk[0] != 0);
    CHECK_EQ(io.events.size(), 0);
    {   // the descriptor binds it so: the first action boots, the second one swaps
        const FileAction &boot = atari800_desc.filetypes[0].actions[0], &swap = atari800_desc.filetypes[0].actions[1];
        CHECK_STR(boot.label, "Mount D1: and cold start");
        CHECK_STR(swap.label, "Mount D1:");
        OpResult r;
        io.events.clear();
        CHECK_EQ(ops_run_bound(&c, swap.op, swap.bind, swap.nbind, "/usb0/d.atr", &r), 0);
        CHECK_EQ(io.events.size(), 0);
        CHECK_EQ(ops_run_bound(&c, boot.op, boot.bind, boot.nbind, "/usb0/d.atr", &r), 0);
        CHECK(io.events.size() >= 2 && io.events.front() == "mode_reset");
    }
    // a write failure during service reaches the notice line
    fs.read_only = true;
    c.notice_pending = false;
    io.command(0x31, 0x57, 3, 0);
    a8_service(&c);
    io.take();
    io.data(std::vector<uint8_t>(128, 9));
    a8_service(&c);
    w = io.take();
    CHECK(w.size() == 2 && w[1] == 'E');
    CHECK(c.notice_pending && strstr(c.notice, "D1:") != 0);
    fs.read_only = false;

    // --- .xex as D1: and a cold start
    std::vector<uint8_t> x = { 0xFF, 0xFF, 0x00, 0x06, 0x00, 0x06, 0x60 };
    fs.put("/usb0/p.xex", x);
    io.events.clear();
    CHECK_EQ(a8_run_xex(&c, "/usb0/p.xex", 0, msg, sizeof(msg)), 0);
    CHECK_STR(ops_state(&c, atari800_desc.media[0].state), "p.xex");
    CHECK_STR(io.events.front().c_str(), "mode_reset");
    CHECK_STR(io.events.back().c_str(), "mode_unreset");
    io.command(0x31, 0x52, 1, 0);
    a8_service(&c);
    w = io.take();
    CHECK(w.size() == 131 && w[2] == 0x00 && w[3] == 0x03);          // sector 1 = the loader's boot header (flags 0, 3 sectors)
    CHECK_EQ(a8_run_xex(&c, "/usb0/junk.atr", 0, msg, sizeof(msg)), 1);
    CHECK(strstr(msg, "not an Atari binary") != 0);
    CHECK_STR(ops_state(&c, atari800_desc.media[0].state), "p.xex");              // a refused file leaves D1: as it was
    // --- T180 D4 / D1 (S7): the sector a game wrote is synced to its file before the disk is let go (eject, replace, reset, restart); a medium that
    // refuses keeps the disk in the drive, stops the action (the machine is not held) and says why; the same action goes through once the medium is back
    {
        fs.put("/usb0/s.atr", atr);
        std::vector<uint8_t> atr2 = atr;
        atr2[16] ^= 0xFF;
        fs.put("/usb0/e.atr", atr2);
        CHECK_EQ(a8_mount(&c, "/usb0/s.atr", 2, false, msg, sizeof(msg)), 0);
        io.command(0x33, 0x57, 4, 0);                                     // D3:, sector 4
        a8_service(&c);
        io.take();
        io.data(std::vector<uint8_t>(128, 0x5C));
        a8_service(&c);
        w = io.take();
        CHECK(w.size() == 2 && w[1] == 'C');
        CHECK(a8_media_pending(&c));
        size_t ev = io.events.size();
        int open = fs.open_files;
        fs.sync_fails = true;
        CHECK(a8_media_flush(&c, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "D3:") != 0 && strstr(msg, "stays mounted") != 0);
        CHECK(a8_eject(&c, 2, msg, sizeof(msg)) != 0);                    // the eject stops ...
        CHECK(a8_state(&c)->disk[2] != 0);                                // ... the disk stays in the drive
        CHECK(a8_mount(&c, "/usb0/e.atr", 2, false, msg, sizeof(msg)) != 0);        // D1: so does a replacement
        CHECK_STR(a8_state(&c)->disk[2]->name(), "s.atr");
        CHECK_EQ(fs.open_files, open);                                    // the new file is not left open
        CHECK(media_writeback(&c, WB_WHY_RESET, msg, sizeof(msg)) != 0);   // the common path asks the engine as well ...
        CHECK(strstr(msg, "D3:") != 0);
        CHECK(boot_cold_reset(&c, msg, sizeof(msg)) != 0);                // ... so a reset stops
        CHECK(boot_restart(&c, msg, sizeof(msg)) != 0);
        CHECK_EQ(io.events.size(), ev);                                   // the machine was never held
        fs.sync_fails = false;
        CHECK_EQ(a8_mount(&c, "/usb0/e.atr", 2, false, msg, sizeof(msg)), 0);       // the medium is back: the replacement goes through
        CHECK_STR(a8_state(&c)->disk[2]->name(), "e.atr");
        CHECK(!a8_media_pending(&c));
        CHECK_EQ(fs.files["/usb0/s.atr"][16 + 3 * 128], 0x5C);
        CHECK_EQ(fs.open_files, open);
        CHECK_EQ(a8_eject(&c, 2, msg, sizeof(msg)), 0);
    }
    DONE("test_atari_media");
}
