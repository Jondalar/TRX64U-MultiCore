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


def run(doc_text):
    d = tempfile.mkdtemp(prefix="apidoc.")
    try:
        os.makedirs(os.path.join(d, "src"))
        open(os.path.join(d, "src", "libapi.h"), "w").write(LIBAPI)
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

    errs, n = C.check()
    check(errs == [], "the real headers and the real document agree: %s" % errs[:5])
    check(n > 300, "the real headers have symbols (%d)" % n)

    print("test_check_api_doc: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
