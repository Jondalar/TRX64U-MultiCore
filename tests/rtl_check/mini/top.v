module top (input wire clk, input wire rst);
    wire dev_we, dev_re;
    wire [7:0] dev_addr, dev_wdata, dev_rdata, ctl_out;
    msys sys (.clock(clk), .reset(rst), .dev_we(dev_we), .dev_re(dev_re), .dev_addr(dev_addr),
              .dev_wdata(dev_wdata), .dev_rdata(dev_rdata), .ctl_out(ctl_out));
    mio #(.G(1)) dev (.clk(clk), .we(dev_we), .re(dev_re), .addr(dev_addr), .wdata(dev_wdata), .rdata(dev_rdata));
endmodule
