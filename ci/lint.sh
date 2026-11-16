#!/usr/bin/env bash
# Stage `lint`: cheap checks that fail before a bench or Vivado runs, in
# trx64-sim-env. Every check runs; the stage fails if one did.
#
#   ci/lint.sh
#
#  - shell: bash -n and shellcheck (errors only) over ci/, tools/, vivado/ and the benches
#  - python: byte-compile and ruff's syntax/undefined-name rules (E9, F63, F7, F82)
#  - the RTL (designs/*/*.v): syntax only (yosys -defer), no elaboration; the VHDL and the
#    benches are analysed by the benches of stage sims
#  - the workflows: parse, every `needs:` names a job, every `uses:` a file
#  - ci/select.sh: its answers for the cases the workflows rely on
#  - T87: the pipeline's queue job (wait_turn), host/layers.txt, the layer cache keys,
#    the host_sha rule of the core entries and the cache janitor, on generated data
#  - ci/sims.txt: every bench's script exists
#  - ci/apptests.sh and tests/parallel.sh: key, marker and parallel-job scenarios (the app host tests themselves run in stage render, T174 D1)
#  - the XDC files: no Tcl control command, which Vivado would silently drop (T95 5.15)
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$ROOT"

# hand-over directories of runs that never reached their end (failed stages)
[ ! -d /cache/runs ] || find /cache/runs -mindepth 1 -maxdepth 1 -mtime +2 -exec rm -rf {} + || true

fails=()
check() { # check <name> <command...>
  local name="$1"; shift
  if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fails+=("$name"); fi
}
check "bash -n" bash -c 'for f in $(git ls-files -- "ci/*.sh" "tools/*.sh" "vivado/*.sh" "designs/*/sim/*/*.sh"); do bash -n "$f" || exit 1; done'
check "shellcheck" bash -c 'git ls-files -- "ci/*.sh" "tools/*.sh" "vivado/*.sh" "designs/*/sim/*/*.sh" | xargs shellcheck -S error'

check "package names (T171)" python3 ci/check_pkg.py
check "package names selftest" python3 ci/check_pkg.py --selftest

check "python compile" bash -c 'git ls-files -- "*.py" | xargs python3 -m py_compile'
check "ruff" bash -c 'git ls-files -- "*.py" | xargs ruff check --no-cache --select E9,F63,F7,F82'

check "core descriptors" python3 tools/core_toml.py --check
check "issue forms (T161)" python3 tools/check_issue_forms.py --selftest
if command -v patch >/dev/null; then check "atari800 gen" sh tools/atari800_gen.sh --check; fi
check "nes gen" python3 tools/nes_gen.py --check
check "zx gen" sh tools/zx_gen.sh --check
check "zxnext gen" python3 tools/zxnext_gen.py --check
check "zxnext tree: pinned upstream, no ROM" bash -c 'python3 tools/check_zxnext_tree.py selftest && python3 tools/check_zxnext_tree.py'
check "zxnext programs" python3 tools/zxnext_progs.py check
check "snes gen" python3 tools/snes_gen.py --check
check "c64scpu cpu copy (T182)" python3 tools/scpu_vendor816.py --check
check "c64scpu memory configuration table (T182 3c)" python3 tools/scpu_cfgtab.py --check
check "snes tree: pinned upstream, no ROM, no Altera wrapper" bash -c 'python3 tools/check_snes_tree.py selftest && python3 tools/check_snes_tree.py'
check "neogeo tree: pinned upstream, no ROM" bash -c 'python3 tools/check_neogeo_tree.py selftest && python3 tools/check_neogeo_tree.py'
check "neogeo fix: the Vivado copies are what the edits make of rtl/" python3 tools/neogeo_fix.py --check
check "zx keys" python3 tools/zxcpc_keys.py --check
check "zx programs" python3 tools/zx_progs.py --check
check "zx snapshots" python3 tools/zx_snap.py selftest
check "zx/cpc: no ROM, media or snapshot" bash -c 'python3 tools/check_no_rom.py selftest && python3 tools/check_no_rom.py'
check "zx/cpc/vc20/c264: core id = descriptor id" bash -c 'python3 tools/check_core_id.py selftest && python3 tools/check_core_id.py'
check "cpc gen" python3 tools/cpc_gen.py --check
check "cpc programs" python3 tools/cpc_progs.py --check
check "cpc slot model" python3 tools/cpc_slots.py selftest
check "zx/cpc shared modules once" bash -c 'python3 tools/check_zxcpc_shared.py selftest && python3 tools/check_zxcpc_shared.py'
check "layer-2 contract" python3 tools/contract_ports.py --check
# Alex, 2026-11-08 (T115 section 5): the old DFX trx64 core stays deleted; the selftest plants each fault
check "no old core (trx64 directory and paths)" python3 tools/check_no_old_core.py
check "no old core: selftest" python3 tools/check_no_old_core.py --selftest
# T107 D7 / T164 WP1: the HDMI mode tables (A1200 and host editions) against every limit, the committed .vh files, and one negative control per rule
check "hdmi modes (A1200 + host)" bash -c 'python3 tools/hdmi_modes.py check && python3 tools/hdmi_modes.py selftest >/dev/null'
# T168 S9: DDT's ported files are byte-identical to the pinned upstream or have their .diff, NOTICE names licence / repository / commit, no character ROM, the palette is the tool's
check "ddt cores: tree marking, no ROM, palette" bash -c 'python3 tools/check_ddt_tree.py selftest && python3 tools/check_ddt_tree.py'
# T89 S1: no Apple ROM bytes (system, character, Disk II boot ROM) anywhere in the tree; the selftest plants every encoding first
check "no Apple ROM content" bash -c 'tools/check_no_roms.sh --selftest && tools/check_no_roms.sh'
check "cor_pack test" python3 tools/test_cor_pack.py
# T104.2: the pinned TRX64 bitstream is packed with the generic application, any other file is refused (the control must fail)
check "import .cor pack test" bash -c 'tools/test_pack_import_cor.sh && ! tools/test_pack_import_cor.sh --control >/dev/null 2>&1'
check "descrec test" python3 tools/test_descrec.py
# T104.2 D10: the menu record the application carries is what menu.toml compiles to (and every name in it is one the application exports); its areas and tabs are the concept's
check "TRX64U menu record" python3 tools/gen_menu_rec.py --check
# T192 D23 (Alex, 2026-11-09: "Until the UI, including the UX, works completely as an API client, it is a botch"): the host's screen changes no state outside host_core, acts on
# no key itself (the key map is one table), carries no hand-written legend; what is not cut yet is the ledger (tools/host_cut_ledger.txt) and only shrinks.
check "host screen is a client of host_core (T192 D23)" bash -c 'python3 -I tools/check_host_cut.py --selftest >/dev/null && python3 -I tools/check_host_cut.py'
# T192.2 P5 (S11, S12): the parity compare of two render outputs reports a pixel, a word of a dump, a missing and an extra file, and passes on two equal copies
check "render compare (T192.2 parity)" python3 -I tools/render_compare.py --selftest
check "TRX64U menu against the UX concept" bash -c 'python3 tools/check_menu_concept.py --selftest && python3 tools/check_menu_concept.py tests/coreapp/golden/menu_c64.txt'
# T192.3 S1, S6 (D23): the core menu controller holds navigation, dialogs and rows and nothing of the domain; no raw key outside its three input handlers. The selftest plants every fault.
check "menu controller gate (T192.3 S1, S6)" python3 tools/check_menuctl_gate.py
check "menu controller gate: selftest" python3 tools/check_menuctl_gate.py --selftest
# T192.3 (Alex, 2026-11-09): one state and one reader of it, the UI service's; red if the v1 state (uistate, `?v=1`, `seq`) or a second reader of the screen returns
check "no v1 UI state, no second reader (T192.3)" python3 tools/check_no_v1_ui.py
check "no v1 UI state: selftest" python3 tools/check_no_v1_ui.py --selftest
# T211 S1 (D1): the host app names its keys itself; no Gideon page/help/tasks code treated as an F key
check "host key names (T211 S1)" bash -c 'python3 -I tools/check_host_keys.py --selftest && python3 -I tools/check_host_keys.py'
check "u2a_pack test" python3 tools/test_u2a_pack.py
check "release checks test" python3 tools/test_release_check.py
check "release bundle test" python3 tools/test_make_release_mc.py
# T148 D8, S5: a package holds .cor files, never a core folder; ROMS.txt comes from the descriptors' ROM table and the manual
check "package layout: no core folder (selftest)" python3 tools/check_package_layout.py selftest
check "ROMS.txt test" python3 tools/test_roms_txt.py
# T100 D12: the host tests of the applications (tests/{hostapp,coreapp,loader}/run.sh) are in stage trxapps (ci/apptests.sh), cached by key; here only
# their key and parallel machinery is checked, without a compiler
check "apptests keys, markers, parallel jobs" ci/test_apptests.sh
# T100 D14: the test cache the app tests and the render benches run on - scenarios d/a/b/c/e/f/g on a throw-away project and the four controls of the cache itself
check "test cache units (D14)" tests/ctlcache_selftest.sh
# T174 D3: the harness is in a unit's key by CC_KEY_VERSION; a commit that changes its code must raise it
check "test cache key version (T174 D3)" python3 tools/check_cc_key_version.py
# T174 D5 (Alex, 2026-11-08, "Segment the caches better"): a bench with a record in ci/sims.reads is keyed by it alone, no directory macro beside it
check "sims reads: no coarse key beside a record (selftest)" python3 -I tools/check_sims_reads.py --selftest
check "sims reads: records and sims.deps agree" python3 -I tools/check_sims_reads.py
# T174 D5.4 (Alex, 2026-11-08: "The negative controls run in exactly 2 places: flash operations, persistence operations. Nowhere else. Never.")
check "negative controls only in flash and persistence tests (selftest)" python3 -I tools/check_controls.py --selftest
check "negative controls only in flash and persistence tests" python3 -I tools/check_controls.py


check "verilog syntax" bash -c '
  rc=0
  for f in $(git ls-files | grep -E "^designs/[^/]+/[^/]+\.v$"); do
    yosys -q -p "read_verilog -sv -defer -I vendor/icepi -I vendor/icepi/vicii-kawari/hdl $f" >/tmp/lint.log 2>&1 || { echo "$f"; tail -5 /tmp/lint.log; rc=1; }
  done; exit $rc'

# T95 5.15: Vivado's XDC parser silently drops every Tcl control command ("Command 'if' is not supported in the xdc
# constraint file" is a CRITICAL WARNING, not an error), so an .xdc that decides anything applies nothing and the run looks
# normal. Constraints that need Tcl belong in a .tcl sourced after synth_design (cores/nes/nes_timing.tcl). The control: the
# same scanner over a file that holds an `if` has to say so.
check "xdc without Tcl" bash -c '
  scan() { grep -lE "^[[:space:]]*(if|foreach|while|for|proc|puts|catch)[[:space:]{]|\[concat " "$@" 2>/dev/null; }
  bad=$(scan $(git ls-files -- "*.xdc"))
  [ -z "$bad" ] || { echo "Tcl commands in an XDC file (Vivado drops them): $bad"; exit 1; }
  t=$(mktemp -d); trap "rm -rf $t" EXIT
  printf "if {1} { set_false_path -from [get_cells x] }\n" > "$t/control.xdc"
  [ -n "$(scan "$t/control.xdc")" ] || { echo "the control passed: the scanner cannot see an if in an XDC file"; exit 1; }'
check "wait_turn" ci/wait_turn.sh --selftest
check "layers_check selftest" ci/layers_check.sh --selftest
check "layers.txt" ci/layers_check.sh
check "host pin (T94 D13)" tools/host_pin.sh --check
# T164 3a Q1 (Alex, 2026-11-01, "Copies instead."): a large core builds from its own copy of the host files (cores/<id>/mono/host, FROM names the commit); the copies need not equal host/, so
# host_pin stays about host/ only. Here: the copy is documented, the build names no live host/ file, the descriptor's sources cover the copy. The selftest plants each fault.
check "large-core host copies (T164)" bash -c 'python3 tools/check_large_copies.py selftest && python3 tools/check_large_copies.py'
check "layer keys" ci/test_layers.sh
check "cache entries" ci/test_cache.sh
check "bundle path (T172: version stamp, .txu kept)" ci/test_bundle.sh
check "updater centring measure (T132, tests/updater/screen_centre.py)" python3 -I tests/updater/screen_centre.py selftest
check "cache gc" ci/test_cache_gc.sh
check "layered stage flow (fake vivado)" ci/test_layers_flow.sh
# T100 D9 (build by change): the choice of the diff base, and the scenarios D9-1..D9-6 on a scratch clone, each with a control that must fail
check "plan base selftest" ci/plan.sh --selftest
check "plan scenarios D9-1..6" ci/test_plan.sh
check "large stage flow (fake vivado)" ci/test_large_flow.sh
check "constraint fail-fast in the host builds" ci/test_constraint_check.sh
check "treesync: persistent build tree touches only what changed (T174)" ci/test_treesync.sh
# T174 D4: the parent dispatches the children of ci/children.txt; every child workflow takes sha/parent/target/plan and checks out inputs.sha, the parent
# is one job on the NAS pool, nothing defers red; every bench has one owner (ci/sims.lane). The test runs the parent against a fake Gitea, with controls.
check "children table and workflows (D4)" python3 ci/orchestrate.py check
check "parent orchestration (D4, fake Gitea)" ci/test_orchestrate.sh
check "select.sh" bash -c '
  t() { [ "$(env -i HOME="$HOME" PATH="$PATH" "${@:2}" ci/select.sh $1)" = "$EXPECT" ]; }
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=push &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_dispatch UE2_INPUT=false &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_dispatch UE2_INPUT=true &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_call UE2_INPUT=true &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_call UE2_INPUT= &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=v1.0 GITHUB_REF_TYPE=tag GITHUB_EVENT_NAME=push &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=release/1 GITHUB_EVENT_NAME=push &&
  EXPECT=yes t update GITHUB_REF_NAME=integ-m1 &&
  EXPECT=yes t update GITHUB_REF_NAME=main &&
  EXPECT=yes t update GITHUB_REF_NAME=v1.0 GITHUB_REF_TYPE=tag &&
  EXPECT=no  t update GITHUB_REF_NAME=t40 SELECT_MSG=x &&
  EXPECT=yes t update GITHUB_REF_NAME=t40 UPDATE_FORCE=true &&
  EXPECT=yes t update GITHUB_REF_NAME=t40 BUNDLE_INPUT=true SELECT_MSG=x &&
  EXPECT=yes t update GITHUB_REF_NAME=t40 "SELECT_MSG=T172 test [bundle]" &&
  EXPECT=no  t update GITHUB_REF_NAME=t40 BUNDLE_INPUT=false SELECT_MSG=bundle &&
  EXPECT=no  t update GITHUB_REF_NAME=t40 SELECT_MSG=x &&
  EXPECT=0   t sims-tier GITHUB_REF_NAME=t40 GITHUB_EVENT_NAME=push SELECT_MSG=x &&
  EXPECT=2   t sims-tier GITHUB_REF_NAME=t40 GITHUB_EVENT_NAME=push "SELECT_MSG=x [sims]" &&
  EXPECT=0   t sims-tier GITHUB_REF_NAME=main GITHUB_EVENT_NAME=push SELECT_MSG=x &&
  EXPECT=2   t sims-tier GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_dispatch SIMS_INPUT=true SELECT_MSG=x &&
  EXPECT=2   t sims-tier GITHUB_REF_NAME=integ-m1 GITHUB_EVENT_NAME=push'
# T172 D4/D5: VERSION is x.y.z (format only; the bump rules are a guide below 1.0.0)
check "VERSION" bash -c 'grep -Eqx "(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)" VERSION'
check "workflow" python3 - <<'PY'
import glob, os, sys, yaml
ok = True
for p in glob.glob(".gitea/workflows/*.yml"):
    jobs = yaml.safe_load(open(p))["jobs"]
    for name, j in jobs.items():
        needs = j.get("needs", [])
        for n in [needs] if isinstance(needs, str) else needs:
            if n not in jobs:
                print(f"{p}: job {name} needs {n}, which does not exist"); ok = False
        if "uses" in j and not os.path.exists(j["uses"]):
            print(f"{p}: job {name} uses {j['uses']}, which does not exist"); ok = False
sys.exit(0 if ok else 1)
PY
# T109 D4: every simulation script is either registered in ci/sims.txt or excluded on purpose in
# ci/sims_excluded.txt with a reason. The structural fix for a structural defect: amiga_audio and the
# Atari machine bench both existed and neither ran, so nothing said the sound had gone.
check "every bench registered" python3 - <<'PYX'
import subprocess, sys
scripts = subprocess.run(["git", "ls-files", "cores/*/sim/*/run.sh", "cores/*/sim/run_*.sh"],
                         capture_output=True, text=True).stdout.split()
listed = open("ci/sims.txt").read()
excluded = {}
for line in open("ci/sims_excluded.txt"):
    line = line.strip()
    if line and not line.startswith("#"):
        path, _, why = line.partition("|")
        excluded[path] = why
ok = True
for s in scripts:
    if s in listed:
        continue
    if s not in excluded:
        print(s + ": in neither ci/sims.txt nor ci/sims_excluded.txt - register it or say why not")
        ok = False
    elif not excluded[s].strip():
        print(s + ": excluded without a reason")
        ok = False
for path in excluded:
    if path not in scripts:
        print("ci/sims_excluded.txt: " + path + " does not exist")
        ok = False
sys.exit(0 if ok else 1)
PYX

check "sims.txt" python3 - <<'PY'
import os, re, sys
ok = True
names = set()
for n, line in enumerate(open("ci/sims.txt"), 1):
    if not line.strip() or line.startswith("#"):
        continue
    f = line.rstrip("\n").split("|", 3)
    if len(f) != 4 or f[0] not in "123" or not f[2].isdigit():
        print(f"ci/sims.txt:{n}: want tier|name|timeout|command"); ok = False; continue
    if f[1] in names:
        print(f"ci/sims.txt:{n}: {f[1]} twice"); ok = False
    names.add(f[1])
    for path in re.findall(r"designs/[\w./-]+", f[3]):
        if not os.path.exists(path):
            print(f"ci/sims.txt:{n}: {path} does not exist"); ok = False
sys.exit(0 if ok else 1)
PY

if [ ${#fails[@]} -gt 0 ]; then echo "FAILED: ${fails[*]}"; exit 1; fi
echo "=== lint done in $(lap)"
