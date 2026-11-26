// tests/rtl_check/mini7 (T239.3): the top file's instance takes its module from a `define of the top file (the scan of the top file puts it in).
`define M7_CORE_MOD m7mac
module top7tmac (
    input wire clk,
    input wire cw_we,
    input wire cw_re,
    input wire [7:0] cw_addr,
    input wire [7:0] cw_wdata,
    output wire [7:0] cw_rdata
);
    `M7_CORE_MOD core (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata));
endmodule
