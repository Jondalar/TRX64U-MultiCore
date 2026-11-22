// Host test of the example library: it compiles example.cc and greet.cc natively, registers the library against a fake LibApi and calls the operation.
// `make test` builds and runs it with the host's C++ compiler.
#include <stdio.h>
#include <string.h>
#include "libapi.h"
#include "libfmt.h"

extern "C" int lib_register(const LibApi *api, LibRecord *out);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main()
{
    LibApi api;
    LibRecord rec;
    memset(&api, 0, sizeof api);
    memset(&rec, 0, sizeof rec);
    api.struct_size = sizeof api;
    api.abi_major = U2A_ABI_MAJOR;
    api.abi_minor = U2A_BASE_MINOR;
    CHECK(lib_register(&api, &rec) == 0);
    CHECK(rec.nops == 1 && strcmp(rec.ops[0].name, "example.greet") == 0);
    CHECK(rec.nstates == 1 && strcmp(rec.states[0].name, "example.version") == 0);

    char msg[16];
    OpArgs a;
    OpOut o;
    memset(&a, 0, sizeof a);
    memset(&o, 0, sizeof o);
    a.n = 1;
    a.sv[0] = "SDK";
    o.msg = msg;
    o.msglen = sizeof msg;
    CHECK(rec.ops[0].fn(0, a, o) == OPE_OK);
    CHECK(strcmp(msg, "Hello, SDK!") == 0);

    a.sv[0] = "a name that is far too long";   // cut at the buffer, still NUL terminated
    CHECK(rec.ops[0].fn(0, a, o) == OPE_OK);
    CHECK(strlen(msg) == sizeof msg - 1);

    api.abi_major = U2A_ABI_MAJOR + 1;          // a base of another major is refused
    CHECK(lib_register(&api, &rec) != 0);

    printf(fails ? "test_example: FAILED\n" : "test_example: ok\n");
    return fails ? 1 : 0;
}
