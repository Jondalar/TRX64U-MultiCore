// T134: a model of the host real-time DDR2 port c_* (ui_clk) for the tape bench (an own copy of cores/zx/sim/ddr2c_model.v, T91, same author): 128 bit words,
// a command is taken when c_rdy is up (randomly down a quarter of the clocks), a read returns its word after LAT_MIN..LAT_MIN+LAT_RND ui clocks, in order.
`timescale 1ns / 1ps
module ddr2c_model #(
    parameter [31:0] BASE = 32'h01800000,
    parameter integer LAT_MIN = 22,
    parameter integer LAT_RND = 30,
    parameter integer SEED = 1
) (
    input  wire         ui_clk,
    input  wire [26:0]  c_addr,
    input  wire [2:0]   c_cmd,
    input  wire         c_en,
    output reg          c_rdy = 1'b0,
    output reg  [127:0] c_rd_data = 128'd0,
    output reg          c_rd_valid = 1'b0
);
    reg [127:0] mem [0:65535] /*verilator public_flat_rw*/;
    integer reads = 0, cyc = 0, seed = SEED;
    reg [127:0] q_data [0:15];
    integer     q_due  [0:15];
    integer     q_wr = 0, q_rd = 0, last_due = 0, lat;
    wire [31:0] byte_addr = {c_addr, 1'b0};
    wire [15:0] widx = (byte_addr - BASE) >> 4;
    always @(posedge ui_clk) begin
        cyc <= cyc + 1;
        c_rdy <= (($random(seed) & 3) != 0);
        c_rd_valid <= 1'b0;
        if (c_en && c_rdy && c_cmd == 3'b001) begin
            reads <= reads + 1;
            q_data[q_wr % 16] = mem[widx];
            lat = LAT_MIN + ({$random(seed)} % (LAT_RND + 1));
            q_due[q_wr % 16] = (cyc + lat > last_due + 1) ? cyc + lat : last_due + 1;
            last_due = q_due[q_wr % 16];
            q_wr = q_wr + 1;
        end
        if (q_rd != q_wr && cyc >= q_due[q_rd % 16]) begin
            c_rd_data <= q_data[q_rd % 16];
            c_rd_valid <= 1'b1;
            q_rd = q_rd + 1;
        end
    end
endmodule
