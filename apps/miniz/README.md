The zip library (T197). `src/` is miniz (MIT), moved here from `apps/unzip/src/miniz` (copy of `apps/ultimate/apps/unzip/src/miniz`, submodule commit `aa2ef5dc`), unchanged.
Since base 4.0 (T239.3 D58) it is no `.u2a` file: the base loads no library file, so the library is linked through a TOML B `[[library]]` (sources `apps/miniz/src/*.c`, `miniz_lib.cc`,
header `zip_lib.h`, export `miniz` major 1, table function `miniz_table`). `miniz_lib.cc` returns the call table of `zip_lib.h`; code linked into the app takes it with
`lib_slot(LIBSLOT_miniz, &size)` (libslot.h), an application file lists `miniz>=1.0` in its needs and calls `api->lib->need(n, &size)` with the position of that entry; it carries only
`zip_lib.h` and miniz's headers for the types. Every core whose TOML B lists the library carries the miniz code (about 350 KB of source).
