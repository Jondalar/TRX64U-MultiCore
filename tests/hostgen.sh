# shellcheck shell=bash
# Sourced by the host test scripts and tools/menusim/run.sh (T239.3 V-3b, D30 point 1, P1-2): a build on a host machine takes the two I/O bases and the table of the base blocks
# from the TOML A of the host bitstream (coreapp/gateware/host.toml), as the board's build does. Nothing of it is a default in iomap.h, u2p.h or descrec.cc.
#   hostio.h   IOBASE and U2P_IO_BASE: the two values of the fragment `descrec.py ldfrag` writes for the board (iomap.h and u2p.h include it on a host build)
#   basetab.h  the base blocks of the TOML A: the table descrec.cc refuses a [[window]] against
# Both are written to one directory that is put on the include path of every compile of the script (CPATH), so no compile line names it. Needs $ROOT of the caller. The directory is
# the same on every run (a test's key must not change with a scratch directory) and a file is written only when its content changes.
HOSTGEN_DIR="$ROOT/.ci/hostgen"
mkdir -p "$HOSTGEN_DIR"
python3 -I "$ROOT/tools/descrec.py" hostio "$ROOT/coreapp/gateware/host.toml" "$HOSTGEN_DIR"
python3 -I "$ROOT/tools/descrec.py" basetab "$ROOT/coreapp/gateware/host.toml" "$HOSTGEN_DIR"
export CPATH="$HOSTGEN_DIR${CPATH:+:$CPATH}"
