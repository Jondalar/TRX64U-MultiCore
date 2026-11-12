#!/usr/bin/env bash
# Fetch a bitstream the NAS runner built and load it into the C64 Ultimate's
# FPGA SRAM through the USB-Blaster (nothing is written to flash).
#
#   tools/load.sh [design] [version]      # defaults: hdmi_test, main-latest
#   tools/load.sh path/to/file.bit        # load a local file
#
# Back to the flashed image: unplug the Blaster's USB, power off, power on.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../ci/pkg.env"
GITEA="${GITEA_URL:-http://gitea.example}"
TEA_CONFIG="${TEA_CONFIG:-$HOME/.config/tea/config.yml}"
TOKEN="${GITEA_TOKEN:-$(awk '/token:/{print $2; exit}' "$TEA_CONFIG" 2>/dev/null || true)}"

if [[ "${1:-}" == *.bit ]]; then
    bit="$1"
else
    design="${1:-hdmi_test}"; version="${2:-main-latest}"
    bit="$(mktemp -d)/$design.bit"
    curl -sf -H "Authorization: token $TOKEN" -o "$bit" \
        "$GITEA/api/packages/TREX/generic/$MC_PKG/$version/$design.bit" \
        || { echo "no $design.bit in $MC_PKG $version" >&2; exit 1; }
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
