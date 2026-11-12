// T104 5.1/5.2 `media_slot: window`: a raw image in a window slot of the DDR2 with the dirty-track write-back, against a model of the Amiga
// core's log (amiga_model.h). The record is the Amiga's real one (coreapp/cores/amiga.cc); the code under test is coreapp/src/winslot.cc.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "ops_glue.h"

static std::vector<uint8_t> adf(uint32_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; i++) {
        v[i] = (uint8_t)(i * seed + (i >> 7));
    }
    return v;
}

static const uint32_t SLOT1 = AM_W_ADF + 1 * AM_ADF_STRIDE;

int main()
{
    // ---- mount through the operation: the image in its slot, the rest of the stride zero, announced
    {
        FakeFs fs;
        std::vector<uint8_t> img = adf(AM_ADF_SIZE, 3);
        fs.put("/usb0/a.adf", img);
        AmigaRig r(&fs);
        memset(&r.io.ddr[SLOT1], 0xEE, AM_ADF_STRIDE);                 // what the DDR2 held before
        OpIn in[2] = { in_str("path", "/usb0/a.adf"), in_int("slot", 1) };
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "window.mount", in, 2, &res), 0);
        CHECK(!memcmp(&r.io.ddr[SLOT1], img.data(), img.size()));
        bool zero = true;
        for (uint32_t i = img.size(); i < AM_ADF_STRIDE; i++) zero &= r.io.ddr[SLOT1 + i] == 0;
        CHECK(zero);                                                    // zero-filled over the full stride
        CHECK_EQ(r.io.regs[AM_FLOPPY_MOUNT + 0x10], 1);
        CHECK_EQ(r.io.regs[AM_FLOPPY_MOUNT], 0);
        CHECK_STR(win_name(&r.c, 1), "a.adf");
        CHECK(win_name(&r.c, 0) == 0);
        CHECK_STR(ops_state(&r.c, "amiga.df1.image"), "a.adf");
        CHECK(ops_state(&r.c, "amiga.df0.image") == 0);
        // a slot or an argument out of range never reaches the handler
        OpIn bad[2] = { in_str("path", "/usb0/a.adf"), in_int("slot", 4) };
        CHECK(ops_call(&r.c, "window.mount", bad, 2, &res) != 0);
    }

    // ---- refusals leave the disk that is in
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 3));
        fs.put("/usb0/big.adf", adf(AM_ADF_SIZE + 1, 3));
        fs.put("/usb0/empty.adf", std::vector<uint8_t>());
        AmigaRig r(&fs);
        char msg[160];
        CHECK_EQ(win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        CHECK(win_mount(&r.c, 0, "/usb0/big.adf", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "901120") != 0);
        CHECK(win_mount(&r.c, 0, "/usb0/empty.adf", msg, sizeof(msg)) != 0);
        CHECK(win_mount(&r.c, 0, "/usb0/missing.adf", msg, sizeof(msg)) != 0);
        CHECK_STR(win_name(&r.c, 0), "a.adf");                          // the old disk is still in
        CHECK_EQ(r.io.regs[AM_FLOPPY_MOUNT], 1);
    }

    // ---- write-back: only the tracks the core logged, only after the burst is over
    {
        FakeFs fs;
        std::vector<uint8_t> img = adf(AM_ADF_SIZE, 5);
        fs.put("/usb0/a.adf", img);
        AmigaRig r(&fs);
        char msg[160];
        CHECK_EQ(win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        std::vector<uint8_t> want = img;
        for (int t : { 5, 159 }) {                                      // the guest writes two tracks into its window ...
            for (uint32_t i = 0; i < AM_ADF_TRACK_BYTES; i++) {
                uint8_t b = (uint8_t)(0x40 + t + i);
                r.io.ddr[AM_W_ADF + t * AM_ADF_TRACK_BYTES + i] = b;
                want[t * AM_ADF_TRACK_BYTES + i] = b;
            }
            r.io.log_track(0, t);                                       // ... and the core logs them
        }
        r.io.delay_ms(10);
        win_service(&r.c);
        CHECK_EQ(r.io.lost, 0);                                         // every pop waited for the head to move on
        CHECK_EQ(r.io.pops, 2);
        CHECK_EQ(r.c.eng->win.slot[0].ndirty, 2);
        CHECK_EQ(fs.writes, 0);                                         // a burst may be going on: nothing is written yet
        r.io.delay_ms(500);
        win_service(&r.c);
        CHECK_EQ(fs.writes, 0);
        r.io.delay_ms(600);                                             // 1 s without a new entry
        win_service(&r.c);
        CHECK_EQ(fs.writes, 2);
        CHECK_EQ(fs.syncs, 1);
        CHECK(fs.files["/usb0/a.adf"] == want);                         // the two tracks changed and nothing else
        CHECK_EQ(r.c.eng->win.slot[0].ndirty, 0);
    }

    // ---- a drive that is written all the time is flushed after 5 s anyway
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 5));
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg));
        int at = -1;
        for (int i = 0; i < 20; i++) {
            r.io.log_track(0, i);
            r.io.delay_ms(400);
            win_service(&r.c);
            if (fs.writes > 0 && at < 0) at = i;
        }
        CHECK(at >= 11 && at <= 13);                                    // 5 s of 400 ms steps
    }

    // ---- a log that overflowed has lost entries: every track of a writable drive is suspect
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 5));
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg));
        r.io.overflow = true;
        win_service(&r.c);
        CHECK(!r.io.overflow);                                          // the overflow bit was cleared
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK_EQ(fs.writes, 160);
    }

    // ---- a read only file or medium: never marked, never written
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 5));
        fs.read_only = true;
        AmigaRig r(&fs);
        char msg[160];
        CHECK_EQ(win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg)), 0);
        r.io.log_track(0, 7);
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(6000);
        win_service(&r.c);
        CHECK_EQ(fs.writes, 0);
        CHECK_EQ(r.c.eng->win.slot[0].ndirty, 0);                       // not even marked: no write-back is tried, so none fails
        CHECK(!r.c.notice_pending);
        CHECK(strstr(win_name(&r.c, 0), "read only") != 0);
    }

    // ---- a disk smaller than the track: the track beyond the file has nothing to be written to
    {
        FakeFs fs;
        fs.put("/usb0/s.adf", adf(368640, 9));
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 0, "/usb0/s.adf", msg, sizeof(msg));
        r.io.log_track(0, 100);
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK_EQ(fs.writes, 0);
        CHECK_EQ(fs.files["/usb0/s.adf"].size(), 368640);
        CHECK_EQ(r.c.eng->win.slot[0].ndirty, 0);
    }

    // ---- eject and a new disk flush the old one first
    {
        FakeFs fs;
        std::vector<uint8_t> a = adf(AM_ADF_SIZE, 5), b = adf(AM_ADF_SIZE, 11);
        fs.put("/usb0/a.adf", a);
        fs.put("/usb0/b.adf", b);
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg));
        r.io.ddr[AM_W_ADF + 3 * AM_ADF_TRACK_BYTES] = 0x77;
        r.io.log_track(0, 3);
        r.io.delay_ms(10);
        win_service(&r.c);
        CHECK_EQ(win_mount(&r.c, 0, "/usb0/b.adf", msg, sizeof(msg)), 0);   // the slot gets another disk
        CHECK_EQ(fs.files["/usb0/a.adf"][3 * AM_ADF_TRACK_BYTES], 0x77);    // a.adf got its track before it was replaced
        CHECK_EQ(r.io.ddr[AM_W_ADF], b[0]);
        r.io.ddr[AM_W_ADF + 4 * AM_ADF_TRACK_BYTES] = 0x55;
        r.io.log_track(0, 4);
        r.io.delay_ms(10);
        win_service(&r.c);
        OpIn in = in_int("slot", 0);
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "window.eject", &in, 1, &res), 0);
        CHECK_EQ(fs.files["/usb0/b.adf"][4 * AM_ADF_TRACK_BYTES], 0x55);
        CHECK_EQ(r.io.regs[AM_FLOPPY_MOUNT], 0);
        CHECK(win_name(&r.c, 0) == 0);
    }

    // ---- a file that cannot be written: told once, the image stays in memory, and it recovers
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 5));
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 0, "/usb0/a.adf", msg, sizeof(msg));
        fs.sync_fails = true;
        r.io.log_track(0, 2);
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK(r.c.notice_pending && strstr(r.c.notice, "DF0") && strstr(r.c.notice, "writing the disk back"));
        r.c.notice_pending = false;
        r.io.log_track(0, 3);                                           // fails again: not told again
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK(!r.c.notice_pending);
        fs.sync_fails = false;                                          // the medium is back: the next write-back succeeds ...
        r.io.log_track(0, 4);
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK(!r.c.notice_pending);
        fs.sync_fails = true;                                           // ... and a later failure is told again
        r.io.log_track(0, 8);
        r.io.delay_ms(10);
        win_service(&r.c);
        r.io.delay_ms(1100);
        win_service(&r.c);
        CHECK(r.c.notice_pending);
    }

    // ---- announced again after a reset, only what is mounted; the poll operation serves the log too
    {
        FakeFs fs;
        fs.put("/usb0/a.adf", adf(AM_ADF_SIZE, 5));
        AmigaRig r(&fs);
        char msg[160];
        win_mount(&r.c, 2, "/usb0/a.adf", msg, sizeof(msg));
        int before = r.io.mount_writes[2];
        int others = r.io.mount_writes[0] + r.io.mount_writes[1] + r.io.mount_writes[3];
        win_reannounce(&r.c);
        CHECK_EQ(r.io.mount_writes[2], before + 1);
        CHECK_EQ(r.io.mount_writes[0] + r.io.mount_writes[1] + r.io.mount_writes[3], others);     // the empty drives are not announced
        r.io.ddr[AM_W_ADF + 2 * AM_ADF_STRIDE + 9 * AM_ADF_TRACK_BYTES] = 0x33;
        r.io.log_track(2, 9);
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "media.poll", 0, 0, &res), 0);          // marks it
        r.io.delay_ms(1100);
        CHECK_EQ(ops_call(&r.c, "media.poll", 0, 0, &res), 0);          // writes it
        CHECK_EQ(fs.files["/usb0/a.adf"][9 * AM_ADF_TRACK_BYTES], 0x33);
    }
    DONE("test_winslot");
}
