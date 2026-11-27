// tests/rtl_check/mini7 (T239.3, the SNES core's snes_core.v): the request on the top's own ports into m7mac, whose memory instance takes its module from a `define.
module top7mac (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    m7mac core (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
