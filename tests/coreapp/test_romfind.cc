// T93 S2 and D5: the ROM manager - roots in order, names case-insensitive, sizes, the message that names files and roots, the
// retry policy, and (with the boot sequence) a core that is never released with a missing required ROM.
#include <string>
#include "check.h"
#include "fakes.h"
#include "romfind.h"

static bool put_dummy(CoreCtx *, const uint8_t *, uint32_t, char *, int) { return 0; }
static const RomSpec specs[2] = {
    { "XL OS", { "ATARIXL.ROM", 0 },               { 16384, 0 }, true,  0, 0 },
    { "BASIC", { "BASIC.ROM", "ATARIBAS.ROM", 0 }, { 8192, 0 },  true,  0, 0 },
};

int main()
{
    FakeFs fs;
    RomResult res[2];
    char sum[300];

    // nothing anywhere: the message names the file and every root searched
    RomStatus st = rom_scan(&fs, "atari800", specs, 2, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_MISSING);
    CHECK(strstr(sum, "ATARIXL.ROM") != 0);
    for (int r = 0; rom_roots[r]; r++) {
        std::string root = std::string(rom_roots[r]) + "/roms/atari800";
        CHECK(strstr(sum, root.c_str()) != 0);
    }

    // only a USB stick has them, in odd case; BASIC under its second name
    fs.put_size("/USB1/ROMS/Atari800/AtariXL.rom", 16384, 1);
    fs.put_size("/usb1/roms/atari800/ataribas.ROM", 8192, 2);
    st = rom_scan(&fs, "atari800", specs, 2, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_FOUND);
    CHECK_STR(sum, "");
    CHECK_STR(res[0].hit.dir, "/usb1/roms/atari800");
    CHECK_STR(res[1].hit.name, "ataribas.ROM");
    CHECK_EQ(res[0].hit.size, 16384);

    // the flash comes before the stick, the stick before the card
    fs.put_size("/sd/roms/atari800/atarixl.rom", 16384, 3);
    fs.put_size("/flash/roms/atari800/BASIC.ROM", 8192, 4);
    rom_scan(&fs, "atari800", specs, 2, res, sum, sizeof(sum));
    CHECK_STR(res[0].hit.dir, "/usb1/roms/atari800");     // usb1 before sd
    CHECK_STR(res[1].hit.dir, "/flash/roms/atari800");    // flash before usb1
    CHECK_STR(res[1].hit.name, "BASIC.ROM");              // and a file of the first name beats the second name in the same root

    // a file of the right name and the wrong size is refused by name and size, with the sizes expected
    FakeFs fs2;
    fs2.put_size("/usb0/roms/atari800/atarixl.rom", 8192);
    fs2.put_size("/usb0/roms/atari800/basic.rom", 8192);
    st = rom_scan(&fs2, "atari800", specs, 2, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_WRONG_SIZE);
    CHECK(strstr(sum, "atarixl.rom") != 0 && strstr(sum, "8192 bytes") != 0 && strstr(sum, "16384") != 0);
    CHECK_EQ(res[0].status, ROM_WRONG_SIZE);
    CHECK_EQ(res[1].status, ROM_FOUND);
    // ... but a right file in a later root wins over a wrong one in an earlier root
    fs2.put_size("/sd/roms/atari800/atarixl.rom", 16384);
    st = rom_scan(&fs2, "atari800", specs, 2, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_FOUND);
    CHECK_STR(res[0].hit.dir, "/sd/roms/atari800");

    // a content check refuses a file of the right size
    static const RomSpec chk[1] = { { "Checked", { "A.ROM", 0 }, { 4, 0 }, true, [](const uint8_t *d, uint32_t) { return d[0] == 0x42; }, 0 } };
    FakeFs fs3;
    fs3.put("/usb0/roms/x/a.rom", std::vector<uint8_t>{1, 2, 3, 4});
    st = rom_scan(&fs3, "x", chk, 1, res, sum, sizeof(sum));
    CHECK_EQ(st, ROM_BAD_CONTENT);
    CHECK(strstr(sum, "a.rom") != 0);
    fs3.put("/sd/roms/x/a.rom", std::vector<uint8_t>{0x42, 2, 3, 4});
    CHECK_EQ(rom_scan(&fs3, "x", chk, 1, res, sum, sizeof(sum)), ROM_FOUND);

    // a ROM that is not required and not there is fine
    static const RomSpec opt[1] = { { "Optional", { "O.ROM", 0 }, { 4, 0 }, false, 0, 0 } };
    FakeFs fs4;
    CHECK_EQ(rom_scan(&fs4, "x", opt, 1, res, sum, sizeof(sum)), ROM_FOUND);
    CHECK_EQ(res[0].status, ROM_ABSENT);
    CHECK_STR(res[0].why, "");

    // T185 S3: an optional file that is there but of the wrong size, or refused by the check, is not silently absent: the status and the reason say so,
    // the scan stays ROM_FOUND (the worst status is the required ROMs') and the summary stays empty
    static const RomSpec optc[2] = { { "Optional", { "O.ROM", 0 }, { 4, 0 }, false, [](const uint8_t *d, uint32_t) { return d[0] == 0x42; }, 0 },
                                     { "Needed", { "N.ROM", 0 }, { 2, 0 }, true, 0, 0 } };
    FakeFs fs4b;
    fs4b.put("/sd/roms/x/n.rom", std::vector<uint8_t>{1, 2});
    fs4b.put("/sd/roms/x/o.rom", std::vector<uint8_t>{1, 2, 3});
    CHECK_EQ(rom_scan(&fs4b, "x", optc, 2, res, sum, sizeof(sum)), ROM_FOUND);
    CHECK_EQ(res[0].status, ROM_WRONG_SIZE);
    CHECK(strstr(res[0].why, "o.rom") != 0 && strstr(res[0].why, "3 bytes") != 0 && strstr(res[0].why, "4 bytes are expected") != 0);
    CHECK_STR(sum, "");
    CHECK_EQ(res[1].status, ROM_FOUND);
    CHECK_STR(res[1].why, "");
    fs4b.put("/sd/roms/x/o.rom", std::vector<uint8_t>{1, 2, 3, 4});
    CHECK_EQ(rom_scan(&fs4b, "x", optc, 2, res, sum, sizeof(sum)), ROM_FOUND);
    CHECK_EQ(res[0].status, ROM_BAD_CONTENT);
    CHECK(strstr(res[0].why, "o.rom") != 0 && strstr(res[0].why, "not a Optional image") != 0);
    fs4b.put("/sd/roms/x/o.rom", std::vector<uint8_t>{0x42, 2, 3, 4});
    CHECK_EQ(rom_scan(&fs4b, "x", optc, 2, res, sum, sizeof(sum)), ROM_FOUND);
    CHECK_EQ(res[0].status, ROM_FOUND);
    CHECK_STR(res[0].why, "");

    // the wait policy: a missing file is told after 10 s (20 passes of 0.5 s), once; inserting the stick ends the wait
    RomWait w;
    CHECK_EQ(w.delay_ms(), 500);
    int told_at = -1, told_n = 0;
    FakeFs fs5;
    for (int pass = 1; pass <= 40; pass++) {
        if (pass == 25) {
            fs5.put_size("/usb0/roms/atari800/atarixl.rom", 16384);
            fs5.put_size("/usb0/roms/atari800/basic.rom", 8192);
        }
        st = rom_scan(&fs5, "atari800", specs, 2, res, sum, sizeof(sum));
        if (st == ROM_FOUND) {
            CHECK_EQ(pass, 25);                      // found at the first pass after the stick went in
            CHECK(!w.step(st, sum));
            break;
        }
        if (w.step(st, sum)) {
            told_n++;
            if (told_at < 0) told_at = pass;
        }
    }
    CHECK_EQ(told_at, 20);                          // 20 passes x 0.5 s = 10 s
    CHECK_EQ(told_n, 1);                            // not told again every pass
    CHECK(!w.told());                               // reset by the found pass
    // a wrong file is told at once, and again when the text changes
    RomWait w2;
    CHECK(w2.step(ROM_WRONG_SIZE, "atarixl.rom is 10 bytes"));
    CHECK(!w2.step(ROM_WRONG_SIZE, "atarixl.rom is 10 bytes"));
    CHECK(w2.step(ROM_WRONG_SIZE, "atarixl.rom is 20 bytes"));
    // the medium pulled mid-session: the listing fails, the ROM is missing again
    fs5.gone = true;
    CHECK_EQ(rom_scan(&fs5, "atari800", specs, 2, res, sum, sizeof(sum)), ROM_MISSING);

    // ROM sets (T91, T92): a machine with variants starts when ONE set is complete; a set that is only partly there does not count
    static const RomSpec sets[5] = {
        { "A OS",    { "a-os.rom", 0 },    { 4, 0 }, false, 0, 0, 1 },
        { "A BASIC", { "a-basic.rom", 0 }, { 4, 0 }, false, 0, 0, 1 },
        { "B OS",    { "b-os.rom", 0 },    { 4, 0 }, false, 0, 0, 2 },
        { "B BASIC", { "b-basic.rom", 0 }, { 4, 0 }, false, 0, 0, 2 },
        { "Always",  { "always.rom", 0 },  { 4, 0 }, true,  0, 0 },
    };
    RomResult sr[5];
    FakeFs fs6;
    fs6.put_size("/usb0/roms/m/always.rom", 4);
    st = rom_scan(&fs6, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_MISSING);                                  // no set at all
    CHECK(strstr(sum, "A OS") != 0 && strstr(sum, "One complete set") != 0);
    fs6.put_size("/usb0/roms/m/a-os.rom", 4);
    fs6.put_size("/usb0/roms/m/b-basic.rom", 4);                // one half of A, one half of B: neither is complete
    st = rom_scan(&fs6, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_MISSING);
    CHECK(strstr(sum, "A BASIC") != 0);                         // the first incomplete set's first missing file is named
    CHECK_EQ(sr[0].status, ROM_FOUND);
    CHECK_EQ(sr[3].status, ROM_FOUND);
    fs6.put_size("/usb0/roms/m/b-os.rom", 4);                   // B is whole
    st = rom_scan(&fs6, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_FOUND);
    CHECK_STR(sum, "");
    CHECK_EQ(sr[1].status, ROM_MISSING);                        // A BASIC is still not there; B runs
    FakeFs fs7;                                                 // a set member of the wrong size: told by size, and the required file still counts
    fs7.put_size("/usb0/roms/m/a-os.rom", 3);
    fs7.put_size("/usb0/roms/m/always.rom", 4);
    st = rom_scan(&fs7, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_WRONG_SIZE);
    CHECK(strstr(sum, "a-os.rom") != 0 && strstr(sum, "3 bytes") != 0);
    FakeFs fs9;                                                 // B lacks one file, A lacks two: the sentence is about B, the closer set
    fs9.put_size("/usb0/roms/m/always.rom", 4);
    fs9.put_size("/usb0/roms/m/b-os.rom", 4);
    st = rom_scan(&fs9, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_MISSING);
    CHECK(strstr(sum, "B BASIC") != 0 && strstr(sum, "A OS") == 0);
    fs7.put_size("/usb0/roms/m/a-os.rom", 4);
    fs7.put_size("/usb0/roms/m/a-basic.rom", 4);
    fs7.remove("/usb0/roms/m/always.rom");                      // a plain required ROM is judged on its own, whatever the sets say
    st = rom_scan(&fs7, "m", sets, 5, sr, sum, sizeof(sum));
    CHECK_EQ(st, ROM_MISSING);
    CHECK(strstr(sum, "Always") != 0);
    // a ROM two variants share has two bits: set 1 is X1 + Shared, set 2 is X2 + Shared
    static const RomSpec shared[3] = {
        { "X1",     { "x1.rom", 0 },     { 4, 0 }, false, 0, 0, 1 },
        { "X2",     { "x2.rom", 0 },     { 4, 0 }, false, 0, 0, 2 },
        { "Shared", { "shared.rom", 0 }, { 4, 0 }, false, 0, 0, 3 },
    };
    RomResult shr[3];
    FakeFs fs8;
    fs8.put_size("/usb0/roms/s/x1.rom", 4);
    fs8.put_size("/usb0/roms/s/x2.rom", 4);
    CHECK_EQ(rom_scan(&fs8, "s", shared, 3, shr, sum, sizeof(sum)), ROM_MISSING);       // both variants lack the shared file
    CHECK(strstr(sum, "Shared") != 0);
    fs8.put_size("/usb0/roms/s/shared.rom", 4);
    CHECK_EQ(rom_scan(&fs8, "s", shared, 3, shr, sum, sizeof(sum)), ROM_FOUND);
    fs8.remove("/usb0/roms/s/x1.rom");
    CHECK_EQ(rom_scan(&fs8, "s", shared, 3, shr, sum, sizeof(sum)), ROM_FOUND);          // set 2 is whole
    fs8.remove("/usb0/roms/s/x2.rom");
    CHECK_EQ(rom_scan(&fs8, "s", shared, 3, shr, sum, sizeof(sum)), ROM_MISSING);        // Shared alone is no set

    // T148 D2: with the core's folder named, that folder is the only place (the roots, first_dir and the shared folders are not listed)
    char own[96];
    CHECK(rom_own_dir("/USB1/cores/apple2/apple2.cor", own, sizeof(own)));
    CHECK_STR(own, "/USB1/cores/apple2/apple2/roms");
    FakeFs fa;
    fa.put_size("/USB1/cores/apple2/apple2/roms/atarixl.rom", 16384, 7);
    fa.put_size("/USB1/cores/apple2/apple2/roms/basic.rom", 8192, 7);
    st = rom_scan(&fa, "atari800", specs, 2, res, sum, sizeof(sum), 0, own);
    CHECK_EQ(st, ROM_FOUND);
    CHECK_STR(res[0].hit.dir, "/USB1/cores/apple2/apple2/roms");
    CHECK_EQ(rom_scan(&fa, "atari800", specs, 2, res, sum, sizeof(sum)), ROM_MISSING);      // without the folder named, the same files are not found
    fa.put_size("/usb0/roms/atari800/atarixl.rom", 16384, 1);
    fa.put_size("/board/atarixl.rom", 16384, 3);
    st = rom_scan(&fa, "atari800", specs, 2, res, sum, sizeof(sum), "/board", own);
    CHECK_STR(res[0].hit.dir, "/USB1/cores/apple2/apple2/roms");
    // the old places are not read: a file there does not make a core run
    FakeFs fb;
    fb.put_size("/usb1/roms/atari800/atarixl.rom", 16384);
    fb.put_size("/usb1/roms/atari800/basic.rom", 8192);
    fb.put_size("/board/atarixl.rom", 16384);
    CHECK_EQ(rom_scan(&fb, "atari800", specs, 2, res, sum, sizeof(sum), "/board", own), ROM_MISSING);
    CHECK_EQ(res[0].status, ROM_MISSING);
    // a wrong-size file in the folder is told with the folder, even when a right file lies in an old place
    FakeFs fc;
    fc.put_size("/USB1/cores/apple2/apple2/roms/atarixl.rom", 100);
    fc.put_size("/sd/roms/atari800/atarixl.rom", 16384);
    fc.put_size("/sd/roms/atari800/basic.rom", 8192);
    CHECK_EQ(rom_scan(&fc, "atari800", specs, 2, res, sum, sizeof(sum), 0, own), ROM_WRONG_SIZE);
    CHECK(strstr(sum, "/USB1/cores/apple2/apple2/roms") != 0 && strstr(sum, "100 bytes") != 0 && !strstr(sum, "/sd/roms"));
    // a shared (device) ROM is not looked for in <root>/roms either
    static const RomSpec drv[1] = { { "Drive", { "d1541.rom", 0 }, { 4, 0 }, true, 0, 0, 0, 1 } };
    FakeFs ff;
    ff.put_size("/usb0/roms/d1541.rom", 4, 1);
    CHECK_EQ(rom_scan(&ff, "c264", drv, 1, res, sum, sizeof(sum), 0, 0), ROM_FOUND);      // the old search (no .cor path) still reads it
    CHECK_STR(res[0].hit.dir, "/usb0/roms");
    CHECK(rom_own_dir("/USB1/cores/c264/c264.cor", own, sizeof(own)));
    CHECK_EQ(rom_scan(&ff, "c264", drv, 1, res, sum, sizeof(sum), 0, own), ROM_MISSING);
    ff.put_size("/USB1/cores/c264/c264/roms/d1541.rom", 4, 2);
    CHECK_EQ(rom_scan(&ff, "c264", drv, 1, res, sum, sizeof(sum), 0, own), ROM_FOUND);
    CHECK_STR(res[0].hit.dir, "/USB1/cores/c264/c264/roms");
    // the path: no .cor, relative, no medium, too long for the buffer
    CHECK(!rom_own_dir("/x.cor", own, sizeof(own)));
    CHECK_STR(own, "");
    CHECK(!rom_own_dir("", own, sizeof(own)));
    CHECK(!rom_own_dir("cores/x/x.cor", own, sizeof(own)));
    CHECK(!rom_own_dir("/USB1/cores/apple2/apple2.cor", own, 10));
    DONE("test_romfind");
}
