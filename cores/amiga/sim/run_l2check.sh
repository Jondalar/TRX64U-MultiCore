#!/bin/sh
# T85: amiga_core behind the real mig_arb: frames, three buffers, picture position, keys; and with 4 MB fast RAM
# (Zorro II autoconfig by the test ROM, the CPU's direct RAM port); and a track read by disk DMA from an ADF in the window
# (Paula's MFM engine, the sector service, the DDR2), decoded and compared; and a hard disk sector read by the ROM through the Gayle IDE registers (PIO) via the mailbox.
# PASS when all runs have errors=0.
# run_l2check.sh [a500|a1200|all] (T100 D10: a bench belongs to one core's lane): a500 = the Amiga core (fx68k, OCS/ECS), a1200 = the Amiga 1200
# core's build (TG68K, AGA, its own copies of paula_floppy.v / amiga_sdc.v; the same bench with its power-up configuration), all = both (by hand).
set -e
here=$(cd "$(dirname "$0")" && pwd)
which=${1:-all}
case "$which" in a500|a1200|all) ;; *) echo "usage: run_l2check.sh [a500|a1200|all]"; exit 2 ;; esac
a500() { [ "$which" != a1200 ]; }
a1200() { [ "$which" != a500 ]; }
if a500; then
sh "$here/run_l2.sh" > /tmp/amiga_l2.log 2>&1 || { tail -30 /tmp/amiga_l2.log; echo FAIL build; exit 1; }
grep -E 'ok  |FAIL|PASS' /tmp/amiga_l2.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2.log || { echo "FAIL amiga_l2"; exit 1; }
FAST=1 MODE=fast sh "$here/run_l2.sh" > /tmp/amiga_l2f.log 2>&1 || { tail -30 /tmp/amiga_l2f.log; echo FAIL build fast; exit 1; }
grep -E 'fast RAM|FAIL|PASS' /tmp/amiga_l2f.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2f.log || { echo "FAIL amiga_l2 fast"; exit 1; }
FRAMES=18 DISK=1 MODE=disk sh "$here/run_l2.sh" > /tmp/amiga_l2d.log 2>&1 || { tail -30 /tmp/amiga_l2d.log; echo FAIL build disk; exit 1; }
grep -E "disk|track|sector|FAIL|PASS" /tmp/amiga_l2d.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2d.log || { echo "FAIL amiga_l2 disk"; exit 1; }
FRAMES=14 IDE=1 MODE=ide sh "$here/run_l2.sh" > /tmp/amiga_l2i.log 2>&1 || { tail -30 /tmp/amiga_l2i.log; echo FAIL build ide; exit 1; }
grep -E "IDE|four words|FAIL|PASS" /tmp/amiga_l2i.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2i.log || { echo "FAIL amiga_l2 ide"; exit 1; }
KEY=1 MODE=key sh "$here/run_l2.sh" > /tmp/amiga_l2k.log 2>&1 || { tail -30 /tmp/amiga_l2k.log; echo FAIL build key; exit 1; }
grep -E "CIA|FAIL|PASS" /tmp/amiga_l2k.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2k.log || { echo "FAIL amiga_l2 key"; exit 1; }
AUDIO=1 MODE=audio sh "$here/run_l2.sh" > /tmp/amiga_l2a.log 2>&1 || { tail -30 /tmp/amiga_l2a.log; echo FAIL build audio; exit 1; }
grep -E "audio|FAIL|PASS" /tmp/amiga_l2a.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2a.log || { echo "FAIL amiga_l2 audio"; exit 1; }
fi
if a1200; then
# T109: the same tone with the Amiga 1200 core's power-up configuration (AGA, 2 MB chip RAM, HDF sizes
# in sectors - the three generics cores/amiga1200/core_sources.tcl passes). Alex heard nothing from the
# A1200 on 2026-10-16 while the A500 sounded fine; this says whether the gateware is where it goes.
AUDIO=1 MODE=audio CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 OUT_SUFFIX=a12a sh "$here/run_l2.sh" > /tmp/amiga_l2a12.log 2>&1 || { tail -30 /tmp/amiga_l2a12.log; echo FAIL build audio a1200; exit 1; }
grep -E "audio|FAIL|PASS" /tmp/amiga_l2a12.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2a12.log || { echo "FAIL amiga_l2 audio a1200"; exit 1; }
# T97 5.11: the A1200 build (TG68K, its block-RAM copies of paula_floppy.v and amiga_sdc.v) reads a track by disk DMA and a hard disk sector
# through the Gayle IDE mailbox, the same checks as above
A12="TG68K=1 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1"
env $A12 FRAMES=18 DISK=1 MODE=disk OUT_SUFFIX=a12d sh "$here/run_l2.sh" > /tmp/amiga_l2d12.log 2>&1 || { tail -30 /tmp/amiga_l2d12.log; echo FAIL build disk a1200; exit 1; }
grep -E "disk|track|sector|FAIL|PASS" /tmp/amiga_l2d12.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2d12.log || { echo "FAIL amiga_l2 disk a1200"; exit 1; }
env $A12 FRAMES=14 IDE=1 MODE=ide OUT_SUFFIX=a12i sh "$here/run_l2.sh" > /tmp/amiga_l2i12.log 2>&1 || { tail -30 /tmp/amiga_l2i12.log; echo FAIL build ide a1200; exit 1; }
grep -E "IDE|four words|FAIL|PASS" /tmp/amiga_l2i12.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2i12.log || { echo "FAIL amiga_l2 ide a1200"; exit 1; }
fi
if a500; then
# T85 disk change: the ROM polls DF0 like trackdisk (select, read /CHNG, step when low, read again) while the bench mounts, ejects, mounts and
# announces again; every insert must be noticed. The A1200 build runs the same.
FRAMES=18 DSKCHG=1 MODE=dskchg OUT_SUFFIX=dc sh "$here/run_l2.sh" > /tmp/amiga_l2c.log 2>&1 || { tail -30 /tmp/amiga_l2c.log; echo FAIL build dskchg; exit 1; }
grep -E "dskchg|FAIL|PASS" /tmp/amiga_l2c.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2c.log || { echo "FAIL amiga_l2 dskchg"; exit 1; }
fi
if a1200; then
env $A12 FRAMES=18 DSKCHG=1 MODE=dskchg OUT_SUFFIX=a12c sh "$here/run_l2.sh" > /tmp/amiga_l2c12.log 2>&1 || { tail -30 /tmp/amiga_l2c12.log; echo FAIL build dskchg a1200; exit 1; }
grep -E "dskchg|FAIL|PASS" /tmp/amiga_l2c12.log
grep -q 'PASS tb_l2 errors=0' /tmp/amiga_l2c12.log || { echo "FAIL amiga_l2 dskchg a1200"; exit 1; }
fi
echo "PASS amiga_l2 ($which)"
