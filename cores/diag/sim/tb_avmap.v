// T259 S1, S2, S3: the channel map, the channel probe, the RGB mode and the SYNCn / FB knobs of the diag core (diag_regs.v 060-065, diag_avout.v), written through the register bus like the
// application does, measured on the ball vectors. Each unit is one diag_regs + diag_avout with its own knob values (parameters), run for 22 lines (a line is 504 dots of four clocks); the ball vectors AVID_R / G / B [9:2] are
// made with the three assign lines of cores/diag/mono/top.v (copied: the ball table is the spec's, H1 reads the names). From the balls a unit decodes the three channel words (names: R, G, B
// = the vectors, MSB = bit 9; scan groups: ga, gb, gc) and checks, by MODE:
//   0 the test picture: the slot words (channel words, bit order undone) are the encoder's words (y_w, c_w, cv_w, one clock later) in the order PERM, whatever the map
//   1 channel probe: on the channel under test one line is the run list sync tip, blanking, bars 0..7 (only bit k set, 128 clocks each), all bits, blanking; the other two channels are
//     blanking (77) at every clock of the two lines
//   2 RGB, RCH 0: the first bar line is white, yellow, cyan, green, magenta, red, blue (200 clocks each), the three words per slot as PERM and REV say; RCH 1..3: the word is a ramp
//     from 0 to 255 over the window, never falling, the other two words 0
// and in every unit the pins: SYNCn and FB follow the knob (a constant, or the encoder's csync / window delayed to the clock of the words: 9 clocks in mode 0, 4 in modes 1 and 2).
`timescale 1ns / 1ps
module avm_unit #(parameter MAP = 0, MODE = 0, PCH = 0, RCH = 0, PERM = 5, REV = 0, SYNM = 0, FBM = 0, WHITE = 16) (input clk, output reg fin, output reg [31:0] errs, output reg [31:0] chk);
    reg we = 0;
    reg [11:0] a = 0;
    reg [7:0] d = 0;
    wire [92:0] cfg;
    wire [7:0] ga, gb, gc;
    wire syn_n, fb;
    diag_regs r (.clk(clk), .we(we), .re(1'b0), .addr({5'd0, a}), .wdata(d), .rdata(), .ack(), .irq(), .build(32'd0), .px_we(), .px_re(), .px_rdata(8'd0),
        .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(1'b0), .run(), .abort(), .go(), .fifo_clr(), .st_we(), .st_addr(), .st_wdata(), .status(8'd0), .pc(8'd0), .ref_pc(8'd0), .ref_cnt(8'd0),
        .fifo_q(32'd0), .fifo_level(10'd0), .fifo_rd(), .live(128'd0), .drv_en(128'd0), .guard_now(1'b0), .cap_rst(), .e_we(1'b0), .e_off(12'd0), .e_wd(8'd0), .e_wack(), .cap_sel(), .cap_st(8'd0), .cap_count(32'd0), .cap_lost(32'd0), .cap_lines(32'd0), .rd_start(), .rd_line_ix(), .rd_valid_i(1'b0), .rd_data(128'd0), .av_cfg(cfg));
    diag_avout o (.clk(clk), .av_cfg(cfg), .ga(ga), .gb(gb), .gc(gc), .syn_n(syn_n), .fb(fb));
    // the balls, as mono/top.v
    wire [9:2] R, G, B;
    assign {G[9], R[9], G[7], G[6], G[4], G[3], B[9], B[6]} = ga;
    assign {R[4], R[3], B[8], B[7], B[5], B[4], B[3], B[2]} = gb;
    assign {R[8], R[7], R[6], R[5], R[2], G[8], G[5], G[2]} = gc;
    wire [7:0] ch0 = MAP ? R : ga, ch1 = MAP ? G : gb, ch2 = MAP ? B : gc;
    task w(input [11:0] ad, input [7:0] dt);
        begin @(posedge clk); a <= ad; d <= dt; we <= 1; @(posedge clk); we <= 0; end
    endtask
    initial begin
        fin = 0; errs = 0; chk = 0;
        repeat (20) @(posedge clk);
        w(12'h051, PERM); w(12'h052, REV); w(12'h05C, WHITE);
        w(12'h060, MAP); w(12'h062, PCH); w(12'h063, RCH); w(12'h064, SYNM); w(12'h065, FBM); w(12'h061, MODE);
    end
    task bad(input [8*60-1:0] m, input integer x, input integer y);
        begin errs = errs + 1; if (errs < 6) $display("FAIL unit map %0d mode %0d pch %0d rch %0d perm %0d rev %0d syn %0d fb %0d: %0s %0d %0d", MAP, MODE, PCH, RCH, PERM, REV, SYNM, FBM, m, x, y); end
    endtask
    function [5:0] who_f(input [2:0] p);
        case (p) 0: who_f = {2'd0, 2'd1, 2'd2}; 1: who_f = {2'd0, 2'd2, 2'd1}; 2: who_f = {2'd1, 2'd0, 2'd2}; 3: who_f = {2'd1, 2'd2, 2'd0}; 4: who_f = {2'd2, 2'd0, 2'd1}; default: who_f = {2'd2, 2'd1, 2'd0}; endcase
    endfunction
    function [7:0] brv(input [7:0] x); brv = {x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]}; endfunction
    function [7:0] sel3(input [1:0] k, input [7:0] x0, x1, x2); sel3 = (k == 0) ? x0 : (k == 1) ? x1 : x2; endfunction
    function [23:0] pal24(input [3:0] i);
        case (i) 0: pal24 = 24'h000000; 1: pal24 = 24'hFFFFFF; 2: pal24 = 24'h813338; 3: pal24 = 24'h75CEC8; 4: pal24 = 24'h8E3C97; 5: pal24 = 24'h56AC4D; 6: pal24 = 24'h2E2C9B;
        7: pal24 = 24'hEDF171; default: pal24 = 24'h000000; endcase
    endfunction
    wire [5:0] who = who_f(PERM);
    wire [7:0] s0 = REV[0] ? brv(ch0) : ch0, s1 = REV[1] ? brv(ch1) : ch1, s2 = REV[2] ? brv(ch2) : ch2;      // the slot words, bit order undone
    // expected pin delays
    localparam DL = (MODE == 1 || MODE == 2) ? 4 : 9;
    reg [15:0] csd = 0, ded = 0;
    always @(posedge clk) begin csd <= {csd[14:0], o.cs_r}; ded <= {ded[14:0], o.de_r}; end
    wire exp_syn = (SYNM == 0) ? 1'b1 : (SYNM == 1) ? 1'b0 : (SYNM == 2) ? ~csd[DL-1] : csd[DL-1];
    wire exp_fb  = (FBM == 0) ? 1'b0 : (FBM == 1) ? 1'b1 : (FBM == 2) ? ded[DL-1] : ~ded[DL-1];
    reg [7:0] py = 0, pc = 0, pcv = 0;
    integer n = 0;
    // the capture of two lines from line 20
    reg cap = 0;
    integer cn = 0, nr = 0, i, j, ok;
    reg [23:0] rv [0:255];
    integer rc [0:255];
    reg [23:0] cur;
    integer bad_other = 0, ramp_fall = 0, ramp_max = 0, ramp_last = 0, ramp_oth = 0;
    reg [7:0] tw;
    wire [7:0] testw = (PCH == 0) ? ch0 : (PCH == 1) ? ch1 : ch2;
    reg [7:0] tr [0:2];                                    // the words R, G, B (RGB mode): the slot words by who
    always @(*) begin tr[who[5:4]] = s0; tr[who[3:2]] = s1; tr[who[1:0]] = s2; end
    always @(posedge clk) begin
        n <= n + 1;
        py <= o.y_w; pc <= o.c_w; pcv <= o.cv_w;
        if (n > 200) begin
            chk = chk + 2;
            if (syn_n !== exp_syn) bad("SYNCn", syn_n, exp_syn);
            if (fb !== exp_fb) bad("FB", fb, exp_fb);
            if (MODE == 0) begin
                chk = chk + 1;
                if (s0 !== sel3(who[5:4], py, pc, pcv) || s1 !== sel3(who[3:2], py, pc, pcv) || s2 !== sel3(who[1:0], py, pc, pcv)) bad("words", s0, sel3(who[5:4], py, pc, pcv));
            end
            if (MODE == 1 && SYNM == 2) begin chk = chk + 1; if (syn_n !== (testw != 8'd0)) bad("sync vs word", syn_n, testw); end
            if (MODE == 1 && FBM == 2) begin chk = chk + 1; if (fb !== (testw != 8'd0 && testw != 8'd77)) bad("window vs word", fb, testw); end
        end
        if (!cap && !fin && o.ly == 9'd20 && o.hx == 10'd0 && o.dv == 2'd0 && n > 1000) begin cap <= 1; cn <= 0; nr = 0; end
        if (cap) begin
            cn <= cn + 1;
            case (MODE)
            1: cur = {16'd0, testw};
            2: cur = {tr[0], tr[1], tr[2]};
            default: cur = 24'd0;
            endcase
            if (nr == 0 || cur !== rv[nr - 1]) begin rv[nr] = cur; rc[nr] = 1; nr = nr + 1; end
            else rc[nr - 1] = rc[nr - 1] + 1;
            if (MODE == 1) begin                         // the other two channels carry blanking only
                chk = chk + 1;
                if (PCH != 0 && ch0 !== 8'd77) bad_other = bad_other + 1;
                if (PCH != 1 && ch1 !== 8'd77) bad_other = bad_other + 1;
                if (PCH != 2 && ch2 !== 8'd77) bad_other = bad_other + 1;
            end
            if (MODE == 2 && RCH != 0) begin
                chk = chk + 1;
                tw = tr[RCH - 1];
                if (tw < ramp_last && !(ramp_last >= 250 && tw == 0)) ramp_fall = ramp_fall + 1;    // only the fall at the window's end
                ramp_last = tw;
                if (tw > ramp_max) ramp_max = tw;
                if (tr[0] != 0 && RCH != 1) ramp_oth = ramp_oth + 1;
                if (tr[1] != 0 && RCH != 2) ramp_oth = ramp_oth + 1;
                if (tr[2] != 0 && RCH != 3) ramp_oth = ramp_oth + 1;
            end
            if (cn >= 4100) begin
                cap <= 0; fin <= 1;
                if (MODE == 1) begin
                    if (bad_other != 0) bad("another channel is not blanking", bad_other, 0);
                    ok = 0;
                    for (i = 0; i + 11 < nr; i = i + 1)
                        if (rv[i] == 0 && rv[i+1] == 77 && rv[i+2] == 1 && rv[i+3] == 2 && rv[i+4] == 4 && rv[i+5] == 8 && rv[i+6] == 16 && rv[i+7] == 32 && rv[i+8] == 64 && rv[i+9] == 128 &&
                            rv[i+10] == 255 && rv[i+11] == 77) begin
                            ok = 1;
                            for (j = 2; j <= 9; j = j + 1) if (rc[i+j] != 128) bad("bar width", j - 2, rc[i+j]);
                            if (rc[i+10] != 576) bad("all-bits bar width", rc[i+10], 576);
                            if (rc[i] != 148) bad("sync tip width", rc[i], 148);
                        end
                    chk = chk + 1;
                    if (!ok) begin bad("no run list sync 77 bars 255 77", nr, 0); for (i = 0; i < nr && i < 20; i = i + 1) $display("  run %0d: %0d x %0d", i, rv[i], rc[i]); end
                end
                if (MODE == 2 && RCH == 0) begin
                    ok = 0;
                    for (i = 0; i + 7 < nr; i = i + 1)
                        if (rv[i] == 24'h000000 && rv[i+1] == 24'hFFFFFF && rv[i+2] == pal24(7) && rv[i+3] == pal24(3) && rv[i+4] == pal24(5) && rv[i+5] == pal24(4) &&
                            rv[i+6] == pal24(2) && rv[i+7] == pal24(6)) begin
                            ok = 1;
                            for (j = 1; j <= 7; j = j + 1) if (rc[i+j] != 200) bad("colour bar width", j, rc[i+j]);
                        end
                    chk = chk + 1;
                    if (!ok) begin bad("no run list black white yellow cyan green magenta red blue", nr, 0); for (i = 0; i < nr && i < 20; i = i + 1) $display("  run %0d: %h x %0d", i, rv[i], rc[i]); end
                end
                if (MODE == 2 && RCH != 0) begin
                    chk = chk + 1;
                    if (ramp_fall != 0) bad("ramp falls", ramp_fall, 0);
                    if (ramp_max < 250) bad("ramp stays low", ramp_max, 250);
                    if (ramp_oth != 0) bad("another word is lit", ramp_oth, 0);
                end
            end
        end
    end
endmodule

module tb_avmap;
    reg clk = 0;
    always #5 clk = ~clk;
    localparam NU = 40;
    wire [NU-1:0] fin;
    wire [31:0] errs [0:NU-1];
    wire [31:0] chk [0:NU-1];
    // S1: the probe, both maps, three channels; with sync and window pins against the word
    avm_unit #(.MAP(1), .MODE(1), .PCH(0)) u0 (clk, fin[0], errs[0], chk[0]);
    avm_unit #(.MAP(1), .MODE(1), .PCH(1)) u1 (clk, fin[1], errs[1], chk[1]);
    avm_unit #(.MAP(1), .MODE(1), .PCH(2)) u2 (clk, fin[2], errs[2], chk[2]);
    avm_unit #(.MAP(0), .MODE(1), .PCH(0)) u3 (clk, fin[3], errs[3], chk[3]);
    avm_unit #(.MAP(0), .MODE(1), .PCH(1)) u4 (clk, fin[4], errs[4], chk[4]);
    avm_unit #(.MAP(0), .MODE(1), .PCH(2)) u5 (clk, fin[5], errs[5], chk[5]);
    avm_unit #(.MAP(1), .MODE(1), .PCH(1), .SYNM(2), .FBM(2)) u6 (clk, fin[6], errs[6], chk[6]);
    avm_unit #(.MAP(0), .MODE(1), .PCH(2), .SYNM(3), .FBM(3)) u7 (clk, fin[7], errs[7], chk[7]);
    // the test picture: names with H1's order (perm 4 = CV Y C), bit orders, scan groups with the C128's order
    avm_unit #(.MAP(1), .MODE(0), .PERM(4), .WHITE(11)) u8 (clk, fin[8], errs[8], chk[8]);
    avm_unit #(.MAP(1), .MODE(0), .PERM(2), .REV(3'b101), .WHITE(11)) u9 (clk, fin[9], errs[9], chk[9]);
    avm_unit #(.MAP(0), .MODE(0), .PERM(5), .WHITE(11)) u10 (clk, fin[10], errs[10], chk[10]);
    // S2: RGB, six permutations on the names map, bit orders, scan groups, one-word ramps
    avm_unit #(.MAP(1), .MODE(2), .PERM(0)) u11 (clk, fin[11], errs[11], chk[11]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(1)) u12 (clk, fin[12], errs[12], chk[12]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(2)) u13 (clk, fin[13], errs[13], chk[13]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(3)) u14 (clk, fin[14], errs[14], chk[14]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(4)) u15 (clk, fin[15], errs[15], chk[15]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(5)) u16 (clk, fin[16], errs[16], chk[16]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(0), .REV(3'b001)) u17 (clk, fin[17], errs[17], chk[17]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(0), .REV(3'b010)) u18 (clk, fin[18], errs[18], chk[18]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(3), .REV(3'b100)) u19 (clk, fin[19], errs[19], chk[19]);
    avm_unit #(.MAP(0), .MODE(2), .PERM(0)) u20 (clk, fin[20], errs[20], chk[20]);
    avm_unit #(.MAP(0), .MODE(2), .PERM(4), .REV(3'b111)) u21 (clk, fin[21], errs[21], chk[21]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(0), .RCH(1)) u22 (clk, fin[22], errs[22], chk[22]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(0), .RCH(2)) u23 (clk, fin[23], errs[23], chk[23]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(0), .RCH(3)) u24 (clk, fin[24], errs[24], chk[24]);
    avm_unit #(.MAP(1), .MODE(2), .PERM(3), .RCH(1)) u25 (clk, fin[25], errs[25], chk[25]);
    // S3: SYNCn and FB in the test picture (mode 0), RGB and the probe
    avm_unit #(.MODE(0), .SYNM(0), .FBM(0)) u26 (clk, fin[26], errs[26], chk[26]);
    avm_unit #(.MODE(0), .SYNM(1), .FBM(1)) u27 (clk, fin[27], errs[27], chk[27]);
    avm_unit #(.MODE(0), .SYNM(2), .FBM(2)) u28 (clk, fin[28], errs[28], chk[28]);
    avm_unit #(.MODE(0), .SYNM(3), .FBM(3)) u29 (clk, fin[29], errs[29], chk[29]);
    avm_unit #(.MODE(0), .SYNM(2), .FBM(3)) u30 (clk, fin[30], errs[30], chk[30]);
    avm_unit #(.MODE(0), .SYNM(3), .FBM(2)) u31 (clk, fin[31], errs[31], chk[31]);
    avm_unit #(.MODE(2), .SYNM(2), .FBM(2)) u32 (clk, fin[32], errs[32], chk[32]);
    avm_unit #(.MODE(2), .SYNM(3), .FBM(1)) u33 (clk, fin[33], errs[33], chk[33]);
    avm_unit #(.MODE(2), .SYNM(1), .FBM(3)) u34 (clk, fin[34], errs[34], chk[34]);
    avm_unit #(.MODE(1), .PCH(0), .SYNM(2), .FBM(0)) u35 (clk, fin[35], errs[35], chk[35]);
    avm_unit #(.MODE(1), .PCH(0), .SYNM(1), .FBM(1)) u36 (clk, fin[36], errs[36], chk[36]);
    avm_unit #(.MODE(0), .SYNM(0), .FBM(1)) u37 (clk, fin[37], errs[37], chk[37]);
    avm_unit #(.MODE(0), .SYNM(1), .FBM(0)) u38 (clk, fin[38], errs[38], chk[38]);
    avm_unit #(.MODE(0), .SYNM(2), .FBM(0), .PERM(5)) u39 (clk, fin[39], errs[39], chk[39]);
    integer i, e, c;
    initial begin
        wait (&fin);
        repeat (10) @(posedge clk);
        e = 0; c = 0;
        for (i = 0; i < NU; i = i + 1) begin e = e + errs[i]; c = c + chk[i]; end
        if (e == 0 && c > 100000) $display("PASS tb_avmap errors=0 checks=%0d units=%0d", c, NU);
        else $display("FAIL tb_avmap errors=%0d checks=%0d", e, c);
        $finish;
    end
    initial begin #2000000000; $display("FAIL tb_avmap timeout fin=%b", fin); $finish; end
endmodule
