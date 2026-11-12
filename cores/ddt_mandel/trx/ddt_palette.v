// T168: the 256 colours of mandelbrUlt as RGB565. Written for this project (TREX). palette_rgb565_256.hex is made by tools/ddt_palette.py from the colours that
// DDT's palette_yuv_256.hex was converted from (that tool checks the YUV words against them). Replaces palette_rom_256x28.sv, whose output was {Y, U, V} for the
// PAL modulator.
`timescale 1ns / 1ps
module ddt_palette (
    input  wire [7:0]  addr,
    output wire [15:0] rgb565
);
    (* rom_style = "distributed" *) reg [15:0] rom [0:255];
    initial $readmemh("palette_rgb565_256.hex", rom);
    assign rgb565 = rom[addr];
endmodule
