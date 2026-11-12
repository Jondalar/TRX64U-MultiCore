// T113: replaces MiSTer's vdcram.v (an Intel altsyncram instance) with a single-port block RAM, same ports. Written here.
module vdcram #(parameter DATA_WIDTH = 8, parameter ADDRESS_WIDTH = 16) (
    input  wire                     clk,
    input  wire                     rd,
    input  wire                     we,
    input  wire [ADDRESS_WIDTH-1:0] addr,
    input  wire [DATA_WIDTH-1:0]    dai,
    output reg  [DATA_WIDTH-1:0]    dao
);
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] m [0:(1<<ADDRESS_WIDTH)-1];
    always @(posedge clk) begin
        if (we) m[addr] <= dai;
        dao <= m[addr];
    end
endmodule
