`timescale 1ns/1ps
module tb;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1;
    // --- TMDS round trip -----------------------------------------------
    reg [7:0] d0, d1; reg [1:0] c0, c1; reg de0, de1;
    wire [19:0] q;
    tmds_pair u (.clk(clk), .rst(rst), .d0(d0), .d1(d1), .c0(c0), .c1(c1), .de0(de0), .de1(de1), .q(q));
    function [9:0] dec; input [9:0] s;          // {is_ctrl, ctrl[1:0], ... , data[7:0]}
        reg [7:0] t; integer i;
        begin
            case (s)
                10'b1101010100: dec = {1'b1, 1'b0, 8'h00};
                10'b0010101011: dec = {1'b1, 1'b0, 8'h01};
                10'b0101010100: dec = {1'b1, 1'b0, 8'h02};
                10'b1010101011: dec = {1'b1, 1'b0, 8'h03};
                default: begin
                    t = s[9] ? ~s[7:0] : s[7:0];
                    dec[0] = t[0];
                    for (i = 1; i < 8; i = i + 1) dec[i] = s[8] ? (t[i] ^ t[i-1]) : ~(t[i] ^ t[i-1]);
                    dec[9:8] = 2'b00;
                end
            endcase
        end
    endfunction
    integer n, errs = 0, ones = 0, bits = 0, k;
    reg [7:0] e0, e1; reg ed0, ed1; reg [1:0] ec0, ec1;
    initial begin
        d0 = 0; d1 = 0; c0 = 0; c1 = 0; de0 = 0; de1 = 0;
        repeat (3) @(posedge clk); rst = 0;
        for (n = 0; n < 20000; n = n + 1) begin
            @(negedge clk);
            // check the word from the previous inputs
            if (n > 0) begin
                if (ed0 && dec(q[9:0]) !== {2'b00, e0}) errs = errs + 1;
                if (ed1 && dec(q[19:10]) !== {2'b00, e1}) errs = errs + 1;
                if (!ed0 && dec(q[9:0]) !== {2'b10, 6'd0, ec0}) errs = errs + 1;
                if (!ed1 && dec(q[19:10]) !== {2'b10, 6'd0, ec1}) errs = errs + 1;
                if (ed0 && ed1) for (k = 0; k < 20; k = k + 1) begin ones = ones + q[k]; bits = bits + 1; end
            end
            d0 = $random; d1 = $random; c0 = $random; c1 = $random;
            de0 = (n % 50) > 5; de1 = de0;
            e0 = d0; e1 = d1; ed0 = de0; ed1 = de1; ec0 = c0; ec1 = c1;
        end
        $display("TMDS: %0d symbol errors, DC balance %0d ones of %0d bits in data", errs, ones, bits);
        $finish;
    end
endmodule
