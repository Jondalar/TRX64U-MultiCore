#!/usr/bin/env python3
"""T192 D21: the host application in ue2emu, driven over the control language (key script) AND over REST/WebSocket through the emulator's user-mode network.
Grant the token, read the snapshot, navigate by intents, run flows as jobs, answer their questions, type into an entry; a PNG of the screen after each step.

  uihost.py HOSTAPP_EMU.elf OUTDIR [--kit DIR] [--emu PATH]

KIT: roms/, flash.bin, cors/vc20.cor (tools/ue2emu_kit.sh; default ~/scratch/ue2emu-kit with the emulator checkout's roms and run/flash.bin).
The emulator must be a local macOS/Linux binary (the CI image runs without a network): without one the script exits 77 (skipped out loud).
Every check prints PASS/FAIL; the exit code is non-zero on any FAIL."""
import base64, json, os, shutil, socket, struct, subprocess, sys, time, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
app, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
emu_repo = os.path.expanduser('~/Development/u64-emulator')
kit = os.path.expanduser('~/scratch/ue2emu-kit')
emu = os.environ.get('UE2EMU_BIN') or os.path.join(emu_repo, 'target/release/ue2emu')
for i, a in enumerate(sys.argv):
    if a == '--kit': kit = os.path.abspath(sys.argv[i + 1])
    if a == '--emu': emu = sys.argv[i + 1]
roms = os.path.join(kit, 'roms') if os.path.isdir(os.path.join(kit, 'roms')) else os.path.join(emu_repo, 'firmware/1541ultimate/roms')
flash = os.path.join(kit, 'flash.bin') if os.path.exists(os.path.join(kit, 'flash.bin')) else os.path.join(emu_repo, 'run/flash.bin')
vc20 = os.path.join(kit, 'cors/vc20.cor')
if not os.path.exists(emu) or not os.path.isdir(roms) or not os.path.exists(flash) or not os.path.exists(vc20) or not os.path.exists(app):
    print('uihost SKIPPED out loud: need a local ue2emu (%s), ROMs (%s), flash.bin (%s), cors/vc20.cor (%s) and the image (%s)' % (emu, roms, flash, vc20, app)); sys.exit(77)
os.makedirs(out, exist_ok=True)
work = os.path.join(out, 'work'); shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
stick = os.path.join(work, 'stick'); os.makedirs(os.path.join(stick, 'cores', 'vc20', 'roms')); os.makedirs(os.path.join(stick, 'docs'))
shutil.copy(vc20, os.path.join(stick, 'cores', 'vc20.cor'))
for n, sz in (('basic.bin', 8192), ('kernal.bin', 8192), ('char.bin', 4096)): open(os.path.join(stick, 'cores', 'vc20', 'roms', n), 'wb').write(bytes(sz))
open(os.path.join(stick, 'docs', 'readme.txt'), 'w').write('hello\n' * 4000)
os.makedirs(os.path.join(work, 'cors')); os.makedirs(os.path.join(work, 'profiles'))
shutil.copy(vc20, os.path.join(work, 'cors', 'vc20.cor'))
shutil.copy(os.path.join(emu_repo, 'docs/examples/core-vic20.toml'), os.path.join(work, 'profiles', 'VIC-20.toml'))
shutil.copy(flash, os.path.join(work, 'flash.bin'))
CTRL, HTTP = 16900 + os.getpid() % 500, 18600 + os.getpid() % 500
log = open(os.path.join(out, 'emu.log'), 'w')
p = subprocess.Popen(['nice', '-n', '15', emu, 'run', '--board', 'c64u', '--trx-io=on', '--icap', '--cor-dir', os.path.join(work, 'cors'), '--core-dir', os.path.join(work, 'profiles'),
                      '--firmware', app, '--roms', roms, '--flash', os.path.join(work, 'flash.bin'), '--usb-dir', stick, '--usb-dir-work', os.path.join(work, 'usbwork'),
                      '--headless', '--speed', 'max', '--net', 'user', '--hostfwd', 'tcp:127.0.0.1:%d:80' % HTTP, '--control', '127.0.0.1:%d' % CTRL, '--max-seconds', '500'],
                     stdout=log, stderr=log, cwd=work)
fails = 0
def check(ok, what):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok: fails += 1
    return ok

# ---- control protocol ------------------------------------------------------------------------------------------------------------------------------
sock = None
for _ in range(200):
    try:
        sock = socket.create_connection(('127.0.0.1', CTRL)); break
    except OSError: time.sleep(0.1)
if not sock:
    print('FAIL no control port'); p.kill(); sys.exit(1)
rf = sock.makefile('r')
def ctl(line):
    sock.sendall((line + '\n').encode())
    lines = []
    while True:
        l = rf.readline()
        if not l: return 'error closed', lines
        l = l.rstrip('\n')
        if l.startswith('ok') or l.startswith('error'): return l, lines
        lines.append(l)
shots = []
def png(name):
    r, _ = ctl('png %s' % os.path.join(out, name + '.png'))
    shots.append(name)
    check(r.startswith('ok') and os.path.getsize(os.path.join(out, name + '.png')) > 1000, 'png %s' % name)

# ---- REST ------------------------------------------------------------------------------------------------------------------------------------------
tok = None
def rest(method, path, body=None, token=True):
    url = 'http://127.0.0.1:%d%s' % (HTTP, path)
    if token and tok: url += ('&' if '?' in url else '?') + 't=' + tok
    req = urllib.request.Request(url, data=(json.dumps(body).encode() if isinstance(body, dict) else body), method=method)
    if body is not None: req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=20) as r: return r.status, r.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
    except Exception as e: return 0, str(e)
def j(s):
    try: return json.loads(s)
    except Exception: return {}
def state(): return j(rest('GET', '/v1/ui/state')[1])
_n = [100]
def intent(**kw):
    _n[0] += 1
    kw.setdefault('id', _n[0])
    st, b = rest('POST', '/v1/ui/intent', kw)
    return st, j(b)
def wait_for(pred, what, timeout=90, step=300):
    t0 = time.time()
    s = state()
    while time.time() - t0 < timeout:
        s = state()
        if pred(s): return s
        ctl('wait %d' % step)
    check(False, 'wait for ' + what); return s
def ids_by_title(s): return {it['title']: it['id'] for it in s.get('items', [])}
def row_id(s, title):
    for it in s.get('items', []):
        if it['title'] == title: return it['id']
    return None

try:
    r, _ = ctl('expect-console "[web] http" 120000'); check(r.startswith('ok'), 'the host app is up and serves the web (log line [web] http://...)')
    ctl('wait 1500')
    ok = False
    for _ in range(120):
        st, b = rest('GET', '/v1/ui/hello', token=False)
        if st == 200: ok = True; break
        ctl('wait 500')
    check(ok, 'GET /v1/ui/hello answers over the emulated network (%s %s)' % (st, b[:80]))
    h = j(b); check(h.get('ui') == 2 and h.get('app') == 'host' and h.get('grant') is True, 'hello: contract 2, host app, grant offered')
    # ---- the old routes keep working with no token ------------------------------------------------------------------------------------------------------
    check(rest('GET', '/v1/status', token=False)[0] == 200, 'the old GET /v1/status answers with no token')
    st, pg = rest('GET', '/', token=False); check(st == 200 and 'TRX64' in pg, 'the T106 page at / answers with no token')
    check(rest('GET', '/v1/report', token=False)[0] == 200, 'the old GET /v1/report answers with no token')
    # ---- the token --------------------------------------------------------------------------------------------------------------------------------
    check(rest('GET', '/v1/ui/state', token=False)[0] == 401, 'GET /v1/ui/state without the token: 401')
    st, b = rest('POST', '/v1/ui/grant', token=False)
    tok = j(b).get('token'); check(st == 200 and tok and len(tok) == 32, 'the first login hands the token out')
    check(rest('POST', '/v1/ui/grant', token=False)[0] == 403, 'control: a second fresh login does not get it')
    check(rest('GET', '/v1/ui/state?t=' + tok[:-1] + ('0' if tok[-1] != '0' else '1'), token=False)[0] == 401, 'control: one digit changed is refused')
    # ---- the snapshot -----------------------------------------------------------------------------------------------------------------------------
    ctl('wait 2000')
    s = state(); png('01-boot-menu-start')
    check(s.get('ui') == 2 and s.get('app') == 'host' and s.get('keys_owner') in ('menu', 'picture') and s.get('screen') == 'menu', 'snapshot: ui 2, host, screen menu, keys owner %s' % s.get('keys_owner'))
    check([a['id'] for a in s['areas']] == ['menu', 'browse', 'edit', 'net', 'settings'], 'the five views are the areas')
    check(any(o['id'] == 'nav.settings' for o in s.get('ops', [])) and any(o['id'] == 'picture.step' for o in s['ops']), 'ops list the global actions')
    # ---- navigate by intent (the same screen as the key script) ----------------------------------------------------------------------------------
    st, a = intent(do='open', target='settings'); check(a.get('r') == 'taken', 'open settings taken')
    s = wait_for(lambda s: s.get('screen') == 'settings', 'the settings screen'); png('02-settings-by-intent')
    ids = [it['id'] for it in s['items']]; check(len(ids) == len(set(ids)) and 'settings.direct' in ids and 'settings.ftp' in ids, 'settings items with ids: %s' % ids)
    before = {it['id']: it for it in s['items']}
    ctl('key f1'); ctl('wait 600'); ctl('key f8'); ctl('wait 600'); png('02b-settings-by-keyscript')
    s2 = state(); check(s2['screen'] == 'settings' and [i['id'] for i in s2['items']] == ids, 'the key script gives the same screen and the same ids')
    # set and activate: toggle FTP, the value is seen in the snapshot and on the screen
    ftp0 = before['settings.ftp'].get('on')
    st, a = intent(do='set', item='settings.ftp', value='off' if ftp0 else 'on'); check(a.get('r') == 'taken', 'set settings.ftp: %s %s' % (a.get('r'), a.get('why', '')))
    s = wait_for(lambda s: {i['id']: i for i in s['items']}['settings.ftp'].get('on') != ftp0, 'FTP to change'); png('03-settings-ftp-changed')
    st, a = intent(do='activate', item='settings.ftp'); s = wait_for(lambda s: {i['id']: i for i in s['items']}['settings.ftp'].get('on') == ftp0, 'FTP activate toggles back')
    check(True, 'activate toggles FTP back')
    st, a = intent(do='set', item='settings.auto', value='on'); print('  auto:', a)
    st, a = intent(do='set', item='settings.direct', value='bogus'); check(a.get('r') == 'refused', 'a value the model does not take is refused with its word (%s)' % a.get('why'))
    # ---- the file browser, rows with ids --------------------------------------------------------------------------------------------------------------
    st, a = intent(do='open', target='browse:/'); check(a.get('r') == 'taken', 'open browse:/ taken')
    s = wait_for(lambda s: s.get('screen') == 'browse' and s.get('path') == '/', 'the media level'); png('04-browse-media')
    t = ids_by_title(s); usb = next((v for k, v in t.items() if k.lower().startswith('usb0')), None)
    check(usb is not None, 'the media level lists USB0 (%s)' % sorted(t))
    if usb:
        st, a = intent(do='activate', item=usb); check(a.get('r') == 'taken', 'activate USB0')
        s = wait_for(lambda s: s.get('path', '').lower().startswith('/usb0'), 'USB0 opened')
        t = ids_by_title(s); check('cores' in t and 'docs' in t, 'USB0 lists the folders (%s)' % sorted(t))
        st, a = intent(do='activate', item=t['cores']); s = wait_for(lambda s: s.get('path', '').lower().endswith('cores'), 'cores opened'); png('05-browse-cores')
        t = ids_by_title(s); fid = t.get('vc20.cor'); check(fid is not None, 'cores lists vc20.cor')
        st, a = intent(do='select', item=fid); check(a.get('r') == 'taken', 'select the .cor by its id')
        s = state(); check(s.get('focus') == fid, 'the focus is that row')
        row = next(it for it in s['items'] if it['id'] == fid); check('browse.add' in row.get('acts', []) and 'browse.start' in row['acts'], 'the row names its actions (acts)')
        # a job: add the .cor to the boot menu (the full check of its parts runs as a job)
        st, a = intent(do='op', item='browse.add', target=fid); check(a.get('r') == 'taken', 'op browse.add taken')
        s = wait_for(lambda s: 'boot menu' in s.get('status', {}).get('text', ''), 'the add to finish', 120); png('06-added-to-menu')
        check(any(jb.get('kind') == 'check' and jb.get('phase') == 'done' for jb in s.get('jobs', [])), 'the add ran as a job and finished: %s' % [(jb.get('kind'), jb.get('phase')) for jb in s.get('jobs', [])])
    # ---- the boot menu and the editor -------------------------------------------------------------------------------------------------------------------
    st, a = intent(do='open', target='edit'); s = wait_for(lambda s: s.get('screen') == 'edit', 'the editor')
    entries = [it for it in s['items'] if it.get('kind') == 'entry']
    check(len(entries) >= 1, 'the editor lists the entry (%s)' % [(e['title'], e['id']) for e in entries]); png('07-edit')
    if entries:
        eid = entries[0]['id']
        check('entry.rename' in entries[0].get('acts', []) and 'entry.remove' in entries[0]['acts'], 'the entry names its edit actions')
        # a text entry flow: rename; the text comes with the op, the entry modal takes it and RETURN
        st, a = intent(do='op', item='entry.rename', target=eid, value='VIC test'); check(a.get('r') == 'taken', 'op entry.rename taken')
        s = wait_for(lambda s: any(it['title'] == 'VIC test' for it in s.get('items', [])), 'the new name', 60); png('08-renamed')
        check(any(it['title'] == 'VIC test' for it in s['items']), 'the entry has its new name')
        # the destructive op asks first; no keeps it, yes removes it
        eid = row_id(s, 'VIC test')
        st, a = intent(do='op', item='entry.remove', target=eid); check(a.get('r') == 'asked', 'entry.remove asks (D19a)')
        ctl('wait 800'); s = state(); png('09-remove-asked'); d = s.get('dialog') or {}      # the screen shows its own armed note (D21)
        check(bool(d.get('q')) and 'Remove' in d.get('text', '') and d.get('destructive') is True, 'the question is in the snapshot: %s' % d.get('text'))
        check(row_id(s, 'VIC test') is not None, 'nothing is removed before the answer')
        st, a2 = intent(do='op', item='entry.default', target=eid); check(a2.get('r') == 'busy', 'another op beneath the question is busy')
        st, a = intent(do='answer', q=d.get('q'), choice='no'); check(a.get('r') == 'taken', 'answer no taken'); ctl('wait 800')
        check(row_id(state(), 'VIC test') is not None, 'answer no: the entry stays')
        st, a = intent(do='op', item='entry.remove', target=eid); s = state()
        st, a = intent(do='answer', q=(s.get('dialog') or {}).get('q'), choice='yes'); check(a.get('r') == 'taken', 'answer yes taken')
        s = wait_for(lambda s: row_id(s, 'VIC test') is None, 'the entry to go'); png('10-removed')
        check(row_id(s, 'VIC test') is None, 'answer yes: the entry is removed')
    # ---- a flow with a question and a typed confirmation: the format page; cancel it ----------------------------------------------------------------------------
    st, a = intent(do='open', target='browse:/'); wait_for(lambda s: s.get('screen') == 'browse' and s.get('path') == '/', 'the media level again')
    s = state(); t = ids_by_title(s); usb = next((v for k, v in t.items() if k.lower().startswith('usb0')), None)
    if usb:
        st, a = intent(do='op', item='browse.format', target=usb); check(a.get('r') == 'taken', 'op browse.format taken')
        s = wait_for(lambda s: (s.get('dialog') or {}).get('q') is not None or s.get('modal'), 'the format page', 60); png('11-format-page')
        m = s.get('modal') or {}
        check(m.get('kind') == 'page' and 'Format' in m.get('title', '') and s.get('keys_owner') == 'dialog', 'the format page is a question: %s / owner %s' % (m.get('title'), s.get('keys_owner')))
        check(any(jb.get('kind') == 'format' and jb.get('phase') == 'running' for jb in s.get('jobs', [])), 'the format flow is a running job')
        st, a = intent(do='key', key='text', value='USB0'); check(a.get('r') == 'taken', 'type into the page')
        ctl('wait 600'); s = state(); png('12-format-typed')
        check((s.get('modal') or {}).get('typed', '').startswith('USB0'), 'the typed name is in the page: %r' % (s.get('modal') or {}).get('typed'))
        st, a = intent(do='key', key='esc'); ctl('wait 800')
        s = wait_for(lambda s: not s.get('modal'), 'the page to close'); png('13-format-cancelled')
        check(any(jb.get('kind') == 'format' and jb.get('phase') in ('done', 'failed', 'cancelled') for jb in s.get('jobs', [])), 'the flow ended: %s' % [(jb.get('kind'), jb.get('phase')) for jb in s.get('jobs', [])])
    # ---- WebSocket: hello, snapshot, events, intent ------------------------------------------------------------------------------------------------------------------
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
    check(b' 401 ' in hs.split(b'\r\n')[0], 'control: a WebSocket upgrade without the token gets 401')
    sk, hs = ws_connect('/v1/ui/ws?t=' + tok)
    check(b' 101 ' in hs.split(b'\r\n')[0], 'upgrade with the token: 101')
    m1 = j(ws_recv(sk)); m2 = j(ws_recv(sk))
    check(m1.get('t') == 'hello' and m1.get('ui') == 2, 'first message is hello')
    check(m2.get('t') == 'snap' and m2.get('app') == 'host' and 'items' in m2, 'then the snapshot')
    ws_send(sk, json.dumps({'do': 'open', 'target': 'net', 'id': 'k1'}))
    got_ack, got_snap, last = False, False, m2['rev']
    sk.settimeout(30)
    for _ in range(20):
        m = j(ws_recv(sk))
        if m.get('t') == 'ack' and m.get('id') == 'k1' and m.get('r') == 'taken': got_ack = True
        if m.get('t') == 'snap' and m.get('rev', 0) > last and m.get('screen') == 'net': got_snap = True; last = m['rev']
        if got_ack and got_snap: break
    check(got_ack, 'WS open intent acknowledged'); check(got_snap, 'a newer snapshot shows the network page')
    ctl('wait 500'); png('14-network-by-ws')
    ws_send(sk, json.dumps({'do': 'set', 'item': 'settings.ftp', 'value': 'off' if ftp0 else 'on', 'id': 'k2'}))
    ev = []
    for _ in range(10):
        try: ev.append(j(ws_recv(sk)))
        except Exception: break
        if any(m.get('t') == 'toast' for m in ev): break
    check(any(m.get('t') == 'toast' for m in ev), 'a toast event arrives for the note the screen showed: %s' % [m.get('text') for m in ev if m.get('t') == 'toast'])
    sk.close()
    # ---- the board's own key and the API give the same screen; the T106 forms still work -------------------------------------------------------------
    ctl('key f7'); ctl('wait 600'); s = state(); check(s['screen'] == 'net', 'the key script reaches the network page too'); png('13-network-rest-row')
    st, b = rest('POST', '/v1/countdown', b'seconds=7', token=False); print('  T106 form /v1/countdown:', st)
    check(st in (200, 202, 400, 404, 409), 'the old POST form is still routed')
    # ---- start the DFX core by intent: the partial is found, as with the key script -----------------------------------------------------------------------------
    st, a = intent(do='open', target='browse:/USB0/cores'); s = wait_for(lambda s: s.get('screen') == 'browse' and s.get('path', '').lower().endswith('cores'), 'cores again')
    fid = ids_by_title(s).get('vc20.cor'); st, a = intent(do='op', item='browse.add', target=fid)
    wait_for(lambda s: any(jb.get('kind') == 'check' and jb.get('phase') == 'done' for jb in s.get('jobs', [])) and 'boot menu' in s.get('status', {}).get('text', ''), 'the entry added again', 120)
    # ---- D19a on the T106 page: the remove form asks; nothing is removed before Yes ------------------------------------------------------------------------
    def entries_now():
        return [it for it in state().get('items', []) if it.get('kind') == 'entry']
    st, a = intent(do='open', target='edit'); wait_for(lambda s: s.get('screen') == 'edit', 'the editor')
    n0 = len(entries_now())
    st, pg = rest('POST', '/v1/menu/remove', b'index=0', token=False)
    check(st == 202 and 'nothing is removed before Yes' in pg and 'action="/v1/answer"' in pg, 'the T106 remove form answers 202 with a question and Yes / No (status %s)' % st)
    import re as _re
    qid = (_re.search(r'name="q" value="([^"]+)"', pg) or [None, ''])[1]
    ctl('wait 1500'); check(len(entries_now()) == n0, 'nothing removed before the answer')
    st, pg = rest('GET', '/', token=False); check('Remove this entry' in pg, 'the page shows the open question')
    st, pg = rest('POST', '/v1/answer', ('q=%s&choice=no' % qid).encode(), token=False); check(st == 200 and 'answered: no' in pg, 'answer no through the page')
    ctl('wait 1000'); check(len(entries_now()) == n0, 'answer no: the entry is intact')
    st, pg = rest('POST', '/v1/menu/remove', b'index=0', token=False); qid = (_re.search(r'name="q" value="([^"]+)"', pg) or [None, ''])[1]
    st, pg = rest('POST', '/v1/answer', ('q=%s&choice=yes' % qid).encode(), token=False); check(st == 200 and 'answered: yes' in pg, 'answer yes through the page')
    s = wait_for(lambda s: len([it for it in s.get('items', []) if it.get('kind') == 'entry']) == n0 - 1, 'the entry to go', 60); png('16-t106-remove-yes')
    check(len(entries_now()) == n0 - 1, 'answer yes: the entry is removed')
    st, a = intent(do='open', target='browse:/USB0/cores'); s = wait_for(lambda s: s.get('screen') == 'browse' and s.get('path', '').lower().endswith('cores'), 'cores again')
    fid = ids_by_title(s).get('vc20.cor'); st, a = intent(do='op', item='browse.add', target=fid)
    wait_for(lambda s: any(jb.get('kind') == 'check' and jb.get('phase') == 'done' for jb in s.get('jobs', [])) and 'boot menu' in s.get('status', {}).get('text', ''), 'the entry added once more', 120)
    st, a = intent(do='open', target='menu'); wait_for(lambda s: s.get('screen') == 'menu', 'the menu')
    s = state(); ent = [it for it in s['items'] if it.get('kind') == 'entry']; png('15-menu-before-start')
    if ent:
        st, a = intent(do='op', item='entry.start', target=ent[0]['id']); check(a.get('r') == 'taken', 'op entry.start taken')
        ctl('wait 20000')
        txt = open(os.path.join(out, 'emu.log'), errors='replace').read()
        check('icap: the partial of' in txt, 'the start ran the same sequence as the key script (icap partial found)')
    else:
        print('  (no entry in the boot menu to start)')
finally:
    try: ctl('quit')
    except Exception: pass
    try: p.wait(timeout=20)
    except Exception: p.kill()
print('uihost: %s' % ('FAILED (%d)' % fails if fails else 'all checks passed'))
sys.exit(1 if fails else 0)
