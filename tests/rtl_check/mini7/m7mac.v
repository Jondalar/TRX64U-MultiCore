// tests/rtl_check/mini7 (T239.3, snes_core.v:236-240): the module of an instance is a `define of this file, under `ifndef; the instance's ports go on in the next lines.
module m7mac (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata
);
    wire       sel = (addr == 8'h10);
    wire       mem_we = we & sel;
    wire [7:0] memq;
`ifndef M7_MEM_MOD                                      // a bench would name another memory here
`define M7_MEM_MOD m7mem
`endif
    `M7_MEM_MOD mem (
        .clk(clk), .we(mem_we),
        .wdata(wdata), .q(memq));
    assign rdata = sel ? memq : 8'h00;
endmodule
