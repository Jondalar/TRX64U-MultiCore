#!/usr/bin/env python3
"""T192 MVP: the core application as the Amiga 1200 in ue2emu (S44 phase A), driven over REST/WebSocket through the emulator's user-mode network while a
control SCRIPT takes the PNGs and presses the board's keys. PNGs go to OUTDIR.

  uimvp.py COREAPP.elf OUTDIR [--roms DIR] [--emu PATH]

Why a script and not the control port: with `--control` the emulator starts the application before its pixel layer is ready (the screen would be the
console log, every PNG black); with `--script` it does not. So the script is written first, and python and the script go in lockstep: the script waits
for a console line the application prints for every applied intent ("[ui] activate fs:x.adf -> taken"), then waits a moment and takes the PNG; python
does the next REST step when that PNG exists.

The emulator must be a local binary (the CI image runs without a network, ci/ue2emu.sh): without one the script exits 77 (skipped out loud).
Every check prints PASS/FAIL; the exit code is non-zero on any FAIL."""
import base64, json, os, shutil, socket, struct, subprocess, sys, time, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
app, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
roms = os.path.expanduser('~/Development/u64-emulator/firmware/1541ultimate/roms')
emu = os.environ.get('UE2EMU_BIN') or os.path.expanduser('~/Development/u64-emulator/target/release/ue2emu')
for i, a in enumerate(sys.argv):
    if a == '--roms': roms = sys.argv[i + 1]
    if a == '--emu': emu = sys.argv[i + 1]
if not os.path.exists(emu) or not os.path.exists(os.path.join(roms, 'chars.bin')):
    print('uimvp SKIPPED out loud: no local ue2emu (%s) or no ROMs (%s)' % (emu, roms)); sys.exit(77)
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    if f.endswith('.png'): os.remove(os.path.join(out, f))
work = os.path.join(out, 'work'); shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
stick = os.path.join(work, 'stick'); os.makedirs(os.path.join(stick, 'games'))
os.makedirs(os.path.join(stick, 'cores', 'amiga1200', 'roms'))   # the core folder; a blank Kickstart image stands in (no ROM is in the repository)
open(os.path.join(stick, 'cores', 'amiga1200', 'roms', 'kick31.rom'), 'wb').write(bytes(524288))
for n, b in (('x.adf', 1), ('y.adf', 2)):
    open(os.path.join(stick, 'games', n), 'wb').write(bytes([b]) * 901120)
HTTP = 18080 + os.getpid() % 500

# ---- the script: (what to wait for, png name) in the order python performs the steps ----------------------------------------------------------------------------
# kinds: ('m', marker, png)   wait for the console line, 400 ms, png
#        ('k', keyname, png)  press the key on the board, 600 ms, png
#        ('w', ms, png)       wait, png
#        ('c', command, png)  a control command, then png
STEPS = [
    ('m', '[ui] open -> taken', '20-rest-open-menu'),
    ('m', '[ui] key f3 -> taken', '21-library-root'),
    ('m', '[ui] activate fs:usb0 -> taken', '22-library-usb0'),
    ('m', '[ui] activate fs:games -> taken', '23-library-games'),
    ('m', '[ui] select fs:x.adf -> taken', '24-select-by-old-id'),
    ('m', '[ui] activate fs:x.adf -> taken', '25-file-sheet'),
    ('m', '-> asked', '26-delete-question-asked-by-intent'),
    ('m', '[ui] answer no -> taken', '27-answered-no-remote'),
    ('m', '[ui] activate fs:x.adf -> taken', '28-file-sheet-again'),
    ('m', '-> asked', '29-delete-question-again'),
    ('m', '[ui] answer yes -> taken', '30-answered-yes-remote-job'),
    ('w', 4000, '31-delete-done'),
    ('m', '[ui] open F5/settings -> taken', '32-settings'),
    ('m', '[ui] set ', '33-settings-after-set'),
    ('m', '[ui] key right -> taken', '33b-picture-question-open'),
    ('k', 'space', '33c-picture-kept-by-a-board-key'),
    ('m', '[ui] answer no -> taken', '33d-picture-reverted-by-answer'),
    ('m', '[ui] op settings.reset_defaults -> asked', '34-rest-question-on-screen'),
    ('x', 'wait 40000', None),                 # python reads the question from the state meanwhile
    ('k', 'larrow', '35-answered-no-on-screen'),
    ('m', '[ui] answer yes -> gone', '36-late-remote-answer-gone'),
    ('m', '[ui] key f7 -> taken', '37-actions-by-websocket'),
    ('c', 'usb-sync --force', '40-synced'),
]
lines = ['expect-console "AMIG Amiga 1200" 120000', 'wait 4000']
for kind, v, name in STEPS:
    if kind == 'm': lines += ['expect-console "%s" 900000' % v, 'wait 2500']   # a page of 18 rows takes the emulated CPU a while to draw
    elif kind == 'k': lines += ['key %s' % v, 'wait 2500']
    elif kind == 'w': lines += ['wait %d' % v]
    elif kind in ('c', 'x'): lines += [v]
    if name: lines += ['png %s/%s.png' % (out, name)]
lines += ['wait 1000', 'quit']
open(os.path.join(work, 'steps.ctl'), 'w').write('\n'.join(lines) + '\n')

cmd = (['taskpolicy', '-c', 'utility'] if sys.platform == 'darwin' else []) + [emu, 'run', '--headless', '--speed', 'max', '--board', 'c64u', '--caps', '25E40202', '--trx-io', 'on',
       '--c64', 'none', '--firmware', app, '--roms', roms, '--flash', os.path.join(work, 'flash.bin'), '--core', os.path.join(HERE, 'profiles', 'amiga1200.toml'),
       '--handover', 'video=' + os.environ.get('UIMVP_VIDEO', '720p'), '--usb-dir', stick, '--usb-dir-work', os.path.join(work, 'usbwork'), '--net', 'user',
       '--hostfwd', 'tcp:127.0.0.1:%d:80' % HTTP, '--script', os.path.join(work, 'steps.ctl'), '--max-seconds', '600']
log = open(os.path.join(out, 'emu.log'), 'w')
p = subprocess.Popen(cmd, stdout=log, stderr=log, cwd=work)
fails = 0
def check(ok, what):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok: fails += 1
    return ok
def shot(name, timeout=120):
    f = os.path.join(out, name + '.png')
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(f) and os.path.getsize(f) > 1000:
            time.sleep(0.2); return True
        if p.poll() is not None: break
        time.sleep(0.1)
    return check(False, 'png %s did not appear' % name)

# ---- REST ------------------------------------------------------------------------------------------------------------------------------------------
tok = None
def rest(method, path, body=None, token=True):
    url = 'http://127.0.0.1:%d%s' % (HTTP, path)
    if token and tok: url += ('&' if '?' in url else '?') + 't=' + tok
    req = urllib.request.Request(url, data=(json.dumps(body).encode() if isinstance(body, dict) else body), method=method)
    if body is not None: req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=30) as r: return r.status, r.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
    except Exception as e: return 0, str(e)
def j(s):
    try: return json.loads(s)
    except Exception: return {}
def state(): return j(rest('GET', '/v1/ui/state')[1])
def intent(**kw):
    st, b = rest('POST', '/v1/ui/intent', kw)
    return st, j(b)
def wait_for(pred, what, timeout=90):
    t0 = time.time()
    while time.time() - t0 < timeout:
        s = state()
        if pred(s): return s
        time.sleep(0.3)
    check(False, 'wait for ' + what); return state()
def title_ids(s): return {it['title']: it['id'] for it in s.get('items', [])}

try:
    ok = False; b = ''; st = 0
    t0 = time.time()
    while time.time() - t0 < 180 and p.poll() is None:
        st, b = rest('GET', '/v1/ui/hello', token=False)
        if st == 200: ok = True; break
        time.sleep(0.5)
    check(ok, 'GET /v1/ui/hello answers over the emulated network (%s %s)' % (st, b[:60]))
    h = j(b); check(h.get('ui') == 2 and h.get('app') == 'core' and h.get('grant') is True, 'hello: contract 2, core app, grant offered')
    # ---- S2 / S14 / S15 ----------------------------------------------------------------------------------------------------------------------------------
    check(rest('GET', '/v1/info', token=False)[0] == 401, 'S15 GET /v1/info without the token: 401')
    check(rest('GET', '/v1/ui/state', token=False)[0] == 401, 'S2 GET /v1/ui/state without the token: 401')
    st, b = rest('POST', '/v1/ui/grant', token=False)
    tok = j(b).get('token'); check(st == 200 and tok and len(tok) == 32, 'S14 the first login hands the token out')
    check(rest('POST', '/v1/ui/grant', token=False)[0] == 403, 'S14 control: a second fresh login does not get it')
    check(rest('GET', '/v1/info')[0] == 200, 'S16 token as URL parameter accepted')
    check(rest('GET', '/v1/info?t=' + tok[:-1] + ('0' if tok[-1] != '0' else '1'), token=False)[0] == 401, 'S16 control: one digit changed is refused')
    s = state(); check(s.get('menu_open') is False and s.get('keys_owner') == 'closed', 'state: the menu is closed, keys_owner closed')
    # ---- S18 ----------------------------------------------------------------------------------------------------------------------------------------------
    codes = [rest('GET', x)[0] for x in ('/v1/ops', '/v1/state', '/v1/events')]
    check(codes == [200, 200, 200], 'S18 /v1/ops, /v1/state, /v1/events answer %s' % codes)
    st, b = rest('GET', '/v1/ui/state'); check(st == 200 and j(b).get('ui') == 2 and 'v' not in j(b) and 'seq' not in j(b), 'S18 the only state is the service\'s (ui 2, no v1 keys)')
    # ---- open the menu over the API (the board's hotkey is the other way, key script) -------------------------------------------------------------------
    st, a = intent(do='open', menu=True); check(a.get('r') == 'taken', 'open menu intent taken')
    shot('20-rest-open-menu')
    check(os.path.getsize(os.path.join(out, '20-rest-open-menu.png')) > 20000, 'the first PNG shows the menu (a black frame is 9 KB: no pixel layer)')
    s = wait_for(lambda s: s.get('menu_open') is True, 'the menu open')
    check(s.get('ui') == 2 and s.get('app') == 'core' and s.get('keys_owner') == 'menu' and isinstance(s.get('rev'), int), 'S19 state v2: ui 2, app, rev, keys_owner')
    ids = [it['id'] for it in s['items']]; check(len(ids) == len(set(ids)) and len(ids) > 3, 'S4 item ids unique (%d items)' % len(ids))
    s2 = state(); check([it['id'] for it in s2['items']] == ids, 'S4 ids equal over two renders of the same state')
    check(s2['rev'] >= s['rev'], 'S10 rev never goes back')
    st, a = intent(do='key', key='f3'); check(st == 200 and a.get('r') == 'taken', 'S7 key intent f3 taken')
    shot('21-library-root'); s = state()
    print('  root:', [(i['title'], i['id']) for i in s['items']])
    t = title_ids(s); usb = next((v for k, v in t.items() if k.lower().startswith('usb0')), None)
    check(usb == 'fs:usb0', 'library root lists usb0 with a stable id (%s)' % usb)
    st, a = intent(do='activate', item='fs:usb0'); check(a.get('r') == 'taken', 'activate usb0')
    shot('22-library-usb0'); s = state(); print('  usb0:', [i['title'] for i in s['items']])
    st, a = intent(do='activate', item='fs:games'); shot('23-library-games'); s = state(); t = title_ids(s)
    check(s.get('path', '').endswith('games'), 'S5 folder opened: ' + s.get('path', ''))
    print('  games:', [i['title'] for i in s['items']])
    fx = t.get('x.adf'); check(fx == 'fs:x.adf', 'x.adf listed with id fs:x.adf')
    # S5 (the listing changing under an id) is a native test: a file added on the host makes the emulator unplug and replug the stick
    st, a = intent(do='select', item=fx); check(a.get('r') == 'taken', 'S5 select by the id after the listing may have changed')
    shot('24-select-by-old-id'); s = state(); check(s.get('focus') == fx, 'S5 focus is the same entry')
    st, a = intent(do='select', item='fs:nonexistent'); check(a.get('r') == 'gone', 'S5 control: an id that is gone answers gone')
    # ---- a job with a question: delete (S6, S13, S15) -----------------------------------------------------------------------------------------------------
    st, a = intent(do='activate', item=fx); shot('25-file-sheet'); s = state()
    dl = title_ids(s).get('Delete'); check(dl is not None, 'the sheet of the file lists Delete (%s)' % dl)
    st, a = intent(do='activate', item=dl); check(a.get('r') == 'asked', 'S6 activate Delete answers asked')
    shot('26-delete-question-asked-by-intent'); s = state()
    d = s.get('dialog') or {}
    check(bool(d.get('q')) and 'Delete' in d.get('text', '') and d.get('destructive') is True and s.get('keys_owner') == 'dialog', 'S6 the question is open (screen and state)')
    check(os.path.exists(os.path.join(stick, 'games', 'x.adf')), 'S6 nothing deleted before the answer')
    st, a2 = intent(do='activate', item='fs:y.adf'); check(a2.get('r') == 'blocked', 'S8/S15 activate beneath an open question is blocked')
    q = d.get('q')
    st, a = intent(do='answer', q=q, choice='no'); check(a.get('r') == 'taken', 'answer no taken'); shot('27-answered-no-remote')
    check(os.path.exists(os.path.join(stick, 'games', 'x.adf')), 'answer no: the file stays')
    st, a = intent(do='answer', q=q, choice='yes'); check(a.get('r') == 'gone', 'S13 control: the late answer is gone')
    st, a = intent(do='activate', item=fx); shot('28-file-sheet-again'); s = state()
    st, a = intent(do='activate', item=title_ids(s).get('Delete')); check(a.get('r') == 'asked', 'asked again'); shot('29-delete-question-again')
    s = state(); q = s['dialog']['q']
    st, a = intent(do='answer', q=q, choice='yes'); check(a.get('r') == 'taken', 'S13 answer yes taken (remote)'); shot('30-answered-yes-remote-job')
    s = wait_for(lambda s: any(jb.get('phase') in ('done', 'failed') for jb in s.get('jobs', [])), 'the delete job to finish')
    shot('31-delete-done')
    check(any(jb.get('phase') == 'done' for jb in s.get('jobs', [])), 'S12 the job finished (phase done): %s' % s.get('jobs'))
    # ---- settings rows: set (S9) ----------------------------------------------------------------------------------------------------------------------
    st, a = intent(do='open', target='F5/settings'); check(a.get('r') == 'taken', 'open F5/settings'); shot('32-settings'); s = state()
    sets = [it for it in s.get('items', []) if it['id'].startswith('set:')]
    check(len(sets) > 0, 'S4 the settings tab lists set: rows (%d on %s)' % (len(sets), s.get('screen')))
    if sets:
        row = next((it for it in sets if it.get('toggle')), sets[0]); old_val = row.get('value', '')
        st, a = intent(do='set', item=row['id'], value=('Yes' if old_val in ('No', 'Off', '') else 'No'))
        check(a.get('r') in ('taken', 'held'), 'S9 set on %s: %s %s' % (row['id'], a.get('r'), a.get('why', '')))
        shot('33-settings-after-set'); s2 = state(); row2 = next((it for it in s2['items'] if it['id'] == row['id']), {})
        check(row2.get('value') != row.get('value') or row2.get('on') != row.get('on'), 'S9 the row shows the new value (%s -> %s)' % (row.get('value'), row2.get('value')))
    # ---- T192.3 D23: the picture question is the service's question (any key keeps the mode; the answer "no" brings the previous one back) ------------------------------
    pic = next((it for it in state().get('items', []) if it.get('title', '').lower().startswith('picture')), None)
    check(pic is not None, 'Q7 the settings tab lists the Picture row')
    if pic:
        st, a = intent(do='select', item=pic['id']); check(a.get('r') == 'taken', 'Q7 select the Picture row')
        before = next((it for it in state()['items'] if it['id'] == pic['id']), {}).get('value')
        st, a = intent(do='key', key='right'); check(a.get('r') == 'taken', 'Q7 key right changes the Picture')
        s = state(); d = s.get('dialog') or {}                                     # read at once: the question has 8 s of emulated time and the emulator runs on while python waits for the PNG
        check(s.get('keys_owner') == 'picture' and 'Keep this picture' in d.get('text', '') and d.get('default') == 'no' and 0 < d.get('timeout_ms', 0) <= 8000,
              'Q7 the picture question is up with its default and its timeout: %s %s' % (s.get('keys_owner'), d))
        st, a = intent(do='activate', item=pic['id']); check(a.get('r') == 'blocked', 'Q7 nothing beneath the picture question can be activated')
        shot('33b-picture-question-open'); shot('33c-picture-kept-by-a-board-key'); s = state()   # the script pressed SPACE on the board in between
        check(s.get('dialog') is None and s.get('keys_owner') == 'menu', 'Q7 a board key kept the picture (the answer yes of the service)')
        kept = next((it for it in s['items'] if it['id'] == pic['id']), {}).get('value')
        check(kept != before, 'Q7 the new Picture stays (%s -> %s)' % (before, kept))
        st, a = intent(do='key', key='right'); check(a.get('r') == 'taken', 'Q7 key right again')
        st, a = intent(do='answer', choice='no'); check(a.get('r') == 'taken', 'Q7 answer no taken (remote, no id: the open question; the 8 s run in emulated time): %s' % a)
        shot('33d-picture-reverted-by-answer'); s = state()
        check(s.get('dialog') is None and s.get('keys_owner') == 'menu', 'Q7 the question is gone and the menu has the keys again')
        check(next((it for it in s['items'] if it['id'] == pic['id']), {}).get('value') == kept, 'Q7 the previous Picture is back (%s)' % kept)
    # ---- REST asks (D18, D19a) --------------------------------------------------------------------------------------------------------------------------
    st, b = rest('POST', '/v1/ops/settings.reset_defaults', {})
    check(st == 202 and j(b).get('r') == 'asked', 'D19a POST /v1/ops/settings.reset_defaults answers asked (202)')
    s = state(); q = (s.get('dialog') or {}).get('q'); check(q is not None, 'the REST question is in the state')
    shot('34-rest-question-on-screen')
    shot('35-answered-no-on-screen')                                   # the script presses the left arrow: <- on the screen answers no
    st, a = intent(do='answer', q=q, choice='yes'); check(a.get('r') == 'gone', 'S13 the screen answered first: the remote answer is gone')
    shot('36-late-remote-answer-gone')
    # ---- WebSocket ----------------------------------------------------------------------------------------------------------------------------------------
    def ws_connect(path):
        sk = socket.create_connection(('127.0.0.1', HTTP), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        sk.sendall(('GET %s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % (path, key)).encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            c = sk.recv(4096)
            if not c: break
            buf += c
        return sk, buf
    def ws_send(sk, text):
        pl = text.encode(); m = os.urandom(4)
        sk.sendall(bytes([0x81, 0x80 | len(pl)]) + m + bytes(b ^ m[i % 4] for i, b in enumerate(pl)))
    def ws_recv(sk):
        def rd(n):
            d = b''
            while len(d) < n:
                c = sk.recv(n - len(d))
                if not c: raise EOFError
                d += c
            return d
        h = rd(2); ln = h[1] & 127
        if ln == 126: ln = struct.unpack('>H', rd(2))[0]
        return rd(ln).decode(errors='replace')
    sk, hs = ws_connect('/v1/ui/ws')
    check(b' 401 ' in hs.split(b'\r\n')[0], 'S16 control: a WebSocket upgrade without the token gets 401')
    sk, hs = ws_connect('/v1/ui/ws?t=' + tok)
    check(b' 101 ' in hs.split(b'\r\n')[0], 'S16 upgrade with the token: 101')
    m1 = j(ws_recv(sk)); m2 = j(ws_recv(sk))
    check(m1.get('t') == 'hello' and m1.get('ui') == 2, 'S16 first message is hello')
    check(m2.get('t') == 'snap' and m2.get('rev', 0) >= m1.get('rev', 0) and 'items' in m2, 'S10 then the snapshot, at least as new as hello')
    ws_send(sk, json.dumps({'do': 'key', 'key': 'f7', 'id': 'k1'}))
    got_ack, got_snap, last = False, False, m2['rev']
    sk.settimeout(30)
    for _ in range(14):
        m = j(ws_recv(sk))
        if m.get('t') == 'ack' and m.get('id') == 'k1' and m.get('r') == 'taken': got_ack = True
        if m.get('t') == 'snap' and m.get('rev', 0) > last and m.get('screen', '').startswith('F7'): got_snap = True; last = m['rev']
        if got_ack and got_snap: break
    check(got_ack, 'S16 WS key intent acknowledged'); check(got_snap, 'S10 a newer snapshot (rev greater) shows the F7 screen')
    shot('37-actions-by-websocket'); sk.close()
    # ---- the files on the stick, then S17 ---------------------------------------------------------------------------------------------------------------
    shot('40-synced')
    check(not os.path.exists(os.path.join(stick, 'games', 'x.adf')), 'the deleted file is gone on the stick after the answer')
    check(os.path.exists(os.path.join(stick, 'games', 'y.adf')), 'the other file stays')
    tf = [os.path.join(dp, f) for dp, _, fs in os.walk(stick) for f in fs if f == 'ui.token']
    check(len(tf) == 1 and open(tf[0]).read().strip() == tok, 'S17 the token is a plain file on the medium: %s' % tf)
    # S17's deletion half (the file removed: the next login gets a new token, the old one is dead) is native: test_uiapp and test_uicore; a host-side delete here
    # is undone by the emulator's usb-dir sync, a guest-side one is a library walk that this MVP run does not repeat.
finally:
    try: p.wait(timeout=60)
    except Exception: p.kill()
print('uimvp: %s' % ('FAILED (%d)' % fails if fails else 'all checks passed'))
sys.exit(1 if fails else 0)
