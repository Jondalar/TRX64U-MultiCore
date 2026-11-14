#!/usr/bin/env python3
"""T215: the one normaliser behind every "did the code change" decision of the CI (Alex 2026-11-12: "The hash should only concern code.
Always. It simply cannot be that a comment triggers everything."). ci/plan.py (changed or not) and ci/lib.sh (content keys of the cache)
both call this file, so they cannot drift.

  ci/code_hash.py index                  stdin: `git ls-files -s` lines; stdout: the same lines with the blob hash of the normalised index content
  ci/code_hash.py hash-paths             stdin: paths (cwd = repo root); stdout: the hash of each normalised working-tree file, one per line
  ci/code_hash.py sha256 <file>...       like sha256sum, over the normalised content
  ci/code_hash.py check                  exit 0 when the tree-sitter install and every grammar load, else 2 (no key is made from a missing install)
  ci/code_hash.py show <file>            the normalised text of a file (debugging)

CODE_HASH=raw in the environment turns normalising off (the key function as it was before T215; ci/key_alias.sh pairs old and new keys with it).

Normalising (T215 D2, Alex: tree-sitter): every comment node of the parse is cut out (tree-sitter + tree-sitter-language-pack, exact versions in
ci/codehash-requirements.txt: they decide the hash), trailing whitespace and blank lines dropped (C-like and HDL; in python/bash/toml/tcl only on lines that carried a comment, a literal may span lines),
indentation untouched. A comment with a tool directive (synthesis, translate_off, pragma, lint_off, verilator ...) is kept. A file of an unknown type is
hashed as it is. A parse with error nodes (the Verilog grammar has them on much real code) still yields the comments as lexer tokens; the cut is
checked: the text without comments must parse to the very same non-comment tokens, else the file is hashed raw and the line is logged."""
import hashlib, os, re, subprocess, sys

# a pinned install on the builder's cache, where an image does not carry it (ci/install_codehash.sh). One directory per Python minor version: the
# tree-sitter wheel is a compiled module of that version (vivado-env and u64-build-env run 3.10, trx64-sim-env 3.12).
_LIBS = os.environ.get("CODE_HASH_LIBS", "/cache/multicore/pylibs-cp%d%d" % sys.version_info[:2])
if os.path.isdir(_LIBS):
    sys.path.insert(0, _LIBS)
    if os.path.isdir(os.path.join(_LIBS, "grammars")):
        os.environ.setdefault("TREE_SITTER_LANGUAGE_PACK_CACHE_DIR", os.path.join(_LIBS, "grammars"))

PARSER_FOR = {      # extension -> grammar of tree-sitter-language-pack
    ".v": "verilog", ".sv": "verilog", ".vh": "verilog", ".svh": "verilog",
    ".vhd": "vhdl", ".vhdl": "vhdl",
    ".c": "c", ".cc": "cpp", ".cpp": "cpp", ".cxx": "cpp", ".h": "cpp", ".hh": "cpp", ".hpp": "cpp", ".hxx": "cpp",
    ".py": "python", ".tcl": "tcl", ".xdc": "tcl", ".sdc": "tcl", ".sh": "bash", ".bash": "bash", ".toml": "toml",
}
_parsers = {}


def lang_of(path):
    return PARSER_FOR.get(os.path.splitext(path)[1].lower())


def _die(msg):
    sys.stderr.write("code_hash: %s\n" % msg)
    sys.exit(2)


def _cached(tslp, lang):
    """Is the grammar of `lang` in the local cache dir? The pack's own listing of what is downloaded (present in the pinned 1.23.1, local only);
    a pack without it falls back to looking for the grammar's library file in the cache dir. Never a download."""
    try:
        return lang in tslp.downloaded_languages()
    except AttributeError:
        d = os.environ.get("TREE_SITTER_LANGUAGE_PACK_CACHE_DIR") or ""
        return any(f.split(".")[0].endswith("_" + lang) or f.split(".")[0].endswith("-" + lang) for _, _, fs in os.walk(d) for f in fs)


def _parser(lang):
    if lang not in _parsers:
        try:
            import tree_sitter_language_pack as tslp
        except ImportError:
            _die("tree-sitter-language-pack is not installed (ci/codehash-requirements.txt, ci/install_codehash.sh)")
        # never a download at run time: a grammar fetched on one machine and not on another would give the two different keys
        if not _cached(tslp, lang):
            _die("grammar %r is not in the local cache dir %s (ci/install_codehash.sh puts it there; no download at run time)"
                 % (lang, os.environ.get("TREE_SITTER_LANGUAGE_PACK_CACHE_DIR", "default")))
        _parsers[lang] = tslp.get_parser(lang)
    return _parsers[lang]


# A comment that carries a tool directive is semantic (`-- synthesis translate_off` hides code from the synthesiser, `// synthesis keep` keeps a
# net): it is a token like any other and never cut. A plain comment that merely mentions one of these words is kept too - the safe side.
_PRAGMA = re.compile(rb"synthesis|synopsys|pragma|translate_(?:off|on)|xilinx|rtl_synthesis|synplify|altera|lint_off|lint_on|verilator|full_case|parallel_case|coverage|vivado|cadence|coding[:=]", re.I)


def _scan(root):
    """(comment byte ranges, the text of every other leaf in order; a pragma comment counts as a leaf). A shebang is no comment."""
    com, leaves, stack = [], [], [root]
    while stack:
        n = stack.pop()
        if "comment" in n.type and _PRAGMA.search(n.text):
            leaves.append(n.text)
        elif "comment" in n.type:
            com.append((n.start_byte, n.end_byte))
        elif n.child_count == 0:
            leaves.append(n.text)
        else:
            stack.extend(reversed(n.children))
    return com, leaves


_CLIKE = ("verilog", "vhdl", "c", "cpp")
_STRICT = ("python", "bash", "toml", "tcl")
_CHAR_C = re.compile(rb"'(?:\\.[^']{0,6}|[^\\'])'")
_CHAR_VHDL = re.compile(rb"'[^']'")


def _in_string(data, start, lang, gone):
    """Independent check for a parse with errors: is the comment that starts at `start` inside a string literal? A string of these languages ends
    with its line, so the line before the comment (without the comments cut out before it) decides. Doubtful (a char literal that is none) counts as yes."""
    ls = data.rfind(b"\n", 0, start) + 1
    pre, pos = b"", ls
    for a, b in gone:
        if b > ls and a < start:
            pre += data[pos:max(a, ls)]
            pos = max(b, pos)
    pre += data[pos:start]
    chr_re = _CHAR_VHDL if lang == "vhdl" else _CHAR_C if lang in ("c", "cpp") else None
    q, i = False, 0
    while i < len(pre):
        c = pre[i:i + 1]
        if q:
            if c == b"\\" and lang != "vhdl":
                i += 1
            elif c == b'"':
                q = False
        elif c == b'"':
            q = True
        elif c == b"'" and chr_re:
            m = chr_re.match(pre, i)
            if m:
                i = m.end() - 1
        i += 1
    return q


def normalise(path, data):
    """bytes -> bytes. Unknown type or CODE_HASH=raw: unchanged. Else every comment node is cut out, trailing blanks and blank lines go.
    Code is never cut. A parse without errors is trusted. A parse with errors (the Verilog grammar has them on much real code, and inside an
    error the lexer takes "a//b" for a comment) is accepted only for the C-like languages and only if no cut comment starts inside a string
    (_in_string); every other case, and every cut after which the text does not parse to the same non-comment tokens, hashes the file raw."""
    lang = lang_of(path)
    if lang is None or os.environ.get("CODE_HASH") == "raw":
        return data
    parser = _parser(lang)
    tree = parser.parse(data)
    com, leaves = _scan(tree.root_node)
    com = sorted((a, b) for a, b in com if not (a == 0 and data.startswith(b"#!")))
    if tree.root_node.has_error and (lang not in _CLIKE or any(_in_string(data, a, lang, com) for a, b in com)):
        sys.stderr.write("code_hash: %s: parse errors, hashed raw\n" % path)
        return data
    out, pos, cl = [], 0, set()
    for a, b in com:
        out.append(data[pos:a])
        cl.add(b"".join(out).count(b"\n"))      # the line of the cut text that carried this comment (a multi-line comment becomes one blank)
        out.append(b" ")
        pos = b
    out.append(data[pos:])
    cut = b"".join(out)
    com2, leaves2 = _scan(parser.parse(cut).root_node)
    if leaves2 != leaves or any(not (a == 0 and cut.startswith(b"#!")) for a, b in com2):
        sys.stderr.write("code_hash: %s: the text without its comments does not parse to the same tokens, hashed raw\n" % path)
        return data
    src = cut.replace(b"\r\n", b"\n")
    if lang in _STRICT:
        # a literal of these languages may span lines (heredoc, triple quotes, Tcl braces): a blank line or a trailing blank inside it is data. Only a
        # line that carried a comment is cleaned up (and dropped when nothing is left); every other line stays.
        if src.endswith(b"\n"):
            src = src[:-1]
        lines, prev = [], b""
        for i, l in enumerate(src.split(b"\n")):
            if i in cl:
                l = l.rstrip()
                if not l and not prev.endswith(b"\\"):
                    prev = l
                    continue
            lines.append(l)
            prev = l
        return b"\n".join(lines) + b"\n" if lines else b""
    lines, prev = [], b""
    for l in src.split(b"\n"):
        l = l.rstrip()
        if l or prev.endswith(b"\\"):      # a blank (or comment-only) line after a line ending in a backslash ends a macro / continuation: it is code
            lines.append(l)
        prev = l
    return b"\n".join(lines) + b"\n" if lines else b""


def blob_hash(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def differs(base_bytes, head_bytes, path):
    """The plan's question for one file; None = absent on that side (an added or deleted file always counts)."""
    if base_bytes is None or head_bytes is None:
        return base_bytes is not head_bytes
    return normalise(path, base_bytes) != normalise(path, head_bytes)


def _index(lines):
    rows = [l.split("\t", 1) for l in lines if l]
    want = []
    if os.environ.get("CODE_HASH") != "raw":
        want = [r[0].split()[1] for r in rows if r[0].split()[0] != "160000" and lang_of(r[1])]
    blobs = {}
    if want:
        p = subprocess.run(["git", "cat-file", "--batch"], input="".join(w + "\n" for w in want).encode(), capture_output=True, check=True)
        buf, pos = p.stdout, 0
        for w in want:
            nl = buf.index(b"\n", pos)
            size = int(buf[pos:nl].split()[2])
            blobs[w] = buf[nl + 1:nl + 1 + size]
            pos = nl + 1 + size + 1
    out = []
    for meta, path in rows:
        mode, sha, stage = meta.split()
        if sha in blobs:
            sha = blob_hash(normalise(path, blobs[sha]))
        out.append("%s %s %s\t%s" % (mode, sha, stage, path))
    return out


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    cmd = argv[1]
    if cmd == "index":
        r = _index(sys.stdin.read().split("\n"))
        if r:
            print("\n".join(r))
    elif cmd == "hash-paths":
        for p in sys.stdin.read().split("\n"):
            if p:
                print(blob_hash(normalise(p, open(p, "rb").read())))
    elif cmd == "sha256":
        for p in argv[2:]:
            print("%s  %s" % (hashlib.sha256(normalise(p, open(p, "rb").read())).hexdigest(), p))
    elif cmd == "check":     # exit 2 with a message when the install is missing; ci/lib.sh calls it first, a key is never made from a broken pipe
        if os.environ.get("CODE_HASH") != "raw":
            for lang in sorted(set(PARSER_FOR.values())):
                _parser(lang)
    elif cmd == "show":
        sys.stdout.write(normalise(argv[2], open(argv[2], "rb").read()).decode("latin-1"))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
