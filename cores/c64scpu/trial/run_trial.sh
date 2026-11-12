#!/bin/sh
# T182 step 1 area trial, runs INSIDE vivado-env:2024.1 at the repository root (builder: see spec section 5 for the docker line). Synthesis only.
#   run_trial.sh S|B|C ...    S = base + 65C816 trial, B = base alone (C128 flat build without VDC, Z80, tape), C = the C128 as shipped (calibration)
# Output: host/build_trial_<v>/utilization_synth.rpt, utilization_hier_synth.rpt, vivado.log. One Vivado at a time (/lock/heavy.lock).
set -e
cd "$(dirname "$0")/../../../host"
export HOME=/tmp/vivado-home; mkdir -p "$HOME"
for v in "$@"; do
  b=build_trial_$v
  rm -rf "$b"; mkdir -p "$b"
  common="TRX_BUILD=$b TRX_STOP=synth TRX_REU=1 TRX_STUB=overlay TRX_TOP_OFF=NO_JTAG_DBG"
  case "$v" in
    # the Z80 stays in S and B: c64_top_c128.v declares rom_hold / rom_wait_q inside `ifdef Z80 (line 617-655), so ROM_CACHE without Z80 does not elaborate;
    # its LUT/FF are read off the hierarchy report and taken out of the figures (spec section 5)
    S) vars="TRX_TRIAL=scpu TRX_VDC=none TRX_Z80=1 TRX_DEFS=ROM_CACHE\ RAM_DDR2\ SID2\ SOCK_AUDIO\ NO_NET" ;;
    S2) vars="TRX_TRIAL=scpu TRX_MCODE=block TRX_VDC=none TRX_Z80=1 TRX_DEFS=ROM_CACHE\ RAM_DDR2\ SID2\ SOCK_AUDIO\ NO_NET" ;;
    B) vars="TRX_TRIAL=none TRX_VDC=none TRX_Z80=1 TRX_DEFS=ROM_CACHE\ RAM_DDR2\ SID2\ SOCK_AUDIO\ NO_NET" ;;
    C) vars="TRX_TRIAL=none TRX_VDC=whole TRX_Z80=1 TRX_DEFS=VDC64\ ROM_CACHE\ RAM_DDR2\ SID2\ SOCK_AUDIO\ TAPE\ NO_NET" ;;
    *) echo "unknown variant $v" >&2; exit 2 ;;
  esac
  eval "env $common $vars flock /lock/heavy.lock nice -n 15 vivado -mode batch -nojournal -log $b/vivado.log -source ../cores/c64scpu/trial/build_trial.tcl" > "$b/out.txt" 2>&1 || echo "variant $v: vivado exit $?"
  echo "variant $v done: $(grep -E '^\| Slice LUTs ' "$b/utilization_synth.rpt" 2>/dev/null)"
done
