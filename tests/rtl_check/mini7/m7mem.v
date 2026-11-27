// tests/rtl_check/mini7: the memory behind m7mac's `define, one register.
module m7mem (
    input  wire       clk,
    input  wire       we,
    input  wire [7:0] wdata,
    output wire [7:0] q
);
    reg [7:0] r = 8'h00;
    always @(posedge clk) if (we) r <= wdata;
    assign q = r;
endmodule
