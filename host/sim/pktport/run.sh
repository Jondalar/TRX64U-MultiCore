#!/bin/sh
# T103 bench: video + audio packets through the generic packet port (core on cpu_clk, crossing FIFO, arbiter on clk50)
# against the same senders wired straight to the arbiter as in host/top.v. iverilog only.
#   run.sh        the bench (PASS) and the core-swap bench
set -e
here=$(cd "$(dirname "$0")" && pwd)
h=$here/../..
out=$here/build; mkdir -p "$out"
src="$here/tb_pktport.v $here/pktport_core.v $h/pktport_host.v $h/udp_stream.v $h/udp_audio.v $h/stream_mux.v"
run() { # name defines
    iverilog -g2012 -o "$out/$1.vvp" $2 $src
    nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log"
    grep -E "^(kind|frames|PASS|FAIL|  )" "$out/$1.log"
}
echo "== port"; run port ""
grep -q "^PASS" "$out/port.log"
# the core swap in the middle of a frame (pktport_host dc input)
dcsrc="$here/tb_pk_dc.v $h/pktport_host.v"
echo "== core swap mid-frame"
iverilog -g2012 -o "$out/dc.vvp" $dcsrc
nice -n 15 vvp -n "$out/dc.vvp" > "$out/dc.log"; sed 's/^/  /' "$out/dc.log" | grep -E "PASS|FAIL"
grep -q "^PASS" "$out/dc.log"
echo "PASS pktport (bench and core swap)"
