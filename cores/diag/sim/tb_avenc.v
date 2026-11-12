// T201 S1: the test picture's encoder words equal the C128's. For each of the four standards a diag_avout (the C128's default knobs: the group order of c128_avpins SWAP_YCV = 1, V sign
// inverted in PAL) runs 1.2 frames; the unedited copy of the C128 encoder (sim/av/c128_yc_ref.v, module c128_yc_ref) gets the same sync / blanking / pixel stream, the same palette (an own table here)
// and the same constants (PHASE_INC, burst window, blanking level of c128_avout.v, written out here), and its three words must equal the dut's groups clock by clock. NTSC runs with the V
// sign normal (the C128's NTSC never had the knob). A picture that never reached the encoder, or an encoder that never made a colour, fails the counters.
// Not covered here: the reference is fed the dut's own raster, sync, burst window and inc, so diag_avout's raster and constants are not compared with c128_avout.v
// (they are copies, read side by side); the palette is the same table typed twice.
`timescale 1ns / 1ps
module tb_avenc;
    reg clk = 0;
    always #5 clk = ~clk;
    function [23:0] pal24; input [3:0] i;
        case (i)
        0: pal24 = 24'h000000; 1: pal24 = 24'hFFFFFF; 2: pal24 = 24'h813338; 3: pal24 = 24'h75CEC8;
        4: pal24 = 24'h8E3C97; 5: pal24 = 24'h56AC4D; 6: pal24 = 24'h2E2C9B; 7: pal24 = 24'hEDF171;
        8: pal24 = 24'h8E5029; 9: pal24 = 24'h553800; 10: pal24 = 24'hC46C71; 11: pal24 = 24'h4A4A4A;
        12: pal24 = 24'h7B7B7B; 13: pal24 = 24'hA9FF9F; 14: pal24 = 24'h706DEB; default: pal24 = 24'hB2B2B2;
        endcase
    endfunction
    function [81:0] cfg; input [1:0] std;
        reg pal; reg [6:0] bs; reg [9:0] be; reg [7:0] blank;
        begin
            pal = (std == 0) || (std == 2);
            bs = pal ? 7'd28 : 7'd20; be = pal ? 10'd99 : 10'd102; blank = pal ? 8'd77 : 8'd73;
            //      vpos  vlen eq-l262-frz wamp blank  synl   gain    bamp   be  bs  trim  vinv         rev  perm  std
            cfg = {8'd0, 5'd6, 3'b000, 5'd11, blank, 8'd0, 5'd16, 6'd32, be, bs, 8'd0, pal ? 1'b1 : 1'b0, 3'd0, 3'd5, std};
        end
    endfunction
    integer errors [0:3];
    integer checks [0:3];
    integer nde [0:3];
    integer nchroma [0:3];
    genvar g;
    generate for (g = 0; g < 4; g = g + 1) begin : u
        wire [7:0] ga, gb, gc;
        diag_avout dut (.clk(clk), .av_cfg(cfg(g)), .ga(ga), .gb(gb), .gc(gc));
        localparam PALP = (g == 0) || (g == 2);
        wire pal = PALP;
        wire [3:0] sidx;
        wire [7:0] ry, rc, rcv;
        c128_yc_ref #(.WIDTH(26), .VINV(PALP ? 1 : 0)) rf (
            .clk(clk), .inc(dut.inc), .pal_en(pal), .burst(pal ? {7'd28, 10'd99} : {7'd20, 10'd102}), .blk(pal ? 8'd77 : 8'd73),
            .hsync(dut.hs_r), .csync(dut.cs_r), .de(dut.de_r), .idx(dut.idx_r), .sidx(sidx), .rgb_s(pal24(sidx)),
            .y_o(ry), .c_o(rc), .cv_o(rcv));
        // the dut's groups are the encoder words one clock later (the permutation register): group A = CV, B = C, C = Y
        reg [7:0] py, pc, pcv;
        reg [31:0] n = 0;
        initial begin errors[g] = 0; checks[g] = 0; nde[g] = 0; nchroma[g] = 0; end
        always @(posedge clk) begin
            py <= ry; pc <= rc; pcv <= rcv; n <= n + 1;
            if (n > 40) begin
                checks[g] = checks[g] + 1;
                if (ga !== pcv || gb !== pc || gc !== py) begin
                    errors[g] = errors[g] + 1;
                    if (errors[g] < 4) $display("FAIL std %0d clk %0d: groups %h %h %h, C128 words cv %h c %h y %h", g, n, ga, gb, gc, pcv, pc, py);
                end
                if (dut.de_r) nde[g] = nde[g] + 1;
                if (dut.de_r && (rc > 8'd140 || rc < 8'd116)) nchroma[g] = nchroma[g] + 1;
            end
        end
    end endgenerate
    integer k, bad;
    initial begin
        repeat (760000) @(posedge clk);
        bad = 0;
        for (k = 0; k < 4; k = k + 1) begin
            $display("std %0d: %0d clocks compared, %0d mismatches, %0d picture clocks, %0d with chroma", k, checks[k], errors[k], nde[k], nchroma[k]);
            if (errors[k] != 0 || checks[k] < 700000 || nde[k] < 100000 || nchroma[k] < 20000) bad = bad + 1;
        end
        if (bad == 0) $display("PASS tb_avenc errors=0");
        else $display("FAIL tb_avenc standards failing=%0d", bad);
        $finish;
    end
endmodule
