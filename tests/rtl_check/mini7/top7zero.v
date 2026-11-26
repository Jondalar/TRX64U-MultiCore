// tests/rtl_check/mini7 (T239.3, c64_basic's command_protocol.vhd): the request on the top's own ports into m7zero, whose decoded read answers the constant 0.
module top7zero (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    m7zero regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
