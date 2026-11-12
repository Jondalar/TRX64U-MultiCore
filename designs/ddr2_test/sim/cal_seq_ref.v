// FROZEN REFERENCE for tb_cal_equiv.v: host/cal_seq.v as it ran on the board in ddr2_test (OPT-ddr2 §8,
// before the shift-register rewrite of §9), module renamed. Do not edit; not part of any build.
// cal_seq: DDR2 init and read calibration through ddr2_lean's register block, without a CPU
// (test design only; the c64_basic boot ROM will run the same steps in software).
//
//  1. init (JEDEC order, Gideon's calibrator): CK on, CKE, 200 us, PALL, EMR2, EMR3, EMR (DLL on,
//     AL1, RTT 150, reduced drive), MR (BL8, CL3, WR3, DLL reset), PALL, REF, REF, MR (DLL reset
//     off), 400 cycles, refresh on.
//  2. calibration, Gideon's search (ddr2_calibrator_u64ii.c) on our primitives: for every bitslip
//     position (0..7) sweep the IDELAY
//     tap 0..31 (DQS and the lane's DQ move together), do a calibration read at each tap, and
//     test the captured DQS words: DQS is read as data and must show the burst's pattern at the
//     beat samples (odd samples: 1,0,1,0 over two words, preceded by a word whose last sample is
//     low). A hit at word index a (1..5) at a tap says: read delay a, this tap is a good
//     place. For each (lane, a) the longest run of hit taps is the window; the tap is chosen in
//     its middle (its edges when the window is cut by the scan range: Gideon's rule with a
//     window of about 34 taps expected), the first a where both lanes have a window wins.
//     All eight bitslip positions are scanned (one common BITSLIP pulse for both lanes between
//     them) and the position whose tap is farthest from the ends of the range wins, the
//     earlier one on a tie: windows are cut by the scan range (32 taps = 2.5 ns, the window
//     about 2.7 ns) and neighbouring positions are 1.67 ns apart. Eight pulses are a full
//     cycle, so the chosen position is reached by (position + 1) more pulses. No window at any
//     position: fail.
module cal_seq_ref (
    input  wire        clk,
    input  wire        rst,
    output reg         r_we = 1'b0,
    output reg  [3:0]  r_addr = 4'd0,
    output reg  [31:0] r_wdata = 32'd0,
    input  wire [31:0] r_rdata,

    output reg         init_done = 1'b0,
    output reg         cal_done = 1'b0,
    output reg         cal_ok = 1'b0,
    output reg  [4:0]  res_tap0 = 5'd0,
    output reg  [4:0]  res_tap1 = 5'd0,
    output reg  [2:0]  res_dly = 3'd0,
    output reg  [3:0]  res_bs = 4'd0,
    output reg  [5:0]  res_w0 = 6'd0,
    output reg  [5:0]  res_w1 = 6'd0,
    output reg         res_tr0 = 1'b0,
    output reg         res_tr1 = 1'b0,
    output reg  [3:0]  att_hits = 4'd0       // attempts that found a window for lane 0 (debug)
);
    // ---- init program: {register, data, wait cycles} ----
    localparam integer N_INIT = 12;
    function [51:0] prog(input [4:0] s);
        case (s)
        5'd0:  prog = {4'd1, 32'h0000_0001, 16'd2000};                                  // CK on, 27 us
        5'd1:  prog = {4'd1, 32'h0000_0009, 16'd15000};                                 // CKE, 200 us
        5'd2:  prog = {4'd2, {13'd0, 3'b010, 3'd0, 13'h0400}, 16'd16};                  // PRECHARGE all
        5'd3:  prog = {4'd2, {13'd0, 3'b000, 3'd2, 13'h0000}, 16'd16};                  // EMR2
        5'd4:  prog = {4'd2, {13'd0, 3'b000, 3'd3, 13'h0000}, 16'd16};                  // EMR3
        5'd5:  prog = {4'd2, {13'd0, 3'b000, 3'd1, 13'h004A}, 16'd16};                  // EMR: DLL on, AL1, RTT 150, reduced
        5'd6:  prog = {4'd2, {13'd0, 3'b000, 3'd0, 13'h0533}, 16'd16};                  // MR: BL8 CL3 WR3, DLL reset
        5'd7:  prog = {4'd2, {13'd0, 3'b010, 3'd0, 13'h0400}, 16'd16};                  // PRECHARGE all
        5'd8:  prog = {4'd2, {13'd0, 3'b001, 3'd0, 13'h0000}, 16'd16};                  // REFRESH
        5'd9:  prog = {4'd2, {13'd0, 3'b001, 3'd0, 13'h0000}, 16'd16};                  // REFRESH
        5'd10: prog = {4'd2, {13'd0, 3'b000, 3'd0, 13'h0433}, 16'd400};                 // MR: DLL reset off, then 200 clocks
        5'd11: prog = {4'd1, 32'h0000_000D, 16'd8};                                     // CK, CKE, refresh on
        default: prog = {4'd0, 32'd0, 16'd0};
        endcase
    endfunction
    reg [4:0]  step = 5'd0;
    wire [51:0] pg = prog(step);

    // ---- sweep data ----
    reg [31:0] mk [0:9];                         // hits per (lane, a-1) over the taps
    reg [63:0] raw0, raw1;
    reg [4:0]  tap = 5'd0;
    reg [3:0]  att = 4'd0;
    reg [5:0]  wcnt = 6'd0;
    reg [15:0] wait_cnt = 16'd0;
    reg [4:0]  poll_n = 5'd0;

    // evaluation of one (lane, a)
    reg [3:0]  ev = 4'd0;                        // 0..9
    reg [5:0]  ev_i = 6'd0;
    reg [5:0]  cur_start = 6'd0, cur_len = 6'd0, best_start = 6'd0, best_len = 6'd1;
    reg [9:0]  okv = 10'd0;
    reg [4:0]  chv [0:9];
    reg [5:0]  wdv [0:9];
    reg [9:0]  trv = 10'd0;
    // the first position's result while the next one is looked at
    reg        have = 1'b0;
    reg [4:0]  k_tap0 = 5'd0, k_tap1 = 5'd0;
    reg [2:0]  k_dly = 3'd0;
    reg [3:0]  k_bs = 4'd0;
    reg [5:0]  k_w0 = 6'd0, k_w1 = 6'd0;
    reg        k_tr0 = 1'b0, k_tr1 = 1'b0;
    reg [4:0]  k_sc = 5'd0, c_sc = 5'd0;
    reg [2:0]  unslip = 3'd0;

    function [4:0] m5(input [63:0] raw);          // hit bit per a = 1..5 (bit a-1)
        integer a;
        reg [7:0] w_prev, w_a, w_n;
        begin
            m5 = 5'd0;
            for (a = 1; a <= 5; a = a + 1) begin
                w_prev = raw[8 * (a - 1) +: 8];
                w_a    = raw[8 * a +: 8];
                w_n    = raw[8 * (a + 1) +: 8];
                m5[a - 1] = ((w_a & 8'hAA) == 8'h22) && ((w_n & 8'hAA) == 8'h22) && ((w_prev & 8'h80) == 8'h00);
            end
        end
    endfunction

    function [4:0] sc_of(input [4:0] t0, input [4:0] t1);
        reg [4:0] d0, d1;
        begin
            d0 = (t0 < 5'd16) ? t0 : (5'd31 - t0);
            d1 = (t1 < 5'd16) ? t1 : (5'd31 - t1);
            sc_of = (d0 < d1) ? d0 : d1;
        end
    endfunction

    // a = 1..5 where both lanes have a window
    reg [2:0] pick_a;
    reg       pick_ok;
    integer   pa;
    always @(*) begin
        pick_ok = 1'b0; pick_a = 3'd0;
        for (pa = 5; pa >= 1; pa = pa - 1)
            if (okv[pa - 1] && okv[5 + pa - 1]) begin pick_ok = 1'b1; pick_a = pa[2:0]; end
    end

    localparam S_RST = 5'd0, S_IWR = 5'd1, S_WAIT = 5'd2, S_INEXT = 5'd3,
               S_TAP = 5'd4, S_CALRD = 5'd5, S_POLL = 5'd6, S_POLL2 = 5'd7,
               S_RB0 = 5'd8, S_RB1 = 5'd9, S_RB2 = 5'd10, S_RB3 = 5'd11, S_RB4 = 5'd12, S_MATCH = 5'd13,
               S_EV0 = 5'd14, S_EV1 = 5'd15, S_PICK = 5'd16, S_APPLY0 = 5'd17, S_APPLY1 = 5'd18,
               S_APPLY2 = 5'd19, S_APPLY3 = 5'd20, S_SLIP = 5'd21, S_SLIP2 = 5'd22, S_FAIL = 5'd23,
               S_DONE = 5'd24, S_UNSLIP = 5'd25, S_FIN = 5'd26;
    reg [4:0] st = S_RST, ret = S_RST;

    reg [4:0]  c_tmp;
    reg [5:0]  f, l, bl_, bs_;
    reg [4:0]  cho;
    reg        ok_tmp, tr_tmp, bit_tmp;
    reg [5:0]  nl;
    integer    k;

    always @(posedge clk) begin
        r_we <= 1'b0;
        if (rst) begin
            st <= S_RST; init_done <= 1'b0; cal_done <= 1'b0; cal_ok <= 1'b0; step <= 5'd0;
            wait_cnt <= 16'd64;
        end else case (st)
        S_RST: begin
            if (wait_cnt == 16'd0) begin st <= S_IWR; end else wait_cnt <= wait_cnt - 16'd1;
        end
        S_IWR: begin
            if (step == N_INIT[4:0]) begin
                init_done <= 1'b1;
                tap <= 5'd0; att <= 4'd0; have <= 1'b0;
                st <= S_TAP;
            end else begin
                r_we <= 1'b1; r_addr <= pg[51:48]; r_wdata <= pg[47:16];
                wait_cnt <= pg[15:0]; ret <= S_INEXT; st <= S_WAIT;
            end
        end
        S_WAIT: if (wait_cnt == 16'd0) st <= ret; else wait_cnt <= wait_cnt - 16'd1;
        S_INEXT: begin step <= step + 5'd1; st <= S_IWR; end

        // ---- sweep ----
        S_TAP: begin
            r_we <= 1'b1; r_addr <= 4'd3; r_wdata <= {19'd0, tap, 3'd0, tap};
            wait_cnt <= 16'd6; ret <= S_CALRD; st <= S_WAIT;
        end
        S_CALRD: begin
            r_we <= 1'b1; r_addr <= 4'd10; r_wdata <= 32'd1;
            wait_cnt <= 16'd10; ret <= S_POLL; st <= S_WAIT; poll_n <= 5'd0;
        end
        S_POLL: begin
            r_addr <= 4'd0; st <= S_POLL2;
        end
        S_POLL2: begin                                              // r_rdata = status of the cycle before
            if (r_rdata[4]) st <= S_RB0;
            else if (poll_n == 5'd31) st <= S_FAIL;                // never captured
            else begin poll_n <= poll_n + 5'd1; wait_cnt <= 16'd3; ret <= S_POLL; st <= S_WAIT; end
        end
        S_RB0: begin r_addr <= 4'd6; st <= S_RB1; end
        S_RB1: begin raw0[31:0] <= r_rdata; r_addr <= 4'd7; st <= S_RB2; end
        S_RB2: begin raw0[63:32] <= r_rdata; r_addr <= 4'd8; st <= S_RB3; end
        S_RB3: begin raw1[31:0] <= r_rdata; r_addr <= 4'd9; st <= S_RB4; end
        S_RB4: begin raw1[63:32] <= r_rdata; st <= S_MATCH; end
        S_MATCH: begin
            c_tmp = m5(raw0);
            for (k = 0; k < 5; k = k + 1) mk[k][tap] <= c_tmp[k];
            c_tmp = m5(raw1);
            for (k = 0; k < 5; k = k + 1) mk[5 + k][tap] <= c_tmp[k];
            if (tap == 5'd31) begin ev <= 4'd0; ev_i <= 6'd0; best_len <= 6'd1; cur_len <= 6'd0; st <= S_EV0; end
            else begin tap <= tap + 5'd1; st <= S_TAP; end
        end

        // ---- longest run of hits per (lane, a), Gideon's choice ----
        S_EV0: begin
            bit_tmp = mk[ev][ev_i[4:0]];
            if (bit_tmp) begin
                nl = cur_len + 6'd1;
                if (cur_len == 6'd0) cur_start <= ev_i;
                cur_len <= nl;
                if (nl > best_len) begin
                    best_len <= nl;
                    best_start <= (cur_len == 6'd0) ? ev_i : cur_start;
                end
            end else cur_len <= 6'd0;
            if (ev_i == 6'd31) st <= S_EV1; else ev_i <= ev_i + 6'd1;
        end
        S_EV1: begin
            f = best_start; bl_ = best_len; l = best_start + best_len - 6'd1;
            ok_tmp = 1'b1; cho = 5'd0;
            tr_tmp = (f == 6'd0) || (l == 6'd31);
            if (bl_ < 6'd2) ok_tmp = 1'b0;
            else if (f == 6'd0 && l == 6'd31) cho = 5'd15;
            else if (f == 6'd0) begin
                if (l < 6'd10) ok_tmp = 1'b0;
                else cho = (l > 6'd17) ? (l - 6'd17) : 5'd0;
            end else if (l == 6'd31) begin
                if (f > 6'd21) ok_tmp = 1'b0;
                else cho = (f + 6'd17 > 6'd31) ? 5'd31 : (f + 6'd17);
            end else if (bl_ < 6'd12) ok_tmp = 1'b0;
            else cho = (f + l) >> 1;
            okv[ev] <= ok_tmp; chv[ev] <= cho; wdv[ev] <= ok_tmp ? bl_ : 6'd0; trv[ev] <= tr_tmp;
            if (ev == 4'd9) st <= S_PICK;
            else begin ev <= ev + 4'd1; ev_i <= 6'd0; best_len <= 6'd1; cur_len <= 6'd0; st <= S_EV0; end
        end
        S_PICK: begin
            if (okv != 10'd0) att_hits <= att_hits + 4'd1;
            // distance of the chosen taps from the ends of the range: the best position of the eight wins
            c_sc = sc_of(chv[pick_a - 3'd1], chv[5 + pick_a - 3'd1]);
            if (pick_ok && (!have || c_sc > k_sc)) begin
                have <= 1'b1;
                k_tap0 <= chv[pick_a - 3'd1]; k_tap1 <= chv[5 + pick_a - 3'd1]; k_dly <= pick_a; k_bs <= att;
                k_w0 <= wdv[pick_a - 3'd1]; k_w1 <= wdv[5 + pick_a - 3'd1];
                k_tr0 <= trv[pick_a - 3'd1]; k_tr1 <= trv[5 + pick_a - 3'd1]; k_sc <= c_sc;
            end
            if (att == 4'd7) st <= S_FIN; else st <= S_SLIP;
        end
        S_FIN: begin
            if (!have) st <= S_FAIL;
            else begin
                res_tap0 <= k_tap0; res_tap1 <= k_tap1; res_dly <= k_dly; res_bs <= k_bs;
                res_w0 <= k_w0; res_w1 <= k_w1; res_tr0 <= k_tr0; res_tr1 <= k_tr1;
                unslip <= k_bs[2:0] + 3'd1;                       // eight pulses are a full cycle: on to the chosen position
                st <= S_UNSLIP;
            end
        end
        S_UNSLIP: begin
            if (unslip == 3'd0) st <= S_APPLY0;
            else begin
                r_we <= 1'b1; r_addr <= 4'd5; r_wdata <= 32'd3; unslip <= unslip - 3'd1;
                wait_cnt <= 16'd32; ret <= S_UNSLIP; st <= S_WAIT;
            end
        end
        S_APPLY0: begin
            r_we <= 1'b1; r_addr <= 4'd3; r_wdata <= {19'd0, res_tap1, 3'd0, res_tap0};
            wait_cnt <= 16'd6; ret <= S_APPLY1; st <= S_WAIT;
        end
        S_APPLY1: begin
            r_we <= 1'b1; r_addr <= 4'd4; r_wdata <= {25'd0, res_dly, 1'b0, res_dly};
            wait_cnt <= 16'd6; ret <= S_APPLY2; st <= S_WAIT;
        end
        S_APPLY2: begin
            r_we <= 1'b1; r_addr <= 4'd1; r_wdata <= 32'h0000_001D;       // calibration done
            wait_cnt <= 16'd6; ret <= S_APPLY3; st <= S_WAIT;
        end
        S_APPLY3: begin cal_ok <= 1'b1; cal_done <= 1'b1; st <= S_DONE; end
        S_SLIP: begin
            r_we <= 1'b1; r_addr <= 4'd5; r_wdata <= 32'd3;
            att <= att + 4'd1; tap <= 5'd0;
            wait_cnt <= 16'd32; ret <= S_TAP; st <= S_WAIT;
        end
        S_FAIL: begin cal_done <= 1'b1; cal_ok <= 1'b0; res_bs <= att; st <= S_DONE; end
        S_DONE: ;
        default: st <= S_RST;
        endcase
    end
endmodule
