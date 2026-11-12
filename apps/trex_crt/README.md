TREX CRT on ABI 3 (T104.3 D0, D9.1). PROVENANCE: `src/cart_base.*`, `cart_dumper.*`, `cart_writer.*`, `cart_inspector.*`, `eapi_drivers.*` and `ui_shim.*` are copies of
`apps/ultimate/apps/trex_crt/src` (the TRX64 line's cartridge tools, GPL-3.0; submodule commit `aa2ef5dc`), the algorithms unchanged. Changed: the 40 x 24 character screen is gone -
`cart_screen.c` publishes what the tools drew as the strings of `trex_ui.h` (state values `cart.*` in `glue.cc`), which the screens of `app.toml` bind to; `main.c` and the key loop
are `trex_main.c` (a task that takes the commands of the screens' operations and runs the tool's own poll function); the three tools share one session on the port;
the write has a confirmation screen, the dump and the verify can be stopped (R/S) and a write or erase locks R/S with its reason; the file of the Flash screen and the
destination of the Dump screen are chosen on the screen. `eapi_drivers.c` carries the EAPI notice of its sources.
