// T91 D5/D11: the app's DMA window (a toggle per byte, ultimate_sys.vhd) and the host's second memory port, as nes_win.v does it: the
// window asks the host for the port (win_port_*) and starts its access from its own registers (no path core -> static host -> core in
// one clock, T103). The address space of the window (the machine's RAM and ROM, nothing else is reachable):
//   dma_rom = 0   the 128 KB RAM, {WIN_PAGE (64 KB page, bit 0), the 16 bit window address}; bank n is at n * 0x4000
//   dma_rom = 1   the 32 KB ROM area, {ROM_SEL (WIN_PAGE b2:1, one bit used), the 14 bit window address}: the host's ROM aperture
//                 (ultimate_sys.vhd, r_rom) is 16 KB wide - its dma_addr[13:0] is contiguous, the bits above are the C64 ROM block's
//                 own layout - so the app picks the 16 KB bank by register, as the Atari and Apple cores do
// Only this window's own requests reach zx_mem; what another requester (JTAG peek, text layer) asks through the host's port is not
// executed. Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zx_win (
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
    input  wire [2:0]  page,                    // register WIN_PAGE: b0 the RAM half, b2:1 the ROM bank
    input  wire        p_req,                   // the host's merged port: not used (see above)
    // zx_mem
    output wire        w_start,
    output wire        w_we,
    output wire        w_rom,
    output wire [16:0] w_a,
    output wire [7:0]  w_di,
    input  wire        w_done,
    input  wire [7:0]  w_do
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] dreq_s = 3'b000;
    reg        done = 1'b0;
    reg [7:0]  rd = 8'd0;
    reg        p_ram = 1'b0;
    reg        own = 1'b0;
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

    reg        s_req = 1'b0, s_rom = 1'b0, s_we = 1'b0;
    reg [15:0] s_addr = 16'd0;
    reg [7:0]  s_wd = 8'd0;
    reg [2:0]  s_page = 3'b000;
    always @(posedge clk) begin
        dreq_s <= {dreq_s[1:0], dma_req_tgl};
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
    assign w_rom = s_rom;
    assign w_a = s_rom ? {2'b00, s_page[1], s_addr[13:0]} : {s_page[0], s_addr};
    assign w_di = s_wd;
endmodule
