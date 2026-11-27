# shellcheck shell=bash
# The driver table of the ZX Spectrum's app for the native builds (T239.5, core move): cores/zx/app/lib/zx_regs.h names the registers through drvtab.h, which the app's build makes from the
# core's TOML A and the base blocks its TOML B names (tools/app_build.py merge_a, the function of the real link). Sourced with ROOT set; zx_drv_dir DIR makes DIR/drv/drvtab.h.
zx_drv_dir() {
  python3 - "$ROOT" "$1" <<'PY' || return 1
import os, sys
root, out = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", "zx", "app")
sdk = descrec.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]
base = os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))
app_build.merge_a({"toml-a": os.path.join(app, "gateware.toml"), "toml-b": os.path.join(app, "sdk.toml"),
                   "base-toml-a": base},
                  list(sdk["base_blocks"]), [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
