#!/usr/bin/env python3
"""T164 WP5, scenario S7: the model behind host/sim/pixscale.

  model.py selftest                         the geometry function against the application's table (test_scale.cc)
  model.py plan  MODE SW SH FMT OUTDIR [full|nn|ctl|ctlnn]   writes OUTDIR/plan.hex: the steps of one run
  model.py check MODE SW SH FMT OUTDIR [full|nn|ctl|ctlnn]   compares OUTDIR/f<frame>.hex with the expected frames

The geometry (Integer, Fit, Fill) is the application's: video_layout_scaled in app/video/videomode.cc, ported line by line.
Nearest is exact: output pixel (x, y) is source pixel (floor(x * SW / OW), floor(y * SH / OH)). Smoothing is the bilinear mean of the
two source pixels and the two source lines the position falls between, weights = the fraction of the position, per channel, rounded
to the nearest integer; the pixel must be within one LSB of it. An axis on which the source is larger than the output stays nearest,
the vertical axis too when a source line is more than 128 words (format 2, SW > 512): the layer's rule. The colour key (format 1) makes
the pixel transparent when the nearest source pixel (floor, floor) is the key.
"""
import sys
import numpy as np

# mode: HTOT VTOT HACT VACT (XA = HTOT - HACT, YA = VTOT - VACT: the counters start at the sync, as hdmi_timing.v's), GEAR, TMDS period ps
MODES = {
    '1080': dict(htot=2640, vtot=1125, hact=1920, vact=1080, gear=1, per=6734),
    '720':  dict(htot=1980, vtot=750,  hact=1280, vact=720,  gear=2, per=6734),
    '576':  dict(htot=864,  vtot=625,  hact=720,  vact=576,  gear=5, per=7407),
    '480':  dict(htot=800,  vtot=525,  hact=640,  vact=480,  gear=5, per=7407),
    '600':  dict(htot=1052, vtot=650,  hact=800,  vact=600,  gear=3, per=8130),
    '768':  dict(htot=1344, vtot=806,  hact=1024, vact=768,  gear=2, per=7692),
    '1024': dict(htot=1688, vtot=1052, hact=1280, vact=1024, gear=1, per=9259),
}
NAMES = ['Fill', 'Fit', 'Integer', 'Down']
KEY = 0xF81F
FILL, FIT, INTEGER, DOWN = 0, 1, 2, 3


def layout(w, h, fw, fh, mode):
    """video_layout_scaled (bias 0): output size and position"""
    if mode == FILL:
        wo, ho = fw, fh
    elif mode == FIT:
        if fw * h <= fh * w:
            wo, ho = fw, fw * h // w
        else:
            ho, wo = fh, fh * w // h
    else:
        s = min(fw // w, fh // h)
        s = max(s, 1)
        wo, ho = w * s, h * s
    rx, ry = fw - wo, fh - ho
    px = rx // 2 if rx > 0 else 0
    py = ry // 2 if ry > 0 else 0
    return wo, ho, px, py


def selftest():
    # rows of tests/coreapp/test_scale.cc (w, h, frame w, frame h, mode, ow, oh, x0, y0)
    rows = [
        (720, 540, 1920, 1080, INTEGER, 1440, 1080, 240, 0),
        (720, 540, 1280, 720, INTEGER, 720, 540, 280, 90),
        (720, 540, 1920, 1080, FIT, 1440, 1080, 240, 0),
        (720, 540, 1280, 720, FIT, 960, 720, 160, 0),
        (720, 540, 1920, 1080, FILL, 1920, 1080, 0, 0),
        (720, 540, 720, 576, FILL, 720, 576, 0, 0),
        (720, 540, 720, 576, FIT, 720, 540, 0, 18),
        (720, 540, 720, 576, INTEGER, 720, 540, 0, 18),
    ]
    bad = 0
    for w, h, fw, fh, m, ow, oh, x0, y0 in rows:
        got = layout(w, h, fw, fh, m)
        if got != (ow, oh, x0, y0):
            bad += 1
            print('FAIL layout', (w, h, fw, fh, m), got, 'want', (ow, oh, x0, y0))
    print('selftest: %d rows, %d wrong' % (len(rows), bad))
    return bad


def steps(mode, sw, sh, fmt, kind):
    """the plan: Fill, Fit, Integer and Down (3/4 of the source at (30, 30): pixels and lines are skipped, nearest on both axes even
    when smoothing is asked for) nearest (key on in format 1), then the same smoothed (alpha 255; the Fit one with alpha 200)"""
    m = MODES[mode]
    out = []
    # kind: full = all four sizes nearest and smoothed; nn = nearest only (the build without smoothing); ctl = Fit, nearest and smoothed;
    # ctlnn = Fit, nearest
    for sm in ((0,) if kind in ('nn', 'ctlnn') else (0, 1)):
        for pol in ((FIT,) if kind in ('ctl', 'ctlnn') else (FILL, FIT, INTEGER, DOWN)):
            if pol == DOWN:
                ow, oh, x0, y0 = sw * 3 // 4, sh * 3 // 4, 30, 30
            else:
                ow, oh, x0, y0 = layout(sw, sh, m['hact'], m['vact'], pol)
            ga = 200 if (sm and pol == FIT) else 255
            out.append(dict(ow=ow, oh=oh, x0=x0, y0=y0, ga=ga, sm=sm, ken=1 if fmt == 1 else 0, pol=pol))
    return out


def plan(mode, sw, sh, fmt, outdir, kind):
    st = steps(mode, sw, sh, fmt, kind)
    with open(outdir + '/plan.hex', 'w') as f:
        f.write('%08x\n' % len(st))
        for s in st:
            for k in ('ow', 'oh', 'x0', 'y0', 'ga', 'sm', 'ken'):
                f.write('%08x\n' % s[k])
            f.write('%08x\n' % 0)
    for i, s in enumerate(st):
        print('step %d: %s %s %dx%d at (%d,%d) alpha %d smooth %d' %
              (i, mode, NAMES[s['pol']], s['ow'], s['oh'], s['x0'], s['y0'], s['ga'], s['sm']))


def rgb24(raw, fmt):
    raw = raw.astype(np.int64)
    if fmt == 2:
        return np.stack([raw & 255, (raw >> 8) & 255, (raw >> 16) & 255], axis=-1)   # channel 0 = B (bits 7:0)
    r5, g6, b5 = (raw >> 11) & 31, (raw >> 5) & 63, raw & 31
    r = (r5 << 3) | (r5 >> 2)
    g = (g6 << 2) | (g6 >> 4)
    b = (b5 << 3) | (b5 >> 2)
    return np.stack([b, g, r], axis=-1)


def expect(sw, sh, fmt, raw, s, m):
    """the expected layer rectangle (oh, ow, 3), channel 0 = bits 7:0"""
    ow, oh, x0, y0 = s['ow'], s['oh'], s['x0'], s['y0']
    P = rgb24(raw, fmt).astype(np.float64)
    x = np.arange(ow)
    y = np.arange(oh)
    sx = (x * sw) // ow
    sy = (y * sh) // oh
    hsm = s['sm'] and sw <= ow
    vsm = s['sm'] and sh <= oh and (fmt == 1 or sw <= 512)
    sx1 = np.minimum(sx + 1, sw - 1) if hsm else sx
    sy1 = np.minimum(sy + 1, sh - 1) if vsm else sy
    wh = (((x * sw) % ow) / ow)[None, :, None] if hsm else 0.0
    wv = (((y * sh) % oh) / oh)[:, None, None] if vsm else 0.0
    t = P[sy][:, sx] * (1 - wh) + P[sy][:, sx1] * wh
    bt = P[sy1][:, sx] * (1 - wh) + P[sy1][:, sx1] * wh
    fg = np.floor(t * (1 - wv) + bt * wv + 0.5).astype(np.int64)
    # the picture below
    xa = m['htot'] - m['hact']
    ya = m['vtot'] - m['vact']
    cx = (xa + x0 + x)[None, :]
    cy = (ya + y0 + y)[:, None]
    bgc = np.stack([((cx >> 8) & 15) * 16 + ((cy >> 7) & 15) + 0 * cy, ((cy & 255) ^ 0x3C) + 0 * cx, (cx & 255) + 0 * cy], axis=-1)
    a = s['ga'] + (s['ga'] >> 7)
    e = bgc + (((fg - bgc) * a) >> 8)
    if s['ken'] and fmt == 1:
        keyed = (raw[sy][:, sx] == KEY)
        e = np.where(keyed[:, :, None], bgc, e)
    return e, bgc


def check(mode, sw, sh, fmt, outdir, kind):
    m = MODES[mode]
    st = steps(mode, sw, sh, fmt, kind)
    raw = np.array([int(l, 16) for l in open(outdir + '/src.hex')], dtype=np.int64).reshape(sh, sw)
    errors = 0
    for i, s in enumerate(st):
        name = '%s %s %dx%d' % (mode, NAMES[s['pol']], s['ow'], s['oh'])
        try:
            # a pixel with an unknown bit (x or z) is a wrong pixel
            lines = [0x7000000 if ('x' in l or 'z' in l) else int(l, 16) for l in open('%s/f%d.hex' % (outdir, i + 2))]
        except IOError:
            print('FAIL step %d (%s): no frame dump' % (i, name))
            errors += 1
            continue
        n = s['ow'] * s['oh']
        if len(lines) != n:
            print('FAIL step %d (%s): %d pixels dumped, want %d' % (i, name, len(lines), n))
            errors += 1
            continue
        got = np.array(lines, dtype=np.int64).reshape(s['oh'], s['ow'])
        got = np.stack([got & 255, (got >> 8) & 255, (got >> 16) & 511], axis=-1)
        e, bgc = expect(sw, sh, fmt, raw, s, m)
        diff = np.abs(got - e).max(axis=-1)
        tol = 1 if s['sm'] else 0
        bad = diff > tol
        # a line the layer did not show (underrun) is the picture below, whole
        late = [r for r in range(s['oh']) if (got[r] == bgc[r]).all() and (e[r] != bgc[r]).any()]
        nbad = int(bad.sum())
        smax = int(diff.max())
        print('%s step %d (%s, %s): %d of %d pixels off by more than %d (max %d), %d late lines' %
              ('ok  ' if nbad == 0 and not late else 'FAIL', i, name, 'smooth' if s['sm'] else 'nearest', nbad, n, tol, smax, len(late)))
        if nbad or late:
            errors += 1
            if nbad:
                yy, xx = np.argwhere(bad)[0]
                print('FAIL first off pixel (%d, %d): got %s want %s' % (xx, yy, got[yy, xx].tolist(), e[yy, xx].tolist()))
    return errors


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'selftest':
        sys.exit(1 if selftest() else 0)
    if cmd == 'params':
        m = MODES[sys.argv[2]]
        print(' '.join('-Ptb.%s=%d' % (k, v) for k, v in (
            ('HTOT', m['htot']), ('VTOT', m['vtot']), ('XA', m['htot'] - m['hact']), ('YA', m['vtot'] - m['vact']),
            ('HACT', m['hact']), ('VACT', m['vact']), ('GEAR', m['gear']), ('PER', m['per']))))
        sys.exit(0)
    mode, sw, sh, fmt, outdir = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
    kind = sys.argv[7] if len(sys.argv) > 7 else 'full'
    if cmd == 'plan':
        plan(mode, sw, sh, fmt, outdir, kind)
    elif cmd == 'check':
        sys.exit(1 if check(mode, sw, sh, fmt, outdir, kind) else 0)
