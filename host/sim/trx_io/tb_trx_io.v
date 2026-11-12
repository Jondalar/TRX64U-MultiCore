// The app's side of our IO block (iverilog, run.sh): trx_io.v in the app's
// 100 MHz clock with trx_sys.v in the C64's 31.5 MHz clock, as top.v joins
// them, driven as ultimate_sys drives it (one byte access at a time, the
// next after the ack). Checks: page 0 below 0x100 answers in one clock, the
// pages in the C64 clock over the handshake; 32-bit registers commit on byte
// 3 and latch on byte 0; SB_DATA reads the state bus word and increments
// SB_ADDR; the stop and capture toggles set IRQ_CAUSE b0/b3 and the line.
`timescale 1ns / 1ps
module tb_trx_io;
    reg cpu_clk = 0, sys_clk = 0;
    always #5 cpu_clk = ~cpu_clk;           // 100 MHz
    always #15.859 sys_clk = ~sys_clk;      // 31.5 MHz

    reg         we = 0, re = 0;
    reg  [16:0] addr = 0;
    reg   [7:0] wdata = 0;
    wire  [7:0] rdata;
    wire        ack, irq;
    wire        rq_tgl, rq_we, rs_tgl;
    wire [15:0] rq_addr;
    wire  [7:0] rq_wdata, rs_rdata;
    reg         stop_tgl = 0, cap_tgl = 0;
    wire  [1:0] rom_sel;
    reg         evt_tgl = 1'b0;
    reg  [33:0] evt_word = 34'd0;

    trx_io tio (
        .clk(cpu_clk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata), .ack(ack), .irq(irq),
        .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata),
        .rs_tgl(rs_tgl), .rs_rdata(rs_rdata), .stop_tgl(stop_tgl), .cap_tgl(cap_tgl), .eng_tgl(1'b0), .gap_tgl(1'b0),
        .fl_enable(), .fl_magic1(), .fl_magic2(), .fl_mask(), .fl_id(),
        .evt_tgl(evt_tgl), .evt_word(evt_word), .fl_ack_tgl(), .inval_tgl(), .rom_sel(rom_sel), .ee_tgl(1'b0),
        .ee_addr(), .ee_we(), .ee_wdata(), .ee_rdata(8'd0),
        .p6_we(), .p6_re(), .p6_rdata(8'd0), .ev_vsync(1'b0), .ev_flip(1'b0),
        .build(32'h6A1B2C3D), .ddr_cal(64'hCAFE0123_C0FFEE45)
    );

    // the state bus: word = {sb_addr, 20'hABCDE}, two clocks after the address
    wire [11:0] sb_addr;
    reg  [11:0] sb_q1 = 0;
    reg  [31:0] sb_rdata = 0;
    always @(posedge sys_clk) begin sb_q1 <= sb_addr; sb_rdata <= {sb_q1, 20'hABCDE}; end
    wire gate_frz;
    wire [7:0] cap_every;
    wire [15:0] tx_view;
    trx_sys tsys (
        .clk(sys_clk),
        .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata),
        .rs_tgl(rs_tgl), .rs_rdata(rs_rdata),
        .j_re(1'b0), .j_we(1'b0), .j_addr(12'd0), .j_wdata(32'd0), .j_rdata(), .j_rvalid(),
        .gate_frz(gate_frz), .cap_every(cap_every),
        .p2_we(), .p2_re(), .p2_addr(), .p2_wdata(), .p2_rdata(32'd0),
        .seq_sb_req(1'b0), .seq_sb_addr(12'd0), .seq_sb_gnt(),
        .sb_addr(sb_addr), .sb_rdata(sb_rdata),
        .sb_frozen(1'b1), .halted(1'b1), .cap_gen(16'h1234), .cap_nosnap(16'h0005),
        .tx_view(tx_view)
    );

    integer fails = 0, clocks;
    task access(input w, input [16:0] a, input [7:0] d, output [7:0] q);
        begin
            @(posedge cpu_clk) #1;
            addr = a; wdata = d; we = w; re = !w;
            @(posedge cpu_clk) #1;
            we = 0; re = 0;
            clocks = 1;
            while (!ack) begin @(posedge cpu_clk) #1; clocks = clocks + 1; end
            q = rdata;
        end
    endtask
    reg [7:0] q;
    reg [31:0] v, w;
    integer i;
    task rd32(input [16:0] a, output [31:0] r);
        integer i;
        begin
            for (i = 0; i < 4; i = i + 1) begin access(0, a + i, 0, q); r[8 * i +: 8] = q; end
        end
    endtask
    task wr32(input [16:0] a, input [31:0] d);
        integer i;
        begin
            for (i = 0; i < 4; i = i + 1) access(1, a + i, d[8 * i +: 8], q);
        end
    endtask
    task check(input ok, input [8*48-1:0] what);
        if (!ok) begin fails = fails + 1; $display("FAIL: %0s", what); end
    endtask


    // Two masters' 32-bit accesses interleaved byte by byte (a firmware task and the ring poll, a task
    // and an app): each reads or writes its own registers. A, reads DDR_CAL0 (0xC0FFEE45), writes CAP_CTRL
    // (byte 0 below 0x80); B reads DDR_CAL1 (0xCAFE0123), writes RESUME_PHASE (byte 0 from 0x80). A torn read gives
    // the other register's bytes 1-3, a torn write the other's bytes 0-2.
    integer ia, ib, na, nb, step, ma, mb, torn_r, torn_w, seed, ev_a, ev_b;
    reg [31:0] va, vb, ra, rb;
    reg [7:0] pcap = 8'h00;
    reg [19:0] pphase = 20'd0;
    always @(posedge sys_clk) begin
        if (cap_every != pcap) begin
            pcap <= cap_every;
            if (cap_every >= 8'h80) begin torn_w = torn_w + 1; $display("torn write of CAP_CTRL: %02X", cap_every); end
            ev_a = ev_a + 1;
        end
        if (tsys.resume_phase != pphase) begin
            pphase <= tsys.resume_phase;
            if (!tsys.resume_phase[7]) begin torn_w = torn_w + 1; $display("torn write of RESUME_PHASE: %05X", tsys.resume_phase); end
            ev_b = ev_b + 1;
        end
    end
    task interleave(input integer rounds);
        integer r;
        begin
            ia = 4; ib = 4; ma = 0; mb = 0; torn_r = 0; torn_w = 0; ev_a = 0; ev_b = 0;
            for (r = 0; r < rounds * 8; r = r + 1) begin
                step = $random(seed);
                if (ia == 4) begin ma = $random(seed) & 1; ia = 0; ra = 0; va = {8'h11, 8'h22, 8'h33, 8'h00} | (($random(seed) & 8'h7F)); end
                if (ib == 4) begin mb = $random(seed) & 1; ib = 0; rb = 0; vb = {8'h00, 8'h44, 8'h55, 8'h80 | (8'h7F & $random(seed))}; end
                if (step[0]) begin                    // A's next byte
                    if (ma) access(1, 17'h00104 + ia, va[8 * ia +: 8], q);
                    else begin access(0, 17'h00020 + ia, 0, q); ra[8 * ia +: 8] = q; end
                    ia = ia + 1;
                    if (ia == 4 && !ma && ra != 32'hC0FFEE45) begin torn_r = torn_r + 1; $display("torn read A: %08X", ra); end
                end else begin                        // B's
                    if (mb) access(1, 17'h00110 + ib, vb[8 * ib +: 8], q);
                    else begin access(0, 17'h00024 + ib, 0, q); rb[8 * ib +: 8] = q; end
                    ib = ib + 1;
                    if (ib == 4 && !mb && rb != 32'hCAFE0123) begin torn_r = torn_r + 1; $display("torn read B: %08X", rb); end
                end
            end
            repeat (40) @(posedge cpu_clk);
        end
    endtask

    initial begin
        #200;
        rd32(17'h00000, v);
        $display("ID %08X (%0d clocks for its last byte)", v, clocks);
        check(v == 32'h32585254 && clocks == 1, "ID, one clock");
        access(0, 17'h00004, 0, q);
        check(q == 8'hFF, "CAPS");
        access(0, 17'h00005, 0, q);
        check(q == 8'hF7, "CAPS2: F4 after F3, BUILD, CPU write path, lens unit, APPLY, trace former; b3 (freeze session) is gone, b6 = turbo");
        rd32(17'h00008, v);
        $display("BUILD %08X", v);
        check(v == 32'h6A1B2C3D, "BUILD: the USR_ACCESS stamp");
        rd32(17'h00020, v); rd32(17'h00024, w);
        $display("DDR_CAL %08X %08X", w, v);
        check(v == 32'hC0FFEE45 && w == 32'hCAFE0123 && clocks == 1, "DDR_CAL0/1: the calibration result words, one clock");
        rd32(17'h00100, v);
        $display("GATE_CTRL %08X (%0d clocks for its last byte)", v, clocks);
        check(v == 32'h00000300 && gate_frz == 0, "GATE_CTRL reset value");
        // bytes 0-2 only collect
        access(1, 17'h00100, 8'h01, q);
        repeat (20) @(posedge cpu_clk);
        check(gate_frz == 0, "GATE_CTRL took byte 0 alone");
        access(1, 17'h00101, 8'h00, q); access(1, 17'h00102, 8'h00, q); access(1, 17'h00103, 8'h00, q);
        repeat (20) @(posedge cpu_clk);
        check(gate_frz == 1, "GATE_CTRL b0 not set by the byte 3 commit");
        rd32(17'h00100, v);
        check(v == 32'h00000301, "GATE_CTRL read back");
        rd32(17'h00108, v);
        check(v == 32'h00051234, "CAP_STAT");
        // the freeze session registers are gone (SLIM D1, D3): they read 0 and ignore writes
        v = 0;
        for (i = 0; i < 8; i = i + 1) begin rd32(17'h05000 + 4 * i, w); v = v | w; end
        check(v == 32'd0, "SH_CTRL .. SH_COMMIT read 0");
        wr32(17'h05000, 32'h0000030F);
        rd32(17'h05000, v);
        check(v == 32'd0, "SH_CTRL ignores writes");
        rd32(17'h05080, v);
        check(v == 32'd0, "the overlay window reads 0");
        // TX_VIEW: the text layer's view of the VIC (reset value = the freeze menu's)
        rd32(17'h05028, v);
        check(v == 32'h00000243 && tx_view == 16'h0243, "TX_VIEW reset value");
        wr32(17'h05028, 32'h0000A5C3);
        rd32(17'h05028, v);
        check(v == 32'h0000A5C3 && tx_view == 16'hA5C3, "TX_VIEW read back and out");
        // the state bus
        wr32(17'h01000, 32'h00000103);
        rd32(17'h01004, v);
        $display("SB_DATA at 103: %08X (%0d clocks for byte 3)", v, clocks);
        check(v == {12'h103, 20'hABCDE}, "SB_DATA");
        rd32(17'h01004, v);
        check(v == {12'h104, 20'hABCDE}, "SB_DATA after the increment");
        rd32(17'h01000, v);
        check(v == 32'h105, "SB_ADDR");
        // interrupt causes
        wr32(17'h00010, 32'h00000009);          // IRQ_EN b0, b3
        repeat (5) @(posedge cpu_clk);
        check(irq == 0, "irq without a cause");
        stop_tgl = 1;
        repeat (40) @(posedge cpu_clk);
        access(0, 17'h00014, 0, q);
        check(q == 8'h01 && irq == 1, "stop cause");
        access(1, 17'h00014, 8'h01, q);
        repeat (5) @(posedge cpu_clk);
        check(irq == 0, "W1C");
        cap_tgl = 1;
        repeat (40) @(posedge cpu_clk);
        access(0, 17'h00014, 0, q);
        check(q == 8'h08 && irq == 1, "capture cause");
        // T60: ROM_SEL (page 4, 0x030): 0 at reset, 2 bits, read back, out to cart_fill
        access(0, 17'h04030, 0, q);
        check(q == 8'h00 && rom_sel == 2'd0, "ROM_SEL reset value 0");
        access(1, 17'h04030, 8'hFD, q); access(0, 17'h04030, 0, q);
        check(q == 8'h01 && rom_sel == 2'd1, "ROM_SEL: bits 1:0 only");
        access(1, 17'h04030, 8'h02, q); access(0, 17'h04030, 0, q);
        check(q == 8'h02 && rom_sel == 2'd2, "ROM_SEL 2");
        access(1, 17'h04030, 8'h00, q);
        // T51: XPORT_CAPS, the event word's two high bits (EVT_HI), the EF3 buttons
        access(0, 17'h04038, 0, q);
        check(q == 8'h03, "XPORT_CAPS: 8 MiB window and EasyFlash 3 banking");
        evt_word = {2'b10, 2'b10, 1'b0, 21'h15A5A5, 8'h3C};
        evt_tgl = 1'b1;
        repeat (20) @(posedge cpu_clk);
        access(0, 17'h04020, 0, q); check(q == 8'h3C, "EVT byte 0");
        access(0, 17'h04021, 0, q); check(q == 8'hA5, "EVT byte 1");
        access(0, 17'h04022, 0, q); check(q == 8'hA5, "EVT byte 2");
        access(0, 17'h04023, 0, q); check(q == 8'h95, "EVT byte 3 (cmd, chip, address 28:24)");
        access(0, 17'h04024, 0, q); check(q == 8'h02, "EVT_HI: address 22:21");
        seed = 12345;
        interleave(2000);
        $display("interleaved: %0d torn reads, %0d torn writes (%0d CAP_CTRL, %0d RESUME_PHASE commits)", torn_r, torn_w, ev_a, ev_b);
        check(torn_r == 0 && torn_w == 0, "interleaved 32-bit accesses of two masters");
        check(ev_a > 100 && ev_b > 100, "interleave exercised the writes");
        if (fails == 0) $display("PASS"); else $display("%0d FAILED", fails);
        $finish;
    end
endmodule
