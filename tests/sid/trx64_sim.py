"""Offline C64 for the SID suite: the TRX64 headless daemon (reSID inside), driven over its JSON-RPC
WebSocket. Used by resid8_board.py --selftest to run the 6502 test programs and take the expected
values from reSID (6581 = model c64-pal, 8580 = model c64c-pal). No board, no VICE.
"""
import asyncio, base64, json, os, subprocess, sys, time

DAEMON = os.environ.get("TRX64_DAEMON", os.path.expanduser("~/Development/C64/Tools/TRX64/target/release/trx64-daemon"))
MODELS = {"6581": "c64-pal", "8580": "c64c-pal"}


class Sim:
    def __init__(self, chip="6581", port=4399):
        self.model, self.port, self.n = MODELS[chip], port, 0
        self.proc = subprocess.Popen(["nice", "-n", "15", DAEMON, "--headless", "--port", str(port), "--model", self.model],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.loop = asyncio.new_event_loop()
        self.ws = self.loop.run_until_complete(self._connect())
        self.rpc("session/create", {"model": self.model})
        self.rpc("session/reset", {"mode": "cold"})
        self.rpc("debug/pause")
        self.run(2_500_000)  # KERNAL to READY

    async def _connect(self):
        import websockets
        for _ in range(50):
            try:
                return await websockets.connect("ws://127.0.0.1:%d/?av=0" % self.port, max_size=None)
            except OSError:
                await asyncio.sleep(0.2)
        raise RuntimeError("daemon did not come up")

    def rpc(self, method, params=None):
        self.n += 1
        async def go():
            await self.ws.send(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params or {}}))
            while True:
                m = json.loads(await self.ws.recv())
                if m.get("id") == self.n:
                    return m
        m = self.loop.run_until_complete(go())
        if "error" in m:
            raise RuntimeError("%s: %s" % (method, m["error"]))
        return m["result"]

    def run(self, cycles):
        done = 0
        while done < cycles:
            step = min(cycles - done, 2_000_000)
            self.rpc("session/run", {"cycles": step})
            done += step

    def load(self, path, run=None):
        p = {"prg_path": path}
        if run is not None:
            p["run"] = "%04x" % run
        r = self.rpc("session/load_prg", p)
        self.rpc("debug/pause")   # load_prg with run leaves the machine "running"; we advance it by hand
        return r

    def read(self, addr, n, lens="cpu"):
        out = b""
        while n:
            k = min(n, 4096)
            r = self.rpc("session/read_memory", {"ranges": [{"addr": addr, "len": k, "lens": lens}]})
            out += base64.b64decode(r["chunks"][0]["bytes"])
            addr, n = addr + k, n - k
        return out

    def close(self):
        try:
            self.loop.run_until_complete(self.ws.close())
        finally:
            self.proc.terminate()
