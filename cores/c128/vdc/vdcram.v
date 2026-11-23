// T113: replaces MiSTer's vdcram.v (an Intel altsyncram instance) with a single-port block RAM, same ports. Written here.
// T257: a second, read-only port (addr_b / dao_b) on its own clock `clk_b`, for the side-effect-free read-back of the VRAM (c128_x.v vk_*). The idea and the port
// names are the MEGA65 core's (eilers/c128Mega65 vdcram.v `rd_b / addr_b / dao_b`, Stefan Eilers, GPL v3, see PROVENANCE.md); the code here is ours: the port reads
// every clock of `clk_b` (no `rd_b`). `clk` is the machine's gated clock, `clk_b` the ungated clock of the same MMCM output (c128_x.v), so the read-back also
// works while the machine is stopped (T257 §5 fault 3). A read of port B that meets a write of port A to the same address can return either byte.
module vdcram #(parameter DATA_WIDTH = 8, parameter ADDRESS_WIDTH = 16) (
    input  wire                     clk,
    input  wire                     clk_b,
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
    always @(posedge clk_b) dao_b <= m[addr_b];
endmodule
