# ROM images of the Atari 800 XL core

The ROM images are not part of this repository. The benches `sim/machine/run.sh os` and `sim/disk/run.sh` convert
`atarixl.rom` and `ataribas.rom` from this folder to hex (`sim/machine/rom2hex.py`) and boot the machine from them; the other
cases use the test OS (`sim/machine/testos2.asm`), which needs no Atari code. On the board the core reads its ROMs from
the stick or the flash (`coreapp/src/romfind.cc`), never from this folder. No code checks a hash.

| file | bytes | md5 of the images used during development | what it is |
|---|---|---|---|
| `atarixl.rom` | 16,384 | `e90c2325ffb21a7830c7f011648d688d` | the XL operating system (reset vector `$C2AA`) |
| `ataribas.rom` | 8,192 | `0bac0c6a50104045d902df4503a4c30b` | Atari BASIC |
| `pbi_bios.rom` | 8,192 | `3a95370b0fe1a4672b535963dc360eba` | the parallel-bus BIOS of the upstream MiSTer core, only needed once the core serves files; no bench here reads it |

Legal source: the Altirra emulator, https://www.virtualdub.org/altirra.html , ships AltirraOS, a free replacement for the Atari OS, and its own BASIC. Copy the XL OS image as
`atarixl.rom` (16,384 bytes) and the BASIC image as `ataribas.rom` (8,192 bytes). Whether the benches boot with
AltirraOS has not been tried.

How others source them (the repository page and its README only, no file links): the MiSTer Atari 800 core this port
is based on, https://github.com/MiSTer-devel/Atari800_MiSTer , explains in its README which ROM files it needs and
how to supply them.
