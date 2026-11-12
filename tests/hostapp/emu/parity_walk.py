#!/usr/bin/env python3
"""T192.2 P5 (S11, S12, S16; epic S1): the PARITY WALK. One key walk in ue2emu on the OLD host app (hostapp_emu.elf of 229b60be8) and on the NEW one,
same kit, same flash.bin copy, same stick, one emulator process each. After every step: <out>/<build>/<NN>_<name>.png (the screen) and .txt (the REST
state of the host app, normalised). Then: the texts must be equal byte for byte (a diff = FAIL, the first differing line is printed); the PNGs are compared
pixel by pixel with tools/render_compare.py (reported; 0 differing pixels is required only for the steps marked layout-unchanged in WALK).

  parity_walk.py --old OLD.elf --new NEW.elf --out DIR [--kit DIR] [--emu PATH] [--controls] [--reuse] [--all-layout] [--walk FILE]
  parity_walk.py --selftest                 the normalise and compare logic on planted differences (no emulator)
  --reuse      keep runs of <out> that already have every step (re-compare); the walk is parity.walk (--walk F)
  --controls   after the walk: (a) old-vs-old with one stick file renamed must differ, (b) one footer character changed in a copy of the new run's
               dump must be red, (c) S12: the same build twice gives identical pngs and dumps (no drop is listed; a listed one must differ between the two runs)
Exit: 0 all equal, 1 a text difference / a layout-unchanged pixel difference / a failed control, 77 = no emulator, kit or ELF (skipped out loud).
Needs a local ue2emu binary and the kit of tools/ue2emu_kit.sh (default ~/scratch/ue2emu-kit: roms/, flash.bin, cors/vc20.cor)."""
import json, os, re, shutil, socket, subprocess, sys, tempfile, time, urllib.request, urllib.error

SPEED = os.environ.get('PARITY_SPEED', 'realtime')   # realtime: the wall time between two script lines is small against the waits; at max the machine runs on between steps and a timer (a note, 8 s picture confirm) fires between them
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def load_rc():
    """tools/render_compare.py ends in sys.exit(main()): load its functions (png_pixels, compare) with that line cut; the compare semantic is its own."""
    src = open(os.path.join(ROOT, 'tools', 'render_compare.py')).read().replace('\nsys.exit(main())', '\n')
    m = type(sys)('render_compare')
    exec(compile(src, 'render_compare.py', 'exec'), m.__dict__)
    return m


# ---- what is dropped from the REST state, and why ---------------------------------------------------------------------------------------------------------
# NOTHING is dropped: two runs of one build give identical dumps (control (c); the revision counter, countdown, clock and uptime are not in this state, the
# boot menu is empty at the start so no countdown runs, the network is the emulator's fixed 10.0.2.15). A drop would be listed here as key path -> reason
# and control (c) fails when a listed drop never differs between two runs (a drop must be needed, and must never hide a footer, a label or a row).
VOLATILE = {
}
# regexes applied to every string value: (pattern, replacement, reason). Each is exercised by the determinism control (the walk shows the text).
VOLATILE_TEXT = [
]


# Declared actions the new build adds on purpose (T192.2 open item 7: the old web page's commands as intents of the service). They are taken out of the
# new build's action list, nothing else of it: the old build has none of them, and any other difference in the list stays a FAIL.
NEW_OPS = {'web.start', 'web.default', 'web.video', 'web.countdown'}


def drop_volatile(o, path=''):
    if isinstance(o, dict):
        return {k: drop_volatile(v, path + '.' + k if path else k) for k, v in o.items() if (path + '.' + k if path else k) not in VOLATILE}
    if isinstance(o, list):
        return [drop_volatile(v, path) for v in o if not (path == 'ops' and isinstance(v, dict) and v.get('id') in NEW_OPS)]
    if isinstance(o, str):
        for pat, rep, _why in VOLATILE_TEXT:
            o = re.sub(pat, rep, o)
    return o


def normalise(state_text):
    """The state JSON as text, one value per line, key order kept (the producer's), the volatile things dropped."""
    o = json.loads(state_text)
    o = drop_volatile(o)
    lines = []

    def walk(v, p):
        if isinstance(v, dict):
            for k, x in v.items():
                walk(x, p + '.' + k)
        elif isinstance(v, list):
            lines.append('%s[%d]' % (p, len(v)))
            for i, x in enumerate(v):
                walk(x, '%s[%d]' % (p, i))
        else:
            lines.append('%s = %s' % (p, json.dumps(v, ensure_ascii=False)))
    walk(o, '')
    return '\n'.join(lines) + '\n'


# ---- the emulator ---------------------------------------------------------------------------------------------------------------------------------
def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


class Emu:
    def __init__(self, elf, work, kit, emu, stick_rename=None):
        self.work = work
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
        stick = os.path.join(work, 'stick')
        make_stick(stick, kit, stick_rename)
        os.makedirs(os.path.join(work, 'cors'))
        os.makedirs(os.path.join(work, 'profiles'))
        shutil.copy(os.path.join(kit, 'cors', 'vc20.cor'), os.path.join(work, 'cors', 'vc20.cor'))
        shutil.copy(os.path.join(kit, 'profiles', 'VIC-20.toml'), os.path.join(work, 'profiles', 'VIC-20.toml'))
        shutil.copy(os.path.join(kit, 'flash.bin'), os.path.join(work, 'flash.bin'))
        self.ctlport, self.http = free_port(), free_port()
        self.log = open(os.path.join(work, 'emu.log'), 'w')
        self.p = subprocess.Popen(['nice', '-n', '15', emu, 'run', '--board', 'c64u', '--trx-io=on', '--icap', '--cor-dir', os.path.join(work, 'cors'),
                                   '--core-dir', os.path.join(work, 'profiles'), '--firmware', elf, '--roms', os.path.join(kit, 'roms'),
                                   '--flash', os.path.join(work, 'flash.bin'), '--usb-dir', stick, '--usb-dir-work', os.path.join(work, 'usbwork'),
                                   '--headless', '--speed', SPEED, '--net', 'user', '--hostfwd', 'tcp:127.0.0.1:%d:80' % self.http,
                                   '--control', '127.0.0.1:%d' % self.ctlport, '--max-seconds', '900'],
                                  stdout=self.log, stderr=self.log, cwd=work)
        self.sock = None
        for _ in range(300):
            try:
                self.sock = socket.create_connection(('127.0.0.1', self.ctlport)); break
            except OSError:
                time.sleep(0.1)
        if not self.sock:
            raise RuntimeError('no control port; log tail:\n' + log_tail(self.log.name))
        self.rf = self.sock.makefile('r')
        self.tok = None

    def ctl(self, line):
        self.sock.sendall((line + '\n').encode())
        lines = []
        while True:
            l = self.rf.readline()
            if not l:
                return 'error closed', lines
            l = l.rstrip('\n')
            if l.startswith('ok') or l.startswith('error'):
                return l, lines
            lines.append(l)

    def rest(self, method, path, token=True):
        url = 'http://127.0.0.1:%d%s' % (self.http, path)
        if token and self.tok:
            url += ('&' if '?' in url else '?') + 't=' + self.tok
        try:
            with urllib.request.urlopen(urllib.request.Request(url, method=method, data=(b'' if method == 'POST' else None)), timeout=20) as r:
                return r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode()
        except Exception as e:
            return 0, str(e)

    def boot(self):
        r, _ = self.ctl('expect-console "[web] http" 180000')
        if not r.startswith('ok'):
            raise RuntimeError('the host app did not come up (%s); log tail:\n%s' % (r, log_tail(self.log.name)))
        self.ctl('wait 1500')
        for _ in range(120):
            st, _b = self.rest('GET', '/v1/ui/hello', token=False)
            if st == 200:
                break
            self.ctl('wait 500')
        else:
            raise RuntimeError('no /v1/ui/hello; log tail:\n' + log_tail(self.log.name))
        st, b = self.rest('POST', '/v1/ui/grant', token=False)
        self.tok = json.loads(b).get('token') if st == 200 else None
        if not self.tok:
            raise RuntimeError('no token (%s %s)' % (st, b[:80]))

    def state_text(self):
        """The state; a 504 (the UI task is busy, e.g. while the picture mode changes) is asked again after 500 ms of emulated time, up to 20 times."""
        for _ in range(20):
            st, b = self.rest('GET', '/v1/ui/state')
            if st == 200:
                return b
            if st != 504:
                break
            self.ctl('wait 500')
        raise RuntimeError('state: %s %s' % (st, b[:100]))

    def close(self):
        try:
            self.ctl('quit')
        except Exception:
            pass
        try:
            self.p.wait(timeout=20)
        except Exception:
            self.p.kill()
        self.log.close()


def log_tail(path, n=25):
    try:
        return ''.join(open(path, errors='replace').readlines()[-n:])
    except Exception:
        return '(no log)'


# ---- the stick --------------------------------------------------------------------------------------------------------------------------------------
def make_stick(stick, kit, rename=None):
    """The same stick for both builds: cores/vc20.cor (+ roms dir), docs/readme.txt (plain file), docs/notes.md, a folder with files. rename = (from, to)
    renames one file after the build (control a)."""
    for d in ('cores/vc20/roms', 'docs', 'games'):
        os.makedirs(os.path.join(stick, d), exist_ok=True)
    shutil.copy(os.path.join(kit, 'cors', 'vc20.cor'), os.path.join(stick, 'cores', 'vc20.cor'))
    for n, sz in (('basic.bin', 8192), ('kernal.bin', 8192), ('char.bin', 4096)):
        open(os.path.join(stick, 'cores', 'vc20', 'roms', n), 'wb').write(bytes(sz))
    open(os.path.join(stick, 'docs', 'readme.txt'), 'w').write('hello\n' * 400)
    open(os.path.join(stick, 'docs', 'notes.md'), 'w').write('# notes\nsecond line\n')
    for i in range(1, 4):
        open(os.path.join(stick, 'games', 'game%d.prg' % i), 'wb').write(bytes([1, 8]) + bytes(range(256)) * i)
    if rename:
        os.rename(os.path.join(stick, rename[0]), os.path.join(stick, rename[1]))
    fx = txu_fixtures()
    for n in ('both.txu', 'not_txu.cfw'):
        shutil.copy(os.path.join(fx, n), os.path.join(stick, 'docs', n))


_FX = []


def txu_fixtures():
    """A .txu package (host + app) and a vendor-like .cfw that is no .txu, made by tests/hostapp/mktxutest.py (the real writer); once per process."""
    if not _FX:
        d = tempfile.mkdtemp(prefix='parity-fx-')
        subprocess.run([sys.executable, os.path.join(ROOT, 'tests', 'hostapp', 'mktxutest.py'), d], check=True, stdout=subprocess.DEVNULL)
        _FX.append(d)
    return _FX[0]


# ---- the walk ---------------------------------------------------------------------------------------------------------------------------------------
# parity.walk: `step NAME [layout]`, then control-language lines (S44 syntax: key, wait, hold, release, restore, type). Every `key` line is followed by
# KEYWAIT ms, the step by SETTLE ms, then png + state. `layout` = a screen the move to the declarative menu must not change at all (0 pixels).
KEYWAIT, SETTLE = 450, 900


def parse_walk(text):
    steps = []
    for ln in text.splitlines():
        ln = ln.strip() if ln.strip().startswith('type ') else ln.split('#', 1)[0].strip()
        if not ln:
            continue
        if ln.startswith('step '):
            f = ln.split()
            steps.append((f[1], 'layout' in f[2:], []))
        elif steps:
            steps[-1][2].append(ln)
        else:
            raise SystemExit('walk: a line before the first step: ' + ln)
    return steps


def run_build(tag, elf, out, kit, emu, steps, rename=None):
    """One emulator process: boot, walk. Writes <out>/<tag>/NN_name.png and .txt (normalised) and <out>/<tag>-raw/NN_name.json (the state as it came,
    for the control that every drop is exercised). Returns the file stems."""
    d, dr = os.path.join(out, tag), os.path.join(out, tag + '-raw')
    for x in (d, dr):
        shutil.rmtree(x, ignore_errors=True)
        os.makedirs(x)
    e = Emu(elf, os.path.join(out, 'work-' + tag), kit, emu, rename)
    stems, name = [], 'boot'
    try:
        e.boot()
        for i, (name, _lay, cmds) in enumerate(steps):
            for c in cmds:
                r, ls = e.ctl(c)
                if not r.startswith('ok'):
                    raise RuntimeError('step %s: "%s" -> %s %s' % (name, c, r, ls))
                if c.startswith(('key', 'restore')):
                    e.ctl('wait %d' % KEYWAIT)
            e.ctl('wait %d' % SETTLE)
            stem = '%02d_%s' % (i, name)
            r, _ = e.ctl('png ' + os.path.join(d, stem + '.png'))
            if not r.startswith('ok'):
                raise RuntimeError('png failed at %s: %s' % (name, r))
            raw = e.state_text()
            open(os.path.join(dr, stem + '.json'), 'w').write(raw)
            open(os.path.join(d, stem + '.txt'), 'w').write(normalise(raw))
            stems.append(stem)
    except Exception:
        print('walk failed on the %s run at step %s; emulator log tail:\n%s' % (tag, name, log_tail(os.path.join(out, 'work-' + tag, 'emu.log'))), file=sys.stderr)
        raise
    finally:
        e.close()
    return stems


# ---- the compare ------------------------------------------------------------------------------------------------------------------------------------
def first_diff(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else '(end of dump)'
        y = lb[i] if i < len(lb) else '(end of dump)'
        if x != y:
            return i + 1, x, y
    return None


# The one mask: the C64's own text cursor. Under the host UI the emulator's picture carries the C64's BASIC screen, whose cursor blinks on the C64's clock
# while the walk's REST reads take a few unscripted milliseconds of emulated time: the phase at the moment of `png` is not reproducible (seen: 1024 px = one
# 32 x 32 cell in old-vs-new, texts equal). A pixel difference is that cursor, and only then not counted, when ALL differing pixels lie in one cell (frame
# width / 60 square) outside the UI canvas (the top-left 960 x 540 of a 1920 x 1080 frame, where every footer, label and row is drawn; frames of another size
# carry no UI at all). A single changed pixel anywhere in the canvas, or a difference larger than a cell, is a real difference.
def cursor_cell_only(w, h, box):
    x0, y0, x1, y1 = box
    if w < 480:
        return False
    cell = w // 60
    if x1 - x0 + 1 > cell or y1 - y0 + 1 > cell:
        return False
    if (w, h) == (1920, 1080):
        return x0 >= 960 or y0 >= 540
    return True


def png_diff(rc, da, db, stem):
    """(differing pixels, of them masked as the C64 cursor, message) of <stem>.png in the dirs da and db. The pixel compare is render_compare's:
    png_pixels decodes, rows are compared as compare() does; compare() itself gives the count that is cross-checked. None = not comparable."""
    pa, pb = os.path.join(da, stem + '.png'), os.path.join(db, stem + '.png')
    ia, ib = rc.png_pixels(pa), rc.png_pixels(pb)
    if not ia or not ib or ia[:2] != ib[:2]:
        return None, 0, 'pictures not comparable pixel by pixel (%s vs %s)' % (ia and ia[:2], ib and ib[:2])
    w, h, bpp, ra = ia
    rb = ib[3]
    cnt, box = 0, None
    for y in range(h):
        if ra[y] == rb[y]:
            continue
        for x in range(w):
            if ra[y][x * bpp:x * bpp + bpp] != rb[y][x * bpp:x * bpp + bpp]:
                cnt += 1
                box = (x, y, x, y) if box is None else (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    if cnt == 0:
        return 0, 0, ''
    skip = {n for n in set(os.listdir(da)) | set(os.listdir(db)) if n != stem + '.png'}
    m = re.search(r'(\d+) pixels', (rc.compare(da, db, skip) or [''])[0])
    assert m and int(m.group(1)) == cnt, 'the pixel count disagrees with render_compare.compare'
    return cnt, (cnt if cursor_cell_only(w, h, box) else 0), ''


def compare_runs(ra, rb, out, layout, quiet=False):
    """Texts byte for byte, PNGs by pixels. Returns (text_fails, layout_fails, rows[(stem, text_ok, px, masked_px)])."""
    rc = load_rc()
    da, db = os.path.join(out, ra), os.path.join(out, rb)
    tf = lf = 0
    rows = []
    for s in sorted(n[:-4] for n in os.listdir(da) if n.endswith('.txt')):
        ta = open(os.path.join(da, s + '.txt')).read()
        tbp = os.path.join(db, s + '.txt')
        fd = first_diff(ta, open(tbp).read()) if os.path.exists(tbp) else (0, '(step)', '(missing)')
        px, masked, msg = png_diff(rc, da, db, s) if os.path.exists(os.path.join(db, s + '.png')) else (None, 0, 'missing')
        lay = s.split('_', 1)[1] in layout
        real = None if px is None else px - masked
        ok_t, ok_p = fd is None, (real == 0 or not lay)
        tf += 0 if ok_t else 1
        lf += 0 if ok_p else 1
        rows.append((s, ok_t, px, masked))
        if not quiet:
            what = 'equal' if px == 0 else (msg if px is None else '%d px differ%s' % (px, ' (all in the C64 cursor cell: masked)' if masked else ''))
            print('%s %-32s text %-8s png %s%s' % ('PASS' if ok_t and ok_p else 'FAIL', s, 'equal' if ok_t else 'DIFFERS', what, '  [layout-unchanged]' if lay else ''))
            if not ok_t:
                print('     first differing line %d:\n       %s: %s\n       %s: %s' % (fd[0], ra, fd[1], rb, fd[2]))
            if px != 0 and real != 0:
                print('     look at: %s  vs  %s' % (os.path.join(da, s + '.png'), os.path.join(db, s + '.png')))
    return tf, lf, rows


def raw_drops_exercised(out, ra, rb):
    """For every key in VOLATILE: the raw states of two runs of one build differ in it somewhere (so the drop is needed, not decoration)."""
    seen = {k: False for k in VOLATILE}
    for n in sorted(os.listdir(os.path.join(out, ra))):
        pb = os.path.join(out, rb, n)
        if not os.path.exists(pb):
            continue
        a, b = json.load(open(os.path.join(out, ra, n))), json.load(open(pb))
        for k in VOLATILE:
            if a.get(k) != b.get(k):
                seen[k] = True
    return seen


# ---- selftest ---------------------------------------------------------------------------------------------------------------------------------------
def selftest():
    bad = 0

    def check(ok, what):
        nonlocal bad
        print('%s selftest: %s' % ('PASS' if ok else 'FAIL', what))
        bad += 0 if ok else 1
    s1 = '{"rev":3,"footer":{"legend":"UP/DOWN select"},"items":[{"id":"a","title":"A"}]}'
    s2 = '{"rev":9,"footer":{"legend":"UP/DOWN select"},"items":[{"id":"a","title":"A"}]}'
    s3 = '{"rev":3,"footer":{"legend":"UP/DOWN selekt"},"items":[{"id":"a","title":"A"}]}'
    check(normalise(s1) != normalise(s2), 'nothing is dropped: a changed counter is a difference')
    VOLATILE['rev'] = 'selftest'
    check(normalise(s1) == normalise(s2), 'a listed drop is dropped')
    del VOLATILE['rev']
    d = first_diff(normalise(s1), normalise(s3))
    check(d is not None and 'selekt' in d[2] and d[0] == 2, 'one footer character is a difference, reported with its line: %s' % (d,))
    check(first_diff(normalise(s1), normalise(s1.replace('"title":"A"', '"title":"B"'))) is not None, 'a row title is not dropped')
    t = tempfile.mkdtemp()
    try:
        rc = load_rc()
        for tag in ('a', 'b', 'c'):
            os.makedirs(os.path.join(t, tag))
            rc.write_png(os.path.join(t, tag, '00_x.png'), 8, 8, lambda x, y, tag=tag: (10, 20, 31) if (tag == 'b' and (x, y) == (3, 3)) else (10, 20, 30))
            open(os.path.join(t, tag, '00_x.txt'), 'w').write('x\n')
        check(png_diff(rc, os.path.join(t, 'a'), os.path.join(t, 'c'), '00_x')[0] == 0, 'two equal pictures: 0 pixels')
        check(png_diff(rc, os.path.join(t, 'a'), os.path.join(t, 'b'), '00_x')[0] == 1, 'one changed pixel: counted')
        tf, lf, _ = compare_runs('a', 'b', t, {'x'}, quiet=True)
        check(tf == 0 and lf == 1, 'a layout-unchanged step with one pixel is a failure, the text is equal')
        tf, lf, _ = compare_runs('a', 'b', t, set(), quiet=True)
        check(tf == 0 and lf == 0, 'the same pixel in a step not listed as layout-unchanged is only reported')
    finally:
        shutil.rmtree(t)
    check(cursor_cell_only(1920, 1080, (1000, 700, 1031, 731)) and not cursor_cell_only(1920, 1080, (1000, 700, 1032, 731)), 'the cursor mask: one 32 x 32 cell yes, one pixel wider no')
    check(not cursor_cell_only(1920, 1080, (100, 400, 100, 400)) and not cursor_cell_only(1920, 1080, (900, 500, 910, 510)), 'the cursor mask: never inside the UI canvas (one pixel of a footer is real)')
    check(cursor_cell_only(960, 540, (208, 454, 223, 469)), 'the cursor mask: a 480p frame carries no UI, one 16 x 16 cell')
    r = parse_walk('step a layout\n key f3  # c\nstep b\n type "x # y"\n')
    check(r == [('a', True, ['key f3']), ('b', False, ['type "x # y"'])], 'the walk parser: %s' % (r,))
    return 1 if bad else 0


# ---- main -------------------------------------------------------------------------------------------------------------------------------------------
def main():
    import argparse
    from concurrent.futures import ThreadPoolExecutor
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--old')
    ap.add_argument('--new')
    ap.add_argument('--out')
    ap.add_argument('--kit', default=os.path.expanduser('~/scratch/ue2emu-kit'))
    ap.add_argument('--emu', default=os.environ.get('UE2EMU_BIN') or os.path.expanduser('~/Development/u64-emulator/target/release/ue2emu'))
    ap.add_argument('--controls', action='store_true')
    ap.add_argument('--walk', default=os.path.join(HERE, 'parity.walk'))
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--all-layout', action='store_true', help='every step counts as layout-unchanged (0 differing pixels outside the masked cursor cell)')
    ap.add_argument('--reuse', action='store_true', help='keep a run of <out> that already has every step (a re-compare while developing the walk)')
    a = ap.parse_args()
    for k in ('old', 'new', 'out', 'kit', 'emu', 'walk'):
        if getattr(a, k):
            setattr(a, k, os.path.abspath(getattr(a, k)))
    if a.selftest:
        return selftest()
    need = [a.emu, os.path.join(a.kit, 'roms'), os.path.join(a.kit, 'flash.bin'), os.path.join(a.kit, 'cors', 'vc20.cor'),
            os.path.join(a.kit, 'profiles', 'VIC-20.toml'), a.walk] + [x for x in (a.old, a.new) if x]
    if not a.old or not a.new or not a.out or any(not os.path.exists(x) for x in need):
        print('parity_walk SKIPPED out loud: need --old, --new, --out, a local ue2emu (%s), the kit (%s: roms/, flash.bin, cors/vc20.cor, profiles/VIC-20.toml) '
              'and the walk (%s)' % (a.emu, a.kit, a.walk))
        return 77
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    steps = parse_walk(open(a.walk).read())
    layout = {n for n, lay, _ in steps if lay or a.all_layout}
    t0 = time.time()
    fails = 0
    jobs = [('old', a.old, None), ('new', a.new, None)]
    if a.controls:
        jobs += [('old-ren', a.old, ('docs/readme.txt', 'docs/readme2.txt')), ('old-2', a.old, None), ('new-2', a.new, None)]
    results = {}

    def one(j):
        done = os.path.join(out, j[0])
        if a.reuse and os.path.isdir(done) and len([n for n in os.listdir(done) if n.endswith('.txt')]) == len(steps):
            for n in os.listdir(done + '-raw'):      # the dumps are made again from the raw states: the drops may have changed
                open(os.path.join(done, n[:-5] + '.txt'), 'w').write(normalise(open(os.path.join(done + '-raw', n)).read()))
            return j[0], sorted(n[:-4] for n in os.listdir(done) if n.endswith('.txt')), None
        try:
            return j[0], run_build(j[0], j[1], out, a.kit, a.emu, steps, j[2]), None
        except Exception as ex:
            return j[0], None, ex
    with ThreadPoolExecutor(max_workers=2) as ex:     # at most two emulators at a time (nice -n 15)
        for tag, stems, err in ex.map(one, jobs):
            results[tag] = (stems, err)
            print('run %s: %s' % (tag, ('%d steps' % len(stems)) if not err else 'FAILED: %s' % err), flush=True)
    for t in ('old', 'new'):
        if results[t][1]:
            print('parity_walk: the %s build did not complete its walk: %s' % (t, results[t][1]))
            return 1
    print('\n== old vs new, step by step (layout-unchanged: %s) ==' % (', '.join(sorted(layout)) or 'none'))
    tf, lf, rows = compare_runs('old', 'new', out, layout)
    pxd = [r for r in rows if r[2] != 0]
    nmask = [r for r in rows if r[3]]
    print('\nold vs new: %d steps, %d with a text difference, %d layout-unchanged steps with differing pixels, %d steps with a pixel difference (%d of them only the masked C64 cursor cell)'
          % (len(rows), tf, lf, len(pxd), len(nmask)))
    fails += 1 if (tf or lf) else 0
    if a.controls:
        print('\n== controls ==')
        ok = True

        def ctrl(good, what):
            nonlocal ok
            print('%s control %s' % ('PASS' if good else 'FAIL', what))
            ok = ok and good
        if results['old-ren'][1]:
            ctrl(False, '(a) the old-vs-old run with a renamed stick file did not complete: %s' % results['old-ren'][1])
        else:
            tfa, _, _ = compare_runs('old', 'old-ren', out, set(), quiet=True)
            ctrl(tfa > 0, '(a) old vs old with docs/readme.txt renamed: %d steps differ in text (must be > 0)' % tfa)
        # (b) the new run's dump with one footer character changed, in a copy of the run (done on the dump, not on a rebuilt app)
        nm = os.path.join(out, 'new-mut')
        shutil.rmtree(nm, ignore_errors=True)
        shutil.copytree(os.path.join(out, 'new'), nm)
        stem = next((s for s, tok, _, _ in rows if tok and 'footer.legend' in open(os.path.join(nm, s + '.txt')).read()), None)
        if stem:
            p = os.path.join(nm, stem + '.txt')
            t = open(p).read()
            i = t.index('.footer.legend = "') + len('.footer.legend = "')
            open(p, 'w').write(t[:i] + chr(ord(t[i]) ^ 1) + t[i + 1:])
            tfb, _, rb = compare_runs('old', 'new-mut', out, set(), quiet=True)
            ctrl(tfb == tf + 1 and not [r for r in rb if r[0] == stem and r[1]], '(b) one footer character changed in the DUMP of step %s of the new run (not a rebuilt app): the compare is red there' % stem)
        else:
            ctrl(False, '(b) no step with an equal footer to mutate')
        # (c) S12: two runs of one build are identical (texts and pixels); the drops are exercised
        masked_used = [r[0] for r in rows if r[3]]
        for t1, t2 in (('old', 'old-2'), ('new', 'new-2')):
            if results[t2][1]:
                ctrl(False, '(c) the second %s run did not complete: %s' % (t1, results[t2][1]))
                continue
            tfc, _, rc_ = compare_runs(t1, t2, out, set(), quiet=True)
            diffpx = [r[0] for r in rc_ if r[2] is None or r[2] != r[3]]
            masked_used.extend(r[0] for r in rc_ if r[3])
            ctrl(tfc == 0 and not diffpx, '(c) S12 determinism, %s twice: texts equal in all %d steps, pixels equal outside the masked C64 cursor cell (differing: %s)' % (t1, len(rc_), diffpx or 'none'))
            seen = raw_drops_exercised(out, t1 + '-raw', t2 + '-raw')
            for k, v in seen.items():
                ctrl(v, '(c) drop "%s" (%s) is exercised: the raw state differs between the two %s runs' % (k, VOLATILE[k], t1))
        print('%s mask C64 cursor cell: needed in %d compare(s) of this run (%s)' % ('NOTE' if masked_used else 'NOTE (not needed in this run, so not exercised by it)', len(masked_used), ', '.join(masked_used[:6]) or 'none'))
        fails += 0 if ok else 1
    print('\nparity_walk: %s in %.0f s; pictures in %s' % ('FAILED' if fails else 'all text equal, layout-unchanged screens pixel-equal', time.time() - t0, out))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
