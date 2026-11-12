// trx_engine.v APPLY (T20 §17.3) alone: a DDR2 model behind the engine's e_* client
// (pipelined reads, random latency and ready), a RAM model behind ap_*
// (random grants). A random write log (a small address range, so addresses repeat)
// is put straight into the stream 0 area; undo and redo of random ranges, from the
// state a replay of the log gives, must end on the replayed state of the other end.
`timescale 1ns / 1ps
module tb_apply;
    reg clk = 0, ui_clk = 0;
    always #15.859 clk = ~clk;              // 31.5 MHz
    always #6.67 ui_clk = ~ui_clk;          // 75 MHz

    reg         r_we = 0;
    reg   [9:0] r_addr = 0;
    reg  [31:0] r_wdata = 0;
    wire [31:0] r_rdata;
    wire [26:0] e_addr;
    wire  [2:0] e_cmd;
    wire        e_en;
    reg         e_rdy = 1'b0;
    reg [127:0] e_rd_data = 128'd0;
    reg         e_rd_valid = 1'b0;
    wire        ap_req, eng_tgl;
    wire [15:0] ap_addr;
    wire  [7:0] ap_wdata;
    reg         ap_gnt = 1'b0;

    trx_engine eng (
        .clk(clk), .r_we(r_we), .r_re(1'b0), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata),
        .log_on(), .seq_busy(), .mk(1'b0), .mk_snap(1'b1), .mk_cap(1'b0), .mk_skip(1'b0),
        .mk_intg(1'b0), .reu_busy(1'b0), .mk_x(10'd100), .mk_line(9'd16), .phi2_tick(1'b1),
        .dbg_put(1'b0), .dbg_rec(64'd0), .cap_gen(16'h0001),
        .tr_ib(1'b0), .tr_pc(16'd0), .tr_op(8'd0), .tr_b1(8'd0), .tr_b2(8'd0), .tr_regs(40'd0),
        .sb_req(), .sb_addr(), .sb_gnt(1'b1), .sb_rdata(32'd0),
        .wa_we(1'b0), .wa_addr(16'd0), .wa_new(8'd0), .wa_old(8'd0),
        .wb_we(1'b0), .wb_addr(16'd0), .wb_new(8'd0), .wb_old(8'd0),
        .wc_we(1'b0), .wc_addr(10'd0), .wc_new(4'd0), .wc_old(4'd0),
        .wd_we(1'b0), .wd_addr(10'd0), .wd_new(4'd0), .wd_old(4'd0),
        .wk_we(1'b0), .wk_addr(15'd0), .wk_new(8'd0), .cart_rbw(),
        .ap_req(ap_req), .ap_addr(ap_addr), .ap_wdata(ap_wdata), .ap_gnt(ap_gnt),
        .eng_tgl(eng_tgl), .gap_tgl(),
        .ui_clk(ui_clk), .ui_rst(1'b0), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy),
        .e_wdf_data(), .e_wdf_mask(), .e_wdf_wren(),
        .e_rd_data(e_rd_data), .e_rd_data_valid(e_rd_valid),
        .ob_v(1'b0), .ob_addr(15'd0), .ob_old(8'd0)
    );

    // ---- DDR2: the stream 0 area is 16 MB at 0x7000000 (fixed, SLIM D4); the model keeps its first 64 KB
    // and aliases the rest onto them (the unit index modulo 4096) ------------------------------------
    localparam UNITS = 4096;                // 16-byte units in the area
    reg [127:0] ddr [0:UNITS - 1];
    reg  [11:0] rq_unit [0:63];
    integer     rq_t [0:63];
    integer     rq_w = 0, rq_r = 0, ui_n = 0, last_t = 0, maxinfl = 0, reads = 0;
    always @(posedge ui_clk) begin
        ui_n <= ui_n + 1;
        e_rdy <= ($random & 3) != 0;
        e_rd_valid <= 1'b0;
        if (e_en && e_rdy) begin
            if (e_cmd !== 3'b001) begin $display("FAIL: engine command %0d", e_cmd); $finish; end
            // byte address 0x7000000 + unit * 16: e_addr counts 16-bit words (BL8)
            rq_unit[rq_w[5:0]] <= e_addr[14:3];
            last_t = (ui_n + 8 + ($random & 7) > last_t + 1) ? ui_n + 8 + ($random & 7) : last_t + 1;
            rq_t[rq_w[5:0]] <= last_t;
            rq_w <= rq_w + 1;
            reads <= reads + 1;
            if (e_addr[26:23] != 4'h7) begin $display("FAIL: address %h outside the area", e_addr); $finish; end
        end
        if (rq_r != rq_w && ui_n >= rq_t[rq_r[5:0]]) begin
            e_rd_data <= ddr[rq_unit[rq_r[5:0]]];
            e_rd_valid <= 1'b1;
            rq_r <= rq_r + 1;
        end
        if (rq_w - rq_r > maxinfl) maxinfl = rq_w - rq_r;
    end

    // ---- RAM behind the port, random grants -----------------------------------------------
    reg [7:0] ram [0:65535];
    always @(posedge clk) begin
        ap_gnt <= ($random & 3) != 0;
        if (ap_req && ap_gnt) ram[ap_addr] <= ap_wdata;
    end

    // ---- the log: record n of stream 0 ------------------------------------------------------
    localparam NREC = 4000;
    reg  [15:0] l_addr [0:NREC - 1];
    reg   [7:0] l_old [0:NREC - 1], l_new [0:NREC - 1];
    integer base_n = 0;                     // index of record 0 (a wrap test starts near the end)
    reg   [7:0] mdl [0:65535];
    integer i, j, k, fails = 0, jobs = 0, dbg = 0, quick = 0;
    initial begin dbg = $test$plusargs("dbg"); quick = $test$plusargs("quick"); end

    task put_rec(input integer n, input [15:0] a, input [7:0] o, input [7:0] nw);
        reg [11:0] u; reg [127:0] w;
        begin
            u = (n >> 2) & 12'hFFF;
            w = ddr[u];
            w[32 * (n & 3) +: 32] = {nw, o, a};
            ddr[u] = w;
        end
    endtask
    // the model after the first m records
    task replay(input integer m);
        integer q;
        begin
            for (q = 0; q < 65536; q = q + 1) mdl[q] = 8'h00;
            for (q = 0; q < m; q = q + 1) mdl[l_addr[q]] = l_new[q];
        end
    endtask
    task load_ram_from_model;
        integer q;
        begin for (q = 0; q < 65536; q = q + 1) ram[q] = mdl[q]; end
    endtask
    task wr(input [9:0] a, input [31:0] d);
        begin
            @(posedge clk) #1; r_we = 1; r_addr = a; r_wdata = d;
            @(posedge clk) #1; r_we = 0;
        end
    endtask
    // run one job and wait for it (AP_CMD b0 busy); t_clk = the clocks it took
    integer t_clk;
    task run_job(input integer start_n, input integer count, input undo);
        integer guard;
        reg tg0;
        begin
            tg0 = eng_tgl;
            wr(10'h025, (start_n + base_n) & 32'h0FFFFFFF);
            wr(10'h026, count);
            wr(10'h027, undo ? 32'd1 : 32'd0);
            guard = 0;
            repeat (3) @(posedge clk);
            #1 r_addr = 10'h027;
            while (r_rdata[0] && guard < 400000) begin @(posedge clk) #1; guard = guard + 1; end
            t_clk = guard;
            if (dbg) begin $display("job start %0d count %0d undo %0d: %0d clocks", start_n, count, undo, guard); $fflush; end
            if (guard >= 400000) begin fails = fails + 1; $display("FAIL: job did not finish (start %0d count %0d undo %0d)", start_n, count, undo); end
            repeat (6) @(posedge clk);
            jobs = jobs + 1;
            if (count != 0 && eng_tgl == tg0) begin fails = fails + 1; $display("FAIL: no completion toggle"); end
            if (count == 0 && eng_tgl != tg0) begin fails = fails + 1; $display("FAIL: toggle for an empty job"); end
        end
    endtask

    task compare(input [255:0] what);
        integer q, bad;
        begin
            bad = 0;
            for (q = 0; q < 65536; q = q + 1) if (ram[q] !== mdl[q]) bad = bad + 1;
            if (bad) begin fails = fails + 1; $display("FAIL %0s: %0d bytes differ", what, bad); end
        end
    endtask

    integer n_tests;
    initial quick = $test$plusargs("quick");
    integer s, c, phase;
    initial begin
        for (i = 0; i < UNITS; i = i + 1) ddr[i] = 128'd0;
        #200;
        // the area: 16 MB at 0x7000000, fixed (writes to the old LOG_BASE/LOG_MASK registers change nothing)
        wr(10'h009, 32'h00000FFF << 4);
        wr(10'h008, 32'h08000000);
        for (phase = 0; phase < 2; phase = phase + 1) begin
            base_n = phase == 0 ? 0 : 4194304 - 700;      // the second log wraps the 16 MB area (4,194,304 records)
            // the log: 700..NREC records, 256 addresses so they repeat
            replay(0);
            for (i = 0; i < NREC; i = i + 1) begin
                l_addr[i] = 16'h2000 + ($random & 255);
                l_old[i] = mdl[l_addr[i]];
                l_new[i] = $random;
                mdl[l_addr[i]] = l_new[i];
                put_rec(base_n + i, l_addr[i], l_old[i], l_new[i]);
            end
            n_tests = 0;
            $display("phase %0d: log written, base %0d", phase, base_n);
            for (k = 0; k < (quick ? 6 : 120); k = k + 1) begin
                s = $unsigned($random) % (NREC - 1);
                c = k < 8 ? k : (k < 30 ? 1 + ($unsigned($random) % 9) : 1 + ($unsigned($random) % 900));
                if (s + c > NREC) c = NREC - s;
                // redo [s, s + c): from the state after s records to the one after s + c
                replay(s); load_ram_from_model();
                run_job(s, c, 1'b0);
                replay(s + c);
                compare("redo");
                // undo of the same range: from s + c back to s, newest first
                load_ram_from_model();
                run_job(s + c - 1, c, 1'b1);
                replay(s);
                compare("undo");
                n_tests = n_tests + 1;
            end
            // every start lane, every end lane
            for (s = 0; s < (quick ? 2 : 8); s = s + 1)
                for (c = 1; c < (quick ? 4 : 10); c = c + 1) begin
                    replay(100 + s); load_ram_from_model(); run_job(100 + s, c, 1'b0);
                    replay(100 + s + c); compare("lanes redo");
                    load_ram_from_model(); run_job(100 + s + c - 1, c, 1'b1);
                    replay(100 + s); compare("lanes undo");
                end
            // count 0 is ignored; a job with the whole log, undo to zero
            run_job(5, 0, 1'b1);
            replay(NREC); load_ram_from_model(); run_job(NREC - 1, NREC, 1'b1);
            replay(0); compare("whole log undo");
            $display("phase %0d: %0d range tests, last job %0d clocks, %0d reads so far, max reads in flight %0d",
                     phase, n_tests, t_clk, reads, maxinfl);
        end
        // a long job's speed: records per clk
        base_n = 0;
        replay(NREC); load_ram_from_model();
        run_job(NREC - 1, NREC, 1'b1);
        $display("speed: %0d records in %0d sys clocks (%0.1f records per clock with grants at 75%%)",
                 NREC, t_clk, NREC * 1.0 / t_clk);
        if (maxinfl > 25) begin fails = fails + 1; $display("FAIL: %0d reads in flight", maxinfl); end
        if (fails) $display("%0d FAILED", fails); else $display("PASS");
        $finish;
    end
endmodule
