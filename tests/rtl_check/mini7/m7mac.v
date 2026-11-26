// tests/rtl_check/mini7 (T239.3, snes_core.v:236-240): the module of an instance is a `define of this file, under `ifndef; the instance's ports go on in the next lines.
// `ifdef, `elsif and `undef decide on the file's own `define: a wrong branch leaves the memory unwritten or the answer 0.
module m7mac (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata
);
    wire       sel = (addr == 8'h10);
    wire       mem_we;
    wire [7:0] memq;
`ifndef M7_MEM_MOD                                      // a bench would name another memory here
`define M7_MEM_MOD m7mem
`endif
`ifdef M7_MEM_MOD                                       // the `define above is seen: the memory is written
    assign mem_we = we & sel;
`else
    assign mem_we = 1'b0;
`endif
`define M7_ALT
`undef M7_ALT
`ifdef M7_ALT                                           // undefined again: not this branch
`define M7_RD 8'h00
`elsif M7_MEM_MOD                                       // this one: the answer is the memory
`define M7_RD memq
`else
`define M7_RD 8'h00
`endif
    `M7_MEM_MOD mem (
        .clk(clk), .we(mem_we),
        .wdata(wdata), .q(memq));
    assign rdata = sel ? `M7_RD : 8'h00;
endmodule
