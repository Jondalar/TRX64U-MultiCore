// T142 S2: the reference memory for the benches - the machine's cartridge ROM, work RAM and battery RAM as plain arrays that answer at once and are never late. It has
// the ports of snes_mem.v and the same capture timing (a request taken at cap is answered one clock later, as a cache hit is), but no DDR2, no cache, no stall. The
// same program on this memory and behind snes_mem.v must leave the same machine (sim/run.sh compares): that is the proof that the caches and the stall are transparent.
`timescale 1ns / 1ps
module snes_mem_ref (
    input  wire        clk,
    input  wire        rst,
    output wire        busy,
    input  wire        cap,
    input  wire        adv,
    input  wire [31:0] base,
    input  wire [22:0] rom_a,
    input  wire        rom_rd,
    output reg  [7:0]  rom_q = 8'd0,
    input  wire [16:0] wram_a,
    input  wire        wram_rd,
    input  wire        wram_wr,
    input  wire [7:0]  wram_d,
    output reg  [7:0]  wram_q = 8'd0,
    input  wire [17:0] bs_a,
    input  wire        bs_rd,
    input  wire        bs_wr,
    input  wire [7:0]  bs_d,
    output reg  [7:0]  bs_q = 8'd0,
    output wire        stall,
    input  wire        ui_clk,
    input  wire        ui_rst,
    output wire [26:0] c_addr,
    output wire [2:0]  c_cmd,
    output wire        c_en,
    input  wire        c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0] c_wdf_mask,
    output wire        c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    output wire [31:0] st_miss_rom,
    output wire [31:0] st_miss_wram,
    output wire [31:0] st_miss_bs,
    output wire [31:0] st_stall,
    output wire [31:0] st_wr
);
    reg [7:0] rom  [0:8388607] /*verilator public_flat_rw*/;
    reg [7:0] wram [0:131071] /*verilator public_flat_rw*/;
    reg [7:0] bsr  [0:262143] /*verilator public_flat_rw*/;
    reg [22:0] ra; reg [16:0] wa; reg [17:0] ba;
    integer i;
    initial begin
        for (i = 0; i < 131072; i = i + 1) wram[i] = 8'd0;
        for (i = 0; i < 262144; i = i + 1) bsr[i] = 8'd0;
    end
    always @(posedge clk) begin
        if (cap) begin ra <= rom_a; wa <= wram_a; ba <= bs_a; end
        if (cap & adv & wram_wr) wram[wram_a] <= wram_d;
        if (cap & adv & bs_wr & ~wram_wr) bsr[bs_a] <= bs_d;
        rom_q <= rom[ra];
        wram_q <= wram[wa];
        bs_q <= bsr[ba];
    end
    assign busy = 1'b0;
    assign stall = 1'b0;
    assign c_addr = 27'd0; assign c_cmd = 3'd0; assign c_en = 1'b0; assign c_wdf_data = 128'd0; assign c_wdf_mask = 16'hFFFF; assign c_wdf_wren = 1'b0;
    assign st_miss_rom = 32'd0; assign st_miss_wram = 32'd0; assign st_miss_bs = 32'd0; assign st_stall = 32'd0; assign st_wr = 32'd0;
endmodule
