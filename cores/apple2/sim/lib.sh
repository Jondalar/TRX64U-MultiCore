# T89 benches: shared by the run scripts. GHDL only in a container (the Mac: container run --rm -v "$PWD":/w -w /w vc20-ghdl sh ...).
# A run script sets `here` (this directory), sources this file, builds a bench with a2_build and runs it. a2_ctl (only the disk write
# benches use it, tools/check_controls.py): a copy of the design with one line changed, which the bench has to FAIL on (output marked "ctl| ").
root=$(cd "$here/../../.." && pwd)
A2_GHDL_OPTS="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
A2_SRC="
  $root/cores/apple2/rtl/src/R65Cx2.vhd
  $root/cores/apple2/rtl/src/timing_generator.vhd $root/cores/apple2/rtl/src/video_generator.vhd
  $root/cores/apple2/rtl/src/drive_ii.vhd $root/cores/apple2/rtl/src/disk_ii.vhd $root/cores/apple2/rtl/src/apple2.vhd
  $root/cores/apple2/rtl/trx/a2_ram.vhd $root/cores/apple2/rtl/trx/a2_glue.vhd $root/cores/apple2/rtl/trx/a2_trk.vhd $root/cores/apple2/rtl/trx/a2_hdd.vhd
  $root/cores/apple2/rtl/trx/a2_palette_pkg.vhd $root/cores/apple2/rtl/trx/a2_pix.vhd $root/cores/apple2/rtl/trx/a2_win.vhd
  $root/cores/apple2/rtl/trx/a2_regs.vhd $root/cores/apple2/rtl/trx/a2_aux.vhd $root/cores/apple2/rtl/trx/a2_core.vhd"
a2_workdir() { d="$here/build/$1"; mkdir -p "$d"; echo "$d"; }
a2_fail_logs() { for f in "$1"/ghdl_*.log; do [ -f "$f" ] && tail -n 30 "$f"; done; exit 1; }
# a2_build <top> <dir> [extra vhdl files before the bench]: analyse the core, the bench package, the harness and the bench, elaborate
a2_build() {
  top=$1; dir=$2; shift 2
  rm -f "$dir"/ghdl_*.log
  ( cd "$dir" && nice -n 15 ghdl -a $A2_GHDL_OPTS $A2_SRC "$here/a2_tb_pkg.vhd" "$here/a2_harness.vhd" "$@" "$here/$top.vhd" > ghdl_a.log 2>&1 \
      && nice -n 15 ghdl -e $A2_GHDL_OPTS "$top" > ghdl_e.log 2>&1 ) || a2_fail_logs "$dir"
}
# a2_ctl <top> <basedir> <name> <file> <sed expression>: a copy of the built design with one line of <file> changed; prints its directory. GHDL obsoletes the units
# that depend on a re-analysed unit, so the file and everything after it in the source order, the harness and the bench are analysed again.
a2_ctl() {
  top=$1; base=$2; name=$3; file=$4; expr=$5
  dir="${base}_ctl_$name"; rm -rf "$dir"; cp -R "$base" "$dir"
  mkdir -p "$dir/mut"
  sed "$expr" "$file" > "$dir/mut/$(basename "$file")"
  if cmp -s "$file" "$dir/mut/$(basename "$file")"; then echo "ctl| $name: the mutation did not change $(basename "$file")" >&2; exit 1; fi
  redo=""; on=""
  for f in $A2_SRC; do
    if [ "$f" = "$file" ]; then on=1; f="$dir/mut/$(basename "$file")"; fi
    [ -z "$on" ] || redo="$redo $f"
  done
  case "$on" in 1) ;; *) redo="$dir/mut/$(basename "$file")" ;; esac
  ( cd "$dir" && nice -n 15 ghdl -a $A2_GHDL_OPTS $redo "$here/a2_harness.vhd" "$here/$top.vhd" > ghdl_a.log 2>&1 \
      && nice -n 15 ghdl -e $A2_GHDL_OPTS "$top" > ghdl_e.log 2>&1 ) || a2_fail_logs "$dir"
  echo "$dir"
}
