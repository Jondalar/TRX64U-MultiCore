#!/usr/bin/env bash
# Boot app host tests (T84): the .cor reader and SHA-256 of host-app against cor_pack.py bundles.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
CXX="${CXX:-c++}"
"$ROOT/tests/ctlcache_selftest.sh"
# T100 D13, D14: every test program and every negative control below is a unit (tests/ctlcache.sh): served from the cache when what it reads
# is unchanged. cc_block wraps the ones that are shell functions (compile and run, recorded whole), the keyed helpers (bc_control, mutant)
# use the compiler's dependency list. T174 D5.4: negative controls remain only for flash operations and persistence (ci/controls.allow.d/hostapp.allow).
source "$ROOT/tests/ctlcache.sh"
CC_SUITE=hostapp
trap 'cc_summary; rm -rf "$T"' EXIT
CC_EXTRA="$H/sha256.c"   # what $T/sha256.o is made of
python3 - "$T" <<'PY'
import os, struct, sys
sys.path.insert(0, os.path.join(os.environ.get("ROOT", "."), "tools"))
sys.path.insert(0, "tools")
import cor_pack as C
t = sys.argv[1]
hdr = C.BIT_MAGIC
for key in b"abcd":
    hdr += bytes([key]) + struct.pack(">H", 6) + b"xxxxx\0"
data = b"\xFF" * 8 + C.SYNC + bytes(range(256)) * 600
bit = hdr + b"e" + struct.pack(">I", len(data)) + data
app = bytes((i * 5) & 0xFF for i in range(70000))
open(t + "/app.bin", "wb").write(app)
open(t + "/good.cor", "wb").write(C.pack('VIC-20 "test"', "0.1", bit, app, 0x30000, 0x30010,
                                         "820eded2", icon=bytes(2048)))
open(t + "/c2.cor", "wb").write(C.pack("x", "1", bit, app, 0x30000, 0x30000, "820eded2", contract=3))
# T84 D5b: a bundle routed against another host
open(t + "/h2.cor", "wb").write(C.pack("x", "1", bit, app, 0x30000, 0x30000, "9ae17385"))
PY
# T115 §7: large .cor packages made by cor_pack.py pack-large (the bitstreams are synthetic: header, IDCODE, AXSS, frames, DESYNC)
python3 - "$T" <<'PY'
import json, os, struct, sys
sys.path.insert(0, "tools")
import cor_pack as C
import test_cor_pack as TC
t = sys.argv[1]
app = bytes((i * 7 + 1) & 0xFF for i in range(70001))        # not a multiple of 128: the staging pads it
icon = bytes((i * 3) & 0xFF for i in range(2048))
bit = TC.full_bit(frames=3000)
open(t + "/lbit.bin", "wb").write(bit)
open(t + "/lapp.bin", "wb").write(app)
open(t + "/licon.bin", "wb").write(icon)
def pk(**kw):
    a = dict(cid="amiga1200", name='Amiga 1200 "test"', version="0.3.0", bit=bit, app=app, icon=icon)
    a.update(kw)
    return C.pack_large(**a)
open(t + "/large.cor", "wb").write(pk())
open(t + "/large_gbl4.cor", "wb").write(pk(bit=TC.full_bit(axss=None)))
C.IDCODE_XC7A50T = 0x03631093                                 # the packer refuses a 100T; this file is made around it
open(t + "/large100t.cor", "wb").write(pk(bit=TC.full_bit(idcode=0x03631093)))
C.IDCODE_XC7A50T = 0x0362C093
def hand(m_over, bit2):
    m, r = C.parse(pk(bit=bit2))
    m.update(m_over)
    body = C.rec(1, json.dumps(m, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r[3]) + C.rec(4, r[4]) + C.rec(5, r[5])
    return C.MAGIC + struct.pack("<I", len(body)) + body
open(t + "/large_idm.cor", "wb").write(hand({"idcode": "03631093"}, bit))                # manifest 100T, bitstream 50T
C.IDCODE_XC7A50T = 0x03631093
b100 = TC.full_bit(idcode=0x03631093)
m100, r100 = C.parse(pk(bit=b100))
C.IDCODE_XC7A50T = 0x0362C093
m100["idcode"] = "0362c093"                                  # manifest says 50T, the bitstream is the 100T's
body = C.rec(1, json.dumps(m100, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r100[3]) + C.rec(4, r100[4]) + C.rec(5, r100[5])
open(t + "/large_bit100.cor", "wb").write(C.MAGIC + struct.pack("<I", len(body)) + body)
open(t + "/large_mapm.cor", "wb").write(hand({"map": "gideon_bl4"}, bit))                 # manifest map against the AXSS write
dbit, _ = TC.bit_file()
open(t + "/dfx2.cor", "wb").write(C.pack("x", "1", dbit, TC.APP, 0x30000, 0x30000, "820eded2", icon=bytes(2048)))
PY
# T104.1 D9, D12: bundles with the optional records 6 (description) and 7 (libraries), and the ways a manifest and its records can disagree
mkdir -p "$T/rec"
python3 - "$T/rec" <<'PY'
import json, os, struct, sys
sys.path.insert(0, "tools")
import cor_pack as C
import descrec
import test_cor_pack as TC
t = sys.argv[1]
desc = descrec.compile_text(open("tests/coreapp/golden/desc_test.toml").read())
lib1, lib2 = TC.u2a(), TC.u2a() + bytes(9000)                # a library record much larger than the partial and the application
bit, _ = TC.bit_file()
app = bytes((i * 5) & 0xFF for i in range(512))
kw = dict(name="VIC-20 rec", version="0.1", partial=bit, app=app, load=0x30000, entry=0x30000, host_key="820eded2")
open(t + "/old.cor", "wb").write(C.pack(**kw))
open(t + "/rec.cor", "wb").write(C.pack(desc=desc, libs=[lib1, lib2], **kw))
open(t + "/desc.bin", "wb").write(desc)
open(t + "/libs.bin", "wb").write(C.pack_libs([lib1, lib2]))
open(t + "/libsonly.cor", "wb").write(C.pack(libs=[lib1], **kw))
open(t + "/recl.cor", "wb").write(C.pack_large("amiga1200", "Amiga 1200", "0.3.0", TC.full_bit(frames=500), app, desc=desc, libs=[lib1]))
m, r = C.parse(open(t + "/rec.cor", "rb").read())
def rebuild(mm, drop=()):
    body = C.rec(1, json.dumps(mm, sort_keys=True, separators=(",", ":")).encode())
    for k in (2, 3, 6, 7):
        if k in r and k not in drop:
            body += C.rec(k, r[k])
    return C.MAGIC + struct.pack("<I", len(body)) + body
open(t + "/nomanifest.cor", "wb").write(rebuild({k: v for k, v in m.items() if k not in ("records", "desc_size", "desc_sha256", "libs_size", "libs_sha256", "libs_count")}))
open(t + "/v2.cor", "wb").write(rebuild(dict(m, records=2)))
open(t + "/nodesc.cor", "wb").write(rebuild(m, drop=(6,)))
PY
cc -c -Wall -I"$H" -o "$T/sha256.o" "$H/sha256.c"
t_test_cor() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_cor" "$ROOT/tests/hostapp/test_cor.cc" "$H/cor.cc" "$T/sha256.o" 2>&1 | head -30
  "$T/test_cor" "$T/good.cor" "$T/c2.cor" "$T/app.bin" "$T/h2.cor"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_cor t_test_cor
t_test_correc() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_correc" "$ROOT/tests/hostapp/test_correc.cc" "$H/cor.cc" "$H/startcheck.cc" "$H/bootcfg.cc" "$T/sha256.o" 2>&1 | head -30
  "$T/test_correc" "$T/rec"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_correc t_test_correc
t_test_bootcfg() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_bootcfg" "$ROOT/tests/hostapp/test_bootcfg.cc" "$H/bootcfg.cc" 2>&1
  "$T/test_bootcfg"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_bootcfg t_test_bootcfg
# T135: the direct-start decision table, the return marker, direct= in boot.cfg, the text layer's layout (no hardware: -DOVTEXT_BENCH)
t_test_directboot() {
  c++ -std=gnu++11 -Wall -DOVTEXT_BENCH -I"$H" -o "$T/test_directboot" "$ROOT/tests/hostapp/test_directboot.cc" "$H/directboot.cc" "$H/handover.cc" "$H/bootcfg.cc" "$H/ovtext.cc" 2>&1
  "$T/test_directboot"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_directboot t_test_directboot
# T203 (#69): the start log - the block per start, the file on the SD card (last 10, no SD / read-only), the late boot.cfg read, the late "started by".
t_test_startfile() {
c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_startfile" "$ROOT/tests/hostapp/test_startfile.cc" "$H/startfile.cc" "$H/directboot.cc" 2>&1
"$T/test_startfile"
return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_startfile t_test_startfile
# T135 B1/B2: "ours" is the stamp AND the word TRX64 in the application; the updater's copy of ourfw.h is the same file
t_test_ourfw() {
  cmp -s "$H/ourfw.h" "$ROOT/vendor/gideon/software/application/update_u2p/ourfw.h" || { echo "FAIL: ourfw.h differs between host-app and vendor/gideon (update_trx.cc)" >&2; exit 1; }
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_ourfw" "$ROOT/tests/hostapp/test_ourfw.cc" "$H/handover.cc" 2>&1
  "$T/test_ourfw"
  return 0
}
cc_block test_ourfw t_test_ourfw
# T139: the kept boot.cfg save, with one decision taken out
bc_control() { cc_no_controls && return 0;   # name, sed expression
  local name="$1" expr="$2" key
  key="$(cc_key hostapp "bc $name $expr" -- -std=gnu++11 -w -I"$H" -- "$ROOT/tests/hostapp/test_bootcfg.cc" "$H/bootcfg.cc")"
  cc_cached "$name" "$key" && return 0
  sed -e "$expr" "$H/bootcfg.cc" > "$T/bc_$name.cc"
  cmp -s "$T/bc_$name.cc" "$H/bootcfg.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++11 -w -I"$H" -o "$T/test_bc_$name" "$ROOT/tests/hostapp/test_bootcfg.cc" "$T/bc_$name.cc" 2>&1
  if cc_exec "$T/rl_$name" "$T/test_bc_$name" > "$T/bc_$name.log" 2>&1; then
    echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1
  fi
  grep -q '^FAIL' "$T/bc_$name.log" || { echo "CONTROL $name: no red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/bc_$name.log") check(s) red)"
  cc_store "$name" "$key" "$T/rl_$name"
}
# T104.1 5.5 control: a host that drops lib_attempt on its own save would clear the core application's crash marker
bc_control bootcfg_dropattempt 's|if (c->lib_attempt > 0 \&\& n >= 0 \&\& n < max) {|if (false) {|'
# T139: the text field's keys (D3), delete / copy / move over a RAM disk (D1, D2).
KBD="$ROOT/host-app/software/io/c64"
LE="$ROOT/app/edit"   # T180 D8: the line editor of the host and the core application
t_test_lineedit() {
  c++ -std=gnu++11 -Wall -I"$LE" -I"$KBD" -o "$T/test_lineedit" "$ROOT/tests/hostapp/test_lineedit.cc" "$LE/lineedit.cc" 2>&1
  "$T/test_lineedit"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_lineedit t_test_lineedit
FOT="$ROOT/tests/hostapp/test_fileops.cc"
t_test_fileops() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_fileops" "$FOT" "$H/fileops.cc" "$H/bootcfg.cc" "$T/sha256.o" 2>&1
  "$T/test_fileops"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_fileops t_test_fileops
# T146: the menu picture (codec, key, settle timer, key queue).
MST="$ROOT/tests/hostapp/test_menusnap.cc"
t_test_menusnap() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_menusnap" "$MST" "$H/menusnap.cc" 2>&1
  "$T/test_menusnap"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_menusnap t_test_menusnap
# T105: which picture the applications choose - a screen that lists no 50 Hz mode must
# not be given one, which is the defect a tester hit on 2026-10-16.
t_test_videomode() {
  c++ -std=gnu++11 -Wall -DVIDEOMODE_NO_BOARD=1 -I"$ROOT/app/video" -o "$T/test_videomode" "$ROOT/tests/hostapp/test_videomode.cc" "$ROOT/app/video/videomode.cc" "$ROOT/app/video/edid.cc" 2>&1
  "$T/test_videomode"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_videomode t_test_videomode
# T101: UI_RES picks the layer's scale and position; 2 does not exist (a canvas of 1280 would not fit pix_layer.v's line buffer), and a
# wrong expectation has to fail.
PORT="$ROOT/host-app/software/lvgl_port"
t_uires() {
  for r in 0 1; do
    c++ -std=gnu++11 -Wall -DUI_RES=$r -DWANT_SCALE=$((2 - r)) -I"$PORT" -o "$T/test_uires$r" "$ROOT/tests/hostapp/test_uires.cc" 2>&1
    "$T/test_uires$r"
  done
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block uires t_uires

# T106: the page and the JSON the host application answers with, and the routes a form drives.
t_test_webui() {
  c++ -std=gnu++11 -Wall -DWEBUI_NO_BOARD=1 -DVIDEOMODE_NO_BOARD=1 -I"$H" -I"$ROOT/app/video" -o "$T/test_webui" \
    "$ROOT/tests/hostapp/test_webui.cc" "$H/webui.cc" "$H/bugreport.cc" "$ROOT/app/video/videomode.cc" 2>&1
  "$T/test_webui" "$H/webui.cc" "$H/webui.h" "$H/webnet.cc"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_webui t_test_webui

# T192.2 P1/P4 (T192 D23): the host's key map is one table (host_core.cc); the legends are generated from it and equal the footers of the screen before the cut.
# No negative control: not a flash or persistence operation (T174 D5.4).
t_test_hostcore() {
  c++ -std=gnu++11 -Wall -Wextra -I"$H" -I"$ROOT/app/video" -I"$ROOT/host-app/software/io/c64" -o "$T/test_hostcore" "$ROOT/tests/hostapp/test_hostcore.cc" "$ROOT/tests/hostapp/hostcore_stubs.cc" \
    "$H/host_core.cc" "$H/startcheck.cc" "$H/cor.cc" "$H/cfw.cc" "$H/handover.cc" "$H/bootcfg.cc" "$T/sha256.o" 2>&1
  "$T/test_hostcore"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_hostcore t_test_hostcore

# T198 (S2, S3, S9, S12, S15): the config overlay's state over a fake file system. No negative control (T174 D5.4: not a flash operation).
t_test_cfgov() {
  c++ -std=gnu++11 -Wall -Wextra -I"$H" -I"$LE" -I"$KBD" -I"$ROOT/app/video" -I"$ROOT/host-app/software/io/c64" -o "$T/test_cfgov" "$ROOT/tests/hostapp/test_cfgov.cc" "$ROOT/tests/hostapp/hostcore_stubs.cc" \
    "$H/host_core.cc" "$H/host_core_cfgov.cc" "$H/configlist.cc" "$H/startcheck.cc" "$H/cor.cc" "$H/cfw.cc" "$H/handover.cc" "$H/bootcfg.cc" "$T/sha256.o" 2>&1
  "$T/test_cfgov"
  return 0
}
cc_block test_cfgov t_test_cfgov

# T192 D23: the gate. hostui.cc changes no state outside host_core, acts on no key itself and carries no hand-written legend; what is not cut is the ledger.
t_check_host_cut() {
  python3 -I "$ROOT/tools/check_host_cut.py" --selftest || return 1
  python3 -I "$ROOT/tools/check_host_cut.py" "$H" || return 1
  python3 -I "$ROOT/tools/render_compare.py" --selftest > /dev/null || return 1
}
cc_block check_host_cut t_check_host_cut

# T192.1 Q5 / S19: the same routes on the shared transport (coreapp/src: the HTTP reader, the WS framing, the JSON writer), driven with raw requests
t_test_webnet() {
  c++ -std=gnu++11 -Wall -DWEBUI_NO_BOARD=1 -DVIDEOMODE_NO_BOARD=1 -DNET_ALLOC=webnet_test_alloc -include "$ROOT/tests/hostapp/webnet_alloc.h" -I"$H" -I"$ROOT/coreapp/src" -I"$ROOT/uicore" -I"$ROOT/app/video" -o "$T/test_webnet" \
    "$ROOT/tests/hostapp/test_webnet.cc" "$H/webnet.cc" "$H/webui.cc" "$H/bugreport.cc" "$ROOT/app/video/videomode.cc" \
    "$ROOT/coreapp/src/httpreq.cc" "$ROOT/coreapp/src/svcjson.cc" "$ROOT/coreapp/src/wsproto.cc" \
    "$ROOT/uicore/uic_json.cc" "$ROOT/uicore/uic_sha.cc" "$ROOT/uicore/uic_svc.cc" "$ROOT/uicore/uic_net.cc" 2>&1
  "$T/test_webnet"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_webnet t_test_webnet

# T192 D21: the host's API from its declaration against a fake model with the real service (uicore) underneath: every page / item / action has an id and an
# intent that reaches the model, the destructive ones ask first, flows are questions and jobs, text entry, the token. No negative control: not a flash or
# persistence operation (T174 D5.4).
t_test_hostapi() {
  c++ -std=gnu++11 -Wall -Wextra -Wno-unused-parameter -g -I"$H" -I"$ROOT/app/video" -I"$ROOT/uicore" -I"$ROOT/host-app/software/io/c64" -o "$T/test_hostapi" \
    "$ROOT/tests/hostapp/test_hostapi.cc" "$ROOT/tests/hostapp/hostcore_stubs.cc" "$H/host_core.cc" "$H/startcheck.cc" "$H/cor.cc" "$H/cfw.cc" "$H/handover.cc" "$H/bootcfg.cc" "$T/sha256.o" "$H/hostdecl.cc" "$H/hostdomain.cc" "$ROOT/uicore/uic_json.cc" "$ROOT/uicore/uic_sha.cc" "$ROOT/uicore/uic_svc.cc" "$ROOT/uicore/uic_net.cc" 2>&1
  "$T/test_hostapi"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_hostapi t_test_hostapi

# T192.1 Q5: the socket loop itself (coreapp/src/nettransport.cc, both apps run it) against a shim of FreeRTOS and lwIP: the client path in pieces, the 503 and
# the slots, the WebSocket loop, the shared response buffer, memory that is not there
t_test_nettransport() {
  c++ -std=gnu++14 -Wall -Wno-unused-function -pthread -I"$ROOT/tests/hostapp/netshim" -I"$ROOT/coreapp/src" -DNET_ALLOC=net_test_alloc -o "$T/test_nettransport" \
    "$ROOT/tests/hostapp/test_nettransport.cc" "$ROOT/coreapp/src/nettransport.cc" "$ROOT/coreapp/src/httpreq.cc" "$ROOT/coreapp/src/wsproto.cc" 2>&1
  timeout 120 "$T/test_nettransport"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_nettransport t_test_nettransport

# T101 S3: the list window and the browser's file-name test (S1, the scan, is gone: T101 S10)
t_test_discover() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_discover" "$ROOT/tests/hostapp/test_discover.cc" "$H/discover.cc" 2>&1
  "$T/test_discover"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_discover t_test_discover
# T101 S10: the start reads (a) the directory entry and (b) the package header of each entry - no part, no listing; adding does the
# full check once. The stub file system counts stats, opens, listings and bytes.
SC_SRC=("$H/startcheck.cc" "$H/cor.cc" "$H/bootcfg.cc" "$T/sha256.o")
t_test_startcheck() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_startcheck" "$ROOT/tests/hostapp/test_startcheck.cc" "${SC_SRC[@]}" 2>&1
  "$T/test_startcheck" "$T/good.cor" "$T/c2.cor" "$T/h2.cor"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_startcheck t_test_startcheck
# the host app no longer searches: no scan, no discovery of bundles anywhere in the app's own sources
if grep -nE "scan_found|discover_cors|FoundCor|found_filter|add_found" "$H"/*.cc "$H"/*.h; then
  echo "T101 S10: the host app still searches for bundles" >&2; exit 1
fi
echo "host app: no scan_found / discover_cors left"
# T115 S1, S2: the .cfw reader, the DDR2 address map and the IPROG bytes, on vectors of the two layouts measured on the board
t_test_cfw() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_cfw" "$ROOT/tests/hostapp/test_cfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$T/sha256.o" 2>&1
  "$T/test_cfw"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_cfw t_test_cfw
# T115 §7: the large .cor (a full bitstream in a package) read through the same parser: offsets, map, IDCODE, hashes on adding, refusals
LARGEARGS=("$T/large.cor" "$T/lbit.bin" "$T/lapp.bin" "$T/licon.bin" "$T/large_gbl4.cor" "$T/large100t.cor" "$T/large_idm.cor" "$T/large_mapm.cor" "$T/dfx2.cor" "$T/large_bit100.cor")
t_test_corlarge() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_corlarge" "$ROOT/tests/hostapp/test_corlarge.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$T/sha256.o" 2>&1
  "$T/test_corlarge" "${LARGEARGS[@]}"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_corlarge t_test_corlarge
# a part read from the medium is checked before it is written/used: a flipped bit on the first read is re-read (board 2026-10-28)
t_test_srcverify() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_srcverify" "$ROOT/tests/hostapp/test_srcverify.cc" "$H/srcverify.cc" "$T/sha256.o" 2>&1
  "$T/test_srcverify"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_srcverify t_test_srcverify
# the stamp text of the flash file carries a version (cor2/cfw2): a stamp of the old format never matches (board 2026-10-28)
t_test_stamp() {
  awk '/^int legacy_stamp_text/,/^}/' "$H/legacy.cc" > "$T/stamp_fn.inc"
  c++ -std=gnu++11 -Wall -I"$H" -DSTAMP_FN="\"$T/stamp_fn.inc\"" -o "$T/test_stamp" "$ROOT/tests/hostapp/test_stamp.cc" 2>&1
  "$T/test_stamp"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_stamp t_test_stamp
# T139: SD data CRC16 is checked, a damaged block is read again (sd_card.cc / sdio.cc built unchanged against a fake card).
SDI="-I$HS/filesystem -I$HS/system -I$HS/io/sd_card -I$ROOT/tests/hostapp"
t_sdcrc() {
  for v in sdcrc; do
    X=""
    for f in io/sd_card/sd_card.cc io/sd_card/sdio.cc filesystem/blockdev.cc; do
      c++ -std=gnu++11 -w $SDI $X -include "$ROOT/tests/hostapp/sdio_fake.h" -c "$HS/$f" -o "$T/${v}_$(basename "$f").o"
    done
    c++ -std=gnu++11 -Wall $SDI $X -c "$ROOT/tests/hostapp/test_sdcrc.cc" -o "$T/${v}_t.o"
    c++ -o "$T/test_$v" "$T/${v}_"*.o
  done
  "$T/test_sdcrc" "$T/sdcrc.log"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block sdcrc t_sdcrc
# T149 D5: the SD speed follows the gateware. sdio.cc built against fake registers: control register bit 7 (SDIO_CAP_IOB) set -> the fast divider,
# clear -> the safe one, the divider is written to SDIO_SPEED; sd_card.cc passes 3 (12.5 MHz) and 1 (25 MHz) and uses nothing else.
t_sdspeed() {
  for v in sdspeed; do
    X=""
    c++ -std=gnu++11 -w $SDI $X -include "$ROOT/tests/hostapp/sdio_fake.h" -c "$HS/io/sd_card/sdio.cc" -o "$T/${v}_sdio.o"
    c++ -std=gnu++11 -Wall $SDI -c "$ROOT/tests/hostapp/test_sdspeed.cc" -o "$T/${v}_t.o"
    c++ -o "$T/test_$v" "$T/${v}_sdio.o" "$T/${v}_t.o"
  done
  "$T/test_sdspeed"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block sdspeed t_sdspeed
grep -q 'RW_SPEED      3' "$HS/io/sd_card/sd_card.cc" && grep -q 'RW_SPEED_IOB  1' "$HS/io/sd_card/sd_card.cc" \
  && grep -q 'sdio_set_rw_speed(RW_SPEED, RW_SPEED_IOB)' "$HS/io/sd_card/sd_card.cc" && ! grep -q 'sdio_set_speed(RW_SPEED' "$HS/io/sd_card/sd_card.cc" \
  || { echo "sd_card.cc: the read/write speed is not 3 (safe) / 1 (IOB gateware) through sdio_set_rw_speed" >&2; exit 1; }
# legacy.cc needs the board, so the removal is checked in the source: a stamp that does not match is deleted before the copy is decided
# (owner 2026-10-28), in the no-match branch and in the "scratch file incomplete" branch; the control (mutant without it) has to go red.
stamp_rm_count() { awk '/no stamp for this bitstream/{f=1} f&&/stamp_remove\(\);/{n++;exit} END{print n+0}' "$1"; }
[ "$(stamp_rm_count "$H/legacy.cc")" = 1 ] || { echo "legacy.cc: a non-matching stamp is not removed" >&2; exit 1; }
sed -e '/an old-format or foreign stamp never stays behind/d' "$H/legacy.cc" > "$T/legacy_keepstamp.cc"
[ "$(stamp_rm_count "$T/legacy_keepstamp.cc")" = 1 ] && { echo "CONTROL keepstamp: still passes without the removal" >&2; exit 1; }
echo "stamp removal: present; control keepstamp fails as it must"
# T115 S3: the FAT12/16 chain walker over fake volumes (the flash drive's measured geometry among them)
t_test_fatwalk() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_fatwalk" "$ROOT/tests/hostapp/test_fatwalk.cc" "$H/fatwalk.cc" 2>&1
  "$T/test_fatwalk"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_fatwalk t_test_fatwalk
# T115 D2 flash file: the real FatFS over a RAM disk - a plain write over scattered free clusters is NOT one run, f_expand makes it one,
# and no free run gives FR_DENIED with the text that names the size. Control: the same test without the reservation must go red.
FFS="$ROOT/vendor/gideon/software/chan_fat/ff14/source"
t_test_fatexpand() {
  if [ -f "$FFS/ff.c" ]; then
    mkdir -p "$T/ffc" "$T/stub"
    # host-app's ffconf.h = the application's with f_expand on (stub RTOS headers: the original includes them)
    : > "$T/stub/FreeRTOS.h"; : > "$T/stub/semphr.h"
    printf '#include "ffconf.h"\n#if FF_USE_EXPAND != 1\n#error host-app ffconf.h does not enable f_expand\n#endif\n' > "$T/ffcheck.c"
    cc -fsyntax-only -I"$H" -I"$T/stub" "$T/ffcheck.c"
    printf '#include "ffconf.h"\n#if FF_USE_EXPAND != 0\n#error the application ffconf.h is not the 0 this test expects\n#endif\n' > "$T/ffcheck0.c"
    cc -fsyntax-only -I"$ROOT/vendor/gideon/software/chan_fat/full" -I"$T/stub" "$T/ffcheck0.c"
    sed -E -e "s/^(#define FF_USE_EXPAND)[[:space:]]+0/\1 1/" -e "s/^(#define FF_MAX_SS)[[:space:]]+4096/\1 512/" "$ROOT/vendor/gideon/software/chan_fat/pc/ffconf.h" > "$T/ffc/ffconf.h"
    grep -q '^#define FF_USE_EXPAND 1' "$T/ffc/ffconf.h" || { echo "fatexpand: the test ffconf.h did not get f_expand" >&2; exit 1; }
    for c in ff ffunicode ffsystem; do
      cc -w -I"$T/ffc" -I"$FFS" -I"$ROOT/vendor/gideon/software/filesystem" -c -o "$T/ffc/$c.o" "$FFS/$c.c"
    done
    c++ -std=gnu++11 -Wall -I"$T/ffc" -I"$H" -I"$FFS" -I"$ROOT/vendor/gideon/software/filesystem" -o "$T/test_fatexpand" \
      "$ROOT/tests/hostapp/test_fatexpand.cc" "$H/fatwalk.cc" "$T/ffc/ff.o" "$T/ffc/ffunicode.o" "$T/ffc/ffsystem.o" 2>&1
    "$T/test_fatexpand"
    if "$T/test_fatexpand" 0 > "$T/fatexpand_bad.log" 2>&1; then
      echo "CONTROL fatexpand_noreserve: the test PASSED without the reservation" >&2; exit 1
    fi
    grep -q '^FAIL' "$T/fatexpand_bad.log" || { echo "CONTROL fatexpand_noreserve: no red check" >&2; exit 1; }
    echo "control fatexpand_noreserve: fails as it must ($(grep -c '^FAIL' "$T/fatexpand_bad.log") check(s) red)"
  else
    echo "SKIPPED test_fatexpand: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  fi
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_fatexpand t_test_fatexpand
# T115 D7 S10: the logo of a .cfw row (built in by design name, a sidecar of exactly 2048 bytes replaces it); the header must be the logo files
python3 "$ROOT/tools/gen_cfw_icons.py" --check
ICONT="$ROOT/tests/hostapp/test_cfwicon.cc"
ICONARGS=("$ROOT/tools/core_logos/u64/icon.rgb565" "$ROOT/tools/core_logos/trx64/icon.rgb565" "$ROOT/tools/core_logos/firmware/icon.rgb565")
t_test_cfwicon() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_cfwicon" "$ICONT" "$H/cfwicon.cc" 2>&1
  "$T/test_cfwicon" "${ICONARGS[@]}"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_cfwicon t_test_cfwicon
# Flash-file controls (T174 D5.4: flash and persistence only): each takes one decision out of the code under test, and the bench has to go red.
mutant() { cc_no_controls && return 0;   # name, source file, sed expression, test source, then the other sources of the bench (the arguments: MUTARGS)
  local name="$1" src="$2" expr="$3" test="$4"; shift 4
  local bad="$T/mut_$name.cc" key
  key="$(cc_key hostapp "mutant $name $src $expr $test $* ${MUTARGS[*]-}" --runargs "${MUTARGS[*]-}" -- -std=gnu++11 -w -I"$H" -- "$test" "$src" "$@")"
  cc_cached "$name" "$key" && return 0
  sed -e "$expr" "$src" > "$bad"
  cmp -s "$bad" "$src" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++11 -w -I"$H" -o "$T/mut_$name" "$test" "$bad" "$@" 2>&1
  if cc_exec "$T/rl_$name" "$T/mut_$name" ${MUTARGS[@]+"${MUTARGS[@]}"} > "$T/mut_$name.log" 2>&1; then
    echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1
  fi
  grep -q '^FAIL' "$T/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mut_$name.log") check(s) red)"
  cc_store "$name" "$key" "$T/rl_$name"
}
FATT="$ROOT/tests/hostapp/test_fatwalk.cc"
mutant sv_unchecked "$H/srcverify.cc" 's|bool check = j.expect_sha \|\| j.twice;|bool check = false;|' "$ROOT/tests/hostapp/test_srcverify.cc" "$T/sha256.o"
# the full check's scratch buffer has to hold the largest part, the records included (a heap overflow only the address sanitizer sees)
t_rec_scratch() {
  if echo 'int main(){return 0;}' | c++ -fsanitize=address -x c++ - -o "$T/asan_probe" 2>/dev/null && "$T/asan_probe"; then
    c++ -std=gnu++11 -w -g -fsanitize=address -I"$H" -o "$T/test_correc_asan" "$ROOT/tests/hostapp/test_correc.cc" "$H/cor.cc" "$H/startcheck.cc" "$H/bootcfg.cc" "$T/sha256.o" 2>&1 | head
    "$T/test_correc_asan" "$T/rec" > /dev/null
  else
    echo "NOTE: no AddressSanitizer in this image: the full check's buffer was NOT checked" >&2
  fi
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block rec_scratch t_rec_scratch
MUTARGS=()
mutant fat_gap_allowed "$H/fatwalk.cc" 's|if (next != c + 1) {|if (false) {|' "$FATT"
mutant fat_odd_entry "$H/fatwalk.cc" 's|((c \& 1) ? v >> 4 : v \& 0xFFF)|(v \& 0xFFF)|' "$FATT"
mutant fat_long_chain "$H/fatwalk.cc" 's|} else if (next < eoc) {|} else if (false) {|' "$FATT"
# T111: the media list, the confirmation, the format over a sparse medium, the independent checker, each with its negative controls
t_fmt() { bash "$ROOT/tests/hostapp/run_fmt.sh" "$T"; }
cc_block fmt t_fmt
# T167 5.9: every page change of an overlay panel goes through TrxPage / trx_page_hide() (lint on hostui.cc)
t_pages() { bash "$ROOT/tests/hostapp/pages_lint.sh" "$H/hostui.cc"; return 0; }
cc_block pages_lint t_pages
# T101 S7: the host app names no machine
python3 "$ROOT/tests/hostapp/neutral.py" --selftest
python3 "$ROOT/tests/hostapp/neutral.py"
# T139: BlockDevice_Flash::write erases every sector of a run (fake flash with erase semantics); the control is the old single erase.
# The bench builds the class (everything above "BlockDevice *flashdisk_blk;"); the registration with the FileManager below it is board code.
FWS="$ROOT/vendor/gideon/software"
t_test_flashwrite() {
  if [ -f "$FWS/chan_fat/ff14/source/ff.c" ]; then
    mkdir -p "$T/fw"
    sed -E -e '/#include "FreeRTOS.h"/d' -e '/#include "semphr.h"/d' -e 's/^(#define FF_FS_REENTRANT)[[:space:]]+1/\1 0/' "$FWS/chan_fat/full/ffconf.h" > "$T/fw/ffconf.h"
    FWINC=(-I"$T/fw" -I"$FWS/chan_fat/ff14/source" -I"$ROOT/tests/hostapp/stubfs" -I"$ROOT/tests/hostapp" -I"$H" -I"$HS/filesystem" -I"$HS/filemanager" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/io/flash")
    sed -E -e '/^BlockDevice \*flashdisk_blk;/,$d' -e '/#include "(filemanager|file_device|filesystem_fat|init_function|u64|c64).h"/d' "$HS/filesystem/blockdev_flash.cc" > "$T/fw/blockdev_flash_class.cc"
    grep -q 'BlockDevice_Flash::write' "$T/fw/blockdev_flash_class.cc" || { echo "test_flashwrite: the cut lost write()" >&2; exit 1; }
    c++ -std=gnu++14 -w "${FWINC[@]}" -I"$HS/filesystem" -o "$T/test_flashwrite" "$ROOT/tests/hostapp/test_flashwrite.cc" "$T/fw/blockdev_flash_class.cc" "$HS/filesystem/blockdev.cc" 2>&1
    "$T/test_flashwrite"
    sed -e 's|n < count; n++) {|n < 1; n++) {|' "$T/fw/blockdev_flash_class.cc" > "$T/fw/mut_flash_erase_first.cc"
    cmp -s "$T/fw/mut_flash_erase_first.cc" "$T/fw/blockdev_flash_class.cc" && { echo "CONTROL flash_erase_first: the sed changed nothing" >&2; exit 1; }
    c++ -std=gnu++14 -w "${FWINC[@]}" -I"$HS/filesystem" -o "$T/mut_flash_erase_first" "$ROOT/tests/hostapp/test_flashwrite.cc" "$T/fw/mut_flash_erase_first.cc" "$HS/filesystem/blockdev.cc" 2>&1
    if "$T/mut_flash_erase_first" > "$T/mut_flash_erase_first.log" 2>&1; then echo "CONTROL flash_erase_first: the test PASSED against the old single erase" >&2; exit 1; fi
    grep -q '^FAIL' "$T/mut_flash_erase_first.log" || { echo "CONTROL flash_erase_first: it failed without a red check" >&2; exit 1; }
    echo "control flash_erase_first: fails as it must ($(grep -c '^FAIL' "$T/mut_flash_erase_first.log") check(s) red)"
  else
    echo "SKIPPED test_flashwrite: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  fi
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_flashwrite t_test_flashwrite

# T148 S6: the "Cores and ROMs" scan and the removal of what belongs to no core, over a RAM disk. Controls only for what protects saves and the flash medium.
CMT="$ROOT/tests/hostapp/test_coremaint.cc"
CMX=("$H/fileops.cc" "$H/bootcfg.cc" "$T/sha256.o")
t_test_coremaint() {
  c++ -std=gnu++11 -Wall -I"$H" -o "$T/test_coremaint" "$CMT" "$H/coremaint.cc" "${CMX[@]}" 2>&1
  "$T/test_coremaint"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block test_coremaint t_test_coremaint
_cm_control() {   # name, sed expression
  local name="$1" expr="$2"
  mkdir -p "$T/cm_$name"
  cp "$H/romreq_table.inc" "$H"/*.h "$T/cm_$name/"
  sed -e "$expr" "$H/coremaint.cc" > "$T/cm_$name/coremaint.cc"
  cmp -s "$T/cm_$name/coremaint.cc" "$H/coremaint.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++11 -w -I"$T/cm_$name" -I"$H" -o "$T/cm_$name/t" "$CMT" "$T/cm_$name/coremaint.cc" "${CMX[@]}" 2>&1
  if "$T/cm_$name/t" > "$T/cm_$name/log" 2>&1; then
    echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1
  fi
  grep -q '^FAIL' "$T/cm_$name/log" || { echo "CONTROL $name: no red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/cm_$name/log") check(s) red)"
}
cm_control() { CC_KIND=control cc_block "$1" _cm_control -- "$@"; }
cm_control cm_saves_deleted 's|add_orphan(rep, p, CMK_FOLDER, false, mine\[i\].size, !in_saves, |add_orphan(rep, p, CMK_FOLDER, false, mine[i].size, true, |'
cm_control cm_folder_with_saves 's|    if (me \&\& kept) {|    if (false) {|'
cm_control cm_flash_scanned 's|!strcasecmp(media\[i\].name, "flash")|false|'
cm_control cm_flash_deletable 's/ || !strncasecmp(path, "\/flash\/", 7)) {/) {/'
cm_control cm_apps_orphan 's|if (!strcasecmp(e\[i\].name, "apps")) {|if (false) {|'
cm_control cm_apps_deletable 's|!strncasecmp(rest, "apps", 4) \&\&|false \&\&|'

# T137 S1: the conversion of a .cfw/.ue2 to a .cor, the table of official firmware, the manifest's origin (run_fwconvert.sh)
t_fwconvert() { bash "$ROOT/tests/hostapp/run_fwconvert.sh" "$T"; }
cc_block fwconvert t_fwconvert

# T137 S2/S3: the restore of the original firmware and the flash layer it shares with T152 (run_fwrestore.sh)
t_fwrestore() { bash "$ROOT/tests/hostapp/run_fwrestore.sh" "$T"; }
cc_block fwrestore t_fwrestore

# T152 S1-S4: the MultiCore update package (.txu), single-part writes of the flash layer, the survey of the cores (run_txu.sh)
t_txu() { bash "$ROOT/tests/hostapp/run_txu.sh" "$T"; }
cc_block txu t_txu

# T192.2-S3/S4: the MultiCore update as a job: the record of every case (pages, notes, flash counts) of the blocking call and of the stepped job equals the golden
# capture of the flow before it was cut into steps; a job that asks writes nothing until it is answered (run_hostjobs.sh)
t_hostjobs() { bash "$ROOT/tests/hostapp/run_hostjobs.sh" "$T"; }
cc_block hostjobs t_hostjobs
# T197 P3 / S3: Unzip in the File Browser: the job over the loader of coreapp/src and the Mac build of unzip + miniz (run_hostzip.sh)
t_hostzip() { bash "$ROOT/tests/hostapp/run_hostzip.sh" "$T/hostzip"; }
cc_block hostzip t_hostzip

# T192.2-S10 (no radio): the WiFi page's modal actions as jobs: blocking call and stepped job equal the golden capture of the old flows (run_hostwifi.sh)
t_hostwifi() { bash "$ROOT/tests/hostapp/run_hostwifi.sh" "$T"; }
cc_block hostwifi t_hostwifi

# T152 S5-S7: the flash protection before a large core against a W25Q128JV model, the SRP controls (run_flashlock.sh)
t_flashlock() { bash "$ROOT/tests/hostapp/run_flashlock.sh" "$T"; }
cc_block flashlock t_flashlock

# T152 D8/S9: the inspection of a .cfw/.ue2 for an ESP32 image set (run_espinspect.sh)
t_espinspect() { bash "$ROOT/tests/hostapp/run_espinspect.sh" "$T"; }
cc_block espinspect t_espinspect

# T152 S8: flashing the ESP32 image set against a model of the ESP32 ROM loader (run_espflash.sh)
t_espflash() { bash "$ROOT/tests/hostapp/run_espflash.sh" "$T"; }
cc_block espflash t_espflash

# T164 WP17 S10-S12, Q7: the host's picture (hostvideo.cc) against a fake screen and a fake mode-request gateware (run_hostvideo.sh)
t_hostvideo() { bash "$ROOT/tests/hostapp/run_hostvideo.sh" "$T"; }
cc_block hostvideo t_hostvideo
# T163 S1/S2: the host's own settings store (hostcfg.cc, the backend hooks in config.cc) against the real ConfigManager (run_hostcfg.sh)
t_hostcfg() { bash "$ROOT/tests/hostapp/run_hostcfg.sh" "$T"; }
cc_block hostcfg t_hostcfg
# T184 D4: the WiFi command channel (reply slots with sequence, timeouts, bounded scan decode, connect feedback) against stubs of the UART and the RTOS (run_wifireply.sh)
t_wifireply() { bash "$ROOT/tests/hostapp/run_wifireply.sh" "$T"; }
cc_block wifireply t_wifireply

# T162: the host app's bug report - the sections, the file on the SD card, the REST route, the log limits, no secrets.
RPT_SRCS=(hostreport.cc hostreport_live.cc bugreport.cc webui.cc)
rpt_build() {   # dir (holds the sources, mutated or not), out
  c++ -std=gnu++11 -w -DWEBUI_NO_BOARD=1 -DVIDEOMODE_NO_BOARD=1 -I"$1" -I"$H" -I"$ROOT/app/video" -o "$2" "$ROOT/tests/hostapp/test_report.cc" \
    "$1/hostreport.cc" "$1/hostreport_live.cc" "$1/bugreport.cc" "$1/webui.cc" "$H/startfile.cc" "$H/directboot.cc" "$ROOT/app/video/videomode.cc" 2>&1
}
t_rpt_ok() {
  mkdir -p "$T/rpt_ok"
  for f in "${RPT_SRCS[@]}"; do cp "$H/$f" "$T/rpt_ok/"; done
  rpt_build "$T/rpt_ok" "$T/rpt_ok/t"
  "$T/rpt_ok/t"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block rpt_ok t_rpt_ok rpt_build

# T164 D1: SNTP time -> software clock -> FAT time stamp in the configured zone. The real rtc_dummy.cc and sntp_time.cc,
# ntpstub/ stands in for FreeRTOS, lwIP and the settings store.
NT="$ROOT/tests/hostapp/test_ntp.cc"
ntp_build() {   # out, rtc source, sntp source (copied next to the stubs: its own directory has the real network_config.h)
  mkdir -p "$T/ntp_$1"; cp "$3" "$T/ntp_$1/sntp_time.cc"
  c++ -std=gnu++11 -w -I"$ROOT/tests/hostapp/ntpstub" -I"$HS/io/rtc" -I"$HS/network" -o "$T/ntp_$1/t" "$NT" "$2" "$T/ntp_$1/sntp_time.cc" 2>&1
}
t_ntp_ok() {
  ntp_build ok "$HS/io/rtc/rtc_dummy.cc" "$HS/network/sntp_time.cc"
  "$T/ntp_ok/t"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block ntp_ok t_ntp_ok ntp_build

# T164 D1: the zone table's POSIX strings give the real zones' offsets around the EU, US and southern-hemisphere changes.
TZT="$ROOT/tests/hostapp/test_tz.cc"
t_tz_ok() {
  c++ -std=gnu++11 -w -DTZ_TABLE="\"$HS/io/rtc/timezones.cc\"" -o "$T/tz_ok" "$TZT" 2>&1
  "$T/tz_ok"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block tz_ok t_tz_ok

# T191: the host's time through the hand-over page into the core application's clock (what CoreIo::wall_time reads on the board).
# The real handover.cc, rtc_dummy.cc and coreapp/src/walltime.h.
WT="$ROOT/tests/hostapp/test_walltime.cc"
wt_build() {   # out, handover.cc source, walltime.h source
  mkdir -p "$T/wt_$1"; cp "$3" "$T/wt_$1/walltime.h"; cp "$2" "$T/wt_$1/handover.cc"
  c++ -std=gnu++11 -w -I"$T/wt_$1" -I"$ROOT/tests/hostapp/ntpstub" -I"$HS/io/rtc" -I"$HS/hostapp" -o "$T/wt_$1/t" "$WT" "$HS/io/rtc/rtc_dummy.cc" "$T/wt_$1/handover.cc" 2>&1
}
t_wt_ok() {
  wt_build ok "$HS/hostapp/handover.cc" "$ROOT/coreapp/src/walltime.h"
  "$T/wt_ok/t"
  return 0   # a closing "cmd && {...}" that did not fire is no failure of the unit
}
cc_block wt_ok t_wt_ok wt_build
