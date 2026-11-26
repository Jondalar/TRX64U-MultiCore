# The source list of the Neo Geo core for tools/rtl_check.py (T239.5 D15; tests/rtl_check, the case of cores/neogeo/app/gateware.toml): our glue that mono/build_mono.tcl reads for the core itself
# (trx/*.v, the top layer2_neogeo.v). The register path runs through layer2_neogeo and trx/neo_regs.v; the vendored chip tree (cores/neogeo/rtl, fix/), the host snapshot of cores/neogeo/mono/host and cores/common
# are not read, the request is the cw_* ports of the top. A file that mono/build_mono.tcl adds to or takes from that path has to be changed here too.
../cores/neogeo/layer2_neogeo.v
../cores/neogeo/trx/neo_audio.v
../cores/neogeo/trx/neo_bk.v
../cores/neogeo/trx/neo_clk48.v
../cores/neogeo/trx/neo_core.v
../cores/neogeo/trx/neo_lc.v
../cores/neogeo/trx/neo_mem.v
../cores/neogeo/trx/neo_memcard.v
../cores/neogeo/trx/neo_mux.v
../cores/neogeo/trx/neo_pad.v
../cores/neogeo/trx/neo_pixw.v
../cores/neogeo/trx/neo_ram.v
../cores/neogeo/trx/neo_regs.v
../cores/neogeo/trx/neo_top.v
