// T160 S2 / T164 WP11 / WP18: the "Volume" setting and the menu's SHIFT+ / SHIFT- keys. Every core gets the one generic item (apply = VOLUME_OP) from the application, the
// A1200 included (its own VOL_CFG register and gain stage are gone, WP18): a step reaches CoreIo::master_volume with the byte of the table, the .cfg keeps the value
// ("Volume=110%") and a new session writes it again; the ends clamp; the keys of both keyboards (the C64's '{' and '}', a USB keyboard's '+' and '_', the bare '-')
// work on every page (the bare '+' and '-' not in the F3 Library, where they page: T180 D7); a core with own_volume gets no item and the keys do nothing.
#include <string>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menuctl.h"
#include "regset.h"
#include "keyboard.h"
#include "volume.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st;
    explicit Rig(const CoreDesc *d, FakeFs *shared = 0)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/vol.cfg", 0);
        st->load();
        c.settings = st;
    }
};

static int last_write(FakeIo &io, uint32_t reg)
{
    int v = -1;
    for (auto &w : io.log) if (w.reg == reg) v = w.v;
    return v;
}
static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}
static int lastv(FakeIo &io) { return io.vol_log.empty() ? -1 : io.vol_log.back().v; }   // the host register's last byte, -1: never written
int main()
{
    Host host;
    // ---- T164 WP11: the host's master volume for every core: the byte of the table (the cases on a core run in the apps of the Amiga 500 and the NES)
    CHECK_EQ(volume_byte(0), 0);
    CHECK_EQ(volume_byte(VOLUME_DEFAULT), 128);                       // 100 % is unity
    CHECK_EQ(volume_byte(VOLUME_STEPS - 1), 255);                     // 200 % is the top, saturating
    CHECK_EQ(volume_byte(1), 6);
    CHECK_EQ(volume_byte(22), 141);                                   // 110 %
    DONE("test_volume");
}
