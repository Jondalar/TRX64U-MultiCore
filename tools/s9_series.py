#!/usr/bin/env python3
"""T115 S9: many starts of the boot menu's default .cfw from the host, each one measured.

    tools/s9_series.py RUNS [--out DIR] [--ip 192.0.2.189] [--app hostapp.bin]
    tools/s9_series.py i2cdump          (one JTAG I2C dump, used by the series)
    tools/s9_series.py ... --cdce-cold  (after the dump: both CDCE913 register 0x01 to its cold value 0x00 over JTAG I2C)
    tools/s9_series.py ... --cdce-pdn   (instead: both CDCE913 powered down, factory PLL bytes, powered up again)
    tools/s9_series.py cdce_gideon      (both CDCE913 to the values Gideon's 3.15a runs with, over JTAG I2C)

Per run, unattended:
  1. openFPGALoader --reset (JPROGRAM): the FPGA configures the host from flash again; the power stays on
     (Alex, 2026-10-24: "Du darfst das gerne per JTAG machen").
  2. wait for the host's REST (/v1/status), then over JTAG I2C (read only): both CDCE913, registers 0x00-0x06 and
     0x10-0x1F, and the other devices on the 1.8 V bus.
  3. the host's countdown starts the default entry (Gideon's .cfw); the console is read over JTAG until the debug
     port goes away with the IPROG, its "legacy" lines are kept.
  4. wait for the firmware's REST (/v1/info), let the C64 come up, take one frame of its VIC stream
     (tools/stream_shot.py) and measure in the middle row where the background starts and ends.
One line per run in DIR/s9.tsv; the frame as DIR/runNN.png, the dump and the console as DIR/runNN.txt.
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "1541ultimate-tools/jtag-venv/bin/python")


def i2cdump():
    sys.path.insert(0, HERE)
    import i2cx
    import trxdbg
    d = trxdbg.Dbg()
    out = []
    try:
        def rd(bus, a, r, n):
            b = i2cx.Bus(d, bus)
            b.start()
            ok = b.byte_out(a << 1) and b.byte_out(r)
            b.start()
            ok = ok and b.byte_out(a << 1 | 1)
            data = [b.byte_in(i == n - 1) for i in range(n)] if ok else []
            b.stop()
            return " ".join(f"{x:02X}" for x in data) if ok else "nack"
        for bus in ("18", "hd"):
            # CDCE913 block read: command bit 7 = 0, the first byte is the count
            out.append(f"cdce {bus} 00: {rd(bus, 0x64, 0x00, 8)}")
            out.append(f"cdce {bus} 10: {rd(bus, 0x64, 0x10, 17)}")
        for a in (0x1A, 0x20, 0x21):
            out.append(f"bus18 {a:02X} 00: {rd('18', a, 0x00, 16)}")
    finally:
        i2cx.release(d)
        d.close()
    print("\n".join(out))
    return 0


def cdce_cold(pdn=False):
    # both CDCE913 register 0x01 to its cold-boot value 0x00 (crystal input, powered up), over JTAG I2C, read back
    sys.path.insert(0, HERE)
    import i2cx
    import trxdbg
    d = trxdbg.Dbg()
    out = []
    try:
        for bus in ("18", "hd"):
            b = i2cx.Bus(d, bus)
            def wr(reg, data):
                b.start()
                ok = b.byte_out(0x64 << 1) and b.byte_out(reg) and all(b.byte_out(x) for x in data)
                b.stop()
                return ok
            if pdn:
                # power down (dividers and PLL stop), factory PLL bytes from 0x14 (block write: count first), power up again
                ok = wr(0x81, [0x10]) and wr(0x14, [8, 0xED, 0x02, 0x01, 0x01, 0x00, 0x40, 0x02, 0x08])
                time.sleep(0.1)
                ok = ok and wr(0x81, [0x00])
            else:
                ok = wr(0x81, [0x00])
            b.start()
            ok2 = b.byte_out(0x64 << 1) and b.byte_out(0x00)
            b.start()
            ok2 = ok2 and b.byte_out(0x64 << 1 | 1)
            v = [b.byte_in(i == 7) for i in range(8)] if ok2 else []
            b.stop()
            out.append(f"cdce_cold {bus} write {ok} reg01 {v[2]:02X}" if v else f"cdce_cold {bus} write {ok} read nack")
    finally:
        i2cx.release(d)
        d.close()
    print("\n".join(out))
    return 0


def cdce_gideon(which=("18", "hd")):
    # both CDCE913 to the values Gideon's 3.15a leaves while it runs (phase b of the state dump, PAL, HDMI 50 Hz), written in the
    # order his firmware uses: the video PLL on the 1.8 V bus by a block write (SetVideoPll), the HDMI PLL powered down, written,
    # powered up with LVCMOS input (SetExternalPLL)
    sys.path.insert(0, HERE)
    import i2cx
    import trxdbg
    d = trxdbg.Dbg()
    out = []
    try:
        def wr(bus, reg, data):
            b = i2cx.Bus(d, bus)
            b.start()
            ok = b.byte_out(0x64 << 1) and b.byte_out(reg) and all(b.byte_out(x) for x in data)
            b.stop()
            return ok
        ok1 = ok2 = None
        if "18" in which:
            ok1 = wr("18", 0x14, [8, 0x57, 0x00, 0x05, 0x05, 0x84, 0x32, 0xC3, 0x4A])
        if "18rom" in which:
            # the boot ROM's own video PLL bytes (init_ntsc in bootloader_u64ii.c init_ext_pll)
            ok1 = wr("18", 0x14, [8, 0x57, 0x00, 0x03, 0x03, 0x02, 0xD0, 0x22, 0x08])
        if "hd" in which:
            ok2 = wr("hd", 0x81, [0x18]) and wr("hd", 0x14, [8, 0x57, 0x00, 0x01, 0x01, 0x0F, 0xA2, 0xCA, 0xAD]) and wr("hd", 0x81, [0x08])
        out.append(f"cdce_gideon 18 {ok1} hd {ok2}")
    finally:
        i2cx.release(d)
        d.close()
    print("\n".join(out))
    return 0


def get(ip, path, timeout=3):
    # curl: a Python started here may lack macOS Local Network access, curl has it
    r = subprocess.run(["curl", "-s", "-m", str(timeout), f"http://{ip}{path}"], capture_output=True, text=True)
    if r.returncode:
        raise OSError(f"curl {path}: rc {r.returncode}")
    return r.stdout


def wait_for(fn, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            v = fn()
            if v:
                return v
        except Exception:
            pass
        time.sleep(1)
    return None


def host_up(ip):
    return json.loads(get(ip, "/v1/status")).get("host_key")


def fw_up(ip):
    return json.loads(get(ip, "/v1/info")).get("firmware_version")


def measure(png):
    from PIL import Image
    img = Image.open(png)
    w, h = img.size
    px = img.load()
    y = h // 2
    border = px[0, y]
    x0 = next((x for x in range(w) if px[x, y] != border), -1)
    x1 = next((x for x in range(w - 1, -1, -1) if px[x, y] != border), -1)
    return w, h, border, px[w // 2, y], x0, x1


def series(runs, out, ip, app=None, cold=False):
    os.makedirs(out, exist_ok=True)
    tsv = os.path.join(out, "s9.tsv")
    if not os.path.exists(tsv):
        with open(tsv, "w") as f:
            f.write("run\ttime\thost_s\tfw_s\tbg_x0\tbg_x1\tverdict\tframe\tlegacy\n")
    for n in range(1, runs + 1):
        tag = f"run{n:02d}"
        t0 = time.time()
        log = []
        key = None
        for attempt in range(5):
            # a reset can leave the firmware running (seen once): then reset again
            r = subprocess.run(["nice", "-n", "15", "openFPGALoader", "-c", "usb-blaster", "--reset"], capture_output=True, text=True)
            log.append(f"reset {attempt + 1} rc {r.returncode} {(r.stdout + r.stderr).strip()[-200:]}")
            if r.returncode:                             # "TDO is stuck at 0" now and then: the cable, not the board
                time.sleep(2)
                continue
            if app:
                # a host app under test, over JTAG before the flash's own one reaches its countdown: hold the RISC-V,
                # load, release (trxdbg riscv); retried until the debug port of the new host answers
                for _ in range(20):
                    r = subprocess.run([os.path.join(HERE, "trxdbg.sh"), "riscv", app], capture_output=True, text=True, timeout=120)
                    if r.returncode == 0:
                        break
                    time.sleep(0.5)
                log.append(f"app {os.path.basename(app)} rc {r.returncode} {(r.stdout + r.stderr).strip()[-120:]}")
            key = wait_for(lambda: host_up(ip), 120)
            if key:
                break
        host_s = time.time() - t0
        log.append(f"host {key} after {host_s:.1f} s")
        try:
            log.append("host app " + str(json.loads(get(ip, "/v1/status")).get("app")))
        except Exception as e:
            log.append(f"host app ? {e}")
        if not key:
            print(f"{tag}: the host did not answer; stopping")
            return 1
        r = subprocess.run([PY, os.path.abspath(__file__), "i2cdump"], capture_output=True, text=True, timeout=60)
        log.append(r.stdout.strip() or r.stderr.strip()[-300:])
        if cold:
            r = subprocess.run([PY, os.path.abspath(__file__), "cdce_cold"] + (["pdn"] if cold == "pdn" else []), capture_output=True,
                               text=True, timeout=60)
            log.append(r.stdout.strip() or r.stderr.strip()[-300:])
        # the console until the IPROG takes the debug port away
        con, last = b"", time.time()
        while time.time() - last < 60:
            r = subprocess.run([os.path.join(HERE, "trxdbg.sh"), "console"], capture_output=True, timeout=30)
            if r.returncode or b"JtagError" in r.stderr:
                break
            con = r.stdout
            time.sleep(0.5)
        legacy = [l for l in con.decode(errors="replace").splitlines() if "legacy" in l]
        log += legacy
        ver = wait_for(lambda: fw_up(ip), 240)
        fw_s = time.time() - t0
        log.append(f"firmware {ver} after {fw_s:.1f} s")
        time.sleep(8)                                    # the C64 comes up, READY
        png = os.path.join(out, tag + ".png")
        # right after its start the firmware may not have the Mac's MAC yet ("Network Host Resolve Error"): try again
        for _ in range(6):
            # a ping first: the firmware learns the Mac's MAC from it (its own ARP probe does not always get an answer in)
            subprocess.run(["ping", "-c", "2", "-t", "3", ip], capture_output=True)
            r = subprocess.run([sys.executable, os.path.join(HERE, "stream_shot.py"), ip, png], capture_output=True, text=True, timeout=60)
            if os.path.exists(png):
                break
            time.sleep(5)
        log.append(r.stdout.strip())
        if os.path.exists(png):
            w, h, border, bg, x0, x1 = measure(png)
            frame = f"{w}x{h} border {border} bg {bg}"
        else:
            x0 = x1 = -1
            frame = "no frame"
        # the state-logging firmware (s9-statelog) writes /Temp/s9state.txt; phase b comes 5 s after its start
        st = os.path.join(out, tag + "_state.txt")
        for _ in range(10):
            r = subprocess.run(["curl", "-s", "-m", "10", "-o", st, f"ftp://{ip}/Temp/s9state.txt"], capture_output=True)
            if r.returncode == 0 and os.path.exists(st) and any(l.startswith(b"b ") for l in open(st, "rb").read().splitlines()):
                break
            time.sleep(2)
        with open(os.path.join(out, tag + ".txt"), "w") as f:
            f.write("\n".join(log) + "\n")
        # background from x 32 = whole (2026-10-21); from x 36 = column 0 hidden (2026-10-24 calibration frame)
        verdict = "whole" if x0 == 32 else ("shifted" if x0 >= 35 else "other")
        hand = next((l for l in legacy if "clock synthesizers" in l), "no legacy line")
        line = f"{n}\t{time.strftime('%H:%M:%S')}\t{host_s:.1f}\t{fw_s:.1f}\t{x0}\t{x1}\t{verdict}\t{frame}\t{hand.strip()}"
        with open(tsv, "a") as f:
            f.write(line + "\n")
        print(line, flush=True)
    return 0


def main(argv):
    if argv[:1] == ["i2cdump"]:
        return i2cdump()
    if argv[:1] == ["cdce_gideon"]:
        return cdce_gideon(tuple(argv[1:]) or ("18", "hd"))
    if argv[:1] == ["cdce_cold"]:
        return cdce_cold("pdn" in argv)
    if not argv:
        print(__doc__)
        return 1
    out = argv[argv.index("--out") + 1] if "--out" in argv else "s9_series"
    ip = argv[argv.index("--ip") + 1] if "--ip" in argv else "192.0.2.189"
    app = os.path.abspath(argv[argv.index("--app") + 1]) if "--app" in argv else None
    cold = "pdn" if "--cdce-pdn" in argv else "--cdce-cold" in argv
    return series(int(argv[0]), out, ip, app, cold)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
