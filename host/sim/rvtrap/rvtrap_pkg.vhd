-- Counters of the monitor (monitor.inc, spliced into a sim copy of core.vhd by run.sh) for the
-- testbench: global signals, driven by the monitor process only.
package rvtrap_pkg is
    constant c_prog : string := "prog.hex";
    signal mon_errors  : natural := 0;
    signal mon_retired : natural := 0;
    signal mon_ecalls  : natural := 0;
    signal mon_irqs    : natural := 0;
    signal mon_mrets   : natural := 0;
end package;
