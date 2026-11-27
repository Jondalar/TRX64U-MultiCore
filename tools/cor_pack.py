#!/usr/bin/env python3
"""TRX64 core bundle (.cor) packer and checker (T84 D5).

One file per core. Little endian throughout:

  0   magic "TRXCOR01"
  8   u32 length of everything after this header
  12  records: u32 type, u32 length, value (back to back, no padding)

Records (type: value):
  1 manifest   compact JSON, keys sorted, flat:
               name, version, contract, fpga, partial_size, partial_sha256, app_size, app_load,
               app_entry, app_sha256, icon_size, icon_sha256 (icon_* only with an icon),
               short (optional: the boot menu's text tile, 1 to 4 of A-Z a-z 0-9 + / .; T209 D15)
  2 partial    the configuration data of layer 2's partial bitstream (Xilinx .bit header
               stripped; the words start at the bytes the ICAP gets, file order)
  3 app        the application image (raw binary, copied to app_load, started at app_entry)
  4 icon       32 x 32 RGB565, little endian (2048 bytes), optional

The records are in this order, each once; the manifest first so a browser can stop reading there.
`contract` is the host/core contract version (docs/specs/T79.1-host-core-contract.md); the boot app
refuses a bundle whose contract is not its own. `host_key` names the one host this partial was
routed against (T84 D5b): the first 8 hex of ci/lib.sh's host_key, which the host build also writes
into the bitstream's AXSS register (host/pins.xdc), so the boot app reads the running host's own
number and refuses a partial that belongs to another one. The contract alone does not cover that: it
only rises when the partition's edge changes, and a host re-routed with the same 228 ports keeps it.
Hashes are SHA-256 over each part (integrity only, no signatures).

  cor_pack.py pack  -o core.cor --name N --version V --partial P.bit|P.bin --app A.bin --host-key K
                    [--load 0x30000] [--entry 0x30000] [--contract 2] [--fpga xc7a50t] [--icon I.rgb565|I.png] [--heap BYTES]
  cor_pack.py check core.cor [--contract 2] [--host-key K]   exit 0 = intact, prints the manifest
  cor_pack.py hostkey host.bit                   prints the AXSS word of a full bitstream (the host key)
  cor_pack.py heap APP.bin --core ID [--sizes TOML_B] [--heap BYTES]
                                                 T239.5 (step 7 of a core lane): the heap check of `pack` on the app alone, before a .cor exists - the layout figures,
                                                 the heap the TOML B's libraries keep against the heap (default 0: the region left behind the image); exit 1 = refused

OPTIONAL records of either kind (T104.1 D9, D12), after the others, each once, both announced by the manifest ("records": 1 and
`desc_size`/`desc_sha256`, `libs_size`/`libs_sha256`). A bundle without them is byte for byte what this tool wrote before they existed.
  6 desc   the menu description, compiled from TOML (tools/descrec.py; the layout is coreapp/src/descrec.h). Since T239.3 D17 it may also carry the core's data
           sections (the descriptor's identity, ROMs, video, hotkeys, mount slots; cores/vc20/app/desc.toml): its [core] name has to be the manifest's name
           A record of TOML A (version 7, T239.3 P1-4) gets the bitstream's AXSS word as the build of its identity; `check` refuses a record with another one.
  7 libs   base 4.0 (T239.3 D58): the menu records of the libraries the core's app links with `register` (TOML B `menu`, compiled by tools/descrec.py) that could not be
           merged into record 6 (merge_lib_menus: a poll, a route, the core's own tables): "TRXL", u32 version 2, u32 count, then for each char name[16] (the
           [[library]] name, NUL-padded), u32 length and the record, padded with zeros to a multiple of 4. The base hands each to the register call of the library of
           that name (libscan.h lib_link_cor). Version 1 (library files, .u2a) is refused: base 4.0 loads no library file.

  8 files  files the core installs on the SD (T175 D-files; the diag core carried its example tests here until T260 D6, they are a folder of its lane's delivery now; a sub-app core uses the record):
           "TRXF", u32 version 1, u32 count, then per file
             u32 entry_len   bytes of the entry after this field, a multiple of 4
             u16 path_len, u16 n_old, u32 size
             n_old x { u32 size, u32 hash_hi, u32 hash_lo }   earlier shipped versions of this file: size and FNV-1a 64 of the whole text
             path (path_len bytes, relative to the core's folder, '/' separators, no leading '/', no '..'), the file's bytes (size), zero padding to 4
           At the start of the core the application writes every file that is missing into <core folder>/<path>; it never overwrites a
           file, except one that is byte for byte (size and hash) an earlier version listed in the entry (coreapp/src/instfiles.cc).
           Manifest: files_size, files_sha256, files_count. Source: the core's descriptor (core.toml `install_dir`, `install_old`).

  cor_pack.py pack ... [--desc D.toml [--desc E.toml]] [--sizes TOML_B] [--files DIR [--files-old DIR]]      (also for pack-large)
  cor_pack.py files-record DIR [--old DIR] -o F.rec            only the record 8 (for the application's benches)

LARGE cores (T115 §7 "The extended .cor"): a core that does not fit layer 2 is one FULL bitstream of the 50T (host and core
flat in one design), started by IPROG the way a .cfw is. Same container and magic, `kind` = "large" in the manifest (a DFX
manifest has no `kind`), records 1 manifest, 3 app, 4 icon, 5 bitstream (the .bit file as it is, header included: the flash file
the host writes for the IPROG). No `contract`, no `host_key`, no partial record: it does not run on the host's static. The
manifest names the FPGA by IDCODE (`idcode`, 8 hex); the host refuses another one when the entry is added and again at the start.

  cor_pack.py pack-large -o core.cor --id I --name N --version V --bit B.bit --app A.bin [--icon I.rgb565|I.png]
                    [--map auto|identity|gideon_bl4] [--bit-commit C] [--app-commit C] [--heap BYTES]
  cor_pack.py check core.cor                     both kinds
"""
import argparse
import hashlib
import json
import re
import struct
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import descrec  # noqa: E402
import libtab  # noqa: E402

MAGIC = b"TRXCOR01"
T_MANIFEST, T_PARTIAL, T_APP, T_ICON, T_BIT, T_DESC, T_LIBS, T_FILES = 1, 2, 3, 4, 5, 6, 7, 8
RECORDS_FORMAT = 1                        # manifest "records": the version of the two optional records below
LIBS_MAGIC = b"TRXL"
FILES_MAGIC = b"TRXF"
FILES_VERSION = 1
FILES_MAX = 256 * 1024                    # one installed file (coreapp/src/instfiles.h INST_FILE_MAX)
FILES_PATH_MAX = 100
FILES_OLD_MAX = 8
LIBS_VERSION = 2                          # base 4.0: menu records by library name (version 1 held .u2a files)
LIB_NAME_MAX = 16                         # coreapp/src/libfmt.h U2A3_NAME_MAX: 1 to 15 characters and the NUL
KIND_LARGE = "large"
LARGE_FORMAT = 2
IDCODE_XC7A50T = 0x0362C093               # host-app cfw.h CFW_IDCODE_XC7A50T
IDCODE_MASK = 0x0FFFFFFF                  # without the 4-bit version
LARGE_APP_ADDR = 0x30000                  # cfw.h CFW_APP_ADDR: load and entry of the application of a full bitstream
LARGE_APP_MAX = 0x400000                  # cfw.h CFW_APP_MAX
LARGE_BIT_MAX = 0x800000                  # cfw.h CFW_BIT_MAX
MAPS = ("identity", "gideon_bl4")
# T239.3 D15, P4-1: the heap word. The application image carries one record `TRXHEAP:` (coreapp/src/heapcfg.c): heap size, image end (the linker's _end),
# end of the application region (__heap_limit) and the stack reserve, four little-endian words. The packer sets the heap from the core's core.toml `heap`
# in the copy it packs and refuses an image whose end + heap + reserve does not fit the region. A heap of 0 is the region that is left: from the 16-aligned
# image end to the region end less the reserve (the application computes the same at start); the packer leaves the word 0 and reports the figure.
HEAP_MAGIC = b"TRXHEAP:"
HEAP_WORDS = 4
HEAP_DEFAULT = 0x7A0000                   # coreapp/src/heapcfg.c: what the image says when the core's TOML sets nothing
HEAP_MIN = 0x10000
CONTRACT = 2
ICON_BYTES = 32 * 32 * 2
BIT_MAGIC = bytes([0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01])
SYNC = bytes([0xAA, 0x99, 0x55, 0x66])


class CorError(Exception):
    pass


def bit_config_data(raw):
    """Configuration data of a Xilinx .bit file (same walk as apps/coreload bit_data()); a file
    without the .bit magic is taken as raw configuration data."""
    if raw[:13] != BIT_MAGIC:
        return raw
    p = 13
    for key in b"abcd":
        if p + 3 > len(raw) or raw[p] != key:
            raise CorError("bad .bit header")
        p += 3 + struct.unpack(">H", raw[p + 1:p + 3])[0]
    if p + 5 > len(raw) or raw[p] != ord("e"):
        raise CorError("bad .bit header")
    n = struct.unpack(">I", raw[p + 1:p + 5])[0]
    data = raw[p + 5:p + 5 + n]
    if len(data) != n:
        raise CorError(".bit header length exceeds the file")
    return data


def check_partial(data):
    if not data or len(data) % 4:
        raise CorError("partial: length is not a positive multiple of 4")
    i = data[:4096].find(SYNC)
    if i < 0 or i % 4:
        raise CorError("partial: no word-aligned sync word in the first 4 KiB")


def sha(b):
    return hashlib.sha256(b).hexdigest()


def norm_host_key(v):
    """The host key as it stands in a manifest: 8 lower-case hex digits (T84 D5b).

    It is the first 8 hex of ci/lib.sh's host_key, which is also the AXSS word of the host
    bitstream (host/pins.xdc), so the boot app can read the running host's and compare.
    """
    if isinstance(v, int):
        v = "%08x" % v
    v = str(v).strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if len(v) != 8 or any(c not in "0123456789abcdef" for c in v):
        raise CorError("host key %r: 8 hex digits expected" % v)
    return v


def rec(t, v):
    return struct.pack("<II", t, len(v)) + v


def pack_libs(menus):
    """Record 7 (base 4.0): [(library name, menu record bytes)] as "TRXL", u32 2, u32 count, then per entry the name (16 bytes, NUL-padded), u32 length, the record, zeros to 4."""
    if not menus:
        raise CorError("libs: no menu record given")
    out = bytearray(LIBS_MAGIC + struct.pack("<II", LIBS_VERSION, len(menus)))
    seen = set()
    for name, blob in menus:
        if not re.match(r"^[a-z0-9_]{1,15}$", name or ""):
            raise CorError("libs: %r is no library name (1 to 15 characters of [a-z0-9_])" % name)
        if name in seen:
            raise CorError("libs: library %s has two menu records" % name)
        seen.add(name)
        if not blob:
            raise CorError("libs: the menu record of library %s is empty" % name)
        out += name.encode("ascii").ljust(LIB_NAME_MAX, b"\0") + struct.pack("<I", len(blob)) + blob + b"\0" * (-len(blob) % 4)
    return bytes(out)


def parse_libs(blob):
    """The menu records of record 7, [(library name, record bytes)] in the order they were packed; each record is read by descrec."""
    if len(blob) < 12 or blob[:4] != LIBS_MAGIC:
        raise CorError("libs: not a library record (magic)")
    ver, cnt = struct.unpack_from("<II", blob, 4)
    if ver == 1:
        raise CorError("libs: version 1 holds library files (.u2a); base 4.0 loads no library file (T239.3 D58): pack the core again")
    if ver != LIBS_VERSION:
        raise CorError("libs: version %d, this tool reads %d" % (ver, LIBS_VERSION))
    p, out = 12, []
    for _ in range(cnt):
        if p + LIB_NAME_MAX + 4 > len(blob):
            raise CorError("libs: truncated")
        name = blob[p:p + LIB_NAME_MAX].split(b"\0")[0].decode("ascii", "replace")
        n, = struct.unpack_from("<I", blob, p + LIB_NAME_MAX)
        p += LIB_NAME_MAX + 4
        if not re.match(r"^[a-z0-9_]{1,15}$", name) or n == 0 or p + n > len(blob):
            raise CorError("libs: an entry has no library name or runs past the end")
        try:
            descrec.parse_desc(blob[p:p + n])
        except descrec.DescError as e:
            raise CorError("libs: the menu record of library %s: %s" % (name, e))
        out.append((name, blob[p:p + n]))
        p += n + (-n % 4)
    if p != len(blob) or not out:
        raise CorError("libs: %d bytes behind the last menu record, or none" % (len(blob) - p))
    return out


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def collect_files(src_dir, old_dir=None):
    """[(relative path, bytes, [(size, fnv64) of earlier shipped versions])] of every file below src_dir (sorted). An earlier version lives in old_dir as
    <stem>.v<N>.<ext> and belongs to the file(s) named <stem>.<ext> (any folder below src_dir)."""
    files = []
    for root, _, names in os.walk(src_dir):
        for n in names:
            full = os.path.join(root, n)
            files.append((os.path.relpath(full, src_dir).replace(os.sep, "/"), open(full, "rb").read()))
    files.sort()
    olds = {}
    if old_dir and os.path.isdir(old_dir):
        for n in sorted(os.listdir(old_dir)):
            m = re.fullmatch(r"(.+)\.v\d+(\.[^.]*)?", n)
            if not m:
                raise CorError("files-old: %s is not <name>.v<N>.<ext>" % n)
            data = open(os.path.join(old_dir, n), "rb").read()
            olds.setdefault(m.group(1) + (m.group(2) or ""), []).append((len(data), fnv64(data)))
    out = [(p, d, olds.get(p.rsplit("/", 1)[-1], [])) for p, d in files]
    named = {p.rsplit("/", 1)[-1] for p, _ in files}
    for k in olds:
        if k not in named:
            raise CorError("files-old: an earlier version of %s, which the core does not install" % k)
    return out


def pack_files(files):
    """Record 8 from [(path, bytes, [(size, hash64)])]."""
    if not files:
        raise CorError("files: no file given")
    out = bytearray(FILES_MAGIC + struct.pack("<II", FILES_VERSION, len(files)))
    for path, data, olds in files:
        pb = path.encode("ascii")
        if not pb or len(pb) > FILES_PATH_MAX or pb[:1] == b"/" or ".." in path.split("/") or "\\" in path or "" in path.split("/"):
            raise CorError("files: path %r (relative, at most %d characters, no '..')" % (path, FILES_PATH_MAX))
        if len(data) > FILES_MAX:
            raise CorError("files: %s is %d bytes, at most %d" % (path, len(data), FILES_MAX))
        if len(olds) > FILES_OLD_MAX:
            raise CorError("files: %s has %d earlier versions, at most %d" % (path, len(olds), FILES_OLD_MAX))
        body = struct.pack("<HHI", len(pb), len(olds), len(data))
        for sz, h in olds:
            body += struct.pack("<III", sz, h >> 32, h & 0xFFFFFFFF)
        body += pb + data
        body += b"\0" * (-len(body) % 4)
        out += struct.pack("<I", len(body)) + body
    return bytes(out)


def parse_files(blob):
    """[(path, bytes, [(size, hash64)])] of record 8, in the order they were packed."""
    if len(blob) < 12 or blob[:4] != FILES_MAGIC:
        raise CorError("files: not a file set (magic)")
    ver, cnt = struct.unpack_from("<II", blob, 4)
    if ver != FILES_VERSION:
        raise CorError("files: version %d, this tool reads %d" % (ver, FILES_VERSION))
    p, out = 12, []
    for _ in range(cnt):
        if p + 4 > len(blob):
            raise CorError("files: truncated")
        n, = struct.unpack_from("<I", blob, p)
        p += 4
        if n % 4 or n < 8 or p + n > len(blob):
            raise CorError("files: an entry runs past the end")
        plen, nold, size = struct.unpack_from("<HHI", blob, p)
        q = p + 8
        olds = []
        for _ in range(nold):
            sz, hh, hl = struct.unpack_from("<III", blob, q)
            olds.append((sz, (hh << 32) | hl))
            q += 12
        if q + plen + size > p + n:
            raise CorError("files: an entry is shorter than it says")
        out.append((blob[q:q + plen].decode("ascii"), blob[q + plen:q + plen + size], olds))
        p += n
    if p != len(blob) or not out:
        raise CorError("files: %d bytes behind the last file, or none" % (len(blob) - p))
    return out


def lib_menu_compile(path, name):
    """The menu record of the linked library `name` from its TOML (TOML B `menu`): the nodes, file actions, settings, tabs, key rows and poll a library may contribute,
    with the refusals u2a_pack.py --menu had for a library file (no windows, no route, nothing hidden or renamed: those are a core's description)."""
    try:
        doc = descrec.toml_loads(open(path).read())
        if "window" in doc:
            raise CorError("library %s: its menu record %s has windows; those are a core's description" % (name, path))
        if "hide" in doc or "rename" in doc or "area" in doc:
            raise CorError("library %s: its menu record %s gives a route or hides or renames something; that is a core's description" % (name, path))
        return descrec.compile_desc(doc)
    except (descrec.DescError, OSError) as e:
        raise CorError("library %s: menu %s: %s" % (name, path, e))


def toml_b_libs(path):
    """Base 4.0 (T239.3 D58): the libraries the TOML B of a core's app links, [(name, [(export, major)], menu record bytes or None)] in the order of the file: the menu of a
    [[library]] with `menu` (a path relative to the TOML B) compiled, for record 7 and the merge into record 6; the exports for check_hooks."""
    try:
        libs = libtab.load(path)
    except libtab.LibTabError as e:
        raise CorError(str(e))
    out = []
    for l in libs:
        menu = lib_menu_compile(os.path.join(os.path.dirname(os.path.abspath(path)), l["menu"]), l["name"]) if l.get("menu") else None
        out.append((l["name"], [(e["name"], e["major"]) for e in l.get("exports", [])], menu))
    return out


def check_hooks(desc, libs):
    """T239.3 base 3.3 (core_hooks.h), base 4.0: a core whose description record names its hooks library (`hooks` in [core]) is linked with that library: the TOML B of its app
    (--sizes) lists a [[library]] of that name that exports corehooks major 1 (the base takes the table of that slot, libslot.h). `libs`: toml_b_libs, None when no TOML B is given."""
    if desc is None:
        return
    try:
        d = descrec.parse_desc(desc)
    except descrec.DescError as e:
        raise CorError(str(e))
    hooks = (d.get("core") or {}).get("hooks")
    if not hooks:
        return
    if libs is None:
        raise CorError('desc: the core names the hooks library "%s" ([core] hooks); the TOML B of its app (--sizes) has to be given, which links it' % hooks)
    mine = [ex for n, ex, _ in libs if n == hooks]
    if not mine:
        raise CorError('desc: the core names the hooks library "%s" ([core] hooks), which the TOML B of its app does not link (%s)'
                       % (hooks, ", ".join(n for n, _, _ in libs) or "no library"))
    if ("corehooks", 1) not in mine[0]:
        raise CorError('library %s is the code of the core ([core] hooks), but its [[library]] does not export corehooks major 1 (exports = [{ name = "corehooks", major = 1 }]); it exports %s'
                       % (hooks, ", ".join("%s=%d" % x for x in mine[0]) or "nothing"))


def merge_menus(desc, menus):
    """T104.2 D10: what the linked libraries contribute to the menu goes into the description when it is packed, so the bundle carries the finished menu and the application
    does not add those menus again at run time. Returns (the description to pack, [(name, record)] of the menus that stay for the run time: record 7)."""
    if not menus:
        return desc, []
    try:
        blob, merged, kept = descrec.merge_lib_menus(desc, menus)
    except descrec.DescError as e:
        raise CorError(str(e))
    for n, why in kept:
        print("cor_pack: library %s keeps its menu for the run time: %s" % (n, why), file=sys.stderr)
    stay = set(n for n, _ in kept)
    return blob, [(n, b) for n, b in menus if n in stay]


def check_desc(m, desc):
    """The description by its own reader; one with the core's data sections names the core the manifest names (the application finds its descriptor by that name)."""
    try:
        d = descrec.parse_desc(desc)
    except descrec.DescError as e:
        raise CorError(str(e))
    if d.get("core") and d["core"]["name"].lower() != str(m.get("name", "")).lower():
        raise CorError('desc: the core section names "%s", the manifest "%s"' % (d["core"]["name"], m.get("name")))


def stamp_desc(desc, axss, what):
    """T239.3 P1-4 (D30 condition 2, D31): a record of TOML A (version 7) gets the bitstream's build word, its AXSS word (the BUILD register of the gateware answers it, trx_io.v), into
    its identity; the service compares ID and build with the standard block at start. Every other record comes back as it was. TOML A without a bitstream that has an AXSS word is refused:
    it could not be told from another bitstream."""
    if desc is None:
        return desc
    try:
        if descrec.identity_build(desc) is None and not _is_toml_a(desc):
            return desc
        if axss is None:
            raise CorError("desc: TOML A needs the bitstream's build word, and %s has no AXSS write (built without USR_ACCESS)" % what)
        return descrec.stamp_build(desc, axss)
    except descrec.DescError as e:
        raise CorError("desc: %s" % e)


def _is_toml_a(desc):
    return "identity" in descrec.parse_desc(desc)


def check_build(recs, axss, what):
    """A made .cor with TOML A carries the build word of its own bitstream (the pair the service compares at start)."""
    if T_DESC not in recs:
        return
    try:
        have = descrec.identity_build(recs[T_DESC])
        toml_a = _is_toml_a(recs[T_DESC])
    except descrec.DescError as e:
        raise CorError("desc: %s" % e)
    if not toml_a:
        return
    if have is None:
        raise CorError("desc: TOML A carries no build word (cor_pack writes it)")
    if axss is None or have != axss:
        raise CorError("desc: TOML A carries the build %08x, %s has %s" % (have, what, "no AXSS word" if axss is None else "%08x" % axss))


def optional_records(m, desc, libs, files=None):
    """The manifest keys and the records for the optional description, the menu records of the linked libraries (`libs`: toml_b_libs, None without a TOML B) and the files;
    nothing at all when none of them is there (the bytes of a bundle without them stay what they were)."""
    check_hooks(desc, libs)
    menus = [(n, b) for n, _, b in libs or [] if b is not None]
    if desc is None and not menus and not files:
        return b""
    desc, menus = merge_menus(desc, menus)
    m["records"] = RECORDS_FORMAT
    body = b""
    if desc is not None:
        check_desc(m, desc)
        m.update(desc_size=len(desc), desc_sha256=sha(desc))
        body += rec(T_DESC, desc)
    if menus:
        blob = pack_libs(menus)
        m.update(libs_size=len(blob), libs_sha256=sha(blob), libs_count=len(menus))
        body += rec(T_LIBS, blob)
    if files:
        blob = pack_files(files)
        m.update(files_size=len(blob), files_sha256=sha(blob), files_count=len(files))
        body += rec(T_FILES, blob)
    return body


def check_optional(m, recs):
    """Record 6 and 7 against the manifest, as every other part is checked; and the description by its own reader."""
    has = [t for t in (T_DESC, T_LIBS, T_FILES) if t in recs]
    if not has and "records" not in m and not any(k in m for k in ("desc_size", "libs_size", "files_size")):
        return
    if m.get("records") != RECORDS_FORMAT:
        raise CorError("records: version %r, this tool reads %d" % (m.get("records"), RECORDS_FORMAT))
    for part, t in (("desc", T_DESC), ("libs", T_LIBS), ("files", T_FILES)):
        if (t in recs) != ((part + "_size") in m):
            raise CorError("%s: record and manifest disagree on its presence" % part)
        if t not in recs:
            continue
        if m[part + "_size"] != len(recs[t]) or m.get(part + "_sha256") != sha(recs[t]):
            raise CorError("%s: size or SHA-256 mismatch" % part)
    if T_DESC in recs:
        check_desc(m, recs[T_DESC])
    if T_LIBS in recs:
        libs = parse_libs(recs[T_LIBS])
        if m.get("libs_count") != len(libs):
            raise CorError("libs: the manifest says %r menu records, the record holds %d" % (m.get("libs_count"), len(libs)))
    if T_FILES in recs:
        files = parse_files(recs[T_FILES])
        if m.get("files_count") != len(files):
            raise CorError("files: the manifest says %r files, the record holds %d" % (m.get("files_count"), len(files)))


AXSS_SYNC = 0xAA995566
REG_AXSS, REG_CMD, CMD_DESYNC = 0x0D, 0x04, 0x0D


def host_key_from_bit(data):
    """The AXSS word of a 7-series bitstream, as 8 lower-case hex (T84 D5b).

    The host build writes the host key there (host/pins.xdc), so a bundle can be checked against
    the very image it belongs to. Same walk over the configuration packets as flash_axss() in
    host/bootrom/bootrom.c: find the sync word, then read type-1 writes of one word.
    """
    i = data.find(struct.pack(">I", AXSS_SYNC))
    if i < 0:
        raise CorError("bitstream: no sync word")
    i += 4
    while i + 4 <= len(data):
        w, = struct.unpack_from(">I", data, i)
        i += 4
        if (w >> 29) == 1:                                  # type 1: register write
            op, reg, count = (w >> 27) & 3, (w >> 13) & 0x3FFF, w & 0x7FF
            if op != 2:
                continue
            if count == 1:
                v, = struct.unpack_from(">I", data, i)
                i += 4
                if reg == REG_AXSS:
                    return "%08x" % v
                if reg == REG_CMD and v == CMD_DESYNC:
                    break
            else:
                i += 4 * count
        elif (w >> 29) == 2:                                # type 2: the configuration data
            i += 4 * (w & 0x7FFFFFF)
    raise CorError("bitstream: no USR_ACCESS (AXSS) word; it was built without a host key")


SHORT_RE = re.compile(r"[A-Za-z0-9+/.]{1,4}")


def put_short(m, short):
    """T209 D15: the optional short name of the boot menu's text tile (manifest `short`, from the core's core.toml); no key when none."""
    if short in (None, ""):
        return
    if not SHORT_RE.fullmatch(short):
        raise CorError("short: 1 to 4 letters, digits, + / . (got %r)" % short)
    m["short"] = short


def toml_b_heap_needs(path):
    """[(name, heap_keep, heap_peak)] of the libraries the TOML B of a core's app links (`heap_keep` and `heap_peak` of a [[library]] table, read and checked by libtab.load; None: not declared). Sub-apps (.u2a) are not counted: S13 checks them at load."""
    try:
        libs = libtab.load(path)
    except libtab.LibTabError as e:
        raise CorError(str(e))
    return [(l["name"], l.get("heap_keep"), l.get("heap_peak")) for l in libs]


def app_heap(app, heap, core, needs=None, report=None):
    """T239.3 D15, P4-1: `app` with the heap word set to `heap` (the core's core.toml `heap`; None keeps the image's own, the default; 0 is the region that is
    left behind the image and the stack reserve). The image must hold one TRXHEAP record; one without it is left alone when no heap is asked for (an imported
    core's own app) and refused when one is. Refused too: an image whose end + heap + stack reserve is beyond the end of the application region, and a core whose
    libraries keep (`heap_keep`, `needs`: (name, keep, peak) per linked library) more than the heap; the message names the core, the figures and
    the limit. `report`, a list, gets the lines of the build report: the figures of the layout and the needs of each library."""
    at, i = [], -1
    while True:
        i = app.find(HEAP_MAGIC, i + 1)
        if i < 0:
            break
        at.append(i + len(HEAP_MAGIC))
    if not at:
        if heap is None:
            return app
        raise CorError("app (%s): no %s record in the image, so heap 0x%x cannot be set" % (core, HEAP_MAGIC.decode(), heap))
    if len(at) > 1:
        raise CorError("app (%s): %d %s records in the image, one expected" % (core, len(at), HEAP_MAGIC.decode()))
    o = at[0]
    if o + 4 * HEAP_WORDS > len(app):
        raise CorError("app (%s): the %s record is cut off" % (core, HEAP_MAGIC.decode()))
    cur, image_end, region_end, reserve = struct.unpack_from("<4I", app, o)
    if heap is None:
        heap = cur
    start = (image_end + 15) & ~15
    left = region_end - reserve - start                 # what heapcfg.c's heapcfg_define makes of the word 0
    word = heap
    if heap == 0:
        heap = left
        if heap < HEAP_MIN:
            raise CorError("core %s: image end 0x%x, stack reserve 0x%x, region end 0x%x: the region left is %d bytes, below 0x%x" % (core, image_end, reserve, region_end, max(heap, 0), HEAP_MIN))
    if heap < HEAP_MIN or heap % 16:
        raise CorError("core %s: heap 0x%x is below 0x%x or not a multiple of 16" % (core, heap, HEAP_MIN))
    need = start + heap + reserve
    if need > region_end:
        raise CorError("core %s: image end 0x%x + heap 0x%x + stack reserve 0x%x = 0x%x, the application region ends at 0x%x (%d bytes too much)"
                       % (core, image_end, heap, reserve, need, region_end, need - region_end))
    keeps = [(n, k) for n, k, _ in needs or [] if k]
    keep_sum = sum(k for _, k in keeps)
    if report is not None:
        report.append("heap (%s): image end 0x%x (aligned 0x%x), region end 0x%x, stack reserve 0x%x; heap 0x%x = %d bytes%s"
                      % (core, image_end, start, region_end, reserve, heap, heap, " (word 0: the region left)" if word == 0 else ""))
        for n, k, pk in needs or []:
            report.append("heap (%s): library %s keeps %s, peak %s" % (core, n, "n/a" if k is None else "%d" % k, "n/a" if pk is None else "%d" % pk))
        if needs:
            report.append("heap (%s): libraries keep %d of %d bytes, %d left" % (core, keep_sum, heap, heap - keep_sum))
    if keep_sum > heap:
        raise CorError("core %s: its libraries keep %d bytes of heap (%s), the heap of the core is %d bytes (%d too much)"
                       % (core, keep_sum, ", ".join("%s %d" % kv for kv in keeps), heap, keep_sum - heap))
    return app[:o] + struct.pack("<I", word) + app[o + 4:]


def pack(name, version, partial, app, load, entry, host_key, contract=CONTRACT, fpga="xc7a50t", icon=None, desc=None, libs=None, files=None, short=None, heap=None, heap_needs=None, report=None):
    partial = bit_config_data(partial)
    check_partial(partial)
    if not app:
        raise CorError("app: empty")
    app = app_heap(app, heap, name, heap_needs, report)
    if not (load <= entry < load + len(app)):
        raise CorError("app: entry outside the image")
    # Record 3 is the raw image: the boot app copies it to app_load and jumps to app_entry. A `.app` file is that
    # image with a 12-byte header (load, length, entry) in front, which is right for the host update file and wrong
    # here - the three header words end up on the entry point and the application executes its own load address.
    # That shipped in the Atari and the NES bundles of trx64mc-main-51e89821 (T93 5.12). The test is exact, not a
    # guess: the header has to agree with the arguments we were given and with the rest of the file.
    if len(app) > 12:
        h_load, h_len, h_entry = struct.unpack("<III", app[:12])
        if h_load == load and h_entry == entry and h_len == len(app) - 12:
            raise CorError("app: this is a .app, not the image - it starts with the 12-byte header "
                           "(load 0x%08x, %d bytes, entry 0x%08x). Pack the .bin beside it." % (h_load, h_len, h_entry))
    if icon is not None and len(icon) != ICON_BYTES:
        raise CorError("icon: must be 32x32 RGB565 (%d bytes)" % ICON_BYTES)
    host_key = norm_host_key(host_key)
    m = {"name": name, "version": version, "contract": contract, "fpga": fpga,
         "host_key": host_key,
         "partial_size": len(partial), "partial_sha256": sha(partial),
         "app_size": len(app), "app_load": load, "app_entry": entry, "app_sha256": sha(app)}
    put_short(m, short)
    if icon is not None:
        m.update(icon_size=len(icon), icon_sha256=sha(icon))
    # P1-4 stamps no build word here: the BUILD register of a DFX core answers the static host (USR_ACCESSE2 in host/top.v), not the partial; which word its identity carries is open (step 2)
    extra = optional_records(m, desc, libs, files)
    man = json.dumps(m, sort_keys=True, separators=(",", ":")).encode()
    body = rec(T_MANIFEST, man) + rec(T_PARTIAL, partial) + rec(T_APP, app)
    if icon is not None:
        body += rec(T_ICON, icon)
    body += extra
    return MAGIC + struct.pack("<I", len(body)) + body


def retag(blob, version, app_fn=None, **fields):
    """the same .cor with another manifest `version` (T221 D4: a release is the tested build with its version strings rewritten) and, when `app_fn` is
    given, the application image (record 3) it returns for the old one, the manifest's app_size / app_sha256 following. `fields` are further manifest
    fields to set (ci/imports.py: app_commit). Everything else stays byte for byte."""
    if blob[:8] != MAGIC:
        raise CorError("not a .cor")
    recs, pos = [], 12
    while pos < len(blob):
        t, n = struct.unpack("<II", blob[pos:pos + 8])
        recs.append([t, blob[pos + 8:pos + 8 + n]])
        pos += 8 + n
    if [r[0] for r in recs].count(T_APP) != 1 or recs[0][0] != T_MANIFEST:
        raise CorError("expected one manifest first and one app record")
    m = json.loads(recs[0][1])
    m["version"] = version
    m.update(fields)
    for r in recs:
        if r[0] == T_APP and app_fn is not None:
            r[1] = app_fn(r[1])
            m.update(app_size=len(r[1]), app_sha256=sha(r[1]))
    recs[0][1] = json.dumps(m, sort_keys=True, separators=(",", ":")).encode()
    body = b"".join(rec(t, v) for t, v in recs)
    out = MAGIC + struct.pack("<I", len(body)) + body
    parse(out)
    return out


def replace_app(blob, app):
    """the same .cor with another application image (record 3) and its manifest fields; `app` is the image or a .app (12-byte header
    load, size, entry, which then sets app_load / app_entry). For the emulator flows: the .cor must carry the image under test."""
    if blob[:8] != MAGIC:
        raise CorError("not a .cor")
    load = entry = None
    if len(app) > 12:
        h_load, h_len, h_entry = struct.unpack("<III", app[:12])
        if h_len == len(app) - 12 and h_load <= h_entry < h_load + h_len:
            load, entry, app = h_load, h_entry, app[12:]
    recs, pos = [], 12
    while pos < len(blob):
        t, n = struct.unpack("<II", blob[pos:pos + 8])
        recs.append([t, blob[pos + 8:pos + 8 + n]])
        pos += 8 + n
    if [r[0] for r in recs].count(T_APP) != 1 or recs[0][0] != T_MANIFEST:
        raise CorError("expected one manifest first and one app record")
    m = json.loads(recs[0][1])
    m.update(app_size=len(app), app_sha256=sha(app))
    if load is not None:
        m.update(app_load=load, app_entry=entry)
    recs[0][1] = json.dumps(m, sort_keys=True, separators=(",", ":")).encode()
    for r in recs:
        if r[0] == T_APP:
            r[1] = app
    body = b"".join(rec(t, v) for t, v in recs)
    out = MAGIC + struct.pack("<I", len(body)) + body
    if T_BIT not in [r[0] for r in recs]:
        parse(out)                                         # a DFX core's .cor: the checker the host's reading follows
    return out


def parse(blob, contract=None, host_key=None):
    """Returns (manifest dict, {type: bytes}); raises CorError on any inconsistency."""
    if len(blob) < 12 or blob[:8] != MAGIC:
        raise CorError("not a TRX64 core bundle (magic)")
    total = struct.unpack("<I", blob[8:12])[0]
    if total != len(blob) - 12:
        raise CorError("length field %d does not match the file (%d)" % (total, len(blob) - 12))
    recs, p, order = {}, 12, []
    while p < len(blob):
        if p + 8 > len(blob):
            raise CorError("truncated record header")
        t, n = struct.unpack("<II", blob[p:p + 8])
        p += 8
        if p + n > len(blob):
            raise CorError("record %d runs past the end" % t)
        if t in recs:
            raise CorError("record %d twice" % t)
        recs[t] = blob[p:p + n]
        order.append(t)
        p += n
    if order[:1] == [T_MANIFEST]:
        try:
            lm = json.loads(recs[T_MANIFEST])
        except ValueError as e:
            raise CorError("manifest is not JSON: %s" % e)
        if isinstance(lm, dict) and lm.get("kind") == KIND_LARGE:
            return parse_large(lm, recs, order)
    if T_BIT in recs:
        raise CorError("record %d in a bundle that is not a large core" % T_BIT)
    if order != sorted(order) or order[:1] != [T_MANIFEST] or T_PARTIAL not in recs or T_APP not in recs:
        raise CorError("records missing or out of order: %s" % order)
    try:
        m = json.loads(recs[T_MANIFEST])
    except ValueError as e:
        raise CorError("manifest is not JSON: %s" % e)
    if "short" in m and not (isinstance(m["short"], str) and SHORT_RE.fullmatch(m["short"])):
        raise CorError("manifest: short %r is not 1 to 4 of A-Z a-z 0-9 + / ." % (m["short"],))
    for k in ("name", "version", "contract", "fpga", "host_key", "partial_size", "partial_sha256",
              "app_size", "app_load", "app_entry", "app_sha256"):
        if k not in m:
            raise CorError("manifest lacks %s" % k)
    for part, t in (("partial", T_PARTIAL), ("app", T_APP), ("icon", T_ICON)):
        if part == "icon" and t not in recs and "icon_size" not in m:
            continue
        if t not in recs or (part + "_size") not in m:
            raise CorError("%s: record and manifest disagree on its presence" % part)
        if m[part + "_size"] != len(recs[t]):
            raise CorError("%s: size %d in the manifest, %d in the file" % (part, m[part + "_size"], len(recs[t])))
        if m[part + "_sha256"] != sha(recs[t]):
            raise CorError("%s: SHA-256 mismatch" % part)
    check_partial(recs[T_PARTIAL])
    if not (m["app_load"] <= m["app_entry"] < m["app_load"] + m["app_size"]):
        raise CorError("app: entry outside the image")
    if T_ICON in recs and len(recs[T_ICON]) != ICON_BYTES:
        raise CorError("icon: wrong size")
    check_optional(m, recs)
    norm_host_key(m["host_key"])                            # the field itself must be well formed
    if contract is not None and m["contract"] != contract:
        raise CorError("contract version %s, the host speaks %s" % (m["contract"], contract))
    if host_key is not None and m["host_key"] != norm_host_key(host_key):
        raise CorError("built for host %s, this host is %s" % (m["host_key"], norm_host_key(host_key)))
    return m, recs


REG_IDCODE = 0x0C


def scan_config(data):
    """(idcode or None, axss word or None) of a 7-series configuration data: the same walk as host_key_from_bit and as cfw.cc
    scan_config (after the sync word type-1 headers carry their words, type-2 headers skip the frame data, DESYNC ends it)."""
    i = data.find(struct.pack(">I", AXSS_SYNC))
    if i < 0:
        raise CorError("bitstream: no sync word")
    i += 4
    idcode = axss = None
    while i + 4 <= len(data):
        w, = struct.unpack_from(">I", data, i)
        i += 4
        t = w >> 29
        if t == 1:
            op, reg, count = (w >> 27) & 3, (w >> 13) & 0x3FFF, w & 0x7FF
            if op == 2 and count == 1 and i + 4 <= len(data):
                v, = struct.unpack_from(">I", data, i)
                if reg == REG_IDCODE:
                    idcode = v
                elif reg == REG_AXSS:
                    axss = v
                elif reg == REG_CMD and v == CMD_DESYNC:
                    break
            i += 4 * count
        elif t == 2:
            i += 4 * (w & 0x7FFFFFF)
        else:
            break
    return idcode, axss


ORIGINS = ("trx", "foreign", "backup")    # T137 D1: ours (no key at all), converted by the host from a .cfw/.ue2, read from this board's flash
FLASH_BIT = 0x000000                      # T137 D2: where a backup's bitstream lay in the flash ...
FLASH_APP = 0x220000                      # ... and its application
FLASH_ID_MAX = 40


def pack_large(cid, name, version, bit, app, icon=None, map_kind="auto", bit_commit=None, app_commit=None,
               fpga="xc7a50t", desc=None, libs=None, files=None, origin=None, flash_id=None, short=None, heap=None, heap_needs=None, report=None):
    """A .cor that carries a LARGE core: the full bitstream and the application (see the docstring).
    origin (T137 D1): None or "trx" writes no key (the bytes of every .cor before T137); "foreign" and "backup" write `origin`; a backup also
    writes flash_bit, flash_app and flash_id (D2: the chip the parts were read from, so they are written back only to the layout they came from)."""
    data = bit_config_data(bit)
    if len(bit) > LARGE_BIT_MAX:
        raise CorError("bitstream: %d bytes, the host stages at most %d" % (len(bit), LARGE_BIT_MAX))
    if len(data) < 64 or len(data) % 4:
        raise CorError("bitstream: not a configuration data of whole words")
    idcode, axss = scan_config(data)
    if idcode is None:
        raise CorError("bitstream: no IDCODE write; this is not a full bitstream")
    if (idcode ^ IDCODE_XC7A50T) & IDCODE_MASK:
        raise CorError("bitstream: IDCODE %08x is not the XC7A50T (%08x)" % (idcode, IDCODE_XC7A50T))
    if not app:
        raise CorError("app: empty")
    app = app_heap(app, heap, cid, heap_needs, report)
    if len(app) > LARGE_APP_MAX:
        raise CorError("app: %d bytes, the host stages at most %d" % (len(app), LARGE_APP_MAX))
    if len(app) > 12:
        h_load, h_len, h_entry = struct.unpack("<III", app[:12])
        if h_load == LARGE_APP_ADDR and h_entry == LARGE_APP_ADDR and h_len == len(app) - 12:
            raise CorError("app: this is a .app (12-byte header in front), not the image; pack the .bin beside it")
    if icon is not None and len(icon) != ICON_BYTES:
        raise CorError("icon: must be 32x32 RGB565 (%d bytes)" % ICON_BYTES)
    if map_kind == "auto":
        map_kind = "identity" if axss is not None else "gideon_bl4"
    if map_kind not in MAPS:
        raise CorError("map %r: one of %s" % (map_kind, ", ".join(MAPS)))
    m = {"format": LARGE_FORMAT, "kind": KIND_LARGE, "id": cid, "name": name, "version": version, "fpga": fpga,
         "idcode": "%08x" % idcode, "map": map_kind,
         "bit_size": len(bit), "bit_sha256": sha(bit),
         "app_size": len(app), "app_load": LARGE_APP_ADDR, "app_entry": LARGE_APP_ADDR, "app_sha256": sha(app)}
    put_short(m, short)
    if icon is not None:
        m.update(icon_size=len(icon), icon_sha256=sha(icon))
    if bit_commit:
        m["bit_commit"] = bit_commit
    if app_commit:
        m["app_commit"] = app_commit
    if origin not in (None,) + ORIGINS:
        raise CorError("origin %r: one of %s" % (origin, ", ".join(ORIGINS)))
    if origin in ("foreign", "backup"):
        m["origin"] = origin
    if (origin == "backup") != bool(flash_id):
        raise CorError("flash_id belongs to a backup and a backup needs it")
    if origin == "backup":
        if len(flash_id) > FLASH_ID_MAX:
            raise CorError("flash_id: %d characters, at most %d" % (len(flash_id), FLASH_ID_MAX))
        m.update(flash_bit=FLASH_BIT, flash_app=FLASH_APP, flash_id=flash_id)
    desc = stamp_desc(desc, axss, "the bitstream")
    extra = optional_records(m, desc, libs, files)
    man = json.dumps(m, sort_keys=True, separators=(",", ":")).encode()
    if len(man) > 2048:
        raise CorError("manifest: %d bytes, the host reads at most 2048" % len(man))
    body = rec(T_MANIFEST, man) + rec(T_APP, app)
    if icon is not None:
        body += rec(T_ICON, icon)
    body += rec(T_BIT, bit) + extra
    return MAGIC + struct.pack("<I", len(body)) + body


def parse_large(m, recs, order):
    """The checks of a large .cor (the manifest is already JSON); returns (manifest, records)."""
    if m.get("format") != LARGE_FORMAT:
        raise CorError("large core: format %r, this tool reads %d" % (m.get("format"), LARGE_FORMAT))
    if order[:1] != [T_MANIFEST] or order != sorted(order) or T_APP not in recs or T_BIT not in recs \
            or T_PARTIAL in recs or set(order) - {T_MANIFEST, T_APP, T_ICON, T_BIT, T_DESC, T_LIBS, T_FILES}:
        raise CorError("large core: records missing, extra or out of order: %s" % order)
    for k in ("id", "name", "version", "fpga", "idcode", "map", "bit_size", "bit_sha256", "app_size", "app_load",
              "app_entry", "app_sha256"):
        if k not in m:
            raise CorError("manifest lacks %s" % k)
    for k in ("contract", "host_key"):
        if k in m:
            raise CorError("large core: manifest has %s, which only a layer-2 core has" % k)
    for part, t in (("bit", T_BIT), ("app", T_APP), ("icon", T_ICON)):
        if part == "icon" and t not in recs and "icon_size" not in m:
            continue
        if t not in recs or (part + "_size") not in m:
            raise CorError("%s: record and manifest disagree on its presence" % part)
        if m[part + "_size"] != len(recs[t]):
            raise CorError("%s: size %d in the manifest, %d in the file" % (part, m[part + "_size"], len(recs[t])))
        if m[part + "_sha256"] != sha(recs[t]):
            raise CorError("%s: SHA-256 mismatch" % part)
    if len(recs[T_BIT]) > LARGE_BIT_MAX or len(recs[T_APP]) > LARGE_APP_MAX:
        raise CorError("a part is larger than the host's staging area")
    if m["app_load"] != LARGE_APP_ADDR or m["app_entry"] != LARGE_APP_ADDR:
        raise CorError("app: a full bitstream's application loads and starts at 0x%x" % LARGE_APP_ADDR)
    if m["map"] not in MAPS:
        raise CorError("map %r unknown" % m["map"])
    if T_ICON in recs and len(recs[T_ICON]) != ICON_BYTES:
        raise CorError("icon: wrong size")
    check_optional(m, recs)
    if m.get("origin", "trx") not in ORIGINS:
        raise CorError("origin %r unknown" % m.get("origin"))
    if (m.get("origin") == "backup") != all(k in m for k in ("flash_bit", "flash_app", "flash_id")) or \
            (m.get("origin") != "backup" and any(k in m for k in ("flash_bit", "flash_app", "flash_id"))):
        raise CorError("flash_bit/flash_app/flash_id belong to a backup, and a backup has all three")
    if m.get("origin") == "backup" and (m["flash_bit"], m["flash_app"]) != (FLASH_BIT, FLASH_APP):
        raise CorError("backup: the parts lay at 0x%x and 0x%x, this layout is 0x%x and 0x%x" % (m["flash_bit"], m["flash_app"], FLASH_BIT, FLASH_APP))
    idcode, axss = scan_config(bit_config_data(recs[T_BIT]))
    check_build(recs, axss, "the bitstream")
    if idcode is None or "%08x" % idcode != m["idcode"]:
        raise CorError("manifest says IDCODE %s, the bitstream has %s" % (m["idcode"], "none" if idcode is None else "%08x" % idcode))
    if (idcode ^ IDCODE_XC7A50T) & IDCODE_MASK:
        raise CorError("bitstream: IDCODE %08x is not the XC7A50T" % idcode)
    return m, recs


def load_icon(path):
    raw = open(path, "rb").read()
    if raw[:4] == b"\x89PNG":
        from PIL import Image  # optional
        im = Image.open(path).convert("RGB").resize((32, 32))
        out = bytearray()
        for r, g, b in im.getdata():
            out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        return bytes(out)
    return raw


def read_extras(a):
    """(desc record or None, the libraries of the TOML B (toml_b_libs) or None, [files] or None) from --desc, --sizes and --files."""
    desc = None
    if a.desc:
        try:
            if getattr(a, "sizes", None):                  # the core's TOML B: its [sizes] are the record maxima the app is linked with (tools/app_build.py)
                descrec.apply_sizes(descrec.toml_loads(open(a.sizes).read()).get("sizes", {}))
            if getattr(a, "toml_a", None):                 # the TOML A the core's app is linked with: a [[window]] may not cover a block of its base (default coreapp/gateware/host.toml)
                descrec.use_base_toml_a(a.toml_a)
            desc = descrec.compile_files([(f, open(f).read()) for f in a.desc], open(a.ops).read() if a.ops else None)
        except descrec.DescError as e:
            raise CorError("%s: %s" % (" + ".join(a.desc), e))
    files = collect_files(a.files, a.files_old) if a.files else None
    return desc, toml_b_libs(a.sizes) if getattr(a, "sizes", None) else None, files


def heap_needs_of(a):
    """The heap the core's linked libraries keep (P4-1): the [[library]] tables of the TOML B (--sizes) the app is linked with."""
    return toml_b_heap_needs(a.sizes) if getattr(a, "sizes", None) else []


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--partial", required=True)
    p.add_argument("--app", required=True)
    p.add_argument("--load", type=lambda s: int(s, 0), default=0x30000)
    p.add_argument("--entry", type=lambda s: int(s, 0), default=None)
    p.add_argument("--contract", type=int, default=CONTRACT)
    p.add_argument("--host-key", required=True, help="the host this partial was routed against, 8 hex")
    p.add_argument("--fpga", default="xc7a50t")
    p.add_argument("--icon")
    p.add_argument("--short", help="T209 D15: the text tile's short name (core.toml `short`)")
    p.add_argument("--heap", type=lambda s: int(s, 0), help="T239.3 D15: the core's heap in bytes (core.toml `heap`), set in the packed copy of the application; 0: the region left behind the image and the stack reserve (P4-1)")
    p.add_argument("--desc", action="append", default=[], help="the menu description (TOML, tools/descrec.py): record 6; repeat for a core's menu.toml and desc.toml (one record; a table or key in two files is refused)")
    p.add_argument("--sizes", help="the TOML B of the core's app: its [sizes] table sets the record maxima the description is compiled against (T239.3 P3-2); the heap its libraries keep (P4-1); the menu records of its libraries with `register` (record 7, base 4.0) and the library of [core] hooks")
    p.add_argument("--toml-a", help="the TOML A the core's app is linked with: a [[window]] of --desc may not cover a block of its base (T239.3 D30 point 1; default coreapp/gateware/host.toml)")
    p.add_argument("--ops", help="the operations and states the application exports (cores/amiga1200/app/tests/golden/ops_amiga1200.txt): every name and binding of --desc is checked against it")
    p.add_argument("--files", help="a folder: every file below it is installed into the core's folder at the same relative path: record 8")
    p.add_argument("--files-old", help="earlier shipped versions of those files, <name>.v<N>.<ext>: the app replaces a file only when it is byte for byte one of them")
    g = sub.add_parser("pack-large")
    g.add_argument("-o", "--out", required=True)
    g.add_argument("--id", required=True)
    g.add_argument("--name", required=True)
    g.add_argument("--version", required=True)
    g.add_argument("--bit", required=True, help="the full bitstream (.bit, header included)")
    g.add_argument("--app", required=True, help="the application image (coreapp.bin, raw, linked to 0x30000)")
    g.add_argument("--icon")
    g.add_argument("--short", help="T209 D15: the text tile's short name (core.toml `short`)")
    g.add_argument("--heap", type=lambda s: int(s, 0), help="T239.3 D15: the core's heap in bytes (core.toml `heap`), set in the packed copy of the application; 0: the region left behind the image and the stack reserve (P4-1)")
    g.add_argument("--desc", action="append", default=[], help="the menu description (TOML): record 6; repeat for a core's menu.toml and desc.toml (one record; a table or key in two files is refused)")
    g.add_argument("--sizes", help="the TOML B of the core's app: its [sizes] table sets the record maxima the description is compiled against (T239.3 P3-2); the heap its libraries keep (P4-1); the menu records of its libraries with `register` (record 7, base 4.0) and the library of [core] hooks")
    g.add_argument("--toml-a", help="the TOML A the core's app is linked with: a [[window]] of --desc may not cover a block of its base (T239.3 D30 point 1; default coreapp/gateware/host.toml)")
    g.add_argument("--ops", help="what the application exports (cores/amiga1200/app/tests/golden/ops_amiga1200.txt): every name and binding of --desc is checked against it")
    g.add_argument("--files", help="a folder: every file below it is installed into the core's folder at the same relative path: record 8")
    g.add_argument("--files-old", help="earlier shipped versions of those files, <name>.v<N>.<ext>: the app replaces a file only when it is byte for byte one of them")
    g.add_argument("--map", default="auto", choices=("auto",) + MAPS)
    g.add_argument("--bit-commit")
    g.add_argument("--app-commit")
    g.add_argument("--origin", choices=ORIGINS, default=None, help="T137 D1; trx (the default) writes no key")
    g.add_argument("--flash-id", help="a backup: the flash chip the parts were read from (get_type_string), T137 D2")
    f = sub.add_parser("files-record")
    f.add_argument("dir")
    f.add_argument("--old")
    f.add_argument("-o", "--out", required=True)
    r = sub.add_parser("replace-app")
    r.add_argument("file")
    r.add_argument("--app", required=True)
    r.add_argument("-o", "--out", required=True)
    k = sub.add_parser("hostkey")
    k.add_argument("file")
    h = sub.add_parser("heap")
    h.add_argument("app")
    h.add_argument("--core", required=True)
    h.add_argument("--sizes", help="the TOML B of the core's app: the heap its libraries keep (heap_keep)")
    h.add_argument("--heap", type=lambda s: int(s, 0), default=0, help="the core's heap in bytes (core.toml `heap`); 0 (default): the region left behind the image")
    c = sub.add_parser("check")
    c.add_argument("file")
    c.add_argument("--contract", type=int, default=None)
    c.add_argument("--host-key", default=None, help="refuse a bundle built for another host")
    a = ap.parse_args(argv)
    try:
        rep = []
        if a.cmd == "pack":
            entry = a.load if a.entry is None else a.entry
            blob = pack(a.name, a.version, open(a.partial, "rb").read(), open(a.app, "rb").read(),
                        a.load, entry, a.host_key, a.contract, a.fpga,
                        load_icon(a.icon) if a.icon else None, *read_extras(a), short=a.short, heap=a.heap, heap_needs=heap_needs_of(a), report=rep)
            open(a.out, "wb").write(blob)
            print("\n".join(rep + ["%s: %d bytes" % (a.out, len(blob))]))
        elif a.cmd == "pack-large":
            blob = pack_large(a.id, a.name, a.version, open(a.bit, "rb").read(), open(a.app, "rb").read(),
                              load_icon(a.icon) if a.icon else None, a.map, a.bit_commit, a.app_commit, "xc7a50t", *read_extras(a),
                              origin=a.origin, flash_id=a.flash_id, short=a.short, heap=a.heap, heap_needs=heap_needs_of(a), report=rep)
            open(a.out, "wb").write(blob)
            print("\n".join(rep + ["%s: %d bytes" % (a.out, len(blob))]))
        elif a.cmd == "files-record":
            blob = pack_files(collect_files(a.dir, a.old))
            open(a.out, "wb").write(blob)
            print("%s: %d bytes" % (a.out, len(blob)))
        elif a.cmd == "replace-app":
            blob = replace_app(open(a.file, "rb").read(), open(a.app, "rb").read())
            open(a.out, "wb").write(blob)
            print("%s: %d bytes" % (a.out, len(blob)))
        elif a.cmd == "hostkey":
            print(host_key_from_bit(open(a.file, "rb").read()))
        elif a.cmd == "heap":
            app_heap(open(a.app, "rb").read(), a.heap, a.core, heap_needs_of(a), rep)
            print("\n".join(rep))
        else:
            m, _ = parse(open(a.file, "rb").read(), a.contract, a.host_key)
            print(json.dumps(m, indent=1, sort_keys=True))
    except CorError as e:
        print("cor_pack: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
