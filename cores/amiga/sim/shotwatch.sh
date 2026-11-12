#!/bin/sh
# prints the newest shot's number of distinct colours
d=build/l2bootstripe; n=$(ls $d/shot_*.ppm | wc -l); n=$((n-1))
python3 - "$d/shot_$n.ppm" <<'PY'
import sys
t=open(sys.argv[1]).read().split(); v=t[4:]
print(sys.argv[1], len(set(zip(v[0::3],v[1::3],v[2::3]))))
PY
