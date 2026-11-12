// S14 (T164 WP10): PHI2 / DOTCLK / RESET under the port lock. PERIOD_PS = the system clock (dot4x) of the System Mode.
// Controls: -DCTL_OLD  today's pin logic (both lines forced low while locked)  -> the rate checks FAIL
//           -DCTL_RAW  the lines switch from the generator to the core without the hold -> the runt check FAILS
`timescale 1ps/1ps
module tb_portlock;
`ifndef PERIOD_PS
`define PERIOD_PS 31718
`endif
    reg clk = 0;
    always #(`PERIOD_PS/2) clk = ~clk;

    reg lock = 1, decouple = 0;
    // a core's port as slot_port generates it (tk counter, PHI2 high 1..16, DOTCLK = tk[1]); its counter has its own phase
    reg [4:0] tk = 5'd5;
    reg       core_run = 0;                  // 0: parked, lines low
    reg       l2_rst = 0;
    always @(posedge clk) tk <= tk + 5'd1;
    reg l2_phi2 = 0, l2_dot = 0;
    always @(posedge clk) begin
        l2_phi2 <= core_run && tk >= 5'd1 && tk <= 5'd16;
        l2_dot  <= core_run && tk[1];
    end
    wire phi2_m, dot_m, rst_m, gen_p, gen_d;
    wire phi2, dotclk, rst_o;
    portlock_clk dut (.clk(clk), .lock(lock), .decouple(decouple), .l2_phi2(l2_phi2), .l2_dotclk(l2_dot), .l2_rst_o(l2_rst),
                      .phi2(phi2_m), .dotclk(dot_m), .rst_o(rst_m), .gen_phi2(gen_p), .gen_dotclk(gen_d));
`ifdef CTL_OLD
    assign phi2 = lock ? 1'b0 : decouple ? 1'b0 : l2_phi2;
    assign dotclk = lock ? 1'b0 : decouple ? 1'b0 : l2_dot;
`elsif CTL_RAW
    assign phi2 = decouple ? 1'b0 : lock ? gen_p : l2_phi2;
    assign dotclk = decouple ? 1'b0 : lock ? gen_d : l2_dot;
`else
    assign phi2 = phi2_m;
    assign dotclk = dot_m;
`endif
    assign rst_o = rst_m;

    // measurement: run lengths in clocks of each line, minimum and maximum high / low over a window
    integer errors = 0;
    integer run_p = 0, run_d = 0;
    reg pp = 0, pd = 0;
    integer minh_p, minl_p, maxh_p, maxl_p, minh_d, minl_d, maxh_d, maxl_d, edges_p, edges_d;
    reg meas = 0;
    task mclear; begin
        minh_p = 999; minl_p = 999; maxh_p = 0; maxl_p = 0; minh_d = 999; minl_d = 999; maxh_d = 0; maxl_d = 0; edges_p = 0; edges_d = 0;
    end endtask
    initial mclear;
    always @(posedge clk) begin
        #1;
        if (phi2 === pp) run_p = run_p + 1;
        else begin
            if (meas) begin
                edges_p = edges_p + 1;
                if (edges_p > 1) begin   // the first run is partial
                    if (pp) begin if (run_p < minh_p) minh_p = run_p; if (run_p > maxh_p) maxh_p = run_p; end
                    else    begin if (run_p < minl_p) minl_p = run_p; if (run_p > maxl_p) maxl_p = run_p; end
                end
            end
            run_p = 1; pp = phi2;
        end
        if (dotclk === pd) run_d = run_d + 1;
        else begin
            if (meas) begin
                edges_d = edges_d + 1;
                if (edges_d > 1) begin
                    if (pd) begin if (run_d < minh_d) minh_d = run_d; if (run_d > maxh_d) maxh_d = run_d; end
                    else    begin if (run_d < minl_d) minl_d = run_d; if (run_d > maxl_d) maxl_d = run_d; end
                end
            end
            run_d = 1; pd = dotclk;
        end
    end

    task check(input ok, input [255:0] what); begin
        if (!ok) begin errors = errors + 1; $display("FAIL: %0s", what); end
    end endtask

    integer i, j;
    realtime t0, t1;
    real khz_p, khz_d;
    initial begin
        // 1. locked, core parked: the rate, the duty, RESET low
        repeat (40) @(posedge clk);
        mclear; meas = 1; t0 = $realtime;
        repeat (32 * 40) @(posedge clk);
        meas = 0; t1 = $realtime;
        khz_p = (edges_p / 2.0) / ((t1 - t0) / 1.0e9);   // ps -> kHz: cycles per ms
        khz_d = (edges_d / 2.0) / ((t1 - t0) / 1.0e9);
        $display("locked: PHI2 high %0d..%0d low %0d..%0d clk (%0d edges), DOTCLK high %0d..%0d low %0d..%0d (%0d edges), RESET_O=%b",
                 minh_p, maxh_p, minl_p, maxl_p, edges_p, minh_d, maxh_d, minl_d, maxl_d, edges_d, rst_o);
        $display("locked: PHI2 %0.1f kHz, DOTCLK %0.1f kHz", khz_p, khz_d);
        // a C64 with the reset held: PAL 985.2 / 7881.5 kHz, NTSC 1022.7 / 8181.4 kHz (this clock: dot4x of the mode)
        check(khz_p > 0.99 * (1.0e9 / (32.0 * `PERIOD_PS)) && khz_p < 1.01 * (1.0e9 / (32.0 * `PERIOD_PS)), "PHI2 rate");
        check(khz_d > 0.99 * (1.0e9 / (4.0 * `PERIOD_PS)) && khz_d < 1.01 * (1.0e9 / (4.0 * `PERIOD_PS)), "DOTCLK rate");
        check(minh_p == 16 && maxh_p == 16 && minl_p == 16 && maxl_p == 16, "PHI2 is not dot4x / 32 at 50%");
        check(minh_d == 2 && maxh_d == 2 && minl_d == 2 && maxl_d == 2, "DOTCLK is not dot4x / 4 at 50%");
        check(rst_o === 1'b1, "the port's RESET is not held low under the lock");
        // 2. a decouple forces both low, even locked
        decouple = 1; repeat (3) @(posedge clk); #1;
        for (i = 0; i < 70; i = i + 1) begin @(posedge clk); #1; check(phi2 === 1'b0 && dotclk === 1'b0, "decouple: a line is not low"); end
        decouple = 0;
        // 3. locked while the core runs: the generator still decides
        core_run = 1; repeat (64) @(posedge clk);
        mclear; meas = 1; repeat (32 * 20) @(posedge clk); meas = 0;
        check(minh_p == 16 && maxh_p == 16 && minl_p == 16 && maxl_p == 16, "PHI2 changed with a running core under the lock");
        // 4. release at every phase, with the core parked and running: no runt, then the core's own lines
        for (j = 0; j < 2; j = j + 1) begin
            for (i = 0; i < 40; i = i + 1) begin
                lock = 1; core_run = j; l2_rst = 0; repeat (200 + i) @(posedge clk);
                mclear; meas = 1;
                lock = 0; l2_rst = 0;
                repeat (300) @(posedge clk);
                meas = 0;
                if (j == 1) begin
                    check(minh_p >= 16 && minl_p >= 16, "PHI2 runt at the hand-over");
                    check(minh_d >= 2 && minl_d >= 2, "DOTCLK runt at the hand-over");
                    check(l2_phi2 === phi2 && l2_dot === dotclk, "after the hand-over the lines are not the core's");
                end else begin
                    check(phi2 === 1'b0 && dotclk === 1'b0, "parked core: a line is not low after the hand-over");
                    check(edges_p <= 2 && edges_d <= 4, "parked core: more than the running cycle after the release");
                end
                check(rst_o === 1'b0, "RESET_O not the core's after the release");
            end
        end
        // 5. the lock set again while the core runs: back to the generator without a runt
        core_run = 1; lock = 0; repeat (100) @(posedge clk);
        mclear; meas = 1; lock = 1; repeat (300) @(posedge clk); meas = 0;
        check(minh_p >= 16 && minl_p >= 16 && minh_d >= 2 && minl_d >= 2, "runt when the lock is set again");
        check(rst_o === 1'b1, "RESET_O not low after the lock is set again");
        if (errors == 0) $display("PASS portlock %0d ps", `PERIOD_PS); else $display("FAIL portlock: %0d errors", errors);
        $finish;
    end
endmodule
