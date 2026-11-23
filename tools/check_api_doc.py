#!/usr/bin/env python3
"""T239.3 S1: the API document of the SDK (app/sdkpkg/api.md) covers every symbol of the public headers of the core application.

The headers are read from coreapp/src: libapi.h, svctab.h, ops.h and libfmt.h, and the headers of the groups that base 3.3 added (GROUP_HEADERS: svc_sync.h,
svc_settings.h, svc_ctx.h, svc_io.h, svc_board.h, svc_overlay.h, svc_fs.h; the Add structs of svc_sys.h and svc_fs.h repeat members of SvcSys and SvcFs and are not read). The symbols are

  api->NAME            each member of LibApi (struct_size, abi_major, abi_minor, log and the nine call tables)
  api->GROUP->NAME     each member of a call table; a table behind a table member (SvcTrx.ring -> SvcRing) is api->trx->ring->NAME
  Svc*.size            the member `size` that starts every call table (one section for all)
  TYPE                 each struct, enum and function-pointer type of libapi.h and svctab.h, the opaque handles, and the listed types of ops.h
  TYPE.member          each member of a struct that is not a call table (AppCtx, the Lib*Decl, LibRecord, SvcFsEntry, the u2p_* records, OpIn, ...)
  ENUMERATOR, MACRO    each enumerator and each listed macro or helper function

A symbol is documented when the document has a heading with the symbol as a code span (`like this`), or a table row whose first cell is the
symbol as a code span, and the section or the row has text. A group (api->GROUP) and every Lib*Decl, LibRecord and AppCtx section also states
the base version in a line that holds "Since base". A code span anywhere in the document (prose included) that names a symbol in the form api->... or TYPE.member and
is not in the headers is stale and fails the check.

Usage: tools/check_api_doc.py [--src DIR] [--doc FILE]     exit 1 and one line for each missing, empty or stale symbol
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "coreapp", "src")
DOC = os.path.join(ROOT, "app", "sdkpkg", "api.md")

# What is taken from ops.h and libfmt.h (everything of libapi.h and svctab.h is taken). A name that is listed and not in the header is an error
# of the real tree: the header was renamed and this list was not.
OPS_TYPES = ["OpErr", "ArgType", "ArgSpec", "OpIn", "OpArgs", "OpOut", "OpResult", "BindKind", "OpBind", "op_fn", "state_fn"]
OPS_NAMES = ["in_int", "in_str", "in_blob", "OP_ARGS_MAX", "OP_MSG_MAX", "OP_DATA_MAX", "OPS_MAX", "STATES_MAX", "EVENTS_MAX"]
OPS_MACRO_PREFIXES = ("ARG_", "B_")
FMT_NAMES = ["U2A_ABI_MAJOR", "U2A_BASE_MINOR", "U2A3_VERSION", "U2A3_NAME_MAX", "U2A3_NEEDS_MAX", "U2A3_IMAGE_MAX", "U2A3_ALIGN_MAX",
             "PRG_APP_NEED", "CART_APP_NEED", "U2A3_FLAG_BUDGET", "U2A3_BUDGET_NONE", "U2A3_FLAG_EXPORTS", "U2A3_EXPORTS_MAX"]
SVC_MACRO_PREFIXES = ("SVC_BUTTON_", "U2P_TRACE_HIT_", "CTX_", "SYNC_")
# base 3.3: one header for each group that follows `lib` in LibApi (their structs, enums, macros and opaque handles are read like those of svctab.h)
GROUP_HEADERS = ["svc_sync.h", "svc_settings.h", "svc_ctx.h", "svc_io.h", "svc_board.h", "svc_overlay.h", "svc_fs.h"]
NOT_SYMBOLS = {"SvcFsAdd", "SvcSysAdd"}
SINCE_TYPES = ("AppCtx", "LibRecord")        # and every Lib*Decl


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def match_brace(text, i):
    """text[i] is '{'; the index of its '}'."""
    depth = 0
    for k in range(i, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return k
    raise ValueError("unbalanced braces")


def top_level(body):
    """The body of a struct with every nested {...} (an inline function) replaced by ';'."""
    out = []
    k = 0
    while k < len(body):
        if body[k] == "{":
            k = match_brace(body, k) + 1
            out.append(";")
        else:
            out.append(body[k])
            k += 1
    return "".join(out)


def members(body):
    """[(type text, name)] of the declarations of a struct body."""
    res = []
    for d in top_level(body).split(";"):
        d = " ".join(d.split())
        d = re.sub(r"^(public|private|protected)\s*:\s*", "", d)
        if not d or re.match(r"(typedef|enum|static|friend|using)\b", d):
            continue
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)", d)
        if m:
            res.append((d, m.group(1)))
            continue
        if "(" in d:
            m = re.search(r"(\w+)\s*\(", d)
            if m:
                res.append((d, m.group(1)))
            continue
        d = d.split("=")[0]
        for part in d.split(","):
            m = re.search(r"(\w+)\s*(\[[^\]]*\]\s*)*$", part.strip())
            if m:
                res.append((part, m.group(1)))
    return res


def structs(text):
    """{name: [(decl, member)]} of the structs of one header, the first definition of a name wins."""
    found = {}
    for m in re.finditer(r"(typedef\s+)?struct\s*(\w*)\s*\{", text):
        end = match_brace(text, m.end() - 1)
        tail = re.match(r"\s*(\w*)\s*;", text[end + 1:])
        name = m.group(2) or (tail.group(1) if tail else "")
        if name and name not in found:
            found[name] = members(text[m.end():end])
    return found


def enums(text):
    """{name: [enumerators]}; an enum with no name is listed under ''."""
    found = {}
    for m in re.finditer(r"enum\s*(\w*)\s*\{(.*?)\}", text, flags=re.S):
        found.setdefault(m.group(1), []).extend(e.split("=")[0].strip() for e in m.group(2).split(",") if e.split("=")[0].strip())
    return found


def fn_typedefs(text):
    return re.findall(r"typedef\s+[\w\s\*]+?\(\s*\*\s*(\w+)\s*\)\s*\(", text)


def macros(text):
    return re.findall(r"^\s*#\s*define\s+(\w+)", text, flags=re.M)


def read(src, name):
    p = os.path.join(src, name)
    if not os.path.exists(p):
        return None
    return strip_comments(open(p, encoding="utf-8").read())


def symbols(src, strict):
    """(ordered dict symbol -> kind, list of errors)"""
    sym = {}
    err = []
    libapi, svctab, ops, fmt = (read(src, n) for n in ("libapi.h", "svctab.h", "ops.h", "libfmt.h"))
    if libapi is None or svctab is None:
        return sym, ["%s: libapi.h or svctab.h is missing" % src]
    extra = [t for t in (read(src, n) for n in GROUP_HEADERS) if t is not None]
    st = structs(libapi)
    for k, v in structs(svctab).items():
        st.setdefault(k, v)
    for t in extra:
        for k, v in structs(t).items():
            if k not in NOT_SYMBOLS:
                st.setdefault(k, v)
    if "LibApi" not in st:
        return sym, ["LibApi is not in libapi.h"]

    # the call tables: the structs reached from LibApi through a pointer member of a Svc type
    prefix = {"LibApi": "api"}
    todo = ["LibApi"]
    while todo:
        s = todo.pop()
        for decl, name in st[s]:
            m = re.search(r"(?:const\s+)?(Svc\w+)\s*\*\s*\w+\s*$", decl)
            if m and m.group(1) in st and m.group(1) not in prefix:
                prefix[m.group(1)] = "%s->%s" % (prefix[s], name)
                todo.append(m.group(1))
    tables = {t for t in prefix if t != "LibApi"}

    for s, ms in st.items():
        if s == "LibApi":
            for decl, name in ms:
                sym["api->" + name] = "member"
                m = re.search(r"(Svc\w+)\s*\*", decl)
                if m and m.group(1) in tables:
                    sym[m.group(1)] = "type"
        elif s in tables:
            sym[s] = "type"
            for decl, name in ms:
                if name == "size":
                    sym["Svc*.size"] = "member"
                else:
                    sym[prefix[s] + "->" + name] = "member"
        else:
            sym[s] = "type"
            for decl, name in ms:
                if name != "":
                    sym["%s.%s" % (s, name)] = "member"
    # the opaque handles of svctab.h
    for n in [n for t in [svctab] + extra for n in re.findall(r"^\s*struct\s+(\w+)\s*;", t, flags=re.M)]:
        if n not in st and not (ops and re.search(r"\b(struct|enum)\s+%s\b" % n, ops)):
            sym[n] = "type"
    for text in [libapi, svctab] + extra:
        for e, vals in enums(text).items():
            if e:
                sym[e] = "type"
            for v in vals:
                sym[v] = "enumerator"
        for t in fn_typedefs(text):
            sym[t] = "type"
        for mac in macros(text):
            if mac.startswith(SVC_MACRO_PREFIXES):
                sym[mac] = "macro"

    if ops is not None:
        ost, oen = structs(ops), enums(ops)
        fns = fn_typedefs(ops)
        helpers = re.findall(r"static\s+inline\s+\w+\s+(\w+)\s*\(", ops)
        mac = macros(ops)
        for n in OPS_TYPES:
            if n in ost:
                sym[n] = "type"
                for _, mem in ost[n]:
                    sym["%s.%s" % (n, mem)] = "member"
            elif n in oen:
                sym[n] = "type"
                for v in oen[n]:
                    sym[v] = "enumerator"
            elif n in fns:
                sym[n] = "type"
            elif strict:
                err.append("ops.h: the listed type %s is not in the header" % n)
        for n in OPS_NAMES:
            if n in helpers or n in mac:
                sym[n] = "macro"
            elif strict:
                err.append("ops.h: the listed name %s is not in the header" % n)
        for n in mac:
            if n.startswith(OPS_MACRO_PREFIXES):
                sym[n] = "macro"
    elif strict:
        err.append("ops.h is missing")
    if fmt is not None:
        mac = macros(fmt)
        for n in FMT_NAMES:
            if n in mac:
                sym[n] = "macro"
            elif strict:
                err.append("libfmt.h: the listed name %s is not in the header" % n)
    elif strict:
        err.append("libfmt.h is missing")
    return sym, err


def parse_doc(path):
    """({symbol: (kind, text)}, {every code span of the document}); a symbol is documented by a heading or the first cell of a row, a span anywhere counts for the stale check."""
    text = open(path, encoding="utf-8").read()
    lines = text.split("\n")
    heads = [(i, m.group(2)) for i, line in enumerate(lines) for m in [re.match(r"^(#+)\s+(.*)$", line)] if m]
    doc = {}
    spans = set(re.findall(r"`([^`\n]+)`", text))        # every code span of the document, prose too: the stale check reads them all
    for n, (i, title) in enumerate(heads):
        end = heads[n + 1][0] if n + 1 < len(heads) else len(lines)
        body = "\n".join(lines[i + 1:end]).strip()
        for s in re.findall(r"`([^`]+)`", title):
            spans.add(s)
            doc.setdefault(s, ("heading", body))
    for line in lines:
        m = re.match(r"^\|\s*(.+?)\s*\|\s*(.*?)\s*\|(?:\s*(.*?)\s*\|)?\s*$", line)
        if not m or set(m.group(1)) <= set("-: "):
            continue
        for s in re.findall(r"`([^`]+)`", m.group(1)):
            spans.add(s)
            doc.setdefault(s, ("row", m.group(2)))
    return doc, spans


def check(src=SRC, doc_path=DOC, strict=True):
    sym, errors = symbols(src, strict)
    if not os.path.exists(doc_path):
        return ["the document %s is missing" % doc_path], 0
    doc, spans = parse_doc(doc_path)
    since_types = set(SINCE_TYPES) | {s for s in sym if re.fullmatch(r"Lib\w+Decl", s)}
    groups = {s for s in sym if re.fullmatch(r"api->\w+", s) and sym.get(s) == "member" and s not in (
        "api->struct_size", "api->abi_major", "api->abi_minor", "api->log")}
    for s in sym:
        if s not in doc:
            errors.append("%s: no section or row for `%s`" % (os.path.relpath(doc_path, ROOT) if doc_path.startswith(ROOT) else doc_path, s))
            continue
        kind, text = doc[s]
        if not text.strip():
            errors.append("%s: the section of `%s` is empty" % (os.path.basename(doc_path), s))
        elif (s in groups or s in since_types) and kind == "heading" and "Since base" not in text:
            errors.append("%s: the section of `%s` does not state the base version (a line with \"Since base\")" % (os.path.basename(doc_path), s))
    known = set(sym)
    for t in spans:
        qualified = t.startswith("api->") or re.fullmatch(r"([A-Za-z_]\w*)\.\w+", t) and t.split(".")[0] in {x.split(".")[0] for x in known if "." in x}
        if qualified and t not in known:
            errors.append("%s: `%s` is not in the headers (stale)" % (os.path.basename(doc_path), t))
    return errors, len(sym)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--doc", default=DOC)
    a = ap.parse_args(argv)
    errors, n = check(a.src, a.doc)
    for e in errors:
        print(e)
    if errors:
        print("check_api_doc: %d problem(s)" % len(errors))
        return 1
    print("check_api_doc: ok: %d symbols documented" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
