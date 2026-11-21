#!/usr/bin/env python3
"""Runs the SuperCPU board suite (T241.1) on the board over REST and prints each program's last screen.

    BOARD=<address> [TRX_TOKEN=<token>] tests/c64scpu/rest.py [program ...]      no program: all eight

The programs must be on the board's SD card (put.sh). The script speaks to either application on the board:
- Gideon's application (the C64 cores trx64g and Gideon's own): PUT /v1/runners:run_prg?file=..., then
  GET /v1/machine:readmem?address=0400&length=1000 (hex address, raw bytes).
- The TRX64 core application (the c64scpu with T250, every core of this repository): it answers GET / with
  "TRX64 core application" and wants a token on every other route (X-Trx-Token). The token comes from TRX_TOKEN; the
  script never asks for one (POST /v1/ui/grant hands it to the first caller, and the person at the board decides who
  that is). It starts the program with POST /v1/ops/<op> {"path": ...}: prg.run or c64.run_prg, else the core's own
  <id>.run_prg from GET /v1/ops (the c64scpu: c64scpu.run_prg), and reads the screen with POST /v1/ops/machine.read_mem in blocks of 256
  bytes (OP_DATA_MAX) at 1024 ($0400 of the CPU's own RAM), the data in base64.
A memory read can disturb a measurement that is running (on Gideon's cores it is a DMA that holds the CPU). So the
screen is read only after the program's run time on trx64g (RUN_S), then every 15 s until its last line is there; the
output says how many reads came before the end. Exit status 0 only when every program ended and shows "0 failed".
"""
import base64
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

DIR = '/SD/tests/scpu/'
# seconds from the start to the last line on trx64g (measured 2026-11-21, rounded up)
RUN_S = {'scpu1_env': 40, 'scpu2_ops': 25, 'scpu3_1mhz': 40, 'scpu4_irq': 20, 'scpu5_chain': 60, 'scpu6_20mhz': 25,
         'scpu7_mirror': 40, 'scpu8_shadow': 25}
MORE_S = 240                      # how long to keep reading after RUN_S (scpu5 sweeps up to K = 129 when no edge is found)
SCREEN = 1024                     # $0400


def text(b):
    """Screen codes as text: letters, digits and punctuation; anything else is '#'."""
    out = []
    for c in b[:1000]:
        r = c & 0x7F
        if r == 0:
            out.append('@')
        elif r <= 26:
            out.append(chr(ord('a') + r - 1))
        elif r == 27:
            out.append('[')
        elif r == 29:
            out.append(']')
        elif 32 <= r <= 63:
            out.append(chr(r))
        else:
            out.append('#')
    s = ''.join(out)
    return [s[i:i + 40].rstrip() for i in range(0, 1000, 40)]


class Board:
    def __init__(self, addr, token):
        self.addr, self.token = addr, token
        self.core_app = False
        self.run_op = None

    def call(self, method, path, body=None, auth=True):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request('http://%s%s' % (self.addr, path), data=data, method=method)
        if data is not None:
            req.add_header('Content-Type', 'application/json')
        if auth and self.core_app and self.token:
            req.add_header('X-Trx-Token', self.token)
        try:
            with urllib.request.urlopen(req, timeout=15) as f:
                return f.status, f.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def probe(self):
        st, b = self.call('GET', '/', auth=False)
        self.core_app = st == 200 and b'TRX64 core application' in b
        if not self.core_app:
            return 'Gideon\'s application'
        if not self.token:
            sys.exit('the core application needs a token: set TRX_TOKEN')
        st, b = self.call('GET', '/v1/info')
        if st != 200:
            sys.exit('GET /v1/info: %d %s' % (st, b[:200].decode(errors='replace')))
        core = json.loads(b).get('core', '')                         # the core's name as the menu shows it
        st, b = self.call('GET', '/v1/ops')
        if st != 200:
            sys.exit('GET /v1/ops: %d %s' % (st, b[:200].decode(errors='replace')))
        names = [o.get('name', '') for o in json.loads(b).get('ops', [])]
        runs = [n for n in ('prg.run', 'c64.run_prg') if n in names] or sorted(n for n in names if n.endswith('.run_prg'))
        if not runs:
            sys.exit('core %s registers no operation that runs a program' % core)
        self.run_op = runs[0]
        return 'the core application, core %s, %s' % (core, self.run_op)

    def start(self, prg):
        if self.core_app:
            st, b = self.call('POST', '/v1/ops/' + self.run_op, {'path': prg})
        else:
            st, b = self.call('PUT', '/v1/runners:run_prg?file=' + prg)
        if st != 200:
            sys.exit('start %s: %d %s' % (prg, st, b[:300].decode(errors='replace')))

    def screen(self):
        if not self.core_app:
            return text(self.call('GET', '/v1/machine:readmem?address=0400&length=1000')[1])
        raw = b''
        while len(raw) < 1000:
            n = min(256, 1000 - len(raw))
            st, b = self.call('POST', '/v1/ops/machine.read_mem', {'address': SCREEN + len(raw), 'length': n})
            if st != 200:
                sys.exit('machine.read_mem: %d %s' % (st, b[:300].decode(errors='replace')))
            raw += base64.b64decode(json.loads(b)['data'])
        return text(raw)


def run(board, name):
    t0 = time.time()
    board.start(DIR + name + '.prg')
    time.sleep(RUN_S.get(name, 60))
    reads = 0
    while True:
        rows = board.screen()
        done = [r for r in rows if re.search(r'\d+ checks, +\d+ failed', r)]
        if done or time.time() - t0 > RUN_S.get(name, 60) + MORE_S:
            break
        reads += 1
        time.sleep(15)
    while rows and not rows[-1]:
        rows.pop()
    note = '' if not reads else ' - %d read(s) during the run: the measurement may be disturbed' % reads
    print('==== %s (%.0f s)%s%s' % (name, time.time() - t0, '' if done else ' NOT DONE', note))
    print('\n'.join(rows), flush=True)
    m = re.search(r'(\d+) failed', done[0]) if done else None
    return bool(m) and m.group(1) == '0'


def main():
    addr = os.environ.get('BOARD')
    if not addr:
        sys.exit('set BOARD to the board address')
    board = Board(addr, os.environ.get('TRX_TOKEN'))
    print('==== the board speaks %s' % board.probe(), flush=True)
    names = [a[:-4] if a.endswith('.prg') else a for a in sys.argv[1:]] or sorted(RUN_S)
    sys.exit(0 if all([run(board, n) for n in names]) else 1)


if __name__ == '__main__':
    main()
