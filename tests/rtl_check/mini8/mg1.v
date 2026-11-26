// tests/rtl_check/mini8 (T239.3 review 8): the 0 of an outer arm of addr[7:4] before a reverse case (case (1'b1)) whose choices decide on addr[3:0] is no decoded read.
module mg1 (input wire clk, input wire we, input wire re, input wire [7:0] addr, input wire [7:0] wdata, output wire [7:0] rdata);
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
                case (1'b1)
                (addr[3:0] == 4'h0): q <= r;
                default: ;
                endcase
            end
            default: ;
            endcase
    end
endmodule
