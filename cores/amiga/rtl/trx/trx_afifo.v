// T85: a small asynchronous FIFO (gray-coded pointers, registers as storage) for the Amiga core's memory
// commands from sys_clk to ui_clk. Written for this project.
// wr is ignored while full; rd is ignored while empty. Both resets assert together at start-up.
`timescale 1ns / 1ps
module trx_afifo #(
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
    reg [W-1:0] mem [0:(1<<AW)-1];
    reg [AW:0] wptr = 0, rptr = 0;                  // binary
    reg [AW:0] wg = 0, rg = 0;                      // gray
    (* ASYNC_REG = "TRUE" *) reg [AW:0] rg_w0 = 0, rg_w1 = 0;     // rg in the write domain
    (* ASYNC_REG = "TRUE" *) reg [AW:0] wg_r0 = 0, wg_r1 = 0;     // wg in the read domain

    wire [AW:0] wnext = wptr + 1'b1;
    wire [AW:0] rnext = rptr + 1'b1;
    wire [AW:0] wnext_g = wnext ^ (wnext >> 1);
    wire [AW:0] rnext_g = rnext ^ (rnext >> 1);

    assign full  = (wg == {~rg_w1[AW:AW-1], rg_w1[AW-2:0]});
    assign empty = (rg == wg_r1);
    assign rdata = mem[rptr[AW-1:0]];

    always @(posedge wclk) begin
        rg_w0 <= rg; rg_w1 <= rg_w0;
        if (wrst) begin wptr <= 0; wg <= 0; end
        else if (wr && !full) begin
            mem[wptr[AW-1:0]] <= wdata;
            wptr <= wnext; wg <= wnext_g;
        end
    end
    always @(posedge rclk) begin
        wg_r0 <= wg; wg_r1 <= wg_r0;
        if (rrst) begin rptr <= 0; rg <= 0; end
        else if (rd && !empty) begin
            rptr <= rnext; rg <= rnext_g;
        end
    end
endmodule
