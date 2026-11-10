// T135: the decision table of the direct start (directboot.cc), the return marker (handover.cc) and the boot.cfg key (bootcfg.cc).
#include "directboot.h"
#include "handover.h"
#include "bootcfg.h"
#include "ovtext.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static DirectIn base()
{
    DirectIn in;
    memset(&in, 0, sizeof(in));
    in.cfg_loaded = true;
    in.direct = 1;
    in.n = 3;
    in.def = 1;
    in.dstate = DD_OK;
    return in;
}

// Issue #14: the default on a USB stick that mounts late. A fake clock and a fake medium drive the loop the host runs (directboot_look).
struct FakeMedium {
    uint32_t t;                   // ms since the start of the wait
    uint32_t mount_at;            // the root is listed from here (0xFFFFFFFF = never)
    bool file;                    // the file is on the medium
    uint32_t key_at;              // C= is held from here
    int waiting_on, waiting_off;
};
static uint32_t fm_now(void *c) { return ((FakeMedium *)c)->t; }
static void fm_sleep(void *c, uint32_t ms) { ((FakeMedium *)c)->t += ms; }
static bool fm_listed(void *c) { FakeMedium *m = (FakeMedium *)c; return m->t >= m->mount_at; }
static DbDefault fm_look(void *c) { FakeMedium *m = (FakeMedium *)c; return (m->file && fm_listed(c)) ? DD_OK : DD_MISSING; }
static bool fm_key(void *c) { FakeMedium *m = (FakeMedium *)c; return m->t >= m->key_at; }
static void fm_waiting(void *c, bool on) { FakeMedium *m = (FakeMedium *)c; if (on) m->waiting_on++; else m->waiting_off++; }
static DbDefault fm_run(FakeMedium &m, bool removable, uint32_t limit, uint32_t grace)
{
    DbEnv env = { fm_now, fm_sleep, fm_look, fm_listed, fm_key, fm_waiting, &m };
    return directboot_look(env, removable, limit, grace);
}
static DbDefault usb_case(uint32_t mount_at, bool file, uint32_t limit, uint32_t grace)
{
    FakeMedium m = { 0, mount_at, file, 0xFFFFFFFF, 0, 0 };
    return fm_run(m, true, limit, grace);
}

int main()
{
    const uint32_t OLD_WAIT = 4000, NEW_WAIT = 30000;
    // #14 S1: the stick mounts 3 s after the start, the default is on it -> starts
    CHECK(usb_case(3000, true, NEW_WAIT, DB_GRACE_MS) == DD_OK);
    // #14 S2: it mounts after 12 s (a slow stick) -> starts; the old 4 s wait with no grace says "missing" (the control, it has to differ)
    CHECK(usb_case(12000, true, NEW_WAIT, DB_GRACE_MS) == DD_OK);
    CHECK(usb_case(12000, true, OLD_WAIT, 0) == DD_MISSING);
    // #14 S3: the medium is listed and the file is not there -> missing, after the grace and not after the 30 s
    {
        FakeMedium m = { 0, 100, false, 0xFFFFFFFF, 0, 0 };
        CHECK(fm_run(m, true, NEW_WAIT, DB_GRACE_MS) == DD_MISSING && m.t < 3000);
    }
    // #14 S4: no medium ever -> missing after the limit, not before; the wait text went up once and came down once
    {
        FakeMedium m = { 0, 0xFFFFFFFF, false, 0xFFFFFFFF, 0, 0 };
        CHECK(fm_run(m, true, NEW_WAIT, DB_GRACE_MS) == DD_MISSING && m.t >= NEW_WAIT && m.waiting_on == 1 && m.waiting_off == 1);
    }
    // #14 S5: C= during the wait ends it at once
    {
        FakeMedium m = { 0, 0xFFFFFFFF, false, 2000, 0, 0 };
        CHECK(fm_run(m, true, NEW_WAIT, DB_GRACE_MS) == DD_MISSING && m.t < 2500 && m.waiting_off == 1);
    }
    // #14 S6: the medium is in the flash disk: no wait at all
    {
        FakeMedium m = { 0, 0xFFFFFFFF, false, 0xFFFFFFFF, 0, 0 };
        CHECK(fm_run(m, false, NEW_WAIT, DB_GRACE_MS) == DD_MISSING && m.t == 0 && m.waiting_on == 0);
    }
    // the file is there at once: no wait text, no sleeping
    {
        FakeMedium m = { 0, 0, true, 0xFFFFFFFF, 0, 0 };
        CHECK(fm_run(m, true, NEW_WAIT, DB_GRACE_MS) == DD_OK && m.t == 0 && m.waiting_on == 0);
    }
    // S1: direct=1, a good default, nothing held, no marker -> start
    DirectIn in = base();
    CHECK(directboot_decide(in) == DB_START);
    CHECK(!directboot_stops_countdown(DB_START));
    // S2 / S3: C= or fire -> the menu, countdown stopped, the default not started
    in = base(); in.key_cbm = true;
    CHECK(directboot_decide(in) == DB_KEY_CBM && directboot_stops_countdown(DB_KEY_CBM));
    in = base(); in.key_fire = true;
    CHECK(directboot_decide(in) == DB_KEY_FIRE && directboot_stops_countdown(DB_KEY_FIRE));
    // S4: the default is missing / changed / refused -> the menu with the reason, no countdown
    in = base(); in.dstate = DD_MISSING;
    CHECK(directboot_decide(in) == DB_MISSING && directboot_stops_countdown(DB_MISSING));
    in = base(); in.dstate = DD_CHANGED;
    CHECK(directboot_decide(in) == DB_CHANGED && directboot_stops_countdown(DB_CHANGED));
    in = base(); in.dstate = DD_REFUSED;
    CHECK(directboot_decide(in) == DB_REFUSED && directboot_stops_countdown(DB_REFUSED));
    // S5: the return marker holds the menu; without it the same inputs start (the control)
    in = base(); in.marker = true;
    CHECK(directboot_decide(in) == DB_RETURN && directboot_stops_countdown(DB_RETURN));
    in.marker = false;
    CHECK(directboot_decide(in) == DB_START);
    // S6: direct=0 -> today's menu with its countdown
    in = base(); in.direct = 0;
    CHECK(directboot_decide(in) == DB_OFF && !directboot_stops_countdown(DB_OFF));
    // the marker and the keys win over direct=0 too (the person asked for the menu)
    in.marker = true;
    CHECK(directboot_decide(in) == DB_RETURN);
    // no boot.cfg: today's menu, which keeps trying to read it
    in = base(); in.cfg_loaded = false;
    CHECK(directboot_decide(in) == DB_NOCFG && !directboot_stops_countdown(DB_NOCFG));
    // issue #69: C= or fire held while boot.cfg is not readable (yet): the menu, countdown stopped
    in = base(); in.cfg_loaded = false; in.key_fire = true;
    CHECK(directboot_decide(in) == DB_KEY_FIRE && directboot_stops_countdown(DB_KEY_FIRE));
    in = base(); in.cfg_loaded = false; in.key_cbm = true;
    CHECK(directboot_decide(in) == DB_KEY_CBM && directboot_stops_countdown(DB_KEY_CBM));
    // no default / no entries / a default index outside the list
    in = base(); in.def = -1;
    CHECK(directboot_decide(in) == DB_NODEFAULT);
    in = base(); in.n = 0;
    CHECK(directboot_decide(in) == DB_NODEFAULT);
    in = base(); in.def = 3;
    CHECK(directboot_decide(in) == DB_NODEFAULT);
    // the file is only looked at when everything before it says start
    in = base();
    CHECK(directboot_needs_file(in));
    in.key_cbm = true;
    CHECK(!directboot_needs_file(in));
    in = base(); in.direct = 0;
    CHECK(!directboot_needs_file(in));
    // the file's state does not matter once a key is held
    in = base(); in.key_fire = true; in.dstate = DD_MISSING;
    CHECK(directboot_decide(in) == DB_KEY_FIRE);
    CHECK(!strcmp(directboot_text(DB_START), "") && directboot_text(DB_MISSING)[0]);

    // D3: the marker lives in the last words of the page, outside the struct and its sum
    static uint32_t page[1024];
    memset(page, 0, sizeof(page));
    CHECK(!handover_menu_take(page));
    handover_fill((Handover *)page, "/USB1/cores/a.cor", "A", "1", 7);
    CHECK(handover_valid((Handover *)page));
    handover_menu_request(page);
    CHECK(handover_valid((Handover *)page));                // old readers still accept the block
    CHECK(handover_menu_take(page));
    CHECK(!handover_menu_take(page));                       // read and cleared
    CHECK(handover_valid((Handover *)page));
    page[1022] = HANDOVER_MENU_MAGIC;                       // one word alone is noise, not a request
    CHECK(!handover_menu_take(page));
    CHECK(HANDOVER_MENU_OFFSET >= sizeof(Handover) && HANDOVER_MENU_OFFSET + 8 == 4096);

    // D4/D7: direct= is on without the line, round-trips, and unknown keys do not hurt
    BootCfg c, d;
    bootcfg_parse(&c, "countdown=3\ndefault=-1\n");
    CHECK(c.direct == 1);
    bootcfg_parse(&c, "countdown=3\ndirect=0\n");
    CHECK(c.direct == 0);
    char buf[512];
    CHECK(bootcfg_format(&c, buf, sizeof(buf)) > 0 && strstr(buf, "direct=0\n"));
    bootcfg_parse(&d, buf);
    CHECK(d.direct == 0 && d.countdown == 3);
    c.direct = 1;
    bootcfg_format(&c, buf, sizeof(buf));
    bootcfg_parse(&d, buf);
    CHECK(d.direct == 1);

    // D5: one line when it fits 40 columns, else two
    char rows[2][41];
    CHECK(ovtext_layout("Amiga", rows) == 1 && !strcmp(rows[0], "Starting Amiga - hold C= for the menu"));
    CHECK(ovtext_layout("Commodore 64 Ultimate 1.1.0 backup", rows) == 2 && strlen(rows[0]) <= 40 && !strcmp(rows[1], "Hold C= for the menu"));

    // D13: the box is 41 x 21 cells (the gateware then scales 720p to 3x3), the text sits in its middle, the rest is blank on black
    {
        static uint8_t scr[OVTEXT_BOX_COLS * OVTEXT_BOX_ROWS], col[OVTEXT_BOX_COLS * OVTEXT_BOX_ROWS];
        CHECK(OVTEXT_BOX_COLS == 41 && OVTEXT_BOX_ROWS == 21);
        ovtext_layout("Amiga", rows);
        ovtext_fill(rows, 1, scr, col);
        int text_cells = 0, bad = 0;
        for (int r = 0; r < OVTEXT_BOX_ROWS; r++)
            for (int c = 0; c < OVTEXT_BOX_COLS; c++) {
                if (col[r * OVTEXT_BOX_COLS + c] != 0x01) bad++;
                if (scr[r * OVTEXT_BOX_COLS + c] != ' ') { text_cells++; if (r != 10) bad++; }
            }
        CHECK(bad == 0 && text_cells == (int)strlen("Starting Amiga - hold C= for the menu") - 7);   // 7 blanks inside the line
        CHECK(scr[10 * OVTEXT_BOX_COLS + (OVTEXT_BOX_COLS - 37) / 2] == 'S');
        ovtext_layout("Commodore 64 Ultimate 1.1.0 backup", rows);
        ovtext_fill(rows, 2, scr, col);
        CHECK(scr[9 * OVTEXT_BOX_COLS + (OVTEXT_BOX_COLS - (int)strlen(rows[0])) / 2] == 'S');
        CHECK(scr[10 * OVTEXT_BOX_COLS + (OVTEXT_BOX_COLS - 20) / 2] == 'H');
        CHECK(scr[0] == ' ' && scr[OVTEXT_BOX_COLS * OVTEXT_BOX_ROWS - 1] == ' ');
    }

    // S3: the fire that brought the menu reads as RETURN in the menu: dropped while it is held, the first idle look ends it, a later RETURN is real
    {
        FireGuard g;
        fireguard_arm(&g, true);
        CHECK(fireguard_drop(&g, 13));                  // RETURN from the held fire
        CHECK(fireguard_drop(&g, 13));                  // ... and its repeat
        CHECK(!fireguard_drop(&g, -1));                 // let go: nothing from the keyboard
        CHECK(!fireguard_drop(&g, 13));                 // a new press is a person pressing RETURN
        fireguard_arm(&g, false);                       // the menu came for another reason (C=, no default, ...): nothing is dropped
        CHECK(!fireguard_drop(&g, 13) && !fireguard_drop(&g, 's'));
        fireguard_arm(&g, true);
        CHECK(!fireguard_drop(&g, -1) && !fireguard_drop(&g, 13));      // fire already released before the first look
    }

    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("directboot: ok\n");
    return 0;
}
