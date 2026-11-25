# shellcheck shell=bash
# The driver table of the shared native units (T239.5 D15). Those units use cores that have moved to their own app as their sample cores (the Atari 800 XL, the NES: their libraries are compiled in
# them, from cores/<id>/app/lib), and the libraries name their registers through drvtab.h. The real shared app has no table of them, so the units get one made here: the blocks of
# coreapp/gateware/shared.toml (but its ident, the core window's standard block, which the first core's own replaces: the same id, caps and build, and caps2), the blocks of the first sample core, and
# the sections of the other sample cores' blocks (without their ident) appended to the header. Two cores' blocks lie in the same window, so no one table can hold both (the overlap rule of a
# record): the header is macros only, the sections of another core are added by text. The block names of the sample cores must differ: the NES's are nes_*, the Commodore 264's c264_*, the ZX Spectrum Next's zxn_*, the Amiga 500's am_* (the 264's own drive_a is left out: shared.toml has the drive block).
#   shared_drv_dir DIR   makes DIR/drv/drvtab.h (and the other files of a link's table) of the shared units. Sourced with ROOT set.
SHARED_SAMPLE_CORES="atari800 nes c264 zxnext amiga"
shared_drv_dir() {
  python3 - "$ROOT" "$1" $SHARED_SAMPLE_CORES <<'PY' || return 1
import os, re, sys
root, out, *cores = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
shared = os.path.join(root, "coreapp", "gateware", "shared.toml")
base = descrec.toml_loads(open(shared).read())
names = [b["name"] for b in base["block"] if b["name"] != "ident"]
docs = [descrec.toml_loads(open(os.path.join(root, "cores", c, "app", "gateware.toml")).read()) for c in cores]
doc = descrec.merge_toml_a(docs[0], base, names, "the shared units' table")
extra = []                                  # the sections of the other cores' own blocks (the header's per-block parts start at their DRV_HAS_<block> line)
for d in docs[1:]:
    own = descrec.merge_toml_a(d, base, [], "the table of a sample core")
    maps = descrec.resolve_maps(own, descrec.load_maps([os.path.join(root, "app", "sdkpkg", "maps")]))
    hd, _ = descrec.drivers_files(maps, "the table of a sample core")
    skip = {d["identity"]["block"], "drive_a"}
    parts = re.split(r"(?m)^(?=#define DRV_HAS_)", hd[:hd.index("#ifdef __cplusplus\nextern")])[1:]    # the trailer (the block list's declarations) is not a block's
    extra += [x for x in parts if re.match(r"#define DRV_HAS_(\w+) ", x).group(1) not in skip]
doc = descrec.resolve_maps(doc, descrec.load_maps([os.path.join(root, "app", "sdkpkg", "maps")]))
what = "the shared units' table"
head, src = descrec.drivers_files(doc, what)
io = (doc["io"]["iobase"], doc["io"]["u2p_io_base"])
os.makedirs(os.path.join(out, "drv"), exist_ok=True)
rows = [(b["name"], b["address"], b["size"]) for b in doc["block"] if b.get("owner") == "base"]
descrec._BASE_BLOCKS["path"], descrec._BASE_BLOCKS["rows"], descrec._BASE_BLOCKS["io"] = what, rows, io
app_build.write_if_changed(os.path.join(out, "drv", "basetab.h"), descrec.basetab_text(rows, what, io))
if extra:
    i = head.rindex("#endif")
    head = head[:i] + "".join(extra) + head[i:]
app_build.write_if_changed(os.path.join(out, "drv", "drvtab.h"), head)
app_build.write_if_changed(os.path.join(out, "drv", "drvtab.c"), src)
PY
}
