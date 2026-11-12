// T95: the 64 colours of the 2C02 as RGB565, for the host's pixel layer. The values are the usual 2C02 palette (the one the
// emulators call "FCEUX/Nestopia default", 24 bit RGB; the real chip makes its colours from a composite signal, there is no
// single right table), rounded down to 565. A table of facts, no upstream code (NES_MiSTer's own palettes are in video.sv, which is not
// part of this core; the machine hands over the 6-bit colour index, `color`).
`timescale 1ns / 1ps
module nes_pal (
    input  wire [5:0]  idx,
    output reg  [15:0] q
);
    always @* begin
        case (idx)
            6'h00: q = 16'h7BEF;   // 7C7C7C
            6'h01: q = 16'h001F;   // 0000FC
            6'h02: q = 16'h0017;   // 0000BC
            6'h03: q = 16'h4157;   // 4428BC
            6'h04: q = 16'h9010;   // 940084
            6'h05: q = 16'hA804;   // A80020
            6'h06: q = 16'hA880;   // A81000
            6'h07: q = 16'h88A0;   // 881400
            6'h08: q = 16'h5180;   // 503000
            6'h09: q = 16'h03C0;   // 007800
            6'h0A: q = 16'h0340;   // 006800
            6'h0B: q = 16'h02C0;   // 005800
            6'h0C: q = 16'h020B;   // 004058
            6'h0D: q = 16'h0000;   // 000000
            6'h0E: q = 16'h0000;   // 000000
            6'h0F: q = 16'h0000;   // 000000
            6'h10: q = 16'hBDF7;   // BCBCBC
            6'h11: q = 16'h03DF;   // 0078F8
            6'h12: q = 16'h02DF;   // 0058F8
            6'h13: q = 16'h6A3F;   // 6844FC
            6'h14: q = 16'hD819;   // D800CC
            6'h15: q = 16'hE00B;   // E40058
            6'h16: q = 16'hF9C0;   // F83800
            6'h17: q = 16'hE2E2;   // E45C10
            6'h18: q = 16'hABE0;   // AC7C00
            6'h19: q = 16'h05C0;   // 00B800
            6'h1A: q = 16'h0540;   // 00A800
            6'h1B: q = 16'h0548;   // 00A844
            6'h1C: q = 16'h0451;   // 008888
            6'h1D: q = 16'h0000;   // 000000
            6'h1E: q = 16'h0000;   // 000000
            6'h1F: q = 16'h0000;   // 000000
            6'h20: q = 16'hFFDF;   // F8F8F8
            6'h21: q = 16'h3DFF;   // 3CBCFC
            6'h22: q = 16'h6C5F;   // 6888FC
            6'h23: q = 16'h9BDF;   // 9878F8
            6'h24: q = 16'hFBDF;   // F878F8
            6'h25: q = 16'hFAD3;   // F85898
            6'h26: q = 16'hFBCB;   // F87858
            6'h27: q = 16'hFD08;   // FCA044
            6'h28: q = 16'hFDC0;   // F8B800
            6'h29: q = 16'hBFC3;   // B8F818
            6'h2A: q = 16'h5ECA;   // 58D854
            6'h2B: q = 16'h5FD3;   // 58F898
            6'h2C: q = 16'h075B;   // 00E8D8
            6'h2D: q = 16'h7BCF;   // 787878
            6'h2E: q = 16'h0000;   // 000000
            6'h2F: q = 16'h0000;   // 000000
            6'h30: q = 16'hFFFF;   // FCFCFC
            6'h31: q = 16'hA73F;   // A4E4FC
            6'h32: q = 16'hBDDF;   // B8B8F8
            6'h33: q = 16'hDDDF;   // D8B8F8
            6'h34: q = 16'hFDDF;   // F8B8F8
            6'h35: q = 16'hFD38;   // F8A4C0
            6'h36: q = 16'hF696;   // F0D0B0
            6'h37: q = 16'hFF15;   // FCE0A8
            6'h38: q = 16'hFECF;   // F8D878
            6'h39: q = 16'hDFCF;   // D8F878
            6'h3A: q = 16'hBFD7;   // B8F8B8
            6'h3B: q = 16'hBFDB;   // B8F8D8
            6'h3C: q = 16'h07FF;   // 00FCFC
            6'h3D: q = 16'hFEDF;   // F8D8F8
            6'h3E: q = 16'h0000;   // 000000
            6'h3F: q = 16'h0000;   // 000000
        endcase
    end
endmodule
