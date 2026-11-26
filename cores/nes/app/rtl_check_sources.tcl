# The source list of the NES core for tools/rtl_check.py (T239.5 D15; tests/rtl_check, the case of cores/nes/app/gateware.toml): the files cores/nes/mono/build_mono.tcl reads for the core itself
# (cores/nes/gen as tools/nes_gen.py makes it, the 6502, our glue in trx/, the top layer2_nes.v). The register path runs through layer2_nes and trx/nes_regs.v; the host snapshot of cores/nes/mono/host is not
# read, the request is the five cw_* ports of the top. A file that mono/build_mono.tcl adds to or takes from that path has to be changed here too.
../cores/nes/gen/apu.sv
../cores/nes/gen/cart.sv
../cores/nes/gen/mappers/MMC1.sv
../cores/nes/gen/mappers/MMC2.sv
../cores/nes/gen/mappers/MMC3.sv
../cores/nes/gen/mappers/VRC.sv
../cores/nes/gen/mappers/generic.sv
../cores/nes/gen/nes.v
../cores/nes/gen/ppu.sv
../cores/nes/gen/regs_savestates.sv
../cores/nes/gen/t65/t65_nes.v
../cores/nes/layer2_nes.v
../cores/nes/trx/nes_audio.v
../cores/nes/trx/nes_core.v
../cores/nes/trx/nes_flags.v
../cores/nes/trx/nes_mem.v
../cores/nes/trx/nes_pad.v
../cores/nes/trx/nes_pal.v
../cores/nes/trx/nes_pixw.v
../cores/nes/trx/nes_regs.v
../cores/nes/trx/nes_win.v
