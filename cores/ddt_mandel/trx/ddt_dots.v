// T168: the raster of the DDT cores. Written for this project (TREX), no upstream code. It replaces video_timing.v of both designs (a 864 x 312 PAL-like raster at 13.5 MHz
// with sync, burst and a 704 sample active line): there is no analogue signal to time any more, only a picture to hand to the frame writer (ddt_pixw.v).
// One pixel is two clocks (ph 0, 1) of `clk` (mclk, 12.2887 MHz); a line is H_TOTAL pixels, a frame V_TOTAL lines, 400 x 312 pixels = 124,800 pixels = 49.2 frames a second
// (their raster: 312 lines, 50.08 Hz). The picture is the top left ACT_W x ACT_H, the rest is blanking for the writer (hblank / vblank).
//
//   x, y        the pixel being made; stable for both clocks of the pixel (the picture logic may take its two clocks)
//   dotst       one clock per pixel (ph 1): the writer takes `rgb565` of the pixel at this edge
//   frame_start one clock at (x, y) = (0, 0), ph 0, never in reset: the strobe their `frame_start` was (high for one 13.5 MHz clock there)
//   run = 0     the raster stands still and gives no dotst: the Freeze of the core (the writer waits, the last frame stays on the screen)
`timescale 1ns / 1ps
module ddt_dots #(
    parameter integer ACT_W = 352,
    parameter integer ACT_H = 256,
    parameter integer H_TOTAL = 400,
    parameter integer V_TOTAL = 312
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        run,
    output reg  [8:0]  x = 9'd0,
    output reg  [8:0]  y = 9'd0,
    output reg         ph = 1'b0,
    output wire        hblank,
    output wire        vblank,
    output wire        dotst,
    output wire        frame_start
);
    assign hblank = (x >= ACT_W);
    assign vblank = (y >= ACT_H);
    assign dotst = run & ph;
    assign frame_start = run & ~rst & ~ph & (x == 9'd0) & (y == 9'd0);

    always @(posedge clk) begin
        if (rst) begin
            x <= 9'd0; y <= 9'd0; ph <= 1'b0;
        end else if (run) begin
            ph <= ~ph;
            if (ph) begin
                if (x == H_TOTAL - 1) begin
                    x <= 9'd0;
                    y <= (y == V_TOTAL - 1) ? 9'd0 : y + 9'd1;
                end else x <= x + 9'd1;
            end
        end
    end
endmodule
