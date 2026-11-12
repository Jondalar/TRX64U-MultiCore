// T168: the character set of the DDT overlays. Written for this project (TREX), no upstream code.
// DDT's c64_font_rom.sv reads c64_chargen_ucg.hex, the C64's character ROM; a ROM never goes into this tree (T168 spec D1, D6). This module has its name and its
// ports (so scroller.v, video_pattern.v and the Mandelbrot text overlay stay as he wrote them) and reads Gideon's 8 x 8 overlay font instead, the one the host
// already carries (host/mem/ovl_font8.mem, 128 ASCII characters x 8 rows, bit 7 = the left pixel). His text is in C64 screen codes (A = 1 ... Z = 26, space 32,
// numbers and punctuation as ASCII): codes below 32 become ASCII 64 + code, the rest are used as they are; bit 7 (reverse video) is not used by any text of his.
`timescale 1ns / 1ps
module c64_font_rom (
    input  wire [10:0] addr,     // {screen code, row}
    output wire [7:0]  data
);
    reg [7:0] mem [0:1023];
    initial $readmemh("mem/ovl_font8.mem", mem);
    wire [6:0] code = addr[9:3];
    wire [6:0] ascii = (code < 7'd32) ? (code + 7'd64) : code;
    assign data = mem[{ascii, addr[2:0]}];
endmodule
