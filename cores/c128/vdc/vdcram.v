// T113: replaces MiSTer's vdcram.v (an Intel altsyncram instance) with a single-port block RAM, same ports. Written here.
// T257: a second, read-only port (addr_b / dao_b) on the same clock, for the side-effect-free read-back of the VRAM (c128_x.v vk_*). The idea and the port names
// are the MEGA65 core's (eilers/c128Mega65 vdcram.v `rd_b / addr_b / dao_b`, Stefan Eilers, GPL v3, see PROVENANCE.md); the code here is ours: the port reads every
// clock (no `rd_b`), read-first, so a read that meets a write of port A at the same address returns the old byte.
module vdcram #(parameter DATA_WIDTH = 8, parameter ADDRESS_WIDTH = 16) (
    input  wire                     clk,
    input  wire                     rd,
    input  wire                     we,
    input  wire [ADDRESS_WIDTH-1:0] addr,
    input  wire [DATA_WIDTH-1:0]    dai,
    output reg  [DATA_WIDTH-1:0]    dao,
    input  wire [ADDRESS_WIDTH-1:0] addr_b,
    output reg  [DATA_WIDTH-1:0]    dao_b
);
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] m [0:(1<<ADDRESS_WIDTH)-1];
    always @(posedge clk) begin
        if (we) m[addr] <= dai;
        dao <= m[addr];
    end
    always @(posedge clk) dao_b <= m[addr_b];
endmodule
