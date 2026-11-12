#!/usr/bin/env bash
# The version string of a build (T172 D2/D4), one answer for the installer manifest and the host app:
#   main              <VERSION>+<sha8>        (VERSION = the file at the repository root, SemVer x.y.z, bumped by hand)
#   tag v*            <VERSION>               (the release; the tag itself is v<VERSION>)
#   any other branch  <branch>@<sha8>
# Read from GITHUB_REF_NAME / GITHUB_REF_TYPE, else from the checkout.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ref="${GITHUB_REF_NAME:-$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)}"
sha8="$(git -C "$ROOT" rev-parse --short=8 HEAD)"
ver="$(tr -d ' \t\r\n' < "$ROOT/VERSION")"
if [ "${GITHUB_REF_TYPE:-}" = tag ]; then printf '%s\n' "$ver"
elif [ "$ref" = main ]; then printf '%s+%s\n' "$ver" "$sha8"
else printf '%s@%s\n' "$ref" "$sha8"; fi
