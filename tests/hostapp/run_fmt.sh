#!/usr/bin/env bash
# T111 benches of tests/hostapp/run.sh (called by it with its scratch directory): S1 the list, S2 the confirmation, S4-S8 and the cancel (Q6) of the
# format over a sparse medium, and the independent checker tools/exfat_check.py on the images that come out.
#
# What is real: ff.c of the submodule with the board's ffconf.h, the FileManager / FileDevice / Disk / Partition / FileSystemFAT of
# host-app/software (with the T111 additions), hostfs.cc, mediaglue.cc, fmt.cc, mbr.cc, mediasniff.cc, fmtconfirm.cc. What is fake: the block device.
#   JOBS            compiles at a time (default 4)
#   T111_IMG_DIR    keep the images of S4 there (for fsck.exfat or a look)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
SW="$ROOT/vendor/gideon/software"
FFS="$SW/chan_fat/ff14/source"
T="${1:?scratch directory}/t111"
mkdir -p "$T/o" "$T/ffc" "$T/img"
if [ ! -f "$FFS/ff.c" ]; then
  echo "SKIPPED T111 benches: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
JOBS="${JOBS:-4}"

# ---- FatFs with the board's configuration -----------------------------------------------------------------------------------------------------
# hostapp/ffconf.h = the application's with f_expand on; the bench has no RTOS, so no mutex. Nothing else may differ from the board's file.
sed -E -e '/#include "FreeRTOS.h"/d' -e '/#include "semphr.h"/d' -e 's/^(#define FF_FS_REENTRANT)[[:space:]]+1/\1 0/' \
    -e 's/^(#define FF_USE_EXPAND)[[:space:]]+0/\1 1/' "$SW/chan_fat/full/ffconf.h" > "$T/ffc/ffconf.h"
python3 -I - "$SW/chan_fat/full/ffconf.h" "$T/ffc/ffconf.h" <<'PY'
import re, sys
def defs(p):
    d = {}
    for l in open(p):
        m = re.match(r"#define\s+(\w+)\s*(.*?)\s*(/\*.*)?$", l.rstrip())
        if m:
            d[m.group(1)] = m.group(2)
    return d
a, b = defs(sys.argv[1]), defs(sys.argv[2])
diff = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
assert diff == ["FF_FS_REENTRANT", "FF_USE_EXPAND"], diff
assert a["FF_FS_EXFAT"] == "1" and a["FF_USE_MKFS"] == "1" and a["FF_MAX_SS"] == "4096" and a["FF_MIN_SS"] == "512" and a["FF_LBA64"] == "0" and a["FF_USE_LABEL"] == "0", a
print("T111 bench ffconf.h: the board's, but for the RTOS mutex (REENTRANT 0) and f_expand (EXPAND 1, as hostapp/ffconf.h)")
PY

CXXF=(-std=gnu++14 -w -g -O1 -DU64=2 -DRUNS_ON_PC)
INC=(-I"$T/ffc" -I"$FFS" -I"$ROOT/tests/hostapp/stubfs" -I"$ROOT/tests/hostapp" -I"$H" -I"$HS/filesystem" -I"$HS/filemanager" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$ROOT/coreapp/src" -I"$ROOT/uicore")
obj() { local s="$1" o="$2"; shift 2; c++ "${CXXF[@]}" "${INC[@]}" ${1+"$@"} -c -o "$o" "$s"; }   # obj SRC OUT [flags]
for c in ff ffunicode ffsystem; do cc -w -g -O1 -I"$T/ffc" -I"$FFS" -I"$HS/filesystem" -c -o "$T/o/$c.o" "$FFS/$c.c" & done
cc -w -g -O1 -I"$H" -c -o "$T/o/sha256.o" "$H/sha256.c" &   # cor.cc (hostfs.cc names a core by cor_probe, T148)
wait

SRCS=("$HS/filesystem/blockdev.cc" "$HS/filesystem/partition.cc" "$HS/filesystem/disk.cc" "$HS/filesystem/file_system.cc" "$HS/filesystem/directory.cc"
      "$HS/filesystem/file.cc" "$HS/filesystem/filesystem_fat.cc" "$HS/filesystem/filesystem_root.cc" "$HS/filesystem/diskio.cc"
      "$HS/filemanager/filemanager.cc" "$HS/filemanager/path.cc" "$HS/filemanager/file_device.cc" "$HS/filemanager/file_partition.cc"
      "$HS/components/mystring.cc" "$HS/components/pattern.cc" "$HS/components/size_str.cc"
      "$H/hostfs.cc" "$H/discover.cc" "$H/mbr.cc" "$H/mediasniff.cc" "$H/mediaui.cc" "$H/mediaglue.cc" "$H/fmt.cc" "$H/fmtconfirm.cc" "$H/cor.cc"
      "$H/coremaint.cc" "$H/fileops.cc" "$H/bootcfg.cc" "$ROOT/coreapp/src/descrec.cc" "$ROOT/coreapp/src/overlay.cc")   # hostfs.cc::core_roms reads record 6 through cm_rows_from_record (T239.3 P1-6; descrec.cc needs overlay.cc: T256)
n=0
for s in "${SRCS[@]}"; do
  obj "$s" "$T/o/$(basename "${s%.cc}").o" &
  n=$((n + 1)); [ $((n % JOBS)) -eq 0 ] && wait
done
wait
FFO=("$T/o/ff.o" "$T/o/ffunicode.o" "$T/o/ffsystem.o" "$T/o/sha256.o")
objs() { local o=(); for s in "${SRCS[@]}"; do o+=("$T/o/$(basename "${s%.cc}").o"); done; printf '%s\n' "${o[@]}"; }

# link TEST OUT: the test and every object
link() {
  local test="$1" out="$2"
  local o=() x
  while read -r x; do o+=("$x"); done < <(objs)
  c++ "${CXXF[@]}" "${INC[@]}" -o "$out" "$ROOT/tests/hostapp/$test.cc" "${o[@]}" "${FFO[@]}" -lm
}

# ---- S1 the list -------------------------------------------------------------------------------------------------------------------------------
echo "== T111 S1: media_sniff"
link test_mediasniff "$T/test_mediasniff"
"$T/test_mediasniff"

# ---- S2 the confirmation ------------------------------------------------------------------------------------------------------------------------
echo "== T111 S2: the confirmation"
link test_fmtconfirm "$T/test_fmtconfirm"
"$T/test_fmtconfirm"

# ---- S4-S8, the cancel -------------------------------------------------------------------------------------------------------------------------
echo "== T111 S4-S8: the format over a sparse medium (64 MiB, 2 GiB, 33 GiB)"
link test_exfatfmt "$T/test_exfatfmt"
"$T/test_exfatfmt" "$T/img"
for i in "$T"/img/*.img; do
  python3 -I "$ROOT/tools/exfat_check.py" "$i"
done
if [ -n "${T111_IMG_DIR:-}" ]; then mkdir -p "$T111_IMG_DIR"; cp --sparse=always "$T"/img/*.img "$T111_IMG_DIR"/; fi

# fsck.exfat as a second judge, where the image has it: the partition cut out of the image (the 64 MiB and the 2 GiB one)
if command -v fsck.exfat >/dev/null 2>&1; then
  for n in exfat_64M exfat_2G; do
    dd if="$T/img/$n.img" of="$T/img/$n.part" bs=512 skip=8192 conv=sparse status=none
    fsck.exfat -n "$T/img/$n.part"
    rm -f "$T/img/$n.part"
  done
  echo "fsck.exfat: clean on the 64 MiB and the 2 GiB volume"
else
  echo "fsck.exfat is not in this image: the second judge is not run here (tools/exfat_check.py passed)"
fi

echo "T111 benches: all pass"
