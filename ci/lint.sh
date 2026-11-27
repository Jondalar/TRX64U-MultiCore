#!/usr/bin/env bash
# Stage `lint`: cheap checks that fail before a bench or Vivado runs, in
# trx64-sim-env. Every check runs; the stage fails if one did.
#
#   ci/lint.sh                          every check (by hand; the matrix runs the groups)
#   ci/lint.sh tree|sdk|host|rtlchk|core:<id>  the checks of one lane of the matrix (T239.5 D7): tree = the tree checks of every push (public tree, no ROMs,
#                                       TODO and specs, versions), sdk = the SDK's lint (its tools, the API document, D24, the shared TOML B), host = the
#                                       host menu's, core:<id> = a core's (core.toml, the core id, the contract ports; the TOML A against the RTL is step 4 of the core lane, T264)
#
#  - shell: bash -n and shellcheck (errors only) over ci/, tools/, vivado/ and the benches
#  - python: byte-compile and ruff's syntax/undefined-name rules (E9, F63, F7, F82)
#  - the RTL (designs/*/*.v): syntax only (yosys -defer), no elaboration; the VHDL and the
#    benches are analysed by the benches of stage sims
#  - the workflows: parse, every `needs:` names a job, every `uses:` a file; the children table against matrix.yml, pipeline.yml only calling the matrix (ci/orchestrate.py check)
#  - ci/select.sh: its answers for the cases the workflows rely on
#  - host/layers.txt, the layer cache keys, the host_sha rule of the core entries and the cache janitor, on generated data
#  - ci/sims.txt: every bench's script exists
#  - ci/apptests.sh and tests/parallel.sh: key, marker and parallel-job scenarios (the app host tests themselves run in stage render, T174 D1)
#  - the XDC files: no Tcl control command, which Vivado would silently drop (T95 5.15)
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$ROOT"

# hand-over directories of runs that never reached their end (failed stages)
[ ! -d /cache/runs ] || find /cache/runs -mindepth 1 -maxdepth 1 -mtime +2 -exec rm -rf {} + || true

LINT_GROUP="${1:-all}"
case "$LINT_GROUP" in all|tree|sdk|host|rtlchk|core:?*) ;; *) echo "ci/lint.sh: all, tree, sdk, host, rtlchk or core:<id>, not $LINT_GROUP" >&2; exit 2 ;; esac
fails=()
check() { # check <name> <command...>   a check of the full lint and of the lane tree (T239.5: the tree checks of every push are every check no other lane owns)
  case "$LINT_GROUP" in all|tree) ;; *) return 0 ;; esac
  run_check "$@"
}
gcheck() { # gcheck <group> <name> <command...>   a check of the full lint and of the lane <group> (tree, sdk, host, core)
  local g="$1"; shift
  case "$LINT_GROUP" in all|"$g"|"$g":*) run_check "$@" ;; esac
}
run_check() {
  local name="$1"; shift
  if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fails+=("$name"); fi
}
check "bash -n" bash -c 'for f in $(git ls-files -- "ci/*.sh" "tools/*.sh" "vivado/*.sh" "designs/*/sim/*/*.sh"); do bash -n "$f" || exit 1; done'
check "shellcheck" bash -c 'git ls-files -- "ci/*.sh" "tools/*.sh" "vivado/*.sh" "designs/*/sim/*/*.sh" | xargs shellcheck -S error'

check "package names (T171)" python3 ci/check_pkg.py
check "package names selftest" python3 ci/check_pkg.py --selftest

check "python compile" bash -c 'git ls-files -- "*.py" | xargs python3 -m py_compile'
check "ruff" bash -c 'git ls-files -- "*.py" | xargs ruff check --no-cache --select E9,F63,F7,F82'

gcheck core "core descriptors" python3 tools/core_toml.py --check
gcheck core "diag pin table (T260)" python3 tools/diag_pins_gen.py --check
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
gcheck tree "zx/cpc: no ROM, media or snapshot" bash -c 'python3 tools/check_no_rom.py selftest && python3 tools/check_no_rom.py'
gcheck core "zx/cpc/vc20/c264: core id = descriptor id" bash -c 'python3 tools/check_core_id.py selftest && python3 tools/check_core_id.py'
check "cpc gen" python3 tools/cpc_gen.py --check
check "cpc programs" python3 tools/cpc_progs.py --check
check "cpc slot model" python3 tools/cpc_slots.py selftest
check "zx/cpc shared modules once" bash -c 'python3 tools/check_zxcpc_shared.py selftest && python3 tools/check_zxcpc_shared.py'
gcheck core "layer-2 contract" python3 tools/contract_ports.py --check
# Alex, 2026-11-08 (T115 section 5): the old DFX trx64 core stays deleted; the selftest plants each fault
check "no old core (trx64 directory and paths)" python3 tools/check_no_old_core.py
check "no old core: selftest" python3 tools/check_no_old_core.py --selftest
gcheck tree "TODO rows and specs (Open lines, DONE specs in docs/specs/done/)" python3 -I tools/check_todo.py
# T107 D7 / T164 WP1: the HDMI mode tables (A1200 and host editions) against every limit, the committed .vh files, and one negative control per rule
check "hdmi modes (A1200 + host)" bash -c 'python3 tools/hdmi_modes.py check && python3 tools/hdmi_modes.py selftest >/dev/null'
# T168 S9: DDT's ported files are byte-identical to the pinned upstream or have their .diff, NOTICE names licence / repository / commit, no character ROM, the palette is the tool's
check "ddt cores: tree marking, no ROM, palette" bash -c 'python3 tools/check_ddt_tree.py selftest && python3 tools/check_ddt_tree.py'
# T89 S1: no Apple ROM bytes (system, character, Disk II boot ROM) anywhere in the tree; the selftest plants every encoding first
gcheck tree "no Apple ROM content" bash -c 'tools/check_no_roms.sh --selftest && tools/check_no_roms.sh'
# T239.1 D24, S1, D6: no private file and no internal host, home path, Gitea host, builder login or claude link in a tracked file; the selftest plants every rule.
# The same check runs in .github/workflows/public-tree.yml on every push (no paths-ignore) and in .githooks/pre-push.
gcheck tree "public tree (T239.1 D24)" bash -c 'python3 -I tools/check_public_tree.py --selftest >/dev/null && python3 -I tools/check_public_tree.py'
# T239.1 D9: while a Gitea workflow carries a copy of the Vivado env (vivado.yml, package.yml), it must agree with ci/vivado.env
check "vivado env copies (T239.1 D9)" bash -c 'python3 tools/check_vivado_env.py --selftest >/dev/null && python3 tools/check_vivado_env.py'
# T239.1 D1, S9: workflow rules (risky triggers with a secret or environment, the builds guard, permissions, SHA and digest pins, no self-hosted, no private repository name); the selftest plants one fault per rule
check "workflow rules (T239.1 D1)" bash -c 'python3 -I tools/check_workflows.py --selftest >/dev/null && python3 -I tools/check_workflows.py'
gcheck tree "toml_a_list fixtures (T264 S1: a Verilog and a VHDL decoder, a moved offset, a removed and an extra register)" python3 -I tests/toml_a_list/test_toml_a_list.py
gcheck tree "toml_a_gen fixtures (T264 S2: a decode without the block, a stale block, an unused constant; a Verilog and a VHDL decoder)" python3 -I tests/toml_a_gen/test_toml_a_gen.py
gcheck tree "generated register constants current and used, every lane core (T264 S2: tools/toml_a_gen.py)" python3 -I tools/toml_a_gen.py check --all
gcheck sdk "cor_pack test" python3 tools/test_cor_pack.py
gcheck rtlchk "rtl_check test (T239.3 D30; T264 S3: an optional tool, its group runs only when tools/rtl_check.py or tests/rtl_check change; the c64_basic case needs a TRX64-Ultimate checkout, P1-7 runs it in that lint)" python3 -I tests/rtl_check/test_rtl_check.py
gcheck sdk "reverse lint: no literal I/O address in a library or base source (T239.3 D30, P2-6)" python3 -I tools/rtl_check.py lint-src app/lib app/sdkpkg/example coreapp/src --allow wsproto.cc:0x10325476u   # P2-6: coreapp/src too; the one literal is a hash initial word
# T104.2: the pinned TRX64 bitstream is packed with the generic application, any other file is refused (the control must fail)
check "import .cor pack test" bash -c 'tools/test_pack_import_cor.sh && ! tools/test_pack_import_cor.sh --control >/dev/null 2>&1'
gcheck sdk "descrec test" python3 tools/test_descrec.py
gcheck sdk "libtab test" python3 tools/test_libtab.py
gcheck sdk "app_build test (T239.3 D31: the maxima per core, the split of the base)" python3 tools/test_app_build.py
gcheck sdk "the SDK links with the options of the core app Makefile (T239.3 D31)" python3 -I tools/app_build.py flags host-app/target/u64ii/riscv/coreapp/Makefile app/lib/appflags.mk
check "base UI table" python3 tools/gen_base_ui.py --check --ops tests/coreapp/golden/ops_amiga1200.txt
# T192 D23 (Alex, 2026-11-09: "Until the UI, including the UX, works completely as an API client, it is a botch"): the host's screen changes no state outside host_core, acts on
# no key itself (the key map is one table), carries no hand-written legend; what is not cut yet is the ledger (tools/host_cut_ledger.txt) and only shrinks.
gcheck host "host screen is a client of host_core (T192 D23)" bash -c 'python3 -I tools/check_host_cut.py --selftest >/dev/null && python3 -I tools/check_host_cut.py'
# T192.2 P5 (S11, S12): the parity compare of two render outputs reports a pixel, a word of a dump, a missing and an extra file, and passes on two equal copies
check "render compare (T192.2 parity)" python3 -I tools/render_compare.py --selftest
# T192.3 S1, S6 (D23): the core menu controller holds navigation, dialogs and rows and nothing of the domain; no raw key outside its three input handlers. The selftest plants every fault.
check "menu controller gate (T192.3 S1, S6)" python3 tools/check_menuctl_gate.py
check "menu controller gate: selftest" python3 tools/check_menuctl_gate.py --selftest
# T192.3 (Alex, 2026-11-09): one state and one reader of it, the UI service's; red if the v1 state (uistate, `?v=1`, `seq`) or a second reader of the screen returns
check "no v1 UI state, no second reader (T192.3)" python3 tools/check_no_v1_ui.py
check "no v1 UI state: selftest" python3 tools/check_no_v1_ui.py --selftest
# T211 S1 (D1): the host app names its keys itself; no Gideon page/help/tasks code treated as an F key
gcheck host "host key names (T211 S1)" bash -c 'python3 -I tools/check_host_keys.py --selftest && python3 -I tools/check_host_keys.py'
# T265 P5: the boot ROM package is made by hand; red when host/bootrom moved after it (a host key input that CI never rebuilds)
gcheck host "boot ROM package not older than its source (T265 P5)" tools/check_bootrom_fresh.sh
gcheck sdk "u2a_pack test" python3 tools/test_u2a_pack.py
# T239.3 S1: the API document of the SDK (app/sdkpkg/api.md) has a section for every symbol of the public headers of the core application (libapi.h, svctab.h, ops.h, libfmt.h);
# the unit test has a complete document and one with a missing section, and runs the real headers against the real document
gcheck sdk "API document of the SDK (T239.3 S1)" python3 -I tools/check_api_doc.py
gcheck sdk "API document: unit test (T239.3 S1)" python3 -I tools/test_check_api_doc.py
check "release checks test" python3 tools/test_release_check.py
check "release bundle test" python3 tools/test_make_release_mc.py
check "builds repository reader (T239.1 S8)" python3 -W ignore tools/test_builds.py
# T148 D8, S5: a package holds .cor files, never a core folder; ROMS.txt comes from the descriptors' ROM table and the manual
check "package layout: no core folder (selftest)" python3 tools/check_package_layout.py selftest
check "ROMS.txt test" python3 tools/test_roms_txt.py
# T239.1 D12: the bench ROMs from the pinned upstream: fetch, size and SHA-256, store, unreachable source (local file, no network)
check "bench ROMs fetch test (T239.1 D12)" python3 -I ci/test_roms.py
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


# T213 D4 / T192 D24 (Alex 2026-11-16): per-core code in the generic app only shrinks (ledger tools/d24_ledger.txt); the selftest plants each breach
gcheck sdk "D24 per-core code gate (selftest)" python3 -I tools/check_d24.py --selftest
gcheck sdk "D24 per-core code gate" python3 -I tools/check_d24.py
gcheck sdk "library table: TOML B and the generic sources (T239.3 D31)" python3 -I tools/libtab.py lint coreapp/libs/shared.toml .

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
# T174 D4, T239.1 S6, T239.5 D22: pipeline.yml only calls the matrix; matrix.yml has a job for each child of ci/children.txt, which calls the child's workflow, which checks out the
# run's commit and runs ci/child.sh; the environments vivado and builds sit only where D1 and D14 want them; every bench has one owner (ci/sims.lane).
check "children table and workflows (D4, S6)" python3 ci/orchestrate.py check
check "children table: graph, outputs, check (S6)" python3 -I ci/test_orchestrate.py
# T239.1 S6: no call of the Gitea API in ci/ or tools/ (releases and statuses are GitHub's now). The named exceptions are the Gitea read of the .cor import on the
# Mac (D22) and the builder's fetch of a pinned finished .cor (ci/imports.py fetch, T239.3 D45: its URL comes from the builder's environment, not from a file). This file and the scanner's own selftest strings are not calls.
check "no Gitea API calls (T239.1 S6)" bash -c '
  hits=$(git ls-files -- ci tools | grep -vE "^(tools/pack_import_cor\.sh|ci/lint\.sh|ci/check_pkg\.py)$" \
    | xargs grep -nE "/api/v1|/api/packages" 2>/dev/null || true)
  [ -z "$hits" ] || { echo "$hits"; exit 1; }'
check "emulator kit (T239.1 D23)" ci/test_emu_kit.sh
check "core round in the emulator (T239.5 D18)" ci/test_core_round.sh
check "public-tree checker unit tests (T239.1 D24)" python3 -I tools/test_check_public_tree.py
check "pre-push path rule (T239.1 D24)" ci/test_pre_push.sh
check "pre-release staging (T239.1 D1, D42)" ci/test_prerelease.sh
check "import entries (T239.1 D43)" python3 -W ignore ci/test_imports.py
# T239.1 D13, D19, D1: the work order, the bitstream fetch and the pre-release publication, each against a fake builds repository (ci/fakegh.py, no network)
check "work order (T239.1 D13, S15)" python3 -W ignore ci/test_order.py
check "bitstream fetch (T239.1 D19)" python3 -W ignore ci/test_fetch_bits.py
check "pre-release publication (T239.1 D1)" python3 -W ignore ci/test_publish_release.py
check "SDK release publication (T261 D4)" python3 -W ignore ci/test_publish_sdk.py
check "select.sh" bash -c '
  t() { [ "$(env -i HOME="$HOME" PATH="$PATH" "${@:2}" ci/select.sh $1)" = "$EXPECT" ]; }
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=push &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_dispatch UE2_INPUT=false &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_dispatch UE2_INPUT=true &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_call UE2_INPUT=true &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=main GITHUB_EVENT_NAME=workflow_call UE2_INPUT= &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=v1.0 GITHUB_REF_TYPE=tag GITHUB_EVENT_NAME=push &&
  EXPECT=no  t ue2 GITHUB_REF_NAME=release/1 GITHUB_EVENT_NAME=push &&
  EXPECT=yes t ue2 GITHUB_REF_NAME=v1.0 GITHUB_REF_TYPE=tag GITHUB_EVENT_NAME=workflow_dispatch UE2_INPUT=true &&
  EXPECT=yes t update GITHUB_REF_NAME=integ-m1 &&
  EXPECT=yes t update GITHUB_REF_NAME=main &&
  EXPECT=yes t update GITHUB_REF_NAME=integration &&
  EXPECT=no  t update GITHUB_REF_NAME=feature SELECT_MSG=x CI_BRANCH=feature &&
  EXPECT=yes t update GITHUB_REF_NAME=12/merge CI_BRANCH=main &&
  EXPECT=2   t sims-tier GITHUB_REF_NAME=integration GITHUB_EVENT_NAME=push &&
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
# T221 S1/S2: every deliverable has its own x.y.z in its own file, the build strings are made from them (ci/version.sh)
gcheck tree "versions (T221 S2, base API lock T239.3 S5)" python3 tools/check_versions.py
gcheck sdk "base API lock test (T239.3 S5)" python3 tools/test_sdk_api.py
gcheck tree "version strings (T221 S1)" ci/test_version.sh
check "workflow" python3 - <<'PY'
import glob, os, sys, yaml
ok = True
for p in glob.glob(".github/workflows/*.yml"):
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
