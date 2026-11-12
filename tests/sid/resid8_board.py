#!/usr/bin/env python3
"""reSID-8 board test suite (T25): the open audio bugs of GideonZ/1541ultimate, as tests that run on the board.

    python3 tests/sid/resid8_board.py [--host H] [--chips 6581,8580] [--items a,b] [--ear] [--engine resid8|ultisid]
    python3 tests/sid/resid8_board.py --selftest        offline: TRX64's reSID + a cycle-exact 6502, no board
    python3 tests/sid/resid8_board.py --dry-run         prints the REST calls it would make, touches nothing

Automated items (PASS/FAIL from C64 RAM / the border colour over REST, per chip model 6581 and 8580):
  lfsr     #954  noise LFSR taps and output bits (OSC3 read once per shift), test bit does not fill at once
  lockup   #954  noise + pulse locks the register at 0 (OSC3 reads 00), the test bit refills it slowly
  osc3     #951 #756 #352  OSC3 pipeline delay: detectSidModel gives 6581 -> 01, 8580 -> 00; VICE waveforms 10/20/40
  adsr     #702  VICE testprogs SID/envelope (00->FF and FF->00 flip, ADSR delay bug, lft-adsr): green border = pass
Ear items (--ear): digi playback (#650 #922), recorded from the UDP audio stream to WAV files + RMS/peak/clipping.
No pass/fail claim for those.

Rules kept: nothing is saved to flash (no configs:save_to_flash), every runtime setting that is changed is read
first and put back at the end (also after an error or Ctrl-C), speaker off while it runs, an item that fails or
errors does not stop the suite. Exit status 1 when any check FAILed or ERRORed.
"""
import argparse, atexit, glob, json, math, os, re, signal, socket, struct, subprocess, sys, time, urllib.error, urllib.parse, urllib.request, wave

HERE = os.path.dirname(os.path.abspath(__file__))
ASM = os.path.join(HERE, "asm")
MEDIA = os.path.join(HERE, "media")
BUILD = os.path.join(HERE, ".cache", "prg")
ROOT = os.path.dirname(os.path.dirname(HERE))
HOST = os.environ.get("TRX_HOST", os.environ.get("TRX64_HOST", "192.0.2.189"))
CHIPS = ("6581", "8580")
PHI2 = 985248.0          # PAL; the cycle counts below are only used for how long to wait
OUT = []                 # (item, chip, check, status, detail)


# ------------------------------------------------------------------------------------------ verdicts
# Each takes the memory the program leaves (dict addr -> bytes) and the chip, returns [(check, status, detail)].
# Status: PASS / FAIL / INFO. The criteria are the real chip's (the issues' measurements), not any emulation's.

OUT_BITS = [0, 2, 5, 9, 11, 14, 18, 20]      # OSC3 bit n = register bit OUT_BITS[n] of the 23-bit noise LFSR
REFILL = bytes.fromhex("fefcfcfcf8f8f8f0")  # first reads after the test bit (#954: 6581 and 8580 alike)


def v_lfsr(mem, chip):
    s = mem[0xC000]
    real = sum(1 for t in range(23, 256) if (s[t] & 1) != ((s[t - 23] ^ s[t - 18]) & 1))
    ulti = sum(1 for t in range(23, 256) if (s[t] & 1) != ((s[t - 23] ^ s[t - 22] ^ s[t - 21] ^ s[t - 16]) & 1))
    outb = sum(1 for n, r in enumerate(OUT_BITS) for t in range(23, 256) if ((s[t] >> n) & 1) != (s[t - r] & 1))
    allones = s[0:3] == b"\xff\xff\xff"
    return [
        ("noise feedback is bit 22 xor bit 17", "PASS" if real == 0 else "FAIL",
         "%d of 233 samples off; UltiSID rule 22^21^20^15 would be %d off" % (real, ulti)),
        ("OSC3 output bits are spaced 20,18,14,11,9,5,2,0 apart", "PASS" if outb == 0 else "FAIL", "%d bit checks off" % outb),
        ("test bit does not fill the register with ones at once", "FAIL" if allones else "PASS",
         "first reads " + s[:12].hex(" ")),
    ]


def v_lockup(mem, chip):
    after = mem[0xC100]
    refill = mem[0xC200][:8]
    return [
        ("noise + pulse(low) clears the register: OSC3 reads 00", "PASS" if set(after) == {0} else "FAIL",
         "%d of 256 reads nonzero, %d distinct" % (sum(1 for b in after if b), len(set(after)))),
        ("test bit refills slowly: fe fc fc fc f8 f8 f8 f0", "PASS" if refill == REFILL else "FAIL",
         "got " + refill.hex(" ")),
    ]


def v_osc3(mem, chip):
    a, b = mem[0xC000][0], mem[0xC000][1]
    want = 1 if chip == "6581" else 0
    info = " ".join(n + "=" + mem[x][:3].hex() for n, x in (("tri", 0xC010), ("saw", 0xC020), ("pul", 0xC030)))
    return [("detectSidModel: %s gives %02x then 03" % (chip, want), "PASS" if (a, b) == (want, 3) else "FAIL",
             "read %02x then %02x" % (a, b)),
            ("first OSC3 reads after test bit (information)", "INFO", info)]


# VICE testprogs: border green (5) = pass, red (2 / 10) = fail
BORDER_PASS, BORDER_FAIL = (5,), (2, 10)


def v_border(mem, chip):
    c = mem[0xD020][0] & 15
    return [("border", "PASS" if c in BORDER_PASS else "FAIL", "border colour %d" % c)]


# item table. kind "own": program from asm/, result read from RAM after the $5A/$A5 markers. kind "vice": fetched
# testprog, border colour.  wait = seconds the program needs (cycles / PHI2) before the first poll, so the REST reads
# (DMA) never land inside a timed loop.
ITEMS = [
    dict(id="lfsr", issue="#954", title="noise LFSR taps and output bits", kind="own", asm="lfsr", wait=2.5,
         reads=[(0xC000, 256)], verdict=v_lfsr),
    dict(id="lockup", issue="#954", title="noise lock-up with another waveform", kind="own", asm="lockup", wait=7.0,
         reads=[(0xC000, 64), (0xC100, 256), (0xC200, 64)], verdict=v_lockup),
    dict(id="osc3", issue="#951 #756 #352", title="OSC3 delay / SID model detection", kind="own", asm="osc3delay", wait=2.5,
         reads=[(0xC000, 2), (0xC010, 3), (0xC020, 3), (0xC030, 3)], verdict=v_osc3),
]
for w in ("10", "20", "40"):
    ITEMS.append(dict(id="wave" + w, issue="#756", title="VICE waveforms-%s (OSC3 vs recorded real chip)" % w, kind="vice",
                      vice="waveforms/waveforms-%s-{chip}.prg" % w, wait=3.0, verdict=v_border, per_chip_file=True))
for n in ("testFlip00toFF", "testFlipFFto00", "testADSRDelayBug", "lft-adsr-test"):
    ITEMS.append(dict(id="adsr-" + n, issue="#702", title="VICE envelope/" + n, kind="vice",
                      vice="envelope/%s.prg" % n, wait=3.0, verdict=v_border))
INFO_ITEMS = [dict(id="wave80", issue="#756", title="VICE waveforms-80 (noise; fails everywhere by design, information)",
                   kind="vice", vice="waveforms/waveforms-80-{chip}.prg", wait=25.0, verdict=v_border, per_chip_file=True,
                   info_only=True)]

# What TRX64's reSID (VICE 3.10 resid 1.0-pre2, single-cycle clocking) answers for the checks above, from --selftest.
# A check that is not listed is expected PASS. These are where the reference itself leaves the real chip.
RESID_EXPECT = {
    ("8580", "lockup", "test bit refills slowly: fe fc fc fc f8 f8 f8 f0"): "FAIL",   # reSID's 8580 refill needs ~9.7 s
}

def verdict_selftest():
    """The verdicts must tell the UltiSID of the issues from the real chip: feed them what #954 / #951 measured on it."""
    reg, smp = (1 << 23) - 1, []
    for _ in range(256):                       # UltiSID wave_map.vhd as #954 describes it: taps 22^21^20^15, outputs 22,20,16,13,11,7,4,2
        smp.append(sum(((reg >> b) & 1) << (7 - i) for i, b in enumerate((22, 20, 16, 13, 11, 7, 4, 2))))
        reg = ((reg << 1) & 0x7FFFFF) | (((reg >> 22) ^ (reg >> 21) ^ (reg >> 20) ^ (reg >> 15)) & 1)
    got = {c: s for c, s, _ in v_lfsr({0xC000: bytes(smp)}, "6581")}
    # the bit spacing of its outputs equals the SID's (22,20,16,.. minus 2 = 20,18,14,..): only a delay of two shifts, which
    # the feedback check sees. So: feedback and test bit must FAIL, the spacing check stays PASS.
    ok = [got[c] for c in got] == ["FAIL", "PASS", "FAIL"]
    record("verdicts", "-", "lfsr verdicts reject #954's UltiSID sequence", "OK" if ok else "FAIL", str(got))
    r = v_lockup({0xC000: b"", 0xC100: bytes(range(1, 256)) + b"\1", 0xC200: b"\xff\xff\xfe\xfe\xfc\xfc\xfc\xf8"}, "6581")
    record("verdicts", "-", "lockup verdicts reject #954's UltiSID (nonzero, FF FF FE FE ...)",
           "OK" if all(c[1] == "FAIL" for c in r) else "FAIL", str(r))
    r = v_osc3({0xC000: b"\x01\x03", 0xC010: b"\0\0\0", 0xC020: b"\0\0\0", 0xC030: b"\0\0\0"}, "8580")
    record("verdicts", "-", "osc3 verdict rejects '8580 detected as 6581' (#951)", "OK" if r[0][1] == "FAIL" else "FAIL", str(r[0]))
    r = v_border({0xD020: b"\x02"}, "6581")
    record("verdicts", "-", "border verdict rejects red", "OK" if r[0][1] == "FAIL" else "FAIL", "")


# ------------------------------------------------------------------------------------------ config names (T25.5)
NAMES = {
    "resid8": dict(addr_cat="SID Addressing", sid_cat="reSID8 Configuration", addr1="reSID8 1 Address",
                   addr2="reSID8 2 Address", split="reSID8 Range Split", model1="reSID8 1 Chip Model",
                   model2="reSID8 2 Chip Model"),
    "ultisid": dict(addr_cat="SID Addressing", sid_cat="UltiSID Configuration", addr1="UltiSID 1 Address",
                    addr2="UltiSID 2 Address", split="UltiSID Range Split", model1="UltiSID 1 Combined Waveforms",
                    model2="UltiSID 2 Combined Waveforms"),
}


def settings_plan(engine, chip, two=False):
    """(category, item, value) in the order they are applied. Automated items: only the engine at $D400 listens.
    two=True (ear items): both engines, $D400 and $D420, for tunes with a second SID."""
    n = NAMES[engine]
    plan = [
        ("Speaker Mixer", "Speaker Enable", "Disabled"),
        (n["addr_cat"], "SID Socket 1 Address", "Unmapped"),
        (n["addr_cat"], "SID Socket 2 Address", "Unmapped"),
        (n["addr_cat"], "Auto Address Mirroring", "Disabled"),
        (n["addr_cat"], n["split"], "Off"),
        (n["addr_cat"], n["addr2"], "$D420" if two else "Unmapped"),
        (n["addr_cat"], n["addr1"], "$D400"),
        (n["sid_cat"], n["model1"], chip),
    ]
    if two:
        plan.append((n["sid_cat"], n["model2"], chip))
    return plan


# ------------------------------------------------------------------------------------------ programs
def asm_prg(name):
    os.makedirs(BUILD, exist_ok=True)
    out = os.path.join(BUILD, name + ".prg")
    src = os.path.join(ASM, name + ".asm")
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        r = subprocess.run([os.path.join(ROOT, "tools", "64tass.sh"), "-q", "-a", "-o", out, src], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("64tass %s: %s" % (name, (r.stdout + r.stderr)[-300:]))
    return out


def vice_prg(rel, chip):
    p = os.path.join(MEDIA, "vice", rel.format(chip=chip))
    if not os.path.exists(p):
        raise FileNotFoundError("%s missing: run tests/sid/fetch_media.py" % p)
    return p


def sys_address(prg):
    d = open(prg, "rb").read()
    if d[0] | d[1] << 8 == 0x0801:
        m = re.search(rb"\x9e\s*(\d+)", d[2:20])
        if m:
            return int(m.group(1))
    return d[0] | d[1] << 8


# ------------------------------------------------------------------------------------------ backends
class Skip(Exception):
    pass


class BoardBackend:
    """The real machine over REST. Reads are DMA: they are only issued after the program has had its time."""

    def __init__(self, host, dry=False):
        self.host, self.dry, self.saved = host, dry, {}

    def _req(self, method, path, params=None, body=None, timeout=20):
        url = "http://%s/v1/%s" % (self.host, path)
        if params:
            url += "?" + urllib.parse.urlencode(params)
        if self.dry:
            print("   [dry-run] %s %s%s" % (method, url, "  (%d bytes body)" % len(body) if body else ""))
            return b"{}"
        req = urllib.request.Request(url, data=body, method=method)
        if body is not None:
            req.add_header("Content-Type", "application/octet-stream")
        try:
            return urllib.request.urlopen(req, timeout=timeout).read()
        except urllib.error.HTTPError as e:
            return e.read() or json.dumps({"errors": ["HTTP %d" % e.code]}).encode()

    def key(self, name):
        """Tap one key on the keyboard matrix (POST machine:input; games scan the matrix, not the $0277 buffer)."""
        url = "http://%s/v1/machine:input" % self.host
        body = json.dumps({"events": [{"kind": "keyboard", "inputs": [name], "transition": "tap"}]}).encode()
        if self.dry:
            print("   [dry-run] POST %s  %s" % (url, body.decode()))
            return
        req = urllib.request.Request(url, data=body, method="POST", headers={"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=10).read()

    def json(self, method, path, params=None, body=None):
        raw = self._req(method, path, params, body)
        try:
            return json.loads(raw.decode() or "{}")
        except ValueError:
            return {"errors": ["not JSON: %r" % raw[:80]]}

    # --- config
    def cfg_get(self, cat, item):
        r = self.json("GET", "configs/%s/%s" % (urllib.parse.quote(cat), urllib.parse.quote(item)))
        if self.dry:
            return "<current>"

        def find(o):
            if isinstance(o, dict):
                if "current" in o:
                    return o["current"]
                for v in o.values():
                    x = find(v)
                    if x is not None:
                        return x
            return None
        return find(r)

    def cfg_set(self, cat, item, value):
        r = self.json("PUT", "configs/%s/%s" % (urllib.parse.quote(cat), urllib.parse.quote(item)), {"value": value})
        if r.get("errors"):
            raise RuntimeError("config %s / %s = %s: %s" % (cat, item, value, r["errors"]))

    def apply(self, plan):
        for cat, item, value in plan:
            if (cat, item) not in self.saved:
                self.saved[(cat, item)] = self.cfg_get(cat, item)
                if self.saved[(cat, item)] is None and not self.dry:
                    raise RuntimeError("config %s / %s does not exist on this machine (wrong --engine?)" % (cat, item))
            self.cfg_set(cat, item, value)
        if not self.dry:
            time.sleep(0.5)

    def restore(self):
        for (cat, item), value in list(self.saved.items()):
            if value is None or self.dry:
                continue
            try:
                self.cfg_set(cat, item, str(value))
                print("   restored %s / %s = %s" % (cat, item, value))
            except Exception as e:                      # keep restoring the rest
                print("   RESTORE FAILED %s / %s -> %s: %s" % (cat, item, value, e))
        self.saved.clear()

    # --- machine
    def read(self, addr, n):
        if self.dry:
            return bytes(n)
        raw = self._req("GET", "machine:readmem", {"address": "%04X" % addr, "length": str(n)})
        if len(raw) != n:
            raise RuntimeError("readmem %04X+%d returned %d bytes: %r" % (addr, n, len(raw), raw[:60]))
        return raw

    def write(self, addr, data):
        self._req("PUT", "machine:writemem", {"address": "%04X" % addr, "data": bytes(data).hex()})

    def run(self, prg):
        r = self.json("POST", "runners:run_prg", body=open(prg, "rb").read())
        if r.get("errors"):
            raise RuntimeError("run_prg: %s" % r["errors"])

    def run_item(self, it, chip, prg):
        """Returns {addr: bytes} for own items, {0xD020: byte} for vice items."""
        if it["kind"] == "own":
            self.write(0xC3FE, b"\x00\x00")
        self.run(prg)
        if self.dry:
            return self.dry_mem(it)
        time.sleep(it["wait"])
        end = time.time() + 40
        last = None
        while time.time() < end:
            if it["kind"] == "own":
                m = self.read(0xC3FE, 2)
                if m == b"\x5a\xa5":
                    return {a: self.read(a, n) for a, n in it["reads"]}
            else:
                c = self.read(0xD020, 1)[0] & 15
                if c in BORDER_PASS + BORDER_FAIL and c == last:     # same colour twice, 1 s apart: the program is at its end
                    return {0xD020: bytes([c])}
                last = c
            time.sleep(1.0)
        raise TimeoutError("no result after %.0f s%s" % (it["wait"] + 40, "" if it["kind"] == "own" else " (border %s)" % last))

    def dry_mem(self, it):
        return {a: bytes(n) for a, n in it.get("reads", [(0xD020, 1)])}

    def reset(self):
        self.json("PUT", "machine:reboot")   # not machine:reset: that keeps a cartridge an ear item started with run_crt


class SimBackend:
    """TRX64's reSID in front of a cycle-counting 6502 (resid_ref.py): the expected values."""

    def __init__(self):
        sys.path.insert(0, HERE)
        import resid_ref
        self.mod = resid_ref
        self.chip = "6581"

    def apply(self, plan):
        for _, item, value in plan:
            if item.endswith("Chip Model") or item.endswith("Combined Waveforms"):
                self.chip = value

    def restore(self):
        pass

    def run_item(self, it, chip, prg):
        c = self.mod.Cpu(chip)
        c.load_prg(open(prg, "rb").read())
        c.pc = sys_address(prg)
        c.run(max_cycles=60_000_000)
        if it["kind"] == "own":
            if bytes(c.m[0xC3FE:0xC400]) != b"\x5a\xa5":
                raise RuntimeError("program did not finish (pc %04x, %d cycles)" % (c.pc, c.cyc))
            return {a: bytes(c.m[a:a + n]) for a, n in it["reads"]}
        return {0xD020: bytes([c.m[0xD020] & 15])}


# ------------------------------------------------------------------------------------------ ear items
EAR = [
    dict(id="ear-nextlevel-insomnia", issue="#922", title="Next Level: Insomnia.sid", how="sid", file="nextlevel/Insomnia.sid"),
    dict(id="ear-nextlevel-insomnia-short", issue="#922", title="Next Level: InsomniaShort.sid (speech)", how="sid",
         file="nextlevel/InsomniaShort.sid"),
    dict(id="ear-nextlevel-skybuster", issue="#922", title="Next Level: SkyBuster.sid", how="sid", file="nextlevel/SkyBuster.sid"),
    dict(id="ear-pigquest-orig", issue="#650", title="A Pig Quest, original cartridge dump: Piggy 18 splash at start",
         how="run", file="pigquest/apigquest_original.crt",
         note="Alex's dump of the original cartridge (board USB1/apigquest.crt, copy it into media/pigquest/)"),
    dict(id="ear-pigquest", issue="#650", title="A Pig Quest, Piggy 18 splash speech", how="run", file="pigquest/*trex*.crt",
         keys=[(5.0, "3")], note="csdb release 229717 (EasyFlash CRT, TREX crack): its menu comes first, key 3 = PIGGY18 SPLASH"),
    dict(id="ear-planetgolf", issue="#650", title="Planet Golf, 'Let's Golf!' / 'It's all about gravity'", how="run",
         file="planetgolf/PlanetGolf*.d64", note="csdb crack (D64, best-effort autostart); the speech comes in play: hit the ball out of bounds (hand work, the recording only catches the start)"),
    dict(id="ear-bozuma", issue="#650", title="BOZUMA, Mummy speaks in the intro", how="run", file="bozuma/*side a.d64",
         note="D64 side A, best-effort autostart"),
    dict(id="ear-lastv8", issue="#650", title="The Last V8 (corrupted sample)", how="run", file="lastv8/*.d64", note="D64, best-effort autostart"),
    dict(id="ear-lamenoter-pcm", issue="Alex", title="LAMENOTER Breadbox Season, U64 PCM variant (sampler, control)", how="reu+prg",
         file="lamenoter/lamenoter-u64.prg", reu="lamenoter/breadbox-pcm.reu"),
    dict(id="ear-lamenoter-sid", issue="Alex", title="LAMENOTER Breadbox Season, 8-bit Mahoney digi on the SID", how="reu+prg",
         file="lamenoter/lamenoter.prg", reu="lamenoter/breadbox.reu",
         note="NOT in the zip Alex has (it holds only the U64/PCM pair); needs lamenoter.prg + breadbox.reu from the full release"),
]


class Recorded(Exception):
    def __init__(self, pcm, lost):
        self.pcm, self.lost = pcm, lost


def save_wav(path, pcm, fs):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(pcm)


def type_keys(b, text):
    """Keyboard buffer at $0277 (10 keys at most) + count at $00C6, like the other board scripts."""
    data = bytes(0x0D if c == "\n" else ord(c) for c in text.upper())
    assert len(data) <= 10
    b.write(0x0277, data)
    b.write(0x00C6, bytes([len(data)]))


def d64_record(b, cap, path, args):
    """Best effort for disk games: reboot, mount, LOAD"*",8, RUN once the second READY. shows, record from the RUN on. If the game needs
    LOAD"*",8,1 or a key press, the recording is silence or a prompt: start it by hand then (the WAV metrics show it)."""
    r = b.json("POST", "drives/a:mount", {"type": "d64", "mode": "readonly"}, open(path, "rb").read())
    if r.get("errors"):
        raise RuntimeError("mount: %s" % r["errors"])
    b.json("PUT", "machine:reboot")          # a reset keeps a cartridge started by an earlier run_crt; a reboot drops it
    time.sleep(6.0)
    type_keys(b, 'LOAD"*",8\n')
    ready = bytes([18, 5, 1, 4, 25, 46])      # "READY." in screen codes: the second one comes when LOAD is done
    end = time.time() + max(args.load_wait, 180)
    while time.time() < end:
        time.sleep(2.0)
        if b._req("GET", "machine:readmem", {"address": "0400", "length": "1000"}).count(ready) >= 2:
            break
    else:
        raise RuntimeError("LOAD did not finish within %d s" % max(args.load_wait, 180))
    type_keys(b, "RUN\n")
    return cap.record(args.ear_seconds)


class AudioCapture:
    """The UDP audio stream of the board: 770-byte packets, 2 byte counter + 192 stereo int16 frames, ~48003 Hz."""
    FS = 48003

    def __init__(self, host, port=11001, mcast=None):
        self.host, self.port, self.mcast = host, port, mcast
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", port))
        self.sock.settimeout(1.0)
        if mcast:
            self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(mcast) + socket.inet_aton(self.my_ip()))

    def my_ip(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((self.host, 80))
        ip = s.getsockname()[0]
        s.close()
        return ip

    def target(self):
        return "%s:%d" % (self.mcast or self.my_ip(), self.port)

    def drain(self):
        """Throw away what queued up in the socket before the recording starts (reset, LOAD): stale audio, and its
        sequence gap would count as lost packets."""
        self.sock.setblocking(False)
        try:
            while True:
                self.sock.recvfrom(2048)
        except (BlockingIOError, socket.error):
            pass
        finally:
            self.sock.settimeout(1.0)

    def record(self, seconds):
        self.drain()
        pcm, lost, last = bytearray(), 0, None
        end = time.time() + seconds
        while time.time() < end:
            try:
                d, snd = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            if snd[0] != self.host or len(d) != 770:
                continue
            seq = struct.unpack_from("<H", d)[0]
            if last is not None and seq != (last + 1) & 0xFFFF:
                lost += (seq - last - 1) & 0xFFFF
            last = seq
            pcm += d[2:]
        return bytes(pcm), lost


def wav_metrics(pcm):
    v = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    out = {}
    for name, ch in (("L", v[0::2]), ("R", v[1::2])):
        if not ch:
            out[name] = None
            continue
        rms = math.sqrt(sum(x * x for x in ch) / len(ch))
        win = 480
        floor = min((math.sqrt(sum(x * x for x in ch[i:i + win]) / win) for i in range(0, len(ch) - win, win)), default=0)
        out[name] = dict(rms=round(rms, 1), peak=max(abs(x) for x in ch), clipped=sum(1 for x in ch if abs(x) >= 32760),
                         floor=round(floor, 1), dc=round(sum(ch) / len(ch), 1))
    return out


def ear_run(b, args, plan_fn):
    cap = AudioCapture(args.host, mcast=args.multicast)
    os.makedirs(args.wav_dir, exist_ok=True)
    print("\n== ear items: %d s per recording, WAV into %s" % (args.ear_seconds, args.wav_dir))
    for chip in args.chips:
        applied = False
        for it in [e for e in EAR if not args.items_ear or e['id'] in args.items_ear.split(',')]:
            tag = "%s [%s]" % (it["id"], chip)
            try:
                files = sorted(glob.glob(os.path.join(MEDIA, it["file"])))
                files = [f for f in files if os.path.isfile(f) and not f.endswith(".xml")]
                if not files:
                    raise Skip("media missing (%s)%s" % (it["file"], ("  " + it["note"]) if it.get("note") else ""))
                if it["how"] == "reu+prg":
                    if not os.path.exists(os.path.join(MEDIA, it["reu"])):
                        raise Skip("REU image missing (%s)  %s" % (it["reu"], it.get("note", "")))
                    if not args.ear_prepared:
                        raise Skip("REU load has no REST call: load %s by hand (file browser > Load into REU, REU 16 MB, "
                                   "REU enabled), then rerun with --ear-prepared --items-ear %s" % (it["reu"], it["id"]))
                if not applied:
                    b.apply(plan_fn(chip))
                    applied = True
                if args.dry_run:
                    print("   [dry-run] %s: start %s, record %d s" % (tag, files[0], args.ear_seconds))
                    continue
                b.json("PUT", "streams/audio:start", {"ip": cap.target()})
                try:
                    ext = os.path.splitext(files[0])[1].lower()
                    if ext == ".d64":
                        pcm, lost = d64_record(b, cap, files[0], args)
                        raise Recorded(pcm, lost)
                    route = {".sid": "runners:sidplay", ".prg": "runners:run_prg", ".crt": "runners:run_crt"}.get(ext)
                    if route is None:
                        raise Skip("%s: no REST runner for %s (mount/run it by hand)" % (os.path.basename(files[0]), ext))
                    r = b.json("POST", route, {"songnr": 0} if ext == ".sid" else None, open(files[0], "rb").read())
                    if r.get("errors"):
                        raise RuntimeError(str(r["errors"]))
                    for delay, key in it.get("keys", []):
                        time.sleep(delay)
                        b.key(key)
                    pcm, lost = cap.record(args.ear_seconds)
                finally:
                    b.json("PUT", "streams/audio:stop")
                raise Recorded(pcm, lost)
            except Recorded as rec:
                pcm, lost = rec.pcm, rec.lost
                path = os.path.join(args.wav_dir, "%s_%s.wav" % (it["id"], chip))
                save_wav(path, pcm, cap.FS)
                note = "%s  lost packets %d  %s" % (path, lost, json.dumps(wav_metrics(pcm)))
                OUT.append((it["id"], chip, "recording", "EAR", note))
                print("  EAR   %-34s %s" % (tag, note))
            except Skip as e:
                OUT.append((it["id"], chip, "recording", "SKIP", str(e)))
                print("  SKIP  %-34s %s" % (tag, e))
            except Exception as e:
                OUT.append((it["id"], chip, "recording", "ERROR", str(e)))
                print("  ERROR %-34s %s" % (tag, e))


# ------------------------------------------------------------------------------------------ driver
def record(item, chip, check, status, detail):
    OUT.append((item, chip, check, status, detail))
    print("  %-5s %-18s %s%s" % (status, item, check, "  (%s)" % detail if detail and status not in ("PASS",) else ""))
    sys.stdout.flush()


def run_items(b, items, chips, engine, selftest):
    for chip in chips:
        print("\n== chip model %s" % chip)
        try:
            b.apply(settings_plan(engine, chip))
        except Exception as e:
            for it in items:
                record(it["id"], chip, "setup", "ERROR", str(e))
            continue
        for it in items:
            try:
                if it["kind"] == "own":
                    prg = asm_prg(it["asm"])
                else:
                    prg = vice_prg(it["vice"], chip)
                mem = b.run_item(it, chip, prg)
                if getattr(b, "dry", False):
                    record(it["id"], chip, it["title"], "DRY", "program " + os.path.basename(prg))
                    continue
                for check, status, detail in it["verdict"](mem, chip):
                    if it["kind"] == "vice":
                        check = it["title"]
                    if it.get("info_only"):
                        status = "INFO"
                    if selftest and status in ("PASS", "FAIL"):
                        want = RESID_EXPECT.get((chip, it["id"], check), "PASS")
                        detail = "reSID answers %s%s" % (status, "" if status == want else ", reference expects " + want)
                        if want == "FAIL":
                            detail += " (known: the reference leaves the real chip here)"
                        status = "OK" if status == want else "FAIL"
                    record(it["id"], chip, check, status, detail)
            except Exception as e:
                # an information-only item never fails the suite, not even when it does not finish
                record(it["id"], chip, "run", "INFO" if it.get("info_only") else "ERROR", "%s: %s" % (type(e).__name__, e))


def summary():
    print("\n== summary")
    by = {}
    for item, chip, check, status, detail in OUT:
        by.setdefault(status, 0)
        by[status] += 1
    for k in ("PASS", "OK", "FAIL", "ERROR", "INFO", "EAR", "SKIP", "DRY"):
        if k in by:
            print("  %-5s %d" % (k, by[k]))
    bad = [o for o in OUT if o[3] in ("FAIL", "ERROR")]
    for item, chip, check, status, detail in bad:
        print("  %s %s [%s] %s -- %s" % (status, item, chip, check, detail))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--chips", default="6581,8580")
    ap.add_argument("--items", default="", help="comma list of item ids (default: all automated)")
    ap.add_argument("--engine", choices=("resid8", "ultisid"), default="resid8",
                    help="which menu names to use; ultisid = Gideon's core (baseline run, the bugs are expected there)")
    ap.add_argument("--with-info", action="store_true", help="also run the information-only waveforms-80 (noise, ~25 s each)")
    ap.add_argument("--ear", action="store_true", help="record the digi items to WAV (needs media, see README)")
    ap.add_argument("--ear-only", action="store_true")
    ap.add_argument("--ear-prepared", action="store_true", help="the REU image is loaded by hand: run the REU ear items too")
    ap.add_argument("--items-ear", default="", help="comma list of ear item ids (default: all)")
    ap.add_argument("--ear-seconds", type=int, default=25)
    ap.add_argument("--load-wait", type=int, default=20, help="seconds between LOAD and RUN for disk games")
    ap.add_argument("--wav-dir", default=os.path.join(HERE, "wav"))
    ap.add_argument("--multicast", default=None, help="239.0.1.65 to stream to the multicast group instead of unicast")
    ap.add_argument("--selftest", action="store_true", help="offline: run the programs on TRX64's reSID, compare with the known reference answers")
    ap.add_argument("--dry-run", action="store_true", help="print the REST calls, do not connect")
    ap.add_argument("--no-reset-end", action="store_true")
    args = ap.parse_args()
    args.chips = [c for c in args.chips.split(",") if c]
    items = [i for i in ITEMS + (INFO_ITEMS if args.with_info else []) if not args.items or i["id"] in args.items.split(",")]
    if args.selftest:
        b = SimBackend()
        print("selftest: TRX64 reSID + cycle-exact 6502 (no board)")
    else:
        b = BoardBackend(args.host, dry=args.dry_run)
        print("%s: %s, engine %s, chips %s" % ("dry-run" if args.dry_run else "board", args.host, args.engine, ",".join(args.chips)))
        if not args.dry_run:
            try:
                v = b.json("GET", "version")
                print("  version", json.dumps(v)[:120])
            except Exception as e:
                print("board not reachable:", e)
                return 2
    atexit.register(lambda: b.restore())
    signal.signal(signal.SIGINT, lambda *a: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        if args.selftest:
            verdict_selftest()
        if not args.ear_only:
            run_items(b, items, args.chips, args.engine, args.selftest)
        if (args.ear or args.ear_only) and not args.selftest:
            ear_run(b, args, lambda chip: settings_plan(args.engine, chip, two=True))
    except KeyboardInterrupt:
        print("\ninterrupted: restoring settings")
    finally:
        b.restore()
        if not args.selftest and not args.dry_run and not args.no_reset_end:
            try:
                b.reset()
            except Exception:
                pass
    return summary()


if __name__ == "__main__":
    sys.exit(main())
