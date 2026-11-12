#!/usr/bin/env python3
"""T101 S7: the host app names no machine. Scans the host app's own files for machine words in code and
strings (comments and the render bench's test block are not code the board runs). neutral.py --selftest plants a
word and must see it."""
import os, re, sys, tempfile

WORDS = re.compile(r"\b(c64|vic|vic20|sid|cia|cartridge|basic|vc-?20|amiga|atari|kernal|sprite)\b", re.I)
OWN = ["hostapp.cc", "hostui.cc", "hostui.h", "bootcfg.cc", "bootcfg.h", "cor.cc", "cor.h", "coreboot.cc", "coreboot.h",
       "hostfs.cc", "hostfs.h", "discover.cc", "discover.h", "startcheck.cc", "startcheck.h", "listwin.h",
       "cfw.cc", "cfw.h", "cfwicon.cc", "cfwicon.h", "fatwalk.cc", "fatwalk.h", "legacy.cc", "legacy.h", "legacy_tramp.cc",
       "mbr.cc", "mbr.h", "mediasniff.cc", "mediasniff.h", "mediaui.cc", "mediaui.h", "mediaglue.cc", "mediaglue.h", "fmt.cc", "fmt.h",
       "fmtconfirm.cc", "fmtconfirm.h", "fwconvert.cc", "fwconvert.h", "fwknown.cc", "fwknown.h", "known_fw.h"]

def code_lines(text):
    out, skip = [], False
    for n, line in enumerate(text.split("\n"), 1):
        if line.startswith("#ifdef HOSTUI_RENDER_TEST"):
            skip = True
        if skip:
            continue
        line = re.sub(r"//.*", "", line)
        line = re.sub(r"/\*.*?\*/", "", line)
        out.append((n, line))
    return out

def scan(path):
    return ["%s:%d: %s" % (os.path.basename(path), n, l.strip()) for n, l in code_lines(open(path).read()) if WORDS.search(l)]

if "--selftest" in sys.argv:
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.cc")
        open(p, "w").write("int ok;\n// vic in a comment\nint vic_bank = 1; int sid;\n")
        hits = scan(p)
        assert len(hits) == 1 and ":3:" in hits[0], hits
    print("neutral selftest: ok (planted word found, comment ignored)")
    sys.exit(0)

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "host-app", "software", "hostapp")
hits = [h for f in OWN for h in scan(os.path.join(root, f))]
for h in hits:
    print("NEUTRALITY:", h)
print("neutral: %s" % ("ok" if not hits else "%d hit(s)" % len(hits)))
sys.exit(1 if hits else 0)
