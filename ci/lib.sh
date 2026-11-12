# shellcheck shell=bash
# Sourced by the stage scripts (T31). Where a run keeps what one stage hands to
# the next: /cache/multicore/runs/<run id>/<stage>/ on a runner (the volume the Vivado cache
# already uses), .ci/runs/local/<stage>/ in a checkout. The multicore/ part matters: this
# repository and TREX/TRX64-Ultimate share the builder and its /cache, their run ids are
# numbered independently, and the two product lines share the machine, not the cache (T94 D7,
# agreed with the TRX64-Ultimate session 2026-10-13). Only /cache/heavy.lock stays shared (slot 0 of
# heavy_lock below, which gives this repository CI_HEAVY_SLOTS Vivado runs at a time). A stage
# that finished writes its directory completely and a file DONE last.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=pkg.env
source "$ROOT/ci/pkg.env"
RUN_ID="${CI_RUN_ID:-${GITHUB_RUN_ID:-local}}"
if [ -n "${CI_HANDOFF:-}" ]; then
  HANDOFF="$CI_HANDOFF"
elif [ -d /cache ] && [ -w /cache ]; then
  HANDOFF="/cache/multicore/runs/$RUN_ID"
else
  HANDOFF="$ROOT/.ci/runs/$RUN_ID"
fi

# stage_dir <stage>: the stage's hand-over directory, emptied.
stage_dir() {
  rm -rf "${HANDOFF:?}/$1"
  mkdir -p "$HANDOFF/$1"
  printf '%s\n' "$HANDOFF/$1"
}

# need_stage <stage>: the directory of a finished earlier stage, or stop.
need_stage() {
  if [ ! -f "$HANDOFF/$1/DONE" ]; then
    echo "stage $1 left nothing in $HANDOFF/$1" >&2
    return 1
  fi
  printf '%s\n' "$HANDOFF/$1"
}

# --- build by change (T100 D9, ci/plan.py) -----------------------------------------------------------------------
# plan_has <target>: 0 when the plan of this run names the target (host, aux, dfx:<core>, large:<core>, app:<kind>). Without a
# plan (a stage run alone, the release, a local run) everything is named: the stage builds as it did before D9.
plan_has() { python3 "$ROOT/ci/plan.py" has "$HANDOFF/plan.json" "$1"; }
# plan_limit <host|core|large>: the minutes one Vivado run of that kind may take (CI_LIMIT_SECONDS replaces it, for the benches).
plan_limit() { python3 "$ROOT/ci/plan.py" limit "$1" "$HANDOFF/plan.json"; }

# vivado_limited <kind> <rc file> <command...>: the command with the time limit of its kind; its exit code goes to the rc file
# (the caller pipes the output into a filter, which would lose it). 124 = stopped at the limit (D9.4: that target is red, the
# stage goes on with the next).
vivado_limited() {
  local kind="$1" rcf="$2" secs rc; shift 2
  secs="${CI_LIMIT_SECONDS:-$(( $(plan_limit "$kind") * 60 ))}"
  echo "time limit for this $kind run: $((secs / 60)) min ($secs s)" >&2      # stderr: stdout goes through the log filter
  rc=0; timeout -k 30 "$secs" "$@" || rc=$?
  echo "$rc" > "$rcf"
  [ "$rc" -ne 124 ] || echo "TIMEOUT: the $kind run was stopped after $((secs / 60)) min" >&2
  return 0
}

# Red targets (D9.6, Alex 2026-10-25: "A"): a target that fails is red, every other target of the stage is still built, and the stage ends red
# (T174 D4: at once - the child is red with its reason, nothing waits for a last job). ci/child.sh gives each child its own red directory.
RED_DIR="${CI_RED_DIR:-$(dirname "$HANDOFF")/$(basename "$HANDOFF").red}"
red_reset() { mkdir -p "$RED_DIR"; rm -f "${RED_DIR:?}/$1."*; }
mark_red() { mkdir -p "$RED_DIR"; printf '%s\n' "$3" > "$RED_DIR/$1.$2"; echo "RED target $2 ($1): $3" >&2; report_red "$1" "$2" "$3"; }
# report_red <stage> <target> <text>: a line in the job summary; the commit status is the child's (ci/child.sh).
report_red() { [ -z "${GITHUB_STEP_SUMMARY:-}" ] || printf 'RED %s.%s: %s\n' "$1" "$2" "$3" >> "$GITHUB_STEP_SUMMARY" 2>/dev/null || true; }
reds_of() { local f; for f in "$RED_DIR/$1."*; do [ -e "$f" ] && echo "${f##*/$1.}: $(cat "$f")"; done; return 0; }
# stage_verdict <stage>: 0 when the stage has no red target; 1 (and the list) otherwise.
stage_verdict() {
  local r; r="$(reds_of "$1")"
  [ -n "$r" ] || return 0
  echo "=== red targets of stage $1:"; printf '%s\n' "$r"
  return 1
}
# plan_note <stage dir> <target> <text>: one line of what the stage did with a target, kept for the summary.
plan_note() { echo "plan: $2 - $3"; printf '%s: %s\n' "$2" "$3" >> "$1/plan_result.txt"; }

# lap: seconds since the previous lap (or the start of the script).
_lap_t=$SECONDS
lap() { local d=$((SECONDS - _lap_t)); _lap_t=$SECONDS; printf '%dm%02ds' $((d / 60)) $((d % 60)); }

branch_slug() { printf '%s' "${GITHUB_REF_NAME:-$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)}" | tr '/' '_'; }

# tree_key <pathspec...>: 16 hex digits over the content of the tracked files
# under the pathspecs, plus (when the checkout has it) the content of vendor/gideon
# and the free text in $KEY_EXTRA. Only the content counts, not commits: a push
# that leaves these files alone gets the same key.
tree_key() {
  {
    # a core's logo (cores/<id>/logo/, T114) is a picture for the boot menu, not gateware: it never moves a key
    git -C "$ROOT" ls-files -s -- "$@" ':(exclude,glob)cores/*/logo/**'
    if [ -d "$ROOT/vendor/gideon" ]; then
      # Only what the gateware reads: the VHDL and the files beside it (vendor_gideon.sh `files`). The software of the two
      # applications in the same tree (software/, tools/, target/, roms/) is not gateware: a change to it moves no bitstream key.
      # VENDOR.md only records the commit the copy was made from and would move every key (run 898 rebuilt the host for a change
      # without one VHDL line); the copied files themselves count
      (cd "$ROOT" && { find vendor/gideon/fpga vendor/gideon/target/simulation -type f
                        ls vendor/gideon/LICENSE.txt vendor/gideon/software/io/usb/nano_minimal.nan vendor/gideon/software/portable/riscv/bootloader_u64ii.c vendor/gideon/tools/parse_nano.py; } | LC_ALL=C sort | xargs sha256sum)
    fi
    printf '%s\n' "${KEY_EXTRA:-}"
  } | sha256sum | cut -c1-16
}

# files_key <pathspec...>: 16 hex digits over the CONTENT (working tree, so an
# edited checkout counts as well as a clean clone) of the tracked files under the
# pathspecs, plus the commit of every submodule among them. Returns 1, printing
# nothing, when a plain pathspec matches no tracked file: a typo in a dependency
# list must never turn into "depends on nothing". A pathspec that starts with
# ':(exclude)' only narrows the others.
files_key() {
  local spec list names
  for spec in "$@"; do
    case "$spec" in ':(exclude)'*) continue ;; esac
    if [ -z "$(git -C "$ROOT" ls-files -- "$spec" | head -1)" ]; then
      echo "files_key: '$spec' matches no tracked file" >&2
      return 1
    fi
  done
  list="$(mktemp)"; names="$(mktemp)"
  git -C "$ROOT" ls-files -s -- "$@" > "$list"
  awk -F'\t' '{ split($1, a, " "); if (a[1] != "160000") print $2 }' "$list" > "$names"
  {
    awk -F'\t' '{ split($1, a, " "); if (a[1] == "160000") print "submodule", a[2], $2 }' "$list"
    (cd "$ROOT" && git hash-object --stdin-paths < "$names") | paste -d' ' - "$names"
  } | sha256sum | cut -c1-16
  rm -f "$list" "$names"
}

# tool_versions: the first version line of each simulator on PATH, on one line.
tool_versions() {
  { iverilog -V 2>/dev/null | head -1; ghdl --version 2>/dev/null | head -1; verilator --version 2>/dev/null | head -1; } | tr '\n' ' '
}

# gateware_key <part>: what decides a bitstream. The RTL and the build scripts
# (not the benches), the constraints, Gideon's VHDL from vendor/gideon (so a
# renewal of the copy that moves fpga/ counts, one that does not does not), the options that change the netlist, the part and the Vivado image.
gateware_key() {
  KEY_EXTRA="$1 vivado-env:2024.1 sid=${TRX_SID:-} slot=${T16_SLOT:-} features=${TRX_FEATURES:-} t79=${T79_HOST_ONLY:-}/${T79_PLACE:-}/${T79_ROUTE:-}/${T79_NO_DFX:-}/${T79_NO_DRIVE:-}/${T79_FLOOR:-} core=${TRX_CORE:-}" tree_key \
    designs constraints cores tools/vivado_lib.tcl tools/vendor_gideon.sh tools/patches/gideon \
    ':(exclude)designs/*/sim' ':(exclude)designs/*/build'
}

# mono_key <part> <core>: what decides the flat bitstream of a core with delivery = "cfw" (T113 Q12, ci/bitstream_mono.sh). The core's own sources and its
# build variant (mono_env), plus the shared: files the build reads in place (vendor/icepi/hdmi, cores/common, the cores/common trx_* files, tools/vivado_lib.tcl, Gideon's
# vendoring). NOT host/ and NOT the reference core: a large core builds from its own copy of the host files (cores/<id>/mono/host, part of its sources;
# T164 3a Q1, Alex 2026-11-01: "If that now needs a new core every time, then no include!" / "Copies instead."), so a change in host/ moves no large core's key
# (run 1126 had rebuilt all of them for a host-only change). The shared files the host alone reads (host/gideon_vhdl.tcl) are left out of the key with host/.
# The core's benches (sim/) and its measurement reports (mono/reports/) are in the core's sources for the layer check (ci/layers_check.sh) but are no input of the build.
mono_key() {
  local lf="${3:-$ROOT/host/layers.txt}" specs l env
  specs=()
  while IFS= read -r l; do
    case "$l" in host/*|:\(exclude\)host/*) ;; *) specs+=("$l");; esac
  done < <(layer_specs "$lf" shared)
  while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$lf" "core:$2")
  specs+=(":(exclude)cores/$2/sim" ":(exclude)cores/$2/mono/reports")
  env="$(python3 "$(layer_root "$lf")/tools/core_toml.py" env "$2" | tr '\n' ' ')"
  KEY_EXTRA="mono $2 $1 vivado-env:2024.1 env=$env" tree_key "${specs[@]}"
}

# aux_key <part>: ddr2_test and hdmi_test (T87 D7), on their own files, not on the chip's.
aux_key() {
  KEY_EXTRA="aux $1 vivado-env:2024.1" tree_key designs/ddr2_test designs/hdmi_test \
    host/cal_seq.v host/ddr2_lean.v host/jtag_dbg.v host/ddr2_pins.xdc \
    constraints tools/vivado_lib.tcl tools/vendor_gideon.sh tools/patches/gideon \
    ':(exclude)designs/*/sim' ':(exclude)designs/*/build'
}

# cache_put <dir> <key-dir>: copy a finished stage directory into the cache atomically.
# Nothing is deleted here: what stays is decided by ci/cache_gc.sh (pins, history, size cap;
# T87 D14), not by a count of the newest entries of one kind.
cache_put() {
  local src="$1" dst="$2"
  mkdir -p "$(dirname "$dst")"
  rm -rf "$dst.tmp.$$"
  cp -R "$src" "$dst.tmp.$$"
  rm -rf "$dst"
  mv "$dst.tmp.$$" "$dst"
}

# pin_entry <entry dir> [group]: this branch uses the entry (T87 D14). Writes pins/branch.<slug>
# (the HEAD commit in it) and removes that pin from the other entries of the same group (the
# entry's directory name without its key; the group is given when the name differs), so a
# branch pins exactly one entry per group. main and integ-m1 pins are never removed by a
# branch move (cache_gc.sh treats them as permanent), a v* tag pins with pins/tag.<name>.
pin_entry() {
  local entry="$1" base group slug other
  base="$(basename "$entry")"
  group="${2:-${base%-*}}"
  if [ "${GITHUB_REF_TYPE:-}" = tag ]; then
    mkdir -p "$entry/pins"
    git -C "$ROOT" rev-parse HEAD > "$entry/pins/tag.${GITHUB_REF_NAME:-unknown}"
    return 0
  fi
  slug="$(branch_slug)"
  for other in "$(dirname "$entry")/$group"-*; do
    [ -d "$other" ] && [ "$other" != "$entry" ] && rm -f "$other/pins/branch.$slug"
  done
  mkdir -p "$entry/pins"
  git -C "$ROOT" rev-parse HEAD > "$entry/pins/branch.$slug"
}

# --- layers (T87, host/layers.txt) -----------------------------------------------

# layer_root <layers file>: the repository the file belongs to (host/layers.txt -> the root), so
# the helpers also work on a scratch clone whose path is not $ROOT.
layer_root() { local d="${1%/layers.txt}"; printf '%s' "${d%/host}"; }
# layer_sections <layers file>: the names of the sections ("host", "shared", "ignore") plus one
# "core:<name>" per core descriptor (T94 D1: cores/<name>/core.toml replaced the core: sections).
layer_sections() {
  sed -n -E 's/^(host|shared|ignore):[[:space:]]*$/\1/p' "$1"
  python3 "$(layer_root "$1")/tools/core_toml.py" list | sed 's/^/core:/'
}
# layer_specs <layers file> <section>: the pathspecs of one section, one per line. A core's are
# the sources of its descriptor.
layer_specs() {
  case "$2" in core:*) python3 "$(layer_root "$1")/tools/core_toml.py" sources "${2#core:}"; return ;; esac
  awk -v want="$2" '
    /^[[:space:]]*(#|$)/ { next }
    /^(host|shared|ignore):[[:space:]]*$/ { sec = $0; sub(/:.*/, "", sec); next }
    /^core:[A-Za-z0-9_-]+[[:space:]]*$/ { sec = $0; sub(/[[:space:]]+$/, "", sec); next }
    /^reference:/ { sec = "reference"; next }
    sec == want { sub(/[[:space:]]+$/, ""); print }' "$1"
}
# layer_cores <layers file>: the core names in build order (core.toml order field).
layer_cores() { python3 "$(layer_root "$1")/tools/core_toml.py" list; }
# layer_reference <layers file>: the core the static is routed with.
layer_reference() { sed -n 's/^reference:[[:space:]]*//p' "$1" | head -1; }

# feat_on <name>: 0 when the optional block is on in TRX_FEATURES (unset or empty = all on).
feat_on() {
  [ -n "${TRX_FEATURES:-}" ] || return 0
  case ",$(printf '%s' "$TRX_FEATURES" | tr ' ' ',')," in *",$1,"*) return 0 ;; *) return 1 ;; esac
}
# feat_state <names...>: "name=on|off ..." for the given blocks.
feat_state() { local n; for n in "$@"; do if feat_on "$n"; then printf '%s=on ' "$n"; else printf '%s=off ' "$n"; fi; done; }

# layer_shared_key <layers file>: the content of the shared: files (its own call: an ':(exclude)' of
# the host section must not take them out again).
layer_shared_key() {
  local specs l
  specs=(); while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$1" shared)
  KEY_EXTRA="shared" tree_key "${specs[@]}"
}

# host_key <part> [layers file]: D3 as amended in section 5 (run 400): the host files, the REFERENCE core's key (the static is routed together with the reference core, and its partial comes out of that very run), the host build options (the blocks that
# live in the host, the placer/router knobs), part and Vivado. Nothing of a core section.
host_key() {
  local lf="${2:-$ROOT/host/layers.txt}" specs l
  specs=(); while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$lf" host)
  KEY_EXTRA="host ref-core=$(layer_reference "$lf"):$(ooc_key "$1" "$(layer_reference "$lf")" "$lf") shared=$(layer_shared_key "$lf") $1 vivado-env:2024.1 $(feat_state uci jtag streams)t79=${T79_PLACE:-}/${T79_ROUTE:-}/${T79_NO_DFX:-}/${T79_FLOOR:-}" tree_key "${specs[@]}"
}
# ooc_key <part> <core> [layers file]: D4. That core's files, the options that change its
# netlist (SID, slot, the blocks that live in the core), part and Vivado. No host in it.
ooc_key() {
  local lf="${3:-$ROOT/host/layers.txt}" specs l
  specs=(); while IFS= read -r l; do specs+=("$l"); done < <(layer_specs "$lf" "core:$2")
  [ "${#specs[@]}" -gt 0 ] || { echo "ooc_key: $lf has no section core:$2" >&2; return 1; }
  KEY_EXTRA="core $2 shared=$(layer_shared_key "$lf") $1 vivado-env:2024.1 sid=${TRX_SID:-} slot=${T16_SLOT:-} $(feat_state reu sampler turbo)drive=${T79_NO_DRIVE:-}" tree_key "${specs[@]}"
}
# core_key <host key> <ooc key>: D5. A core's implementation depends on the host it is routed
# against and its own synthesis; the implementation knobs are part of it.
core_key() { printf '%s %s impl=%s/%s\n' "$1" "$2" "${T79_PLACE:-}" "${T79_ROUTE:-}" | sha256sum | cut -c1-16; }

# manifest_get <manifest.json> <field>: one top-level string field.
manifest_get() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))' "$1" "$2"; }

# The update files of stage update are the .cfw/.ue2 only (T172). trx64host_update_<sha8>.txu is stage package's file of the same package version
# and the sweep must not touch it (run 1128: it matched on the name prefix, the .txu was deleted and its SHA256SUMS line dropped).
# update_file_names: the Gitea files listing (JSON, stdin) -> names of the update files in it.
update_file_names() {
  python3 -c 'import json,sys
for r in json.load(sys.stdin):
    n = r["name"]
    if n.startswith(("trx64_update_", "trx64host_update_")) and n.endswith((".cfw", ".ue2")): print(n)'
}
# update_sums_without_ours: a SHA256SUMS (stdin) without the lines of update files (.cfw/.ue2).
update_sums_without_ours() { grep -vE ' (trx64_update_|trx64host_update_).*\.(cfw|ue2)$'; }

# publish_files <dir> [reset]: every file of <dir> into the generic package
# $MC_PKG (ci/pkg.env, never the firmware repository's), versions <branch>-<8 digit commit> and <branch>-latest of the
# Gitea registry. A file cannot be replaced in place, so the old one is deleted
# first; with "reset" the whole version goes first. Needs PACKAGE_TOKEN.
publish_files() {
  local dir="$1" reset="${2:-}" owner version f
  if [ -z "${PACKAGE_TOKEN:-}" ]; then
    echo "PACKAGE_TOKEN is not set for this repository; nothing is published"
    return 0
  fi
  owner="${GITHUB_REPOSITORY%%/*}"
  local api="${GITHUB_SERVER_URL}/api/packages/${owner}/generic/${MC_PKG}"
  for version in "$(branch_slug)-$(git -C "$ROOT" rev-parse --short=8 HEAD)" "$(branch_slug)-latest"; do
    [ -z "$reset" ] || curl -s -o /dev/null -X DELETE -H "Authorization: token ${PACKAGE_TOKEN}" "$api/$version" || true
    for f in "$dir"/*; do
      curl -s -o /dev/null -X DELETE -H "Authorization: token ${PACKAGE_TOKEN}" "$api/$version/$(basename "$f")" || true
      curl -sf -X PUT -H "Authorization: token ${PACKAGE_TOKEN}" --upload-file "$f" "$api/$version/$(basename "$f")"
    done
    echo "published $MC_PKG $version"
  done
}

# heavy_lock: the Vivado runs of one machine are limited by SLOTS (T100 D11). Slot 0 is /cache/heavy.lock, the lock the
# TRX64-Ultimate repository takes as well (its semantics are unchanged: one holder); the further slots are this repository's own,
# /cache/multicore/heavy.slot<n>. CI_HEAVY_SLOTS (default 2) is how many Vivado runs of this repository may run at a time, so with
# slot 0 busy by the other repository the run still gets slot 1. Every slot is tried without waiting, in turn; when all are taken the
# caller polls (CI_HEAVY_POLL seconds, default 5) until one is free. The slot held is printed. flock works across the job containers
# of one host because they share its kernel; the builder reaches /cache over NFS, where flock becomes a POSIX lock on the NAS - the
# slot files are on the same volume, so all builder jobs see each other, while a NAS-local process does not see a builder lock
# (NAS and builder each run their own heavy stages). Idempotent: a script that calls it again keeps its slot; held until it exits.
# Tests: CI_HEAVY_LOCK (slot 0 file) and CI_HEAVY_SLOT_DIR (directory of the other slots) replace the /cache paths.
heavy_lock() {
  [ -z "${HEAVY_SLOT:-}" ] || return 0
  local lock0="${CI_HEAVY_LOCK:-/cache/heavy.lock}" dir="${CI_HEAVY_SLOT_DIR:-/cache/multicore}" n i f waited=""
  [ -d "$(dirname "$lock0")" ] && [ -w "$(dirname "$lock0")" ] && command -v flock >/dev/null || return 0
  n="${CI_HEAVY_SLOTS:-2}"
  mkdir -p "$dir" 2>/dev/null || n=1
  while :; do
    i=0
    while [ "$i" -lt "$n" ]; do
      if [ "$i" -eq 0 ]; then f="$lock0"; else f="$dir/heavy.slot$i"; fi
      exec 9>"$f"
      if flock -n 9; then
        HEAVY_SLOT="$i"
        echo "heavy lock held: slot $i of $n ($f)"
        return 0
      fi
      exec 9>&-
      i=$((i + 1))
    done
    [ -n "$waited" ] || echo "waiting for a free heavy slot (all $n are taken: $lock0, $dir/heavy.slot*)"
    waited=1
    sleep "${CI_HEAVY_POLL:-5}"
  done
}

# heavy_trylock: a slot only if one is free now (or already held); else 1. For the cache janitor at the end of a stage that built
# nothing: a run of cache hits must not wait an hour behind someone else's Vivado just to clean up (run 260, 2026-10-30).
heavy_trylock() {
  [ -z "${HEAVY_SLOT:-}" ] || return 0
  local lock0="${CI_HEAVY_LOCK:-/cache/heavy.lock}" dir="${CI_HEAVY_SLOT_DIR:-/cache/multicore}" n i f
  [ -d "$(dirname "$lock0")" ] && [ -w "$(dirname "$lock0")" ] && command -v flock >/dev/null || return 0
  n="${CI_HEAVY_SLOTS:-2}"
  mkdir -p "$dir" 2>/dev/null || n=1
  i=0
  while [ "$i" -lt "$n" ]; do
    if [ "$i" -eq 0 ]; then f="$lock0"; else f="$dir/heavy.slot$i"; fi
    exec 9>"$f"
    if flock -n 9; then HEAVY_SLOT="$i"; echo "heavy lock held: slot $i of $n ($f)"; return 0; fi
    exec 9>&-
    i=$((i + 1))
  done
  return 1
}

# cache_gc_if_free: the janitor under a heavy slot when one is free; skipped, not waited for, when every slot is busy.
cache_gc_if_free() {
  if heavy_trylock; then
    "$ROOT/ci/cache_gc.sh" || echo "cache_gc failed (the build is fine)"
  else
    echo "cache_gc skipped: every heavy slot is busy (a Vivado run elsewhere); a later run cleans up"
  fi
}

# build_update_host <manifest.json> <null_full.bit> <hostapp.app> <outdir>: the MultiCore host update
# file (trx64host_update_*.cfw/.ue2) from the same package; its bitstream is the host with the null core (T123). The application it installs
# is our own boot app from stage trxapps, so the commit it records is the manifest's trxapps_commit.
# Before this existed the file was built on a Mac in the u64-build-env image and a release could not be
# made without it (Alex, 2026-10-15: build it with the pipeline).
build_update_host() {
  local manifest="$1" bit="$2" app="$3" out="$4"
  if [ -f /opt/esp/esp-idf/export.sh ]; then
    set +u; . /opt/esp/esp-idf/export.sh >/dev/null; set -u
  fi
  local id bitc appc
  id="$(branch_slug)-$(git -C "$ROOT" rev-parse --short=8 HEAD)"
  # Eight digits, not the whole sha: the manifest record keeps 16 bytes NUL-terminated for each
  # (tools/trx_pkg.py:40-41), and a 40-character sha is refused there - which is what run 51 showed.
  bitc="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["trx64_commit"][:8])' "$manifest")"
  appc="$(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print((m.get("trxapps_commit") or m["trx64_commit"])[:8])' "$manifest")"
  "$ROOT/tools/make_update.sh" --native --id "$id" --bit-commit "$bitc" --app-commit "$appc" \
      --out "$out" "$bit" "$app"
}

# T174 D2.8: the emulator the app suites run in (tests/coreapp/emu/run.sh, tests/hostapp/emu_host.sh). A Linux image whose entrypoint is `ue2emu`; the
# tag is the pinned emulator release (u64-emulator v0.7.0 = 4612884e7a63, S44). This is the ONE place that names it (UE2EMU_IMAGE in the environment
# overrides); ci/ue2emu.sh runs it. The CI jobs have no docker: they run in u64-build-env:emu-$UE2EMU_TAG (ci/emu/build.sh), which has ue2emu on PATH.
UE2EMU_TAG="v0.7.0"
UE2EMU_IMAGE="${UE2EMU_IMAGE:-ue2emu:$UE2EMU_TAG}"
# The kit the emulator needs and the repository must not hold (ROMs, the flash image, the VIC-20 profile, a DFX .cor): a directory, never in git.
# tools/ue2emu_kit.sh makes one. The builder's is in the CI cache.
ue2emu_kit() {
  local k
  for k in "${UE2EMU_KIT:-}" /cache/ue2emu-kit /srv/trx64-build/cache/ue2emu-kit "$HOME/scratch/ue2emu-kit"; do
    [ -n "$k" ] && [ -f "$k/flash.bin" ] && [ -d "$k/roms" ] && { printf '%s\n' "$k"; return 0; }
  done
  return 1
}
