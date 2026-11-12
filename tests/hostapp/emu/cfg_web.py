#!/usr/bin/env python3
"""T198 S13 in ue2emu: the host's REST status lists `configs: n` per entry and POST /v1/boot with config= starts the core with that config.
One local emulator (control port + a host port forward, like parity_walk.py): the VIC-20 bundle with 3 configs is added to the boot menu by keys,
then GET /v1/status must show configs 3 and POST /v1/boot index=0&config=Bravo must make the core application print "[core] config Bravo: <path>";

  cfg_web.py --img DIR [--kit DIR] [--emu PATH]     DIR holds hostapp_emu.elf, coreapp.elf, coreapp.app
Exit 0 PASS, 1 FAIL, 77 = no local emulator / kit / images (skipped out loud; the container route of tools/dev.sh has no network)."""
import argparse, json, os, shutil, socket, subprocess, sys, tempfile, time, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def skip(msg):
    print('cfg_web SKIPPED out loud: ' + msg, file=sys.stderr)
    sys.exit(77)


class Emu:
    def __init__(self, img, kit, emu, work):
        shutil.rmtree(work, ignore_errors=True)
        for d in ('cors', 'profiles', 'stick/cores/vc20/roms', 'stick/cores/vc20/configs'):
            os.makedirs(os.path.join(work, d))
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'cor_pack.py'), 'replace-app', os.path.join(kit, 'cors', 'vc20.cor'),
                               '--app', os.path.join(img, 'coreapp.app'), '-o', os.path.join(work, 'cors', 'vc20.cor')], stdout=subprocess.DEVNULL)
        shutil.copy(os.path.join(work, 'cors', 'vc20.cor'), os.path.join(work, 'stick', 'cores'))
        shutil.copy(os.path.join(kit, 'profiles', 'VIC-20.toml'), os.path.join(work, 'profiles'))
        shutil.copy(os.path.join(img, 'coreapp.elf'), os.path.join(work, 'profiles'))
        for n, sz in (('basic', 8192), ('kernal', 8192), ('char', 4096)):
            open(os.path.join(work, 'stick/cores/vc20/roms', n + '.bin'), 'wb').write(b'\0' * sz)
        for n in ('Alpha', 'Bravo', 'Charlie'):
            open(os.path.join(work, 'stick/cores/vc20/configs', n + '.cfg'), 'w').write('# T198 emulator flow\n')
        shutil.copy(os.path.join(kit, 'flash.bin'), os.path.join(work, 'flash.bin'))
        s = socket.socket(); s.bind(('127.0.0.1', 0)); self.ctlport = s.getsockname()[1]; s.close()
        s = socket.socket(); s.bind(('127.0.0.1', 0)); self.http = s.getsockname()[1]; s.close()
        self.logpath = os.path.join(work, 'emu.log')
        self.log = open(self.logpath, 'w')
        self.p = subprocess.Popen(['nice', '-n', '15', emu, 'run', '--board', 'c64u', '--trx-io=on', '--icap', '--cor-dir', os.path.join(work, 'cors'),
                                   '--core-dir', os.path.join(work, 'profiles'), '--firmware', os.path.join(img, 'hostapp_emu.elf'), '--roms', os.path.join(kit, 'roms'),
                                   '--flash', os.path.join(work, 'flash.bin'), '--usb-dir', os.path.join(work, 'stick'), '--usb-dir-work', os.path.join(work, 'usbwork'),
                                   '--headless', '--speed', 'realtime', '--net', 'user', '--hostfwd', 'tcp:127.0.0.1:%d:80' % self.http,
                                   '--control', '127.0.0.1:%d' % self.ctlport, '--max-seconds', '400'], stdout=self.log, stderr=self.log, cwd=work)
        self.sock = None
        for _ in range(300):
            try:
                self.sock = socket.create_connection(('127.0.0.1', self.ctlport)); break
            except OSError:
                time.sleep(0.1)
        if not self.sock:
            raise RuntimeError('no control port')
        self.rf = self.sock.makefile('r')
        self.tok = None

    def ctl(self, line):
        self.sock.sendall((line + '\n').encode())
        while True:
            l = self.rf.readline()
            if not l:
                return 'error closed'
            l = l.rstrip('\n')
            if l.startswith('ok') or l.startswith('error'):
                return l

    def key(self, k, wait=500):
        self.ctl('key ' + k)
        self.ctl('wait %d' % wait)

    def rest(self, method, path, token=True, body=None):
        url = 'http://127.0.0.1:%d%s' % (self.http, path)
        if token and self.tok:
            url += ('&' if '?' in url else '?') + 't=' + self.tok
        try:
            with urllib.request.urlopen(urllib.request.Request(url, method=method, data=(body if body is not None else b'' if method == 'POST' else None)), timeout=20) as r:
                return r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode()
        except Exception as e:
            return 0, str(e)

    def grant(self):
        r = self.ctl('expect-console "[web] http" 180000')
        if not r.startswith('ok'):
            raise RuntimeError('the host application did not come up: ' + r)
        for _ in range(120):
            st, _b = self.rest('GET', '/v1/ui/hello', token=False)
            if st == 200:
                break
            self.ctl('wait 500')
        st, b = self.rest('POST', '/v1/ui/grant', token=False)
        self.tok = json.loads(b).get('token') if st == 200 else None
        if not self.tok:
            raise RuntimeError('no token (%s %s)' % (st, b[:80]))

    def console(self):
        self.log.flush()
        return open(self.logpath, errors='replace').read()

    def close(self):
        try:
            self.ctl('quit')
        except Exception:
            pass
        try:
            self.p.wait(timeout=20)
        except Exception:
            self.p.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--img', required=True)
    ap.add_argument('--kit', default=os.path.expanduser('~/scratch/ue2emu-kit'))
    ap.add_argument('--emu', default=os.environ.get('UE2EMU_BIN') or os.path.expanduser('~/Development/u64-emulator/target/release/ue2emu'))
    a = ap.parse_args()
    if not os.access(a.emu, os.X_OK):
        skip('no local emulator (%s)' % a.emu)
    for f in ('hostapp_emu.elf', 'coreapp.elf', 'coreapp.app'):
        if not os.path.exists(os.path.join(a.img, f)):
            skip('no %s in %s' % (f, a.img))
    if not os.path.exists(os.path.join(a.kit, 'cors', 'vc20.cor')):
        skip('no kit ' + a.kit)
    work = tempfile.mkdtemp(prefix='cfgweb-')
    e = Emu(a.img, a.kit, a.emu, work)
    fails = []
    try:
        e.grant()
        e.ctl('wait 6000'); e.key('space', 800)
        # the browser: F3, USB0, cores, vc20.cor (the list: .., vc20/, vc20.cor), A adds it (the bundle check takes its time)
        e.key('f3', 1500); e.key('down'); e.key('down'); e.key('return', 2000); e.key('down'); e.key('return', 2000)
        e.key('down'); e.key('down'); e.key('a', 15000); e.key('f1', 1500); e.key('up', 500)   # any key stops the countdown (5 s of real time at 1 MIPS)
        st, b = e.rest('GET', '/v1/status')
        ent = [x for x in (json.loads(b).get('menu') or [])] if st == 200 else []
        if st == 200 and ent and ent[0].get('configs') == 3:
            print('PASS S13 GET /v1/status: entry %s has configs: 3' % ent[0].get('path'))
        else:
            fails.append('GET /v1/status: %s %s' % (st, b[:200]))
        st, b = e.rest('POST', '/v1/boot', body=b'index=0&config=Bravo')
        e.ctl('wait 30000')
        con = e.console()
        if st == 200 and 'accepted' in b and '[core] config Bravo: /USB0/cores/vc20/configs/Bravo.cfg' in con:
            print('PASS S13 POST /v1/boot config=Bravo: the core started with the config (%s)' % b.strip()[:60])
        else:
            fails.append('POST /v1/boot config=Bravo: %s %s, no "[core] config Bravo:" line' % (st, b[:120]))
    except Exception as ex:
        fails.append('exception: %s' % ex)
    finally:
        e.close()
        if fails:
            print('log tail:\n' + '\n'.join(open(e.logpath, errors='replace').read().splitlines()[-15:]), file=sys.stderr)
        if os.environ.get('KEEP'):
            print('work kept: ' + work, file=sys.stderr)
        else:
            shutil.rmtree(work, ignore_errors=True)
    for f in fails:
        print('FAIL ' + f)
    sys.exit(1 if fails else 0)


main()
