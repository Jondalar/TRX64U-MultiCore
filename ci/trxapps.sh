#!/usr/bin/env bash
# Stage `trxapps`: our own two RISC-V applications, built here instead of on the Mac (T94 P6).
#   host-app/  -> hostapp.app / hostapp.elf   the boot menu that loads a core from a .cor (T101)
#   coreapp/   -> coreapp.bin / coreapp.elf   the blueprint a core runs (T93), one image for every blueprint core,
#                 and a copy per blueprint core under coreapps/<core>.app, the name pack_cores_mc.sh wants; a core with a TOML B of its own
#                 (cores/<id>/app/sdk.toml, SDK_OWN_APPS) gets no copy but its app linked from the SDK package (tools/sdk_core_app.sh), under the same name
#   apps/miniz      -> miniz.u2a      the zip library unzip.u2a imports (T197), shipped beside it
#   apps/unzip      -> unzip.u2a      Unzip to folder / Unzip here / Extract one file on .zip (T104.3, T180 D9): shipped in the package and the release, the
#                 one copy every core reads from <root>/cores/apps/ (coreapp/src/appindex.h)
#   apps/trex_crt   -> trex_crt.u2a   Dump / Inspect / Flash cartridge and Write to Cartridge on .crt (T228 R21: FW's trex_crt on our app ABI), shipped like unzip
#   apps/softboot   -> softboot.u2a   Run as Softboot on .cfw/.ue2/.u64, offered off the MultiCore host only (T228 DC1), shipped like unzip
#   apps/online     -> online.u2a     the Assembly64 / CommoServe search (T239.3 L-3): a resident sub-app (format 3, needs base 3.3) with the tab F3 Online and the table "online"; a core carries it in its .cor
#   app/lib/testlib -> testlib.u2a            the board's test library (T104.1 D10): a .u2a of format 3, built with the
#                 image's riscv32-unknown-elf compiler; copied to <stick>/trx/lib/ it is loaded at the next core start
#
# Until this stage existed the core applications came from the generic package trx64-coreapps, uploaded
# by hand from a Mac, and the host app for an update file was whatever lay in a local result/ directory.
# Alex, 2026-10-15: "Bau bitte per Pipeline, das sind laute manuelle krücken gerade" (translated: build it
# with the pipeline, these are loud manual crutches). So: same image and toolchain as the app stage had (removed 2026-11-01), same
# ccache, and the result is cached by the content of the trees plus the vendor/gideon software tree - the
# third-party parts (lwip, lvgl, FreeRTOS, chan_fat, httpd) are links into our copy of Gideon's software, so its
# tree belongs in the key.
#
# T174 D1: this stage BUILDS images and, since T239.3 D17, the SDK package with its check (tools/sdk_check.sh builds the example library and runs its host test; whether
# that test belongs in the stage `render` is Alex's decision, open) - minutes, and package and update wait for it. The host tests, the render benches and the
# 64 bit file benches are the stage `render` (ci/render.sh) with a key of its own; they no longer sit in front of the package.
# Incremental: the build runs in a persistent tree per branch (/cache/multicore/build/ci-<branch>, tools/treesync.py copies only the files whose
# content changed, so make rebuilds only what changed); the heavy slot is let go as soon as the images are built (R1c).
#
#   ci/trxapps.sh [--clean] [--compile-only]
#                                --clean (or CI_BUILD_CLEAN=1) empties the persistent tree first: a build from scratch
#                                --compile-only: compile and link the two applications and stop (tools/dev.sh build): no images, no stage result
#   TRX_BUILD_INPLACE=1          build in this very tree (tools/dev.sh: the tree is the synced working tree; no image cache, no slot, no sync)
#   TRXAPPS_ONLY=sdk|host        T239.5 (D8, the build step of a lane of the matrix): sdk = the shared base, its libraries, the sub-apps (.u2a) and the SDK package (its
#                                check is the lane's step 7); host = the host app and its emulator build. Unset: everything below (the old pipeline's stage images). The value is in the key.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

clean="${CI_BUILD_CLEAN:-}" compile_only=""
for a in "$@"; do case "$a" in --clean) clean=1 ;; --compile-only) compile_only=1 ;; *) echo "ci/trxapps.sh: unknown argument $a" >&2; exit 2 ;; esac; done
inplace="${TRX_BUILD_INPLACE:-}"
only="${TRXAPPS_ONLY:-}"
case "$only" in ""|sdk|host) ;; *) echo "ci/trxapps.sh: TRXAPPS_ONLY is sdk or host, not $only" >&2; exit 2 ;; esac
part() { [ -z "$only" ] || [ "$only" = "$1" ]; }   # part sdk|host: this run builds that part

out="$(stage_dir trxapps)"
# T174 D2.2/D4 (Alex, 2026-11-07: the app is built and tested natively first, RISC-V last): the parent starts this child only after `apptests` is green.
# The content of the tracked files, not the commit and not tree ids: a documentation file (ci/code_hash.py is_doc) must not cost a rebuild.
# Raw index entries (git ls-files -s: mode, blob id or submodule commit, path) of what `keep` leaves; hash-object cannot hash a submodule (run 2020: host-app/software/FreeRTOS), as before not comment-normalised. app/ is in the key because
# the shared view layer lives there and both applications compile it (T104 D8) - without it a change to
# the shell would be served out of the cache and never reach the board.
key=""; cache=""
if [ -z "$inplace" ]; then
  key="$(cd "$ROOT" && set -o pipefail && git ls-files -s -- host-app coreapp uicore ci/version.sh app tools/sdk_pack.sh tools/sdk_api.py tools/sdk_check.sh tools/check_no_roms.sh tools/u2a_pack.py tools/descrec.py tools/libtab.py tools/core_toml.py tools/sdk_core_app.sh tools/app_build.py tools/rtl_check.py tools/cor_pack.py tools/menusim tools/gen_base_ui.py tools/check_d24.py tools/d24_ledger.txt tests/lvgl_objs.sh tests/overlay_probe.h tests/drv_fake.h tests/parallel.sh tests/coreapp ':(glob)cores/*/app/**' LICENSE apps/unzip apps/miniz apps/trex_crt apps/softboot apps/online cores/apple2/rtl/roms vendor/gideon/software vendor/gideon/tools vendor/gideon/target vendor/gideon/roms > "$out/.keyall" && cut -f2 "$out/.keyall" | python3 ci/code_hash.py keep > "$out/.keynames" && [ -s "$out/.keynames" ] && { awk -F'\t' 'NR==FNR{k[$0];next} ($2 in k)' "$out/.keynames" "$out/.keyall"; printf 'image %s\n' "$(image_id build)"; printf 'own-app %s\n' "$(python3 -I "$ROOT/tools/core_toml.py" list-own-app | tr '\n' ' ')"; } | sha256sum | cut -c1-16)" || { echo "trxapps: stage key failed" >&2; exit 1; }
  rm -f "$out/.keynames" "$out/.keyall"
  key="$key${only:+-$only}"
  cache="${CI_TRXAPPS_CACHE:-/cache/trxapps}/$key"
fi

# The blueprint cores: a descriptor in coreapp/src/table.cc, so the one image serves them (T93 D14).
# `trx64` is not in this list: its core.toml says app = "ultimate", the host runs Gideon's application for it. trx64u is TRX64-Ultimate's (T239.3 D21):
# its application is built there, and this stage builds none for it.
# Every core whose core.toml says blueprint is in the list below. The A1200 ran the generic image from 2026-10-20 (T104) until T213.3: it has its
# own app now, built by its matrix lane (ci/lane.sh core:amiga1200), not by this stage (T239.5 D13). vc20 (since V-4 its own app, SDK_OWN_APPS) and c264 since 2026-10-22 (Alex: "apart from trx64 and c64u,
# switch everything to the generic core app for now", translated), the A500 (`amiga`) since 2026-10-24 (Alex: "the app must also be switched
# to generic!", translated). coreapp/cores/amiga.cc is the A500's source. cores/vc20/app/lib/vc20.cc and coreapp/cores/c264.cc
# replace the machine code of the fork branch t90-c16app.
BLUEPRINT_CORES="${BLUEPRINT_CORES:-atari800 nes apple2 zx cpc zxnext snes neogeo c264 pet amiga ddt_novic diag}"
# T239.3 V-4 (D28 point 3, D31): the cores whose app is linked from the SDK package alone, with the libraries and the TOML A its cores/<id>/app/sdk.toml names (tools/sdk_core_app.sh), not copied from the
# shared image. The VIC-20 is the first (S11); the shared image holds none of its code (no core table entry, no source in the Makefile), the packages' coreapps/vc20.app is the linked app.
# T239.3 V-5 (D25): the stage key holds every cores/*/app tree and the output of list-own-app (it follows the `pipeline` flag of the core.toml files), so a new TOML B or a changed flag is a new key. The list is the cores whose TOML B names a descriptor ([app] desc, tools/core_toml.py list-own-app), not a name written here; a core of it that is also in BLUEPRINT_CORES would get the shared image
# first and the linked app after it, so the stage refuses that, and a core of the list gets exactly one app (checked after the loop below).
SDK_OWN_APPS="${SDK_OWN_APPS:-$(python3 -I "$ROOT/tools/core_toml.py" list-own-app | tr '\n' ' ')}"
for c in $SDK_OWN_APPS; do
  case " $BLUEPRINT_CORES " in *" $c "*) echo "trxapps: $c has a TOML B of its own (SDK_OWN_APPS) and is in BLUEPRINT_CORES: it would be given the shared app (D25)" >&2; exit 1;; esac
done

if [ -n "$cache" ] && [ -f "$cache/DONE" ]; then
  echo "=== trxapps $key: from cache $cache"
  cp -r "$cache"/. "$out/"
  rm -f "$out/DONE"
else
  [ -n "$inplace" ] || heavy_lock
  # B = where the build runs. CI: the persistent tree of this branch, synced from the checkout; in place: this tree.
  B="$ROOT"
  if [ -z "$inplace" ] && [ -d /cache/multicore ] && [ -w /cache/multicore ]; then
    B="${CI_BUILD_TREE:-/cache/multicore/build/ci-$(branch_slug)}"
    echo "=== trxapps $key: building in the persistent tree $B${clean:+ (clean)}"
    python3 -I "$ROOT/tools/treesync.py" "$ROOT" "$B" ${clean:+--clean}
    # the version stamps (rules.mk gitinfo) come from the checkout this run is for, not from the tree
    export GIT_DIR="$ROOT/.git" GIT_WORK_TREE="$ROOT"
  else
    echo "=== trxapps ${key:-in place}: building in $B"
  fi
  app="$B/vendor/gideon"
  _t_b=$SECONDS
  if [ -f /opt/esp/esp-idf/export.sh ]; then
    set +u; . /opt/esp/esp-idf/export.sh >/dev/null; set -u
  fi
  if command -v ccache >/dev/null; then
    export CCACHE_DIR="${CCACHE_DIR:-/ccache}" CCACHE_MAXSIZE=5G CCACHE_BASEDIR="$B"
    mkdir -p "$CCACHE_DIR" /tmp/ccache-shim-trx
    for t in riscv32-unknown-elf-gcc riscv32-unknown-elf-g++ riscv32-unknown-elf-c++; do
      real="$(command -v "$t" || true)"
      [ -n "$real" ] || continue
      printf '#!/bin/sh\nexec ccache %s "$@"\n' "$real" > "/tmp/ccache-shim-trx/$t"
      chmod +x "/tmp/ccache-shim-trx/$t"
    done
    export PATH="/tmp/ccache-shim-trx:$PATH"
  fi
  # hex2bin writes the {dest, length, run} record of an .app; built once for this image, as the local
  # scripts do it, and reached through host-app/tools (TOOLS in target/common/environment.mk).
  if ! { [ -x "$app/tools/hex2bin" ] && head -c4 "$app/tools/hex2bin" | grep -q ELF; }; then
    make -C "$app/tools" hex2bin checksum swap >/dev/null
  fi
  mkdir -p "$B/host-app/tools"
  for t in hex2bin checksum; do ln -sfn "$app/tools/$t" "$B/host-app/tools/$t"; done
  git config --global --add safe.directory "$ROOT" || true
  git config --global --add safe.directory "$B" || true

  # one make parallelism for the whole stage: the libraries and each application (CI_BUILD_JOBS, default: the cores; the objects of one application are
  # independent, only `gitinfo` must not run beside them)
  J="${CI_BUILD_JOBS:-$(nproc)}"
  H="$B/host-app/target"
  for lib in lwip lvgl; do nice -n 15 make -j"$J" -C "$H/libs/riscv/$lib"; done
  # `gitinfo` first and alone: `all` has it beside the compile targets and in parallel the rule rewrites gitinfo.h while a source that
  # includes it is compiled (T93 5.15)
  apps=""; part host && apps="hostapp"; part sdk && apps="$apps coreapp"
  for a in $apps; do
    make -C "$H/u64ii/riscv/$a" gitinfo
    nice -n 15 make -j"$J" -C "$H/u64ii/riscv/$a"
  done

  # T239.3 P3-1 (D31, S8): the symbol check on what the app's TOML B names: every source of its libraries is linked, and no generic object uses a symbol a library defines
  # (the generic application reaches the libraries through the library table only). The file is the TOML B of the shared app.
  ! part sdk || NM=riscv32-unknown-elf-nm python3 -I "$B/tools/libtab.py" symcheck "$B/coreapp/libs/shared.toml" "$H/u64ii/riscv/coreapp/output" "$B"

  if [ -n "$compile_only" ]; then
    echo "=== trxapps --compile-only: both applications compile and link ($((SECONDS - _t_b))s)"
    size "$H/u64ii/riscv/hostapp/result/hostapp.elf" "$H/u64ii/riscv/coreapp/result/coreapp.elf" 2>/dev/null || ls -l "$H/u64ii/riscv/hostapp/result/" "$H/u64ii/riscv/coreapp/result/"
    exit 0
  fi

  # T104.2 S12: the boot ROM starts an application from the slot at 0x220000 and takes at most 0x1E0000 - 12 bytes of it; an image that
  # does not fit would build, flash and not start. The number goes in the log on every build.
  if part sdk; then
    python3 -I "$B/tools/check_app_size.py" "$H/u64ii/riscv/coreapp/result/coreapp.app"
    cp "$H/u64ii/riscv/coreapp/result/coreapp.bin" "$H/u64ii/riscv/coreapp/result/coreapp.elf" "$H/u64ii/riscv/coreapp/result/coreapp.app" "$out/"   # the .app: what ue2emu and tools/trxdbg.sh boot staged take (T104.2)
  fi
  if part host; then
  cp "$H/u64ii/riscv/hostapp/result/hostapp.app" "$H/u64ii/riscv/hostapp/result/hostapp.elf" "$out/"
  # T174 D2.8: the host app for the emulator (tests/hostapp/emu_host.sh): the same sources with HOSTAPP_EMU_FLASH, which only flashfw.cc reads (the emulated
  # flash chip is accepted). Rebuilt from that one object and the link, then the tree is put back to the board build (touch + make again) so a later
  # incremental build and the images above stay what the board gets. Never packed, never flashed: it lives in this stage's hand-over as hostapp_emu.*.
  hdir="$H/u64ii/riscv/hostapp"
  touch "$B/host-app/software/hostapp/flashfw.cc"
  nice -n 15 make -j"$J" -C "$hdir" HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH
  cp "$hdir/result/hostapp.elf" "$out/hostapp_emu.elf"; cp "$hdir/result/hostapp.app" "$out/hostapp_emu.app"
  touch "$B/host-app/software/hostapp/flashfw.cc"
  nice -n 15 make -j"$J" -C "$hdir"
  cmp -s "$hdir/result/hostapp.app" "$out/hostapp.app" || { echo "trxapps: the board build of the host app changed while the emulator build was made" >&2; exit 1; }
  fi
  # The library build path (app/lib/lib.mk) with the compiler of this image. app/ is in the key above, so a change to it rebuilds the stage.
  # Built beside its sources (the outputs are gitignored) and cleaned again - which also keeps the tree equal to the checkout for the next sync.
  # T174 D3: the two library applications are keyed by the content of what they read (their folder, the library makefile, the SDK, the core application
  # headers they include with -I, the packer) and the compiler's version line: an unchanged key copies the .u2a from $CI_TRXAPPS_CACHE/lib-apps/<key>
  # instead of building and cleaning every run (the stage key above is coarser: any change in app/ or coreapp/ rebuilds the stage, but not these two).
  libapp() {   # libapp DIR NAME.u2a : DIR relative to the tree
    local d="$1" n="$2" k c
    k="$( (cd "$ROOT" && git ls-files -s -- "$d" apps/miniz app/lib/app.mk app/lib/lib.mk app/lib/sdkroot.mk app/lib/lib.ld app/sdk coreapp/src tools/u2a_pack.py tools/descrec.py > "$out/.liball" && cut -f2 "$out/.liball" | python3 ci/code_hash.py keep > "$out/.libnames" && [ -s "$out/.libnames" ] && awk -F'\t' 'NR==FNR{k[$0];next} ($2 in k)' "$out/.libnames" "$out/.liball"; rm -f "$out/.libnames" "$out/.liball"; riscv32-unknown-elf-gcc --version | head -1; echo "$d $n") | sha256sum | cut -c1-16)"
    c="${CI_TRXAPPS_CACHE:-/cache/trxapps}/lib-apps/$k"
    if [ -f "$c/$n" ]; then echo "=== trxapps $n: from cache $c (key $k)"; cp "$c/$n" "$out/"; return 0; fi
    nice -n 15 make -j"$J" -C "$B/$d" RV_PREFIX=riscv32-unknown-elf- "$n"
    cp "$B/$d/$n" "$out/"
    make -C "$B/$d" RV_PREFIX=riscv32-unknown-elf- clean
    mkdir -p "$c.tmp$$" 2>/dev/null && cp "$out/$n" "$c.tmp$$/$n" && rm -rf "$c" && mv "$c.tmp$$" "$c" || true
  }
  if part sdk; then
  libapp app/lib/testlib testlib.u2a
  # T180 D9: Unzip, an application of several C files (app/lib/app.mk); same compiler, built beside its sources and cleaned again
  libapp apps/unzip unzip.u2a
  # T197 P1: the zip code is the library (apps/miniz, resident, format 3); unzip.u2a imports it and carries none of it. S2: the size shows it - an application
  # that linked its own miniz would be as large as the library.
  libapp apps/miniz miniz.u2a
  # T228 R21: the cartridge tools and Softboot, applications like Unzip (app/lib/app.mk); no library needed, so nothing to compare
  libapp apps/trex_crt trex_crt.u2a
  libapp apps/softboot softboot.u2a
  # T239.3 L-3: the online search, a sub-app a core carries in its .cor (record 7); shipped beside the others so the packers of the cores can take it
  libapp apps/online online.u2a
  uz=$(wc -c < "$out/unzip.u2a"); mz=$(wc -c < "$out/miniz.u2a")
  echo "=== trxapps: unzip.u2a $uz bytes, miniz.u2a $mz bytes"
  [ "$uz" -lt "$mz" ] || { echo "trxapps: unzip.u2a ($uz bytes) is not smaller than the zip library ($mz): it carries its own miniz (T197 S2)" >&2; exit 1; }
  fi
  if [ -z "$only" ]; then
  mkdir -p "$out/coreapps"
  # The .bin, never the .app: cor_pack puts the image at app_load as it is and jumps to app_entry, so an
  # .app's 12-byte header would sit on the entry point - that is what shipped on 2026-10-15 and left the
  # Atari and the NES executing their own load address (T93 5.12).
  for c in $BLUEPRINT_CORES; do cp "$H/u64ii/riscv/coreapp/result/coreapp.bin" "$out/coreapps/$c.app"; done
  fi
  command -v ccache >/dev/null && ccache --show-stats | head -8 || true
  echo "=== trxapps: images built in $((SECONDS - _t_b))s"

  # T174 R1c: the RISC-V build is done and this stage holds nothing else that needs Vivado's slot: let it go before the cache entry is written
  # (heavy_lock keeps its lock on fd 9 and HEAVY_SLOT; Alex, T100 D14 open item 2). Run 1133 held slot 0 for 36 minutes of tests.
  if [ -n "${HEAVY_SLOT:-}" ]; then exec 9>&-; echo "heavy slot $HEAVY_SLOT released after the build"; unset HEAVY_SLOT; fi

  # T239.3 D17 / P8, D31 / P3-2: the SDK package (tools/sdk_pack.sh) from this build's sources and its base archives (`make sdkbase`: the objects of the sources that depend on nothing a core decides,
  # the libraries, the lists of the rest), after the heavy slot is let go, into the hand-over beside the images (the artifact handoff-trxapps), and its check (tools/sdk_check.sh): the example
  # library, its menu record and the example core's app built from the unpacked package alone. The package carries no app image. The sources of the package are in the stage key above
  # (app/ with app/sdkpkg, the packer tools, the menu simulator and the rig it builds from, the two scripts).
  if part sdk; then
  nice -n 15 make -C "$H/u64ii/riscv/coreapp" sdkbase
  SDK_COMMIT="${SDK_COMMIT:-$(cd "$ROOT" && git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)}" bash "$B/tools/sdk_pack.sh" "$out" "$H/u64ii/riscv/coreapp/output/sdkbase"
  # the package's check; the SDK lane of the matrix runs it as its own step 7 (ci/lane.sh sdk test), not inside the build
  [ "$only" = sdk ] || RV_PREFIX=riscv32-unknown-elf- bash "$B/tools/sdk_check.sh" "$out"/trx64-sdk-*.tar.gz
  # T239.5: the shared app's link map, for the core lanes' S12 check (the library a core's TOML B shares with the shared app is the same, and linked in both)
  cp "$H/u64ii/riscv/coreapp/output/coreapp.map" "$out/"
  fi
  if [ -z "$only" ]; then
  # T239.3 V-4 (S11): the app of each core of SDK_OWN_APPS, linked from the unpacked package alone against its TOML A and TOML B; the check of its link map is in the script. coreapps/<core>.app is the
  # linked image (cor_pack takes the .bin, as for the shared image above), <core>.map beside it.
  for c in $SDK_OWN_APPS; do
    RV_PREFIX=riscv32-unknown-elf- bash "$B/tools/sdk_core_app.sh" "$out"/trx64-sdk-*.tar.gz "$c" "$out/coreapps"
  done
  # V-5: the app of such a core is the linked one and none of the shared image's copies: no blueprint copy of it exists in the hand-over, and its image is not the shared image
  for c in $SDK_OWN_APPS; do
    [ -s "$out/coreapps/$c.app" ] || { echo "trxapps: $c has no linked app in coreapps/" >&2; exit 1; }
    ! cmp -s "$out/coreapps/$c.app" "$H/u64ii/riscv/coreapp/result/coreapp.bin" || { echo "trxapps: coreapps/$c.app is the shared image (D25)" >&2; exit 1; }
  done
  # S12: the cbm1541 library of the VIC-20's TOML B is the one of the shared app's, and both link maps (coreapp.map, vc20.map) have its sources as inputs.
  # trx64u's own image is TRX64-Ultimate's since P1-8 (6696d1028): no build here makes coreapp_trx64u.map any more.
  case " $SDK_OWN_APPS " in *" vc20 "*)
    python3 -I "$B/tools/libtab.py" samelib "$B/coreapp/libs/shared.toml" "$B/cores/vc20/app/sdk.toml" cbm1541 "$H/u64ii/riscv/coreapp/output/coreapp.map" "$out/coreapps/vc20.map";;
  esac
  fi

  if [ -n "$cache" ]; then
    mkdir -p "$cache.tmp.$$"
    cp -r "$out"/. "$cache.tmp.$$/"
    : > "$cache.tmp.$$/DONE"
    rm -rf "$cache"
    mv "$cache.tmp.$$" "$cache"
  fi
fi

printf '%s\n' "${key:-inplace}" > "$out/trxapps_key"
( cd "$ROOT" && git rev-parse HEAD 2>/dev/null || echo inplace ) > "$out/trxapps_commit"
ls -l "$out"; [ ! -d "$out/coreapps" ] || ls -l "$out/coreapps"
sums=()
part host && sums+=("$out/hostapp.app")
part sdk && sums+=("$out/coreapp.bin" "$out/testlib.u2a" "$out/unzip.u2a" "$out/miniz.u2a" "$out/trex_crt.u2a" "$out/softboot.u2a" "$out/online.u2a")
sha256sum "${sums[@]}"; ! part sdk || [ ! -f "$out/coreapp.app" ] || sha256sum "$out/coreapp.app"   # an older cache entry has no .app
: > "$out/DONE"
echo "=== trxapps done in $(lap)"

