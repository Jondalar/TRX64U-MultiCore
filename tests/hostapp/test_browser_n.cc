// T94-independent bench of GitHub #93: the host's File Browser lists a folder of any number of entries and pages to its end ("+" / "-" and the cursor keys),
// the parent row stays, and what does not fit the memory budget is said on a last row instead of a list that just stops. Fake folder, the browser's own
// hc_browse_load / hc_nav_act. No negative control: not a flash or persistence operation (T174 D5.4).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_core_nav.h"
#include "host_core.h"

static int g_fail, g_nfake, g_lists;
static uint32_t g_heap = 0xFFFFFFFFu;

static int fake_list(const char *path, HostDirEntry *out, int max, int *total)
{
    (void)path;
    g_lists++;
    // folders first (3), then the files, as hostfs_list does
    int n = 0;
    for (int i = 0; i < g_nfake; i++) {
        bool dir = i < 3;
        if (n < max) {
            memset(&out[n], 0, sizeof(out[n]));
            if (dir) snprintf(out[n].name, sizeof(out[n].name), "dir%d", i);
            else snprintf(out[n].name, sizeof(out[n].name), "Game %04d (USA).sfc.zip", i);
            out[n].dir = dir;
            out[n].size64 = out[n].size = dir ? 0 : 1000 + i;
            n++;
        }
    }
    if (total) *total = g_nfake;
    return n;
}
static bool fake_exists(const char *) { return true; }
static void fake_arm_end(uint32_t, int) {}
static bool fake_key_local(void) { return true; }
static void fake_gone(void) {}
static uint32_t fake_heap(void) { return g_heap; }

#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void run(int nfake, uint32_t heap, bool cursor_keys)
{
    g_nfake = nfake;
    g_heap = heap;
    g_lists = 0;
    char why[200] = {0};
    hc_browse_goto("/Usb0/snes", why, 200);     // a folder below the media: the parent row ".." is row 0
    const HcNav &v = hc_nav();
    int expect_rows = 1 + nfake;
    CHECK(v.bup, "parent row");
    CHECK(!strcmp(v.bl[0].name, ".."), "row 0 is ..");
    int shown = v.bn - 1, more = v.bmore;
    CHECK(shown + more == nfake, "shown %d + more %d == %d", shown, more, nfake);
    printf("n=%d heap=%u: rows %d, more %d, list reads %d\n", nfake, (unsigned)heap, v.bn, more, g_lists);
    if (heap == 0xFFFFFFFFu) {
        CHECK(more == 0 && v.bn == expect_rows, "whole list");
    } else {
        CHECK(more > 0, "the budget cut the list and says so");
    }
    // folders first, then files in the order the lister gave (sorting over the whole folder is the lister's: the buffer takes it whole)
    CHECK(v.bn > 4 && v.bl[1].dir && v.bl[3].dir && !v.bl[4].dir, "folders first");
    CHECK(more == 0 || v.bl[v.bn - 1].name[0] != 0, "last entry is a real row");
    int total_rows = hc_browse_nrows();
    int nsel = hc_browse_nsel();                 // the cursor stands on entries only: the "more" line is information (ux-review R10)
    CHECK(nsel == v.bn && total_rows == nsel + (more > 0), "rows %d, selectable %d", total_rows, nsel);
    int steps = 0, last = -1;
    hc_nav_geometry(5, 13, 1);
    if (cursor_keys) {
        for (int i = 0; i < total_rows + 5; i++) {
            if (hc_nav_act(HCX_BROWSE, HCA_CURSOR_DOWN) == HCN_NONE) break;
            steps++;
        }
        CHECK(steps == nsel - 1, "cursor DOWN walked %d of %d", steps, nsel - 1);
    } else {
        for (int i = 0; i < total_rows + 2; i++) {
            int r = hc_nav_act(HCX_BROWSE, HCA_BROWSE_PAGE_DOWN);
            int sel = hc_nav().bsel;
            if (sel == last) break;
            last = sel;
            steps++;
            (void)r;
        }
        CHECK(hc_nav().bsel == nsel - 1, "+ reached the last entry: %d of %d after %d pages", hc_nav().bsel, nsel - 1, steps);
    }
    struct { char name[160]; } r;
    const HcNav &w = hc_nav();
    int sel = w.bsel;
    CHECK(sel == nsel - 1, "cursor on the last entry");
    snprintf(r.name, sizeof(r.name), "%s", w.bl[sel].name);
    char pos[40];
    hc_browse_pos_text(pos, sizeof(pos), w.bfirst, 13);
    char wantpos[40];
    int total_e = v.bn - 1;
    snprintf(wantpos, sizeof(wantpos), "of %d", total_e);
    CHECK(total_e <= 13 ? !pos[0] : strstr(pos, wantpos) != 0, "position '%s' says %s", pos, wantpos);
    if (more > 0) {
        char mt[120];
        hc_browse_more_text(mt, sizeof(mt));
        CHECK(strstr(mt, "more not shown") != 0, "the line behind the last entry says: %s", mt);
        char want[16];
        snprintf(want, sizeof(want), "%d more", more);
        CHECK(strstr(mt, want) != 0, "count %d in: %s", more, mt);
        CHECK(sel < w.bn, "the cursor never stands on the information line");
        hc_nav_act(HCX_BROWSE, HCA_CURSOR_DOWN);
        CHECK(hc_nav().bsel == nsel - 1, "DOWN on the last entry stays");
    } else {
        char want[40];
        snprintf(want, sizeof(want), "Game %04d", nfake - 1);
        CHECK(strstr(r.name, want) != 0, "last entry is %s, row is %s", want, r.name);
    }
    printf("  at the end: row %d '%s' position '%s'\n", hc_nav().bsel, r.name, pos);
    // back up with - and with UP to row 0 (the parent row)
    if (cursor_keys) {
        for (int i = 0; i < total_rows + 5; i++) if (hc_nav_act(HCX_BROWSE, HCA_CURSOR_UP) == HCN_NONE) break;
    } else {
        for (int i = 0; i < total_rows + 2; i++) if (hc_nav_act(HCX_BROWSE, HCA_BROWSE_PAGE_UP) == HCN_NONE && hc_nav().bsel == 0) break;
    }
    CHECK(hc_nav().bsel == 0 && !strcmp(hc_nav().bl[0].name, ".."), "back at the parent row");
}

int main()
{
    HcNavPlatform p = { fake_list, fake_exists, fake_arm_end, fake_key_local, 0, fake_gone, fake_heap };
    hc_nav_init(&p);
    run(784, 0xFFFFFFFFu, false);
    run(784, 0xFFFFFFFFu, true);
    run(3000, 0xFFFFFFFFu, false);
    run(3000, 0xFFFFFFFFu, true);
    run(10, 0xFFFFFFFFu, false);
    // a heap budget of 1000 rows (a quarter of free): the rest is said, not cut
    run(3000, (uint32_t)sizeof(HostDirEntry) * 4 * 1000, false);
    run(3000, (uint32_t)sizeof(HostDirEntry) * 4 * 1000, true);
    printf(g_fail ? "browser_n: %d FAILED\n" : "browser_n: all passed\n", g_fail);
    return g_fail != 0;
}
