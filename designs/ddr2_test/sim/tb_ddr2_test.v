// The ddr2_test design (top.v: cal_seq, ddr2_tester, JTAG register map) on the DDR2 model with a
// small address space (AW = 10: 1024 bursts, 16 KB): calibration, rounds in both orders with
// write masks and pingpong, the register map through the JTAG bus (forced here: BSCANE2 has no
// model), a bit flipped in the DRAM model during the retention hold (the tester has to find
// it: one error word, the address, expected and actual data), clear, more rounds clean.
//
//   iverilog -g2012 -o tb.vvp tb_ddr2_test.v ../top.v ../cal_seq.v ../ddr2_tester.v \
//     ../../../host/ddr2_lean.v ../../../host/jtag_dbg.v xilinx_models.v ddr2_model.v
`timescale 1ps/1ps
module tb #(
    parameter integer WR_DLY = 900,
    parameter integer RD_DLY = 600,
    parameter integer ROUNDS = 8
);
    localparam AW = 10;
    reg clk50 = 1'b0;
    always #10000 clk50 = ~clk50;

    wire [15:0] dq; wire [1:0] dqs_p, dqs_n; wire [12:0] ddr_a; wire [2:0] ddr_ba;
    wire ras_n, cas_n, we_n; wire [0:0] ck_p, ck_n, cke, cs_n, odt; wire [1:0] dm;
    wire led_b, led_c1, led_c2, a13;
    wire s0, s1, s2, s3;

    top #(.AW(AW)) dut (
        .RMII_REFCLK(clk50), .LED_BOARDn(led_b), .LED_CASE1n(led_c1), .LED_CASE2n(led_c2),
        .SLOT_ADDROEn(s0), .SLOT_DATAOEn(s1), .SLOT_ROMLn(s2), .SLOT_ROMHn(s3),
        .ddr2_dq(dq), .ddr2_dqs_n(dqs_n), .ddr2_dqs_p(dqs_p), .ddr2_addr(ddr_a), .SDRAM_A13(a13),
        .ddr2_ba(ddr_ba), .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n),
        .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n), .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_odt(odt), .ddr2_dm(dm));
    ddr2_board #(.WR_DLY(WR_DLY), .RD_DLY(RD_DLY)) board (
        .ddr2_addr(ddr_a), .ddr2_ba(ddr_ba), .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n),
        .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n), .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_odt(odt),
        .ddr2_dm(dm), .ddr2_dq(dq), .ddr2_dqs_p(dqs_p), .ddr2_dqs_n(dqs_n),
        .fpga_oe_dq(dut.u_ddr.phy.dq[0].tq === 1'b0), .fpga_oe_dqs(dut.u_ddr.phy.dqs[0].tq === 1'b0));

    integer errors = 0;
    task check(input [31:0] got, input [31:0] want, input [8*28-1:0] what);
        begin
            if (got !== want) begin errors = errors + 1; $display("FAIL %0s: %08x, want %08x", what, got, want); end
            else $display("ok   %0s: %08x", what, got);
        end
    endtask

    // the JTAG bus, forced
    reg [23:0] fa = 0; reg [31:0] fd = 0; reg fre = 0, fwe = 0;
    initial begin
        force dut.j_addr = fa; force dut.j_wdata = fd; force dut.j_re = fre; force dut.j_we = fwe;
    end
    always @(*) begin
        force dut.j_addr = fa; force dut.j_wdata = fd; force dut.j_re = fre; force dut.j_we = fwe;
    end
    reg [31:0] jr;
    task jread(input [23:0] a);
        begin
            @(posedge dut.ui_clk); fa = a; fre = 1;
            @(posedge dut.ui_clk); fre = 0;
            @(posedge dut.ui_clk); @(posedge dut.ui_clk);
            jr = dut.j_rdata;
        end
    endtask
    task jwrite(input [23:0] a, input [31:0] d);
        begin
            @(posedge dut.ui_clk); fa = a; fd = d; fwe = 1;
            @(posedge dut.ui_clk); fwe = 0;
            @(posedge dut.ui_clk);
        end
    endtask

    reg [127:0] saved;
    reg [22:0] vb;
    reg [19:0] vix;
    integer tmo;
    initial begin
        wait (dut.cs_done);
        jread(24'h000000); check(jr, 32'hDD2A0002, "ID");
        jread(24'h000001); $display("STATUS %h", jr);
        if (!jr[2]) begin $display("FAIL: calibration failed"); $finish; end
        jread(24'h000002); $display("CAL0 %h: tap0 %0d tap1 %0d dly %0d bs %0d", jr, jr[4:0], jr[12:8], jr[18:16], jr[27:24]);
        jread(24'h000003); $display("CAL1 %h: width0 %0d width1 %0d trunc %b%b hits %0d", jr, jr[5:0], jr[13:8], jr[17], jr[16], jr[23:20]);
        jread(24'h000013); check(jr, AW, "AW");
        jread(24'h000100); check(jr[31:16], 16'hDD20, "lean id");
        jread(24'h000101); check(jr[4:0], 5'h1D, "lean CTRL");
        jread(24'h000103); check(jr[4:0], dut.res_tap0, "lean TAP");
        jread(24'h000104); check(jr[2:0], dut.res_dly, "lean RDDLY");
        // one retention hold, long enough to hit it
        jwrite(24'h000021, 32'd3000);
        // wait for the first hold, flip a bit in the model's copy of a burst that pass 0/1 wrote
        wait (dut.tester.st == 4'd6);
        vb = 23'd777; vix = {vb[22:10], vb[2:0], vb[9:3]};
        saved = board.dram.mem[vix];
        board.dram.mem[vix] = saved ^ (128'd1 << 77);
        $display("TB: flipped bit 77 of burst %0d in round %0d", vb, dut.tester.round);
        wait (dut.tester.round == 1);
        jread(24'h000005); check(jr, 1, "one error word");
        jread(24'h000006); check(jr, 777, "first error burst");
        jread(24'h000009); $display("expected[31:0] %h", jr);
        begin : cmpx
            reg [127:0] ex, ac;
            jread(24'h000009); ex[31:0] = jr; jread(24'h00000A); ex[63:32] = jr; jread(24'h00000B); ex[95:64] = jr; jread(24'h00000C); ex[127:96] = jr;
            jread(24'h00000D); ac[31:0] = jr; jread(24'h00000E); ac[63:32] = jr; jread(24'h00000F); ac[95:64] = jr; jread(24'h000010); ac[127:96] = jr;
            if ((ex ^ ac) !== (128'd1 << 77)) begin errors = errors + 1; $display("FAIL first error data: exp %h act %h", ex, ac); end
            else $display("ok   first error data differs in bit 77");
        end
        jread(24'h000007); check(jr, 0, "first error round");
        jread(24'h000008); check(jr, 2, "first error pass (read)");
        // clear, no hold, the rest must be clean
        jwrite(24'h000022, 0);
        jwrite(24'h000021, 32'd0);
        repeat (4) @(posedge dut.ui_clk);
        jread(24'h000005); check(jr, 0, "errors after clear");
        tmo = 0;
        while (dut.tester.rounds < ROUNDS && tmo < 40000) begin #1000000; tmo = tmo + 1; end
        jread(24'h000004); $display("rounds %0d", jr);
        jread(24'h000005); check(jr, 0, "errors at the end");
        jread(24'h000011); $display("commands %0d", jr);
        jread(24'h000001); $display("STATUS %h", jr);
        if (dut.tester.rounds < ROUNDS) begin errors = errors + 1; $display("FAIL: only %0d rounds", dut.tester.rounds); end
        // both orders and pingpong ran: the toggling control alternates the order
        $display("order now %0d, LEDs: case1 %b case2 %b", dut.tester.ord, led_c1, led_c2);
        if (board.dram.errors != 0 || board.contention != 0) begin errors = errors + 1; $display("FAIL: DRAM errors %0d contention %0d", board.dram.errors, board.contention); end
        $display("DRAM: refreshes %0d, activates %0d, reads %0d, writes %0d, longest refresh gap %0d ns",
                 board.dram.n_ref, board.dram.n_act, board.dram.n_rd, board.dram.n_wr, board.dram.max_ref_gap / 1000);
        if (errors) $display("FAILED %0d", errors);
        else $display("PASS ddr2_test: calibration, %0d rounds (both orders, masks, pingpong), injected bit flip found and cleared, JTAG map", dut.tester.rounds);
        $finish;
    end
    initial begin #(20000000000); $display("FAIL: TIMEOUT"); $finish; end
endmodule
