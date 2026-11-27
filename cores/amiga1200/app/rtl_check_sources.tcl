# The source list of the A1200's design for tools/rtl_check.py (tests/rtl_check, the case of cores/amiga1200/app/gateware.toml). The checker reads the file names of a list as text and
# cannot decide the filter of core_sources.tcl (`ni $a12_own`, `ni $a12_trx`): with that list the Amiga's own cores/amiga/rtl/trx/amiga_regs.v (sorted before cores/amiga1200/rtl) would be
# the module it checks. This list names the files the register path runs through (the top, the core, the register block, and what amiga_core instantiates beside them) once, the A1200's copy
# where there is one. A file that cores/amiga1200/core_sources.tcl adds to or takes from that path has to be changed here too.
set mm  ../cores/amiga/rtl
set a12 ../cores/amiga1200/rtl
../cores/amiga/layer2_amiga.v
$a12/amiga_core.v $a12/amiga_regs.v $a12/amiga_fb.v $a12/amiga_sdc.v $a12/amiga_mem.v $a12/amiga_input.v
$mm/trx/amiga_audio.v $mm/trx/amiga_ce.v $mm/trx/amiga_eport.v $mm/trx/amiga_rtc.v $mm/trx/trx_afifo.v $mm/trx/trx_tdp16.v
