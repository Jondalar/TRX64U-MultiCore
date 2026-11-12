// T95 D12: the iNES header (16 bytes, as the app hands it over in the register window) into the machine's `mapper_flags` word
// and the two address masks of cart_top. DERIVED from GameLoader in NES_MiSTer's NES.sv (49a0a66, GPL v3, see rtl/NOTICE): the
// same expressions, so the machine sees the word it was written for; only the loader's state machine (it reads the header from
// a stream and copies the image to SDRAM) is not here - the app writes the header bytes and the image through the registers
// and the DDR2 window. Combinational: the bytes are quasi static (the app writes them while the machine is in reset).
//   ines[i] is hdr[8*i+7:8*i]; bytes 0..3 ("NES", 0x1A) are not looked at - the app checked them.
`timescale 1ns / 1ps
module nes_flags (
    input  wire [127:0] hdr,
    output wire [63:0]  mapper_flags,
    output wire [9:0]   prg_mask,
    output wire [9:0]   chr_mask,
    output wire [21:0]  prg_bytes,        // PRG size in bytes (0 if the header says none)
    output wire [21:0]  chr_bytes         // CHR ROM size in bytes (0: CHR RAM)
);
    wire [7:0] ines [0:15];
    genvar g;
    generate for (g = 0; g < 16; g = g + 1) begin : b
        assign ines[g] = hdr[8*g+7:8*g];
    end endgenerate

    wire [7:0] prgrom = ines[4];                          // 16 KB pages
    wire [7:0] chrrom = ines[5];                          // 8 KB pages (0: CHR RAM)
    wire [3:0] chrram = ines[11][3:0];                    // NES 2.0 CHR-RAM size shift count (64 << count)
    wire is_nes20 = (ines[7][3:2] == 2'b10);
    wire is_nes20_prg = (is_nes20 && (ines[9][3:0] == 4'hF));
    wire is_nes20_chr = (is_nes20 && (ines[9][7:4] == 4'hF));
    wire has_chr_ram = ~is_nes20 ? (chrrom == 0) : |chrram;

    wire [21:0] prg_size2 = is_nes20_prg ? ({19'b0, ines[4][1:0], 1'b1} << ines[4][7:2]) : {prgrom, 14'b0};
    wire [21:0] chr_size2 = is_nes20_chr ? ({19'b0, ines[5][1:0], 1'b1} << ines[5][7:2]) : {1'b0, chrrom, 13'b0};
    wire [21:0] chr_ram_size = is_nes20 ? (22'd64 << chrram) : 22'h2000;

    function [9:0] mask;
        input [10:0] size;
        integer i;
        begin
            for (i = 0; i < 10; i = i + 1) mask[i] = (size > (11'd1 << i));
        end
    endfunction
    assign prg_mask = mask(prg_size2[21:11]);
    assign chr_mask = mask(|chr_size2 ? chr_size2[21:11] : chr_ram_size[21:11]);
    assign prg_bytes = prg_size2;
    assign chr_bytes = chr_size2;

    wire [2:0] prg_size = prgrom <= 1  ? 3'd0 :
                          prgrom <= 2  ? 3'd1 :
                          prgrom <= 4  ? 3'd2 :
                          prgrom <= 8  ? 3'd3 :
                          prgrom <= 16 ? 3'd4 :
                          prgrom <= 32 ? 3'd5 :
                          prgrom <= 64 ? 3'd6 : 3'd7;
    wire [2:0] chr_size = chrrom <= 1  ? 3'd0 :
                          chrrom <= 2  ? 3'd1 :
                          chrrom <= 4  ? 3'd2 :
                          chrrom <= 8  ? 3'd3 :
                          chrrom <= 16 ? 3'd4 :
                          chrrom <= 32 ? 3'd5 :
                          chrrom <= 64 ? 3'd6 : 3'd7;

    // differentiate dirty iNES 1.0 headers from proper iNES 2.0 ones
    wire is_dirty = !is_nes20 && ((ines[9][7:1] != 0) || (ines[10] != 0) || (ines[11] != 0) || (ines[12] != 0)
                                  || (ines[13] != 0) || (ines[14] != 0) || (ines[15] != 0));
    wire [7:0] mapper = {is_dirty ? 4'b0000 : ines[7][7:4], ines[6][7:4]};
    wire [7:0] ines2mapper = {is_nes20 ? ines[8] : 8'h00};
    wire [3:0] prgram = {is_nes20 ? ines[10][3:0] : 4'h0};
    wire [3:0] prg_nvram = (is_nes20 ? ines[10][7:4] : 4'h0);
    wire       piano = is_nes20 && (ines[15][5:0] == 6'h19);
    wire       mapper99 = ({ines2mapper[1:0], mapper} == 10'd99);
    wire [3:0] vs_hardware_type = (is_nes20 && (ines[7][1:0] == 2'd1)) ? ines[13][7:4] : 4'd0;
    wire       mapper99_dual = mapper99 && (vs_hardware_type == 4'd5);
    wire       mapper99_raid = mapper99 && (vs_hardware_type == 4'd6);
    wire       has_saves = ines[6][1];

    assign mapper_flags[63:60] = 4'd0;
    assign mapper_flags[59:53] = is_nes20 ? ines[15][6:0] : 7'd0;
    assign mapper_flags[52:49] = vs_hardware_type;
    assign mapper_flags[48:45] = (is_nes20 && (ines[7][1:0] == 2'd1)) ? ines[13][3:0] : 4'd0;
    assign mapper_flags[44:43] = is_nes20 ? ines[7][1:0] : 2'd0;
    assign mapper_flags[42:41] = mapper99 ? chr_size2[14:13] : 2'd0;
    assign mapper_flags[40:38] = mapper99_dual ? prg_size2[16:14] : mapper99_raid ? 3'd4 : mapper99 ? prg_size2[15:13] : 3'd0;
    assign mapper_flags[37:36] = is_nes20 ? ines[12][1:0] : 2'b00;
    assign mapper_flags[35]    = is_nes20;
    assign mapper_flags[34:31] = prg_nvram;
    assign mapper_flags[30]    = piano;
    assign mapper_flags[29:26] = prgram;
    assign mapper_flags[25]    = has_saves;
    assign mapper_flags[24:17] = ines2mapper;
    assign mapper_flags[16]    = ines[6][3];
    assign mapper_flags[15]    = has_chr_ram;
    assign mapper_flags[14]    = ines[6][0];
    assign mapper_flags[13:11] = chr_size;
    assign mapper_flags[10:8]  = prg_size;
    assign mapper_flags[7:0]   = mapper;
endmodule
