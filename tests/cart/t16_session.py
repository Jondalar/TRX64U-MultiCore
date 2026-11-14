#!/usr/bin/env python3
"""T16 board session with Alex's real cartridges (TRX64-Ultimate/docs/specs/T16-board-session.md).

One cartridge per run; Alex only powers off, plugs, powers on and presses keys.

    t16_session.sh boot                      load bitstream + app into SRAM, wait for REST
    t16_session.sh run --cart ef1            read-only stages (P3, P5) for one cartridge
    t16_session.sh run --cart ef1 --stages write,writer,ingame --write     P4 (writes!)
    t16_session.sh run --cart ef1 --stages full --write       the whole plan for one cart
    t16_session.sh plan                      the plan of the final session, with durations
    t16_session.sh all --write               every cart in turn (asks to swap carts), one typed confirmation
    t16_session.sh status                    slot status and pins, nothing else
    t16_session.sh mkbaseline                per-bank hashes of the candidate baseline CRTs
    t16_session.sh selftest                  offline logic test against the simulated carts
    t16_session.sh --dry-run run --cart gmod2 ...   the same run against the simulation

Carts: rom16 (C64anabalt), ef1 (EasyFlash 1, Timo's Castle), megabyter (Lykia),
gmod2 (Sam's Journey). Stages: pre boot dump soak pause100 matrix internal
(default); port gate app_up app_dump app_pref (read-only, trex_crt driven by Alex's keys); write eeprom writer app_write app_err ingame persist (only with --write). `--stages full`
is the per-cart plan of the final session (`plan` prints it with the durations); `all` runs the carts
one after the other. Every stage records PASS /
FAIL / WARN lines with the spec section they belong to; the report goes to
$T16_OUT/<cart>/report-<time>.json (default ~/.cache/trx64-t16).

Safety, in this order: nothing is written unless --write is given and the typed
confirmation matches; a write stage needs two byte-identical dumps of this cart in
this run; the first write of any stage is a probe that cannot change a bit; erase is
only ever sent to a sector that is blank in both backups; SLOT_CTRL.ROM_WR_EN is 1
only inside WrEn() and is cleared (and read back) on every exit path, also after an
exception, Ctrl-C and at interpreter exit; the first mismatch aborts the session.

REST + JTAG (tools/trxdbg.py) are used; run under the JTAG venv (t16_session.sh).
"""
from __future__ import annotations

import argparse
import atexit
import hashlib
import json
import os
import random
import signal
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
HOST = os.environ.get("TRX64_HOST", "192.0.2.189")
OUT = Path(os.environ.get("T16_OUT", Path.home() / ".cache" / "trx64-t16"))
BASELINES = HERE / "data" / "baseline_candidates.json"

# trx pages (TRX64-Ultimate/docs/specs/T16-cartridge-port.md §13.7)
SLOT_CTRL = 0x7000          # b1 ROM_WR_EN
SLOT_STATUS = 0x7004
GATE_CTRL = 0x0100          # b0: the app's C64_STOP pauses the whole machine (freeze sessions)
CFG_CATEGORY = "C64 and Cartridge Settings"
CFG_PREF = "Cartridge Preference"
CFG_WRITES = "Cartridge Port Writes"      # gone from the firmware (T57 Decision 2026-10-05): the pre stage checks it stays gone
CFG_SHARING = ("Bus Sharing - ROMs", "Bus Sharing - I/O1", "Bus Sharing - I/O2")

ROML, ROMH_A, ROMH_E = 0x8000, 0xA000, 0xE000
BANK_SZ = 0x2000


# Started as a script this module is __main__; t16_sim imports it as t16_session. One module object, so
# both see the same RestError and Abort classes (the dry run raised a RestError nobody caught).
if __name__ == "__main__":
    sys.modules.setdefault("t16_session", sys.modules["__main__"])


class Abort(Exception):
    """A check failed that makes every further step unsafe or meaningless."""


# ---------------------------------------------------------------------------
# The carts
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Cart:
    key: str
    title: str
    crt_type: int
    banks: int
    romh: str                 # "yes", "no" or "pins" (16K when GAME reads 0)
    pins: tuple               # allowed (GAME, EXROM) pin pairs while the game runs
    hdr: tuple                # CRT header (EXROM, GAME)
    flash: str                # "", "ef", "mb", "gm"
    eeprom: bool
    soak: int                 # default soak seconds (spec P3.3: 10 minutes on the EF)
    baseline: str             # key in baseline_candidates.json
    ingame: str = ""          # what Alex does for the in-game save
    app_type: int = -1        # type trex_crt reports for it (-1: not checked)

    def bank_value(self, bank: int) -> int:
        return bank & (self.banks - 1) if self.banks > 1 else 0


CARTS = {
    "rom16": Cart("rom16", "C64anabalt (plain 16K ROM)", 0, 1, "pins", {(0, 0), (1, 0)}, (0, 0), "", False, 60, "rom16", "", -1),
    "ef1": Cart("ef1", "EasyFlash 1 (Timo's Castle)", 32, 64, "yes", {(0, 0), (0, 1)}, (1, 0), "ef", False, 600, "ef1",
                "Timo's Castle spielen bis ein Highscore moeglich ist, im Spiel speichern, zurueck auf ein stabiles Bild", 32),
    "megabyter": Cart("megabyter", "MegaByter (Lykia)", 86, 128, "no", {(1, 0)}, (0, 1), "mb", False, 60, "megabyter",
                "Lykia: im Spiel speichern, falls es das kann (sonst einfach weiter)", 86),
    "gmod2": Cart("gmod2", "GMod2 (Sam's Journey)", 60, 64, "no", {(1, 0)}, (0, 1), "gm", True, 60, "gmod2",
                "Sam's Journey: im Spiel speichern (EEPROM), zurueck auf ein stabiles Bild", 60),
}


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def decode_status(v: int) -> dict:
    return dict(ours=bool(v >> 31 & 1), game=v & 1, exrom=v >> 1 & 1, dma=v >> 2 & 1, irq=v >> 3 & 1,
                nmi=v >> 4 & 1, reset=v >> 5 & 1, ext=v >> 8 & 15, int_=v >> 12 & 15,
                armed=bool(v >> 16 & 1), fault=bool(v >> 17 & 1), wr_en=bool(v >> 18 & 1), cause=v >> 20 & 15)


# ---------------------------------------------------------------------------
# Board access
# ---------------------------------------------------------------------------

class RestError(Exception):
    pass


class Rest:
    def __init__(self, host: str = HOST):
        self.host = host
        self.calls = 0

    def req(self, method: str, path: str, body: bytes | None = None, retries: int = 0, timeout: float = 30) -> bytes:
        last = None
        for attempt in range(retries + 1):
            self.calls += 1
            try:
                r = urllib.request.Request(f"http://{self.host}{path}", data=body, method=method)
                with urllib.request.urlopen(r, timeout=timeout) as f:
                    return f.read()
            except urllib.error.HTTPError as e:
                raise RestError(f"{method} {path}: HTTP {e.code} {e.read()[:160]!r}") from None
            except (urllib.error.URLError, OSError) as e:
                last = e
                time.sleep(0.5 * (attempt + 1))
        raise RestError(f"{method} {path}: {last!r}")

    def version(self) -> str:
        return self.req("GET", "/v1/version", retries=2).decode("latin-1")

    def readmem(self, addr: int, n: int, lens: str | None = None) -> bytes:
        q = f"/v1/machine:readmem?address={addr:04X}&length={n}"
        if lens:
            q += f"&lens={lens}"
        data = self.req("GET", q, retries=3)       # reads are idempotent: retry
        if len(data) != n:
            raise RestError(f"readmem {addr:04X}: {len(data)} bytes, want {n}")
        return data

    def poke(self, addr: int, val: int) -> None:
        # never retried: a repeated flash command byte could change a running sequence
        self.req("PUT", f"/v1/machine:writemem?address={addr:04X}&data={val & 0xFF:02X}")

    def pause(self) -> None:
        self.req("PUT", "/v1/machine:pause")

    def resume(self) -> None:
        self.req("PUT", "/v1/machine:resume")

    def reset(self) -> None:
        self.req("PUT", "/v1/machine:reset")

    def menu(self) -> None:
        self.req("PUT", "/v1/machine:menu_button")

    def run_crt(self, path: str) -> None:
        self.req("PUT", "/v1/runners:run_crt?file=" + urllib.parse.quote(path))

    def apps(self) -> list:
        try:
            return [a.get("id") for a in json.loads(self.req("GET", "/v1/apps")).get("apps", [])]
        except (RestError, ValueError):
            return []

    def _cfg(self, item: str, cat: str = CFG_CATEGORY) -> str:
        q = urllib.parse.quote
        return f"/v1/configs/{q(cat)}/{q(item)}"

    def cfg_get(self, item: str, cat: str = CFG_CATEGORY) -> str:
        reply = json.loads(self.req("GET", self._cfg(item, cat)))
        v = reply.get(cat, {}).get(item)
        if isinstance(v, dict):
            v = v.get("current", v.get("value"))
        if v is None:
            raise RestError(f"no config {item!r} in {reply}")
        return str(v)

    def cfg_set(self, item: str, value: str, cat: str = CFG_CATEGORY) -> None:
        """Runtime only: never saved to flash (PUT without save_to_flash)."""
        self.req("PUT", f"{self._cfg(item, cat)}?value={urllib.parse.quote(value)}")


class Jtag:
    """tools/trxdbg.py's Dbg, opened lazily (needs the pyftdi venv)."""

    def __init__(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import trxdbg
        self.t = trxdbg
        self.d = trxdbg.Dbg()

    def close(self) -> None:
        self.d.close()

    def status(self) -> int:
        return self.d.trx_read(SLOT_STATUS)

    def ctrl(self) -> int:
        return self.d.trx_read(SLOT_CTRL)

    def set_ctrl(self, v: int) -> None:
        self.d.trx_write(SLOT_CTRL, v)

    def gate(self) -> int:
        return self.d.trx_read(GATE_CTRL)

    def set_gate(self, v: int) -> None:
        self.d.trx_write(GATE_CTRL, v)

    def machine_pause(self, on: bool) -> None:
        self.d.machine_pause(on)

    def paused(self) -> bool:
        return bool(self.d.read(self.t.MACHINE) & self.t.MACHINE_PAUSED)

    def glue(self) -> dict:
        """State bus glue word: halted (b0) = the CPU words are live; CAP_GEN (b31:16) counts the capture snaps. Under
        REST's CPU pause alone the bus is NOT halted and keeps the words from before the pause (T16 Q8)."""
        w = self.d.sb_words(0, 1)[0]
        return dict(halted=bool(w & 1), gen=w >> 16 & 0xFFFF)

    def cpu(self) -> dict:
        g = self.glue()
        cpu = self.d.sb_words(1, 3)
        vic = self.d.sb_words(2, 12)
        vb = b"".join(w.to_bytes(4, "little") for w in vic)
        return dict(halted=g["halted"], gen=g["gen"],
                    pc=cpu[1] & 0xFFFF, a=cpu[0] & 0xFF, x=cpu[0] >> 8 & 0xFF, y=cpu[0] >> 16 & 0xFF,
                    s=cpu[0] >> 24, p=cpu[1] >> 16 & 0xFF, port01=cpu[2] >> 8 & 0xFF, ddr=cpu[2] & 0xFF,
                    raster=((vb[0x11] & 0x80) << 1) | vb[0x12])

    def screen(self) -> str:
        data = [self.d.read(0x0400 + i) & 0xFF for i in range(1000)]
        return "\n".join("".join(self.t.screen_char(c) for c in data[r * 40:r * 40 + 40]).rstrip() for r in range(25))


# ---------------------------------------------------------------------------
# Session context
# ---------------------------------------------------------------------------

class Ctx:
    def __init__(self, args, cart: Cart, rest, jtag, outdir: Path):
        self.a = args
        self.cart = cart
        self.rest = rest
        self.jtag = jtag
        self.out = outdir
        self.results: list[dict] = []
        self.state: dict = {}
        self.chunk = args.chunk
        self.lens = None if args.read_path == "plain" else "cart"
        # the two dump passes: "both" reads pass 1 as Gideon's core does (DMA bus reads, $01 set by DMA, Q8) and pass 2
        # through the lens, so the pass comparison is also plain-vs-lens
        self.pass_path = {1: "plain", 2: "lens"} if args.read_path == "both" else \
            {1: "plain" if args.read_path == "plain" else "lens", 2: "plain" if args.read_path == "plain" else "lens"}
        self.t0 = time.time()
        self.board = None           # easyflash_eapi_test.Board (FTP), made on demand
        self.hook = None            # dry run: what Alex's key presses do to the simulated cart
        self.ask_hook = None        # dry run: what the simulated Alex answers

    # logging and results
    def say(self, msg: str = "") -> None:
        print(msg, flush=True)

    def rec(self, stage: str, spec: str, name: str, ok: bool | None, detail: str = "", hard: bool = False) -> bool:
        tag = "WARN" if ok is None else ("PASS" if ok else "FAIL")
        self.results.append(dict(stage=stage, spec=spec, name=name, result=tag, detail=detail,
                                 t=round(time.time() - self.t0, 1)))
        self.say(f"  [{tag}] {spec:8s} {name}" + (f": {detail}" if detail else ""))
        if ok is False and hard:
            raise Abort(f"{name}: {detail}")
        return bool(ok)

    def ask(self, msg: str, default: bool = True, tag: str = "") -> bool:
        """Ja/Nein question for Alex. Anything but j/y is a No; EOF (no terminal) and Ctrl-D are the default."""
        self.say(f"\n>>> {msg}")
        if self.ask_hook:                         # dry run: the simulated Alex answers
            return self.ask_hook(tag, msg, default)
        if self.a.yes:
            self.say("    (--yes: weiter)")
            return default
        try:
            r = input(f"    [{'J/n' if default else 'j/N'}] ").strip().lower()
        except EOFError:
            self.say("    (keine Eingabe moeglich: Vorgabe)")
            return default
        return default if not r else r[0] in "jy"

    def pause_for_alex(self, msg: str, tag: str = "", arg: str = "") -> None:
        """Alex does something on the C64U; the cart may be touched meanwhile, so what was verified is stale."""
        self.say(f"\n>>> {msg}")
        self.state["dirty"] = True
        if self.hook:
            self.hook(tag, arg, msg)
        if not self.a.yes:
            try:
                input("    Wenn fertig: hier Enter druecken ")
            except EOFError:
                raise Abort("keine Eingabe moeglich (kein Terminal)") from None

    def status(self) -> dict:
        if self.a.gideon:       # no slot block on his bitstream: pins unknown, nothing armed or latched by us
            g, e = sorted(self.cart.pins)[0]
            return dict(ours=False, game=g, exrom=e, dma=1, irq=1, nmi=1, reset=0, ext=0, int_=0, armed=True,
                        fault=False, wr_en=False, cause=0)
        return decode_status(self.jtag.status())

    def has_romh(self, st: dict | None = None) -> bool:
        c = self.cart
        if self.a.romh:
            return self.a.romh == "yes"
        if c.romh != "pins":
            return c.romh == "yes"
        st = st or self.status()
        return st["game"] == 0

    def board_ftp(self):
        if self.board is None:
            sys.path.insert(0, str(HERE))
            from easyflash_eapi_test import Board
            self.board = Board(host=self.rest.host, jtag=False, directory=self.a.board_dir)
        return self.board


class WrEn:
    """ROM_WR_EN = 1 for the duration of the block, then the value it had before (the firmware keeps it at 1, T57
    Decision 2026-10-05; clearing it would take away the in-game saves). Put back and read back on every exit path."""
    active: list = []

    def __init__(self, ctx: Ctx):
        self.ctx = ctx
        self.prev = False

    def __enter__(self):
        j = self.ctx.jtag
        self.prev = decode_status(j.status())["wr_en"]
        WrEn.active.append(self)
        if not self.prev:
            j.set_ctrl(j.ctrl() | 2)
        if not decode_status(j.status())["wr_en"]:
            self._restore()
            raise Abort("ROM_WR_EN did not read back as set")
        self.ctx.say("    ROM_WR_EN = 1" + (" (already: firmware keeps it on)" if self.prev else ""))
        return self

    def _restore(self) -> None:
        j = self.ctx.jtag
        for _ in range(3):
            j.set_ctrl((j.ctrl() | 2) if self.prev else (j.ctrl() & ~2))
            if decode_status(j.status())["wr_en"] == self.prev:
                return
        print(f"!!! ROM_WR_EN is not back at {int(self.prev)}: reset the C64 or power the C64U off !!!", file=sys.stderr)

    def __exit__(self, *exc):
        self._restore()
        if self in WrEn.active:
            WrEn.active.remove(self)
        self.ctx.say(f"    ROM_WR_EN = {int(self.prev)}")
        return False


class Inhibit:
    """ROM_WR_EN = 0 for the duration of the block, cleared over JTAG (the firmware no longer has a way to do it, and it
    does not touch the bit again until the next settings change or reset); the bit as it was afterwards."""

    def __init__(self, ctx: Ctx):
        self.ctx = ctx
        self.prev = True

    def __enter__(self):
        c = self.ctx
        self.prev = c.status()["wr_en"]
        WrEn.active.append(self)
        c.jtag.set_ctrl(c.jtag.ctrl() & ~2)
        if not wait_wr(c, False, 1.0):
            self._restore()
            raise Abort("ROM_WR_EN does not go to 0")
        c.say("    ROM_WR_EN = 0 (JTAG)")
        return self

    def _restore(self) -> None:
        j = self.ctx.jtag
        for _ in range(3):
            j.set_ctrl((j.ctrl() | 2) if self.prev else (j.ctrl() & ~2))
            if decode_status(j.status())["wr_en"] == self.prev:
                return
        print(f"!!! ROM_WR_EN is not back at {int(self.prev)}: reset the C64 or power the C64U off !!!", file=sys.stderr)

    def __exit__(self, *exc):
        self._restore()
        if self in WrEn.active:
            WrEn.active.remove(self)
        return False


def expected_wr(ctx: Ctx) -> bool:
    """What the firmware makes of ROM_WR_EN: always 1 on our core (no write protection on the expansion port, T57
    Decision 2026-10-05, trx_slot_apply_policy), armed or not."""
    return True


def wait_wr(ctx: Ctx, want: bool, secs: float) -> bool:
    end = time.monotonic() + secs
    while True:
        if ctx.status()["wr_en"] == want:
            return True
        if time.monotonic() > end:
            return False
        time.sleep(0.2)


def _safety_exit() -> None:
    for w in reversed(list(WrEn.active)):
        try:
            w._restore()
        except Exception as e:  # noqa: BLE001
            print(f"!!! could not put ROM_WR_EN back: {e!r}", file=sys.stderr)


atexit.register(_safety_exit)


TERMINATE = {"now": False}


def _on_signal(sig, frame):
    _safety_exit()
    if sig == signal.SIGTERM:
        TERMINATE["now"] = True              # kill: the run ends after the report, no "continue with the next stage"
    raise KeyboardInterrupt


def stage_cleanup(ctx: Ctx) -> None:
    """After a stage that ended with an exception: the machine must run, whatever the stage held. Every step on its own."""
    for what, fn in (("REST resume", lambda: ctx.rest.resume()),
                     ("machine pause off", lambda: ctx.jtag.machine_pause(False) if ctx.jtag else None)):
        try:
            fn()
        except Exception as e:  # noqa: BLE001
            print(f"    cleanup {what}: {type(e).__name__}: {e}", file=sys.stderr)


def run_stages(ctx: Ctx, stages: list) -> int:
    """Every stage on its own: an exception (any, also Ctrl-C) is a FAIL line and the next stage runs. Only the stages that write to
    the cart are skipped after the first failed one. Ctrl-C ends the stage; a second Ctrl-C within 3 s (or SIGTERM) ends the run."""
    rc = 0
    blocked = None
    for s in stages:
        print(f"\n--- {s}")
        if ctx.state.get("jtag_lost") and s != "app_up":
            ctx.rec(s, "-", "skipped: JTAG handle lost", False, ctx.state["jtag_lost"])
            rc = 1
            continue
        if blocked and s in WRITE_STAGES:
            ctx.rec(s, "-", "skipped: writes to the cart after a failed hard check", None, blocked)
            continue
        try:
            STAGES[s](ctx)
        except KeyboardInterrupt:
            if TERMINATE["now"]:
                raise
            print("\n!!! Ctrl-C: Stage abgebrochen. Nochmal Ctrl-C innerhalb von 3 s beendet den ganzen Lauf.", file=sys.stderr)
            ctx.rec(s, "-", "stage interrupted by Ctrl-C", False, "")
            blocked = blocked or f"{s}: interrupted"
            rc = 1
            stage_cleanup(ctx)
            time.sleep(3)
            continue
        except Exception as e:  # noqa: BLE001 - a crash in one stage is a finding, not the end of the run
            if not isinstance(e, Abort):
                with open(OUT / "last_exception.txt", "a") as f:
                    import traceback
                    f.write(f"--- {time.strftime('%F %T')} stage {s}\n{traceback.format_exc()}\n")
                e = Abort(f"{type(e).__name__}: {e}")
            print(f"\n!!! ABORT stage {s}: {e}\n!!! nothing further was written: the stages that write to the cart "
                  "are skipped, the read-only stages go on", file=sys.stderr)
            ctx.rec(s, "-", "stage failed", False, str(e))
            blocked = blocked or f"{s}: {e}"
            rc = 1
            stage_cleanup(ctx)
    return rc


# ---------------------------------------------------------------------------
# Stage helpers
# ---------------------------------------------------------------------------

def wait_alive(ctx: Ctx, seconds: float = 3.0, samples: int = 12) -> tuple[bool, str]:
    """Machine runs: the program counter takes at least two values. The state bus only moves at capture snaps, so a
    machine under REST's CPU pause (or a bus that stopped snapping) reads as dead here, never as alive: call this with
    the machine running."""
    pcs, rasters, gens = set(), set(), set()
    gap = seconds / samples
    for _ in range(samples):
        c = ctx.jtag.cpu()
        pcs.add(c["pc"])
        rasters.add(c["raster"])
        gens.add(c.get("gen"))
        time.sleep(gap)
    # The state bus holds the VIC registers as written ($D012 = the raster compare value), not
    # the current raster line, so only the PC counts here; the picture questions check the rest.
    ok = len(pcs) >= 2
    return ok, f"{len(pcs)} PCs, {len(rasters)} raster values, {len(gens)} capture snaps" + \
        ("" if ok or len(gens) > 1 else " (state bus not refreshing: held or stale)")


def fresh_cpu(ctx: Ctx, gen0: int, snaps: int = 2, secs: float = 3.0) -> dict:
    """The CPU words of the state bus after at least `snaps` capture snaps since gen0 (read while the machine runs again).
    A read before that returns the words from before a REST CPU pause and would confirm whatever the test expects."""
    end = time.monotonic() + secs
    while True:
        c = ctx.jtag.cpu()
        if (c["gen"] - gen0) & 0xFFFF >= snaps and (c["gen"] - gen0) & 0xFFFF < 0x8000:
            return c
        if time.monotonic() > end:
            raise Abort(f"state bus stale: CAP_GEN {gen0} -> {c['gen']}, no capture snap after the resume; its words prove nothing")
        time.sleep(0.05)


def halted_cpu(ctx: Ctx, secs: float = 2.0) -> dict:
    """The CPU words of the state bus while the whole machine is paused: valid only when the glue word says halted."""
    end = time.monotonic() + secs
    while True:
        c = ctx.jtag.cpu()
        if c["halted"]:
            return c
        if time.monotonic() > end:
            raise Abort("state bus not halted under the machine pause: its words are from before the pause")
        time.sleep(0.05)


def wait_rest(ctx: Ctx, timeout: float = 90.0) -> None:
    end = time.monotonic() + timeout
    while True:
        try:
            ctx.rest.version()
            return
        except RestError:
            if time.monotonic() > end:
                raise Abort("REST does not answer") from None
            time.sleep(2)


def reset_into_cart(ctx: Ctx, settle: float = 4.0) -> None:
    ctx.rest.reset()
    time.sleep(settle)


def read_win(ctx: Ctx, addr: int, n: int, lens: str | None = "default") -> bytes:
    """n bytes at addr through REST: lens "cart" (forced select, our bitstream), None (a plain DMA bus read,
    what Gideon's path does; needs $01 to map the cart: set_port() while the CPU is held)."""
    if lens == "default":
        lens = ctx.lens
    out = bytearray()
    for off in range(0, n, ctx.chunk):
        k = min(ctx.chunk, n - off)
        if ctx.a.read_path == "lens3" and lens:
            # old workaround for the torn LK_RES reads (T16 §13.11): a per-byte majority of three reads
            r = [ctx.rest.readmem(addr + off, k, lens) for _ in range(3)]
            out += bytes(a if a == b or a == c else b for a, b, c in zip(*r))
        else:
            out += ctx.rest.readmem(addr + off, k, lens)
    return bytes(out)


def set_port(ctx: Ctx) -> None:
    """The CPU held by REST: $00 = $2F, $01 = $37 into the 6510 port (T16 Q8: the write is for this hold only,
    the program's port comes back at the resume), so a plain DMA read sees ROML/ROMH and $DE00 is I/O."""
    ctx.rest.poke(0x00, 0x2F)
    ctx.rest.poke(0x01, 0x37)


def romh_base(ctx: Ctx) -> int:
    """ROMH where the cart's own GAME/EXROM put it: $E000 in Ultimax, else $A000."""
    st = ctx.status()
    return ROMH_E if (st["game"], st["exrom"]) == (0, 1) else ROMH_A


def bank_order(cart: Cart, shuffle: int | None) -> list:
    order = list(range(cart.banks))
    if shuffle is not None:
        random.Random(shuffle).shuffle(order)
    return order


def reset_and_wait(ctx: Ctx, secs: float = 12.0) -> tuple[bool, str]:
    """RESET, then wait until the program runs (PC takes two values). A game that a test left in a state
    where it crashed ($01 changed, a bank the code does not expect) must not be read through its own
    memory map: a dump started at once reads RAM and fails for the wrong reason. One more reset if it
    does not come up."""
    ok, d = False, "no PC sample"
    for attempt in range(2):
        reset_into_cart(ctx)
        end = time.monotonic() + secs
        while time.monotonic() < end:
            ok, d = wait_alive(ctx, 1.5, 6)
            if ok:
                return True, d
    return ok, d


def dump_pass(ctx: Ctx, order: list, romh: bool, path: str | None = None) -> dict:
    """Every bank ROML (+ROMH) through the read path, machine held by REST's CPU pause. path "plain" = DMA bus
    reads (as Gideon's core), "lens" = forced select; default: the second pass's path of this run."""
    c = ctx.cart
    path = path or ctx.pass_path[2]
    lens = None if path == "plain" else "cart"
    banks = {}
    ctx.rest.pause()
    try:
        if lens is None:
            set_port(ctx)
        hi_base = romh_base(ctx) if lens is None else ROMH_A
        for b in order:
            if c.banks > 1:
                ctx.rest.poke(0xDE00, c.bank_value(b))
            roml = read_win(ctx, ROML, BANK_SZ, lens)
            hi = read_win(ctx, hi_base, BANK_SZ, lens) if romh else b""
            banks[b] = roml + hi
    finally:
        ctx.rest.resume()
    return banks


def image_of(banks: dict, n: int) -> bytes:
    return b"".join(banks[b] for b in range(n))


def sanity(ctx: Ctx, banks: dict, stage: str) -> None:
    c = ctx.cart
    img = image_of(banks, c.banks)
    ctx.rec(stage, "P3.2", "dump is not constant (no zeros / bus garbage)", len(set(img)) > 4,
            f"{len(set(img))} distinct byte values", hard=True)
    if c.banks > 1:
        distinct = len({sha(banks[b]) for b in banks})
        ctx.rec(stage, "P3.3", "bank switching by IO1 changes the data", distinct >= 2,
                f"{distinct} distinct banks of {c.banks}"
                + ("" if distinct >= 2 else " (DE00 write did not reach the cart? CPU port $01 hides I/O)"), hard=True)


def build_crt(cart: Cart, banks: dict, name: str, romh: bool, eeprom: bytes | None = None) -> bytes:
    h = bytearray(b"C64 CARTRIDGE   ")
    h += struct.pack(">IHHBBB", 0x40, 0x0100, cart.crt_type, cart.hdr[0], cart.hdr[1], 0) + bytes(5)
    h += name.encode()[:32].ljust(32, b"\0")
    ctype = 0 if cart.crt_type == 0 else 2
    for b in range(cart.banks):
        d = banks[b]
        h += b"CHIP" + struct.pack(">IHHHH", 0x2010, ctype, b, ROML, BANK_SZ) + d[:BANK_SZ]
        if romh:
            h += b"CHIP" + struct.pack(">IHHHH", 0x2010, ctype, b, ROMH_A, BANK_SZ) + d[BANK_SZ:2 * BANK_SZ]
    if eeprom:
        h += b"CHIP" + struct.pack(">IHHHH", 0x10 + len(eeprom), 1, 0, 0xDE00, len(eeprom)) + eeprom
    return bytes(h)


def crt_banks(data: bytes) -> tuple[int, dict]:
    """(hardware type, {bank: roml || romh}) from a CRT. A 16 KiB packet at $8000 counts as ROML+ROMH."""
    pos = struct.unpack(">I", data[0x10:0x14])[0]
    typ = struct.unpack(">H", data[0x16:0x18])[0]
    lo, hi = {}, {}
    while pos + 16 <= len(data) and data[pos:pos + 4] == b"CHIP":
        ln, ct, bank, load, size = struct.unpack(">IHHHH", data[pos + 4:pos + 16])
        body = data[pos + 16:pos + 16 + size]
        if ct != 1:
            if load == 0x8000 and size == 0x4000:
                lo[bank], hi[bank] = body[:BANK_SZ], body[BANK_SZ:]
            elif load == 0x8000:
                lo[bank] = body
            elif load in (0xA000, 0xE000):
                hi[bank] = body
        pos += ln
    return typ, {b: lo[b] + hi.get(b, b"") for b in sorted(lo)}


def load_baselines() -> dict:
    try:
        return json.loads(BASELINES.read_text())
    except (OSError, ValueError):
        return {}


def baseline_compare(ctx: Ctx, banks: dict, romh: bool) -> None:
    c = ctx.cart
    bl = load_baselines().get(c.baseline)
    if ctx.a.baseline_crt:
        _, ref = crt_banks(Path(ctx.a.baseline_crt).read_bytes())
        ref_sha = {b: sha(d) for b, d in ref.items()}
        src = ctx.a.baseline_crt
    elif bl:
        ref_sha = {int(b): s for b, s in bl["bank_sha256"].items()}
        src = bl["source"]
    else:
        ctx.rec("dump", "P3.2/4", "equals the dump on Gideon's bitstream", None,
                "no baseline for this cart (see TRX64-Ultimate/docs/specs/T16-board-session.md §Baseline): self-consistency only")
        return
    diff = [b for b in range(c.banks) if ref_sha.get(b) != sha(banks[b])]
    if not diff:
        ctx.rec("dump", "P3.2/4", "equals baseline dump", True, f"{c.banks} banks, baseline {src} (candidate, provenance unverified)")
    else:
        ctx.rec("dump", "P3.2/4", "equals baseline dump", None,
                f"{len(diff)} banks differ ({diff[:8]}...) against {src}: inconclusive, the cart may have been rewritten since; "
                f"compare on a second reader before suspecting the port")


# ---------------------------------------------------------------------------
# Stages
# ---------------------------------------------------------------------------

def st_pre(ctx: Ctx) -> None:
    a = ctx.a
    if not a.dry_run:
        r = subprocess.run(["pgrep", "-f", "lorenz.py"], capture_output=True)
        if r.returncode == 0:
            raise Abort("tests/lorenz.py holds the board and JTAG")
    ver = ctx.rest.version()
    ctx.rec("pre", "-", "REST answers", True, ver[:80])
    if a.gideon:
        ctx.rec("pre", "-", "Gideon-core run: no slot block, plain DMA reads", None, "TRX64-Ultimate/docs/specs/T16-board-session.md, Baseline")
        return
    st = ctx.status()
    ctx.rec("pre", "§13.7", "our slot block present (SLOT_STATUS b31)", st["ours"], hex(ctx.jtag.status()), hard=True)
    ctx.rec("pre", "§13.7", "no fault latched", not st["fault"], f"cause {st['cause']}", hard=True)
    try:
        item = ctx.rest.cfg_get(CFG_WRITES, CFG_CATEGORY)
    except RestError:
        item = None
    ctx.rec("pre", "T57 D2026-10-05", "firmware has no 'Cartridge Port Writes' item (write protection removed)", item is None,
            f"item still answers {item!r}" if item is not None else "")
    ctx.state["wr0"] = st["wr_en"]
    ctx.rec("pre", "T57 D2026-10-05", f"ROM_WR_EN is 1 (always on; ext mask {st['ext']:04b}, int {st['int_']:04b})",
            st["wr_en"], f"bit {int(st['wr_en'])}", hard=True)
    t = time.time()
    for _ in range(5):
        ctx.rest.readmem(ROML, 16, ctx.lens)
    ms = (time.time() - t) / 5 * 1000
    ctx.state["req_ms"] = ms
    ctx.say(f"  REST read {ms:.0f} ms/request ({ctx.lens or 'plain DMA'}); full dump ~"
            f"{2 * ctx.cart.banks * (1 + 2 * (BANK_SZ // ctx.chunk)) * ms / 60000:.1f} min for both passes")
    apps = ctx.rest.apps()
    ctx.rec("pre", "-", "trex_crt app registered (for writer stage / manual dump)", True if "trex_crt" in apps else None,
            f"apps: {apps}")


def st_boot(ctx: Ctx) -> None:
    c = ctx.cart
    st = ctx.status()
    pins = (st["game"], st["exrom"])
    ctx.rec("boot", "P1/P3.1", f"GAME/EXROM pins {pins} fit {c.title}", pins in c.pins,
            f"allowed {sorted(c.pins)}", hard=True)
    ctx.rec("boot", "P3.1", "port armed (Auto + cart detected at reset)", st["armed"],
            f"ext mask {st['ext']:04b} int {st['int_']:04b}; preference {ctx.state.get('pref', '?')}")
    if not st["armed"]:
        ctx.say("    Port not armed. Cartridge Preference must be Auto (or Manual with Bus Sharing External) "
                "and the C64 reset with the cart in. Trying a reset.")
        reset_into_cart(ctx)
        st = ctx.status()
        ctx.rec("boot", "P3.1", "armed after reset", st["armed"], hard=True)
    ctx.rec("boot", "§13.2", "external mask has ROM", st["ext"] != 0, f"{st['ext']:04b}")
    ok, d = wait_alive(ctx, 5.0, 16)
    ctx.rec("boot", "P3.1", "cart code runs (PC and raster move)", ok, d)
    ctx.state["screen"] = ctx.jtag.screen()
    ctx.say("    screen text:\n" + "\n".join("      | " + l for l in ctx.state["screen"].splitlines() if l.strip())[:1200])
    r = ctx.ask(f"{c.title}: Ist das Spiel/Menue auf dem HDMI-Bild da und sieht normal aus?", tag="picture")
    ctx.rec("boot", "P3.1", "picture OK (Alex)", r)
    st = ctx.status()
    ctx.rec("boot", "P3", "no fault", not st["fault"], f"cause {st['cause']}", hard=True)


def first_backup(ctx: Ctx, base: Path, crt: bytes, banks: dict, romh: bool, stamp: str, meta: dict) -> None:
    """The backup of the original cart is written ONCE (O_EXCL, then read-only) and never replaced: it is what every restore
    in this suite writes back. A later dump of a changed cart (a save, a wipe, another cart) goes to a file of its own."""
    c = ctx.cart
    first = base / f"t16_backup_{c.key}.crt"
    run_sha = {b: sha(d) for b, d in banks.items()}
    restore_crt, restore_banks, restore_sha = crt, banks, meta["sha256"]
    if not first.exists():
        with open(first, "xb") as f:
            f.write(crt)
        os.chmod(first, 0o444)
        (ctx.out / "verified_backup.json").write_text(json.dumps(meta, indent=1))
        ctx.rec("dump", "T16 backup", "FIRST backup written (once, read-only)", True, f"{first} sha256 {meta['sha256'][:16]}")
    else:
        old = first.read_bytes()
        try:
            typ, ob = crt_banks(old)
            old_sha = {b: sha(d) for b, d in ob.items()}
            usable = typ == c.crt_type and sorted(ob) == list(range(c.banks)) and all(len(d) == len(banks[0]) for d in ob.values())
        except Exception:  # noqa: BLE001
            usable, ob, old_sha = False, {}, {}
        if usable and old_sha == run_sha:
            ctx.rec("dump", "T16 backup", "cart == the FIRST backup (file kept, not rewritten)", True, f"{first.name} sha256 {meta['sha256'][:16]}")
            restore_crt = old
        elif usable:
            diff = [b for b in range(c.banks) if old_sha.get(b) != run_sha.get(b)]
            new = base / f"t16_dump_{c.key}-{stamp}.crt"
            new.write_bytes(crt)
            ctx.rec("dump", "T16 backup", "cart differs from the FIRST backup: kept, restores write the FIRST backup", None,
                    f"{len(diff)} banks differ {diff[:8]}; this dump is {new.name}; first backup {first.name} untouched")
            restore_crt, restore_banks, restore_sha = old, ob, sha(image_of(ob, c.banks))
        else:
            new = base / f"t16_dump_{c.key}-{stamp}.crt"
            new.write_bytes(crt)
            ctx.rec("dump", "T16 backup", "the FIRST backup file is not usable for this cart (type/banks/unreadable)", False,
                    f"{first.name} left alone; this dump is {new.name}; restores use THIS run's dump")
    ctx.state.update(backup_crt=restore_crt, restore_banks=restore_banks, restore_sha=restore_sha, expect=banks)
    ctx.say(f"  restore image: {restore_sha[:16]} ({'first backup' if restore_crt is not crt else 'this dump'}), {len(restore_crt)} bytes")


def check_cart(ctx: Ctx, stage: str, spec: str, name: str, want: dict, seed: int, hard: bool = False, sample: bool = False) -> bool:
    """The cart equals `want`. A full dump (sample: first and last bank of every 8-bank flash sector, enough to see an erase)
    unless the last dump already proved exactly this and nothing touched the cart since: no second compare against the same
    file for the same state."""
    c = ctx.cart
    want_sha = sha(image_of(want, c.banks))
    if not ctx.state.get("dirty") and ctx.state.get("verified_sha") == want_sha:
        return ctx.rec(stage, spec, name, True, f"{want_sha[:16]} (proved by the dump before, cart untouched since: no second dump)", hard=hard)
    if ctx.state.get("dirty"):
        # Alex used the menu / an app since the last dump: back into the cart's own boot state first, as after the dump
        # stage (2026-10-10: a sampled read right after the freeze menu differed, a fresh dump of the same cart was equal)
        reset_into_cart(ctx)
    if sample:
        order = sorted({b for s in range(0, c.banks, 8) for b in (s, min(s + 7, c.banks - 1))})
        got = dump_pass(ctx, order, ctx.state["romh"])
        same = all(got[b] == want[b] for b in order)
        if same:
            return ctx.rec(stage, spec, name, True, f"{len(order)} of {c.banks} banks (sector edges), not a full dump", hard=hard)
        ctx.say(f"  sampled banks differ ({[b for b in order if got[b] != want[b]][:8]}): a full dump decides")
    got = dump_pass(ctx, bank_order(c, seed), ctx.state["romh"])
    got_sha = sha(image_of(got, c.banks))
    same = got_sha == want_sha
    ctx.state.update(dirty=False, verified_sha=got_sha, expect=want if same else got)
    if not same and hard:
        ctx.say(f"!!! Die Karte weicht ab. Wiederherstellen: t16_backup_{c.key}.crt (Kopie des ERSTEN Backups, liegt im Board-Verzeichnis "
                f"und in {ctx.out / 'dump'}) im Dateibrowser mit 'Write to Cartridge' schreiben.")
    return ctx.rec(stage, spec, name, same, got_sha[:16], hard=hard)


def st_dump(ctx: Ctx) -> None:
    c = ctx.cart
    st = ctx.status()
    romh = ctx.has_romh(st)
    ctx.state["romh"] = romh
    shuffle = {1: None, 2: 0xC64}
    passes = []
    for n in (1, 2):
        t = time.time()
        order = bank_order(c, shuffle[n])
        ctx.say(f"  dump pass {n} ({'forward' if shuffle[n] is None else 'shuffled'} bank order, {c.banks} banks, "
                f"{'ROML+ROMH' if romh else 'ROML'}, path {ctx.pass_path[n]}) ...")
        banks = dump_pass(ctx, order, romh, ctx.pass_path[n])
        ctx.rec("dump", "P3.2/4", f"pass {n} complete", len(banks) == c.banks, f"{time.time() - t:.0f} s")
        if n == 1:
            sanity(ctx, banks, "dump")
        passes.append(banks)
        st = ctx.status()
        ctx.rec("dump", "P3", "no fault after the pass", not st["fault"], f"cause {st['cause']}", hard=True)
    img = [image_of(p, c.banks) for p in passes]
    same = img[0] == img[1]
    bad = [b for b in range(c.banks) if passes[0][b] != passes[1][b]]
    ctx.rec("dump", "P3.4", f"pass 1 == pass 2 ({ctx.pass_path[1]} forward vs {ctx.pass_path[2]} shuffled)", same,
            f"sha256 {sha(img[0])[:16]}" if same else f"banks differ: {bad[:16]}", hard=True)
    base = ctx.out / "dump"
    base.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    for n, d in enumerate(img, 1):
        (base / f"{c.key}-{stamp}-pass{n}.bin").write_bytes(d)
    full = sha(img[0])
    meta = dict(cart=c.key, sha256=full, banks=c.banks, romh=romh, per_bank=[sha(passes[0][b]) for b in range(c.banks)],
                two_passes_equal=True, time=stamp)
    (base / f"{c.key}-{stamp}.json").write_text(json.dumps(meta, indent=1))
    ctx.say(f"  dump sha256 {full}")
    crt = build_crt(c, passes[0], f"T16 {c.key} backup", romh)
    ctx.state.update(backup_ok=True, backup=passes[0], backup_sha=full, romh=romh, run_crt=crt,
                     verified_sha=full, dirty=False)
    first_backup(ctx, base, crt, passes[0], romh, stamp, meta)
    baseline_compare(ctx, passes[0], romh)
    # the bank register is left on the last bank: back into the cart's own boot state
    reset_into_cart(ctx)
    if not ctx.a.gideon:
        ok, d = wait_alive(ctx, 3.0, 10)
        ctx.rec("dump", "P3.3", "game runs again after the dump + reset", ok, d)


def st_soak(ctx: Ctx) -> None:
    secs = ctx.a.soak if ctx.a.soak is not None else ctx.cart.soak
    if ctx.a.quick:
        secs = min(secs, 30)
    ctx.say(f"  soak {secs} s, checking every 5 s ...")
    end, bad, n = time.monotonic() + secs, 0, 0
    while time.monotonic() < end:
        ok, d = wait_alive(ctx, 2.0, 8)
        st = ctx.status()
        n += 1
        if st["fault"] or not ok:
            bad += 1
            ctx.say(f"    check {n}: alive={ok} {d} fault={st['fault']}")
        time.sleep(3)
    ctx.rec("soak", "P3.3", f"{secs} s running with the cart, {n} checks", bad == 0, f"{bad} bad")


def _alive_and_clean(ctx: Ctx) -> tuple[bool, str]:
    ok, d = wait_alive(ctx, 1.0, 6)
    st = ctx.status()
    pins = (st["game"], st["exrom"])
    return bool(ok and not st["fault"] and pins in ctx.cart.pins), f"{d}, pins {pins}, fault {st['fault']}"


def st_pause100(ctx: Ctx) -> None:
    rnd = random.Random(16)
    bad = []
    t = time.time()
    for i in range(int(os.environ.get("T16_PAUSE_N", "100"))):
        ctx.jtag.machine_pause(True)
        time.sleep(rnd.uniform(0.04, 0.3))           # whole PAL frames are 20 ms
        if not ctx.jtag.paused():
            bad.append((i, "not paused"))
        ctx.jtag.machine_pause(False)
        time.sleep(rnd.uniform(0.05, 0.25))
        if i % 10 == 9:
            ok, d = _alive_and_clean(ctx)
            if not ok:
                bad.append((i, d))
                ctx.say(f"    cycle {i + 1}: {d}")
        if ctx.status()["fault"]:
            bad.append((i, "fault"))
            break
    ok, d = wait_alive(ctx, 2.0, 8)
    ctx.rec("pause100", "P3.6", f"100 x machine pause/resume (JTAG reg 9 b3), {time.time() - t:.0f} s", not bad and ok,
            f"{len(bad)} bad {bad[:3]}; final {d}")
    r = ctx.ask("Nach 100 Pause/Weiter-Zyklen: Ist das Spiel noch normal (kein Absturz, kein Muell)?", tag="picture")
    ctx.rec("pause100", "P3.6", "picture OK after 100 cycles (Alex)", r)


VARIANTS = tuple(v for v in ("jtag_pause", "rest_pause", "rest_pause_gated", "menu_freeze", "lens_run")
                 if v not in os.environ.get("T16_SKIP_VARIANTS", "").split(","))


def _hold(ctx: Ctx, variant: str, secs: float) -> str:
    j, r = ctx.jtag, ctx.rest
    extra = ""
    if variant == "jtag_pause":                       # option A: PHI2 stops, the cart freezes with the machine
        j.machine_pause(True)
        time.sleep(0.05)
        a = read_win(ctx, ROML, 64)
        time.sleep(secs)
        b = read_win(ctx, ROML, 64)
        j.machine_pause(False)
        extra = "park read stable" if a == b else "PARK READ CHANGED"
        if a != b:
            raise Abort(f"{variant}: ROML bytes changed during the pause")
    elif variant == "rest_pause":                     # CPU pause only, PHI2 keeps running
        r.pause()
        time.sleep(secs)
        r.resume()
    elif variant == "rest_pause_gated":               # machine pause with freeze sessions
        g = j.gate()
        j.set_gate(g | 1)
        try:
            r.pause()
            time.sleep(secs)
            r.resume()
        finally:
            j.set_gate(g)
    elif variant == "menu_freeze":                    # freeze overlay (T12)
        r.menu()
        time.sleep(max(secs, 1.0))
        r.menu()
        time.sleep(1.0)
    elif variant == "lens_run":                       # readmem lens while running (pauses for the read)
        t = time.monotonic()
        while time.monotonic() - t < secs:
            read_win(ctx, ROML, 256)
            time.sleep(0.05)
    return extra


def st_matrix(ctx: Ctx) -> None:
    durs = [float(x) for x in ctx.a.matrix_secs.split(",")]
    reps = 2 if ctx.a.quick else 3
    table = {}
    for v in VARIANTS:
        for d in durs:
            okc = 0
            note = ""
            for _ in range(reps):
                note = _hold(ctx, v, d)
                time.sleep(0.5)
                ok, det = _alive_and_clean(ctx)
                okc += ok
                if not ok:
                    ctx.say(f"    {v} {d}s: {det}")
            table[f"{v}/{d}s"] = f"{okc}/{reps}"
            ctx.rec("matrix", "P5/§8.2", f"{v} {d} s x{reps}: alive after resume, no fault", okc == reps, note)
    ctx.rec("matrix", "P5/§8.2", "option B (PARK_PHI2_RUN) not built (§13.8): not tested", None,
            "decision Q3: option A; this cart class needs no running clock if every row above passes")
    r = ctx.ask("Nach der Pausen-Matrix: Ist das Bild noch normal?", tag="picture")
    ctx.rec("matrix", "P5", "picture OK after the matrix (Alex)", r)
    ctx.state["matrix"] = table


def _set_pref(ctx: Ctx, pref: str, sharing: str | None = None) -> None:
    ctx.rest.cfg_set(CFG_PREF, pref)
    if sharing:
        for item in CFG_SHARING:
            ctx.rest.cfg_set(item, sharing)


def st_internal(ctx: Ctx) -> None:
    c = ctx.cart
    if c.key not in ("ef1", "rom16"):
        ctx.rec("internal", "P3.5", "preference Internal with the cart in", None, "spec P3.5 covers the EF / ROM cart only; skipped")
        return
    pref0 = ctx.rest.cfg_get(CFG_PREF)
    ctx.say(f"  preference {pref0} -> Internal (runtime only), reset, run a CRT from the board's drive ...")
    try:
        b = ctx.board_ftp()
        crt = ctx.state.get("run_crt")
        if crt is None:
            raise Abort("no dump CRT in this run (run the dump stage first)")
        name = f"t16_internal_{c.key}.crt"
        b.upload(name, crt)
        _set_pref(ctx, "Internal")
        ctx.rest.reset()
        time.sleep(3)
        st = ctx.status()
        ctx.rec("internal", "P3.5", "not armed with preference Internal", not st["armed"] and st["ext"] == 0,
                f"armed {st['armed']} ext {st['ext']:04b}")
        ctx.rest.run_crt(b.path(name))
        time.sleep(6)
        ok, d = wait_alive(ctx, 3.0, 10)
        st = ctx.status()
        ctx.rec("internal", "P3.5", "internal CRT runs on cart_slot, port quiet", ok and not st["armed"] and not st["fault"],
                f"{d}, armed {st['armed']}")
        ctx.rec("internal", "P3.5", "picture OK (Alex)", ctx.ask("Laeuft die interne Kopie des Carts (HDMI-Bild normal)?", tag="picture"))
    finally:
        _set_pref(ctx, pref0)
        reset_into_cart(ctx)
    st = ctx.status()
    ctx.rec("internal", "P3.5", "back to the original preference and armed", st["armed"], f"pref {pref0}")


# --- Q8, gate, trex_crt ----------------------------------------------------

def st_port(ctx: Ctx) -> None:
    """T16 Q8: a DMA write of $01 while the CPU is held reaches the 6510 port and lasts for the hold only. With the
    game's own $01 (Lykia, Sam's Journey: $35, ROML hidden) a plain DMA read must not see the cart before the write and
    must equal the lens read after it; the program's port is back after the resume. Both holds: REST's CPU pause (the
    app's C64_STOP) and the whole-machine pause."""
    j, r = ctx.jtag, ctx.rest
    for hold in ("rest", "machine"):
        c0 = j.cpu()
        port0, ddr0 = c0["port01"], c0["ddr"]
        notes = []
        if hold == "rest":
            r.pause()
        else:
            j.machine_pause(True)
            time.sleep(0.05)
        try:
            before = r.readmem(ROML, 32)                       # plain DMA read with the program's $01
            r.poke(0x00, 0x2F)
            # The state bus is loaded only while the machine is halted or at a capture snap (c64_core.v
            # sb_capture); under REST's CPU pause it keeps the port from before the pause (spec T16 Q8,
            # board 2026-10-05). So the port write is proven by what it does: $35 hides ROML from a plain
            # DMA read, $37 shows it again.
            r.poke(0x01, 0x35)
            hidden = r.readmem(ROML, 32)
            r.poke(0x01, 0x37)
            plain = r.readmem(ROML, 32)
            lens = r.readmem(ROML, 32, "cart")
            ctx.rec("port", "T16 Q8", f"{hold} hold: DMA write of $01 reaches the CPU port ($35 hides ROML, $37 shows it)",
                    hidden != lens and plain == lens,
                    f"$35 {hidden[:8].hex()} $37 {plain[:8].hex()} lens {lens[:8].hex()}", hard=True)
            if hold == "machine":
                c1 = halted_cpu(ctx)                           # the bus is live only when halted
                ctx.rec("port", "T16 Q8", "machine hold: the state bus shows the written port", c1["port01"] == 0x37 and c1["ddr"] == 0x2F,
                        f"$00 {c1['ddr']:02X} $01 {c1['port01']:02X} (program's {ddr0:02X}/{port0:02X})", hard=True)
            ctx.rec("port", "T16 Q8", f"{hold} hold: plain DMA read after $01 = $37 equals the lens read", plain == lens,
                    f"plain {plain[:8].hex()} lens {lens[:8].hex()}", hard=True)
            if before != plain:
                notes.append("the program's own $01 hid ROML from the plain read before the write (the Q8 case)")
        finally:
            gen_hold = j.glue()["gen"]
            if hold == "rest":
                r.resume()
            else:
                j.machine_pause(False)
        time.sleep(0.3)
        c2 = fresh_cpu(ctx, gen_hold)          # not before the bus has snapped again: else it is the word from before the hold
        ctx.rec("port", "T16 Q8", f"{hold} hold: the program's port is back after the resume",
                (c2["port01"], c2["ddr"]) == (port0, ddr0), f"$00 {c2['ddr']:02X} $01 {c2['port01']:02X}", hard=True)
        ok, d = wait_alive(ctx, 2.0, 8)
        ctx.rec("port", "T16 Q8", f"{hold} hold: program runs on", ok, d + ("; " + "; ".join(notes) if notes else ""))
        ctx.rec("port", "P3", "no fault", not ctx.status()["fault"], "", hard=True)


def st_gate(ctx: Ctx) -> None:
    """T57 Decision 2026-10-05 on the board: ROM_WR_EN is 1 whatever the arming is: with the port armed, after the
    preference is set to Internal (port not armed) and reset, and again after everything is put back."""
    r = ctx.rest
    pref0 = r.cfg_get(CFG_PREF)
    ctx.rec("gate", "T57 D2026-10-05", "start: port armed, ROM_WR_EN 1", ctx.status()["armed"] and ctx.status()["wr_en"],
            f"armed {ctx.status()['armed']} bit {int(ctx.status()['wr_en'])}")
    try:
        r.cfg_set(CFG_PREF, "Internal")
        r.reset()
        time.sleep(4)
        st = ctx.status()
        ctx.rec("gate", "T57 D2026-10-05", "preference Internal (port not armed): ROM_WR_EN still 1",
                not st["armed"] and st["wr_en"], f"armed {st['armed']} bit {int(st['wr_en'])}")
    finally:
        r.cfg_set(CFG_PREF, pref0)
        reset_into_cart(ctx)
    ctx.rec("gate", "T57 D2026-10-05", "settings back: armed again, ROM_WR_EN 1",
            ctx.status()["armed"] and ctx.status()["wr_en"], "")


def app_file(ctx: Ctx) -> Path:
    for cand in (ctx.a.app_u2a, os.environ.get("TREX_CRT_U2A")):
        if cand and Path(cand).is_file():
            return Path(cand)
    raise Abort("trex_crt.u2a not found: build it (apps/trex_crt/build.sh) and give --app-u2a PATH")


def app_path(ctx: Ctx) -> str:
    return ctx.board_ftp().path("trex_crt.u2a")


def st_app_up(ctx: Ctx) -> None:
    f = app_file(ctx)
    ctx.board_ftp().upload("trex_crt.u2a", f.read_bytes())
    ctx.rec("app_up", "T57", f"trex_crt.u2a on the board ({f.stat().st_size} bytes)", True, f"{app_path(ctx)} sha256 {sha(f.read_bytes())[:12]}")


# trex_crt has no unattended mode (Alex, T57 Q5): Alex starts it from the C64U's menus, the script prepares the board, tells him
# the keys and checks what it can from outside (JTAG status, a dump of the whole cart, the gate, the reset).
# Prompts for Alex: German, numbered steps on the C64U, then "Enter here" (the input prompt). He types nothing but j/n and Enter.
KEYS_APP = ("  1. C64U: Menu-Taste (Freeze), F5 (Tasks) -> 'C64 Machine' -> 'Dump Cartridge'.\n"
            "     (Erstes Mal: Menu -> Dateibrowser -> {app} -> Enter -> 'Run App'.)")
KEYS_DUMP = KEYS_APP + ("\n  2. F1 Scan (warten bis die Karte erkannt ist), F3 Dump, Typ 'Auto' oder der vorgeschlagene.\n"
                        "  3. Ziel {dir}, Name '{name}'. 'Dump complete' abwarten, dann RUN/STOP.\n"
                        "     Der C64 resettet danach selbst.")
KEYS_WRITE = ("  1. C64U: Menu-Taste (Freeze), Dateibrowser -> {file} -> Kontextmenue 'Write to Cartridge'.\n"
              "  2. F1 Scan, F3 Schreiben (Erase + Programmieren), 'Write complete' abwarten, F5 Verify -> 'verified OK'.\n"
              "  3. RUN/STOP. Der C64 resettet danach selbst.")
KEYS_VERIFY = ("  1. C64U: Menu-Taste (Freeze), Dateibrowser -> {file} -> Kontextmenue 'Write to Cartridge'.\n"
               "  2. NUR F1 (Scan) und F5 (Verify), NICHT F3. Es muss 'verified OK' kommen.\n"
               "  3. RUN/STOP. Der C64 resettet danach selbst.")


def app_snapshot(ctx: Ctx) -> dict:
    return ctx.status()


def app_post(ctx: Ctx, stage: str, label: str, st0: dict) -> None:
    """What every tool run must leave behind (T57 §5.4/§5.5), seen from outside: no fault, the write gate and the masks as
    before, the cartridge runs again after the reset the tool asked for."""
    time.sleep(6)
    st = ctx.status()
    ctx.rec(stage, "T57 §5.5", f"{label}: no fault, gate {int(st['wr_en'])} as before ({int(st0['wr_en'])}), masks "
            f"{st['ext']:04b}/{st['int_']:04b} as before", not st["fault"] and st["wr_en"] == st0["wr_en"]
            and (st["ext"], st["int_"]) == (st0["ext"], st0["int_"]), f"was {st0['ext']:04b}/{st0['int_']:04b}")


def st_app_dump(ctx: Ctx) -> None:
    c = ctx.cart
    b = ctx.board_ftp()
    name = f"t16-{c.key}-app"   # Alex types it: the C64 keyboard has no underscore
    st0 = app_snapshot(ctx)
    ctx.state["app_dump_file"] = b.path(name + ".crt")
    ctx.pause_for_alex(f"DUMP mit trex_crt ({name}):\n" + KEYS_DUMP.format(app=app_path(ctx), dir=b.dir, name=name)
                       + f"\n     Statuszeile muss zeigen: {c.title.split(' (')[0]}, {c.banks} Bank(s).", tag="dump", arg=name)
    app_post(ctx, "app_dump", "dump", st0)
    ok, d = wait_alive(ctx, 3.0, 10)
    ctx.rec("app_dump", "T57 §5.5", "cartridge boots again after the tool (reset on exit)", ok, d)
    ctx.rec("app_dump", "T57 §6", f"the dumper named the cart right ({c.title}, type {c.app_type}) and said 'Dump complete' (Alex)",
            ctx.ask("Hat der Dumper die richtige Karte gezeigt und ohne Fehler beendet?", tag="dumper"))
    crt = ctx.state.get("run_crt")
    data = b.download(name + ".crt") if crt is not None and not c.eeprom else None
    if data:
        _, banks = crt_banks(data)
        same = {k: sha(v) for k, v in banks.items()} == {k: sha(v) for k, v in crt_banks(crt)[1].items()}
        ctx.rec("app_dump", "T57 B1", "the app's dump file == the dump of this run (read back over FTP)", same, f"{len(banks)} banks")
    else:
        ctx.rec("app_dump", "T57 B1", "the app's dump file compared with ours", None,
                "FTP read-back stalls or not possible (GMod2: no EEPROM packet in our CRT); the writer's F5 verify below stands in")
    ctx.pause_for_alex(f"VERIFY der Dump-Datei ({name}), nichts wird geschrieben:\n" + KEYS_VERIFY.format(file=b.path(name + ".crt")), tag="verify", arg=name)
    app_post(ctx, "app_dump", "verify", st0)
    ctx.rec("app_dump", "T57 B1", "cart == the app's own dump (F5 verified OK, Alex)", ctx.ask("Hat F5 'verified OK' gemeldet?", tag="verified"))


def st_app_pref(ctx: Ctx) -> None:
    """B4: with preference Internal the port is not armed: the tool says so, reads nothing, changes nothing; Auto and a reset bring it back."""
    r = ctx.rest
    pref0 = r.cfg_get(CFG_PREF)
    try:
        r.cfg_set(CFG_PREF, "Internal")
        r.reset()
        time.sleep(4)
        st = ctx.status()
        ctx.rec("app_pref", "T57 §4.4", "preference Internal: port not armed", not st["armed"], f"ext {st['ext']:04b}")
        st0 = app_snapshot(ctx)
        ctx.pause_for_alex("PORT AUS (Preference Internal), Dumper starten:\n" + KEYS_APP.format(app=app_path(ctx)) +
                           "\n  2. Er muss ablehnen: Popup 'The port is not enabled ...' (oder 'No cartridge on the pins').\n"
                           "  3. Taste druecken, RUN/STOP. NICHT F1/F3.", tag="notarmed")
        ctx.rec("app_pref", "T57 U3", "the tool refuses with the 'port is not enabled' popup (Alex)",
                ctx.ask("Kam das Popup und wurde nichts gelesen?", tag="popup_pref"))
        app_post(ctx, "app_pref", "refused run", st0)
    finally:
        r.cfg_set(CFG_PREF, pref0)
        reset_into_cart(ctx)
    ctx.rec("app_pref", "T57 §4.4", "Auto and reset: armed again", ctx.status()["armed"], "")


# --- P4 ---------------------------------------------------------------------

def load_first_backup(ctx: Ctx) -> None:
    """No dump in this run: the FIRST backup (two identical passes when it was written, never overwritten) is the starting
    point. Alex: a cart's data does not change by itself, so it is not dumped again before every write; the compares
    after a write still dump."""
    c = ctx.cart
    first = ctx.out / "dump" / f"t16_backup_{c.key}.crt"
    if not first.exists():
        raise Abort(f"no verified backup: neither a dump in this run nor {first}")
    data = first.read_bytes()
    typ, banks = crt_banks(data)
    if typ != c.crt_type or sorted(banks) != list(range(c.banks)):
        raise Abort(f"{first.name} does not fit {c.title} (type {typ}, {len(banks)} banks)")
    full = sha(image_of(banks, c.banks))
    romh = len(banks[0]) > BANK_SZ
    ctx.state.update(backup_ok=True, backup=banks, backup_sha=full, romh=romh, run_crt=data, backup_crt=data,
                     restore_banks=banks, restore_sha=full, expect=banks, dirty=True)
    ctx.rec("backup", "T16 backup", "the FIRST backup is the starting point (no new dump)", True, f"{first.name} sha256 {full[:16]}")


def require_backup(ctx: Ctx) -> None:
    if not ctx.a.write:
        raise Abort("write stages need --write")
    if not (ctx.state.get("backup_ok") and ctx.state.get("backup")):
        load_first_backup(ctx)
    if not ctx.a.yes_write and not ctx.state.get("write_confirmed"):
        if not ctx.ask(f"Jetzt wird auf {ctx.cart.title} GESCHRIEBEN. Backup (2 gleiche Durchlaeufe) liegt: {ctx.state['backup_sha'][:12]}. "
                       "Wirklich schreiben?", default=False, tag="confirm_write"):
            raise Abort("write not confirmed")
        ctx.state["write_confirmed"] = True              # once per run, not once per write stage


def blank_sector(ctx: Ctx) -> tuple[int, list]:
    """First 64 KiB flash sector (groups of 8 banks, from bank 8 on: the MegaByter's low sectors are small
    boot sectors) that is all $FF in the verified backup. Returns (first bank, banks)."""
    c = ctx.cart
    bk = ctx.state["backup"]
    for s in range(c.banks // 8 - 1, 0, -1):
        banks = list(range(s * 8, s * 8 + 8))
        if all(set(bk[b]) == {0xFF} for b in banks):
            return banks[0], banks
    raise Abort("no sector in the backup is all $FF: nothing can be erased non-destructively; use the writer stage")


PATTERN = bytes([0xA5, 0x5A, 0xC3, 0x3C, 0x0F, 0xF0, 0x81, 0x7E, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0x01])


class FlashDrv:
    """Flash command cycles for the three flash carts over REST; reads (status, data) go through the
    cart lens, so they do not depend on $01 or the PLA. The command writes are plain DMA writes."""

    def __init__(self, ctx: Ctx):
        self.c, self.r, self.kind = ctx.cart, ctx.rest, ctx.cart.flash
        self.ctx = ctx
        self.chips = (0, 1) if self.kind == "ef" else (0,)

    def _rd(self, bank: int, off: int, n: int, chip: int = 0) -> bytes:
        c = self.c
        if self.kind == "gm":
            self.r.poke(0xDE00, 0x80 | bank)
        else:
            self.r.poke(0xDE00, bank)
        base = ROMH_A if chip else ROML                   # EF ROMH chip is read at $A000 by the lens
        return self.r.readmem(base + off, n, "cart")

    def enter(self) -> None:
        if self.kind == "ef":
            self.r.poke(0xDE00, 0)
            self.r.poke(0xDE02, 0x05)
        elif self.kind == "mb":
            self.r.poke(0xDE02, 0x03)

    def leave(self) -> None:
        if self.kind == "mb":
            self.r.poke(0xDE02, 0x00)
        self.r.poke(0xDE00, 0x00)

    def cyc(self, chip: int, bank: int, off: int, val: int) -> None:
        r = self.r
        if self.kind == "ef":
            r.poke(0xDE00, bank)
            r.poke((ROMH_E if chip else ROML) + off, val)
        elif self.kind == "mb":
            r.poke(0xDE00, bank)
            r.poke(ROML + off, val)
        else:                                              # gmod2: bank register in two steps, write via $E000
            r.poke(0xDE00, 0x80 | bank)
            r.poke(0xDE00, 0xC0 | bank)
            r.poke(0xE000 + off, val)

    def cmd(self, chip: int, bank: int, c: int) -> None:
        if self.kind == "ef":
            a1, a2, bk = 0x555, 0x2AA, 0
        elif self.kind == "mb":
            a1, a2, bk = 0xAAA, 0x555, bank
        else:
            self.cyc(0, 0x5555 >> 13, 0x5555 & 0x1FFF, 0xAA)
            self.cyc(0, 0x2AAA >> 13, 0x2AAA & 0x1FFF, 0x55)
            self.cyc(0, 0x5555 >> 13, 0x5555 & 0x1FFF, c)
            return
        self.cyc(chip, bk, a1, 0xAA)
        self.cyc(chip, bk, a2, 0x55)
        self.cyc(chip, bk, a1, c)

    def poll(self, chip: int, bank: int, off: int, want: int, timeout: float) -> bool:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            v = self._rd(bank, off, 1, chip)[0]
            if v == want:
                return True
            time.sleep(0.01)
        return False

    def program(self, chip: int, bank: int, off: int, val: int) -> bool:
        if val == 0xFF:
            return True
        self.cmd(chip, bank, 0xA0)
        if self.kind == "gm":
            self.cyc(0, bank, off, val)
        else:
            self.cyc(chip, bank, off, val)
        return self.poll(chip, bank, off, val, 1.0)

    def erase_sector(self, chip: int, bank: int) -> bool:
        self.cmd(chip, bank, 0x80)
        if self.kind == "gm":
            self.cyc(0, 0x5555 >> 13, 0x5555 & 0x1FFF, 0xAA)
            self.cyc(0, 0x2AAA >> 13, 0x2AAA & 0x1FFF, 0x55)
            self.cyc(0, bank, 0, 0x30)
        else:
            a1, a2, bk = (0x555, 0x2AA, 0) if self.kind == "ef" else (0xAAA, 0x555, bank)
            self.cyc(chip, bk, a1, 0xAA)
            self.cyc(chip, bk, a2, 0x55)
            self.cyc(chip, bank, 0, 0x30)
        return self.poll(chip, bank, 0, 0xFF, 60.0)

    def reset_chip(self, chip: int = 0, bank: int = 0) -> None:
        self.cyc(chip, bank, 0, 0xF0)

    def autoselect(self, chip: int = 0) -> tuple[int, int]:
        self.cmd(chip, 0, 0x90)
        idoff = 2 if self.kind == "mb" else 1
        m = self._rd(0, 0, 1, chip)[0]
        d = self._rd(0, idoff, 1, chip)[0]
        self.reset_chip(chip)
        return m, d


IDS = {"ef": (0x01, 0xA4), "mb": (0xC2, 0x58), "gm": (0x01, 0xA4)}


def st_write(ctx: Ctx) -> None:
    """P4, script part: probe, inhibit check, program + erase in a sector that is blank in both backups."""
    require_backup(ctx)
    c = ctx.cart
    if not c.flash:
        ctx.rec("write", "P4", "flash write test", None, "this cart has no flash")
        return
    first, banks = blank_sector(ctx)
    tb = banks[1]
    ctx.say(f"  test sector: banks {banks[0]}-{banks[-1]} (all $FF in both backups)")
    ctx.rest.pause()
    set_port(ctx)                                             # I/O visible whatever $01 the game left (T16 Q8)
    drv = FlashDrv(ctx)
    ids = IDS[c.flash]
    try:
        # 1. with ROM_WR_EN = 0 the autoselect command must NOT reach the chip (write inhibit, §6.1 gate 3)
        with Inhibit(ctx):
            drv.enter()
            m, d = drv.autoselect(0)
        idoff = 2 if c.flash == "mb" else 1
        plain = (ctx.state["backup"][0][0], ctx.state["backup"][0][idoff])
        ctx.rec("write", "§6.1", "ROM_WR_EN=0: command sequence does not reach the flash (reads plain ROM data)",
                (m, d) == plain, f"read ${m:02X}/${d:02X}, ROM has ${plain[0]:02X}/${plain[1]:02X}", hard=True)
        # 2. probe with ROM_WR_EN = 1: autoselect is a command that changes no bit
        with WrEn(ctx):
            drv.enter()
            m, d = drv.autoselect(0)
            ctx.rec("write", "P4", f"ROM_WR_EN=1: autoselect ID ${m:02X}/${d:02X}", (m, d) == ids,
                    f"expected ${ids[0]:02X}/${ids[1]:02X}", hard=True)
            if c.flash == "ef":
                m2, d2 = drv.autoselect(1)
                ctx.rec("write", "P4", f"EF ROMH chip autoselect ${m2:02X}/${d2:02X}", (m2, d2) == ids, hard=True)
            # 3. program the pattern into the blank sector
            for chip in drv.chips:
                for i, v in enumerate(PATTERN):
                    if not drv.program(chip, tb, 0x40 + i, v):
                        drv.reset_chip(chip, tb)
                        raise Abort(f"program chip {chip} bank {tb} +{0x40 + i:#x} timed out")
            ctx.rec("write", "P4", f"programmed {len(PATTERN)} bytes per chip in bank {tb}", True)
            drv.leave()
        # 4. verify by read-back through the lens, ROM_WR_EN already 0
        for chip in drv.chips:
            got = drv._rd(tb, 0x40, len(PATTERN), chip)
            ctx.rec("write", "P4", f"pattern reads back (chip {chip})", got == PATTERN, got.hex(), hard=True)
        # 5. sector erase restores the backup
        with WrEn(ctx):
            drv.enter()
            for chip in drv.chips:
                ok = drv.erase_sector(chip, banks[0])
                ctx.rec("write", "P4", f"sector erase chip {chip}", ok, hard=True)
            drv.leave()
        for chip in drv.chips:
            allff = all(set(drv._rd(b, 0, BANK_SZ, chip)) == {0xFF} for b in banks)
            ctx.rec("write", "P4", f"sector blank again == backup (chip {chip})", allff, hard=True)
    finally:
        try:
            ctx.rest.resume()
        except RestError:
            pass
    # the test left the game as it was (or crashed, tonight: $01 = $38 after the write test): reset it and wait
    # for the program before the dump, else the dump reads RAM and fails falsely
    alive, d = reset_and_wait(ctx)
    ctx.rec("write", "P4", "game runs again after the write test + reset", alive, d)
    # the whole cart must equal the backup again
    check_cart(ctx, "write", "P4", "full dump after the write test == backup", ctx.state["backup"], 7, hard=True)
    reset_into_cart(ctx)


# EEPROM (GMod2): M93C86 through $DE00 bits CS 6, CLK 5, DI 4, DO 7 (cart_base.c)
CS, CLK, DI, DO = 0x40, 0x20, 0x10, 0x80


class Eeprom:
    def __init__(self, ctx: Ctx):
        self.r = ctx.rest

    def _clock(self, bit: int) -> None:
        v = CS | (DI if bit else 0)
        self.r.poke(0xDE00, v)
        self.r.poke(0xDE00, v | CLK)
        self.r.poke(0xDE00, v)

    def _send(self, bits: int, n: int) -> None:
        for b in range(n - 1, -1, -1):
            self._clock((bits >> b) & 1)

    def read_word(self, addr: int) -> int:
        self.r.poke(0xDE00, CS)
        self._send((0x6 << 10) | (addr & 0x3FF), 13)
        w = 0
        for b in range(15, -1, -1):
            self.r.poke(0xDE00, CS)
            self.r.poke(0xDE00, CS | CLK)
            if self.r.readmem(0xDE00, 1)[0] & DO:
                w |= 1 << b
        self.r.poke(0xDE00, 0)
        return w

    def ewen(self, on: bool) -> None:
        self.r.poke(0xDE00, 0)
        self.r.poke(0xDE00, CS)
        self._send((1 << 12) | ((3 << 8) if on else 0), 13)
        self.r.poke(0xDE00, 0)

    def write_word(self, addr: int, w: int) -> bool:
        self.r.poke(0xDE00, 0)
        self.r.poke(0xDE00, CS)
        self._send((1 << 28) | (1 << 26) | ((addr & 0x3FF) << 16) | w, 29)
        self.r.poke(0xDE00, 0)
        self.r.poke(0xDE00, CS)
        end = time.monotonic() + 1.0
        ready = False
        while time.monotonic() < end:
            if self.r.readmem(0xDE00, 1)[0] & DO:
                ready = True
                break
            time.sleep(0.002)
        self.r.poke(0xDE00, 0)
        return ready


def st_eeprom(ctx: Ctx) -> None:
    """GMod2 EEPROM: read a few words twice, write a pattern, verify, write the originals back, verify."""
    require_backup(ctx)
    if not ctx.cart.eeprom:
        return
    start = ctx.a.eeprom_word
    n = ctx.a.eeprom_words
    test = [0xA55A, 0x5AA5, 0x1234, 0xFEDC][:n]
    ctx.say(f"  EEPROM words {start}..{start + n - 1} (bit-banged over REST, machine held by REST pause)")
    ctx.rest.pause()
    e = Eeprom(ctx)
    try:
        a = [e.read_word(start + i) for i in range(n)]
        b = [e.read_word(start + i) for i in range(n)]
        ctx.rec("write", "P4", "EEPROM words read twice, identical", a == b, " ".join(f"{w:04X}" for w in a), hard=True)
        (ctx.out / "eeprom_backup.json").write_text(json.dumps(dict(start=start, words=a)))
        try:
            e.ewen(True)
            for i, w in enumerate(test):
                ctx.rec("write", "P4", f"EEPROM write word {start + i}", e.write_word(start + i, w), hard=True)
            e.ewen(False)
            got = [e.read_word(start + i) for i in range(n)]
            ctx.rec("write", "P4", "EEPROM pattern reads back", got == test, " ".join(f"{w:04X}" for w in got), hard=True)
        finally:
            # restore the originals whatever happened above
            e.ewen(True)
            ok = all(e.write_word(start + i, a[i]) for i in range(n))
            e.ewen(False)
        back = [e.read_word(start + i) for i in range(n)]
        ctx.rec("write", "P4", "EEPROM originals restored and verified", ok and back == a, " ".join(f"{w:04X}" for w in back), hard=True)
    finally:
        ctx.rest.poke(0xDE00, 0)
        ctx.rest.resume()
    reset_into_cart(ctx)


def make_crts(ctx: Ctx):
    """(test CRT, backup CRT, {bank: data} of the test image, uploaded file names). The test image is the backup with a
    pattern in a bank of the blank sector. GMod2: the app's own dump file is both (the CRT we can build has no EEPROM
    packet and a fetch of the app's file stalls), so a write is a rewrite of what is there: the chip is erased and
    programmed in between, and the verify proves it."""
    c = ctx.cart
    b = ctx.board_ftp()
    bk = dict(ctx.state["backup"])
    tn, bn = f"t16_test_{c.key}.crt", f"t16_backup_{c.key}.crt"
    if c.eeprom:
        if not ctx.state.get("app_dump_file"):
            raise Abort("GMod2 write rounds use the app's own dump as the image: run the app_dump stage first")
        name = ctx.state["app_dump_file"]
        return None, None, bk, (name, name)
    # The app's writer erases and programs the whole cart, so the pattern only needs a bank that is $FF (Alex: "im .crt
    # sehen welche Banks frei sind, ein Sektor weit hinten"); a whole blank sector is only needed by the script's own
    # sector write test (stage write).
    free = [b for b in range(c.banks - 1, 0, -1) if set(bk[b]) == {0xFF}]
    if not free:
        raise Abort("no bank in the backup is all $FF: no place for the test pattern")
    test = dict(bk)
    banks = [free[0] - 1, free[0]]
    d = bytearray(test[banks[1]])
    d[0x40:0x40 + len(PATTERN)] = PATTERN
    if ctx.state["romh"]:
        d[BANK_SZ + 0x40:BANK_SZ + 0x40 + len(PATTERN)] = PATTERN[::-1]
    test[banks[1]] = bytes(d)
    t_crt = build_crt(c, test, f"T16 {c.key} test", ctx.state["romh"])
    b_crt = ctx.state["backup_crt"]                           # the FIRST backup (or this run's dump where there is none)
    (ctx.out / tn).write_bytes(t_crt)
    b.upload(tn, t_crt)
    b.upload(bn, b_crt)
    return t_crt, b_crt, test, (b.path(tn), b.path(bn))


def st_writer(ctx: Ctx) -> None:
    """P4, on-device part with Alex's keys : trex_crt's writer writes a test CRT and
    then the backup CRT. Needs no preference trick any more (T57): the port armed, the gate is on."""
    require_backup(ctx)
    c = ctx.cart
    if not c.flash:
        return
    if c.eeprom:
        ctx.rec("writer", "P4", "manual writer rounds", None, "GMod2: use app_write (the app's own dump is its image)")
        return
    t_crt, b_crt, test, (tp, bp) = make_crts(ctx)
    rb = ctx.state["restore_banks"]
    ctx.say(f"  hochgeladen: {tp} und {bp}")
    st = ctx.status()
    ctx.rec("writer", "P4", "port armed", st["armed"], f"ext {st['ext']:04b}", hard=True)
    for round_, (name, want, what) in enumerate(((tp, test, "TEST-CRT"), (bp, rb, "BACKUP-CRT")), 1):
        ctx.say(f"\n  Runde {round_}: {what} schreiben")
        ctx.pause_for_alex(
            f"{what} SCHREIBEN ({name}):\n" + KEYS_WRITE.format(file=name), tag="writefile", arg=name)
        reset_into_cart(ctx)
        same = check_cart(ctx, "writer", "P4", f"dump after the {what} write equals the written image", want, round_,
                          hard=(round_ == 2))
        if not same and round_ == 1:
            ctx.say("  TEST-Schreiben fehlgeschlagen. Jetzt das Backup zurueckschreiben ist der naechste und einzige sichere Schritt.")
    ctx.rec("writer", "P4", "cart restored to the first backup (sha256)", ctx.state["verified_sha"] == ctx.state["restore_sha"],
            ctx.state["restore_sha"][:16])
    reset_into_cart(ctx)


def st_app_write(ctx: Ctx) -> None:
    """B6/B7/B9: trex_crt's writer (the gate is 1 before and after: the app puts it back to what it found), driven by Alex's keys. Test CRT, then the backup; every round ends with a dump of the whole cart compared to
    the written image. GMod2: the app's own dump file is the image (a rewrite of what is there, EEPROM included)."""
    require_backup(ctx)
    c = ctx.cart
    if not c.flash:
        ctx.rec("app_write", "T57", "write test", None, "this cart has no flash")
        return
    t_crt, b_crt, test, (tp, bp) = make_crts(ctx)
    rounds = (((tp, test, "TEST"), (bp, ctx.state["restore_banks"], "BACKUP")) if t_crt is not None
              else ((tp, ctx.state["backup"], "REWRITE"),))
    wr_before = ctx.status()["wr_en"]
    ctx.rec("app_write", "T57 B6/B7", f"gate {int(wr_before)} before the write", wr_before, "")
    for name, want, what in rounds:
        st0 = app_snapshot(ctx)
        ctx.pause_for_alex(f"{what}-CRT SCHREIBEN ({name}):\n" + KEYS_WRITE.format(file=name), tag="write", arg=name)
        app_post(ctx, "app_write", what, st0)
        ctx.rec("app_write", "T57 B9", f"{what}: written and 'verified OK' by the app (Alex)",
                ctx.ask("Schreiben fertig und F5 meldet 'verified OK' (kein Fehler-Popup)?", tag="verified"))
        ok, d = wait_alive(ctx, 2.0, 8)
        ctx.rec("app_write", "T57 B6/B7", f"{what}: gate back at 1 after the app's write", ctx.status()["wr_en"],
                f"bit {int(ctx.status()['wr_en'])}")
        check_cart(ctx, "app_write", "T57 B9", f"{what}: dump of the whole cart equals the written image", want, 11,
                   hard=(what != "TEST"))
    ok = ctx.state["verified_sha"] == ctx.state["restore_sha"] if t_crt is not None else ctx.state["verified_sha"] == ctx.state["backup_sha"]
    ctx.rec("app_write", "T57", "cart equals the first backup at the end (last full dump)", ok,
            f"{ctx.state['verified_sha'][:16]} vs {ctx.state['restore_sha'][:16]}")


def st_app_err(ctx: Ctx) -> None:
    """B10: a CRT without a single chip must be refused and nothing may be erased. Two behaviours are accepted: the app that
    validates when the file is opened (popup, the writer screen never appears: branch t57-validate-open) and the older one
    (writer screen with a message). Either way the gate comes back as it was and the cart equals what it held before. After
    the popup the check is sampled (the writer never ran); after the old behaviour it is a full dump (F1/F3 were reachable).
    --app-validates-open turns the old behaviour into a FAIL. With it no backup is needed (Alex: the popup comes before
    anything could write, F3 is never reached), and the cart compare runs only when this run made a backup."""
    if not ctx.a.app_validates_open:
        require_backup(ctx)
    c = ctx.cart
    if not c.flash:
        return
    b = ctx.board_ftp()
    hdr = bytearray(b"C64 CARTRIDGE   ")
    hdr += struct.pack(">IHHBBB", 0x40, 0x0100, c.crt_type, c.hdr[0], c.hdr[1], 0) + bytes(5)
    hdr += b"T16 empty".ljust(32, b"\0")
    name = f"t16_bad_{c.key}.crt"
    b.upload(name, bytes(hdr))
    st0 = app_snapshot(ctx)
    ctx.pause_for_alex(
        f"LEERE CRT oeffnen ({name}, keine Chips, muss abgelehnt werden):\n"
        f"  1. C64U: Menu-Taste (Freeze), Dateibrowser -> {b.path(name)} -> 'Write to Cartridge'.\n"
        "  2. Neu: Popup gleich beim Oeffnen, kein Schreib-Bildschirm. Alt: Schreib-Bildschirm mit Fehlermeldung,\n"
        "     dort NUR F1 druecken, NICHT F3.\n"
        "  3. RUN/STOP.", tag="badcrt", arg=name)
    app_post(ctx, "app_err", "rejected CRT", st0)
    refused = ctx.ask("Wurde die leere CRT abgelehnt (Popup oder Fehlermeldung), ohne dass etwas geschrieben wurde?", tag="refused")
    ctx.rec("app_err", "T57 B10", "rejected with a message (Alex)", refused)
    popup = ctx.ask("War es ein Popup gleich beim Oeffnen, bevor ein Schreib-Bildschirm erschien? (n = alter Schreib-Bildschirm)",
                    tag="popup")
    if popup:
        ctx.rec("app_err", "T57 B10", "the app validates the CRT when it is opened (popup, writer screen never reached)", True)
    else:
        ctx.rec("app_err", "T57 B10", "app without validate-on-open: refused inside the writer screen",
                False if ctx.a.app_validates_open else None,
                "--app-validates-open: the popup is required" if ctx.a.app_validates_open else "older trex_crt, accepted")
    exp = ctx.state.get("expect") or ctx.state.get("backup")
    if exp is None:
        ctx.rec("app_err", "T57 B10", "cart compare", None, "no backup in this run (--app-validates-open): not compared")
        return
    check_cart(ctx, "app_err", "T57 B10", "cart still equals what it held before (nothing erased)", exp, 12, hard=True, sample=popup)


def app_dump_sha(ctx: Ctx, stage: str, name: str) -> str | None:
    """Alex dumps the cartridge with trex_crt into the board directory; the file read back over FTP (None when that stalls)."""
    b = ctx.board_ftp()
    st0 = app_snapshot(ctx)
    ctx.pause_for_alex(f"DUMP mit trex_crt ({name}):\n" + KEYS_DUMP.format(app=app_path(ctx), dir=b.dir, name=name), tag="dump", arg=name)
    app_post(ctx, stage, f"dump {name}", st0)
    data = b.download(name + ".crt")
    return sha(data) if data else None


def st_ingame(ctx: Ctx) -> None:
    """In-game save: the write gate is 1 for the game (the firmware keeps it on, T57 Decision 2026-10-05), Alex plays and saves, then the dump shows what changed. EF: the EAPI writes the flash; GMod2: the EEPROM
    (the app's dump file hashes before and after); MegaByter: flash, if the game saves at all."""
    require_backup(ctx)
    c = ctx.cart
    if not c.flash:
        return
    st = ctx.status()
    ctx.rec("ingame", "T57 D2026-10-05", "write gate is 1 for the running game", bool(st["wr_en"]),
            "" if st["wr_en"] else "the game's save cannot reach a flash chip")
    before_sha = None
    if c.flash == "gm":
        before_sha = app_dump_sha(ctx, "ingame", "t16-gmod2-before")
    ctx.pause_for_alex("IM SPIEL SPEICHERN: " + c.ingame + ".\n  Danach auf einem stabilen Bild bleiben.", tag="ingame")
    ctx.state["dirty"] = True
    reset_into_cart(ctx)
    if c.flash == "gm":
        after = app_dump_sha(ctx, "ingame", "t16-gmod2-after")
        if after:
            (ctx.out / "after_save.sha256").write_text(str(after))
        if before_sha and after:
            ctx.rec("ingame", "P4", "the in-game save changed the cartridge (app dump sha256 differs)", after != before_sha,
                    f"{str(before_sha)[:12]} -> {str(after)[:12]}")
        else:
            ctx.rec("ingame", "P4", "the in-game save changed the cartridge", None,
                    "the dump files cannot be read back over FTP; compare t16-gmod2-before.crt and t16-gmod2-after.crt by hand")
        ctx.say("  Power cycle and the persist stage compare the app dump again.")
        return
    after_banks = dump_pass(ctx, bank_order(c, 9), ctx.state["romh"])
    diff = [b for b in range(c.banks) if after_banks[b] != ctx.state["backup"][b]]
    (ctx.out / "after_save.sha256").write_text(sha(image_of(after_banks, c.banks)))
    ctx.state["after_save"] = after_banks
    ctx.rec("ingame", "P4", "the in-game save changed the flash (banks differ from the backup)", bool(diff) if c.flash == "ef" else None,
            f"banks {diff[:16]}" + ("" if diff else " (no save made?)"))


def st_powercycle(ctx: Ctx) -> None:
    """Alex powers the C64U off and on. With --sram the flash holds another core: bitstream and app go into SRAM again
    (load.sh + trxdbg boot, needs the JTAG handle free). Without it our core must come back by itself. The JTAG handle is
    closed for the time and reopened in every case; if that fails the later stages are skipped, not run on a dead handle."""
    sram = getattr(ctx.a, "sram", False)
    ctx.pause_for_alex("POWER-CYCLE:\n  1. C64U AUS, 5 Sekunden warten, wieder EIN (Cart bleibt stecken).\n"
                       + ("  2. Core + App holt das Skript selbst per JTAG, nichts tun.\n" if sram else "")
                       + "  Wenn der eigene Boot-Bildschirm der C64U da ist:", tag="power")
    old = ctx.jtag
    if old:
        old.close()
    reopened = False
    try:
        if sram:
            ns = argparse.Namespace(bit=ctx.a.bit, app=ctx.a.app, host=ctx.rest.host, dry_run=ctx.a.dry_run, version=ctx.a.version)
            rc = cmd_boot(ns)
            ctx.rec("powercycle", "-", "bitstream and app loaded again", rc == 0, f"rc {rc}", hard=True)
        wait_rest(ctx)
    finally:
        try:
            ctx.jtag = old.reopen() if ctx.a.dry_run else Jtag()      # every later stage needs it, also after a failed boot
            reopened = True
        except Exception as e:  # noqa: BLE001
            ctx.state["jtag_lost"] = f"{type(e).__name__}: {e}"
    if not reopened:
        raise Abort("JTAG nach dem Neustart nicht wieder zu oeffnen (Blaster-USB noch dran?): " + ctx.state["jtag_lost"])
    st = ctx.status()
    ctx.rec("powercycle", "-", "our core runs after the power cycle (SLOT_STATUS b31)", st["ours"], hex(ctx.jtag.status()))
    if not st["ours"]:
        raise Abort("nach dem Neustart laeuft nicht unser Core" + ("" if sram else " (Flash hat einen anderen Core: Lauf mit --sram wiederholen)"))
    time.sleep(5)


def st_persist(ctx: Ctx) -> None:
    """After the power cycle: what the in-game save wrote is still in the cartridge."""
    c = ctx.cart
    f = ctx.out / "after_save.sha256"
    if not f.exists():
        ctx.rec("persist", "P4", "persistence of the in-game save", None, "no after_save.sha256 from an ingame stage")
        return
    want = f.read_text().strip()
    if c.flash == "gm":
        got = app_dump_sha(ctx, "persist", "t16-gmod2-persist")
        ctx.rec("persist", "P4", "the saved state survived the power cycle (app dump sha256)", None if not got else got == want,
                f"{str(got)[:12]} vs {want[:12]}" if got else "not read back over FTP: compare t16_gmod2_persist.crt with t16_gmod2_after_save.crt")
        return
    banks = dump_pass(ctx, bank_order(c, 13), ctx.state.get("romh", c.romh != "no"))
    got = sha(image_of(banks, c.banks))
    ctx.rec("persist", "P4", "the saved state survived the power cycle (dump sha256)", got == want, f"{got[:12]} vs {want[:12]}")


STAGES = {"pre": st_pre, "boot": st_boot, "dump": st_dump, "soak": st_soak, "pause100": st_pause100,
          "matrix": st_matrix, "internal": st_internal, "port": st_port, "gate": st_gate,
          "app_up": st_app_up, "app_dump": st_app_dump, "app_pref": st_app_pref, "write": st_write, "eeprom": st_eeprom, "writer": st_writer,
          "app_write": st_app_write, "app_err": st_app_err, "ingame": st_ingame, "powercycle": st_powercycle,
          "persist": st_persist}
DEFAULT = "pre,boot,dump,soak,pause100,matrix,internal"
WRITE_STAGES = {"write", "eeprom", "writer", "app_write", "app_err", "ingame", "persist"}

# The plan of the final session per cart (`--stages full`), with what each costs; minutes are estimates (REST request
# time on the port is measured by `pre`, the dumps dominate).
FULL = {
    "rom16": "pre,boot,port,dump,app_up,app_dump,app_pref,soak,pause100,matrix,internal",
    "ef1": "pre,boot,gate,port,dump,app_up,app_dump,app_pref,write,app_write,app_err,ingame,powercycle,persist,"
           "soak,pause100,matrix,internal",
    "megabyter": "pre,boot,gate,port,dump,app_up,app_dump,app_pref,write,app_write,app_err,ingame,soak,pause100,matrix",
    "gmod2": "pre,boot,gate,port,dump,app_up,app_dump,app_pref,write,eeprom,app_write,app_err,ingame,powercycle,persist,"
             "soak,pause100,matrix",
}
MINUTES = {"pre": 1, "boot": 2, "port": 1, "gate": 1, "dump": 12, "app_up": 1, "app_dump": 6, "app_pref": 3,
           "write": 4, "eeprom": 3, "app_write": 12, "app_err": 2, "ingame": 4, "powercycle": 3, "persist": 4,
           "soak": 2, "pause100": 3, "matrix": 8, "internal": 3, "writer": 4}


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def fetch_app(dest: Path, version: str = "main-latest") -> Path:
    cfg = Path.home() / ".config/tea/config.yml"
    token = os.environ.get("GITEA_TOKEN", "")
    if not token and cfg.exists():
        for line in cfg.read_text().splitlines():
            if "token:" in line:
                token = line.split("token:")[1].strip()
                break
    url = os.environ.get("GITEA_URL", "http://gitea.example") + "/api/packages/TREX/generic/" + os.environ["APP_PKG"] + "/" + os.environ.get("APP_VERSION", version) + "/ultimate.app"
    r = urllib.request.Request(url, headers={"Authorization": f"token {token}"})
    with urllib.request.urlopen(r, timeout=60) as f:
        dest.write_bytes(f.read())
    return dest


def cmd_boot(args) -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    if args.bit:
        load = [str(ROOT / "tools/load.sh"), args.bit]
    else:
        load = [str(ROOT / "tools/load.sh"), "c64_basic", args.version]
    if args.dry_run:
        print("dry run: would run", " ".join(load), "then trxdbg boot staged", args.app or f"<{args.version} ultimate.app>")
        return 0
    app = Path(args.app) if args.app else fetch_app(OUT / "ultimate.app", args.version)
    print("app:", app, sha(app.read_bytes())[:16])
    if subprocess.run(["pgrep", "-f", "lorenz.py"], capture_output=True).returncode == 0:
        print("tests/lorenz.py holds the board and JTAG", file=sys.stderr)
        return 2
    if subprocess.run(["nice", "-n", "15", *load]).returncode:
        return 1
    time.sleep(2)
    if subprocess.run([str(ROOT / "tools/trxdbg.sh"), "boot", "staged", str(app)]).returncode:
        return 1
    rest = Rest(args.host)
    end = time.monotonic() + 120
    while time.monotonic() < end:
        try:
            print("REST:", rest.version())
            # A JTAG-loaded app runs with the default config: the built-in speaker is on and
            # loud. Off for the session (runtime only, not saved).
            try:
                urllib.request.urlopen(urllib.request.Request(
                    f"http://{args.host}/v1/configs/Speaker%20Mixer/Speaker%20Enable?value=Disabled",
                    method="PUT"), timeout=5).read()
            except OSError:
                pass
            break
        except RestError:
            time.sleep(3)
    else:
        print("REST does not answer after the app boot", file=sys.stderr)
        return 1
    j = Jtag()
    try:
        st = decode_status(j.status())
    finally:
        j.close()
    print("slot status:", st)
    return 0 if st["ours"] else 1


def cmd_stress(args) -> int:
    """No cartridge needed: the lens read of an EMULATED cartridge (a CRT from the board's drive) asks the lens unit
    (LK_REQ/LK_RES, the IO block's byte protocol) once per byte, as a real-port read does twice, while the firmware's
    TRX Ring task polls the same block every 100 ms. T16 §13.11 item 4: with the old trx_sys.v the reads come back with
    torn bytes ($00 where the done flag was read from another register); with the new one never. Prints the count."""
    rest = Rest(args.host)
    rest.run_crt(args.crt)
    time.sleep(args.settle)
    n = args.bytes
    rest.pause()
    try:
        votes = [rest.readmem(0x8000, n, "cart") for _ in range(3)]
        ref = bytes(a if a == b or a == c else b for a, b, c in zip(*votes))
        bad, zeros, total, t0 = [], 0, 0, time.time()
        for i in range(args.rounds):
            d = rest.readmem(0x8000, n, "cart")
            total += len(d)
            for k, (x, y) in enumerate(zip(d, ref)):
                if x != y:
                    bad.append((i, k, x, y))
                    zeros += x == 0
    finally:
        rest.resume()
    dt = time.time() - t0
    print(f"{args.rounds} reads of {n} bytes through the lens of the emulated cartridge ({dt:.0f} s, {total / dt / 1024:.0f} KB/s): "
          f"{len(bad)} bytes differ from the 3-read majority, {zeros} of them $00; first {bad[:6]}")
    return 1 if bad else 0


def cmd_status(args) -> int:
    j = Jtag()
    try:
        v = j.status()
        print(f"SLOT_STATUS 0x{v:08X}", decode_status(v))
        print("SLOT_CTRL 0x%08X" % j.ctrl())
    finally:
        j.close()
    return 0


def cmd_mkbaseline(args) -> int:
    dl = Path.home() / "Downloads"
    sources = {"ef1": ["timoNew.crt", "timo-rewwrite.crt", "timo-ef-type32.crt"],
               "megabyter": ["lykianew.crt", "lykiaptv.crt"],
               "rom16": ["canabalt.crt", "C64anabalt-header-fixed.crt"]}
    out = {}
    for key, files in sources.items():
        sets = []
        for f in files:
            p = dl / f
            if p.exists():
                typ, banks = crt_banks(p.read_bytes())
                sets.append((f, typ, banks, time.strftime("%Y-%m-%d", time.localtime(p.stat().st_mtime))))
        if not sets:
            continue
        ref = sets[0]
        agree = [s[0] for s in sets if {b: sha(d) for b, d in s[2].items()} == {b: sha(d) for b, d in ref[2].items()}]
        out[key] = dict(source=f"~/Downloads/{ref[0]}", identical_copies=agree, crt_type=ref[1],
                        file_date=ref[3], banks=len(ref[2]),
                        bank_sha256={str(b): sha(d) for b, d in ref[2].items()},
                        full_sha256=sha(b"".join(ref[2][b] for b in sorted(ref[2]))),
                        provenance="unverified: files from the 2026-03 dumper/writer work on earlier hardware (Q9); "
                                   "not the output of a documented Gideon-bitstream run")
        print(key, ref[0], "banks", len(ref[2]), "identical copies:", agree)
    BASELINES.write_text(json.dumps(out, indent=1) + "\n")
    print("wrote", BASELINES)
    return 0


def gideon_setup(ctx: Ctx):
    """On Gideon's firmware the only read path is REST's DMA read, which reaches the port only with the
    external ROM/IO1 selected: Manual + Bus Sharing External (runtime only), reset so it takes effect."""
    pref0 = ctx.rest.cfg_get(CFG_PREF)
    share0 = {i: ctx.rest.cfg_get(i) for i in CFG_SHARING}
    _set_pref(ctx, "Manual", "External")
    ctx.rest.reset()
    time.sleep(4)

    def restore():
        _set_pref(ctx, pref0)
        for i, v in share0.items():
            ctx.rest.cfg_set(i, v)
        ctx.rest.reset()
    return restore


NEEDS_WRITE = {"write", "eeprom", "writer", "app_write", "app_err", "ingame"}


def expand_stages(args, cart: Cart) -> list:
    spec = args.stages or DEFAULT
    out = []
    for part in spec.split(","):
        if part == "full":
            out += FULL[cart.key].split(",")
        elif part:
            out.append(part)
    if "full" in spec.split(",") and not args.write:
        out = [x for x in out if x not in NEEDS_WRITE and x not in ("powercycle", "persist")]
    return out


def cmd_plan(args) -> int:
    """What the final session does, cart by cart, and how long it takes."""
    total = 0
    print("Final cart session (TRX64-Ultimate/docs/specs/T16-board-session.md). Alex: power, plug, answer a few questions, play for the in-game saves.\n")
    for key in ("rom16", "ef1", "megabyter", "gmod2"):
        st = FULL[key].split(",")
        mins = sum(MINUTES.get(x, 2) for x in st) + 3
        total += mins
        cart = CARTS[key]
        part = ["a few yes/no questions about the picture"]
        if "app_dump" in st:
            part.append("trex_crt by its keys (dump, verify" + (", writes" if "app_write" in st else "") + "), the script prints them")
        if "ingame" in st:
            part.append("one in-game save")
        if "powercycle" in st:
            part.append("one power cycle")
        print(f"{key:10s} {cart.title}\n  stages: {','.join(st)}\n  about {mins} min incl. 3 min to swap the cart; Alex's part: {', '.join(part)}")
    print(f"\nabout {total / 60:.1f} h in all; `t16_session.sh all --write` runs it, asking once for the write confirmation.")
    return 0


def cmd_all(args) -> int:
    """Every cart in turn: Alex swaps the cart and powers on, the bitstream and the app are loaded, the full plan runs."""
    rc = 0
    if args.write and not args.yes_write:
        try:
            got = input("Das schreibt auf die Flash-Carts (Erase + Programmieren, Spiel-Speichern, EEPROM-Testworte). Backups werden "
                        "vorher gemacht und nach jedem Test zurueckgeschrieben. Wirklich? [j/N] ").strip().lower()
        except EOFError:
            got = ""
        if not got.startswith("j"):
            print("nicht bestaetigt")
            return 1
    results = {}
    for key in ("rom16", "ef1", "megabyter", "gmod2"):
        if args.only and key not in args.only.split(","):
            continue
        print(f"\n################ {CARTS[key].title}")
        if not args.yes:
            try:
                input(f">>> C64U AUS, {CARTS[key].title} in den Expansionsport stecken, C64U EIN. Wenn der eigene Boot-Bildschirm der C64U "
                      "da ist: Enter ")
            except EOFError:
                print("keine Eingabe moeglich")
                return 1
        sub = argparse.Namespace(**vars(args))
        sub.cmd = "boot"
        if cmd_boot(sub):
            results[key] = "boot failed"
            rc = 1
            if args.stop_on_fail:
                break
            continue
        sub = argparse.Namespace(**vars(args))
        sub.cmd, sub.cart, sub.stages, sub.yes_write = "run", key, "full", args.write
        sub.sram = True                     # `all` loads our core into SRAM for every cart
        r = cmd_run(sub)
        results[key] = "ok" if r == 0 else f"rc {r}"
        rc = rc or r
        if r and args.stop_on_fail:
            break
    print("\n=== all carts:", results)
    return rc


def cmd_run(args) -> int:
    cart = CARTS[args.cart]
    outdir = OUT / ("dryrun" if args.dry_run else "") / cart.key
    outdir.mkdir(parents=True, exist_ok=True)
    if args.gideon:
        args.stages = args.stages or "dump"
    stages = expand_stages(args, cart)
    bad = [s for s in stages if s not in STAGES]
    if bad:
        print("unknown stage:", bad, "; known:", list(STAGES))
        return 2
    if args.dry_run:
        sys.path.insert(0, str(HERE))
        from t16_sim import SimBoard
        sim = SimBoard(cart, seed=args.sim_seed, faults=args.sim_fault)
        sim.install_clock(sys.modules[__name__])
        rest, jtag = sim.rest, sim.jtag
    else:
        rest = Rest(args.host)
        try:
            jtag = None if args.gideon else Jtag()
        except Exception as e:  # noqa: BLE001
            print(f"JTAG nicht zu oeffnen ({type(e).__name__}: {e}).\nBlaster-USB dran? Laeuft noch ein anderer Prozess auf dem Board "
                  "(pgrep -fl 'load.sh|trxdbg|_board.py|t16_session')? Venv: tests/cart/t16_session.sh benutzen.", file=sys.stderr)
            return 2
    if args.gideon:
        args.read_path = "plain"
        args.stages = args.stages or "dump"
    if args.read_path == "both":
        pass
    ctx = Ctx(args, cart, rest, jtag, outdir)
    if args.dry_run:
        ctx.board, ctx.hook, ctx.ask_hook = sim.ftp, sim.alex, sim.alex_ask

    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)
    TERMINATE["now"] = False
    rc = 0
    restore = lambda: None  # noqa: E731
    print(f"=== T16 session: {cart.title}  stages {','.join(stages)}  {'DRY RUN (simulated cart)' if args.dry_run else HOST}")
    try:
        try:
            ctx.state["pref"] = rest.cfg_get(CFG_PREF)
        except Exception:  # noqa: BLE001
            ctx.state["pref"] = "?"
        if args.gideon:
            restore = gideon_setup(ctx)
        elif jtag:
            ctx.state["wr0"] = decode_status(jtag.status())["wr_en"]       # what the firmware left at the start
        rc = run_stages(ctx, stages) or rc
    except KeyboardInterrupt:
        print("\n!!! Lauf beendet (Ctrl-C/SIGTERM); der Bericht wird noch geschrieben.", file=sys.stderr)
        rc = 130
    except Exception as e:  # noqa: BLE001 - outside a stage: still a FAIL line and the report, no traceback
        print(f"\n!!! Fehler ausserhalb der Stages: {type(e).__name__}: {e}", file=sys.stderr)
        ctx.rec("run", "-", "error outside the stages", False, f"{type(e).__name__}: {e}")
        rc = 1
    finally:
        _safety_exit()
        if args.gideon:
            try:
                restore()
            except Exception as e:  # noqa: BLE001
                print("could not restore the preference:", e)
        jt = ctx.jtag
        try:
            if jt and not ctx.state.get("jtag_lost"):
                st = decode_status(jt.status())
                wr0 = ctx.state.get("wr0", False)
                ctx.rec("end", "§6.1", f"ROM_WR_EN is back at its start value ({int(wr0)}) at the end",
                        st["wr_en"] == wr0, f"now {int(st['wr_en'])}")
                if st["wr_en"] != wr0:
                    rc = 1
        except BaseException as e:  # noqa: BLE001
            print("could not read the final status:", e)
            ctx.rec("end", "§6.1", "final ROM_WR_EN read", False, f"{type(e).__name__}: {e}")
        report = outdir / f"report-{time.strftime('%Y%m%d-%H%M%S')}.json"
        try:
            report.write_text(json.dumps(dict(cart=cart.key, host=args.host, stages=stages, results=ctx.results,
                                              matrix=ctx.state.get("matrix"), backup_sha=ctx.state.get("backup_sha"),
                                              rest_calls=getattr(rest, "calls", None)), indent=1))
        except Exception as e:  # noqa: BLE001
            print("Bericht nicht schreibbar:", e, file=sys.stderr)
        if jt and not args.dry_run:
            try:
                jt.close()
            except Exception:  # noqa: BLE001
                pass
    n = {t: sum(1 for r in ctx.results if r["result"] == t) for t in ("PASS", "WARN", "FAIL")}
    for r in ctx.results:
        if r["result"] != "PASS":
            print(f"  {r['result']:4s} {r['stage']:10s} {r['name']}" + (f": {r['detail']}" if r["detail"] else ""))
    print(f"\n=== {cart.key}: {n['PASS']} pass, {n['WARN']} warn, {n['FAIL']} fail   report {report}")
    return rc if rc == 130 else (1 if n["FAIL"] else rc)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--dry-run", action="store_true", help="simulated cart and board (offline)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("boot")
    p.add_argument("--bit")
    p.add_argument("--app")
    p.add_argument("--version", default=os.environ.get("T16_VERSION", "main-latest"),
                   help="package version of bitstream and app on Gitea (<branch>-latest)")
    sub.add_parser("status")
    sub.add_parser("plan")
    st = sub.add_parser("stress", help="no cartridge: lens reads of an emulated cartridge, counts torn bytes (T16 §13.11 item 4)")
    st.add_argument("--crt", default="/USB1/jupiter_fracture_(PAL).crt")
    st.add_argument("--rounds", type=int, default=40)
    st.add_argument("--bytes", type=int, default=0x1000)
    st.add_argument("--settle", type=float, default=8.0)
    al = sub.add_parser("all", help="every cart in turn")
    al.add_argument("--write", action="store_true")
    al.add_argument("--yes-write", action="store_true")
    al.add_argument("--yes", action="store_true")
    al.add_argument("--only", help="comma separated carts")
    al.add_argument("--stop-on-fail", action="store_true")
    al.add_argument("--bit")
    al.add_argument("--app")
    al.add_argument("--version", default=os.environ.get("T16_VERSION", "main-latest"))
    al.add_argument("--app-u2a")
    al.add_argument("--quick", action="store_true")
    al.add_argument("--soak", type=int)
    al.add_argument("--chunk", type=int, default=0x1000)
    al.add_argument("--read-path", choices=("both", "lens", "plain", "lens3"), default="both")
    al.add_argument("--matrix-secs", default="0.1,1,10")
    al.add_argument("--gideon", action="store_true", default=False)
    al.add_argument("--romh", default=None)
    al.add_argument("--baseline-crt")
    al.add_argument("--board-dir", default="")
    al.add_argument("--eeprom-word", type=int, default=1000)
    al.add_argument("--eeprom-words", type=int, default=4)
    al.add_argument("--app-validates-open", action="store_true")
    al.add_argument("--sim-seed", type=int, default=1)
    al.add_argument("--sim-fault", default="")
    sub.add_parser("mkbaseline")
    sub.add_parser("selftest")
    r = sub.add_parser("run")
    r.add_argument("--cart", choices=sorted(CARTS), required=True)
    r.add_argument("--stages", help=f"default {DEFAULT}; also port,gate,app_up,app_dump,app_pref (read-only), "
                                    "write,eeprom,writer,app_write,app_err,ingame,powercycle,persist (need --write); `full` = the plan of the final session")
    r.add_argument("--write", action="store_true", help="allow the write stages")
    r.add_argument("--yes-write", action="store_true", help="skip the typed confirmation (still needs --write)")
    r.add_argument("--yes", action="store_true", help="no interactive questions (picture checks count as OK)")
    r.add_argument("--quick", action="store_true")
    r.add_argument("--sram", action="store_true", help="our core is not in the flash: the power cycle stage loads it into SRAM again")
    r.add_argument("--soak", type=int)
    r.add_argument("--chunk", type=int, default=0x1000, help="bytes per REST read (large replies stall on the board)")
    r.add_argument("--read-path", choices=("both", "lens", "plain", "lens3"), default="both",
                   help="both: pass 1 plain DMA reads, pass 2 lens; lens: forced ROML/ROMH select (our bitstream); "
                        "plain: a DMA bus read, the only path on Gideon's core; lens3: lens with a 3x majority (old workaround)")
    r.add_argument("--matrix-secs", default="0.1,1,10")
    r.add_argument("--gideon", action="store_true",
                   help="run on Gideon's core: REST only, plain DMA reads, dump stage only (no JTAG slot block)")
    r.add_argument("--romh", choices=("yes", "no"), help="force the ROMH half (needed with --gideon for rom16: pins unknown)")
    r.add_argument("--baseline-crt")
    r.add_argument("--bit")
    r.add_argument("--app")
    r.add_argument("--version", default=os.environ.get("T16_VERSION", "main-latest"))
    r.add_argument("--app-u2a", help="trex_crt.u2a to upload (default: the app tree's apps/trex_crt/trex_crt.u2a)")
    r.add_argument("--board-dir", default="")
    r.add_argument("--eeprom-word", type=int, default=1000)
    r.add_argument("--eeprom-words", type=int, default=4)
    r.add_argument("--app-validates-open", action="store_true",
                   help="trex_crt has validate-on-open (branch t57-validate-open): app_err then REQUIRES the popup; without it both behaviours pass")
    r.add_argument("--sim-seed", type=int, default=1)
    r.add_argument("--sim-fault", default="")
    a = ap.parse_args(argv)
    if a.cmd == "boot":
        return cmd_boot(a)
    if a.cmd == "status":
        return cmd_status(a)
    if a.cmd == "plan":
        return cmd_plan(a)
    if a.cmd == "stress":
        return cmd_stress(a)
    if a.cmd == "all":
        a.dry_run = False
        return cmd_all(a)
    if a.cmd == "mkbaseline":
        return cmd_mkbaseline(a)
    if a.cmd == "selftest":
        sys.path.insert(0, str(HERE))
        import t16_sim
        return t16_sim.selftest()
    return cmd_run(a)


def safe_main(argv=None) -> int:
    """main() without a traceback for Alex: whatever escapes is written to $T16_OUT/last_exception.txt and reported in one line."""
    try:
        return main(argv)
    except KeyboardInterrupt:
        print("\nabgebrochen (Ctrl-C)", file=sys.stderr)
        return 130
    except SystemExit:
        raise
    except Exception as e:  # noqa: BLE001
        import traceback
        try:
            OUT.mkdir(parents=True, exist_ok=True)
            with open(OUT / "last_exception.txt", "a") as f:
                f.write(f"--- {time.strftime('%F %T')}\n{traceback.format_exc()}\n")
        except OSError:
            pass
        print(f"\nFEHLER: {type(e).__name__}: {e}\n(Einzelheiten in {OUT / 'last_exception.txt'})", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(safe_main())
