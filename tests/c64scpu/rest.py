#!/usr/bin/env python3
"""Runs the SuperCPU board suite (T241.1) on the board over REST and prints each program's last screen.

    BOARD=<address> tests/c64scpu/rest.py [program ...]      e.g. scpu5_chain; no program: all eight

The programs must be on the board's SD card (put.sh). Each one is started with PUT /v1/runners:run_prg, and its screen
($0400, 1000 bytes) is read with GET /v1/machine:readmem. The core must answer both: the board's C64 cores do, and the
c64scpu since T250.

A memory read is a DMA on the board's C64 cores: it holds the CPU and spoils a measurement that is running. So the
screen is read only after the program's run time on the board (RUN_S), then every 15 s until its last line is there.
"""
import os
import sys
import time
import urllib.request

DIR = '/SD/tests/scpu/'
# seconds from the start to the last line on the board (measured 2026-11-21 on trx64g, rounded up)
RUN_S = {'scpu1_env': 40, 'scpu2_ops': 25, 'scpu3_1mhz': 40, 'scpu4_irq': 20, 'scpu5_chain': 60, 'scpu6_20mhz': 25,
         'scpu7_mirror': 40, 'scpu8_shadow': 25}
MORE_S = 120


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


def call(board, method, path):
    req = urllib.request.Request('http://%s%s' % (board, path), method=method)
    with urllib.request.urlopen(req, timeout=15) as f:
        return f.read()


def run(board, name):
    t0 = time.time()
    call(board, 'PUT', '/v1/runners:run_prg?file=%s%s.prg' % (DIR, name))
    time.sleep(RUN_S.get(name, 60))
    while True:
        rows = text(call(board, 'GET', '/v1/machine:readmem?address=0400&length=1000'))
        done = any('checks,' in r for r in rows)
        if done or time.time() - t0 > RUN_S.get(name, 60) + MORE_S:
            break
        time.sleep(15)
    while rows and not rows[-1]:
        rows.pop()
    print('==== %s (%.0f s)%s' % (name, time.time() - t0, '' if done else ' NOT DONE'))
    print('\n'.join(rows), flush=True)
    return done


def main():
    board = os.environ.get('BOARD')
    if not board:
        sys.exit('set BOARD to the board address')
    names = [a[:-4] if a.endswith('.prg') else a for a in sys.argv[1:]] or sorted(RUN_S)
    sys.exit(0 if all([run(board, n) for n in names]) else 1)


if __name__ == '__main__':
    main()
