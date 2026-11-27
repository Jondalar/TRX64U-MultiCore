# shellcheck shell=bash
# The driver table the native builds of the Commodore 264's app need (T239.5 D15). cores/c264/app/lib/c264_regs.h names the registers through drvtab.h, which the app's build makes from the core's TOML A
# and the base blocks its TOML B names (tools/app_build.py merge_a, the function of the real link); the drive window is the core's own block drive_a. Sourced with ROOT set.
#   c264_drv_dir DIR   makes DIR/drv/drvtab.h of the 264's app (c264app: the group c264, TEST_APP_C264; the table of that app holds its own blocks and the base blocks of its TOML B).
c264_drv_dir() {
  python3 - "$ROOT" "$1" <<'PY' || return 1
import os, sys
root, out = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", "c264", "app")
sdk = descrec.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]
base = os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))
app_build.merge_a({"toml-a": os.path.join(app, "gateware.toml"), "toml-b": os.path.join(app, "sdk.toml"),
                   "base-toml-a": base},
                  list(sdk["base_blocks"]), [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
