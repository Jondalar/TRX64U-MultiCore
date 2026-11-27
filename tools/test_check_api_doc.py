#!/usr/bin/env python3
"""Unit test of tools/check_api_doc.py (T239.3 S1): a complete document passes, a document with a missing section fails and names the symbol,
and the real headers against the real document pass. The fixture is a small copy of the shape of the headers: two call tables, one plain struct.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_api_doc as C  # noqa: E402

LIBAPI = """
#include "svctab.h"
struct LibApi {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    void (*log)(const char *text);
    const SvcSys *sys;
    const SvcLib *lib;
};
struct AppCtx { uint32_t struct_size; const char *action; };
struct LibOpDecl { const char *name; int nargs; };
"""
SVCTAB = """
typedef struct SvcSys { uint32_t size; uint32_t (*ms)(void); /* a comment with a ; and { */ void (*sleep_ms)(uint32_t ms); } SvcSys;
typedef struct SvcLib { uint32_t size; const void *(*find)(const char *name); } SvcLib;
"""
DOC = """# Fixture

## `api->log` and members

Since base 3.0.

| Member | Contract |
|---|---|
| `api->struct_size` | The size. |
| `api->abi_major` | The major. |
| `api->abi_minor` | The minor. |

### `api->log`

Writes a line.

## `api->sys` (`SvcSys`)

Since base 3.0.

The system group.

### `api->sys->ms`

Milliseconds since start.

### `api->sys->sleep_ms`

Sleeps.

## `api->lib` (`SvcLib`)

Since base 3.1.

### `api->lib->find`

Finds a table.

### `Svc*.size`

The size of the table.

## `AppCtx`

Since base 3.0.

| Member | Contract |
|---|---|
| `AppCtx.struct_size` | The size. |
| `AppCtx.action` | The action. |

## `LibOpDecl`

Since base 3.0.

| Member | Contract |
|---|---|
| `LibOpDecl.name` | The name. |
| `LibOpDecl.nargs` | The count. |
"""

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL %s" % what)


SYNC = """
struct SvcMutex;
enum { SYNC_OK = 0, SYNC_BAD = 1 };
#define SYNC_FOREVER 0xFFFFFFFFu
typedef struct SvcSync { uint32_t size; struct SvcMutex *(*mutex_new)(void); void (*enter)(void); } SvcSync;
typedef struct SvcFsAdd { int (*not_a_symbol)(void); } SvcFsAdd;
"""
SYNC_DOC = """
## `api->sync` (`SvcSync`)

Since base 3.3.

### `api->sync->mutex_new`

Makes a mutex.

### `api->sync->enter`

Enters.

### `SvcMutex`

The handle.

### `SYNC_FOREVER`

Wait without a limit.

### `SYNC_OK`, `SYNC_BAD`

The two values of an anonymous enum.
"""


def run(doc_text, group=False):
    d = tempfile.mkdtemp(prefix="apidoc.")
    try:
        os.makedirs(os.path.join(d, "src"))
        open(os.path.join(d, "src", "libapi.h"), "w").write(LIBAPI.replace("const SvcLib *lib;", "const SvcLib *lib;\n    const SvcSync *sync;") if group else LIBAPI)
        if group:
            open(os.path.join(d, "src", "svc_sync.h"), "w").write(SYNC)
        open(os.path.join(d, "src", "svctab.h"), "w").write(SVCTAB)
        open(os.path.join(d, "api.md"), "w").write(doc_text)
        return C.check(os.path.join(d, "src"), os.path.join(d, "api.md"), strict=False)
    finally:
        shutil.rmtree(d)


def main():
    errs, n = run(DOC)
    check(errs == [], "the complete fixture passes: %s" % errs)
    check(n == 18, "the fixture has 18 symbols, found %d" % n)

    errs, _ = run(DOC.replace("### `api->sys->sleep_ms`\n\nSleeps.\n", ""))
    check(len(errs) == 1 and "`api->sys->sleep_ms`" in errs[0], "a missing section names its symbol: %s" % errs)

    errs, _ = run(DOC.replace("| `AppCtx.action` | The action. |\n", ""))
    check(len(errs) == 1 and "`AppCtx.action`" in errs[0], "a missing row names its symbol: %s" % errs)

    errs, _ = run(DOC.replace("### `api->sys->ms`\n\nMilliseconds since start.\n", "### `api->sys->ms`\n"))
    check(len(errs) == 1 and "empty" in errs[0], "an empty section fails: %s" % errs)

    errs, _ = run(DOC.replace("Since base 3.1.\n", "The library group.\n"))
    check(len(errs) == 1 and "Since base" in errs[0] and "`api->lib`" in errs[0], "a group without its version fails: %s" % errs)

    errs, _ = run(DOC + "\n### `api->sys->gone`\n\nA call that is not in the headers.\n")
    check(len(errs) == 1 and "stale" in errs[0], "a stale symbol fails: %s" % errs)

    errs, _ = run(DOC + "\nBody prose that calls `api->sys->logx` and reads `AppCtx.actionx`.\n")
    check(len(errs) == 2 and all("stale" in e for e in errs), "a stale symbol in body prose fails: %s" % errs)

    # base 3.3: the headers of the groups after `lib` are read as well (their members, enumerators, macros, opaque handles), and the Add structs are not symbols
    errs, n = run(DOC + SYNC_DOC, group=True)
    check(errs == [] and n == 18 + 8, "a group header is read: %s, %d symbols" % (errs, n))
    errs, _ = run(DOC + SYNC_DOC.replace("### `api->sync->enter`\n\nEnters.\n", ""), group=True)
    check(len(errs) == 1 and "`api->sync->enter`" in errs[0], "a member of a group header is checked: %s" % errs)
    errs, _ = run(DOC + SYNC_DOC.replace("### `SYNC_OK`, `SYNC_BAD`\n\nThe two values of an anonymous enum.\n", ""), group=True)
    check(len(errs) == 2 and "SYNC_OK" in errs[0] + errs[1] and "SYNC_BAD" in errs[0] + errs[1], "the enumerators of an anonymous enum are checked: %s" % errs)

    errs, n = C.check()
    check(errs == [], "the real headers and the real document agree: %s" % errs[:5])
    check(n > 300, "the real headers have symbols (%d)" % n)

    print("test_check_api_doc: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
