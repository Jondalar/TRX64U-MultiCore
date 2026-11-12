// T168: a model of the host's general DDR2 port e_* (ui_clk) for the DDT benches: 128 bit words, a command is taken when `e_rdy` is up (randomly down a quarter of the
// clocks), a write offers command and data at once (e_wdf_mask bit 1 = byte not written). e_addr counts 16 bit units (27 bits), a word is eight of them. The memory is 4 MB
// from BASE: the four frame buffers of the pixel layer (default FB_BASE 0x01D00000, stride 1 MB). A write outside it is counted in `stray`. Written for this project (TREX).
`timescale 1ns / 1ps
module ddr2e_model #(
    parameter [31:0] BASE = 32'h01D00000,
    parameter integer SEED = 1
) (
    input  wire         ui_clk,
    input  wire [26:0]  e_addr,
    input  wire [2:0]   e_cmd,
    input  wire         e_en,
    output reg          e_rdy = 1'b0,
    input  wire [127:0] e_wdf_data,
    input  wire [15:0]  e_wdf_mask,
    input  wire         e_wdf_wren
);
    reg [127:0] mem [0:262143] /*verilator public_flat_rw*/;
    integer writes = 0, stray = 0, seed = SEED, k;
    wire [31:0] byte_addr = {e_addr, 1'b0};
    wire [31:0] off = byte_addr - BASE;
    wire [17:0] widx = off[21:4];
    reg [127:0] w;
    always @(posedge ui_clk) begin
        e_rdy <= (($random(seed) & 3) != 0);
        if (e_en && e_rdy && e_cmd == 3'b000 && e_wdf_wren) begin
            if (byte_addr < BASE || off >= 32'h00400000) stray <= stray + 1;
            else begin
                writes <= writes + 1;
                w = mem[widx];
                for (k = 0; k < 16; k = k + 1) if (!e_wdf_mask[k]) w[8 * k +: 8] = e_wdf_data[8 * k +: 8];
                mem[widx] <= w;
            end
        end
    end
endmodule
