#!/bin/sh
# T103: the c64u core's cw_* window (tb_c64u_cw.v) under iverilog, and the mutations that have to make it fail:
# each control is a copy of the design with one line changed, and the bench must end in FAIL on it.
set -e
here=$(cd "$(dirname "$0")" && pwd)
c=$here/..
out=$here/build; mkdir -p "$out"
run() { # name io.v sys.v
    iverilog -g2012 -I "$c" -o "$out/$1.vvp" -s tb_c64u_cw "$here/tb_c64u_cw.v" "$2" "$3"
    nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log"
}
echo "== window"; run cw "$c/c64u_io.v" "$c/c64u_sys.v"
grep -E "^(FAIL|PASS|torn)" "$out/cw.log"
grep -q "^PASS tb_c64u_cw" "$out/cw.log"
mut() { # name file sed-expression what
    sed "$3" "$2" > "$out/$1.v"
    if cmp -s "$2" "$out/$1.v"; then echo "ctl| $1: the mutation did not change the file"; exit 1; fi
    case "$2" in *c64u_io.v) run "$1" "$out/$1.v" "$c/c64u_sys.v" ;; *) run "$1" "$c/c64u_io.v" "$out/$1.v" ;; esac
    echo "== control $1 ($4, must fail)"
    grep -E "^FAIL" "$out/$1.log" | head -3 | sed 's/^/ctl| /'
    grep -q "^FAIL tb_c64u_cw" "$out/$1.log"
}
mut NEG_LEVEL_UNMASKED "$c/c64u_io.v" 's/((lvl_en \& level_irq) != 4.d0)/(level_irq != 4'"'"'d0)/' "level interrupt ignores LEVEL_IRQ_EN"
mut NEG_ID "$c/c64u_io.v" 's/12.h003: q <= "U";/12'"'"'h003: q <= "2";/' "ID byte 3 wrong"
mut NEG_TPL_ALL "$c/c64u_io.v" 's/assign udp_we    = we \&\& p9 \&\& !off\[8\];/assign udp_we    = we \&\& p9;/' "STREAM_ENA writes hit the template RAM"
mut NEG_CAPS_ORDER "$c/c64u_io.v" 's/12.h070: q <= core_caps\[7:0\];/12'"'"'h070: q <= core_caps[15:8];/' "CORE_CAPS byte 0 wrong"
mut NEG_UCI_ACK "$c/c64u_io.v" 's/wire        dev_ack = uci_ack || pb_ack || rc_ack;/wire        dev_ack = pb_ack || rc_ack;/' "UCI ack never reaches the app"
mut NEG_GATE "$c/c64u_sys.v" "s/14'h0040: gate_frz  <= op_wd\[0\];/14'h0041: gate_frz  <= op_wd[0];/" "GATE_CTRL write lands elsewhere"
mut NEG_TORN "$c/c64u_sys.v" 's/tfind = (vld\[0\] \&\& tag\[13:0\]  == a) ? 2'"'"'b10 :/tfind = (vld[0]) ? 2'"'"'b10 :/' "the per-word latch ignores its tag (one buffer for all masters)"
mut NEG_DESC_SHIFT "$c/c64u_io.v" 's/        cd_i  <= off\[5:0\];/        cd_i  <= off[5:0] + 6'"'"'d1;/' "descriptor bytes one place off"
mut NEG_SOCK_EN "$c/c64u_io.v" 's/12.h092: sock_en\[1\]       <= wdata\[0\];/12'"'"'h092: sock_en[0]       <= wdata[0];/' "SID2_EN lands on socket 1"
echo "PASS c64u_cw (bench passes, all controls fail)"
