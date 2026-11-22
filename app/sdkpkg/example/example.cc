// Example library: one operation, example.greet(name), and one state value, example.version. The menu record is menu.toml.
// It is built only from the SDK package: see README.md.
#include "libapi.h"
#include "libfmt.h"
#include "greet.h"

static int greet(CoreCtx *, const OpArgs &a, OpOut &o)
{
    greet_text(a.s(0), o.msg, o.msglen);
    return OPE_OK;
}

static const char *version_text(CoreCtx *) { return "example 1"; }

static const ArgSpec greet_args[] = { ARG_STRING("name", 32) };
static const LibOpDecl ops[] = { { "example.greet", greet_args, 1, greet } };
static const LibStateDecl states[] = { { "example.version", version_text } };

// lib.ld puts this section first, so the entry is at offset 0 of the image
#ifdef __riscv
#define LIB_ENTRY_ATTR __attribute__((visibility("default"), section(".text.lib_register")))
#else
#define LIB_ENTRY_ATTR
#endif
extern "C" LIB_ENTRY_ATTR int lib_register(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != U2A_ABI_MAJOR) {
        return 1;
    }
    out->ops = ops;
    out->nops = 1;
    out->states = states;
    out->nstates = 1;
    return 0;
}
