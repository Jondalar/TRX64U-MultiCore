#!/bin/sh
# T140 Q2b: the C128 large core's ultimate_sys (its copy of the host's, cores/c128/mono/host/ultimate_sys.vhd, built with g_net = 0 by the define NO_NET: no Ethernet MAC, the WiFi UART kept) in GHDL, with
# rvlite_bfm_nonet.vhd in place of the RISC-V. Every access into the two I/O windows must be acknowledged, a read gives 0, and the
# pins, interrupts and DMA ports of the dropped blocks stay idle under noise at their inputs.
# Source guard: core.toml builds with NO_NET and mono/top.v turns it into g_net = 0 and the capability bit CAPAB_ETH_RMII off (the app then starts no RMII driver); the file keeps uart_dma outside the g_eth generate (the ESP32's case buttons come over it).
# Needs ghdl on PATH (OSS CAD Suite). One process at a time, niced. Run from the repository root.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
design=$root/cores/c128/mono/host   # the large core's own copy of the host files (T164: a host change does not reach it)
usb=$root/host/sim/usb
out=$here/build
fork=$root/cores/c128/mono/host/ultimate_sys.vhd
G="nice -n 15 ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"

# the file has the Ethernet inside a generate that g_net = 0 skips, and uart_dma outside any generate
awk '/^ *g_eth: if g_net \/= 0 generate/{f=1} /entity work\.ethernet_interface/{if(f)e=1} /^ *end generate;/{f=0} /entity work\.uart_dma/{if(!f)u=1} END{exit !(e&&u)}' "$fork" || { echo "FAIL: $fork: ethernet_interface is not inside 'g_eth: if g_net /= 0 generate', or uart_dma is"; exit 1; }
grep -q "g_noeth: if g_net = 0 generate" "$fork" || { echo "FAIL: the file has no g_noeth branch"; exit 1; }
grep -q "TRX_DEFS=.*[ \"]NO_NET" "$root/cores/c128/mono/core.toml" || { echo "FAIL: core.toml mono_env does not define NO_NET"; exit 1; }
grep -q "ultimate_sys #(.g_capabilities(HOST_CAPS), .g_net(NET_ON))" "$root/cores/c128/mono/top.v" || { echo "FAIL: mono/top.v does not pass g_net"; exit 1; }
awk '/^`ifdef NO_NET/{f=1} f&&/localparam \[31:0\] HOST_CAPS = .*28.h4E40202/{a=1} f&&/localparam +NET_ON += 0;/{b=1} /^`else/{f=0} END{exit !(a&&b)}' "$root/cores/c128/mono/top.v" || { echo "FAIL: mono/top.v under NO_NET does not clear CAPAB_ETH_RMII (bit 24) and set NET_ON = 0"; exit 1; }
echo "ok   NO_NET: g_net = 0, capability bit 24 off, the Ethernet inside the g_eth generate, uart_dma outside"

"$root/tools/gideon_sources.sh"
rm -rf "$out"; mkdir -p "$out"

# one run: $1 = name, $2 = the ultimate_sys.vhd to use
run_one() {
  mkdir -p "$out/$1"
  python3 - "$design/gideon_vhdl.tcl" "$root/vendor/gideon/fpga" "$design" "$2" "$root/host" > "$out/$1/files.txt" <<'PY'
import sys
tcl, g, design, sysv, base = sys.argv[1:]   # base: a relative name is relative to host/, where Vivado runs
body = open(tcl).read().split("\nset g ", 1)[1].split("set gvhdl [list", 1)[1].split("]", 1)[0]
for tok in body.replace("\\", " ").split():
    p = tok.replace("$g", g).replace("$hd", design)
    p = p if p.startswith("/") else base + "/" + p
    print(sysv if p == design + "/ultimate_sys.vhd" else p)
PY
  (cd "$out/$1" && $G -a $opts --work=unisim "$usb/unisim_sim.vhd" &&
    $G -i $opts "$usb/tl_string_util_pkg.vhd" $(cat files.txt) &&
    $G -i $opts "$here/tb_nonet_pkg.vhd" "$here/tb_nonet.vhd" "$here/rvlite_bfm_nonet.vhd" &&
    $G -m $opts tb_nonet && $G -a $opts "$here/rvlite_bfm_nonet.vhd" && $G -e $opts tb_nonet) > "$out/$1/tb.log" 2>&1 ||
    { tail -20 "$out/$1/tb.log"; return 2; }
  (cd "$out/$1" && $G -r $opts tb_nonet --stop-time=2ms > run.log 2>&1) || true
  grep -E "BFM:|PASS|FAIL|error" "$out/$1/run.log" | sed 's/^.*(report note)://' | head -8
  grep -q PASS "$out/$1/run.log"
}

run_one fork "$fork" || { echo "FAIL: the fork"; exit 1; }
echo "ok   NO_NET ultimate_sys: every access to 0x060800-0x06083F acked, reads 0, eth pins/irq/dma idle"

echo PASS
