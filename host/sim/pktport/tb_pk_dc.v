`timescale 1ns/1ps
// T103: a core swap (core_decouple) in the middle of a frame. The core writes 10 of 20 bytes, dc rises and top.v forces
// the port idle (valid = 0, req = 0). The FIFO must close the frame with an eof byte, so the MAC side sees the 10 bytes
// with an end and then the next whole frame, never the two glued together. Control -DNEG_NODC: dc stays 0, the half frame has
// no end and the following frame is glued to it (the bench must see that and say FAIL).
module tb_pk_dc;
    reg clk50 = 0, cpu_clk = 0;
    always #10 clk50 = ~clk50;
    always #5  cpu_clk = ~cpu_clk;
    reg rst = 1; initial #200 rst = 0;
    reg dc = 0, req = 0, valid = 0, eof = 0; reg [7:0] data = 0;
    wire grant, ready, st_req, st_valid, st_eof; wire [7:0] st_data;
    reg own = 0; wire pop = own & st_valid;
    pktport_host h (.wclk(cpu_clk), .wrst(rst), .dc(dc), .pp_req(req), .pp_grant(grant), .pp_data(data),
        .pp_valid(valid), .pp_eof(eof), .pp_ready(ready),
        .rclk(clk50), .rrst(rst), .st_req(st_req), .st_data(st_data), .st_valid(st_valid), .st_eof(st_eof), .pop(pop));
    // MAC side: takes a frame when st_req, one byte per clk50 (pop = valid)
    integer n = 0, nf = 0, len [0:7]; reg [7:0] first [0:7]; reg [7:0] b;
    always @(posedge clk50) begin
        if (!own && st_req) own <= 1;
        if (pop) begin
            if (n == 0) first[nf] <= st_data;
            n <= n + 1;
            if (st_eof) begin len[nf] <= n + 1; nf <= nf + 1; n <= 0; own <= 0; end
        end
    end
    task send(input integer count, input integer start, input integer last_eof);
        integer i;
        begin
            req <= 1; @(posedge cpu_clk); while (!grant) @(posedge cpu_clk);
            for (i = 0; i < count; i = i + 1) begin
                valid <= 1; data <= start + i; eof <= (i == count - 1) && last_eof;
                @(posedge cpu_clk); while (!ready) @(posedge cpu_clk);
            end
            valid <= 0; eof <= 0;
            if (last_eof) req <= 0;
        end
    endtask
    integer errs = 0;
    initial begin
        @(negedge rst);
        repeat (10) @(posedge cpu_clk);
        send(10, 8'h10, 0);            // half a frame, no eof
`ifndef NEG_NODC
        dc <= 1; req <= 0; valid <= 0;   // top.v: port idle while decoupled
        repeat (40) @(posedge cpu_clk);
        dc <= 0;
`else
        repeat (40) @(posedge cpu_clk);
        req <= 0;
`endif
        repeat (10) @(posedge cpu_clk);
        send(20, 8'h50, 1);            // the next core's first whole frame
        repeat (400) @(posedge clk50);
        if (nf != 2) begin $display("FAIL: %0d frames at the MAC side, want 2", nf); errs = errs + 1; end
        else begin
            if (len[0] !== 11) begin $display("FAIL: closed frame is %0d bytes, want 11 (10 + eof)", len[0]); errs = errs + 1; end
            if (len[1] !== 20 || first[1] !== 8'h50) begin $display("FAIL: next frame %0d bytes starting %h, want 20 / 50", len[1], first[1]); errs = errs + 1; end
        end
        if (errs == 0) $display("PASS pk_dc (frames: %0d + %0d bytes)", len[0], len[1]); else $display("FAIL pk_dc");
        $finish;
    end
endmodule
