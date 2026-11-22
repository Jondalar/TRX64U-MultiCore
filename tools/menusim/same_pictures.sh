#!/usr/bin/env bash
# T239.3 P7: the pictures of two trees are the same. For each core and mode the key sequence is run in the tree BEFORE and the tree AFTER (menusim's
# --shots: the frame buffer after every key press, not drawn again, plus the page drawn whole at the end), and every picture pair is compared pixel by
# pixel. A pair that differs is named with the number of pixels, the box they lie in and the first one; the run ends 1 when any differs.
#
#   tools/menusim/same_pictures.sh --before TREE --after TREE [--mode M]... [--keys-file F]... <core>...
#   --keys-file F   a key sequence file of tools/menusim (default: speed_keys.txt, speed_keys_long.txt and speed_keys_pages.txt, all three)
# The tools are this tree's (menusim.cc, run.sh); only the application sources come from TREE (run.sh --tree).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BEFORE=""; AFTER=""; MODES=(); FILES=(); CORES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --before) BEFORE="$(cd "${2:?}" && pwd)"; shift 2 ;;
    --after) AFTER="$(cd "${2:?}" && pwd)"; shift 2 ;;
    --mode) MODES+=("${2:?}"); shift 2 ;;
    --keys-file) FILES+=("${2:?}"); shift 2 ;;
    -*) echo "same_pictures.sh: unknown option $1" >&2; exit 2 ;;
    *) CORES+=("$1"); shift ;;
  esac
done
[ -n "$BEFORE" ] && [ -n "$AFTER" ] && [ ${#CORES[@]} -gt 0 ] || { sed -n 2,9p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ ${#MODES[@]} -gt 0 ] || MODES=(1080p)
[ ${#FILES[@]} -gt 0 ] || FILES=(speed_keys.txt speed_keys_long.txt speed_keys_pages.txt)
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
bad=0; total=0
for c in "${CORES[@]}"; do for m in "${MODES[@]}"; do for f in "${FILES[@]}"; do
  KEYS="$(grep -v '^#' "$HERE/$f" | tr -d ' \n')"
  d="$W/$c-$m-${f%.txt}"; mkdir -p "$d/before" "$d/after"
  "$HERE/run.sh" --tree "$BEFORE" "$c" --mode "$m" --keys "$KEYS" --shots "$d/before" > "$d/before.log" 2>&1 || { cat "$d/before.log" >&2; exit 1; }
  "$HERE/run.sh" --tree "$AFTER" "$c" --mode "$m" --keys "$KEYS" --shots "$d/after" > "$d/after.log" 2>&1 || { cat "$d/after.log" >&2; exit 1; }
  out="$(python3 -I - "$d/before" "$d/after" "$c $m ${f%.txt}" <<'PY'
import os, sys, zlib, struct

def load(p):
    b = open(p, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(b):
        n, t = struct.unpack('>I4s', b[pos:pos + 8])
        d = b[pos + 8:pos + 8 + n]
        if t == b'IHDR': w, h = struct.unpack('>II', d[:8])
        if t == b'IDAT': idat += d
        pos += 12 + n
    raw = zlib.decompress(idat)
    st = w * 3 + 1
    return w, h, [raw[y * st + 1:(y + 1) * st] for y in range(h)]

a_dir, b_dir, tag = sys.argv[1:4]
names = sorted(os.listdir(a_dir))
if names != sorted(os.listdir(b_dir)):
    print('DIFF %s: the pictures are not the same set: %s / %s' % (tag, len(names), len(os.listdir(b_dir)))); sys.exit(1)
bad = 0
for n in names:
    wa, ha, ra = load(os.path.join(a_dir, n))
    wb, hb, rb = load(os.path.join(b_dir, n))
    if (wa, ha) != (wb, hb):
        print('DIFF %s %s: size %dx%d / %dx%d' % (tag, n, wa, ha, wb, hb)); bad += 1; continue
    cnt, x0, y0, x1, y1, first = 0, 1 << 30, 1 << 30, -1, -1, None
    for y in range(ha):
        if ra[y] == rb[y]: continue
        for x in range(wa):
            if ra[y][x * 3:x * 3 + 3] != rb[y][x * 3:x * 3 + 3]:
                cnt += 1; x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
                if first is None: first = (x, y, ra[y][x * 3:x * 3 + 3].hex(), rb[y][x * 3:x * 3 + 3].hex())
    if cnt:
        bad += 1
        print('DIFF %s %s: %d px in box %d,%d-%d,%d first at %d,%d before %s after %s' % ((tag, n, cnt, x0, y0, x1, y1) + first))
print('%s %s: %d pictures, %d differ' % ('SAME' if not bad else 'DIFF', tag, len(names), bad))
sys.exit(1 if bad else 0)
PY
)" && rc=0 || rc=$?
  echo "$out"
  total=$((total + 1)); [ "$rc" = 0 ] || bad=$((bad + 1))
done; done; done
echo "same_pictures: $total runs, $bad with differences"
[ "$bad" = 0 ]
