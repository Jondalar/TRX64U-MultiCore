# shellcheck shell=bash
# Sourced with ROOT set by tests/coreapp/srclist.sh, tests/coreapp/run.sh and cores/atari800/sim/disk/run.sh.
# own_drv_dir <core> <dir>: the driver table of a core's own app (the call of its real link, tools/app_build.py merge_a: its TOML A and the base blocks its TOML B names),
# <dir>/drv/drvtab.h; the general form of vc20_drv_dir
own_drv_dir() {
  python3 - "$ROOT" "$1" "$2" <<'PY' || return 1
import os, sys
root, core, out = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", core, "app")
sdk = descrec.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]
app_build.merge_a({"toml-a": os.path.join(app, sdk.get("toml_a", "gateware.toml")), "toml-b": os.path.join(app, "sdk.toml"),
                   "base-toml-a": os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))},
                  list(sdk.get("base_blocks", [])), [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
