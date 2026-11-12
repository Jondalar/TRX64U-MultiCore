// The smallest library (T104.1 D10): one operation, test.echo(text), and one state value. It is the build path's proof and the bench's
// real RISC-V image; it needs no libc. Build: app/lib/testlib/Makefile.
#include "libapi.h"

static int echo(CoreCtx *, const OpArgs &a, OpOut &o)
{
    const char *s = a.s(0);
    int i = 0;
    while (s[i] && i < o.msglen - 1) {
        o.msg[i] = s[i];
        i++;
    }
    o.msg[i] = 0;
    return OPE_OK;
}

static const char *version_text(CoreCtx *) { return "testlib 1"; }

static const ArgSpec echo_args[] = { ARG_STRING("text", 64) };
static const LibOpDecl ops[] = { { "test.echo", echo_args, 1, echo } };
static const LibStateDecl states[] = { { "test.version", version_text } };

// lib.ld puts this section first, so the entry is at offset 0 of the image (the packer reads the symbol either way)
#ifdef __riscv
#define LIB_ENTRY_ATTR __attribute__((visibility("default"), section(".text.lib_register")))
#else
#define LIB_ENTRY_ATTR
#endif
extern "C" LIB_ENTRY_ATTR int lib_register(const LibApi *api, LibRecord *out)
{
    if (api->abi_major != 3) {
        return 1;
    }
    out->ops = ops;
    out->nops = 1;
    out->states = states;
    out->nstates = 1;
    return 0;
}
