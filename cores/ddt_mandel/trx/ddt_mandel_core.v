// T168: mandelbrUlt (DDT, CC BY 4.0, rtl/NOTICE) without its board. This module is what top.v of the design does apart from the pins: the engines and their scheduler,
// the view registers (centre, scale, iterations) moved by the joystick, the BRAM frame buffer, the splash and palette-cycle counters. Those blocks are his top.v as
// written (marked "top.v"); what is ours is around them: the clocks (cpu_clk for the engines, as his clk_calc of 100 MHz; mclk where his clk_pix of 13.5 MHz was), the
// raster (ddt_dots.v), the joystick port from the contract (c64_joy2, 1 = pressed) instead of KBJOY_2_JOY (active low), the reset (the core's hold and cpu_rst instead of
// his power-on counter and the MMCM lock), and the colour (ddt_mandel_video.v). Not here: the MMCM, the THS8136 interface, the composite arithmetic, LED_*, CIA_*.
`timescale 1ns / 1ps
module ddt_mandel_core #(
    parameter integer MANDEL_CORES = 6,             // his top.v has 20 (120 DSP); pb_l2 owns 40 DSP: 6 engines are 36 (T168 D10)
    parameter integer JOY_CLK_HZ = 100_000_000      // joystick_buttons' time base; the bench sets it lower to shorten the debounce
) (
    input  wire        cpu_clk,                     // 100 MHz: the engines, the scheduler, the view registers
    input  wire        cpu_rst,
    input  wire        hold,                        // the application's reset hold (CTRL b0), cpu_clk
    input  wire        halt,                        // Freeze (cpu_clk): the joystick is ignored
    input  wire        mclk,                        // 12.29 MHz: the raster and the colour
    input  wire        mclk_rst,
    input  wire [4:0]  joy2,                        // c64_joy2: up, down, left, right, fire; 1 = pressed
    input  wire        restore,                     // c64_restore, 1 = pressed
    output wire        render_busy,                 // cpu_clk
    output wire [11:0] max_iters_o,                 // cpu_clk
    // the picture, mclk
    output wire [15:0] rgb565,
    output wire        hblank,
    output wire        vblank,
    output wire        dotst,
    output wire [8:0]  px_x,
    output wire [8:0]  px_y,
    output wire        video_rst                    // mclk: the writer's reset
);
    // Mandelbrot logical resolution is 352x256.
    localparam integer FB_W = 352;
    localparam integer FB_H = 256;
    localparam integer FB_AW = $clog2(FB_W * FB_H);
    localparam integer CLK_CALC_HZ = JOY_CLK_HZ;

    // ---- resets: cpu_clk side (his rst_calc) and mclk side
    reg rst_calc = 1'b1;
    always @(posedge cpu_clk) rst_calc <= cpu_rst | hold;
    (* ASYNC_REG = "TRUE" *) reg [1:0] hold_v = 2'b11;
    always @(posedge mclk) hold_v <= {hold_v[0], hold};
    reg rst_v = 1'b1;
    always @(posedge mclk) rst_v <= mclk_rst | hold_v[1];
    assign video_rst = rst_v;

    // ---- the joystick (Freeze masks it): his joystick_buttons wants the contacts active low
    wire [4:0] joy_n = halt ? 5'h1f : ~joy2;
    wire       rst_n = halt ? 1'b1  : ~restore;
    wire move_up, move_down, move_left, move_right, move_tick;
    wire zoom_in_pulse, zoom_out_pulse;
    wire iters_dec_pulse, iters_inc_pulse, zoom_reset_pulse;
    joystick_buttons #(
        .CLK_HZ(CLK_CALC_HZ), .SAMPLE_HZ(1000), .MOVE_HZ(512)
    ) u_joy (
        .clk(cpu_clk), .rst(rst_calc),
        .joy_up_n(joy_n[0]), .joy_down_n(joy_n[1]),
        .joy_left_n(joy_n[2]), .joy_right_n(joy_n[3]),
        .joy_set_n(joy_n[4]), .joy_rst_n(rst_n),
        .move_up(move_up), .move_down(move_down), .move_left(move_left),
        .move_right(move_right), .move_tick(move_tick),
        .zoom_in_pulse(zoom_in_pulse), .zoom_out_pulse(zoom_out_pulse),
        .iters_dec_pulse(iters_dec_pulse), .iters_inc_pulse(iters_inc_pulse),
        .zoom_reset_pulse(zoom_reset_pulse)
    );

    // ---- top.v: the view registers
    localparam integer FRAC = 22;
    localparam integer signed SCALE_INIT_INT = (3 <<< FRAC) / FB_W;
    localparam signed [24:0] SCALE_INIT = SCALE_INIT_INT;
    reg signed [24:0] center_x_q;
    reg signed [24:0] center_y_q;
    reg signed [24:0] scale_q;
    reg [11:0] max_iters;

    always @(posedge cpu_clk) begin
        if (rst_calc) begin
            center_x_q <= -25'sd2097152;
            center_y_q <= 25'sd0;
            scale_q    <= SCALE_INIT;
            max_iters  <= 12'd128;
        end else begin
            if (move_tick) begin
                if (move_left)  center_x_q <= center_x_q - scale_q;
                if (move_right) center_x_q <= center_x_q + scale_q;
                if (move_up)    center_y_q <= center_y_q - scale_q;
                if (move_down)  center_y_q <= center_y_q + scale_q;
            end
            if (zoom_reset_pulse) scale_q <= SCALE_INIT;
            if (zoom_in_pulse) begin
                if ((scale_q >>> 1) == 0) scale_q <= 25'sd1;
                else scale_q <= scale_q >>> 1;
            end
            if (zoom_out_pulse) scale_q <= scale_q <<< 1;
            if (iters_dec_pulse) begin
                if (max_iters <= 12'd16) max_iters <= 12'd16;
                else max_iters <= max_iters - 12'd16;
            end
            if (iters_inc_pulse) begin
                if (max_iters >= 12'd4079) max_iters <= 12'd4095;
                else max_iters <= max_iters + 12'd16;
            end
        end
    end
    assign max_iters_o = max_iters;

    wire pan_step = move_tick & (move_up | move_down | move_left | move_right);
    wire render_restart = pan_step | zoom_in_pulse | zoom_out_pulse |
                          iters_inc_pulse | iters_dec_pulse | zoom_reset_pulse;

    // ---- top.v: the renderer and the frame buffer
    wire        wb_we, wb_bank;
    wire [9:0]  wb_addr;
    wire [7:0]  wb_data;
    wire        wb_full0, wb_full1;
    wire [$clog2(FB_H)-1:0] wb_y0, wb_y1;
    wire        commit_take, commit_done, commit_bank;

    fb_scanline_writer #(.NCORES(MANDEL_CORES), .FB_W(FB_W), .FB_H(FB_H), .FRAC(FRAC))
    u_writer (
        .clk(cpu_clk), .rst(rst_calc),
        .render_busy(render_busy),
        .wb_we(wb_we), .wb_bank(wb_bank), .wb_addr(wb_addr), .wb_data(wb_data),
        .wb_full0(wb_full0), .wb_full1(wb_full1), .wb_y0(wb_y0), .wb_y1(wb_y1),
        .commit_take(commit_take), .commit_done(commit_done),
        .commit_bank(commit_bank),
        .center_x_q(center_x_q), .center_y_q(center_y_q), .scale_q(scale_q),
        .restart(render_restart), .iters_q(max_iters)
    );

    wire        rb_bank_rd;
    wire [9:0]  rb_addr_rd;
    wire [7:0]  rb_data_rd;
    linebuf_iter8 u_renderbuf (
        .clk_wr(cpu_clk), .we(wb_we), .bank_wr(wb_bank), .addr_wr(wb_addr),
        .data_wr(wb_data), .clk_rd(cpu_clk), .bank_rd(rb_bank_rd),
        .addr_rd(rb_addr_rd), .data_rd(rb_data_rd)
    );

    wire fb_we;
    wire [FB_AW-1:0] fb_wr_addr;
    wire [7:0] fb_wr_data;
    fb_bram_dma #(.FB_W(FB_W), .FB_H(FB_H), .FB_AW(FB_AW)) u_fb_dma (
        .clk(cpu_clk), .rst(rst_calc), .rb_bank(rb_bank_rd), .rb_addr(rb_addr_rd),
        .rb_data(rb_data_rd), .wb_full0(wb_full0), .wb_full1(wb_full1),
        .wb_y0(wb_y0), .wb_y1(wb_y1), .commit_take(commit_take),
        .commit_done(commit_done), .commit_bank(commit_bank), .fb_we(fb_we),
        .fb_addr(fb_wr_addr), .fb_data(fb_wr_data)
    );

    // ---- the raster and the read side of the frame buffer (his: fb_window, fb_line_base, framebuffer_bram on clk_pix)
    wire [8:0] x, y;
    wire       ph, frame_start;
    ddt_dots #(.ACT_W(FB_W), .ACT_H(FB_H)) u_dots (
        .clk(mclk), .rst(rst_v), .run(1'b1), .x(x), .y(y), .ph(ph),
        .hblank(hblank), .vblank(vblank), .dotst(dotst), .frame_start(frame_start)
    );
    assign px_x = x;
    assign px_y = y;

    wire fb_window = !hblank && !vblank;
    wire [16:0] fb_line_base = ({8'd0, y} << 8) + ({8'd0, y} << 6) + ({8'd0, y} << 5);
    wire [16:0] fb_rd_addr = fb_line_base + {8'd0, x};
    wire [7:0] fb_iter8;
    framebuffer_bram #(.FB_W(FB_W), .FB_H(FB_H), .ADDR_W(FB_AW)) u_framebuffer (
        .clk_wr(cpu_clk), .wr_en(fb_we), .wr_addr(fb_wr_addr), .wr_data(fb_wr_data),
        .clk_rd(mclk), .rd_en(fb_window),
        .rd_addr(fb_window ? fb_rd_addr[FB_AW-1:0] : {FB_AW{1'b0}}), .rd_data(fb_iter8)
    );

    // ---- top.v: the splash and the palette cycle, counted in frames of the raster (his in clk_pix)
    localparam [8:0] SPLASH_FRAMES = 9'd501;
    localparam [6:0] PALETTE_HOLD_FRAMES = 7'd100;

    // Synchronize the raw port-2 joystick contacts into the video clock domain (his joy_n_pix_ff1/2).
    reg [4:0] joy_n_pix_ff1 = 5'h1f;
    reg [4:0] joy_n_pix_ff2 = 5'h1f;
    always @(posedge mclk) begin
        joy_n_pix_ff1 <= joy_n;
        joy_n_pix_ff2 <= joy_n_pix_ff1;
    end

    wire joy_fire_only_pix = !joy_n_pix_ff2[4] && (&joy_n_pix_ff2[3:0]);

    reg [8:0] splash_frame_count = 9'd0;
    reg [6:0] palette_hold_frame_count = 7'd0;
    reg       startup_text_enable = 1'b1;
    reg       palette_cycle_qualified = 1'b0;

    wire palette_cycle_enable = palette_cycle_qualified && joy_fire_only_pix;

    always @(posedge mclk) begin
        if (rst_v) begin
            splash_frame_count <= 9'd0;
            palette_hold_frame_count <= 7'd0;
            startup_text_enable <= 1'b1;
            palette_cycle_qualified <= 1'b0;
        end else begin
            // Handle palette cycle.
            if (!joy_fire_only_pix) begin
                palette_hold_frame_count <= 7'd0;
                palette_cycle_qualified <= 1'b0;
            end else if (frame_start && !palette_cycle_qualified) begin
                if (palette_hold_frame_count == (PALETTE_HOLD_FRAMES - 7'd1)) begin
                    palette_hold_frame_count <= PALETTE_HOLD_FRAMES;
                    palette_cycle_qualified <= 1'b1;
                end else begin
                    palette_hold_frame_count <= palette_hold_frame_count + 7'd1;
                end
            end

            if (frame_start) begin
                // Handle splash screen delay.
                if (startup_text_enable) begin
                    if (splash_frame_count == (SPLASH_FRAMES - 9'd1))
                        startup_text_enable <= 1'b0;
                    else
                        splash_frame_count <= splash_frame_count + 9'd1;
                end
            end
        end
    end

    ddt_mandel_video u_video (
        .clk(mclk), .frame_start(frame_start), .x(x), .y(y), .fb_iter8(fb_iter8),
        .text_enable(startup_text_enable), .palette_cycle_enable(palette_cycle_enable),
        .rgb565(rgb565)
    );
endmodule
