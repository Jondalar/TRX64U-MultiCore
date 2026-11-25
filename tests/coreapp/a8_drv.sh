# shellcheck shell=bash
# The driver tables the native builds of the Atari 800 XL need (T239.5 D15). cores/atari800/app/lib/atari800_regs.h names the registers through drvtab.h, which the app's build makes from the
# core's TOML A and the base blocks its TOML B names (tools/app_build.py merge_a, the function of the real link). Sourced with ROOT set.
#   a8_drv_dir DIR         makes DIR/drv/drvtab.h of the Atari app (a8app: the group atari800, TEST_APP_ATARI800; the table of that app holds its own blocks and the base blocks of its TOML B).
#   The table of the shared units, which use the Atari 800 XL as one of their sample cores (its library is compiled in them, from cores/atari800/app/lib), is shared_drv.sh's.
a8_drv_dir() {
  python3 - "$ROOT" "$1" <<'PY' || return 1
import os, sys
root, out = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", "atari800", "app")
sdk = descrec.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]
base = os.path.join(root, "coreapp", "gateware", sdk.get("base_toml_a", "shared.toml"))
app_build.merge_a({"toml-a": os.path.join(app, "gateware.toml"), "toml-b": os.path.join(app, "sdk.toml"),
                   "base-toml-a": base},
                  list(sdk["base_blocks"]), [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
