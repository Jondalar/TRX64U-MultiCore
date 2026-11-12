#!/usr/bin/env bash
# T104.1 D16 bench: file sizes and offsets of 64 bit, on a real exFAT image of 6 GiB (sparse). Builds the host stack (FatFS,
# FileSystemFAT, FileManager, VFS) and runs fs64_test.cpp. Then the NEGATIVE CONTROLS: the sources are copied, one 64 bit type is
# made 32 bit with sed, the same test is built against the copy, and it must FAIL - a control that passes means the test could
# not see the defect. The product tree is never touched.
#   make            (or ./run.sh)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SW="$(cd "$HERE/../.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "${T:?}"' EXIT
CXX="${CXX:-c++}"
CC="${CC:-cc}"
FLAGS="-std=gnu++14 -g -O0 -w -DU64=2 -DRUNS_ON_PC"
TESTMAIN="$SW/io/usb/tests/host_test_main.cpp"
FF="$SW/chan_fat/ff14/source"

# FatFS is C and does not depend on the sources under test: built once
$CC -g -O0 -w -I"$SW/chan_fat/pc" -I"$FF" -I"$SW/filesystem" -c "$FF/ff.c" -o "$T/ff.o"
$CC -g -O0 -w -I"$SW/chan_fat/pc" -I"$FF" -I"$SW/filesystem" -c "$FF/ffunicode.c" -o "$T/ffunicode.o"

# The firmware's own printf (small_printf.cc), under other names so that it does not take the place of the Mac's: the lines of the FTP
# server are formatted with it in the test, as they are on the board
$CXX $FLAGS -Dprintf=fw_printf -Dsprintf=fw_sprintf -Dsnprintf=fw_snprintf -Dvsnprintf=fw_vsnprintf -Dvsprintf=fw_vsprintf \
     -Dsscanf=fw_sscanf -Dputs=fw_puts -I"$SW/system" -I"$SW/infra" -c "$SW/system/small_printf.cc" -o "$T/small_printf.o"

# build NAME ROOT: ROOT holds filesystem/ network/ components/ api/ (the product in $SW, or a mutated copy of it)
build() {
  local name="$1" root="$2"
  local inc="-I$root/filesystem -I$root/network -I$root/components -I$root/api -I$SW/filemanager -I$SW/infra -I$SW/system -I$SW/userinterface -I$SW/io/stream -I$SW/io/usb/tests -I$SW/chan_fat/pc -I$FF -I$SW/components -I$SW/filesystem -I$SW/network -I$SW/api"
  local srcs=("$root/filesystem/blockdev.cc" "$root/filesystem/blockdev_emul.cc" "$root/filesystem/partition.cc"
              "$root/filesystem/file_system.cc" "$root/filesystem/directory.cc" "$root/filesystem/file.cc"
              "$root/filesystem/filesystem_fat.cc" "$root/filesystem/filesystem_root.cc" "$root/filesystem/diskio.cc"
              "$root/network/vfs.cc" "$root/components/size_str.cc" "$SW/filemanager/filemanager.cc" "$SW/filemanager/path.cc"
              "$SW/components/mystring.cc" "$SW/components/pattern.cc" "$HERE/fs64_test.cpp" "$TESTMAIN" "$SW/api/json.cc")
  $CXX $FLAGS $inc "${srcs[@]}" "$T/ff.o" "$T/ffunicode.o" "$T/small_printf.o" -lpthread -o "$T/$name"
}

echo "== product"
build product "$SW"
"$T/product"

# control NAME FILE:SED-EXPR...: copies of the sources, FILE (relative to the copy) broken by SED-EXPR, built, must FAIL
control() {
  local name="$1"
  shift
  local d="$T/mut_$name"
  mkdir -p "$d"
  cp -R "$SW/filesystem" "$SW/network" "$SW/components" "$SW/api" "$d/"
  local spec file expr before
  for spec in "$@"; do
    file="${spec%%:*}"
    expr="${spec#*:}"
    before="$(shasum "$d/$file")"
    sed -E "$expr" "$d/$file" > "$d/$file.new"
    mv "$d/$file.new" "$d/$file"
    if [ "$before" = "$(shasum "$d/$file")" ]; then
      echo "control $name: the sed expression for $file changed nothing" >&2
      exit 2
    fi
  done
  if ! build "$name" "$d" 2> "$T/$name.err"; then
    echo "control $name: does not build" >&2
    head -20 "$T/$name.err" >&2
    exit 2
  fi
  if "$T/$name" > "$T/$name.out" 2>&1; then
    echo "control $name: PASSED but must fail" >&2
    exit 1
  fi
  echo "control $name: fails as it must: $(grep -m1 -E 'Fs64|FAIL|failed' "$T/$name.out" | cut -c1-150)"
}

echo "== negative controls"
control file_size_32bit 'filesystem/file.h:s/virtual uint64_t get_size/virtual uint32_t get_size/' \
                        'filesystem/filesystem_fat.h:s/uint64_t get_size/uint32_t get_size/' \
                        'filesystem/filesystem_fat.cc:s/uint64_t FileFAT :: get_size/uint32_t FileFAT :: get_size/'
control seek_32bit      'filesystem/file.h:s/virtual FRESULT seek\(uint64_t pos\)/virtual FRESULT seek(uint32_t pos)/' \
                        'filesystem/filesystem_fat.h:s/FRESULT seek\(uint64_t pos\)/FRESULT seek(uint32_t pos)/' \
                        'filesystem/filesystem_fat.cc:s/FileFAT :: seek\(uint64_t pos\)/FileFAT :: seek(uint32_t pos)/'
control fileinfo_32bit  'filesystem/file_info.h:s/uint64_t  size;/uint32_t  size;/'
control vfs_stat_32bit  'network/vfs.h:s/uint64_t st_size;/uint32_t st_size;/'
control ftp_lines_32bit 'network/ftp_format.h:s/u64_dec\(size, sz\)/u64_dec((uint32_t)size, sz)/; s/u64_dec\(st->st_size, sz\)/u64_dec((uint32_t)st->st_size, sz)/; s/u64_dec_pad\(st->st_size, sz, 11\)/u64_dec_pad((uint32_t)st->st_size, sz, 11)/'
control rest_json_32bit 'api/json.h:s/new JSON_Integer64\(val\)/new JSON_Integer64((uint32_t)val)/'
control size_str_32bit  'components/size_str.cc:s/size_to_string_bytes\(uint64_t size/size_to_string_bytes(uint32_t size/' \
                        'components/size_str.h:s/size_to_string_bytes\(uint64_t size/size_to_string_bytes(uint32_t size/'
echo "fs64 bench: product passes, all controls fail"
