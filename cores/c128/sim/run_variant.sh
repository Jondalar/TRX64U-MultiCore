#!/bin/sh
# T140 D24 item 4 (room): the C128 release build leaves the JTAG debug master out and a dev build can have it back. Source guards (no Vivado here):
# core.toml's mono_env carries TRX_TOP_OFF=NO_JTAG_DBG; build_mono.tcl turns TRX_TOP_OFF into defines; mono/top.v has the NO_JTAG_DBG branch that ties
# the debug port low and instantiates jtag_dbg only in the other; build.sh --jtag filters exactly that entry out.. Final line PASS or FAIL. Run from anywhere.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
fail=0
toml_ok() { python3 - "$1" <<'EOF'
import re, sys
s = open(sys.argv[1]).read()
m = re.search(r'^mono_env\s*=\s*\[(.*?)\]', s, re.S | re.M)
sys.exit(0 if m and '"TRX_TOP_OFF=NO_JTAG_DBG"' in m.group(1) else 1)
EOF
}
filter_ok() { grep -q 'JTAG" = 1 \] && \[ "\$e" = "TRX_TOP_OFF=NO_JTAG_DBG" \] && continue' "$1" && grep -q -- '--jtag) JTAG=1' "$1"; }
if toml_ok "$root/cores/c128/mono/core.toml"; then echo "ok   core.toml mono_env: TRX_TOP_OFF=NO_JTAG_DBG (release build without the JTAG debug master)"; else echo "FAIL core.toml guard"; fail=1; fi
if grep -q 'TRX_TOP_OFF' "$root/cores/c128/mono/build_mono.tcl" && grep -q '^`ifdef NO_JTAG_DBG' "$root/cores/c128/mono/top.v" \
   && awk '/^`ifdef NO_JTAG_DBG/{f=1} f&&/assign dbg_we = 1.b0;/{a=1} f&&/^`else/{e=1} e&&/jtag_dbg dbg \(/{b=1} /^`endif/{if(f)exit} END{exit !(a&&b)}' "$root/cores/c128/mono/top.v"; then
  echo "ok   top.v: NO_JTAG_DBG ties the debug port low and leaves jtag_dbg out, build_mono.tcl passes TRX_TOP_OFF on"; else echo "FAIL top.v / build_mono.tcl guard"; fail=1; fi
if filter_ok "$root/cores/c128/mono/build.sh"; then echo "ok   build.sh --jtag drops the flag for a dev build"; else echo "FAIL build.sh guard"; fail=1; fi
if [ $fail = 0 ]; then echo "PASS run_variant"; else echo "FAIL run_variant"; exit 1; fi
