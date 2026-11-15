// T134 S10: cassette 1 and the CB2 sound. A hand-written test ROM (no Commodore byte) starts the VIA shift register in free-run mode (CB2 square wave) and
// counts the PIA1 CA1 edges of the tape line. The bench plays a synthetic pulse train from a DDR2 model through the pulse engine and checks
//   - the CB2 line period against the T2 latch, and the audio sample stream against the CB2 line (within 1 %)
//   - with the motor off (PIA1 CB2 high) the tape stands still, nothing reaches CA1; with the motor on the intervals of the read line equal the stream's words
//   - the PIA1 CA1 flag sees one positive edge per pulse (the ROM's counter), the sense / motor / read / write lines of TAPE_LINES and the pin outputs
//   - the external datasette: TAPE_PIN b4 takes the READ and SENSE pins instead of the file
`timescale 1ns / 1ps
module tb_pet_tape;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #2.5 mclk = ~mclk;
    always #5 ui_clk = ~ui_clk;
    reg sys_rst = 1, ui_rst = 1;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq, px_we, px_re, halted, running;
    wire [26:0] e_addr; wire [2:0] e_cmd; wire e_en, e_wdf_wren; wire [127:0] e_wdf_data; wire [15:0] e_wdf_mask;
    wire [3:0] pix_color; wire [9:0] pix_x; wire [8:0] pix_y; wire signed [23:0] audio_l, audio_r;
    wire [7:0] ram_rdata, dma_rdata; wire dma_done_tgl, win_port_use, win_port_rom, win_port_we; wire [15:0] win_port_addr; wire [7:0] win_port_wdata;
    wire [31:0] j_rdata; wire j_rvalid;
    reg  [16:0] cw_addr = 0; reg [7:0] cw_wdata = 0; reg cw_we = 0, cw_re = 0;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_rd_valid; wire [127:0] c_rd_data;
    reg  rd_pin = 1'b1, sn_pin = 1'b1;
    wire tp_motor_on, tp_write, tp_sense_pull; wire [7:0] tp_pin;
    ddr2c_model ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));
    pet_core dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst), .reset_req(1'b0), .halt_req(1'b0),
        .halted(halted), .running(running), .keys(64'd0), .joy1(5'd0), .joy2(5'd0),
        .dma_req_tgl(1'b0), .dma_addr(16'd0), .dma_we(1'b0), .dma_wdata(8'd0), .dma_rom(1'b0), .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(1'b0),
        .win_port_use(win_port_use), .win_port_addr(win_port_addr), .win_port_rom(win_port_rom), .win_port_we(win_port_we), .win_port_wdata(win_port_wdata),
        .port_req(1'b0), .port_addr(16'd0), .port_rom(1'b0), .port_we(1'b0), .port_wdata(8'd0), .ram_rdata(ram_rdata),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(px_we), .px_re(px_re), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'd0), .ddr_cal(64'd0),
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(j_rdata), .j_rvalid(j_rvalid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(1'b1), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .tp_read_pin(rd_pin), .tp_sense_pin(sn_pin), .tp_motor_on(tp_motor_on), .tp_write(tp_write), .tp_sense_pull(tp_sense_pull), .tp_pin(tp_pin));

    integer i, errs = 0, k;
    reg [7:0] rv;
    task cw_wr(input [11:0] a, input [7:0] d); begin
        @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1; @(posedge cpu_clk); cw_we <= 0;
    end endtask
    task cw_rd(input [11:0] a); begin
        @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_re <= 1; @(posedge cpu_clk); cw_re <= 0;
        while (!cw_ack) @(posedge cpu_clk);
        rv = cw_rdata;
    end endtask
    task rom(input [15:0] a, input [7:0] d); begin dut.mem.rom.mem[a - 16'h8000] = d; end endtask
    reg [15:0] pc, lpc;
    task emit(input [7:0] d); begin rom(pc, d); pc = pc + 1; end endtask
    task emit3(input [7:0] o, input [15:0] a); begin emit(o); emit(a[7:0]); emit(a[15:8]); end endtask
    localparam [7:0] T2N = 8'd62;                                   // a bit lasts two T2 periods of N + 2 = 64 us: the square wave of 1010 is 256 us
    // the stream (words of 1 us ticks): the level starts low; each word holds the level, then toggles; 0 ends the tape
    reg [15:0] w [0:15];
    initial begin
        w[0] = 300; w[1] = 200; w[2] = 100; w[3] = 100; w[4] = 150; w[5] = 150; w[6] = 200; w[7] = 200;
        w[8] = 400; w[9] = 400; w[10] = 100; w[11] = 100; w[12] = 0; w[13] = 0; w[14] = 0; w[15] = 0;
        ddr.mem[0] = {w[7], w[6], w[5], w[4], w[3], w[2], w[1], w[0]};
        ddr.mem[1] = {w[15], w[14], w[13], w[12], w[11], w[10], w[9], w[8]};
        for (i = 0; i < 32768; i = i + 1) dut.mem.rom.mem[i] = 8'hEA;
        for (i = 0; i < 2048; i = i + 1) dut.mem.chr.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.vram.mem[i] = 8'h00;
        pc = 16'hF000;
        emit(8'h78); emit(8'hD8); emit(8'hA2); emit(8'hFF); emit(8'h9A);                 // SEI CLD LDX #$FF TXS
        emit(8'hA9); emit(8'h00); emit3(8'h8D, 16'h0200); emit3(8'h8D, 16'h0201);          // the counter and the marker
        emit(8'hA9); emit(T2N); emit3(8'h8D, 16'hE848);                                    // T2 latch low
        emit(8'hA9); emit(8'h10); emit3(8'h8D, 16'hE84B);                                  // ACR: shift register free-run on T2 (mode 100)
        emit(8'hA9); emit(8'h00); emit3(8'h8D, 16'hE849);                                  // T2 high: loads the counter
        emit(8'hA9); emit(8'hAA); emit3(8'h8D, 16'hE84A);                                  // SR: 1010 1010, recirculating
        emit(8'hA9); emit(8'h06); emit3(8'h8D, 16'hE811);                                  // PIA1 CRA: CA1 positive edge, PA selected
        emit(8'hA9); emit(8'h3C); emit3(8'h8D, 16'hE813);                                  // PIA1 CRB: CB2 high = motor off
        emit(8'hA2); emit(8'h0A);                                                          // delay about 13 ms
        emit(8'hA0); emit(8'h00); emit(8'h88); emit(8'hD0); emit(8'hFD); emit(8'hCA); emit(8'hD0); emit(8'hF8);
        emit(8'hA9); emit(8'h01); emit3(8'h8D, 16'h0201);                                  // marker: phase 1 over
        emit(8'hA9); emit(8'h34); emit3(8'h8D, 16'hE813);                                  // PIA1 CRB: CB2 low = motor on
        lpc = pc; emit3(8'hAD, 16'hE811); emit(8'h10); emit(8'hFB);                                  // L: LDA CRA; BPL L
        emit3(8'hAD, 16'hE810); emit3(8'hEE, 16'h0200); emit3(8'h4C, lpc);            // LDA PA (clears the flag); INC $0200; JMP L
        rom(16'hFFFC, 8'h00); rom(16'hFFFD, 8'hF0);
        #100 sys_rst = 0; ui_rst = 0;
    end
    // the CB2 line and the audio stream: rising edges / rising zero crossings between 3 ms and 40 ms
    realtime cb_first = 0, cb_last = 0, au_first = 0, au_last = 0;
    integer cb_n = 0, au_n = 0;
    reg au_neg = 1'b1;
    always @(posedge dut.cb2_audio) if ($realtime > 3000000.0 && $realtime < 40000000.0) begin
        if (cb_n == 0) cb_first = $realtime; cb_last = $realtime; cb_n = cb_n + 1;
    end
    reg signed [23:0] a_max = 0, a_min = 0;
    always @(audio_l) begin
        if (audio_l > a_max) a_max = audio_l; if (audio_l < a_min) a_min = audio_l;
        if (audio_l > 24'sd100000 && au_neg) begin
            au_neg = 1'b0;
            if ($realtime > 3000000.0 && $realtime < 40000000.0) begin if (au_n == 0) au_first = $realtime; au_last = $realtime; au_n = au_n + 1; end
        end
        if (audio_l < -24'sd100000) au_neg = 1'b1;
    end
    // the read line of the PET (PIA1 CA1) after the motor goes on
    realtime t_edge [0:31];
    integer ne = 0;
    reg rec = 1'b0;
    always @(dut.cass_rd) if (rec && ne < 32) begin t_edge[ne] = $realtime; ne = ne + 1; end
    realtime d, p_exact, p_au;
    integer exp_w [1:11];
    initial begin
        exp_w[1] = 200; exp_w[2] = 100; exp_w[3] = 100; exp_w[4] = 150; exp_w[5] = 150; exp_w[6] = 200; exp_w[7] = 200; exp_w[8] = 400; exp_w[9] = 400; exp_w[10] = 100; exp_w[11] = 100;
        #3000000;                                        // 3 ms: the ROM is in its delay, the motor is off
        cw_wr(12'h140, 8'd1);                            // play, with the motor off
        #1000000;
        cw_rd(12'h150); if (rv[0] !== 1'b0) begin $display("FAIL tape playing with the motor off, status %h", rv); errs = errs + 1; end
        cw_rd(12'h153); if (rv[2:0] !== 3'b010) begin $display("FAIL lines with motor off %b (want 010: read low, sense low, motor off)", rv[2:0]); errs = errs + 1; end
        if (tp_motor_on !== 1'b0 || tp_sense_pull !== 1'b1) begin $display("FAIL pin outputs motor %b sense_pull %b", tp_motor_on, tp_sense_pull); errs = errs + 1; end
        if (dut.mem.ram.mem[16'h0200] !== 8'h00) begin $display("FAIL CA1 counted %0d with the motor off", dut.mem.ram.mem[16'h0200]); errs = errs + 1; end
        wait (dut.mem.ram.mem[16'h0201] === 8'h01);
        if (ne != 0) begin $display("FAIL the read line moved with the motor off (%0d edges)", ne); errs = errs + 1; end
        rec = 1'b1;
        @(posedge tp_motor_on); #1000;
        cw_rd(12'h153); if (rv[0] !== 1'b1) begin $display("FAIL motor line %b", rv); errs = errs + 1; end
        k = 0;
        while (k < 4000) begin cw_rd(12'h150); if (rv[1]) k = 99999; else begin k = k + 1; #5000; end end
        if (k != 99999) begin $display("FAIL the tape did not reach its end, status %h", rv); errs = errs + 1; end
        #500000;
        rec = 1'b0;
        if (ne != 12) begin $display("FAIL %0d edges on the read line, want 12", ne); errs = errs + 1; end
        for (k = 1; k <= 11; k = k + 1) if (k < ne) begin
            d = t_edge[k] - t_edge[k-1];
            if (d < exp_w[k] * 1000.0 - 150.0 || d > exp_w[k] * 1000.0 + 150.0) begin $display("FAIL interval %0d is %0f ns, want %0d us", k, d, exp_w[k]); errs = errs + 1; end
        end
        if (dut.mem.ram.mem[16'h0200] !== 8'h06) begin $display("FAIL CA1 counted %0d positive edges, want 6", dut.mem.ram.mem[16'h0200]); errs = errs + 1; end
        // the external datasette
        cw_wr(12'h152, 8'h10);
        #200000;
        k = dut.mem.ram.mem[16'h0200];                   // the switch may have made an edge itself
        #100000 rd_pin = 1'b0; #200000 rd_pin = 1'b1; #200000 rd_pin = 1'b0; #200000 rd_pin = 1'b1; #200000;
        if (dut.mem.ram.mem[16'h0200] !== k + 2) begin $display("FAIL external read pin: counted %0d, want %0d", dut.mem.ram.mem[16'h0200], k + 2); errs = errs + 1; end
        cw_wr(12'h140, 8'd0);
        #20000 cw_rd(12'h153); if (rv[1] !== 1'b0) begin $display("FAIL sense low with play off and the pin open"); errs = errs + 1; end
        sn_pin = 1'b0; #20000 cw_rd(12'h153); if (rv[1] !== 1'b1) begin $display("FAIL the external sense key is not seen"); errs = errs + 1; end
        if (tp_pin !== 8'h10) begin $display("FAIL tp_pin %h", tp_pin); errs = errs + 1; end
        // the sound
        p_exact = (cb_last - cb_first) / (cb_n - 1);
        p_au = (au_last - au_first) / (au_n - 1);
        $display("audio min %0d max %0d", a_min, a_max); $display("CB2 period %0f ns over %0d periods (want %0d), audio %0f ns over %0d", p_exact, cb_n, 4 * (T2N + 2) * 1000, p_au, au_n);
        if (cb_n < 40 || p_exact < 4 * (T2N + 2) * 1000 * 0.99 || p_exact > 4 * (T2N + 2) * 1000 * 1.01) begin $display("FAIL CB2 period"); errs = errs + 1; end
        if (au_n < 40 || p_au < p_exact * 0.99 || p_au > p_exact * 1.01) begin $display("FAIL audio period"); errs = errs + 1; end
        if (errs == 0) $display("PASS tb_pet_tape: CB2 sound, tape pulses, motor, sense, CA1, external pins");
        else $display("FAIL tb_pet_tape: %0d errors", errs);
        $finish;
    end
    initial begin #120000000; $display("FAIL tb_pet_tape: timeout"); $finish; end
endmodule
