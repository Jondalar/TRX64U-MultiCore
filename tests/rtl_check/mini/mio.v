module mio (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata
);
    reg [7:0] a = 8'd0;
    reg [7:0] q = 8'd0;
    always @(posedge clk) begin
        if (we && addr == 8'h10) a <= wdata;
        q <= 8'd0;
        if (re)
            case (addr)
            8'h10: q <= a;
            8'h11: q <= 8'hA5;
            default: ;
            endcase
    end
    assign rdata = q;
endmodule
