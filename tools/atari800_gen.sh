#!/bin/sh
# T88: cores/atari800/gen = Watson's files (cores/atari800/rtl, byte for byte as vendored) + tools/patches/atari800/*.patch
# (what VHDL-2008 needs that Quartus did not) + the core's clock enable on every clocked process (tools/atari800_ce28.py, D3).
# rtl/ is never edited; gen/ is generated and committed (the CI has no checkout of Watson's tree). Run it after every change of a
# patch or of the transformation; `tools/atari800_gen.sh --check` fails if gen/ is not what the inputs make.
#   Left out of gen/ (replaced by our own entity of the same name in cores/atari800/trx, or not used): the Quartus FIFOs
#   fifo_receive / fifo_transmit, and atari800core_simple_sdram (our a8_machine is the top, the app reaches RAM and ROM by a window).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
rtl=$here/cores/atari800/rtl
gen=$here/cores/atari800/gen
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/in"
for d in a8core components sioemu; do cp -R "$rtl/$d" "$tmp/in/$d"; done
rm "$tmp/in/sioemu/fifo_receive.vhd" "$tmp/in/sioemu/fifo_transmit.vhd" "$tmp/in/a8core/atari800core_simple_sdram.vhd"
for p in "$here"/tools/patches/atari800/*.patch; do
  [ -f "$p" ] || continue
  (cd "$tmp/in" && patch -p1 -s < "$p") || { echo "patch failed: $p"; exit 1; }
done
rm -rf "$tmp/out"
python3 "$here/tools/atari800_ce28.py" "$tmp/in" "$tmp/out"
if [ "${1:-}" = --check ]; then
  diff -r "$tmp/out" "$gen" >/dev/null || { echo "cores/atari800/gen is not what tools/atari800_gen.sh makes: run it"; exit 1; }
  echo "gen is current"
else
  rm -rf "$gen"
  mkdir -p "$gen"
  cp -R "$tmp/out/." "$gen/"
fi
