// tests/rtl_check/mini9 (T239.3 review 9): a Verilog arm of a region (`case (addr[7:4]) 4'h2: q <= 8'h00`) before the compare of 0x20: no read.
module mn3 (input wire clk, input wire we, input wire re, input wire mode, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    assign rdata = q;
    always @(posedge clk) begin
        q <= 8'h00;
        if (we && addr == 8'h20) r <= wdata;
        if (re) begin
            case (addr[7:4])
            4'h2: q <= 8'h00;
            default: ;
            endcase
            if (addr == 8'h20) q <= r;
        end
    end
endmodule
