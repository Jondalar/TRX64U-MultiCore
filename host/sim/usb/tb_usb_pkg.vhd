-- tb_usb: what rvlite_bfm tells the testbench
package tb_usb_pkg is
    signal bfm_step   : natural := 0;
    signal bfm_errors : natural := 0;
end package;
