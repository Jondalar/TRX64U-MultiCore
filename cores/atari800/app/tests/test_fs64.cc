// T104.1 D16: CoreFs and CoreFile carry sizes and offsets of 64 bit (an HDF on an exFAT stick can be 4 GiB and more), and the code that
// keeps a size in 32 bit takes it saturated (fs_size32), so that a file of 4 GiB + N bytes is never mistaken for one of N bytes.
// The medium is a fake with a SPARSE file: only the bytes the test writes exist, the size is a number.
#include <map>
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "romfind.h"
#include "atari_atr.h"

#include "sparsefs.h"

static const RomSpec specs[1] = {
    { "XL OS", { "DUMMYXL.ROM", 0 }, { 16384, 0 }, true, 0, 0 },
};

int main()
{
    // fs_size32: below 4 GiB the value itself, from there on the largest 32 bit number
    CHECK_EQ(fs_size32(0), 0);
    CHECK_EQ(fs_size32(184320), 184320);
    CHECK_EQ(fs_size32(0xFFFFFFFFull), 0xFFFFFFFFll);
    CHECK_EQ(fs_size32(FOUR_GIB), 0xFFFFFFFFll);
    CHECK_EQ(fs_size32(FOUR_GIB + 184320), 0xFFFFFFFFll);
    CHECK_EQ(sizeof(((FsEntry *)0)->size), 8);

    // a file of 5 GiB + 16 bytes: size, and two markers, 1 GiB and 5 GiB, that a 32 bit offset would confuse
    SparseFs fs;
    fs.put_big("/usb0/big.hdf", 5 * GiB + 16);
    CoreFile *f = fs.open("/usb0/big.hdf", true);
    CHECK(f != 0);
    CHECK_EQ(f->size(), (long long)(5 * GiB + 16));
    uint8_t a[16], b[16], r[16];
    memset(a, 'A', 16);
    memset(b, 'B', 16);
    CHECK(f->write(1 * GiB, a, 16));
    CHECK(f->write(5 * GiB, b, 16));
    CHECK(f->read(5 * GiB, r, 16) && !memcmp(r, b, 16));
    CHECK(f->read(1 * GiB, r, 16) && !memcmp(r, a, 16));
    CHECK(!f->read(5 * GiB + 1, r, 16));                  // past the end
    fs.close(f);

    // the listing and the stat give the real size
    FsEntry e[4];
    int n = fs.list("/usb0", e, 4);
    CHECK_EQ(n, 1);
    CHECK_EQ(e[0].size, (long long)(5 * GiB + 16));
    uint64_t sz = 0;
    bool dir = true;
    CHECK(fs.stat("/usb0/big.hdf", &sz, &dir) && !dir);
    CHECK_EQ(sz, (long long)(5 * GiB + 16));

    // a ROM of 4 GiB + 16384 bytes is not the ROM of 16384 bytes
    SparseFs rf;
    rf.put_big("/usb0/roms/dummy800/dummyxl.rom", FOUR_GIB + 16384);
    RomResult res[1];
    char sum[300];
    RomStatus st = rom_scan(&rf, "dummy800", specs, 1, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_WRONG_SIZE);
    CHECK(strstr(sum, "dummyxl.rom") != 0);
    // ... and the file of the right size is still found
    rf.put_size("/sd/roms/dummy800/dummyxl.rom", 16384, 7);
    st = rom_scan(&rf, "dummy800", specs, 1, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_FOUND);
    CHECK_EQ(res[0].hit.size, 16384);

    // an XFD is 184320 bytes; a file of 4 GiB + 184320 bytes is not one
    SparseFs af;
    af.put_big("/usb0/huge.xfd", FOUR_GIB + 184320);
    char err[200] = "";
    AtariDisk *d = atr_open(&af, "/usb0/huge.xfd", err, sizeof(err));
    CHECK(d == 0);
    CHECK(strlen(err) > 0);
    af.put_big("/usb0/disk.xfd", 184320);
    d = atr_open(&af, "/usb0/disk.xfd", err, sizeof(err));
    CHECK(d != 0);
    CHECK_EQ(d ? d->sectors() : 0, 720);
    delete d;

    DONE("fs64");
}
