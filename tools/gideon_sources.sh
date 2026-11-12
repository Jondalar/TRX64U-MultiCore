#!/bin/sh
# vendor/gideon is our own copy of the parts of Gideon's 1541ultimate that we use (T120, Alex 2026-11-01: "own copy"); it is in git,
# there is no submodule behind it any more.
#
#   tools/gideon_sources.sh                       check that the copy is there (what the builds and benches call; changes nothing)
#   tools/gideon_sources.sh <1541ultimate dir>    renew the copy from a checkout of the fork: tools/vendor_gideon.sh (the VHDL, with the patches of
#                                                 tools/patches/gideon, VENDOR.md) and tools/vendor_gideon_app.sh (the software). The checkout needs its nested
#                                                 repositories software/lwip and software/httpd (git submodule update --init).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
if [ $# -ge 1 ]; then
  "$here/tools/vendor_gideon.sh" "$1" >/dev/null
  "$here/tools/vendor_gideon_app.sh" "$1" >/dev/null
  echo "vendor/gideon renewed from $1 ($(git -C "$1" rev-parse --short=8 HEAD))"
  exit 0
fi
[ -d "$here/vendor/gideon/fpga" ] || { echo "vendor/gideon is missing from this checkout (it is in git: tools/gideon_sources.sh <1541ultimate dir> makes it)" >&2; exit 1; }
