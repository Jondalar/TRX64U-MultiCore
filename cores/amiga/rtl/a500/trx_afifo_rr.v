// T141/A500 (2026-11-08): copy of cores/amiga1200/rtl/trx_afifo_rr.v for the Amiga 500 core's amiga_sdc (cores/amiga/rtl/trx).
// T97 5.11: trx_afifo (cores/amiga/rtl/trx/trx_afifo.v) with a registered read, for the Amiga 1200 core's amiga_sdc. The shared FIFO
// reads its storage combinationally (mem[rptr]), which Vivado builds as distributed RAM: 190 LUT for the two command/response
// queues of amiga_sdc (156 and 128 bit wide, 4 deep). Here the read is a register, q, loaded on every rclk edge from the
// address the read pointer is about to have (rptr, or rptr + 1 when this edge pops), so q equals mem[rptr] whenever the FIFO is
// not empty - the only time a consumer looks at rdata - and the storage is block RAM (write port on wclk, read port on rclk).
// Why q is valid when empty falls: the word is written on wclk before its gray pointer is, the pointer takes two rclk edges
// through the synchroniser, and q is reloaded on each of them, so the last reload sees the word. Pointers, gray codes, full and
// empty are the shared file's, unchanged. Not a drop-in for consumers that read rdata while empty (amiga_mem does not use this
// one). Proven equal to the shared FIFO on everything a consumer sees by cores/amiga1200/sim/afifo/run.sh.
`timescale 1ns / 1ps
module trx_afifo_rr #(
    parameter W  = 8,
    parameter AW = 3
) (
    input  wire         wclk,
    input  wire         wrst,
    input  wire         wr,
    input  wire [W-1:0] wdata,
    output wire         full,
    input  wire         rclk,
    input  wire         rrst,
    input  wire         rd,
    output wire [W-1:0] rdata,
    output wire         empty
);
    (* ram_style = "block" *) reg [W-1:0] mem [0:(1<<AW)-1];
    reg [AW:0] wptr = 0, rptr = 0;                  // binary
    reg [AW:0] wg = 0, rg = 0;                      // gray
    (* ASYNC_REG = "TRUE" *) reg [AW:0] rg_w0 = 0, rg_w1 = 0;     // rg in the write domain
    (* ASYNC_REG = "TRUE" *) reg [AW:0] wg_r0 = 0, wg_r1 = 0;     // wg in the read domain
    reg [W-1:0] q = 0;

    wire [AW:0] wnext = wptr + 1'b1;
    wire [AW:0] rnext = rptr + 1'b1;
    wire [AW:0] wnext_g = wnext ^ (wnext >> 1);
    wire [AW:0] rnext_g = rnext ^ (rnext >> 1);

    assign full  = (wg == {~rg_w1[AW:AW-1], rg_w1[AW-2:0]});
    assign empty = (rg == wg_r1);
    assign rdata = q;

    wire pop = rd && !empty;
    wire [AW-1:0] raddr = pop ? rnext[AW-1:0] : rptr[AW-1:0];

    always @(posedge wclk) begin
        rg_w0 <= rg; rg_w1 <= rg_w0;
        if (wrst) begin wptr <= 0; wg <= 0; end
        else if (wr && !full) begin
            wptr <= wnext; wg <= wnext_g;
        end
        if (!wrst && wr && !full) mem[wptr[AW-1:0]] <= wdata;
    end
    always @(posedge rclk) begin
        wg_r0 <= wg; wg_r1 <= wg_r0;
        if (rrst) begin rptr <= 0; rg <= 0; end
        else if (pop) begin
            rptr <= rnext; rg <= rnext_g;
        end
        q <= mem[raddr];
    end
endmodule
