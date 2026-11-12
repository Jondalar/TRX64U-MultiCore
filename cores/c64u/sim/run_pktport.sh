#!/bin/sh
# T103: the c64u core's copies of the UDP senders (udp_stream_c64u, udp_audio_c64u, stream_mux_c64u in pktport_core_c64u) against the
# host's ORIGINAL senders: host/sim/pktport/tb_pktport.v feeds the same pixels, audio and MAC frames to (A) the originals wired as in
# host/top.v and (B) the copies behind the real host/pktport_host.v, and compares every frame byte for byte. The copies must be identical
# in effect; the control flips a bit on the port and has to fail. iverilog, about a minute.
set -e
here=$(cd "$(dirname "$0")" && pwd)
c=$here/..
h=$here/../../../host
out=$here/build; mkdir -p "$out"
sed 's/pktport_core core (/pktport_core_c64u core (/' "$h/sim/pktport/tb_pktport.v" > "$out/tb_pktport_c64u.v"
src="$out/tb_pktport_c64u.v $c/pktport_core_c64u.v $c/udp_stream_c64u.v $c/udp_audio_c64u.v $c/stream_mux_c64u.v $h/pktport_host.v $h/udp_stream.v $h/udp_audio.v $h/stream_mux.v"
run() { iverilog -g2012 -o "$out/$1.vvp" $2 $src; nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log"; grep -E "^(kind|frames|PASS|FAIL|  )" "$out/$1.log" | sed "s/^/${PFX:-}/"; }
echo "== copies against originals"; run pk ""
grep -q "^PASS" "$out/pk.log"
echo "== control NEG_CORRUPT (must fail)"; PFX='ctl| ' run pk_neg "-DNEG_CORRUPT"
grep -q "^FAIL" "$out/pk_neg.log"
echo "== control NEG_RDY_ONLY (must fail)"; PFX='ctl| ' run pk_rdy "-DNEG_RDY_ONLY"
grep -q "^FAIL" "$out/pk_rdy.log"
echo "PASS c64u_pktport (the copies equal the originals, both controls fail)"
