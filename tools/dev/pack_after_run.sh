#!/bin/bash
set -e
T=$(awk '/token:/{print $2; exit}' "$HOME/.config/tea/config.yml")
while :; do s=$(curl -s -H "Authorization: token $T" "http://gitea.example/api/v1/repos/TREX/TRX64-Ultimate/actions/runs/786" | python3 -c 'import json,sys; r=json.load(sys.stdin); print(r.get("status"), r.get("conclusion"))'); case "$s" in completed*) break;; esac; sleep 90; done
echo "run 402: $s"
case "$s" in *success*) ;; *) exit 1;; esac
C=$(ssh builder 'cd /srv/trx64-build/cache/bit; for d in $(ls -t | head -6); do [ "$(cat $d/from)" = 6d9b1aec ] && echo $d && break; done')
echo "cache $C"
D=$CLAUDE_JOB_DIR/tmp/vc20d; mkdir -p $D
for f in c64_basic.bit c64_basic_pb_l2_partial.bit c64_basic_pb_l2_vc20_partial.bit; do scp -q builder:/srv/trx64-build/cache/bit/$C/$f $D/; done
cd /path/to/Development/TRX64-Ultimate-wt-bootapp
# T84 D5b: the host key of the full bitstream this run produced
HK="$(python3 tools/cor_pack.py hostkey $D/c64_basic.bit)"
python3 tools/cor_pack.py pack --host-key "$HK" -o $D/vc20.cor --name VC-20 --version 6d9b1ae --partial $D/c64_basic_pb_l2_vc20_partial.bit --app /path/to/Development/1541ultimate-wt-vc20app/target/u64ii/riscv/ultimate/result_vc20/ultimate.bin --load 0x30000 --entry 0x30000 >/dev/null
python3 tools/cor_pack.py pack --host-key "$HK" -o $D/trx64.cor --name "TRX64 Ultimate (C64)" --version 1.0-rc1 --partial $D/c64_basic_pb_l2_partial.bit --app $CLAUDE_JOB_DIR/tmp/flash/rc1.bin --load 0x30000 --entry 0x30000 >/dev/null
line() { python3 tools/cor_pack.py check $1 | python3 -c 'import json,sys; t=sys.stdin.read(); j=json.loads(t[t.index("{"):]); print(j["partial_sha256"])'; }
VS=$(line $D/vc20.cor); TS=$(line $D/trx64.cor)
printf '# TRX64 boot menu\ncountdown=5\ndefault=1\ncore=/USB1/cores/trx64.cor|TRX64 Ultimate (C64)|%s|%s\ncore=/USB1/cores/vc20.cor|VC-20|%s|%s\n' "$TS" "$(stat -f %z $D/trx64.cor)" "$VS" "$(stat -f %z $D/vc20.cor)" > $D/boot.cfg
cat $D/boot.cfg
echo ready
