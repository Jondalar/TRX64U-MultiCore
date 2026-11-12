// T116: models of the host's two DDR2 ports for the Next core's benches (the circuits of cores/cpc/sim/ddr2c_model.v and ddr2e_model).
// c_* (the real-time port, ui_clk): 128 bit words, a command is taken when `c_rdy` is up (randomly down a quarter of the clocks), a read returns
// its word LAT_MIN..LAT_MIN+LAT_RND ui clocks after its command, in order, a write offers command and data at once (mask bit 1 = byte not
// written). The memory is 8 MB at BASE (the machine's 2 MB at +0, the SD staging sector at +0x200000); `mem` is public for the bench.
// LAT_MIN 22 / LAT_RND 30 at 75 MHz is 293 to 693 ns, the figure of T95 5.9. `fast` makes every command ready at once with the minimum latency.
`timescale 1ns / 1ps
module zxn_ddr2c #(
    parameter [31:0] BASE = 32'h01000000,
    parameter integer LAT_MIN = 22,
    parameter integer LAT_RND = 30,
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
    output reg  [127:0] c_rd_data = 128'd0,
    output reg          c_rd_valid = 1'b0
);
    reg [127:0] mem [0:524287] /*verilator public_flat_rw*/;      // 8 MB
    integer reads = 0, writes = 0, cyc = 0, seed = SEED;
    reg [127:0] q_data [0:15];
    integer     q_due  [0:15];
    integer     q_wr = 0, q_rd = 0, last_due = 0, lat, k;
    wire [31:0] byte_addr = {c_addr, 1'b0};
    wire [18:0] widx = (byte_addr - BASE) >> 4;
    reg [127:0] w;
    integer wr_after_rd_violations = 0;
    initial for (k = 0; k < 524288; k = k + 1) mem[k] = 128'd0;
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

// the host's general port e_* as the frame writer uses it: a write of one 128 bit word per command (the frame buffers at FB_BASE, 4 MB)
module zxn_ddr2e #(
    parameter [31:0] BASE = 32'h01D00000
) (
    input  wire         ui_clk,
    input  wire [26:0]  e_addr,
    input  wire [2:0]   e_cmd,
    input  wire         e_en,
    output reg          e_rdy = 1'b0,
    input  wire [127:0] e_wdf_data,
    input  wire         e_wdf_wren
);
    reg [127:0] fb [0:262143] /*verilator public_flat_rw*/;       // 4 MB: four buffers of 1 MB stride
    integer writes = 0, seed = 5, k;
    wire [31:0] byte_addr = {e_addr, 1'b0};
    wire [17:0] widx = (byte_addr - BASE) >> 4;
    initial for (k = 0; k < 262144; k = k + 1) fb[k] = 128'd0;
    always @(posedge ui_clk) begin
        e_rdy <= (($random(seed) & 3) != 0);
        if (e_en && e_rdy && e_cmd == 3'b000 && e_wdf_wren) begin
            fb[widx] <= e_wdf_data;
            writes <= writes + 1;
        end
    end
endmodule
