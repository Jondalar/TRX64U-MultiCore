// bling_io: keyboard bytes -> FIFO/FLAGS/IRQ, SHIFT LOCK LED frame on BLING_LED,
// the app's LED registers read 0 (OPT-bling). iverilog -o tb tb_bling_io.v ../bling_io.v ../bling_keyboard.v
`timescale 1ns/1ps
module tb_bling_io;
    reg clk = 0, sclk = 0, rx = 1;
    always #5 clk = ~clk;                     // 100 MHz
    always #15.86 sclk = ~sclk;               // 31.5278 MHz
    reg rst = 1;

    wire [63:0] keys; wire [7:0] last, other; wire rx_tgl, locked, lock_off;
    bling_keyboard kb (.clk(sclk), .v60(1'b0), .rx(rx), .keys(keys), .last(last), .other(other),
                       .rx_tgl(rx_tgl), .locked(locked), .lock_off(lock_off));

    reg we = 0; reg [11:0] addr = 0; reg [7:0] wdata = 0;
    wire [7:0] rdata; wire irq, bling_led;
    bling_io dut (.clk(clk), .rst(rst), .we(we), .addr(addr), .wdata(wdata), .rdata(rdata),
                  .irq(irq), .rx_tgl(rx_tgl), .rx_byte(last), .locked(locked), .lock_off(lock_off),
                  .drive_act(1'b0), .hd_act(1'b0), .lamp_en(1'b0), .bling_led(bling_led));

    integer err = 0;
    task fail(input [8*40-1:0] what, input integer got, input integer exp); begin
        err = err + 1; $display("FAIL %0s: got %h exp %h at %0t", what, got, exp, $time);
    end endtask
    task wr(input [11:0] a, input [7:0] d); begin
        @(posedge clk); #1 we = 1; addr = a; wdata = d;
        @(posedge clk); #1 we = 0;
    end endtask
    reg [7:0] rv;
    task rd(input [11:0] a); begin
        @(posedge clk); #1 addr = a;
        @(posedge clk); @(posedge clk); #1 rv = rdata;
    end endtask
    task send(input [7:0] b); integer i; begin
        rx = 0; #4000;
        for (i = 0; i < 8; i = i + 1) begin rx = b[i]; #4000; end
        rx = 1; #8000;
    end endtask

    // ---- WS2812 decoder on bling_led: a frame ends with a gap of 100 us or more ----
    reg [1703:0] fr_bits;
    integer fr_n = 0, fr_cnt = 0, fr_len = 0, ws_bad = 0, ws_bits = 0;
    real t_rise = 0, t_fall = 0, t_prev = 0, th;
    always @(posedge bling_led) begin
        if (fr_n != 0 && $realtime - t_fall > 100000.0) begin
            fr_len = fr_n; fr_n = 0; fr_cnt = fr_cnt + 1;
        end else if (fr_n != 0 && ($realtime - t_prev) != 1250.0) begin
            ws_bad = ws_bad + 1;
            if (ws_bad < 5) $display("WS bit period %0.1f ns", $realtime - t_prev);
        end
        t_prev = $realtime; t_rise = $realtime;
    end
    always @(negedge bling_led) begin
        th = $realtime - t_rise;
        t_fall = $realtime;
        if (th != 350.0 && th != 800.0) begin
            ws_bad = ws_bad + 1;
            if (ws_bad < 5) $display("WS high %0.1f ns", th);
        end
        if (fr_n < 1704) fr_bits[1703 - fr_n] = th > 575.0;
        fr_n = fr_n + 1;
    end
    // the last complete frame: 1704 bits, 70 LEDs dark, the 71st = want
    task frame_check(input [23:0] want, input [8*24-1:0] what); integer c0; begin
        c0 = fr_cnt;
        wait (fr_cnt >= c0 + 2);                // the second one started after the change
        if (fr_len !== 1704) fail("frame bits", fr_len, 1704);
        if (fr_bits[1703:24] !== 1680'd0) fail("dark LEDs", 0, 0);
        if (fr_bits[23:0] !== want) fail(what, fr_bits[23:0], want);
        if (ws_bad) fail("ws timing", ws_bad, 0);
    end endtask

    function [7:0] dat(input integer i); dat = (i * 7 + 3) & 8'hFF; endfunction
    integer i;
    initial begin
        #100 rst = 0;
        #1000;
        // ---------------- Bling RX ----------------
        rd(12'h802); if (rv !== 8'h04) fail("flags empty", rv, 8'h04);
        wr(12'h803, 8'h01);
        if (irq !== 1'b0) fail("irq empty", irq, 0);
        send(8'h0A);
        #200;
        rd(12'h802); if (rv !== 8'h84) fail("flags 1 byte", rv, 8'h84);
        if (irq !== 1'b1) fail("irq", irq, 1);
        send(8'h41);
        #200;
        rd(12'h802); if (rv !== 8'h86) fail("flags lock", rv, 8'h86);
        if (keys[15] !== 1'b1) fail("lock -> shift", keys[15], 1);
        rd(12'h800); if (rv !== 8'h0A) fail("data 1", rv, 8'h0A);
        wr(12'h801, 8'h01);
        rd(12'h800); if (rv !== 8'h41) fail("data 2", rv, 8'h41);
        wr(12'h801, 8'h01);
        rd(12'h802); if (rv !== 8'h06) fail("flags drained", rv, 8'h06);
        if (irq !== 1'b0) fail("irq drained", irq, 0);
        wr(12'h802, 8'h01);                     // the menu: shift lock off
        #200;
        if (keys[15] !== 1'b0) fail("lock_off", keys[15], 0);
        rd(12'h802); if (rv !== 8'h07) fail("flags lock_off", rv, 8'h07);
        wr(12'h802, 8'h00);
        #200;
        if (keys[15] !== 1'b1) fail("lock back", keys[15], 1);
        // overflow: 18 bytes in, the first 16 kept
        for (i = 0; i < 18; i = i + 1) send(8'h80 + i);
        for (i = 0; i < 16; i = i + 1) begin
            rd(12'h800); if (rv !== 8'h80 + i) fail("fifo order", rv, 8'h80 + i);
            wr(12'h801, 8'h01);
        end
        rd(12'h802); if (rv[7] !== 1'b0) fail("fifo empty", rv, 0);
        wr(12'h803, 8'h00);
        send(8'hC1);                            // ignored by bling_keyboard, SHIFT LOCK stays
        #200 if (irq !== 1'b0) fail("irq off", irq, 1);
        rd(12'h802); if (rv !== 8'h86) fail("flags 0xC1 keeps lock", rv, 8'h86);
        $display("bling rx done, %0d errors", err);

        // ---------------- SHIFT LOCK LED ----------------
        // the RX block above left SHIFT LOCK down (0x41 once, toggled back by nothing)
        frame_check(24'h1E2400, "amber while locked");
        send(8'h41);                            // SHIFT LOCK again: released
        #200;
        rd(12'h802); if (rv[1] !== 1'b0) fail("flags released", rv, 0);
        frame_check(24'h000000, "dark when released");
        send(8'h41);
        #200;
        frame_check(24'h1E2400, "amber again");
        $display("shift lock LED done, %0d errors", err);

        // ---------------- the app's LED registers ----------------
        for (i = 0; i < 256; i = i + 1) begin
            wr(12'h600 + i, 8'hA5 ^ i); wr(12'h900 + i, 8'h5A ^ i);
        end
        for (i = 0; i < 256; i = i + 17) begin
            rd(12'h600 + i); if (rv !== 8'h00) fail("case reg reads 0", rv, 0);
            rd(12'h900 + i); if (rv !== 8'h00) fail("kb led reg reads 0", rv, 0);
        end
        rd(12'h6FF); if (rv !== 8'h00) fail("case busy reads 0", rv, 0);
        rd(12'h9FF); if (rv !== 8'h00) fail("kb busy reads 0", rv, 0);
        rd(12'h802); if (rv !== 8'h86 && rv !== 8'h87) fail("rx flags untouched", rv, 8'h86);
        frame_check(24'h1E2400, "still amber");
        $display("led registers done");

        if (err) $display("FAIL (%0d)", err); else $display("PASS");
        $finish;
    end
    initial begin #60000000; $display("TIMEOUT"); $finish; end
endmodule
