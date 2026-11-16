// T216 P6 adds mode 2 (96 KiB: sixteen banks) and mode 3 (32 KiB: no bank RAM).
// T134 Q13: the SuperPET board. mode 0: the CPU switch says 6809; a synthetic program in the Waterloo ROM area (no Waterloo byte in git) banks the
// $9000 window, writes the screen RAM, reads the ROM at $A000 / $E010, the dongle and the ACIA, write protects the RAM. mode 1: PROG: the 6502 unlocks the
// control register and hands over to the 6809 through $EFF8 b0.
`timescale 1ns / 1ps
module tb_pet_spet;
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


    integer i, errs = 0;
    reg [15:0] pc, p9;
    task rom(input [15:0] a, input [7:0] d); begin dut.mem.rom.mem[a - 16'h8000] = d; end endtask
    task emit(input [7:0] d); begin rom(pc, d); pc = pc + 1; end endtask
    task w9(input [7:0] d); begin dut.mem.rom09.mem[p9 - 16'hA000] = d; p9 = p9 + 1; end endtask
    task cw(input [11:0] a, input [7:0] d); begin
        @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1; @(posedge cpu_clk); cw_we <= 0; end endtask
    // 6809: LDA #d / STA abs / LDA abs
    task ldai(input [7:0] d); begin w9(8'h86); w9(d); end endtask
    task sta(input [15:0] a); begin w9(8'hB7); w9(a[15:8]); w9(a[7:0]); end endtask
    task lda(input [15:0] a); begin w9(8'hB6); w9(a[15:8]); w9(a[7:0]); end endtask
    // 6502: LDA #d / STA abs
    task ldai65(input [7:0] d); begin emit(8'hA9); emit(d); end endtask
    task sta65(input [15:0] a); begin emit(8'h8D); emit(a[7:0]); emit(a[15:8]); end endtask
    task chk(input [15:0] a, input [7:0] v, input [8*24-1:0] what); begin
        if (dut.mem.ram.mem[a] !== v) begin $display("FAIL %0s: ram[%h] = %h, want %h", what, a, dut.mem.ram.mem[a], v); errs = errs + 1; end
    end endtask
    initial begin
        if (!$value$plusargs("mode=%d", mode)) mode = 0;
        for (i = 0; i < 32768; i = i + 1) dut.mem.rom.mem[i] = 8'hEA;
        for (i = 0; i < 24576; i = i + 1) dut.mem.rom09.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.chr.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.vram.mem[i] = 8'h00;
        for (i = 0; i < 65536; i = i + 1) dut.xram.mem[i] = 8'h00;
        for (i = 0; i < 32768; i = i + 1) dut.mem.ram.mem[i] = 8'h00;
        dut.mem.rom09.mem[0] = 8'h5A;                                   // $A000
        dut.mem.rom09.mem[15'h4010] = 8'hC3;                              // $E010

        dut.mem.rom09.mem[15'h5FFE] = 8'hF0; dut.mem.rom09.mem[15'h5FFF] = 8'h00;
        rom(16'hFFFC, 8'h00); rom(16'hFFFD, 8'hF0);
        if (mode == 0) begin
            p9 = 16'hF000;
    ldai(8'h80); sta(16'hEFFC);
    ldai(8'h11); sta(16'h9000);
    ldai(8'h83); sta(16'hEFFC);
    ldai(8'h33); sta(16'h9000);
    ldai(8'h34); sta(16'h9FFF);
    ldai(8'h80); sta(16'hEFFC);
    lda(16'h9000); sta(16'h0200);
    ldai(8'h83); sta(16'hEFFC);
    lda(16'h9000); sta(16'h0201);
    lda(16'h9FFF); sta(16'h0202);
    ldai(8'h01); sta(16'h8000);
    lda(16'h8000); sta(16'h0203);
    lda(16'hA000); sta(16'h0204);
    lda(16'hE010); sta(16'h0205);
    lda(16'hEFE0); sta(16'h0206);
    lda(16'hEFF1); sta(16'h0207);
    ldai(8'h10); sta(16'hEFE0); ldai(8'h13); sta(16'hEFE0); lda(16'hEFE0); sta(16'h020B);
    ldai(8'h14); sta(16'hEFE0); ldai(8'h15); sta(16'hEFE0); lda(16'hEFE0); sta(16'h020C);
    ldai(8'h20); sta(16'hEFE0); ldai(8'h2B); sta(16'hEFE0); lda(16'hEFE0); sta(16'h020D);
    ldai(8'h00); sta(16'hEFF8);
    ldai(8'h77); sta(16'h9000);
    lda(16'h9000); sta(16'h0208);
    ldai(8'h01); sta(16'hEFFE);
    lda(16'h9000); sta(16'h0209);
    ldai(8'hA5); sta(16'h020A);
    w9(8'h20); w9(8'hFE);
        end else if (mode >= 2) begin
            // T216 P6: the 96 KiB. mode 2: all sixteen 4 KiB banks behind $9000 keep their own bytes (first and last byte of each); mode 3: SuperPET RAM = 32 KiB, the ROM is behind $9000 and the write goes nowhere
            p9 = 16'hF000;
            if (mode == 2) begin
                for (i = 0; i < 16; i = i + 1) begin
                    ldai(8'h80 | i[7:0]); sta(16'hEFFC);
                    ldai(8'h40 + i[7:0]); sta(16'h9000);
                    ldai(8'hA0 + i[7:0]); sta(16'h9FFF);
                end
                for (i = 0; i < 16; i = i + 1) begin
                    ldai(8'h80 | i[7:0]); sta(16'hEFFC);
                    lda(16'h9000); sta(16'h0220 + i[15:0]);
                    lda(16'h9FFF); sta(16'h0240 + i[15:0]);
                end
            end else begin
                ldai(8'h80); sta(16'hEFFC);
                ldai(8'h77); sta(16'h9000);
                lda(16'h9000); sta(16'h0220);
            end
            ldai(8'hA5); sta(16'h0200);
            w9(8'h20); w9(8'hFE);
        end else begin
    pc = 16'hF000;
    emit(8'h78); emit(8'hD8); emit(8'hA2); emit(8'hFF); emit(8'h9A);
    ldai65(8'hA5); sta65(16'h0200);
    ldai65(8'h80); sta65(16'hEFFC);
    ldai65(8'h02); sta65(16'hEFF8);
    ldai65(8'hEE); sta65(16'h0203);
    emit(8'h4C); emit(pc[7:0] - 8'd1); emit(pc[15:8]);
    p9 = 16'hF000;
    ldai(8'h09); sta(16'h0201);
    lda(16'hA000); sta(16'h0202);
    w9(8'h20); w9(8'hFE);
        end
        cw(12'h0A1, 8'd5);
        cw(12'h0A7, 8'd2);
        cw(12'h0AA, mode == 1 ? 8'd2 : 8'd1);
        if (mode == 3) cw(12'h0AE, 8'd0);
        #100 sys_rst = 0; ui_rst = 0;
        #1500000;
        if (mode == 0) begin
            chk(16'h0200, 8'h11, "bank 0 byte"); chk(16'h0201, 8'h33, "bank 3 byte"); chk(16'h0202, 8'h34, "bank 3 top byte");
            chk(16'h0203, 8'h01, "screen RAM"); chk(16'h0204, 8'h5A, "Waterloo $A000"); chk(16'h0205, 8'hC3, "Waterloo $E010");
            chk(16'h0206, 8'hD6, "dongle reset value"); chk(16'h0207, 8'h30, "ACIA status");
            chk(16'h020B, 8'hD6, "dongle step 1"); chk(16'h020C, 8'h56, "dongle step 2"); chk(16'h020D, 8'h46, "dongle step 3");
            chk(16'h0208, 8'h33, "RAM write protect"); chk(16'h0209, 8'hEA, "ROM back at $9000"); chk(16'h020A, 8'hA5, "marker");
            if (dut.mem.vram.mem[0] !== 8'h01) begin $display("FAIL video RAM byte %h", dut.mem.vram.mem[0]); errs = errs + 1; end
        end else if (mode == 2) begin
            chk(16'h0200, 8'hA5, "marker");
            for (i = 0; i < 16; i = i + 1) begin
                chk(16'h0220 + i, 8'h40 + i, "bank first byte"); chk(16'h0240 + i, 8'hA0 + i, "bank last byte");
            end
        end else if (mode == 3) begin
            chk(16'h0200, 8'hA5, "marker"); chk(16'h0220, 8'hEA, "ROM behind $9000 at 32 KiB");
        end else begin
            chk(16'h0200, 8'hA5, "6502 ran first"); chk(16'h0201, 8'h09, "6809 took over"); chk(16'h0202, 8'h5A, "6809 reads Waterloo");
            chk(16'h0203, 8'h00, "6502 stopped");
        end
        if (errs == 0) $display("PASS tb_pet_spet mode %0d", mode);
        else $display("FAIL tb_pet_spet mode %0d: %0d errors", mode, errs);
        $finish;
    end

endmodule
