#!/bin/sh
# T37 in the c64u core: the SID sockets on the port's bus and their audio (tb_c64u_sock.v) under iverilog, and the
# mutations that have to make it fail: each control is a copy of the design with one line changed.
set -e
here=$(cd "$(dirname "$0")" && pwd)
c=$here/..
host=$c/../../host
out=$here/build; mkdir -p "$out"
run() { # name slot_port.v sid_sock_audio.v
    iverilog -g2012 -I "$c" -o "$out/$1.vvp" -s tb_c64u_sock "$here/tb_c64u_sock.v" "$2" "$3" "$host/i2s_out.v"
    nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log"
}
echo "== sockets"; run sock "$c/slot_port_c64u.v" "$c/sid_sock_audio_c64u.v"
grep -E "^(FAIL|PASS)" "$out/sock.log"
grep -q "^PASS tb_c64u_sock" "$out/sock.log"
mut() { # name file sed-expression what
    sed "$3" "$2" > "$out/$1.v"
    if cmp -s "$2" "$out/$1.v"; then echo "ctl| $1: the mutation did not change the file"; exit 1; fi
    case "$2" in *slot_port_c64u.v) run "$1" "$out/$1.v" "$c/sid_sock_audio_c64u.v" ;; *) run "$1" "$c/slot_port_c64u.v" "$out/$1.v" ;; esac
    echo "== control $1 ($4, must fail)"
    grep -E "^FAIL" "$out/$1.log" | head -3 | sed 's/^/ctl| /'
    grep -q "^FAIL tb_c64u_sock" "$out/$1.log"
}
mut NEG_CS_SHORT "$c/slot_port_c64u.v" "s/wire sk_t = tk >= 5'd2 \&\& tk <= 5'd17;/wire sk_t = tk >= 5'd2 \&\& tk <= 5'd16;/" "the select rises before PHI2 falls"
mut NEG_NO_ARM "$c/slot_port_c64u.v" "s/armed <= (present || sock_on) \&\& synced;/armed <= present \&\& synced;/" "an enabled socket does not arm the port (no PHI2)"
mut NEG_SPLIT "$c/slot_port_c64u.v" "s/3'd1: sa = c_addr\[5\];/3'd1: sa = c_addr[6];/" "split A5 takes A6"
mut NEG_DELAY_BIT "$c/sid_sock_audio_c64u.v" "s/if (lr_s\[1\] != lr_q) begin/if (lrck != lr_q) begin/; s/lr_q <= lr_s\[1\];/lr_q <= lrck;/" "LRCK not aligned with the data: the delay bit is taken as the MSB"
mut NEG_GAIN_SWAP "$c/sid_sock_audio_c64u.v" "s/mg <= {1'b0, g5}; ms <= 1'b0;/mg <= {1'b0, g4}; ms <= 1'b0;/" "socket 1 left takes the right gain"
mut NEG_NO_EN "$c/sid_sock_audio_c64u.v" "s/2'd1: begin mx <= e\[1\] ? adc_r : 24'sd0;/2'd1: begin mx <= adc_r;/" "a disabled socket still adds"
mut NEG_ARMED_SOCK "$c/slot_port_c64u.v" "s/fault, armed_c \&\& !fault, m_int/fault, act, m_int/" "SLOT_STATUS b16 reports a socket as a cartridge (the app switches the UCI off)"
echo "PASS c64u_sock (bench passes, all controls fail)"
