# ROM images for the host benches

The ROM images are not part of this repository. The simulation benches under `host/sim/` read the C64 and 1541
ROMs from this folder; `tools/rom2mem.py` turns the three C64 images into `c64_rom.mem` in a bench's run directory
and stops when a file is missing or has the wrong size. The files are not checked against a hash by the code; the
sha256 below is the one of the images the benches were developed with.

| file | bytes | sha256 |
|---|---|---|
| `characters.901225-01.bin` | 4,096 | `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420` |
| `kernal.901227-03.bin` | 8,192 | `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721` |
| `basic.901226-01.bin` | 8,192 | `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d` |
| `1541-ii.bin` | 16,384 | `326c289c38753323d7e8167897447cf61ef35189d82eb8d75210ece949adda7c` |

Legal sources:

- The VICE distribution, https://vice-emu.sourceforge.io/ , carries the C64 Kernal (901227-03), Basic (901226-01),
  character ROM (901225-01) and the 1541-II drive ROM in its `data/` folders. Copy them here and rename them as above.
- The Commodore ROM images of the C64 Forever package, https://www.c64forever.com/ .

How others source them (the repository page and its README only, no file links): the MiSTer C64 core,
https://github.com/MiSTer-devel/C64_MiSTer , explains in its README which ROM files it needs and how to supply them.

Check the size and sha256 after copying (`shasum -a 256 roms/*`). A different ROM revision runs the benches only
as far as the code of that revision behaves the same.
