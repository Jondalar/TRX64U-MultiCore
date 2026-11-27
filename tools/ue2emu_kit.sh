#!/usr/bin/env bash
# T174 D2.8: assemble the kit the emulator suites need and the repository must not hold (ROMs, the flash image, a core profile, a DFX .cor). The kit is a
# directory outside git: CI finds it as /cache/ue2emu-kit (ci/lib.sh ue2emu_kit), a Mac as UE2EMU_KIT or ~/scratch/ue2emu-kit.
#
#   tools/ue2emu_kit.sh KITDIR --emu-repo <u64-emulator checkout> --cor <vc20.cor>
#
# KITDIR/roms (the emulator's firmware/1541ultimate/roms: C64 and drive ROMs), KITDIR/flash.bin (run/flash.bin: the SPI flash every run starts from),
# KITDIR/profiles/VIC-20.toml (cores/vc20/app/ue2emu.toml with the symbols line, named after the manifest name of the DFX core: tools/ue2emu_core.py profile),
# KITDIR/cors/vc20.cor (a DFX core of this repository; the host finds its partial by the SHA-256, so any .cor of the same core serves).
set -euo pipefail
kit="${1:?usage: tools/ue2emu_kit.sh KITDIR --emu-repo DIR --cor FILE}"; shift
emu="" cor=""
while [ $# -gt 0 ]; do case "$1" in --emu-repo) emu="$2"; shift 2 ;; --cor) cor="$2"; shift 2 ;; *) echo "ue2emu_kit: unknown argument $1" >&2; exit 2 ;; esac; done
[ -d "$emu/firmware/1541ultimate/roms" ] && [ -f "$emu/run/flash.bin" ] && [ -f "$cor" ] ||
  { echo "ue2emu_kit: need --emu-repo with firmware/1541ultimate/roms and run/flash.bin, and --cor FILE" >&2; exit 1; }
mkdir -p "$kit/roms" "$kit/profiles" "$kit/cors"
cp -R "$emu/firmware/1541ultimate/roms/." "$kit/roms/"
cp "$emu/run/flash.bin" "$kit/flash.bin"
python3 -I "$(dirname "$0")/ue2emu_core.py" profile vc20 "$cor" "$kit/profiles" > /dev/null
cp "$cor" "$kit/cors/vc20.cor"
du -sh "$kit"; ls "$kit"
