// T168: look_ma_no_vic (DDT, CC BY 4.0, rtl/NOTICE) without its board. What his top.v does apart from the pins: a 13.5 MHz clock, the raster, the pattern. Here:
// mclk (12.29 MHz, where his clk_pix of 13.5 MHz was) with the raster of ddt_dots.v (400 x 312 pixels of two clocks, 352 x 200 of them the picture) and the pattern
// (ddt_novic_video.v). The design has no input and no memory but ROMs; the application's hold resets the raster, Freeze stops it. Written for this project (TREX).
`timescale 1ns / 1ps
module ddt_novic_core (
    input  wire        cpu_clk,
    input  wire        hold,                        // the application's reset hold (CTRL b0), cpu_clk
    input  wire        halt,                        // Freeze (cpu_clk)
    input  wire        mclk,
    input  wire        mclk_rst,
    output wire [15:0] rgb565,
    output wire        hblank,
    output wire        vblank,
    output wire        dotst,
    output wire [8:0]  px_x,
    output wire [8:0]  px_y,
    output wire        video_rst,                   // mclk: the writer's reset
    output reg  [31:0] frames = 32'd0               // mclk, frames since the reset (the register INFO; quasi static for the reader)
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] hold_v = 2'b11, halt_v = 2'b00;
    always @(posedge mclk) begin
        hold_v <= {hold_v[0], hold};
        halt_v <= {halt_v[0], halt};
    end
    reg rst_v = 1'b1;
    always @(posedge mclk) rst_v <= mclk_rst | hold_v[1];
    assign video_rst = rst_v;

    wire [8:0] x, y;
    wire       ph, frame_start;
    ddt_dots #(.ACT_W(352), .ACT_H(200)) u_dots (
        .clk(mclk), .rst(rst_v), .run(~halt_v[1]), .x(x), .y(y), .ph(ph),
        .hblank(hblank), .vblank(vblank), .dotst(dotst), .frame_start(frame_start)
    );
    assign px_x = x;
    assign px_y = y;

    always @(posedge mclk) begin
        if (rst_v) frames <= 32'd0;
        else if (frame_start) frames <= frames + 32'd1;
    end

    ddt_novic_video u_video (
        .clk(mclk), .frame_start(frame_start), .px(x), .py(y), .rgb565(rgb565)
    );
endmodule
