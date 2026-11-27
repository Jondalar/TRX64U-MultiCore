// T154: what a game writes to its cartridge goes back to the user's .crt. The service (writeback.cc), the .crt writer (writeback_crt.cc) and the C64's
// flash path (c64_cart.cc: the interrupt handler, the geometry of the chips, the registers) are the real ones; the machine is the C64 model, the change
// source a fake gateware that raises exactly what the bitstream raises (IRQ_CAUSE b4 with the EVT word, b5 for the EEPROM) and waits for the ACK.
// Scenarios of the spec: S1 Auto, S2 Ask, S3 Off, S4 before reset / eject / reload, S5 read only medium, S6 negative control (run.sh breaks the
// event path and S1 must fail), S8 a battery RAM save (the service side).
#include <string>
#include <vector>
#include "check.h"
#include "c64_model.h"
#include "c64_cart.h"
#include "c64_cart_desc.h"
#include "boot.h"
#include "writeback.h"
#include "ops_glue.h"

const CoreDesc *const core_table[] = { 0 };

typedef std::vector<uint8_t> Bytes;

// ---- the file -------------------------------------------------------------------------------------------------------------------------------
static void put16(Bytes &b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void put32(Bytes &b, uint32_t v) { put16(b, (uint16_t)(v >> 16)); put16(b, (uint16_t)v); }
static Bytes crt_header(uint16_t type, uint8_t exrom, uint8_t game)
{
    Bytes b;
    const char *sig = "C64 CARTRIDGE   ";
    b.insert(b.end(), sig, sig + 16);
    put32(b, 0x40);
    put16(b, 0x0100);
    put16(b, type);
    b.push_back(exrom); b.push_back(game);
    b.resize(0x40, 0);
    return b;
}
static void chip(Bytes &b, uint16_t type, uint16_t bank, uint16_t load, uint16_t size, uint8_t fill)
{
    b.insert(b.end(), {'C', 'H', 'I', 'P'});
    put32(b, 16u + size);
    put16(b, type);
    put16(b, bank);
    put16(b, load);
    put16(b, size);
    for (int i = 0; i < size; i++) b.push_back((uint8_t)(fill + i));
}
// an EasyFlash with `banks` banks, ROML at $8000 and ROMH at $A000 (bank 0's ROMH at $E000 as the Ultimax boot), flash packets
static Bytes easyflash(int banks)
{
    Bytes b = crt_header(32, 1, 0);
    for (int i = 0; i < banks; i++) {
        chip(b, 2, i, 0x8000, 0x2000, (uint8_t)(0x10 * i));
        chip(b, 2, i, i == 0 ? 0xE000 : 0xA000, 0x2000, (uint8_t)(0x10 * i + 7));
    }
    return b;
}

struct Pk { uint16_t type, bank, load, size; size_t data; };
static std::vector<Pk> packets(const Bytes &f)
{
    std::vector<Pk> v;
    size_t pos = 0x40;
    while (pos + 16 <= f.size() && !memcmp(&f[pos], "CHIP", 4)) {
        Pk p;
        p.type = (uint16_t)(f[pos + 8] << 8 | f[pos + 9]);
        p.bank = (uint16_t)(f[pos + 10] << 8 | f[pos + 11]);
        p.load = (uint16_t)(f[pos + 12] << 8 | f[pos + 13]);
        p.size = (uint16_t)(f[pos + 14] << 8 | f[pos + 15]);
        p.data = pos + 16;
        v.push_back(p);
        pos += ((size_t)f[pos + 4] << 24 | (size_t)f[pos + 5] << 16 | (size_t)f[pos + 6] << 8 | f[pos + 7]);
    }
    return v;
}
static const Pk *find(const std::vector<Pk> &v, int bank, int load)
{
    for (auto &p : v) if (p.bank == bank && p.load == load) return &p;
    return 0;
}

// a packet the test needs: a missing one is a red check and a harmless dummy, not a crash of the bench against a broken product
static const Pk &need(const std::vector<Pk> &v, int bank, int load)
{
    static const Pk none = { 0, 0, 0, 0, 0 };
    const Pk *p = find(v, bank, load);
    CHECK(p != 0);
    return p ? *p : none;
}

// ---- the platform and the gateware ----------------------------------------------------------------------------------------------------------
struct FakePort : WbPort {
    FakeIo *io;
    uint8_t causes = 0, en = 0;
    uint8_t (*handler)(uint8_t) = 0;
    std::vector<std::string> log;
    uint32_t ticks_ms() { return io->t; }
    void enter() { }
    void leave() { }
    void irq_attach(uint8_t c, uint8_t (*h)(uint8_t)) { causes = c; handler = h; }
    void irq_enable(uint8_t c, bool on)
    {
        en = on ? (en | c) : (en & ~c);
        log.push_back(std::string(on ? "en " : "dis ") + std::to_string(c));
    }
    // The line is level: whatever is pending and enabled calls the handler.
    // IRQ_CAUSE is write-1-to-clear: what the handler wrote there is cleared here, and returned.
    uint8_t service(C64Model &m)
    {
        uint8_t act = (uint8_t)(m.regs[C64_IRQ_CAUSE & 0xFFF] & en & causes), cleared = 0;
        if (!act || !handler) return 0;
        m.log.clear();
        handler(act);
        for (auto &w : m.log) if (w.reg == C64_IRQ_CAUSE) cleared |= w.v;
        m.regs[C64_IRQ_CAUSE & 0xFFF] &= (uint8_t)~cleared;
        return cleared;
    }
};

// The responder: raises a flash operation; the C64 would be held busy until the ACK, so the ACK is what the test checks.
static bool flash_op(C64Model &m, FakePort &port, int cmd, int chip_no, uint32_t addr, uint8_t data)
{
    uint32_t evt = ((uint32_t)cmd << 30) | ((uint32_t)chip_no << 29) | ((addr & 0x1FFFFF) << 8) | data;
    for (int i = 0; i < 4; i++) m.p4[C64_CART_EVT + i] = (uint8_t)(evt >> (8 * i));
    m.p4[C64_CART_EVT_HI] = (uint8_t)((addr >> 21) & 3);
    m.p4[C64_CART_CMD] = 0;
    m.regs[C64_IRQ_CAUSE & 0xFFF] |= C64_IRQ_FLASH;
    bool cleared = (port.service(m) & C64_IRQ_FLASH) != 0;   // the handler has to clear the bit, and ACK
    return cleared && m.p4_get(C64_CART_CMD) == (C64_CART_CMD_INVAL | C64_CART_CMD_ACK);
}
static const int EV_PROG = 1, EV_SECTOR = 2, EV_CHIP = 3;
// image offsets of a two chip cartridge: chip address -> bank slots
static uint32_t ef_off(int chip_no, uint32_t a) { return ((a >> 13) << 14) | ((uint32_t)chip_no << 13) | (a & 0x1FFF); }

// The machine: a descriptor that records its reset
static std::vector<std::string> g_ev;
static FakeFs *g_fs;
static int hook(CoreCtx *, char *, int) { return 0; }
static int init_hook(CoreCtx *, char *, int) { g_ev.push_back("init mutations=" + std::to_string(g_fs->mutations)); return 0; }
static CoreDesc make_desc()
{
    CoreDesc d;
    memset(&d, 0, sizeof(d));
    d.init = init_hook;
    d.release = hook;
    d.reset_ms = 1;
    return d;
}

struct Rig {
    C64Model m;
    FakeFs fs;
    FakePort port;
    CoreDesc desc = make_desc();
    CoreCtx c;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &desc;
        c.io = &m;
        c.fs = &fs;
        port.io = &m;
        g_fs = &fs;
        g_ev.clear();
        wb_attach(&c, &port);                                       // first: the service still points at the previous rig's (freed) port until it is
        wb_unregister();
        wb_set_mode(WB_AUTO);
    }
    bool run(const char *path)
    {
        char msg[200];
        int rc = c64_cart_run(&c, path, msg, sizeof(msg));
        if (rc) printf("  c64_cart_run: %s\n", msg);
        m.regs[C64_IRQ_CAUSE & 0xFFF] = 0;                  // the start wrote 0x30 to the W1C register: on the bitstream that clears, the model only stores
        return rc == 0;
    }
    const Bytes &file(const char *p) { return fs.files[lower(p)]; }
    void quiet() { m.delay_ms(WB_QUIET_MS + 100); }
    // a program of ROML byte `inner` of bank `bank` to `value`
    bool prog_roml(int bank, uint32_t inner, uint8_t value) { return flash_op(m, port, EV_PROG, 0, (uint32_t)bank * 0x2000 + inner, value); }
    bool prog_romh(int bank, uint32_t inner, uint8_t value) { return flash_op(m, port, EV_PROG, 1, (uint32_t)bank * 0x2000 + inner, value); }
    uint8_t img(uint32_t off) { return m.FakeIo::ddr.at(off); }
};
static const char *F = "/SD/games/ef.crt";

static Rig *ef_rig(int banks, int mode = WB_AUTO)
{
    Rig *r = new Rig;
    r->fs.put(F, easyflash(banks));
    CHECK(r->run(F));
    wb_set_mode(mode);
    return r;
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);                            // a crash against a broken product still leaves its red lines
    // ---- the start: the responder is configured from the part, the file registered, the interrupt on
    {
        Rig *r = ef_rig(4);
        const C64FlashPart *p = 0;
        for (auto &d : c64_cart_descs) if (d.cart_type == 0x11) p = d.flash;
        CHECK(p != 0);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ENABLE), 1);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_MAGIC1) | (r->m.p4_get(C64_CART_FL_MAGIC1 + 1) << 8), 0x0555);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_MAGIC2) | (r->m.p4_get(C64_CART_FL_MAGIC2 + 1) << 8), 0x02AA);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_MASK) | (r->m.p4_get(C64_CART_FL_MASK + 1) << 8), 0x07FF);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID), 0x01);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID + 1), 0xA4);
        CHECK(wb_active());
        CHECK_STR(wb_path(), F);
        CHECK_EQ(r->port.en & 0x30, 0x30);
        CHECK(!wb_pending());
        delete r;
    }

    // ---- D50/D51 (review): the real c64_cart.cc drops the cartridge's cached lines (cmd.inval) after an image write and in c64_cart_drop_cache, and an image write is no change to write back
    {
        Rig *r = ef_rig(4);
        uint8_t b[16];
        memset(b, 0x5A, sizeof(b));
        r->m.p4[C64_CART_CMD] = 0;
        c64_cart_image_write(&r->c, 0x100, b, sizeof(b));
        CHECK_EQ(r->m.p4_get(C64_CART_CMD), C64_CART_CMD_INVAL);
        CHECK_EQ(r->img(0x100), 0x5A);                              // the bytes went to the image
        CHECK(!wb_pending());                                       // a restore is not a change
        r->m.p4[C64_CART_CMD] = 0;
        c64_cart_drop_cache(&r->c);
        CHECK_EQ(r->m.p4_get(C64_CART_CMD), C64_CART_CMD_INVAL);
        CHECK(!wb_pending());
        delete r;
    }

    // ---- S1: EasyFlash save on Auto
    {
        Rig *r = ef_rig(4);
        Bytes before = r->file(F);
        // the game erases the sector of ROML (banks 0-7 of chip 0), programs bank 1 ROML at $10, and a byte of bank 2 ROMH
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 0, 0, 0));
        CHECK_EQ(r->img(ef_off(0, 0x2000 + 5)), 0xFF);                 // ROML of bank 1 erased
        CHECK_EQ(r->img(ef_off(1, 2 * 0x2000 + 3)), 0x20 + 7 + 3);    // ROMH of bank 2 (the other chip) untouched
        CHECK(r->prog_roml(1, 0x10, 0x5A));
        CHECK_EQ(r->img(ef_off(0, 0x2000 + 0x10)), 0x5A);
        CHECK(r->prog_roml(1, 0x10, 0x0F));                           // a program only clears bits
        CHECK_EQ(r->img(ef_off(0, 0x2000 + 0x10)), 0x0A);
        CHECK(r->prog_romh(2, 3, 0x11));
        CHECK(wb_pending());
        int w = r->fs.mutations;
        wb_poll(&r->c);
        CHECK_EQ(r->fs.mutations, w);                                  // younger than the quiet time: not yet
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        const Bytes &now = r->file(F);
        std::vector<Pk> pk = packets(now);
        CHECK_EQ(pk.size(), 8);
        // the packets hold the image as it is; the header is as it was
        CHECK(memcmp(&now[0], &before[0], 0x40) == 0);
        bool same = true;
        std::vector<Pk> bpk = packets(before);
        for (auto &p : pk) {
            const Pk &bp = need(bpk, p.bank, p.load);
            uint32_t off = (uint32_t)p.bank * 0x4000 + (p.load & 0x2000);
            same = same && memcmp(&now[p.data], &r->m.FakeIo::ddr[off], p.size) == 0;
            same = same && p.type == bp.type && p.size == bp.size;
            if (!same) { printf("  packet bank %d load %04x differs (type %d/%d size %d/%d)\n", p.bank, p.load, p.type, bp.type, p.size, bp.size); break; }
        }
        CHECK(same);
        const Pk *b1 = &need(pk, 1, 0x8000);
        CHECK_EQ(now[b1->data + 0x10], 0x0A);
        CHECK_EQ(now[b1->data + 5], 0xFF);
        CHECK_EQ(now[need(pk, 2, 0xA000).data + 3], (0x20 + 7 + 3) & 0x11);
        // the bank 3 ROMH packet was not in the sector erase (other chip) and is the original
        const Pk *b3h = &need(pk, 3, 0xA000);
        CHECK(memcmp(&now[b3h->data], &before[need(bpk, 3, 0xA000).data], 0x2000) == 0);
        CHECK(r->fs.has("/SD/games/ef.crt.bak"));                      // the file as it was is kept once
        CHECK(r->file("/SD/games/ef.crt.bak") == before);
        CHECK(!r->fs.has("/SD/games/ef.crt.tmp"));
        // the hold: flash operations waited while the image was read
        CHECK(r->port.log.size() >= 2);
        delete r;
    }

    // ---- an erase alone is a change too: the erased banks reach the file
    {
        Rig *r = ef_rig(4);
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 0, 0, 0));
        CHECK(wb_pending());
        r->quiet();
        wb_poll(&r->c);
        std::vector<Pk> pk = packets(r->file(F));
        const Pk &e = need(pk, 1, 0x8000);
        CHECK_EQ(r->file(F)[e.data], 0xFF);
        CHECK_EQ(r->file(F)[e.data + 0x1FFF], 0xFF);
        CHECK(r->file(F)[need(pk, 1, 0xA000).data] != 0xFF);
        delete r;
    }

    // ---- T154 D6 (Alex 2026-11-27): the machine's flash waits (the interrupt is off) only while the changed blocks are copied; the file is written with
    // the flash going on, and it holds the image as it was at the copy. A program during the write goes into the next file.
    {
        struct Probe { Rig *r; int writes, held; bool done, ok; };
        Rig *r = ef_rig(4);
        Probe pr = { r, 0, 0, false, false };
        r->fs.write_ms = 32;
        r->fs.tick = [](void *a, uint32_t) {
            Probe *q = (Probe *)a;
            q->writes++;
            if (!(q->r->port.en & C64_IRQ_FLASH)) q->held++;
            if (!q->done) { q->done = true; q->ok = q->r->prog_roml(2, 7, 0x00); }   // the game saves on while the file is written
        };
        r->fs.tick_arg = &pr;
        CHECK(r->prog_roml(0, 1, 0x00));
        size_t log0 = r->port.log.size();
        r->quiet();
        wb_poll(&r->c);
        CHECK(pr.writes > 0);
        CHECK_EQ(pr.held, 0);                                           // no file write with the flash held
        CHECK(pr.ok);                                                   // the program during the write was acknowledged at once
        const std::string dis = "dis " + std::to_string(C64_IRQ_FLASH), en = "en " + std::to_string(C64_IRQ_FLASH);
        CHECK(r->port.log.size() >= log0 + 2 && r->port.log[log0] == dis && r->port.log[log0 + 1] == en);   // held for the copy, then on again
        std::vector<Pk> pk = packets(r->file(F));
        CHECK_EQ(r->file(F)[need(pk, 0, 0x8000).data + 1], 0x00);       // the change before the copy is in the file
        CHECK_EQ(r->file(F)[need(pk, 2, 0x8000).data + 7], 0x27);       // the one during the write is not: the file is the state at the copy
        CHECK(wb_pending());
        r->fs.tick = 0;
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        pk = packets(r->file(F));
        CHECK_EQ(r->file(F)[need(pk, 2, 0x8000).data + 7], 0x00);       // ... it is in the next one
        CHECK_EQ(r->file(F)[need(pk, 0, 0x8000).data + 1], 0x00);
        CHECK_EQ(r->port.en & C64_IRQ_FLASH, C64_IRQ_FLASH);            // and the flash is on afterwards
        delete r;
    }

    // ---- a block the file does not hold gets a packet; an erased one does not; EEPROM-less
    {
        Rig *r = ef_rig(2);                                             // the file has banks 0-1, the chip has 64
        CHECK(r->prog_roml(5, 7, 0x42));                                // bank 5 is not in the file
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 1, 8 * 0x2000, 0));    // sector 1 of the ROMH chip (banks 8-15): erased, blank
        r->quiet();
        wb_poll(&r->c);
        std::vector<Pk> pk = packets(r->file(F));
        CHECK_EQ(pk.size(), 5);
        const Pk *n = find(pk, 5, 0x8000);
        CHECK(n != 0);
        if (n) { CHECK_EQ(n->size, 0x2000); CHECK_EQ(n->type, 2); CHECK(n->data + 9 <= r->file(F).size()); CHECK_EQ(r->file(F)[n->data + 7], 0x42); CHECK_EQ(r->file(F)[n->data + 8], 0xFF); }
        CHECK(find(pk, 9, 0xA000) == 0);
        delete r;
    }

    // ---- geometry: a chip erase wipes both chips; a sector is 64 KB of ONE chip
    {
        Rig *r = ef_rig(16);
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 1, 9 * 0x2000 + 4, 0));   // ROMH chip, sector of banks 8-15
        CHECK_EQ(r->img(ef_off(1, 8 * 0x2000)), 0xFF);
        CHECK_EQ(r->img(ef_off(1, 15 * 0x2000 + 0x1FFF)), 0xFF);
        CHECK_EQ(r->img(ef_off(1, 7 * 0x2000)) != 0xFF, true);          // bank 7 ROMH is in the sector before
        CHECK_EQ(r->img(ef_off(0, 9 * 0x2000)) != 0xFF, true);          // ROML of the same banks untouched
        CHECK(flash_op(r->m, r->port, EV_CHIP, 0, 0, 0));
        CHECK_EQ(r->img(ef_off(0, 3 * 0x2000)), 0xFF);                   // a chip erase wipes the whole chip it is sent to (ROML) ...
        CHECK_EQ(r->img(ef_off(0, 15 * 0x2000 + 0x1FFF)), 0xFF);
        CHECK_EQ(r->img(ef_off(1, 3 * 0x2000)) != 0xFF, true);           // ... and not the other chip
        delete r;
    }

    // ---- S2: Ask
    {
        Rig *r = ef_rig(4, WB_ASK);
        Bytes before = r->file(F);
        CHECK(r->prog_roml(0, 9, 0x00));
        r->quiet();
        wb_poll(&r->c);
        CHECK(r->file(F) == before);                                    // Ask never writes in the background
        char q[120], note[200];
        CHECK(wb_menu_open(&r->c, q, sizeof(q), note, sizeof(note)));
        CHECK(strstr(q, "ef.crt") != 0);
        char msg[200];
        CHECK_EQ(wb_answer(&r->c, true, msg, sizeof(msg)), 0);
        CHECK(r->file(F) != before);
        CHECK_EQ(r->file(F)[need(packets(r->file(F)), 0, 0x8000).data + 9], 0x00);
        CHECK(!wb_pending());
        // No drops the change and it is not asked again
        Bytes saved = r->file(F);
        CHECK(r->prog_roml(1, 2, 0x01));
        CHECK(wb_menu_open(&r->c, q, sizeof(q), note, sizeof(note)));
        CHECK_EQ(wb_answer(&r->c, false, msg, sizeof(msg)), 0);
        CHECK(r->file(F) == saved);
        CHECK(!wb_pending());
        CHECK(!wb_menu_open(&r->c, q, sizeof(q), note, sizeof(note)));
        delete r;
    }

    // ---- S3: Off - never written, whatever happens
    {
        Rig *r = ef_rig(4, WB_OFF);
        Bytes before = r->file(F);
        int w = r->fs.mutations;
        CHECK(r->prog_roml(0, 9, 0x00));
        r->quiet();
        wb_poll(&r->c);
        char q[120], note[200], msg[200];
        CHECK(!wb_menu_open(&r->c, q, sizeof(q), note, sizeof(note)));
        CHECK_EQ(boot_cold_reset(&r->c, msg, sizeof(msg)), 0);
        CHECK_EQ(c64_cart_remove(&r->c, msg, sizeof(msg)), 0);
        CHECK(r->file(F) == before);
        CHECK_EQ(r->fs.mutations, w);
        delete r;
    }

    // ---- S4: Auto, a change younger than the quiet time, then reset / eject / a new cartridge: the file first
    {
        Rig *r = ef_rig(4);
        Bytes before = r->file(F);
        CHECK(r->prog_roml(2, 1, 0x00));
        char msg[200];
        int w = r->fs.mutations;
        g_ev.clear();
        CHECK_EQ(boot_cold_reset(&r->c, msg, sizeof(msg)), 0);
        CHECK(r->file(F) != before);
        CHECK(g_ev.size() >= 1 && g_ev[0] != "init mutations=" + std::to_string(w));   // the file was written before the machine was held
        Bytes after = r->file(F);

        CHECK(r->prog_roml(2, 2, 0x00));
        CHECK_EQ(c64_cart_remove(&r->c, msg, sizeof(msg)), 0);
        CHECK(r->file(F) != after);
        CHECK(!wb_active());
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ENABLE), 0);
        after = r->file(F);

        // a new cartridge: the one before goes to its file first
        CHECK(r->run(F));
        CHECK(r->prog_roml(3, 3, 0x00));
        r->fs.put("/SD/games/other.crt", easyflash(2));
        CHECK(r->run("/SD/games/other.crt"));
        CHECK(r->file(F) != after);
        CHECK_STR(wb_path(), "/SD/games/other.crt");

        // reload and power-off request
        CHECK(r->prog_roml(1, 3, 0x00));
        after = r->file("/SD/games/other.crt");
        CHECK_EQ(boot_reload(&r->c, msg, sizeof(msg)), 0);
        CHECK(r->file("/SD/games/other.crt") != after);
        after = r->file("/SD/games/other.crt");
        CHECK(r->prog_roml(1, 4, 0x00));
        CHECK_EQ(wb_flush(&r->c, WB_WHY_POWER, msg, sizeof(msg)), 0);
        CHECK(r->file("/SD/games/other.crt") != after);
        delete r;
    }

    // ---- the maximum age: a game that never gets quiet is written anyway
    {
        Rig *r = ef_rig(4);
        Bytes before = r->file(F);
        for (int s = 0; s < 31; s++) {
            CHECK(r->prog_roml(0, (uint32_t)s, 0x00));
            r->m.delay_ms(1000);
            wb_poll(&r->c);
            if (r->file(F) != before) {
                CHECK(s >= 29);
                break;
            }
        }
        CHECK(r->file(F) != before);
        delete r;
    }

    // ---- S5: a read only medium - said once per change, the image keeps it
    {
        Rig *r = ef_rig(4);
        r->fs.read_only = true;
        Bytes before = r->file(F);
        int w = r->fs.write_attempts + r->fs.mutations;
        CHECK(r->prog_roml(0, 9, 0x00));
        r->quiet();
        wb_poll(&r->c);
        CHECK(r->c.notice_pending);
        CHECK(strstr(r->c.notice, "cannot be written") != 0);
        r->c.notice_pending = false;
        wb_poll(&r->c);
        r->m.delay_ms(5000);
        wb_poll(&r->c);
        CHECK(!r->c.notice_pending);                                    // once
        CHECK(r->file(F) == before);
        CHECK_EQ(r->fs.write_attempts + r->fs.mutations, w);
        CHECK_EQ(r->img(ef_off(0, 9)), 0x00);                           // the machine keeps it
        char q[120], note[200];
        CHECK(!wb_menu_open(&r->c, q, sizeof(q), note, sizeof(note)));  // Ask would not ask for what cannot be saved
        CHECK(r->prog_roml(0, 10, 0x00));                               // a new change: told once again
        r->quiet();
        wb_poll(&r->c);
        CHECK(r->c.notice_pending);
        // the card is writable again: the change is still pending and goes out
        r->fs.read_only = false;
        r->c.notice_pending = false;
        r->m.delay_ms(WB_QUIET_MS);
        r->m.delay_ms(WB_QUIET_MS);
        wb_poll(&r->c);
        CHECK(!wb_pending());
        delete r;
    }

    // ---- a failed write keeps the change, tells, and tries again after the retry time
    {
        Rig *r = ef_rig(4);
        Bytes before = r->file(F);
        CHECK(r->prog_roml(0, 9, 0x00));
        r->quiet();
        r->fs.mut_budget = 3;                                           // the card is full after a few steps
        wb_poll(&r->c);
        CHECK(wb_pending());
        CHECK(r->c.notice_pending);
        CHECK(r->file(F) == before);                                    // the old file is whole
        r->fs.mut_budget = -1;
        r->c.notice_pending = false;
        wb_poll(&r->c);
        CHECK(wb_pending());                                            // not before the retry time
        r->m.delay_ms(WB_RETRY_MS + 10);
        wb_poll(&r->c);
        CHECK(!wb_pending());
        CHECK(r->file(F) != before);
        delete r;
    }

    // ---- GMod2: the EEPROM travels the same way
    {
        Rig *r = new Rig;
        Bytes b = crt_header(60, 0, 1);
        for (int i = 0; i < 4; i++) chip(b, 2, i, 0x8000, 0x2000, (uint8_t)(0x20 * i));
        chip(b, 0, 0, 0xDE00, 0x800, 0x40);
        r->fs.put("/SD/games/g.crt", b);
        CHECK(r->run("/SD/games/g.crt"));
        CHECK_EQ(r->m.ee[2048 + 5], 0x45);                              // the EEPROM packet went into the window
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID + 1), 0xA4);
        CHECK(wb_active());
        // the C64 writes the EEPROM
        r->m.ee[2048 + 5] = 0x99;
        r->m.ee[0] = 1;
        r->m.regs[C64_IRQ_CAUSE & 0xFFF] |= C64_IRQ_EEPROM;
        r->port.service(r->m);
        CHECK(wb_pending());
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        std::vector<Pk> pk = packets(r->file("/SD/games/g.crt"));
        CHECK_EQ(pk.size(), 5);
        const Pk *e = find(pk, 0, 0xDE00);
        CHECK(e != 0);
        if (e) { CHECK_EQ(r->file("/SD/games/g.crt")[e->data + 5], 0x99); CHECK_EQ(r->file("/SD/games/g.crt")[e->data + 6], 0x46); }
        CHECK_EQ(r->m.ee[0], 0);                                        // the dirty flag was cleared before the copy
        // a copy torn by a write of the C64 is repeated; one that keeps tearing keeps the change
        r->m.ee[2048 + 5] = 0x77;
        r->m.regs[C64_IRQ_CAUSE & 0xFFF] |= C64_IRQ_EEPROM;
        r->port.service(r->m);
        r->m.ee_tear = 2;
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        CHECK_EQ(r->file("/SD/games/g.crt")[need(packets(r->file("/SD/games/g.crt")), 0, 0xDE00).data + 5], 0x77);
        r->m.ee[2048 + 5] = 0x55;
        r->m.regs[C64_IRQ_CAUSE & 0xFFF] |= C64_IRQ_EEPROM;
        r->port.service(r->m);
        r->m.ee_tear = 100;
        r->quiet();
        wb_poll(&r->c);
        CHECK(wb_pending());
        CHECK_EQ(r->file("/SD/games/g.crt")[need(packets(r->file("/SD/games/g.crt")), 0, 0xDE00).data + 5], 0x77);
        delete r;
    }

    // ---- EasyFlash 3 (CRT 90, T51): the 8 MiB window, the chip's own address order, 23-bit events, write-back like the others
    {
        const char *E3 = "/SD/games/ef3.crt";
        Bytes img = crt_header(90, 1, 0);
        for (int bank = 0; bank < 512; bank++) {                         // slot-major, bank = slot * 64 + bank
            chip(img, 2, bank, 0x8000, 0x2000, (uint8_t)(bank * 7));
            chip(img, 2, bank, bank == 0 ? 0xE000 : 0xA000, 0x2000, (uint8_t)(bank * 7 + 3));
        }
        // refused when the bitstream does not say it has the window and the banking
        {
            Rig *r = new Rig;
            r->fs.put(E3, img);
            r->m.p4[C64_CART_XPORT_CAPS] = 0x01;                        // the window alone is not enough
            CHECK(!r->run(E3));
            CHECK(!wb_active());
            delete r;
        }
        Rig *r = new Rig;
        r->fs.put(E3, img);
        r->m.p4[C64_CART_XPORT_CAPS] = C64_XCAPS_8MB | C64_XCAPS_EF3;
        CHECK(r->run(E3));
        wb_set_mode(WB_AUTO);
        CHECK(wb_active());
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ENABLE), 1);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_MAGIC1) | (r->m.p4_get(C64_CART_FL_MAGIC1 + 1) << 8), 0x0AAA);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_MAGIC2) | (r->m.p4_get(C64_CART_FL_MAGIC2 + 1) << 8), 0x0555);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID), 0xC2);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID + 1), 0xCB);
        CHECK_EQ(r->m.p4_get(C64_CART_FL_ID + 2), 0x02);
        // the whole image is in the window: bank 511, ROMH, and bank 0's ROMH that the file has at $E000
        CHECK_EQ(r->img(511u * 0x4000 + 0x2000 + 1), (uint8_t)(511 * 7 + 3 + 1));
        CHECK_EQ(r->img(300u * 0x4000 + 2), (uint8_t)(300 * 7 + 2));
        CHECK_EQ(r->img(0x2000), 3);
        Bytes before = r->file(E3);
        // slot 2, bank_hi 3, bank_lo 5, ROMH, byte 7: chip address {slot, bank_hi, ROMH, bank_lo, A12-A0}, bits 22:21 through EVT_HI
        uint32_t a = (2u << 20) | (3u << 17) | (1u << 16) | (5u << 13) | 7;
        CHECK(flash_op(r->m, r->port, EV_PROG, 0, a, 0x0F));
        uint32_t bank = 2 * 64 + 3 * 8 + 5;
        uint32_t off = bank * 0x4000 + 0x2000 + 7;
        CHECK_EQ(r->img(off), (uint8_t)((bank * 7 + 3 + 7) & 0x0F));
        CHECK_EQ(r->img(off + 1), (uint8_t)(bank * 7 + 3 + 8));                    // its neighbour is as it was
        // a 64 KB sector: 8 banks of ONE half (here ROML of slot 1, banks 0-7); the ROMH half of the same banks stays
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 0, 1u << 20, 0));
        CHECK_EQ(r->img(64u * 0x4000 + 5), 0xFF);
        CHECK_EQ(r->img(71u * 0x4000 + 0x1FFF), 0xFF);
        CHECK(r->img(72u * 0x4000) != 0xFF);                                      // the next group
        CHECK(r->img(64u * 0x4000 + 0x2000 + 5) != 0xFF);                         // the other half
        // a boot sector: 8 KB, the second one is ROML of bank 1 of slot 0
        CHECK(flash_op(r->m, r->port, EV_SECTOR, 0, 0x2000, 0));
        CHECK_EQ(r->img(1u * 0x4000 + 10), 0xFF);
        CHECK(r->img(2u * 0x4000 + 10) != 0xFF);
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        const Bytes &now = r->file(E3);
        CHECK_EQ(now.size(), before.size());
        std::vector<Pk> pk = packets(now), bpk = packets(before);
        CHECK_EQ(pk.size(), 1024);
        bool ok = pk.size() == bpk.size();
        int changed = 0;
        for (size_t i = 0; ok && i < pk.size(); i++) {
            uint32_t o = (uint32_t)pk[i].bank * 0x4000 + (pk[i].load & 0x2000);
            ok = ok && pk[i].bank == bpk[i].bank && pk[i].load == bpk[i].load && memcmp(&now[pk[i].data], &r->m.FakeIo::ddr[o], pk[i].size) == 0;
            if (memcmp(&now[pk[i].data], &before[bpk[i].data], pk[i].size)) changed++;
        }
        CHECK(ok);
        CHECK_EQ(changed, 1 + 8 + 1);                                             // the programmed half bank, eight erased ROML halves, the boot sector
        // the chip erase is faithful (T51 D5): the whole 8 MiB, and the file follows
        CHECK(flash_op(r->m, r->port, EV_CHIP, 0, 0, 0));
        CHECK_EQ(r->img(511u * 0x4000 + 0x2000), 0xFF);
        CHECK_EQ(r->img(0), 0xFF);
        CHECK_EQ(r->img(0x7FFFFF), 0xFF);
        r->quiet();
        wb_poll(&r->c);
        pk = packets(r->file(E3));
        CHECK_EQ(pk.size(), 1024);
        bool blank = true;
        for (auto &q : pk) for (int i = 0; i < q.size; i += 997) blank = blank && r->file(E3)[q.data + i] == 0xFF;
        CHECK(blank);
        delete r;
    }

    // ---- the setting: the apply operation sets the mode
    {
        Rig *r = new Rig;
        OpResult res;
        OpIn in = in_int("value", 1);
        CHECK_EQ(ops_call(&r->c, "writeback.mode", &in, 1, &res), 0);
        CHECK_EQ(wb_mode(), WB_ASK);
        in = in_int("value", 0);
        CHECK_EQ(ops_call(&r->c, "writeback.mode", &in, 1, &res), 0);
        CHECK_EQ(wb_mode(), WB_OFF);
        delete r;
    }

    // ---- S8: a battery RAM save, the service side (no console core reports a write yet - nothing in the machine calls this)
    {
        Rig *r = new Rig;
        std::vector<uint8_t> ram(8192, 0);
        struct Src { static int read(void *u, uint32_t off, void *buf, uint32_t n) { memcpy(buf, (uint8_t *)u + off, n); return 0; } };
        WbRaw raw = { Src::read, ram.data(), (uint32_t)ram.size() };
        r->c.cor_path[0] = 0;
        snprintf(r->c.cor_path, sizeof(r->c.cor_path), "/SD/cores/dummycon.cor");
        char path[260];
        CHECK(wb_save_path(&r->c, "/SD/games/Zelda II.bin", path, sizeof(path)));
        CHECK_STR(path, "/SD/cores/dummycon/saves/Zelda II.sav");                   // Q2, T148 D5
        r->c.cor_path[0] = 0;
        CHECK(wb_save_path(&r->c, "/SD/games/Zelda II.bin", path, sizeof(path)));
        CHECK_STR(path, "/SD/games/Zelda II.sav");                             // no core folder: beside the game
        snprintf(r->c.cor_path, sizeof(r->c.cor_path), "/SD/cores/dummycon.cor");
        wb_save_path(&r->c, "/SD/games/Zelda II.bin", path, sizeof(path));
        WbMedium m;
        memset(&m, 0, sizeof(m));
        snprintf(m.path, sizeof(m.path), "%s", path);
        snprintf(m.name, sizeof(m.name), "Zelda II.sav");
        m.write = wb_raw_write;
        m.user = &raw;
        m.may_create = true;
        wb_register(m);
        char buf[8];
        CHECK_EQ(wb_raw_load(&r->c, path, buf, 8), 0);                          // a fresh game: no file
        ram[100] = 0x77;
        wb_mark(100, 1);
        r->quiet();
        wb_poll(&r->c);
        CHECK(!wb_pending());
        CHECK_EQ(r->fs.files[lower(path)].size(), 8192);
        CHECK_EQ(r->fs.files[lower(path)][100], 0x77);
        CHECK_EQ(wb_raw_load(&r->c, path, ram.data(), 8192), 8192);
        ram[100] = 0x78;                                                        // the second save goes over it; the first is kept once
        wb_mark(100, 1);
        r->quiet();
        wb_poll(&r->c);
        CHECK_EQ(r->fs.files[lower(path)][100], 0x78);
        CHECK(r->fs.has(std::string(path) + ".bak"));
        delete r;
    }
    DONE("writeback");
}
