#!/bin/sh
# Formal equivalence: old (sim/kb_equiv) vs new (ROM table) c64u_keyboard.
# Check only, not a CI gate. Needs yosys + sby (OSS CAD Suite) with the slang plugin.
#   sh run.sh          k-induction proof (unbounded), about a second
#   sh run.sh mutant   the same proof against a deliberately broken copy: must FAIL
set -e
cd "$(dirname "$0")"
export PATH="$HOME/.cache/trx64-oss/oss-cad-suite/bin:$PATH"
case "${1:-ind}" in
ind)
    nice -n 15 sby -f kb_equiv.sby ;;
mutant)
    # new design with the PCA9555 read address changed to the write address, and
    # a second one that inverts the key polarity of the row scan
    rc=0
    for m in "addr_r|s/ADDR_R = 8'h43/ADDR_R = 8'h42/" "keys_pol|s/keys_a\[g\*8 +: 8\] <= ~rx/keys_a[g*8 +: 8] <= rx/"; do
        name=${m%%|*}; ex=${m#*|}
        rm -rf mutant_src; mkdir -p mutant_src
        sed "$ex" ../../c64u_keyboard.v > mutant_src/c64u_keyboard.v
        if cmp -s ../../c64u_keyboard.v mutant_src/c64u_keyboard.v; then echo "mutation $name did not apply"; exit 1; fi
        sed "s#\.\./\.\./c64u_keyboard\.v#mutant_src/c64u_keyboard.v#" kb_equiv.sby > mutant.sby
        if nice -n 15 sby -f mutant.sby >/dev/null 2>&1; then echo "MUTANT $name NOT CAUGHT"; rc=1
        else echo "mutant $name caught (expected)"; fi
    done
    rm -rf mutant_src mutant mutant.sby
    exit $rc ;;
esac
