#!/bin/sh
# T181 D2: tb_hotkey (iverilog) on host/app_input.v.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
run() { # dir
  iverilog -g2012 -y "$1" -o "$out/t.vvp" "$here/tb_hotkey.v" 2>"$out/err" || { cat "$out/err"; echo "COMPILE ERROR"; return 2; }
  vvp -n "$out/t.vvp"
}
run "$root/host" > "$out/base.log"; tail -5 "$out/base.log"
grep -q '^PASS hotkey' "$out/base.log" || { echo "FAIL base bench"; exit 1; }
echo "PASS hotkey bench"
