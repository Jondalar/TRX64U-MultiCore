// T230 P-2 (S3, S4, S4b): the start file on the monolith (no hand-over page): looked for on /SD, /USB0, /USB1; taken only when its cor= is
// a .cor of this core (the stub below, named like a monolith core); a file for another core (trx64g's leftover) is dropped and deleted, the cfg_path stays.
#include <string.h>
#include "check.h"
#include "start.h"
#include "handover.h"
#include "fakes.h"

// the core of the file: a stub whose cor_name is the base name of its .cor (what start_identify matches), as test_start.cc does
static const CoreDesc mono = { {'M', 'O', 'N', 'O'}, "TRX64U", "Monolith", "trx64u", "Monolith Settings" };
extern const CoreDesc amiga_desc;
const CoreDesc *const core_table[] = { &mono, &amiga_desc, 0 };

static void mk(FakeFs *fs, const char *path, const char *cor, const char *config)
{
    char t[300];
    int n = startfile_format(t, sizeof(t), cor, config, false);
    CHECK(n > 0);
    fs->put_text(path, std::string(t, n));
}

static void ctx(CoreCtx *c, FakeFs *fs)
{
    memset(c, 0, sizeof(*c));
    c->fs = fs;
    c->desc = &mono;
    strcpy(c->cfg_path, "/SD/cores/trx64u/trx64u.cfg");        // what the monolith has before the file (the profile's own)
}

int main()
{
    char cfg[40];
    bool safe;
    uint64_t sz;
    bool dir;
    // S3: trx64u from /SD, config "Demo": taken, deleted, cor_path/cfg_path of the file
    {
        FakeFs fs;
        CoreCtx c;
        ctx(&c, &fs);
        mk(&fs, "/SD/trx64-start.txt", "/SD/cores/trx64u.cor", "Demo");
        CHECK(start_file_take(&c, 0, cfg, sizeof(cfg), &safe));
        CHECK_STR(cfg, "Demo");
        CHECK(!fs.stat("/SD/trx64-start.txt", &sz, &dir));
        CHECK_STR(c.cor_path, "/SD/cores/trx64u.cor");
        CHECK_STR(c.cfg_path, "/SD/cores/trx64u/trx64u.cfg");
    }
    // S4: trx64u.cor on /USB0: the file lies on the stick
    {
        FakeFs fs;
        CoreCtx c;
        ctx(&c, &fs);
        mk(&fs, "/USB0/trx64-start.txt", "/USB0/cores/trx64u.cor", "Demo");
        CHECK(start_file_take(&c, 0, cfg, sizeof(cfg), &safe));
        CHECK_STR(cfg, "Demo");
        CHECK(!fs.stat("/USB0/trx64-start.txt", &sz, &dir));
        CHECK_STR(c.cor_path, "/USB0/cores/trx64u.cor");
        CHECK_STR(c.cfg_path, "/USB0/cores/trx64u/trx64u.cfg");
    }
    // S4b: a leftover of trx64g on /SD, trx64u started from /USB0: the leftover is dropped and deleted, the stick's file is taken, trx64g.cfg is not touched
    {
        FakeFs fs;
        CoreCtx c;
        ctx(&c, &fs);
        mk(&fs, "/SD/trx64-start.txt", "/SD/cores/trx64g.cor", "Foreign");
        mk(&fs, "/USB0/trx64-start.txt", "/USB0/cores/trx64u.cor", "Demo");
        fs.put_text("/SD/cores/trx64g/trx64g.cfg", "g");
        CHECK(start_file_take(&c, 0, cfg, sizeof(cfg), &safe));
        CHECK_STR(cfg, "Demo");
        CHECK(!fs.stat("/SD/trx64-start.txt", &sz, &dir));
        CHECK_STR(c.cfg_path, "/USB0/cores/trx64u/trx64u.cfg");
        CHECK_STR(fs.text("/SD/cores/trx64g/trx64g.cfg").c_str(), "g");
    }
    // S4b alone: only the leftover exists: nothing taken, cor_path and cfg_path untouched
    {
        FakeFs fs;
        CoreCtx c;
        ctx(&c, &fs);
        mk(&fs, "/SD/trx64-start.txt", "/SD/cores/trx64g.cor", "Foreign");
        CHECK(!start_file_take(&c, 0, cfg, sizeof(cfg), &safe));
        CHECK_STR(cfg, "");
        CHECK(!fs.stat("/SD/trx64-start.txt", &sz, &dir));
        CHECK_STR(c.cor_path, "");
        CHECK_STR(c.cfg_path, "/SD/cores/trx64u/trx64u.cfg");
    }
    // a file that is no start file is dropped, the next medium is still looked at
    {
        FakeFs fs;
        CoreCtx c;
        ctx(&c, &fs);
        fs.put_text("/SD/trx64-start.txt", "noise");
        mk(&fs, "/USB1/trx64-start.txt", "/USB1/cores/TRX64U.COR", "X");
        CHECK(start_file_take(&c, 0, cfg, sizeof(cfg), &safe));
        CHECK_STR(cfg, "X");
    }
    return 0;
}
