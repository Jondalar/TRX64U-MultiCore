// T194: c128_act.v - per VIC frame the CPU's writes to the VIC, the colour RAM and the VDC, DEN at the frame's end, a frame number.
// Frame 1: 3 VIC, 5 colour RAM, 0 VDC writes (strobes that last several clocks count once), DEN 1. Frame 2: 300 VIC writes (saturate at 255), 1 VDC write, DEN 0.
// Frame 3: nothing. The values are latched with the wrap of the raster line to 0 and seen one frame later; a write in the clock of the frame edge belongs to the new frame.
`timescale 1ns / 1ps
module tb_act;
    reg clk = 0, rst = 1, vic_wr = 0, cram_wr = 0, vdc_wr = 0, den = 0;
    reg [8:0] line = 9'd5;                                // 5: a line inside the frame; the frame ends when it wraps to 0
    wire [39:0] q;
    always #5 clk = ~clk;
    c128_act dut (.clk(clk), .rst(rst), .vic_wr(vic_wr), .cram_wr(cram_wr), .vdc_wr(vdc_wr), .den(den), .line(line), .q(q));
    integer nbad = 0, i;
    task strobe(input integer which, input integer len);   // 1 vic, 2 cram, 4 vdc
        begin
            @(posedge clk); #1;
            if (which[0]) vic_wr = 1; if (which[1]) cram_wr = 1; if (which[2]) vdc_wr = 1;
            repeat (len) @(posedge clk);
            #1 vic_wr = 0; cram_wr = 0; vdc_wr = 0;
            repeat (2) @(posedge clk);
        end
    endtask
    task frame_end;
        begin @(posedge clk); #1 line = 9'd0; repeat (4) @(posedge clk); #1 line = 9'd1; repeat (2) @(posedge clk); #1 line = 9'd311; repeat (2) @(posedge clk); end
    endtask
    task chk(input [511:0] what, input [39:0] got, input [39:0] want);
        begin
            if (got !== want) begin $display("FAIL %0s: %010x, want %010x", what, got, want); nbad = nbad + 1; end
            else $display("ok   %0s", what);
        end
    endtask
    initial begin
        repeat (4) @(posedge clk); #1 rst = 0;
        den = 1;
        strobe(1, 6); strobe(1, 1); strobe(1, 3);          // 3 VIC writes of different length
        for (i = 0; i < 5; i = i + 1) strobe(2, 2);
        frame_end;
        chk("frame 1: seq 1, DEN, vdc 0, cram 5, vic 3", q, {8'd1, 8'h01, 8'd0, 8'd5, 8'd3});
        den = 0;
        for (i = 0; i < 300; i = i + 1) strobe(1, 1);
        strobe(4, 5);
        strobe(5, 2);                                       // VIC and VDC in the same clocks: both count
        frame_end;
        chk("frame 2: seq 2, no DEN, vdc 2, cram 0, vic 255", q, {8'd2, 8'h00, 8'd2, 8'd0, 8'hFF});
        frame_end;
        chk("frame 3: seq 3, nothing", q, {8'd3, 8'h00, 8'd0, 8'd0, 8'd0});
        // a write in the clock of the frame edge is the next frame's
        @(posedge clk); #1 line = 9'd0; vic_wr = 1; @(posedge clk); #1 vic_wr = 0; repeat (3) @(posedge clk); #1 line = 9'd1;
        repeat (2) @(posedge clk);
        frame_end;
        chk("frame 5: the write at the edge counted once, in frame 5", q, {8'd5, 8'h00, 8'd0, 8'd0, 8'd1});
        if (nbad == 0) $display("PASS tb_act"); else $display("FAIL tb_act: %0d wrong", nbad);
        $finish;
    end
endmodule
