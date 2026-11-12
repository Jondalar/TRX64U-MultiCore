// T104.2 D10: what the TRX64U application exports to a menu description - every operation with its typed arguments and every state value, as the
// registry holds them for the C64 descriptor. tools/descrec.py checks a menu.toml against tests/coreapp/golden/ops_trx64u.txt when it is compiled.
// That golden is the export of the REAL image (tools: tests/ui/export.py --update; the suite's export.feature compares it with a running image): the
// image registers the board-side contributors too (u64cfg, libfiles, ringops, monitor, online, diag, netops, trx64u), which this native program does not
// link: tests/coreapp/board_stubs.cc registers, for them, whatever of the golden is not registered natively (names and shapes only). So the list compared here is
// the native definitions (the base, the C64's, the drive library's) plus the golden's rest; it has to be the golden's lines, in any order: a renamed or reshaped
// native operation or state shows as one extra and one missing line.
//
//   ops_export            writes the list to stdout:  op <TAB> name <TAB> arg:type:lo:hi:maxlen:label|label ...   and   state <TAB> name
//   ops_export FILE       the test: the list has to be the file (never written from here: the golden is the image's export); a line "FAIL" says what differs
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "ctx.h"
#include "ops_glue.h"
#include "cores.h"

const CoreDesc *const core_table[] = { &c64_desc, 0 };

static void put(std::string *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(std::string *out, const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    *out += b;
}

int main(int argc, char **argv)
{
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.desc = &c64_desc;
    OpRegistry *r = ops_of(&c);
    static const char *const type[] = { "int", "bool", "enum", "string", "path", "slot", "blob" };
    std::string out;
    for (int i = 0; i < r->nops(); i++) {
        const OpDef *o = r->op_at(i);
        put(&out, "op\t%s", o->name);
        for (int k = 0; k < o->nargs; k++) {
            const ArgSpec &a = o->args[k];
            put(&out, "\t%s:%s:%d:%d:%u:", a.name, type[a.type], (int)a.lo, (int)a.hi, (unsigned)a.maxlen);
            for (int32_t l = a.lo; a.type == AT_ENUM && a.labels && l <= a.hi; l++) {
                put(&out, "%s%s", l > a.lo ? "|" : "", a.labels[l]);
            }
        }
        put(&out, "\n");
    }
    for (int i = 0; i < r->nstates(); i++) {
        put(&out, "state\t%s\n", r->state_at(i)->name);
    }
    if (argc < 2) {
        fputs(out.c_str(), stdout);
        return 0;
    }
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    auto sorted = [](const std::string &t) {
        std::vector<std::string> v;
        std::stringstream is(t);
        std::string l;
        while (std::getline(is, l)) v.push_back(l);
        std::sort(v.begin(), v.end());
        return v;
    };
    if (sorted(ss.str()) != sorted(out)) {
        printf("FAIL the operations and states the application exports are not %s:\n--- want\n%s--- got\n%s", argv[1], ss.str().c_str(), out.c_str());
        return 1;
    }
    printf("ops_export: %d operations, %d states, as the golden\n", r->nops(), r->nstates());
    return 0;
}
