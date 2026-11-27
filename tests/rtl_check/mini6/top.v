module top (input wire clk, input wire rst, input wire [7:0] j_addr, input wire j_we);
    wire dev_we, dev_re, rq_tgl, rq_we, rs_tgl, d_we;
    wire [7:0] d_addr, d_wdata, dev_addr, dev_wdata, dev_rdata, ctl_out, rq_addr, rq_wdata, rs_rdata;
    msys sys (.clock(clk), .reset(rst), .dev_we(dev_we), .dev_re(dev_re), .dev_addr(dev_addr),
              .dev_wdata(dev_wdata), .dev_rdata(dev_rdata), .ctl_out(ctl_out));
    m6io io (.clk(clk), .we(dev_we), .re(dev_re), .addr(dev_addr), .wdata(dev_wdata), .rdata(dev_rdata),
             .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata), .rs_tgl(rs_tgl), .rs_rdata(rs_rdata));
    m6pipe pipe (.clk(clk), .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata),
                 .rs_tgl(rs_tgl), .rs_rdata(rs_rdata), .j_addr(j_addr), .j_we(j_we), .d_we(d_we), .d_addr(d_addr), .d_wdata(d_wdata));
    m6dev dev (.clk(clk), .we(d_we), .addr(d_addr), .wdata(d_wdata));
endmodule
