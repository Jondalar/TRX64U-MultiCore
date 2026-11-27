// Host test of the boot app's menu file (host-app/software/hostapp/bootcfg.cc).
#include "bootcfg.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main()
{
    BootCfg c;
    bootcfg_init(&c);
    CHECK(c.countdown == 5 && c.def == -1 && c.n == 0);             // T84 D4a: 5 s
    CHECK(bootcfg_add(&c, "TRX64 (C64)", "/SD/cores/trx64.cor", "aa", 1234));
    CHECK(c.def == 0);                                              // the first entry becomes the default
    CHECK(bootcfg_add(&c, "VIC-20", "/Usb1/vc20.cor", "bb"));
    CHECK(!bootcfg_add(&c, "bad|name", "/x.cor", "cc"));
    CHECK(bootcfg_add(&c, "VIC-20 PAL", "/Usb1/vc20.cor", "dd") && c.n == 2 && !strcmp(c.e[1].name, "VIC-20") && !strcmp(c.e[1].hash, "dd"));   // T198 D17: the name stays
    bootcfg_move(&c, 0, +1);
    CHECK(!strcmp(c.e[1].name, "TRX64 (C64)") && c.def == 1);       // the default follows its entry
    char buf[2048];
    int n = bootcfg_format(&c, buf, sizeof(buf));
    CHECK(n > 0);
    BootCfg d;
    bootcfg_parse(&d, buf);
    CHECK(d.n == 2 && d.def == 1 && d.countdown == 5 && !strcmp(d.e[0].path, "/Usb1/vc20.cor") && !strcmp(d.e[1].hash, "aa") && d.e[1].size == 1234 && d.e[0].size == 0);
    bootcfg_remove(&d, 1);
    CHECK(d.n == 1 && d.def == 0);
    // T101 S10: mtime and header digest survive a round trip; an older line (3 or 4 fields) reads as "never checked"
    CHECK(bootcfg_add(&c, "Checked", "/Usb1/chk.cor", "ee", 777, 0x58C12345u, 0xDEADBEEFu));
    n = bootcfg_format(&c, buf, sizeof(buf));
    bootcfg_parse(&d, buf);
    CHECK(d.n == 3 && d.e[2].size == 777 && d.e[2].mtime == 0x58C12345u && d.e[2].hdr == 0xDEADBEEFu && d.e[1].mtime == 0 && d.e[1].hdr == 0);
    bootcfg_parse(&d, "core=/a.cor|A|aa|10\ncore=/b.cor|B|bb\ncore=/c.cor|C|cc|20|99|0000abcd\n");
    CHECK(d.n == 3 && d.e[0].size == 10 && d.e[0].mtime == 0 && d.e[1].size == 0 && d.e[1].mtime == 0 && d.e[2].mtime == 99 && d.e[2].hdr == 0xabcd);
    CHECK(bootcfg_add(&d, "A again", "/a.cor", "a2", 11, 5, 6) && d.n == 3 && d.e[0].mtime == 5 && d.e[0].hdr == 6 && strcmp(d.e[0].name, "A again"));
    bootcfg_parse(&d, "# x\r\ncountdown=99\r\ndefault=7\r\ncore=/a|b|c\r\ngarbage\r\ncore=nodelims\r\n");
    CHECK(d.countdown == 60 && d.def == -1 && d.n == 1 && !strcmp(d.e[0].name, "b"));
    bootcfg_parse(&d, "");
    CHECK(d.n == 0 && d.countdown == 5);
    // T105: the picture survives a round trip, and nonsense in the file is not a mode
    {
        BootCfg v;
        bootcfg_init(&v);
        v.vid_sys = 1; v.vid_hdmi = 4;
        char txt[4096];
        CHECK(bootcfg_format(&v, txt, sizeof(txt)) > 0);
        CHECK(strstr(txt, "video=1,4\n") != 0 && v.vid_av == 1);   // T265 D9: the A/V port is on (RGB) unless the file says 0; on is not written
        BootCfg w;
        bootcfg_init(&w);
        bootcfg_parse(&w, txt);
        CHECK(w.vid_sys == 1 && w.vid_hdmi == 4 && w.vid_av == 1);
        v.vid_av = 0;
        CHECK(bootcfg_format(&v, txt, sizeof(txt)) > 0 && strstr(txt, "video=1,4,0") != 0);
        bootcfg_init(&w);
        bootcfg_parse(&w, txt);
        CHECK(w.vid_sys == 1 && w.vid_hdmi == 4 && w.vid_av == 0);
        bootcfg_init(&w);
        bootcfg_parse(&w, "video=1,4\n");                         // a file of an older host: no third field, RGB on
        CHECK(w.vid_hdmi == 4 && w.vid_av == 1);
        bootcfg_parse(&w, "video=-1,-1,7\n");                      // not 0: on
        CHECK(w.vid_sys == -1 && w.vid_av == 1);
        bootcfg_init(&w);
        bootcfg_parse(&w, "video=9,99\n");
        CHECK(w.vid_sys == -1 && w.vid_hdmi == -1);
        bootcfg_init(&w);
        bootcfg_parse(&w, "video=0\n");                            // a system without a resolution
        CHECK(w.vid_sys == 0 && w.vid_hdmi == -1);
    }

    // T104.1 5.5: the core application's boot-attempt counter is a key of this file; the host keeps it across its own saves and does not write it
    // when it is 0
    {
        BootCfg v;
        bootcfg_init(&v);
        CHECK(v.lib_attempt == 0);
        char txt[4096];
        CHECK(bootcfg_format(&v, txt, sizeof(txt)) > 0);
        CHECK(strstr(txt, "lib_attempt") == 0);
        v.lib_attempt = 1;
        CHECK(bootcfg_format(&v, txt, sizeof(txt)) > 0);
        CHECK(strstr(txt, "lib_attempt=1\n") != 0);
        BootCfg w;
        bootcfg_parse(&w, txt);
        CHECK(w.lib_attempt == 1);
        bootcfg_parse(&w, "lib_attempt=-4\n");
        CHECK(w.lib_attempt == 0);
        bootcfg_parse(&w, "lib_attempt=500\n");
        CHECK(w.lib_attempt == 99);
    }

    // T139: autostart_s keeps the seconds while the countdown is off; a boot.cfg of the run-1022 host (no such line) reads as it did
    {
        static const char old_file[] = "# TRX64 boot menu\ncountdown=7\ndirect=0\ndefault=1\nvideo=1,2\n"
                                       "core=/SD/cores/a.cor|Alpha|aa|10|99|0000abcd\ncore=/SD/cores/b.cor|Beta|bb|20|98|00001234\n";
        BootCfg o, r;
        bootcfg_parse(&o, old_file);
        CHECK(o.countdown == 7 && o.autostart_s == 7 && o.direct == 0 && o.def == 1 && o.vid_sys == 1 && o.vid_hdmi == 2 && o.n == 2);
        char t1[4096], t2[4096];
        CHECK(bootcfg_format(&o, t1, sizeof(t1)) > 0 && strstr(t1, "autostart_s=7\n") != 0);
        bootcfg_parse(&r, t1);
        CHECK(bootcfg_format(&r, t2, sizeof(t2)) > 0 && !strcmp(t1, t2));          // parse -> format -> parse is identical
        CHECK(r.countdown == 7 && r.n == 2 && !strcmp(r.e[1].name, "Beta") && r.e[1].hdr == 0x1234 && r.e[0].mtime == 99);
        bootcfg_parse(&o, "countdown=0\ndirect=1\n");                              // an old "never": the seconds were not kept
        CHECK(o.countdown == 0 && o.autostart_s == BOOT_COUNTDOWN_DEFAULT);
        bootcfg_autostart(&o, true, 0);                                              // on again with the default seconds
        CHECK(o.countdown == BOOT_COUNTDOWN_DEFAULT);
        bootcfg_autostart(&o, true, 20);
        CHECK(o.countdown == 20 && o.autostart_s == 20);
        bootcfg_autostart(&o, false, 0);                                             // off is countdown=0, the 20 s stay
        CHECK(o.countdown == 0 && o.autostart_s == 20);
        CHECK(bootcfg_format(&o, t1, sizeof(t1)) > 0 && strstr(t1, "countdown=0\n") != 0 && strstr(t1, "autostart_s=20\n") != 0);
        bootcfg_parse(&r, t1);
        CHECK(r.countdown == 0 && r.autostart_s == 20);                              // ... across a power cycle
        bootcfg_autostart(&r, true, 0);
        CHECK(r.countdown == 20);
        bootcfg_autostart(&r, true, 99);
        CHECK(r.countdown == 60 && r.autostart_s == 60);
        bootcfg_parse(&r, "countdown=12\nautostart_s=30\n");                       // a countdown above 0 is what runs
        CHECK(r.countdown == 12 && r.autostart_s == 12);
        bootcfg_parse(&r, "countdown=0\nautostart_s=300\n");
        CHECK(r.countdown == 0 && r.autostart_s == 60);
        bootcfg_parse(&r, "countdown=0\nautostart_s=0\n");
        CHECK(r.autostart_s == 1);
    }
    // T139: rename keeps order, default and every other field of the entry; refusals change nothing
    {
        BootCfg c;
        bootcfg_init(&c);
        CHECK(bootcfg_add(&c, "One", "/a.cor", "h1", 11, 22, 33) && bootcfg_add(&c, "Two", "/b.cor", "h2", 44, 55, 66) && bootcfg_add(&c, "Three", "/c.cor", "h3"));
        c.def = 1;
        CHECK(bootcfg_rename(&c, 1, "Two renamed"));
        CHECK(!strcmp(c.e[1].name, "Two renamed") && !strcmp(c.e[0].name, "One") && !strcmp(c.e[2].name, "Three") && c.def == 1 && c.n == 3);
        CHECK(!strcmp(c.e[1].path, "/b.cor") && !strcmp(c.e[1].hash, "h2") && c.e[1].size == 44 && c.e[1].mtime == 55 && c.e[1].hdr == 66);
        CHECK(!bootcfg_rename(&c, 1, "") && !bootcfg_rename(&c, 1, "   ") && !strcmp(c.e[1].name, "Two renamed"));
        CHECK(!bootcfg_rename(&c, 1, "a|b") && !bootcfg_rename(&c, 1, "a\nb") && !strcmp(c.e[1].name, "Two renamed"));
        CHECK(!bootcfg_rename(&c, 3, "x") && !bootcfg_rename(&c, -1, "x"));
        char n47[48], n48[49];
        memset(n47, 'x', 47); n47[47] = 0;
        memset(n48, 'y', 48); n48[48] = 0;
        CHECK(bootcfg_rename(&c, 0, n47) && !strcmp(c.e[0].name, n47));              // 47 characters fit
        CHECK(!bootcfg_rename(&c, 0, n48) && !strcmp(c.e[0].name, n47));             // 48 do not, and the entry keeps its name
        char txt[4096];
        CHECK(bootcfg_format(&c, txt, sizeof(txt)) > 0);
        BootCfg d;
        bootcfg_parse(&d, txt);                                                      // "read back": the file holds the new name
        CHECK(d.n == 3 && !strcmp(d.e[1].name, "Two renamed") && d.def == 1 && !strcmp(d.e[0].name, n47) && d.e[1].mtime == 55);
    }

    printf(fails ? "FAILED %d\n" : "test_bootcfg: all ok\n", fails);
    return fails != 0;
}
