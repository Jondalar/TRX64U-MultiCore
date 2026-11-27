// T93 S1: the application finds its core, and refuses a core it has no description for or one the bundle does not name.
#include <string.h>
#include "check.h"
#include "start.h"
#include "handover.h"
#include "fakes.h"

static const CoreDesc a = { {'D', '8', 'X', 'L'}, "Dummy 800 XL", "Dummy", "dummy800", "Dummy 800 XL Settings" };
static CoreDesc n = { {'D', 'C', 'O', 'N'}, "Dummy Console System", "Console", "dummycon", "Dummy Console Settings" };
const CoreDesc *const core_table[] = { &a, &n, 0 };

int main()
{
    char msg[300];
    const CoreDesc *d = 0;
    Handover h;
    handover_fill(&h, "/USB1/cores/dummy800.cor", "Dummy 800 XL", "0.0.1", 0x1234u);

    CHECK_EQ(start_identify((const uint8_t *)"D8XL", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &a);
    CHECK_STR(msg, "[core] D8XL Dummy 800 XL, cor /USB1/cores/dummy800.cor");

    handover_fill(&h, "/SD/dummycon.cor", "Dummy Console System", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"DCON", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &n);

    // an ID block that matches no descriptor, in every byte position
    CHECK_EQ(start_identify((const uint8_t *)"A8XM", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(d == 0);
    CHECK(strstr(msg, "A8XM") != 0 && strstr(msg, "stays held") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"B8XL", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK_EQ(start_identify((const uint8_t *)"\0\0\0\0", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(strstr(msg, "????") != 0);                  // unprintable bytes are shown as ?, not as terminal noise

    // a descriptor other than the one the .cor manifest names
    handover_fill(&h, "/USB1/cores/demo.cor", "Amiga", "0.0.1", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"D8XL", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0);
    CHECK(strstr(msg, "Amiga") != 0 && strstr(msg, "Dummy 800 XL") != 0);
    // a descriptor with a core_id that is not shared: the id decides there too
    n.core_id = "dummycon";
    handover_fill(&h, "/SD/dummycon.cor", "Console (renamed)", "1", 0, 0, "dummycon");
    CHECK_EQ(start_identify((const uint8_t *)"DCON", &h, &d, msg, sizeof(msg)), START_OK);
    CHECK(d == &n);
    handover_fill(&h, "/SD/dummycon.cor", "Dummy Console System", "1", 0, 0, "dummy800");           // the name fits, the id says another core
    CHECK_EQ(start_identify((const uint8_t *)"DCON", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    CHECK(d == 0 && strstr(msg, "stays held"));
    handover_fill(&h, "/SD/dummycon.cor", "Dummy Console System", "1", 0, 0);                         // no id (older host): the name, as before
    CHECK_EQ(start_identify((const uint8_t *)"DCON", &h, &d, msg, sizeof(msg)), START_OK);
    n.core_id = 0;

    // no hand-over (JTAG or developer start): runs, without a path and so without a .cfg
    Handover bad;
    memset(&bad, 0xAA, sizeof(bad));
    CHECK_EQ(start_identify((const uint8_t *)"D8XL", &bad, &d, msg, sizeof(msg)), START_OK);
    CHECK(strstr(msg, "no hand-over") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"D8XL", 0, &d, msg, sizeof(msg)), START_OK);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    start_paths(&c, &bad);
    CHECK_STR(c.cor_path, "");
    CHECK_STR(c.cfg_path, "");
    handover_fill(&h, "/SD/cores/dummy800.cor", "Dummy 800 XL", "0.0.7", 1);
    start_paths(&c, &h);
    CHECK_STR(c.cor_path, "/SD/cores/dummy800.cor");
    CHECK_STR(c.cfg_path, "/SD/cores/dummy800/dummy800.cfg");        // T148 D1: in the core folder
    CHECK_STR(c.core_version, "0.0.7");
    // a path that is not a .cor has no .cfg place
    handover_fill(&h, "/SD/dev.bin", "Dummy 800 XL", "0.0.7", 1);
    start_paths(&c, &h);
    CHECK_STR(c.cfg_path, "");
    // the manifest name of the .cor decides: a bundle of the same machine, named otherwise, does not start this core
    handover_fill(&h, "/usb1/cores/dummycon.cor", "Dummy Console System", "1.0.0", 0x1234u);
    CHECK_EQ(start_identify((const uint8_t *)"D8XL", &h, &d, msg, sizeof(msg)), START_COR_MISMATCH);
    // an ID that begins like a core's is not it, and the text names the ID it found; so does the ID block of an older bitstream
    CHECK_EQ(start_identify((const uint8_t *)"A8XM", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(d == 0 && strstr(msg, "A8XM") != 0 && strstr(msg, "stays held") != 0);
    CHECK_EQ(start_identify((const uint8_t *)"A8X\0", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK_EQ(start_identify((const uint8_t *)"RTX2", 0, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
    CHECK(core_find_by_name("dummy 800 xl") == &a);
    DONE("test_start");
}
