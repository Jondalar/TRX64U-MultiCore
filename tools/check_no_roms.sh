#!/usr/bin/env bash
# T89 S1: no Apple ROM content in the repository, the CI artefacts or a .cor (Alex: ROM, Kickstart and disk images never go into a
# release, a .cor or a package). A file is refused when it is
#   - a 16,384 byte image whose RESET vector ($3FFC/$3FFD of the image = $FFFC/$FFFD) reads $FA62 (every Apple //e family system ROM)
#   - one of the known images (sha256 below: the enhanced //e system ROM, the //e character ROM, the Disk II boot ROM)
#   - any file, text or binary, that holds the first 24 bytes of the Disk II boot ROM (the P5A code) in any encoding: raw, or spelled
#     as hex digits with anything between them ("a2 20", X"a2", X"20", 0xa2, 0x20, one byte a line, a long hex string)
# The script holds a 4 byte anchor and the sha256 of the 24 bytes, not the bytes.
#   tools/check_no_roms.sh              every tracked file of the repository
#   tools/check_no_roms.sh <path>...    those files / directories, recursively (a package directory, a .cor)
#   tools/check_no_roms.sh --selftest   the scanner against planted files in every encoding; the clean files have to pass
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 -I - "$ROOT" "$@" <<'PY'
import hashlib, os, re, subprocess, sys, tempfile

ROOT, args = sys.argv[1], sys.argv[2:]
KNOWN = {   # sha256 of the known images; the images themselves are never in the tree
    "aab38a03ca8deabbb2f868733148c2efd6f655a59cd9c5d058ef3e0b7aa86a1a": "enhanced Apple //e system ROM",
    "6340138fc3bcce271cfebc5800edff5166d133c8d6330775f0945a8e5919924a": "Apple //e character ROM",
    "de1e3e035878bab43d0af8fe38f5839c527e9548647036598ee6fe7ec74d2a7d": "Disk II boot ROM",
}
ANCHOR = bytes.fromhex("a220a000")
SHA24 = "81343dd46894eb7714039020b9ec646b3023cf3a1cd8e5f6c441ececdb1af803"
LEN = 24

def holds_sequence(data, anchor, sha, n):
    """Does `data` hold the n bytes (anchor first, sha256 sha) raw, or spelled as hex digits in any text encoding?"""
    i = data.find(anchor)
    while i >= 0:
        if hashlib.sha256(data[i:i + n]).hexdigest() == sha:
            return "raw"
        i = data.find(anchor, i + 1)
    # text: only the hex digits count, whatever stands between them (separators, quotes, X, 0x, $)
    digits = re.sub(rb"[^0-9a-fA-F]", b"", re.sub(rb"(?i)x\"|0x|\$", b"", data)).lower()
    a = anchor.hex().encode()
    j = digits.find(a)
    while j >= 0:
        w = digits[j:j + 2 * n]
        if len(w) == 2 * n and hashlib.sha256(bytes.fromhex(w.decode())).hexdigest() == sha:
            return "text"
        j = digits.find(a, j + 1)
    return None

def scan_bytes(data, anchor=ANCHOR, sha=SHA24, n=LEN):
    why = []
    if len(data) == 16384 and data[0x3FFC:0x3FFE] == b"\x62\xfa":
        why.append("16 KB image whose RESET vector reads $FA62")
    h = hashlib.sha256(data).hexdigest()
    if h in KNOWN:
        why.append(KNOWN[h])
    how = holds_sequence(data, anchor, sha, n)
    if how:
        why.append("holds the Disk II boot ROM (%s)" % how)
    return why

def files_of(paths):
    for p in paths:
        if os.path.isdir(p):
            for d, _, fs in os.walk(p):
                for f in fs:
                    yield os.path.join(d, f)
        else:
            yield p

if args[:1] == ["--selftest"]:
    r = os.urandom(LEN)                                  # a stand-in for the 24 bytes: the scanner is the same, the bytes are not ours
    anchor, sha = r[:4], hashlib.sha256(r).hexdigest()
    tmp = tempfile.mkdtemp()
    def put(name, data): open(os.path.join(tmp, name), "wb").write(data)
    put("clean.bin", os.urandom(4096)); put("clean_text.txt", b"just words and 00 01 02 03 a2 20 a0\n")
    img = bytearray(os.urandom(16384)); img[0x3FFC:0x3FFE] = b"\x62\xfa"; put("planted_rom.bin", bytes(img))
    put("planted_raw.cor", os.urandom(100) + r + os.urandom(100))
    put("planted_lines.txt", b"".join(b"%02x\n" % b for b in r))
    put("planted_vhdl.vhd", b", ".join(b'X"%02x"' % b for b in r))
    put("planted_c.h", b", ".join(b"0x%02x" % b for b in r))
    put("planted_long.mem", b"@0 " + r.hex().encode() + b"\n")
    want = {"planted_rom.bin", "planted_raw.cor", "planted_lines.txt", "planted_vhdl.vhd", "planted_c.h", "planted_long.mem"}
    got = set()
    for f in files_of([tmp]):
        if scan_bytes(open(f, "rb").read(), anchor, sha, LEN): got.add(os.path.basename(f))
    ok = got == want
    print("check_no_roms selftest: " + ("ok (planted: %s; the clean files passed)" % ", ".join(sorted(got)) if ok else "FAILED: found %s, wanted %s" % (sorted(got), sorted(want))))
    sys.exit(0 if ok else 1)

if args:
    paths = args
else:
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout
    paths = [os.path.join(ROOT, p) for p in out.decode().split("\0") if p]
bad = n = 0
for f in files_of(paths):
    try:
        if os.path.islink(f) or not os.path.isfile(f) or os.path.getsize(f) > 64 << 20:
            continue
        data = open(f, "rb").read()
    except OSError:
        continue
    n += 1
    why = scan_bytes(data)
    if why:
        print("ROM CONTENT: %s: %s" % (os.path.relpath(f), "; ".join(why)))
        bad += 1
if bad:
    print("check_no_roms: %d file(s) with ROM content" % bad)
    sys.exit(1)
print("check_no_roms: ok (%d files)" % n)
PY
