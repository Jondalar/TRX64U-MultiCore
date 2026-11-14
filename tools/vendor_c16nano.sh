#!/bin/sh
# T90: copy the parts of C16Nano (https://github.com/MiSTle-Dev/C16Nano, GPL-3.0) that the C264 core uses into
# cores/c264/rtl/src, unchanged. Only files of the list below are taken, and only when the checkout is the commit the spec was
# written against and every file has the SHA-256 it had when it was read (docs/specs/done/T90-c16-core.md, section 1). A different
# commit or a changed file stops the script: the licence reading of section 1 is of THESE bytes.
# Usage: tools/vendor_c16nano.sh <C16Nano checkout>
set -e
src=${1:?C16Nano checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/c264/rtl/src
want=82fed408f899509c4b569c02c1bc4bcf23017276
have=$(git -C "$src" rev-parse HEAD)
[ "$have" = "$want" ] || { echo "C16Nano is at $have, the spec read $want"; exit 1; }
sums="
d42b84a4befa1c135e6fa26fbf39e33938f14d0baef160cadd8e1abf9db6e7c8  src/ted.v
3d387efd8c221852f22b24c9bf91417bbec28f5ca4530accf2e86541c2f405c7  src/colors_to_rgb.v
8923ff7a4ef10c8f30bb3ce847573b2429c86f19b31ca18452f9ee5fc4ca38c9  src/t65/T65.vhd
4ee87d3d72e256a7a0de0c0a9ac902950f5cfa7d18504fb06f5607c4e078eaf7  src/t65/T65_ALU.vhd
79b84bb75812cc28928214c36cc801f883c5e6b352a225b94068ab2d381b1f0e  src/t65/T65_MCode.vhd
1d9b584832dba92d57e6510675791daa1ef737f349ebe145db1fe31c24675dfa  src/t65/T65_Pack.vhd
3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986  LICENSE
"
(cd "$src" && echo "$sums" | sed '/^$/d' | shasum -a 256 -c -) || { echo "a file differs from the one that was read"; exit 1; }
# the clause that the VIC-20 files carried (MikeJ / WoS "Replay hardware only") must not be in any of them
if grep -il "replay hardware\|commercial use is forbidden\|not supposed to be used" "$src/src/ted.v" "$src/src/colors_to_rgb.v" "$src"/src/t65/*.vhd; then
  echo "a restricting clause is in a file"; exit 1
fi
mkdir -p "$dst/t65"
cp "$src/src/ted.v" "$src/src/colors_to_rgb.v" "$dst/"
cp "$src"/src/t65/T65.vhd "$src"/src/t65/T65_ALU.vhd "$src"/src/t65/T65_MCode.vhd "$src"/src/t65/T65_Pack.vhd "$dst/t65/"
cp "$src/LICENSE" "$here/cores/c264/rtl/LICENSE"
echo "cores/c264/rtl/src filled from C16Nano $want"
