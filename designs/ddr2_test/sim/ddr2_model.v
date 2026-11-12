// Behavioural DDR2 SDRAM, x16, for the ddr2_lean benches: MT47H64M16 (1 Gbit, 8 banks, row 13,
// column 10 bits), timings of the -25E as in host/mig_ddr2.prj, from the JEDEC rules.
//   - commands are taken at the rising edge of CK when cke and cs_n allow: MRS/EMRS (CL, AL, BL,
//     WR, DLL, OCD, DQS#), ACTIVE, READ/WRITE with and without auto-precharge, PRECHARGE (A10 =
//     all), REFRESH. Data is BL8 (sequential, column bits 2:0 = 0) only.
//   - checks (each prints "DDR2 ERROR: ..." and counts in `errors`): init order and the 200 us /
//     200 clock waits, tMRD, tRCD (AL included), tRAS, tRP, tRC, tRRD, tFAW, tRFC, tWR, tWTR, tRTP,
//     read/write turn-around, data bus overlap, tDQSS (+-0.25 tCK, rising edge first), DQ
//     setup/hold against DQS, command/address setup/hold against CK, refresh interval (9 x
//     tREFI, the postponing limit), banks open when a refresh or mode register comes.
//   - reads: DQS and DQ come DQSCK after the CK edge, DQ is unknown from tQHS before to tDQSQ after
//     every DQS edge, DQS is driven low one clock before the first edge and half a clock after
//     the last beat. Each DQ bit has its own tDQSQ/tQHS.
// The ports are what is on the DRAM's pins (ddr2_board delays them); outputs are Z when idle.
// Memory: {row, bank, column / 8} modulo 2^MEMBITS indexes 128-bit bursts, keep the bench's
// addresses below that; a burst never written reads back a hash of its index.
`timescale 1ps/1ps

module ddr2_model #(
    parameter integer TCK     = 6668,            // ps
    parameter integer MEMBITS = 20,
    parameter integer DQSCK   = 200,             // ps after the CK edge
    parameter integer T_PWRUP = 200000000,       // ps, 200 us from the first clock
    parameter integer QUIET   = 0
) (
    input  wire        ck,
    input  wire        cke, cs_n, ras_n, cas_n, we_n,
    input  wire [2:0]  ba,
    input  wire [12:0] addr,
    input  wire [1:0]  dm,
    input  wire        odt,
    input  wire [15:0] dq_in,
    input  wire [1:0]  dqs_in,
    output reg  [15:0] dq_out,
    output reg  [1:0]  dqs_out,
    output reg  [1:0]  dqs_n_out
);
    localparam integer tRCD = 12500, tRP = 12500, tRAS = 40000, tRC = 55000, tRRD = 10000,
                       tFAW = 45000, tWR = 15000, tWTR = 7500, tRFC = 127500,
                       tREFI = 7800000, tDS = 250, tDH = 250, tIS = 250, tIH = 375;
    localparam integer HALF = TCK / 2;
    integer errors = 0;
    integer n_ref = 0, n_act = 0, n_rd = 0, n_wr = 0;
    time    max_ref_gap = 0;
    integer refs_live = 0;

    task err(input [255:0] what);
        begin
            errors = errors + 1;
            if (!QUIET || errors < 20) $display("DDR2 ERROR @%0t: %0s", $time, what);
        end
    endtask

    // ---- memory ----
    reg [127:0] mem  [0:(1 << MEMBITS) - 1];
    reg         memv [0:(1 << MEMBITS) - 1];
    integer mi;
    initial for (mi = 0; mi < (1 << MEMBITS); mi = mi + 1) memv[mi] = 1'b0;
    function [127:0] fresh(input [31:0] idx);
        reg [31:0] x;
        begin
            x = idx * 32'h9E3779B1;
            fresh = {x ^ 32'hA5A5A5A5, x + 32'h01234567, ~x, x ^ 32'h5A5A5A5A};
        end
    endfunction
    function [MEMBITS-1:0] index(input [12:0] row, input [2:0] bank, input [9:0] col);
        reg [23:0] full;
        begin
            full  = {row, bank, col[9:3]};
            index = full[MEMBITS-1:0];
        end
    endfunction

    // ---- mode registers ----
    integer cl = 0, al = 0, wr_ck = 0, bl = 0, rl = 0, wl = 0;
    reg     dll_en = 1'b0, emr_done = 1'b0;
    time    t_dll_reset = 0;

    // ---- clock edges at the DRAM ----
    integer hc = 1;                              // half clock count, rising edges even
    time    hc_time [0:63];
    time    t_first_ck = 0;
    reg     ck_seen = 1'b0;

    // ---- bank and global state ----
    reg        b_open [0:7];
    reg [12:0] b_row  [0:7];
    time       b_act  [0:7];
    time       b_ready [0:7];                     // next ACTIVE no earlier than this
    time       b_pre_ok [0:7];                    // explicit PRECHARGE no earlier
    time       act_hist [0:3];
    time       t_last_act = 0, t_last_ref = 0, t_last_mrs = 0, t_last_rd = 0, t_last_wr = 0,
               t_wr_end = 0, t_pall_done = 0;
    reg        rd_seen = 1'b0, wr_seen = 1'b0;
    integer    init_step = 0;                     // 1 PALL, 3 EMR2/3, 4 EMR, 5 MR+DLL reset, 6 PALL, 8 two REF, 9 MR
    reg        init_done = 1'b0;
    integer bk;
    initial begin
        dq_out = 16'hzzzz; dqs_out = 2'bzz; dqs_n_out = 2'bzz;
        for (bk = 0; bk < 8; bk = bk + 1) begin
            b_open[bk] = 1'b0; b_row[bk] = 13'd0; b_act[bk] = 0; b_ready[bk] = 0; b_pre_ok[bk] = 0;
        end
        for (bk = 0; bk < 4; bk = bk + 1) act_hist[bk] = 0;
        for (bk = 0; bk < 64; bk = bk + 1) hc_time[bk] = 0;
    end

    // ---- command / address hold against CK (setup is checked at the edge) ----
    time t_cmd_change = 0, t_ck_rise = 0;
    always @(cs_n or ras_n or cas_n or we_n or ba or addr) begin
        if (ck_seen && init_done && ($time - t_ck_rise) < tIH) err("command/address hold (tIH)");
        t_cmd_change = $time;
    end

    // ---- read output schedule, by half clock ----
    reg        s_drv [0:63];                       // DQS driven during this half clock
    reg        s_beat [0:63];                      // a data beat starts at this edge
    reg [15:0] s_data [0:63];
    integer hk;
    initial for (hk = 0; hk < 64; hk = hk + 1) begin s_drv[hk] = 1'b0; s_beat[hk] = 1'b0; s_data[hk] = 16'd0; end
    function integer dqsq(input integer bit_i); dqsq = 100 + (bit_i % 4) * 60; endfunction
    function integer qhs(input integer bit_i);  qhs = 250 + (bit_i % 3) * 60;  endfunction

    // ---- writes in flight ----
    reg        wb_v   [0:3];
    integer    wb_h   [0:3];
    reg [12:0] wb_row [0:3];
    reg [2:0]  wb_bank[0:3];
    reg [9:0]  wb_col [0:3];
    integer    wb_beat0 [0:3];
    integer    wb_beat1 [0:3];
    integer wi;
    initial for (wi = 0; wi < 4; wi = wi + 1) begin wb_v[wi] = 1'b0; wb_beat0[wi] = 0; wb_beat1[wi] = 0; end

    // ---- DRAM clock: read data out, then the command of a rising edge ----
    always @(ck) begin : clk_edge
        integer b, k, slot, h;
        reg [2:0]  cmd;
        reg [12:0] a_l;
        reg [2:0]  ba_l;
        reg [MEMBITS-1:0] ix;
        reg [127:0] w;
        time       pre_start, t_int;
        reg        ap;
        if (ck === 1'b1) hc = (hc % 2 != 0) ? hc + 1 : hc + 2;
        else             hc = (hc % 2 != 0) ? hc + 2 : hc + 1;
        h = hc % 64;
        hc_time[h] = $time;
        if (!ck_seen) begin ck_seen = 1'b1; t_first_ck = $time; end

        // read data and strobe of the half clock that starts here
        if (s_drv[h]) begin
            if (s_beat[h]) begin
                dqs_out   <= #(DQSCK) {2{(hc % 2 == 0) ? 1'b1 : 1'b0}};
                dqs_n_out <= #(DQSCK) {2{(hc % 2 == 0) ? 1'b0 : 1'b1}};
                for (b = 0; b < 16; b = b + 1) dq_out[b] <= #(DQSCK + dqsq(b)) s_data[h][b];
            end else begin
                dqs_out   <= #(DQSCK) 2'b00;
                dqs_n_out <= #(DQSCK) 2'b11;
            end
        end else if (s_drv[(h + 63) % 64]) begin
            dqs_out <= #(DQSCK) 2'bzz; dqs_n_out <= #(DQSCK) 2'bzz;
        end
        // DQ unknown from tQHS before the next edge (or off after the last beat)
        if (s_beat[(h + 1) % 64]) begin
            for (b = 0; b < 16; b = b + 1) dq_out[b] <= #(DQSCK + HALF - qhs(b)) 1'bx;
        end else if (s_beat[h]) begin
            for (b = 0; b < 16; b = b + 1) dq_out[b] <= #(DQSCK + HALF - qhs(b)) 1'bz;
        end
        s_drv[(h + 40) % 64] = 1'b0;                // entries long past
        s_beat[(h + 40) % 64] = 1'b0;

        if (ck === 1'b1) begin
            t_ck_rise = $time;
            if (ck_seen && cs_n === 1'b0 && cke === 1'b1) begin
                cmd  = {ras_n, cas_n, we_n};
                a_l  = addr; ba_l = ba;
                if (cmd != 3'b111) begin
                    if (init_done && ($time - t_cmd_change) < tIS) err("command/address setup (tIS)");
                    if (!init_done && ($time - t_first_ck) < T_PWRUP) err("command before 200 us of clock");
                end
                case (cmd)
                3'b111: ;                                           // NOP
                3'b000: begin                                       // MRS / EMRS
                    for (b = 0; b < 8; b = b + 1) if (b_open[b]) err("MRS with a bank open");
                    if ($time < t_pall_done) err("MRS inside tRP");
                    if (t_last_mrs != 0 && ($time - t_last_mrs) < 2 * TCK) err("tMRD");
                    t_last_mrs = $time;
                    case (ba_l)
                    3'd0: begin
                        bl = (a_l[2:0] == 3'd3) ? 8 : (a_l[2:0] == 3'd2) ? 4 : 0;
                        if (bl != 8) err("burst length not 8");
                        if (a_l[3]) err("interleaved burst");
                        cl = a_l[6:4];
                        wr_ck = a_l[11:9] + 1;
                        if (a_l[8]) begin
                            t_dll_reset = $time;
                            if (init_step == 4) init_step = 5;
                        end else if (init_step == 8) init_step = 9;
                        if (!emr_done) err("MR before EMR(1)");
                        rl = cl + al; wl = rl - 1;
                    end
                    3'd1: begin
                        dll_en = !a_l[0];
                        al = a_l[5:3];
                        if (!dll_en) err("DLL disabled");
                        if (a_l[10] || a_l[11] || a_l[12]) err("EMR: DQS#/RDQS/outputs not default");
                        if (a_l[9:7] != 3'b000 && a_l[9:7] != 3'b111) err("EMR: OCD bits");
                        emr_done = 1'b1;
                        if (init_step == 3) init_step = 4;
                        rl = cl + al; wl = rl - 1;
                    end
                    3'd2, 3'd3: begin
                        if (a_l != 13'd0) err("EMR2/3 not zero");
                        if (init_step == 1 || init_step == 2) init_step = init_step + 1;
                    end
                    default: err("MRS bank address");
                    endcase
                end
                3'b001: begin                                       // REFRESH
                    for (b = 0; b < 8; b = b + 1) begin
                        if (b_open[b]) err("REFRESH with a bank open");
                        if ($time < b_ready[b]) err("REFRESH inside tRP/tRC");
                    end
                    if ($time < t_pall_done) err("REFRESH inside tRP");
                    if (t_last_ref != 0 && ($time - t_last_ref) < tRFC) err("tRFC");
                    if (init_done && t_last_ref != 0) begin
                        refs_live = refs_live + 1;
                        if (refs_live > 2 && ($time - t_last_ref) > max_ref_gap) max_ref_gap = $time - t_last_ref;
                        if (($time - t_last_ref) > 9 * tREFI) err("refresh interval over 9 x tREFI");
                    end
                    t_last_ref = $time;
                    n_ref = n_ref + 1;
                    if (init_step == 6 || init_step == 7) init_step = init_step + 1;
                end
                3'b010: begin                                       // PRECHARGE
                    if (a_l[10]) begin
                        for (b = 0; b < 8; b = b + 1) begin
                            if (b_open[b] && $time < b_pre_ok[b]) err("PRECHARGE inside tRAS");
                            b_open[b] = 1'b0;
                        end
                        t_pall_done = $time + tRP + TCK;
                        for (b = 0; b < 8; b = b + 1) if (b_ready[b] < t_pall_done) b_ready[b] = t_pall_done;
                        if (init_step == 0 || init_step == 5) init_step = init_step + 1;
                    end else begin
                        if (b_open[ba_l] && $time < b_pre_ok[ba_l]) err("PRECHARGE inside tRAS");
                        b_open[ba_l] = 1'b0;
                        if (b_ready[ba_l] < $time + tRP) b_ready[ba_l] = $time + tRP;
                    end
                end
                3'b011: begin                                       // ACTIVE
                    if (init_step < 9) err("ACTIVE before the init sequence is complete");
                    else init_done = 1'b1;
                    if (b_open[ba_l]) err("ACTIVE on an open bank");
                    if ($time < b_ready[ba_l]) err("ACTIVE inside tRP/tRC");
                    if (t_last_act != 0 && ($time - t_last_act) < tRRD) err("tRRD");
                    if (act_hist[0] != 0 && ($time - act_hist[0]) < tFAW) err("tFAW");
                    if (t_last_ref != 0 && ($time - t_last_ref) < tRFC) err("ACTIVE inside tRFC");
                    if (t_last_mrs != 0 && ($time - t_last_mrs) < 2 * TCK) err("ACTIVE inside tMRD");
                    b_open[ba_l] = 1'b1; b_row[ba_l] = a_l; b_act[ba_l] = $time;
                    b_pre_ok[ba_l] = $time + tRAS;
                    if (b_ready[ba_l] < $time + tRC) b_ready[ba_l] = $time + tRC;
                    t_last_act = $time;
                    act_hist[0] = act_hist[1]; act_hist[1] = act_hist[2]; act_hist[2] = act_hist[3]; act_hist[3] = $time;
                    n_act = n_act + 1;
                end
                3'b101, 3'b100: begin                               // READ / WRITE
                    ap = a_l[10];
                    if (!init_done) err("READ/WRITE before init");
                    if (!b_open[ba_l]) err("READ/WRITE to a closed bank");
                    t_int = $time + al * TCK;
                    if (t_int < b_act[ba_l] + tRCD) err("tRCD");
                    ix = index(b_row[ba_l], ba_l, a_l[9:0]);
                    if (cmd == 3'b101) begin
                        n_rd = n_rd + 1;
                        if ($time - t_dll_reset < 200 * TCK) err("READ inside tDLLK (200 clocks)");
                        if (wr_seen && t_int < t_wr_end + tWTR) err("tWTR (write to read)");
                        if (wr_seen && $time < t_last_wr + (wl + 4) * TCK) err("read while write data is in");
                        if (rd_seen && $time < t_last_rd + 4 * TCK) err("read to read inside the burst");
                        t_last_rd = $time; rd_seen = 1'b1;
                        w = memv[ix] ? mem[ix] : fresh(ix);
                        // first edge at hc + 2 RL; DQS low from one clock before to half a clock after
                        for (k = 0; k < 8; k = k + 1) begin
                            s_beat[(hc + 2 * rl + k) % 64] = 1'b1;
                            s_drv[(hc + 2 * rl + k) % 64]  = 1'b1;
                            s_data[(hc + 2 * rl + k) % 64] = w[16 * k +: 16];
                        end
                        s_drv[(hc + 2 * rl - 2) % 64] = 1'b1;
                        s_drv[(hc + 2 * rl - 1) % 64] = 1'b1;
                        s_drv[(hc + 2 * rl + 8) % 64] = 1'b1;
                        pre_start = $time + (al + 2 + 2) * TCK;              // AL + BL/2 - 2 + max(tRTP, 2 clocks)
                    end else begin
                        n_wr = n_wr + 1;
                        if (rd_seen && $time < t_last_rd + 6 * TCK) err("read to write turn-around (RL + BL/2 + 1 - WL)");
                        if (wr_seen && $time < t_last_wr + 4 * TCK) err("write to write inside the burst");
                        t_last_wr = $time; wr_seen = 1'b1;
                        t_wr_end = $time + (wl + 4) * TCK;
                        slot = -1;
                        for (k = 3; k >= 0; k = k - 1) if (!wb_v[k]) slot = k;
                        if (slot < 0) err("too many writes in flight");
                        else begin
                            wb_v[slot] = 1'b1; wb_h[slot] = hc + 2 * wl;
                            wb_row[slot] = b_row[ba_l]; wb_bank[slot] = ba_l; wb_col[slot] = a_l[9:0];
                            wb_beat0[slot] = 0; wb_beat1[slot] = 0;
                            if (!memv[ix]) begin mem[ix] = fresh(ix); memv[ix] = 1'b1; end
                        end
                        pre_start = t_wr_end + tWR;
                    end
                    if (pre_start < b_act[ba_l] + tRAS) pre_start = b_act[ba_l] + tRAS;
                    if (ap) begin
                        b_open[ba_l] = 1'b0;
                        if (b_ready[ba_l] < pre_start + tRP) b_ready[ba_l] = pre_start + tRP;
                    end else if (b_pre_ok[ba_l] < pre_start) b_pre_ok[ba_l] = pre_start;
                end
                default: err("unsupported command");
                endcase
            end
        end
    end

    // ---- refresh watchdog: nothing after init may go over 9 x tREFI without a refresh ----
    always @(posedge ck) if (init_done && t_last_ref != 0 && ($time - t_last_ref) > 9 * tREFI + 2 * TCK) begin
        err("no refresh for 9 x tREFI"); t_last_ref = $time;
    end

    // ---- write data: DQ changes (hold against the last DQS edge) and DQS edges per lane ----
    time dq_chg [0:15];
    time dqs_edge_t [0:1];
    reg  dq_hold_active [0:1];
    reg  [15:0] dq_prev = 16'd0;
    integer ln;
    initial begin
        for (ln = 0; ln < 16; ln = ln + 1) dq_chg[ln] = 0;
        dqs_edge_t[0] = 0; dqs_edge_t[1] = 0; dq_hold_active[0] = 1'b0; dq_hold_active[1] = 1'b0;
    end
    always @(dq_in) begin : dq_watch
        integer b;
        for (b = 0; b < 16; b = b + 1)
            if (dq_in[b] !== dq_prev[b]) begin
                if (dq_hold_active[b / 8] && ($time - dqs_edge_t[b / 8]) < tDH && $time != dqs_edge_t[b / 8])
                    err("DQ hold (tDH) after DQS");
                dq_chg[b] = $time;
            end
        dq_prev = dq_in;
    end

    reg lvl_prev [0:1];
    initial begin lvl_prev[0] = 1'bx; lvl_prev[1] = 1'bx; end
    genvar gl;
    generate for (gl = 0; gl < 2; gl = gl + 1) begin : lane_g
        always @(dqs_in[gl]) begin : dqs_edge
            reg [127:0] m;
            integer j, s, bt;
            reg [MEMBITS-1:0] ixw;
            time exp_t, dt;
            if ((dqs_in[gl] === 1'b0 || dqs_in[gl] === 1'b1) && (lvl_prev[gl] === 1'b0 || lvl_prev[gl] === 1'b1)) begin
                // the oldest write that still wants a beat on this lane; edges long before its
                // first one are the read strobe coming back through the board
                s = -1;
                for (j = 0; j < 4; j = j + 1)
                    if (wb_v[j] && (gl == 0 ? wb_beat0[j] : wb_beat1[j]) < 8 && (s < 0 || wb_h[j] < wb_h[s])) s = j;
                if (s >= 0) begin
                    bt = (gl == 0) ? wb_beat0[s] : wb_beat1[s];
                    exp_t = hc_time[(wb_h[s] + bt) % 64];
                    if ($time + TCK / 2 >= exp_t) begin
                        dt = ($time > exp_t) ? $time - exp_t : exp_t - $time;
                        if (dt > TCK / 4) err("tDQSS");
                        if ((bt % 2 == 0) != (dqs_in[gl] === 1'b1)) err("DQS polarity");
                        for (j = 0; j < 8; j = j + 1)
                            if (($time - dq_chg[8 * gl + j]) < tDS) err("DQ setup (tDS) before DQS");
                        if ((^dq_in[8 * gl +: 8]) === 1'bx) err("DQ unknown at a DQS edge");
                        ixw = index(wb_row[s], wb_bank[s], wb_col[s]);
                        if (dm[gl] === 1'b0) begin
                            m = mem[ixw];
                            m[16 * bt + 8 * gl +: 8] = dq_in[8 * gl +: 8];
                            mem[ixw] = m;
                        end
                        dqs_edge_t[gl] = $time; dq_hold_active[gl] = 1'b1;
                        if (gl == 0) wb_beat0[s] = bt + 1; else wb_beat1[s] = bt + 1;
                        if (wb_beat0[s] >= 8 && wb_beat1[s] >= 8) wb_v[s] = 1'b0;
                    end
                end
            end
            lvl_prev[gl] = dqs_in[gl];
        end
    end endgenerate
endmodule

// The board: flight times between the FPGA's pins and the DRAM, and a contention check
// (fpga_oe_dq/_dqs: the FPGA's drivers are on, taken from the bench).
//   WR_DLY: FPGA -> DRAM (CK, command, DQ, DQS, DM); RD_DLY: DRAM -> FPGA (DQ, DQS).
module ddr2_board #(
    parameter integer WR_DLY  = 500,
    parameter integer RD_DLY  = 500,
    parameter integer TCK     = 6668,
    parameter integer DQSCK   = 200,
    parameter integer T_PWRUP = 200000000,
    parameter integer MEMBITS = 20,
    parameter integer QUIET   = 0
) (
    input  wire [12:0] ddr2_addr,
    input  wire [2:0]  ddr2_ba,
    input  wire        ddr2_ras_n, ddr2_cas_n, ddr2_we_n,
    input  wire [0:0]  ddr2_ck_p, ddr2_ck_n, ddr2_cke, ddr2_cs_n, ddr2_odt,
    input  wire [1:0]  ddr2_dm,
    inout  wire [15:0] ddr2_dq,
    inout  wire [1:0]  ddr2_dqs_p,
    inout  wire [1:0]  ddr2_dqs_n,
    input  wire        fpga_oe_dq,
    input  wire        fpga_oe_dqs
);
    // flight times are transport delays (a pulse shorter than the delay must not vanish)
    wire ck_d, cke_d, cs_d, ras_d, cas_d, we_d, odt_d;
    wire [2:0] ba_d; wire [12:0] a_d; wire [1:0] dm_d;
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_ck  (.i(ddr2_ck_p[0]),  .o(ck_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_cke (.i(ddr2_cke[0]),   .o(cke_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_cs  (.i(ddr2_cs_n[0]),  .o(cs_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_ras (.i(ddr2_ras_n),    .o(ras_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_cas (.i(ddr2_cas_n),    .o(cas_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_we  (.i(ddr2_we_n),     .o(we_d));
    ddr2_flight #(.W(1),  .DLY(WR_DLY)) f_odt (.i(ddr2_odt[0]),   .o(odt_d));
    ddr2_flight #(.W(3),  .DLY(WR_DLY)) f_ba  (.i(ddr2_ba),       .o(ba_d));
    ddr2_flight #(.W(13), .DLY(WR_DLY)) f_a   (.i(ddr2_addr),     .o(a_d));
    ddr2_flight #(.W(2),  .DLY(WR_DLY)) f_dm  (.i(ddr2_dm),       .o(dm_d));

    wire [15:0] dq_to_dram;
    wire [1:0]  dqs_to_dram;
    ddr2_flight #(.W(16), .DLY(WR_DLY)) f_dq  (.i(ddr2_dq),       .o(dq_to_dram));
    ddr2_flight #(.W(2),  .DLY(WR_DLY)) f_dqs (.i(ddr2_dqs_p),    .o(dqs_to_dram));

    wire [15:0] dq_from; wire [1:0] dqs_from, dqsn_from;
    ddr2_model #(.TCK(TCK), .DQSCK(DQSCK), .T_PWRUP(T_PWRUP), .MEMBITS(MEMBITS), .QUIET(QUIET)) dram (
        .ck(ck_d), .cke(cke_d), .cs_n(cs_d), .ras_n(ras_d), .cas_n(cas_d), .we_n(we_d),
        .ba(ba_d), .addr(a_d), .dm(dm_d), .odt(odt_d),
        .dq_in(dq_to_dram), .dqs_in(dqs_to_dram),
        .dq_out(dq_from), .dqs_out(dqs_from), .dqs_n_out(dqsn_from)
    );
    wire [15:0] dq_arrived; wire [1:0] dqs_arrived, dqsn_arrived;
    ddr2_flight #(.W(16), .DLY(RD_DLY)) f_dqr  (.i(dq_from),   .o(dq_arrived));
    ddr2_flight #(.W(2),  .DLY(RD_DLY)) f_dqsr (.i(dqs_from),  .o(dqs_arrived));
    ddr2_flight #(.W(2),  .DLY(RD_DLY)) f_dqsn (.i(dqsn_from), .o(dqsn_arrived));
    assign ddr2_dq    = dq_arrived;
    assign ddr2_dqs_p = dqs_arrived;
    assign ddr2_dqs_n = dqsn_arrived;

    // contention at either end: the DRAM drives while the FPGA does
    wire fpga_oe_dq_at_dram;
    ddr2_flight #(.W(1), .DLY(WR_DLY)) f_oe (.i(fpga_oe_dq), .o(fpga_oe_dq_at_dram));
    integer contention = 0;
    always @(dq_arrived or fpga_oe_dq)
        if (fpga_oe_dq && dq_arrived !== 16'hzzzz) begin
            contention = contention + 1;
            if (contention < 5) $display("BOARD: DQ contention at the FPGA @%0t", $time);
        end
    always @(dq_from or fpga_oe_dq_at_dram)
        if (fpga_oe_dq_at_dram && dq_from !== 16'hzzzz) begin
            contention = contention + 1;
            if (contention < 5) $display("BOARD: DQ contention at the DRAM @%0t", $time);
        end
endmodule

// a transport delay (also high-Z passes: the net's driver is a reg here)
module ddr2_flight #(parameter integer W = 1, parameter integer DLY = 0) (
    input  wire [W-1:0] i,
    output wire [W-1:0] o
);
    reg [W-1:0] r;
    initial r = {W{1'bz}};
    always @(i) r <= #(DLY) i;
    assign o = r;
endmodule
