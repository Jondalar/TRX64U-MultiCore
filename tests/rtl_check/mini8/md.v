// tests/rtl_check/mini8 (T239.3 review 1): the access type selects the arm (case {re, we}), the address is compared inside it: the 0 of the arm is no decode.
module md (
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
        case ({re, we})
            2'b10: begin
                q <= 8'h00;
                if (addr == 8'h20) q <= r;
            end
            2'b01: if (addr == 8'h20) r <= wdata;
            default: q <= 8'h00;
        endcase
    end
endmodule
