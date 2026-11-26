// tests/rtl_check/mini9 (T239.3 review 9): the top of mp7, the request on its own ports; mode is an input nothing drives (state).
module topp7 (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    wire mode_in;
    mp7 regs (.clk(clk), .we(cw_we), .re(cw_re), .mode(mode_in), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
