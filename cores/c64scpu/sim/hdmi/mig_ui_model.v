// Behavioral model of a DDR2 controller's native UI (app_*) for simulation, shaped like the MIG 7-series
// UI it stood in for (DDR2-600 x16, 4:1, ui_clk = 75 MHz, BL8: one 128-bit word per command, strict
// ordering, BANK_ROW_COLUMN: app_addr[25:23] bank, [22:10] row, 4 bank machines). Since OPT-ddr2 §9 the
// controller in c64_basic is ddr2_lean (in order, a 2-entry command queue, closed page, banks on the low
// burst bits, refresh every 560 cycles, read latency about 8 cycles + queue); this model is NOT that: it is
// the interface contract the benches of the clients (mig_arb, cart_fill, the engine, pix_layer, ...)
// run against, with more commands in flight and open rows, and none of them depends on the difference.
// ddr2_lean's own timing is checked against designs/ddr2_test/sim/ddr2_model.v (run.sh arb: the real
// mig_arb on ddr2_lean).
// Timing, in ui_clk cycles (assumed, not measured on the board):
//   read: data RD_LAT after the command is taken if the bank has the row
//         open, + MISS more if not (precharge + activate), at least one
//         cycle after the previous read's data (in order), + WTR when a write
//         was taken in the WTR cycles before;
//   app_rdy low while all 4 bank machines are busy (one per command, from
//   when it is taken until its column command: BM_HIT cycles, BM_MISS on a
//   row miss), while 8 reads wait for their data, during a refresh
//   (REF_BUSY cycles every REFI, which also closes the rows), and on
//   STALL_PCT percent of the cycles at random; app_wdf_rdy low on STALL_PCT
//   percent at random.
// Writes land when command and data have both been taken (data in order); a
// read returns memory as it is when its command is taken.
// mem holds MEM_WORDS 128-bit words; addresses wrap.
module mig_ui_model #(
    parameter MEM_WORDS = 1 << 20,
    parameter RD_LAT    = 20,
    parameter MISS      = 4,
    parameter WTR       = 3,
    parameter BM_HIT    = 4,
    parameter BM_MISS   = 8,
    parameter REFI      = 585,          // 7.8 us
    parameter REF_BUSY  = 14,           // tRP + tRFC (127.5 ns) and the drain
    parameter STALL_PCT = 3,
    parameter CALIB     = 200
) (
    input  wire         ui_clk,
    output reg          ui_rst = 1'b1,
    output reg          calib_done = 1'b0,
    input  wire [26:0]  app_addr,
    input  wire [2:0]   app_cmd,
    input  wire         app_en,
    output wire         app_rdy,
    input  wire [127:0] app_wdf_data,
    input  wire [15:0]  app_wdf_mask,
    input  wire         app_wdf_wren,
    output wire         app_wdf_rdy,
    output reg  [127:0] app_rd_data = 0,
    output reg          app_rd_data_valid = 1'b0
);
    reg [127:0] mem [0:MEM_WORDS-1] /*verilator public_flat_rw*/;

    reg [31:0] cyc = 0;
    reg [31:0] lfsr = 32'h1;
    always @(posedge ui_clk) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    wire stall_c = (lfsr[6:0] % 100) < STALL_PCT;
    wire stall_d = (lfsr[22:16] % 100) < STALL_PCT;

    // refresh
    reg [15:0] ref_cnt = 0;
    reg [7:0]  ref_busy = 0;

    // open reads (in order): data and when it is due
    reg [127:0] q_data [0:7];
    reg [31:0]  q_due  [0:7];
    reg [3:0]   q_n = 0;
    reg [2:0]   q_rd = 0, q_wr = 0;
    integer k;
    reg [31:0]  bm_free [0:3];
    initial for (k = 0; k < 4; k = k + 1) bm_free[k] = 0;
    reg [31:0]  last_due = 0, last_wr = 0;
    reg [12:0]  open_row [0:7];
    reg [7:0]   row_valid = 0;

    // writes: commands and data, each in order
    reg [26:0]  wc_addr [0:7];
    reg [127:0] wd_data [0:7];
    reg [15:0]  wd_mask [0:7];
    reg [3:0]   wc_n = 0, wd_n = 0;
    reg [2:0]   wc_rd = 0, wc_wr = 0, wd_rd = 0, wd_wr = 0;

    wire bm_ok = bm_free[0] <= cyc || bm_free[1] <= cyc || bm_free[2] <= cyc || bm_free[3] <= cyc;
    assign app_rdy     = calib_done && ref_busy == 0 && bm_ok && q_n < 8 && wc_n < 8 && !stall_c;
    assign app_wdf_rdy = calib_done && wd_n < 8 && !stall_d;

    wire take_c = app_en && app_rdy;
    wire take_d = app_wdf_wren && app_wdf_rdy;
    wire [2:0]  bank = app_addr[25:23];
    wire [12:0] row  = app_addr[22:10];
    wire [31:0] widx = app_addr[26:3] % MEM_WORDS;

    reg [31:0] due;
    reg [127:0] m;
    always @(posedge ui_clk) begin
        cyc <= cyc + 1;
        if (cyc == 16) ui_rst <= 1'b0;
        if (cyc == CALIB) calib_done <= 1'b1;

        if (ref_busy != 0) ref_busy <= ref_busy - 1;
        if (ref_cnt == REFI - 1) begin
            ref_cnt <= 0; ref_busy <= REF_BUSY; row_valid <= 0;
        end else ref_cnt <= ref_cnt + 1;

        // read data out, in order
        app_rd_data_valid <= 1'b0;
        if (q_n != 0 && q_due[q_rd] <= cyc) begin
            app_rd_data       <= q_data[q_rd];
            app_rd_data_valid <= 1'b1;
            q_rd <= q_rd + 1;
        end

        // writes first (blocking): a read taken after a complete write sees it
        if (take_d) begin
            wd_data[wd_wr] = app_wdf_data; wd_mask[wd_wr] = app_wdf_mask; wd_wr = wd_wr + 1; wd_n = wd_n + 1;
        end
        if (take_c && app_cmd != 3'b001) begin
            wc_addr[wc_wr] = app_addr; wc_wr = wc_wr + 1; wc_n = wc_n + 1;
            last_wr <= cyc;
        end
        if (wc_n != 0 && wd_n != 0) begin
            m = mem[wc_addr[wc_rd][26:3] % MEM_WORDS];
            for (k = 0; k < 16; k = k + 1)
                if (!wd_mask[wd_rd][k]) m[8*k +: 8] = wd_data[wd_rd][8*k +: 8];
            mem[wc_addr[wc_rd][26:3] % MEM_WORDS] = m;
            wc_rd = wc_rd + 1; wd_rd = wd_rd + 1; wc_n = wc_n - 1; wd_n = wd_n - 1;
        end
        if (take_c) begin
            if (!row_valid[bank] || open_row[bank] != row) begin
                row_valid[bank] <= 1'b1; open_row[bank] <= row;
            end
            // a free bank machine takes the command
            if (bm_free[0] <= cyc) k = 0; else if (bm_free[1] <= cyc) k = 1;
            else if (bm_free[2] <= cyc) k = 2; else k = 3;
            bm_free[k] <= cyc + ((!row_valid[bank] || open_row[bank] != row) ? BM_MISS : BM_HIT);
            if (app_cmd == 3'b001) begin
                due = cyc + RD_LAT;
                if (!row_valid[bank] || open_row[bank] != row) due = due + MISS;
                if (cyc - last_wr < WTR) due = due + WTR;
                if (due <= last_due) due = last_due + 1;
                last_due <= due;
                q_data[q_wr] <= mem[widx]; q_due[q_wr] <= due; q_wr <= q_wr + 1;
            end
        end
        q_n <= q_n + (take_c && app_cmd == 3'b001) - (q_n != 0 && q_due[q_rd] <= cyc);
    end
endmodule
