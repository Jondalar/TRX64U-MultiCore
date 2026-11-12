# T149 (D5): the SD interface with IOB flops and its constraints, for the flat builds of amiga1200 and zxnext only.
# Sourced by cores/<amiga1200|zxnext>/mono/build_mono.tcl with host/ as the working directory, AFTER gideon_vhdl.tcl (it needs $gvhdl)
# and BEFORE read_vhdl; the constraints are read after pins.xdc (trx_sd_xdc). Nothing under host/ is edited (the host gateware is
# frozen, D5): the host's i_sd instantiation is changed in a COPY of ultimate_sys.vhd in the build directory.
#
# trx_sd_swap <build dir>: $gvhdl gets <build dir>/ultimate_sys_sd.vhd (ultimate_sys.vhd with `i_sd: entity work.spi_peripheral_io`
# turned into `i_sd: entity work.sd_spi_peripheral_io`) instead of ultimate_sys.vhd, and sd_spi.vhd in front of it. It fails, naming
# the file, when the instantiation is not there exactly once (the host changed and this recipe has to follow) or when the list does not
# hold ultimate_sys.vhd exactly once. i_flash keeps Gideon's block.
proc trx_sd_swap {bdir} {
    global hd
    upvar 1 gvhdl gvhdl
    set k [lsearch -exact $gvhdl $hd/ultimate_sys.vhd]
    if {$k < 0 || [lsearch -exact -start [expr {$k + 1}] $gvhdl ultimate_sys.vhd] >= 0} {
        error "T149 SD: ultimate_sys.vhd is not in \$gvhdl exactly once"
    }
    set fh [open $hd/ultimate_sys.vhd]; set txt [read $fh]; close $fh
    set n [regsub -all {i_sd: entity work\.spi_peripheral_io\y} $txt {i_sd: entity work.sd_spi_peripheral_io} txt]
    if {$n != 1} { error "T149 SD: 'i_sd: entity work.spi_peripheral_io' found $n times in ultimate_sys.vhd, expected exactly 1" }
    set fh [open $bdir/ultimate_sys_sd.vhd w]; puts -nonewline $fh $txt; close $fh
    set gvhdl [lreplace $gvhdl $k $k ../cores/amiga1200/sd/sd_spi.vhd $bdir/ultimate_sys_sd.vhd]
    puts "=== T149 SD: i_sd is sd_spi_peripheral_io (IOB flops), 25 MHz allowed; ultimate_sys.vhd copy in $bdir"
}

proc trx_sd_xdc {} { read_xdc ../cores/amiga1200/sd/sd_timing.xdc }

# trx_report_sd: the SD pins' flops and paths on the routed design. One line per flop (IN_IOB=1 when the site is an ILOGIC/OLOGIC;
# Vivado has no IS_IOB property on these cells), one per path group (worst slack of the pad paths).
proc trx_report_sd {} {
    set cells [get_cells -hier -quiet -filter {NAME =~ *i_sd/spi1/sck_o_reg* || NAME =~ *i_sd/spi1/mosi_o_reg* || NAME =~ *i_sd/spi1/ssn_o_reg* || NAME =~ *i_sd/spi1/miso_i_reg*}]
    if {[llength $cells] == 0} { puts "=== T149 SD IOB: WARNING no SD flops found" }
    foreach c $cells {
        set site [get_property SITE $c]
        puts "=== T149 SD IOB: $c SITE=$site IN_IOB=[expr {[regexp {^(ILOGIC|OLOGIC)} $site] ? 1 : 0}] IOB=[get_property IOB $c]"
    }
    foreach {tag sel} [list \
        miso [list -from [get_ports SD_MISO]] \
        out  [list -to [get_ports {SD_MOSI SD_SSn}]]] {
        foreach dt {max min} {
            set p [lindex [get_timing_paths -quiet -delay_type $dt -max_paths 1 {*}$sel] 0]
            if {$p eq ""} { puts "=== T149 SD paths: $tag $dt: none (unconstrained?)" } else {
                puts "=== T149 SD paths: $tag $dt: slack [get_property SLACK $p] ns, [get_property STARTPOINT_PIN $p] -> [get_property ENDPOINT_PIN $p]"
            }
        }
    }
}
