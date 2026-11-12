#!/bin/sh
# T149 S2: cores/amiga1200/sd/sd_spi.vhd against a card model with a late MISO (card output delay 14 ns + board 3 ns, SCK pad 5 ns,
# input 1.5 ns) at 25 MHz SCK. Passes: the new block at rate 1, 2, 3, with the card's delays at the worst case and at
# zero. Controls that have to FAIL: Gideon's original spi.vhd at rate 1 with the worst-case delays, and the new block
# with a card slower than the window (28 ns). Needs ghdl on PATH. One process at a time, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
"$root/tools/gideon_sources.sh"
out=$here/build
mkdir -p "$out"
cd "$out"
GH="nice -n 15 ghdl"
F="--std=08 -frelaxed -fsynopsys"
$GH -a $F $root/vendor/gideon/fpga/ip/busses/vhdl_source/io_bus_pkg.vhd \
    $root/vendor/gideon/fpga/io/spi/vhdl_source/spi.vhd $root/cores/amiga1200/sd/sd_spi.vhd $here/tb_sd_spi.vhd
$GH -e $F tb_sd_spi
# the capability bit (D5): sd_spi_peripheral_io reads bit 7 of the control register as 1, Gideon's spi_peripheral_io (the host gateware) as 0
$GH -a $F $root/vendor/gideon/fpga/io/spi/vhdl_source/spi_peripheral_io.vhd $here/tb_sd_cap.vhd
$GH -e $F tb_sd_cap
fails=0
run() { # <name> <expect: pass|fail> <generics...>
    name=$1; want=$2; shift 2
    $GH -r $F tb_sd_spi "$@" > "$name.log" 2>&1 || true
    line=$(grep RESULT "$name.log" || true)
    bad=$(echo "$line" | grep -c "rx_errors=0/.* mosi_errors=0" || true)
    if [ -z "$line" ]; then echo "FAIL $name: no result line"; fails=$((fails + 1)); return; fi
    if [ "$want" = pass ] && [ "$bad" = 1 ]; then echo "PASS $name: $line"
    elif [ "$want" = fail ] && [ "$bad" = 0 ]; then echo "PASS control $name fails as it must: $line"
    else echo "FAIL $name (wanted $want): $line"; fails=$((fails + 1)); fi
}
run new_r1        pass -gg_new=true  -gg_rate=1
run new_r2        pass -gg_new=true  -gg_rate=2
run new_r3        pass -gg_new=true  -gg_rate=3
run new_r1_edge   pass -gg_new=true  -gg_rate=1 -gg_t_card=19000
run new_r1_fast   pass -gg_new=true  -gg_rate=1 -gg_t_card=0 -gg_t_board=0 -gg_t_out=2500 -gg_t_in=800
run old_r1_fast   pass -gg_new=false -gg_rate=1 -gg_t_card=0 -gg_t_board=0 -gg_t_out=2500 -gg_t_in=800
run old_r1        fail -gg_new=false -gg_rate=1
run old_r3        pass -gg_new=false -gg_rate=3
run new_r1_slow   fail -gg_new=true  -gg_rate=1 -gg_t_card=28000
cap() { # <name> <expect ctrl> <generics...>
    name=$1; want=$2; shift 2
    $GH -r $F tb_sd_cap "$@" > "$name.log" 2>&1 || true
    line=$(grep RESULT "$name.log" || true)
    if echo "$line" | grep -q "ctrl=$want\$"; then echo "PASS $name: $line"
    else echo "FAIL $name (wanted ctrl=$want): $line"; fails=$((fails + 1)); fi
}
cap cap_new      86 -gg_new=true
cap cap_old      06 -gg_new=false
[ $fails -eq 0 ] && echo "ALL PASS" || { echo "$fails FAILED"; exit 1; }
