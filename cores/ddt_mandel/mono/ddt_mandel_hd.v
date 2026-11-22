// T255: mandelbrUlt (DDT, CC BY 4.0, cores/ddt_mandel/NOTICE) as one large-core module: his engines, scheduler, view registers, joystick logic and BRAM frame buffer, and the
// scan-out of that frame buffer into the timing of the HDMI side. Written for this project (TREX), GPL v3. The first half is ddt_mandel_core.v of the partial core (T168), the
// blocks of his top.v as he wrote them (marked "top.v"), without the application's hold and freeze; the second half replaces his 13.5 MHz raster and PAL video with a
// 4 x scan-out in the pixel clock.
//
// Scan-out: the frame buffer is 352 x 256; each pixel is shown 4 x 4 (1408 x 1024), centred in the 1920 x 1080 active area of the timing (borders 256 / 28). The pipeline
// below takes the timing's counters (t_*) and gives the colour 8 clocks later, the counters with it. Stages (pix_clk):
//   S1 ax, ay       position in the active area
//   S2 fx, fy       frame buffer pixel and window flag
//   S3 row bases    fy * 352 as two partial sums; the text cell (fx / 8, fy / 8)
//   S4 address      to the frame buffer (BRAM, one clock); text: character address
//   S5 iter8        the byte of the frame buffer; text: the character code
//   S6 palette index (his rotation arithmetic); text: ASCII code
//   S7 RGB565       palette ROM; text: the font row
//   S8 RGB888       text bit or palette colour, expanded
// All arithmetic is shifts and adds: no multiplier (the engines own every DSP).
`timescale 1ns / 1ps
(* use_dsp = "no" *)
module ddt_mandel_hd #(
    parameter integer MANDEL_CORES = 20,
    parameter integer JOY_CLK_HZ = 100_000_000,       // joystick_buttons' time base (the bench sets it lower)
    // ROM images, read relative to Vivado's working directory host/; the bench overrides them
    parameter PAL_FILE  = "../cores/ddt_mandel/mem/palette_rgb565_256.hex",
    parameter FONT_FILE = "../cores/ddt_mandel/mono/mem/ovl_font8.mem"
) (
    input  wire        cpu_clk,                       // 100 MHz: the engines, the scheduler, the view registers
    input  wire        cpu_rst,
    input  wire [4:0]  joy_n,                         // port 2, up down left right fire, 0 = pressed, synchronised to cpu_clk
    input  wire        rst_key_n,                     // RESTORE, 0 = pressed, synchronised to cpu_clk
    output wire        render_busy,                   // cpu_clk
    output wire [11:0] max_iters_o,                   // cpu_clk
    // the HDMI timing (pix_clk)
    input  wire        pix_clk,
    input  wire        pix_rst,
    input  wire [11:0] xact0,
    input  wire [10:0] yact0,
    input  wire [11:0] t_cx,
    input  wire [10:0] t_cy,
    input  wire        t_de, t_hs, t_vs,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,
    output reg  [10:0] cy
);
    // Mandelbrot logical resolution is 352x256.
    localparam integer FB_W = 352;
    localparam integer FB_H = 256;
    localparam integer FB_AW = $clog2(FB_W * FB_H);
    localparam integer CLK_CALC_HZ = JOY_CLK_HZ;

    // ---- reset, cpu_clk side (his rst_calc)
    reg rst_calc = 1'b1;
    always @(posedge cpu_clk) rst_calc <= cpu_rst;

    // ---- the joystick: his joystick_buttons wants the contacts active low
    wire move_up, move_down, move_left, move_right, move_tick;
    wire zoom_in_pulse, zoom_out_pulse;
    wire iters_dec_pulse, iters_inc_pulse, zoom_reset_pulse;
    joystick_buttons #(
        .CLK_HZ(CLK_CALC_HZ), .SAMPLE_HZ(1000), .MOVE_HZ(512)
    ) u_joy (
        .clk(cpu_clk), .rst(rst_calc),
        .joy_up_n(joy_n[0]), .joy_down_n(joy_n[1]),
        .joy_left_n(joy_n[2]), .joy_right_n(joy_n[3]),
        .joy_set_n(joy_n[4]), .joy_rst_n(rst_key_n),
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

    // ---- scan-out, pix_clk. The pixel's source is fixed: no ce (the mode's gear is 1).
    // S1: the position in the active area (the timing's de is high only at cx >= xact0, cy >= yact0)
    reg [11:0] ax1;
    reg [10:0] ay1;
    reg        de1, hs1, vs1;
    reg [11:0] cx1;
    reg [10:0] cy1;
    always @(posedge pix_clk) begin
        ax1 <= t_cx - xact0;
        ay1 <= t_cy - yact0;
        de1 <= t_de; hs1 <= t_hs; vs1 <= t_vs; cx1 <= t_cx; cy1 <= t_cy;
    end

    // S2: the window (1408 x 1024 at 256, 28) and the frame buffer pixel (position / 4)
    localparam [11:0] X0 = 12'd256, X1 = 12'd256 + 12'd1408;
    localparam [10:0] Y0 = 11'd28,  Y1 = 11'd28 + 11'd1024;
    wire [11:0] wx = ax1 - X0;
    wire [10:0] wy = ay1 - Y0;
    wire        in_win = de1 && (ax1 >= X0) && (ax1 < X1) && (ay1 >= Y0) && (ay1 < Y1);
    reg  [8:0]  fx2;
    reg  [7:0]  fy2;
    reg         inw2, de2, hs2, vs2;
    reg [11:0]  cx2;
    reg [10:0]  cy2;
    always @(posedge pix_clk) begin
        fx2 <= in_win ? wx[10:2] : 9'd0;
        fy2 <= in_win ? wy[9:2]  : 8'd0;
        inw2 <= in_win;
        de2 <= de1; hs2 <= hs1; vs2 <= vs1; cx2 <= cx1; cy2 <= cy1;
    end

    // S3: fy * 352 = fy * 256 + fy * 64 + fy * 32 as two sums; the text cell
    reg [16:0] rb_a3, rb_b3;
    reg [5:0]  tcx3;
    reg [4:0]  tcy3;
    reg [2:0]  fxl3, fyl3;
    reg        inw3, txa3, de3, hs3, vs3;
    reg [11:0] cx3;
    reg [10:0] cy3;
    always @(posedge pix_clk) begin
        rb_a3 <= {fy2, 8'd0} + {2'd0, fy2, 6'd0};
        rb_b3 <= {4'd0, fy2, 5'd0} + {8'd0, fx2};
        tcx3 <= fx2[8:3];
        tcy3 <= fy2[7:3];
        fxl3 <= fx2[2:0];
        fyl3 <= fy2[2:0];
        txa3 <= (fy2 < 8'd200);                  // his text matrix covers 200 lines (25 rows of 8)
        inw3 <= inw2;
        de3 <= de2; hs3 <= hs2; vs3 <= vs2; cx3 <= cx2; cy3 <= cy2;
    end

    // S4: the address to the frame buffer; the text cell goes into screen_matrix (index, then code: S4, S5)
    reg [FB_AW-1:0] addr4;
    reg [2:0]  fxl4, fyl4;
    reg        inw4, txa4, de4, hs4, vs4;
    reg [11:0] cx4;
    reg [10:0] cy4;
    always @(posedge pix_clk) begin
        addr4 <= rb_a3 + rb_b3;
        fxl4 <= fxl3; fyl4 <= fyl3; inw4 <= inw3; txa4 <= txa3;
        de4 <= de3; hs4 <= hs3; vs4 <= vs3; cx4 <= cx3; cy4 <= cy3;
    end

    reg        startup_text_enable = 1'b1;      // his splash text, off after SPLASH_FRAMES frames

    // S5: the byte of the frame buffer (his framebuffer_bram: one registered read), the character code
    wire [7:0] iter5;
    framebuffer_bram #(.FB_W(FB_W), .FB_H(FB_H), .ADDR_W(FB_AW)) u_framebuffer (
        .clk_wr(cpu_clk), .wr_en(fb_we), .wr_addr(fb_wr_addr), .wr_data(fb_wr_data),
        .clk_rd(pix_clk), .rd_en(1'b1), .rd_addr(addr4), .rd_data(iter5)
    );
    wire       text_enable = startup_text_enable;
    wire [7:0] code5;
    screen_matrix #(.ENGINES(MANDEL_CORES)) u_scr (
        .clk(pix_clk), .text_enable(text_enable), .cx(tcx3), .cy(tcy3), .code(code5)
    );
    // the character cell is taken at S3 (tcx3, tcy3) and answers at S5; the other fields of S3 wait two clocks
    reg [2:0]  fxl5, fyl5;
    reg        inw5, txa5, de5, hs5, vs5;
    reg [11:0] cx5;
    reg [10:0] cy5;
    always @(posedge pix_clk) begin
        fxl5 <= fxl4; fyl5 <= fyl4; inw5 <= inw4; txa5 <= txa4;
        de5 <= de4; hs5 <= hs4; vs5 <= vs4; cx5 <= cx4; cy5 <= cy4;
    end

    // ---- his frame counters: the splash and the palette cycle, counted in frames of the HDMI timing (50 Hz as his PAL raster)
    localparam [8:0] SPLASH_FRAMES = 9'd501;
    localparam [6:0] PALETTE_HOLD_FRAMES = 7'd100;
    reg vs_q = 1'b0;
    always @(posedge pix_clk) vs_q <= t_vs;
    wire frame_start = t_vs && !vs_q;

    // the raw port-2 contacts into the pixel clock (his joy_n_pix_ff1/2)
    (* ASYNC_REG = "TRUE" *) reg [4:0] joy_n_pix_ff1 = 5'h1f;
    (* ASYNC_REG = "TRUE" *) reg [4:0] joy_n_pix_ff2 = 5'h1f;
    always @(posedge pix_clk) begin
        joy_n_pix_ff1 <= joy_n;
        joy_n_pix_ff2 <= joy_n_pix_ff1;
    end
    wire joy_fire_only_pix = !joy_n_pix_ff2[4] && (&joy_n_pix_ff2[3:0]);

    reg [8:0] splash_frame_count = 9'd0;
    reg [6:0] palette_hold_frame_count = 7'd0;
    reg       palette_cycle_qualified = 1'b0;
    wire palette_cycle_enable = palette_cycle_qualified && joy_fire_only_pix;
    always @(posedge pix_clk) begin
        if (pix_rst) begin
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

    // ---- his palette rotation: index 0 stays black, 1..255 rotate once per frame while the palette cycle is on
    reg [7:0] palette_rot = 8'd0;
    always @(posedge pix_clk) begin
        if (frame_start && palette_cycle_enable) begin
            if (palette_rot == 8'd254) palette_rot <= 8'd0;
            else palette_rot <= palette_rot + 8'd1;
        end
    end

    // S6: the palette index; the ASCII code of the character (his code is a C64 screen code: below 32 it is a letter, + 64)
    wire [7:0] palette_iter_minus1 = (iter5 == 8'd0) ? 8'd0 : (iter5 - 8'd1);
    wire [8:0] palette_sum = {1'b0, palette_iter_minus1} + {1'b0, palette_rot};
    wire [7:0] palette_index = (iter5 == 8'd0) ? 8'd0 :
                               ((palette_sum >= 9'd255) ? (palette_sum - 9'd255 + 9'd1) : (palette_sum[7:0] + 8'd1));
    reg [7:0]  pidx6;
    reg [6:0]  ascii6;
    reg [2:0]  fxl6, fyl6;
    reg        inw6, txa6, de6, hs6, vs6;
    reg [11:0] cx6;
    reg [10:0] cy6;
    always @(posedge pix_clk) begin
        pidx6 <= palette_index;
        ascii6 <= (code5[6:0] < 7'd32) ? (code5[6:0] + 7'd64) : code5[6:0];
        fxl6 <= fxl5; fyl6 <= fyl5; inw6 <= inw5; txa6 <= txa5;
        de6 <= de5; hs6 <= hs5; vs6 <= vs5; cx6 <= cx5; cy6 <= cy5;
    end

    // S7: the colour (palette ROM) and the font row. The font is Gideon's 8 x 8 overlay font, as in the partial core (trx/c64_font_rom.v).
    (* rom_style = "distributed" *) reg [15:0] pal [0:255];
    initial $readmemh(PAL_FILE, pal);
    reg [7:0] font [0:1023];
    initial $readmemh(FONT_FILE, font);
    reg [15:0] pal7;
    reg [7:0]  row7;
    reg [2:0]  fxl7;
    reg        inw7, txa7, de7, hs7, vs7;
    reg [11:0] cx7;
    reg [10:0] cy7;
    always @(posedge pix_clk) begin
        pal7 <= pal[pidx6];
        row7 <= font[{ascii6, fyl6}];
        fxl7 <= fxl6; inw7 <= inw6; txa7 <= txa6;
        de7 <= de6; hs7 <= hs6; vs7 <= vs6; cx7 <= cx6; cy7 <= cy6;
    end

    // S8: his colour choice: white where the text has a pixel, the palette inside the picture, black outside; RGB565 to RGB888 by bit replication
    wire font_bit = txa7 && inw7 && row7[3'd7 - fxl7];
    wire [15:0] c565 = font_bit ? 16'hFFFF : (inw7 ? pal7 : 16'h0000);
    always @(posedge pix_clk) begin
        rgb <= {c565[15:11], c565[15:13], c565[10:5], c565[10:9], c565[4:0], c565[4:2]};
        de <= de7; hs <= hs7; vs <= vs7; cx <= cx7; cy <= cy7;
    end
endmodule
