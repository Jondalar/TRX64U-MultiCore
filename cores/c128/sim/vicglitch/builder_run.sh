#!/bin/sh
# on the builder: run a sim script of the checkout in ~/scratch/vicglitch inside trx64-sim-env, detached (docker -d), niced, with a timeout.
#   sh builder_run.sh <logname> <script relative to the checkout> [args]      log: ~/scratch/vicglitch/<logname>.log; container vg_<logname>
cd "$HOME/scratch/vicglitch" || exit 1
log=$1; shift
docker rm -f "vg_$log" > /dev/null 2>&1
docker run -d --name "vg_$log" --user "$(id -u):$(id -g)" -v "$PWD:/src" -v "$HOME/scratch/c128rom:/rom:ro" -e C128_ROMDIR=/rom -e C128_DEV -e C128_DEVLEN -e C128_ARGS -e C128_FRAME -w /src trx64-sim-env:4 \
    sh -c "nice -n 15 timeout 14400 sh $* > /src/$log.log 2>&1"
