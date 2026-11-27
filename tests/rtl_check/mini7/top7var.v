// tests/rtl_check/mini7 (T239.3, apple2's a2_regs.vhd): the request on the top's own ports into m7var, whose write branch assigns the address to a process variable.
module top7var (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    m7var regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
