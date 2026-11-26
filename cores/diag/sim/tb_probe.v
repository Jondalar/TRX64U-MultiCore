// T260 S1-S4: the diag core's pin bank, engine v2 and capture, through the register bus the application uses (diag_regs.v + probe.v + diag_cap.v wired as mono/layer2_diag.v wires them)
// against a pin model (open-drain pins wired-AND with the outside, pull pins read their drive, the others follow the outside or their drive) and a DDR2 client model (mig_arb's e_*).
//   S1  every pin of the table, held low from outside in turn: the snapshot (SAMPLE words and the LIVE registers) has that pin's bit low at its table index and no other bit moved
//   S2  DRIVE: every drivable pin in each mode it allows (dg_en / dg_o / dg_z, the DRVEN register), refusals (not drivable, class, bad index, guard), release at END, ABORT, a step
//       error, decouple and reset; WRITE_REG; EDGES / TIME_TO_EDGE timing against the TB's own clock counter
//   S3  the pin-table ROM through registers 0A0 / 0C0-0DF, printed as ROM lines the run script compares with tools/diag_pins_gen.py --dump
//   S4  CAPTURE of 4 pins x 4096 samples every clock into the DDR2 model, sample for sample against the TB's own pin history; the reader; partial lines, a second capture, overrun, refusals
// Timing is checked against the TB's own clock counter, never against the engine's counters.
`timescale 1ns / 1ps
module tb_probe;
    `include "diag_pintab.vh"
    localparam [26:0] BASE_W = 27'h3000000;
    reg clk = 0; always #5 clk = ~clk;
    reg ui_clk = 0; always #6.5 ui_clk = ~ui_clk;
    reg rst = 1, ui_rst = 1, dc = 0;
    reg [16:0] cw_addr = 17'd0; reg [7:0] cw_wdata = 8'd0; reg cw_we = 0, cw_re = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq;
    wire px_we, px_re;
    reg [7:0] px_rdata = 8'h5A;
    reg [127:0] ext = {128{1'b1}};              // the outside: bit clear = held low from outside
    integer cyc = 0; always @(posedge clk) cyc <= cyc + 1;

    wire run, abort, go, fclr, st_we, fifo_rd, cap_rst, rd_start;
    wire [7:0] st_addr, pc, ref_pc, ref_cnt; wire [31:0] st_wdata, fifo_q; wire [9:0] fifo_level; wire [127:0] live;
    wire running, waiting, done, ovf, err, refused, gtrip, gnow, ev;
    wire [127:0] dg_en, dg_o, dg_z;
    wire r_we, r_wack; wire [11:0] r_off; wire [7:0] r_wd;
    wire c_go, c_refused, c_sampling, c_idle, c_over, c_stop, c_rdbusy, c_rdvalid;
    wire [23:0] c_k; wire [31:0] c_m, c_count, c_lost; wire [19:0] c_lines; wire [18:0] c_rdix; wire [127:0] c_rddata; wire [223:0] c_sel;
    wire e_en, e_wdf_wren; wire [26:0] e_addr; wire [2:0] e_cmd; wire [127:0] e_wdf_data; wire [15:0] e_wdf_mask;
    wire e_rdy; reg e_rd_valid = 0; reg [127:0] e_rd_data = 128'd0;
    reg calib = 0;

    // ---- the pin model
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
        .build(32'hCAFE0001), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(ev),
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
    diag_cap cap (.clk(clk), .rst(rst), .live(live), .sel(c_sel), .go(c_go), .k(c_k), .m(c_m), .stop(c_stop), .ring_rst(cap_rst),
        .refused(c_refused), .sampling(c_sampling), .idle_all(c_idle), .overrun(c_over), .count(c_count), .lost(c_lost), .lines(c_lines),
        .rd_start(rd_start), .rd_line_ix(c_rdix), .rd_busy(c_rdbusy), .rd_valid(c_rdvalid), .rd_data(c_rddata),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib(calib),
        .e_en(e_en), .e_addr(e_addr), .e_cmd(e_cmd), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .e_rdy(e_rdy), .e_rd_valid(e_rd_valid), .e_rd_data(e_rd_data));

    // ---- the DDR2 client model (mig_arb's engine client: a command is taken in the clock e_rdy is high; a read's data comes later)
    reg [127:0] mem [0:16383];
    reg [127:0] mem_ok [0:0];
    integer wait_n = 0, wcnt = 0, rd_dly = 0, wr_cnt = 0, rd_cnt = 0, bad_cmd = 0;
    reg rd_pend = 0; reg [127:0] rd_buf;
    integer ln;
    assign e_rdy = e_en & calib & (wcnt == 0);        // like mig_arb: the command is taken in the clock e_en and e_rdy are both high
    always @(posedge ui_clk) begin
        e_rd_valid <= 0;
        if (rd_dly > 0) begin rd_dly <= rd_dly - 1; if (rd_dly == 1) begin e_rd_valid <= 1; e_rd_data <= rd_buf; end end
        if (e_en && calib) begin
            if (wcnt > 0) wcnt <= wcnt - 1;
            else begin
                wcnt <= wait_n;
                ln = (e_addr - BASE_W) >> 3;
                if (e_addr[2:0] != 3'd0 || e_addr < BASE_W || ln > 16383) bad_cmd = bad_cmd + 1;
                else if (e_cmd == 3'b000) begin mem[ln] <= e_wdf_data; wr_cnt = wr_cnt + 1; if (e_wdf_mask != 16'd0 || !e_wdf_wren) bad_cmd = bad_cmd + 1; end
                else begin rd_buf <= mem[ln]; rd_dly <= 4; rd_cnt = rd_cnt + 1; end
            end
        end
    end

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
    task step(input [31:0] w);
        begin wr(12'h020, w[7:0]); wr(12'h021, w[15:8]); wr(12'h022, w[23:16]); wr(12'h023, w[31:24]); end
    endtask
    task pop(output [31:0] v); begin rd32(12'h030, v); end endtask
    task status(output [7:0] s); begin rd(12'h011, s); end endtask
    task start; begin wr(12'h024, 8'd0); end endtask
    task runit; begin wr(12'h012, 8'h01); end endtask
    task wait_done; integer n; reg [7:0] s;
        begin n = 0; s = 0; while (!s[2] && n < 4000) begin status(s); n = n + 1; end if (!s[2]) begin errors = errors + 1; $display("FAIL engine did not finish"); end end
    endtask
    task clear; begin wr(12'h012, 8'h08); end endtask
    task settle; begin repeat (8) @(posedge clk); end endtask

    function [31:0] w_drive(input [6:0] pin, input [1:0] mode); w_drive = {4'd1, mode, 19'd0, pin}; endfunction
    function [31:0] w_wait(input [23:0] n); w_wait = {4'd2, 4'd0, n}; endfunction
    localparam [31:0] W_SAMPLE = {4'd3, 28'd0}, W_END = 32'd0, W_USER = {4'd8, 28'd0}, W_CAPWAIT = {4'd11, 28'd0};
    function [31:0] w_edges(input [1:0] e, input [6:0] pin); w_edges = {4'd4, e, 19'd0, pin}; endfunction
    function [31:0] w_tte(input [1:0] e, input [6:0] pin); w_tte = {4'd5, e, 19'd0, pin}; endfunction
    function [31:0] w_loop(input [7:0] kk, input [15:0] c); w_loop = {4'd7, 4'd0, c, kk}; endfunction
    function [31:0] w_wreg(input [11:0] off, input [7:0] d); w_wreg = {4'd9, off, 8'd0, d}; endfunction
    function [31:0] w_cap(input [23:0] kk); w_cap = {4'd10, 4'd0, kk}; endfunction

    reg [7:0] b; reg [31:0] v, v1, v2, v3, v4; reg [127:0] snap;
    // the snapshot of an idle board: every bank pin high, except the `pull` pins (they read their own drive: 1 = pulling), and nothing past the bank
    reg [127:0] base;
    integer bi;
    initial begin base = 128'd0; for (bi = 0; bi < DG_N; bi = bi + 1) base[bi] = (cls_of(bi) != 3'd2); end
    integer i, j, n, t0, t1;

    // ---- the TB's own view of one pin: the clock it went low and high again
    reg [127:0] ext_q = {128{1'b1}};
    task read_snapshot(output [127:0] sn);   // the LIVE registers
        integer q; reg [7:0] bb;
        begin for (q = 0; q < 16; q = q + 1) begin rd(12'h040 + q, bb); sn[8*q +: 8] = bb; end end
    endtask
    task pop_sample(output [31:0] t, output [127:0] sn);
        begin pop(t); pop(v1); pop(v2); pop(v3); pop(v4); sn = {v4, v3, v2, v1}; end
    endtask
    task release_wait; begin settle; end endtask

    // ---- S2 helpers
    function [2:0] cls_of(input [6:0] p); reg [3:0] c; begin c = dgpin_cls(p); cls_of = c[2:0]; end endfunction
    function guard_of(input [6:0] p); reg [3:0] c; begin c = dgpin_cls(p); guard_of = c[3]; end endfunction
    reg [127:0] exp_en, exp_o, exp_z;
    task run_list_done;   // the list is written from step 0 and run; END is the last step
        begin wr(12'h024, 8'd0); end
    endtask
    integer p, m, lo, errs0;
    reg [2:0] cl;
    reg [31:0] tt;

    // ---- S4 pattern: the four lanes toggle with periods 3, 5, 7 and an LFSR bit; the model of the outside is a function of the TB's clock counter
    reg pat_on = 0;
    localparam [6:0] L0 = IX_KBJOY_1_JOY0, L1 = IX_PADDLE_CMP0, L2 = IX_UI_BUTTON1, L3 = IX_BLING_KEYB;
    reg [15:0] lf = 16'hACE1;
    reg [3:0] pv;
    always @(posedge clk) if (pat_on) begin
        lf <= {lf[14:0], lf[15] ^ lf[13] ^ lf[12] ^ lf[10]};
        pv = {lf[0], (cyc / 7) % 2 == 1, (cyc / 5) % 2 == 1, (cyc / 3) % 2 == 1};
        ext[L0] <= pv[0]; ext[L1] <= pv[1]; ext[L2] <= pv[2]; ext[L3] <= pv[3];
    end
    reg [3:0] hist [0:(1<<14)-1];
    always @(posedge clk) hist[cyc & 16383] <= {ext[L3], ext[L2], ext[L1], ext[L0]};   // the outside as it stood in this clock
    integer g_clk;
    always @(posedge clk) if (c_go) g_clk <= cyc;

    reg [127:0] expl;
    reg [31:0] smpw;
    integer first_line, nsamp, bad, tk;

    initial begin
        for (i = 0; i < 16384; i = i + 1) mem[i] = 128'd0;
        repeat (20) @(posedge clk); rst = 0; ui_rst = 0; calib = 1; repeat (5) @(posedge clk);

        // ================= S3: the pin-table ROM
        rd(12'h080, b); check(b == DG_N, "PIN_COUNT = the table's count");
        rd(12'h006, b); check(b == 8'h1F, "DCAPS 0x1F");
        for (i = 0; i < 128; i = i + 1) begin
            wr(12'h0A0, i[7:0]);
            begin : romrd
                reg [8*16-1:0] nm; reg [7:0] c; integer q; reg [7:0] d0, d1, d2, d3, d4;
                nm = 0;
                for (q = 0; q < 16; q = q + 1) begin rd(12'h0C0 + q, c); nm = {nm[8*15-1:0], c}; end
                rd(12'h0D0, d0); rd(12'h0D1, d1); rd(12'h0D2, d2); rd(12'h0D3, d3); rd(12'h0D4, d4);
                if (i < DG_N) begin
                    $write("ROM %0d ", i);
                    for (q = 15; q >= 0; q = q - 1) if (nm[8*q +: 8] != 8'd0) $write("%c", nm[8*q +: 8]);
                    $display(" %0d %0d %0d %0d %0d", d0, d1, d2, d3, d4);
                end else begin
                    check(nm == 0 && d0 == 0 && d1 == 0 && d2 == 0 && d3 == 0 && d4 == 0, "ROM entry past the bank reads 0");
                end
            end
        end
        rd(12'h0DF, b); check(b == 8'd0, "ROM byte 31 is 0");

        // ================= S1: every pin low from outside in turn
        start; step(W_SAMPLE); step(W_END);
        for (i = 0; i < DG_N; i = i + 1) begin
            ext = {128{1'b1}}; ext[i] = 1'b0; settle;
            clear; start; runit; wait_done;
            pop_sample(tt, snap);
            if (snap !== ((base & ~((cls_of(i) == 3'd2) ? 128'd0 : (128'd1 << i)))) && i < 3) $display("  pin %0d: SAMPLE %h t=%0d", i, snap, tt);
            check(snap === ((base & ~((cls_of(i) == 3'd2) ? 128'd0 : (128'd1 << i)))), "S1: the SAMPLE snapshot has exactly pin i low");
            read_snapshot(snap);
            check(snap === ((base & ~((cls_of(i) == 3'd2) ? 128'd0 : (128'd1 << i)))), "S1: the LIVE registers have exactly pin i low");
        end
        ext = {128{1'b1}}; settle;
        // a second pin low together (a bit in another byte): both low, nothing else
        ext[IX_SLOT_ADDR0] = 0; ext[IX_AVID_FB] = 0; settle; read_snapshot(snap);
        check(snap === (base & ~((128'd1 << IX_SLOT_ADDR0) | (128'd1 << IX_AVID_FB))), "S1: two pins low, two bits low");
        ext = {128{1'b1}}; settle;
        // SAMPLE timestamp: two SAMPLEs 100 clocks apart differ by 102 (2 + 100)
        clear; start; step(W_SAMPLE); step(w_wait(24'd100)); step(W_SAMPLE); step(W_END); start; runit; wait_done;
        pop(v1); pop(v2); pop(v2); pop(v2); pop(v2); pop(v3); if (v3 - v1 != 32'd109) $display("  t1=%0d t2=%0d", v1, v3); check(v3 - v1 == 32'd109, "SAMPLE t: SAMPLE(7 clocks of pushes: 2+5) + WAIT(2+100) = 109 between the two");

        // ================= S2: DRIVE, guard, refusal, release
        // every pin in every mode: the list drives, waits, and the TB reads the engine's outputs while it waits for GO
        errs0 = errors;
        for (i = 0; i < DG_N; i = i + 1) begin
            cl = cls_of(i);
            for (m = 0; m < 4; m = m + 1) begin
                // expected: allowed?
                lo = (m == 0) || (cl != 0 && (m == 1 || (m == 2 && cl >= 3) || (m == 3 && cl == 5)));
                clear; start; step(w_drive(i[6:0], m[1:0])); step(w_wait(24'd6)); step(W_USER); step(W_END); runit;
                begin : wt integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
                settle;
                exp_en = 0; exp_o = 0; exp_z = 0;
                if (lo && m != 0) begin
                    if (m == 3) exp_z[i] = 1; else begin exp_en[i] = 1; exp_o[i] = (m == 2) || (cl == 2); end
                end
                check(dg_en === exp_en, "S2: dg_en = only the driven pin");
                check(dg_z === exp_z, "S2: dg_z = only the hi-z pin");
                check((dg_o & exp_en) === exp_o, "S2: dg_o level (pull pins 1 = pulling)");
                rd(12'h070 + (i / 8), b); check(b[i % 8] === exp_en[i], "S2: DRVEN register shows the pin");
                rd(12'h011, b); check(b[5] === (!lo), "S2: refused flag = the mode was not allowed");
                if (!lo) begin rd(12'h017, b); check(b == 8'd0, "S2: REF_PC = the DRIVE step 0"); end
                wr(12'h012, 8'h04); wait_done;          // GO -> WAIT_USER done -> END
                settle;
                check(dg_en === 128'd0 && dg_z === 128'd0, "S2: END releases every output");
            end
        end
        // a pin past the bank
        clear; start; step(w_drive(7'd121, 2'd1)); step(w_drive(7'd127, 2'd0)); step(W_END); runit; wait_done;
        rd(12'h011, b); check(b[5] && !b[4], "S2: pins past the bank are refused (no step error)"); rd(12'h018, b); check(b == 8'd2, "S2: REF_CNT counts both");
        rd(12'h017, b); check(b == 8'd0, "S2: REF_PC = step 0");
        // one allowed drive keeps REF_CNT at 0 and refused clear at RUN
        clear; start; step(w_drive(IX_KBJOY_1_JOY0, 2'd1)); step(W_END); runit; wait_done;
        rd(12'h011, b); check(!b[5], "S2: RUN clears the refused flag"); rd(12'h018, b); check(b == 0, "S2: RUN clears REF_CNT");

        // guard: a cartridge pulls GAME low
        ext[IX_SLOT_GAMEn] = 0; settle;
        rd(12'h081, b); check(b[0], "S2: GUARD register shows GAME low");
        clear; start; step(w_drive(IX_SLOT_ROMLn, 2'd1)); step(w_drive(IX_SLOT_ADDR3, 2'd2)); step(w_drive(IX_KBJOY_1_JOY0, 2'd1)); step(w_drive(IX_SLOT_ADDRDIR, 2'd0)); step(w_wait(24'd10)); step(W_END);
        runit; wait_done; settle;
        check(dg_en === 128'd0 || dg_en === (128'd1 << IX_KBJOY_1_JOY0), "S2: with GAME low only the joystick pin (not a cartridge pin) is driven");
        rd(12'h011, b); check(b[5], "S2: guard refusal flagged"); rd(12'h018, b); check(b == 8'd2, "S2: two guarded drives refused, the release accepted");
        // END released the joystick pin
        ext[IX_SLOT_GAMEn] = 1; settle;
        // guard trip while driven: drive ROML, then EXROM low
        clear; start; step(w_drive(IX_SLOT_ROMLn, 2'd1)); step(w_drive(IX_SLOT_ADDR12, 2'd2)); step(W_USER); step(W_END);
        runit; begin : wg integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
        settle; check(dg_en[IX_SLOT_ROMLn] && dg_en[IX_SLOT_ADDR12], "S2: both cartridge pins driven with the slot empty");
        ext[IX_SLOT_EXROMn] = 0; settle;
        check(!dg_en[IX_SLOT_ROMLn] && !dg_en[IX_SLOT_ADDR12], "S2: a cartridge pulling EXROM low releases the guarded pins");
        rd(12'h011, b); check(b[7], "S2: guard trip flag");
        ext[IX_SLOT_EXROMn] = 1; settle;
        check(!dg_en[IX_SLOT_ROMLn] && !dg_en[IX_SLOT_ADDR12], "S2: they stay released after the cartridge is gone (no auto resume)");
        wr(12'h012, 8'h04); wait_done;

        // release at ABORT, at a step error (reserved op 6), at decouple and at reset; the pins stay driven until then
        clear; start; step(w_drive(IX_KBJOY_2_JOY3, 2'd1)); step(w_drive(IX_PADDLE_DIS, 2'd2)); step(W_USER); step(W_END); runit;
        begin : wa integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
        settle; check(dg_en[IX_KBJOY_2_JOY3] && dg_en[IX_PADDLE_DIS] && dg_o[IX_PADDLE_DIS], "S2: two pins driven before ABORT");
        wr(12'h012, 8'h02); settle; check(dg_en === 128'd0, "S2: ABORT releases every output"); rd(12'h011, b); check(b[2] && !b[0], "S2: ABORT: done, not running");
        clear; start; step(w_drive(IX_KBJOY_2_JOY3, 2'd1)); step(w_wait(24'd4)); step({4'd6, 28'd0}); step(W_END); runit; wait_done; settle;
        rd(12'h011, b); check(b[4], "S2: reserved op 6 is a step error"); check(dg_en === 128'd0, "S2: a step error releases every output");
        clear; start; step(w_drive(IX_C2N_READ, 2'd1)); step(W_USER); step(W_END); runit;
        begin : wd integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
        settle; check(dg_en[IX_C2N_READ], "S2: C2N_READ driven");
        dc = 1; settle; check(dg_en === 128'd0, "S2: decouple releases every output"); rd(12'h011, b); check(b[2], "S2: decouple stops the engine"); dc = 0;
        clear; start; step(w_drive(IX_C2N_READ, 2'd1)); step(W_USER); step(W_END); runit;
        begin : wr2 integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
        settle; check(dg_en[IX_C2N_READ], "S2: driven before the reset");
        rst = 1; repeat (3) @(posedge clk); rst = 0; settle; check(dg_en === 128'd0, "S2: reset releases every output");

        // WRITE_REG: the A/V window is written by the engine, the engine's own registers are refused as an error
        clear; start; step(w_wreg(12'h054, 8'h5C)); step(w_wreg(12'h065, 8'h02)); step(W_END); runit; wait_done;
        rd(12'h054, b); check(b == 8'h5C, "S2: WRITE_REG 054"); rd(12'h065, b); check(b == 8'h02, "S2: WRITE_REG 065");
        clear; start; step(w_wreg(12'h012, 8'h01)); step(W_END); runit; wait_done; rd(12'h011, b); check(b[4], "S2: WRITE_REG outside 050-06F is a step error");
        rd(12'h016, b); check(b == 8'd0, "S2: PC at the bad WRITE_REG");

        // LOOP and WAIT exactness with a DRIVE pulse measured against the TB clock
        clear; start; step(w_drive(IX_KBJOY_1_JOY4, 2'd1)); step(w_wait(24'd50)); step(w_drive(IX_KBJOY_1_JOY4, 2'd0)); step(W_END); runit;
        t0 = -1; t1 = -1;
        begin : pls integer nn; nn = 0; while (t1 < 0 && nn < 4000) begin @(posedge clk); nn = nn + 1; if (t0 < 0 && !dg_in[IX_KBJOY_1_JOY4]) t0 = cyc; if (t0 >= 0 && t1 < 0 && dg_in[IX_KBJOY_1_JOY4]) t1 = cyc; end end
        if (t1 - t0 != 54) $display("  pulse %0d .. %0d", t0, t1); check(t0 > 0 && t1 - t0 == 54, "S2: DRIVE low .. WAIT 50 .. release is 54 clocks at the pin (DRIVE 2 + WAIT 2 + 50)");
        wait_done;

        // EDGES (any, rising, falling) of a pin the TB toggles every 10 clocks for 100 clocks: 10 edges, 5 of each
        ext[IX_UI_BUTTON0] = 1; settle;
        clear; start; step(w_edges(2'd0, IX_UI_BUTTON0)); step(32'd100); step(w_edges(2'd1, IX_UI_BUTTON0)); step(32'd100); step(w_edges(2'd2, IX_UI_BUTTON0)); step(32'd100); step(W_END);
        runit;
        begin : tg integer nn; nn = 0; while (!done && nn < 3000) begin @(posedge clk); nn = nn + 1; if (running && (cyc % 10) == 0) ext[IX_UI_BUTTON0] = ~ext[IX_UI_BUTTON0]; end end
        wait_done; pop(v1); pop(v2); pop(v3);
        check(v1 >= 9 && v1 <= 11, "EDGES any: about 10 edges in 100 clocks"); check(v2 >= 4 && v2 <= 6, "EDGES rising: about 5"); check(v3 >= 4 && v3 <= 6, "EDGES falling: about 5");
        check(v1 >= v2 + v3 - 1 && v1 <= v2 + v3 + 1, "EDGES: any = rising + falling (the three runs are not synchronous, so +-1)");
        // EDGES exact: a pin driven low by the engine once: exactly one falling edge, no rising, one any
        ext = {128{1'b1}}; settle;
        clear; start; step(w_edges(2'd2, IX_KBJOY_1_JOY1)); step(32'd40); step(W_END);   // nothing moves: 0
        runit; wait_done; pop(v1); check(v1 == 0, "EDGES on a quiet pin counts 0");
        clear; start; step(w_drive(IX_KBJOY_1_JOY1, 2'd1)); step(w_edges(2'd2, IX_KBJOY_1_JOY1)); step(32'd40); step(W_END); runit; wait_done; pop(v1);
        check(v1 == 1, "EDGES falling after an engine DRIVE low: exactly 1 (the drive shows 3 clocks after its execute clock, inside the window)");
        clear; start; step(w_edges(2'd1, IX_KBJOY_1_JOY1)); step(32'd0); step(W_END); runit; wait_done; pop(v1); check(v1 == 0, "EDGES with N = 0 pushes 0");
        clear; start; step(w_edges(2'd0, 7'd125)); step(32'd10); step(W_END); runit; wait_done; pop(v1); check(v1 == 32'hFFFFFFFF, "EDGES on a pin past the bank pushes 0xFFFFFFFF");
        rd(12'h011, b); check(b[5] && !b[4], "EDGES on a bad pin is refused, not an error");

        // TIME_TO_EDGE: PADDLE_DIS (pin 67, pz) driven low, the TB's comparator CMP0 (pin 63) rises K clocks after it
        begin : ttep integer kq, nn; reg [31:0] rr;
            kq = 37;
            ext = {128{1'b1}}; ext[IX_PADDLE_CMP0] = 0; settle;
            clear; start; step(w_drive(IX_PADDLE_DIS, 2'd1)); step(w_tte(2'd1, IX_PADDLE_CMP0)); step(32'd500); step(W_END); runit;
            nn = 0; t0 = -1;
            while (!done && nn < 3000) begin @(posedge clk); nn = nn + 1; if (t0 < 0 && dg_en[IX_PADDLE_DIS]) begin t0 = cyc; end if (t0 > 0 && cyc == t0 + kq) ext[IX_PADDLE_CMP0] = 1; end
            wait_done; pop(rr);
            check(rr[31] == 0, "TTE: no timeout"); check(rr[30:0] > 25 && rr[30:0] < 60, "TTE rising: the time is about the TB's");
            // timeout
            ext[IX_PADDLE_CMP0] = 0; settle; clear; start; step(w_tte(2'd1, IX_PADDLE_CMP0)); step(32'd200); step(W_END); runit; wait_done; pop(rr);
            check(rr == {1'b1, 31'd200}, "TTE timeout: {1, timeout}");
            // an edge of the wrong sense does not count: falling expected, the pin rises
            ext[IX_PADDLE_CMP0] = 0; settle; clear; start; step(w_tte(2'd2, IX_PADDLE_CMP0)); step(32'd100); step(W_END); runit;
            nn = 0; while (!running && nn < 100) begin @(posedge clk); nn = nn + 1; end
            repeat (20) @(posedge clk); ext[IX_PADDLE_CMP0] = 1; wait_done; pop(rr); check(rr[31] == 1, "TTE falling: a rising edge times out");
        end

        // LOOP: a SAMPLE five times
        ext = {128{1'b1}}; settle; clear; start; step(w_loop(8'd1, 16'd5)); step(W_SAMPLE); step(W_END); runit; wait_done;
        rd(12'h034, b); check(b == 8'd25, "LOOP 5 x SAMPLE: 25 words");
        // FIFO overflow: 103 SAMPLEs push 515 words, 512 fit
        clear; start; step(w_loop(8'd1, 16'd103)); step(W_SAMPLE); step(W_END); runit; wait_done;
        rd(12'h011, b); check(b[3], "FIFO overflow flagged"); rd(12'h034, b); check(b == 8'd0, "FIFO level 512 reads 0x200 low byte 0"); rd(12'h035, b); check(b[1:0] == 2'd2, "FIFO level 512");
        clear; rd(12'h011, b); check(!b[3], "CLEAR clears the overflow");

        // ================= S4: capture
        for (i = 0; i < 32; i = i + 1) wr(12'h100 + i, 8'h7F);
        wr(12'h100, L0); wr(12'h101, L1); wr(12'h102, L2); wr(12'h103, L3);
        wr(12'h012, 8'h10);     // CAP RESET
        ext = {128{1'b1}}; settle;
        pat_on = 1; repeat (4) @(posedge clk);
        wait_n = 0;
        clear; start; step(w_cap(24'd1)); step(32'd4096); step(W_CAPWAIT); step(W_END); runit; wait_done;
        pat_on = 0;
        // wait until every line is in the model
        begin : wc integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 400) begin rd(12'h120, s); nn = nn + 1; end end
        rd(12'h120, b); check(b[1] && !b[0] && !b[2], "S4: capture ended, all lines written, no overrun");
        rd32(12'h124, v); check(v == 32'd4096, "S4: CAP_COUNT 4096"); rd32(12'h128, v); check(v == 0, "S4: CAP_LOST 0"); rd32(12'h12C, v); check(v == 32'd1024, "S4: CAP_LINES 1024");
        check(wr_cnt == 1024 && bad_cmd == 0, "S4: 1024 line writes, all aligned and unmasked");
        bad = 0; first_line = 0;
        for (i = 0; i < 4096; i = i + 1) begin
            smpw = mem[i / 4][32 * (i % 4) +: 32];
            // sample i is the outside as it stood 2 clocks before its tick clock (the 2 FF synchroniser); tick clock = g_clk + 1 + i
            expl = hist[(g_clk + 1 + i - 2) & 16383];
            if (smpw !== {28'd0, expl[3:0]}) begin bad = bad + 1; if (bad < 6) $display("  sample %0d: ring %h expected %h", i, smpw, expl[3:0]); end
        end
        check(bad == 0, "S4: 4 pins x 4096 samples, sample for sample, against the TB's own history");
        // the register reader gives the same lines
        wr(12'h130, 8'h00); wr(12'h131, 8'h00); wr(12'h132, 8'h00);
        bad = 0;
        for (i = 0; i < 6; i = i + 1) begin
            wr(12'h134, 8'h01);
            begin : rw integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[4] && nn < 100) begin rd(12'h120, s); nn = nn + 1; end end
            for (j = 0; j < 16; j = j + 1) begin rd(12'h140 + j, b); if (b !== mem[i][8 * j +: 8]) bad = bad + 1; end
        end
        check(bad == 0, "S4: the register reader returns lines 0-5 byte for byte");
        rd(12'h130, b); check(b == 8'h60, "S4: CAP_RADDR advanced by 16 per line read (6 lines = 0x60)");
        // a partial line, a second capture behind the first, 7 lanes of 32 (lane 31 = pin 119, a read-only pin), a slow DDR2 gives an overrun
        for (i = 0; i < 32; i = i + 1) wr(12'h100 + i, 8'h7F);
        wr(12'h100, IX_SLOT_ADDR0); wr(12'h11F, IX_AVID_FB); wr(12'h105, IX_AVID_R2);
        ext = {128{1'b1}}; ext[IX_SLOT_ADDR0] = 0; ext[IX_AVID_FB] = 0; settle;
        clear; start; step(w_cap(24'd3)); step(32'd10); step(W_CAPWAIT); step(W_END); runit; wait_done;
        begin : wc2 integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 400) begin rd(12'h120, s); nn = nn + 1; end end
        rd32(12'h12C, v); check(v == 32'd1027, "S4: the second capture of 10 samples took 3 lines (a partial line is stored)");
        rd32(12'h124, v); check(v == 32'd4106, "S4: CAP_COUNT adds up");
        smpw = mem[1024][31:0]; check(smpw === 32'h00000020, "S4: lanes 0 and 31 low (held low), lane 5 high: 0x00000020");
        check(mem[1026][127:64] === 64'd0 && mem[1026][63:32] !== 32'd0 || mem[1026][127:64] === 64'd0, "S4: the partial line's unused slots (2, 3) are zero");
        smpw = mem[1026][31:0]; check(smpw === 32'h00000020, "S4: sample 8 of the second capture (line 2, slot 0)");
        // refusals: K = 0, M = 0, M too big, a second CAPTURE while sampling
        clear; start; step(w_cap(24'd0)); step(32'd10); step(W_END); runit; wait_done; rd(12'h011, b); check(b[5], "S4: CAPTURE with K = 0 is refused");
        clear; start; step(w_cap(24'd1)); step(32'd0); step(W_END); runit; wait_done; rd(12'h011, b); check(b[5], "S4: CAPTURE with M = 0 is refused");
        clear; start; step(w_cap(24'd1)); step(32'd2097153); step(W_END); runit; wait_done; rd(12'h011, b); check(b[5], "S4: CAPTURE with M > 2^21 is refused");
        clear; start; step(w_cap(24'd100)); step(32'd50); step(w_cap(24'd1)); step(32'd10); step(W_CAPWAIT); step(W_END); runit; wait_done; rd(12'h011, b);
        check(b[5] && !b[4], "S4: a CAPTURE while one is sampling is refused"); rd(12'h018, b); check(b == 8'd1, "S4: exactly that one was refused");
        rd32(12'h124, v); check(v == 32'd4106 + 32'd50, "S4: the first of the two ran to its 50 samples");
        // ABORT stops a capture
        clear; start; step(w_cap(24'd1000)); step(32'd1000); step(W_USER); step(W_END); runit;
        begin : wab integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 200) begin status(s); nn = nn + 1; end end
        repeat (3000) @(posedge clk); wr(12'h012, 8'h02); repeat (200) @(posedge clk);
        rd(12'h120, b); check(!b[0], "S4: ABORT ends the capture"); rd32(12'h124, v); check(v > 4156 + 1 && v < 4156 + 60, "S4: the aborted capture took the samples of the time it ran (3000 clocks / 1000)");
        // END does not stop a capture
        wr(12'h012, 8'h10);
        clear; start; step(w_cap(24'd200)); step(32'd30); step(W_END); runit; wait_done;
        rd(12'h120, b); check(b[0], "S4: END leaves the capture sampling"); begin : wend integer nn; nn = 0; rd(12'h120, b); while (b[0] && nn < 3000) begin rd(12'h120, b); nn = nn + 1; end end
        rd32(12'h124, v); check(v == 32'd30, "S4: CAP RESET zeroed the count, then 30 samples");
        // overrun: the DDR2 takes 400 ui clocks per command, K = 1, 64 samples = 16 lines on 4 buffers
        wr(12'h012, 8'h10); wait_n = 400; wr(12'h100, L0);
        clear; start; step(w_cap(24'd1)); step(32'd64); step(W_CAPWAIT); step(W_END); runit; wait_done;
        rd(12'h120, b); check(b[2], "S4: a slow DDR2 gives an overrun"); rd32(12'h128, v); check(v > 0, "S4: CAP_LOST counts the dropped samples");
        wait_n = 0;
        begin : wl integer nn; reg [7:0] s; nn = 0; s = 0; while (!s[1] && nn < 4000) begin rd(12'h120, s); nn = nn + 1; end end
        rd(12'h120, b); check(b[1], "S4: the lines that were not dropped reach the DDR2 in the end");

        if (errors == 0) $display("PASS tb_probe errors=0 checks=%0d", checks); else $display("FAIL tb_probe errors=%0d checks=%0d", errors, checks);
        $finish;
    end
    initial begin #400000000; $display("FAIL tb_probe timeout"); $finish; end
endmodule
