// T91: the bench of the ZX Spectrum core (zx_core with cores/zx/rtl, gen and shared; Verilator). One binary, modes by +MODE=<name>
// (cores/zx/sim/run.sh builds it and runs the modes; every mode ends with one PASS or FAIL line). The programs the Z80 runs are
// ours (tools/zx_progs.py, cores/zx/sim/prog); the benches never see a Spectrum ROM.
// T119: the picture is the frame buffer the core writes through e_* (a model of the host's port below). The picture checks that read the
// scaler's ring (video48, shadow128, pause) read it from `ring` still: at every frame-done report the newest buffer is turned back into
// colour indices and put where the old scaler path had them (rows 5 .. 274 of the buffer = lines 16 .. 285, column c = dot 106 + c + c / 9),
// so the checks are the same ones. pix_* is the dummy raster the scaler locks to.
`timescale 1ns / 1ps
module tb_zx;
    reg sys_clk = 1'b0, cpu_clk = 1'b0, mclk = 1'b0, ui_clk = 1'b0;
    always #15.86 sys_clk = ~sys_clk;                 // 31.5278 MHz
    always #5 cpu_clk = ~cpu_clk;                     // 100 MHz
    always #40.69 mclk = ~mclk;                       // 12.288 MHz
    always #6.667 ui_clk = ~ui_clk;                   // 75 MHz
`ifdef ZX_HOSTPORT   // T119 (board black, 2026-10-23): the host's own mig_arb and a MIG model instead of the bench's models of c_* and e_*
    reg sys_rst = 1'b1;
    wire ui_rst;
`else
    reg sys_rst = 1'b1, ui_rst = 1'b1;
`endif
    reg reset_req = 1'b0, halt_req = 1'b0;
    reg [63:0] keys = 64'd0;
    reg [11:0] dbg_a = 12'd0;                          // JTAG word (trxdbg "trx N")
    wire [31:0] j_rd;
    reg [4:0] joy1 = 5'd0, joy2 = 5'd0;
    reg dma_req_tgl = 1'b0, dma_we = 1'b0, dma_rom = 1'b0;
    reg [15:0] dma_addr = 16'd0;
    reg [7:0] dma_wdata = 8'd0;
    wire dma_done_tgl;
    wire [7:0] dma_rdata;
    reg cw_we = 1'b0, cw_re = 1'b0;
    reg [16:0] cw_addr = 17'd0;
    reg [7:0] cw_wdata = 8'd0;
    wire [7:0] cw_rdata;
    wire cw_ack, cw_irq, px_we, px_re;
    wire [3:0] pix_color;
    wire [9:0] pix_x;
    wire [8:0] pix_y;
    wire signed [23:0] audio_l, audio_r;
    wire halted, running;
    wire [26:0] c_addr;
    wire [2:0] c_cmd;
    wire c_en, c_rdy, c_rd_valid;
    wire [127:0] c_rd_data;
    wire win_port_use, win_port_rom, win_port_we;
    wire [15:0] win_port_addr;
    wire [7:0] win_port_wdata;
    wire [26:0] e_addr;
    wire [2:0] e_cmd;
    wire e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;
`ifdef ZX_HOSTPORT
    wire e_rdy;
`else
    reg e_rdy = 1'b1;
`endif

    zx_core #(.ID_TRX2(0)) dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(reset_req), .halt_req(halt_req), .halted(halted), .running(running),
        .keys(keys), .joy1(joy1), .joy2(joy2),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(1'b0),
        .win_port_use(win_port_use), .win_port_addr(win_port_addr), .win_port_rom(win_port_rom), .win_port_we(win_port_we),
        .win_port_wdata(win_port_wdata),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(px_we), .px_re(px_re), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0),
        .build(32'h12345678), .ddr_cal(64'd0), .dbg_re(1'b0), .dbg_addr(dbg_a), .j_rdata(j_rd), .j_rvalid(),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );
`ifdef ZX_HOSTPORT
    // host/ddr2_port.v mig_arb as top.v wires it for a core: c_* (the tape) and e_* (the picture) of the core, the pixel layer's v_* and the
    // CPU / REU / drive clients idle; MIG's app_* port is host/sim/mig_ui_model.v (stalls, refresh, calibration, ui_rst from the model).
    wire h_calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(1 << 22), .STALL_PCT(10), .CALIB(200)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(h_calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(h_calib),
        .req_tgl(3'b000), .addr(78'd0), .we(3'b000), .wdata(96'd0), .be(12'd0), .done_tgl(), .rdata(), .rdata_line(),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(128'd0), .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0),
        .c_wdf_rdy(), .c_rd_data(c_rd_data), .c_rd_data_valid(c_rd_valid),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0), .v_wdf_wren(1'b0), .v_wdf_rdy(),
        .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
`else
    ddr2c_model ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));
`endif

    // ---------------------------------------------------------------- the app's side: registers and window
    task cw_wr(input [11:0] a, input [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1'b1;
            @(posedge cpu_clk); cw_we <= 1'b0;
            @(posedge cpu_clk);
        end
    endtask
    reg [7:0] cw_val;
    task cw_rd(input [11:0] a);
        begin
            @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_re <= 1'b1;
            @(posedge cpu_clk); cw_re <= 1'b0;
            @(posedge cpu_clk);
            @(posedge cpu_clk); cw_val = cw_rdata;
        end
    endtask
    // A ROM access is made as the application makes it: the 16 KB bank in WIN_PAGE b2:1 (ROM_SEL), and a window address of 14 bits - the host's ROM
    // aperture (ultimate_sys.vhd) puts the C64 ROM block's own layout above them, so the bench puts 01 there and the core must not care.
    // `a` is the machine's ROM address here: bank = a[15:14].
    task win_acc(input rom, input we, input [15:0] a, input [7:0] d);
        reg t0;
        begin
            if (rom) cw_wr(12'h0D4, {5'd0, a[15:14], 1'b0});
            t0 = dma_done_tgl;
            @(posedge cpu_clk); dma_rom <= rom; dma_we <= we; dma_addr <= rom ? {2'b01, a[13:0]} : a; dma_wdata <= d; dma_req_tgl <= ~dma_req_tgl;
            while (dma_done_tgl == t0) @(posedge cpu_clk);
            cw_val = dma_rdata;
        end
    endtask
    task win_wr(input rom, input [15:0] a, input [7:0] d); win_acc(rom, 1'b1, a, d); endtask
    task win_rd(input rom, input [15:0] a); win_acc(rom, 1'b0, a, 8'd0); endtask

    // ---------------------------------------------------------------- helpers
    function [7:0] ram_b(input [16:0] a); ram_b = dut.mem.ram[a]; endfunction
    function [7:0] rom_b(input [14:0] a); rom_b = dut.mem.rom[a]; endfunction
    // the Z80 address space as the CPU sees it (48K: banks 5, 2, 0)
    function [7:0] cpu_mem(input [15:0] a);
        reg [2:0] b;
        begin
            b = (a[15:14] == 2'b01) ? 3'd5 : (a[15:14] == 2'b10) ? 3'd2 : 3'd0;
            cpu_mem = dut.mem.ram[{b, a[13:0]}];
        end
    endfunction

    integer frames_seen = 0;
    integer sw_l0, sw_s0, sw_f0, sw_l1, sw_s1, sw_f1, sw_l2, sw_s2, sw_f2;
    reg sw_m;
    reg [31:0] sw_j10, sw_j11, sw_j12, sw_j13;
    reg sw_len_ok;
    integer sw_n;
    integer wd_frames = 0;
    // ---------------------------------------------------------------- S14: the host's scaler on the core's pix_* (T91 board 2026-10-22)
    // host/vic_scaler.v as top.v wires it (vclk = sys_clk, clk = the 148.87 MHz pixel clock, blank = c64_halted, relock from the decoupler).
    reg pix_clk = 1'b0, pix_rst = 1'b1;
    always #3.358 pix_clk = ~pix_clk;
    initial begin #200; @(posedge pix_clk); pix_rst <= 1'b0; end
    wire sc_locked, sc_de;
    wire [23:0] sc_rgb;
    vic_scaler sc (.vclk(sys_clk), .pix(pix_color), .px(pix_x), .py(pix_y), .clk(pix_clk), .rst(pix_rst),
        .pal_clk(cpu_clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0), .rgb(sc_rgb), .de(sc_de), .hs(), .vs(), .cx(), .cy(),
        .locked(sc_locked), .blank(halted), .x_text(), .v60(1'b0), .scan(1'b0), .relock(1'b0));
    // the host's own measurement of the core's raster (top.v vm_*, what `trxdbg mode` prints as "VIC: lines, clocks per line, per frame")
    reg [8:0] vm_lines = 9'd0, vm_py_d = 9'd0;
    reg [11:0] vm_line_clk = 12'd0, vm_lc = 12'd0;
    reg [19:0] vm_frame_clk = 20'd0, vm_fc = 20'd0;
    reg [9:0] vm_px_d = 10'd0;
    always @(posedge sys_clk) begin
        vm_py_d <= pix_y; vm_px_d <= pix_x; vm_lc <= vm_lc + 12'd1; vm_fc <= vm_fc + 20'd1;
        if (pix_x == 10'd0 && vm_px_d != 10'd0) begin vm_line_clk <= vm_lc; vm_lc <= 12'd1; end
        if (pix_y == 9'd0 && vm_py_d != 9'd0) begin vm_lines <= vm_py_d + 9'd1; vm_frame_clk <= vm_fc; vm_fc <= 20'd1; end
    end
    integer sc_lock_at = -1, sc_nonblack = 0, sc_active = 0;
    always @(posedge pix_clk) begin
        if (sc_locked && sc_lock_at < 0) sc_lock_at = frames_seen;
        if (sc_locked && sc_de) begin sc_active = sc_active + 1; if (sc_rgb != 24'd0) sc_nonblack = sc_nonblack + 1; end
    end
    always @(posedge sys_clk) if (dut.vs_rise) begin wd_frames <= wd_frames + 1; if (wd_frames > 250) begin $display("FAIL %0s: watchdog, the bench did not end within 250 frames", mode); $finish; end end
    always @(posedge sys_clk) if (dut.vs_rise) begin
        frames_seen <= frames_seen + 1;
        if ($test$plusargs("PROGRESS") && (frames_seen % 10) == 0) $display("  frame %0d  pc=%04x", frames_seen, dut.cpu_a);
    end

    // ---------------------------------------------------------------- the machine's load and the result compare
    reg [8*200-1:0] mode, romfile, expfile, rom2file, exp2file;
    integer maxframes, i, errs;
    reg [7:0] expm [0:255];
    reg [7:0] img [0:32767];

    task boot(input model128);
        begin
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, {7'd0, model128});
            $readmemh(romfile, dut.mem.rom);
            cw_wr(12'h0A0, 8'h00);
        end
    endtask
    task wait_done;
        begin
            while (cpu_mem(16'h9FFF) != 8'hA5 && frames_seen < maxframes) @(posedge sys_clk);
        end
    endtask
    // xlo..xhi: result bytes that may differ and are reported as known deviations (T80's SCF/CCF X/Y flags, see the report); 1,0 = none
    integer xdev;
    task compare(input [8*20-1:0] tag, input integer xlo, input integer xhi);
        begin
            if (cpu_mem(16'h9FFF) != 8'hA5) begin
                $display("FAIL %0s: no end marker after %0d frames", tag, frames_seen);
                $finish;
            end
            $readmemh(expfile, expm);
            errs = 0; xdev = 0;
            for (i = 0; i < 256; i = i + 1)
                if (cpu_mem(16'h9000 + i) !== expm[i]) begin
                    if (i >= xlo && i <= xhi) begin
                        xdev = xdev + 1;
                        $display("  known deviation at $%04x: got %02x expected %02x", 16'h9000 + i, cpu_mem(16'h9000 + i), expm[i]);
                    end else begin
                        if (errs < 10) $display("  mismatch at $%04x: got %02x expected %02x", 16'h9000 + i, cpu_mem(16'h9000 + i), expm[i]);
                        errs = errs + 1;
                    end
                end
            if (errs == 0) $display("PASS %0s: %0d result bytes equal the expected ones after %0d frames (%0d known deviations)", tag, 256 - xdev, frames_seen, xdev);
            else $display("FAIL %0s: %0d of 256 result bytes differ", tag, errs);
        end
    endtask

    // ---------------------------------------------------------------- S4: the clocks
    integer clk_cnt = 0, px_cnt = 0, per_n = 0, last_per = 0, last_px = 0;
    integer mk_t = 0, mk_n = 0, mk_ok = 0;
    reg [9:0] pxd = 10'd0;
    always @(posedge sys_clk) begin
        clk_cnt <= clk_cnt + 1;
        mk_t <= mk_t + 1;
        if (dut.ce_pix) px_cnt <= px_cnt + 1;
        if (dut.vs_rise) begin
            if (per_n >= 2) begin last_per <= clk_cnt + 1; last_px <= px_cnt; end
            per_n <= per_n + 1; clk_cnt <= 0; px_cnt <= 0;
        end
        // the scaler's lock event (vic_scaler.v: py == ALIGN && px == 0), and the time since the one before
        if (pix_x == 10'd0 && pix_y == 9'd20 && pxd != 10'd0) begin
            mk_n <= mk_n + 1;
            if (mk_n >= 2 && mk_t + 1 == 628992) mk_ok <= mk_ok + 1;
            mk_t <= 0;
        end
        pxd <= pix_x;
    end

    // ---------------------------------------------------------------- S6: the interrupts' pushed PCs
    integer int_n = 0;
    reg nint_d = 1'b0;
    always @(posedge sys_clk) begin
        nint_d <= dut.nINT;
        if (dut.nINT && !nint_d) begin
            int_n <= int_n + 1;
            if (mode == "cont48" || mode == "cont128") $display("INT %0d PC %04x", int_n, {cpu_mem(16'hBFEF), cpu_mem(16'hBFEE)});
        end
    end

    // ---------------------------------------------------------------- S7: the picture as the scaler stores it
    // The scaler's ring (vic_scaler.v: every clock the colour is written at {py, px}); a frame is complete at the VSync rise (the
    // lines that are shown are written between the first line after VBlank and the end of the picture, the VSync comes later).
    localparam X0 = 106;
    reg [3:0] ring  [0:312*512-1];
    reg [3:0] snap0 [0:312*512-1];
    reg [3:0] snap1 [0:312*512-1];
    reg [3:0] snap2 [0:312*512-1];
    integer snap_f0 = -1, snap_f1 = -1, snap_f2 = -1, sn, sq;
    // the host's e_ port (T119): a command is taken when e_en and e_rdy are both high; the port is busy one clock in eight. The frame
    // buffers are at the register default FB_BASE 0x01D00000, four of FB_STRIDE 1 MB; e_addr counts 16 bit units, a word is 8 of them.
    localparam integer FB_W0 = 32'h01D00000 / 2 / 8;           // the word index of buffer 0
    reg [127:0] fbm [0:4 * 65536 - 1];
    reg [4 * 65536 - 1:0] fbseen, fbever;
    integer fb_writes = 0, fb_bad_cmd = 0, fb_writes_held = 0;
    integer fbi;
    initial begin
        for (fbi = 0; fbi < 4 * 65536; fbi = fbi + 1) fbm[fbi] = 128'd0;
        fbseen = 0; fbever = 0;
    end
    always @(posedge ui_clk) begin
`ifndef ZX_HOSTPORT
        e_rdy <= ($random & 7) != 0;
`endif
        if (e_en && e_rdy) begin
            if (e_cmd != 3'b000 || !e_wdf_wren || e_wdf_mask != 16'd0 || e_addr[2:0] != 3'd0 || e_addr[26:3] < FB_W0 || e_addr[26:3] >= FB_W0 + 4 * 65536)
                fb_bad_cmd = fb_bad_cmd + 1;
            else begin
                fbm[e_addr[26:3] - FB_W0] = e_wdf_data;
                fbseen[e_addr[26:3] - FB_W0] = 1'b1;
                fbever[e_addr[26:3] - FB_W0] = 1'b1;
            end
            fb_writes = fb_writes + 1;
            if (halted) fb_writes_held = fb_writes_held + 1;
        end
    end
    // the palette of the core: {bright, green, red, blue} -> RGB565 (215 of 255, bright 255)
    function [15:0] rgb_of(input [3:0] i);
        reg [4:0] l5;
        reg [5:0] l6;
        begin
            l5 = i[3] ? 5'd31 : 5'd26;
            l6 = i[3] ? 6'd63 : 6'd53;
            rgb_of = {i[1] ? l5 : 5'd0, i[2] ? l6 : 6'd0, i[0] ? l5 : 5'd0};
        end
    endfunction
    function [3:0] idx_of(input [15:0] c);
        idx_of = {(c[15:11] == 5'd31) || (c[10:5] == 6'd63) || (c[4:0] == 5'd31), c[10:5] != 6'd0, c[15:11] != 5'd0, c[4:0] != 5'd0};
    endfunction
    integer hp_bad, hp_polls, hp_valid, hp_badbuf, hp_ring, hp_lastb, hp_words, hp_diff;
    reg [7:0] hp_seq;
    reg [31:0] hp_j;
    reg hp_ok;
    integer cv_b, cv_r, cv_c, cv_n = 0, cv_badpix = 0, cv_rows_full = 0, cv_w, cv_lastbuf = -1, cv_bufs_round = 0, cv_seq0 = -1;
    reg [127:0] cv_word;
    reg [15:0] cv_p;
    reg [3:0] cv_i;
    reg cv_full;
    always @(dut.fb_tgl) if (cv_n >= 0 && sys_rst == 1'b0) begin
        cv_n = cv_n + 1;
        cv_b = dut.fb_latest[1:0];
        cv_full = 1'b1;
        for (cv_r = 0; cv_r < 280; cv_r = cv_r + 1) begin
            for (cv_w = 0; cv_w < 43; cv_w = cv_w + 1) if (!fbseen[cv_b * 65536 + cv_r * 43 + cv_w]) cv_full = 1'b0;
            if (cv_r >= 5 && cv_r < 275)
                for (cv_c = 0; cv_c < 344; cv_c = cv_c + 1) begin
                    cv_word = fbm[cv_b * 65536 + cv_r * 43 + cv_c / 8];
                    cv_p = cv_word >> (16 * (cv_c % 8));
                    cv_i = idx_of(cv_p);
                    if (rgb_of(cv_i) !== cv_p) cv_badpix = cv_badpix + 1;
                    ring[(cv_r + 11) * 512 + 106 + cv_c + cv_c / 9] = cv_i;
                end
        end
        if (cv_full) cv_rows_full = cv_rows_full + 1;
        if (cv_lastbuf >= 0 && cv_b == ((cv_lastbuf + 1) & 3)) cv_bufs_round = cv_bufs_round + 1;
        cv_lastbuf = cv_b;
        fbseen = 0;                                  // the next frame has to write every word again
    end
    always @(posedge sys_clk) if (dut.vs_rise) begin
        if (frames_seen == snap_f0) for (sn = 0; sn < 312*512; sn = sn + 1) snap0[sn] = ring[sn];
        if (frames_seen == snap_f1) for (sn = 0; sn < 312*512; sn = sn + 1) snap1[sn] = ring[sn];
        if (frames_seen == snap_f2) for (sn = 0; sn < 312*512; sn = sn + 1) snap2[sn] = ring[sn];
    end
    // the screen of RAM bank b as the ULA reads it, and the colour index of paper pixel (x, y) by the Spectrum's rules
    function [7:0] scr_b(input [2:0] bank, input [13:0] off); scr_b = dut.mem.ram[{bank, off}]; endfunction
    function [3:0] zx_exp(input [2:0] bank, input integer x, input integer y, input integer fph);
        reg [13:0] boff, aoff;
        reg [7:0] b, at;
        reg on;
        begin
            boff = {1'b0, y[7:6], y[2:0], y[5:3], x[7:3]};
            aoff = 14'h1800 + (y / 8) * 32 + (x / 8);
            b = scr_b(bank, boff); at = scr_b(bank, aoff);
            on = b[7 - (x % 8)] ^ (at[7] & fph[0]);
            zx_exp = {at[6], on ? at[2:0] : at[5:3]};
            if (zx_exp == 4'd8) zx_exp = 4'd0;           // bright black is black: the frame buffer holds RGB, one value for both (the palette's 215 / 255 levels of black are 0)
        end
    endfunction
    function integer dot(input integer k); dot = X0 + k + k / 9; endfunction
    // mismatches of the 256 x 192 paper area in snapshot sel at horizontal pixel offset k0
    function integer paper_diff(input integer sel, input [2:0] bank, input integer fph, input integer k0);
        integer x, y, n;
        reg [3:0] got;
        begin
            n = 0;
            for (y = 0; y < 192; y = y + 1)
                for (x = 0; x < 256; x = x + 1) begin
                    got = (sel == 0) ? snap0[(55 + y) * 512 + dot(k0 + x)] : (sel == 1) ? snap1[(55 + y) * 512 + dot(k0 + x)] : snap2[(55 + y) * 512 + dot(k0 + x)];
                    if (got !== zx_exp(bank, x, y, fph)) n = n + 1;
                end
            paper_diff = n;
        end
    endfunction
    function integer best_k0(input integer sel, input [2:0] bank, input integer fph);
        integer k, d, bd, bk;
        begin
            bd = 1 << 30; bk = -1;
            for (k = 24; k <= 48; k = k + 1) begin
                // a quick rating from the first 8 lines only
                d = paper_rows(sel, bank, fph, k, 8);
                if (d < bd) begin bd = d; bk = k; end
            end
            best_k0 = bk;
        end
    endfunction
    function integer paper_rows(input integer sel, input [2:0] bank, input integer fph, input integer k0, input integer rows);
        integer x, y, n;
        reg [3:0] got;
        begin
            n = 0;
            for (y = 0; y < rows; y = y + 1)
                for (x = 0; x < 256; x = x + 1) begin
                    got = (sel == 0) ? snap0[(55 + y) * 512 + dot(k0 + x)] : (sel == 1) ? snap1[(55 + y) * 512 + dot(k0 + x)] : snap2[(55 + y) * 512 + dot(k0 + x)];
                    if (got !== zx_exp(bank, x, y, fph)) n = n + 1;
                end
            paper_rows = n;
        end
    endfunction
    // the border band the program writes for the visual line v (video48: 8 lines per colour, written 224 T-states apart from the line after the interrupt's)
    function [2:0] band(input integer v); band = (((v + 63) % 312) >> 3) & 7; endfunction
    function integer border_diff(input integer shift_lines);
        integer idx, v, n;
        reg [3:0] l, r;
        begin
            n = 0;
            for (idx = 0; idx < 270; idx = idx + 1) begin
                v = (idx < 39) ? 273 + idx : idx - 39;
                l = snap0[(16 + idx) * 512 + dot(2)];
                r = snap0[(16 + idx) * 512 + dot(340)];
                if (l !== {1'b0, band(v + shift_lines)} || r !== {1'b0, band(v + shift_lines)}) n = n + 1;
            end
            border_diff = n;
        end
    endfunction
    integer v_k0, v_d0, v_d1, v_d2, v_ph, v_b, v_ctl1, v_ctl2, v_alt;

    // ---------------------------------------------------------------- S8: sound (audio128)
    // rising zero crossings of audio_l, with the time in sys_clk, per phase of the program (the byte at $9000)
    integer a_t = 0, a_ph = 0, a_phs = 0, a_nz = 0;
    integer xn [0:4];
    integer xt [0:4*400-1];
    reg signed [23:0] a_prev = 24'sd0;
    integer ai;
    initial for (ai = 0; ai < 5; ai = ai + 1) xn[ai] = 0;
    always @(posedge sys_clk) begin
        a_t <= a_t + 1;
        if (mode == "audio128") begin
            if (cpu_mem(16'h9000) != a_ph[7:0]) begin a_ph <= cpu_mem(16'h9000); a_phs <= a_t; end
            if (a_prev < 0 && audio_l >= 0 && a_ph >= 1 && a_ph <= 3 && a_t - a_phs > 2 * 628992 && xn[a_ph] < 400) begin
                xt[a_ph * 400 + xn[a_ph]] <= a_t;
                xn[a_ph] <= xn[a_ph] + 1;
            end
            if (a_ph == 4 && a_t - a_phs > 8 * 628992 && audio_l != 0) a_nz <= a_nz + 1;
            a_prev <= audio_l;
        end
    end
    function real mean_period(input integer ph);
        begin
            mean_period = (xn[ph] > 1) ? (xt[ph * 400 + xn[ph] - 1] - xt[ph * 400]) * 1.0 / (xn[ph] - 1) : 0.0;
        end
    endfunction
    function real cv_period(input integer ph);
        integer i;
        real m, v, d;
        begin
            m = mean_period(ph); v = 0.0;
            for (i = 0; i + 1 < xn[ph]; i = i + 1) begin d = (xt[ph * 400 + i + 1] - xt[ph * 400 + i]) - m; v = v + d * d; end
            cv_period = (xn[ph] > 2 && m > 0.0) ? $sqrt(v / (xn[ph] - 1)) / m : 0.0;
        end
    endfunction
    real beep_t, ay_tp, own_b, nom_b, own_a, nom_a, m1, m2, e1, e2, e1n, e2n, cv1, cv2, cv3, ctl;
    integer meta_fd, meta_r;
    reg [8*20-1:0] meta_k;
    integer meta_v;

    // ---------------------------------------------------------------- S9/S10: keys, joysticks, tape
    // the machine's key matrix as hardware facts: half-row r (port high byte with bit r low), bit c; the keys of the Spectrum's rows
    // (row 0: CAPS Z X C V; 1: A S D F G; 2: Q W E R T; 3: 1 2 3 4 5; 4: 0 9 8 7 6; 5: P O I U Y; 6: ENTER L K J H; 7: SPACE SYM M N B)
    function integer kpos(input integer r, input integer c); kpos = r * 5 + c; endfunction
    reg [39:0] want_mat;
    reg [7:0] want_kemp;
    integer kerr, kn, kfd, kr2, sc0;
    reg [63:0] kv;
    reg [39:0] mv;
    task wait_scans(input integer n);
        begin
            sc0 = cpu_mem(16'h9009);
            while (((cpu_mem(16'h9009) - sc0) & 255) < n) @(posedge sys_clk);
        end
    endtask
    task check_rows(input [8*24-1:0] what);
        integer r;
        reg [7:0] exp_row;
        begin
            wait_scans(3);
            for (r = 0; r < 8; r = r + 1) begin
                exp_row = {3'b101, ~want_mat[5 * r +: 5]};
                if (cpu_mem(16'h9000 + r) !== exp_row) begin
                    if (kerr < 6) $display("  keys: %0s row %0d read %02x expected %02x", what, r, cpu_mem(16'h9000 + r), exp_row);
                    kerr = kerr + 1;
                end
            end
            if (cpu_mem(16'h9008) !== want_kemp) begin
                if (kerr < 6) $display("  keys: %0s kempston read %02x expected %02x", what, cpu_mem(16'h9008), want_kemp);
                kerr = kerr + 1;
            end
            kn = kn + 1;
        end
    endtask
    function [39:0] joy_expect(input integer mode, input [4:0] j);          // j: 0 up, 1 down, 2 left, 3 right, 4 fire
        reg [39:0] m;
        begin
            m = 40'd0;
            case (mode)
            2: begin                                                        // Sinclair 1: 6 left, 7 right, 8 down, 9 up, 0 fire
                if (j[2]) m[kpos(4, 4)] = 1; if (j[3]) m[kpos(4, 3)] = 1; if (j[1]) m[kpos(4, 2)] = 1;
                if (j[0]) m[kpos(4, 1)] = 1; if (j[4]) m[kpos(4, 0)] = 1;
            end
            3: begin                                                        // Sinclair 2: 1 left, 2 right, 3 down, 4 up, 5 fire
                if (j[2]) m[kpos(3, 0)] = 1; if (j[3]) m[kpos(3, 1)] = 1; if (j[1]) m[kpos(3, 2)] = 1;
                if (j[0]) m[kpos(3, 3)] = 1; if (j[4]) m[kpos(3, 4)] = 1;
            end
            4: begin                                                        // Cursor: 5 left, 6 down, 7 up, 8 right, 0 fire
                if (j[2]) m[kpos(3, 4)] = 1; if (j[1]) m[kpos(4, 4)] = 1; if (j[0]) m[kpos(4, 3)] = 1;
                if (j[3]) m[kpos(4, 2)] = 1; if (j[4]) m[kpos(4, 0)] = 1;
            end
            default: ;
            endcase
            joy_expect = m;
        end
    endfunction
    function [7:0] kemp_expect(input [4:0] j); kemp_expect = {3'b000, j[4], j[0], j[1], j[2], j[3]}; endfunction
    reg [4:0] jv;
    integer jp, jm, kvn;
    reg [8*200-1:0] vecfile, ddrfile;
    integer t_pos0, t_pos1, t_stat, t_n, t_ok;

    // ---------------------------------------------------------------- S11: snapshots, loaded the way the app has to load them
    reg [7:0] snap_ram [0:131071];
    reg [11:0] snap_ra [0:63];
    reg [7:0]  snap_rv [0:63];
    reg [7:0] ayw [0:15];
    integer snap_nb, snap_nr, snap_bank [0:7], sb, sk, snap_model;
    reg [8*200-1:0] ramfile, regfile;
    integer rfd, ra_i, rv_i;
    task snap_load;
        begin
            if (!$value$plusargs("RAM=%s", ramfile)) ramfile = "";
            if (!$value$plusargs("REGS=%s", regfile)) regfile = "";
            if (!$value$plusargs("NB=%d", snap_nb)) snap_nb = 3;
            for (sb = 0; sb < 8; sb = sb + 1) begin
                snap_bank[sb] = sb;
                if (sb == 0 && $value$plusargs("B0=%d", i)) snap_bank[0] = i;
                if (sb == 1 && $value$plusargs("B1=%d", i)) snap_bank[1] = i;
                if (sb == 2 && $value$plusargs("B2=%d", i)) snap_bank[2] = i;
            end
            $readmemh(ramfile, snap_ram);
            cw_wr(12'h0A0, 8'h01);
            // the registers file: "offset value" lines (model, SNAP_DIR, 7FFD, border, AY)
            rfd = $fopen(regfile, "r");
            snap_nr = 0;
            while ($fscanf(rfd, "%h %h", ra_i, rv_i) == 2) begin
                snap_ra[snap_nr] = ra_i; snap_rv[snap_nr] = rv_i; snap_nr = snap_nr + 1;
                if (ra_i >= 12'h120 && ra_i < 12'h130) ayw[ra_i - 12'h120] = rv_i;
                if (ra_i == 12'h0A1) snap_model = rv_i;
            end
            $fclose(rfd);
            for (sk = 0; sk < snap_nr; sk = sk + 1)
                cw_wr(snap_ra[sk], snap_rv[sk]);
            // RAM through the window: bank b at b * 16 KB, the page register selects the 64 KB half
            for (sb = 0; sb < snap_nb; sb = sb + 1) begin
                cw_wr(12'h0D4, {7'd0, snap_bank[sb][2]});
                for (sk = 0; sk < 16384; sk = sk + 1) win_wr(1'b0, {snap_bank[sb][1:0], 14'd0} + sk, snap_ram[sb * 16384 + sk]);
            end
            cw_wr(12'h0D4, 8'h00);
            // ROM markers for the 128K check (ROM 0 / ROM 1 first bytes)
            dut.mem.rom[15'h0000] = 8'hB0;
            dut.mem.rom[15'h4000] = 8'hB1;
            cw_wr(12'h0A5, 8'h01);                  // SNAP_GO
            repeat (200) @(posedge cpu_clk);        // the start crosses into sys_clk: busy is seen a few clocks after the write (2 us is plenty)
            cw_rd(12'h0A4);
            while (cw_val[2]) cw_rd(12'h0A4);       // busy
            cw_wr(12'h0A0, 8'h00);                  // the app lets go of the reset hold; the machine runs on
        end
    endtask
    integer ay_mask [0:15];
    initial begin
        ay_mask[0] = 255; ay_mask[1] = 15; ay_mask[2] = 255; ay_mask[3] = 15; ay_mask[4] = 255; ay_mask[5] = 15; ay_mask[6] = 31; ay_mask[7] = 255;
        ay_mask[8] = 31; ay_mask[9] = 31; ay_mask[10] = 31; ay_mask[11] = 255; ay_mask[12] = 255; ay_mask[13] = 15; ay_mask[14] = 255; ay_mask[15] = 255;
    end
    integer sk_err;
    always @(posedge sys_clk) if ($test$plusargs("SNAPDBG")) begin
        if (dut.go_ev) $display("  [snap] go_ev ctrl_s2=%02x sst=%0d busy=%b", dut.ctrl_s2, dut.sst, dut.snap_busy);
        if (dut.snap_dirset) $display("  [snap] DIRSet pc=%04x sp=%04x a=%02x cpu_reset=%b", dut.snap_regs[79:64], dut.snap_regs[63:48], dut.snap_regs[7:0], dut.cpu_reset);
        if (dut.sst != 0) $display("  [snap] sst=%0d cpu_reset=%b mask=%b rst_q=%b", dut.sst, dut.cpu_reset, dut.snap_mask, dut.rst_q);
    end

    // ---------------------------------------------------------------- S13: Freeze
    // per machine frame (counted at the ULA's VSync, so a pause does not make frames): a hash of the picture as the scaler stores it, the
    // screen RAM, the Z80's registers and the tape position. The reference run has no pause; the other one has three, and every line
    // it prints must be a line of the reference.
    reg [63:0] fh;
    integer pf_n = 0, fy, fx, fa, fk;
    always @(posedge sys_clk) if (dut.vs_rise && (mode == "pause_ref" || mode == "pause_run")) begin
        fh = 64'hcbf29ce484222325;
        for (fy = 16; fy < 286; fy = fy + 1)
            for (fx = 96; fx < 496; fx = fx + 1) fh = (fh ^ ring[fy * 512 + fx]) * 64'h100000001b3;
        for (fa = 0; fa < 16384; fa = fa + 1) fh = (fh ^ dut.mem.ram[{3'd5, fa[13:0]}]) * 64'h100000001b3;
        for (fa = 0; fa < 256; fa = fa + 1) fh = (fh ^ dut.mem.ram[{3'd2, fa[13:0]}]) * 64'h100000001b3;
        for (fk = 0; fk < 27; fk = fk + 1) fh = (fh ^ dut.cpu_reg[8 * fk +: 8]) * 64'h100000001b3;
        for (fk = 0; fk < 4; fk = fk + 1) fh = (fh ^ dut.tape.pos[8 * fk +: 8]) * 64'h100000001b3;
        fh = (fh ^ dut.tape.level) * 64'h100000001b3;
        pf_n <= pf_n + 1;
        $display("F %0d %016h", pf_n + 1, fh);
    end
    // while the machine is held: nothing may move
    integer h_ce = 0, h_cpu = 0, h_ay = 0, h_tape = 0, h_pix = 0, h_aud = 0, h_age = 0, h_holds = 0, h_tpos = 0, h_tbad = 0;
    integer mk_fc = -1, mk_bad = 0, mk_cnt2 = 0;
    reg [9:0] hx_d = 10'd0;
    reg halted_d = 1'b0;
    always @(posedge sys_clk) begin
        halted_d <= halted;
        hx_d <= pix_x;
        if (halted) begin
            h_age <= h_age + 1;
            if (dut.ce_pix) h_ce <= h_ce + 1;
            if (dut.ce_cpu_sp || dut.ce_cpu_sn) h_cpu <= h_cpu + 1;
            if (dut.ce_ay) h_ay <= h_ay + 1;
            if (dut.ce_tape) h_tape <= h_tape + 1;
            if (e_en && e_rdy) h_pix <= h_pix + 1;
            if (h_age > 3000 && audio_l != 0) h_aud <= h_aud + 1;
            if (!halted_d) begin h_holds <= h_holds + 1; h_tpos <= dut.tape.pos; end
            if (dut.tape.pos != h_tpos && halted_d) h_tbad <= h_tbad + 1;
        end else h_age <= 0;
        // the scaler's lock event: always at the same place of the free-running frame counter
        if (pix_x == 10'd0 && pix_y == 9'd20 && hx_d != 10'd0) begin
            mk_cnt2 <= mk_cnt2 + 1;
            if (mk_fc < 0) mk_fc <= dut.fc; else if (dut.fc != mk_fc) mk_bad <= mk_bad + 1;
        end
    end

    initial begin
        if (!$value$plusargs("MODE=%s", mode)) mode = "z80";
        if (!$value$plusargs("FRAMES=%d", maxframes)) maxframes = 400;
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "";
        if (!$value$plusargs("EXP=%s", expfile)) expfile = "";
        repeat (8) @(posedge sys_clk);
        sys_rst = 1'b0;
`ifndef ZX_HOSTPORT
        ui_rst = 1'b0;
`endif
        case (mode)
        "z80", "z80s", "page48": begin
            boot(1'b0);
            wait_done;
            if (mode == "z80" || mode == "z80s") compare(mode, 18, 21);     // SCF, CCF: T80 deviates in their X/Y flags (its README)
            else compare(mode, 1, 0);
        end
        "page128": begin
            boot(1'b1);
            wait_done;
            compare(mode, 1, 0);
            // the app's window sees the same RAM and ROM: bank b at b * 16 KB (the page register selects the 64 KB half)
            errs = 0;
            for (i = 0; i < 8; i = i + 1) begin
                cw_wr(12'h0D4, {7'd0, i[2]});
                win_rd(1'b0, {i[1:0], 14'd0});
                if (cw_val !== 8'h10 + i) begin errs = errs + 1; $display("  window: bank %0d first byte %02x", i, cw_val); end
                win_rd(1'b0, {i[1:0], 14'h3FFF});
                if (cw_val !== 8'h20 + i) begin errs = errs + 1; $display("  window: bank %0d last byte %02x", i, cw_val); end
            end
            win_rd(1'b1, 16'h0000); if (cw_val !== 8'hF3) begin errs = errs + 1; $display("  window: rom0 %02x", cw_val); end
            win_rd(1'b1, 16'h4000); if (cw_val !== 8'hB1) begin errs = errs + 1; $display("  window: rom1 %02x", cw_val); end
            if (errs == 0) $display("PASS %0s window: RAM banks and ROM read through the app's window equal the Z80's", mode);
            else $display("FAIL %0s window: %0d differences", mode, errs);
        end
        "model": begin
            // 48K first, then the app rewrites the ROM through the window, switches the model and the machine cold-boots into it
            boot(1'b0);
            wait_done;
            compare("model 48K", 1, 0);
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, 8'h01);
            if (!$value$plusargs("ROM2=%s", rom2file)) rom2file = "";
            if (!$value$plusargs("EXP2=%s", exp2file)) exp2file = "";
            $readmemh(rom2file, img);
            for (i = 0; i < 32768; i = i + 1) win_wr(1'b1, i[15:0], img[i]);
            // the RAM still holds the end marker of the 48K program: clear it, the 128K program must set it again
            cw_wr(12'h0D4, 8'h00);
            win_wr(1'b0, 16'h9FFF, 8'h00);
            expfile = exp2file;
            frames_seen = 0;
            cw_wr(12'h0A0, 8'h00);
            wait_done;
            compare("model 128K", 1, 0);
        end
        "clk48", "clk128": begin
            boot(mode == "clk128");
            while (frames_seen < 8) @(posedge sys_clk);
            begin : rep
                real hz, real_hz, err;
                integer tpf;
                tpf = (mode == "clk128") ? 70908 : 69888;
                hz = 31527777.78 * tpf / 628992.0;
                real_hz = (mode == "clk128") ? 3546895.0 : 3500000.0;
                err = (hz / real_hz - 1.0) * 100.0;
                $display("%0s: frame %0d sys_clk, %0d pixel clocks, lock events %0d (periods ok %0d), T-state clock %f Hz (%f %% of the real machine)",
                         mode, last_per, last_px, mk_n, mk_ok, hz, err);
                if (last_per == 628992 && last_px == ((mode == "clk128") ? 141816 : 139776) && mk_ok >= 4 && mk_ok >= mk_n - 3 &&
                    ((mode == "clk128") ? (err > 0.0 && err < 0.21) : (err > 0.0 && err < 0.09)))
                    $display("PASS %0s: frame = 628992 sys_clk, %0d pixel clocks, lock event every frame, clock %f %% off", mode, last_px, err);
                else $display("FAIL %0s: frame %0d sys_clk (want 628992), pixel clocks %0d, lock events %0d/%0d", mode, last_per, last_px, mk_ok, mk_n);
            end
        end
        "scaler", "scaler128": begin
            boot(mode == "scaler128");
            while (frames_seen < 14) @(posedge sys_clk);
            $display("scaler: host raster measurement: %0d lines, %0d clocks per line, %0d clocks per frame", vm_lines, vm_line_clk, vm_frame_clk);
            $display("scaler: locked %0d, locked at machine frame %0d, active pixels %0d, non-black %0d", sc_locked, sc_lock_at, sc_active, sc_nonblack);
            if (sc_locked && mk_ok >= 4) $display("PASS scaler: the host's vic_scaler locks to the dummy raster (the picture is the frame buffer's, T119)");
            else $display("FAIL scaler: the host's vic_scaler did not lock (locked %0d, line 20 events in the frame period %0d)", sc_locked, mk_ok);
        end
        "cont48", "cont128": begin
            boot(mode == "cont128");
            while (int_n < 16) @(posedge sys_clk);
            $display("done %0s", mode);
        end
        "video48": begin
            snap_f0 = 24; snap_f1 = 40; snap_f2 = 56;
            boot(1'b0);
            while (frames_seen < 58) @(posedge sys_clk);
            v_ph = -1;
            v_k0 = best_k0(0, 3'd5, 0);
            v_d0 = paper_diff(0, 3'd5, 0, v_k0);
            v_alt = paper_diff(0, 3'd5, 1, best_k0(0, 3'd5, 1));
            if (v_d0 == 0) v_ph = 0; else if (v_alt == 0) v_ph = 1;
            if (v_ph == 1) v_k0 = best_k0(0, 3'd5, 1);
            v_d1 = paper_diff(1, 3'd5, 1 - v_ph, v_k0);
            v_d2 = paper_diff(2, 3'd5, v_ph, v_k0);
            v_ctl1 = paper_diff(1, 3'd5, v_ph, v_k0);             // control: the frame 16 later with the SAME flash phase must differ
            v_b = border_diff(0);
            v_ctl2 = border_diff(8);                               // control: the bands shifted by one colour must differ
            $display("video48: paper pixel 0 at picture pixel %0d (dot %0d); flash phase %0d; mismatches: frame A %0d, +16 frames (inverted) %0d, +32 frames %0d; border %0d of 270 lines wrong",
                     v_k0, dot(v_k0), v_ph, (v_ph == 0) ? v_d0 : v_alt, v_d1, v_d2, v_b);
            $display("video48 controls: +16 frames with the wrong flash phase differs in %0d pixels; bands shifted by 8 lines differ in %0d lines", v_ctl1, v_ctl2);
            if (v_ph >= 0 && v_d1 == 0 && v_d2 == 0 && v_b == 0 && v_ctl1 > 0 && v_ctl2 > 0)
                $display("PASS video48: 49152 paper pixels (15 colours, bright, FLASH) and 270 border lines equal the Spectrum rules, FLASH toggles every 16 frames");
            else $display("FAIL video48");
        end
        "pixw", "pixw128", "pixwhost": begin
            // T119 S2: the frame buffer the core writes through e_*. The picture is the one of video48 / shadow128; here the buffer is
            // checked as such: every pixel is a palette colour of the core, all 43 x 280 words of every frame are written, the buffers
            // go round, every command is a plain write, the paper sits in the middle (44 pixels of border each side, 44 lines above)
            // in both models, and the bench's own rules (Spectrum screen, FLASH, border bands) are met on what the buffer holds.
            hp_ok = 1'b1;
            if (mode == "pixw" || mode == "pixwhost") begin
                snap_f0 = 24; snap_f1 = 40; snap_f2 = 56;
                if (mode == "pixwhost") begin
                    // the application's way in (zx.cc zx_init / zx_release, Video::init / poll): the host's reset line (C64_MODE_RESET) is up while
                    // the core is held and the ROM is loaded, CTRL b0 comes off first, the reset line after it; the layer's buffers are written
                    // into FB_BASE / FB_STRIDE byte by byte; then the loop reads FRAME_SEQ and, when it changed, FB_LATEST, over the register bus
                    reset_req = 1'b1;
                    boot(1'b0);
                    repeat (40) @(posedge cpu_clk);
                    reset_req = 1'b0;
                    for (i = 0; i < 4; i = i + 1) begin
                        cw_wr(12'h0C4 + i, 32'h01D00000 >> (8 * i));
                        cw_wr(12'h0C8 + i, 32'h00100000 >> (8 * i));
                    end
                    hp_bad = 0;
                    for (i = 0; i < 4; i = i + 1) begin
                        cw_rd(12'h0C4 + i); if (cw_val !== ((32'h01D00000 >> (8 * i)) & 32'hFF)) hp_bad = hp_bad + 1;
                        cw_rd(12'h0C8 + i); if (cw_val !== ((32'h00100000 >> (8 * i)) & 32'hFF)) hp_bad = hp_bad + 1;
                    end
                    cw_rd(12'h0C1); hp_seq = cw_val;
                    hp_polls = 0; hp_valid = 0; hp_badbuf = 0; hp_ring = 0; hp_lastb = -1;
                    while (frames_seen < 58) begin
                        cw_rd(12'h0C1);
                        if (cw_val !== hp_seq) begin
                            hp_seq = cw_val;
                            cw_rd(12'h0C0);
                            hp_polls = hp_polls + 1;
                            if (cw_val[7]) begin
                                hp_valid = hp_valid + 1;
                                if (cw_val[6:2] != 0) hp_badbuf = hp_badbuf + 1;
                                if (hp_lastb >= 0 && cw_val[1:0] == ((hp_lastb + 1) & 3)) hp_ring = hp_ring + 1;
                                hp_lastb = cw_val[1:0];
                            end
                        end
                    end
                    // what the register bus reports is what the core holds (a frame may complete between the two reads: seq or seq - 1)
                    cw_rd(12'h0C1); hp_seq = cw_val;
                    if (hp_seq !== dut.fb_seq && hp_seq !== dut.fb_seq - 8'd1) hp_bad = hp_bad + 1;
                    // the DDR2 holds what the core wrote: every word of the four buffers, from the MIG model's memory, equals the e_ port's capture
                    hp_words = 0; hp_diff = 0;
                    for (fbi = 0; fbi < 4 * 65536; fbi = fbi + 1)
                        if (fbever[fbi]) begin
                            hp_words = hp_words + 1;
`ifdef ZX_HOSTPORT
                            if (mig.mem[(FB_W0 + fbi) % (1 << 22)] !== fbm[fbi]) hp_diff = hp_diff + 1;
`endif
                        end
                    // the board's JTAG words 8 and 9 (commands the host took, lines handed over): they follow the e_ port the bench watches
                    dbg_a = 12'd8; @(posedge ui_clk); @(posedge ui_clk);
                    hp_j = j_rd;
                    if (hp_j[31:16] > fb_writes[15:0] + 16'd2 || hp_j[31:16] + 16'd2 < fb_writes[15:0]) hp_bad = hp_bad + 1;
                    if (hp_j[15:0] < 16'd57 * 16'd280) hp_bad = hp_bad + 1;
                    hp_ok = (hp_bad == 0 && hp_polls >= 16 && hp_valid == hp_polls && hp_badbuf == 0 && hp_ring >= hp_valid - 3 &&
                             hp_words == 4 * 280 * 43 && hp_diff == 0);
                    $display("pixwhost: JTAG word 8 = %08x (%0d commands taken by the bench); register bus - FB_BASE/FB_STRIDE/JTAG wrong %0d, FRAME_SEQ changed %0d times, FB_LATEST valid %0d, bad buffer field %0d, ring steps %0d; DDR2: %0d words written (want %0d), %0d differ from the e_ port",
                             hp_j, fb_writes, hp_bad, hp_polls, hp_valid, hp_badbuf, hp_ring, hp_words, 4 * 280 * 43, hp_diff);
                end else begin
                    boot(1'b0);
                    while (frames_seen < 58) @(posedge sys_clk);
                end
                v_k0 = best_k0(0, 3'd5, 0);
                v_d0 = paper_diff(0, 3'd5, 0, v_k0);
                v_alt = paper_diff(0, 3'd5, 1, best_k0(0, 3'd5, 1));
                v_ph = -1;
                if (v_d0 == 0) v_ph = 0; else if (v_alt == 0) v_ph = 1;
                if (v_ph == 1) v_k0 = best_k0(0, 3'd5, 1);
                v_d1 = paper_diff(1, 3'd5, 1 - v_ph, v_k0);
                v_b = border_diff(0);
            end else begin
                snap_f0 = 20;
                boot(1'b1);
                while (frames_seen < 22) @(posedge sys_clk);
                v_k0 = best_k0(0, 3'd7, 0);
                v_d0 = paper_diff(0, 3'd7, 0, v_k0);
                v_alt = paper_diff(0, 3'd7, 1, best_k0(0, 3'd7, 1));
                v_ph = (v_d0 == 0 || v_alt == 0) ? 0 : -1;
                v_d1 = 0;
                v_b = 0;
            end
            $display("%0s: paper pixel 0 at buffer column %0d (want 44); paper mismatches %0d / %0d, +16 frames %0d, border lines wrong %0d; %0d frames reported, %0d complete, %0d bad pixels, buffers in sequence %0d, bad commands %0d, writes %0d",
                     mode, v_k0, v_d0, v_alt, v_d1, v_b, cv_n, cv_rows_full, cv_badpix, cv_bufs_round, fb_bad_cmd, fb_writes);
            if (v_k0 == 44 && v_ph >= 0 && v_d1 == 0 && v_b == 0 && cv_n >= 18 && cv_rows_full >= cv_n - 1 && cv_badpix == 0 &&
                cv_bufs_round >= cv_n - 2 && fb_bad_cmd == 0 && dut.fb_over == 16'd0 && hp_ok)
                $display("PASS %0s: the frame buffer holds the ULA picture - 344 x 280, paper centred at column 44 / row 44, Spectrum rules met, every word of every frame written, buffers in a ring", mode);
            else $display("FAIL %0s", mode);
        end
        "sw48", "sw128": begin
            // T119 5.10 (board 2026-10-24): the model changes at a reset in the middle of a run, as the app does it (CTRL b0 up, model, ROM, CTRL b0 down).
            // sw48: 128K runs first, then 48K; sw128: 48K first, then 128K. After the switch the ULA has to start lines again (d_ls), the writer
            // has to finish frames (fb_seq) and the frames have to count (VSync). `lines per frame` is the ULA's own: 312 / 311.
            if (!$value$plusargs("ROM2=%s", rom2file)) rom2file = "";
            boot(mode == "sw48");
            while (frames_seen < 14) @(posedge sys_clk);
            sw_l0 = dut.d_ls; sw_s0 = dut.fb_seq; sw_f0 = dut.frames;
            while (frames_seen < 24) @(posedge sys_clk);
            sw_l1 = dut.d_ls - sw_l0; sw_s1 = dut.fb_seq - sw_s0; sw_f1 = dut.frames - sw_f0;
            romfile = rom2file;
            boot(mode == "sw128");
            repeat (4000) @(posedge sys_clk);
            sw_l0 = dut.d_ls; sw_s0 = dut.fb_seq; sw_f0 = dut.frames; sw_m = dut.m128_l;
            frames_seen = 0;
            if (!$value$plusargs("SWF=%d", sw_n)) sw_n = 12;
            while (frames_seen < sw_n) @(posedge sys_clk);
            sw_l2 = dut.d_ls - sw_l0; sw_s2 = dut.fb_seq - sw_s0; sw_f2 = dut.frames - sw_f0;
            dbg_a = 12'd10; repeat (2) @(posedge sys_clk); sw_j10 = j_rd; dbg_a = 12'd11; repeat (2) @(posedge sys_clk); sw_j11 = j_rd;
            $display("%0s: JTAG word 10 = %08x (HBlank rises %0d, dots with HBlank high %0d), word 11 = %08x (pixel enables %0d, line starts %0d)", mode, sw_j10, sw_j10[31:16], sw_j10[15:0], sw_j11, sw_j11[31:16], sw_j11[15:0]);
            dbg_a = 12'd12; repeat (2) @(posedge sys_clk); sw_j12 = j_rd; dbg_a = 12'd13; repeat (2) @(posedge sys_clk); sw_j13 = j_rd;
            sw_len_ok = (sw_m ? (sw_j12[31:16] == 16'd456 && sw_j13[15:0] == 16'd311) : (sw_j12[31:16] == 16'd448 && sw_j13[15:0] == 16'd312)) && sw_j13[31:16] == 16'd32;
            $display("%0s: JTAG word 12 = %08x (line %0d dots, HSync rises %0d), word 13 = %08x (HSync %0d dots high, %0d lines per frame)", mode, sw_j12, sw_j12[31:16], sw_j12[15:0], sw_j13, sw_j13[31:16], sw_j13[15:0]);
            $display("%0s: before the switch (10 frames): %0d line starts, %0d frames written, %0d VSyncs; after it (model %0s): %0d line starts, %0d frames written, %0d VSyncs in 12 frames",
                     mode, sw_l1, sw_s1, sw_f1, sw_m ? "128K" : "48K", sw_l2, sw_s2, sw_f2);
            if (sw_l1 >= 3000 && sw_s1 >= 8 && sw_f1 >= 9 && sw_m == (mode == "sw128") && sw_l2 >= 3500 && sw_s2 >= 10 && sw_f2 >= 11 && ((sw_j10[31:16] - sw_j11[15:0] + 16'd1) & 16'hFFFF) <= 2 && sw_len_ok)
                $display("PASS %0s: the ULA starts lines and the writer finishes frames before and after the model switch at a reset", mode);
            else $display("FAIL %0s", mode);
        end
        "swapp48", "coldapp48": begin
            // T119 5.12 (board 2026-11-01, flat zx.cor: 48K black, 128K -> 48K freezes): the switch as the core app makes it, through the registers and the
            // window only (zx.cc zx_init / select_model / put_model_rom / zx_release): CTRL b0 up and the host's reset line, ZX_MODEL, the ROM bank through
            // WIN_PAGE + the ROM window, read back, WIN_PAGE 0, the reset line and CTRL b0 down. swapp48: from a running 128K; coldapp48: from power-up.
            if (!$value$plusargs("ROM2=%s", rom2file)) rom2file = "";
            if (mode == "swapp48") begin
                boot(1'b1);
                while (frames_seen < 14) @(posedge sys_clk);
            end else begin
                cw_wr(12'h0A0, 8'h01);
                repeat (2000) @(posedge sys_clk);
            end
            $readmemh(rom2file, img);
            reset_req = 1'b1;
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, 8'h00);
            errs = 0;
            for (i = 0; i < 16384; i = i + 1) win_wr(1'b1, i[15:0], img[i]);
            for (i = 0; i < 16384; i = i + 1) begin
                win_rd(1'b1, i[15:0]);
                if (cw_val !== img[i]) errs = errs + 1;
            end
            cw_wr(12'h0D4, 8'h00);
            cw_wr(12'h0A0, 8'h00);
            reset_req = 1'b0;
            repeat (4000) @(posedge sys_clk);
            sw_l0 = dut.d_ls; sw_s0 = dut.fb_seq; sw_f0 = dut.frames; sw_m = dut.m128_l;
            frames_seen = 0;
            while (frames_seen < 12) @(posedge sys_clk);
            sw_l2 = dut.d_ls - sw_l0; sw_s2 = dut.fb_seq - sw_s0; sw_f2 = dut.frames - sw_f0;
            dbg_a = 12'd12; repeat (2) @(posedge sys_clk); sw_j12 = j_rd; dbg_a = 12'd13; repeat (2) @(posedge sys_clk); sw_j13 = j_rd;
            sw_len_ok = (sw_j12[31:16] == 16'd448 && sw_j13[15:0] == 16'd312 && sw_j13[31:16] == 16'd32);
            $display("%0s: ROM read-back differences %0d; model %0s; after the switch: %0d line starts, %0d frames written, %0d VSyncs in 12 frames; JTAG word 12 = %08x (line %0d dots), word 13 = %08x (HSync %0d dots, %0d lines per frame)",
                     mode, errs, sw_m ? "128K" : "48K", sw_l2, sw_s2, sw_f2, sw_j12, sw_j12[31:16], sw_j13, sw_j13[31:16], sw_j13[15:0]);
            if (errs == 0 && !sw_m && sw_l2 >= 3500 && sw_s2 >= 10 && sw_f2 >= 11 && sw_len_ok)
                $display("PASS %0s: the app's own switch sequence leaves a 48K machine with lines and frames", mode);
            else $display("FAIL %0s", mode);
        end
        "shadow128": begin
            snap_f0 = 20;
            boot(1'b1);
            while (frames_seen < 22) @(posedge sys_clk);
            v_k0 = best_k0(0, 3'd7, 0);
            v_d0 = paper_diff(0, 3'd7, 0, v_k0);
            v_alt = paper_diff(0, 3'd7, 1, best_k0(0, 3'd7, 1));
            v_ctl1 = paper_diff(0, 3'd5, 0, best_k0(0, 3'd5, 0));       // control: bank 5's screen must NOT be what is shown
            $display("shadow128: the shown picture differs from bank 7's screen in %0d pixels (flash phase 0) / %0d (phase 1); from bank 5's in %0d", v_d0, v_alt, v_ctl1);
            if ((v_d0 == 0 || v_alt == 0) && v_ctl1 > 10000) $display("PASS shadow128: the shadow screen (7FFD bit 3, RAM bank 7) is displayed in 128K mode");
            else $display("FAIL shadow128");
        end
        "audio128": begin
            boot(1'b1);
            maxframes = 90;
            while (cpu_mem(16'h9FFF) != 8'hA5 && frames_seen < maxframes) @(posedge sys_clk);
            if (!$value$plusargs("BEEP_T=%d", meta_v)) meta_v = 0;
            beep_t = meta_v;
            if (!$value$plusargs("AY_TP=%d", meta_v)) meta_v = 0;
            ay_tp = meta_v;
            own_b = beep_t * 628992.0 / 70908.0;                       // the beeper period in sys_clk by this machine's own timebase
            nom_b = 31527777.78 * beep_t / 3546895.0;                  // and by the real machine's
            own_a = 16.0 * ay_tp * 4.0 * 628992.0 / 141816.0;          // AY tone: 16 x TP chip clocks, a chip clock = 4 pixel clocks
            nom_a = 31527777.78 * 16.0 * ay_tp / 1773447.5;
            m1 = mean_period(1); m2 = mean_period(2);
            e1 = (m1 / own_b - 1.0) * 100.0; e1n = (m1 / nom_b - 1.0) * 100.0;
            e2 = (m2 / own_a - 1.0) * 100.0; e2n = (m2 / nom_a - 1.0) * 100.0;
            cv1 = cv_period(1); cv2 = cv_period(2); cv3 = cv_period(3);
            ctl = ((m1 / (own_b * 1.01)) - 1.0) * 100.0;
            $display("audio128: beeper %0d periods, mean %f sys_clk (expected %f own, %f nominal): %f %% / %f %%; cv %f", xn[1], m1, own_b, nom_b, e1, e1n, cv1);
            $display("audio128: AY tone %0d periods, mean %f sys_clk (expected %f own, %f nominal): %f %% / %f %%; cv %f", xn[2], m2, own_a, nom_a, e2, e2n, cv2);
            $display("audio128: AY noise %0d crossings, interval cv %f; silence: %0d non-zero samples", xn[3], cv3, a_nz);
            $display("audio128 control: the beeper against an expectation 1%% too long is off by %f %%", ctl);
            if (cpu_mem(16'h9FFF) == 8'hA5 && xn[1] > 50 && xn[2] > 50 && e1 > -0.3 && e1 < 0.3 && e1n > -0.3 && e1n < 0.3 && e2n > -0.3 && e2n < 0.3 &&
                e2 > -0.3 && e2 < 0.3 && cv1 < 0.02 && cv2 < 0.02 && cv3 > 0.3 && xn[3] > 20 && a_nz == 0 && (ctl < -0.5 || ctl > 0.5))
                $display("PASS audio128: beeper and AY tone periods within 0.3 %% of the programmed value, noise has no tone peak, silence is exactly 0");
            else $display("FAIL audio128");
        end
        "keys48": begin
            if (!$value$plusargs("VEC=%s", vecfile)) vecfile = "";
            boot(1'b0);
            kerr = 0; kn = 0;
            while (cpu_mem(16'h9009) == 0) @(posedge sys_clk);
            kfd = $fopen(vecfile, "r");
            while ($fscanf(kfd, "%h %h", kv, mv) == 2) begin
                keys = kv; want_mat = mv; want_kemp = 8'h00;
                check_rows("matrix");
            end
            $fclose(kfd);
            keys = 64'd0;
            // joysticks: every direction/fire combination, each port, each mode (modes 1 Kempston 2 Sinclair 1 3 Sinclair 2 4 Cursor)
            for (jp = 1; jp <= 2; jp = jp + 1)
                for (jm = 1; jm <= 4; jm = jm + 1) begin
                    cw_wr(12'h0A2, (jp == 1) ? jm : 0);
                    cw_wr(12'h0A3, (jp == 2) ? jm : 0);
                    for (kvn = 0; kvn < 32; kvn = kvn + 1) begin
                        jv = kvn;
                        joy1 = (jp == 1) ? jv : 5'd0;
                        joy2 = (jp == 2) ? jv : 5'd0;
                        want_mat = joy_expect(jm, jv);
                        want_kemp = (jm == 1) ? kemp_expect(jv) : 8'h00;
                        check_rows("joystick");
                    end
                end
            // a joystick that is "none" does nothing; a port that is idle does not disturb the other
            cw_wr(12'h0A2, 8'd0); cw_wr(12'h0A3, 8'd0); joy1 = 5'h1F; joy2 = 5'h1F; want_mat = 40'd0; want_kemp = 8'h00; check_rows("joystick off");
            joy1 = 5'd0; joy2 = 5'd0;
            // the app's injection register: each of the 40 keys
            for (kvn = 0; kvn < 40; kvn = kvn + 1) begin
                cw_wr(12'h160 + kvn / 8, 8'd0);
                cw_wr(12'h160, 8'd0); cw_wr(12'h161, 8'd0); cw_wr(12'h162, 8'd0); cw_wr(12'h163, 8'd0); cw_wr(12'h164, 8'd0);
                cw_wr(12'h160 + kvn / 8, 8'd1 << (kvn % 8));
                want_mat = 40'd1 << kvn; want_kemp = 8'h00;
                check_rows("inject");
            end
            cw_wr(12'h160, 8'd0); cw_wr(12'h161, 8'd0); cw_wr(12'h162, 8'd0); cw_wr(12'h163, 8'd0); cw_wr(12'h164, 8'd0);
            if (kerr == 0) $display("PASS keys48: %0d key sets (matrix, joysticks, injection) read back exactly as the table lists them, nothing else", kn);
            else $display("FAIL keys48: %0d differences in %0d key sets", kerr, kn);
        end
        "tape48": begin
            if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
            `ifndef ZX_HOSTPORT
            $readmemh(ddrfile, ddr.mem);
`endif
            boot(1'b0);
            // the app: stream at the default base (0x01800000), position 0, play
            cw_wr(12'h148, 8'd0); cw_wr(12'h149, 8'd0); cw_wr(12'h14A, 8'd0); cw_wr(12'h14B, 8'd0); cw_wr(12'h14C, 8'd1);
            cw_wr(12'h140, 8'd1);
            t_ok = 0;
            // the tape stops at its marker after block A: wait for it (status bit 2), then check it stands still, then start it again
            cw_rd(12'h150);
            while (!cw_val[2] && frames_seen < 70) begin repeat (20000) @(posedge sys_clk); cw_rd(12'h150); end
            cw_rd(12'h150); t_stat = cw_val;
            cw_rd(12'h148); t_pos0 = cw_val; cw_rd(12'h149); t_pos0 = t_pos0 + cw_val * 256;
            repeat (3 * 628992) @(posedge sys_clk);
            cw_rd(12'h148); t_pos1 = cw_val; cw_rd(12'h149); t_pos1 = t_pos1 + cw_val * 256;
            cw_rd(12'h150);
            $display("tape48: stop marker reached at frame %0d, status %02x, position %0d then %0d three frames later, status now %02x, CPU status A=%02x", frames_seen, t_stat, t_pos0, t_pos1, cw_val, cpu_mem(16'h9000));
            if (t_stat[2] && !t_stat[0] && t_pos0 == t_pos1 && cpu_mem(16'h9000) == 8'h01) t_ok = 1;
            cw_wr(12'h140, 8'd0);
            cw_wr(12'h140, 8'd1);
            wait_done;
            cw_rd(12'h150);
            while (!cw_val[1] && frames_seen < maxframes) begin repeat (20000) @(posedge sys_clk); cw_rd(12'h150); end
            t_stat = cw_val;
            cw_rd(12'h151);
            $display("tape48: end at frame %0d, tape status %02x (bit 1 = end of tape, bit 3 = DDR2 stuck), underruns %0d", frames_seen, t_stat, cw_val);
            compare("tape48", 1, 0);
            // the loaded bytes through the app's window equal the tape's data
            errs = 0;
            for (i = 0; i < 18; i = i + 1) begin
                win_rd(1'b0, 16'h9010 + i);
                if (cw_val !== expm[16 + i]) errs = errs + 1;
                win_rd(1'b0, 16'h9040 + i);
                if (cw_val !== expm[64 + i]) errs = errs + 1;
            end
            if (errs == 0 && t_ok && t_stat[1] && !t_stat[3]) $display("PASS tape48 window: block A loads at tape speed and passes its XOR, block B (one bit flipped) is refused, the tape stops at the marker and at its end");
            else $display("FAIL tape48 window: %0d window differences, marker check %0d, end status %02x", errs, t_ok, t_stat);
        end
        "snap48": begin
            boot(1'b0);
            snap_load;
            wait_done;
            compare(mode, 1, 0);
        end
        "snapint": begin
            maxframes = 20;
            boot(1'b0);
            snap_load;
            while (frames_seen < 8) @(posedge sys_clk);
            if (cpu_mem(16'h9100) >= 8'd5) $display("PASS snapint: IM 2, I = 0x80 and IFF were loaded: the interrupt service routine ran %0d times in 8 frames", cpu_mem(16'h9100));
            else $display("FAIL snapint: the interrupt service routine ran %0d times in 8 frames", cpu_mem(16'h9100));
        end
        "snap128": begin
            boot(1'b1);
            snap_load;
            wait_done;
            sk_err = 0;
            if (cpu_mem(16'h9FFF) != 8'hA5) begin sk_err = 99; $display("  snap128: the program did not finish"); end
            if (dut.page_reg !== 8'h13) begin sk_err = sk_err + 1; $display("  snap128: 7FFD = %02x, want 13", dut.page_reg); end
            if (dut.border_color !== 3'd5) begin sk_err = sk_err + 1; $display("  snap128: border %0d, want 5", dut.border_color); end
            if (cpu_mem(16'h9020) !== 8'h33) begin sk_err = sk_err + 1; $display("  snap128: bank 3 byte %02x, want 33", cpu_mem(16'h9020)); end
            if (cpu_mem(16'h9021) !== 8'hB1) begin sk_err = sk_err + 1; $display("  snap128: ROM byte %02x, want B1 (ROM 1)", cpu_mem(16'h9021)); end
            for (i = 0; i < 14; i = i + 1)
                if (cpu_mem(16'h9030 + i) !== (ayw[i] & ay_mask[i])) begin sk_err = sk_err + 1; $display("  snap128: AY register %0d read %02x, want %02x", i, cpu_mem(16'h9030 + i), ayw[i] & ay_mask[i]); end
            if (sk_err == 0) $display("PASS snap128: 7FFD (bank 3, ROM 1), border, 14 AY registers and the CPU state were loaded");
            else $display("FAIL snap128: %0d differences", sk_err);
        end
        "pause_ref": begin
            if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
            `ifndef ZX_HOSTPORT
            $readmemh(ddrfile, ddr.mem);
`endif
            boot(1'b0);
            cw_wr(12'h148, 8'd0); cw_wr(12'h149, 8'd0); cw_wr(12'h14A, 8'd0); cw_wr(12'h14B, 8'd0); cw_wr(12'h14C, 8'd1);
            cw_wr(12'h140, 8'd1);
            while (frames_seen < 24) @(posedge sys_clk);
            $display("done pause_ref");
        end
        "pause_run": begin
            if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
            `ifndef ZX_HOSTPORT
            $readmemh(ddrfile, ddr.mem);
`endif
            boot(1'b0);
            cw_wr(12'h148, 8'd0); cw_wr(12'h149, 8'd0); cw_wr(12'h14A, 8'd0); cw_wr(12'h14B, 8'd0); cw_wr(12'h14C, 8'd1);
            cw_wr(12'h140, 8'd1);
            // pause 1: asked in the middle of frame 4, held for three frames and a bit
            while (frames_seen < 4) @(posedge sys_clk);
            repeat (200000) @(posedge sys_clk);
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            repeat (3 * 628992 + 123456) @(posedge sys_clk);
            halt_req = 1'b0;
            while (halted && frames_seen < 80) @(posedge sys_clk);
            // a request that ends before the frame boundary changes nothing
            while (frames_seen < 8) @(posedge sys_clk);
            repeat (50000) @(posedge sys_clk);
            halt_req = 1'b1;
            repeat (30000) @(posedge sys_clk);
            halt_req = 1'b0;
            sk_err = h_holds;
            // pause 3: seven frames
            while (frames_seen < 12) @(posedge sys_clk);
            repeat (400000) @(posedge sys_clk);
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            repeat (7 * 628992) @(posedge sys_clk);
            halt_req = 1'b0;
            while (halted && frames_seen < 80) @(posedge sys_clk);
            while (frames_seen < 24) @(posedge sys_clk);
            $display("pause_run: %0d holds; while held: pixel clocks %0d, CPU clocks %0d, AY clocks %0d, tape ticks %0d, picture writes %0d, non-zero audio samples %0d, tape position changes %0d; lock event %0d times, off its frame phase %0d times",
                     h_holds, h_ce, h_cpu, h_ay, h_tape, h_pix, h_aud, h_tbad, mk_cnt2, mk_bad);
            // a reset takes the pause away (the menu's Reset works while frozen)
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            reset_req = 1'b1;
            repeat (8) @(posedge sys_clk);
            reset_req = 1'b0;
            repeat (4) @(posedge sys_clk);
            i = halted;
            halt_req = 1'b0;
            if (h_holds == 3 && sk_err == 1 && h_ce == 0 && h_cpu == 0 && h_ay == 0 && h_tape == 0 && h_pix == 0 && h_aud == 0 && h_tbad == 0 && mk_bad == 0 && i == 0)
                $display("PASS pause_run: held at the frame boundary, nothing moved while held, the lock event stayed on its phase, a reset took the hold away");
            else $display("FAIL pause_run: holds %0d (want 3) / the short request held %0d times (want 1 in total), counters above, halted after reset %0d", h_holds, sk_err, i);
        end
        default: $display("FAIL unknown mode %0s", mode);
        endcase
        $finish;
    end
endmodule
