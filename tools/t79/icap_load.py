#!/usr/bin/env python3
"""T79: load layer 2's partial bitstream through the ICAP loader of layer 1 (the RISC-V does it),
instead of over JTAG (partial_load.py).

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python tools/t79/icap_load.py PARTIAL.bit [--raw] [--src ADDR]

1. strips the .bit header (the ICAP wants the raw configuration words from the dummy words before
   the sync word on; a header of any length would also misalign the 32-bit words; --raw sends the
   whole file as partial_load.py does), NOT bit-reversed: top.v swaps the bits in each byte;
2. puts the bytes into DDR2 (default 0x02000000) and the parameters at 0xFF80
   (host/icap_test/icap_test.c), clears its result block;
3. decouple on, tools/trxdbg.sh riscv build/icap_test.bin (build with tools/build_icap_test.sh);
4. waits for the program, prints its result and console line;
5. reads DONE/INIT over JTAG (as partial_load.py), decouple off when DONE is set.

Run it with the venv python above (u64ii_jtag needs pyftdi); trxdbg.sh is called as a command.
"""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TRXDBG = os.path.join(ROOT, "tools", "trxdbg.sh")
PROG = os.path.join(ROOT, "software", "icap_test", "build", "icap_test.bin")
PARAM, RESULT = 0xFF80, 0xFF90
MAGIC_DONE = 0x1CA90002
STATUS = {0: "ok", 1: "wrong ID byte (no ICAP loader in this bitstream?)", 2: "bad length",
          3: "word count differs from the words sent"}


def trxdbg(*args, check=True):
    r = subprocess.run([TRXDBG, *map(str, args)], capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"trxdbg {' '.join(map(str, args))} failed:\n{r.stdout}{r.stderr}")
    return r.stdout


def raw_config(data):
    """The configuration data of a Xilinx .bit file: after the header fields a..d, field e."""
    pos = 2 + int.from_bytes(data[0:2], "big") + 2          # length + magic field, then field 'a'
    for key in b"abcd":
        assert data[pos] == key, f"bad .bit header: field {chr(key)} not at {pos}"
        pos += 3 + int.from_bytes(data[pos + 1:pos + 3], "big")
    assert data[pos] == ord("e"), "bad .bit header: field e"
    n = int.from_bytes(data[pos + 1:pos + 5], "big")
    raw = data[pos + 5:pos + 5 + n]
    assert len(raw) == n and n % 4 == 0, "bad .bit header: field e length"
    return raw


def ddr_bytes(addr, length):
    out = []
    for line in trxdbg("ddrdump", hex(addr), length).splitlines():
        out += [int(x, 16) for x in line.split(":", 1)[1].split()]
    return bytes(out)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    raw_mode = "--raw" in args
    src = int(args[args.index("--src") + 1], 0) if "--src" in args else 0x02000000
    path = [a for a in args if not a.startswith("--") and not a.startswith("0x")][0]
    data = open(path, "rb").read()
    if not raw_mode:
        data = raw_config(data)
    assert b"\xaa\x99\x55\x66" in data[:4096], "no sync word"
    assert data.index(b"\xaa\x99\x55\x66") % 4 == 0, "sync word not word aligned"
    if len(data) % 4:
        data += b"\xff" * (-len(data) % 4)
    assert os.path.exists(PROG), f"{PROG} missing: run tools/build_icap_test.sh"
    print(f"{path}: {len(data)} bytes ({len(data) // 4} words) to DDR2 0x{src:X}")

    tmp = os.path.join(ROOT, "tools", "t79", ".icap_load.tmp")
    open(tmp, "wb").write(data)
    t = time.monotonic()
    trxdbg("ddrload", tmp, hex(src))
    print(f"loaded in {time.monotonic() - t:.0f} s")
    # spot check: the first and the last word
    head, tail = ddr_bytes(src, 16), ddr_bytes(src + len(data) - 16, 16)
    assert head == data[:16] and tail == data[-16:], "DDR2 read back differs"
    # parameters and a cleared result block (9 words from 0xFF80)
    params = src.to_bytes(4, "little") + len(data).to_bytes(4, "little") + bytes(28)
    open(tmp, "wb").write(params)
    trxdbg("ddrload", tmp, hex(PARAM))
    os.unlink(tmp)

    print(trxdbg("decouple", "on").strip())
    print(trxdbg("riscv", PROG).strip())
    res = None
    for _ in range(120):
        time.sleep(1)
        b = ddr_bytes(RESULT, 16)
        w = [int.from_bytes(b[i:i + 4], "little") for i in range(0, 16, 4)]
        if w[0] == MAGIC_DONE:
            res = w
            break
    print(trxdbg("console", check=False).strip().splitlines()[-1:] or "(no console text)")
    if res is None:
        print("TIMEOUT: the program did not finish (leaving the core decoupled)")
        return 1
    print(f"program: status {res[1]} ({STATUS.get(res[1], '?')}), ID 0x{res[2]:02X}")
    b = ddr_bytes(RESULT + 16, 16)
    print(f"words counted by the loader {int.from_bytes(b[0:4], 'little')}, "
          f"expected {int.from_bytes(b[4:8], 'little')}")

    sys.path.insert(0, os.path.expanduser("~/Development/1541ultimate-tools/tooling"))
    import u64ii_jtag as jt
    from u64ii_bscan import Blaster
    cable = Blaster.open()
    tap = jt.Tap(cable)
    tap.reset()
    cap = tap.ir_capture()
    tap.reset()
    cable.release()
    done = bool(cap & jt.IR_CAPTURE_DONE)
    print(f"IR capture 0x{cap:02X} (INIT {bool(cap & jt.IR_CAPTURE_INIT)}, DONE {done})")
    if res[1] != 0 or not done:
        print("NOT OK: leaving the core decoupled (fix, or `tools/trxdbg.sh decouple off` by hand)")
        return 1
    print(trxdbg("decouple", "off").strip())
    print("OK. Next: PUT /v1/machine:reboot (the app writes ROMs again), then look at the screen.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
