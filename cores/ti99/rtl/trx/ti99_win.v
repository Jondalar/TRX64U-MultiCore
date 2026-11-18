// TI-99/4A core: the app's DMA window (a toggle per byte, as for the C64 and VC-20 cores) as a wishbone master onto the
// Mega99 mainboard's ROM/GROM/PEB slave (24-bit byte address). Written for this project.
//
// The app sees a 64 KB aperture (dma_addr, 16 bit); the register block supplies the upper byte (wbhi) of the wishbone
// address, so the whole map is reachable:
// What the host sends is not the app's offset (cores/common/dma_win.v: "dma_addr = byte offset in the block: char 0x0000, KERNAL 0x2000, BASIC
// 0x4000", the app's apertures being BASIC +0x0000 (8 KB), KERNAL +0x2000 (8 KB), CHAR +0x4000 (4 KB, mirrored), colour RAM +0x7000), so the window
// undoes that: dma_addr 0x4000..0x5FFF (BASIC) is linear 0x0000..0x1FFF, 0x2000..0x3FFF (KERNAL) stays, 0x0000..0x0FFF (CHAR) is linear 0x4000..0x4FFF.
// wbadd is added to address bits 15:8 afterwards, so any 8 KB of the map can be reached through the BASIC block (a GROM n through offset 0 with
// wbadd = n * 0x20). The app's linear offset, wbhi and wbadd: wishbone address = {wbhi, 16'h0000} + linear + {wbadd, 8'h00}.
//   wbhi 0x01  console ROM   0x010000 high bytes (4096), 0x011000 low bytes (4096)
//   wbhi 0x10  GROM 0..7     0x100000 + n * 0x2000 (6144 bytes each)
//   wbhi 0x03 / 0x20..       cartridge ROM
//   wbhi 0x80+               the peripheral expansion box (DSRs, disk)
// One byte per request: the toggle is sampled in sys_clk through three flops (the address and data are stable by then),
// the wishbone cycle runs until ack (or a time-out, then the byte reads 0), then done toggles. wbhi crosses from cpu_clk
// through two flops: the app writes it long before the byte request, and the request's own synchroniser is a stage longer.
`timescale 1ns / 1ps
module ti99_win #(
    parameter TIMEOUT = 1023
) (
    input  wire        clk,
    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire [7:0]  dma_wdata,
    input  wire [7:0]  wbhi,
    input  wire [7:0]  wbadd,
    output reg         dma_done_tgl = 1'b0,
    output reg  [7:0]  dma_rdata = 8'h00,
    output reg  [23:0] m_adr = 24'd0,
    output reg  [7:0]  m_dat = 8'd0,
    output reg         m_we = 1'b0,
    output reg         m_stb = 1'b0,
    output wire        m_cyc,
    input  wire [7:0]  s_dat,
    input  wire        s_ack,
    output reg         busy = 1'b0
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [7:0] hi_a = 8'd0, hi_b = 8'd0, ad_a = 8'd0, ad_b = 8'd0;
    reg [10:0] to = 11'd0;
    reg        gap = 1'b0;

    assign m_cyc = m_stb;

    // the host's block-relative address back to the app's linear offset
    reg [15:0] lin;
    always @* begin
        if (dma_addr[15:13] == 3'b010)      lin = {3'b000, dma_addr[12:0]};              // BASIC block: linear 0x0000..0x1FFF
        else if (dma_addr[15:12] == 4'h0)   lin = {4'h4, dma_addr[11:0]};                // CHAR block:  linear 0x4000..0x4FFF
        else                                lin = dma_addr;                              // KERNAL (0x2000..0x3FFF) and anything else as it is
    end
    wire [23:0] wb_a = {hi_b, lin} + {8'h00, ad_b, 8'h00};

    always @(posedge clk) begin
        rq   <= {rq[1:0], dma_req_tgl};
        hi_a <= wbhi;
        hi_b <= hi_a;
        ad_a <= wbadd;
        ad_b <= ad_a;
        if (gap) begin
            gap <= 1'b0;                                  // one idle clock so the slave's ack falls before the next byte
        end else if (!busy) begin
            if (rq[2] != rq[1]) begin
                m_adr <= wb_a;
                m_dat <= dma_wdata;
                m_we  <= dma_we;
                m_stb <= 1'b1;
                busy  <= 1'b1;
                to    <= 11'd0;
            end
        end else begin
            to <= to + 11'd1;
            if (s_ack || to >= TIMEOUT) begin
                if (!m_we) dma_rdata <= s_ack ? s_dat : 8'h00;
                else       dma_rdata <= 8'h00;
                dma_done_tgl <= ~dma_done_tgl;
                m_stb <= 1'b0;
                m_we  <= 1'b0;
                busy  <= 1'b0;
                gap   <= 1'b1;
            end
        end
    end
endmodule
