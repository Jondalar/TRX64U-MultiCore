#!/bin/sh
# T119 5.15: does Vivado's synthesis of the ULA keep the 48K's HBlank and HSync? The ULA with the ties of zx_core (ulaw.v) is synthesised
# (synth_design out of context, as the large core does it, write_verilog -mode funcsim) and the netlist is simulated (tb_ulaw.v, Verilator, the
# primitives of cells.v): 20 lines of each model, HBlank and HSync must rise on every line (48K: period 448, 128K: 456). Runs ON THE BUILDER
# (docker: vivado-env, trx64-sim-env), takes the Vivado slot lock for the synthesis (about 1 minute), niced. No CI lane: Vivado is not in the
# push pipeline. The whole zx_core netlist is driven the same way by tb_fsim.v (ports only; notes in T119 5.15).
#   cores/zx/sim/fsim/run.sh [<ula.sv>]      default cores/zx/rtl/ula.sv; the unpatched file (patch -R tools/zx_ula.patch) must print FAIL 48K
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
ula=${1:-$root/cores/zx/rtl/ula.sv}
cache=${CI_CACHE:-/srv/trx64-build/cache}
w=$(mktemp -d /var/tmp/zxfsim.XXXXXX)
trap 'rm -rf "$w"' EXIT
mkdir "$w/o" && chmod 777 "$w/o"
cp "$ula" "$w/ula.sv"; cp "$here/ulaw.v" "$here/cells.v" "$here/tb_ulaw.v" "$w/"
cat > "$w/synth.tcl" <<'EOF'
read_verilog -sv /w/ula.sv
read_verilog /w/ulaw.v
synth_design -top ulaw -part xc7a50tfgg484-1 -mode out_of_context
write_verilog -mode funcsim -force /o/net.v
EOF
nice -n 15 docker run --rm --init -v "$w:/w" -v "$w/o:/o" -v "$cache:/cache:ro" vivado-env:2024.1 sh -c '
  exec 9</cache/heavy.lock; flock 9; export HOME=/tmp/vh; mkdir -p $HOME; cd /o
  timeout 900 vivado -mode batch -nojournal -log /o/vivado.log -source /w/synth.tcl > /o/stdout.txt 2>&1 || echo "vivado failed"; chmod -R a+rwX /o'
[ -f "$w/o/net.v" ] || { tail -20 "$w/o/vivado.log"; echo "FAIL: no netlist"; exit 1; }
docker run --rm -u "$(id -u):$(id -g)" -v "$w:/w" -w /w trx64-sim-env:4 sh -c '
  verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -Wno-WIDTH -j 2 --top-module tb_ulaw -Mdir obj --x-assign 0 --x-initial 0 \
    tb_ulaw.v cells.v o/net.v > build.log 2>&1 || { tail -20 build.log; exit 1; }
  ./obj/Vtb_ulaw | grep -E "^(48K|128K|PASS|FAIL)"'
