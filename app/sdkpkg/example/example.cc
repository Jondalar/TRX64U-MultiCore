// Example library: one operation, example.greet(name), one state value, example.version, and the hooks of the example core (the export corehooks, api.md 9.7: the code of a core is its
// library). The menu record is menu.toml; the core's description record is app/desc.toml, which names this library with `hooks = "example"`.
// Base 4.0 (T239.3 D58) loads no library file: the library is linked into the core's app by the [[library]] block of app/toml_b.toml (`register = "example_register"`, `menu = "../menu.toml"`,
// `exports = [{ name = "corehooks", major = 1 }]`), and the base calls example_register at start as it called lib_register of a loaded file. It is built only from the SDK package: see the SDK's README.md.
#include <stddef.h>
#include "libapi.h"
#include "libfmt.h"
#include "greet.h"
#include "core_hooks.h"

static int greet(CoreCtx *, const OpArgs &a, OpOut &o)
{
    greet_text(a.s(0), o.msg, o.msglen);
    return OPE_OK;
}

static const char *version_text(CoreCtx *) { return "example 1"; }

static const ArgSpec greet_args[] = { ARG_STRING("name", 32) };
static const LibOpDecl ops[] = { { "example.greet", greet_args, 1, greet } };
static const LibStateDecl states[] = { { "example.version", version_text } };

// The example core's machine is a register block that needs no setup: init and release leave it as it is and answer done. The members after media_pending are not in the table (`size` and the size of the export both end before them), so the base makes no such call.
static int hook_done(char *, int) { return 0; }

static const CoreHooks hooks = { offsetof(CoreHooks, start), hook_done, hook_done, 0, 0, 0, 0 };
static const LibExportDecl exports[] = { { "corehooks", 1, &hooks, (uint32_t)offsetof(CoreHooks, start) } };

extern "C" int example_register(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != U2A_ABI_MAJOR) {
        return 1;
    }
    out->ops = ops;
    out->nops = 1;
    out->states = states;
    out->nstates = 1;
    out->export_list = exports;
    out->nexport_list = 1;
    return 0;
}
