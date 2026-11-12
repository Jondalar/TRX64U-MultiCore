// T208 S5: scpu_cache.v alone, with a DDR2 model of its two jobs (S line fetch, SW byte write). Three directed races of the next-line prefetch against a CPU write that no CPU program can
// time to the clock (the CPU's enables are at least 3 clk2x apart, which tb_c64scpu.v asserts), so they are driven on the module's ports:
//   race_a   the prefetch of line LA + 1 would start in the clock after rd_if; wr_go of a byte of line LA + 1 is in that very clock
//   race_b   the same with the prefetch held back by `quiet` (rd_if while the fill's quiet window runs; the write in the first clock the prefetch may start)
//   race_c   a prefetch is open, a write to its line (kill_q) and in the next clock a write to a cached other line: the byte write has to win (inval waits one clock)
// The DDR2 model serves the S job 3 clocks after its toggle and lands the SW byte 6 clocks after its toggle: the worst case of the engine's order (c64scpu_ddrx.v serves writes first; a toggle seen
// one ui clock later than the other's would break that). The check after each race: whatever the cache holds of the line equals the DDR2 model's bytes (a stale line is a FAIL).
// Controls: -DSCPU_MUT_NOPGOWR (race_a and race_b must FAIL), -DSCPU_MUT_NOINVDEFER (race_c must FAIL). Run by run_mdust.sh (iverilog).
`timescale 1ns / 1ps
module tb_scpu_cache;
    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg flush = 1'b0, f_go = 1'b0, f_if = 1'b0, rd_done = 1'b0, rd_if = 1'b0, wr_go = 1'b0;
    reg [8:0] ix = 9'd0;
    reg [20:0] f_line = 21'd0, rd_ln = 21'd0;
    reg [24:0] w_p = 25'd0;
    reg [7:0] wr_d = 8'd0;
    wire [140:0] cq;
    wire wbusy, flushing;
    wire s_req_tgl, sw_req_tgl;
    wire [20:0] s_req_line;
    wire [24:0] sw_req_addr;
    wire [7:0] sw_req_data;
    reg s_done_tgl = 1'b0, sw_done_tgl = 1'b0;
    reg [127:0] fill = 128'd0;
    wire [31:0] n_hit, n_miss;
    scpu_cache dut (.clk(clk), .flush(flush), .ix(ix), .cq_o(cq), .f_go(f_go), .f_if(f_if), .f_line(f_line), .rd_done(rd_done), .rd_if(rd_if), .rd_ln(rd_ln),
        .wr_go(wr_go), .w_p(w_p), .wr_d(wr_d), .wbusy(wbusy), .flushing_o(flushing),
        .s_req_tgl(s_req_tgl), .s_req_line(s_req_line), .s_done_tgl(s_done_tgl), .fill(fill),
        .sw_req_tgl(sw_req_tgl), .sw_req_addr(sw_req_addr), .sw_req_data(sw_req_data), .sw_done_tgl(sw_done_tgl), .n_hit(n_hit), .n_miss(n_miss));

    reg [7:0] mem [0:4095];
    integer i;
    initial for (i = 0; i < 4096; i = i + 1) mem[i] = 8'h11;
    reg s_l = 1'b0, w_l = 1'b0;
    integer s_cnt = 0, w_cnt = 0;
    reg [20:0] s_ln;
    reg [24:0] w_ad;
    reg [7:0] w_dt;
    always @(posedge clk) begin
        if (s_req_tgl != s_l) begin s_l <= s_req_tgl; s_cnt <= 3; s_ln <= s_req_line; end
        else if (s_cnt > 0) begin
            s_cnt <= s_cnt - 1;
            if (s_cnt == 1) begin fill <= {mem[s_ln*16+15], mem[s_ln*16+14], mem[s_ln*16+13], mem[s_ln*16+12], mem[s_ln*16+11], mem[s_ln*16+10], mem[s_ln*16+9], mem[s_ln*16+8],
                                            mem[s_ln*16+7], mem[s_ln*16+6], mem[s_ln*16+5], mem[s_ln*16+4], mem[s_ln*16+3], mem[s_ln*16+2], mem[s_ln*16+1], mem[s_ln*16]}; s_done_tgl <= ~s_done_tgl; end
        end
        if (sw_req_tgl != w_l) begin w_l <= sw_req_tgl; w_cnt <= 6; w_ad <= sw_req_addr; w_dt <= sw_req_data; end
        else if (w_cnt > 0) begin
            w_cnt <= w_cnt - 1;
            if (w_cnt == 1) begin mem[w_ad] <= w_dt; sw_done_tgl <= ~sw_done_tgl; end
        end
    end

    integer bad = 0;
    task ticks(input integer n); integer k; begin for (k = 0; k < n; k = k + 1) @(negedge clk); end endtask
    task demand(input [20:0] ln, input pf_stream); begin    // one demand miss, program stream or data, then wait until it and the quiet window are over
        @(negedge clk); f_go = 1'b1; f_if = pf_stream; f_line = ln; @(negedge clk); f_go = 1'b0; f_if = 1'b0;
        ticks(2); while (dut.st_wait || dut.quiet != 3'd0) @(negedge clk);
    end endtask
    task pulse_rd(input [20:0] ln); begin rd_ln = ln; rd_if = 1'b1; @(negedge clk); rd_if = 1'b0; end endtask
    task check(input string name, input [20:0] ln); // what the cache holds of line ln equals the DDR2 model
        integer k; reg ok;
        begin
            ix = ln[8:0]; ticks(3);
            ok = 1'b1;
            if (cq[140] && cq[139:128] == ln[20:9]) for (k = 0; k < 16; k = k + 1) if (cq[8 * k +: 8] !== mem[ln * 16 + k]) ok = 1'b0;
            if (!ok) begin bad = bad + 1; $display("FAIL %s: line %0d byte holds stale data (cache %h, DDR2 %h at byte 3 of the written line)", name, ln, cq[127:0], {mem[ln*16+3]}); end
            else $display("ok   %s: line %0d %s", name, ln, (cq[140] && cq[139:128] == ln[20:9]) ? "cached and equal to the DDR2" : "not cached");
        end
    endtask
    task wr(input [24:0] p, input [7:0] d); begin w_p = p; wr_d = d; wr_go = 1'b1; @(negedge clk); wr_go = 1'b0; end endtask

    task race(input string name, input [20:0] la, input in_quiet);
        integer guard;
        begin
            ix = la[8:0] + 9'd1;           // the CPU's address is in the line the write goes to
            if (!in_quiet) begin
                demand(la, 1'b1);
                ticks(3);
                pulse_rd(la);                 // pf_pend in the next clock, nothing holds the prefetch
            end else begin
                @(negedge clk); f_go = 1'b1; f_if = 1'b1; f_line = la; @(negedge clk); f_go = 1'b0; f_if = 1'b0;
                while (!dut.st_wait) @(negedge clk);
                while (dut.st_wait) @(negedge clk);
                pulse_rd(la);                 // the fill has just ended: quiet is running, the prefetch is held back by it
            end
            guard = 0;                        // the first clock in which the prefetch of stream + 1 may start: the write goes into that clock
            while (!(dut.pf_pend && !dut.f_go && !dut.st_wait && !dut.flushing && !dut.wb && dut.quiet == 3'd0) && guard < 40) begin @(negedge clk); guard = guard + 1; end
            if (guard >= 40) begin bad = bad + 1; $display("FAIL %s: the prefetch never became ready", name); end
            w_p = {dut.stream_nx, 4'd3}; wr_d = 8'hEE; wr_go = 1'b1; @(negedge clk); wr_go = 1'b0;
            ticks(120);
            check(name, dut.stream);
        end
    endtask

    initial begin
        ticks(5);
        race("race_a", 21'd5, 1'b0);
        race("race_b", 21'd140, 1'b1);
        // race_c
        demand(21'd90, 1'b0);                 // line 90, cached (data)
        demand(21'd60, 1'b1);                 // program stream line 60
        pulse_rd(21'd60);                     // the prefetch of line 61 starts in the next clock
        while (!(dut.st_wait && dut.pf_cur)) @(negedge clk);
        ix = 9'd61; @(negedge clk);           // the cache read for the first write
        ix = 9'd90; w_p = {21'd61, 4'd1}; wr_d = 8'hCC; wr_go = 1'b1; @(negedge clk);
        w_p = {21'd90, 4'd5}; wr_d = 8'hAB; @(negedge clk); wr_go = 1'b0;
        ticks(120);
        check("race_c line 61", 21'd61);
        ix = 9'd90; ticks(3);
        if (!(cq[140] && cq[40 +: 8] == 8'hAB)) begin bad = bad + 1; $display("FAIL race_c: the byte write to the cached line 90 was lost (byte 5 = %h, want ab)", cq[40 +: 8]); end
        else $display("ok   race_c: the byte write to line 90 during the invalidate kept (byte 5 = ab)");
        if (bad == 0) $display("PASS tb_scpu_cache"); else $display("FAIL tb_scpu_cache: %0d", bad);
        $finish;
    end
endmodule
