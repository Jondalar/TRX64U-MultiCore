# Helpers shared by the designs/*/build.tcl scripts.
#
# Two things are cached between builds, keyed by a hash of what goes into
# them (sources, settings, Vivado version):
#   - IP cores (the GTP wizard): generated and synthesized once.
#   - Out-of-context checkpoints of big, rarely changed blocks (the C64 core).
# The cache lives in $TRX_CACHE (tools/vivado.sh and CI mount a persistent
# directory there), else in .cache/ at the repository root.

# Where a build puts its reports: build.tcl sets TRX_BUILD's directory here.
if {![info exists ::trx_build]} { set ::trx_build build }

# Vivado threads: TRX_VIVADO_THREADS from the runner's config envs (builder: all of its
# cores), else 3 (the NAS has four and runs Gitea too; the Mac keeps to its limit).
if {[info exists ::env(TRX_VIVADO_THREADS)]} {
  set_param general.maxThreads $::env(TRX_VIVADO_THREADS)
} else {
  set_param general.maxThreads 3
}

proc trx_cache_dir {} {
    if {[info exists ::env(TRX_CACHE)] && $::env(TRX_CACHE) ne ""} {
        set d $::env(TRX_CACHE)
    } else {
        set d [file normalize [file join [file dirname [info script]] .. .cache]]
    }
    file mkdir $d
    return $d
}
# [info script] is only right while this file is being sourced.
set ::trx_cache [trx_cache_dir]

# Where the generated IP (MIG DDR2, the HDMI GT) is cached. Its key is a pure function of the
# IP configuration, the part, the contents and base names of the configuration files and the
# Vivado version (trx_hash below): it does not depend on where in the tree those files live, and
# ci/cache_gc.py never collects an ip_* directory. So this cache is shared by both product lines
# (TRX64U MultiCore and TRX64-Ultimate) while bit/, the out-of-context checkpoints and the
# handoff are per repository (T94 D7): TRX_IP_CACHE, by default the parent of TRX_CACHE when
# that is a .../multicore root, else TRX_CACHE itself.
proc trx_ip_cache_dir {} {
    if {[info exists ::env(TRX_IP_CACHE)] && $::env(TRX_IP_CACHE) ne ""} {
        set d $::env(TRX_IP_CACHE)
    } elseif {[file tail $::trx_cache] eq "multicore"} {
        set d [file dirname $::trx_cache]
    } else {
        set d $::trx_cache
    }
    file mkdir $d
    return $d
}
set ::trx_ip_cache [trx_ip_cache_dir]

# --- the DFX boundary (T87 D7) -------------------------------------------------
# Host-side infrastructure, not a core's: build.tcl (host stage) and core.tcl (every core's
# stage) both call these, so they live here and not in one core's recipe - the VC-20's stage of
# run 805 died with "invalid command name trx_keep_boundary" because it sourced the VC-20's
# recipe, which has no reason to know about them.
# T87 (runs 396 to 399): the boundary nets of layer 2 must stay real nets. A layer-2 output that the core
# ties to a constant is merged into GNDNet in the linked design; its load pins in the static are routed
# and locked as GNDNet, and a core carved out of that static fails with [Route 35-341] "Fixed routing
# constraint on net GNDNet conflicts with pin ..." (GND_HD_Inserted_Net). layer2_<core>.v puts a DONT_TOUCH LUT1
# behind every output seen constant (l2_keep); trx_check_boundary lists any that is still constant.
# DONT_TOUCH on every boundary net (runs 397, 398) is gone (run 399: it did not help against the
# constants, the l2_keep buffers in layer2_<core>.v do, and the extra constraints are not worth the placer risk);
# the proc stays as a no-op name for the callers in build.tcl and core.tcl.
proc trx_keep_boundary {} {
  puts "=== T87: boundary nets are kept real by the l2_keep buffers in layer2_<core>.v (no DONT_TOUCH on the nets)"
}
# Diagnostic (run 398 listed 53 such pins before layer2_<core>.v had the l2_keep buffers for them): layer-2
# output pins that are still on a constant net, all of them, at the given point of the flow. A constant
# pin that has loads in the static is the failure of runs 396 to 398; one without loads is harmless.
proc trx_check_boundary {when} {
  set const {}
  foreach p [get_pins -quiet -of_objects [get_cells l2] -filter {DIRECTION == OUT}] {
    set n [get_nets -quiet -of_objects $p]
    if {$n ne "" && [get_property TYPE $n] in {GROUND POWER}} { lappend const $p }
  }
  puts "=== T87 ($when): [llength $const] layer-2 output pins on a constant net: $const"
}

# sha256 over the given files (contents and names, in the given order) plus
# free text, cut to 16 hex digits.
proc trx_hash {files text} {
    set lines {}
    foreach f $files {
        lappend lines "[lindex [exec sha256sum $f] 0] [file tail $f]"
    }
    lappend lines $text [version -short]
    set tmp [file join $::trx_cache .hash.[pid]]
    set fh [open $tmp w]
    puts $fh [join $lines \n]
    close $fh
    set h [lindex [exec sha256sum $tmp] 0]
    file delete $tmp
    return [string range $h 0 15]
}

# Create, generate and synthesize an IP once per configuration; later builds
# only read it. config is the -dict list for set_property; key_files are files
# the configuration reads (e.g. a MIG .prj), hashed into the key as well and
# copied next to the IP; @DIR@ in config stands for that directory.
proc trx_ip {name vlnv config {key_files {}}} {
    # the part belongs in the key (a generated IP is part specific); the XC7A50T
    # keeps the key it always had, so the cache made before T53 stays valid
    set part [get_property PART [current_project]]
    set ptxt [expr {[string match xc7a50t* $part] ? "" : " $part"}]
    set dir [file join $::trx_ip_cache ip_${name}_[trx_hash $key_files "$vlnv $config$ptxt"]]
    set config [string map [list @DIR@ $dir] $config]
    set xci [file join $dir $name $name.xci]
    if {![file exists [file join $dir done]]} {
        file delete -force $dir
        file mkdir $dir
        foreach f $key_files { file copy -force $f $dir }
        lassign [split $vlnv :] vendor library ipname
        create_ip -name $ipname -vendor $vendor -library $library -module_name $name -dir $dir
        set_property -dict $config [get_ips $name]
        generate_target {synthesis} [get_ips $name]
        synth_ip [get_ips $name]
        close [open [file join $dir done] w]
        puts "=== ip $name: built into $dir"
    } else {
        read_ip $xci
        puts "=== ip $name: from cache $dir"
    }
}

# Out-of-context synthesis of module $top, cached. read_sources is a script
# that reads every source of the block (it runs in a fresh in-memory project);
# key_files are all files whose contents decide the result. Returns the
# checkpoint; the caller reads the stub before synth_design of the full design
# and fills the black box with read_checkpoint -cell afterwards.
proc trx_ooc {top part key_files read_sources synth_args} {
    set key [trx_hash $key_files "$top $part $synth_args $read_sources"]
    set dcp  [file join $::trx_cache ooc_${top}_$key.dcp]
    set stub [file join $::trx_cache ooc_${top}_$key.stub.v]
    if {![file exists $dcp] || ![file exists $stub]} {
        create_project -in_memory -part $part
        set_property target_language Verilog [current_project]
        uplevel #0 $read_sources
        synth_design -top $top -part $part -mode out_of_context {*}$synth_args
        report_utilization -hierarchical -file $::trx_build/utilization_${top}_ooc.rpt
        write_checkpoint -force $dcp.tmp.[pid]
        write_verilog -force -mode synth_stub $stub.tmp.[pid]
        file rename -force $dcp.tmp.[pid] $dcp
        file rename -force $stub.tmp.[pid] $stub
        close_project
        puts "=== ooc $top: synthesized into $dcp"
    } else {
        puts "=== ooc $top: from cache $dcp"
    }
    return [list $dcp $stub]
}

# Every file below dir, sorted.
proc trx_tree {dir} {
    set out {}
    foreach f [glob -nocomplain -directory $dir *] {
        if {[file isdirectory $f]} {
            lappend out {*}[trx_tree $f]
        } else {
            lappend out $f
        }
    }
    return [lsort $out]
}

# T84 D5b (Alex, 2026-10-14): the host's identity in the bitstream's AXSS register. It is the host
# KEY (first 8 hex of ci/lib.sh host_key), exported as TRX_HOST_KEY8 by ci/bitstream_layers.sh, so
# the number follows the host's sources and a cache hit keeps it; a hand build without the variable
# keeps the TIMESTAMP host/pins.xdc sets. This cannot live in the XDC: an XDC is restricted Tcl and
# an "if" there is a CRITICAL WARNING, the whole block is skipped and the bitstream comes out with
# no AXSS word at all (run 26). Call it on the open design right before every FULL write_bitstream
# (a partial carries no configuration registers).
proc trx_axss {} {
    if {[info exists ::env(TRX_HOST_KEY8)] && $::env(TRX_HOST_KEY8) ne ""} {
        set_property BITSTREAM.CONFIG.USR_ACCESS 0x$::env(TRX_HOST_KEY8) [current_design]
        puts "=== T84 D5b: USR_ACCESS = host key 0x$::env(TRX_HOST_KEY8)"
    } else {
        puts "=== T84 D5b: TRX_HOST_KEY8 is not set, USR_ACCESS stays as host/pins.xdc set it"
    }
}
