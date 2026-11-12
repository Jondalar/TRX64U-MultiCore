// T191: amiga_rtc (the Amiga's MSM6242B-compatible clock). S1 time load + read back, S2 ticks and carries, S3 HOLD/E/F, S4 Test Kit's probe,
// S5 the enable.
`timescale 1ns/1ps
module tb_rtc;
    reg clk = 0; always #17.6 clk = ~clk;
    reg ce28 = 1;
    reg en = 1; reg [8:0] ld = 0; reg sel = 0, rd = 0, lwr = 0; reg [3:0] ra = 0, din = 0;
    wire [15:0] dout; wire loaded;
    localparam DIV = 4999;                                 // 5,000 clocks a second
    amiga_rtc #(.DIV(DIV)) dut (.clk(clk), .ce28(ce28), .en(en), .ld(ld), .loaded(loaded), .sel(sel), .rd(rd), .lwr(lwr), .ra(ra), .din(din), .dout(dout));
    integer errors = 0;
    task check(input cond, input [8*72-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end else $display("ok  : %0s", what); end
    endtask
    // the application's load: value first, then the index with the toggle (what amiga_regs does)
    reg ltg = 0;
    task app_nib(input [3:0] idx, input [3:0] v);
        begin ld[3:0] = v; repeat (3) @(posedge clk); ld[7:4] = idx; ltg = ~ltg; ld[8] = ltg; repeat (6) @(posedge clk); end
    endtask
    task app_load(input [3:0] y10, input [3:0] y1, input [3:0] mo10, input [3:0] mo1, input [3:0] d10, input [3:0] d1,
                  input [3:0] h10, input [3:0] h1, input [3:0] mi10, input [3:0] mi1, input [3:0] s10, input [3:0] s1, input [3:0] wd);
        begin
            app_nib(14, 1);
            app_nib(0, s1); app_nib(1, s10); app_nib(2, mi1); app_nib(3, mi10); app_nib(4, h1); app_nib(5, h10);
            app_nib(6, d1); app_nib(7, d10); app_nib(8, mo1); app_nib(9, mo10); app_nib(10, y1); app_nib(11, y10); app_nib(12, wd);
            app_nib(15, 0); app_nib(14, 0);
        end
    endtask
    // the 68000's byte read / write at $DC0000 + 4n + 3
    reg [3:0] got;
    task bus_rd(input [3:0] n);
        begin @(posedge clk); sel = 1; rd = 1; ra = n; repeat (3) @(posedge clk); got = dout[3:0]; sel = 0; rd = 0; repeat (2) @(posedge clk); end
    endtask
    task bus_wr(input [3:0] n, input [3:0] v);
        begin @(posedge clk); sel = 1; lwr = 1; ra = n; din = v; repeat (4) @(posedge clk); sel = 0; lwr = 0; repeat (2) @(posedge clk); end
    endtask
    function [47:0] nibs;           // the 12 BCD nibbles read as a number (S1 first), plus weekday below
        input dummy; nibs = 0;
    endfunction
    reg [3:0] r [0:12];
    task read_all; integer i; begin for (i = 0; i < 13; i = i + 1) begin bus_rd(i[3:0]); r[i] = got; end end endtask
    task expect_time(input [3:0] y10, input [3:0] y1, input [3:0] mo10, input [3:0] mo1, input [3:0] d10, input [3:0] d1,
                     input [3:0] h10, input [3:0] h1, input [3:0] mi10, input [3:0] mi1, input [3:0] s10, input [3:0] s1, input [3:0] wd,
                     input [8*40-1:0] what);
        begin
            read_all;
            check(r[0]==s1 && r[1]==s10 && r[2]==mi1 && r[3]==mi10 && r[4]==h1 && r[5]==h10 && r[6]==d1 && r[7]==d10 &&
                  r[8]==mo1 && r[9]==mo10 && r[10]==y1 && r[11]==y10 && r[12]==wd, what);
            if (!(r[0]==s1 && r[1]==s10 && r[2]==mi1 && r[3]==mi10 && r[4]==h1 && r[5]==h10 && r[6]==d1 && r[7]==d10 &&
                  r[8]==mo1 && r[9]==mo10 && r[10]==y1 && r[11]==y10 && r[12]==wd))
                $display("   got %0d%0d-%0d%0d-%0d%0d %0d%0d:%0d%0d:%0d%0d wd %0d", r[11], r[10], r[9], r[8], r[7], r[6], r[5], r[4], r[3], r[2], r[1], r[0], r[12]);
        end
    endtask
    task wait_s(input integer n); begin repeat (n * 5000 + 2500) @(posedge clk); end endtask   // n seconds and a half after a load
    integer i;
    initial begin
        repeat (5) @(posedge clk);
        check(!loaded, "S1 not loaded before the app");
        // S1: 2026-11-06 14:05:09, Friday (5); loaded with the loader hold set, so nothing ticks during the load
        app_load(2, 6, 1, 1, 0, 6, 1, 4, 0, 5, 0, 9, 5);
        check(loaded, "S1 loaded flag set by the app's last step");
        read_all;
        expect_time(2, 6, 1, 1, 0, 6, 1, 4, 0, 5, 0, 9, 5, "S1 time read back as BCD");
        // S2: the second ticks (the sub-second counter was zeroed by index 15, then the hold was dropped)
        wait_s(1);
        read_all; check(r[1]*10 + r[0] == 10, "S2 seconds advanced by one second (09 -> 10)");
        // carries: loaded by the application (a load zeroes the sub-second counter, so the count is exact)
        app_load(2, 6, 1, 2, 3, 1, 2, 3, 5, 9, 5, 8, 6);
        wait_s(3);
        expect_time(2, 7, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, "S2 2026-12-31 23:59:58+3s = 2027-01-01 00:00:01, wd 0");
        app_load(2, 8, 0, 2, 2, 8, 2, 3, 5, 9, 5, 9, 1);
        wait_s(1);
        expect_time(2, 8, 0, 2, 2, 9, 0, 0, 0, 0, 0, 0, 2, "S2 leap: 2028-02-28 23:59:59+1s = 02-29 00:00:00");
        app_load(2, 7, 0, 2, 2, 8, 2, 3, 5, 9, 5, 9, 1);
        wait_s(1);
        expect_time(2, 7, 0, 3, 0, 1, 0, 0, 0, 0, 0, 0, 2, "S2 2027-02-28 23:59:59+1s = 03-01 (not leap)");
        // the bus writes the registers too (SetClock): under HOLD, then read back
        bus_wr(13, 1);
        bus_wr(0, 3); bus_wr(1, 2); bus_wr(2, 7); bus_wr(3, 1); bus_wr(4, 5); bus_wr(5, 0); bus_wr(6, 9); bus_wr(7, 1); bus_wr(8, 4); bus_wr(9, 0);
        bus_wr(10, 5); bus_wr(11, 3); bus_wr(12, 4);
        expect_time(3, 5, 0, 4, 1, 9, 0, 5, 1, 7, 2, 3, 4, "S1 bus writes to registers 0-C read back");
        bus_wr(13, 0);
        // S3: HOLD freezes the counters, they catch up by one second
        bus_rd(13); check(got == 0, "S3 D reads 0 after power-up/clear");
        bus_wr(13, 1); bus_rd(13); check(got == 1, "S3 HOLD written to D reads back as 1");
        bus_rd(0); begin : hold_blk reg [3:0] s0; s0 = got; wait_s(3); bus_rd(0); check(got == s0, "S3 seconds stand still during HOLD"); end
        bus_wr(13, 0); bus_rd(13); check(got == 0, "S3 HOLD cleared reads 0");
        repeat (30) @(posedge clk); bus_rd(0); check(got != 0, "S3 the held second is applied on release");
        bus_wr(14, 4'hA); bus_rd(14); check(got == 4'hA, "S3 E reads back what was written");
        bus_wr(15, 4'h1); bus_rd(15); check(got == 4'h1, "S3 F reads back what was written");
        bus_wr(15, 4'h4);
        // S4: Test Kit's probe: HOLD set, read back, clear, read back, in the sequence of battclock.c
        bus_rd(13); begin : probe reg ok; ok = 1; bus_wr(13, 1); bus_rd(13); if (got != 1) ok = 0; bus_wr(13, 0); bus_rd(13); if (got != 0) ok = 0;
            bus_rd(15); if (got != 4) ok = 0; check(ok, "S4 Test Kit probe: D write/read back, F = 4"); end
        // S5: the enable
        en = 0; bus_rd(0); check(got == 0 && dout == 0, "S5 disabled: nothing on the data bus");
        bus_wr(1, 4'h3); en = 1; bus_rd(1); check(got != 3, "S5 disabled: a write is not taken");
        // 30 s adjust: 14:05:40 -> 14:06:00
        bus_wr(13, 1); bus_wr(0, 0); bus_wr(1, 4); bus_wr(2, 5); bus_wr(3, 0); bus_wr(13, 8); repeat (4) @(posedge clk);
        bus_rd(1); check(got == 0, "ADJ: seconds tens to 0");
        bus_rd(2); check(got == 6, "ADJ: minute rounded up from 5 to 6");
        $display("%s tb_rtc errors=%0d", errors == 0 ? "PASS" : "FAIL", errors);
        $finish;
    end
    initial begin #400000000; $display("FAIL timeout"); $finish; end
endmodule
