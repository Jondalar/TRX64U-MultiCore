// T255: the bench of the Mandelbrot large core's picture path: ddt_mandel_hd (engines, frame buffer, 4 x scan-out, text, palette) behind the HDMI side's real timing generator
// (hdmi_timing, mode 2: 1080p50, the copy in mono/host), one whole frame of the output captured pixel by pixel and compared with the model (mandel_model.py, written from DDT's code).
//   +MODE=pix   the renderer finishes the default view (128 iterations); a frame with the splash text equals the model with text, then (the splash counter run out) a frame without it
//   +MODE=joy   as pix, then port 2 holds fire and presses up (zoom in): after the renderer has finished again the frame equals the model of the zoomed view
// Every pixel of the 1920 x 1080 active area is checked: the 1408 x 1024 window at (256, 28) is the model's picture, each pixel 4 x 4, the borders are black; the output's cx / cy counters
// travel with the pixel (the encoder depends on that), and the number of pixels with de is 1920 x 1080.
`timescale 1ns / 1ps
module tb_ddt_hd;
    parameter integer NE = 2;                      // engines (the design has 20; the picture does not depend on the number)
    reg cpu_clk = 0, pix_clk = 0;
    always #5 cpu_clk = ~cpu_clk;
    always #3.3585 pix_clk = ~pix_clk;             // 148.87 MHz

    integer fails = 0;
    reg [8 * 16 - 1:0] mode_s;
    reg [8 * 200 - 1:0] exp1_f, exp2_f, exp3_f;
    reg [15:0] exp1 [0:90111];
    reg [15:0] exp2 [0:90111];
    reg [15:0] exp3 [0:90111];

    // ---- the timing, as ddt_hdmi.v has it
    reg pix_rst = 1'b1;
    wire t_de, t_hs, t_vs, pce, running;
    wire [11:0] t_cx, xact0;
    wire [10:0] t_cy, yact0;
    hdmi_timing mtim (
        .clk(pix_clk), .rst(pix_rst), .mode(4'd2), .align(1'b0), .valid(), .aligned(),
        .ce(pce), .rgb(), .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy), .running(running),
        .hact(), .vact(), .xact0(xact0), .yact0(yact0), .gear(), .vic(), .rate(),
        .sx_e(), .fw_m1(), .sy_e(), .ey_e(), .ph_e(), .mode_q()
    );

    reg cpu_rst = 1'b1;
    reg [4:0] joy_n = 5'h1f;
    wire [23:0] rgb;
    wire de, hs, vs;
    wire [11:0] cx;
    wire [10:0] cy;
    wire render_busy;
    ddt_mandel_hd #(.MANDEL_CORES(NE), .JOY_CLK_HZ(1000000), .PAL_FILE("palette_rgb565_256.hex"), .FONT_FILE("mem/ovl_font8.mem")) dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst), .joy_n(joy_n), .rst_key_n(1'b1), .render_busy(render_busy), .max_iters_o(),
        .pix_clk(pix_clk), .pix_rst(pix_rst), .xact0(xact0), .yact0(yact0),
        .t_cx(t_cx), .t_cy(t_cy), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy)
    );

    // ---- frames and the capture of one
    reg vs_q = 0;
    always @(posedge pix_clk) vs_q <= t_vs;
    wire frame_edge = t_vs && !vs_q;
    integer frames = 0;
    always @(posedge pix_clk) if (frame_edge) frames <= frames + 1;

    reg [23:0] cap [0:1920 * 1080 - 1];
    reg        capturing = 0;
    integer    cap_n = 0, cap_bad_pos = 0;
    always @(posedge pix_clk) if (capturing && de) begin
        if (cx < xact0 || cy < yact0 || cx - xact0 >= 1920 || cy - yact0 >= 1080) cap_bad_pos = cap_bad_pos + 1;
        else cap[(cy - yact0) * 1920 + (cx - xact0)] <= rgb;
        cap_n = cap_n + 1;
    end

    task wait_frames(input integer n);
        integer f0;
        begin
            f0 = frames;
            while (frames - f0 < n) @(posedge pix_clk);
        end
    endtask

    task capture_frame;
        begin
            cap_n = 0; cap_bad_pos = 0;
            @(posedge pix_clk);
            while (!frame_edge) @(posedge pix_clk);
            capturing = 1;
            @(posedge pix_clk);
            while (!frame_edge) @(posedge pix_clk);
            capturing = 0;
            repeat (20) @(posedge pix_clk);
        end
    endtask

    function [23:0] x888(input [15:0] c);
        x888 = {c[15:11], c[15:13], c[10:5], c[10:9], c[4:0], c[4:2]};
    endfunction

    task compare(input integer which, input [8 * 40 - 1:0] what);
        integer x, y, bad, first, fx, fy;
        reg [23:0] want;
        reg [15:0] w;
        begin
            bad = 0; first = -1;
            if (cap_n != 1920 * 1080) begin $display("FAIL %0s: %0d pixels with de, not %0d", what, cap_n, 1920 * 1080); fails = fails + 1; end
            if (cap_bad_pos != 0) begin $display("FAIL %0s: %0d pixels whose cx / cy are outside the active area", what, cap_bad_pos); fails = fails + 1; end
            for (y = 0; y < 1080; y = y + 1)
                for (x = 0; x < 1920; x = x + 1) begin
                    if (x >= 256 && x < 256 + 1408 && y >= 28 && y < 28 + 1024) begin
                        fx = (x - 256) >> 2; fy = (y - 28) >> 2;
                        w = (which == 1) ? exp1[fy * 352 + fx] : (which == 2) ? exp2[fy * 352 + fx] : exp3[fy * 352 + fx];
                        want = x888(w);
                    end else want = 24'd0;
                    if (cap[y * 1920 + x] !== want) begin
                        bad = bad + 1;
                        if (first < 0) begin first = y * 1920 + x; $display("     first difference at x %0d y %0d: core %h, model %h", x, y, cap[y * 1920 + x], want); end
                    end
                end
            if (bad == 0) $display("ok   %0s: all 2073600 pixels equal the model", what);
            else begin $display("FAIL %0s: %0d of 2073600 pixels differ from the model", what, bad); fails = fails + 1; end
        end
    endtask

    task wait_render_done;
        integer n;
        reg seen;
        begin
            seen = 0; n = 0;
            while (!(seen && !render_busy) && n < 100000) begin
                if (render_busy) seen = 1;
                n = n + 1;
                #1000;
            end
            if (!seen || render_busy) begin $display("FAIL the renderer never went busy and idle"); fails = fails + 1; end
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
        #1000; cpu_rst = 0;
        repeat (40) @(posedge pix_clk);
        pix_rst = 0;
        wait_render_done;
        wait_frames(2);
        capture_frame;
        compare(1, "default view with the splash text");
        // the splash lasts 501 frames; run its counter out
        dut.splash_frame_count = 9'd498;
        wait_frames(6);
        capture_frame;
        compare(2, "default view, splash over");
        if (mode_s == "joy") begin
            // fire + up: zoom in (joystick_buttons: zoom_in_pulse = fire & pressed_pulse[0]); bits: 0 up, 1 down, 2 left, 3 right, 4 fire; 0 = pressed
            joy_n = 5'b01111;
            #3000000;
            joy_n = 5'b01110;
            #3000000;
            joy_n = 5'b01111;
            #3000000;
            joy_n = 5'b11111;
            #3000000;
            wait_render_done;
            wait_frames(2);
            capture_frame;
            compare(3, "after fire + up (zoom in)");
        end
        if (fails == 0) $display("PASS ddt_mandel_hd %0s", mode_s); else $display("FAILED ddt_mandel_hd %0s (%0d)", mode_s, fails);
        $finish;
    end
endmodule
