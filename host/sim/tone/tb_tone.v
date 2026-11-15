// T189 S1-S5: trx_tone (+ trx_gain behind it, as in host/top.v). The reference is a second implementation in double precision (RBJ shelf, here, no table
// shared with trx_tone.v). Frames of 64 clocks for the long sine runs (the filter needs about 30 clocks), 256 clocks for S5 (the real frame).
`timescale 1ns / 1ps
module tb_tone;
    reg clk = 0; always #40.69 clk = ~clk;
    reg frame_en = 0;
    reg [7:0] sel = 8'd0, vol = 8'd128;
    reg signed [23:0] il = 0, ir = 0;
    wire signed [24:0] tl, tr;
    wire signed [23:0] gl, gr;
    trx_tone dut (.clk(clk), .frame_en(frame_en), .sel(sel), .in_l(il), .in_r(ir), .out_l(tl), .out_r(tr));
    trx_gain gn (.clk(clk), .vol(vol), .in_l(tl), .in_r(tr), .out_l(gl), .out_r(gr));
    integer errors = 0;
    real PI = 3.14159265358979323846;

    // ---- reference biquad (double precision, RBJ cookbook, S = 1, Fs = 48 kHz) ----
    real rb0, rb1, rb2, ra1, ra2;       // normalised (a0 = 1)
    real rx1, rx2, ry1, ry2;
    task ref_design(input integer kind);     // 1 bass: low shelf 200 Hz +6 dB; 2 treble: high shelf 4 kHz +6 dB
        real A, w, cs, sn, al, r, f0, a0, b0, b1, b2, a1, a2;
        begin
            A = $pow(10.0, 6.0 / 40.0);
            f0 = (kind == 1) ? 200.0 : 4000.0;
            w = 2.0 * PI * f0 / 48000.0; cs = $cos(w); sn = $sin(w);
            al = sn / 2.0 * $sqrt(2.0); r = 2.0 * $sqrt(A) * al;
            if (kind == 1) begin
                b0 = A * ((A + 1) - (A - 1) * cs + r); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - r);
                a0 = (A + 1) + (A - 1) * cs + r; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - r;
            end else begin
                b0 = A * ((A + 1) + (A - 1) * cs + r); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - r);
                a0 = (A + 1) - (A - 1) * cs + r; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - r;
            end
            rb0 = b0 / a0; rb1 = b1 / a0; rb2 = b2 / a0; ra1 = a1 / a0; ra2 = a2 / a0;
            rx1 = 0; rx2 = 0; ry1 = 0; ry2 = 0;
        end
    endtask
    function real ref_step(input real x);
        real y;
        begin
            y = rb0 * x + rb1 * rx1 + rb2 * rx2 - ra1 * ry1 - ra2 * ry2;
            rx2 = rx1; rx1 = x; ry2 = ry1; ry1 = y; ref_step = y;
        end
    endfunction
    function real ref_mag_db(input real f);   // |H| in dB of the current reference design
        real w, nr, ni, dr, di;
        begin
            w = 2.0 * PI * f / 48000.0;
            nr = rb0 + rb1 * $cos(w) + rb2 * $cos(2 * w); ni = -(rb1 * $sin(w) + rb2 * $sin(2 * w));
            dr = 1.0 + ra1 * $cos(w) + ra2 * $cos(2 * w); di = -(ra1 * $sin(w) + ra2 * $sin(2 * w));
            ref_mag_db = 10.0 * $log10((nr * nr + ni * ni) / (dr * dr + di * di));
        end
    endfunction

    // ---- one frame: frame_en, the core presents its pair 3 clocks later, the readings ----
    integer FR = 64, RD = 40;
    reg signed [24:0] o_en_l, o_127_l, o_rd_l; reg signed [24:0] o_rd_r;
    reg signed [23:0] g_rd_l, g_rd_r;
    task frame(input signed [23:0] xl, input signed [23:0] xr);
        integer i;
        begin
            frame_en <= 1; o_en_l = tl;                      // what the HDMI hold sees at frame_en (blocking read before the edge's updates)
            @(posedge clk); frame_en <= 0;
            for (i = 1; i < FR; i = i + 1) begin
                @(posedge clk);
                if (i == 3) begin il <= xl; ir <= xr; end
                #1;
                if (i == RD) begin o_rd_l = tl; o_rd_r = tr; g_rd_l = gl; g_rd_r = gr; end
                if (i == 127) o_127_l = tl;
            end
        end
    endtask

    // ---- S1 ----
    integer n, k;
    reg signed [23:0] x;
    task s1(input [255:0] name);
        begin
            il = 0; ir = 0; #1; if (tl !== 25'sd0 || tr !== 25'sd0) begin errors = errors + 1; $display("FAIL S1 zero (%0s)", name); end
            il = 24'sd1; ir = -24'sd1; #1; if (tl !== 25'sd1 || tr !== -25'sd1) begin errors = errors + 1; $display("FAIL S1 +-1 (%0s)", name); end
            il = 24'sh7FFFFF; ir = 24'sh800000; #1; if (tl !== 25'sh007FFFFF || tr !== 25'sh1800000) begin errors = errors + 1; $display("FAIL S1 extremes (%0s) %h %h", name, tl, tr); end
            for (n = 0; n < 200; n = n + 1) begin
                x = $random; il = x; ir = ~x; #1;
                if (tl !== {x[23], x} || tr !== {~x[23], ~x}) begin errors = errors + 1; $display("FAIL S1 random (%0s)", name); end
            end
        end
    endtask

    // ---- S2: one point: RMS gain of a 0.25 FS sine, measured out/in, against the reference ----
    real sx, sy, amp, ph, dphi, g_meas, g_ref, dd;
    task s2_point(input integer kind, input real f);
        integer fi, nfr;
        begin
            ref_design(kind);
            sel = kind; repeat (8) @(posedge clk);
            amp = 0.25 * 8388608.0; ph = 0; dphi = 2.0 * PI * f / 48000.0; sx = 0; sy = 0;
            nfr = 2400 + ((f < 100.0) ? 9600 : 4800);
            for (fi = 0; fi < nfr; fi = fi + 1) begin
                x = $rtoi(amp * $sin(ph)); ph = ph + dphi;
                frame(x, x);
                if (fi >= 2400) begin sx = sx + $itor(x) * $itor(x); sy = sy + $itor(o_rd_l) * $itor(o_rd_l); end
            end
            g_meas = 10.0 * $log10(sy / sx); g_ref = ref_mag_db(f); dd = g_meas - g_ref; if (dd < 0) dd = -dd;
            $display("S2 %0s %6.0f Hz: measured %6.2f dB, ideal %6.2f dB", kind == 1 ? "bass  " : "treble", f, g_meas, g_ref);
            if (dd > 0.5) begin errors = errors + 1; $display("FAIL S2 kind %0d f %0f", kind, f); end
        end
    endtask
    real fl [0:6];
    integer fk;

    // ---- S3: full-scale sine, chain tone -> gain, against the clipped ideal ----
    real ideal, e, emax;
    task s3(input integer kind, input real f, input [7:0] v);
        integer fi; reg signed [23:0] exp_s;
        begin
            ref_design(kind); sel = kind; vol = v; repeat (8) @(posedge clk);
            ph = 0; dphi = 2.0 * PI * f / 48000.0; emax = 0;
            for (fi = 0; fi < 6000; fi = fi + 1) begin
                x = $rtoi(8388607.0 * $sin(ph)); ph = ph + dphi;
                frame(x, x);
                ideal = ref_step($itor(x)) * $itor(v) / 128.0;
                if (ideal > 8388607.0) ideal = 8388607.0; if (ideal < -8388608.0) ideal = -8388608.0;
                e = $itor(g_rd_l) - ideal; if (e < 0) e = -e; if (fi > 100 && e > emax) emax = e;
            end
            $display("S3 kind %0d %0.0f Hz vol %0d: max |out - clipped ideal| = %0.0f LSB (of 8388608)", kind, f, v, emax);
            if (emax > 170000.0) begin errors = errors + 1; $display("FAIL S3 kind %0d f %0f vol %0d", kind, f, v); end
        end
    endtask

    // ---- S4: burst then silence; DC step ----
    integer big;
    task s4(input integer kind);
        integer fi; real dcg, dcr;
        begin
            ref_design(kind); sel = kind; vol = 8'd128; repeat (8) @(posedge clk);
            ph = 0; dphi = 2.0 * PI * 100.0 / 48000.0;
            for (fi = 0; fi < 2000; fi = fi + 1) begin x = $rtoi(8388607.0 * $sin(ph)); ph = ph + dphi; frame(x, x); end
            big = 0;
            for (fi = 0; fi < 14400; fi = fi + 1) begin
                frame(24'sd0, 24'sd0);
                if (fi >= 9600 && (o_rd_l > 2 || o_rd_l < -2)) big = big + 1;
            end
            $display("S4 kind %0d: after 0.2 s of silence %0d of 4800 samples outside +-2 LSB (last %0d)", kind, big, o_rd_l);
            if (big != 0) begin errors = errors + 1; $display("FAIL S4 silence kind %0d", kind); end
            for (fi = 0; fi < 9600; fi = fi + 1) frame(24'sd2000000, 24'sd2000000);
            dcg = 20.0 * $log10($itor(o_rd_l) / 2000000.0);
            dcr = ref_mag_db(0.001);
            $display("S4 kind %0d DC step: %0.4f dB, ideal %0.4f dB", kind, dcg, dcr);
            dd = dcg - dcr; if (dd < 0) dd = -dd; if (dd > 0.05) begin errors = errors + 1; $display("FAIL S4 DC kind %0d", kind); end
        end
    endtask

    // ---- S5 ----
    integer same, nfr5;
    reg signed [24:0] prev_127;
    task s5(input integer kind);
        integer fi;
        begin
            sel = kind; vol = 8'd128; FR = 256; RD = 100; repeat (8) @(posedge clk);
            same = 0; nfr5 = 0;
            for (fi = 0; fi < 400; fi = fi + 1) begin
                frame($random, $random);
                if (fi > 4) begin nfr5 = nfr5 + 1; if (o_en_l === prev_127) same = same + 1; end
                prev_127 = o_127_l;
            end
            $display("S5 kind %0d: HDMI hold (next frame_en) == I2S take (frame_en + 127) in %0d of %0d frames", kind, same, nfr5);
            if (same != nfr5) begin errors = errors + 1; $display("FAIL S5 kind %0d", kind); end
            FR = 64; RD = 40;
        end
    endtask

    always @(errors) if (errors != 0 && $test$plusargs("stop")) begin $display("FAIL tb_tone (stopped at the first error)"); $finish; end
    initial begin
        fl[0] = 50.0; fl[1] = 100.0; fl[2] = 200.0; fl[3] = 400.0; fl[4] = 1000.0; fl[5] = 4000.0; fl[6] = 16000.0;
        #1000;
        sel = 0; s1("reset value");
        for (k = 1; k <= 2; k = k + 1) for (fk = 0; fk < 7; fk = fk + 1) s2_point(k, fl[fk]);
        // S3: full-scale at the shelf's top, volume 255 and 128
        s3(1, 100.0, 8'd255); s3(1, 100.0, 8'd128); s3(2, 10000.0, 8'd255); s3(2, 10000.0, 8'd128);
        s4(1); s4(2);
        s5(1); s5(2);
        // back to Flat while a signal plays: exact again
        sel = 0; repeat (8) @(posedge clk); s1("back to flat");
        if (errors == 0) $display("PASS tb_tone errors=0"); else $display("FAIL tb_tone errors=%0d", errors);
        $finish;
    end
endmodule
