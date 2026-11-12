// T143: a model of the host's two DDR2 ports for the Neo Geo benches (the circuits of cores/zxnext/sim/ddr2_model.v, one memory behind both ports as in the board).
// c_* (the real-time port) wins the arbiter: an e_* command is taken only in a clock without a c_* command (c_en low, as host/ddr2_port.v has it: the e_* ready is low while c_en is high). Commands are 128 bit words; a command is taken when its
// ready is up (randomly down a quarter of the clocks, always up with FAST), a read returns its word LAT_MIN..LAT_MIN+LAT_RND ui clocks after the command, all reads in order on
// rd_data with the valid of the port that asked; a write offers command and data at once (mask bit 1 = byte not written). The memory is 64 MB at BASE; `mem` is public for
// the bench. LAT_MIN 22 / LAT_RND 30 at 75 MHz is 293 to 693 ns, the figure of T95 5.9 (the arbiter's header gives 546 to 706 ns for a cart line under the worst video load).
`timescale 1ns / 1ps
module neo_ddr2 #(
    parameter [31:0] BASE = 32'h01000000,
    parameter integer SEED = 1
) (
    input  wire         ui_clk,
    input  wire [26:0]  c_addr,
    input  wire [2:0]   c_cmd,
    input  wire         c_en,
    output reg          c_rdy = 1'b0,
    input  wire [127:0] c_wdf_data,
    input  wire [15:0]  c_wdf_mask,
    input  wire         c_wdf_wren,
    output reg          c_rd_valid = 1'b0,
    input  wire [26:0]  e_addr,
    input  wire [2:0]   e_cmd,
    input  wire         e_en,
    output wire         e_rdy,
    input  wire [127:0] e_wdf_data,
    input  wire [15:0]  e_wdf_mask,
    input  wire         e_wdf_wren,
    output reg          e_rd_valid = 1'b0,
    output reg  [127:0] rd_data = 128'd0
);
    reg [127:0] mem [0:4194303] /*verilator public_flat_rw*/;      // 64 MB
    integer LAT_MIN = 22, LAT_RND = 30, FAST = 0;
    integer reads = 0, writes = 0, cyc = 0, seed = SEED, e_reads = 0, e_en_cyc = 0, e_blocked = 0;
    reg [127:0] q_data [0:31];
    integer     q_due  [0:31];
    reg         q_who  [0:31];
    integer     q_wr = 0, q_rd = 0, last_due = 0, lat, k;
    reg [31:0]  ba;
    reg [21:0]  widx;
    reg [127:0] w;
    reg         c_take, e_take;
    reg         e_rdy_r = 1'b0;
    assign e_rdy = e_rdy_r && !c_en;                                // the arbiter: a c_* command in the clock keeps the e_* one out (host/ddr2_port.v: eng = ... && !c_en)
    initial begin
        for (k = 0; k < 4194304; k = k + 1) mem[k] = 128'd0;
        if ($value$plusargs("LATMIN=%d", LAT_MIN)) ;
        if ($value$plusargs("LATRND=%d", LAT_RND)) ;
        if ($test$plusargs("FAST")) FAST = 1;
    end
    always @(posedge ui_clk) begin
        cyc <= cyc + 1;
        c_rdy <= FAST ? 1'b1 : (($random(seed) & 3) != 0);
        e_rdy_r <= FAST ? 1'b1 : (($random(seed) & 3) != 0);
        c_rd_valid <= 1'b0;
        e_rd_valid <= 1'b0;
        c_take = c_en && c_rdy;
        if (e_en) e_en_cyc = e_en_cyc + 1;
        if (e_en && e_rdy_r && c_en) e_blocked = e_blocked + 1;
        e_take = e_en && e_rdy_r && !c_en;
        if (c_take || e_take) begin
            ba = c_take ? {c_addr, 1'b0} : {e_addr, 1'b0};
            widx = (ba - BASE) >> 4;
            if ((c_take ? c_cmd : e_cmd) == 3'b001) begin
                reads <= reads + 1;
                if (!c_take) begin e_reads = e_reads + 1; if (e_reads < 4) $display("  ddr2: e read %0d accepted at ui clock %0d (address %h)", e_reads, cyc, ba); end
                q_data[q_wr % 32] = mem[widx];
                q_who[q_wr % 32] = c_take;
                lat = FAST ? 2 : (LAT_MIN + ({$random(seed)} % (LAT_RND + 1)));
                q_due[q_wr % 32] = (cyc + lat > last_due + 1) ? cyc + lat : last_due + 1;
                last_due = q_due[q_wr % 32];
                q_wr = q_wr + 1;
            end else if ((c_take ? c_cmd : e_cmd) == 3'b000 && (c_take ? c_wdf_wren : e_wdf_wren)) begin
                writes <= writes + 1;
                w = mem[widx];
                for (k = 0; k < 16; k = k + 1) if (!(c_take ? c_wdf_mask[k] : e_wdf_mask[k])) w[8 * k +: 8] = (c_take ? c_wdf_data : e_wdf_data) >> (8 * k);
                mem[widx] = w;
            end
        end
        if (q_rd != q_wr && cyc >= q_due[q_rd % 32]) begin
            rd_data <= q_data[q_rd % 32];
            if (q_who[q_rd % 32]) c_rd_valid <= 1'b1; else e_rd_valid <= 1'b1;
            q_rd = q_rd + 1;
        end
    end
endmodule
