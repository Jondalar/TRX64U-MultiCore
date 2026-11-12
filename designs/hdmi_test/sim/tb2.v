`timescale 1ns/1ps
module tb2;
    reg clk = 0; always #10 clk = ~clk;        // 50 MHz
    reg rst = 1;
    wire scl_low, sda_low, done;
    cdce_i2c u (.clk(clk), .rst(rst), .scl_low(scl_low), .sda_low(sda_low), .done(done));
    wire scl = ~scl_low, sda = ~sda_low;
    // I2C monitor
    integer nb = 0, nbit = 0, starts = 0, stops = 0; reg [8:0] sh;
    always @(negedge sda) if (scl) begin starts = starts + 1; nbit = 0; $write("\n S"); end
    always @(posedge sda) if (scl) begin stops = stops + 1; $write(" P"); end
    always @(posedge scl) begin
        sh = {sh[7:0], sda}; nbit = nbit + 1;
        if (nbit == 9) begin $write(" %02h", sh[8:1]); nbit = 0; nb = nb + 1; end
    end
    // video timing
    reg vclk = 0; always #13.468 vclk = ~vclk;
    wire [23:0] a, b; wire de, hs, vs;
    video_720p v (.clk(vclk), .rst(rst), .rgb0(a), .rgb1(b), .de(de), .hs(hs), .vs(vs));
    integer clks = 0, hs_rise = -1, line_len = 0, de_clks = 0, lines = 0, vs_rise = -1, frame_lines = 0, hs_len = 0; reg phs = 0, pvs = 0;
    always @(posedge vclk) if (!rst) begin
        clks = clks + 1; if (de) de_clks = de_clks + 1; if (hs) hs_len = hs_len + 1;
        if (hs && !phs) begin if (hs_rise >= 0) line_len = clks - hs_rise; hs_rise = clks; lines = lines + 1; end
        if (vs && !pvs) begin if (vs_rise >= 0) frame_lines = lines - vs_rise; vs_rise = lines; end
        phs = hs; pvs = vs;
    end
    initial begin
        #100 rst = 0;
        wait (done);
        $display("\nI2C: %0d starts, %0d stops, %0d bytes; done at %0t", starts, stops, nb, $time);
        #40000000;
        $display("video: line %0d clocks (825), frame %0d lines (750), active %0d clocks per frame (460800), hsync %0d", line_len, frame_lines, de_clks, hs_len);
        $finish;
    end
endmodule
