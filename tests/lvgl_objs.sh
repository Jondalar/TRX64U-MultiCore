#!/usr/bin/env bash
# One LVGL build for every native bench (tests/view, tests/coreapp/render, tests/hostapp/render).
# Prints the directory holding the objects; builds it once per (lv_conf.h, LVGL tree) pair (the tree is vendor/gideon/software/lvgl, in git: its key
# is the index entry of every file of it, not the repository's commit).
#
#   OBJS="$(tests/lvgl_objs.sh "$LVGL_DIR" "$LV_CONF_H")"
#   cc ... "$OBJS"/*.o
#   tests/lvgl_objs.sh --key "$LVGL_DIR" "$LV_CONF_H"     only the key (the last part of the directory), builds nothing: it goes into the
#                                                          keys of the render benches (T100 D14), whose cache entries must not outlive these objects
#
# Each bench used to compile the ~400 LVGL sources itself, which cost the pipeline a minute and a half
# per run for the same bytes three times (T104 5.10).
set -euo pipefail
KEYONLY=""; if [ "$1" = "--key" ]; then KEYONLY=1; shift; fi
L="$1"; CONF="$2"
key="$( { cat "$CONF"; git -C "$L" ls-files -s . 2>/dev/null | grep . || find "$L/src" -name '*.c' -newer "$CONF" | head -1; } | shasum -a 256 2>/dev/null || { cat "$CONF"; } | sha256sum)"
key="${key%% *}"; key="${key:0:16}"
if [ -n "$KEYONLY" ]; then echo "$key"; exit 0; fi
DIR="${TRX_LVGL_CACHE:-${TMPDIR:-/tmp}/trx-lvgl}/$key"
if [ -f "$DIR/DONE" ]; then echo "$DIR"; exit 0; fi
# Built in a directory of its own and moved into place at the end: two benches that start together on a cold cache (the render benches run in
# parallel on the builder) used to share DIR, and the second one's `rm -rf` took the first one's object files away (cc: "can't create lv_os.o", rc 1).
mkdir -p "$(dirname "$DIR")"
W="$(mktemp -d "$DIR.tmp.XXXXXX")"
trap 'rm -rf "$W"' EXIT
cp "$CONF" "$W/lv_conf.h"
find "$L/src" -name '*.c' | grep -v "drivers/\|libs/\|/draw/\(eve\|nema\|nxp\|renesas\|sdl\|sifli\|vg_lite\|nanovg\|opengles\|dma2d\|espressif\)\|osal/\|stdlib/\(rtthread\|micropython\|uefi\)\|/fs/\|font/\(freetype\|tiny\|font_manager\)" > "$W/files.txt"
echo "$L/src/osal/lv_os_none.c" >> "$W/files.txt"
echo "$L/src/osal/lv_os.c" >> "$W/files.txt"
echo "$L/src/fs/lv_fs.c" >> "$W/files.txt"
( cd "$W" && cc -O1 -w -c -DLV_CONF_INCLUDE_SIMPLE -I"$W" -I"$L" -I"$L/src" $(cat "$W/files.txt") ) >&2
: > "$W/DONE"
# the first finisher wins; a second one (same key, same bytes) throws its copy away and uses the one in place
if [ -f "$DIR/DONE" ]; then :; else rm -rf "$DIR"; mv "$W" "$DIR"; fi
echo "$DIR"
