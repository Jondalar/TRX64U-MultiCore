// T156: the application's USB pad registers of the Neo Geo core (neo_regs PAD1/PAD2, 0F0-0F3) through the controller (neo_pad) into the machine's joystick words
// (MiSTer layout: b0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D, 8 Start, 9 Select, 10 Coin): the real register decode, the real clock crossing (cpu_clk to the
// machine's 48 MHz). Each of the eleven buttons alone on each player, all of them, the OR with the joystick and key sources, the pad pulled, the high byte that commits the
// pair, the port swap (it moves the host's joysticks and not the pads), the registers read back.
`timescale 1ns / 1ps
module tb_pad;
    reg clk = 0, clk48 = 0;
    always #5 clk = ~clk;
    always #10.4 clk48 = ~clk48;
    reg we = 0, re = 0;
    reg [16:0] addr = 0;
    reg [7:0] wdata = 0;
    wire [7:0] rdata;
    wire ack, irq;
    wire [15:0] app1, app2, pad0, pad1;
    reg [4:0] joy1 = 0, joy2 = 0;
    reg [63:0] keys = 0;
    reg swap = 0;

    neo_regs regs (.clk(clk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata), .ack(ack), .irq(irq), .build(32'd0), .ddr_cal(64'd0), .px_we(), .px_re(), .px_rdata(8'd0), .ev_vsync(1'b0), .ev_flip(1'b0), .ev_frame(1'b0), .ev_mover(1'b0), .ctrl(), .system(), .dip(), .pad_swap(), .pad1(app1), .pad2(app2), .cfg(), .fb_base(), .fb_stride(), .rom_base(), .v1_base(), .v2_base(), .m1_base(), .img_base(), .mv_cmd(), .cp_src(), .cp_dst(), .cp_len(), .mv_go_tgl(), .mv_done(8'd0), .rtc(), .p2_mask(), .c_mask(), .v1_mask(), .v2_mask(), .m_mask(), .c_start(), .c_dl(), .s_dl(), .save_ack_tgl(), .status(8'd0), .fb_latest(8'd0), .fb_seq(8'd0), .fb_over(16'd0), .seen_cols(16'd0), .seen_lines(16'd0), .st_stall_clk(32'd0), .st_c_late(32'd0), .st_s_late(32'd0), .st_p_reads(32'd0), .st_c_cmds(32'd0), .st_e_cmds(32'd0), .st_va_miss(32'd0), .st_mz_miss(32'd0), .st_c_lat_max(8'd0), .st_c_max(8'd0), .st_s_max(8'd0), .pck_age(8'd0), .st_pf(352'd0));
    neo_pad pad (.clk(clk48), .joy1(joy1), .joy2(joy2), .keys(keys), .swap(swap), .app1(app1), .app2(app2), .pad0(pad0), .pad1(pad1));

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
            wr(player == 1 ? 12'h0F0 : 12'h0F2, w[7:0]);
            wr(player == 1 ? 12'h0F1 : 12'h0F3, w[15:8]);
        end
    endtask
    task settle; begin repeat (8) @(posedge clk48); end endtask

    integer n;
    initial begin
        repeat (10) @(posedge clk);
        settle;
        check(pad0 === 16'h0000 && pad1 === 16'h0000, "nothing pressed: both words zero");
        for (n = 0; n < 11; n = n + 1) begin
            app(1, 16'd1 << n); settle;
            check(pad0 === (16'd1 << n) && pad1 === 16'h0000, "player 1: exactly this bit of port 0");
            if (pad0 !== (16'd1 << n)) $display("     button %0d: port 0 read %h", n, pad0);
            app(1, 16'd0);
            app(2, 16'd1 << n); settle;
            check(pad1 === (16'd1 << n) && pad0 === 16'h0000, "player 2: exactly this bit of port 1");
            if (pad1 !== (16'd1 << n)) $display("     button %0d: port 1 read %h", n, pad1);
            app(2, 16'd0);
        end
        app(1, 16'h07FF); app(2, 16'h07FF); settle;
        check(pad0 === 16'h07FF && pad1 === 16'h07FF, "all eleven buttons");
        app(1, 16'h0000); settle;
        check(pad0 === 16'h0000 && pad1 === 16'h07FF, "player 1 pulled: released, player 2 still pressed");
        app(2, 16'h0000); settle;
        check(pad0 === 16'h0000 && pad1 === 16'h0000, "both pulled: released");
        // the OR with the joystick and key sources: fire = A and SPACE = B on port 0, the pad adds C and Start; releasing the pad releases only its bits
        joy1 = 5'b10000; keys[60] = 1'b1; app(1, 16'h0140); settle;
        check(pad0 === (16'h0010 | 16'h0020 | 16'h0040 | 16'h0100), "A (fire), B (SPACE), C and Start (pad) together");
        app(1, 16'h0000); settle;
        check(pad0 === (16'h0010 | 16'h0020), "pad released: the fire button and SPACE stay");
        joy1 = 0; keys = 0; settle;
        check(pad0 === 16'h0000, "everything released");
        // the port swap moves the host's joysticks, not the pads
        joy1 = 5'b00001; app(2, 16'h0004); swap = 1; settle;
        check(pad0 === 16'h0000 && pad1 === (16'h0008 | 16'h0004), "swapped: the stick moved to port 1, the pad of player 2 stayed there");
        swap = 0; joy1 = 0; app(2, 16'h0000);
        // the high byte commits: a low byte alone changes nothing, the pair is never half old
        app(1, 16'h0700); wr(12'h0F0, 8'hFF); settle;
        check(pad0 === 16'h0700, "the low byte alone is not taken");
        wr(12'h0F1, 8'h00); settle;
        check(pad0 === 16'h00FF, "the high byte takes the pair");
        // the registers read back
        app(1, 16'h0585); app(2, 16'h0218);
        rd(12'h0F0); check(rb === 8'h85, "PAD1 low reads back");
        rd(12'h0F1); check(rb === 8'h05, "PAD1 high reads back");
        rd(12'h0F2); check(rb === 8'h18, "PAD2 low reads back");
        rd(12'h0F3); check(rb === 8'h02, "PAD2 high reads back");
        $display("     %0d checks, %0d failed", checks, fails);
        if (fails == 0) $display("PASS tb_neo_pad");
        else $display("FAIL tb_neo_pad");
        $finish;
    end
    initial begin #50000000; $display("FAIL tb_neo_pad: timeout"); $finish; end
endmodule
