#!/usr/bin/env bash
# Fetch a bitstream of a CI pre-release of the public builds repository (tools/builds.sh, no token) and load it into the C64 Ultimate's
# FPGA SRAM through the USB-Blaster (nothing is written to flash).
#
#   tools/load.sh [design] [version]      # defaults: hdmi_test, the newest main-<sha8> release (builds.sh latest)
#   tools/load.sh path/to/file.bit        # load a local file
#   version is a release tag of the builds repository, e.g. main-51e89821, or main-latest
#
# Back to the flashed image: unplug the Blaster's USB, power off, power on.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "${1:-}" == *.bit ]]; then
    bit="$1"
else
    design="${1:-hdmi_test}"; version="${2:-main-latest}"
    [ "$version" != main-latest ] || version="$("$HERE/builds.sh" latest)"
    bit="$(mktemp -d)/$design.bit"
    "$HERE/builds.sh" fetch "$version" "$design.bit" "$bit" \
        || { echo "no $design.bit in the builds repository, release $version" >&2; exit 1; }
    echo "fetched $design $version ($(shasum -a 256 "$bit" | cut -c1-16))"
fi
for attempt in 1 2 3; do
    if openFPGALoader -c usb-blaster -m "$bit" 2>&1 | grep -q "done 1"; then
        echo "loaded into SRAM: $bit"
        exit 0
    fi
    sleep 1
done
echo "load failed (is the C64U on and the Blaster on the dock?)" >&2
exit 1
