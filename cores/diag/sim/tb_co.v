// T175 co-check: runs a step list the APPLICATION compiled (tests/coreapp/diag_co.cc capture, from a shipped example test) on the real probe engine behind its register block,
// the way the application drives it (ABORT, CLEAR, STEP_ADDR = 0, the words, RUN, poll STATUS, at a wait: read PC, GO; at done: pop the FIFO), against a model of the
// peripherals the test asks the person to plug in, and writes down what the application will read: STATUS, the FIFO words and the PCs it waited at. The application's
// decoder then reads that record (diag_co.cc replay). The TB judges nothing about the test; the two sides meet only through these files.
//   +steps=<hex>   word count, then the step words
//   +model=<hex>   phase count P, then per phase six words: external pin levels (1 = high, held low from outside when 0), comparator bits forced high, then K0..K3, the clocks after
//                  PADDLE_DIS is let go (low or off) until comparator n flips (0: never). Phase p holds from the p-th GO on (phase 0 until the first GO); the last phase stays.
//   +out=<hex>     STATUS byte, FIFO word count, the words, wait count, the PCs
`timescale 1ns / 1ps
module tb_co;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1;
    reg [16:0] cw_addr = 17'd0; reg [7:0] cw_wdata = 8'd0; reg cw_we = 0, cw_re = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq;
    wire px_we, px_re;
    reg [9:0] ext_joy = 10'h3FF;
    reg [3:0] cmp_force = 4'd0;
    integer K0 = 0, K1 = 0, K2 = 0, K3 = 0;

    wire run, abort, go, fclr, st_we, fifo_rd;
    wire [7:0] st_addr, pc; wire [31:0] st_wdata, fifo_q; wire [9:0] fifo_level; wire [13:0] live;
    wire running, waiting, done, ovf, err, ev;
    wire [9:0] drv_low; wire pdo, pde, pao, pae;

    wire [9:0] pin = ext_joy & ~drv_low;                // open drain, wired AND
    reg [31:0] ch = 0;
    always @(posedge clk) if (pde && pdo) ch <= 0; else if (ch != 32'hFFFFFFFF) ch <= ch + 1;
    wire [3:0] cmp = cmp_force | {(K3 != 0 && ch >= K3), (K2 != 0 && ch >= K2), (K1 != 0 && ch >= K1), (K0 != 0 && ch >= K0)};

    diag_regs regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(32'hCAFE0001), .px_we(px_we), .px_re(px_re), .px_rdata(8'h00), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(ev),
        .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .status({err, ovf, done, waiting, running}), .pc(pc), .fifo_q(fifo_q), .fifo_level(fifo_level), .fifo_rd(fifo_rd), .live(live), .drv_low(drv_low));
    probe dut (.clk(clk), .rst(rst), .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .pins_async({cmp, pin}), .drv_low(drv_low), .pad_dis_o(pdo), .pad_dis_en(pde), .pad_adj_o(pao), .pad_adj_en(pae),
        .fifo_rd(fifo_rd), .fifo_q(fifo_q), .fifo_level(fifo_level), .live(live),
        .running(running), .waiting(waiting), .done(done), .ovf(ovf), .err(err), .pc_o(pc), .ev(ev));

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
    reg [31:0] outw [0:1023];
    reg [31:0] pcs [0:63];
    integer nw, nph, phase, npc, nout, i, guard, nf;
    reg [8*200-1:0] f_steps, f_model, f_out;
    reg [7:0] s, p8, l0, l1;
    reg [31:0] w;
    integer fd;

    task apply_phase;
        integer q;
        begin
            q = phase < nph ? phase : nph - 1;
            ext_joy = mw[1 + q * 6][9:0];
            cmp_force = mw[2 + q * 6][3:0];
            K0 = mw[3 + q * 6]; K1 = mw[4 + q * 6]; K2 = mw[5 + q * 6]; K3 = mw[6 + q * 6];
        end
    endtask

    initial begin
        if (!$value$plusargs("steps=%s", f_steps) || !$value$plusargs("model=%s", f_model) || !$value$plusargs("out=%s", f_out)) begin
            $display("FAIL tb_co: +steps= +model= +out= needed"); $finish;
        end
        $readmemh(f_steps, sw);
        $readmemh(f_model, mw);
        nw = sw[0]; nph = mw[0]; phase = 0; npc = 0;
        apply_phase;
        repeat (6) @(posedge clk); rst <= 0; repeat (6) @(posedge clk);
        // an earlier test left its mark: seven words stored and run to the END, so STEP_ADDR stands at 7 and DONE is set (the application writes 0 to STEP_ADDR
        // to load the next list at 0; an engine that kept 7 would run the leftovers, one that kept DONE would end the next test at once)
        for (i = 0; i < 7; i = i + 1) wstep(32'h00000000);
        wr(12'h012, 8'h01);
        guard = 0; s = 0;
        while (!s[2] && guard < 1000) begin rd(12'h011, s); guard = guard + 1; end
        if (!s[2]) begin $display("FAIL tb_co: the earlier run did not end"); $finish; end
        // the application's start: ABORT, CLEAR, STEP_ADDR = 0, the words, RUN
        wr(12'h012, 8'h02);
        wr(12'h012, 8'h08);
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
                pcs[npc] = p8; npc = npc + 1;
                phase = phase + 1; apply_phase;
                wr(12'h012, 8'h04);
                repeat (8) @(posedge clk);
                rd(12'h011, s);
            end
            guard = guard + 1;
        end
        if (guard >= 40000000) begin $display("FAIL tb_co: the engine did not finish"); $finish; end
        rd(12'h034, l0); rd(12'h035, l1);
        nf = {l1, l0};
        outw[0] = s; outw[1] = nf;
        for (i = 0; i < nf; i = i + 1) begin rword(w); outw[2 + i] = w; end
        outw[2 + nf] = npc;
        for (i = 0; i < npc; i = i + 1) outw[3 + nf + i] = pcs[i];
        nout = 3 + nf + npc;
        fd = $fopen(f_out, "w");
        for (i = 0; i < nout; i = i + 1) $fdisplay(fd, "%h", outw[i]);
        $fclose(fd);
        // the application ends with ABORT: every output released
        wr(12'h012, 8'h02);
        repeat (10) @(posedge clk);
        if (drv_low != 10'd0 || pde || pae) begin $display("FAIL tb_co: outputs not released after the final ABORT"); $finish; end
        $display("tb_co: status %h, %0d FIFO words, %0d waits", s, nf, npc);
        $finish;
    end
    initial begin #4000000000; $display("FAIL tb_co timeout"); $finish; end
endmodule
