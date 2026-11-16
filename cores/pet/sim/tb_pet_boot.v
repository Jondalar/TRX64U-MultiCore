// T134 S2/S3: the PET machine boots a hand-written test ROM (no Commodore byte): a RAM check, characters in the video RAM, the PIA's blanking
// control, a marker in RAM. Then the picture: the second frame's first text row is read off the pixel stream (pet_vid -> pet_pixw input).
`timescale 1ns / 1ps
module tb_pet_boot;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #2.5 mclk = ~mclk;
    always #5 ui_clk = ~ui_clk;
    reg sys_rst = 1, ui_rst = 1;
    reg [2:0] model = 3'd0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq, px_we, px_re, halted, running;
    wire [26:0] e_addr; wire [2:0] e_cmd; wire e_en, e_wdf_wren; wire [127:0] e_wdf_data; wire [15:0] e_wdf_mask;
    wire [3:0] pix_color; wire [9:0] pix_x; wire [8:0] pix_y; wire signed [23:0] audio_l, audio_r;
    wire [7:0] ram_rdata, dma_rdata; wire dma_done_tgl, win_port_use, win_port_rom, win_port_we; wire [15:0] win_port_addr; wire [7:0] win_port_wdata;
    wire [31:0] j_rdata; wire j_rvalid;
    reg  [16:0] cw_addr = 0; reg [7:0] cw_wdata = 0; reg cw_we = 0, cw_re = 0;
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
        .c_addr(), .c_cmd(), .c_en(), .c_rdy(1'b0), .c_rd_data(128'd0), .c_rd_valid(1'b0),
        .tp_read_pin(1'b1), .tp_sense_pin(1'b1), .tp_motor_on(), .tp_write(), .tp_sense_pull(), .tp_pin());

    integer i, errs = 0, k;
    task rom(input [15:0] a, input [7:0] d); begin dut.mem.rom.mem[a - 16'h8000] = d; end endtask
    reg [15:0] pc;
    task emit(input [7:0] d); begin rom(pc, d); pc = pc + 1; end endtask
    initial begin
        for (i = 0; i < 32768; i = i + 1) dut.mem.rom.mem[i] = 8'hEA;
        for (i = 0; i < 2048; i = i + 1) dut.mem.chr.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.vram.mem[i] = 8'h00;
        for (i = 0; i < 8; i = i + 1) begin dut.mem.chr.mem[8 + i] = 8'h81; dut.mem.chr.mem[16 + i] = 8'hFF; dut.mem.chr.mem[1024 + 8 + i] = 8'h81; dut.mem.chr.mem[1024 + 16 + i] = 8'hFF; end      // code 1: bits 7 and 0; code 2: all
        pc = 16'hF000;
        emit(8'h78); emit(8'hD8); emit(8'hA2); emit(8'hFF); emit(8'h9A);
        emit(8'hA9); emit(8'hA5); emit(8'h8D); emit(8'hFF); emit(8'h7F); emit(8'hAD); emit(8'hFF); emit(8'h7F); emit(8'hC9); emit(8'hA5); emit(8'hD0); emit(8'h6F);   // BNE fail ($F080)
        emit(8'hA9); emit(8'h5A); emit(8'h8D); emit(8'h10); emit(8'h00); emit(8'hAD); emit(8'h10); emit(8'h00); emit(8'hC9); emit(8'h5A); emit(8'hD0); emit(8'h63);
        emit(8'hA9); emit(8'h01); emit(8'h8D); emit(8'h00); emit(8'h80); emit(8'hA9); emit(8'h81); emit(8'h8D); emit(8'h01); emit(8'h80);
        emit(8'hA9); emit(8'h02); emit(8'h8D); emit(8'h28); emit(8'h80);
        emit(8'hA9); emit(8'h3C); emit(8'h8D); emit(8'h11); emit(8'hE8);
        emit(8'hA9); emit(8'hA5); emit(8'h8D); emit(8'h00); emit(8'h02);
        emit(8'h4C); emit(8'h00); emit(8'hF1);                  // JMP $F100 (done loop: JMP to itself there)
        pc = 16'hF100; emit(8'h4C); emit(8'h00); emit(8'hF1);
        rom(16'hFFFC, 8'h00); rom(16'hFFFD, 8'hF0);
        // fail target for the two BNEs: offsets above point into NOP space after the code; put a marker writer at the end of the NOP sled
        pc = 16'hF080; emit(8'hA9); emit(8'hEE); emit(8'h8D); emit(8'h00); emit(8'h02); emit(8'h4C); emit(8'h85); emit(8'hF0);
        #100 sys_rst = 0; ui_rst = 0;
    end
    // the pixel stream as the frame writer sees it
    reg [15:0] line0 [0:639], line16 [0:639];
    integer xd = 0, yd = 0, frames_seen = 0;
    reg hb_d = 1;
    always @(posedge sys_clk) begin
        hb_d <= dut.vid.hblank;
        if (dut.vid.frame_end) begin frames_seen = frames_seen + 1; end
        if (frames_seen >= 2 && frames_seen < 3) begin
            if (hb_d && !dut.vid.hblank) xd = 0;
            if (!dut.vid.hblank && !dut.vid.vblank) begin
                if (yd == 0) line0[xd] = dut.vid.rgb565;
                if (yd == 16) line16[xd] = dut.vid.rgb565;
                xd = xd + 1;
            end
            if (!hb_d && dut.vid.hblank) yd = yd + 1;
            if (dut.vid.vblank) yd = 0;
        end
    end
    initial begin
        #25000000;                                   // 25 ms: the code has run, one frame is over
        if (dut.mem.ram.mem[16'h0200] !== 8'hA5) begin $display("FAIL marker %h", dut.mem.ram.mem[16'h0200]); errs = errs + 1; end
        if (dut.mem.vram.mem[0] !== 8'h01 || dut.mem.vram.mem[1] !== 8'h81 || dut.mem.vram.mem[40] !== 8'h02) begin $display("FAIL video RAM"); errs = errs + 1; end
        wait (frames_seen >= 3);
        #1000;
        $write("line0: "); for (k = 0; k < 48; k = k + 1) $write("%0d", line0[k] != 0); $write("\nline16: "); for (k = 0; k < 48; k = k + 1) $write("%0d", line16[k] != 0); $write("\n");
        // line 0: cell 0 (code 1): dots 0,1 and 14,15 on; cell 1 (code $81 reverse): dots 16,17 off, 18..29 on, 30,31 off; cell 2: blank code 0 = off
        if (line0[0] !== 16'h27E6 || line0[1] !== 16'h27E6 || line0[2] !== 16'h0 || line0[13] !== 16'h0 || line0[14] !== 16'h27E6 || line0[15] !== 16'h27E6) begin
            $display("FAIL line0 cell0 %h %h %h %h %h", line0[0], line0[1], line0[2], line0[14], line0[15]); errs = errs + 1; end
        if (line0[16] !== 16'h0 || line0[17] !== 16'h0 || line0[18] !== 16'h27E6 || line0[29] !== 16'h27E6 || line0[30] !== 16'h0 || line0[31] !== 16'h0) begin
            $display("FAIL line0 cell1 %h %h %h %h %h", line0[16], line0[17], line0[18], line0[29], line0[30]); errs = errs + 1; end
        if (line0[32] !== 16'h0) begin $display("FAIL line0 cell2"); errs = errs + 1; end
        if (line16[0] !== 16'h27E6 || line16[15] !== 16'h27E6) begin $display("FAIL row1 cell0 %h", line16[0]); errs = errs + 1; end
        if (errs == 0) $display("PASS tb_pet_boot: marker, video RAM, text row pixels, %0d frames", frames_seen);
        else $display("FAIL tb_pet_boot: %0d errors", errs);
        $finish;
    end
endmodule
