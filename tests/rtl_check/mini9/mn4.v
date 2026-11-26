// tests/rtl_check/mini9 (T239.3 review 9): a reverse case whose choice is an inequality (`case (1'b1) (addr != 8'h20): q <= 8'h00`): no read.
module mn4 (input wire clk, input wire we, input wire re, input wire mode, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    assign rdata = q;
    always @(posedge clk) begin
        q <= 8'h00;
        if (we && addr == 8'h20) r <= wdata;
        if (re)
            case (1'b1)
            (addr != 8'h20): q <= 8'h00;
            default: q <= r;
            endcase
    end
endmodule
