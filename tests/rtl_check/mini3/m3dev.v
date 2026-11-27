// The module behind the continuous assignments of m3pipe (trx_engine.v behind trx_sys.v in miniature): a register written by the strobe and the address it is handed.
module m3dev (
    input  wire       clk,
    input  wire       we,
    input  wire [7:0] addr,
    input  wire [7:0] wdata
);
    reg [7:0] r4 = 8'd0;
    always @(posedge clk)
        if (we && addr == 8'h40) r4 <= wdata;
endmodule
