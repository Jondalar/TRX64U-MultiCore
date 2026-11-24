#!/usr/bin/env bash
# Stage `package`: what the earlier stages made, in $ROOT/out (the workflow uploads it as the artifact `package`; ci/update.sh and ci/prerelease.sh read it from there,
# and only the job `prerelease` of a push to main, t239.1 or a v* tag publishes it, ci/publish_release.py). Nothing is published here. The files: <design>.bit,
# <design>.timing.rpt, <design>.utilization*.rpt (tools/load.sh reads <design>.bit), manifest.json and SHA256SUMS over all of it, and one <core>.cor per core that has an
# application - the file the board loads, ready to copy onto the stick. The manifest records, per bitstream entry, the release (tag, id, SHA-256 of its SHA256SUMS and of
# each asset) the entry was fetched from (ci/fetch_bits.sh, T239.1 D19).
#
# T100 D9: the package is whole, so it carries every core's .cor and every large core's large .cor whatever the plan named: a target the plan did not name came
# out of the entries ci/collect.sh restored. What the plan names only decides what was built fresh. A red target of an earlier stage (host, a core, a large core)
# leaves its file out; the others are packed (D9.6); the pipeline starts the package only when every job before it is green.
#
# trx64u.cor and trx64g.cor (T239.1 D43, replacing D16): the builder packs them from release/imports/*.pin (ci/imports.py) and publishes them as entries
# imp-<id>-<key>; collect hands them over as bit-imports. trx64u.cor is TRX64-Ultimate's file (T239.3 D21, P1-8): the package takes it by the sha256 of its pin
# (release/imports/trx64u.pin) and changes no byte; trx64g.cor too since T239.3 D46 (release/imports/trx64g.pin).
#
# The 100T bitstream is not part of this: the builder's target ue2 makes it by hand (T239.1 D16).
#
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

out="$ROOT/out"
rm -rf "$out"; mkdir -p "$out"

red_reset package

# T174 D2 7/8 + D4: the complete release package (every .cor, the imports, the bundle). The parent starts this child on main, integ-*, release/*,
# tags (plan scope `full`) and when update files are asked for; a feature branch gets its touched .cor files from the child `cor` (ci/cor.sh).
# The bitstream lanes (T100 D10): stage bit-host (host + reference core + aux designs), bit-<core> per DFX core, large-<core> per large
# core, each in its own hand-over directory - one writer per directory, so lanes in parallel never touch each other's files (a shared
# directory would be emptied by the stage_dir of whichever lane starts last). They are merged here into one directory with the layout
# the rest of this script reads (stage bit-c64u: the files of every lane, wns.txt / plan_result.txt joined, layers.json merged).
# A whole-stage run (release, a stage run alone) left bit-c64u / bit-mono instead and is used as it is.
merge_lanes() { # merge_lanes <out dir> <stage dir>...: the first is the base (key, from), the others add their files
  local dst="$1" d f; shift
  mkdir -p "$dst"
  for d in "$@"; do
    for f in "$d"/*; do
      case "${f##*/}" in
        wns.txt|plan_result.txt) cat "$f" >> "$dst/${f##*/}" ;;
        layers.json|DONE|SHA256SUMS) ;;
        key|from) [ -e "$dst/${f##*/}" ] || cp "$f" "$dst/" ;;
        *) [ -e "$f" ] && cp "$f" "$dst/" ;;
      esac
    done
  done
  python3 - "$dst/layers.json" "$@" <<'PY'
import json, os, sys
dst, dirs = sys.argv[1], sys.argv[2:]
out = {"cores": {}}
for d in dirs:
    p = os.path.join(d, "layers.json")
    if os.path.exists(p):
        j = json.load(open(p))
        if "host" in j:
            out = {"host": j["host"], **out}
        out["cores"].update(j.get("cores", {}))
if "host" in out:
    json.dump(out, open(dst, "w"), indent=2)
PY
  touch "$dst/wns.txt"
  : > "$dst/DONE"
}
if [ -f "$HANDOFF/bit-c64u/DONE" ]; then
  c64u="$HANDOFF/bit-c64u"
else
  c64u="$HANDOFF/bit-lanes"; rm -rf "$c64u"
  lanedirs=()
  for lane in $(python3 "$ROOT/ci/plan.py" lanes); do
    case "$lane" in
      host) st=bit-host ;;
      core:*) st="bit-${lane#core:}" ;;
      *) continue ;;
    esac
    if [ -f "$HANDOFF/$st/DONE" ]; then lanedirs+=("$HANDOFF/$st")
    else
      echo "lane $lane left nothing in $HANDOFF/$st" >&2
      mark_red package "lane-${lane//:/-}" "the lane job handed nothing over"
    fi
  done
  [ -f "$HANDOFF/bit-host/DONE" ] || { echo "the host lane left nothing: no package" >&2; exit 1; }
  merge_lanes "$c64u" "${lanedirs[@]}"      # (bit-host is the first: its key and from are the package's)
fi
for g in "$c64u"/*.bit "$c64u"/*.rpt "$c64u"/*.gz "$c64u"/*.steps.txt; do [ ! -e "$g" ] || cp "$g" "$out/"; done
# layered build (T87): the host id/sha and per core the key and the partial's SHA-256 go into the manifest
[ ! -f "$c64u/layers.json" ] || cp "$c64u/layers.json" "$out/layers.json"
cp "$c64u/wns.txt" "$out/wns.c64u.txt"
[ -f "$c64u/plan_result.txt" ] && cat "$c64u/plan_result.txt"
# Every core's partial and full image goes into the package: the pre-release is how a build reaches the Mac and the board. The .cor files come from these.

# Our own applications (T94 P6): the host boot app goes into the package so an update file can be made
# from exactly this run, and the blueprint images are what the .cor bundles below are packed with. Until
# this stage existed they came from the generic package trx64-coreapps, uploaded by hand from a Mac -
# which is how trx64mc-main-51e89821 ended up carrying an application nobody had built from its commit.
trxapps="$(need_stage trxapps)"
cp "$trxapps/hostapp.app" "$trxapps/hostapp.elf" "$trxapps/coreapp.bin" "$trxapps/coreapp.elf" "$out/"   # T221: the ELF is stamped beside the image (release_promote.py rewrites both)
# T180 D9: Unzip, the one application every core reads from <root>/cores/apps/; the release bundle takes it from here (tools/release_mc.sh)
[ -s "$trxapps/unzip.u2a" ] && cp "$trxapps/unzip.u2a" "$out/" || mark_red package unzip "stage trxapps made no unzip.u2a"
# T197 P5: the zip library unzip.u2a loads (apps/miniz); the updater copies every .u2a beside the update file into <cores>/apps
[ -s "$trxapps/miniz.u2a" ] && cp "$trxapps/miniz.u2a" "$out/" || mark_red package miniz "stage trxapps made no miniz.u2a"
# T228 R21: the cartridge tools and Softboot, shipped the same way (the updater copies every .u2a beside the update file into <cores>/apps)
for a in trex_crt softboot online; do
  [ -s "$trxapps/$a.u2a" ] && cp "$trxapps/$a.u2a" "$out/" || mark_red package "$a" "stage trxapps made no $a.u2a"
done
cp "$trxapps/trxapps_commit" "$out/trxapps_commit"
# T172: the host app is cached by the content of its tree, so the version string it was built with is that of the run that filled the cache. The
# copy that ships is stamped with this run's version (the field TRXVSTAMP: in the image, host-app/target/common/rules.mk), before the .txu and the
# update files are made from it; the cache itself stays as it is. A long branch name is cut so the commit stays whole.
# T221: three fields, each from its own SemVer file: the host menu (TRXVSTAMP), the host gateware shown in the "host" chip (TRXHSTAMP) and the bundle (TRXBSTAMP).
# The core app of a blueprint core carries its own version (TRXVSTAMP, coreapp/version.toml); its copies are stamped below, before a .cor is packed with them.
for fld in "TRXVSTAMP hostapp" "TRXHSTAMP host" "TRXBSTAMP bundle"; do
  python3 -I "$ROOT/tools/stamp_version.py" stamp "$out/hostapp.app" "$out/hostapp.elf" --marker "${fld% *}" --version "$("$ROOT/ci/version.sh" "${fld#* }")" \
    || mark_red package stamp "tools/stamp_version.py could not stamp ${fld% *} into the host app"
done
python3 -I "$ROOT/tools/stamp_version.py" stamp "$out/coreapp.bin" "$out/coreapp.elf" --version "$("$ROOT/ci/version.sh" coreapp)" \
  || mark_red package stamp "tools/stamp_version.py could not stamp the core app"

# The applications the .cor bundles are packed with (stage trxapps, coreapps/): the blueprint image of every blueprint core, and for a core with a TOML B of its own
# (cores/<id>/app/sdk.toml, tools/core_toml.py list-own-app) its app linked from the SDK package (T239.3 V-5, D25: the other cores keep the shared image until each is pulled). trx64 is a
# Gideon-base build that lives in another tree and is not built in this repository (T93 5.2); a core without an application is named and skipped by
# pack_cores_mc.sh.
coreapps="$out/.coreapps"
mkdir -p "$coreapps"
cp "$trxapps/coreapps"/*.app "$coreapps/"
# V-5: a core with a TOML B of its own must have its linked app here, and it is not the blueprint image the others were copied from (the stage checks it too; a hand-over from the cache or an older run must not slip past)
own_apps="$(python3 -I "$ROOT/tools/core_toml.py" list-own-app)" || mark_red package own-app "core_toml.py list-own-app failed"
for c in $own_apps; do
  [ -s "$coreapps/$c.app" ] || mark_red package "$c" "stage trxapps made no linked app for $c (TOML B of its own)"
  ! cmp -s "$coreapps/$c.app" "$trxapps/coreapp.bin" || mark_red package "$c" "coreapps/$c.app is the shared image, not the app linked from the SDK package"
done
python3 -I "$ROOT/tools/stamp_version.py" stamp "$coreapps"/*.app --version "$("$ROOT/ci/version.sh" coreapp)" \
  || mark_red package stamp "tools/stamp_version.py could not stamp the version into the blueprint core apps"
ls -l "$coreapps"

# The .cor files of this package, packed here instead of by hand on the Mac (T100 §5, 2026-10-14).
# Alex waited for a person for them after every run: a .cor is the partial plus the core's
# application, bound to the AXSS word of THIS package's c64_basic.bit, and the boot app refuses it
# on any other host - so it can only be packed once the host of this very run is routed, and it is
# the one file he needs on the stick. A core without an application in the stage is named and
# skipped, exactly as on the Mac.
if [ -f "$out/c64_basic.bit" ]; then
  "$ROOT/tools/pack_cores_mc.sh" --bits "$out" --app-dir "$coreapps" --out "$out" || {
    echo "packing the .cor files failed; the package keeps the bitstreams" >&2
    rm -f "$out"/*.cor
    mark_red package cor "packing the .cor files failed"
  }
else
  echo "no c64_basic.bit (the host is red): no .cor can be packed against it"
fi
rm -rf "$coreapps"

# The large .cor of every large core (T115 D9, Alex 2026-10-28: our cores always as .cor, with logo and metadata): the flat bitstream
# of stage bit-mono and the generic core application of stage trxapps, packed by tools/pack_large_cor.sh (python3 only); the name and
# version come from cores/<id>/mono/core.toml. (`delivery = "cfw"` there still means "a flat build"; no .cfw is shipped for it.)
mono=""
if [ -f "$HANDOFF/bit-mono/DONE" ]; then
  mono="$HANDOFF/bit-mono"
else
  monodirs=()
  for lane in $(python3 "$ROOT/ci/plan.py" lanes); do
    case "$lane" in large:*) ;; *) continue ;; esac
    if [ -f "$HANDOFF/large-${lane#large:}/DONE" ]; then monodirs+=("$HANDOFF/large-${lane#large:}")
    else echo "lane $lane left nothing" >&2; mark_red package "lane-${lane//:/-}" "the lane job handed nothing over"; fi
  done
  if [ "${#monodirs[@]}" -gt 0 ]; then mono="$HANDOFF/large-lanes"; rm -rf "$mono"; merge_lanes "$mono" "${monodirs[@]}"; fi
fi
if [ -n "$mono" ]; then
  for bit in "$mono"/*_mono.bit; do
    [ -e "$bit" ] || continue
    core="$(basename "$bit" _mono.bit)"
    echo "=== large .cor of $core"
    desc="$ROOT/cores/$core/mono/core.toml"
    # T255 D6: app = "none" (the core's bitstream has no CPU): the application record is a 128 byte placeholder, not the generic core application
    appbin="$out/coreapp.bin"
    if [ "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" app)" = none ]; then
      appbin="$out/.noapp_$core.bin"; head -c 128 /dev/zero > "$appbin"
    fi
    if ! "$ROOT/tools/pack_large_cor.sh" --id "$core" --name "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" name)" \
        --version "$("$ROOT/ci/version.sh" with "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" version)")" \
      --short "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" short)" --bit "$bit" --app-bin "$appbin" \
        --bit-commit "$(git -C "$ROOT" rev-parse --short=8 HEAD)" --app-commit "$(cut -c1-8 "$trxapps/trxapps_commit")" --out "$out"; then
      echo "the large .cor of $core could not be packed" >&2
      rm -f "$out/$core.cor"
      mark_red package "cor-$core" "tools/pack_large_cor.sh failed"
    fi
    rm -f "$out/.noapp_$core.bin"
  done
  for bit in "$mono"/*_mono.bit; do [ -e "$bit" ] && cp "$bit" "$out/"; done
  cp "$mono"/*_mono.timing.rpt "$mono"/*_mono.steps.txt "$mono"/*_mono.vivado.log.gz "$out/" 2>/dev/null || true
  cat "$mono/plan_result.txt" 2>/dev/null || true
else
  echo "no large-core lane handed anything over: no large core in this package"
fi

# T239.1 D43: trx64u.cor and trx64g.cor from the builder's entries (collect: bit-imports). Both pins have COR_SHA256 (T239.3 D21, D46): each file is checked
# against its sha256 and taken unchanged (ci/imports.py finish).
if [ -f "$HANDOFF/bit-imports/DONE" ]; then
  for c in "$HANDOFF/bit-imports"/*.cor; do
    [ -e "$c" ] || continue
    id="$(basename "$c" .cor)"
    python3 -I "$ROOT/ci/imports.py" finish "$c" -o "$out/$id.cor" \
      && python3 "$ROOT/tools/cor_pack.py" check "$out/$id.cor" > /dev/null \
      || { rm -f "$out/$id.cor"; mark_red package "cor-$id" "ci/imports.py could not finish $id.cor"; }
  done
else
  echo "no bit-imports handed over: no trx64u.cor, no trx64g.cor"
fi

# T148 D8: ROMS.txt (what each core needs in its own folder, the legal sources; made from the descriptors' ROM table and the manual) is part of the
# package, and the package holds .cor files only - never a core folder, which is the user's (tools/check_package_layout.py).
python3 "$ROOT/tools/gen_roms_txt.py" -o "$out/ROMS.txt" || mark_red package roms-txt "tools/gen_roms_txt.py failed"
python3 "$ROOT/tools/check_package_layout.py" "$out" || mark_red package layout "the package holds a core folder"

sha="$(git -C "$ROOT" rev-parse --short=8 HEAD)"

# T152 D3/D5: the MultiCore update file (.txu, trx64host_update_<file_id>.txu: the version on a tag, else the commit) of this run: the host bitstream the update stage writes into the flash (null_full.bit, the host with the null core
# in the partition, T123) and OUR host app, both from this run. The host part carries the host key (its AXSS stamp) and the host-app interface version it
# provides; the app part the interface it needs. tools/txu_pack.py checks both like the host does before the first erase. No null_full.bit (the host lane is
# red or left nothing): the file is not made and the red target says so - an app-only .txu is not made here on purpose, the update stage works from a whole run.
if [ -f "$out/null_full.bit" ] && [ -f "$out/hostapp.app" ]; then
  "$ROOT/tools/pack_txu_mc.sh" --host "$out/null_full.bit" --app "$out/hostapp.app" --branch "$(branch_slug)" --sha "$sha" --file-id "$(file_id)" --version "$("$ROOT/ci/version.sh" bundle)" \
    --commit "$(git -C "$ROOT" rev-parse --short=15 HEAD)" --out "$out" || mark_red package txu "tools/pack_txu_mc.sh could not pack the .txu"
else
  echo "no null_full.bit or hostapp.app in this package: no .txu"
fi
BITDIR="$c64u" HANDOFF_DIR="$HANDOFF" python3 - "$out" "$sha" "$(git -C "$ROOT" rev-parse HEAD)" "$(branch_slug)" "$(cat "$trxapps/trxapps_commit")" "$("$ROOT/ci/version.sh" bundle)" \
  "$("$ROOT/ci/version.sh" host)" "$("$ROOT/ci/version.sh" hostapp)" "$("$ROOT/ci/version.sh" coreapp)" "$ROOT" <<'PY'
import hashlib, json, os, sys
out, short, full, branch, trxapps, version, v_host, v_menu, v_coreapp, root = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import cor_pack
def cor_versions():          # T221 D6: the version of every core in the package, read from the .cor files themselves
    vs = {}
    for f in sorted(os.listdir(out)):
        if f.endswith(".cor"):
            m, _ = cor_pack.parse(open(os.path.join(out, f), "rb").read())
            vs[f[:-4]] = m["version"]
    return vs
def wns(name):
    p = os.path.join(out, name)
    return dict(l.split() for l in open(p)) if os.path.exists(p) else {}
# the releases the bitstream entries came from (ci/fetch_bits.py wrote bit-releases.json into the hand-over directory)
rp = os.path.join(os.environ["HANDOFF_DIR"], "bit-releases.json")
bitstreams = json.load(open(rp)) if os.path.exists(rp) else None
files = {}
for f in sorted(os.listdir(out)):
    if (f.endswith(".txt") and f.startswith("wns.")) or f in ("layers.json", "trxapps_commit"):
        continue
    data = open(os.path.join(out, f), "rb").read()
    files[f] = {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
json.dump({"trx64_commit": full, "trxapps_commit": trxapps, "branch": branch, "version": version,
           "parts": {"host_gateware": v_host, "host_menu": v_menu, "core_app": v_coreapp, "cores": cor_versions()},
           "gateware_key": open(os.path.join(os.environ["BITDIR"], "key")).read().strip(),
           "bitstream_from": open(os.path.join(os.environ["BITDIR"], "from")).read().strip(),
           "wns_ns": wns("wns.c64u.txt"),
           "layers": json.load(open(os.path.join(out, "layers.json"))) if os.path.exists(os.path.join(out, "layers.json")) else None,
           "bitstreams": bitstreams,
           "files": files}, open(os.path.join(out, "manifest.json"), "w"), indent=2)
PY
rm -f "$out"/wns.*.txt "$out/layers.json" "$out/trxapps_commit"
(cd "$out" && sha256sum $(ls | grep -v '^SHA256SUMS$') > SHA256SUMS)
ls -l "$out"
cat "$out/manifest.json"

stage_verdict package || exit 1

# the run's hand-over directory goes, unless ci/select.sh ue2 says yes here; that needs UE2_INPUT, which the package job does not get, so it always goes
[ "$("$ROOT/ci/select.sh" ue2)" = yes ] || rm -rf "${HANDOFF:?}"
echo "=== package done in $(lap)"
