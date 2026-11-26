// tests/rtl_check/mini8 (T239.3 review 7): the request on the top's own ports into m8fn.
module top8fn (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    wire [31:0] bstamp;
    wire [63:0] st;
    m8fn regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .build(bstamp), .stat(st));
endmodule
