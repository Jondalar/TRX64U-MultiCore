// T104.1 D15 scope (Alex 2026-10-22, Mount & Run of a PRG inside a disk image): the read only file system of a .d64 and a .d81 and the folder it
// makes of an image for CoreFs. The disks are made here, by the test, with files of its own - no disk image of anyone else.
#include "check.h"
#include "fakes.h"
#include "d1541_gcr.h"
#include "d1541_fs.h"
#include "disk_build.h"

int main()
{
    for (int pass = 0; pass < 2; pass++) {
        bool is81 = pass == 1;
        Disk d(is81, is81 ? 4 : 3);                      // the 1581's directory chain starts at 40/4 here, as the header says
        Bytes small = pat(10, 1), exact = pat(254, 2), three = pat(600, 3), empty;
        d.add("HELLO", 0x82, small);                     // PRG, closed
        d.add("EXACT", 0x82, exact);
        d.add("THREE SECTORS", 0x82, three);
        d.add("SCRATCHED", 0x02, small);                 // PRG but not closed
        d.add("GONE", 0x00, small);                      // type 0
        d.add("DATA", 0x81, pat(300, 4));                // SEQ
        const uint8_t odd[] = { 'A', '/', 'B', ':', 0xC1, 0xC2, 0x22 };   // a '/', a ':', shifted A and B, a '"'
        d.add(odd, 7, 0x82, small);
        d.add("EMPTY", 0x82, empty);
        d.add("0123456789ABCDEF", 0x82, pat(5, 9));      // 16 characters
        for (int i = 0; i < 9; i++) {                    // the directory needs a second sector
            char nm[16];
            snprintf(nm, sizeof(nm), "F%d", i);
            d.add(nm, 0x82, pat(20 + i, 20 + i));
        }

        FakeFs fs;
        const char *img = is81 ? "/usb0/games/t.d81" : "/usb0/games/t.d64";
        fs.put(img, d.img);
        CoreFile *f = fs.open(img, false);
        DiskReader r(f, is81);
        static DiskEntry e[160];
        int n = r.list(e, 160);
        CHECK_EQ(n, 7 + 9);                              // the scratched, the unclosed are not there
        CHECK_STR(e[0].name, "HELLO.prg");
        CHECK_EQ(e[0].type, 2);
        CHECK_EQ(e[0].blocks, 1);
        CHECK_STR(e[1].name, "EXACT.prg");
        CHECK_STR(e[2].name, "THREE SECTORS.prg");
        CHECK_EQ(e[2].blocks, 3);
        CHECK_STR(e[3].name, "DATA.seq");
        CHECK_EQ(e[3].type, 1);
        CHECK_STR(e[4].name, "A_B_AB_.prg");           // '/' and ':' and the quote cannot be in a path; shifted letters are letters
        CHECK_STR(e[5].name, "EMPTY.prg");
        CHECK_STR(e[6].name, "0123456789ABCDEF.prg");
        CHECK_EQ(e[6].pet_len, 16);
        CHECK_STR(e[7].name, "F0.prg");
        CHECK_STR(e[15].name, "F8.prg");                 // behind the second directory sector
        CHECK_EQ(e[4].pet_len, 7);
        CHECK_EQ(e[4].pet[1], '/');                      // the disk's own name is kept for typing
        CHECK_EQ(e[4].pet[4], 0xC1);
        // sizes and bytes
        CHECK_EQ(r.file_size(e[0].track, e[0].sector), 10);
        CHECK_EQ(r.file_size(e[1].track, e[1].sector), 254);
        CHECK_EQ(r.file_size(e[2].track, e[2].sector), 600);
        CHECK_EQ(r.file_size(e[5].track, e[5].sector), 0);
        Bytes got(700);
        CHECK_EQ(r.read_file(e[2].track, e[2].sector, got.data(), 700), 600);
        CHECK(!memcmp(got.data(), three.data(), 600));
        CHECK_EQ(r.read_file(e[0].track, e[0].sector, got.data(), 700), 10);
        CHECK(!memcmp(got.data(), small.data(), 10));
        CHECK_EQ(r.read_file(e[1].track, e[1].sector, got.data(), 700), 254);
        CHECK(!memcmp(got.data(), exact.data(), 254));
        CHECK_EQ(r.read_file(e[2].track, e[2].sector, got.data(), 599), -1);   // does not fit
        // a chain that leaves the disk, and one that loops
        {
            Disk b(is81);
            b.add("BAD", 0x82, pat(700, 5));
            int t = b.sec(is81 ? 40 : 18, is81 ? 3 : 1)[3], s = b.sec(is81 ? 40 : 18, is81 ? 3 : 1)[4];
            b.sec(t, s)[0] = 99;                         // the first sector links to track 99
            fs.put("/b.img.d64", b.img);
            CoreFile *bf = fs.open("/b.img.d64", false);
            DiskReader br(bf, is81);
            CHECK_EQ(br.file_size(t, s), -1);
            CHECK_EQ(br.read_file(t, s, got.data(), 700), -1);
            fs.close(bf);
            Disk l(is81);
            l.add("LOOP", 0x82, pat(700, 6));
            int lt = l.sec(is81 ? 40 : 18, is81 ? 3 : 1)[3], ls = l.sec(is81 ? 40 : 18, is81 ? 3 : 1)[4];
            l.sec(lt, ls)[0] = (uint8_t)lt; l.sec(lt, ls)[1] = (uint8_t)ls;        // links to itself
            fs.put("/l.img.d64", l.img);
            CoreFile *lf = fs.open("/l.img.d64", false);
            DiskReader lr(lf, is81);
            CHECK_EQ(lr.file_size(lt, ls), -1);
            fs.close(lf);
        }
        fs.close(f);

        // ---- the folder ----
        DiskFs dfs(&fs);
        fs.put("/usb0/games/readme.txt", Bytes(3, 1));
        FsEntry fe[64];
        CHECK_EQ(dfs.list(img, fe, 64), 16);
        CHECK_STR(fe[0].name, "HELLO.prg");
        CHECK(!fe[0].dir);
        CHECK_EQ(fe[2].size, (uint64_t)3 * 254);
        CHECK_EQ(dfs.list(std::string(std::string(img) + "/").c_str(), fe, 64), 16);   // a trailing slash is the same folder
        CHECK_EQ(dfs.list(std::string(std::string(img) + "/HELLO.prg").c_str(), fe, 64), -1);   // an image has no folders below it
        int dn = dfs.list("/usb0/games", fe, 64);                                        // an ordinary folder is the stick's
        CHECK(dn >= 2);
        bool saw_img = false;
        for (int i = 0; i < dn; i++) saw_img = saw_img || !strcmp(fe[i].name, is81 ? "t.d81" : "t.d64");
        CHECK(saw_img);
        // stat, load, open
        uint64_t sz = 0; bool isdir = true;
        std::string in = std::string(img) + "/THREE SECTORS.prg";
        CHECK(dfs.stat(in.c_str(), &sz, &isdir));
        CHECK_EQ(sz, 600u);
        CHECK(!isdir);
        CHECK(dfs.stat(img, &sz, &isdir) && !isdir);                                     // the image is still a file
        CHECK(!dfs.stat((std::string(img) + "/NOPE.prg").c_str(), &sz, &isdir));
        Bytes buf(700);
        CHECK_EQ(dfs.load(in.c_str(), buf.data(), 700), 600);
        CHECK(!memcmp(buf.data(), three.data(), 600));
        CHECK_EQ(dfs.load(in.c_str(), buf.data(), 100), -1);
        CoreFile *vf = dfs.open(in.c_str(), false);
        CHECK(vf != 0);
        if (vf) {
            CHECK_EQ(vf->size(), 600u);
            CHECK(!vf->writable());
            uint8_t tail[4];
            CHECK(vf->read(596, tail, 4) && !memcmp(tail, &three[596], 4));
            CHECK(!vf->read(598, tail, 4));
            CHECK(!vf->write(0, tail, 1));
            dfs.close(vf);
        }
        CoreFile *vw = dfs.open(in.c_str(), true);                                       // a request to write gets a file that cannot be
        CHECK(vw != 0 && !vw->writable());
        if (vw) dfs.close(vw);
        CHECK(dfs.open((std::string(img) + "/NOPE.prg").c_str(), false) == 0);
        // an odd name is found by the name the browser showed
        CHECK(dfs.stat((std::string(img) + "/A_B_AB_.prg").c_str(), &sz, &isdir) && sz == 10);
        // case does not matter, as on the stick
        CHECK(dfs.stat((std::string(img) + "/hello.PRG").c_str(), &sz, &isdir) && sz == 10);
        // read only
        CHECK(!dfs.save(in.c_str(), buf.data(), 10));
        CHECK(!dfs.remove(in.c_str()));
        CHECK(dfs.save("/usb0/games/new.txt", buf.data(), 10) && fs.has("/usb0/games/new.txt"));    // the stick is not
        CHECK(dfs.remove("/usb0/games/new.txt"));
        // the ordinary files of the stick open as before
        CoreFile *tf = dfs.open("/usb0/games/readme.txt", false);
        CHECK(tf != 0 && tf->size() == 3);
        if (tf) dfs.close(tf);
        CHECK_EQ(dfs.load("/usb0/games/readme.txt", buf.data(), 700), 3);
        // a FOLDER that happens to be called x.d64 is a folder
        fs.put("/usb0/folder.d64/inside.txt", Bytes(5, 2));
        CHECK_EQ(dfs.list("/usb0/folder.d64", fe, 64), 1);
        CHECK_STR(fe[0].name, "inside.txt");
        // more than the open-file table: closing frees the slots
        for (int k = 0; k < 10; k++) {
            CoreFile *a = dfs.open(in.c_str(), false);
            CHECK(a != 0);
            if (a) dfs.close(a);
        }
    }

    // ---- paths
    {
        char image[100];
        const char *entry = 0;
        CHECK(disk_split("/usb0/a/b.d64", image, sizeof(image), &entry) && !strcmp(image, "/usb0/a/b.d64") && !*entry);
        CHECK(disk_split("/usb0/a/b.D81/X Y.prg", image, sizeof(image), &entry) && !strcmp(image, "/usb0/a/b.D81") && !strcmp(entry, "X Y.prg"));
        CHECK(!disk_split("/usb0/a/b.d64x/c.prg", image, sizeof(image), &entry));
        CHECK(!disk_split("/usb0/a/b.d6/c.prg", image, sizeof(image), &entry));
        CHECK(!disk_split("/usb0/a/b.d64.bak", image, sizeof(image), &entry));
        CHECK(!disk_split("/usb0/a/plain.prg", image, sizeof(image), &entry));
        CHECK(!disk_split("/usb0/a/b.d64/c.prg", image, 5, &entry));            // too long for the buffer
        // the browser's name of a PETSCII name
        char nm[24];
        const uint8_t p[] = { 'M', 'O', 'N', 'Y', 0xC9, 0x2F };
        disk_display_name(p, 6, 2, nm, sizeof(nm));
        CHECK_STR(nm, "MONYI_.prg");
        disk_display_name(p, 0, 3, nm, sizeof(nm));
        CHECK_STR(nm, "_.usr");
    }

    DONE("d1541_fs");
}
