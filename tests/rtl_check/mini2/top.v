module top (input wire clk, input wire rst);
    m2sys sys (.clock(clk), .reset(rst));
endmodule
