// T93 S1: the application finds its core, and refuses a core it has no description for or one the bundle does not name.
#include <string.h>
#include "check.h"
#include "start.h"
#include "handover.h"
#include "fakes.h"
#ifdef TEST_APP_AMIGA
#include "amiga_regs.h"
#endif

static const CoreDesc a = { {'A', '8', 'X', 'L'}, "Atari 800 XL", "Atari", "atari800", "Atari 800 XL Settings" };
static CoreDesc n = { {'N', 'E', 'S', '_'}, "Nintendo Entertainment System", "NES", "nes", "NES Settings" };
#ifdef TEST_APP_AMIGA
// T239.5 D13: the Amiga 500's cases run as its app (run.sh amigaapp, unit amiga_start); the shared unit start keeps the cases of the two stubs.
extern const CoreDesc amiga_desc;
// T213.3 (T239.5 D13): the A1200 has left the shared app, so no app has two records with one ID word any more. start_identify still chooses
// among such records (by the bundle's name, the core's id, the gateware's feature bits); a twin of the A500's record with the A1200's identity
// keeps those cases (filled at the start of main).
static CoreDesc a12twin;
const CoreDesc *const core_table[] = { &a, &n, &amiga_desc, &a12twin, 0 };
#else
const CoreDesc *const core_table[] = { &a, &n, 0 };
#endif

int main()
{
    char msg[300];
#ifdef TEST_APP_AMIGA
    a12twin = amiga_desc;
    a12twin.cor_name = "Amiga 1200 (AGA, 68020)";
    a12twin.title = "Amiga 1200";
    a12twin.core_id = "amiga1200";
    a12twin.id_feat_val = AM_FEAT_FBWIN;                                      // the A1200's gateware has the display window (T115 D9)
#endif
    const CoreDesc *d = 0;
    Handover h;
#ifndef TEST_APP_AMIGA
    handover_fill(&h, "/USB1/cores/atari800.cor", "Atari 800 XL", "0.0.1", 0x1234u);

    CHECK_EQ(start_identify((const uint8_t *)"A8XL", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a);
    CHECK_STR(msg, "[core] A8XL Atari 800 XL, cor /USB1/cores/atari800.cor");

    handover_fill(&h, "/SD/nes.cor", "Nintendo Entertainment System", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"NES_", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &n);

    // an ID block that matches no descriptor, in every byte position
    CHECK_EQ(start_identify((const uint8_t *)"A8XM", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(d == 0);
    CHECK(strstr(msg, "A8XM") != 0 && strstr(msg, "stays held") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"B8XL", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK_EQ(start_identify((const uint8_t *)"\0\0\0\0", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(strstr(msg, "????") != 0);                  // unprintable bytes are shown as ?, not as terminal noise

    // a descriptor other than the one the .cor manifest names
    handover_fill(&h, "/USB1/cores/amiga.cor", "Amiga", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"A8XL", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0);
    CHECK(strstr(msg, "Amiga") != 0 && strstr(msg, "Atari 800 XL") != 0);
#else
    // T104 5.5: the Amiga and the A1200 are one gateware with one ID word - the bundle's name tells the descriptors apart
    handover_fill(&h, "/SD/amiga1200.cor", "Amiga 1200 (AGA, 68020)", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a12twin);
    CHECK(strstr(msg, "Amiga 1200") != 0);
    handover_fill(&h, "/SD/amiga.cor", "Amiga (OCS/ECS, 68000)", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &amiga_desc);
    // a bundle of another machine on this gateware: refused, the message names the bundle and both candidates
    handover_fill(&h, "/SD/nes.cor", "Nintendo Entertainment System", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0);
    CHECK(strstr(msg, "Nintendo") && strstr(msg, "Amiga 1200") && strstr(msg, "Amiga (OCS"));
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
    handover_fill(&h, "/SD/x.cor", "Amiga 1200 (AGA, 68020)", "0.3.0", 0, 0, "nes");                  // an id that fits no candidate: refused, whatever the name says
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0 && strstr(msg, "stays held"));
    handover_fill(&h, "/SD/x.cor", "", "0.3.0", 0, 0, "amiga1200");                                   // an id alone is enough
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a12twin);
    handover_fill(&h, "/SD/x.cor", "", "0.3.0", 0, 0, "nes");                                           // an id alone that fits nothing is a mismatch, not "no bundle"
    CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(strstr(msg, "\"nes\"") != 0);
#endif
#ifndef TEST_APP_AMIGA
    // a descriptor with a core_id that is not shared: the id decides there too
    n.core_id = "nes";
    handover_fill(&h, "/SD/nes.cor", "NES (renamed)", "1", 0, 0, "nes");
    CHECK_EQ(start_identify((const uint8_t *)"NES_", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &n);
    handover_fill(&h, "/SD/nes.cor", "Nintendo Entertainment System", "1", 0, 0, "atari800");           // the name fits, the id says another core
    CHECK_EQ(start_identify((const uint8_t *)"NES_", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0 && strstr(msg, "stays held"));
    handover_fill(&h, "/SD/nes.cor", "Nintendo Entertainment System", "1", 0, 0);                         // no id (older host): the name, as before
    CHECK_EQ(start_identify((const uint8_t *)"NES_", &h, &d, msg, sizeof(msg)), START_OK);
    n.core_id = 0;
#else
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
        handover_fill(&h, "/SD/nes.cor", "Nintendo Entertainment System", "0.0.1", 0x1234u);
        io.regs[AM_FEAT] = AM_FEAT_FBWIN;
        CHECK_EQ(start_identify((const uint8_t *)"AMIG", &h, &d, msg, sizeof(msg), &io), START_COR_MISMATCH);   // a name that is wrong stays wrong
        handover_fill(&h, "/SD/x.cor", "", "0.0.1", 0x1234u);
    }
#endif

#ifndef TEST_APP_AMIGA
    // no hand-over (JTAG or developer start): runs, without a path and so without a .cfg
    Handover bad;
    memset(&bad, 0xAA, sizeof(bad));
    CHECK_EQ(start_identify((const uint8_t *)"A8XL", &bad, &d, msg, sizeof(msg)), START_OK);
    CHECK(strstr(msg, "no hand-over") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"A8XL", 0, &d, msg, sizeof(msg)), START_OK);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    start_paths(&c, &bad);
    CHECK_STR(c.cor_path, "");
    CHECK_STR(c.cfg_path, "");
    handover_fill(&h, "/SD/cores/atari800.cor", "Atari 800 XL", "0.0.7", 1);
    start_paths(&c, &h);
    CHECK_STR(c.cor_path, "/SD/cores/atari800.cor");
    CHECK_STR(c.cfg_path, "/SD/cores/atari800/atari800.cfg");        // T148 D1: in the core folder
    CHECK_STR(c.core_version, "0.0.7");
    // a path that is not a .cor has no .cfg place
    handover_fill(&h, "/SD/dev.bin", "Atari 800 XL", "0.0.7", 1);
    start_paths(&c, &h);
    CHECK_STR(c.cfg_path, "");
    // the manifest name of the .cor decides: a bundle of the same machine, named otherwise, does not start this core
    handover_fill(&h, "/usb1/cores/nes.cor", "Nintendo Entertainment System", "1.0.0", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"A8XL", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    // an ID that begins like a core's is not it, and the text names the ID it found; so does the ID block of an older bitstream
    CHECK_EQ(start_identify((const uint8_t *)"A8XM", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(d == 0 && strstr(msg, "A8XM") != 0 && strstr(msg, "stays held") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"A8X\0", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK_EQ(start_identify((const uint8_t *)"RTX2", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(core_find_by_name("atari 800 xl") == &a);
#else
    CHECK(core_find_by_name("Amiga") == 0);                   // a part of the Amiga 500's name is not its name
#endif
    DONE("test_start");
}
