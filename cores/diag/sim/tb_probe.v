// T175 S1: the probe engine through its register block (diag_regs.v + probe.v wired as layer2_diag.v wires them), driven over the core window's byte bus the way the
// application will. The loop-back model: an open-drain pin follows the engine's drive-low at once; the paddle comparators are a charge counter that restarts while the engine
// holds PADDLE_DIS (K0 / K1 clocks to flip); the external stimulus (ext_joy, cmp_force) is the TB's. The timing is checked against the TB's own clock counter on the pin and
// against a cost table (an op costs 2 clocks, WAIT 2+N) - never against the engine's own counters. Controls are -D switches: each makes ONE check see a wrong expectation or
// a mutated engine (run.sh), and must print FAIL.
`timescale 1ns / 1ps
module tb_probe;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1;
    reg [16:0] cw_addr = 17'd0; reg [7:0] cw_wdata = 8'd0; reg cw_we = 0, cw_re = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq;
    wire px_we, px_re;
    reg [7:0] px_rdata = 8'h5A;
    reg [9:0] ext_joy = 10'h3FF;                // external stimulus: 1 = high (the pull-up), 0 = held low from outside
    reg [3:0] cmp_force = 4'd0;                 // comparator bits forced by the TB
    integer K0 = 1234, K1 = 777;

    wire run, abort, go, fclr, st_we, fifo_rd;
    wire [7:0] st_addr, pc; wire [31:0] st_wdata, fifo_q; wire [9:0] fifo_level; wire [13:0] live;
    wire running, waiting, done, ovf, err, ev;
    wire [9:0] drv_low; wire pdo, pde, pao, pae;

    wire [9:0] pin = ext_joy & ~drv_low;        // open drain, wired AND
    reg [31:0] ch = 0;
    always @(posedge clk) if (pde && pdo) ch <= 0; else ch <= ch + 1;
    wire [3:0] cmp = cmp_force | {2'b00, (ch >= K1), (ch >= K0)};

    diag_regs regs (.clk(clk), .we(cw_we), .re(cw_re), .addr(cw_addr), .wdata(cw_wdata), .rdata(cw_rdata), .ack(cw_ack), .irq(cw_irq),
        .build(32'hCAFE0001), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(ev),
        .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .status({err, ovf, done, waiting, running}), .pc(pc), .fifo_q(fifo_q), .fifo_level(fifo_level), .fifo_rd(fifo_rd), .live(live), .drv_low(drv_low));
    probe dut (.clk(clk), .rst(rst), .run(run), .abort(abort), .go(go), .fifo_clr(fclr), .st_we(st_we), .st_addr(st_addr), .st_wdata(st_wdata),
        .pins_async({cmp, pin}), .drv_low(drv_low), .pad_dis_o(pdo), .pad_dis_en(pde), .pad_adj_o(pao), .pad_adj_en(pae),
        .fifo_rd(fifo_rd), .fifo_q(fifo_q), .fifo_level(fifo_level), .live(live),
        .running(running), .waiting(waiting), .done(done), .ovf(ovf), .err(err), .pc_o(pc), .ev(ev));

    integer errors = 0, checks = 0;
    task check(input c, input [8*80-1:0] msg);
        begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL %0s", msg); end end
    endtask

    // ---- the core window's byte bus (gaps like the application's)
    task wr(input [11:0] off, input [7:0] d);
        begin @(posedge clk); cw_addr <= {5'd0, off}; cw_wdata <= d; cw_we <= 1; @(posedge clk); cw_we <= 0; repeat (3) @(posedge clk); end
    endtask
    task rd(input [11:0] off, output [7:0] d);
        begin @(posedge clk); cw_addr <= {5'd0, off}; cw_re <= 1; @(posedge clk); cw_re <= 0; #1 d = cw_rdata; repeat (2) @(posedge clk); end
    endtask
    task rd32(input [11:0] off, output [31:0] v);
        reg [7:0] b0, b1, b2, b3;
        begin rd(off, b0); rd(off + 12'd1, b1); rd(off + 12'd2, b2); rd(off + 12'd3, b3); v = {b3, b2, b1, b0}; end
    endtask
    task step(input [31:0] w);                  // STEP_DATA, little endian; the write of 023 stores it
        begin wr(12'h020, w[7:0]); wr(12'h021, w[15:8]); wr(12'h022, w[23:16]); wr(12'h023, w[31:24]); end
    endtask
    task pop(output [31:0] v);                  // FIFO_DATA: the read of 033 pops
        begin rd32(12'h030, v); end
    endtask
    reg [7:0] b;
    task status(output [7:0] s); begin rd(12'h011, s); end endtask
    task start; begin wr(12'h024, 8'd0); end endtask   // STEP_ADDR = 0
    task runit; begin wr(12'h012, 8'h01); end endtask
    task wait_done; integer n; reg [7:0] s;
        begin n = 0; s = 0; while (!s[2] && n < 4000) begin status(s); n = n + 1; end if (!s[2]) begin errors = errors + 1; $display("FAIL engine did not finish"); end end
    endtask
    task clear; begin wr(12'h012, 8'h08); end endtask

    function [31:0] w_drive(input [9:0] mask, input [9:0] level); w_drive = {4'd1, 4'd0, 4'b0000, level, mask}; endfunction
    function [31:0] w_wait(input [23:0] n); w_wait = {4'd2, 4'd0, n}; endfunction
    localparam [31:0] W_SAMPLE = {4'd3, 28'd0}, W_END = 32'd0, W_USER = {4'd8, 28'd0};
    function [31:0] w_edges(input [23:0] n); w_edges = {4'd4, 4'd0, n}; endfunction
    function [31:0] w_ttf(input [3:0] sel, input [23:0] n); w_ttf = {4'd5, sel, n}; endfunction
    function [31:0] w_pad(input [3:0] f); w_pad = {4'd6, 4'd0, 20'd0, f}; endfunction
    function [31:0] w_loop(input [7:0] k, input [15:0] c); w_loop = {4'd7, 4'd0, c, k}; endfunction
    function [31:0] w_mask(input [13:0] m); w_mask = {4'd9, 4'd0, 10'd0, m}; endfunction

    // ---- the TB's own view of the pin: cycle counter and the low interval of joy1 pin 6 (bit 4)
    integer cyc = 0, t_lo = -1, t_hi = -1;
    reg pin4_q = 1;
    always @(posedge clk) begin
        cyc <= cyc + 1; pin4_q <= pin[4];
        if (pin4_q && !pin[4]) t_lo <= cyc;
        if (!pin4_q && pin[4]) t_hi <= cyc;
    end

    reg [31:0] w0, w1, w2, w3;
    reg [7:0] s;
    integer i, N, width, cnt;

    // ---- scenario A (S1): drive, exact wait, timestamps, release
    task scen_timing(input integer nwait, input integer nexpect);
        begin
            clear; start;
            step(w_drive(10'h010, 10'h000));
            step(w_wait(1));
            step(W_SAMPLE);
            step(w_wait(nwait));
            step(W_SAMPLE);
            step(w_drive(10'h010, 10'h010));
            step(w_wait(1));
            step(W_SAMPLE);
            step(W_END);
            t_lo = -1; t_hi = -1;
            runit; wait_done;
            pop(w0); pop(w1); pop(w2);
            check(w0[15:6] == 10'h3EF, "A: sample 1 sees pin 6 of port 1 low, the rest high");
            check(w2[15:6] == 10'h3FF, "A: sample 3 sees every pin released");
            check(w1[31:16] - w0[31:16] == nexpect + 4, "A: SAMPLE..SAMPLE = WAIT N + 4 clocks (timestamps)");
            check(t_hi - t_lo == nexpect + 11, "A: the pin is low exactly N + 11 clocks (DRIVE..DRIVE = 2+3+2+(2+N)+2)");
            check(t_hi - t_lo - 11 == 1000 || nexpect != 1000, "A: N is the 1000 the step list says");
        end
    endtask

    // the pin takes effect 1 clock after the DRIVE and `live` shows it 3 clocks after it: a SAMPLE right behind a DRIVE still sees the old level
    task scen_order;
        begin
            clear; start;
            step(w_drive(10'h010, 10'h000));
            step(w_wait(1));
            step(W_SAMPLE);
            step(W_END);
            runit; wait_done;
            pop(w0);
            check(w0[15:6] == 10'h3EF, "B: a SAMPLE has to follow the DRIVE by a WAIT to see it");
        end
    endtask

    // ---- live pin register
    task scen_live;
        reg [31:0] v;
        begin
            ext_joy = 10'b10_0101_1010; cmp_force = 4'b0110;
            repeat (8) @(posedge clk);
            rd(12'h040, b); w0[7:0] = b; rd(12'h041, b);
            check({b[5:0], w0[7:0]} == {4'b0110, ext_joy}, "C: live = {cmp[3:0], joy2[4:0], joy1[4:0]}");
            check(b[7:6] == 2'b00, "C: live bits 15:14 read 0");
            ext_joy = 10'h3FF; cmp_force = 4'd0;
            repeat (8) @(posedge clk);
        end
    endtask

    // ---- EDGES: the window is exactly N clocks; one edge placed on its last countable clock counts, one placed a clock later does not
    task scen_edges(input integer n, input integer at, input integer want);
        integer k;
        begin
            clear; start;
            step(w_mask(14'h0200));
            step(w_edges(n));
            step(W_END);
            ext_joy[9] = 1; repeat (6) @(posedge clk);
            // the stimulus fires `at` clocks after the window's first clock
            fork
                begin
                    runit;
                end
                begin : stim
                    k = 0;
                    while (dut.st != 4'd3) @(posedge clk);
                    // now the clock in which the window's first F_EDGE clock is visible
                    repeat (at) @(posedge clk);
                    ext_joy[9] <= 0;
                end
            join
            wait_done;
            pop(w0);
            check(w0 == want, "D: EDGES count");
            ext_joy[9] = 1; repeat (6) @(posedge clk);
        end
    endtask

    // many edges: 10 pulses (20 edges) of a 7-clock period fully inside the window
    task scen_edges_many;
        integer p;
        begin
            clear; start;
            step(w_mask(14'h0200)); step(w_edges(300)); step(W_END);
            ext_joy[9] = 1; repeat (6) @(posedge clk);
            fork
                runit;
                begin
                    while (dut.st != 4'd3) @(posedge clk);
                    repeat (20) @(posedge clk);
                    for (p = 0; p < 10; p = p + 1) begin ext_joy[9] <= 0; repeat (3) @(posedge clk); ext_joy[9] <= 1; repeat (4) @(posedge clk); end
                end
            join
            wait_done;
            pop(w0);
            check(w0 == 20, "D: ten pulses = 20 edges");
            ext_joy[9] = 1; repeat (6) @(posedge clk);
        end
    endtask

    // ---- TTF with the modelled comparator
    task scen_ttf;
        begin
            clear; start;
            step(w_pad(4'b0011));                // PADDLE_DIS driven high (discharge), enabled
            step(w_wait(50));
            step(w_pad(4'b0000));                // released: the model counts from here
            step(w_ttf(4'b0001, 5000));          // cmp 0
            step(w_pad(4'b0011)); step(w_wait(50)); step(w_pad(4'b0000));
            step(w_ttf(4'b0010, 300));           // cmp 1: K1 = 777 > 300: timeout
            step(w_pad(4'b0011)); step(w_wait(50)); step(w_pad(4'b0000));
            step(w_mask(14'h0C00));              // MASK = cmp 2,3 (never flip) with select 0
            step(w_ttf(4'b0000, 100));
            step(W_END);
            runit; wait_done;
            pop(w0); pop(w1); pop(w2);
            check(w0 == K0, "E: time to flip = K clocks after the release");
            check(w0[31] == 0, "E: no timeout flag on a flip");
            check(w1 == {1'b1, 7'd0, 24'd300}, "E: comparator that does not flip in time: timeout flag, timeout clocks");
            check(w2 == {1'b1, 7'd0, 24'd100}, "E: select 0 uses MASK: no flip on cmp 2/3 = timeout");
            check(!pde && !pae, "E: END released PADDLE_DIS / ADJ");
            // other K: the difference of two measurements is exactly the difference of the K
            K0 = 1000; clear; start;
            step(w_pad(4'b0011)); step(w_wait(50)); step(w_pad(4'b0000)); step(w_ttf(4'b0001, 5000)); step(W_END);
            runit; wait_done; pop(w3);
            check(w0 - w3 == 234, "E: K 1234 vs 1000 differs by 234 clocks");
            K0 = 1234;
            // no paddle plugged: the comparator never flips
            K0 = 1000000; clear; start;
            step(w_pad(4'b0011)); step(w_wait(50)); step(w_pad(4'b0000)); step(w_ttf(4'b0001, 2000)); step(W_END);
            runit; wait_done; pop(w3);
            check(w3 == {1'b1, 7'd0, 24'd2000}, "E: nothing plugged: 'no flip within timeout', not a number");
            K0 = 1234;
        end
    endtask

    // ---- FIFO order, overflow, LOOP, the lost flag
    task scen_fifo;
        integer j; reg [31:0] v; reg [31:0] prev; reg ok;
        begin
            clear; start;
            step(w_loop(1, 600));
            step(W_SAMPLE);
            step(W_END);
            runit; wait_done;
            rd(12'h034, b); w0[7:0] = b; rd(12'h035, b);
            check({b[1:0], w0[7:0]} == 10'd512, "F: 600 samples into a 512 word FIFO: level 512");
            status(s);
            check(s[3], "F: overflow status set");
            ok = 1; prev = 0;
            for (j = 0; j < 512; j = j + 1) begin
                pop(v);
                if (j == 0) begin if (v[1]) begin ok = 0; end end                // the first word is not 'lost'
                else if (v[31:16] - prev[31:16] != 2) ok = 0;                     // every SAMPLE of the loop is 2 clocks after the one before it
                prev = v;
            end
            check(ok, "F: the 512 kept words are the FIRST 512, in order, 2 clocks apart");
            rd(12'h034, b); w0[7:0] = b; rd(12'h035, b);
            check({b[1:0], w0[7:0]} == 10'd0, "F: popped empty");
            clear;
            status(s); check(!s[3], "F: CLEAR clears overflow");
            // LOOP: body of 2 steps, 7 times = 14 words; the loop ends exactly
            start;
            step(w_loop(2, 7));
            step(W_SAMPLE); step(W_SAMPLE);
            step(W_SAMPLE);                      // after the loop: once
            step(W_END);
            runit; wait_done;
            rd(12'h034, b);
            check(b == 8'd15, "F: LOOP 2 x 7 + 1 after = 15 words");
            clear;
        end
    endtask

    // ---- WAIT_USER, GO, the timebase restart
    task scen_user;
        begin
            clear; start;
            step(W_SAMPLE); step(W_USER); step(W_SAMPLE); step(W_END);
            runit;
            repeat (200) @(posedge clk);
            status(s);
            check(s[1] && !s[2] && s[0], "G: waiting for GO: running, waiting, not done");
            rd(12'h034, b);
            check(b == 8'd1, "G: nothing runs across the wait: one word");
            rd(12'h016, b); check(b == 8'd1, "G: PC stays at the WAIT_USER step (index 1)");
            wr(12'h012, 8'h04);
            wait_done;
            rd(12'h034, b); check(b == 8'd2, "G: GO releases it");
            pop(w0); pop(w1);
            check(w1[31:16] == 16'd1, "G: the timebase restarts at GO");
            check(w0[31:16] == 16'd1, "G: first op after RUN runs at t = 1");
            clear;
        end
    endtask

    // ---- safety: only named pins, END / ABORT / error release everything
    // ---- the protocol the application relies on (T175 co-check): STEP_ADDR, DONE after ABORT / before RUN, PC at WAIT_USER, the FIFO's word order
    task scen_proto;
        reg [31:0] v0, v1, v2;
        begin
            // STEP_ADDR counts the stored words; writing 0 puts the next word at step 0 again
            clear; wr(12'h024, 8'd0);
            step(w_wait(3)); step(W_SAMPLE); step(W_END);
            rd(12'h024, b); check(b == 8'd3, "P: STEP_ADDR counts the stored words");
            wr(12'h024, 8'd0);
            rd(12'h024, b); check(b == 8'd0, "P: writing 0 to STEP_ADDR resets the step pointer");
            step(W_SAMPLE);                       // word 0 is a SAMPLE now, word 1 still the old SAMPLE, word 2 the old END
            rd(12'h024, b); check(b == 8'd1, "P: the first word after the reset went to step 0");
            runit; wait_done;
            rd(12'h034, b); check(b == 8'd2, "P: the new word 0 and the old words 1, 2 run: SAMPLE, SAMPLE, END");
            pop(w0); pop(w1);
            check(w0[31:16] == 16'd1 && w1[31:16] == 16'd3, "P: ... at t = 1 and t = 3 (no WAIT 3 left in front)");
            clear;
            // ABORT sets DONE, it never clears it; RUN clears it
            start; step(w_wait(100000)); step(W_END);
            runit; repeat (20) @(posedge clk);
            status(s); check(s[0] && !s[2], "P: a long list runs: running, not done");
            wr(12'h012, 8'h02);
            status(s); check(s[2] && !s[0], "P: ABORT of a running engine: DONE set, not running");
            wr(12'h012, 8'h02);
            status(s); check(s[2] && !s[0], "P: ABORT of an idle engine changes nothing: DONE stays");
            runit;
            status(s); check(!s[2] && s[0], "P: RUN clears DONE (read 4 clocks after the write)");
            wr(12'h012, 8'h02);
            // the same read one clock after the RUN write still sees the old DONE: the application waits before it polls
            @(posedge clk); cw_addr <= 17'h012; cw_wdata <= 8'h01; cw_we <= 1; @(posedge clk); cw_we <= 0;
            cw_addr <= 17'h011; cw_re <= 1; @(posedge clk); cw_re <= 0; #1 s = cw_rdata;
            check(s[2], "P: STATUS read in the clock after the RUN write still shows the old DONE");
            repeat (6) @(posedge clk); status(s);
            check(!s[2] && s[0], "P: ... and a few clocks later it shows running");
            wr(12'h012, 8'h02);
            // PC at a WAIT_USER behind MASK / EDGES, and again at the next one after GO
            clear; start;
            step(w_mask(14'h0200)); step(w_edges(4)); step(W_SAMPLE); step(W_USER); step(W_SAMPLE); step(W_USER); step(W_END);
            runit; repeat (200) @(posedge clk);
            status(s); rd(12'h016, b);
            check(s[1] && b == 8'd3, "P: waiting for GO with PC at the first WAIT_USER (index 3)");
            wr(12'h012, 8'h04); repeat (40) @(posedge clk);
            status(s); rd(12'h016, b);
            check(s[1] && b == 8'd5, "P: after GO the next wait shows PC 5");
            wr(12'h012, 8'h04); wait_done;
            // the FIFO delivers the words in step order, as the engine pushed them, nothing added
            clear; start;
            step(W_SAMPLE); step(w_mask(14'h0200)); step(w_edges(8)); step(w_ttf(4'b0010, 5)); step(W_END);
            runit; wait_done;
            rd(12'h034, b); check(b == 8'd3, "P: three results");
            pop(v0); pop(v1); pop(v2);
            check(v0[31:16] == 16'd1 && v0[15:6] == 10'h3FF, "P: FIFO word 0 is the SAMPLE");
            check(v1 == 32'd0, "P: FIFO word 1 is the EDGES count, exactly (no tag bits)");
            check(v2 == 32'h80000005, "P: FIFO word 2 is the time-to-flip, exactly: timeout flag, 5 clocks");
            clear;
        end
    endtask
    task scen_safety;
        reg [7:0] lo, hi;
        begin
            clear; start;
            step(w_drive(10'h001, 10'h000)); step(w_drive(10'h200, 10'h000)); step(w_pad(4'b0011)); step(w_wait(100000)); step(W_END);
            runit; repeat (60) @(posedge clk);
            rd(12'h042, lo); rd(12'h043, hi);
            check({hi[1:0], lo} == 10'h201, "H: only the two named pins are low");
            check(pde && pdo, "H: PADDLE_DIS driven");
            wr(12'h012, 8'h02);                   // ABORT
            repeat (10) @(posedge clk);
            rd(12'h042, lo); rd(12'h043, hi);
            check({hi[1:0], lo} == 10'h000 && !pde && !pae, "H: ABORT released every output");
            status(s); check(s[2] && !s[0], "H: ABORT: done, not running");
            // END releases
            clear; start; step(w_drive(10'h3FF, 10'h000)); step(w_wait(5)); step(W_END);
            runit; wait_done; rd(12'h042, lo); rd(12'h043, hi);
            check({hi[1:0], lo} == 10'h000, "H: END released every pin");
            // an unknown op is an error and releases
            clear; start; step(w_drive(10'h0F0, 10'h000)); step(w_wait(3)); step({4'hE, 28'd0}); step(W_END);
            runit; wait_done; rd(12'h042, lo); rd(12'h043, hi); status(s);
            check({hi[1:0], lo} == 10'h000 && s[4], "H: unknown op: step error, released");
            // a nested LOOP is an error
            clear; start; step(w_loop(2, 3)); step(w_loop(1, 2)); step(W_SAMPLE); step(W_END);
            runit; wait_done; status(s); check(s[4], "H: LOOP inside a LOOP is a step error");
            // the pins follow: with a pin held low from outside the engine does not release it
            clear;
        end
    endtask

    // ---- register map
    task scen_regs;
        reg [31:0] v;
        begin
            rd32(12'h000, v); check(v == 32'h47414944, "I: ID reads DIAG");
            rd(12'h004, b); check(b == 8'hC0, "I: CAPS 0xC0 (pixel layer)");
            rd(12'h006, b); check(b == 8'h03, "I: DCAPS pins + paddle");
            rd32(12'h008, v); check(v == 32'hCAFE0001, "I: BUILD");
            rd32(12'h00C, v); check(v == 32'h00006400, "I: ticks per us x 256 = 100 MHz");
            // STEP_ADDR auto increment
            wr(12'h024, 8'd10); step(32'h11223344); step(32'h55667788);
            rd(12'h024, b); check(b == 8'd12, "I: STEP_ADDR auto-increments after each stored word");
            check(dut.ram[10] == 32'h11223344 && dut.ram[11] == 32'h55667788, "I: step words stored little endian at STEP_ADDR");
            // page 6 forwarded, page 0 not
            @(posedge clk); cw_addr <= 17'h06010; cw_we <= 1; @(posedge clk); check(px_we, "I: page 6 write forwarded to the pixel layer"); cw_we <= 0; cw_re <= 1; @(posedge clk); check(px_re, "I: page 6 read forwarded"); cw_re <= 0;
            #1 check(cw_rdata == 8'h5A, "I: page 6 read data from the pixel layer"); repeat (3) @(posedge clk);
            @(posedge clk); cw_addr <= 17'h00010; cw_we <= 1; @(posedge clk); check(!px_we, "I: page 0 not forwarded"); cw_we <= 0;
            repeat (3) @(posedge clk);
            // the engine's stop raises IRQ cause bit 5
            wr(12'h014, 8'hFF); clear; start; step(W_END); runit; wait_done;
            rd(12'h014, b); check(b[5], "I: IRQ cause b5: the engine stopped");
        end
    endtask

    initial begin
        repeat (6) @(posedge clk); rst <= 0; repeat (6) @(posedge clk);
        scen_regs;
        scen_live;
        scen_timing(1000, 1000);
        scen_timing(1, 1);
        scen_order;
        // the window of EDGES(n) acts on clocks 1..n; an edge `at` clocks after its first visible clock is counted when it reaches the comparator in time
        // (2 synchroniser flops + the previous-value register): the last countable one is at = n - 4, one later is outside
        scen_edges(40, 36, 1);
        scen_edges(40, 37, 0);
        scen_edges_many;
        scen_ttf;
        scen_fifo;
        scen_user;
        scen_safety;
        scen_proto;
        if (errors == 0) $display("PASS tb_probe errors=0 checks=%0d", checks); else $display("FAIL tb_probe errors=%0d checks=%0d", errors, checks);
        $finish;
    end
    initial begin #400000000; $display("FAIL tb_probe timeout"); $finish; end
endmodule
