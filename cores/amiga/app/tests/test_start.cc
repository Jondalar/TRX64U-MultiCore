// T93 S1: the application finds its core, and refuses a core it has no description for or one the bundle does not name.
#include <string.h>
#include "check.h"
#include "start.h"
#include "handover.h"
#include "fakes.h"
#include "amiga_regs.h"

static const CoreDesc a = { {'D', '8', 'X', 'L'}, "Dummy 800 XL", "Dummy", "dummy800", "Dummy 800 XL Settings" };
static CoreDesc n = { {'D', 'C', 'O', 'N'}, "Dummy Console System", "Console", "dummycon", "Dummy Console Settings" };
// T239.5 D13: the Amiga 500's cases run as its app (run.sh amigaapp, unit amiga_start); the shared unit start keeps the cases of the two stubs.
extern const CoreDesc amiga_desc;
// T213.3 (T239.5 D13): the A1200 has left the shared app, so no app has two records with one ID word any more. start_identify still chooses
// among such records (by the bundle's name, the core's id, the gateware's feature bits); a twin of the A500's record with the A1200's identity
// keeps those cases (filled at the start of main).
static CoreDesc a12twin;
const CoreDesc *const core_table[] = { &a, &n, &amiga_desc, &a12twin, 0 };

int main()
{
    char msg[300];
    a12twin = amiga_desc;
    a12twin.cor_name = "Amiga 1200 (AGA, 68020)";
    a12twin.title = "Amiga 1200";
    a12twin.core_id = "amiga1200";
    a12twin.id_feat_val = AM_FEAT_FBWIN;                                      // the A1200's gateware has the display window (T115 D9)
    const CoreDesc *d = 0;
    Handover h;
    // T104 5.5: the Amiga and the A1200 are one gateware with one ID word - the bundle's name tells the descriptors apart
    handover_fill(&h, "/SD/amiga1200.cor", "Amiga 1200 (AGA, 68020)", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a12twin);
    CHECK(strstr(msg, "Amiga 1200") != 0);
    handover_fill(&h, "/SD/amiga.cor", "Amiga (OCS/ECS, 68000)", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &amiga_desc);
    // a bundle of another machine on this gateware: refused, the message names the bundle and both candidates
    handover_fill(&h, "/SD/dummycon.cor", "Dummy Console System", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0);
    CHECK(strstr(msg, "Dummy Console") && strstr(msg, "Amiga 1200") && strstr(msg, "Amiga (OCS"));
    // T115 7 H2, I1-I5: the core's id picks, the display name is only the fallback
    handover_fill(&h, "/SD/amiga1200.cor", "Amiga 1200 (AGA)", "0.3.0", 0, 0, "amiga1200");        // the board's case: a name that is not the descriptor's
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a12twin);
    handover_fill(&h, "/SD/amiga.cor", "Amiga", "0.2.0", 0, 0, "amiga");
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &amiga_desc);
    handover_fill(&h, "/SD/amiga1200.cor", "Amiga 1200 (AGA, 68020)", "0.3.0", 0, 0, "AMIGA");          // id and name disagree: the id wins (case does not matter)
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &amiga_desc);
    handover_fill(&h, "/SD/x.cor", "Amiga 1200 (AGA, 68020)", "0.3.0", 0, 0, "dummycon");                  // an id that fits no candidate: refused, whatever the name says
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0 && strstr(msg, "stays held"));
    handover_fill(&h, "/SD/x.cor", "", "0.3.0", 0, 0, "amiga1200");                                   // an id alone is enough
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a12twin);
    handover_fill(&h, "/SD/x.cor", "", "0.3.0", 0, 0, "dummycon");                                           // an id alone that fits nothing is a mismatch, not "no bundle"
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(strstr(msg, "\"dummycon\"") != 0);
    // no bundle to choose by: nothing is guessed (the policy for a real mismatch of the ID block is not decided here)
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", 0, &d, msg, sizeof(msg)), START_AMBIGUOUS);
    CHECK(d == 0);
    CHECK(strstr(msg, "Amiga 1200") && strstr(msg, "Amiga (OCS") && strstr(msg, "stays held"));
    handover_fill(&h, "/SD/x.cor", "", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_AMBIGUOUS);
    // T115 D9: no bundle name (a .cfw), but the gateware's feature bits tell the A1200 (AM_FEAT_FBWIN) from the A500
    {
        FakeIo io;
        io.regs[AM_FEAT] = AM_FEAT_FBWIN | 0x01;
        CHECK_EQ(start_identify((const uint8_t *)"AMIG", 0, &d, msg, sizeof(msg), &io), START_OK);
        CHECK(d == &a12twin);
        io.regs[AM_FEAT] = 0x00;
        CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg), &io), START_OK);   // the empty name: the bits decide
        CHECK(d == &amiga_desc);
        handover_fill(&h, "/SD/dummycon.cor", "Dummy Console System", "0.0.1", 0x1234u);
        io.regs[AM_FEAT] = AM_FEAT_FBWIN;
        CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg), &io), START_COR_MISMATCH);   // a name that is wrong stays wrong
        handover_fill(&h, "/SD/x.cor", "", "0.0.1", 0x1234u);
    }

    CHECK(core_find_by_name("Amiga") == 0);                   // a part of the Amiga 500's name is not its name
    DONE("test_start");
}
