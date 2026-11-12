// T140 D24 item 1: c128_avpins on one-hot test words; prints the AVID_R/G/B buses (hex) for check_avpins.py. -DSWAP=0 builds the first order.
`timescale 1ns/1ps
`ifndef SWAP
`define SWAP 1
`endif
module tb_c128_avpins;
    reg [7:0] y, c, cv;
    wire [9:2] r, g, b;
    c128_avpins #(.SWAP_YCV(`SWAP)) dut (.y(y), .c(c), .cv(cv), .AVID_R(r), .AVID_G(g), .AVID_B(b));
    integer i;
    initial begin
        // one-hot words: every bit of every word shows up on exactly one ball
        for (i = 0; i < 8; i = i + 1) begin
            y = 8'd1 << i; c = 8'd1 << ((i + 3) % 8); cv = 8'd1 << ((i + 5) % 8);
            #1 $display("W %0d %02x %02x %02x R %02x G %02x B %02x", i, y, c, cv, r, g, b);
        end
        $finish;
    end
endmodule
