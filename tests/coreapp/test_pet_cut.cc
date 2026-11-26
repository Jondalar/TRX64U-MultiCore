// T228 DC5 (the PET's own app, T239.5 D16): the machine line names the RAM only while its row is shown - the 2001, 3032 and 4032 have the row, the CRTC models have 32 KiB whatever it says.
// The case stood in test_cut.cc (the unit cut) while the PET was in the shared app; it needs only the descriptor, its settings and the menu's machine line.
#include <string>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "menuctl.h"
#include "ops_glue.h"

extern const CoreDesc pet_desc;                 // cores.h lists the shared app's cores; the PET is its own app (cores/pet/app/lib)

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    FakeIo io; FakeFs fs; CoreCtx c; Host host;
    memset(&c, 0, sizeof(c));
    c.desc = &pet_desc; c.io = &io; c.fs = &fs;
    Settings *st = new Settings(pet_desc.settings, pet_desc.nsettings, pet_desc.store, &fs, "/sd/cores/rec.cfg", 0);
    st->load();
    c.settings = st;
    c.started = true;
    ops_of(&c);
    MenuCtl *m = new MenuCtl(&c, &host);
    m->open();
    int model = st->find("Model (at next reset)"), ram = st->find("RAM (at next reset)");
    CHECK(model >= 0 && ram >= 0);
    char line[160];
    st->set(model, 2);                                      // 4032
    m->machine_line(line, sizeof(line));
    CHECK_STR(line, "4032 - RAM 32 KiB");
    st->set(model, 3);                                      // 8032
    m->machine_line(line, sizeof(line));
    CHECK_STR(line, "8032");
    DONE("test_pet_cut");
}
