// SPDX-License-Identifier: GPL-3.0-or-later
// U16: TRX64U-MultiCore's host/sim/tmds_rep/tb_tmds_rep.v (T164 S3 there), unchanged below this line. ci/sim_iv.sh tb_tmds_rep.v
// T164 S3: host/tmds_rep.v. The GTP words (usrclk) are unpacked into the serial line bits (bit 0 of a word is sent first, the older word first) and
// compared with the pixel symbol stream at 1/gear rate: gear 1, 2, 3, 5, each after a reset that changes the gear, with random symbols on the
// three data lanes and the constant clock symbol on the fourth. Per lane and gear: every line bit of every pixel against the symbol (bit j / gear),
// the middle line bit of every group of `gear` (the decode of the spec) against the symbol, and the symbol never twice in a row.
`timescale 1ns/1ps
module tb;
    parameter NP   = 3000;          // pixels per gear
    parameter LATB = 30;            // line bits from the start of a pixel to its first bit in the captured stream: 3 pix_clk (the bench reads the word register one usrclk later)
    localparam MAXB = NP * 50 + 4000;

    reg clk = 1'b0, usr = 1'b0, rst = 1'b1;
    reg [3:0] gear = 4'd1;
    always begin
        #3.35;                      // 149 MHz
        clk <= ~clk;
        if (!clk) usr <= ~usr;      // rises with every second rising edge of clk, assigned in the same NBA step as clk
    end

    reg [9:0] h0 [0:NP+1], h1 [0:NP+1], h2 [0:NP+1];
    reg [9:0] sym0, sym1, sym2;
    reg [3:0] tph = 4'd0;
    integer   pi = 0;
    wire ce = !rst && (tph == gear - 4'd1);
    always @(posedge clk)
        if (rst) begin tph <= 4'd0; pi <= 0; sym0 <= h0[0]; sym1 <= h1[0]; sym2 <= h2[0]; end
        else if (tph == gear - 4'd1) begin
            tph <= 4'd0; pi <= pi + 1;
            sym0 <= h0[pi + 1]; sym1 <= h1[pi + 1]; sym2 <= h2[pi + 1];
        end else tph <= tph + 4'd1;

    wire [19:0] w0, w1, w2, wc;
    tmds_rep dut (.clk(clk), .usrclk(usr), .rst(rst), .ce(ce), .gear(gear), .sym0(sym0), .sym1(sym1), .sym2(sym2),
                  .w0(w0), .w1(w1), .w2(w2), .wc(wc));

    reg s0 [0:MAXB-1], s1 [0:MAXB-1], s2 [0:MAXB-1], sc [0:MAXB-1];
    integer wn = 0;
    reg cap = 1'b0;
    integer b;
    always @(posedge usr)
        if (cap) begin
            for (b = 0; b < 20; b = b + 1) begin
                s0[wn * 20 + b] = w0[b]; s1[wn * 20 + b] = w1[b]; s2[wn * 20 + b] = w2[b]; sc[wn * 20 + b] = wc[b];
            end
            wn = wn + 1;
        end

    integer errors = 0, i, j, g, lane, gi, nchk, bad, mid_bad, dup_bad;
    reg [9:0] want, mid;
    reg [9:0] cs;
    reg bit_got;
    task check_lane(input integer ln, input integer gg, input integer npx);
        begin
            bad = 0; mid_bad = 0; dup_bad = 0;
            for (i = 0; i < npx; i = i + 1) begin
                case (ln) 0: want = h0[i]; 1: want = h1[i]; 2: want = h2[i]; default: want = 10'b00000_11111; endcase
                for (j = 0; j < 10 * gg; j = j + 1) begin
                    case (ln) 0: bit_got = s0[LATB + i * 10 * gg + j]; 1: bit_got = s1[LATB + i * 10 * gg + j];
                              2: bit_got = s2[LATB + i * 10 * gg + j]; default: bit_got = sc[LATB + i * 10 * gg + j]; endcase
                    if (bit_got !== want[j / gg]) bad = bad + 1;
                end
                for (j = 0; j < 10; j = j + 1) begin    // the decode: the middle line bit of each group of gg
                    case (ln) 0: bit_got = s0[LATB + i * 10 * gg + j * gg + gg / 2]; 1: bit_got = s1[LATB + i * 10 * gg + j * gg + gg / 2];
                              2: bit_got = s2[LATB + i * 10 * gg + j * gg + gg / 2]; default: bit_got = sc[LATB + i * 10 * gg + j * gg + gg / 2]; endcase
                    mid[j] = bit_got;
                end
                if (mid !== want) mid_bad = mid_bad + 1;
                if (i > 0 && ln < 3 && mid === cs) dup_bad = dup_bad + 1;   // a symbol sent twice
                cs = mid;
            end
            if (bad || mid_bad || dup_bad) begin
                errors = errors + 1;
                $display("FAIL gear %0d lane %0d: %0d line bits wrong, %0d symbols wrong by the middle-bit decode, %0d symbols twice", gg, ln, bad, mid_bad, dup_bad);
            end
        end
    endtask

    integer gears [0:4];
    initial begin
        gears[0] = 1; gears[1] = 2; gears[2] = 3; gears[3] = 5; gears[4] = 3;
        for (i = 0; i <= NP + 1; i = i + 1) begin
            h0[i] = $random; h1[i] = $random; h2[i] = $random;
            if (i > 0) begin   // consecutive symbols differ: "sent twice" must be visible
                if (h0[i] === h0[i-1]) h0[i] = ~h0[i];
                if (h1[i] === h1[i-1]) h1[i] = ~h1[i];
                if (h2[i] === h2[i-1]) h2[i] = ~h2[i];
            end
        end
        for (gi = 0; gi < 5; gi = gi + 1) begin
            g = gears[gi];
            @(posedge usr); rst <= 1'b1; cap <= 1'b0; wn = 0;
            repeat (6) @(posedge usr);
            gear <= g;                      // the gear changes while reset is high
            repeat (4) @(posedge usr);
            rst <= 1'b0; cap <= 1'b1;
            repeat ((NP * g + 20) / 2) @(posedge usr);
            cap <= 1'b0;
            nchk = (wn * 20 - LATB) / (10 * g) - 1;
            if (nchk > NP) nchk = NP;
            if (nchk < NP - 2) begin errors = errors + 1; $display("FAIL gear %0d: only %0d pixels captured", g, nchk); end
            for (lane = 0; lane < 4; lane = lane + 1) check_lane(lane, g, nchk);
            $display("gear %0d: %0d pixels, %0d line bits per lane", g, nchk, nchk * 10 * g);
        end
        if (errors == 0) $display("PASS tmds_rep: gears 1 2 3 5 3, %0d pixels each, 4 lanes exact", NP);
        else $display("FAIL tmds_rep: %0d lane checks failed", errors);
        $finish;
    end
endmodule
