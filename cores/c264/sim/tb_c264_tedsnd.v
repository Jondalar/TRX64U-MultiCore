// T90 S7: the TED's sound from a register write to the samples. The real ted.v (clk_en = the core's NCO) gets the CPU's writes to
// $FF0E/$FF12/$FF11 on its bus, its digi_sound feeds c264_audio, and the frequency is counted from audio_l (rising crossings of a
// Schmitt trigger per second). A: voice 1 = 768 at $FF0E/$FF12, volume 8 -> the TED's pitch 886724 / 8 / (1024 - 768) = 433 Hz.
// The level: one voice at volume 8 must reach 4 M of 8.4 M (the quiet scale of 100/256 gave 1.3 M). B: voice 1 off ($FF11 = $08) -> silent. C: voice 2 = 512 at $FF0F/$FF10, volume 5 -> 216 Hz.
`timescale 1ns / 1ps
module tb_c264_tedsnd;
    reg sys_clk = 0, mclk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #40.69 mclk = ~mclk;
    reg reset = 1;
    wire ted_en;
    c264_nco nco (.clk(sys_clk), .hold(1'b0), .en(ted_en));
    reg [15:0] addr = 16'h0000;
    reg [7:0] dat = 8'h00;
    reg rw = 1'b1;
    wire signed [15:0] digi;
    wire [7:0] k = 8'hFF;
    ted t (.clk(sys_clk), .clk_en(ted_en), .addr_in(addr), .reset(reset), .addr_out(), .data_in(dat), .data_out(),
        .rw(rw), .cpuclk(), .color(), .csync(), .hblank(), .vblank(), .irq(), .ba(), .mux(), .ras(), .cas(), .cs0(), .cs1(),
        .aec(), .snd(), .digi_sound(digi), .k(k), .cpuenable(), .pal(), .hsync(), .vsync(), .burst(), .even(), .data_oe(),
        .refresh_o());
    wire signed [15:0] a_in = digi;
    wire signed [23:0] al, ar;
    c264_audio aud (.clk(sys_clk), .a_in(a_in), .mute(1'b0), .mclk(mclk), .audio_l(al), .audio_r(ar));

    integer fails = 0;
    task check(input ok, input [8*70-1:0] what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
        end
    endtask
    // a CPU write, held for 40 TED clocks so that it spans a cycle end whatever the phase
    task wr(input [15:0] a, input [7:0] d);
        integer n;
        begin
            @(posedge sys_clk); addr <= a; dat <= d; rw <= 1'b0;
            for (n = 0; n < 40; n = n + 1) begin @(posedge sys_clk); while (!ted_en) @(posedge sys_clk); end
            addr <= 16'h0; rw <= 1'b1;
        end
    endtask
    integer lo_seen, hi_cnt;
    task measure(input integer ms, output integer hz, output integer pk);
        integer i, n;
        reg signed [23:0] cur;
        begin
            hi_cnt = 0; lo_seen = 0; pk = 0; n = ms * 48;
            for (i = 0; i < n; i = i + 1) begin
                repeat (656) @(posedge sys_clk);
                cur = al;
                if (cur > pk) pk = cur;
                if (-cur > pk) pk = -cur;
                if (cur < -24'sd100000) lo_seen = 1;
                else if (cur > 24'sd100000 && lo_seen) begin hi_cnt = hi_cnt + 1; lo_seen = 0; end
            end
            hz = (hi_cnt * 1000) / ms;
        end
    endtask
    integer hz, pk;
    initial begin
        repeat (200) @(posedge sys_clk);
        reset = 0;
        repeat (2000) @(posedge sys_clk);
        wr(16'hFF11, 8'h08);
        repeat (656 * 400) @(posedge sys_clk);
        measure(40, hz, pk);
        check(pk < 2000, "silent TED: audio quiet");
        wr(16'hFF0E, 8'h00); wr(16'hFF12, 8'h03); wr(16'hFF11, 8'h18);
        repeat (656 * 400) @(posedge sys_clk);
        measure(100, hz, pk);
        $display("A: %0d Hz, peak %0d", hz, pk);
        check(hz >= 420 && hz <= 446, "voice 1 = 433 Hz from the samples");
        check(pk > 4000000 && pk < 8388607, "voice 1 volume 8: -6 dBFS, not clipped");
        wr(16'hFF11, 8'h08);
        repeat (656 * 400) @(posedge sys_clk);
        measure(40, hz, pk);
        check(hz == 0, "voice 1 off: no pitch");
        wr(16'hFF0F, 8'h00); wr(16'hFF10, 8'h02); wr(16'hFF11, 8'h25);
        repeat (656 * 400) @(posedge sys_clk);
        measure(100, hz, pk);
        $display("C: %0d Hz, peak %0d", hz, pk);
        check(hz >= 208 && hz <= 224, "voice 2 = 216 Hz from the samples");
        check(pk > 1500000, "voice 2 volume 5 is clearly audible");
        if (fails == 0) $display("PASS tb_c264_tedsnd"); else $display("FAIL tb_c264_tedsnd (%0d)", fails);
        $finish;
    end
    initial begin #400000000; $display("FAIL tb_c264_tedsnd timeout"); $finish; end
endmodule
