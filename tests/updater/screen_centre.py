#!/usr/bin/env python3
"""T132 bug 1: measure where the TRX64 updater's title and header lines sit on the 40-column C64 text screen.

  screen_centre.py LOG centred    every header line starts at column (40 - length) // 2: PASS, else FAIL (exit 1)
  screen_centre.py LOG off        the control: the title must NOT be centred (exit 0 only if it is off, 1 if it is centred = the control is blind)
  screen_centre.py selftest       the measurement on made-up screens (centred, off by two, off by the old coloured title)

LOG is an ue2emu run log holding at least one `--- c64 ---` ... `--- c64 ---` block (the script verb `c64screen`); the block is the C64 text screen as
25 lines of 40 columns with trailing blanks cut, so the left margin of a line is its start column and its length is the text's width. The last block
that holds the title is measured. Lines measured: the title "** TRX64 Ultimate Updater **", and (centred mode) the header lines under it:
"Package <id>" / "Restore backup <id>", "Detected Flash:", "Detected FPGA Type:", "Board Revision:".
"""
import sys

W = 40
TITLE = "** trx64 ultimate updater **"
HEADERS = ("package ", "restore backup ", "detected flash:", "detected fpga type:", "board revision:")


def blocks(text):
    out, cur = [], None
    for ln in text.splitlines():
        if ln.strip() == "--- c64 ---":
            if cur is None:
                cur = []
            else:
                out.append(cur)
                cur = None
        elif cur is not None:
            cur.append(ln)
    return out


def measure(lines):
    """[(what, column, want)] for the title and the header lines of one screen; None when the title is not on it."""
    res, seen = [], set()
    for ln in lines:
        s = ln.strip()
        low = s.lower()
        if low == TITLE:
            res.append(("title", len(ln) - len(ln.lstrip(" ")), (W - len(s)) // 2))
        elif low.startswith(HEADERS) and low[:12] not in seen:   # the header block is the first of each kind; later status lines are not
            seen.add(low[:12])
            res.append((s, len(ln) - len(ln.lstrip(" ")), (W - len(s)) // 2))
    return res if any(r[0] == "title" for r in res) else None


def judge(text, mode, log=print):
    """exit code for a log text in mode centred / off"""
    shots = [m for m in (measure(b) for b in blocks(text)) if m]
    if not shots:
        log("FAIL no c64 screen with the updater title")
        return 1
    m = shots[-1]
    for what, col, want in m:
        log("  %-34s column %2d, centred would be %2d%s" % (what, col, want, "" if col == want else "   <- off"))
    title = [r for r in m if r[0] == "title"][0]
    if mode == "off":
        if title[1] == title[2]:
            log("FAIL control is blind: the title is centred")
            return 1
        log("control: the title is off centre (column %d, centre %d), as the unfixed code has it" % (title[1], title[2]))
        return 0
    bad = [r for r in m if r[1] != r[2]]
    if bad or len(m) < 4:
        log("FAIL %d of %d lines off centre%s" % (len(bad), len(m), "" if len(m) >= 4 else " (and fewer than the title and 3 header lines found)"))
        return 1
    log("PASS title and %d header lines centred on %d columns" % (len(m) - 1, W))
    return 0


def screen(title_col, head_col):
    rows = [" " * title_col + "** TRX64 Ultimate Updater **", "", ""]
    for t in ("Package main-e794f15d", "Detected Flash: S25FL128L", "Detected FPGA Type: XC7A50T", "Board Revision: U64E2"):
        rows.append(" " * (head_col(t)) + t)
    return "--- c64 ---\n" + "\n".join(rows) + "\n--- c64 ---\n"


def selftest():
    quiet = lambda *_: None   # noqa: E731
    centred = screen(6, lambda t: (W - len(t)) // 2)
    assert judge(centred, "centred", quiet) == 0, "centred screen refused"
    assert judge(centred, "off", quiet) == 1, "the control accepts a centred title"
    off = screen(4, lambda t: (W - len(t)) // 2)          # the title two columns left: the coloured title of the unfixed code
    assert judge(off, "centred", quiet) == 1, "an off-centre title passes"
    assert judge(off, "off", quiet) == 0, "the control rejects an off-centre title"
    hdr = screen(6, lambda t: 0)                          # title right, headers flush left
    assert judge(hdr, "centred", quiet) == 1, "left-aligned headers pass"
    assert judge("nothing here\n", "centred", quiet) == 1, "no screen passes"
    print("screen_centre selftest ok")
    return 0


def main(a):
    if a == ["selftest"]:
        return selftest()
    if len(a) != 2 or a[1] not in ("centred", "off"):
        sys.exit(__doc__)
    return judge(open(a[0], errors="replace").read(), a[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
