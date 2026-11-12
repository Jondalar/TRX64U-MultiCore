#!/usr/bin/env python3
"""T168 S9: the ported trees of DDT's designs (cores/ddt_mandel, cores/ddt_novic) are marked as CC BY 4.0 asks.

  1. every file of rtl/ is byte-identical to the upstream file it was copied from (rtl/SHA256SUMS holds the hashes taken at the pinned commit)
  2. every changed file has its .diff next to the upstream copy, and the .diff is the difference between that copy and the file that is built (regenerated here with difflib's unified diff; `python3 tools/check_ddt_tree.py regen` rewrites them)
  3. NOTICE names the licence (CC BY 4.0), the author's repository and the commit, and lists every changed file
  4. no character ROM: no c64_chargen*, no *.rom, no file of rtl/ or trx/ reads one (the C64's character ROM is Commodore's and never goes into the tree)
  5. the Mandelbrot palette is what tools/ddt_palette.py makes of DDT's colours

  tools/check_ddt_tree.py            check the tree
  tools/check_ddt_tree.py regen      rewrite the .diff files
  tools/check_ddt_tree.py selftest   the checks against a scratch copy with one thing broken each; each MUST fail
"""
import difflib, hashlib, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# core -> (repository, commit, {changed file under trx/: upstream file under rtl/})
CORES = {
    "ddt_mandel": ("github.com/0x444454/mandelbrUlt", "93aee1cc6ae33ff927a32012b5696f1dee1619a1", {"screen_matrix.v": "screen_matrix.v", "pixel_gen_mandelbrot.sv": "pixel_gen_mandelbrot.sv", "fb_scanline_writer.sv": "fb_scanline_writer.sv"}),
    "ddt_novic": ("github.com/0x444454/look_ma_no_vic", "1002b7f070e9a9b89cc47e49413523e269a9d382", {"ddt_novic_video.v": "video_pattern.v"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def diff_text(old, new):
    """The unified diff of upstream copy and built file, made by difflib so that every machine makes the same bytes (the diff of macOS and of GNU differ)."""
    a = open(old, newline="").read().splitlines(keepends=True)
    b = open(new, newline="").read().splitlines(keepends=True)
    out = difflib.unified_diff(a, b, "rtl/%s (upstream)" % os.path.basename(old), "trx/%s" % os.path.basename(new), n=3)
    return "".join(l if l.endswith("\n") else l + "\n\\ No newline at end of file\n" for l in out)


def check(root):
    bad = []
    for core, (repo, commit, changed) in CORES.items():
        d = os.path.join(root, "cores", core)
        rtl = os.path.join(d, "rtl")
        sums = {}
        for line in open(os.path.join(rtl, "SHA256SUMS")):
            h, name = line.split()
            sums[name] = h
        for name, h in sums.items():
            p = os.path.join(rtl, name)
            if not os.path.exists(p):
                bad.append("%s: rtl/%s is listed in SHA256SUMS and missing" % (core, name))
            elif sha(p) != h:
                bad.append("%s: rtl/%s differs from the upstream copy (SHA256SUMS)" % (core, name))
        for name in sorted(os.listdir(rtl)):
            if name.endswith(".diff") or name == "SHA256SUMS":
                continue
            if name not in sums:
                bad.append("%s: rtl/%s is not in SHA256SUMS (an upstream copy has a hash, a changed file lives in trx/)" % (core, name))
        notice = open(os.path.join(d, "NOTICE")).read()
        for need in ("CC BY 4.0", repo, commit):
            if need not in notice:
                bad.append("%s: NOTICE does not name %s" % (core, need))
        for new, old in changed.items():
            dp = os.path.join(rtl, old + ".diff")
            if not os.path.exists(dp):
                bad.append("%s: rtl/%s.diff is missing" % (core, old))
            elif open(dp).read() != diff_text(os.path.join(rtl, old), os.path.join(d, "trx", new)):
                bad.append("%s: rtl/%s.diff is not the difference between rtl/%s and trx/%s (regenerate it)" % (core, old, old, new))
            if "trx/" + new not in notice:
                bad.append("%s: NOTICE does not list the changed file trx/%s" % (core, new))
        for sub, dirs, files in os.walk(d):
            if os.sep + "build" in sub:
                continue
            for f in files:
                if re.search(r"chargen|\.rom$|\.bin$", f, re.I):
                    bad.append("%s: %s looks like a ROM image" % (core, os.path.relpath(os.path.join(sub, f), d)))
                if f.endswith((".v", ".sv", ".tcl")):
                    txt = open(os.path.join(sub, f), errors="replace").read()
                    code = re.sub(r"//[^\n]*", "", txt)
                    if re.search(r"readmemh\s*\(\s*\"[^\"]*chargen", code):
                        bad.append("%s: %s reads a character ROM" % (core, f))
    r = subprocess.run([sys.executable, os.path.join(root, "tools", "ddt_palette.py"), "--check"], capture_output=True, text=True)
    if r.returncode:
        bad.append("palette: " + (r.stderr or r.stdout).strip())
    return bad


def selftest():
    cases = []
    def mut(name, fn):
        cases.append((name, fn))
    mut("an upstream copy edited", lambda t: open(os.path.join(t, "cores/ddt_mandel/rtl/joystick_buttons.sv"), "a").write("\n// x\n"))
    mut("a changed file edited without its diff", lambda t: open(os.path.join(t, "cores/ddt_novic/trx/ddt_novic_video.v"), "a").write("\n// x\n"))
    mut("the diff missing", lambda t: os.remove(os.path.join(t, "cores/ddt_mandel/rtl/screen_matrix.v.diff")))
    mut("the licence missing from NOTICE", lambda t: open(os.path.join(t, "cores/ddt_novic/NOTICE"), "w").write("nothing"))
    mut("a character ROM added", lambda t: open(os.path.join(t, "cores/ddt_novic/trx/c64_chargen_ucg.hex"), "w").write("00\n"))
    mut("an unlisted file in rtl/", lambda t: open(os.path.join(t, "cores/ddt_mandel/rtl/extra.v"), "w").write("module x; endmodule\n"))
    for name, fn in cases:
        with tempfile.TemporaryDirectory() as t:
            for sub in ("cores/ddt_mandel", "cores/ddt_novic", "tools"):
                shutil.copytree(os.path.join(ROOT, sub), os.path.join(t, sub), ignore=shutil.ignore_patterns("build", "__pycache__"))
            bad = check(t)
            if bad:
                print("selftest: the clean copy fails: " + "; ".join(bad))
                return 1
            fn(t)
            if not check(t):
                print("selftest: %s was not found" % name)
                return 1
    print("selftest ok: %d breakages found" % len(cases))
    return 0


if len(sys.argv) > 1 and sys.argv[1] == "selftest":
    sys.exit(selftest())
if len(sys.argv) > 1 and sys.argv[1] == "regen":
    for core, (repo, commit, changed) in CORES.items():
        d = os.path.join(ROOT, "cores", core)
        for new, old in changed.items():
            open(os.path.join(d, "rtl", old + ".diff"), "w", newline="").write(diff_text(os.path.join(d, "rtl", old), os.path.join(d, "trx", new)))
            print("wrote cores/%s/rtl/%s.diff" % (core, old))
    sys.exit(0)
problems = check(ROOT)
for p in problems:
    print("FAIL " + p)
print("check_ddt_tree: %s" % ("%d problems" % len(problems) if problems else "ok"))
sys.exit(1 if problems else 0)
