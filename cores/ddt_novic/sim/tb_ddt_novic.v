// T168: the bench of the DDT "Look Ma, No VIC" core at the partition's real edge: layer2_ddt_novic with all its ports (declared from the module header by mk_l2ports.py),
// the host's general DDR2 port e_* modelled (ddr2e_model.v of the Mandelbrot bench), the application's side played over the cw_* bus.
//   +MODE=pix   the core starts in hold; the bench puts the scroller's frame counter at +F0 so that the zoom envelope (every 512 frames) starts in the first few frames; the
//               core runs; at three points the bench stops it with Freeze, reads FRAME_SEQ and FB_LATEST, and compares the newest complete buffer pixel for pixel with picture
//               n = FRAME_SEQ - 1 of the model (novic_model.py, written from DDT's code); Freeze is released between the points
// The PASS / FAIL lines are the verdict.
`timescale 1ns / 1ps
module tb_ddt_novic;
`include "ports.vh"
    integer fails = 0;
    parameter integer NFR = 24;                      // pictures in the model file
    parameter integer F0 = 508;                      // the scroller's frame_mod512 when the core starts
    reg [8 * 200 - 1:0] exp_f;
    reg [15:0] exp [0:NFR * 70400 - 1];

    always #5 cpu_clk = ~cpu_clk;
    always #15.859 sys_clk = ~sys_clk;
    always #40.6875 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;
    always #15.859 c64_gclk = ~c64_gclk;
    wire e_rdy_m;
    always @* e_rdy = e_rdy_m;

    layer2_ddt_novic dut (.*);
    ddr2e_model u_ddr (.ui_clk(ui_clk), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy_m), .e_wdf_data(e_wdf_data),
                       .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    task cw_write(input [11:0] a, input [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr = {5'd0, a}; cw_wdata = d; cw_we = 1;
            @(posedge cpu_clk); cw_we = 0;
            @(posedge cpu_clk);
        end
    endtask
    task cw_read(input [11:0] a, output [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr = {5'd0, a}; cw_re = 1;
            @(posedge cpu_clk); cw_re = 0;
            @(posedge cpu_clk); @(posedge cpu_clk); d = cw_rdata;
        end
    endtask
    reg [7:0] rd, seq, lat;
    integer fd, npt = 0;
    reg [31:0] id;
    integer k;

    // stop the core at the next frame count >= target (Freeze), read what the application reads, compare the newest complete buffer with the model
    task point(input integer target, input [8 * 24 - 1:0] what);
        integer b, r, c, bad, n, word, lane, guard;
        reg [127:0] wv;
        reg [15:0] got, want;
        begin
            seq = 0; guard = 0;
            while (seq < target && guard < 2000) begin
                #500000;
                cw_read(12'h0C1, seq);
                guard = guard + 1;
            end
            cw_write(12'h0A0, 8'h02);                  // Freeze
            #3000000;
            cw_read(12'h0A4, rd);
            if (!rd[1]) begin $display("FAIL %0s: the core does not report held", what); fails = fails + 1; end
            cw_read(12'h0C1, seq);
            cw_read(12'h0C0, lat);
            if (!lat[7]) begin $display("FAIL %0s: FB_LATEST says no complete frame (%h)", what, lat); fails = fails + 1; end
            b = lat[1:0];
            n = seq - 1;
            if (n < 0 || n >= NFR) begin $display("FAIL %0s: picture %0d is not in the model file", what, n); fails = fails + 1; n = 0; end
            bad = 0;
            for (r = 0; r < 200; r = r + 1)
                for (c = 0; c < 352; c = c + 1) begin
                    word = (b * 32'h100000 + r * 704 + c * 2) >> 4;
                    lane = (c * 2) & 15;
                    wv = u_ddr.mem[word];
                    got = wv[8 * lane +: 16];
                    want = exp[n * 70400 + r * 352 + c];
                    if (got !== want) begin
                        bad = bad + 1;
                        if (bad == 1) $display("     first difference at row %0d col %0d: core %h, model %h", r, c, got, want);
                    end
                end
            if (bad != 0) begin                          // keep what the core wrote, for the model's comparison tool
                fd = $fopen($sformatf("got_%0d.hex", npt), "w");
                for (r = 0; r < 200; r = r + 1)
                    for (c = 0; c < 352; c = c + 1) begin
                        wv = u_ddr.mem[(b * 32'h100000 + r * 704 + c * 2) >> 4];
                        $fwrite(fd, "%04x\n", wv[8 * ((c * 2) & 15) +: 16]);
                    end
                $fclose(fd);
            end
            npt = npt + 1;
            if (bad == 0) $display("ok   %0s: buffer %0d equals picture %0d of the model, 70400 pixels", what, b, n);
            else begin $display("FAIL %0s: %0d of 70400 pixels differ from picture %0d of the model (buffer %0d)", what, bad, n, b); fails = fails + 1; end
            cw_write(12'h0A0, 8'h00);                  // release
        end
    endtask

    initial begin
        if (!$value$plusargs("EXP=%s", exp_f)) exp_f = "exp.hex";
        $readmemh(exp_f, exp);
        sys_rst = 1; cpu_rst = 1; mclk_rst = 1; ui_rst = 1;
        #200;
        cw_write(12'h0A0, 8'h01);                      // hold, while the resets are still up: the raster must not have started
        #1000; sys_rst = 0; cpu_rst = 0; mclk_rst = 0; ui_rst = 0;
        #2000;
        for (k = 0; k < 4; k = k + 1) begin cw_read(k, rd); id[8 * k +: 8] = rd; end
        if (id === 32'h4E544444) $display("ok   ID through the register window is DDTN"); else begin $display("FAIL ID %h", id); fails = fails + 1; end
        // the application's start: frame buffer registers (the descriptor's values), release
        cw_write(12'h0C4, 8'h00); cw_write(12'h0C5, 8'h00); cw_write(12'h0C6, 8'hD0); cw_write(12'h0C7, 8'h01);
        cw_write(12'h0C8, 8'h00); cw_write(12'h0C9, 8'h00); cw_write(12'h0CA, 8'h10); cw_write(12'h0CB, 8'h00);
        cw_read(12'h0A8, rd);
        if (rd !== 8'd0) begin $display("FAIL frame counter is %0d in hold", rd); fails = fails + 1; end
        dut.novic.u_video.u_scroll.frame_mod512 = F0;   // time warp: the zoom envelope starts after 511 - F0 frames instead of 511
        #20000;
        cw_write(12'h0A0, 8'h00);
        point(2, "picture after 2 frames");
        point(9, "zoom envelope running");
        point(14, "later picture");
        if (u_ddr.stray != 0) begin $display("FAIL %0d writes outside the frame buffers", u_ddr.stray); fails = fails + 1; end
        else $display("ok   no DDR2 write outside the four buffers (%0d words written)", u_ddr.writes);
        if (fails == 0) $display("PASS ddt_novic pix"); else $display("FAILED ddt_novic pix (%0d)", fails);
        $finish;
    end
endmodule
