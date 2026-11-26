#!/bin/sh
# T175 / T260 S1-S4: the probe engine v2, the pin bank, the capture and the pin-table ROM of the diag core (cores/diag/rtl probe.v, diag_regs.v, diag_cap.v, diag_pinrom.v) through the
# register window (cores/diag/sim/tb_probe.v): every pin of the table into the snapshot (S1), DRIVE / guard / refusal / release and the timing of the ops against the TB's own clock
# counter (S2), the ROM read through registers compared with tools/diag_pins_gen.py --dump (S3), a 4 pin x 4096 sample capture into a DDR2 client model, the register reader, partial lines,
# refusals, ABORT and overrun (S4).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/probe; mkdir -p "$out"
rtl=$root/cores/diag/rtl
rm -f "$out"/*.log
iverilog -g2012 -I "$rtl" -o "$out/base.vvp" "$here/tb_probe.v" "$rtl/probe.v" "$rtl/diag_regs.v" "$rtl/diag_cap.v" "$rtl/diag_pinrom.v"
vvp -n "$out/base.vvp" > "$out/base.log" || true
grep "^PASS\|^FAIL" "$out/base.log"
grep -q '^PASS tb_probe errors=0' "$out/base.log" || { grep -v '^ROM ' "$out/base.log" | head -60; echo "FAIL run.sh: the engine bench fails"; exit 1; }
# S3: what the application reads from the ROM is the table
grep '^ROM ' "$out/base.log" | sed 's/^ROM //' > "$out/rom.txt"
python3 "$root/tools/diag_pins_gen.py" --dump > "$out/table.txt"
cmp "$out/rom.txt" "$out/table.txt" || { diff "$out/rom.txt" "$out/table.txt" | head; echo "FAIL run.sh: the ROM read through the registers differs from pins.toml"; exit 1; }
echo "PASS S3 the pin-table ROM read through registers 0A0 / 0C0-0DF equals pins.toml ($(wc -l < "$out/rom.txt") pins)"
echo "PASS probe (engine v2, pin bank, capture and ROM bench pass)"
