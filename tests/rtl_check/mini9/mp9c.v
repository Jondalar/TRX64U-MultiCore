// tests/rtl_check/mini9 (T239.3 review 9): the control of p9: the compare names addr itself (`addr == 8'h20`): no write of 0x25.
module mp9c (input wire clk, input wire we, input wire re, input wire mode, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    reg [7:0] sel;
    reg wr_en;
    assign rdata = q;
    always @* begin
        sel = 8'h20;
        wr_en = 1'b0;
        if (mode) begin
            sel = addr;
            if (we && addr == 8'h20) wr_en = 1'b1;
        end
    end
    always @(posedge clk) begin
        q <= 8'h00;
        if (wr_en) r <= wdata;
        if (re && addr == 8'h20) q <= r;
    end
endmodule
