# The source list of the ZX Spectrum Next core for tools/rtl_check.py (T239.5 D15; tests/rtl_check, the case of cores/zxnext/app/gateware.toml): the files cores/zxnext/mono/build_mono.tcl reads for the core itself
# (cores/zxnext/gen as tools/zxnext_gen.py makes it, the Spectrum core's key table, our glue in trx/, the top layer2_zxnext.v). The register path runs through layer2_zxnext and trx/zxn_regs.v; the host snapshot of
# cores/zxnext/mono/host is not read, the request is the cw_* ports of the top. A file that mono/build_mono.tcl adds to or takes from that path has to be changed here too.
../cores/zx/shared/zxcpc_keyeng.v
../cores/zx/trx/zx_keys.v
../cores/zxnext/gen/zxnext.v
../cores/zxnext/layer2_zxnext.v
../cores/zxnext/trx/zxn_audio.v
../cores/zxnext/trx/zxn_clk.v
../cores/zxnext/trx/zxn_core.v
../cores/zxnext/trx/zxn_i2c.v
../cores/zxnext/trx/zxn_keys.v
../cores/zxnext/trx/zxn_mem.v
../cores/zxnext/trx/zxn_pixw.v
../cores/zxnext/trx/zxn_regs.v
../cores/zxnext/trx/zxn_sd.v
