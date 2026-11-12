// T168: the picture logic of mandelbrUlt, from mandel_video_pattern.v (CC BY 4.0, DDT; rtl/NOTICE). What is kept as he wrote it: the palette rotation
// (index 0 stays black, 1..255 rotate once per frame while the palette cycle is on), the palette index from the 8 bit iteration count, the 44 x 25 text overlay
// (screen_matrix + font, one font pixel is two samples of his 704 sample line = one pixel here), its extent (704 samples x 200 lines) and the white of the text.
// What is changed: the colour is RGB565 from ddt_palette instead of {luma, U, V} for the PAL modulator; the subcarrier, the burst, the sync and the blanking
// of the analogue raster are gone; the raster is ddt_dots.v's, one pixel per two clocks, the pixel column x counts framebuffer pixels (his x counted samples, two to a pixel,
// so the text matrix sees x_sample = 2 x); the one clock of delay for the BRAM read he balances with *_d1 registers is the clock the pixel has anyway.
`timescale 1ns / 1ps
module ddt_mandel_video (
    input  wire        clk,                   // mclk
    input  wire        frame_start,           // one clock per frame
    input  wire [8:0]  x,                     // framebuffer column 0..351 (+ blanking), stable for the pixel
    input  wire [8:0]  y,                     // framebuffer line 0..255 (+ blanking)
    input  wire [7:0]  fb_iter8,              // the frame buffer's byte of (x, y), valid a clock after x, y
    input  wire        text_enable,
    input  wire        palette_cycle_enable,
    output wire [15:0] rgb565
);
    localparam integer FB_W = 352;
    localparam integer FB_H = 256;
    localparam integer IMG_LINES = 200;       // the text matrix covers 200 lines (25 rows of 8)

    wire img_active = (x < FB_W) && (y < FB_H);

    // Fixed char matrix overlay: 44 columns x 25 rows. Each pixel of a 8x8 character is repeated for two samples horizontally: here, one pixel.
    wire [9:0] x_s = {x, 1'b0};                                 // his sample coordinate
    wire text_active = img_active && (x_s < 10'd704) && (y < IMG_LINES);
    wire [5:0] cx = x_s[9:4];
    wire [4:0] cy = y[7:3];
    wire [2:0] font_x = x_s[3:1];
    wire [2:0] font_y = y[2:0];

    wire [7:0] char_code;
    screen_matrix u_scr (
        .text_enable(text_enable), .cx(cx), .cy(cy), .code(char_code)
    );
    wire [10:0] font_addr = {char_code, font_y};
    wire [7:0] font_row;
    c64_font_rom u_font (.addr(font_addr), .data(font_row));
    wire font_bit = text_enable && img_active && text_active && (font_row[7 - font_x]);

    reg  [7:0] palette_rot = 8'd0;

    // Keep palette index 0 fixed and rotate indices 1..255 upward by one entry once per frame while the mode is enabled.
    // This way the Mandelbrot set color will always be black.
    always @(posedge clk) begin
        if (frame_start && palette_cycle_enable) begin
            if (palette_rot == 8'd254)
                palette_rot <= 8'd0;
            else
                palette_rot <= palette_rot + 8'd1;
        end
    end

    wire [7:0] palette_iter_minus1 = (fb_iter8 == 8'd0) ?
                                      8'd0 : (fb_iter8 - 8'd1);
    wire [8:0] palette_sum = {1'b0, palette_iter_minus1} +
                             {1'b0, palette_rot};
    wire [7:0] palette_index = (fb_iter8 == 8'd0) ? 8'd0 :
                               ((palette_sum >= 9'd255) ?
                                (palette_sum - 9'd255 + 9'd1) :
                                (palette_sum[7:0] + 8'd1));

    wire [15:0] palette_rgb;
    ddt_palette u_palette (.addr(palette_index), .rgb565(palette_rgb));

    // text is white (his LUMA_MAX, no chroma); outside the picture the DAC was blanked
    assign rgb565 = font_bit ? 16'hFFFF : (img_active ? palette_rgb : 16'h0000);
endmodule
