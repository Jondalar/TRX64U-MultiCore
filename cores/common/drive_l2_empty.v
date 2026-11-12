// T79 measurement only (T79_NO_DRIVE=1, never for the board): drive_l2 with every output at
// its idle level, read instead of drive_l2.vhd to measure layer 2 without the 1541.
module drive_l2 (
  input clock,
  input reset,
  input tick_16MHz,
  input tick_4MHz,
  input tick_1kHz,
  input drv_io_read,
  input drv_io_write,
  input [23:0] drv_io_addr,
  input [7:0] drv_io_wdata,
  output [7:0] drv_io_rdata,
  output drv_io_ack,
  output drv_io_nc,
  input gcr_io_read,
  input gcr_io_write,
  input [23:0] gcr_io_addr,
  input [7:0] gcr_io_wdata,
  output [7:0] gcr_io_rdata,
  output gcr_io_ack,
  output gcr_io_nc,
  output dp_req_tgl,
  output [25:0] dp_addr,
  output dp_we,
  output [31:0] dp_wdata,
  output [3:0] dp_be,
  input dp_done_tgl,
  input [127:0] dp_rdata,
  input [7:0] snoop_tag,
  input snoop_request,
  input snoop_rwn,
  input [25:0] snoop_addr,
  input [3:0] snoop_be,
  input [31:0] snoop_wdata,
  input [31:0] snoop_rdata,
  input snoop_rack,
  input [7:0] snoop_rack_tag,
  input [7:0] snoop_dack_tag,
  input drive_stop,
  input halt,
  input c64_reset_n,
  input iec_reset_n,
  input iec_atn_i,
  input iec_clk_i,
  input iec_data_i,
  output iec_clk_o,
  output iec_data_o,
  output [2:0] leds_n,
  output [255:0] dbg,
  output [31:0] cpu_trace,
  output cpu_trace_valid);
  assign drv_io_rdata = 8'd0;
  assign drv_io_ack = 1'd0;
  assign drv_io_nc = 1'd0;
  assign gcr_io_rdata = 8'd0;
  assign gcr_io_ack = 1'd0;
  assign gcr_io_nc = 1'd0;
  assign dp_req_tgl = 1'd0;
  assign dp_addr = 26'd0;
  assign dp_we = 1'd0;
  assign dp_wdata = 32'd0;
  assign dp_be = 4'd0;
  assign iec_clk_o = 1'd1;
  assign iec_data_o = 1'd1;
  assign leds_n = 3'd7;
  assign dbg = 256'd0;
  assign cpu_trace = 32'd0;
  assign cpu_trace_valid = 1'd0;
endmodule
