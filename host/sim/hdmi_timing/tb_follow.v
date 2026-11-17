// T188 D3 bench: hdmi_timing with FOLLOW, a core frame source of a chosen rate, a model of the application's flip (poll latency 2..30 % of a frame).
// The DUT is compiled with a reduced table (run.sh: every row 24 pixels wide, gear 1, the vertical raster of the real row): the follow works in lines,
// so the line count of the real rows is kept and the bench is 200 times shorter than at 576p x 880.
// Per output frame start the model flips to the newest core frame the application had committed by then (the pix_layer flip at the frame start).
// Counted after the lock-in: a repeat (the same core frame twice in a row), a skip (a core frame never shown), the range of follow_e, and for every
// frame that its length is the table's plus follow_e (the padded / skipped lines are really in the frame).
// +mix=<p>: a core frame is 312 lines long with probability p, else 313 (the Amiga's non-interlaced frame, agnus_beamcounter long_frame); fc then is the 313-line rate.
// Plusargs: +fc=<core Hz> (float), +mode=<m>, +frames=<output frames after lock-in>, +seed=<n>, +ph=<core start phase 0..1>, +jit=<fraction of a frame period>.
`timescale 1ns/1ps
module tb;
    parameter FOLLOW = 1;
    reg clk = 0;
    always #3 clk = ~clk;
    reg rst = 1; reg [3:0] mode = 2;
    wire ce, de, hs, vs, running, valid, aligned;
    wire [23:0] rgb; wire [11:0] cx, hact, xact0; wire [10:0] cy, vact, yact0; wire [3:0] gear; wire [6:0] vic;
    wire [31:0] rate; wire [12:0] sx_e, fw_m1; wire [10:0] sy_e, ey_e; wire [2:0] ph_e;
    wire signed [5:0] fe;
    reg core_tgl = 0;
    hdmi_timing #(.FOLLOW(FOLLOW)) dut (.clk(clk), .rst(rst), .mode(mode), .align(1'b0), .valid(valid), .aligned(aligned), .ce(ce), .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy),
        .running(running), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0), .gear(gear), .vic(vic), .rate(rate),
        .sx_e(sx_e), .fw_m1(fw_m1), .sy_e(sy_e), .ey_e(ey_e), .ph_e(ph_e), .mode_q(), .core_tgl(core_tgl), .follow_e(fe));

    real fc, ph, jit, mix;
    integer nfr, seed, htot, vtot, fnom, i, ok_m;
    integer clkn = 0;
    // the core: events at clkn >= tev; frame n available to the application at ev_a[n]
    real tev, pc, fo;
    integer nev = 0;
    real ev_a [0:4095];
    integer shown = 0, last_shown = 0, nshown = 0;
    integer frames_out = 0, repeats = 0, skips = 0, lockin = 140, emin = 99, emax = -99, bad_len = 0, len_n = 0, fe_mid = 0, started = 0, t_prev = 0;
    integer core_in_window = 0, first_shown_n = -1, ev_at_start = 0;
    reg ce_q = 0, fs = 0;

    initial begin
        if (!$value$plusargs("fc=%f", fc)) fc = 50.080128;
        if (!$value$plusargs("mode=%d", mode)) mode = 2;
        if (!$value$plusargs("frames=%d", nfr)) nfr = 700;
        if (!$value$plusargs("seed=%d", seed)) seed = 1;
        if (!$value$plusargs("ph=%f", ph)) ph = 0.37;
        if (!$value$plusargs("jit=%f", jit)) jit = 0.002;
        if (!$value$plusargs("mix=%f", mix)) mix = 0.0;      // fraction of 312-line core frames among 313-line ones (fc is the rate of the 313-line frames then)
        htot = 24;
        case (mode) 0: vtot = 625; 1: vtot = 749; 2: vtot = 1125; 6: vtot = 525; 7: vtot = 750; 8: vtot = 1125; default: vtot = 625; endcase
        fnom = htot * vtot;                                  // clocks of the table's frame
        fo = 49.920128;
        pc = fnom * fo / fc;                                 // clocks of a core frame
        tev = pc * ph + fnom * 0.5;                          // first event
        repeat (20) @(posedge clk);
        rst = 0;
    end

    // the core source
    always @(posedge clk) begin
        clkn <= clkn + 1;
        if (!rst && clkn >= tev && nev < 4000) begin
            core_tgl <= ~core_tgl;
            nev = nev + 1;
            // the application sees the frame 2 .. 30 % of a frame period later (poll 5 ms, a few ms of the machine), random
            ev_a[nev] = tev + pc * (0.02 + 0.28 * ($itor($unsigned($random(seed)) % 1000) / 1000.0));
            tev = tev + pc * (1.0 + jit * (($itor($unsigned($random(seed)) % 2001) / 1000.0) - 1.0)) * (($itor($unsigned($random(seed)) % 1000) / 1000.0 < mix) ? 312.0 / 313.0 : 1.0);
        end
    end

    // the monitor: frame starts, the model flip, the frame lengths
    always @(posedge clk) begin
        ce_q <= ce;
        if (ce_q && cx == 0 && cy == 0 && running) begin
            if (started) begin
                // the frame that just ended: its length against the table's plus follow_e (sampled mid-frame)
                if (frames_out > 2) begin
                    len_n = len_n + 1;
                    if ((clkn - t_prev) != htot * (vtot + fe_mid)) bad_len = bad_len + 1;
                end
            end
            started = 1; t_prev = clkn;
            // the flip: the newest frame the application committed by now
            while (last_shown < nev && ev_a[last_shown + 1] <= clkn) last_shown = last_shown + 1;
            frames_out = frames_out + 1;
            if (frames_out > lockin) begin
                if (last_shown == shown && nshown > 0) repeats = repeats + 1;
                else if (last_shown > shown + 1 && nshown > 0) skips = skips + (last_shown - shown - 1);
                if (nshown == 0) first_shown_n = last_shown;
                nshown = nshown + 1;
                if (fe < emin) emin = fe;
                if (fe > emax) emax = fe;
            end
            shown = last_shown;
        end
        if (ce_q && cx == 10 && cy == yact0 + 5) fe_mid = fe;
    end

    initial begin
        wait (!rst);
        wait (frames_out > lockin + nfr);
        $display("fc=%0.3f mode=%0d follow=%0d frames=%0d core_frames=%0d repeats=%0d skips=%0d fe=[%0d..%0d] fe_end=%0d len_bad=%0d/%0d",
                 fc, mode, FOLLOW, nshown, last_shown - first_shown_n, repeats, skips, emin, emax, fe, bad_len, len_n);
        $finish;
    end
    initial begin #2000000000; $display("FAIL timeout"); $finish; end
endmodule
