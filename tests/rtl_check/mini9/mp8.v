// tests/rtl_check/mini9 (T239.3 review 9): a plain case item `8'b0010_0???` (no match in synthesis, a match in casez): refused.
module mp8 (input wire clk, input wire we, input wire re, input wire mode, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
    reg [7:0] r = 8'h00;
    reg [7:0] q = 8'h00;
    assign rdata = q;
    always @(posedge clk) begin
        q <= 8'h00;
        if (we && addr == 8'h20) r <= wdata;
        if (re)
            case (addr)
            8'h20: q <= r;
            8'b0010_0???: q <= 8'h01;
            default: ;
            endcase
    end
endmodule
