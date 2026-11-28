// T104 5.1/5.2 `media_slot: sectors` and T104.1 D16: an HDF served sector by sector through the mailbox, including an image of more than 4 GiB
// (a sparse file behind CoreFs), against the Amiga model. The record is the Amiga's (cores/amiga/app/lib/amiga.cc); the code under test is sectors.cc.
#include "check.h"
#include "amiga_model.h"
#include "sparsefs.h"
#include "engines.h"
#include "ops_glue.h"

static const uint32_t STAGE = AM_W_STAGE;

static void serve(AmigaRig &r, int unit, bool write, uint32_t lba)
{
    r.io.post(unit, write, lba);
    CHECK_EQ(sec_service(&r.c, 16), 1);
    CHECK(!r.io.pending());                              // MBOX_DONE was written: the core's request is answered
}

static std::vector<uint8_t> sector_pattern(uint8_t seed)
{
    std::vector<uint8_t> v(512);
    for (int i = 0; i < 512; i++) v[i] = (uint8_t)(seed + i * 3);
    return v;
}

int main()
{
    // ---- more than 4 GiB: the size reaches the core in sectors, a sector past 4 GiB is the right sector
    {
        SparseFs fs;
        fs.put_big("/usb0/big.hdf", 5 * GiB);
        AmigaRig r(&fs);
        r.io.regs[AM_FEAT] = 0x01;                       // the size register counts sectors
        char msg[200];
        CHECK_EQ(sec_mount(&r.c, 0, "/usb0/big.hdf", msg, sizeof(msg)), 0);
        uint32_t sectors = (uint32_t)(5 * GiB / 512);    // 0x00A00000
        for (int i = 0; i < 4; i++) CHECK_EQ(r.io.regs[AM_HDF_MOUNT + 4 + i], (sectors >> (8 * i)) & 0xFF);
        CHECK_EQ(r.io.regs[AM_HDF_MOUNT], 1);
        int size_at = -1, mount_at = -1;                 // the size is set before the unit is announced
        for (size_t i = 0; i < r.io.log.size(); i++) {
            if (r.io.log[i].reg == AM_HDF_MOUNT + 4 + 2) size_at = (int)i;
            if (r.io.log[i].reg == AM_HDF_MOUNT && r.io.log[i].v == 1) mount_at = (int)i;
        }
        CHECK(size_at >= 0 && mount_at > size_at);
        CHECK_STR(sec_name(&r.c, 0), "big.hdf");

        // two markers a 32 bit offset would confuse: sector 0x800003 is at byte 4 GiB + 1536, and 0x800003 << 9 wraps to 0x1800
        std::vector<uint8_t> hi = sector_pattern(0x10), lo = sector_pattern(0x90);
        CoreFile *f = fs.open("/usb0/big.hdf", true);
        CHECK(f->write(4 * GiB + 3 * 512, hi.data(), 512));
        CHECK(f->write(0x1800, lo.data(), 512));
        fs.close(f);

        memset(&r.io.ddr[STAGE], 0xEE, 512);
        serve(r, 0, false, 0x800003);
        CHECK(!memcmp(&r.io.ddr[STAGE], hi.data(), 512));            // not the sector at 0x1800

        std::vector<uint8_t> wr = sector_pattern(0x55);
        memcpy(&r.io.ddr[STAGE], wr.data(), 512);
        serve(r, 0, true, 0x800007);                                 // a write past 4 GiB lands there ...
        uint8_t back[512];
        f = fs.open("/usb0/big.hdf", true);
        CHECK(f->read(4 * GiB + 7 * 512, back, 512) && !memcmp(back, wr.data(), 512));
        CHECK(f->read(0x3800, back, 512));                           // ... and not at the wrapped offset
        bool zero = true;
        for (int i = 0; i < 512; i++) zero &= back[i] == 0;
        CHECK(zero);
        fs.close(f);

        memset(&r.io.ddr[STAGE], 0xEE, 512);                         // the last sector, and the one after it (zeros, the file ends)
        serve(r, 0, false, (uint32_t)(5 * GiB / 512) - 1);
        serve(r, 0, false, (uint32_t)(5 * GiB / 512));
        zero = true;
        for (int i = 0; i < 512; i++) zero &= r.io.ddr[STAGE + i] == 0;
        CHECK(zero);
    }

    // ---- a core that counts bytes cannot be told the size of an image of 4 GiB and more
    {
        SparseFs fs;
        fs.put_big("/usb0/big.hdf", 5 * GiB);
        fs.put_big("/usb0/three.hdf", 3 * GiB);
        fs.put_big("/usb0/four.hdf", 4 * GiB);
        AmigaRig r(&fs);                                             // FEATURES b0 = 0
        char msg[200];
        CHECK(sec_mount(&r.c, 0, "/usb0/big.hdf", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "4 GiB") != 0);
        CHECK(sec_name(&r.c, 0) == 0);
        CHECK_EQ(r.io.regs[AM_HDF_MOUNT], 0);                         // nothing was announced
        CHECK(sec_mount(&r.c, 0, "/usb0/four.hdf", msg, sizeof(msg)) != 0);   // 4 GiB itself does not fit 32 bit bytes
        CHECK_EQ(sec_mount(&r.c, 1, "/usb0/three.hdf", msg, sizeof(msg)), 0);
        uint32_t bytes = (uint32_t)(3 * GiB);
        for (int i = 0; i < 4; i++) CHECK_EQ(r.io.regs[AM_HDF_MOUNT + 8 + 4 + i], (bytes >> (8 * i)) & 0xFF);
    }

    // ---- two units, small images, refusals
    {
        FakeFs fs;
        fs.put("/usb0/a.hdf", std::vector<uint8_t>(1 << 20, 0));
        fs.put("/usb0/b.hdf", std::vector<uint8_t>(1 << 20, 0));
        fs.put("/usb0/tiny.hdf", std::vector<uint8_t>(100, 0));
        std::vector<uint8_t> pa = sector_pattern(1), pb = sector_pattern(2);
        memcpy(&fs.files["/usb0/a.hdf"][512 * 5], pa.data(), 512);
        memcpy(&fs.files["/usb0/b.hdf"][512 * 5], pb.data(), 512);
        AmigaRig r(&fs);
        r.io.regs[AM_FEAT] = 1;
        char msg[200];
        OpResult res;
        OpIn m0[2] = { in_str("path", "/usb0/a.hdf"), in_int("unit", 0) }, m1[2] = { in_str("path", "/usb0/b.hdf"), in_int("unit", 1) };
        CHECK_EQ(ops_call(&r.c, "sectors.mount", m0, 2, &res), 0);
        CHECK_EQ(ops_call(&r.c, "sectors.mount", m1, 2, &res), 0);
        OpIn bad[2] = { in_str("path", "/usb0/a.hdf"), in_int("unit", 2) };
        CHECK(ops_call(&r.c, "sectors.mount", bad, 2, &res) != 0);
        CHECK(sec_mount(&r.c, 0, "/usb0/tiny.hdf", msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "smaller than one sector") != 0);
        CHECK_STR(sec_name(&r.c, 0), "a.hdf");                       // the old disk is still in
        CHECK_STR(ops_state(&r.c, "amiga.hd1.image"), "b.hdf");
        serve(r, 0, false, 5);
        CHECK(!memcmp(&r.io.ddr[STAGE], pa.data(), 512));
        serve(r, 1, false, 5);                                        // the unit bit of the mailbox picks the file
        CHECK(!memcmp(&r.io.ddr[STAGE], pb.data(), 512));
        // an empty mailbox serves nothing; a request for a unit nobody mounted is answered with zeros
        CHECK_EQ(sec_service(&r.c, 16), 0);
        OpIn ej = in_int("unit", 1);
        CHECK_EQ(ops_call(&r.c, "sectors.eject", &ej, 1, &res), 0);
        CHECK_EQ(r.io.regs[AM_HDF_MOUNT + 8], 0);
        CHECK(sec_name(&r.c, 1) == 0);
        memset(&r.io.ddr[STAGE], 0xEE, 512);
        serve(r, 1, false, 5);
        bool zero = true;
        for (int i = 0; i < 512; i++) zero &= r.io.ddr[STAGE + i] == 0;
        CHECK(zero);
    }

    // ---- a read only image is read, a write to it changes nothing
    {
        FakeFs fs;
        fs.put("/usb0/a.hdf", std::vector<uint8_t>(1 << 20, 0x11));
        fs.read_only = true;
        AmigaRig r(&fs);
        char msg[200];
        CHECK_EQ(sec_mount(&r.c, 0, "/usb0/a.hdf", msg, sizeof(msg)), 0);
        CHECK(strstr(sec_name(&r.c, 0), "read only") != 0);
        memset(&r.io.ddr[STAGE], 0x99, 512);
        serve(r, 0, true, 2);
        CHECK_EQ(fs.files["/usb0/a.hdf"][1024], 0x11);
        CHECK(!r.c.eng->sec.unit[0].unsynced);
        CHECK_EQ(fs.write_attempts, 0);                               // a read only image is not even tried
    }

    // ---- written sectors are synced when the writing stops, and on eject
    {
        SparseFs fs;
        fs.put_big("/usb0/big.hdf", 5 * GiB);
        AmigaRig r(&fs);
        r.io.regs[AM_FEAT] = 1;
        char msg[200];
        sec_mount(&r.c, 0, "/usb0/big.hdf", msg, sizeof(msg));
        memset(&r.io.ddr[STAGE], 0x42, 512);
        serve(r, 0, true, 9);
        r.io.delay_ms(500);
        sec_idle(&r.c);
        CHECK_EQ(fs.file_syncs, 0);
        r.io.delay_ms(600);
        sec_idle(&r.c);
        CHECK_EQ(fs.file_syncs, 1);
        sec_idle(&r.c);
        CHECK_EQ(fs.file_syncs, 1);                                   // once
        serve(r, 0, true, 10);
        CHECK_EQ(sec_eject(&r.c, 0, msg, sizeof(msg)), 0);
        CHECK_EQ(fs.file_syncs, 2);                                   // eject syncs what is unsynced
    }

    // ---- the poll serves the mailbox and asks to be called again at once
    {
        FakeFs fs;
        fs.put("/usb0/a.hdf", std::vector<uint8_t>(1 << 20, 0));
        AmigaRig r(&fs);
        r.io.regs[AM_FEAT] = 1;
        char msg[200];
        sec_mount(&r.c, 0, "/usb0/a.hdf", msg, sizeof(msg));
        r.io.post(0, false, 1);
        const OpDef *d = ops_of(&r.c)->find_op("media.poll");
        CHECK(d != 0);
        OpArgs none;
        OpResult res;
        res.more = false;
        ops_of(&r.c)->bind(d, 0, 0, &none, res.msg, sizeof(res.msg));
        CHECK_EQ(ops_of(&r.c)->run(&r.c, d, none, &res), 0);
        CHECK(res.more);                                              // sector after sector: no sleeping for a tick in between
        CHECK(!r.io.pending());
        CHECK_EQ(ops_of(&r.c)->run(&r.c, d, none, &res), 0);
        CHECK(!res.more);                                             // idle
    }

    // ---- T104 5.14: after Reload core the unit's registers are at their configuration values; the announcement writes the size too, not only the bit
    {
        SparseFs fs;
        fs.put_big("/usb0/a.hdf", 3 * GiB);
        AmigaRig r(&fs);
        r.io.regs[AM_FEAT] = 0x01;
        char msg[200];
        CHECK_EQ(sec_mount(&r.c, 0, "/usb0/a.hdf", msg, sizeof(msg)), 0);
        uint32_t sectors = (uint32_t)(3 * GiB / 512);
        for (int i = 0; i < 4; i++) r.io.regs[AM_HDF_MOUNT + 4 + i] = 0;      // the new core
        r.io.regs[AM_HDF_MOUNT] = 0;
        sec_reannounce(&r.c);
        CHECK_EQ(r.io.regs[AM_HDF_MOUNT], 1);
        for (int i = 0; i < 4; i++) CHECK_EQ(r.io.regs[AM_HDF_MOUNT + 4 + i], (sectors >> (8 * i)) & 0xFF);
    }
    DONE("test_sectors");
}
