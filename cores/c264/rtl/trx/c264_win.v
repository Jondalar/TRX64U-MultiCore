// T90: the app's DMA window (a toggle per byte, ultimate_sys.vhd) and the host's second memory port, as vc20_win does it for
// the VC-20: the window asks the host for the port (win_port_*); the host merges it with the JTAG and gives the winner back
// as p_req / p_* in the same clock; the read data comes back from the memory one clock after the request.
// The port's address space:
//   p_rom = 0   the 64 KB RAM, byte address = the machine's (what machine:readmem / writemem and a PRG load use)
//   p_rom = 1   the app's ROM apertures: dma_addr 0x2000-0x3FFF (the host's KERNAL aperture, ROMS_BASE + 0x2000) and 0x4000-0x5FFF
//               (BASIC, ROMS_BASE + 0x0000) are the two halves of ONE 16 KB aperture, offset = dma_addr[13:0] (the app sees
//               ROMS_BASE .. ROMS_BASE + 0x3FFF contiguous); rom_sel names the array behind it:
//               0 KERNAL, 1 BASIC, 2 function low, 3 function high, 4 cartridge low, 5 cartridge high.
//               The host's char (0x0000) and colour RAM (0x6000) apertures are not here: they read 0 and write nothing.
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module c264_win (
    input  wire        clk,
    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire [7:0]  dma_wdata,
    input  wire        dma_rom,
    output wire        dma_done_tgl,
    output wire [7:0]  dma_rdata,
    input  wire        dbg_busy,
    input  wire [2:0]  rom_sel,                 // sys_clk, quasi static (the app sets it before the bytes)
    output wire        port_use,
    output wire [15:0] port_addr,
    output wire        port_rom,
    output wire        port_we,
    output wire [7:0]  port_wdata,
    input  wire [7:0]  port_rdata,              // the memory's w_do
    input  wire        p_req,
    input  wire [15:0] p_addr,
    input  wire        p_rom,
    input  wire        p_we,
    input  wire [7:0]  p_wdata,
    output wire        w_wr,
    output reg         w_hit,
    output wire        w_rom,
    output wire [2:0]  w_sel,
    output reg  [15:0] w_ai,
    output wire [7:0]  w_di
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] dreq_s = 3'b000;
    reg        done = 1'b0;
    reg [7:0]  rd = 8'd0;
    reg        p_ram = 1'b0;
    reg        ram_rd = 1'b0;
    reg [15:0] pr_addr = 16'd0;
    reg        pr_rom = 1'b0, pr_we = 1'b0;
    reg [7:0]  pr_wd = 8'd0;
    wire       use_i = p_ram & ~dbg_busy;

    assign port_use = use_i;
    assign port_addr = pr_addr;
    assign port_rom = pr_rom;
    assign port_we = pr_we;
    assign port_wdata = pr_wd;
    assign dma_done_tgl = done;
    assign dma_rdata = rd;

    always @(posedge clk) begin
        dreq_s <= {dreq_s[1:0], dma_req_tgl};
        ram_rd <= use_i;
        if (dreq_s[2] != dreq_s[1]) begin
            p_ram <= 1'b1;
            pr_addr <= dma_addr;
            pr_rom <= dma_rom;
            pr_we <= dma_we;
            pr_wd <= dma_wdata;
        end
        if (use_i) p_ram <= 1'b0;
        if (ram_rd) begin
            done <= ~done;
            rd <= pr_we ? 8'h00 : port_rdata;
        end
    end

    // the memory side: one address decode for the whole port
    assign w_rom = p_rom;
    assign w_sel = rom_sel;
    assign w_di = p_wdata;
    assign w_wr = p_req & p_we;
    always @* begin
        w_hit = 1'b1;
        w_ai = p_addr;
        if (p_rom) begin
            w_ai = {2'b00, p_addr[13:0]};
            w_hit = (p_addr[15] == 1'b0) && (p_addr[14:13] == 2'b01 || p_addr[14:13] == 2'b10) && (rom_sel <= 3'd5);
        end
    end
endmodule
