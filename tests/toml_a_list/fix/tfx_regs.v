// fixture decoder (Verilog): page 0, byte offsets
module tfx_regs(input clk, input we, input re, input [16:0] addr, input [7:0] wdata, output reg [7:0] q);
    wire win = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    reg [7:0] mode_r;
    reg [31:0] base_r;
    always @(posedge clk) begin
        if (we && !win && page == 4'h0) begin
            case (off)
                12'h010: mode_r <= wdata;
                12'h014: base_r[7:0] <= wdata;
                12'h015: base_r[15:8] <= wdata;
                12'h016: base_r[23:16] <= wdata;
                12'h017: base_r[31:24] <= wdata;
            endcase
        end
        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                12'h000, 12'h001, 12'h002, 12'h003: q <= 8'h55;
                12'h004: q <= 8'hC0;
                12'h005: q <= 8'h02;
                12'h008: q <= 8'h01;
                12'h009: q <= 8'h01;
                12'h00A: q <= 8'h01;
                12'h00B: q <= 8'h01;
                12'h010: q <= mode_r;
                12'h014: q <= base_r[7:0];
                12'h015: q <= base_r[15:8];
                12'h016: q <= base_r[23:16];
                12'h017: q <= base_r[31:24];
            endcase
        end
    end
endmodule
