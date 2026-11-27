// tests/rtl_check/mini8 (T239.3 review 1): the same without the arm on the access type: the 0 of 0x25 is the default before the if.
module md2 (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata
);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    assign rdata = q;
    always @(posedge clk) begin
        q <= 8'h00;
        if (re && addr == 8'h20) q <= r;
        if (we && addr == 8'h20) r <= wdata;
    end
endmodule
