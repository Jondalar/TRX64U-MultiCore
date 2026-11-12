// T168: the bench of the DDT Mandelbrot core at the partition's real edge: layer2_ddt_mandel with all its ports (declared from the module header by mk_l2ports.py),
// the host's general DDR2 port e_* modelled (ddr2e_model.v), the application's side played over the cw_* bus (ID, hold, frame buffer registers, the registers it polls).
//   +MODE=pix   the renderer finishes the default view (128 iterations), the frame writer fills the ring; the newest complete buffer is compared with the model
//               (exp_text.hex: the splash text screen is on), then the splash counter is run out and a later frame is compared with exp_plain.hex
//   +MODE=joy   as pix, then c64_joy2 holds fire and presses up (zoom in): after the renderer has finished again the buffer equals exp_zoom.hex
// The model (mandel_model.py) is written from DDT's code, not from this core. PASS / FAIL lines are the verdict.
`timescale 1ns / 1ps
module tb_ddt_mandel;
`include "ports.vh"
    integer fails = 0;
    reg [8 * 16 - 1:0] mode_s;
    reg [8 * 200 - 1:0] exp1_f, exp2_f, exp3_f;
    reg [15:0] exp1 [0:90111];
    reg [15:0] exp2 [0:90111];
    reg [15:0] exp3 [0:90111];

    always #5 cpu_clk = ~cpu_clk;
    always #15.859 sys_clk = ~sys_clk;
    always #40.6875 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;
    always #15.859 c64_gclk = ~c64_gclk;
    wire e_rdy_m;
    always @* e_rdy = e_rdy_m;

    layer2_ddt_mandel dut (.*);
    ddr2e_model u_ddr (.ui_clk(ui_clk), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy_m), .e_wdf_data(e_wdf_data),
                       .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // ---- the application's side
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
    reg [7:0] rd;
    reg [31:0] id;
    integer k;

    // the newest complete buffer, compared with an expectation
    task compare(input integer which, input [8 * 24 - 1:0] what);
        integer b, r, c, bad, first, word, lane;
        reg [127:0] wv;
        reg [15:0] got, want;
        begin
            cw_read(12'h0C0, rd);
            if (!rd[7]) begin $display("FAIL %0s: FB_LATEST says no complete frame (%h)", what, rd); fails = fails + 1; end
            b = rd[1:0];
            bad = 0; first = -1;
            for (r = 0; r < 256; r = r + 1)
                for (c = 0; c < 352; c = c + 1) begin
                    word = (b * 32'h100000 + r * 704 + c * 2) >> 4;
                    lane = (c * 2) & 15;
                    wv = u_ddr.mem[word];
                    got = wv[8 * lane +: 16];
                    want = (which == 1) ? exp1[r * 352 + c] : (which == 2) ? exp2[r * 352 + c] : exp3[r * 352 + c];
                    if (got !== want) begin
                        bad = bad + 1;
                        if (first < 0) begin first = r * 352 + c; $display("     first difference at row %0d col %0d: core %h, model %h", r, c, got, want); end
                    end
                end
            if (bad == 0) $display("ok   %0s: buffer %0d equals the model, 90112 pixels", what, b);
            else begin $display("FAIL %0s: %0d of 90112 pixels differ from the model (buffer %0d)", what, bad, b); fails = fails + 1; end
        end
    endtask

    task wait_render_done;
        integer n;
        reg seen_busy;
        begin
            seen_busy = 0;
            n = 0;
            while (!(seen_busy && rd[7] == 1'b0) && n < 4000) begin
                cw_read(12'h0AB, rd);
                if (rd[7]) seen_busy = 1;
                n = n + 1;
                #20000;
            end
            if (!seen_busy || rd[7]) begin $display("FAIL the renderer never went busy and idle (busy seen %0d, last %h)", seen_busy, rd); fails = fails + 1; end
        end
    endtask
    task wait_frames(input integer n);
        reg [7:0] s0, s1;
        integer got, guard;
        begin
            cw_read(12'h0C1, s0);
            got = 0; guard = 0;
            while (got < n && guard < 400) begin
                #1000000;
                cw_read(12'h0C1, s1);
                if (s1 !== s0) begin got = got + 1; s0 = s1; end
                guard = guard + 1;
            end
            if (got < n) begin $display("FAIL only %0d of %0d frames written", got, n); fails = fails + 1; end
        end
    endtask

    initial begin
        if (!$value$plusargs("MODE=%s", mode_s)) mode_s = "pix";
        if (!$value$plusargs("EXP1=%s", exp1_f)) exp1_f = "exp_text.hex";
        if (!$value$plusargs("EXP2=%s", exp2_f)) exp2_f = "exp_plain.hex";
        if (!$value$plusargs("EXP3=%s", exp3_f)) exp3_f = "exp_zoom.hex";
        $readmemh(exp1_f, exp1);
        $readmemh(exp2_f, exp2);
        $readmemh(exp3_f, exp3);
        sys_rst = 1; cpu_rst = 1; mclk_rst = 1; ui_rst = 1;
        #1000; sys_rst = 0; cpu_rst = 0; mclk_rst = 0; ui_rst = 0;
        #2000;
        for (k = 0; k < 4; k = k + 1) begin cw_read(k, rd); id[8 * k +: 8] = rd; end
        if (id === 32'h4D544444) $display("ok   ID through the register window is DDTM"); else begin $display("FAIL ID %h", id); fails = fails + 1; end
        // the application's start: hold, frame buffer registers (the descriptor's values), release
        cw_write(12'h0A0, 8'h01);
        cw_write(12'h0C4, 8'h00); cw_write(12'h0C5, 8'h00); cw_write(12'h0C6, 8'hD0); cw_write(12'h0C7, 8'h01);
        cw_write(12'h0C8, 8'h00); cw_write(12'h0C9, 8'h00); cw_write(12'h0CA, 8'h10); cw_write(12'h0CB, 8'h00);
        #20000;
        cw_write(12'h0A0, 8'h00);
        wait_render_done;
        wait_frames(3);
        compare(1, "default view with the splash text");
        // the splash lasts 501 frames; run its counter out
        dut.mandel.splash_frame_count = 9'd498;
        wait_frames(6);
        compare(2, "default view, splash over");
        if (mode_s == "joy") begin
            // fire + up: zoom in (joystick_buttons: zoom_in_pulse = fire & pressed_pulse[0]); bits: 0 up, 1 down, 2 left, 3 right, 4 fire
            c64_joy2 = 5'b10000;
            #3000000;
            c64_joy2 = 5'b10001;
            #3000000;
            c64_joy2 = 5'b10000;
            #3000000;
            c64_joy2 = 5'b00000;
            #3000000;
            wait_render_done;
            wait_frames(3);
            compare(3, "after fire + up (zoom in)");
        end
        if (u_ddr.stray != 0) begin $display("FAIL %0d writes outside the frame buffers", u_ddr.stray); fails = fails + 1; end
        else $display("ok   no DDR2 write outside the four buffers (%0d words written)", u_ddr.writes);
        if (fails == 0) $display("PASS ddt_mandel %0s", mode_s); else $display("FAILED ddt_mandel %0s (%0d)", mode_s, fails);
        $finish;
    end
endmodule
