// T104.1 D9, D12: the description record. The reader of the board (descrec.cc) reads what the packer (tools/descrec.py) wrote - the fixture is compiled
// from tests/coreapp/golden/desc_test.toml by the run script, so the two implementations are tested against each other - and refuses what is not a
// record: every truncation, a wrong version, a table or a string outside the record, a count above what the board takes, a string that does not end,
// a kind or a widget it does not know, a tab that belongs to no area. No input makes it read outside the bytes it was given (the whole record is
// copied into an exact-size buffer, so a stray read is the AddressSanitizer's or the page's to see).
//
//   test_descrec DESC_TEST.bin
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "descrec.h"

typedef std::vector<uint8_t> Bytes;

static Bytes slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static uint32_t rd(const Bytes &b, uint32_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static void wr(Bytes &b, uint32_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }

// The record in a buffer of exactly its size, so that a read past its end is not hidden by slack.
static bool g_keep = true;
static int parse(const Bytes &b, DescRecord *d, char *why = 0)
{
    uint8_t *exact = new uint8_t[b.size() ? b.size() : 1];
    memcpy(exact, b.data(), b.size());
    char w[200];
    int rc = descrec_parse(exact, (uint32_t)b.size(), d, why ? why : w, 200);
    if (!g_keep) {
        delete[] exact;                  // the per-byte loops read nothing afterwards; the checks of one record keep it (the strings point into it)
    }
    return rc;
}

enum { A, T, N, F, S, H, R, B, E, Z, V, C, K, M, Y, X, Q };
static const uint32_t ESIZE[17] = { 20, 24, 52, 40, 32, 4, 8, 16, 4, 4, 4, 24, 36, 24, 24, 36, 36 };
static uint32_t table_off(const Bytes &b, int t) { return rd(b, 24 + 8 * t); }
static uint32_t table_cnt(const Bytes &b, int t) { return rd(b, 28 + 8 * t); }
static uint32_t entry(const Bytes &b, int t, int i) { return table_off(b, t) + i * ESIZE[t]; }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);                                 // a mutant that reads out of bounds may crash: what it said before has to be out
    CHECK(argc == 3);
    Bytes good = slurp(argv[1]);
    Bytes own = slurp(argv[2]);
    CHECK(good.size() > DESCREC_HEADER);
    DescRecord *d = new DescRecord;
    char why[200];

    // ---- what the packer wrote
    CHECK_EQ(parse(good, d, why), 0);
    CHECK(d->has_route);
    CHECK_EQ(d->skel.nareas, 5);
    CHECK_STR(d->areas[0].key, "F1");
    CHECK_STR(d->areas[0].name, "Home");
    CHECK(d->areas[2].name == 0);                                  // F5 names none: the core's short title
    CHECK(d->areas[4].shifted && !d->areas[0].shifted);
    CHECK_EQ(d->areas[2].ntabs, 4);
    CHECK_STR(d->areas[2].tabs[2].id, "drives");
    CHECK_EQ(d->areas[2].tabs[2].n, 4);
    CHECK_EQ(d->areas[2].tabs[2].widget, TW_SETTINGS);
    CHECK_STR(d->areas[2].tabs[2].groups, "drive_a,drive_b");
    CHECK_EQ(d->areas[2].tabs[3].widget, TW_NODES);
    CHECK(d->areas[2].tabs[0].groups == 0 && d->areas[2].tabs[0].n == 0);
    CHECK_EQ(d->rec.nnodes, 4);
    CHECK_STR(d->nodes[0].anchor, "F7/more");
    CHECK_STR(d->nodes[1].anchor, "F5/tools");
    CHECK_STR(d->nodes[1].id, "selftest");
    CHECK_EQ(d->nodes[1].kind, LN_OP);
    CHECK_EQ(d->nodes[1].order, 5);
    CHECK_STR(d->nodes[2].state, "test.slot");
    CHECK_EQ(d->nodes[3].kind, LN_GROUP);
    CHECK_EQ(d->nodes[3].order, -10);                              // a negative key survives the 32 bit field
    CHECK_EQ(d->rec.ntabs, 1);
    CHECK_STR(d->ctabs[0].area, "F7"); CHECK_STR(d->ctabs[0].id, "more"); CHECK_STR(d->ctabs[0].name, "More");
    CHECK_EQ(d->ctabs[0].widget, TW_NODES); CHECK(d->ctabs[0].groups == 0); CHECK_EQ(d->ctabs[0].n, 0);
    CHECK_STR(d->rec.poll.op, "test.task"); CHECK_EQ(d->rec.poll.period_ms, 250); CHECK(!d->rec.poll.when_frozen);
    CHECK_EQ(d->rec.nfile_actions, 1);
    CHECK_EQ(d->actions[0].next, 2);
    CHECK_STR(d->actions[0].ext[1], ".tst3");                      // the packer lower-cases an extension
    CHECK(d->actions[0].closes_menu);
    CHECK_EQ(d->actions[0].nbind, 1);
    CHECK_EQ(d->actions[0].bind[0].kind, BIND_FILE);
    CHECK_STR(d->actions[0].bind[0].arg, "path");
    CHECK_EQ(d->rec.nsettings, 2);
    CHECK_STR(d->settings[0].key, "Drive Bus ID");
    CHECK_STR(d->settings[0].group, "drive_a");
    CHECK(d->settings[0].values == 0 && d->settings[0].lo == 8 && d->settings[0].hi == 11 && d->settings[0].def == 9);
    CHECK_EQ(d->settings[1].nvalues, 3);
    CHECK_STR(d->settings[1].values[2], "Odd");
    CHECK(d->settings[1].group == 0);
    CHECK_EQ(d->ov.nhide, 1);
    CHECK_STR(d->ov.hide[0], "lib_hidden");
    CHECK_EQ(d->ov.nrename, 1);
    CHECK_STR(d->ov.rename[0].label, "Renamed by the description");

    // ---- the refusals, one by one
    {
        Bytes b = good; b[0] ^= 1;
        CHECK(parse(b, d, why) != 0 && strstr(why, "magic"));
        b = good; b[4] = 4;                                         // 3 is the current layout, 2 is read as 3 without a layout and attributes
        CHECK(parse(b, d, why) != 0 && strstr(why, "version 4"));
        b = good; b[4] = 1;                                         // the first layout (header 136) is not read any more
        CHECK(parse(b, d, why) != 0 && strstr(why, "version 1"));
        b = good; b[6] = 0x71;
        CHECK(parse(b, d, why) != 0 && strstr(why, "header or the length"));
        b = good; wr(b, 12, 4);
        CHECK(parse(b, d, why) != 0 && strstr(why, "flags"));
        b = good; wr(b, 16, (uint32_t)good.size());
        CHECK(parse(b, d, why) != 0 && strstr(why, "string table"));
        b = good; wr(b, 20, 0);
        CHECK(parse(b, d, why) != 0);
        b = good; wr(b, 28 + 8 * H, DR_HIDE + 1);                   // a table that still lies inside the record, with one entry more than the board holds
        CHECK(parse(b, d, why) != 0 && strstr(why, "hide list (this application takes 16)"));
        b = good; wr(b, 24 + 8 * N, (uint32_t)good.size() - 8);
        CHECK(parse(b, d, why) != 0 && strstr(why, "table of nodes"));
        b = good; wr(b, 24 + 8 * N, 4);
        CHECK(parse(b, d, why) != 0);                              // a table that starts inside the header
        // a string that does not end in the table: the last byte of the table is its NUL
        b = good; b[good.size() - 1] = 'x';
        CHECK(parse(b, d, why) != 0);                               // the last string loses its end: whoever uses it is refused
        // an offset into the string table that is past it
        b = good; wr(b, entry(b, N, 0), 0x0FFFFFFF);
        CHECK(parse(b, d, why) != 0);
        // a node of a kind that does not exist
        b = good; wr(b, entry(b, N, 0) + 4, 9);
        CHECK(parse(b, d, why) != 0 && strstr(why, "node 0"));
        // a binding of an unknown kind
        b = good; wr(b, entry(b, B, 0) + 4, 9);
        CHECK(parse(b, d, why) != 0 && strstr(why, "binding 0"));
        // a tab with a widget that does not exist
        b = good; wr(b, entry(b, T, 0) + 12, 9);
        CHECK(parse(b, d, why) != 0 && strstr(why, "tab 0"));
        // an area key that is not F1..F8
        {
            b = good;
            uint32_t str_off = rd(b, 16);
            // point area 0's key at the string of tab 0's name ("Home") - not a key
            wr(b, entry(b, A, 0), rd(b, entry(b, T, 0) + 4));
            (void)str_off;
            CHECK(parse(b, d, why) != 0 && strstr(why, "no key F1"));
        }
        // an area that claims no tabs, and one whose tabs are not where the previous area's end (the counts still add up)
        b = good; wr(b, entry(b, A, 0) + 16, 0);
        CHECK(parse(b, d, why) != 0);
        b = good; wr(b, entry(b, A, 1) + 12, 2);
        CHECK(parse(b, d, why) != 0 && strstr(why, "out of place"));
        // the route flag and the area table disagree
        b = good; wr(b, 12, 0);
        CHECK(parse(b, d, why) != 0 && strstr(why, "route flag"));
        // a file action with a run of extensions past the table
        b = good; wr(b, entry(b, F, 0) + 12, 99);
        CHECK(parse(b, d, why) != 0 && strstr(why, "file action 0"));
        // a setting with a run of values past the table
        b = good; wr(b, entry(b, S, 1) + 12, 99);
        CHECK(parse(b, d, why) != 0 && strstr(why, "setting 1"));
        // a rename without a label
        b = good; wr(b, entry(b, R, 0) + 4, 0xFFFFFFFFu);
        CHECK(parse(b, d, why) != 0 && strstr(why, "rename entry 0"));
        // two areas with the same key
        b = good; wr(b, entry(b, A, 1), rd(b, entry(b, A, 0)));
        CHECK(parse(b, d, why) != 0 && strstr(why, "twice"));
        // the poll: a period without an operation, an operation without a period, a reserved word, a period past a minute
        b = good; wr(b, 24 + 8 * DR_TABLES, DESCREC_NONE);
        CHECK(parse(b, d, why) != 0 && strstr(why, "poll"));
        b = good; wr(b, 28 + 8 * DR_TABLES, 0);
        CHECK(parse(b, d, why) != 0 && strstr(why, "poll"));
        b = good; wr(b, 36 + 8 * DR_TABLES, 1);
        CHECK(parse(b, d, why) != 0 && strstr(why, "poll"));
        // the list of merged libraries: a string that is empty is no list (none is DESCREC_NONE), one outside the table is not a string
        b = good; wr(b, 32 + 8 * DR_TABLES, 0);
        CHECK(parse(b, d, why) != 0 && strstr(why, "merged"));
        b = good; wr(b, 32 + 8 * DR_TABLES, (uint32_t)good.size());
        CHECK(parse(b, d, why) != 0);
        b = good; wr(b, 28 + 8 * DR_TABLES, 60001);
        CHECK(parse(b, d, why) != 0 && strstr(why, "poll"));
        // a contributed tab: a widget that does not exist, an area that is no key, a number past 99
        b = good; wr(b, entry(b, C, 0) + 12, 9);
        CHECK(parse(b, d, why) != 0 && strstr(why, "contributed tab 0"));
        b = good; wr(b, entry(b, C, 0), rd(b, entry(b, C, 0) + 4));
        CHECK(parse(b, d, why) != 0 && strstr(why, "contributed tab 0"));
        b = good; wr(b, entry(b, C, 0) + 20, 100);
        CHECK(parse(b, d, why) != 0 && strstr(why, "contributed tab 0"));
        // an extension without a dot
        b = good; wr(b, entry(b, E, 0), rd(b, entry(b, V, 0)));
        CHECK(parse(b, d, why) != 0 && strstr(why, "extension"));
    }
    // ---- nothing but the record's own bytes is read: every truncation is refused, none reads past the end
    for (size_t n = 0; n < good.size(); n++) {
        Bytes cut(good.begin(), good.begin() + n);
        CHECK(parse(cut, d) != 0);
    }
    g_keep = false;
    Bytes longer = good;
    longer.push_back(0);
    CHECK(parse(longer, d) != 0);                                  // the length field is exact

    // ---- every single byte changed to every other value near it: the reader answers, never crashes (the buffer is exact: ASan would see a stray read)
    int refused = 0, accepted = 0;
    for (size_t i = 0; i < good.size(); i++) {
        uint8_t keep = good[i];
        static const uint8_t vals[] = { 0x00, 0xFF, 0x01, 0x80 };
        for (unsigned k = 0; k < sizeof(vals); k++) {
            Bytes b = good;
            b[i] = (uint8_t)(keep ^ vals[k]);
            if (b[i] == keep) continue;
            (parse(b, d) ? refused : accepted)++;
        }
    }
    CHECK(refused > 100);                                          // most changes to the structure are caught ...
    CHECK(accepted > 100);                                         // ... and a changed letter in a label is a different label, not an error

    g_keep = true;
    // too short to be a record, and too long
    {
        Bytes b(DESCREC_HEADER - 1, 0);
        CHECK(parse(b, d) != 0);
        Bytes big(DESCREC_MAX_SIZE + 1, 0);
        CHECK(parse(big, d) != 0);
        Bytes empty;
        CHECK(parse(empty, d) != 0);
    }

    // ---- the core's own menu (T104.2 D10): the record of TRX64U, as menu.toml compiles
    {
        CHECK(!d->has_own);                                        // a description without the own tables leaves the descriptor as it is
        CoreDesc base, out;
        memset(&base, 0, sizeof(base));
        base.cor_name = "x";
        static const Task base_tasks[1] = { { "Base task", "x.y" } };
        base.tasks = base_tasks; base.ntasks = 1;                  // the descriptor's own table survives a record that has none of its own
        descrec_apply(*d, &base, &out);
        CHECK(!memcmp(&base, &out, sizeof(base)));
        DescRecord *o = new DescRecord;
        CHECK_EQ(parse(own, o, why), 0);
        CHECK(o->has_own);
        CHECK_EQ(o->ntasks, 4);                                    // Reset and the rest of the machine's tasks are nodes of the record now; the four disk tasks stay tables
        CHECK_STR(o->tasks[0].label, "Save Disk as D64");
        CHECK(o->tasks[0].ask && o->tasks[0].ask_state && !o->tasks[0].pick_dir && !o->tasks[0].closes_menu);
        CHECK_STR(o->tasks[0].group, "c64");
        CHECK_STR(o->tasks[2].label, "Create blank D64");
        CHECK(o->tasks[2].pick_dir && o->tasks[2].ask && o->tasks[2].ask_state && !o->tasks[2].closes_menu);
        CHECK_STR(o->tasks[2].ask_state, "drive.suggest.d64");
        CHECK_EQ(o->tasks[2].nbind, 3);
        CHECK_EQ(o->tasks[2].bind[0].kind, BIND_TEXT);
        CHECK_EQ(o->nmedia, 2);
        CHECK_STR(o->media[1].name, "Drive A:");
        CHECK_STR(o->media[1].eject_op, "drive.eject");
        CHECK(!o->media[0].eject_op && o->media[0].neject_bind == 0);
        CHECK_EQ(o->nftypes, 15);
        CHECK_STR(o->ftypes[0].ext, ".prg");
        CHECK_EQ(o->ftypes[0].nactions, 3);
        CHECK_STR(o->ftypes[0].actions[1].ask, "Start address");
        CHECK_EQ(o->ftypes[1].actions[1].ask_off, 26);               // the .P00 header
        CHECK_STR(o->ftypes[4].ext, ".d64");
        CHECK_STR(o->ftypes[4].inner_ext, ".prg");
        CHECK_EQ(o->ftypes[4].ninner, 2);
        CHECK_STR(o->ftypes[4].inner[1].label, "Real Run");
        CHECK(!o->ftypes[5].inner && o->ftypes[5].ninner == 0);
        CHECK_EQ(o->ncsettings, 160);                              // the four tables of the record and the settings its nodes declare
        CHECK_STR(o->csettings[0].key, "Freeze mode");
        CHECK_EQ(o->csettings[0].when, WHEN_RESET);
        CHECK_EQ(o->csettings[1].def, 1);
        CHECK_EQ(o->csettings[1].when, WHEN_LIVE);
        CHECK_STR(o->csettings[3].key, "Disk swap delay");
        CHECK_STR(o->csettings[3].values[9], "1000 ms");
        CHECK_STR(o->csettings[2].group, "drives");
        // the settings tabs name their groups, and so does the tasks tab
        int grouped = 0;
        bool tasks_groups = false;
        for (int a = 0; a < o->skel.nareas; a++) for (int t = 0; t < o->areas[a].ntabs; t++) {
            const SkelTab &tb = o->areas[a].tabs[t];
            if (tb.groups) { grouped++; if (tb.widget == TW_TASKS && !strcmp(tb.groups, "c64,files")) tasks_groups = true; }
        }
        CHECK_EQ(grouped, 10);
        CHECK(tasks_groups);
        descrec_apply(*o, &base, &out);
        CHECK(out.tasks == o->tasks && out.ntasks == 4 && out.nmedia == 2 && out.nfiletypes == 15 && out.nsettings == 160);

        // the refusals of the new tables
        Bytes b;
        b = own; wr(b, 12, rd(b, 12) & ~2u);                       // the flag says no own tables, the tables are there
        CHECK(parse(b, o, why) != 0 && strstr(why, "own-menu flag"));
        b = own; wr(b, entry(b, K, 0) + 20, DESCREC_NONE);                    // an ask_state without an ask (task 0 has both)
        CHECK(parse(b, o, why) != 0 && strstr(why, "task 0"));
        b = own; wr(b, entry(b, K, 0) + 16, 8);                               // a flag nobody defined
        CHECK(parse(b, o, why) != 0 && strstr(why, "task 0"));
        b = own; wr(b, entry(b, K, 0) + 8, DR_BINDS);                         // a binding run past the table
        CHECK(parse(b, o, why) != 0 && strstr(why, "task 0"));
        b = own; wr(b, entry(b, M, 0) + 16, 1);                               // a binding of an eject that is not there
        CHECK(parse(b, o, why) != 0 && strstr(why, "media slot 0"));
        b = own; wr(b, entry(b, Y, 1), rd(b, entry(b, Y, 0)));                // the same extension twice
        CHECK(parse(b, o, why) != 0 && strstr(why, "file type"));
        b = own; wr(b, entry(b, Y, 0) + 12, 0);                               // a file type without an action
        CHECK(parse(b, o, why) != 0 && strstr(why, "file type 0"));
        b = own; wr(b, entry(b, Y, 3) + 8, 1000);                             // an action run outside the table
        CHECK(parse(b, o, why) != 0 && strstr(why, "file type 3"));
        b = own; wr(b, entry(b, X, 0) + 24, 5);                               // an address default without a prompt
        CHECK(parse(b, o, why) != 0 && strstr(why, "file type action 0"));
        b = own; wr(b, entry(b, X, 0) + 16, 2);                               // a flag nobody defined
        CHECK(parse(b, o, why) != 0 && strstr(why, "file type action 0"));
        b = own; wr(b, entry(b, Q, 0) + 12, 1);                               // a setting with one value
        CHECK(parse(b, o, why) != 0 && strstr(why, "core setting 0"));
        b = own; wr(b, entry(b, Q, 0) + 16, 2);                               // a default that is no value
        CHECK(parse(b, o, why) != 0 && strstr(why, "core setting 0"));
        b = own; wr(b, entry(b, Q, 0) + 20, 4);                               // a time that is neither live, at reset, a text (2) nor a secret text (3)
        CHECK(parse(b, o, why) != 0 && strstr(why, "core setting 0"));
        b = own; wr(b, entry(b, Q, 1), rd(b, entry(b, Q, 0)));                // the same key twice
        CHECK(parse(b, o, why) != 0 && strstr(why, "twice"));
        // a tab that lists groups on a widget that has none
        for (int i = 0; i < (int)table_cnt(own, T); i++) {
            b = own;
            if (rd(b, entry(b, T, i) + 16) == DESCREC_NONE) {
                wr(b, entry(b, T, i) + 16, rd(b, entry(b, T, i)));
                wr(b, entry(b, T, i) + 12, TW_BROWSER);
                CHECK(parse(b, o, why) != 0 && strstr(why, "takes none"));
                break;
            }
        }
        g_keep = false;
        for (size_t n = 0; n < own.size(); n += 3) {                       // truncations and single-byte changes of the whole own record: an answer, never a stray read
            Bytes cut(own.begin(), own.begin() + n);
            CHECK(parse(cut, o) != 0);
        }
        int refused2 = 0;
        for (size_t i = 0; i < own.size(); i += 2) {
            Bytes c = own;
            c[i] ^= 0xFF;
            refused2 += parse(c, o) != 0;
        }
        CHECK(refused2 > 100);
        g_keep = true;
        delete o;
    }

    delete d;
    DONE("test_descrec");
}
