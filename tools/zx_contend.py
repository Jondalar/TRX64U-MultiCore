#!/usr/bin/env python3
"""T91 S6: the Spectrum's memory contention as a model of the machine's published timing (hardware fact), and the check of a bench log
against it. The bench (cores/zx/sim, mode cont48 / cont128) runs a NOP sled in contended RAM ($4000-$5FFF, then JP $4000 at $6000)
under IM 1 and logs the program counter the interrupt pushed ("INT n PC xxxx"). The model executes the same sled with the real timing:
  - one frame is 69,888 T-states (48K) / 70,908 (128K); the interrupt line goes low at T-state 0 of the frame
  - the first contended T-state is 14,335 (48K) / 14,361 (128K); a line is 224 / 228 T-states; in the first 128 T-states of each of the 192
    lines the ULA delays an access to $4000-$7FFF by 6,5,4,3,2,1,0,0 T-states (period 8) counted from the first contended T-state of the line
  - an access that starts at T-state t takes its length plus delay(t) (Fuse's accounting: opcode fetch 4, operand read 3)
  - the interrupt is taken at the end of the instruction that is running when the line goes low (or, `lat`, one T-state later); the
    handler of the bench program (cores/zx/sim/prog, tools/zx_progs.py cont) takes 13 + 117 T-states and is not contended
Given the pushed PC of the first logged interrupt, the unknown phase b1 (T-states from the line going low to the end of the running
instruction, 0-14) and lat, the model predicts the PC of every later interrupt; the check passes when one (b1, lat) predicts all of them.

  tools/zx_contend.py check 48|128 < log
  tools/zx_contend.py selftest       the model against the published table (and its own negative control)
"""
import sys

PARAMS = {48: dict(frame=69888, line=224, first=14335), 128: dict(frame=70908, line=228, first=14361)}
SLED, JPADDR, HANDLER = 0x4000, 0x6000, 13 + 117


def delay(model, t):
    p = PARAMS[model]
    if t < p["first"]:
        return 0
    off = t - p["first"]
    line, col = divmod(off, p["line"])
    if line >= 192 or col >= 128:
        return 0
    return (6, 5, 4, 3, 2, 1, 0, 0)[col % 8]


def sled_instr(model, T, pc, fstart):
    """execute the instruction at pc starting at absolute time T; returns (T', pc')"""
    def acc(T, n):
        return T + delay(model, T - fstart) + n
    if pc == JPADDR:
        T = acc(T, 4)
        T = acc(T, 3)
        T = acc(T, 3)
        return T, SLED
    return acc(T, 4), pc + 1


def predict(model, pc1, b1, lat, n):
    """the pcs of the interrupts after the first, given the first pc and the phase"""
    frame = PARAMS[model]["frame"]
    out = []
    k = 0                                     # frame index; INT k asserts at k * frame
    T = b1 + HANDLER                          # the sled resumes (frame 0 started at 0)
    pc = pc1
    for _ in range(n):
        nxt = (k + 1) * frame
        while True:
            T, pc = sled_instr(model, T, pc, k * frame)
            if T >= nxt + lat:                # the line went low at nxt: this instruction was running
                break
        # an instruction that ended before the line went low is not it; the loop above stops at the first end at or after it
        out.append(pc)
        k += 1
        T = T + HANDLER                       # accept, handler, back in the sled
    return out


def check(model, pcs):
    pcs = [p for p in pcs if p >= SLED]
    if len(pcs) < 4:
        print("FAIL cont%d: only %d interrupts logged" % (model, len(pcs)))
        return 1
    fits = []
    for lat in (0, 1):
        for b1 in range(0, 15):
            if predict(model, pcs[0], b1, lat, len(pcs) - 1) == pcs[1:]:
                fits.append((b1, lat))
    if fits:
        print("PASS cont%d: the model predicts %d later interrupts from the first (phase b1=%d lat=%d; %d fits)" %
              (model, len(pcs) - 1, fits[0][0], fits[0][1], len(fits)))
        return 0
    best = max(((sum(a == b for a, b in zip(predict(model, pcs[0], b1, lat, len(pcs) - 1), pcs[1:])), b1, lat)
                for lat in (0, 1) for b1 in range(15)))
    print("FAIL cont%d: no phase fits; best %d of %d (b1=%d lat=%d); logged %s" %
          (model, best[0], len(pcs) - 1, best[1], best[2], " ".join("%04x" % p for p in pcs)))
    return 1


def selftest():
    # the published pattern: 6,5,4,3,2,1,0,0 from T-state 14335 of the first line, none before, none after the 128th T-state of a line
    assert [delay(48, 14335 + i) for i in range(10)] == [6, 5, 4, 3, 2, 1, 0, 0, 6, 5]
    assert delay(48, 14334) == 0 and delay(48, 14335 + 128) == 0 and delay(48, 14335 + 224) == 6
    assert delay(48, 14335 + 192 * 224) == 0 and delay(128, 14361) == 6 and delay(128, 14361 + 228) == 6
    # a frame of the sled advances by fewer NOPs than an uncontended one would (69888 / 4 = 17472)
    pcs = [0x4100]
    for lat in (0,):
        p = predict(48, pcs[0], 3, lat, 6)
    print("selftest: ok (frames advance the sled to %s)" % " ".join("%04x" % x for x in p))
    return 0


def main(argv):
    if argv[1:] == ["selftest"]:
        return selftest()
    if len(argv) == 3 and argv[1] == "check":
        pcs = []
        for ln in sys.stdin:
            f = ln.split()
            if len(f) == 4 and f[0] == "INT":
                pcs.append(int(f[3], 16))
        return check(int(argv[2]), pcs)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
