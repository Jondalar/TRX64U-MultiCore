// T95: a model of the host's real-time DDR2 port c_* (ui_clk) for the NES benches: 128 bit words, a command is taken when `c_rdy`
// is up (randomly down a quarter of the clocks), a write offers command and data at once (mask bit 1 = byte not written), a read
// returns its word LAT_MIN..LAT_MIN+LAT_RND ui clocks after its command, in order (mig_ui_model.v's contract; the host's
// mig_arb measured 546-706 ns worst case for a cart line, host/ddr2_port.v). The memory is 4 MB at BASE, as the image sits there.
`timescale 1ns / 1ps
module ddr2c_model #(
    parameter [31:0] BASE = 32'h01000000,
    parameter integer LAT_MIN = 22,                 // ui_clk is 75 MHz (13.3 ns): 22..52 clocks = 293..693 ns
    parameter integer LAT_RND = 30,
    parameter integer SEED = 1,
    parameter integer DEPTH_LOG2 = 18               // the memory is 2^DEPTH_LOG2 words of 16 bytes (default 4 MB; T182 uses 21: 32 MB)
) (
    input  wire         ui_clk,
    input  wire [26:0]  c_addr,
    input  wire [2:0]   c_cmd,
    input  wire         c_en,
    output reg          c_rdy = 1'b0,
    input  wire [127:0] c_wdf_data,
    input  wire [15:0]  c_wdf_mask,
    input  wire         c_wdf_wren,
    output reg  [127:0] c_rd_data = 128'd0,
    output reg          c_rd_valid = 1'b0
);
    reg [127:0] mem [0:(1 << DEPTH_LOG2) - 1] /*verilator public_flat_rw*/;
    integer reads = 0, writes = 0, cyc = 0, seed = SEED;
    reg [127:0] q_data [0:15];
    integer     q_due  [0:15];
    integer     q_wr = 0, q_rd = 0, last_due = 0, k, lat;
    wire [31:0] byte_addr = {c_addr, 1'b0};
    wire [DEPTH_LOG2-1:0] widx = (byte_addr - BASE) >> 4;
    reg [127:0] w;
    always @(posedge ui_clk) begin
        cyc <= cyc + 1;
        c_rdy <= (($random(seed) & 3) != 0);
        c_rd_valid <= 1'b0;
        if (c_en && c_rdy) begin
            if (c_cmd == 3'b001) begin
                reads <= reads + 1;
                q_data[q_wr % 16] = mem[widx];
                lat = LAT_MIN + ({$random(seed)} % (LAT_RND + 1));
                q_due[q_wr % 16] = (cyc + lat > last_due + 1) ? cyc + lat : last_due + 1;
                last_due = q_due[q_wr % 16];
                q_wr = q_wr + 1;
            end else if (c_cmd == 3'b000 && c_wdf_wren) begin
                writes <= writes + 1;
                w = mem[widx];
                for (k = 0; k < 16; k = k + 1) if (!c_wdf_mask[k]) w[8 * k +: 8] = c_wdf_data[8 * k +: 8];
                mem[widx] <= w;
            end
        end
        if (q_rd != q_wr && cyc >= q_due[q_rd % 16]) begin
            c_rd_data <= q_data[q_rd % 16];
            c_rd_valid <= 1'b1;
            q_rd = q_rd + 1;
        end
    end
endmodule
