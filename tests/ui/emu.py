"""Drives ue2emu headless: starts it with the coreapp image, speaks its control protocol, reads the application's state over REST.

The control protocol is the emulator's (one command per line, the answer ends in a line `ok` or `error ...`): key, wait, png, button. The state is
`GET /v1/ui/state` of the application, the snapshot of the UI service (T192.1 contract 2, built by coreapp/src/uiapp.cc): JSON, read-only. The TRX64U run has no core
folder, so the service asks for no token (uiapp.cc token_path); the Amiga 1200 run has one on the stick and fetches the token with the first login (`_grant`).
"""
import json
import os
import shutil
import socket
import subprocess
import time
import urllib.request

import fixtures

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EMU = os.environ.get("UE2EMU", os.path.expanduser("~/Development/u64-emulator/target/release/ue2emu"))
ROMS = os.environ.get("UE2EMU_ROMS", os.path.expanduser("~/Development/u64-emulator/firmware/1541ultimate/roms"))
APP = os.environ.get("COREAPP", os.path.join(HERE, "build", "coreapp.app"))
CTL_PORT = int(os.environ.get("UI_CTL_PORT", "47411"))
REST_PORT = int(os.environ.get("UI_REST_PORT", "47480"))
WORK = os.path.join(HERE, "work")
OUT = os.path.join(HERE, "out")


CORES = ("amiga1200", "nes", "vc20", "c128", "atari800", "pet", "diag")      # the cores with a profile in tests/coreapp/emu/profiles (no C64 in the machine)


class EmuError(Exception):
    pass


class Emu:
    def __init__(self, log=None, core="trx64u"):
        self.core = core                     # "trx64u" (the C64 record), "amiga1200" or "nes" (the profiles of tests/coreapp/emu/profiles)
        self.token = None
        self.proc = None
        self.sock = None
        self.f = None
        self.logpath = log or os.path.join(WORK, "emu.log")
        self.shots = 0

    # ---- life -------------------------------------------------------------------------------------------------------------------------
    def start(self):
        for need, what in ((EMU, "ue2emu (UE2EMU)"), (ROMS, "the emulator's ROM folder (UE2EMU_ROMS)"), (APP, "the coreapp image (COREAPP, tests/ui/build.sh)")):
            if not os.path.exists(need):
                raise EmuError("%s not found: %s" % (what, need))
        shutil.rmtree(WORK, ignore_errors=True)
        os.makedirs(WORK)
        stick = os.path.join(WORK, "stick")
        fixtures.make_stick(stick, ROMS, self.core)
        # the SD card: the emulator builds a FAT volume from a directory for a USB stick; the same volume is the SD card's image
        sd_dir = os.path.join(WORK, "sd_src")
        fixtures.make_sd_dir(sd_dir)
        mk = os.path.join(WORK, "mk_sd")
        os.makedirs(mk)
        script = os.path.join(mk, "quit.txt")
        open(script, "w").write("wait 1000\nquit\n")
        subprocess.run(["nice", "-n", "15", EMU, "run", "--board", "c64u", "--caps", "25E40202", "--trx-io", "on", "--firmware", APP, "--roms", ROMS, "--c64-roms", ROMS,
                        "--flash", os.path.join(mk, "flash.bin"), "--usb-dir", sd_dir, "--usb-dir-work", os.path.join(mk, "work"), "--headless", "--speed", "max",
                        "--script", script], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=mk, timeout=120)
        sd_img = os.path.join(WORK, "sd.img")
        made = [os.path.join(dp, fn) for dp, _, fns in os.walk(os.path.join(mk, "work")) for fn in fns if fn == "image-1.img"]
        if not made:
            raise EmuError("the emulator made no volume for the SD card")
        shutil.copy(made[0], sd_img)
        cmd = ["nice", "-n", "15", EMU, "run", "--board", "c64u", "--caps", "25E40202", "--trx-io", "on", "--firmware", APP, "--roms", ROMS,
               "--flash", os.path.join(WORK, "flash.bin"), "--c64-roms", ROMS, "--usb-dir", stick, "--usb-dir-work", os.path.join(WORK, "usbwork"), "--sd", sd_img,
               "--headless", "--speed", "max", "--control", "127.0.0.1:%d" % CTL_PORT, "--net", "user",
               "--hostfwd", "tcp:127.0.0.1:%d:80" % REST_PORT]
        if self.core in CORES:         # no C64 in the machine; the core folder is on the stick (fixtures.make_stick), the profile answers the gateware's registers
            cmd[cmd.index("--c64-roms"):cmd.index("--c64-roms") + 2] = ["--c64", "none"]
            cmd += ["--core", os.path.join(ROOT, "tests", "coreapp", "emu", "profiles", self.core + ".toml"), "--handover", "video=" + os.environ.get("UI_VIDEO", "720p")]
        cmd += os.environ.get("UI_EMU_ARGS", "").split()      # e.g. UI_EMU_ARGS="--log io" (T210: the register writes of the first frames)
        self.log = open(self.logpath, "w")
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT, cwd=WORK)
        deadline = time.time() + 60
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise EmuError("ue2emu ended at start (see %s)" % self.logpath)
            try:
                self.sock = socket.create_connection(("127.0.0.1", CTL_PORT), timeout=300)
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise EmuError("the control port did not open")
        self.f = self.sock.makefile("rw", buffering=1)

    def stop(self):
        try:
            if self.f:
                self.f.write("quit\n")
                self.f.flush()
        except OSError:
            pass
        if self.proc:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.sock:
            self.sock.close()
        self.proc = self.sock = self.f = None

    # ---- control port -----------------------------------------------------------------------------------------------------------------
    def cmd(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        out = []
        while True:
            l = self.f.readline()
            if not l:
                raise EmuError("the control port closed")
            l = l.rstrip("\n")
            out.append(l)
            if l == "ok" or l.startswith("error"):
                break
        if out[-1] != "ok":
            raise EmuError("%s: %s" % (line, out[-1]))
        return out[:-1]

    def key(self, name, hold_ms=None):
        self.cmd("key %s%s" % (name, " %d" % hold_ms if hold_ms else ""))

    def wait(self, ms):
        self.cmd("wait %d" % ms)

    def png(self, name):
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, name + ".png")
        self.cmd("png " + path)
        return path

    # ---- the application's state ------------------------------------------------------------------------------------------------------
    def _grant(self):
        """A core with a core folder asks for the service's token (T192.1 S14): the first login gets it."""
        req = urllib.request.Request("http://127.0.0.1:%d/v1/ui/grant" % REST_PORT, data=b"", method="POST")
        with urllib.request.urlopen(req, timeout=10) as r:
            self.token = json.loads(r.read().decode("utf-8")).get("token")

    def state(self, tries=40):
        last = None
        for _ in range(tries):
            try:
                if self.core in CORES and not self.token:
                    self._grant()
                url = "http://127.0.0.1:%d/v1/ui/state" % REST_PORT + ("?t=" + self.token if self.token else "")
                with urllib.request.urlopen(url, timeout=10) as r:
                    return json.loads(r.read().decode("utf-8"))
            except Exception as e:           # the application has not started its REST server yet, or the menu loop is busy
                last = e
                self.wait(300)
        raise EmuError("no state from the application: %s" % last)
