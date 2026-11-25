# shellcheck shell=bash
# The driver tables the native builds of the Atari 800 XL need (T239.5 D15). cores/atari800/app/lib/atari800_regs.h names the registers through drvtab.h, which the app's build makes from the
# core's TOML A and the base blocks its TOML B names (tools/app_build.py merge_a, the function of the real link). Sourced with ROOT set.
#   a8_drv_dir DIR         makes DIR/drv/drvtab.h of the Atari app (a8app: the group atari800, TEST_APP_ATARI800; the table of that app holds its own blocks and the base blocks of its TOML B).
#   a8_shared_drv_dir DIR  makes DIR/drv/drvtab.h of the shared units: the blocks of coreapp/gateware/shared.toml (but its ident, the core window's standard block, which the Atari's own replaces: the
#                          same id, caps and build, and caps2) and the Atari's own blocks. The shared units use the Atari 800 XL as their sample core (its library is compiled in them, from cores/atari800/app/lib);
#                          the real shared app has no table of it.
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
a8_shared_drv_dir() {
  python3 - "$ROOT" "$1" <<'PY' || return 1
import os, sys
root, out = sys.argv[1:]
sys.path.insert(0, os.path.join(root, "tools"))
import app_build, descrec
app = os.path.join(root, "cores", "atari800", "app")
shared = os.path.join(root, "coreapp", "gateware", "shared.toml")
names = [b["name"] for b in descrec.toml_loads(open(shared).read())["block"] if b["name"] != "ident"]
app_build.merge_a({"toml-a": os.path.join(app, "gateware.toml"), "toml-b": os.path.join(app, "sdk.toml"), "base-toml-a": shared},
                  names, [os.path.join(root, "app", "sdkpkg", "maps")], out)
PY
}
