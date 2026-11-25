// T260 S7 golden: the register writes of the T259 A/V page (the C code of portdiag.cc), per shipped preset and per RIGHT on each row. The panel runner
// (cores/diag/app/lib/panel.cc) must write the same registers in the same order; tests/coreapp/golden/diag_av_trace.txt is that record.
// UPDATE_GOLDEN=1 writes the file, otherwise the run compares with it.
#include "check.h"
#include "fakes.h"
#include "boot.h"
#include "cores.h"
#include "ops_glue.h"
#include "portdiag.h"
#include "portdiag_regs.h"
#include "diagtest.h"
#include "settings_ops.h"
#include "settings.h"
#include <fstream>
#include <sstream>

static std::string slurp(const std::string &p)
{
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Rig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    OpRegistry reg;
    Settings *st;
    Rig()
    {
        memset(&c, 0, sizeof(c));
        c.desc = &portdiag_desc;
        c.io = &io;
        c.fs = &fs;
        snprintf(c.core_version, sizeof(c.core_version), "0.1.0");
        snprintf(c.cor_path, sizeof(c.cor_path), "/SD/cores/diag.cor");
        portdiag_desc.register_ops(reg);
        settings_register(reg);
        c.ops = &reg;
        st = new Settings(portdiag_desc.settings, portdiag_desc.nsettings, portdiag_desc.store, &fs, "/SD/cores/diag.cfg", 0);
        st->load();
        c.settings = st;
    }
    ~Rig() { delete st; }
    void dump(std::string &out)
    {
        char b[64];
        for (auto &w : io.log) {
            snprintf(b, sizeof(b), "w %03X %02X\n", (unsigned)w.reg, (unsigned)w.v);
            out += b;
        }
        snprintf(b, sizeof(b), "vfmt %02X\n", (unsigned)io.vfmt);
        out += b;
        io.log.clear();
    }
};

int main()
{
    std::string out = "# T260 S7 golden: the A/V page's register writes (portdiag.cc as of T259), `w <register> <value>` in order\n";
    const char *presets[] = { "Sony-PAL", "NTSC-50.rix01", "NTSC-60.rix02" };
    for (const char *pn : presets) {
        Rig r;
        r.fs.put_text(std::string("/SD/cores/diag/presets/") + pn + ".toml", slurp(std::string(COREAPP_DIR) + "/../cores/diag/sd/presets/" + pn + ".toml"));
        OpResult res;
        OpIn in = in_str("name", pn);
        r.io.log.clear();
        CHECK_EQ(r.reg.call(&r.c, "pd.av_preset", &in, 1, &res), OPE_OK);
        out += std::string("preset ") + pn + "\n";
        r.dump(out);
        // RIGHT on each row from this preset's state: the next value of the row, the last one wraps to the first
        for (int k = 0; k < DT_AV_N; k++) {
            const AvKnob &kn = dt_av_knobs[k];
            int cur = r.st->value(r.st->find(kn.key));
            int nx = (cur + 1) % kn.n;
            r.io.log.clear();
            CHECK_EQ(settings_set(&r.c, kn.key, kn.labels[nx], &res), OPE_OK);
            out += std::string("right ") + pn + " / " + kn.key + " -> " + kn.labels[nx] + "\n";
            r.dump(out);
            settings_set(&r.c, kn.key, kn.labels[cur], &res);        // back, so every row starts from the preset
            r.io.log.clear();
        }
    }
    std::string gp = std::string(GOLDEN_DIR) + "/diag_av_trace.txt";
    if (getenv("UPDATE_GOLDEN")) {
        std::ofstream(gp) << out;
        printf("wrote %s (%d bytes)\n", gp.c_str(), (int)out.size());
    } else {
        std::string g = slurp(gp);
        CHECK(!g.empty());
        CHECK(g == out);
    }
    DONE("test_diag_avtrace");
}
