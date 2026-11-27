// A sub-app that record 8 of the bundle installs into the core's apps folder is listed at the FIRST start (TRX64-Ultimate finding). Two parts:
//  - the functions: before core_files_install the apps folder of the core is empty, after it AppIndex::scan lists the file (the bundle is made by tests/coreapp/gen_bundle.py: subapp.cor);
//  - the order in coreapp_run (coreapp/src/run.cc): it calls core_files_install before it calls app_index.scan. coreapp_run needs the board, so the test reads the source for the two calls.
//
//   test_firststart DIR       (DIR holds subapp.cor)
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "cores.h"
#include "fakes.h"
#include "test_core.h"
#include "bundle.h"
#include "instfiles.h"
#include "appindex.h"

const CoreDesc *const core_table[] = { &testcore::desc, 0 };

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

struct Log : LibLog {
    void line(const char *) override { }
};

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    Bytes cor = slurp(std::string(argv[1]) + "/subapp.cor");
    CHECK(cor.size() > 100);

    FakeFs fs;
    FakeIo io;
    fs.put("/usb0/cores/subapp.cor", cor);
    CoreDesc dd = testcore::desc;
    dd.cor_name = "Sub App Core";
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &dd;
    c.fs = &fs;
    c.io = &io;
    c.host_key = 0x820eded2;
    snprintf(c.cor_path, sizeof(c.cor_path), "%s", "/usb0/cores/subapp.cor");
    static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
    int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
    CHECK(nf >= 1);
    CHECK_STR(folders[0], "/usb0/cores/subapp/apps");
    Log log;
    AppIndex idx;
    idx.scan(&fs, folders, nf, &log);
    CHECK_EQ(idx.count(), 0);                                           // the folder does not exist yet
    CHECK_EQ(core_files_install(&c), 1);                                // record 8 writes apps/probe.u2a
    CHECK(fs.has("/usb0/cores/subapp/apps/probe.u2a"));
    idx.scan(&fs, folders, nf, &log);
    CHECK_EQ(idx.count(), 1);                                           // listed once it is installed
    CHECK(idx.find("probe") != 0);

    // the order of the start
    std::string src;
    {
        std::ifstream f(std::string(COREAPP_DIR) + "/src/run.cc");
        CHECK(f.good());
        std::stringstream s;
        s << f.rdbuf();
        src = s.str();
    }
    size_t inst = src.find("core_files_install(&ctx);");
    size_t scan = src.find("app_index.scan(");
    CHECK(inst != std::string::npos && scan != std::string::npos);
    CHECK(inst < scan);
    CHECK(src.find("core_files_install(&ctx);", inst + 1) == std::string::npos);           // once

    DONE("test_firststart");
}
