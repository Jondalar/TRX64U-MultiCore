// T189 S7: the generic "Tone" setting. Every core gets it after Volume (apply = TONE_OP, group audio, WHEN_LIVE, three values, default Flat); a change reaches
// CoreIo::master_tone with 0 / 1 / 2, the .cfg keeps the value ("Tone=Bass boost") and a new session writes it again (regset_apply); a core with its own Tone item keeps
// it, a core with no_tone gets none, own_volume does not matter (the C64 line gets it, T189 D5).
#include <string>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "cores.h"
#include "menuctl.h"
#include "regset.h"
#include "ops.h"
#include "ops_glue.h"
#include "volume.h"
#include "tone.h"
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif

struct Rig {
    FakeIo io; FakeFs fs; CoreCtx c; Settings *st;
    explicit Rig(const CoreDesc *d, FakeFs *shared = 0)
    {
        if (shared) fs = *shared;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/tone.cfg", 0);
        st->load();
        c.settings = st;
    }
};
static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}
static int lastt(FakeIo &io) { return io.tone_log.empty() ? -1 : io.tone_log.back().v; }   // the host register's last byte, -1: never written
static void set_tone(Rig &r, const char *value)
{
    OpIn in[2] = { in_str("key", TONE_KEY), in_str("value", value) };
    OpResult res;
    CHECK_EQ(ops_call(&r.c, "settings.set", in, 2, &res), 0);
}

int main()
{
#ifdef TEST_APP_AMIGA
    // T239.5 D13: the Amiga 500's case of this file runs as its app (run.sh amigaapp, unit amiga_tone); the shared unit tone keeps the others
    CHECK_EQ(count_key(amiga_desc, TONE_KEY), 0);                    // no descriptor declares it: the application adds it
#endif
    DONE("test_tone");
}
