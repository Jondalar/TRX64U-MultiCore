// T134 S2/S3: the PET machine boots a hand-written test ROM (no Commodore byte): a RAM check, characters in the video RAM, the PIA's blanking
// control, a marker in RAM. Then the picture: the second frame's first text row is read off the pixel stream (pet_vid -> pet_pixw input).
`timescale 1ns / 1ps
module tb_pet_models;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #2.5 mclk = ~mclk;
    always #5 ui_clk = ~ui_clk;
    reg sys_rst = 1, ui_rst = 1;
    integer mode = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq, px_we, px_re, halted, running;
    wire [26:0] e_addr; wire [2:0] e_cmd; wire e_en, e_wdf_wren; wire [127:0] e_wdf_data; wire [15:0] e_wdf_mask;
    wire [3:0] pix_color; wire [9:0] pix_x; wire [8:0] pix_y; wire signed [23:0] audio_l, audio_r;
    wire [7:0] ram_rdata, dma_rdata; wire dma_done_tgl, win_port_use, win_port_rom, win_port_we; wire [15:0] win_port_addr; wire [7:0] win_port_wdata;
    wire [31:0] j_rdata; wire j_rvalid;
    reg [63:0] keys = 64'd0;
    reg  [16:0] cw_addr = 0; reg [7:0] cw_wdata = 0; reg cw_we = 0, cw_re = 0;
    pet_core dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst), .reset_req(1'b0), .halt_req(1'b0),
        .halted(halted), .running(running), .keys(keys), .joy1(5'd0), .joy2(5'd0),
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
    reg [15:0] pc;
    task rom(input [15:0] a, input [7:0] d); begin dut.mem.rom.mem[a - 16'h8000] = d; end endtask
    task emit(input [7:0] d); begin rom(pc, d); pc = pc + 1; end endtask
    task ram(input [7:0] d); begin dut.mem.ram.mem[pc] = d; pc = pc + 1; end endtask
    task cw(input [11:0] a, input [7:0] d); begin
        @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1; @(posedge cpu_clk); cw_we <= 0; end endtask
    // LDA #d / STA abs
    task ldai(input [7:0] d); begin ram(8'hA9); ram(d); end endtask
    task sta(input [15:0] a); begin ram(8'h8D); ram(a[7:0]); ram(a[15:8]); end endtask
    task lda(input [15:0] a); begin ram(8'hAD); ram(a[7:0]); ram(a[15:8]); end endtask
    initial begin
        if (!$value$plusargs("mode=%d", mode)) mode = 0;
        for (i = 0; i < 32768; i = i + 1) dut.mem.rom.mem[i] = 8'hEA;
        for (i = 0; i < 4096; i = i + 1) dut.mem.chr.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.vram.mem[i] = 8'h00;
        for (i = 0; i < 65536; i = i + 1) dut.xram.mem[i] = 8'h00;
        for (i = 0; i < 8; i = i + 1) begin dut.mem.chr.mem[8 + i] = 8'h81; dut.mem.chr.mem[16 + i] = 8'hFF; dut.mem.chr.mem[1024 + 8 + i] = 8'h81; dut.mem.chr.mem[1024 + 16 + i] = 8'hFF; dut.mem.chr.mem[2048 + 8 + i] = 8'h3C; dut.mem.chr.mem[3072 + 8 + i] = 8'h3C; end   // the upper pair (CRTC R12 bit 5): code 1 = dots 2..5
        rom(16'hFFFC, 8'h00); rom(16'hFFFD, 8'hF0);
        pc = 16'hF000;
        emit(8'h78); emit(8'hD8); emit(8'hA2); emit(8'hFF); emit(8'h9A);
        if (mode == 0) begin
            // 8032: three characters into the 80 column video RAM, the CRTC's R1 = 40 as the real editor ROMs program it (two columns per CRTC character), a marker
            emit(8'hA9); emit(8'h01); emit(8'h8D); emit(8'h00); emit(8'h80);
            emit(8'hA9); emit(8'h81); emit(8'h8D); emit(8'h01); emit(8'h80);
            emit(8'hA9); emit(8'h02); emit(8'h8D); emit(8'h50); emit(8'h80);
            emit(8'hA9); emit(8'h01); emit(8'h8D); emit(8'h80); emit(8'hE8);
            emit(8'hA9); emit(8'h28); emit(8'h8D); emit(8'h81); emit(8'hE8);
            emit(8'hA9); emit(8'hA5); emit(8'h8D); emit(8'h00); emit(8'h02);
            // T216 S13: what the real 8032 editor leaves in R10 / R11 / R14 / R15 (the table: R10 = 0, R11 = 0, cursor address 0): no cursor glyph at (0,0)
            emit(8'hA9); emit(8'h0A); emit(8'h8D); emit(8'h80); emit(8'hE8); emit(8'hA9); emit(8'h00); emit(8'h8D); emit(8'h81); emit(8'hE8);
            emit(8'hA9); emit(8'h0B); emit(8'h8D); emit(8'h80); emit(8'hE8); emit(8'hA9); emit(8'h00); emit(8'h8D); emit(8'h81); emit(8'hE8);
            emit(8'h4C); emit(8'h37); emit(8'hF0);
        end else begin
            emit(8'h4C); emit(8'h00); emit(8'h03);              // JMP $0300: the test runs from low RAM (the 8096's extra RAM replaces the ROM)
            pc = 16'h0300;
            if (mode == 1) begin
                ldai(8'h11); sta(16'h8010);                      // video RAM (extra RAM off)
                ldai(8'h80); sta(16'hFFF0);                      // extra RAM on, blocks 0 / 0
                ldai(8'h22); sta(16'h8010);                      // extra block A0
                ldai(8'h84); sta(16'hFFF0);
                ldai(8'h33); sta(16'h8010);                      // extra block A1
                ldai(8'h80); sta(16'hFFF0); lda(16'h8010); sta(16'h0200);   // 22
                ldai(8'h84); sta(16'hFFF0); lda(16'h8010); sta(16'h0201);   // 33
                ldai(8'h88); sta(16'hFFF0);
                ldai(8'h44); sta(16'hC010);                      // extra block B1 (ROM space shadowed)
                ldai(8'h80); sta(16'hFFF0); lda(16'hC010); sta(16'h0202);   // B0: 00
                ldai(8'h88); sta(16'hFFF0); lda(16'hC010); sta(16'h0203);   // B1: 44
                ldai(8'hA0); sta(16'hFFF0); lda(16'h8010); sta(16'h0204);   // screen peek-through: the video RAM, 11
                ldai(8'h00); sta(16'hFFF0); lda(16'h8010); sta(16'h0205);   // off: the video RAM, 11
                ldai(8'h81); sta(16'hFFF0); ldai(8'h99); sta(16'h8010); lda(16'h8010); sta(16'h0206);   // write protect A0: still 22
                ldai(8'h00); sta(16'hFFF0); lda(16'hFFF0); sta(16'h0207);   // off: the ROM's byte at $FFF0 (NOP = EA)
                ldai(8'hA5); sta(16'h0208);
            end else begin
                ldai(8'h5A); sta(16'h1FFF); lda(16'h1FFF); sta(16'h0200);   // 8 KiB: the last byte holds
                ldai(8'h5A); sta(16'h2000); lda(16'h2000); sta(16'h0201);   // above: open bus (FF)
                ldai(8'hA5); sta(16'h0208);
            end
            ram(8'h4C); ram(pc[7:0] - 8'd1); ram(pc[15:8]);     // JMP self (pc already advanced by one: target = the JMP's own address)
        end
        // the model and the RAM size are set through the core window while the machine is in reset
        #12;
        cw(12'h0A1, mode == 0 ? 8'd3 : mode == 1 ? 8'd4 : 8'd0);
        cw(12'h0A7, mode == 2 ? 8'd0 : 8'd2);
        #100 sys_rst = 0; ui_rst = 0;
    end
    // the pixel stream as the frame writer sees it
    reg [15:0] line0 [0:639], line16 [0:639], line0b [0:639];
    integer xd = 0, yd = 0, frames_seen = 0, startmod = 0, altmod = 0;
    reg [15:0] line0c [0:639];
    reg hb_d = 1;
    always @(posedge sys_clk) begin
        hb_d <= dut.vid.hblank;
        if (dut.vid.frame_end) begin
            frames_seen = frames_seen + 1;
            if (frames_seen == 3 && mode == 0) begin dut.r13 = 8'd1; startmod = 1; end   // the CRTC start address moves by one character
            if (frames_seen == 5 && mode == 0) begin dut.r13 = 8'd0; dut.r12 = 8'h20; altmod = 1; end   // R12 bit 5 (MA13): the upper pair of character sets
        end
        if ((frames_seen == 2 || (frames_seen == 4 && startmod) || (frames_seen == 6 && altmod)) && mode == 0) begin
            if (hb_d && !dut.vid.hblank) xd = 0;
            if (!dut.vid.hblank && !dut.vid.vblank) begin
                if (yd == 0) begin if (frames_seen == 2) line0[xd] = dut.vid.rgb565; else if (frames_seen == 4) line0b[xd] = dut.vid.rgb565; else line0c[xd] = dut.vid.rgb565; end
                if (yd == 16 && frames_seen == 2) line16[xd] = dut.vid.rgb565;
                xd = xd + 1;
            end
            if (!hb_d && dut.vid.hblank) yd = yd + 1;
            if (dut.vid.vblank) yd = 0;
        end
    end
    initial begin
        #30000000;
        if (mode == 0) begin
            if (dut.mem.ram.mem[16'h0200] !== 8'hA5) begin $display("FAIL marker %h", dut.mem.ram.mem[16'h0200]); errs = errs + 1; end
            if (!dut.col80) begin $display("FAIL col80"); errs = errs + 1; end
            if (dut.vid.col80 !== 1'b1) begin $display("FAIL vid col80"); errs = errs + 1; end
            wait (frames_seen >= 7);
            #1000;
            $write("line0:  "); for (k = 0; k < 40; k = k + 1) $write("%0d", line0[k] != 0); $write("\nline16: "); for (k = 0; k < 40; k = k + 1) $write("%0d", line16[k] != 0);
            $write("\nline0b: "); for (k = 0; k < 40; k = k + 1) $write("%0d", line0b[k] != 0); $write("\n");
            // 80 columns: 8 dots per character, no doubling. cell 0 code 1: dots 0 and 7; cell 1 code $81 (reverse): dot 8 off, 9..14 on, 15 off; cell 2 blank
            if (!(line0[0] == 16'h27E6 && line0[1] == 0 && line0[6] == 0 && line0[7] == 16'h27E6)) begin $display("FAIL 80 col cell0"); errs = errs + 1; end
            if (!(line0[8] == 0 && line0[9] == 16'h27E6 && line0[14] == 16'h27E6 && line0[15] == 0 && line0[16] == 0)) begin $display("FAIL 80 col cell1"); errs = errs + 1; end
            // row 1 starts at character 80 (video RAM $8050): code 2 is all dots on, 8 dots wide, then blank
            if (!(line16[0] == 16'h27E6 && line16[7] == 16'h27E6 && line16[8] == 0)) begin $display("FAIL 80 col row 1"); errs = errs + 1; end
            // start address 1: the first character is the old second one
            // R12 bit 5: cell 0 (code 1) now comes from the upper pair: dots 2..5
            if (!(line0c[0] == 0 && line0c[1] == 0 && line0c[2] == 16'h27E6 && line0c[5] == 16'h27E6 && line0c[6] == 0 && line0c[7] == 0)) begin $display("FAIL alternate character set (R12 bit 5)"); errs = errs + 1; end
            if (!(line0b[0] == 0 && line0b[1] == 16'h27E6 && line0b[6] == 16'h27E6 && line0b[7] == 0 && line0b[8] == 0)) begin $display("FAIL CRTC start address"); errs = errs + 1; end
        end else if (mode == 1) begin
            if (dut.mem.ram.mem[16'h0208] !== 8'hA5) begin $display("FAIL marker %h", dut.mem.ram.mem[16'h0208]); errs = errs + 1; end
            for (k = 0; k < 8; k = k + 1) $write("%h ", dut.mem.ram.mem[16'h0200 + k]); $write("\n");
            if (dut.mem.ram.mem[16'h0200] !== 8'h22) begin $display("FAIL block A0"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0201] !== 8'h33) begin $display("FAIL block A1"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0202] !== 8'h00) begin $display("FAIL block B0"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0203] !== 8'h44) begin $display("FAIL block B1"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0204] !== 8'h11) begin $display("FAIL screen peek-through"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0205] !== 8'h11) begin $display("FAIL extra RAM off"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0206] !== 8'h22) begin $display("FAIL write protect"); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0207] !== 8'hEA) begin $display("FAIL ROM back at FFF0 %h", dut.mem.ram.mem[16'h0207]); errs = errs + 1; end
        end else begin
            if (dut.mem.ram.mem[16'h0208] !== 8'hA5) begin $display("FAIL marker %h", dut.mem.ram.mem[16'h0208]); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0200] !== 8'h5A) begin $display("FAIL 8K last byte %h", dut.mem.ram.mem[16'h0200]); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h0201] !== 8'hFF) begin $display("FAIL 8K above %h", dut.mem.ram.mem[16'h0201]); errs = errs + 1; end
            if (dut.mem.ram.mem[16'h2000] === 8'h5A) begin $display("FAIL write above 8K reached the RAM"); errs = errs + 1; end
        end
        if (errs == 0) $display("PASS tb_pet_models mode %0d", mode);
        else $display("FAIL tb_pet_models mode %0d: %0d errors", mode, errs);
        $finish;
    end
endmodule
