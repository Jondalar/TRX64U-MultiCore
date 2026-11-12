// T134: the iverilog benches (boot, models) do not carry the disk drive (SystemVerilog, run_disk.sh covers it): this stand-in has pet_ieee's ports,
// answers nothing on the bus (every line reads back what the PET drives, as before the drive was there) and keeps the DDR2 port idle.
`timescale 1ns / 1ps
module pet_ieee (
    input  wire sys_clk, input wire cpu_clk, input wire rst,
    input  wire [7:0] p_data_o, input wire p_atn_o, input wire p_ndac_o, input wire p_nrfd_o, input wire p_dav_o, input wire p_eoi_o,
    output wire [7:0] p_data_i, output wire p_atn_i, output wire p_ndac_i, output wire p_nrfd_i, output wire p_dav_i, output wire p_eoi_i,
    input  wire r_en, input wire r_4040, input wire [31:0] r_size0, input wire [31:0] r_size1, input wire [1:0] r_ro, input wire [1:0] r_mnt_tgl,
    input  wire [31:0] r_base0, input wire [31:0] r_base1, input wire [1:0] r_clr_dirty_tgl,
    output wire [1:0] s_dirty, output wire [1:0] s_led, output wire s_led_err,
    output wire ev_tgl, output wire [7:0] ev_chunk, output wire ev_unit,
    input  wire rom_wr, input wire rom_sel, input wire [14:0] rom_addr, input wire [7:0] rom_data,
    output wire drv_tgl, output wire [25:0] drv_addr, output wire drv_we, output wire [31:0] drv_wdata, output wire [3:0] drv_be,
    input  wire drv_done_tgl, input wire [127:0] drv_rdata
);
    assign p_data_i = p_data_o; assign p_atn_i = p_atn_o; assign p_ndac_i = p_ndac_o; assign p_nrfd_i = p_nrfd_o; assign p_dav_i = p_dav_o; assign p_eoi_i = p_eoi_o;
    assign s_dirty = 2'b00; assign s_led = 2'b00; assign s_led_err = 1'b0; assign ev_tgl = 1'b0; assign ev_chunk = 8'd0; assign ev_unit = 1'b0;
    assign drv_tgl = 1'b0; assign drv_addr = 26'd0; assign drv_we = 1'b0; assign drv_wdata = 32'd0; assign drv_be = 4'd0;
endmodule
