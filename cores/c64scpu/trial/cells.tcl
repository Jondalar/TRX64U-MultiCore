# T182 trial: count the LUTs / FFs of the trial by instance path inside a synthesized checkpoint (TRX_DCP), to see where the CPU-side glue went.
open_checkpoint $::env(TRX_DCP)
foreach pat {*scpu_t/cpu/cpu816/* *scpu_t/cpu/* *scpu_t/stub/*} {
  set cells [get_cells -hier -filter "NAME =~ $pat && IS_PRIMITIVE"]
  set lut 0; set ff 0; set ram 0
  foreach c $cells {
    set t [get_property REF_NAME $c]
    if {[string match LUT* $t]} { incr lut } elseif {[string match FD* $t]} { incr ff } elseif {[string match RAM* $t]} { incr ram }
  }
  puts "=== cells $pat: LUT $lut FF $ff RAM-primitives $ram (primitive cells [llength $cells])"
}
set direct [get_cells -hier -filter {NAME =~ *scpu_t/cpu/* && IS_PRIMITIVE && NAME !~ *cpu816*}]
foreach c $direct { puts "=== glue [get_property REF_NAME $c] [get_property NAME $c]" }
