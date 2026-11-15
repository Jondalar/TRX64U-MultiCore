// T134 Q13: the SuperPET board. mode 0: the CPU switch says 6809; a synthetic program in the Waterloo ROM area (no Waterloo byte in git) banks the
// $9000 window, writes the screen RAM, reads the ROM at $A000 / $E010, the dongle and the ACIA, write protects the RAM. mode 1: PROG: the 6502 unlocks the
// control register and hands over to the 6809 through $EFF8 b0.
`timescale 1ns / 1ps
module tb_pet_real;
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


    // T216 S9, S11: a real ROM set (VICE's data/PET files, not in git: +roms=<dir>, skipped without them) boots a model; +model=N (the core's board number 0..5), +hz50,
    // +basic= +edit= +kernal= +chars= the file names, +ms= the simulated time. The screen text and the CRTC state are printed; a model with a CRTC must still show 80 columns at the end.
    integer i, errs = 0, fd, c, n, row, col, ms, model, cols;
    reg [8*200-1:0] dir, fb, fe, fk, fc;
    reg [8*260-1:0] fn;
    reg [7:0] lo;
    integer beeps = 0, gfx0 = 0, gfx1 = 0;
    always @(posedge sys_clk) if (dut.v_gfx) gfx1 = gfx1 + 1; else gfx0 = gfx0 + 1;
    reg cb2_d = 0;
    always @(posedge sys_clk) begin cb2_d <= dut.cb2_audio; if (dut.cb2_audio != cb2_d) beeps = beeps + 1; end
    reg [15:0] jsr [0:31]; integer jn = 0, jk;
    always @(posedge sys_clk) if (dut.rdy && dut.sync_o && dut.cpu_di == 8'h20) begin jsr[jn % 32] = dut.a65; jn = jn + 1; end
    task loadrom(input [8*200-1:0] name, input integer where, input integer is_chr); begin
        $sformat(fn, "%0s/%0s", dir, name);
        fd = $fopen(fn, "rb");
        if (fd == 0) begin $display("SKIP tb_pet_real: %0s not found", fn); $finish; end
        n = 0; c = $fgetc(fd);
        while (c != -1) begin
            if (is_chr) dut.mem.chr.mem[n] = c[7:0]; else dut.mem.rom.mem[where - 16'h8000 + n] = c[7:0];
            n = n + 1; c = $fgetc(fd);
        end
        $fclose(fd);
    end endtask
    task cw(input [11:0] a, input [7:0] d); begin
        @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1; @(posedge cpu_clk); cw_we <= 0; end endtask
    initial begin
        if (!$value$plusargs("roms=%s", dir)) begin $display("SKIP tb_pet_real: +roms=<VICE data/PET> not given"); $finish; end
        if (!$value$plusargs("model=%d", model)) model = 3;
        if (!$value$plusargs("ms=%d", ms)) ms = 60;
        if (!$value$plusargs("basic=%s", fb)) fb = "basic-4.901465-23-20-21.bin";
        if (!$value$plusargs("edit=%s", fe)) fe = "edit-4-80-b-60Hz.901474-03.bin";
        if (!$value$plusargs("kernal=%s", fk)) fk = "kernal-4.901465-22.bin";
        if (!$value$plusargs("chars=%s", fc)) fc = "characters-2.901447-10.bin";
        for (i = 0; i < 32768; i = i + 1) dut.mem.rom.mem[i] = 8'hEA;
        for (i = 0; i < 4096; i = i + 1) dut.mem.chr.mem[i] = 8'h00;
        for (i = 0; i < 2048; i = i + 1) dut.mem.vram.mem[i] = 8'h20;
        for (i = 0; i < 65536; i = i + 1) dut.xram.mem[i] = 8'h00;
        for (i = 0; i < 32768; i = i + 1) dut.mem.ram.mem[i] = 8'h00;
        loadrom(fb, (model == 0 || model == 1) ? 16'hC000 : 16'hB000, 0);     // BASIC 1 / 2: 8 KiB from $C000, BASIC 4: 12 KiB from $B000 (a 2001 + BASIC 2 is board 0 with a BASIC 2 file)
        loadrom(fe, 16'hE000, 0);
        loadrom(fk, 16'hF000, 0);
        loadrom(fc, 0, 1);
        if ($test$plusargs("hz50")) cw(12'h0A5, 8'd1);
        cw(12'h0A1, model[7:0]);
        cw(12'h0A7, 8'd2);
        #100 sys_rst = 0; ui_rst = 0;
        repeat (ms) #1000000;
        cols = dut.col80 ? 80 : 40;
        $display("board %0d after %0d ms: PC %h, CRTC R1 %0d R12 %h, picture %0d columns, CB2 edges %0d, gfx low %0d high %0d", model, ms, dut.a65, dut.r1, dut.r12, cols, beeps, gfx0, gfx1);
        $write("last JSR sites:"); for (jk = (jn > 32 ? jn - 32 : 0); jk < jn; jk = jk + 1) $write(" %h", jsr[jk % 32]); $write("\n");
        for (row = 0; row < 25; row = row + 1) begin
            for (col = 0; col < 80; col = col + 1) begin
                lo = dut.mem.vram.mem[row * (cols == 80 ? 80 : 40) + col];
                if (col < cols) $write("%s", (lo[6:0] >= 7'd1 && lo[6:0] <= 7'd26) ? 8'd64 + lo[6:0] : (lo[6:0] >= 7'd32 && lo[6:0] < 7'd64) ? lo[6:0] : 8'd46);
            end
            $write("\n");
        end
        if (model >= 3 && cols != 80) begin $display("FAIL tb_pet_real: the CRTC board shows %0d columns", cols); errs = errs + 1; end
        if (errs == 0) $display("PASS tb_pet_real");
        $finish;
    end
endmodule
