#!/usr/bin/env bash
# T87 S4/S5 on the keys (no Vivado): in a scratch clone, a change in a core file moves the OOC key and
# the core key but NOT the host key; a change in a host file moves the host key and the core key (it
# depends on the host) but NOT the OOC key; two clones with the same content have the same keys
# whatever their branch is called; the build options land where D3/D4 say.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
git clone -q . "$W/a" 2>/dev/null
git -C . ls-files -z -m -o --exclude-standard -- ci host cores vendor tools | while IFS= read -r -d '' f; do
  [ -f "$f" ] && { mkdir -p "$W/a/$(dirname "$f")"; cp "$f" "$W/a/$f"; }
done
git -C "$W/a" add -A
git -C "$W/a" -c user.name=t -c user.email=t@t commit -q --allow-empty -m scratch
git clone -q "$W/a" "$W/b"
P=xc7a50tfgg484-1
keys() { # keys <repo>: H O C
  ( ROOT="$1"; export ROOT; source "$1/ci/lib.sh"; ROOT="$1"
    H="$(host_key $P "$1/host/layers.txt")"; O="$(ooc_key $P c64ref "$1/host/layers.txt")"
    echo "$H $O $(core_key "$H" "$O")" )
}
read -r H0 O0 C0 <<< "$(keys "$W/a")"
read -r H1 O1 C1 <<< "$(keys "$W/b")"
[ "$H0 $O0 $C0" = "$H1 $O1 $C1" ] || { echo "FAIL: same content, other clone, other keys"; exit 1; }
# a file of the REFERENCE core moves the host too (the static is routed with it, its partial is the host run's)
echo "// x" >> "$W/b/cores/c64ref/c64_mem_c64ref.v"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" != "$H0" ] && [ "$O" != "$O0" ] && [ "$C" != "$C0" ] || { echo "FAIL: a reference-core file must move host, OOC and core key"; exit 1; }
git -C "$W/b" reset -q --hard
# T115: cores/c64u is a source tree (no descriptor, ignore:), its files do not move the host key; the five TRX modules the host's top reads sit in cores/common (shared:)
echo "// x" >> "$W/b/cores/c64u/c64_mem_c64u.v"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" = "$H0" ] && [ "$O" = "$O0" ] || { echo "FAIL: a file of c64u (no longer the reference core) moved the host or the reference OOC key"; exit 1; }
git -C "$W/b" reset -q --hard
echo "// x" >> "$W/b/cores/common/trx_io.v"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" != "$H0" ] && [ "$O" != "$O0" ] || { echo "FAIL: trx_io.v is read by host/build.tcl (shared:) and must move host and OOC key"; exit 1; }
git -C "$W/b" reset -q --hard
# T174 R2: host/check/ (the Vivado session's observers) is no input - an edit there moves no key; a new file anywhere else under host/ is in no section until listed (ci/layers_check.sh)
echo "# x" >> "$W/b/host/check/trx_constraints.tcl"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" = "$H0" ] && [ "$O" = "$O0" ] && [ "$C" = "$C0" ] || { echo "FAIL: an edit of host/check/ moved a key (T174 R2: the false miss of run 1133)"; exit 1; }
git -C "$W/b" reset -q --hard
echo "# x" >> "$W/b/host/core.tcl"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" != "$H0" ] || { echo "FAIL: control: an edit of host/core.tcl (a real input) did not move the host key"; exit 1; }
git -C "$W/b" reset -q --hard
# a second core (the VC-20): its files move its own keys and not the host's
keys2() { ( ROOT="$1"; export ROOT; source "$1/ci/lib.sh"; ROOT="$1"; L="$1/host/layers.txt"
    HH="$(host_key $P "$L")"; OO="$(ooc_key $P vc20 "$L")"; echo "$HH $OO $(core_key "$HH" "$OO")" ); }
read -r HX OX CX <<< "$(keys2 "$W/b")"
echo "// x" >> "$W/b/cores/vc20/layer2_vc20.v"; git -C "$W/b" add -A
read -r H O C <<< "$(keys2 "$W/b")"
[ "$H" = "$HX" ] || { echo "FAIL: a file of a non-reference core moved the host key"; exit 1; }
[ "$O" != "$OX" ] && [ "$C" != "$CX" ] || { echo "FAIL: a file of a core did not move its own keys"; exit 1; }
git -C "$W/b" reset -q --hard
# a core's logo is a picture, not gateware (T114): it moves no key; the control is the .v file just above
mkdir -p "$W/b/cores/vc20/logo"; echo "x" >> "$W/b/cores/vc20/logo/icon.rgb565"; git -C "$W/b" add -A
read -r H O C <<< "$(keys2 "$W/b")"
[ "$H $O $C" = "$HX $OX $CX" ] || { echo "FAIL: a logo file moved a key"; exit 1; }
git -C "$W/b" reset -q --hard
# a host file
echo "// x" >> "$W/b/host/ddr2_lean.v"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" != "$H0" ] || { echo "FAIL: a host file did not move the host key"; exit 1; }
[ "$O" = "$O0" ] || { echo "FAIL: a host file moved the OOC key"; exit 1; }
[ "$C" != "$C0" ] || { echo "FAIL: the core key does not depend on the host"; exit 1; }
git -C "$W/b" reset -q --hard
# a shared file moves both
echo "# x" >> "$W/b/host/gideon_vhdl.tcl"; git -C "$W/b" add -A
read -r H O C <<< "$(keys "$W/b")"
[ "$H" != "$H0" ] && [ "$O" != "$O0" ] || { echo "FAIL: a shared file must move host and OOC key"; exit 1; }
git -C "$W/b" reset -q --hard
# options: SID is the core's, the UCI block is the host's
H=$(TRX_SID=redip2 keys "$W/b" | cut -d' ' -f1); O=$(TRX_SID=redip2 keys "$W/b" | cut -d' ' -f2)
[ "$H" != "$H0" ] && [ "$O" != "$O0" ] || { echo "FAIL: TRX_SID must move the core key and the host (reference core)"; exit 1; }
H=$(TRX_FEATURES=none keys "$W/b" | cut -d' ' -f1); O=$(TRX_FEATURES=none keys "$W/b" | cut -d' ' -f2)
[ "$H" != "$H0" ] && [ "$O" != "$O0" ] || { echo "FAIL: TRX_FEATURES=none must move both (host blocks and core blocks off)"; exit 1; }
H=$(TRX_FEATURES=reu,sampler,turbo keys "$W/b" | cut -d' ' -f1)
[ "$H" != "$H0" ] || { echo "FAIL: host blocks off must move the host key"; exit 1; }
echo "layers test ok"
