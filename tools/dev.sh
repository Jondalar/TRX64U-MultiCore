#!/usr/bin/env bash
# T174 D1: ONE entry point of the dev loop, for people and agents. Run it on the Mac, in the checkout you work in; it executes on the builder
# (ssh builder), in the CI's build image, in a persistent build tree per branch, so `make` rebuilds only what changed and the test caches are CI's.
#
#   tools/dev.sh build     [--branch B] [--clean]   compile and link both applications (hostapp, coreapp), incremental; no images
#   tools/dev.sh test      [--branch B]             the host tests of the applications: `ci/apptests.sh --tier fast` (the unit tests of what changed)
#   tools/dev.sh riscv     [--branch B] [--clean]   the build stage of the pipeline (ci/trxapps.sh): the RISC-V images, size check, testlib/unzip;
#                                                   the images come back to .ci/dev/<branch>/
#   tools/dev.sh emu       [--branch B] [--clean]   the emulator script of the host app when this branch touches it (T174 D2.2/D2.8), ON THE BUILDER: the images are built
#                                                   first (ue2emu runs the RISC-V image itself), then ue2emu runs in the Linux image UE2EMU_IMAGE (ci/lib.sh) through
#                                                   ci/ue2emu.sh, with the kit of the CI cache (ROMs, flash image, profile, a vc20.cor; tools/ue2emu_kit.sh): the
#                                                   host app's flow tests/hostapp/emu_host.sh (host > vc20.cor > core menu > back; PNGs come back to
#                                                   .ci/dev/<branch>/emu/). The Mac's own tests/ui (the profile cores) runs for the core app when a
#                                                   local ue2emu exists (UE2EMU). No kit or no image on the builder: said out loud, not run.
#   tools/dev.sh cor CORE  [--branch B]             ONE .cor (T174 D2.7): the images of this tree, then ci/pack_one.sh with the bitstream the pipeline's cache
#                                                   holds for this tree's gateware; kept in ~/scratch/cor/<branch>/ on the builder and copied to
#                                                   .ci/dev/<branch>/CORE.cor. No bitstream for the tree's gateware yet: it says which key is missing.
#   tools/dev.sh integrate [--branch B] [--clean]   everything before a merge, in the order of the principles (D2.2/D2.5): the native tests first (ci/render.sh -f
#                                                   with every negative control: CONTROLS_ALL=1, host tests, render benches, file benches), then the RISC-V
#                                                   images, then the emulator scripts on the Mac (as `emu`)
#
# The working tree is what counts, committed or not: tools/treesync.py copies the files whose content changed to ~/dev-trees/<branch> on the
# builder (build outputs there stay), --clean empties it first (ccache is kept). --branch B only names the tree (default: the current branch), so two
# checkouts or agents never share one. Every container run goes through ~/scratch/appslot.sh of the builder: three app runs at a time, never beside Vivado
# (it waits and says so); without that script the run goes on at once and says that too. CCACHE_DISABLE=1 tools/dev.sh ... measures without ccache.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mode="${1:-}"; [ $# -eq 0 ] || shift
branch="" clean="" core=""
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) branch="${2:?--branch needs a name}"; shift 2 ;;
    --clean) clean=1; shift ;;
    -*) echo "tools/dev.sh: unknown argument $1" >&2; exit 2 ;;
    *) if [ "$mode" = cor ] && [ -z "$core" ]; then core="$1"; shift; else echo "tools/dev.sh: unknown argument $1" >&2; exit 2; fi ;;
  esac
done
case "$mode" in build|test|riscv|emu|cor|integrate) ;; *) sed -n 2,30p "$0" | sed 's/^# \{0,1\}//'; exit 2 ;; esac
[ "$mode" != cor ] || [ -n "$core" ] || { echo "tools/dev.sh cor: which core? (tools/core_toml.py list-cor)" >&2; exit 2; }
[ -n "$branch" ] || branch="$(git rev-parse --abbrev-ref HEAD)"
slug="$(printf '%s' "$branch" | tr '/ ' '__')"
host="${DEV_HOST:-builder}"       # DEV_HOST=admin@192.0.2.2 DEV_CPUS=3 DEV_TREES=/srv/dev-trees: the NAS (at most 3 cores) while Vivado holds the builder (Alex, 2026-11-08)
tree="${DEV_TREES:-dev-trees}/$slug"   # relative to the builder's home; the NAS needs DEV_TREES on a data share (its home is a 400 MB RAM disk)
t0=$SECONDS
lap() { printf '=== dev.sh %s: %s took %ds\n' "$mode" "$1" $((SECONDS - t0)); t0=$SECONDS; }

python3 -I tools/treesync.py "$ROOT" "$tree" --host "$host" ${clean:+--clean}
lap "tree sync"
abs="$(ssh "$host" "cd $tree && pwd")"
# the files this branch changed against integration (merge-base, D48), plus untracked ones: the base of "changed" in the builder's tree (img_setup)
mbase0="$(git merge-base HEAD origin/integration 2>/dev/null || git merge-base HEAD integration 2>/dev/null || true)"
{ [ -z "$mbase0" ] || git diff --name-only "$mbase0"; git ls-files -o --exclude-standard; } | sort -u | ssh "$host" "cat > $tree/.dev-changed"

# what runs in the container (bash -c); the tree is /work, the CI cache is /cache, ccache is /ccache
img_setup='set -uo pipefail
trap "chown -R $HOST_UID:$HOST_GID /work" EXIT
git config --global --add safe.directory /work
[ -e .git ] || git init -q
git add -A >/dev/null 2>&1 || true   # ci/render.sh keys over git ls-files: the dev tree needs an index
# the tree has no commit (a commit would change the version stamp every run), so "changed" needs a base TREE: the index without the files this
# branch changed against integration (.dev-changed, written by dev.sh), so `git diff BASE` and the plan name exactly those files (ci/apptests.sh --base)
rm -f .dev-base
if [ -s .dev-changed ]; then
  tr "\\n" "\\0" < .dev-changed | xargs -0 git rm --cached -q --ignore-unmatch -- >/dev/null 2>&1 || true
  git write-tree > .dev-base; git add -A >/dev/null 2>&1 || true
fi
export CI_HANDOFF=/work/.dev-out TRX_BUILD_INPLACE=1 CI_BUILD_JOBS="${CI_BUILD_JOBS:-6}"
'
case "$mode" in
  build)  inner='ci/trxapps.sh --compile-only'"${clean:+ --clean}" ;;
  test)   inner='b=""; [ ! -s .dev-base ] || b="--base $(cat .dev-base)"; ci/apptests.sh --tier fast $b' ;;
  riscv|emu) inner='ci/trxapps.sh' ;;
  cor)    inner="ci/trxapps.sh && ci/pack_one.sh $core" ;;
  integrate) inner='CONTROLS_ALL=1 CI_RENDER_OK=/work/.dev-out/render-ok ci/render.sh -f && ci/trxapps.sh' ;;
esac
# --clean of the tree was done by the sync; ci/trxapps.sh has nothing of its own to clean in place
run="$(cat <<EOF
#!/bin/bash
export PATH="\$PATH:/srv/.qpkg/container-station/bin"   # the NAS (DEV_HOST) has docker there
cd "$abs" || exit 1
docker run --rm --init --cpus=${DEV_CPUS:-6} ${CCACHE_DISABLE:+-e CCACHE_DISABLE=1} ${UPDATE_GOLDEN:+-e UPDATE_GOLDEN=1} ${CI_APPTESTS_FORCE:+-e CI_APPTESTS_FORCE=1} -e SDK_COMMIT="$(git rev-parse --short=8 HEAD)" -e JOBS="${JOBS:-$(( ${DEV_CPUS:-6} - 1 ))}" -e CI_BUILD_JOBS="${DEV_CPUS:-6}" -e HOST_UID=\$(id -u) -e HOST_GID=\$(id -g) \\
  -v "$abs":/work -w /work -v /srv/trx64-build/cache:/cache -v /srv/containers/1541ultimate-ci/ccache:/ccache \\
  u64-build-env:ci bash -c '$img_setup
if [ -f /opt/esp/esp-idf/export.sh ]; then set +u; . /opt/esp/esp-idf/export.sh >/dev/null; set -u; fi
$inner'
EOF
)"
printf '%s\n' "$run" | ssh "$host" "cat > $tree/.dev-run.sh"
slot='~/scratch/appslot.sh'
if ssh "$host" "[ -x $slot ]"; then
  echo "=== dev.sh $mode: waiting for an app slot (3 at a time, never beside Vivado) ..."
  ssh "$host" "$slot bash $tree/.dev-run.sh" && rc=0 || rc=$?
else
  echo "=== dev.sh $mode: WARNING ~/scratch/appslot.sh is missing on $host: no slot limit, no Vivado check" >&2
  ssh "$host" "bash $tree/.dev-run.sh" && rc=0 || rc=$?
fi
lap "$mode on $host (rc=$rc)"
[ "$rc" -eq 0 ] || { echo "=== dev.sh $mode: FAILED (rc=$rc)" >&2; exit "$rc"; }

case "$mode" in riscv|emu|cor|integrate)
  out=".ci/dev/$slug"; mkdir -p "$out"
  for f in hostapp.app hostapp.elf coreapp.app coreapp.bin coreapp.elf testlib.u2a unzip.u2a; do
    scp -q "$host:$tree/.dev-out/trxapps/$f" "$out/" 2>/dev/null || echo "(no $f in the result)"
  done
  rm -rf "$out"/trx64-sdk-*   # the SDK package of the build (tools/sdk_pack.sh): the tarball, unpacked beside it
  if scp -q "$host:$tree/.dev-out/trxapps/trx64-sdk-*.tar.gz" "$out/" 2>/dev/null; then tar -xzf "$out"/trx64-sdk-*.tar.gz -C "$out" && chmod -R a+rX "$out"/trx64-sdk-*; else echo "(no SDK package in the result)"; fi
  ls -l "$out"; shasum -a 256 "$out"/hostapp.app "$out"/coreapp.bin 2>/dev/null || sha256sum "$out"/hostapp.app "$out"/coreapp.bin
  lap "images to $out" ;;
esac

if [ "$mode" = cor ]; then
  ssh "$host" "mkdir -p ~/scratch/cor/$slug && cp $tree/.dev-out/cor/$core.cor ~/scratch/cor/$slug/" && echo "(on $host: ~/scratch/cor/$slug/$core.cor)"
  scp -q "$host:$tree/.dev-out/cor/$core.cor" "$out/" && ls -l "$out/$core.cor" && shasum -a 256 "$out/$core.cor"
  lap "$core.cor to $out"
fi

if [ "$mode" = emu ] || [ "$mode" = integrate ]; then
  # which app(s) this branch touches: the diff against integration plus untracked files (the working tree is what counts)
  mbase="$(git merge-base HEAD origin/integration 2>/dev/null || git merge-base HEAD integration 2>/dev/null || true)"
  files="$( { [ -z "$mbase" ] || git diff --name-only "$mbase"; git ls-files -o --exclude-standard; } | sort -u)"
  core_touched="" host_touched=""
  printf '%s\n' "$files" | grep -qE '^(coreapp/|app/|apps/|vendor/gideon/(software|tools|target)/|tests/(ui|coreapp)/)' && core_touched=1
  printf '%s\n' "$files" | grep -qE '^(host-app/|app/|vendor/gideon/(software|tools|target)/|tests/hostapp/)' && host_touched=1
  [ -n "$mbase" ] || { echo "=== dev.sh $mode: no merge-base with integration found: both apps count as touched" >&2; core_touched=1 host_touched=1; }
  # the emulator scripts run on the builder, outside the build container (the emulator is a container of its own: ci/ue2emu.sh)
  if [ -z "$host_touched" ]; then
    echo "=== dev.sh $mode: the host app is not touched by this branch: no emulator script was run"
  else
    {
      printf '#!/bin/bash\ncd "%s" || exit 1\nrc=0\nexport EMU_OUT="%s/.dev-out/emu"\n' "$abs" "$abs"
      [ -z "$host_touched" ] || printf 'tests/hostapp/emu_host.sh "%s/.dev-out/trxapps" "%s/.dev-out/emu/host" || { [ $? = 77 ] || rc=1; }\n' "$abs" "$abs"
      printf 'exit $rc\n'
    } | ssh "$host" "cat > $tree/.dev-emu.sh"
    echo "=== dev.sh $mode: emulator scripts on $host (ue2emu in the container image $(grep -o 'UE2EMU_IMAGE:-[^}]*' ci/lib.sh | head -1 | cut -d- -f2))"
    if ssh "$host" "[ -x $slot ]"; then ssh "$host" "$slot bash $tree/.dev-emu.sh" && erc=0 || erc=$?
    else ssh "$host" "bash $tree/.dev-emu.sh" && erc=0 || erc=$?; fi
    mkdir -p "$out/emu"; scp -q "$host:$abs/.dev-out/emu/host/*.png" "$out/emu/" 2>/dev/null && ls "$out/emu"
    lap "emulator scripts on $host (rc=$erc)"
    [ "$erc" -eq 0 ] || { echo "=== dev.sh $mode: emulator scripts FAILED (rc=$erc)" >&2; exit "$erc"; }
  fi
  emu="${UE2EMU:-$HOME/Development/u64-emulator/target/release/ue2emu}"
  if [ -n "$core_touched" ] && [ -x "$emu" ] && [ -f "$out/coreapp.app" ]; then
    echo "=== dev.sh $mode: tests/ui on the Mac (local ue2emu) with $out/coreapp.app"
    COREAPP="$ROOT/$out/coreapp.app" nice -n 15 tests/ui/run.sh
    lap "tests/ui"
  fi
fi
echo "=== dev.sh $mode: ok"
