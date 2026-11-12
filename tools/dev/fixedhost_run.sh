#!/bin/bash
exec 9>/cache/heavy.lock; flock 9; echo "lock $(date)"
export HOME=/tmp/vh; mkdir -p $HOME
E="TRX_FEATURES=jtag TRX_SID=redip2 T79_FLOOR=B TRX_L2_KEY=2d479e191206e0a5"
mkdir -p /w/cache_X; for f in /cache/ooc_* /cache/ip_*; do ln -sfn $f /w/cache_X/; done
cd /w/host
# A: what static occupies inside pb_l2 (old host)
env $E TRX_CACHE=/w/cache_X TRX_HOST_DCP=/cache/bit/host-226669f5ccc2fff9/static_routed.dcp EXP_MODE=A \
  vivado -mode batch -nojournal -log exp_A.log -source exp.tcl >/dev/null 2>&1; echo "A exit $?"
# H: host build with CONTAIN_ROUTING on pb_l1
env $E TRX_CACHE=/w/cache_X TRX_STAGE=host TRX_BUILD=build_contain EXP_CONTAIN=1 \
  vivado -mode batch -nojournal -log build_contain.log -source build.tcl >/dev/null 2>&1; echo "H exit $?"
# C: the C64 against that host, locked
env $E TRX_CACHE=/w/cache_X TRX_HOST_DCP=/w/host/build_contain/routed.dcp EXP_MODE=E1C \
  vivado -mode batch -nojournal -log exp_E1C.log -source exp.tcl >/dev/null 2>&1; echo "C exit $?"
echo "done $(date)"
