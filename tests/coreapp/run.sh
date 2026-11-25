#!/usr/bin/env bash
# Native tests of the core-application blueprint (T93), after the pattern of tests/hostapp/run.sh (T101): the logic of
# coreapp/src is compiled on the Mac against stubs, no board. Negative controls (T174 D5.4, Alex 2026-11-08) exist only for the tests of flash
# operations and of persistence (settings, saves, mounts, write-back; ci/controls.allow.d/coreapp.allow): the product source is copied, one
# expression is broken on purpose with sed, the same test is built against the broken copy and must FAIL. The product tree is never touched.
#
#   tests/coreapp/run.sh            all tests and the controls of flash and persistence
#   CA_GROUP=<core>|cross|shared tests/coreapp/run.sh    one group of units (T174 D3, cores.txt; ci/apptests.sh runs one suite per group)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/coreapp/src"
CORES="$ROOT/coreapp/cores"
HA="$ROOT/host-app/software/hostapp"
TST="$ROOT/tests/coreapp"
T="$(mktemp -d)"
source "$ROOT/tests/parallel.sh"
source "$ROOT/tests/hostgen.sh"     # T239.3 V-3b: the I/O bases and the table of the base blocks from the TOML A of the host, on CPATH (no default in iomap.h, u2p.h, descrec.cc)
source "$ROOT/tests/ctlcache.sh"   # T100 D13, D14: every test program and control is a unit, served from the cache when what it reads is unchanged
CC_SUITE=coreapp
# T174 D3: groups. CA_GROUP=all (default) runs every unit; CA_GROUP=<core>|cross|shared runs only the units of that group (ci/apptests.sh suites
# coreapp:<core>, coreapp:cross, coreapp). A unit is in the group of the one core whose files (tests/coreapp/cores.txt) its sources name, `cross` when it
# names several cores or reads the cores at run time (RUNTIME_CROSS), `shared` when it names none. Every dependency the compiler lists for a unit is
# checked against its group (ca_check, called from the key functions of run and control_job): a unit that includes a file of another core fails the suite.
CA_GROUP="${CA_GROUP:-all}"
RUNTIME_CROSS=" test_noflash.cc "   # reads coreapp/cores at run time (tests/ctlcache.py RUNTIME_READS)
eval "ca_owner() { CA_O=''; case \"\$1\" in $(awk -F'[: ]+' '/^[a-z0-9_]+:/ { printf "%s", ""; for (i = 2; i <= NF; i++) printf "%s%s", (i > 2 ? "|" : ""), $i; printf ") CA_O=%s;; ", $1 }' "$TST/cores.txt") esac; }"
ca_dephook() { ca_check "$CA_CUR_GROUP" "$CA_CUR_NAME" || : > "$T/ca_bad"; }   # tests/ctlcache.sh calls it with the unit's -MM listing on stdin
CC_DEPS_HOOK=ca_dephook
ca_in() { [ "$CA_GROUP" = all ] || [ "$CA_GROUP" = "$1" ]; }
# T239.5 D4: CA_UNITS="<unit> ..." runs only the named units (the tests a core's TOML B names for one step of its lane: tools/libtab.py tests); a control is a unit of its own
# and runs only when it is named too. Every named unit must be one this harness runs in the group, else the run fails (a TOML B that names a test nobody wrote). Empty: every unit.
CA_UNITS="${CA_UNITS:-}"
ca_unit() { [ -z "$CA_UNITS" ] && return 0; case " $CA_UNITS " in *" $1 "*) echo "$1" >> "$T/ca_units_done"; return 0 ;; esac; return 1; }
ca_group() {   # ca_group TESTFILE SRC... : prints the group
  local f g="" o
  # T239.5: a unit built as a core's own app (vc20app sets CA_APP) is in that core's group: its lane runs it (CA_GROUP=<core>), the shared suite does not
  [ -z "${CA_APP:-}" ] || { echo "$CA_APP"; return; }
  case "$RUNTIME_CROSS" in *" $1 "*) echo cross; return ;; esac
  shift
  for f in "$@"; do
    ca_owner "${f##*/}"; o="$CA_O"; [ -n "$o" ] || continue
    if [ -z "$g" ]; then g="$o"; elif [ "$g" != "$o" ]; then echo cross; return; fi
  done
  echo "${g:-shared}"
}
ca_check() {   # ca_check GROUP NAME : the -MM listing on stdin; refuses a dependency owned by another core than the group's
  local g="$1" f
  while IFS= read -r f; do
    ca_owner "${f##*/}"; [ -n "$CA_O" ] || continue
    case "$f" in */coreapp/cores/*) ;; *) continue ;; esac
    [ "$g" = cross ] || [ "$CA_O" = "$g" ] || { echo "coreapp: unit $2 is in group $g but lists $f of core $CA_O (tests/coreapp/cores.txt)" >&2; return 1; }
  done < <(tr ' ' '\n' | grep '/coreapp/cores/' || true)
}
trap 'pjob_kill; cc_summary; rm -rf "$T"' EXIT
CXX="${CXX:-c++}"
FLAGS="-std=gnu++11 -Wall -Wno-unused-function -fno-exceptions -pthread"
PASS=0
# the shared base every core application links, and the one descriptor each blueprint core adds to it
BASEL="configlist.cc board_stubs.cc appindex_stub.cc t64.cc boot.cc partial.cc cor.cc sha256_cc.cc contrib.cc bindcheck.cc settings_ops.cc memops.cc romfind.cc corefolder.cc settings.cc cfgfile.cc video.cc videomode.cc edid.cc picture.cc picmodes.cc volume.cc tone.cc baseset.cc hookhost.cc hotkeys.cc home.cc ops.cc ops_glue.cc exec.cc video_stub.cc regset.cc engines.cc ledload.cc winslot.cc sectors.cc mediamounts.cc amiga_ffs.cc folderhd.cc folderhd_fold.cc folderhd_exports.cc romset.cc mousesink.cc mouse_exports.cc rawkeys.cc pad.cc hidpad.cc pad_ops.cc padwiz.cc pad_exports.cc padkeys.cc padkeys_exports.cc libtab_native.cc fbwin.cc kickstart.cc newfile.cc textcopy.cc newdisk.cc writeback.cc writeback_crt.cc menubuild.cc menutree.cc"
ATARI="atari800.cc atari_media.cc atari_sio.cc atari_atr.cc atari_xex.cc atari_cart.cc $BASEL"
# the cbm1541 library (T104.1 D15): the drive logic (app/lib/cbm1541) and its contributor; every descriptor with a drive links them
DRV="cbm1541.cc d1541_drive.cc d1541_gcr.cc d1541_wd.cc d1541_fs.cc"
C64="c64_go.cc c64_cart.cc cbm_prg.cc board.cc $DRV"

NESSRC="nes.cc ines.cc $BASEL"
# build NAME TEST.cc SRC... : the product sources are looked up in $SRC, then $CORES, then $HA
LOADERS="$ROOT/app/loaders"
VIDEO="$ROOT/app/video"
LIB1541="$ROOT/app/lib/cbm1541"
source "$TST/vc20_drv.sh"
vc20_drv_dir "$T/vc20tab" || { echo "coreapp: the VIC-20's driver table was not generated" >&2; exit 1; }   # vc20_regs.h names the registers through it; not $T/vc20, the unit vc20 is built there
vc20_drv_dir "$T/vc20wd" wd || { echo "coreapp: the driver table of a VIC-20 drive with the WD177x was not generated" >&2; exit 1; }   # test_cbm1541's 1581 tests (cbm1541_wd)
source "$TST/ddt_drv.sh"
ddt_drv_dir "$T/ddttab" || { echo "coreapp: DDT Look Ma, No VIC's driver table was not generated" >&2; exit 1; }   # ddt_regs.h names the registers through it
DDTLIB="$ROOT/cores/ddt_novic/app/lib"   # the DDT core's library lives in its own repository part (cores/ddt_novic/app/lib), not in coreapp/cores
source "$TST/zx_drv.sh"
zx_drv_dir "$T/zxtab" || { echo "coreapp: the ZX Spectrum's driver table was not generated" >&2; exit 1; }   # zx_regs.h names the registers through it
ZXLIB="$ROOT/cores/zx/app/lib"   # the ZX Spectrum's library lives in its own repository part (cores/zx/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h
source "$TST/cpc_drv.sh"
cpc_drv_dir "$T/cpctab" || { echo "coreapp: the CPC's driver table was not generated" >&2; exit 1; }   # cpc_regs.h names the registers through it
source "$TST/c128_drv.sh"
c128_drv_dir "$T/c128tab" || { echo "coreapp: the C128's driver table was not generated" >&2; exit 1; }   # c128_regs.h names the registers through it
C128LIB="$ROOT/cores/c128/app/lib"   # the C128's library lives in its own repository part (cores/c128/app/lib), not in coreapp/cores; it holds its own copies of hk_defaults.h and drive_rom_pick.h
source "$TST/pet_drv.sh"
pet_drv_dir "$T/pettab" || { echo "coreapp: the PET's driver table was not generated" >&2; exit 1; }   # pet_regs.h names the registers through it
PETLIB="$ROOT/cores/pet/app/lib"   # the PET's library lives in its own repository part (cores/pet/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.* (moved there) and hk_defaults.h
CPCLIB="$ROOT/cores/cpc/app/lib"   # the CPC's library lives in its own repository part (cores/cpc/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h
source "$TST/a1200_drv.sh"
a1200_drv_dir "$T/a12tab" || { echo "coreapp: the Amiga 1200's driver table was not generated" >&2; exit 1; }   # amiga1200_regs.h names the registers through it
A1200LIB="$ROOT/cores/amiga1200/app/lib"   # T213.3 P1: the Amiga 1200's library lives in its own repository part (cores/amiga1200/app/lib)
VC20LIB="$ROOT/cores/vc20/app/lib"   # T239.3 V-4: the VIC-20's library lives in its own repository part (cores/vc20/app/lib), not in coreapp/cores
EDIT="$ROOT/app/edit"
MZ_APP="$ROOT/apps/miniz"   # T197: the zip library, one source for the board image and for this test
UZ_APP="$ROOT/apps/unzip"   # T180 D9: the application's own C files are compiled for the Mac by test_unzip (build: .c files go through cc)
find_src() { for d in "${MUTDIR:-/nonexistent}" "${APPLIB:-/nonexistent}" "$SRC" "$CORES" "$VC20LIB" "$A1200LIB" "$DDTLIB" "$LOADERS" "$VIDEO" "$LIB1541" "$EDIT" "$HA" "$UZ_APP/src" "$MZ_APP/src" "$MZ_APP" "$UZ_APP" "$ROOT/app/sdk" "$ROOT/uicore" "$TST"; do [ -f "$d/$1" ] && { echo "$d/$1"; return; }; done; echo "missing source $1" >&2; exit 2; }
# the driver table the shared app is linked with (T239.3 D31): the drive library (d1541_drive.cc) and the SID library (resid8.cc) name their registers through it
python3 "$ROOT/tools/descrec.py" drivers "$ROOT/coreapp/gateware/shared.toml" "$T/drv" --maps "$ROOT/app/sdkpkg/maps"
INC=(-DGOLDEN_DIR=\"$TST/golden\" -DCOREAPP_DIR=\"$ROOT/coreapp\" -I"$ROOT/host-app/software/io/c64" -DVIDEOMODE_NO_BOARD=1 -I"$SRC" -I"$ROOT/uicore" -I"$CORES" -I"$T/drv" -I"$LOADERS" -I"$VIDEO" -I"$ROOT/app/view" -I"$LIB1541" -I"$EDIT" -I"$HA" -I"$TST" -I"$TST/ftpstub" -I"$TST/hidstub" -I"$ROOT/host-app/software/io/usb")
# The VIC-20 is its own app (T239.3 V-4: cores/vc20/app, linked from the SDK package with its own table, no shared table, no other core): its units are built like that app.
# vc20app CMD...: runs `run` or `control` with the VIC-20's include path (its library, its plain table from its TOML A and the base blocks of its TOML B, no table of the shared app)
# and with TEST_APP_VC20 defined, so a test file that serves both apps takes the VIC-20's rows only in these builds and the shared app's rows only in the others.
INC_VC20=(-DTEST_APP_VC20=1 -I"$VC20LIB" -I"$T/vc20tab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_VC20+=("$i"); done
# The Amiga 1200 is its own app as well (T213.3: cores/amiga1200/app). a12app CMD... builds like vc20app, with the A1200's library, its table and TEST_APP_A1200, and with a shim amiga_regs.h
# (tests/coreapp/a1200shim) in front of the shared header, so the files that serve both apps get the A1200's registers; the shared app's amiga.cc is not in these builds (amiga1200.cc is).
INC_A1200=(-DTEST_APP_A1200=1 -I"$TST/a1200shim" -I"$A1200LIB" -I"$T/a12tab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_A1200+=("$i"); done
# DDT Look Ma, No VIC is its own app too (cores/ddt_novic/app): ddtapp CMD... builds like vc20app, with its library, its table and TEST_APP_DDT.
INC_DDT=(-DTEST_APP_DDT=1 -I"$DDTLIB" -I"$T/ddttab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_DDT+=("$i"); done
# The ZX Spectrum is its own app too (cores/zx/app): zxapp CMD... builds like ddtapp, with its library, its table and TEST_APP_ZX; APPLIB puts its library first in the source lookup,
# so its own copies of tape_deck.cc, tape_pulses.cc (and the headers, by -I) are the ones built, not the shared app's.
INC_ZX=(-DTEST_APP_ZX=1 -I"$ZXLIB" -I"$T/zxtab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_ZX+=("$i"); done
INC_CPC=(-DTEST_APP_CPC=1 -I"$CPCLIB" -I"$T/cpctab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_CPC+=("$i"); done
INC_PET=(-DTEST_APP_PET=1 -I"$PETLIB" -I"$T/pettab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_PET+=("$i"); done
INC_C128=(-DTEST_APP_C128=1 -I"$C128LIB" -I"$T/c128tab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_C128+=("$i"); done
c128app() { local INC=("${INC_C128[@]}") CA_APP=c128 APPLIB="$C128LIB"; "$@"; }
petapp() { local INC=("${INC_PET[@]}") CA_APP=pet APPLIB="$PETLIB"; "$@"; }
cpcapp() { local INC=("${INC_CPC[@]}") CA_APP=cpc APPLIB="$CPCLIB"; "$@"; }
zxapp() { local INC=("${INC_ZX[@]}") CA_APP=zx APPLIB="$ZXLIB"; "$@"; }
ddtapp() { local INC=("${INC_DDT[@]}") CA_APP=ddt_novic; "$@"; }
a12app() { local INC=("${INC_A1200[@]}") CA_APP=amiga1200; "$@"; }
vc20app() { local INC=("${INC_VC20[@]}") CA_APP=vc20; "$@"; }
build() {
  local out="$1" test="$2"; shift 2
  local files=()
  local more=() f o
  set -- $(printf '%s\n' "$@" | awk '!seen[$0]++')   # a source named by the base and by the unit is linked once
  case "$test" in *test_unzip.cc) more=(-I"$ROOT/app/sdk" -I"$UZ_APP/src" -I"$MZ_APP/src" -I"$MZ_APP");; esac
  for s in "$@"; do
    f="$(find_src "$s")"
    case "$f" in
      *.c) o="$(mktemp -d "${TMPDIR:-/tmp}/uzc.XXXXXX")/$(basename "$f").o"
           cc -std=gnu99 -w -c ${MUTDIR:+-I"$MUTDIR"} "${INC[@]}" ${more[@]+"${more[@]}"} "$f" -o "$o"
           files+=("$o");;
      *) files+=("$f");;
    esac
  done
  $CXX $FLAGS ${MUTDIR:+-I"$MUTDIR"} "${INC[@]}" ${more[@]+"${more[@]}"} -o "$out" "$test" ${files[@]+"${files[@]}"}
}
run() {   # run NAME TEST.cc SRC...
  local name="$1" test="$2"; shift 2
  # T100 D14: the test program is a unit as well - the key is its dependency list (test, sources, every header) and its run arguments,
  # the manifest what the run opened (tests/ctlcache.sh); an unchanged key with unchanged reads is "cached PASS" and is neither built nor run
  set -- $(printf '%s\n' "$@" | awk '!seen[$0]++')
  local key srcs=() s t0=$SECONDS grp; for s in "$@"; do srcs+=("$(find_src "$s")"); done
  grp="$(ca_group "$test" "$@")"; ca_in "$grp" || return 0
  ca_unit "$name" || return 0
  CA_CUR_GROUP="$grp" CA_CUR_NAME="$name"
  key="$(cc_key coreapp "test $name $test $* ${RARGS:-}" --runargs "${RARGS:-}" -- $FLAGS "${INC[@]}" -- "$TST/$test" ${srcs[@]+"${srcs[@]}"})"
  [ -z "${UPDATE_GOLDEN:-}${FH_ONLY:-}" ] || key=""   # a run that writes goldens or runs a part of a test proves nothing for the next one
  if cc_cached "$name" "$key" test; then PASS=$((PASS + 1)); return 0; fi
  build "$T/$name" "$TST/$test" "$@"
  cc_exec "$T/rl_$name" "$T/$name" ${RARGS:-}
  echo "test $name: PASS $((SECONDS - t0))s"
  cc_store "$name" "$key" "$T/rl_$name" test
  PASS=$((PASS + 1))
}
# control NAME FILE 'sed-expr' TEST.cc SRC... : breaks FILE, builds the test against the copy, expects a failing run.
# T100 D12: controls run JOBS at a time (tests/parallel.sh); each in its own $JOBDIR, nothing shared between them.
control_job() {
  local name="$1" file="$2" expr="$3" test="$4"; shift 4
  set -- $(printf '%s\n' "$@" | awk '!seen[$0]++')
  # T100 D13: the key is made of the unmutated tree's dependency list (the mutated file is in it) and the sed itself
  local key srcs=() s; for s in "$@"; do srcs+=("$(find_src "$s")"); done
  CA_CUR_GROUP="$(ca_group "$test" "$@")" CA_CUR_NAME="$name"
  key="$(cc_key coreapp "control $name $file $expr $test $* ${CARGS:-}" --runargs "${CARGS:-}" -- $FLAGS "${INC[@]}" -- "$TST/$test" ${srcs[@]+"${srcs[@]}"})"
  if cc_cached "$name" "$key"; then return 0; fi
  local d="$JOBDIR/mut"; mkdir -p "$d"
  cp "$SRC"/* "$d"/ 2>/dev/null || true
  cp -R "$CORES"/* "$d"/ 2>/dev/null || true
  [ -z "${APPLIB:-}" ] || cp "$APPLIB"/* "$d"/   # a core of its own app: its library over the shared sources of the same name
  cp "$LOADERS"/* "$d"/ 2>/dev/null || true
  cp "$VIDEO"/* "$d"/ 2>/dev/null || true
  cp "$LIB1541"/* "$d"/ 2>/dev/null || true
  cp "$EDIT"/* "$d"/ 2>/dev/null || true
  cp "$UZ_APP"/src/*.c "$UZ_APP"/glue.cc "$d"/ 2>/dev/null || true
  cp "$HA"/handover.* "$HA"/wifinet.h "$d"/
  # portable in-place edit: GNU sed has no backup-suffix operand and BSD sed demands one, and the
  # difference only showed when this suite started running in CI (run 54)
  sed -e "$expr" "$d/$file" > "$d/$file.sed" && mv "$d/$file.sed" "$d/$file"
  if cmp -s "$d/$file" "$(find_src "$file")"; then echo "CONTROL $name: the sed expression changed nothing - the control is vacuous" >&2; exit 1; fi
  MUTDIR="$d" build "$JOBDIR/x" "$TST/$test" "$@" || { echo "CONTROL $name: the mutant does not build" >&2; exit 1; }
  # A control writes no golden file. Without this, UPDATE_GOLDEN=1 on a full run lets the broken build
  # overwrite the golden with its own output and the next run is green against a wrong file. The
  # variable has to be removed, not emptied: the test asks getenv(), and an empty value is still a
  # pointer and still true.
  if cc_exec "$JOBDIR/rl" env -u UPDATE_GOLDEN timeout 30 "$JOBDIR/x" ${CARGS:-} > "$JOBDIR/x.log" 2>&1; then
    echo "CONTROL $name: the test PASSED against the broken source - it cannot see this defect" >&2; cat "$JOBDIR/x.log" >&2; exit 1
  fi
  local red; red=$(grep -c '^FAIL' "$JOBDIR/x.log" || true)
  if [ "$red" -lt 1 ]; then echo "CONTROL $name: the run failed without a red check (a crash?) - that is not the test seeing the defect" >&2; cat "$JOBDIR/x.log" >&2; exit 1; fi
  echo "control $name: fails as it must ($red check(s) red)"
  cc_store "$name" "$key" "$JOBDIR/rl"
}
control() { cc_no_controls && return 0; ca_in "$(ca_group "$4" "${@:5}")" || return 0; ca_unit "$1" || return 0; pjob_spawn control_job "$@"; }

if ca_in shared; then   # T174 D3: the table of cores is sound, and the group check can fail
  python3 -I - "$TST/cores.txt" "$CORES" <<'PY'
import fnmatch, os, sys
own = {}
for ln in open(sys.argv[1]):
    if ln.startswith("#") or ":" not in ln:
        continue
    core, pats = ln.split(":", 1)
    for pat in pats.split():
        hits = [f for f in os.listdir(sys.argv[2]) if fnmatch.fnmatchcase(f, pat)]
        if not hits:
            sys.exit("cores.txt: '%s' of core %s matches no file in coreapp/cores" % (pat, core.strip()))
        for f in hits:
            if own.setdefault(f, core.strip()) != core.strip():
                sys.exit("cores.txt: %s belongs to %s and %s" % (f, own[f], core.strip()))
PY
  printf '%s\n' "$CORES/c264.cc $CORES/c264_regs.h $CORES/c64_go.cc" | ca_check c264 probe || { echo "groups: the group's own files were refused" >&2; exit 1; }
fi
run handover test_handover.cc handover.cc
control handover_cfgext handover.cc 's/strcasecmp(cor_path + l - 4, ".cor") != 0 || //' test_handover.cc handover.cc

run start test_start.cc start.cc handover.cc amiga.cc $BASEL
run startfile test_startfile.cc start.cc handover.cc amiga.cc $BASEL     # T230 P-2: the start file on the monolith
# the host key from the host's BUILD register through the hand-over page to the F8 "Reload core" check (board 2026-11-04): a .cor of the running host passes, one of another host is refused with both keys named
run hostkey test_hostkey.cc start.cc handover.cc amiga.cc $BASEL
# T115 7 H2: the pick by id (I1-I5)

ZXN="zxnext.cc $BASEL"
run zxnext test_zxnext.cc $ZXN
# T116 D11: Auto
NEOG="neogeo.cc neoset.cc $BASEL"
run neogeo test_neogeo.cc $NEOG
control neo_save_read neogeo.cc 's/    c->io->ddr_read(NEO_W_BACKUP, buf, 0x10000);//' test_neogeo.cc $NEOG
control neo_save_load neogeo.cc 's/    c->io->ddr_write(NEO_W_BACKUP, buf, 0x10000);//' test_neogeo.cc $NEOG
control neo_own_save neogeo.cc 's/bool own = corefolder_save_path(c->cor_path, file, out, outlen);/bool own = false;/' test_neogeo.cc $NEOG
control neo_old_save neogeo.cc 's/        if (n < 0 \&\& st->old_save_path\[0\]) {/        if (false) {/' test_neogeo.cc $NEOG

# T142: the SNES application
run snes_rom test_snes_rom.cc snes_rom.cc
SNES="snes.cc snes_rom.cc $BASEL"
run snes test_snes.cc $SNES
control snes_load snes.cc 's/int n = st->save_path\[0\] ? c->fs->load(st->save_path, st->shadow, info.ram_bytes) : -1;/int n = -1;/' test_snes.cc $SNES
control snes_changed snes.cc 's/    if (!force \&\& !memcmp(cur, st->shadow, n)) {/    if (false) {/' test_snes.cc $SNES
control snes_flush_init snes.cc 's/    flush_save(c, false, msg, msglen);                        \/\/ what the game wrote/    (void)0;                                                  \/\/ what the game wrote/' test_snes.cc $SNES
control snes_retry snes.cc 's/    if (!c->fs->save(st->save_path, cur, n)) {/    if (!c->fs->save(st->save_path, cur, n) \&\& (memcpy(st->shadow, cur, n), true)) {/' test_snes.cc $SNES
control snes_dir snes.cc 's|snprintf(dir, len, "%s/saves", parent);|snprintf(dir, len, "%s/save", parent);|' test_snes.cc $SNES

ROMF="romfind.cc corefolder.cc sha256_cc.cc"
run romfind test_romfind.cc $ROMF

run boot_atari test_boot_atari.cc $ATARI

run video test_video.cc amiga.cc $ATARI nes.cc ines.cc c64scpu.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV
# T136 S3: the A1200's 32-bit pixels: the stride and the format follow the descriptor's bytes per pixel

run settings test_settings.cc $ATARI
control set_loses_value settings.cc '/^bool Settings::set(int i, int v)/,/^}/s/    return write_cfg();/    { bool ok_ = write_cfg(); if (!ok_) core_[i] = -1; return ok_; }/' test_settings.cc $ATARI
control set_writes_all settings.cc 's/^        if (core_\[i\] >= 0 || all) {/        if (true) {/' test_settings.cc $ATARI
control set_reset_keeps settings.cc 's/if (fs_->remove(cfg_path_) || write_cfg())/if (false)/' test_settings.cc $ATARI
control set_case cfgfile.cc 's/strncasecmp(a, b, la) == 0/strncmp(a, b, la) == 0/' test_settings.cc $ATARI

ca_in shared && if command -v 64tass >/dev/null; then "$ROOT/tools/gen_xexboot.sh" --check; else echo "(64tass not installed: the xexboot.asm / xexboot_bin.h drift check is skipped)"; fi
# T104 5.6 item 1: the CBM BASIC loader, library code the VC-20 and the C16 will share
run cbm_prg test_cbm_prg.cc cbm_prg.cc
# T134: the PET models' ROM names and the PRG loader numbers for BASIC 1: the unit pet_models, built as the PET's app (below)

run atari_xex test_atari_xex.cc atari_xex.cc

run atari_sio test_atari_sio.cc atari_sio.cc atari_atr.cc
control sio_nowrite atari_sio.cc 's/if (d->write(pend_sector_, data_)) {/if (true) {/' test_atari_sio.cc atari_sio.cc atari_atr.cc

run atari_media test_atari_media.cc $ATARI

run ines test_ines.cc ines.cc

run nes test_nes.cc $NESSRC

# T175: the diag core's test files (diagtest.cc) against a model of the probe engine. The example tests are record 8 of diag.cor (T175 D-files): the real packer makes the
# record, the application's installer (instfiles.cc) writes it onto the fake card (diag_examples.h)
python3 "$ROOT/tools/cor_pack.py" files-record "$ROOT/cores/diag/sd" --old "$ROOT/cores/diag/superseded" -o "$T/diag_files.rec" > /dev/null
export DIAG_FILES_REC="$T/diag_files.rec"
python3 "$TST/gen_bundle.py" "$T/bundles"       # diag.cor: the .cor as the packer makes it, for the bench of the installer on a bundle
export DIAG_COR="$T/bundles/diag.cor"
run diagtest test_diagtest.cc diagtest.cc instfiles.cc
control diagtest_halffile diagtest.cc 's/        if (env->fs->remove(a)) {  /        if (true) {  /' test_diagtest.cc diagtest.cc instfiles.cc
control diagtest_overwrite instfiles.cc 's/if (fs->stat(path, \&sz, \&dir) \&\& (dir || /if (false \&\& (dir || /' test_diagtest.cc diagtest.cc instfiles.cc
control diagtest_superseded instfiles.cc 's/(dir || !is_earlier_version(fs, path, sz, olds, nold))/(true)/' test_diagtest.cc diagtest.cc instfiles.cc

run portdiag test_portdiag.cc portdiag.cc xinput.cc diagtest.cc instfiles.cc instcore.cc $BASEL
run freeze test_freeze.cc freeze.cc

# T104.1 D12, Q10: the libraries of a folder, in dependency order, with the real descriptors' capability lists
LIBSRC="overlay.cc libload.cc libmenu.cc descrec.cc libscan.cc desc_check.cc menutree.cc $ATARI nes.cc ines.cc board.cc"
run libscan test_libscan.cc $LIBSRC
GUARDSRC="libguard.cc bootcfg.cc"
run libguard test_libguard.cc $GUARDSRC
control guard_noset libguard.cc 's/    if (!write(1)) {/    if (false) {/' test_libguard.cc $GUARDSRC
control guard_noclear libguard.cc 's/    return v == 0 || write(0);/    return true;/' test_libguard.cc $GUARDSRC
control guard_nokey libguard.cc 's/    if (held) {/    if (false) {/' test_libguard.cc $GUARDSRC
control guard_nocounter libguard.cc 's/    if (known_ \&\& v > 0) {/    if (false) {/' test_libguard.cc $GUARDSRC
control guard_always_write libguard.cc 's/    if (!known_ || stored_ == 0) {/    if (!known_) {/;s/    return v == 0 || write(0);/    return write(0);/' test_libguard.cc $GUARDSRC
control guard_overwrite libguard.cc 's/    if (!known_) {                       \/\/ unreadable, or not there/    if (false) {                       \/\/ unreadable, or not there/' test_libguard.cc $GUARDSRC
control guard_unmounted libguard.cc 's/        return false;                    \/\/ "\/x": a volume root, never certain/        return true;/' test_libguard.cc $GUARDSRC
control guard_listed_statfail libguard.cc 's/            return false;                \/\/ listed but not statable: not absent/            return true;/' test_libguard.cc $GUARDSRC
control guard_write_in_begin libguard.cc 's/    out->armed = false;                  \/\/ armed by arm(), just before the first library loads/    out->armed = write(1);/' test_libguard.cc $GUARDSRC
WAITSRC="libscan.cc overlay.cc libload.cc libmenu.cc descrec.cc ops.cc"
run libwait test_libwait.cc $WAITSRC

RARGS="$T/bundles/desc_own.bin" run desc test_desc.cc desc_check.cc menutree.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc
# T104.1 D14: the checker refuses by name what the registry cannot serve
# T157: the "Joystick ports" setting of every core that has the swap: there once, applied to the core's register, kept in the .cfg
run joyswap test_joyswap.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc snes_rom.cc c264.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc
run ledcores test_ledcores.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc snes_rom.cc c264.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc
# T181: the hotkeys while a core runs (hotkeys.h): the mask registers, S1 within 100 ms, S2 a non-hotkey changes nothing, S3 held = once (volume repeats), the USB keyboard's filter,
# S5 the default of every core is no key use of that core
run hotkeys test_hotkeys.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc snes_rom.cc c264.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc c64scpu.cc neogeo.cc neoset.cc
# T181 D4: a hotkey action while the menu is closed draws nothing (no ui_show, no toast, no layer)
ca_in shared && python3 "$ROOT/tools/check_hotkey_nooverlay.py"
ca_in shared && python3 "$ROOT/tools/check_hotkey_nooverlay.py" --selftest
# T180 D10 (#40): the home folder of every core (the A1200 here): Set as home folder lands in its .cfg, Enter Home on Startup decides where F3 opens
run home test_home.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc libfiles.cc $ATARI
control home_not_stored libfiles.cc 's/    ops_call(c, "settings.set", in, 2, &r);/    (void)r;/' test_home.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc libfiles.cc $ATARI
# T192.3 D30: the menu opens where it was left ("Menu opens at" = Last screen): area, tab, row, Library folder and file, scroll; Home (F1) as before; what is gone falls back
run reopen test_reopen.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc libfiles.cc $ATARI
# T160: the A1200's Volume setting and the menu's SHIFT+ / SHIFT- (C64 '{' '}', USB '+' '_' '-')
run volume test_volume.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc $ATARI
# T189 S7: the generic Tone setting (the register write 0x51, the .cfg, a new session, own Tone / no_tone / own_volume)
run tone test_tone.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc $ATARI
# T239.3 base 3.3: the application's own settings (look, keyboard joystick, USB mouse) and the core hooks of a library's table "corehooks"
run baseset test_baseset.cc amiga.cc nes.cc ines.cc $ATARI
# T239.3 D32, P2-4 QC: the rows of the seven extend functions are rows of the base UI TOML (coreapp/base_ui.toml): whole, placed, and the code's own tables agree with them
run baserows test_baserows.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc $ATARI
# T239.3 base 3.3, a core whose code is a library (core_hooks.h): the record names it (hooks), the base takes its table of that library through the real loader, calls the descriptor's hooks
# (init, release, warm_reset, cold_start, media_flush, media_pending) and serves the [mount] slots through it; the records and library files are made by the real tools
CL="$T/corelib_fx"
python3 -I "$TST/gen_corelib.py" "$CL" "$ROOT"
RARGS="$CL" CC_EXTRA="$CL/desc.rec $CL/desc_nohooks.rec $CL/desc_upper.rec $CL/desc_empty.rec $CL/desc_long.rec $CL/cmodel.u2a $CL/cnohooks.u2a $CL/cmajor2.u2a" \
  run corelib test_corelib.cc overlay.cc libload.cc libmenu.cc descrec.cc libsvc.cc hooklib.cc nes.cc ines.cc $ATARI
# T164 WP11: the host's master volume for every core (the generic item, the register write, the toast on every page)
# T104.1 Q10: the capability list the loader asks
# a tab that silently drops the last of its entries: the golden would differ, and so must the count check
# T184 D3: the one WiFi row of every core (test_desc.cc): a core without it, and a core with a second row, are both seen
# T104.2 D10: tasks and media list by group like settings, an information tab names its lines

run menuctl test_menuctl.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc nes.cc ines.cc libfiles.cc $ATARI
# T180 D3, D6, D7 (S4-S6): the F3 Library of a core that is not the C64 - no [Recent]/[Favorites], the file operations of the common part, + / - paging with the volume on SHIFT.
run f3 test_f3.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc nes.cc ines.cc libfiles.cc $ATARI
# GitHub #93 (T209 "File Browser, n entries"): the Library and the copy/move folder pick list a folder of 784 / 3000 entries to its end; a heap budget cuts the list and says so
run library_n test_library_n.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc nes.cc ines.cc libfiles.cc $ATARI
# #38: the name limit back to 55 (the prefill and the typing), the field without scrolling, the operation's argument back to 55
# T180 D8: a cursor in every entry - the editor is wired in (CRSR left dropped), inserts at the cursor, draws it where it is
# T180 #38: a file with a FAT long name (80 and 255 characters) in the F3 Library: listed, the sheet, mounted, renamed, copied, moved, deleted by its whole name. Controls: the entry
# back at 55 characters, the path buffers back at 260, the list row not cut with "..", the path argument of an operation back at 255, the clipboard path cut, a folder frame cut.

# T107: the Picture setting, the layout that follows a mode, and the start-up path that uses the same layout
PICSRC="desc_check.cc menutree.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc $ATARI nes.cc ines.cc $C64"
# T164 S10 (WP15): the EDID into the row mask, the best row (Q6), the list without an EDID (Q9); the row table against host/hdmi_modes.vh
run edid test_edid.cc edid.cc
run sid_decide test_sid_decide.cc
run picture test_picture.cc $PICSRC c64scpu.cc cbm_run.cc freeze.cc
# T164 WP16 (G2, G14): the gateware is found by its magic and a requested mode is waited for by the status register
run gwstatus test_gwstatus.cc videomode.cc edid.cc
# T164 WP16 (S10, S11, S12, Q7): the list, the screen that changes, default and override, confirm or revert
run pictfollow test_pictfollow.cc $PICSRC
# T164 D10c amended (Alex 2026-11-04): the Auto value names its mode and whose it is; plain "Auto" has to fail, and so does a first value that a stored label no longer finds

# T107 D7/D8: a core whose gateware owns the picture (the A1200): real frame per mode, Integer without the cap / Fit / Fill, the Scale item for that core only
SCSRC="desc_check.cc menutree.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc amiga.cc $ATARI nes.cc ines.cc"
run scale test_scale.cc $SCSRC
# T107 5.9: the menu laid out again with the mode: no relayout, the origin not centred, a fractional factor
# T164 D10a: the compact canvas below 960 wide: the old fixed 960 x 540 canvas at 800 x 600 / 576p, and a canvas the port is not told of

run noflash test_noflash.cc
control noflash_guard fsif.h 's/(path\[6\] == 0 || path\[6\] == .\/.)/true/' test_noflash.cc

# T104.3 D4: the dialogs an application asks for (appdlg.cc): the keys, the pickers, the bar, the cancel that answers as no screen would
run appdlg test_appdlg.cc appdlg.cc

# T104.1 D1, D2, D6: the registry of operations, state and events, the executor with the core lock, the contributor's poll
run ops test_ops.cc ops.cc exec.cc service.cc
run sidfile test_sidfile.cc sidfile.cc

run service test_service.cc service.cc ops.cc

# T104.1 D16: CoreFs / CoreFile carry 64 bit sizes and offsets; code with a 32 bit size takes it saturated (fs_size32)
run fs64 test_fs64.cc $ROMF atari_atr.cc

# T104.2 (the generic application on the TRX64 Ultimate monolith). Controls break one expression of the product; the bench has to go red.
# The C64 model (c64_model.h) is the bench's reading of ultimate_sys.vhd / trx_sys.v / trx_dbg.v, not the gateware: what the board does
# is the board steps of the spec.
ALLC="$ATARI nes.cc ines.cc board.cc start.cc handover.cc menutree.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc desc_check.cc overlay.cc descrec.cc"

# S2, S4, S11: the profile from the ICAP byte and the hand-over, the capabilities the probes strike, the settings file, the menu without a way back
run board test_board.cc $ALLC
control board_migrate_overwrite start.cc 's/if (c->fs->stat(c->cfg_path, \&size, \&dir) || /if (/' test_board.cc $ALLC
control board_migrate_move start.cc 's/    int got = c->fs->load(old, buf, sizeof(buf));/    int got = c->fs->load(old, buf, sizeof(buf)); c->fs->remove(old);/' test_board.cc $ALLC
control board_cfg_dir board.cc 's|"/flash/%s", core_id);|"/flash/trx64", core_id);|' test_board.cc $ALLC
# T115 7 H1: the held page (R1-R5)
control board_cfg_path start.cc 's/if (!c->cfg_path\[0\] \&\& prof \&\& prof->flash_cfg \&\& prof->cfg_dir\[0\] \&\& c->desc \&\& c->desc->folder) {/if (false) {/' test_board.cc $ALLC
control board_cfg_named_by_store start.cc 's/prof->cfg_dir, c->desc->folder);/prof->cfg_dir, c->desc->store);/' test_board.cc $ALLC

# Run Cartridge (.crt of the Library, the library c64cart): the image, the type, the descriptor, the reset into the cartridge
run c64_cart test_c64_cart.cc c64_cart.cc $BASEL
control cart_flash_banks c64_cart.cc 's/    if (banks < k->flash_banks) {/    if (false) {/' test_c64_cart.cc c64_cart.cc $BASEL
# T154: what a game writes to its cartridge goes back to the .crt (the service, the .crt writer, the C64's flash path). S6 is the event path: without the marks of a program and an erase (the bench's fake gateware still raises them) S1 has to fail.
WBL="c64_cart.cc $BASEL"
run writeback test_writeback.cc $WBL
control wb_s6_program_not_marked c64_cart.cc 's/            wb_mark(off, 1);/            (void)off;/' test_writeback.cc $WBL
control wb_s6_erase_not_marked c64_cart.cc 's/        wb_mark(off, 0x2000);/        (void)off;/' test_writeback.cc $WBL
control wb_no_ack c64_cart.cc 's/^        io->wr(C64_CART_CMD, C64_CART_CMD_INVAL | C64_CART_CMD_ACK);/        (void)0;/' test_writeback.cc $WBL
control wb_program_sets_bits c64_cart.cc 's/            b \&= data;/            b = data;/' test_writeback.cc $WBL
control wb_sector_is_a_bank c64_cart.cc 's/                \*size = p->run\[r\].size;/                *size = 0x4000;/' test_writeback.cc $WBL
control wb_flash_off c64_cart.cc 's/    io->wr(C64_CART_FL_ENABLE, 1);/    (void)0;/' test_writeback.cc $WBL
control wb_irq_not_enabled c64_cart.cc 's/        port->irq_enable(C64_IRQ_FLASH | C64_IRQ_EEPROM, true);/        (void)port;/' test_writeback.cc $WBL
control wb_no_hold c64_cart.cc 's/        port->irq_enable(C64_IRQ_FLASH, !on);/        (void)port;/' test_writeback.cc $WBL
control wb_no_quiet writeback.h 's/#define WB_QUIET_MS    2000/#define WB_QUIET_MS    0/' test_writeback.cc $WBL
control wb_off_writes writeback.cc 's/^    if (g.mode != WB_AUTO || !wb_pending()) {$/    if (!wb_pending()) {/' test_writeback.cc $WBL
control wb_no_flush_before_reset boot.cc 's/^    if (media_writeback(c, why, msg, msglen)) {/    if (false) {/' test_writeback.cc $WBL
control wb_no_flush_before_eject c64_cart.cc 's/^    wb_flush(c, WB_WHY_EJECT, fm, sizeof(fm));                        \/\/ the file first, then the cartridge goes/    (void)fm;/' test_writeback.cc $WBL
control wb_no_bak writeback.cc 's/        ok = fs->rename(g.m.path, bak);/        ok = fs->remove(g.m.path);/' test_writeback.cc $WBL
control wb_image_not_read writeback_crt.cc 's/            m->io->ddr_read(m->img_base + p.img_off, buf, p.size);/            memset(buf, 0, p.size);/' test_writeback.cc $WBL
control wb_blank_gets_packet writeback_crt.cc 's/            blank = buf\[i\] == 0xFF;/            blank = false;/' test_writeback.cc $WBL
control wb_no_new_packet writeback_crt.cc 's/        if (!dirty.test(b) || /        if (true || /' test_writeback.cc $WBL
control wb_unwritable_tried writeback.cc 's/^    if (!target_writable(c)) {$/    if (false) {/' test_writeback.cc $WBL
control wb_eeprom_ignored writeback_crt.cc 's/    bool ee_new = dirty.aux \&\& m->eeprom_read;/    bool ee_new = false;/' test_writeback.cc $WBL
control wb_eeprom_torn c64_cart.cc 's/        if (wr_count(io) == before) {/        if (true) {/' test_writeback.cc $WBL
control wb_eeprom_not_loaded c64_cart.cc 's/^        load_eeprom(io, f, fsize, hdr);/        (void)0;/' test_writeback.cc $WBL
# T239.3 D51: the write-back service for libraries through api->wb: register, report, write now, the safe write; the setting decides; the base's driver's medium is not taken; the operation; the end of the image
run wblib test_wblib.cc libload.cc libsvc.cc descrec.cc overlay.cc libmenu.cc $BASEL
control wb_lib_mark_dropped libsvc.cc 's/            l->wb_svc_->mark(off, len);/            (void)off;/' test_wblib.cc libload.cc libsvc.cc descrec.cc overlay.cc libmenu.cc $BASEL
control wb_ef3_order c64_cart.cc 's/(((a >> 13) \& 7) << 14)/(((a >> 13) \& 7) << 13)/' test_writeback.cc $WBL
control wb_ef3_window c64_cart.cc 's/^        g_image_max = IMAGE_MAX_EF3;/        g_image_max = IMAGE_MAX;/' test_writeback.cc $WBL
control wb_ef3_caps c64_cart.cc 's/(C64_XCAPS_8MB | C64_XCAPS_EF3)) != (C64_XCAPS_8MB | C64_XCAPS_EF3) ||/(C64_XCAPS_8MB | C64_XCAPS_EF3)) == 0x7F ||/' test_writeback.cc $WBL
control wb_ef3_evt_hi c64_cart.cc 's/ | (hi << 21)) \& (g_flash->chip_size - 1)/) \& (g_flash->chip_size - 1)/' test_writeback.cc $WBL
control wb_ef3_chip_erase c64_cart.cc 's/        size = p->chip_size;/        size = 0x10000;/' test_writeback.cc $WBL
control wb_failed_write_dropped writeback.cc 's/^        end(false);$/        end(true);/' test_writeback.cc $WBL
control wb_ask_no_forget writeback.cc 's/^        wb_forget();$/        (void)0;/' test_writeback.cc $WBL
WBM="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc nes.cc ines.cc $ATARI"
run writeback_menu test_writeback_menu.cc $WBM
# T192.3 leak 4 (persistence): the answer "no" of the module's question keeps the file as it is
control wbm_answer_ignored writeback.cc 's/wb_answer(c, a.i(0) != 0, o.msg, o.msglen)/wb_answer(c, true, o.msg, o.msglen)/' test_writeback_menu.cc $WBM

# D6: the address prompt of "Load PRG and start from"
run prompt test_prompt.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $ATARI

# D5, S5: w == 0 means the core draws its own picture

# T104.2 Q1: the flash stays closed to the application except for the one settings file
control noflash_exception fsif.h 's/        if (e.path\[i\]\[0\] \&\& !strcasecmp(path, e.path\[i\])) {/        if (fs_path_is_flash(path)) {/' test_noflash.cc
control noflash_exact fsif.h 's/e.path\[i\]\[0\] \&\& !strcasecmp(path, e.path\[i\])/e.path[i][0] \&\& !strncasecmp(path, e.path[i], 20)/' test_noflash.cc
control noflash_refuse fsif.h 's/return fs_path_is_flash(path) \&\& !fs_flash_rw() \&\& !fs_flash_exception(path);/return false;/' test_noflash.cc
control noflash_stale_kept fsif.h 's/        e.path\[i\]\[0\] = 0;//' test_noflash.cc
control noflash_boot_mc_exact fsif.h 's/!strcasecmp(path, FS_FLASH_BOOT_MULTICORE)/!strncasecmp(path, FS_FLASH_BOOT_MULTICORE, 12)/' test_noflash.cc
control noflash_deep_path fsif.h 's/ || strchr(slash + 1, .\/.))/)/' test_noflash.cc
control noflash_boot_mc_missing fsif.h 's/if (!strcasecmp(path, FS_FLASH_BOOT_MULTICORE)) {/if (false) {/' test_noflash.cc
control board_bootcfg board.cc 's/    snprintf(b->boot_cfg, sizeof(b->boot_cfg), "\/flash\/%s\/boot.cfg", core_id);/    snprintf(b->boot_cfg, sizeof(b->boot_cfg), "%s", FS_FLASH_BOOT_MULTICORE);/' test_board.cc $ALLC

# T89 Apple //e disk: 6-and-2 nibblizer (golden from the RWTS definition + an independent POSTNIB decoder), file orders, the track window service
A2D="apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc"
run apple2_disk test_apple2_disk.cc $A2D
control a2d_no_flush apple2_disk.cc '/T180 D4: written back first/s/if (a2_flush(c, drive, msg, msglen))/if (false)/' test_apple2_disk.cc $A2D
A2="apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc $BASEL"
run apple2 test_apple2.cc $A2

# T91 (the ZX Spectrum on the generic application): the tape converter is a unit of each app that has a deck: tape_zx, tape_cpc and tape_pet below (each core's own copy)


# T83 (the VIC-20 on the generic application): the descriptor against a model of the register block and the ROM aperture; the PRG job (cbm_run.cc) is shared with the 264
VC20="vc20.cc cbm_run.cc cbm_prg.cc freeze.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $DRV $BASEL"
vc20app run vc20 test_vc20.cc $VC20
# The VIC-20's cases inside the shared tests (test_desc, test_ledcores, test_hotkeys, test_createflow, test_memops, test_media_mounts_engines, test_descgen, gen_romreq) stand under TEST_APP_VC20
# and run as the units vc20_* at the end of this file, built as the VIC-20's app: its lane of the matrix runs them (T239.5 D4, D10), the old pipeline does not (D1).

# T90 (the Commodore 264 on the generic application): the descriptor against a model of the register block and the 16 KB aperture
C264="c264.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
run c264 test_c264.cc $C264

# T182 step 8: the C64SCPU descriptor - S2 (no SuperCPU ROM, no start; refused files; the gate) and S7 (speed and JiffyDOS reach SCPU_SW; the drive's ROM)
C64SCPU="c64scpu.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
run c64scpu test_c64scpu.cc $C64SCPU
# the C128's System Mode, the JiffyDOS kit files and the register page E: the units c128 and c128pic, built as its app (below)

# T104 5 (the Amiga and the A1200 on the generic application): the engines the records are served by, each against a model of the core
AM="amiga.cc $BASEL"
run winslot test_winslot.cc $AM
control win_overflow winslot.cc 's/if (c->io->rd(w->dlog_status) \& 2) {/if (false) {/' test_winslot.cc $AM
control win_idle winslot.cc 's/now - f.last_ms >= w->idle_flush_ms || //' test_winslot.cc $AM
control win_maxdirty winslot.cc 's/ || now - f.first_ms >= w->max_dirty_ms//' test_winslot.cc $AM
control win_eject_flush winslot.cc 's/^    if (f.mounted \&\& !flush_drive(c, n)) {/    if (false) {/' test_winslot.cc $AM

run sectors test_sectors.cc $AM
control sec_stage_write sectors.cc 's/c->io->ddr_read(s->stage_off, sec, SEC_BYTES);/memset(sec, 0, SEC_BYTES);/' test_sectors.cc $AM
control sec_idle sectors.cc 's/now - h.last_write_ms >= s->idle_sync_ms/false/' test_sectors.cc $AM
control sec_eject_sync sectors.cc 's/^    bool ok = h.file->sync();/    bool ok = true;/' test_sectors.cc $AM

# T180 D4/D1 (S1, S7): every path that lets go of a writable medium writes back first, completely; a failed write-back keeps the medium, stops the action and says why;
# a mount on an occupied slot lets the old medium go the same way. The A1200 record on its model: a floppy, a hard disk image and a folder disk with writes no sync has taken yet.
MWB="test_media_writeback.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
run media_writeback $MWB
control mwb_return_host engines.cc 's/if (media_writeback(c, WB_WHY_CORE, o.msg, o.msglen)) {/if (wb_flush(c, WB_WHY_CORE, o.msg, o.msglen)) {/' $MWB
control mwb_reset_writes_nothing boot.cc 's/    if (media_writeback(c, why, msg, msglen)) {/    if (false) {/' $MWB
control mwb_reset_goes_on boot.cc 's/printf("\[core\] write-back failed, the action stops: %s\\n", msg);/printf("write-back failed: %s\\n", msg); return 0;/' $MWB
control mwb_all_win engines.cc 's/if (!win_flush_all(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' $MWB
control mwb_all_sec engines.cc 's/if (!sec_sync_all(c, m, sizeof(m)) \&\& !bad++) {/if (false \&\& sec_sync_all(c, m, sizeof(m))) {/' $MWB
control mwb_win_eject winslot.cc 's/if (f.mounted \&\& !flush_drive(c, n)) {/if (f.mounted \&\& (flush_drive(c, n), false)) {/' $MWB
control mwb_win_unsynced winslot.cc 's/(f.ndirty == 0 \&\& !f.unsynced)/f.ndirty == 0/' $MWB
control mwb_win_replace winslot.cc 's/    if (eject_slot(c, n, msg, msglen)) {           \/\/ the old disk/    if (false) {                                   \/\/ the old disk/' $MWB
control mwb_sec_eject sectors.cc 's/^    if (!sync_unit(c, u, msg, msglen)) {$/    if (!sync_unit(c, u, msg, msglen) \&\& false) {/' $MWB
control mwb_sec_replace sectors.cc 's/    if (eject_unit(c, u, msg, msglen)) {              \/\/ T180 D1/    if (false) {                                      \/\/ T180 D1/' $MWB
control mwb_saving engines.h 's/if (c_ \&\& c_->saving_depth++ == 0) {/if (false) {/' $MWB

# T180 D2/D5 (S2, S3, S8): "Persist medium mounts" keeps the mounts in the .cfg and the next start mounts them before the release; a missing medium leaves its slot empty with a
# note; a media set is the same keys in <core folder>/media/<name>.mnt, loaded with a write-back first and one reset. The A1200 record again, a .cfg and a core folder in the fake file system.
MNT="test_media_mounts.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
run media_mounts $MNT
# T198 S1, S7, S8, S10, S11, S14, S15: the configs (save, start with one, load, legacy media sets, no folder, many files)
run configs test_configs.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM
CFG="test_configs.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
control cfg_save_inplace mediamounts.cc 's/export_config(tmp, pk, pv, count, &ns)/export_config(path, pk, pv, count, \&ns)/' test_configs.cc ${CFG#test_configs.cc }
control cfg_save_no_swap mediamounts.cc 's/        ok = c->fs->rename(tmp, path);/        ok = true;/' test_configs.cc ${CFG#test_configs.cc }
control cfg_no_recovery mediamounts.cc 's/        configlist_fix_plan(&fix, n, G::get, raw);/        fix.n = 0;/' test_configs.cc ${CFG#test_configs.cc }
control cfg_load_rebind mediamounts.cc 's/    c->settings->rebind(path);/    (void)path;/' test_configs.cc ${CFG#test_configs.cc }
control mnt_not_stored mediamounts.cc 's/    if (!c->mounts_ready || !persist_on(c)) {/    if (true) {/' $MNT
control mnt_off_acts mediamounts.cc 's/    if (c->settings \&\& persist_on(c)) {/    if (c->settings) {/' $MNT
control mnt_off_keys_stay mediamounts.cc 's/    if (!persist_on(c)) {/    if (false) {/' $MNT
control mnt_eject_not_stored winslot.cc 's/^        mounts_changed(c, false, n);                \/\/ T180 D2.*$/        (void)0;/' $MNT
# T180 #38: a floppy image with a FAT long name picked in the Library into DF0: the whole path reaches the mount; the status line shows the cut form. Controls: the entry at 55 characters,
# the window's name cut without the "..".
control mnt_set_writeback mediamounts.cc 's/    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {/    if (false) {/' $MNT
# T180 D4 for the engines with their own flush (CoreDesc::media_flush / media_pending): the common path asks them on every exit, each descriptor hands its engine over, and each
# engine writes back before it lets the medium go (eject, replacement, reset, reload, restart), completely, and keeps the medium when it cannot
control mwb_hook_flush engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' $MWB
control mwb_hook_pending engines.cc 's/ || (d \&\& d->media_pending \&\& d->media_pending(c))//' $MWB
DESCL="test_desc.cc desc_check.cc menutree.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc"
# Apple II: the eject and the replacement stop on a flush that fails, the sync is not forgotten, a track the file did not take is kept and written again
control a2d_flush_ignored apple2_disk.cc 's/if (a2_flush(c, drive, msg, msglen)) {/if (a2_flush(c, drive, msg, msglen) \&\& false) {/' test_apple2_disk.cc $A2D
control a2d_sync_ignored apple2_disk.cc 's/if (!d->file->sync()) {/if (!d->file->sync() \&\& false) {/' test_apple2_disk.cc $A2D
control a2d_kept_dropped apple2_disk.cc 's/    if (r < 0) {/    if (false) {/' test_apple2_disk.cc $A2D
control a2d_kept_retry apple2_disk.cc 's/g_kept\[drive\], d->kept_bits) < 0) {/g_kept[drive], d->kept_bits) < -1) {/' test_apple2_disk.cc $A2D
control a2d_saving apple2_disk.cc 's/MediaSaving saving(c, d->kept || d->needs_sync || (/MediaSaving saving(c, 0 \&\& (/' test_apple2_disk.cc $A2D
control a2d_hdd_sync_ignored apple2_hdd.cc 's/if (!u->file->sync()) {/if (!u->file->sync() \&\& false) {/' test_apple2_disk.cc $A2D
# Atari: a sync the medium refuses leaves the drive dirty and the disk in the drive
control a8_flush_ignored atari_media.cc 's/if (!st->sio.flush_drive(drive, msg, msglen)) {/if (!st->sio.flush_drive(drive, msg, msglen) \&\& false) {/' test_atari_media.cc $ATARI
control a8_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_atari_media.cc $ATARI
# the drive library of the Commodore cores: written back before eject and replacement, a track the file did not take is written again, an unlinked disk stops nothing
vc20app control drv_mount_settle d1541_drive.cc '/T180 D1, D4: what the head wrote/s/settle();/(void)0;/' test_cbm1541.cc $VC20
vc20app control drv_eject_settle d1541_drive.cc '/T180 D4: written back (again) before the gate asks/s/settle();/wait_for_writeback();/' test_cbm1541.cc $VC20
vc20app control drv_failed_forgotten d1541_drive.cc 's/            failed_tr_ |= 1ull << tr;/            (void)tr;/' test_cbm1541.cc $VC20
vc20app control drv_unlinked_stops d1541_drive.cc 's/state_ != DS_NONE \&\& state_ != DS_CLOSED \&\& unsaved_/state_ != DS_NONE \&\& unsaved_/' test_cbm1541.cc $VC20
vc20app control drv_unsaved_ignored d1541_drive.cc 's/ \&\& !(state_ != DS_NONE \&\& state_ != DS_CLOSED \&\& unsaved_);/;/' test_cbm1541.cc $VC20
vc20app control drv_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_cbm1541.cc $VC20
# the saves of the SNES and the Neo Geo: in the file before the game is let go, the game stays when it cannot be written
control snes_replace_flush snes.cc '/T180 D1, D4: the old game/s/if (snes_media_flush(c, msg, msglen))/if (false)/' test_snes.cc $SNES
control snes_flush_fails snes.cc '/^static int snes_media_flush/,/^}/s/if (flush_save(c, false, msg, msglen)) {/if (flush_save(c, false, msg, msglen) \&\& false) {/' test_snes.cc $SNES
control neo_leaving_flush neogeo.cc 's/    if (neo_media_flush(c, msg, msglen)) {            \/\/ T180 D1, D4/    if (false) {                                      \/\/ T180 D1, D4/' test_neogeo.cc $NEOG
control neo_failed_forgotten neogeo.cc 's/st->save_failed = true;/(void)0;/' test_neogeo.cc $NEOG
control neo_unchanged_stops neogeo.cc 's/return st->loaded \&\& st->save_path\[0\] \&\& (st->save_failed || st->dirty_seen || (c->io->rd(NEO_STATUS) \& NEO_ST_DIRTY));/return st->loaded \&\& st->save_path[0];/' test_neogeo.cc $NEOG

# T180 D2/D5 for every core (S2, S3, S8): the engines with a mount state of their own (Apple II, Atari, CPC, ZX tape, the drive library of the VC-20 / C128 / C264 / C64, the cartridge ports,
# the NES / SNES / Neo Geo game) keep their mounts in the .cfg, restore them BEFORE the release, leave a missing medium empty with a note, and take a media set in and out with one reset.
# The descriptors are the real ones on models of their register blocks. Every engine tells the application (mount_notify) from its own mount and eject.
MMEL="${DESCL#test_desc.cc }"
MME="test_media_mounts_engines.cc $MMEL start.cc handover.cc"
run media_mounts_engines $MME
control mme_a2_mount apple2_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2$/    (void)0;/' $MME
control mme_a2_eject apple2_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: with.*$/    (void)0;/' $MME
control mme_a2_path apple2_disk.cc 's/^    snprintf(d->path, sizeof(d->path), "%s", path);$/    (void)0;/' $MME
control mme_a2_hdd_mount apple2_hdd.cc 's/^    mount_notify(c, A2_DRIVES + unit); *\/\/ T180 D2$/    (void)0;/' $MME
control mme_a2_hdd_eject apple2_hdd.cc 's/^    mount_notify(c, A2_DRIVES + unit); *\/\/ T180 D2: the slots.*$/    (void)0;/' $MME
control mme_a8_disk atari_media.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: the slots.*$/    (void)0;/' $MME
control mme_a8_cart atari_media.cc 's/^    mount_notify(c, SIO_DRIVES); *\/\/ T180 D2$/    (void)0;/' $MME
control mme_a8_cart_reg atari_media.cc 's/^    a8_write_cart_reg(c); *\/\/ the machine is held.*$/    (void)0;/' $MME
control mme_a8_xex atari_media.cc 's/"xex %s", path/"%s", path/' $MME
control mme_drive_mount d1541_drive.cc 's/^    changed(); *\/\/ T180 D2$/    (void)0;/' $MME
control mme_drive_eject d1541_drive.cc 's/^    changed(); *\/\/ T180 D2: the empty drive is stored$/    (void)0;/' $MME
control mme_drive_eject_empty d1541_drive.cc 's/^        changed(); *\/\/ T180 D2: an eject of the empty.*$/        (void)0;/' $MME
control mme_drive_modes cbm1541.cc 's/m == MM_RO ? "ro " : //' $MME
control mme_drive_hook cbm1541.cc 's/    d->on_change(drive_changed, c);/    (void)drive_changed;/' $MME
control mme_c64_cart c64_cart.cc 's/^    mount_notify_name(c, "Cartridge"); *\/\/ T180 D2$/    (void)0;/' $MME
control mme_c264_cart c264.cc 's/^    mount_notify(c, 1); *\/\/ T180 D2: the slots.*$/    (void)0;/' $MME
control mme_c264_model c264.cc 's/        c->settings->set(mi, 1); *\/\/ Plus\/4/        (void)mi;/' $MME
control mme_nes_eject nes.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $MME
control mme_snes_eject snes.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $MME
control mme_neo_eject neogeo.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $MME
control mme_drive_fail d1541_drive.cc 's/^        changed(); *\/\/ T180 D2: the old disk is out and the drive is empty$/        (void)0;/' $MME
control mme_c64_giveup c64_cart.cc 's/^    mount_notify_name(c, "Cartridge"); *\/\/ T180 D2: told by the name of the slot.*$/    (void)0;/' $MME
control mme_nes_fail nes.cc 's/^        mount_notify(c, 0); *\/\/ the old program is gone and the slot is empty (T180 D2)$/        (void)0;/' $MME
control mme_snes_fail snes.cc 's/^        mount_notify(c, 0); *\/\/ the old game is gone and the slot is empty (T180 D2)$/        (void)0;/' $MME
control mme_neo_fail neogeo.cc 's/^        mount_notify(c, 0); *\/\/ the old game is gone and the slot is empty (T180 D2)$/        (void)0;/' $MME
control mme_neo_folder neogeo.cc 's/"folder %s", arg/"%s", arg/' $MME
control mme_engine_spec mediamounts.cc 's/    return ms->spec(c, idx - nwin(c) - nsec(c));/    return 0;/' $MME
control mme_engine_mount mediamounts.cc 's/        return c->desc->mount_slots->mount(c, idx - nwin(c) - nsec(c), spec, msg, msglen);/        return 1;/' $MME
control mme_engine_eject mediamounts.cc 's/        return c->desc->mount_slots->eject(c, idx - nwin(c) - nsec(c), msg, msglen);/        return 1;/' $MME
control mme_engine_changed mediamounts.cc 's/    changed_idx(c, nwin(c) + nsec(c) + n);/    (void)n;/' $MME
control mme_writeback mediamounts.cc 's/    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {/    if (false) {/' $MME
control mme_not_ready mediamounts.cc 's/^    c->mounts_ready = true;$/    (void)0;/' $MME

# T180: desc_check refuses a table of slots that cannot be keys of the .cfg

# T128: a folder as an Amiga hard disk (docs/specs/T128-folder-hdd.md). One program for S1-S8 and S10 with an independent reader and writer of FFS volumes
# (folderhd_bench.h); S9 is tests/coreapp/fmfs64/.
FHD="test_folderhd.cc $AM"
run folderhd $FHD
# S1: the disk the gateware accepts
# S2: a valid FFS volume
# S3: offsets beyond 4 GiB
# S4: names, dates, protection
# S5: data written in place waits for the metadata that owns the block (D5 changed, spec 5.5 no. 2)
control fh_nopatch folderhd_fold.cc 's/cc == 1 \&\& consistent/false/' $FHD
control fh_consistent folderhd_fold.cc 's/consistent = ffs_rd(rb, FL_BM_FLAG) == 0xFFFFFFFFu;/consistent = true;/' $FHD
# S6: the folder follows
control fh_swap_sync winslot.cc 's/^    if (f.mounted \&\& !flush_drive(c, n)) {/    sec_sync_all(c); if (f.mounted \&\& !flush_drive(c, n)) {/' $FHD
control fh_nofold folderhd_fold.cc 's/if (gen == folded_gen) {/if (true) {/' $FHD
control fh_rename folderhd_fold.cc 's/p.ops |= OP_REN;/(void)0;/' $FHD
control fh_delete folderhd_fold.cc 's/p.ops = OP_DEL;/p.ops = 0;/' $FHD
control fh_content folderhd_fold.cc 's/int cc = content_changed(me, h);/int cc = 0;/' $FHD
control fh_mkdir folderhd_fold.cc 's/if (!fs->mkdir(path)) {/if (false) {/' $FHD
control fh_sidecar folderhd_fold.cc 's/if (p.protect == 0 \&\& cl == 0) {/if (true) {/' $FHD
control fh_scratch folderhd_fold.cc 's/if (!keep \&\& xr_ok) sr_remove_range(run_b, run_n);/(void)0;/' $FHD
control fh_link folderhd_fold.cc 's/if (!isdir \&\& h.sectype != FFS_ST_FILE) {/if (false) {/' $FHD
control fh_abort folderhd_fold.cc 's/if (phase >= PH_WALK \&\& phase <= PH_MATERIAL \&\& gen != job_gen) {/if (false) {/' $FHD
control fh_bmflag folderhd_fold.cc 's/if (ffs_rd(rb, FL_BM_FLAG) != 0xFFFFFFFFu) {/if (false) {/' $FHD
control fh_limbo folderhd_fold.cc 's/if (!(p.ops \& OP_HOP)) continue;/continue;/' $FHD
# S7: loss
control fh_inplace folderhd_fold.cc 's/tmp_path(p.tmp_seq, out, n);/path_of(p.node, out, n);/' $FHD
control fh_repair folderhd_fold.cc 's/nodes\[r.node\].hsize = r.size;/(void)0;/' $FHD
control fh_absent folderhd_fold.cc 's/bool gone = fs->remove(path) || host_absent(path);/bool gone = fs->remove(path) || true;/' $FHD
# S8: limits and the memory
# S10: the poll is not held longer than the slice
control fh_slice folderhd_fold.cc 's/if (now - t0 + longest >= FH_SLICE_MS) {/if (false) {/' $FHD

run regset test_regset.cc $AM
# T138 S3: the A1200 offers the Zorro III board (FAST_CFG 4), the A500 does not

run mousesink test_mousesink.cc $AM
# T141: the USB keyboard as raw Amiga key events (S3 the keys, S4 each key once / the menu / the register's room)
run rawkeys test_rawkeys.cc $AM
# rk_menukey is gone (T141 D5): SCROLL LOCK, the one menu key left, has no Amiga code, so dropping it from the menu keys sends nothing either
# T159 S1/S2: case LED 1 = the machine's drive LED (the selector written once)
run ledload test_ledload.cc $AM

# T190 (#51): Mouse sensitivity / acceleration in the Amiga cores: the rows, a +10 report scaled as each setting says, the restart applying the stored values
run mousescale test_mousescale.cc amiga.cc $BASEL
control scale_store settings.cc 's/^    return write_cfg();$/    return true;/' test_mousescale.cc amiga.cc $BASEL
t_mousescale() {
  python3 -I "$TST/test_mousescale.py" > /dev/null
  return 0
}
ca_in shared && cc_block mousescale t_mousescale
PASS=$((PASS + 1))

# T104.1 5.6: the mouse library, a report stream into the Amiga's counters and the C64's 1351, and the keys-only UI
run mouselib test_mouselib.cc amiga.cc $BASEL
t_mousenav() {
  python3 -I "$TST/test_mousenav.py" > /dev/null
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
ca_in shared && cc_block mousenav t_mousenav
PASS=$((PASS + 1))

run romset test_romset.cc $AM

# the descriptor checks of the new records
DESCS="desc_check.cc menutree.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc"

# T104 5.15: the Amiga 500 runs the generic application; its record against the register decode of the A500 gateware
run amiga500 test_amiga500.cc $AM
# T191: the clock: the setting not bound to AM_RTC_EN, loaded again at every release, the nibbles written without the loader hold, the weekday dropped

# T97 5.10: the picture window follows the display window, in the application (fbwin.h)
run fbwin test_fbwin.cc $AM

# S12: the image has to fit the flash slot the boot ROM starts it from; the script ci/trxapps.sh runs on the built coreapp.app
t_appsize() {
  python3 -I "$TST/test_appsize.py"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
ca_in shared && cc_block appsize t_appsize
PASS=$((PASS + 1))

# T184 D3 S3: the host's "WiFi settings" reach the core application read only (hostwifi.cc against the real ConfigManager, run_hostwifi.sh)
t_hostwifi() { bash "$TST/run_hostwifi.sh" "$T"; }
ca_in shared && cc_block hostwifi t_hostwifi
# T184 R6: the core's WiFi state words (netops.cc st_wifi / st_wifi_line use wifi_state_word) are Off / No module, never the driver's Disabled / No module detected
run wifistate test_wifistate.cc
PASS=$((PASS + 1))

# T104.1 D15 (cbm1541): the disk in memory, then the drive library behind the service layer on the VIC-20 descriptor
IMG="d1541_gcr.cc"
run d1541_image test_d1541_image.cc $IMG

# T104.1 D15 scope: a .d64 / .d81 as a folder (the read only file system) and the browser, name prompt and Mount & Run of a PRG inside an image
FSL="d1541_fs.cc d1541_gcr.cc"
run d1541_fs test_d1541_fs.cc $FSL

# T180 #38: fs_short_name, CoreFs::list_short and the write-back of a medium with a 255 character file name
run longnames test_longnames.cc $AM
control longnames_side writeback.cc 's/size_t keep = lb + le > FS_NAME_MAX ? FS_NAME_MAX - le : lb;/size_t keep = lb;/' test_longnames.cc $AM

vc20app run cbm1541 test_cbm1541.cc $VC20
# The drive library's 1581 tests (d1541_wd.cc, the block drive_wd) run on the VIC-20's descriptor against a table that is the VIC-20's plus drive_wd (vc20_drv.sh, merge_a as the app's link does it).
# No shipped app has that table: this is a test of the library, not of an app; the 1581 in the shared app is tested against the shared table by test_c64scpu, and in the C128's app against its own table by the unit c128 (a .d81 in drive A).
INC_VC20_SAVE=("${INC_VC20[@]}"); INC_VC20=(-I"$T/vc20wd/drv" "${INC_VC20[@]}")
vc20app run cbm1541_wd test_cbm1541.cc $VC20
INC_VC20=("${INC_VC20_SAVE[@]}")
# T180 #38: disk images with a FAT long name picked into the drive: the DiskFs finds the image of a long path, the drive keeps the whole name and path. Controls: the container path
# buffer at 260, the name at 56, the mount path at 260.
# T104 5.14 Reload core: the label, the check before the cut, the order of the sequence, what the new core is told again
vc20app control drv_busy d1541_drive.cc 's/if ((r(DR_STATUS) \& DR_STAT_WRITEBUSY) \&\& drive_track == tr) {/if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_flush d1541_drive.cc 's/^    wait_for_writeback();/    (void)0;/' test_cbm1541.cc $VC20
vc20app control drv_discard d1541_drive.cc 's/    if (discard_armed_) {/    if (true) {/' test_cbm1541.cc $VC20
vc20app control drv_unsaved d1541_drive.cc 's/if (any \&\& !ok) {/if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_exists newfile.cc 's/^        if (fs->stat(full, \&sz, \&isdir)) {/        if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_bare newfile.cc 's/^            if (fs->stat(full, \&sz, \&isdir)) {/            if (false) {/' test_cbm1541.cc $VC20


# T122: Create blank DSK / ADF / ATR / DSK on the Apple IIe, the Amiga 500 and 1200, the Atari 800 XL and the CPC. The images are read back by
# checkers written from the format definitions; the entries are the real descriptors' tasks, run by the menu.
NDSRC="atari800.cc atari_media.cc atari_sio.cc atari_atr.cc atari_xex.cc atari_cart.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc amiga.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $BASEL"
run newdisk test_newdisk.cc $NDSRC
control nd_exists newfile.cc 's/^        if (fs->stat(full, \&sz, \&isdir)) {/        if (false) {/' test_newdisk.cc $NDSRC
control nd_bare newfile.cc 's/^            if (fs->stat(full, \&sz, \&isdir)) {/            if (false) {/' test_newdisk.cc $NDSRC


# T104 5.16: fast seek. The real FatFs (vendor/gideon chan_fat) built with coreapp/ffconf/ffconf.h - the application's own configuration - over a
# RAM disk; the policy of coreapp/src/fastseek.cc under test. Controls: the map never built (argv 0), and five defects in the policy.
FFS="$ROOT/vendor/gideon/software/chan_fat/ff14/source"
FSD="$ROOT/vendor/gideon/software/filesystem"
t_fastseek() {
  if [ -f "$FFS/ff.c" ]; then
    mkdir -p "$T/fsk/stub"
    printf '#include <stddef.h>\n#include <stdlib.h>\ntypedef void *SemaphoreHandle_t;\n#define pdTRUE 1\n' > "$T/fsk/stub/FreeRTOS.h"
    printf '#define xSemaphoreCreateMutex() ((SemaphoreHandle_t)1)\n#define vSemaphoreDelete(s) ((void)(s))\n#define xSemaphoreTake(s, t) 1\n#define xSemaphoreGive(s) ((void)(s))\n' > "$T/fsk/stub/semphr.h"
    FSKINC=(-I"$ROOT/coreapp/ffconf" -I"$T/fsk/stub" -I"$FFS" -I"$FSD" -I"$SRC")
    # the application's ffconf.h = the original plus FF_USE_FASTSEEK, and nothing else
    printf '#include "ffconf.h"\n#if FF_USE_FASTSEEK != 1\n#error the coreapp ffconf.h does not enable fast seek\n#endif\n' > "$T/fsk/chk1.c"
    cc -fsyntax-only "${FSKINC[@]}" "$T/fsk/chk1.c"
    printf '#include "ffconf.h"\n#if FF_USE_FASTSEEK != 0\n#error the original ffconf.h is not the 0 this check expects\n#endif\n' > "$T/fsk/chk0.c"
    cc -fsyntax-only -I"$ROOT/vendor/gideon/software/chan_fat/full" -I"$T/fsk/stub" -I"$FFS" "$T/fsk/chk0.c"
    for c in ff ffunicode ffsystem; do
      cc -w "${FSKINC[@]}" -c -o "$T/fsk/$c.o" "$FFS/$c.c"
    done
    fsk_build() {   # fsk_build OUT FASTSEEK.cc
      c++ -std=gnu++11 -Wall "${FSKINC[@]}" -o "$1" "$TST/test_fastseek.cc" "$2" "$T/fsk/ff.o" "$T/fsk/ffunicode.o" "$T/fsk/ffsystem.o"
    }
    fsk_build "$T/test_fastseek" "$SRC/fastseek.cc"
    "$T/test_fastseek"
  else
    echo "SKIPPED test_fastseek: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  fi
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
ca_in shared && cc_block fastseek t_fastseek
PASS=$((PASS + 1))

# T122 §5 (board, Atari 800, 2026-10-24): a clean name in the prompt for every core, the folder browser of Create (where it starts, that the chosen
# folder is the folder), the file written in chunks with a percentage on screen, cancel, and no half image.
CFSRC="$NDSRC cbm_run.cc cbm_prg.cc $DRV"
run createflow test_createflow.cc $CFSRC
# (a) the prompt
# (b) the folder
# (c) the progress
control cf_chunk newfile.h 's|^#define NF_CHUNK 16384u|#define NF_CHUNK 0x7FFFFFFFu|' test_createflow.cc $CFSRC
control cf_remove newfile.cc 's|^        fs_->remove(full_); .*|        (void)0;|' test_createflow.cc $CFSRC

# T104.1 D3, D4, D8, D9, D12: the menu merge, the description record, the bundle's records, REST and WS JSON-RPC.
# The fixtures are made by the real packer (tools/cor_pack.py, tools/descrec.py): the readers of the board are tested against what the tools write.
python3 "$TST/gen_bundle.py" "$T/bundles"
BUN="$T/bundles"
CARGS="$BUN/desc_test.bin $BUN/desc_own.bin" RARGS="$BUN/desc_test.bin $BUN/desc_own.bin"
run descrec test_descrec.cc overlay.cc descrec.cc
run overlay test_overlay.cc overlay.cc video.cc videomode.cc edid.cc video_stub.cc                # T256: the overlay service with a fake layer (S1, S7, S9, S10)
# T104.2 D10: the tables of the core's own menu, the groups of the tabs, the lines of an information tab, the libraries merged at pack time

CONSRC="overlay.cc descrec.cc $PICSRC"
CARGS="$BUN/desc_test.bin $BUN/desc_default.bin" RARGS="$CARGS"
run contrib test_contrib.cc $CONSRC

BUNSRC="overlay.cc descrec.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc handover.cc $PICSRC"   # handover.cc: bundle.cc reads the hand-over (T239.3 D17)
CARGS="$BUN" RARGS="$BUN"
run bundle test_bundle.cc $BUNSRC
run firststart test_firststart.cc ${BUNSRC/appindex_stub.cc/appindex.cc}   # TRX64-Ultimate finding: a sub-app that record 8 installs is listed at the first start
# T239.3 D17: the data description of every core that has one (cores/*/app/desc.toml, found by gen_bundle.py's glob) compiled by the real tool, made into a descriptor by the generic
# reader (from the record and from a bundle by start_identify), and compared field by field with the compiled-in descriptor of the same core; a damaged record or an unknown section
# is refused by name. It links every core, as the uicover walk does, and the application's real table under another name (compiled_table.cc) next to an empty one for the start.
ALLCORES="amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64"
CARGS="$BUN/app" RARGS="$CARGS"
run descgen test_descgen.cc compiled_table.cc $ALLCORES freeze.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc overlay.cc descrec.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc desc_check.cc
# T239.3 D17 part 2: the tables of the sections 7 .. 17 (the Amiga 1200's, from golden/desc_sections.toml, and what no descriptor has together, golden/desc_sections_extra.toml)
CARGS="$BUN/desc_sections.bin $BUN/desc_sections_extra.bin" RARGS="$CARGS"
run descsec test_descsec.cc amiga.cc $C64 $BASEL overlay.cc descrec.cc
# T239.3 base 3.3: the key table, the register windows (and the registers the base drives itself) and the job flag of a task, from golden/desc_keys.toml as the real tool compiles it
CARGS="$BUN/desc_keys.bin" RARGS="$CARGS"
run deckeys test_deckeys.cc overlay.cc descrec.cc
# T239.3 P1-1 (D30, D34): TOML A, the record of version 7, from golden/desc_toml_a.toml and the SDK's module maps as the real tool compiles them
CARGS="$BUN/desc_toml_a.bin" RARGS="$CARGS"
run desctoml test_desctoml.cc overlay.cc descrec.cc

# T239.3 D32: where the rows the application adds stand in the menu: the base UI TOML's table, a core's routes and places over it (golden/desc_places.toml as the real tool compiles it), the menu built from both
CARGS="$BUN/desc_places.bin" RARGS="$CARGS"
run uiplace test_uiplace.cc overlay.cc descrec.cc $PICSRC

# T239.3 P1-4 (D30 condition 2, D31): the service's start check of ID and build against the record's identity, the same fixture, a model of the standard block
CARGS="$BUN/desc_toml_a.bin" RARGS="$CARGS"
run identity test_identity.cc $BUNSRC
CARGS="$BUN" RARGS="$BUN"
# T104.2 D10: a library merged at pack time is not added again, the application's own record serves a start without a .cor and never beats the .cor's, the record's own menu replaces the descriptor's tables

SVC="uic_json.cc uic_sha.cc uic_svc.cc uic_net.cc svcjson.cc svc.cc httpreq.cc rest.cc rpc.cc rpccontract.cc wsproto.cc"
RESTSRC="$SVC $PICSRC"
unset CARGS RARGS
run rest test_rest.cc $RESTSRC
# T192.1 MVP: the UI service on the real menu - ids, intents through the key path, questions, the token on the routes
# T192.1 D21: the API is generated from the declaration; this walks the declaration of every core and asserts every row, setting value and operation is reachable
run uicover test_uicover.cc uiapp.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc table.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 overlay.cc descrec.cc $SVC
# T192.3 S6 / S15: every key of the menu on every core of the table: one hash per (core, tab, start state), the golden made before the intent cut; the controller as a client of the key table reproduces it
RARGS="$BUN/desc_cut.bin" run keywalk test_keywalk.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc table.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc instfiles.cc instcore.cc cbm_run.cc $C64 overlay.cc descrec.cc
# T192.3: Timewarp's and the monitor's keys of the record menu are operations, the file manager module on its own, DEL deletes nothing, the neighbour takes a vanished row's place, the table is whole
RARGS="$BUN/desc_cut.bin" run cut test_cut.cc overlay.cc libload.cc libmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc table.cc amiga.cc $ATARI nes.cc ines.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc zxnext.cc snes.cc neogeo.cc snes_rom.cc neoset.cc c264.cc c64scpu.cc portdiag.cc xinput.cc diagtest.cc cbm_run.cc $C64 descrec.cc
run uiapp test_uiapp.cc uiapp.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc nes.cc ines.cc libfiles.cc $SVC $ATARI

# T162: the core application's bug report - sections, the descriptor's named registers (and no others), the file, GET /v1/report, no secrets
CRSRC="$RESTSRC corereport.cc bugreport.cc amiga.cc apple2.cc apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc overlay.cc descrec.cc"   # descrec.cc: T228 R8
run corereport test_corereport.cc $CRSRC
control cr_overwrite corereport.cc 's|if (c->fs->stat(path, \&sz, \&dir)) {|if (false) {|' test_corereport.cc $CRSRC
control cr_no_folder corereport.cc 's|if (c->fs->mkdir(dirs\[i\])) {|if (true) {|' test_corereport.cc $CRSRC
control rs_setop settings_ops.cc 's/    bool saved = c->settings->set(i, v);/    bool saved = true; (void)v;/' test_rest.cc $RESTSRC

# T104.1 (Alex 2026-10-26): the memory of every core as machine.read_mem / machine.write_mem
unset CARGS RARGS
MEMSRC="$SVC $DESCS"
run memops test_memops.cc $MEMSRC

# T156: USB gamepads. The HID parser on descriptors written to the spec (none of them is the 8BitDo SN30 Pro's: its bytes are not known yet), the pad
# service on the real NES and SNES descriptors, the mapping wizard in the real menu. Controls: one defect each, the same test built against it must fail.
PADSRC="snes.cc snes_rom.cc nes.cc ines.cc neogeo.cc neoset.cc $ATARI"
run hidpad test_hidpad.cc hidpad.cc
RARGS="$BUN/desc_own.bin" run pad test_pad.cc $PADSRC overlay.cc descrec.cc                   # descrec.cc: T228 R10, the menu record's pad (golden/desc_own.toml)
run xinput test_xinput.cc xinput.cc $PADSRC     # T196: XInput pads (claim, 360 and GIP reports through the pad service, start sequences)
control pad_cfgkeys settings.cc 's/if (!strncasecmp(key, "Pad ", 4)) {/if (false) {/' test_pad.cc $PADSRC overlay.cc descrec.cc
run padmenu test_padmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $PADSRC
# T192.3 leak 1 (persistence): the wizard module saves the map through pad_save_map and tells the truth about it
control pad_wiz_not_saved padwiz.cc 's/bool saved = c->settings \&\& pad_save_map(c->settings, g_player);/bool saved = false;/' test_padmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $PADSRC
control pad_wiz_saved_lie padwiz.cc 's/bool saved = c->settings \&\& pad_save_map(c->settings, g_player);/bool saved = true;/' test_padmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $PADSRC

# T142 (board, 2026-10-29): the board's printf (small_printf.cc) has no precision: "%.*s" / "%.8s" print wrong text, and the Mac's printf is
# fine with them, so no test sees it. No format string of the application may carry one (textcopy.h nf_copy copies instead).
printf_lint() {   # printf_lint FILE... : one line for every format with a string precision
  local f
  for f in "$@"; do
    sed 's#//.*$##' "$f" | grep -nE '"[^"]*%[-0-9]*\.(\*|[0-9]+)s' | sed "s#^#$f:#" || true
  done
}
if ca_in shared; then   # T174 D3: a check of the shared sources, in the group `shared` only
bad="$(printf_lint "$SRC"/*.cc "$SRC"/*.h "$CORES"/*.cc "$CORES"/*.h)"
if [ -n "$bad" ]; then echo "printf lint: a string precision the board's printf does not know:" >&2; echo "$bad" >&2; exit 1; fi
fi
# T148: the core folder (S1-S4, S7): made at the first start, settings moved in, ROMs from the folder alone, copied verified from the old places
CF="start.cc handover.cc amiga.cc $C64 $BASEL"
run corefolder test_corefolder.cc $CF
# T148 D13: the start waits for the medium of the hand-over path before the core folder (and so before the .cor reopen and the libraries)
MW="mediumwait.cc corefolder.cc sha256_cc.cc"
run mediumwait test_mediumwait.cc $MW
control cf_no_saves corefolder.cc 's/        !corefolder_sub(cor_path, "saves", s, sizeof(s))) {/        false) {/;s/    ok = ok \&\& fs->mkdir(s);//' test_corefolder.cc $CF
control cf_cfg_keep_old corefolder.cc 's/    fs->remove(oldp);//' test_corefolder.cc $CF
control cf_cfg_overwrite corefolder.cc 's/if (fs->stat(newp, \&size, \&dir) || !fs->stat/if (!fs->stat/' test_corefolder.cc $CF
control cf_cfg_noverify corefolder.cc 's/ || fs->load(newp, b, sizeof(b)) != got || memcmp(a, b, (size_t)got) != 0//' test_corefolder.cc $CF
control cf_copy_noverify corefolder.cc 's/    if (back != got || memcmp(want, have, 32) != 0) {/    if (false) {/' test_corefolder.cc $CF
control cf_copy_keepbad corefolder.cc 's/        fs->remove(dst);//' test_corefolder.cc $CF
control cf_overwrite romfind.cc 's/        lack\[i\] = !have;/        lack[i] = true;/' test_corefolder.cc $CF
control cf_start_cfg start.cc 's/if (!corefolder_cfg(c->cor_path, c->cfg_path, sizeof(c->cfg_path))) {/if (!handover_cfg_path(c->cor_path, c->cfg_path, sizeof(c->cfg_path))) {/' test_corefolder.cc $CF

# T148 D7: the ROM table the host application's maintenance page reads is the descriptors' RomSpecs; a stale copy is a red run
t_romreq() {
  build "$T/gen_romreq" "$TST/gen_romreq.cc" $DESCS
  "$T/gen_romreq" > "$T/romreq_shared.inc"
  # the host's table also holds the rows of cores that are not in the shared table (the VIC-20's app has its own): the shared app's rows must all be in it, in their order
  python3 -I - "$T/romreq_shared.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the shared app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
shared = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in shared) and shared else 1)
PY
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
ca_in shared && cc_block romreq t_romreq
PASS=$((PASS + 1))

# T180 D9 (#43): "Unzip here" in every core from the one copy in <dir of the .cor>/apps. The fixtures are made by the real packer from the real app.toml
# and the Mac build of apps/unzip runs inside the real loader over the FakeFs; the test needs no RISC-V instruction.
python3 -I "$TST/gen_unzip.py" "$T/unzipfx" "$ROOT"
UZ="$T/unzipfx"; ATARI_NOSTUB="${ATARI/appindex_stub.cc/appindex.cc}"   # the real index instead of the stub the base links
UZSRC="overlay.cc descrec.cc libmenu.cc bundle.cc mediumwait.cc handover.cc libload.cc libscan.cc unzip.c miniz.c miniz_tinfl.c miniz_zip.c miniz_lib.cc u2p_compat.c glue.cc desc_check.cc menutree.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc $ATARI_NOSTUB nes.cc ines.cc $C64"
CC_EXTRA="$UZ/unzip.u2a $UZ/unzip2.u2a $UZ/miniz.u2a $UZ/ziplist.u2a $UZ/otherlib.u2a $UZ/pollib.u2a $UZ/menulib.u2a $UZ/hooklib.u2a $UZ/plain.u2a $UZ/ringlib.u2a $UZ/ring2lib.u2a $UZ/ringapp.u2a $UZ/t.zip $UZ/manifest.txt"
CARGS="$UZ" RARGS="$UZ"
run unzip test_unzip.cc $UZSRC

# T186 (S1-S3): the FTP client against a scripted server model (test_ftp.cc, lwIP fakes in ftpstub/, virtual clock); fixes adapted from the fork f7f665ab
run ftp test_ftp.cc ftp_client.cc ftp_exports.cc

# T239.5 D4, D10: the VIC-20's cases of the shared tests (TEST_APP_VC20), built as its app (vc20app: its own table and its core only, the group vc20) - the units its
# TOML B (cores/vc20/app/sdk.toml) names for the steps 8 to 10 of its lane. The sources are the shared units' generic ones with the VIC-20 in place of the shared cores.
VC20D="desc_check.cc overlay.cc descrec.cc $VC20"
CARGS="" RARGS=""   # the run arguments of the units above (the unzip fixtures) are no argument of these
RARGS="$T/bundles/desc_own.bin" vc20app run vc20_desc test_desc.cc $VC20D
vc20app run vc20_ledcores test_ledcores.cc $VC20D
vc20app run vc20_hotkeys test_hotkeys.cc $VC20D
vc20app run vc20_createflow test_createflow.cc $VC20D
vc20app run vc20_mounts test_media_mounts_engines.cc $VC20D start.cc handover.cc
RARGS="$BUN/app" vc20app run vc20_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $VC20D
vc20app run vc20_memops test_memops.cc $SVC $VC20D
# the VIC-20's rows of the host's ROM table (t_romreq above checks the shared app's rows only): gen_romreq built as the VIC-20's app, each of its rows in the host's table
t_romreq_vc20() {
  local INC=("${INC_VC20[@]}")
  build "$T/gen_romreq_vc20" "$TST/gen_romreq.cc" $VC20D
  "$T/gen_romreq_vc20" > "$T/romreq_vc20.inc"
  python3 -I - "$T/romreq_vc20.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the VIC-20's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test vc20_romreq: PASS"
  return 0
}
if ca_in vc20 && ca_unit vc20_romreq; then t_romreq_vc20; PASS=$((PASS + 1)); fi

# T213.3 P3, T239.5 D4: the Amiga 1200's units, built as its app (a12app: its library cores/amiga1200/app/lib and its own table, the group amiga1200) - the units its TOML B
# (cores/amiga1200/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: amiga1200.cc and the base's (no amiga.cc, no other core).
A12="amiga1200.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
A12D="desc_check.cc overlay.cc descrec.cc $A12"
a12app run a12 test_amiga500.cc $A12
a12app run a12_memops test_memops.cc $SVC $A12D
RARGS="$T/bundles/desc_own.bin" a12app run a12_desc test_desc.cc $A12D
RARGS="$BUN/app" a12app run a12_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $A12D
a12app run a12_ledcores test_ledcores.cc $A12D
a12app run a12_hotkeys test_hotkeys.cc $A12D
a12app run a12_createflow test_createflow.cc $A12D
# the Amiga 1200's row of the host's ROM table: gen_romreq built as the A1200's app, its rows in the host's table
t_romreq_a12() {
  local INC=("${INC_A1200[@]}")
  build "$T/gen_romreq_a12" "$TST/gen_romreq.cc" $A12D
  "$T/gen_romreq_a12" > "$T/romreq_a12.inc"
  python3 -I - "$T/romreq_a12.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Amiga 1200's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test a12_romreq: PASS"
  return 0
}
if ca_in amiga1200 && ca_unit a12_romreq; then t_romreq_a12; PASS=$((PASS + 1)); fi
a12app run a12_folderhd test_folderhd.cc $A12
a12app run a12_mouselib test_mouselib.cc $A12
a12app run a12_base test_a12base.cc $A12
CARGS="$BUN/desc_sections.bin $BUN/desc_sections_extra.bin" RARGS="$CARGS" a12app run a12_descsec test_descsec.cc overlay.cc descrec.cc $A12
a12app run a12_romset test_romset.cc $A12
a12app run a12_regset test_regset.cc $A12
a12app run a12_joyswap test_joyswap.cc $A12
a12app run a12_mousescale test_mousescale.cc $A12
a12app run a12_fbwin test_fbwin.cc $A12
a12app run a12_scale test_scale.cc desc_check.cc $A12   # T213.3: the picture items and their apply on the A1200's record (the shared unit scale runs them on the A500's)
a12app run a12_rawkeys test_rawkeys.cc $A12
a12app run a12_ledload test_ledload.cc $A12

# T239.5 D4, D16: DDT Look Ma, No VIC's units, built as its app (ddtapp: its library cores/ddt_novic/app/lib and its own table, the group ddt_novic) - the units its TOML B
# (cores/ddt_novic/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: ddt_novic.cc and the base's (no other core).
DDT="ddt_novic.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
DDTD="desc_check.cc overlay.cc descrec.cc $DDT"
CARGS="" RARGS=""
ddtapp run ddt test_ddt.cc $DDT
ddtapp run ddt_memops test_memops.cc $SVC $DDTD
RARGS="$T/bundles/desc_own.bin" ddtapp run ddt_desc test_desc.cc $DDTD
RARGS="$BUN/app" ddtapp run ddt_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $DDTD
ddtapp run ddt_hotkeys test_hotkeys.cc $DDTD

# T239.5 D4, D16: the ZX Spectrum's units, built as its app (zxapp: its library cores/zx/app/lib with its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h, its own table, the
# group zx) - the units its TOML B (cores/zx/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: zx.cc, zx_snap.cc, the tape deck and the base's.
# The snapshot reader is compared with tools/zx_snap.py's reader on a corpus made here, in a temporary directory: no snapshot is ever committed.
ZXC="$T/zxcorpus"; mkdir -p "$ZXC"; python3 "$ROOT/tools/zx_snap.py" corpus "$ZXC"; export ZX_CORPUS="$ZXC"
ZXS="zx.cc zx_snap.cc tape_pulses.cc tape_deck.cc $BASEL"
ZXA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $ZXS"
ZXD="desc_check.cc overlay.cc descrec.cc $ZXA"
CARGS="" RARGS=""
zxapp run zx test_zx.cc $ZXS
zxapp run zx_snap test_zx_snap.cc zx_snap.cc
zxapp run boot_zx test_boot_zx.cc $ZXS
zxapp run tape_zx test_tape.cc tape_pulses.cc
zxapp run zx_memops test_memops.cc $SVC $ZXD
RARGS="$T/bundles/desc_own.bin" zxapp run zx_desc test_desc.cc $ZXD
RARGS="$BUN/app" zxapp run zx_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $ZXD
zxapp run zx_hotkeys test_hotkeys.cc $ZXD
zxapp run zx_mounts test_media_mounts_engines.cc $ZXD start.cc handover.cc
# the ZX Spectrum's rows of the host's ROM table: gen_romreq built as the Spectrum's app, its rows in the host's table
t_romreq_zx() {
  local INC=("${INC_ZX[@]}") APPLIB="$ZXLIB"
  build "$T/gen_romreq_zx" "$TST/gen_romreq.cc" $ZXD
  "$T/gen_romreq_zx" > "$T/romreq_zx.inc"
  python3 -I - "$T/romreq_zx.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the ZX Spectrum's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test zx_romreq: PASS"
  return 0
}
if ca_in zx && ca_unit zx_romreq; then t_romreq_zx; PASS=$((PASS + 1)); fi

# T239.5 D4, D16: the CPC's units, built as its app (cpcapp: its library cores/cpc/app/lib with its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h, its own table, the group cpc) -
# the units its TOML B (cores/cpc/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: cpc.cc, cpc_disk.cc, the tape deck and the base's.
# The descriptor, the two DSK drives with their write-back and the tape, against a model of the core.
CPCS="cpc.cc cpc_disk.cc tape_pulses.cc tape_deck.cc $BASEL"
CPCA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $CPCS"
CPCD="desc_check.cc overlay.cc descrec.cc $CPCA"
CPCM="test_media_mounts_engines.cc $CPCD start.cc handover.cc"
CARGS="" RARGS=""
cpcapp run cpc test_cpc.cc $CPCS
cpcapp run cpc_menu test_cpc_menu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $CPCS
cpcapp run tape_cpc test_tape.cc tape_pulses.cc
cpcapp run cpc_memops test_memops.cc $SVC $CPCD
RARGS="$T/bundles/desc_own.bin" cpcapp run cpc_desc test_desc.cc $CPCD
RARGS="$BUN/app" cpcapp run cpc_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $CPCD
cpcapp run cpc_hotkeys test_hotkeys.cc $CPCD
cpcapp run cpc_ledcores test_ledcores.cc $CPCD
cpcapp run cpc_joyswap test_joyswap.cc $CPCD
cpcapp run cpc_mounts $CPCM
cpcapp run cpc_newdisk test_newdisk.cc newdisk.cc newfile.cc $CPCD
cpcapp run cpc_createflow test_createflow.cc newdisk.cc newfile.cc $CPCD
cpcapp control cpc_disk_take cpc_disk.cc 's/            c->io->wr(dirty_reg\[drive\] + m, b);    \/\/ taken.*$/            (void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_idle cpc_disk.cc 's/now - d.last_ms >= CPCD_IDLE_MS || //' test_cpc.cc $CPCS
cpcapp control cpc_disk_maxdirty cpc_disk.cc 's/ || now - d.first_ms >= CPCD_MAX_DIRTY_MS//' test_cpc.cc $CPCS
cpcapp control cpc_disk_short cpc_disk.cc 's/uint32_t len = disk.size - off < CPC_DIRTY_CHUNK ? disk.size - off : CPC_DIRTY_CHUNK;/uint32_t len = CPC_DIRTY_CHUNK;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_eject cpc_disk.cc 's/^    cpcd_poll(c, s, false);   .*$/    (void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_retry cpc_disk.cc 's/now - d.try_ms >= 1000/false/' test_cpc.cc $CPCS
cpcapp control cpc_disk_mountflush cpc_disk.cc 's/if (d.mounted \&\& !flush_drive(c, d, drive)) {/if (false) {/' test_cpc.cc $CPCS
cpcapp control cpc_disk_marks cpc_disk.cc 's/c->io->wr(dirty_reg\[drive\] + m, 0xFF);/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_flushinit cpc.cc 's/    cpcd_flush_all(c, \&st->disks);   .*$/    (void)0;/;s/^static int cpc_media_flush.*$/static int cpc_media_flush(CoreCtx *, char *, int) { return 0; }/' test_cpc.cc $CPCS
cpcapp control cpc_unsynced_forgotten cpc_disk.cc 's/disk.unsynced = true;/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_mount_poll cpc_disk.cc '/T180 D1: the last marks/s/cpcd_poll(c, s, false);/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_cpc.cc $CPCS
cpcapp control mme_cpc_mount cpc_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: the slots are A, B, Tape$/    (void)0;/' $CPCM
cpcapp control mme_cpc_eject cpc_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2$/    (void)0;/' $CPCM
cpcapp control mme_cpc_eject_empty cpc_disk.cc 's/^        mount_notify(c, drive); *\/\/ T180 D2: an eject of the empty.*$/        (void)0;/' $CPCM
cpcapp control mme_cpc_model cpc.cc 's/int mi = c->media_restoring \&\& c->settings/int mi = false \&\& c->settings/' $CPCM
cpcapp control mme_cpc_fail cpc_disk.cc 's/^            mount_notify(c, drive); *\/\/ the old disk is out and the drive is empty$/            (void)0;/' $CPCM
cpcapp control mme_tape_notify tape_deck.cc 's/^    mount_notify(c, r.slot); *\/\/ T180 D2$/    (void)0;/' $CPCM
cpcapp control mme_tape_fail tape_deck.cc 's/^            mount_notify(c, r.slot); *\/\/ the old tape is out and the deck is empty$/            (void)0;/' $CPCM
# the CPC's rows of the host's ROM table: gen_romreq built as the CPC's app, its rows in the host's table
t_romreq_cpc() {
  local INC=("${INC_CPC[@]}") APPLIB="$CPCLIB"
  build "$T/gen_romreq_cpc" "$TST/gen_romreq.cc" $CPCD
  "$T/gen_romreq_cpc" > "$T/romreq_cpc.inc"
  python3 -I - "$T/romreq_cpc.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the CPC's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test cpc_romreq: PASS"
  return 0
}
if ca_in cpc && ca_unit cpc_romreq; then t_romreq_cpc; PASS=$((PASS + 1)); fi

# T239.5 D4, D16: the PET's units, built as its app (petapp: its library cores/pet/app/lib with its own tape_deck.*, tape_pulses.* and hk_defaults.h, its own table, the group pet) - the units its
# TOML B (cores/pet/app/sdk.toml) names for the steps 2, 8 and 9 of its lane. The sources are the app's own: pet.cc, the tape deck, the PRG library (cbm_run.cc, cbm_prg.cc, c64_go.cc) and the base's.
PETS="pet.cc tape_pulses.cc tape_deck.cc cbm_run.cc cbm_prg.cc c64_go.cc $BASEL"
PETA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $PETS"
PETD="desc_check.cc overlay.cc descrec.cc $PETA"
CARGS="" RARGS=""
petapp run pet_models test_pet_models.cc cbm_prg.cc
petapp run tape_pet test_tape.cc tape_pulses.cc
RARGS="$T/bundles/desc_own.bin" petapp run pet_desc test_desc.cc $PETD
RARGS="$BUN/app" petapp run pet_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $PETD
petapp run pet_hotkeys test_hotkeys.cc $PETD
petapp run pet_cut test_pet_cut.cc $PETD
# the PET's rows of the host's ROM table: gen_romreq built as the PET's app, its rows in the host's table
t_romreq_pet() {
  local INC=("${INC_PET[@]}") APPLIB="$PETLIB"
  build "$T/gen_romreq_pet" "$TST/gen_romreq.cc" $PETD
  "$T/gen_romreq_pet" > "$T/romreq_pet.inc"
  python3 -I - "$T/romreq_pet.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the PET's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test pet_romreq: PASS"
  return 0
}
if ca_in pet && ca_unit pet_romreq; then t_romreq_pet; PASS=$((PASS + 1)); fi

# T239.5 D4, D16: the C128's units, built as its app (c128app: its library cores/c128/app/lib with its own copies of hk_defaults.h and drive_rom_pick.h, its own table with its own drive window, the group c128) -
# the units its TOML B (cores/c128/app/sdk.toml) names for the steps 2, 8 and 9 of its lane. The sources are the app's own: c128.cc, the PRG library (cbm_run.cc, cbm_prg.cc), Freeze, the drive library and the base's.
C128S="c128.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
C128X="$C128S menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc"   # T185 D4: the bench also drives the file selector of the settings rows
C128A="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $C128S"
C128D="desc_check.cc overlay.cc descrec.cc $C128A"
C128M="test_media_mounts_engines.cc $C128D start.cc handover.cc"
CARGS="" RARGS=""
# T113 5.5 (the Commodore 128 on the generic application): the descriptor against a model of register page E and the DDR2 window; the extra keys are pure
c128app run c128 test_c128.cc $C128X
# T194 S3: the C128's two pictures on the one layer (the VDC, and the VIC-IIe's capture in format 3): Integer / Fit / Fill, 1080p and 720p, 50 and 60 Hz family, Smoothing, the switch both ways
c128app run c128pic test_c128pic.cc $C128X
RARGS="$T/bundles/desc_own.bin" c128app run c128_desc test_desc.cc $C128D
RARGS="$BUN/app" c128app run c128_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $C128D
c128app run c128_hotkeys test_hotkeys.cc $C128D
c128app run c128_ledcores test_ledcores.cc $C128D
c128app run c128_joyswap test_joyswap.cc $C128D
c128app run c128_mounts $C128M
# the C128's rows of the host's ROM table: gen_romreq built as the C128's app, its rows in the host's table
t_romreq_c128() {
  local INC=("${INC_C128[@]}") APPLIB="$C128LIB"
  build "$T/gen_romreq_c128" "$TST/gen_romreq.cc" $C128D
  "$T/gen_romreq_c128" > "$T/romreq_c128.inc"
  python3 -I - "$T/romreq_c128.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the C128's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test c128_romreq: PASS"
  return 0
}
if ca_in c128 && ca_unit c128_romreq; then t_romreq_c128; PASS=$((PASS + 1)); fi

pjob_drain
[ ! -e "$T/ca_bad" ] || { echo "tests/coreapp: a unit lists a file of a core outside its group (above)" >&2; exit 1; }
for u in $CA_UNITS; do
  grep -qx "$u" "$T/ca_units_done" 2>/dev/null || { echo "tests/coreapp: $u (CA_UNITS) is no unit of this harness in the group $CA_GROUP" >&2; exit 1; }
done
echo "tests/coreapp: $PASS test programs passed, $PJ_N parallel controls (JOBS=$JOBS), every negative control failed as it must"
