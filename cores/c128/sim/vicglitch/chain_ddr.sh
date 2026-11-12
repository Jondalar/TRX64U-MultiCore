#!/bin/sh
# builder chain: the synthetic bench, then the game on the DDR2 build (they share the build)
sh cores/c128/sim/run_vicblk.sh ddr
echo ==== robots ddr
sh cores/c128/sim/run_robots.sh ddr ${1:-6500}
