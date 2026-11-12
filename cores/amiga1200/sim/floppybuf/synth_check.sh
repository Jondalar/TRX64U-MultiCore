#!/bin/sh
# T97 5.11: the sector buffer of paula_floppy.v after Vivado synthesis. Needs Vivado, so it is not in ci/sims.txt; run it by hand on the
# builder when no CI job runs, in two containers that share one output directory <o> (vivado-env has no iverilog and no xsim kernel):
#   docker run --rm --name <t>-syn -v <repo>:/r:ro -v <o>:/o -w /o vivado-env:2024.1 sh /r/cores/amiga1200/sim/floppybuf/synth_check.sh synth
#   docker run --rm --name <t>-sim -v <repo>:/r:ro -v <o>:/o -w /o trx64-sim-env:4  sh /r/cores/amiga1200/sim/floppybuf/synth_check.sh sim
# synth: synthesises paula_floppy out of context (xc7a50t, as core.tcl) for the copy ("new"), writes the funcsim netlists, copies the Xilinx primitive models the netlists use, and LINTS every fd_dma_mem* RAMB
#   instance: its write enable (WEA/WEBWE) must not be constant 0.
# sim: simulates each netlist with tb_floppybuf (-DNETLIST) against the shared file, every output on every clock.
# Exit 0 only when the copy passes.
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
rtl=$root/cores/amiga1200/rtl
shared=$root/cores/amiga/rtl/src/minimig-aga/paula_floppy.v
o=$PWD

synth_one() {   # synth_one <name> <paula_floppy.v>
  d=$o/$1; rm -rf "$d"; mkdir -p "$d/src"; cp "$2" "$d/src/paula_floppy.v"; cp "$rtl/paula_floppy_fifo.v" "$d/src/"
  cat > "$d/synth.tcl" <<EOF
read_verilog -sv [glob $d/src/*.v]
synth_design -top paula_floppy -part xc7a50tfgg484-1 -mode out_of_context -flatten_hierarchy rebuilt
write_verilog -mode funcsim -force $d/net.v
report_utilization -file $d/util.rpt
EOF
  (cd "$d" && vivado -mode batch -nojournal -log viv.log -source synth.tcl > out.txt 2>&1) || { echo "FAIL: synthesis of $1"; return 2; }
  mkdir -p "$d/lib"; cp "$XILINX_VIVADO/data/verilog/src/glbl.v" "$d/lib/"
  for p in $(grep -oE "^ *[A-Z][A-Z0-9_]+ " "$d/net.v" | tr -d " " | sort -u); do
    [ -f "$XILINX_VIVADO/data/verilog/src/unisims/$p.v" ] && cp "$XILINX_VIVADO/data/verilog/src/unisims/$p.v" "$d/lib/"
  done
  # lint: the sector-buffer RAMB instances and their write-enable pins (cell name from the NLW_<cell>_DOADO unconnected-output net)
  awk -v name="$1" '
    /^ *RAMB(18|36)E1 *#/ { inram=1; cell=""; }
    inram && /NLW_[A-Za-z0-9_]+_DO(A|PA)/ { cell=$0; sub(/.*NLW_/,"",cell); sub(/_DO(A|PA).*/,"",cell) }
    inram && /\.WE(A|BWE)\(/ { we[cell]=we[cell] $0 }
    inram && /\);/ { inram=0 }
    END { bad=0; n=0
      for (c in we) if (c ~ /fd_dma_mem/) { n++; t=we[c]; gsub(/1.b0|[ ,{}();\.A-Za-z]/,"",t);
        if (t=="") { print name ": " c ": write enable constant 0 (never written)"; bad=1 } else print name ": " c ": write enable driven" }
      if (n==0) { print name ": no fd_dma_mem RAMB found"; bad=1 }
      exit bad }' "$d/net.v" > "$d/lint.txt"; lint=$?
  cat "$d/lint.txt"
  return $lint
}

sim_one() {   # sim_one <name>: 0 = the netlist equals the shared file
  d=$o/$1
  perl -pe 's/^module paula_floppy\b/module paula_floppy_ref/' "$shared" > "$d/pf_ref.v"
  perl -pe 's/\bpaula_floppy_fifo\b/paula_floppy_fifo_nl/g' "$d/net.v" > "$d/net_nl.v"
  iverilog -g2012 -DNETLIST -o "$d/tb" "$here/tb_floppybuf.v" "$d/pf_ref.v" "$rtl/paula_floppy_fifo.v" "$d/net_nl.v" "$d"/lib/*.v \
    -s tb_floppybuf -s glbl > "$d/iv.txt" 2>&1 || { echo "FAIL: iverilog of the $1 netlist"; head -n 8 "$d/iv.txt"; return 2; }
  timeout 900 vvp -n "$d/tb" > "$d/sim.txt" 2>&1
  grep -E "^tb_floppybuf|^PASS|^FAIL|MISMATCH" "$d/sim.txt" | head -n 6 | sed "s/^/$1 sim: /"
  grep -q '^PASS tb_floppybuf' "$d/sim.txt"
}

rc=0
case $1 in
  synth)
    synth_one new "$rtl/paula_floppy.v" || { echo "FAIL: the copy does not pass the lint"; rc=1; }
    ;;
  sim)
    sim_one new || { echo "FAIL: the copy's netlist differs from the shared file"; rc=1; }
    ;;
  *) echo "usage: synth_check.sh synth|sim"; exit 2 ;;
esac
[ $rc = 0 ] && echo "PASS amiga1200_floppybuf_synth $1"
exit $rc
