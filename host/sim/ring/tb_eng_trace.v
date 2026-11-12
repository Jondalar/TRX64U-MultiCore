// trx_engine.v alone (iverilog, run_trace.sh): the trace former (T21 §4.11.1).
// A synthetic instruction stream: an IB (tr_ib with phi2_tick, as trx_dbg.v
// gives it) every 2 to 6 phi2 cycles (a phi2 cycle is 32 clocks), stream 0
// writes inside the instructions, the registers valid 5 clocks after the IB
// (the engine samples them at 12), b1/b2 changing 40 clocks after it. The record of
// instruction j is written at the IB of j+1: stream 3's words 3j .. 3j+2 in the
// DDR2 model must hold
//   [23:0] the phi2 count after the fetch edge, ws (stream 0 records before the
//   instruction), pc, write_count, A X Y SP P (the values given for j), opcode,
//   b1, b2 (the values the bench gave for j)
// and LOG_CNT[3] must count them. A flush between two instructions drops the
// pending one (and only that); trace off stops the records.
`timescale 1ns / 1ps
module tb_eng_trace;
    reg clk = 0, ui_clk = 0;
    always #15.859 clk = ~clk;              // 31.5 MHz
    always #6.667 ui_clk = ~ui_clk;         // 75 MHz

    reg         r_we = 0;
    reg   [9:0] r_addr = 0;
    reg  [31:0] r_wdata = 0;
    wire [31:0] r_rdata;
    reg         phi2_tick = 0;
    reg         tr_ib = 0;
    reg  [15:0] tr_pc = 0;
    reg   [7:0] tr_op = 0, tr_b1 = 0, tr_b2 = 0;
    reg  [39:0] tr_regs = 0;
    reg         wa_we = 0;
    reg  [15:0] wa_addr = 0;
    wire [26:0] e_addr;
    wire  [2:0] e_cmd;
    wire        e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;

    trx_engine eng (
        .clk(clk), .r_we(r_we), .r_re(1'b0), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata),
        .log_on(), .seq_busy(), .mk(1'b0), .mk_snap(1'b1), .mk_cap(1'b0), .mk_skip(1'b0),
        .mk_intg(1'b0), .reu_busy(1'b0), .mk_x(10'd100), .mk_line(9'd16), .phi2_tick(phi2_tick),
        .dbg_put(1'b0), .dbg_rec(64'd0), .cap_gen(16'h0001),
        .tr_ib(tr_ib), .tr_pc(tr_pc), .tr_op(tr_op), .tr_b1(tr_b1), .tr_b2(tr_b2), .tr_regs(tr_regs),
        .sb_req(), .sb_addr(), .sb_gnt(1'b1), .sb_rdata(32'd0),
        .wa_we(wa_we), .wa_addr(wa_addr), .wa_new(8'h55), .wa_old(8'hAA),
        .wb_we(1'b0), .wb_addr(16'd0), .wb_new(8'd0), .wb_old(8'd0),
        .wc_we(1'b0), .wc_addr(10'd0), .wc_new(4'd0), .wc_old(4'd0),
        .wd_we(1'b0), .wd_addr(10'd0), .wd_new(4'd0), .wd_old(4'd0),
        .wk_we(1'b0), .wk_addr(15'd0), .wk_new(8'd0), .cart_rbw(),
        .eng_tgl(), .gap_tgl(),
        .ui_clk(ui_clk), .ui_rst(1'b0), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(1'b1),
        .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .e_rd_data(128'd0), .e_rd_data_valid(1'b0),
        .ob_v(1'b0), .ob_addr(15'd0), .ob_old(8'd0)
    );

    // the drain's stream 3 words: 0x6800000-0x6FFFFFF
    reg  [63:0] s3 [0:2047];
    integer     n3 = 0;
    wire [26:0] byte_a = {e_addr[25:3], 4'd0};
    always @(posedge ui_clk)
        if (e_en && e_wdf_wren && e_cmd == 3'd0 && byte_a >= 27'h6800000 && byte_a < 27'h7000000) begin
            if (((byte_a - 27'h6800000) >> 3) + (e_wdf_mask[7:0] == 8'h00 ? 0 : 1) != n3) begin
                $display("FAIL: stream 3 word at byte %h half %0d, expected word %0d", byte_a,
                         e_wdf_mask[7:0] == 8'h00 ? 0 : 1, n3);
            end
            s3[n3] = e_wdf_mask[7:0] == 8'h00 ? e_wdf_data[63:0] : e_wdf_data[127:64];
            n3 = n3 + 1;
        end

    task wr(input [9:0] a, input [31:0] d);
        begin
            @(posedge clk) #1; r_we = 1; r_addr = a; r_wdata = d;
            @(posedge clk) #1; r_we = 0;
        end
    endtask

    // the bench's own bookkeeping, per instruction j
    localparam N = 60;
    integer tk = 0;                         // phi2 ticks the engine has seen
    always @(posedge clk) if (phi2_tick) tk <= tk + 1;
    reg  [23:0] x_cyc [0:N + 1];
    integer     x_ws [0:N + 1], x_wc [0:N + 1];
    reg  [15:0] x_pc [0:N + 1];
    reg   [7:0] x_op [0:N + 1], x_a [0:N + 1], x_x [0:N + 1], x_y [0:N + 1], x_s [0:N + 1],
                x_p [0:N + 1], x_b1 [0:N + 1], x_b2 [0:N + 1];
    reg         x_ok [0:N + 1];             // a record is expected
    integer     wtot = 0;                   // stream 0 records made so far
    integer j, c, k, fails = 0, nrec = 0, flush_at = 25;

    // one phi2 cycle: 32 clocks; an IB on the first one, then writes on some
    task cycle(input integer wr_now);
        begin
            @(posedge clk) #1; phi2_tick = 1; if (wr_now) begin wa_we = 1; wa_addr = 16'h0100 + wtot; end
            @(posedge clk) #1; phi2_tick = 0; wa_we = 0;
            if (wr_now) wtot = wtot + 1;
            repeat (30) @(posedge clk);
        end
    endtask

    integer len, w, e, nexp;
    reg [63:0] w0, w1, w2;
    initial begin
        #500;
        wr(10'h000, 32'h111);               // restart, on, trace
        repeat (10) @(posedge clk);
        for (j = 0; j <= N; j = j + 1) begin
            len = 2 + (j % 5);
            w   = j % 4;
            // the fetch cycle: the IB, tr_ib one clock with the tick
            @(posedge clk) #1; phi2_tick = 1; tr_ib = 1; tr_pc = 16'h0800 + 3 * j; tr_op = 8'hA0 + j;
            @(posedge clk) #1; phi2_tick = 0; tr_ib = 0;
            // expected (the cycle count includes the fetch edge)
            x_cyc[j] = tk[23:0]; x_ws[j] = wtot; x_pc[j] = 16'h0800 + 3 * j; x_op[j] = 8'hA0 + j;
            x_a[j] = j + 1; x_x[j] = j + 2; x_y[j] = j + 3; x_s[j] = 8'hF0 - j; x_p[j] = 8'h30 + j;
            x_b1[j] = 8'h11 + j; x_b2[j] = 8'h22 + j;
            x_wc[j] = w < len - 1 ? w : len - 1;
            repeat (3) @(posedge clk);
            tr_regs = {x_p[j], x_s[j], x_y[j], x_x[j], x_a[j]};
            repeat (36) @(posedge clk);
            tr_b1 = x_b1[j]; tr_b2 = x_b2[j];
            for (c = 1; c < len; c = c + 1) cycle(c <= w);
            if (j == flush_at) wr(10'h000, 32'h211);    // flush: the pending instruction goes
        end
        repeat (400) @(posedge ui_clk);
        r_addr = 10'h016;
        repeat (200) @(posedge clk);
        $display("LOG_CNT[3] = %0d, %0d words in the area", r_rdata[27:0], n3);

        nexp = 0;
        for (j = 0; j < N; j = j + 1) begin
            if (j == flush_at) begin
                // the one the flush dropped
            end else begin
                e = nexp * 3;
                nexp = nexp + 1;
                if (e + 2 >= n3) begin
                    fails = fails + 1; $display("FAIL: record for instruction %0d missing", j);
                end else begin
                    w0 = s3[e]; w1 = s3[e + 1]; w2 = s3[e + 2];
                    if (w0 !== {40'd0, x_cyc[j]} || w1 !== {x_wc[j][15:0], x_pc[j], 4'd0, x_ws[j][27:0]} ||
                        w2 !== {x_b2[j], x_b1[j], x_op[j], x_p[j], x_s[j], x_y[j], x_x[j], x_a[j]}) begin
                        fails = fails + 1;
                        $display("FAIL: instruction %0d: %h %h %h, expected %h %h %h", j, w0, w1, w2,
                                 {40'd0, x_cyc[j]}, {x_wc[j][15:0], x_pc[j], 4'd0, x_ws[j][27:0]},
                                 {x_b2[j], x_b1[j], x_op[j], x_p[j], x_s[j], x_y[j], x_x[j], x_a[j]});
                    end
                end
            end
        end
        if (n3 != nexp * 3) begin fails = fails + 1; $display("FAIL: %0d words, %0d expected", n3, nexp * 3); end
        if (r_rdata[27:0] != nexp) begin fails = fails + 1; $display("FAIL: LOG_CNT[3] %0d, expected %0d", r_rdata[27:0], nexp); end
        $display("%0d records checked, writes counted %0d", nexp, wtot);
        if (fails) $display("%0d FAILED", fails); else $display("PASS");
        $finish;
    end
endmodule
