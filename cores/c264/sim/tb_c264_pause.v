// T90 S6, LOCAL ONLY (real ROMs): Freeze. Two cores, same ROMs (loaded straight into the arrays), same reset. A runs free; B is held
// from 100 ms to 160 ms of machine time (halt_req, as the app's C64_STOP). Every TED clock of each machine adds its bus (address,
// data, R/W) to a running hash; the hash at every 1,000,000th TED clock must be equal in A and B, and B must not count TED clocks
// (or change anything on its bus) while held. Plusargs: +KERNAL=hex +BASIC=hex +MS=machine ms of A
`timescale 1ns / 1ps
module tb_c264_pause;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #40.69 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;
    reg sys_rst = 1, reset_req = 1, hold_b = 0;
    wire e_rdy = 1'b1;
    wire [26:0] ea0, ea1; wire [2:0] ec0, ec1; wire een0, een1; wire [127:0] ed0, ed1; wire [15:0] em0, em1; wire ew0, ew1;
    wire halted_a, halted_b;
    `define CORE(name, hold, ea, ec, een, ed, em, ew, hl) c264_core name ( \
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(1'b0), \
        .reset_req(reset_req), .halt_req(hold), .halted(hl), .running(), .keys(64'd0), .joy1(5'd0), .joy2(5'd0), .restore(1'b0), \
        .tp_read_pin(1'b1), .tp_sense_pin(1'b1), .iec_atn_i(1'b1), .iec_clk_i(1'b1), .iec_data_i(1'b1), .iec_atn_o(), .iec_clk_o(), .iec_data_o(), \
        .dma_req_tgl(1'b0), .dma_addr(16'd0), .dma_we(1'b0), .dma_wdata(8'd0), .dma_rom(1'b0), .dma_done_tgl(), .dma_rdata(), .dbg_busy(1'b0), \
        .win_port_use(), .win_port_addr(), .win_port_rom(), .win_port_we(), .win_port_wdata(), \
        .port_req(1'b0), .port_addr(16'd0), .port_rom(1'b0), .port_we(1'b0), .port_wdata(8'd0), .ram_rdata(), \
        .pix_color(), .pix_x(), .pix_y(), .audio_l(), .audio_r(), \
        .cw_we(1'b0), .cw_re(1'b0), .cw_addr(17'd0), .cw_wdata(8'd0), .cw_rdata(), .cw_ack(), .cw_irq(), \
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'd0), .ddr_cal(64'd0), \
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(), \
        .e_addr(ea), .e_cmd(ec), .e_en(een), .e_rdy(e_rdy), .e_wdf_data(ed), .e_wdf_mask(em), .e_wdf_wren(ew))
    `CORE(A, 1'b0, ea0, ec0, een0, ed0, em0, ew0, halted_a);
    `CORE(B, hold_b, ea1, ec1, een1, ed1, em1, ew1, halted_b);

    reg [8*200-1:0] f_k, f_b;
    reg [7:0] rk [0:16383], rb [0:16383];
    integer i, ms;
    reg [63:0] ha = 0, hb = 0;
    integer ca = 0, cb = 0, frozen_changes = 0, fails = 0;
    reg [63:0] snap_a [0:63], snap_b [0:63];
    reg [63:0] last_b_sig;
    reg [34:0] bus_b_prev;
    always @(posedge sys_clk) begin
        if (A.ted_en) begin ha <= (ha * 64'd1099511628211) ^ {29'd0, A.c16_addr, A.c16_data, A.rnw}; ca <= ca + 1; if (ca % 1000000 == 999999) snap_a[ca / 1000000] <= (ha * 64'd1099511628211) ^ {29'd0, A.c16_addr, A.c16_data, A.rnw}; end
        if (B.ted_en) begin hb <= (hb * 64'd1099511628211) ^ {29'd0, B.c16_addr, B.c16_data, B.rnw}; cb <= cb + 1; if (cb % 1000000 == 999999) snap_b[cb / 1000000] <= (hb * 64'd1099511628211) ^ {29'd0, B.c16_addr, B.c16_data, B.rnw}; end
        // while held nothing on B's machine bus or counters may move
        if (halted_b && hold_b) begin
            if ({B.c16_addr, B.c16_data, B.rnw} !== bus_b_prev && bus_b_prev !== 35'bx && B.sreset == 0) frozen_changes <= frozen_changes + 1;
        end
        bus_b_prev <= {B.c16_addr, B.c16_data, B.rnw};
    end
    integer cb_at_hold;
    initial begin
        if (!$value$plusargs("KERNAL=%s", f_k) || !$value$plusargs("BASIC=%s", f_b)) begin $display("need +KERNAL= +BASIC="); $finish; end
        if (!$value$plusargs("MS=%d", ms)) ms = 600;
        $readmemh(f_k, rk); $readmemh(f_b, rb);
        for (i = 0; i < 16384; i = i + 1) begin
            A.mem.kern.mem[i] = rk[i]; A.mem.basic.mem[i] = rb[i];
            B.mem.kern.mem[i] = rk[i]; B.mem.basic.mem[i] = rb[i];
        end
        #200; sys_rst = 0; #1000; reset_req = 0;
        #(100 * 1000000.0 + 35 * 1000000.0);          // 100 ms after the reset stretch ends
        hold_b = 1;
        #(1000000.0);
        cb_at_hold = cb;
        #(59 * 1000000.0);
        if (cb == cb_at_hold) $display("ok   B counted no TED clock in 59 ms held"); else begin $display("FAIL B counted %0d TED clocks while held", cb - cb_at_hold); fails = fails + 1; end
        if (halted_b) $display("ok   B reports halted"); else begin $display("FAIL B not halted"); fails = fails + 1; end
        if (frozen_changes == 0) $display("ok   B's bus did not change while held"); else begin $display("FAIL B's bus changed %0d times while held", frozen_changes); fails = fails + 1; end
        hold_b = 0;
        #(ms * 1000000.0);
        $display("A %0d TED clocks, B %0d", ca, cb);
        for (i = 0; i < ((ca < cb ? ca : cb) / 1000000); i = i + 1)
            if (snap_a[i] !== snap_b[i]) begin $display("FAIL bus hash differs at %0d million TED clocks", i + 1); fails = fails + 1; i = 1000; end
        if (i < 1000) $display("ok   paused and free machine agree, bus cycle for bus cycle, at every million TED clocks (%0d checks)", (ca < cb ? ca : cb) / 1000000);
        if (fails == 0) $display("PASS tb_c264_pause"); else $display("FAILED tb_c264_pause");
        $finish;
    end
endmodule
