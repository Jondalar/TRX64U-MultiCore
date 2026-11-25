// The host key across the hand-over: the host application stamps the key it read from the BUILD register into the page, the core application
// parses it in start_paths and F8 "Reload core" compares it with the .cor's manifest in partial_check. Board 2026-11-04 (main-91f629ce, host
// 96F6AB08): "built for another host" for a core the same host had just started.
#include <string.h>
#include <string>
#include <vector>
#include "check.h"
#include "start.h"
#include "handover.h"
#include "partial.h"
#include "fakes.h"
#include "cor_build.h"
#include "cor.h"

static const CoreDesc vc = { {'V', 'C', '2', '0'}, "VC20", "VIC-20", "vc20", "VIC-20 Settings" };
const CoreDesc *const core_table[] = { &vc, 0 };             // T239.5 D13: no moved core in the shared unit (the A500 was a second row no case read)

int main()
{
    std::vector<uint8_t> partial(4096, 0x5a);
    const uint32_t running = 0x96f6ab08u;                          // what running_host_key() returns on the board (hostapp.cc)
    const char *path = "/usb0/cores/vc20.cor";
    for (int pass = 0; pass < 2; pass++) {
        // pass 0: the .cor is for the running host; pass 1: a .cor really built for another host (negative control)
        FakeFs fs;
        FakeIo io;
        std::vector<uint8_t> cor = make_cor("VC20", pass ? "96f6ab09" : "96f6ab08", partial);
        fs.put(path, cor);

        // host side: core_stage's cor_probe with the running key, core_jump's handover_fill with the same key
        cor_host_key = running;
        Handover page;
        handover_fill(&page, path, "VC20", "0.1", cor_host_key);

        // core side: the page as copied by app_task, start_paths, then the F8 check
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = &vc;
        c.fs = &fs;
        c.io = &io;
        start_paths(&c, &page);
        CHECK_EQ(c.host_key, running);
        cor_host_key = 0;                                          // the core application is another program: the global starts at 0
        CorInfo ci;
        char msg[200];
        int rc = partial_check(&c, &ci, msg, sizeof(msg));
        if (pass == 0) {
            if (rc) printf("FAIL reload refused for the running host: %s\n", msg);
            CHECK_EQ(rc, 0);
        } else {
            CHECK_EQ(rc, 1);
            CHECK(strstr(msg, "built for another host") != 0);
            CHECK(strstr(msg, "96f6ab09") != 0 && strstr(msg, "96f6ab08") != 0);      // the message names both keys (what the file says, what the host is)
        }
    }
    // board 2026-11-06 (main-a9ef1237, Apple II, host 2BB1DE24): the firmware's small_printf writes %x in capitals, the page held "2BB1DE24"
    // and start_paths read 0x20010024. Capitals parse to the same key, Reload accepts the matching .cor, a character that is no hex digit voids it.
    {
        const char *forms[] = { "2BB1DE24", "2bb1de24", "2bB1dE24" };
        for (int k = 0; k < 3; k++) {
            FakeFs fs;
            FakeIo io;
            fs.put("/SD/cores/apple2.cor", make_cor("VC20", "2bb1de24", partial));
            Handover page;
            handover_fill(&page, "/SD/cores/apple2.cor", "VC20", "0.1", 0x2bb1de24u);
            memset(page.host_build, 0, sizeof(page.host_build));
            strcpy(page.host_build, forms[k]);
            page.sum = handover_sum(&page);
            CoreCtx c;
            memset(&c, 0, sizeof(c));
            c.desc = &vc;
            c.fs = &fs;
            c.io = &io;
            start_paths(&c, &page);
            CHECK_EQ(c.host_key, 0x2bb1de24u);
            cor_host_key = 0;
            CorInfo ci;
            char msg[200];
            int rc = partial_check(&c, &ci, msg, sizeof(msg));
            if (rc) printf("FAIL reload refused for %s: %s\n", forms[k], msg);
            CHECK_EQ(rc, 0);
        }
        Handover page;
        handover_fill(&page, "/SD/cores/apple2.cor", "VC20", "0.1", 0x2bb1de24u);
        CHECK_STR(page.host_build, "2bb1de24");                    // the writer's own form, whatever the libc prints
        const char *bad[] = { "2BB1ZE24", "2bb1 e24", "0x2bb1de" };
        for (int k = 0; k < 3; k++) {
            memset(page.host_build, 0, sizeof(page.host_build));
            strcpy(page.host_build, bad[k]);
            page.sum = handover_sum(&page);
            CoreCtx c;
            memset(&c, 0, sizeof(c));
            c.desc = &vc;
            start_paths(&c, &page);
            CHECK_EQ(c.host_key, 0u);
        }
    }
    DONE("test_hostkey");
}
