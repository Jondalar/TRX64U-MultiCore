// tests/rtl_check/mini8 (T239.3 review): the request on the top's own ports into mf2.
module topf2 (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    mf2 regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
