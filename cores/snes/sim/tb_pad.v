// T156: the application's USB pad registers of the SNES core (snes_regs PAD1/PAD2) through the controller (snes_pad): the real register decode, the real
// serial protocol as the S-CPU drives it (latch while JOY_STRB is high, 16 shifts with the port clock). Fast (iverilog), exhaustive where the core bench
// ($4218.. in tb_snes.v +PADAPP) is one pattern: each of the twelve buttons alone on each player (S2), all twelve, the OR with the joystick and key sources
// (S4), the pad pulled (S5), the high byte that commits the pair, the registers read back. With -DSNES_MUT_NOAPP the pad registers reach nothing (S6: the
// bench must FAIL), with -DSNES_MUT_PADORDER B and Y of port 1 are swapped (the bench must FAIL).
`timescale 1ns / 1ps
module tb_pad;
    reg clk = 0;
    always #5 clk = ~clk;
    reg we = 0, re = 0;
    reg [16:0] addr = 0;
    reg [7:0] wdata = 0;
    wire [7:0] rdata;
    wire ack, irq;
    wire [15:0] pad1, pad2;
    reg [4:0] joy1 = 0, joy2 = 0;
    reg [63:0] keys = 0;
    reg strobe = 0, clk1 = 0, clk2 = 0;
    wire [1:0] di1, di2;
    reg rst = 1;

    snes_regs regs (.clk(clk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata), .ack(ack), .irq(irq), .build(32'd0), .ddr_cal(64'd0),
        .px_we(), .px_re(), .px_rdata(8'd0), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_frame(1'b0), .ctrl(), .sys_cfg(), .rom_type(), .rom_mask(), .ram_mask(),
        .rtc(), .cart_base(), .pad1(pad1), .pad2(pad2), .ipl_wr(), .ipl_wa(), .ipl_wd(), .st_miss_rom(32'd0), .st_miss_wram(32'd0), .st_miss_bs(32'd0),
        .st_stall(32'd0), .st_wr(32'd0), .fb_base(), .fb_stride(), .status(8'd0), .frames(16'd0), .romaddr(16'd0), .fb_latest(8'd0), .fb_seq(8'd0), .fb_over(16'd0));
    snes_pad pad (.clk(clk), .rst(rst), .joy1(joy1), .joy2(joy2), .keys(keys), .app1(pad1), .app2(pad2), .strobe(strobe), .clk1(clk1), .clk2(clk2), .di1(di1), .di2(di2));

    integer fails = 0, checks = 0;
    task check(input ok, input [8*60-1:0] what);
        begin checks = checks + 1; if (!ok) begin fails = fails + 1; $display("FAIL %0s", what); end end
    endtask

    task wr(input [11:0] a, input [7:0] d);
        begin @(posedge clk); addr <= {5'd0, a}; wdata <= d; we <= 1; @(posedge clk); we <= 0; end
    endtask
    reg [7:0] rb;
    task rd(input [11:0] a);
        begin @(posedge clk); addr <= {5'd0, a}; re <= 1; @(posedge clk); re <= 0; @(posedge clk); @(posedge clk); rb = rdata; end
    endtask
    // the application's write of a 16 bit word: low byte, then the high byte
    task app(input integer player, input [15:0] w);
        begin
            wr(player == 1 ? 12'h0F4 : 12'h0F6, w[7:0]);
            wr(player == 1 ? 12'h0F5 : 12'h0F7, w[15:8]);
        end
    endtask

    // one controller read as the S-CPU does it: latch, then 16 bits, the first is B; ones for a released button
    reg [15:0] got1, got2;
    integer k;
    task read_ports;
        begin
            repeat (4) @(posedge clk);                      // the joystick and key synchronisers
            strobe <= 1; repeat (3) @(posedge clk); strobe <= 0; repeat (3) @(posedge clk);
            for (k = 0; k < 16; k = k + 1) begin
                got1[k] = di1[0]; got2[k] = di2[0];
                clk1 <= 1; clk2 <= 1; repeat (3) @(posedge clk); clk1 <= 0; clk2 <= 0; repeat (3) @(posedge clk);
            end
        end
    endtask

    integer n;
    initial begin
        repeat (10) @(posedge clk);
        rst = 0;
        repeat (10) @(posedge clk);
        read_ports; check(got1 === 16'hFFFF && got2 === 16'hFFFF, "nothing pressed: both ports read all ones");
        // S2: each of the twelve buttons alone, player 1 then player 2; the other port stays released
        for (n = 0; n < 12; n = n + 1) begin
            app(1, 16'd1 << n); read_ports;
            check(got1 === (16'hFFFF ^ (16'd1 << n)) && got2 === 16'hFFFF, "player 1: exactly this serial bit goes low");
            if (got1 !== (16'hFFFF ^ (16'd1 << n))) $display("     button %0d: port 1 read %h", n, got1);
            app(1, 16'd0);
            app(2, 16'd1 << n); read_ports;
            check(got2 === (16'hFFFF ^ (16'd1 << n)) && got1 === 16'hFFFF, "player 2: exactly this serial bit goes low");
            if (got2 !== (16'hFFFF ^ (16'd1 << n))) $display("     button %0d: port 2 read %h", n, got2);
            app(2, 16'd0);
        end
        app(1, 16'h0FFF); app(2, 16'h0FFF); read_ports;
        check(got1 === 16'hF000 && got2 === 16'hF000, "all twelve buttons: twelve zeros, four ones");
        // S5: the pad is pulled (the application writes zero): released at once, the other player is untouched
        app(1, 16'h0000); read_ports;
        check(got1 === 16'hFFFF && got2 === 16'hF000, "player 1 pulled: released, player 2 still pressed");
        app(2, 16'h0000); read_ports;
        check(got1 === 16'hFFFF && got2 === 16'hFFFF, "both pulled: released");
        // S4: the OR with the joystick and key sources: fire = B and SPACE = A on port 1, the pad adds Y and X; releasing the pad releases only its bits
        joy1 = 5'b10000; keys[60] = 1'b1; app(1, 16'h0202); read_ports;
        check(got1 === ~(16'h0001 | 16'h0002 | 16'h0100 | 16'h0200), "B (fire), Y (pad), A (SPACE), X (pad) together");
        app(1, 16'h0000); read_ports;
        check(got1 === ~(16'h0001 | 16'h0100), "pad released: the fire button and SPACE stay");
        joy1 = 0; keys = 0; read_ports;
        check(got1 === 16'hFFFF, "everything released");
        // the high byte commits: a low byte alone changes nothing, the pair is never half old
        app(1, 16'h0F00); wr(12'h0F4, 8'hFF); read_ports;
        check(got1 === (16'hFFFF ^ 16'h0F00), "the low byte alone is not taken");
        wr(12'h0F5, 8'h00); read_ports;
        check(got1 === (16'hFFFF ^ 16'h00FF), "the high byte takes the pair");
        // the registers read back
        app(1, 16'h0A85); app(2, 16'h0518);
        rd(12'h0F4); check(rb === 8'h85, "PAD1 low reads back");
        rd(12'h0F5); check(rb === 8'h0A, "PAD1 high reads back");
        rd(12'h0F6); check(rb === 8'h18, "PAD2 low reads back");
        rd(12'h0F7); check(rb === 8'h05, "PAD2 high reads back");
        $display("     %0d checks, %0d failed", checks, fails);
        if (fails == 0) $display("PASS tb_snes_pad");
        else $display("FAIL tb_snes_pad");
        $finish;
    end
    initial begin #50000000; $display("FAIL tb_snes_pad: timeout"); $finish; end
endmodule
