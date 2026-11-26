// tests/rtl_check/mini9 (T239.3 review 9): an arm of a region that answers 0 before an if on the low bits: no read.
module mn7 (input wire clk, input wire we, input wire re, input wire mode, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    assign rdata = q;
    always @(posedge clk) begin
        q <= 8'h00;
        if (we && addr == 8'h20) r <= wdata;
        if (re)
            case (addr[7:4])
            4'h2: begin
                q <= 8'h00;
                if (re && addr[3:0] == 4'h0) q <= r;
            end
            default: ;
            endcase
    end
endmodule
