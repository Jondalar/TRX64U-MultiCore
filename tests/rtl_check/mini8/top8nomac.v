// tests/rtl_check/mini8 (T239.3 review 7): the module of the top file's instance is a macro the file does not define: refused, not skipped.
module top8nomac (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    `M8_NO_MOD regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
