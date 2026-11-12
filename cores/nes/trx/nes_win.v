// T95: the app's DMA window (a toggle per byte, ultimate_sys.vhd) and the host's second memory port, as c264_win does it: the
// window asks the host for the port (win_port_*); the host merges it with the JTAG port and the other requesters and gives the winner
// back as p_req / p_*. This window does not use p_*: it starts its access from its own registers (below), so no path runs
// core -> static host -> core in one clock (pipeline 874: WNS -0.949 ns on p_ram_reg -> host mux -> s_addr_reg, T103).
// The cartridge is in the DDR2 (nes_mem.v), so an access takes as long as the DDR2 takes:
// the window raises `dma_done_tgl` when nes_mem says it is done (w_done), and the read data comes with it.
// The window's address space (the cartridge, nothing else of the machine is reachable): the image as the iNES file holds it,
// without the header, 0-based, 21 bits = {PAGE register (64 KB pages), the 16 bit window address}:
//   p_rom = 0   the PRG image (a 16 KB image is at 0..0x3FFF; the machine mirrors it by prg_mask)
//   p_rom = 1   the CHR image (CHR ROM)
// Only this window's own requests reach nes_mem; what another requester (JTAG peek, text layer) asks through the host's port
// is not executed: the JTAG read of the cartridge window is not supported, the window is for the app. Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module nes_win (
    input  wire        clk,
    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire [7:0]  dma_wdata,
    input  wire        dma_rom,
    output wire        dma_done_tgl,
    output wire [7:0]  dma_rdata,
    input  wire        dbg_busy,
    output wire        port_use,
    output wire [15:0] port_addr,
    output wire        port_rom,
    output wire        port_we,
    output wire [7:0]  port_wdata,
    input  wire [4:0]  page,                    // register WIN_PAGE: 64 KB page of the window
    input  wire        p_req,
    input  wire [15:0] p_addr,
    input  wire        p_rom,
    input  wire        p_we,
    input  wire [7:0]  p_wdata,
    // nes_mem
    output wire        w_start,                 // one clock: a window access begins
    output wire        w_we,
    output wire        w_chr,
    output wire [20:0] w_a,
    output wire [7:0]  w_di,
    input  wire        w_done,
    input  wire [7:0]  w_do
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] dreq_s = 3'b000;
    reg        done = 1'b0;
    reg [7:0]  rd = 8'd0;
    reg        p_ram = 1'b0;
    reg        own = 1'b0;                      // the access in progress is this window's
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

    // The host's merge network lives in the static part, far from here (CI run 39: 8.9 ns of routing from p_ram_reg to the
    // cartridge RAM's address pins, WNS -0.664). Everything that comes back is registered before it is used.
    reg        s_req = 1'b0, s_rom = 1'b0, s_we = 1'b0;
    reg [15:0] s_addr = 16'd0;
    reg [7:0]  s_wd = 8'd0;
    reg [4:0]  s_page = 5'd0;
    always @(posedge clk) begin
        dreq_s <= {dreq_s[1:0], dma_req_tgl};
        // the access starts from the window's own registers one clock after use_i (the host's p_* are not used, see above)
        s_req <= use_i; s_rom <= pr_rom; s_we <= pr_we; s_addr <= pr_addr; s_wd <= pr_wd; s_page <= page;
        if (dreq_s[2] != dreq_s[1]) begin
            p_ram <= 1'b1;
            pr_addr <= dma_addr;
            pr_rom <= dma_rom;
            pr_we <= dma_we;
            pr_wd <= dma_wdata;
        end
        if (use_i) begin p_ram <= 1'b0; own <= 1'b1; end
        if (w_done && own) begin
            own <= 1'b0;
            done <= ~done;
            rd <= pr_we ? 8'h00 : w_do;
        end
    end

    assign w_start = s_req;
    assign w_we = s_we;
    assign w_chr = s_rom;
    assign w_a = {s_page, s_addr};
    assign w_di = s_wd;
endmodule
