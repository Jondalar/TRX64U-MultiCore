#!/usr/bin/env bash
# T104.1 D16: the board's CoreFs (coreapp/src/fm_fs.cc) over the FileManager, FileSystemFAT and FatFS on a real exFAT image of 6 GiB
# (sparse). It builds the stack of vendor/gideon/software for the Mac - that is the File with 64 bit size and offsets of the fork's
# branch t104-fs64 - so it needs the submodule at that commit or later: with the pinned 32 bit File the test cannot pass, and
# the script says so instead of failing somewhere in a compiler message.
#   tests/coreapp/fmfs64/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/tests/coreapp/fmfs64"
SW="$ROOT/vendor/gideon/software"
SRC="$ROOT/coreapp/src"
T="$(mktemp -d)"
trap 'rm -rf "${T:?}"' EXIT
CXX="${CXX:-c++}"
CC="${CC:-cc}"
FLAGS="-std=gnu++14 -g -O0 -w -DU64=2 -DRUNS_ON_PC"
FF="$SW/chan_fat/ff14/source"

if ! grep -q "get_size32" "$SW/filesystem/file.h"; then
  echo "tests/coreapp/fmfs64: vendor/gideon has the 32 bit File (no File::get_size32). Check out the fork's t104-fs64 (T104.1 D16) first." >&2
  exit 2
fi

$CC -g -O0 -w -I"$SW/chan_fat/pc" -I"$FF" -I"$SW/filesystem" -c "$FF/ff.c" -o "$T/ff.o"
$CC -g -O0 -w -I"$SW/chan_fat/pc" -I"$FF" -I"$SW/filesystem" -c "$FF/ffunicode.c" -o "$T/ffunicode.o"

# build NAME COREDIR: COREDIR holds fsif.h fm_fs.h fm_fs.cc (the product's coreapp/src, or a mutated copy of the three)
build() {
  local name="$1" core="$2"
  local inc="-I$core -I$SW/filesystem -I$SW/network -I$SW/components -I$SW/api -I$SW/filemanager -I$SW/infra -I$SW/system -I$SW/userinterface -I$SW/io/stream -I$SW/io/usb/tests -I$SW/chan_fat/pc -I$FF"
  local srcs=("$core/fm_fs.cc" "$HERE/medium_stub.cc" "$SW/filesystem/blockdev.cc" "$SW/filesystem/blockdev_emul.cc" "$SW/filesystem/partition.cc"
              "$SW/filesystem/file_system.cc" "$SW/filesystem/directory.cc" "$SW/filesystem/file.cc" "$SW/filesystem/filesystem_fat.cc"
              "$SW/filesystem/filesystem_root.cc" "$SW/filesystem/diskio.cc" "$SW/filemanager/filemanager.cc" "$SW/filemanager/path.cc"
              "$SW/components/mystring.cc" "$SW/components/pattern.cc" "$HERE/test_fmfs64.cc" "$SW/io/usb/tests/host_test_main.cpp")
  $CXX $FLAGS $inc "${srcs[@]}" "$T/ff.o" "$T/ffunicode.o" -lpthread -o "$T/$name"
}

echo "== product"
build product "$SRC"
"$T/product"

# T104 5.16: the same CoreFs with the fast seek. FatFS gets FF_USE_FASTSEEK 1 (the pc configuration with that one option changed), the bench
# counts the sectors the block device is asked for per random block of a fragmented image.
mkdir -p "$T/ffc"
sed -E 's/^(#define FF_USE_FASTSEEK)[[:space:]]+0/\1 1/' "$SW/chan_fat/pc/ffconf.h" > "$T/ffc/ffconf.h"
grep -q '^#define FF_USE_FASTSEEK 1' "$T/ffc/ffconf.h" || { echo "fmfs64: the seek ffconf.h did not get fast seek" >&2; exit 2; }
$CC -g -O0 -w -I"$T/ffc" -I"$FF" -I"$SW/filesystem" -c "$FF/ff.c" -o "$T/ff_seek.o"
$CC -g -O0 -w -I"$T/ffc" -I"$FF" -I"$SW/filesystem" -c "$FF/ffunicode.c" -o "$T/ffunicode_seek.o"
# build_seek NAME COREDIR: COREDIR holds fsif.h fm_fs.h fm_fs.cc fastseek.h fastseek.cc fastseek_api.h fastseek_exports.cc board.h
build_seek() {
  local name="$1" core="$2"
  local inc="-I$core -I$T/ffc -I$SW/filesystem -I$SW/network -I$SW/components -I$SW/api -I$SW/filemanager -I$SW/infra -I$SW/system -I$SW/userinterface -I$SW/io/stream -I$SW/io/usb/tests -I$FF"
  local srcs=("$core/fm_fs.cc" "$HERE/medium_stub.cc" "$core/fastseek.cc" "$core/fastseek_exports.cc" "$SW/filesystem/blockdev.cc" "$SW/filesystem/blockdev_emul.cc" "$SW/filesystem/partition.cc"
              "$SW/filesystem/file_system.cc" "$SW/filesystem/directory.cc" "$SW/filesystem/file.cc" "$SW/filesystem/filesystem_fat.cc"
              "$SW/filesystem/filesystem_root.cc" "$SW/filesystem/diskio.cc" "$SW/filemanager/filemanager.cc" "$SW/filemanager/path.cc"
              "$SW/components/mystring.cc" "$SW/components/pattern.cc" "$HERE/test_fmfs_seek.cc" "$SW/io/usb/tests/host_test_main.cpp")
  $CXX $FLAGS $inc "${srcs[@]}" "$T/ff_seek.o" "$T/ffunicode_seek.o" -lpthread -o "$T/$name"
}
echo "== fast seek"
build_seek seek_product "$SRC"
"$T/seek_product"
# T128 S9: the folder disk over the real stack - the CoreFs calls it added (free_bytes, mkdir, rename, dates and attributes, an empty file) and the handle
# policy with 200 fragmented files and 8 handles.
build_folder() {
  local name="$1" core="$2"
  local inc="-I$core -I$HERE/.. -I$T/ffc -I$SW/filesystem -I$SW/network -I$SW/components -I$SW/api -I$SW/filemanager -I$SW/infra -I$SW/system -I$SW/userinterface -I$SW/io/stream -I$SW/io/usb/tests -I$FF"
  local srcs=("$core/fm_fs.cc" "$HERE/medium_stub.cc" "$core/fastseek.cc" "$core/fastseek_exports.cc" "$core/folderhd.cc" "$core/folderhd_fold.cc" "$core/amiga_ffs.cc" "$SW/filesystem/blockdev.cc"
              "$SW/filesystem/blockdev_emul.cc" "$SW/filesystem/partition.cc" "$SW/filesystem/file_system.cc" "$SW/filesystem/directory.cc"
              "$SW/filesystem/file.cc" "$SW/filesystem/filesystem_fat.cc" "$SW/filesystem/filesystem_root.cc" "$SW/filesystem/diskio.cc"
              "$SW/filemanager/filemanager.cc" "$SW/filemanager/path.cc" "$SW/components/mystring.cc" "$SW/components/pattern.cc"
              "$HERE/test_fmfs_folder.cc" "$SW/io/usb/tests/host_test_main.cpp")
  $CXX $FLAGS $inc "${srcs[@]}" "$T/ff_seek.o" "$T/ffunicode_seek.o" -lpthread -o "$T/$name"
}
echo "== folder disk (T128 S9)"
build_folder folder_product "$SRC"
"$T/folder_product"
# Directory listing through the core application's FatFs options (FF_USE_FASTSEEK 1, LFN on the stack), exFAT, 4 KiB clusters: 300 entries with long
# names and spaces span several clusters and sectors; every one must come back (T139: the board's SD listing lost entries; this is the
# stack-independent half of the question - the CoreFs and FatFs logic list a multi-cluster exFAT directory completely).
build_list() {
  local name="$1" core="$2"
  local inc="-I$core -I$HERE/.. -I$T/ffc -I$SW/filesystem -I$SW/network -I$SW/components -I$SW/api -I$SW/filemanager -I$SW/infra -I$SW/system -I$SW/userinterface -I$SW/io/stream -I$SW/io/usb/tests -I$FF"
  local srcs=("$core/fm_fs.cc" "$HERE/medium_stub.cc" "$core/fastseek.cc" "$core/fastseek_exports.cc" "$SW/filesystem/blockdev.cc" "$SW/filesystem/blockdev_emul.cc" "$SW/filesystem/partition.cc"
              "$SW/filesystem/file_system.cc" "$SW/filesystem/directory.cc" "$SW/filesystem/file.cc" "$SW/filesystem/filesystem_fat.cc"
              "$SW/filesystem/filesystem_root.cc" "$SW/filesystem/diskio.cc" "$SW/filemanager/filemanager.cc" "$SW/filemanager/path.cc"
              "$SW/components/mystring.cc" "$SW/components/pattern.cc" "$HERE/test_fmfs_list.cc" "$SW/io/usb/tests/host_test_main.cpp")
  $CXX $FLAGS $inc "${srcs[@]}" "$T/ff_seek.o" "$T/ffunicode_seek.o" -lpthread -o "$T/$name"
}
echo "== directory listing"
build_list list_product "$SRC"
"$T/list_product"
echo "fmfs64: product passes"
