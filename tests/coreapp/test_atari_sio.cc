// T93 D6 / S7: the SIO drive. Command frames in, ACK / COMPLETE / data / checksum out, writes back to the file; a wrong
// checksum is refused; a drive that is not there stays silent; a bridge that is stuck does not hang the application.
#include <string>
#include "check.h"
#include "sio_model.h"
#include "atari_atr.h"

static std::vector<uint8_t> atr_sd(int sectors)       // 128-byte sectors, sector n filled with n
{
    std::vector<uint8_t> v(16, 0);
    uint32_t paras = sectors * 128 / 16;
    v[0] = 0x96; v[1] = 0x02; v[2] = paras & 0xFF; v[3] = (paras >> 8) & 0xFF; v[4] = 128; v[6] = (paras >> 16) & 0xFF;
    for (int n = 1; n <= sectors; n++) v.insert(v.end(), 128, (uint8_t)n);
    return v;
}
static std::vector<uint8_t> atr_dd(int sectors)        // 3 boot sectors of 128, then 256-byte ones; sector n filled with n
{
    std::vector<uint8_t> v(16, 0);
    uint32_t bytes = 3 * 128 + (sectors - 3) * 256, paras = bytes / 16;
    v[0] = 0x96; v[1] = 0x02; v[2] = paras & 0xFF; v[3] = (paras >> 8) & 0xFF; v[4] = 0; v[5] = 1; v[6] = (paras >> 16) & 0xFF;
    for (int n = 1; n <= sectors; n++) v.insert(v.end(), n <= 3 ? 128 : 256, (uint8_t)n);
    return v;
}

static std::string said;
static int said_n;
static void say(void *, const char *t) { said = t; said_n++; }

struct Rig {
    SioModel io; FakeFs fs; SioLink link; AtariDisk *disk[4]; SioEngine eng;
    Rig() : link(&io), eng(&link, disk) { for (auto &d : disk) d = 0; eng.on_message(say, 0); said_n = 0; said.clear(); }
    ~Rig() { for (auto d : disk) delete d; }
    void mount(int i, const char *path)
    {
        char err[200];
        disk[i] = atr_open(&fs, path, err, sizeof(err));
        if (!disk[i]) printf("mount failed: %s\n", err);
    }
};

int main()
{
    // the checksum: hand-computed
    { uint8_t f[4] = { 0x31, 0x52, 0x01, 0x00 }; CHECK_EQ(sio_checksum(f, 4), 0x84); }
    { uint8_t f[2] = { 0xFF, 0x01 }; CHECK_EQ(sio_checksum(f, 2), 0x01); }       // 0x100 loses 256 and gains 1
    { uint8_t f[2] = { 0xFF, 0xFF }; CHECK_EQ(sio_checksum(f, 2), 0xFF); }       // 0x1FE -> 0xFE + 1
    { uint8_t f[3] = { 0x80, 0x80, 0x80 }; CHECK_EQ(sio_checksum(f, 3), 0x81); } // 0x100 -> 1, then + 0x80 = 0x81
    { std::vector<uint8_t> z(128, 1); CHECK_EQ(sio_checksum(z.data(), 128), 128); }
    { uint8_t f[5] = { 0x31, 0x52, 0x01, 0x00, 0x84 }; CHECK_EQ(sio_checksum(f, 4), f[4]); }

    // --- read sectors
    {
        Rig r;
        r.fs.put("/usb0/a.atr", atr_sd(720));
        r.mount(0, "/usb0/a.atr");
        r.io.command(0x31, 0x52, 5, 0);
        CHECK(r.eng.poll());
        CHECK(r.io.sent_at_atari_speed());                 // T108: the transmit divisor is the receive divisor, not the reset value 0
        r.io.settle();
        CHECK_EQ(r.io.wire_us.size(), 131);
        if (r.io.wire_us.size() == 131) {                  // T5: COMPLETE starts 250 us after the END of the ACK byte (a byte is 521 us)
            CHECK(r.io.wire_us[1] - r.io.wire_us[0] >= 521 + 250);
            CHECK(r.io.wire_us[2] - r.io.wire_us[1] <= 521);   // the data follows COMPLETE back to back, as it always did
        }
        auto w = r.io.take();
        CHECK_EQ(w.size(), 1 + 1 + 128 + 1);
        CHECK(w.size() == 131 && w[0] == 'A' && w[1] == 'C');
        bool data_ok = w.size() == 131;
        for (int i = 0; data_ok && i < 128; i++) data_ok &= w[2 + i] == 5;
        CHECK(data_ok);
        CHECK_EQ(w.size() == 131 ? w[130] : -1, 130);       // 128 bytes of 5 with the carry brought round, summed by hand
        // last sector, and one past it
        r.io.command(0x31, 0x52, 0xD0, 0x02);              // 720
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 131 && w[0] == 'A' && w[2] == (uint8_t)(720 & 0xFF));
        r.io.command(0x31, 0x52, 0xD1, 0x02);              // 721: NAK
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        r.io.command(0x31, 0x52, 0, 0);                    // sector 0: NAK
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        CHECK_EQ(r.eng.frames(), 4);
    }
    // --- the wrong checksum is rejected, and nothing is read
    {
        Rig r;
        r.fs.put("/usb0/a.atr", atr_sd(40));
        r.mount(0, "/usb0/a.atr");
        r.io.command(0x31, 0x52, 5, 0, 1);
        r.eng.poll();
        auto w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        CHECK_EQ(r.eng.bad_checksums(), 1);
        // a frame whose marker never came is not executed
        r.io.command(0x31, 0x52, 5, 0, 0, false);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 0);
        // and a four-byte frame with a marker is not a frame
        r.io.rx.push_back((1 << 8) | 0x31); r.io.rx.push_back((2 << 8) | 0x52); r.io.rx.push_back((3 << 8) | 5);
        r.io.rx.push_back((4 << 8) | 0); r.io.rx.push_back((4 << 8) | 0xFF);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 0);
        // stray bytes (count 0) are ignored
        r.io.rx.push_back(0x55); r.io.rx.push_back(0xAA);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 0);
        // and the engine is still fine afterwards
        r.io.command(0x31, 0x52, 2, 0);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 131);
    }
    // --- a drive that is not there does not answer; D2: does
    {
        Rig r;
        r.fs.put("/usb0/b.atr", atr_sd(40));
        r.mount(1, "/usb0/b.atr");
        r.io.command(0x31, 0x53, 0, 0);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 0);
        CHECK_EQ(r.io.tx_div, 0);                          // a frame for a drive that is not there touches nothing
        r.io.rx_div = 47;                                  // the Atari speaks at another speed: the answer follows it
        r.io.command(0x32, 0x53, 0, 0);
        r.eng.poll();
        CHECK_EQ(r.io.tx_div, 47);
        CHECK(r.io.sent_at_atari_speed());
        auto w = r.io.take();
        CHECK(w.size() == 1 + 1 + 4 + 1);
        if (w.size() == 7) { CHECK_EQ(w[0], 'A'); CHECK_EQ(w[1], 'C'); CHECK_EQ(w[2], 0x10); CHECK_EQ(w[3], 0xFF); CHECK_EQ(w[4], 0xE0); CHECK_EQ(w[5], 0); CHECK_EQ(w[6], 0xF0); }          // 10 + FF -> 10, + E0 = F0
        r.io.command(0x35, 0x53, 0, 0);                    // D5: no such drive
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 0);
        r.io.command(0x31 + 1, 0x21, 0, 0);                // format: NAK
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        CHECK_EQ(r.eng.unknown_commands(), 1);
    }
    // --- double density: 256-byte sectors from 4 up (259 bytes through a 256-byte FIFO), 128 below; status bit 5; percom
    {
        Rig r;
        r.fs.put("/usb0/dd.atr", atr_dd(720));
        r.mount(0, "/usb0/dd.atr");
        r.io.command(0x31, 0x52, 1, 0);
        r.eng.poll();
        auto w = r.io.take();
        CHECK_EQ(w.size(), 131);
        r.io.command(0x31, 0x52, 4, 0);
        r.eng.poll();
        w = r.io.take();
        CHECK_EQ(w.size(), 1 + 1 + 256 + 1);
        bool ok = w.size() == 259 && w[0] == 'A' && w[1] == 'C';
        for (int i = 0; ok && i < 256; i++) ok &= w[2 + i] == 4;
        CHECK(ok);
        r.io.command(0x31, 0x52, 0xD0, 0x02);              // 720: the last one, at the file's end
        r.eng.poll();
        w = r.io.take();
        CHECK_EQ(w.size(), 259);
        CHECK_EQ(w.size() == 259 ? w[2] : 0, 720 & 0xFF);          // the filler of sector 720 is its number mod 256 (0xD0)
        CHECK_EQ(w.size() == 259 ? w[258] : 0, 208);
        r.io.command(0x31, 0x53, 0, 0);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 7 && (w[2] & 0x20));
        r.io.command(0x31, 0x4E, 0, 0);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 1 + 1 + 12 + 1);
        if (w.size() == 15) { CHECK_EQ(w[2], 40); CHECK_EQ(w[4], 0); CHECK_EQ(w[5], 18); CHECK_EQ(w[7], 4); CHECK_EQ(w[8], 1); CHECK_EQ(w[9], 0); CHECK_EQ(w[10], 0xFF); }
    }
    // --- writes
    {
        Rig r;
        r.fs.put("/usb0/w.atr", atr_sd(40));
        r.mount(0, "/usb0/w.atr");
        std::vector<uint8_t> nd(128);
        for (int i = 0; i < 128; i++) nd[i] = (uint8_t)(0xC0 + i);
        r.io.command(0x31, 0x57, 7, 0);
        r.eng.poll();
        auto w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'A');                // ACK for the command, then the Atari sends the data
        r.io.data(nd);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 2 && w[0] == 'A' && w[1] == 'C');  // data ACK, COMPLETE
        auto &file = r.fs.files["/usb0/w.atr"];
        CHECK_EQ(file[16 + 6 * 128], 0xC0);                  // sector 7 at 16 + 6 x 128
        CHECK_EQ(file[16 + 6 * 128 + 127], (uint8_t)(0xC0 + 127));
        CHECK_EQ(file[16 + 5 * 128], 6);                     // its neighbours are untouched
        CHECK_EQ(file[16 + 7 * 128], 8);
        // the write is read back through the drive
        r.io.command(0x31, 0x52, 7, 0);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 131 && w[2] == 0xC0 && w[130] == 255);   // the data and its checksum, summed by hand
        // a wrong data checksum: ACK for the command, NAK for the data, nothing written
        std::vector<uint8_t> bad(128, 0x11);
        r.io.command(0x31, 0x50, 9, 0);
        r.eng.poll();
        r.io.take();
        r.io.data(bad, 1);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        CHECK_EQ(file[16 + 8 * 128], 9);
        CHECK_EQ(r.eng.bad_checksums(), 1);
        // the data never comes: the engine gives up after a while and takes the next command
        r.io.command(0x31, 0x57, 9, 0);
        r.eng.poll();
        r.io.take();
        r.io.advance(150000);
        r.eng.poll();
        r.io.command(0x31, 0x53, 0, 0);
        r.eng.poll();
        CHECK_EQ(r.io.take().size(), 7);
        // sync policy: not before 1 s of quiet, then once
        r.io.command(0x31, 0x57, 10, 0);
        r.eng.poll();
        r.io.data(nd);
        r.eng.poll();                                         // the write happens now
        int s0 = r.fs.syncs;
        r.eng.flush_idle(1000);
        CHECK_EQ(r.fs.syncs, s0);
        r.io.advance(500000);
        r.eng.flush_idle(1000);
        CHECK_EQ(r.fs.syncs, s0);
        r.io.advance(600000);
        r.eng.flush_idle(1000);
        CHECK_EQ(r.fs.syncs, s0 + 1);
        r.eng.flush_idle(1000);
        CHECK_EQ(r.fs.syncs, s0 + 1);                         // nothing dirty any more
    }
    // --- a read-only image refuses writes at once; a medium that fails during a write gives ERROR and one message
    {
        Rig r;
        r.fs.put("/usb0/r.atr", atr_sd(40));
        r.fs.read_only = true;
        r.mount(0, "/usb0/r.atr");
        CHECK(!r.disk[0]->writable());
        r.io.command(0x31, 0x57, 3, 0);
        r.eng.poll();
        auto w = r.io.take();
        CHECK(w.size() == 1 && w[0] == 'N');
        r.io.command(0x31, 0x53, 0, 0);
        r.eng.poll();
        w = r.io.take();
        CHECK(w.size() == 7 && (w[2] & 0x08));                // write protected in the status
        Rig q;
        q.fs.put("/usb0/q.atr", atr_sd(40));
        q.mount(0, "/usb0/q.atr");
        std::vector<uint8_t> nd(128, 0x42);
        for (int round = 0; round < 2; round++) {
            q.fs.read_only = true;                            // the card is write protected after the mount (the file was opened rw)
            q.io.command(0x31, 0x57, 3, 0);
            q.eng.poll();
            q.io.take();
            q.io.data(nd);
            q.eng.poll();
            w = q.io.take();
            CHECK(w.size() == 2 && w[0] == 'A' && w[1] == 'E');
        }
        CHECK_EQ(said_n, 1);                                  // told once, however often it fails
        CHECK(said.find("D1:") != std::string::npos);
        q.fs.read_only = false;
        q.io.command(0x31, 0x57, 3, 0);
        q.eng.poll();
        q.io.take();
        q.io.data(nd);
        q.eng.poll();
        w = q.io.take();
        CHECK(w.size() == 2 && w[1] == 'C');                  // works again
    }
    // --- T180 D4: a sync the medium refuses is not forgotten: the drive stays dirty, the idle flush tries again, flush_drive / flush_all say no with the reason
    {
        Rig r;
        r.fs.put("/usb0/s.atr", atr_sd(40));
        r.mount(0, "/usb0/s.atr");
        std::vector<uint8_t> nd(128, 0x77);
        r.io.command(0x31, 0x57, 4, 0);
        r.eng.poll();
        r.io.take();
        r.io.data(nd);
        r.eng.poll();
        CHECK(r.eng.pending());
        r.fs.sync_fails = true;
        r.io.advance(1100000);
        r.eng.flush_idle(1000);
        CHECK(r.eng.pending());                               // the sync failed: still owed
        CHECK_EQ(said_n, 1);
        r.io.advance(1100000);
        r.eng.flush_idle(1000);                               // tried again, told once
        CHECK(r.eng.pending());
        CHECK_EQ(said_n, 1);
        char m[200];
        CHECK(!r.eng.flush_drive(0, m, sizeof(m)));
        CHECK(strstr(m, "D1:") != 0 && strstr(m, "stays mounted") != 0);
        CHECK(!r.eng.flush_all(m, sizeof(m)));
        CHECK(r.eng.pending());
        r.fs.sync_fails = false;
        CHECK(r.eng.flush_all(m, sizeof(m)));
        CHECK(!r.eng.pending());
        int s = r.fs.syncs;
        CHECK(r.eng.flush_all(m, sizeof(m)));
        CHECK_EQ(r.fs.syncs, s);                              // nothing owed: nothing synced
    }
    // --- the stick is pulled: a read fails -> ERROR, no hang
    {
        Rig r;
        r.fs.put("/usb0/g.atr", atr_sd(40));
        r.mount(0, "/usb0/g.atr");
        r.fs.gone = true;
        r.io.command(0x31, 0x52, 2, 0);
        r.eng.poll();
        auto w = r.io.take();
        CHECK(w.size() == 2 && w[0] == 'A' && w[1] == 'E');
    }
    // --- a bridge that never finishes (the core is held): poll returns, sends nothing, counts the failure
    {
        Rig r;
        r.fs.put("/usb0/a.atr", atr_sd(40));
        r.mount(0, "/usb0/a.atr");
        r.io.stuck = true;
        r.io.command(0x31, 0x52, 5, 0);
        CHECK(!r.eng.poll());
        CHECK(r.link.failures() > 0);
        CHECK_EQ(r.io.take().size(), 0);
        r.io.stuck = false;
        CHECK(r.eng.poll());                                   // the frame is still in the FIFO and is answered now
        CHECK_EQ(r.io.take().size(), 131);
    }
    // --- ATR geometry, hand-computed
    {
        char err[100];
        std::vector<uint8_t> a = atr_sd(720);
        AtrGeometry g = atr_geometry(a.data(), (uint32_t)a.size(), err, sizeof(err));
        CHECK(g.ok && g.sectors == 720 && g.sector_size == 128 && g.header == 16);
        CHECK_EQ(a.size(), 16 + 92160);                        // 90 KB
        CHECK_EQ(atr_sector_offset(g, 1), 16);
        CHECK_EQ(atr_sector_offset(g, 720), 16 + 719 * 128);
        CHECK_EQ(atr_sector_offset(g, 721), 0);
        std::vector<uint8_t> d = atr_dd(720);
        g = atr_geometry(d.data(), (uint32_t)d.size(), err, sizeof(err));
        CHECK(g.ok && g.sectors == 720 && g.sector_size == 256 && g.packed_boot);
        CHECK_EQ(atr_sector_offset(g, 3), 16 + 256);
        CHECK_EQ(atr_sector_offset(g, 4), 16 + 384);
        CHECK_EQ(atr_sector_offset(g, 5), 16 + 384 + 256);
        // XFD: 92160 bytes of 128; 184320 of 256 with the boot sectors padded
        std::vector<uint8_t> x(92160, 0);
        g = atr_geometry(x.data(), 92160, err, sizeof(err));
        CHECK(g.ok && g.header == 0 && g.sectors == 720);
        CHECK_EQ(atr_sector_offset(g, 2), 128);
        g = atr_geometry(x.data(), 184320, err, sizeof(err));
        CHECK(g.ok && g.sector_size == 256 && g.sectors == 720);
        CHECK_EQ(atr_sector_offset(g, 2), 256);
        g = atr_geometry(x.data(), 100, err, sizeof(err));
        CHECK(!g.ok);
        // ATR with a sector size the drive does not do
        std::vector<uint8_t> b = atr_sd(40);
        b[4] = 0; b[5] = 2;
        g = atr_geometry(b.data(), (uint32_t)b.size(), err, sizeof(err));
        CHECK(!g.ok && strstr(err, "512") != 0);
        // a header that claims more than the file holds, and trailing junk after the declared size
        b = atr_sd(40); b[2] = 0xFF; b[3] = 0xFF;
        g = atr_geometry(b.data(), (uint32_t)b.size(), err, sizeof(err));
        CHECK(g.ok && g.sectors == 40);
        b = atr_sd(40); b.insert(b.end(), 500, 0);
        g = atr_geometry(b.data(), (uint32_t)b.size(), err, sizeof(err));
        CHECK(g.ok && g.sectors == 40);
    }
    DONE("test_atari_sio");
}
