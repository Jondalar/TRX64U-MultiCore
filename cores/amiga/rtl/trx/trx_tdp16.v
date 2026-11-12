// T85: a true dual port RAM of 16-bit words on one clock, read first (the old word comes out when a port writes), both ports with a
// registered read: what a block RAM is. Under the chipset's clock enable (ce28). Written for this project.
`timescale 1ns / 1ps
module trx_tdp16 #(
    parameter AW = 12
) (
    input  wire          ce28,
    input  wire          clk,
    input  wire [AW-1:0] a_addr,
    input  wire          a_we,
    input  wire [15:0]   a_din,
    output reg  [15:0]   a_dout = 16'd0,
    input  wire [AW-1:0] b_addr,
    input  wire          b_we,
    input  wire [15:0]   b_din,
    output reg  [15:0]   b_dout = 16'd0
);
    (* ram_style = "block" *) reg [15:0] mem [0:(1<<AW)-1];
    always @(posedge clk) if (ce28) begin
        if (a_we) mem[a_addr] <= a_din;
        a_dout <= mem[a_addr];
    end
    always @(posedge clk) if (ce28) begin
        if (b_we) mem[b_addr] <= b_din;
        b_dout <= mem[b_addr];
    end
endmodule
