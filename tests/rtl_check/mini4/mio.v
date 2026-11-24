// mini4: the dev module of mini with a generate-for that is no counting loop, a pattern rtl_check does not know: the block that reaches it is stopped, the block that does not is checked.
module mio #(parameter G = 0) (
    input  wire       clk,
    input  wire       we,
    input  wire       re,
    input  wire [7:0] addr,
    input  wire [7:0] wdata,
    output wire [7:0] rdata
);
    reg [7:0] a = 8'd0;
    reg [7:0] q = 8'd0;
    reg [3:0] bh = 4'd0;
    reg [3:0] bl = 4'd0;
    reg [7:0] m0 = 8'd0;
    reg [7:0] m1 = 8'd0;
    reg [7:0] g = 8'd0;
    integer k;
    always @(posedge clk) begin
        if (we && addr == 8'h10) a <= wdata;
        if (we && addr == 8'h12) {bh, bl} <= wdata;
        for (k = 0; k < 2; k = k + 1)
            if (we && addr == 8'h14 + k) begin
                if (k == 0) m0 <= wdata;
                else m1 <= wdata;
            end
        q <= 8'd0;
        if (re)
            case (addr)
            8'h10: q <= a;
            8'h11: q <= 8'hA5;
            8'h12: q <= {bh, bl};
            8'h14: q <= m0;
            8'h15: q <= m1;
            8'h18: q <= g;
            default: ;
            endcase
    end
    generate
        if (G == 1) begin : gg
            always @(posedge clk)
                if (we && addr == 8'h18) g <= wdata;
        end
    endgenerate
    genvar gi;
    generate
        for (gi = 1; gi < 8; gi = gi * 2) begin : pw
            reg [7:0] x = 8'd0;
        end
    endgenerate
    assign rdata = q;
endmodule
