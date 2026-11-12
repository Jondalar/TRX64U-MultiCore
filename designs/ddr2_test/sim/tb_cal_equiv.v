// cal_seq (host/cal_seq.v, shift-register storage of the hits and results) against the frozen
// reference cal_seq_ref.v (the version that ran 4 hours on the board): the same register block model
// feeds both, every output and every register write of the two is compared on every clock, over
// RUNS random calibrations. Each run draws, per bitslip position and lane, a window of hit taps (start,
// length 0..40, so cut by the 32-tap range on either side, too short, absent), a few dropouts inside the
// window, spurious hits at other read delays `a`, and random DQS words everywhere else. The model follows
// the reference's writes (taps, bitslip pulses, calibration reads) and answers the reads of ddr2_lean's
// register block that cal_seq makes (status bit 4, the four DQS capture words).
//   iverilog -g2012 -o tb.vvp tb_cal_equiv.v cal_seq_ref.v ../../../host/cal_seq.v && vvp tb.vvp
`timescale 1ns/1ps
module tb #(parameter integer RUNS = 240, parameter integer SEED = 1);
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst = 1;

    wire        r_we_a, r_we_b;
    wire [3:0]  r_addr_a, r_addr_b;
    wire [31:0] r_wdata_a, r_wdata_b;
    reg  [31:0] r_rdata;
    wire        id_a, cd_a, co_a, id_b, cd_b, co_b, tr0_a, tr1_a, tr0_b, tr1_b;
    wire [4:0]  t0_a, t1_a, t0_b, t1_b;
    wire [2:0]  dly_a, dly_b;
    wire [3:0]  bs_a, bs_b, ah_a, ah_b;
    wire [5:0]  w0_a, w1_a, w0_b, w1_b;
    cal_seq_ref ref_ (.clk(clk), .rst(rst), .r_we(r_we_a), .r_addr(r_addr_a), .r_wdata(r_wdata_a), .r_rdata(r_rdata),
        .init_done(id_a), .cal_done(cd_a), .cal_ok(co_a), .res_tap0(t0_a), .res_tap1(t1_a), .res_dly(dly_a),
        .res_bs(bs_a), .res_w0(w0_a), .res_w1(w1_a), .res_tr0(tr0_a), .res_tr1(tr1_a), .att_hits(ah_a));
    cal_seq dut (.clk(clk), .rst(rst), .r_we(r_we_b), .r_addr(r_addr_b), .r_wdata(r_wdata_b), .r_rdata(r_rdata),
        .init_done(id_b), .cal_done(cd_b), .cal_ok(co_b), .res_tap0(t0_b), .res_tap1(t1_b), .res_dly(dly_b),
        .res_bs(bs_b), .res_w0(w0_b), .res_w1(w1_b), .res_tr0(tr0_b), .res_tr1(tr1_b), .att_hits(ah_b));

    // ---- the register block model ----
    integer win_f [0:15], win_l [0:15], win_a [0:15];   // [pos * 2 + lane]
    integer spur_a [0:15], spur_f [0:15], spur_l [0:15];
    integer drop [0:15];                                  // tap with a dropout (-1 none)
    integer slips = 0, tap = 0, valid_cnt = 0;
    reg have_cap = 0;
    reg [63:0] raw0, raw1;
    integer seed = SEED;
    function integer rnd(input integer n);               // 0 .. n-1
        begin rnd = $unsigned($random(seed)) % n; end
    endfunction

    task make_raw(input integer lane, output [63:0] raw);
        integer pos, a, i, idx;
        reg [63:0] r;
        begin
            r = {$random(seed), $random(seed)};
            pos = slips % 8; idx = pos * 2 + lane;
            if (tap >= win_f[idx] && tap <= win_l[idx] && tap != drop[idx] && win_a[idx] > 0) begin
                a = win_a[idx];
                r[8 * (a - 1) + 7] = 1'b0;
                r[8 * a + 1] = 1'b1; r[8 * a + 3] = 1'b0; r[8 * a + 5] = 1'b1; r[8 * a + 7] = 1'b0;
                r[8 * (a + 1) + 1] = 1'b1; r[8 * (a + 1) + 3] = 1'b0; r[8 * (a + 1) + 5] = 1'b1; r[8 * (a + 1) + 7] = 1'b0;
            end
            if (tap >= spur_f[idx] && tap <= spur_l[idx] && spur_a[idx] > 0) begin
                a = spur_a[idx];
                r[8 * (a - 1) + 7] = 1'b0;
                r[8 * a + 1] = 1'b1; r[8 * a + 3] = 1'b0; r[8 * a + 5] = 1'b1; r[8 * a + 7] = 1'b0;
                r[8 * (a + 1) + 1] = 1'b1; r[8 * (a + 1) + 3] = 1'b0; r[8 * (a + 1) + 5] = 1'b1; r[8 * (a + 1) + 7] = 1'b0;
            end
            raw = r;
        end
    endtask

    always @(posedge clk) begin
        if (rst) begin slips <= 0; valid_cnt <= 0; end
        else begin
            if (r_we_a && r_addr_a == 4'd3) tap <= r_wdata_a[4:0];
            if (r_we_a && r_addr_a == 4'd5) slips <= slips + 1;
            if (r_we_a && r_addr_a == 4'd10) begin
                make_raw(0, raw0); make_raw(1, raw1);
                valid_cnt <= 3;
            end else if (valid_cnt != 0) valid_cnt <= valid_cnt - 1;
            case (r_addr_a)
            4'd0: r_rdata <= {27'd0, (valid_cnt == 0 && have_cap), 4'd0};
            4'd6: r_rdata <= raw0[31:0];
            4'd7: r_rdata <= raw0[63:32];
            4'd8: r_rdata <= raw1[31:0];
            4'd9: r_rdata <= raw1[63:32];
            default: r_rdata <= 32'd0;
            endcase
            if (r_we_a && r_addr_a == 4'd10) have_cap <= 1'b1;
        end
    end

    // ---- compare on every clock ----
    integer mism = 0, cmp = 0;
    wire [160:0] va = {r_we_a, r_addr_a, r_wdata_a, id_a, cd_a, co_a, t0_a, t1_a, dly_a, bs_a, w0_a, w1_a, tr0_a, tr1_a, ah_a};
    wire [160:0] vb = {r_we_b, r_addr_b, r_wdata_b, id_b, cd_b, co_b, t0_b, t1_b, dly_b, bs_b, w0_b, w1_b, tr0_b, tr1_b, ah_b};
    always @(negedge clk) if (!rst) begin
        cmp = cmp + 1;
        if (va !== vb) begin
            mism = mism + 1;
            if (mism < 5) $display("MISMATCH at %0t: ref %h dut %h", $time, va, vb);
        end
    end

    integer run, p, l, ok_runs = 0, fail_runs = 0, c, kind;
    initial begin
        for (run = 0; run < RUNS; run = run + 1) begin
            kind = run % 4;          // 0 clean windows, 1 short/cut/odd windows, 2 nothing, 3 very noisy
            for (p = 0; p < 8; p = p + 1) for (l = 0; l < 2; l = l + 1) begin
                win_a[p * 2 + l] = (kind == 2) ? 0 : (rnd(5) + 1);
                win_f[p * 2 + l] = rnd(44) - 12;
                win_l[p * 2 + l] = win_f[p * 2 + l] + (kind == 0 ? 10 + rnd(30) : rnd(41));
                drop[p * 2 + l]  = (kind == 3 || rnd(3) == 0) ? rnd(32) : -1;
                spur_a[p * 2 + l] = (kind == 3 || rnd(3) == 0) ? (rnd(5) + 1) : 0;
                spur_f[p * 2 + l] = rnd(32); spur_l[p * 2 + l] = spur_f[p * 2 + l] + rnd(30);
                // often the same window on both lanes (the case that calibrates)
                if (l == 1 && kind != 3 && (kind == 0 ? rnd(8) != 0 : rnd(2) == 0)) begin
                    win_a[p * 2 + 1] = win_a[p * 2]; win_f[p * 2 + 1] = win_f[p * 2] + rnd(5) - 2;
                    win_l[p * 2 + 1] = win_l[p * 2] + rnd(5) - 2;
                end
            end
            rst = 1; have_cap = 0; tap = 0;
            repeat (4) @(posedge clk);
            rst = 0;
            c = 0;
            while (!(cd_a && cd_b) && c < 400000) begin @(posedge clk); c = c + 1; end
            repeat (20) @(posedge clk);
            if (!(cd_a && cd_b)) begin $display("FAILED: run %0d did not end (ref %b dut %b)", run, cd_a, cd_b); mism = mism + 1; end
            if (co_a) ok_runs = ok_runs + 1; else fail_runs = fail_runs + 1;
            if (run < 6 || run % 20 == 0)
                $display("run %0d kind %0d: ok %b tap %0d/%0d dly %0d bs %0d w %0d/%0d hits %0d  (dut: ok %b tap %0d/%0d dly %0d bs %0d w %0d/%0d hits %0d)",
                    run, kind, co_a, t0_a, t1_a, dly_a, bs_a, w0_a, w1_a, ah_a, co_b, t0_b, t1_b, dly_b, bs_b, w0_b, w1_b, ah_b);
        end
        if (mism != 0 || ok_runs == 0 || fail_runs == 0) $display("FAILED: %0d mismatches, %0d ok runs, %0d failed runs", mism, ok_runs, fail_runs);
        else $display("PASS cal_equiv: %0d random calibrations (%0d ok, %0d without a window), %0d clocks compared, 0 mismatches", RUNS, ok_runs, fail_runs, cmp);
        $finish;
    end
endmodule
