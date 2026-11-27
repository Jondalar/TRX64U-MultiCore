// T175 / T260 co-check: runs a step list the APPLICATION compiled (cores/diag/app/tests/diag_co.cc capture, from a shipped example test) on the real probe engine v2 behind its register block
// (diag_regs.v and probe.v wired as tb_probe.v wires them, without the capture writer), the way the application drives it (ABORT, CLEAR, CAP RESET, STEP_ADDR = 0, the words, RUN, poll STATUS; at a wait: read
// PC, pop the FIFO, GO; at done: pop the rest, read REF_PC / REF_CNT), against a model of the peripherals the test asks the person to plug in, and writes down what the application will read.
// The application's decoder then reads that record (diag_co.cc replay). The TB judges nothing about the test; the two sides meet only through these files.
//   +steps=<hex>   word count, then the step words
//   +model=<hex>   phase count P, then per phase six words: the ten joystick pins from outside (bit n = table pin 50 + n, 1 = high, held low from outside when 0), the paddle comparator pins
//                  forced high (bit n = PADDLE_CMPn), then K0..K3, the clocks after PADDLE_DIS is no longer driven high until comparator n flips (0: never). The paddle model is
//                  PADDLE_DIS (pin 67, driven HIGH = the capacitors discharge: the count stays 0). Phase p holds from the p-th GO on (phase 0 until the first GO); the last phase stays.
//   +out=<hex>     STATUS byte, FIFO word count, the words, wait count, the PCs, per wait the FIFO words popped up to it (cumulative), REF_PC, REF_CNT
`timescale 1ns / 1ps
module tb_co;
    `include "diag_pintab.vh"
    localparam [6:0] P_J1 = 7'd50, P_CMP = 7'd63, P_DIS = 7'd67;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1, dc = 0;
    reg [16:0] cw_addr = 17'd0; reg [7:0] cw_wdata = 8'd0; reg cw_we = 0, cw_re = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq;
    wire px_we, px_re;

    // ---- the outside: the joystick pins and the comparators of the model; everything else is high
    reg [127:0] ext_r = {128{1'b1}};
    reg [3:0] cmp_force = 4'd0;
    integer K0 = 0, K1 = 0, K2 = 0, K3 = 0;
    reg [31:0] ch = 0;
    wire [127:0] dg_en, dg_o, dg_z;
    always @(posedge clk) if (dg_en[P_DIS] && dg_o[P_DIS]) ch <= 0; else if (ch != 32'hFFFFFFFF) ch <= ch + 1;
    wire [3:0] cmp = cmp_force | {(K3 != 0 && ch >= K3), (K2 != 0 && ch >= K2), (K1 != 0 && ch >= K1), (K0 != 0 && ch >= K0)};
    wire [127:0] ext = (ext_r & ~(128'hF << P_CMP)) | ({124'd0, cmp} << P_CMP);

    wire run, abort, go, fclr, st_we, fifo_rd, cap_rst, rd_start;
    wire [7:0] st_addr, pc, ref_pc, ref_cnt; wire [31:0] st_wdata, fifo_q; wire [9:0] fifo_level; wire [127:0] live;
    wire running, waiting, done, ovf, err, refused, gtrip, gnow, ev;
    wire r_we, r_wack; wire [11:0] r_off; wire [7:0] r_wd;
    wire c_go, c_refused, c_sampling, c_idle, c_over, c_stop, c_rdbusy, c_rdvalid;
    wire [23:0] c_k; wire [31:0] c_m, c_count, c_lost; wire [19:0] c_lines; wire [18:0] c_rdix; wire [127:0] c_rddata; wire [223:0] c_sel;
    wire e_en, e_wdf_wren; wire [26:0] e_addr; wire [2:0] e_cmd; wire [127:0] e_wdf_data; wire [15:0] e_wdf_mask;

    // ---- the pin model of tb_probe.v: open drain pins are wired AND with the outside, pull pins read their drive, push / pz pins read their drive when driven and the outside otherwise
    wire [127:0] dg_in;
    genvar k;
    generate for (k = 0; k < 128; k = k + 1) begin : pm
        localparam [3:0] C = dgpin_cls(k);
        if (k >= DG_N) begin : none assign dg_in[k] = 1'b0; end
        else if (C[2:0] == 3'd1) begin : od assign dg_in[k] = ext[k] & ~dg_en[k]; end
        else if (C[2:0] == 3'd2) begin : pull assign dg_in[k] = dg_en[k]; end
        else if (C[2:0] >= 3'd3) begin : pp assign dg_in[k] = dg_en[k] ? dg_o[k] : ext[k]; end
        else begin : ro assign dg_in[k] = ext[k]; end
    end endgenerate

    diag_regs regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(32'hCAFE0001), .px_we(px_we), .px_re(px_re), .px_rdata(8'h00), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(ev),
        .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .status({gtrip, c_sampling, refused, err, ovf, done, waiting, running}), .pc(pc), .ref_pc(ref_pc), .ref_cnt(ref_cnt),
        .fifo_q(fifo_q), .fifo_level(fifo_level), .fifo_rd(fifo_rd), .live(live), .drv_en(dg_en), .guard_now(gnow), .cap_rst(cap_rst),
        .e_we(r_we), .e_off(r_off), .e_wd(r_wd), .e_wack(r_wack),
        .cap_sel(c_sel), .cap_st({3'b000, c_rdvalid, c_rdbusy, c_over, c_idle, c_sampling}), .cap_count(c_count), .cap_lost(c_lost), .cap_lines({12'd0, c_lines}),
        .rd_start(rd_start), .rd_line_ix(c_rdix), .rd_valid_i(c_rdvalid), .rd_data(c_rddata));
    probe dut (.clk(clk), .rst(rst), .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .dc(dc), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .pins_async(dg_in), .dg_en(dg_en), .dg_o(dg_o), .dg_z(dg_z), .fifo_rd(fifo_rd), .fifo_q(fifo_q), .fifo_level(fifo_level), .live(live),
        .running(running), .waiting(waiting), .done(done), .ovf(ovf), .err(err), .refused(refused), .guard_trip(gtrip), .guard_now(gnow),
        .ref_pc(ref_pc), .ref_cnt(ref_cnt), .pc_o(pc), .ev(ev),
        .reg_we(r_we), .reg_addr(r_off), .reg_wdata(r_wd), .reg_wack(r_wack),
        .cap_go(c_go), .cap_k(c_k), .cap_m(c_m), .cap_refused(c_refused), .cap_sampling(c_sampling), .cap_stop(c_stop));
    // the capture writer (diag_cap.v) is not instantiated: the four example tests capture nothing, and with it the bench runs six times slower (tb_probe.v covers the capture). Its register side reads as an idle, empty ring
    assign c_idle = 1'b1; assign c_over = 1'b0; assign c_count = 32'd0; assign c_lost = 32'd0; assign c_lines = 20'd0; assign c_rdbusy = 1'b0; assign c_rdvalid = 1'b0; assign c_rddata = 128'd0; assign c_sampling = 1'b0; assign c_refused = 1'b1;
    // the core window's byte bus with the gaps of the application (a few clocks between two accesses)
    task wr(input [11:0] off, input [7:0] d);
        begin @(posedge clk); cw_addr <= {5'd0, off}; cw_wdata <= d; cw_we <= 1; @(posedge clk); cw_we <= 0; repeat (3) @(posedge clk); end
    endtask
    task rd(input [11:0] off, output [7:0] d);
        begin @(posedge clk); cw_addr <= {5'd0, off}; cw_re <= 1; @(posedge clk); cw_re <= 0; #1 d = cw_rdata; repeat (2) @(posedge clk); end
    endtask
    task wstep(input [31:0] w);
        begin wr(12'h020, w[7:0]); wr(12'h021, w[15:8]); wr(12'h022, w[23:16]); wr(12'h023, w[31:24]); end
    endtask
    task rword(output [31:0] v);
        reg [7:0] b0, b1, b2, b3;
        begin rd(12'h030, b0); rd(12'h031, b1); rd(12'h032, b2); rd(12'h033, b3); v = {b3, b2, b1, b0}; end
    endtask

    reg [31:0] sw [0:512];
    reg [31:0] mw [0:128];
    reg [31:0] outw [0:4095];
    reg [31:0] pcs [0:63];
    reg [31:0] cums [0:63];
    integer nw, nph, phase, npc, nout, i, guard, nf, ni;
    reg [8*200-1:0] f_steps, f_model, f_out;
    reg [7:0] s, p8, l0, l1, r0, r1;
    reg [31:0] w;
    integer fd;

    task apply_phase;
        integer q;
        begin
            q = phase < nph ? phase : nph - 1;
            ext_r[P_J1 +: 10] = mw[1 + q * 6][9:0];
            cmp_force = mw[2 + q * 6][3:0];
            K0 = mw[3 + q * 6]; K1 = mw[4 + q * 6]; K2 = mw[5 + q * 6]; K3 = mw[6 + q * 6];
        end
    endtask
    // pops what the FIFO holds into outw (the application's drain: the level, then that many words)
    task drain;
        begin
            rd(12'h035, l1); rd(12'h034, l0);
            ni = {l1, l0};
            for (i = 0; i < ni; i = i + 1) begin rword(w); outw[2 + nf + i] = w; end
            nf = nf + ni;
        end
    endtask

    initial begin
        if (!$value$plusargs("steps=%s", f_steps) || !$value$plusargs("model=%s", f_model) || !$value$plusargs("out=%s", f_out)) begin
            $display("FAIL tb_co: +steps= +model= +out= needed"); $finish;
        end
        $readmemh(f_steps, sw);
        $readmemh(f_model, mw);
        nw = sw[0]; nph = mw[0]; phase = 0; npc = 0; nf = 0;
        apply_phase;
        repeat (20) @(posedge clk); rst <= 0; repeat (6) @(posedge clk);
        // an earlier test left its mark: seven words stored and run to the END, so STEP_ADDR stands at 7 and DONE is set (the application writes 0 to STEP_ADDR
        // to load the next list at 0; an engine that kept 7 would run the leftovers, one that kept DONE would end the next test at once)
        for (i = 0; i < 7; i = i + 1) wstep(32'h00000000);
        wr(12'h012, 8'h01);
        guard = 0; s = 0;
        while (!s[2] && guard < 1000) begin rd(12'h011, s); guard = guard + 1; end
        if (!s[2]) begin $display("FAIL tb_co: the earlier run did not end"); $finish; end
        // the application's start: ABORT, CLEAR, CAP RESET, STEP_ADDR = 0, the words, RUN
        wr(12'h012, 8'h02);
        wr(12'h012, 8'h08);
        wr(12'h012, 8'h10);
        wr(12'h024, 8'h00);
        for (i = 0; i < nw; i = i + 1) wstep(sw[1 + i]);
        rd(12'h024, p8);
        if (p8 != nw[7:0]) begin $display("FAIL tb_co: STEP_ADDR reads %0d after %0d words", p8, nw); $finish; end
        wr(12'h012, 8'h01);
        guard = 0; s = 0;
        while (!(s[2] || s[4]) && guard < 40000000) begin
            repeat (8) @(posedge clk);
            rd(12'h011, s);
            if (s[1]) begin
                rd(12'h016, p8);
                pcs[npc] = p8;
                drain;
                cums[npc] = nf; npc = npc + 1;
                phase = phase + 1; apply_phase;
                wr(12'h012, 8'h04);
                repeat (8) @(posedge clk);
                rd(12'h011, s);
            end
            guard = guard + 1;
        end
        if (guard >= 40000000) begin $display("FAIL tb_co: the engine did not finish"); $finish; end
        drain;
        rd(12'h017, r0); rd(12'h018, r1);
        outw[0] = s; outw[1] = nf;
        outw[2 + nf] = npc;
        for (i = 0; i < npc; i = i + 1) outw[3 + nf + i] = pcs[i];
        for (i = 0; i < npc; i = i + 1) outw[3 + nf + npc + i] = cums[i];
        outw[3 + nf + 2 * npc] = r0; outw[4 + nf + 2 * npc] = r1;
        nout = 5 + nf + 2 * npc;
        fd = $fopen(f_out, "w");
        for (i = 0; i < nout; i = i + 1) $fdisplay(fd, "%h", outw[i]);
        $fclose(fd);
        // the application ends with ABORT: every output released
        wr(12'h012, 8'h02);
        repeat (10) @(posedge clk);
        if (dg_en !== 128'd0) begin $display("FAIL tb_co: outputs not released after the final ABORT"); $finish; end
        $display("tb_co: status %h, %0d FIFO words, %0d waits, %0d refused", s, nf, npc, r1);
        $finish;
    end
    initial begin #4000000000; $display("FAIL tb_co timeout"); $finish; end
endmodule
