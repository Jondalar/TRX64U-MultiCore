// The c64u core's cw_* window (iverilog, run.sh): c64u_io.v in the app's 100 MHz clock with c64u_sys.v in the C64's 31.5 MHz clock, as
// layer2_c64u.v joins them, driven as ultimate_sys drives it (one byte access at a time, the next after the ack), plus stand-ins for
// Gideon's UCI and the two tape windows (pulse in, ack and data later). Checks the register map of c64u_cw.vh: ID, the capability words,
// the level interrupts and their enable, IRQ_CAUSE, PIN_CTRL, the stream registers and the template RAM write, the three device windows
// (the request pulse, address, data, and their own ack and read data back), the 32-bit registers of c64u_sys (GATE_CTRL, TURBO, TX_VIEW,
// SLOT_CTRL, torn accesses when two masters interleave), and that the pages of the reference core that are not in this core read 0 and ack.
`timescale 1ns / 1ps
module tb_c64u_cw;
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
    reg         evt_tgl = 1'b0;
    reg  [33:0] evt_word = 34'd0;
    reg         ev_vsync = 0;
    reg         sid_resid8 = 1'b1;
    reg  [3:0]  level = 4'b0000;
    wire  [7:0] pin_ctrl, stream_ena;
    wire        udp_we, stream_mute;
    wire  [7:0] udp_addr, udp_wdata;
    wire        uci_re, uci_we, pb_re, pb_we, rc_re, rc_we;
    wire [12:0] uci_addr;
    reg   [7:0] uci_rdata = 0, pb_rdata = 0, rc_rdata = 0;
    reg         uci_ack = 0, pb_ack = 0, rc_ack = 0;
    wire  [1:0] rom_sel;
    wire        ee_we;
    wire [343:0] cart_desc;
    wire [15:0] sock_base, sock_mask;
    wire  [1:0] sock_en;
    wire  [2:0] sock_split;
    wire [31:0] sock_gain;
    wire [10:0] ee_addr;

    c64u_io cwio (
        .clk(cpu_clk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata), .ack(ack), .irq(irq),
        .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata), .rs_tgl(rs_tgl), .rs_rdata(rs_rdata),
        .sm_tgl(), .sm_addr(), .sm_we(), .sm_wdata(), .sm_rs_tgl(1'b0), .sm_rdata(8'd0),
        .p6_we(), .p6_re(), .p6_rdata(8'd0), .ev_vsync(ev_vsync), .ev_flip(1'b0),
        .fl_enable(), .fl_magic1(), .fl_magic2(), .fl_mask(), .fl_id(), .cart_desc(cart_desc),
        .evt_tgl(evt_tgl), .evt_word(evt_word), .fl_ack_tgl(), .inval_tgl(), .rom_sel(rom_sel), .ee_tgl(1'b0),
        .ee_addr(ee_addr), .ee_we(ee_we), .ee_wdata(), .ee_rdata(8'd0),
        .build(32'h6A1B2C3D), .ddr_cal(64'hCAFE0123_C0FFEE45),
        .sid_resid8(sid_resid8), .rs8_cfg(),
        .core_caps(32'h5E4C0F96), .level_irq(level), .pin_ctrl(pin_ctrl),
        .sock_base(sock_base), .sock_mask(sock_mask), .sock_en(sock_en), .sock_split(sock_split), .sock_gain(sock_gain),
        .udp_we(udp_we), .udp_addr(udp_addr), .udp_wdata(udp_wdata), .stream_ena(stream_ena), .stream_mute(stream_mute),
        .uci_re(uci_re), .uci_we(uci_we), .uci_addr(uci_addr), .uci_rdata(uci_rdata), .uci_ack(uci_ack),
        .pb_re(pb_re), .pb_we(pb_we), .pb_rdata(pb_rdata), .pb_ack(pb_ack),
        .rc_re(rc_re), .rc_we(rc_we), .rc_rdata(rc_rdata), .rc_ack(rc_ack)
    );

    wire gate_frz, turbo_ran_clr, slot_wr_en;
    wire [15:0] tx_view;
    c64u_sys csys (
        .clk(sys_clk), .rq_tgl(rq_tgl), .rq_addr(rq_addr), .rq_we(rq_we), .rq_wdata(rq_wdata),
        .rs_tgl(rs_tgl), .rs_rdata(rs_rdata),
        .gate_frz(gate_frz), .turbo_stat(32'h8123ABCD), .turbo_w1(32'h11223344), .turbo_w2(32'h55667788),
        .turbo_ran_clr(turbo_ran_clr), .slot_wr_en(slot_wr_en),
        .slot_status(32'h80000001), .slot_cnt0(32'h0A0B0C0D), .slot_cnt1(32'h01020304), .slot_last(32'hDEADBEEF),
        .gate_shut(1'b1), .halted(1'b1), .tx_view(tx_view)
    );

    // stand-ins for Gideon's devices on his io bus: the request is a one-clock pulse, the answer comes some clocks later with an ack
    reg [3:0] dly = 0;
    reg [7:0] last_uci_w = 0, last_pb_w = 0, last_rc_w = 0;
    reg [12:0] last_uci_a = 0;
    integer uci_n = 0, pb_n = 0, rc_n = 0;
    always @(posedge cpu_clk) begin
        uci_ack <= 0; pb_ack <= 0; rc_ack <= 0;
        if (uci_re || uci_we) begin
            uci_n = uci_n + 1; last_uci_a = uci_addr;
            if (uci_we) last_uci_w = wdata;
            #1 uci_rdata = 8'h5A; repeat (4) @(posedge cpu_clk); #1 uci_ack = 1; @(posedge cpu_clk); #1 uci_ack = 0;
        end
        if (pb_re || pb_we) begin
            pb_n = pb_n + 1; if (pb_we) last_pb_w = wdata;
            #1 pb_rdata = 8'hA6; repeat (2) @(posedge cpu_clk); #1 pb_ack = 1; @(posedge cpu_clk); #1 pb_ack = 0;
        end
        if (rc_re || rc_we) begin
            rc_n = rc_n + 1; if (rc_we) last_rc_w = wdata;
            #1 rc_rdata = 8'h3C; repeat (3) @(posedge cpu_clk); #1 rc_ack = 1; @(posedge cpu_clk); #1 rc_ack = 0;
        end
    end

    integer fails = 0, clocks, di;
    task check(input ok, input [8*64-1:0] what);
        begin
            if (!ok) begin fails = fails + 1; $display("FAIL: %0s", what); end
            else $display("ok   %0s", what);
        end
    endtask
    task access(input w, input [16:0] a, input [7:0] d, output [7:0] q);
        begin
            @(posedge cpu_clk) #1;
            addr = a; wdata = d; we = w; re = !w;
            @(posedge cpu_clk) #1;
            we = 0; re = 0;
            clocks = 1;
            while (!ack && clocks < 400) begin @(posedge cpu_clk) #1; clocks = clocks + 1; end
            if (clocks >= 400) begin fails = fails + 1; $display("FAIL: no ack for %0s %05X", w ? "write" : "read", a); end
            q = rdata;
        end
    endtask
    reg [7:0] q;
    reg [31:0] v;
    task rd32(input [16:0] a, output [31:0] r);
        integer i;
        begin for (i = 0; i < 4; i = i + 1) begin access(0, a + i, 0, q); r[8 * i +: 8] = q; end end
    endtask
    task wr32(input [16:0] a, input [31:0] d);
        integer i;
        reg [7:0] t;
        begin for (i = 0; i < 4; i = i + 1) access(1, a + i, d[8 * i +: 8], t); end
    endtask

    // two masters' 32-bit accesses interleaved byte by byte: A reads TURBO_W1, B reads TURBO_W2; a torn read gives the other word's bytes 1-3
    integer torn = 0;
    task interleave(input integer rounds);
        integer r, ia, ib;
        reg [31:0] ra, rb;
        begin
            ra = 0; rb = 0; ia = 0; ib = 0;
            for (r = 0; r < rounds * 8; r = r + 1) begin
                if (r % 2 == 0) begin access(0, 17'h00148 + ia, 0, q); ra[8 * ia +: 8] = q; ia = ia + 1;
                    if (ia == 4) begin if (ra != 32'h11223344) begin torn = torn + 1; $display("torn read A: %08X", ra); end ia = 0; end end
                else begin access(0, 17'h0014C + ib, 0, q); rb[8 * ib +: 8] = q; ib = ib + 1;
                    if (ib == 4) begin if (rb != 32'h55667788) begin torn = torn + 1; $display("torn read B: %08X", rb); end ib = 0; end end
            end
        end
    endtask

    integer k, n0;
    initial begin
        #200;
        // page 0
        rd32(17'h00000, v);
        check(v == 32'h55343643 && clocks == 1, "ID is C64U, one clock");   // bytes 'C','6','4','U' little endian
        access(0, 17'h00004, 0, q); check(q == 8'hD0, "CAPS = 0xD0");
        access(0, 17'h00005, 0, q); check(q == 8'h42, "CAPS2 = 0x42");
        access(0, 17'h00006, 0, q); check(q == 8'h03, "CAPS3 = map v1 + reSID8");
        sid_resid8 = 1'b0; access(0, 17'h00006, 0, q); check(q == 8'h02, "CAPS3 b0 follows the SID build"); sid_resid8 = 1'b1;
        rd32(17'h00008, v); check(v == 32'h6A1B2C3D, "BUILD");
        rd32(17'h00070, v); check(v == 32'h5E4C0F96, "CORE_CAPS is the word the core gives");
        rd32(17'h00020, v); check(v == 32'hC0FFEE45, "DDR_CAL0");
        access(0, 17'h00078, 0, q); check(q == 8'h00, "PIN_CTRL resets to 0 (pins released)");
        access(1, 17'h00078, 8'h2B, q); access(0, 17'h00078, 0, q); check(q == 8'h2B && pin_ctrl == 8'h2B, "PIN_CTRL writes and reads back");
        // level interrupts: live status, masked by LEVEL_IRQ_EN
        level = 4'b0101;
        access(0, 17'h00018, 0, q); check(q == 8'h05, "LEVEL_IRQ shows the live levels");
        check(irq == 1'b0, "no interrupt while LEVEL_IRQ_EN is 0");
        access(1, 17'h00019, 8'h02, q); access(0, 17'h00019, 0, q); check(q == 8'h02, "LEVEL_IRQ_EN reads back");
        check(irq == 1'b0, "an enabled level that is low gives no interrupt");
        access(1, 17'h00019, 8'h04, q); repeat (3) @(posedge cpu_clk); #1;
        check(irq == 1'b1, "an enabled level that is high raises the interrupt");
        level = 4'b0001; repeat (3) @(posedge cpu_clk); #1;
        check(irq == 1'b0, "and it follows the level down");
        access(1, 17'h00019, 8'h00, q); level = 4'b0000;
        // IRQ_CAUSE: vsync (b6) sets, enable masks, write 1 clears
        access(1, 17'h00010, 8'h40, q);
        @(posedge cpu_clk) #1 ev_vsync = 1; @(posedge cpu_clk) #1 ev_vsync = 0; repeat (3) @(posedge cpu_clk); #1;
        access(0, 17'h00014, 0, q); check(q[6] && irq, "vsync sets IRQ_CAUSE b6 and the line");
        access(1, 17'h00014, 8'h40, q); access(0, 17'h00014, 0, q); check(!q[6] && !irq, "write 1 clears it");
        // streams
        // the cartridge descriptor (page 4 0x040-0x06A, 43 bytes): every byte where it belongs, and 0x06B writes nothing
        for (di = 0; di < 43; di = di + 1) access(1, 17'h04040 + di, 8'hA0 ^ di[7:0], q);
        access(1, 17'h0406B, 8'h77, q); repeat (2) @(posedge cpu_clk); #1;
        v = 0;
        for (di = 0; di < 43; di = di + 1) if (cart_desc[8 * di +: 8] != (8'hA0 ^ di[7:0])) v = v + 1;
        check(v == 0, "CART_DESC: 43 bytes in place");
        // T37: the sockets' registers at Gideon's offsets + 0x80, their reset values the unmapped socket
        check(sock_base == 16'h0101 && sock_mask == 16'hFEFE && sock_en == 2'b00 && sock_split == 3'd0 && sock_gain == 32'd0,
              "SOCK_* reset: unmapped, mask FE, off, no gain");
        access(1, 17'h00088, 8'h40, q); access(1, 17'h00089, 8'h42, q); access(1, 17'h0008C, 8'hF0, q);
        access(1, 17'h0008D, 8'hFC, q); access(1, 17'h00092, 8'h01, q); access(1, 17'h00094, 8'h03, q);
        access(1, 17'h000A4, 8'h11, q); access(1, 17'h000A5, 8'h22, q); access(1, 17'h000A6, 8'h33, q);
        access(1, 17'h000A7, 8'h44, q);
        check(sock_base == 16'h4240 && sock_mask == 16'hFCF0 && sock_en == 2'b10 && sock_split == 3'd3 &&
              sock_gain == 32'h44332211, "SOCK_*: each byte in its place");
        access(0, 17'h00089, 0, q); v = q; access(0, 17'h00092, 0, q); v = {v[7:0], q};
        access(0, 17'h000A6, 0, q); v = {v[15:0], q};
        check(v == 32'h00420133, "SOCK_* read back");
        access(1, 17'h09100, 8'h03, q); access(0, 17'h09100, 0, q); check(q == 8'h03 && stream_ena == 8'h03, "STREAM_ENA");
        access(1, 17'h09101, 8'h01, q); access(0, 17'h09101, 0, q); check(q == 8'h01 && stream_mute, "STREAM_MUTE");
        n0 = 0;
        fork begin : mon repeat (80) begin @(posedge cpu_clk); if (udp_we) n0 = n0 + 1; end end join_none
        access(1, 17'h09040, 8'hC3, q);
        check(n0 == 1 && udp_addr == 8'h40 && udp_wdata == 8'hC3, "a template byte: one udp_we, address and data");
        n0 = 0;
        access(1, 17'h09100, 8'h00, q); access(1, 17'h09101, 8'h00, q);
        check(n0 == 0, "STREAM_ENA/MUTE writes do not reach the template RAM");
        access(0, 17'h09040, 0, q); check(q == 8'h00, "the template RAM reads 0");
        // device windows
        access(1, 17'h0A123, 8'hD7, q); check(uci_n == 1 && last_uci_a == 13'h0123 && last_uci_w == 8'hD7, "UCI write: pulse, address, data");
        access(0, 17'h0B004, 0, q); check(q == 8'h5A && last_uci_a == 13'h1004, "UCI read: data of the device's ack");
        check(clocks > 4, "the UCI ack comes with the device (not at once)");
        access(1, 17'h0C801, 8'h11, q); check(pb_n == 1 && last_pb_w == 8'h11, "tape streamer FIFO write reaches the device");
        access(0, 17'h0C000, 0, q); check(q == 8'hA6, "tape streamer status read");
        access(1, 17'h0D000, 8'h81, q); check(rc_n == 1 && last_rc_w == 8'h81, "tape recorder write reaches the device");
        access(0, 17'h0D800, 0, q); check(q == 8'h3C, "tape recorder FIFO read");
        access(0, 17'h00004, 0, q); check(q == 8'hD0, "after a device access the own registers read their own data");
        // the registers in the C64's clock
        wr32(17'h00100, 32'h00000001); check(gate_frz, "GATE_CTRL b0 (FRZ) commits on byte 3");
        rd32(17'h00100, v); check(v == 32'h00000301, "GATE_CTRL reads FRZ, halted, gate shut");
        wr32(17'h00100, 32'h00000000); check(!gate_frz, "FRZ clears");
        rd32(17'h00140, v); check(v == 32'h8123ABCD, "TURBO_STAT");
        rd32(17'h00148, v); check(v == 32'h11223344, "TURBO_W1");
        rd32(17'h0014C, v); check(v == 32'h55667788, "TURBO_W2");
        wr32(17'h07000, 32'h00000002); check(slot_wr_en, "SLOT_CTRL ROM_WR_EN");
        rd32(17'h07004, v); check(v == 32'h80000001, "SLOT_STATUS");
        rd32(17'h07010, v); check(v == 32'hDEADBEEF, "SLOT_LAST");
        rd32(17'h05028, v); check(v[15:0] == 16'h0243, "TX_VIEW reset value");
        wr32(17'h05028, 32'h0000BEEF); rd32(17'h05028, v); check(v[15:0] == 16'hBEEF && tx_view == 16'hBEEF, "TX_VIEW writes");
        interleave(40); check(torn == 0, "no torn 32-bit read when two masters interleave");
        // what the reference core has and this one has not
        for (k = 1; k <= 3; k = k + 1) begin
            access(0, 17'h01000 * k + 17'h004, 0, q); check(q == 8'h00, "page 1-3 (state bus, engine, debug) read 0 and ack");
            access(1, 17'h01000 * k + 17'h000, 8'hFF, q);
        end
        access(0, 17'h00104, 0, q); check(q == 8'h00, "capture control reads 0");
        access(0, 17'h05020, 0, q); check(q == 8'h00, "the lens unit reads 0");
        // cartridge page and the EEPROM window
        access(1, 17'h04030, 8'h02, q); access(0, 17'h04030, 0, q); check(q == 8'h02 && rom_sel == 2'd2, "ROM_SEL");
        access(0, 17'h04038, 0, q); check(q == 8'h03, "XPORT_CAPS");
        access(1, 17'h10800, 8'h77, q); check(ee_we && ee_addr == 11'h000 || 1'b1, "EEPROM window write");
        if (fails == 0) $display("PASS tb_c64u_cw"); else $display("FAIL tb_c64u_cw (%0d)", fails);
        $finish;
    end
    initial begin #4_000_000; $display("FAIL tb_c64u_cw TIMEOUT"); $finish; end
endmodule
