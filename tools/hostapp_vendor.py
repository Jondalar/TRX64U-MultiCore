#!/usr/bin/env python3
"""Vendor the GPL files of the T84 host boot app out of vendor/gideon (our copy of the Ultimate app's software).

host-app/software/<dir>/<file> is a byte-for-byte copy of
vendor/gideon/software/<dir>/<file> (licence headers untouched); the list is
host-app/vendor.list, one path relative to ultimate-app/software per line, and
host-app/PROVENANCE.md is generated from it. Third-party libraries (lwip, lvgl,
FreeRTOS, chan_fat, httpd) are not copied: host-app/software/<name> is a symlink
to vendor/gideon's copy.

  tools/hostapp_vendor.py scan     add every file the last build read (output/**/*.d) + build inputs
  tools/hostapp_vendor.py add F..  add files (paths relative to ultimate-app/software)
  tools/hostapp_vendor.py copy     copy the list, rewrite PROVENANCE.md
"""
import os, re, shutil, subprocess, sys, glob, hashlib

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
HA = os.path.join(ROOT, "host-app")
SUB = os.path.join(ROOT, "vendor/gideon")
SRC = os.path.join(SUB, "software")
DST = os.path.join(HA, "software")
LIST = os.path.join(HA, "vendor.list")
# copies changed after copying (a plain `copy` would overwrite them): path -> what was changed and why
MODIFIED = {
    "components/config.cc": "T163: ConfigBackend - with one set (host app) the stores live in the backend's file; the flash pages are only read, once, to take them over",
    "components/config.h": "T163: declares ConfigBackend, config_set_backend()",
    "io/c64/joystick_output.cc": "T228 R10: setPadPort(), the core application's pad service as a source of its own per port, ANDed into both ports' lines",
    "io/c64/joystick_output.h": "T228 R10: declares setPadPort() and the two sources",
    "io/c64/keyboard_c64.h": "KEYBOARD_C64_TIMER_SCAN also for -DTRX_HOSTAPP (taps shorter than one UI loop were lost); getch_nowait()",
    "io/c64/trx_io.cc": "T93: trx_io_alt_id, a core application's own ID besides \"TRX2\"; the host app never sets it",
    "io/c64/trx_io.h": "T93: declares trx_io_alt_id",
    "io/usb/usb_hid.cc": "T104.1 5.6: usb_hid_set_mouse_sink(), one hook per mouse report for the mouse library; T141: usb_hid_set_keyboard_sink(), the combined boot report per keyboard report; T156: usb_hid_set_pad_sink(), an interface that is neither keyboard nor mouse is offered to the gamepad library; no sink (host app): unchanged",
    "io/usb/usb_hid.h": "T104.1 5.6: declares the hook, usb_hid_set_mouse1_position(), usb_hid_set_joy1_output(); T141: the keyboard hook; T156: UsbPadSink, usb_hid_set_pad_sink()",
    "io/sd_card/sd_card.cc": "T139: the data CRC16 of every block / CID / CSD is checked and a damaged read is repeated (SD_READ_RETRIES); T149 D5: RW_SPEED 3 (12.5 MHz), RW_SPEED_IOB 1 (25 MHz) only where sdio_set_rw_speed sees SDIO_CAP_IOB",
    "io/sd_card/sd_card.h": "T139: declares read_once(), read_register(), SD_READ_RETRIES",
    "io/sd_card/sdio.cc": "T139: sdio_read_block checks the CRC16 (sdio_crc16, SDIO_ERR_CRC), counters; uintptr_t for the alignment test; T149 D5: sdio_set_rw_speed (25 MHz only if the control register's bit 7 reads 1)",
    "io/sd_card/sdio.h": "T139: SDIO_ERR_CRC, sdio_crc16(), counters; SDIO_EXTERNAL_REGS lets a native test supply the registers; T149 D5: SDIO_CAP_IOB, sdio_set_rw_speed()",
    "io/rtc/rtc_dummy.cc": "T164 D1: get_fat_time() of a clock that is not set (1970, negative year) reads 1980-01-01 00:00:00, not a wrapped 2097",
    "io/rtc/timezones.cc": "T164 D1: every zone carries its own DST rule (IANA tzdata, POSIX form); Gideon's table gave all zones the US rule",
    "io/wifi/wifi.cc": "T184: -DTRX_HOSTAPP leaves out the U64Config power-mode store and low-voltage message and the Browsable access-point list, connectWithFeedback (the host page reads wifi_aps and getConnectionFeedback() itself)",
    "io/wifi/wifi.h": "T184 D4: connectWithFeedback (popup + progress box of the Ultimate menu) is left out with -DTRX_HOSTAPP",
    "io/wifi/esp32.h": "T184: no menu.h (the Ultimate menu classes are not in the host or core app; nothing in esp32.h or esp32.cc uses them)",
    "io/network/network_interface.cc": "T228 R21 row 57: effectuate_settings renames the mDNS responder to the new host name (mdns_follow_unsafe, a tcpip callback)",
    "io/network/network_esp32.cc": "T184: -DTRX_HOSTAPP leaves out the tree browser, popups and string boxes of the Ultimate menu (getDisplayString, context actions, AP list/enter); the settings store, effectuate_settings and on_edit are unchanged",
    "io/network/network_esp32.h": "T184: declares only what network_esp32.cc keeps with -DTRX_HOSTAPP",
    "filesystem/blockdev_flash.cc": "T139: write() erases every sector of the run, not only the first (FatFS writes runs of sectors; the rest was programmed over old data)",
    "filemanager/file_device.cc": "T111: a list of all FileDevices and find(name); release_volume(), the unmount the format needs (detach_disk is not changed)",
    "filemanager/file_device.h": "T111: declares them and get_block_device()",
    "lvgl_port/trx_lv_port.cc": "T101: UI_RES (scale and PX_POS from the build); T93 D9: trx_lv_suspend() / trx_lv_resume(), the layer handed to a core's picture and back; 5.14: trx_lv_refresh_now()",
    "lvgl_port/trx_lv_port.h": "T93 D9 and 5.14: declares them; T101: UI_RES, TRX_LV_SCALE/X0/Y0",
}
THIRD = {"lwip", "lvgl", "FreeRTOS", "chan_fat", "httpd"}
OWN = {"hostapp"}          # our own code lives in host-app/software/hostapp
EXTRA_TARGET = ["target/common/environment.mk", "target/common/rules.mk", "target/common/gcc.mk",
                "target/common/ld.mk", "target/libs/riscv/lwip/makefile", "target/libs/riscv/lvgl/makefile",
                "target/u64ii/riscv/ultimate/Makefile", "target/u64ii/riscv/ultimate/linker.x"]


def read_list():
    return [l.strip() for l in open(LIST)] if os.path.exists(LIST) else []


def write_list(items):
    open(LIST, "w").write("".join(i + "\n" for i in sorted(set(items)) if i))


def scan():
    items = set(read_list())
    for d in glob.glob(os.path.join(HA, "target/**/output/**/*.d"), recursive=True):
        text = open(d).read().replace("\\\n", " ")
        for tok in text.split():
            if tok.endswith(":"):
                continue
            m = re.search(r".*software/(.+)$", os.path.normpath(os.path.join(os.path.dirname(d), "..", tok)) if not tok.startswith("/") else tok)
            if not m:
                continue
            rel = m.group(1)
            top = rel.split("/")[0]
            if top in THIRD or top in OWN:
                continue
            if os.path.isfile(os.path.join(SRC, rel)):
                items.add(rel)
    write_list(items)
    print(len(items), "files in", LIST)


def copy():
    items = read_list()
    for top in os.listdir(DST):
        p = os.path.join(DST, top)
        if top in THIRD or top in OWN:
            continue
        if os.path.islink(p):
            os.unlink(p)
        elif os.path.isdir(p):
            shutil.rmtree(p)
    for rel in items:
        # tools/... are build tools at the root of vendor/gideon; everything else is under software/
        s, d = (os.path.join(SUB, rel), os.path.join(HA, rel)) if rel.startswith("tools/") else \
               (os.path.join(SRC, rel), os.path.join(DST, rel))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)
        if rel in MODIFIED:
            subprocess.check_call(["patch", "-s", d, os.path.join(HA, "patches", rel.replace("/", "_") + ".patch")])
    sha = re.search(r"\| 1541ultimate \| ([0-9a-f]{40}) \|", open(os.path.join(SUB, "VENDOR.md")).read()).group(1)
    out = ["# Provenance of host-app (T84)\n\n",
           "Everything under `host-app/software/<dir>/` except `hostapp/` is a byte-for-byte copy of "
           "`vendor/gideon/software/<dir>/` (Ultimate app, GPL-3.0, Gideon Zweijtzer and contributors; "
           "licence headers untouched), vendor/gideon is the 1541ultimate fork at commit `%s`, made by `tools/hostapp_vendor.py copy` from "
           "`vendor.list`. `target/` holds the Makefiles derived from `target/common`, `target/libs/riscv/{lwip,lvgl}` "
           "and `target/u64ii/riscv/ultimate` of the same commit (trimmed, the C64 side removed).\n\n" % sha,
           "Not copied, linked to vendor/gideon's copy (third-party, own licences): "
           + ", ".join("`software/%s`" % t for t in sorted(THIRD)) + ".\n\n",
           "Own code (this repo's licence): `host-app/software/hostapp/`, `coreapp/` (T93, built on this base), `tools/cor_pack.py`, `tools/build_hostapp.sh`, `tools/build_coreapp.sh`.\n\n",
           "| File (under host-app/) | Origin (under vendor/gideon/) | sha256 (first 12) |\n|---|---|---|\n"]
    for rel in items:
        tool = rel.startswith("tools/")
        h = hashlib.sha256(open(os.path.join(SUB if tool else SRC, rel), "rb").read()).hexdigest()[:12]
        out.append("| `%s` | `%s%s` | %s%s |\n" % (rel if tool else "software/" + rel, "" if tool else "software/", rel, h,
                   " (MODIFIED: %s)" % MODIFIED[rel] if rel in MODIFIED else ""))
    open(os.path.join(HA, "PROVENANCE.md"), "w").write("".join(out))
    print(len(items), "files copied")


if __name__ == "__main__":
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    if c == "scan":
        scan()
    elif c == "add":
        write_list(read_list() + sys.argv[2:])
    elif c == "copy":
        copy()
    else:
        print(__doc__)
