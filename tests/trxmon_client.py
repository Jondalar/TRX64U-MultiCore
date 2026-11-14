#!/usr/bin/env python3
"""A small client for trxmon.u2a, the TRX64 runtime contract on the C64 Ultimate.

trxmon serves WebSocket JSON-RPC 2.0 on TCP 4312 (TRX64-Ultimate/docs/specs/T21-monitor.md
§7.4) with TRX64's method names. This client speaks it with the standard
library only, for board tests and for poking at the machine by hand:

    trxmon_client.py HOST[:PORT] ping
    trxmon_client.py HOST state
    trxmon_client.py HOST pause | run | step
    trxmon_client.py HOST read ADDR LEN [LENS]      # hex, lens cpu|ram|rom|io|cart
    trxmon_client.py HOST break ADDR | unbreak [NUM] | breaks
    trxmon_client.py HOST runc CYCLES               # session/run, waits for the stop
    trxmon_client.py HOST mon "d c000"              # monitor/exec, prints the text
    trxmon_client.py HOST watch [SECONDS]           # prints notifications
    trxmon_client.py HOST selftest                  # the board check below

`selftest` needs trxmon running and changes the machine's run state (it
pauses, steps, sets a breakpoint where the CPU just was and runs into it); it
leaves the machine paused with no breakpoint set.
"""
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time

PORT = 4312
GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class TrxmonError(Exception):
    pass


class Client:
    def __init__(self, host, port=PORT, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.timeout = timeout
        self.next_id = 1
        self.notes = []
        self.buf = b""
        key = base64.b64encode(os.urandom(16))
        req = (b"GET /?av=0 HTTP/1.1\r\nHost: " + host.encode() + b"\r\nUpgrade: websocket\r\n"
               b"Connection: Upgrade\r\nSec-WebSocket-Key: " + key + b"\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.sock.sendall(req)
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise TrxmonError("the server closed during the handshake")
            head += chunk
        head, self.buf = head.split(b"\r\n\r\n", 1)
        if not head.startswith(b"HTTP/1.1 101"):
            raise TrxmonError("no WebSocket upgrade: " + head.split(b"\r\n")[0].decode(errors="replace"))
        want = base64.b64encode(hashlib.sha1(key + GUID).digest())
        if b"Sec-WebSocket-Accept: " + want not in head:
            raise TrxmonError("bad Sec-WebSocket-Accept")

    def close(self):
        try:
            self._send_frame(8, struct.pack(">H", 1000))
        finally:
            self.sock.close()

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            hdr = struct.pack("BB", 0x80 | opcode, 0x80 | n)
        elif n < 65536:
            hdr = struct.pack(">BBH", 0x80 | opcode, 0x80 | 126, n)
        else:
            hdr = struct.pack(">BBQ", 0x80 | opcode, 0x80 | 127, n)
        body = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        self.sock.sendall(hdr + mask + body)

    def _need(self, n, deadline):
        while len(self.buf) < n:
            left = deadline - time.monotonic()
            if left <= 0:
                raise socket.timeout()
            self.sock.settimeout(left)
            chunk = self.sock.recv(65536)
            if not chunk:
                raise TrxmonError("the server closed the connection")
            self.buf += chunk

    def _recv_message(self, deadline):
        """One text message (pings answered, binary frames dropped)."""
        msg = b""
        while True:
            self._need(2, deadline)
            b0, b1 = self.buf[0], self.buf[1]
            n, h = b1 & 127, 2
            if n == 126:
                self._need(4, deadline)
                n, h = struct.unpack(">H", self.buf[2:4])[0], 4
            elif n == 127:
                self._need(10, deadline)
                n, h = struct.unpack(">Q", self.buf[2:10])[0], 10
            if b1 & 0x80:
                raise TrxmonError("the server masked a frame")
            self._need(h + n, deadline)
            payload, self.buf = self.buf[h:h + n], self.buf[h + n:]
            op = b0 & 15
            if op == 9:
                self._send_frame(10, payload)
                continue
            if op == 8:
                raise TrxmonError("the server closed the WebSocket")
            if op in (0, 1):
                msg += payload
                if b0 & 0x80:
                    return json.loads(msg.decode())
            # binary (A/V) and pong frames are not for us

    def call(self, method, params=None, timeout=None):
        rid = self.next_id
        self.next_id += 1
        req = {"jsonrpc": "2.0", "id": rid, "method": method, "params": params or {}}
        self._send_frame(1, json.dumps(req).encode())
        deadline = time.monotonic() + (timeout or self.timeout)
        while True:
            m = self._recv_message(deadline)
            if m.get("id") == rid:
                if "error" in m:
                    raise TrxmonError("%s: %s (%s)" % (method, m["error"].get("message"), m["error"].get("code")))
                return m["result"]
            if "method" in m:
                self.notes.append(m)

    def wait_note(self, method, timeout):
        deadline = time.monotonic() + timeout
        while True:
            for i, m in enumerate(self.notes):
                if m.get("method") == method:
                    return self.notes.pop(i)
            try:
                m = self._recv_message(deadline)
            except socket.timeout:
                return None
            if "method" in m:
                self.notes.append(m)

    def mon(self, command, timeout=None):
        r = self.call("monitor/exec", {"command": command}, timeout=timeout or 15.0)
        if "error" in r:
            raise TrxmonError("monitor: " + r["error"])
        return r["output"]

    def read(self, addr, length, lens="cpu"):
        r = self.call("session/read_memory", {"ranges": [{"addr": addr, "len": length, "lens": lens}]})
        return base64.b64decode(r["chunks"][0]["bytes"])


def hexnum(s):
    return int(s.lstrip("$"), 16)


def selftest(c):
    """The board check: every step prints what it saw; any mismatch raises."""
    pong = c.call("ping")
    print("ping:", pong)
    if pong.get("runtime_version") != "trx64-runtime/2" or pong.get("backend") != "c64u":
        raise TrxmonError("not trxmon on a C64 Ultimate: %r" % pong)

    st = c.call("debug/pause", {"source": "llm"})
    print("pause:", st["runState"], "pc $%04x" % st["pc"], "stop", st["stop"])
    if st["runState"] != "paused":
        raise TrxmonError("debug/pause did not pause")

    s = c.call("session/state")
    cpu = s["cpu"]
    print("state: pc $%04x a %02x x %02x y %02x sp %02x p %02x, raster %d, cycles %d" % (
        cpu["pc"], cpu["a"], cpu["x"], cpu["y"], cpu["sp"], cpu["flags"], s["vic"]["rasterLine"], s["c64Cycles"]))

    vec = c.read(0xFFFA, 6, "rom")
    print("rom $fffa: %s" % vec.hex())
    if vec[4:6] != c.read(0xFFFE, 2, "rom"):
        raise TrxmonError("rom lens reads differ")
    ram0 = c.read(cpu["pc"], 3, "cpu")
    print("cpu @pc: %s" % ram0.hex())
    print(c.mon("r"))
    print(c.mon("d"))

    before = c.call("session/state")["c64Cycles"]
    st = c.call("debug/step")
    note = c.wait_note("debug/stopped", 2.0)
    after = c.call("session/state")
    print("step: pc $%04x -> $%04x, %d cycles" % (cpu["pc"], after["cpu"]["pc"], after["c64Cycles"] - before))
    if after["c64Cycles"] <= before:
        raise TrxmonError("a step did not move the cycle counter")
    if not note:
        raise TrxmonError("no debug/stopped after debug/step")

    r = c.call("session/run", {"cycles": 20000}, timeout=10.0)
    print("session/run 20000:", r)
    here = c.call("session/state")["cpu"]["pc"]

    # The loop the CPU is in comes back to where it is now: break there.
    b = c.call("debug/break_add", {"pc": here})
    print("break_add $%04x:" % here, b)
    c.call("debug/continue", {"source": "llm"})
    hit = c.wait_note("debug/breakpoint_hit", 5.0)
    if hit is None:
        c.call("debug/pause")
        c.call("debug/break_del", {})
        raise TrxmonError("no debug/breakpoint_hit at $%04x within 5 s" % here)
    print("breakpoint_hit:", hit["params"]["pc"], hit["params"]["registers"].replace("\n", " | "))
    if hit["params"]["pc"] != here:
        raise TrxmonError("stopped at $%04x, not $%04x" % (hit["params"]["pc"], here))
    c.call("debug/break_del", {})
    print(c.mon("status"))
    print("selftest: ok")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    host, cmd, args = argv[1], argv[2], argv[3:]
    port = PORT
    if ":" in host:
        host, port = host.rsplit(":", 1)
        port = int(port)
    c = Client(host, port)
    try:
        if cmd == "ping":
            print(json.dumps(c.call("ping"), indent=1))
        elif cmd == "state":
            print(json.dumps(c.call("session/state"), indent=1))
        elif cmd == "pause":
            print(json.dumps(c.call("debug/pause", {"source": "llm"}), indent=1))
        elif cmd == "run":
            print(json.dumps(c.call("debug/continue", {"source": "llm"}), indent=1))
        elif cmd == "step":
            print(json.dumps(c.call("debug/step", {"source": "llm"}), indent=1))
        elif cmd == "read":
            data = c.read(hexnum(args[0]), hexnum(args[1]), args[2] if len(args) > 2 else "cpu")
            base = hexnum(args[0])
            for i in range(0, len(data), 16):
                print("%04x  %s" % ((base + i) & 0xFFFF, data[i:i + 16].hex(" ")))
        elif cmd == "break":
            print(json.dumps(c.call("debug/break_add", {"pc": hexnum(args[0])})))
        elif cmd == "unbreak":
            print(json.dumps(c.call("debug/break_del", {"id": int(args[0])} if args else {})))
        elif cmd == "breaks":
            print(json.dumps(c.call("debug/break_list")))
        elif cmd == "runc":
            n = int(args[0])
            print(json.dumps(c.call("session/run", {"cycles": n}, timeout=5.0 + n / 500000.0)))
        elif cmd == "mon":
            print(c.mon(" ".join(args)))
        elif cmd == "watch":
            end = time.monotonic() + (float(args[0]) if args else 60.0)
            while time.monotonic() < end:
                try:
                    m = c._recv_message(end)
                except socket.timeout:
                    break
                print(json.dumps(m))
        elif cmd == "selftest":
            selftest(c)
        else:
            print(__doc__)
            return 2
    except TrxmonError as e:
        print("error:", e)
        return 1
    finally:
        c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
