# shellcheck shell=bash
# The driver table of the VIC-20's app for the native builds (T239.3 V-4): cores/vc20/app/lib/vc20_regs.h names the registers through drvtab.h, which the app's build makes from the
# core's TOML A and the base blocks its TOML B names (tools/app_build.py merge_a, the function of the real link). Sourced with ROOT set; vc20_drv_dir DIR makes DIR/drv/drvtab.h.
# vc20_drv_dir DIR wd makes the table of a drive that has the WD177x as well (the block drive_wd of shared.toml next to the VIC-20's own drive_a): the VIC-20's app has no such table, only
# test_cbm1541's 1581 tests run against it (run.sh cbm1541_wd).
vc20_drv_dir() {
  python3 - "$ROOT" "$1" "${2:-}" <<'PY' || return 1
import os, sys
root, out, wd = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", "vc20", "app")
sdk = descrec.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]
blocks = list(sdk["base_blocks"])                       # what the app links: its TOML B's blocks of its base_toml_a, nothing else
# The table of the VIC-20's app as run.sh's vc20app builds against it; the shared units never see it (they take $T/drv from shared.toml). It names audio_master (the rows Volume and Tone exist only where the table has it, D36).
base = os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))
if wd:                                                # the host's TOML A has no drive_wd: the block of shared.toml is added to a copy of the base for this table only
    wdb = [b for b in descrec.toml_loads(open(os.path.join(root, "coreapp", "gateware", "shared.toml")).read())["block"] if b["name"] == "drive_wd"][0]
    base = out.rstrip("/") + ".base.toml"
    os.makedirs(os.path.dirname(base), exist_ok=True)
    with open(base, "w") as f:
        f.write(open(os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))).read())
        f.write('\n[[block]]\nname = "drive_wd"\nmodule = "%s"\nowner = "base"\naddress = 0x%X\nsize = 0x%X\nmap = "%s"\n' % (wdb["module"], wdb["address"], wdb["size"], wdb["map"]))
    blocks.append("drive_wd")
app_build.merge_a({"toml-a": os.path.join(app, "gateware.toml"), "toml-b": os.path.join(app, "sdk.toml"),
                   "base-toml-a": base},
                  blocks, [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
