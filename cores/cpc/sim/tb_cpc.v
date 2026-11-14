// T92: the bench of the CPC core (cpc_core with cores/cpc/rtl, gen, trx and the Spectrum core's shared parts; Verilator). One binary, modes by
// +MODE=<name> (cores/cpc/sim/run.sh builds it and runs the modes; every mode ends with one PASS or FAIL line). The programs the Z80 runs are
// ours (tools/cpc_progs.py, tools/zx_progs.py); the benches never see a CPC ROM.
`timescale 1ns / 1ps
module tb_cpc;
    reg sys_clk = 1'b0, cpu_clk = 1'b0, mclk = 1'b0, ui_clk = 1'b0;
    always #15.86 sys_clk = ~sys_clk;                 // 31.5278 MHz
    always #5 cpu_clk = ~cpu_clk;                     // 100 MHz
    always #40.69 mclk = ~mclk;                       // 12.288 MHz
    always #6.667 ui_clk = ~ui_clk;                   // 75 MHz
    reg sys_rst = 1'b1, ui_rst = 1'b1;
    reg reset_req = 1'b0, halt_req = 1'b0;
    reg [63:0] keys = 64'd0;
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
    wire [26:0] c_addr, e_addr;
    wire [2:0] c_cmd, e_cmd;
    wire c_en, c_rdy, c_rd_valid, e_en, e_rdy, e_wdf_wren, c_wdf_wren;
    wire [127:0] c_rd_data, c_wdf_data, e_wdf_data;
    wire [15:0] c_wdf_mask, e_wdf_mask;
    wire win_port_use, win_port_rom, win_port_we;
    wire [15:0] win_port_addr;
    wire [7:0] win_port_wdata;

    cpc_core #(.ID_TRX2(0)) dut (
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
        .build(32'h12345678), .ddr_cal(64'd0), .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid)
    );
    ddr2c_model ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
                     .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));
    ddr2e_model fbm (.ui_clk(ui_clk), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_wren(e_wdf_wren));

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
    // the Z80's view of the base RAM (the program's results are in $9000-$9FFF, outside the ROMs)
    function [7:0] cpu_mem(input [15:0] a); cpu_mem = a[0] ? dut.mem.ram_o[a[15:1]] : dut.mem.ram_e[a[15:1]]; endfunction
    function [7:0] ext_mem(input [15:0] a); ext_mem = dut.mem.ram_x[a]; endfunction

    integer frames_seen = 0;
    integer wd_clk = 0, wd_max = 300000000;
    integer fclk = 0;                                  // frames by the clock (628,992 sys_clk each) for the programs that do not program the CRTC
    always @(posedge sys_clk) fclk <= wd_clk / 628992;
    // a watchdog in sys_clk (frames only count when the program has programmed the CRTC); debug aids: +PCTRACE (the address bus every 2M clocks), +VSTRACE, +IOTRACE
    always @(posedge sys_clk) begin
        wd_clk <= wd_clk + 1;
        if (wd_clk > wd_max) begin $display("FAIL %0s: watchdog, the bench did not end within %0d sys_clk (pc %04x, %0d frames)", mode, wd_max, dut.cpu_addr, frames_seen); $finish; end
        if ($test$plusargs("PCTRACE") && (wd_clk % 2000000) == 0) $display("  t=%0d pc=%04x 9FFF=%02x", wd_clk, dut.cpu_addr, cpu_mem(16'h9FFF));
    end
    always @(posedge sys_clk) if (dut.vsync && !dut.vs_d) frames_seen <= frames_seen + 1;
    always @(posedge sys_clk) if ($test$plusargs("VSTRACE") && dut.vsync != dut.vs_d) $display("  vsync %0d at sys_clk %0d", dut.vsync, wd_clk);
    reg iow_d = 1'b0;
    always @(posedge sys_clk) if ($test$plusargs("IOTRACE")) begin
        iow_d <= dut.io_wr;
        if (dut.io_wr && !iow_d) $display("  IOW %04x %02x at %0d", dut.cpu_addr, dut.cpu_dout, wd_clk);
    end

    reg [8*200-1:0] mode, romfile, expfile;
    integer maxframes, i, errs = 0, xdev;
    reg [7:0] expm [0:255];

    task boot(input [1:0] model);
        begin
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, {6'd0, model});
            $readmemh(romfile, dut.mem.rom_os);
            cw_wr(12'h0A0, 8'h00);
        end
    endtask
    integer cmds = 0, rd_cmds = 0;
    reg rc_wd = 1'b0;
    task rc_mount(input integer dsz);
        begin
            cw_wr(12'h180, 8'h00); cw_wr(12'h181, 8'h00); cw_wr(12'h182, 8'hA0); cw_wr(12'h183, 8'h01);
            cw_wr(12'h184, dsz[7:0]); cw_wr(12'h185, dsz[15:8]); cw_wr(12'h186, dsz[23:16]); cw_wr(12'h187, 8'h00);
            cw_wr(12'h190, 8'h01);
        end
    endtask
    task wait_done;
        begin
            while (cpu_mem(16'h9FFF) != 8'hA5 && frames_seen < maxframes) @(posedge sys_clk);
        end
    endtask
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
    integer clk_cnt = 0, c16_cnt = 0, per_n = 0, last_per = 0, last_c16 = 0, mk_t = 0, mk_n = 0, mk_ok = 0;
    reg [9:0] pxd = 10'd0;
    always @(posedge sys_clk) begin
        clk_cnt <= clk_cnt + 1;
        mk_t <= mk_t + 1;
        if (dut.ce_16) c16_cnt <= c16_cnt + 1;
        if (dut.vsync && !dut.vs_d) begin
            if (per_n >= 2) begin last_per <= clk_cnt + 1; last_c16 <= c16_cnt + (dut.ce_16 ? 1 : 0); end
            per_n <= per_n + 1; clk_cnt <= 0; c16_cnt <= 0;
        end
        if (pix_x == 10'd0 && pix_y == 9'd20 && pxd != 10'd0) begin
            mk_n <= mk_n + 1;
            if (mk_n >= 2 && mk_t + 1 == 628992) mk_ok <= mk_ok + 1;
            mk_t <= 0;
        end
        pxd <= pix_x;
    end

    // ---------------------------------------------------------------- S8: sound (audio)
    integer a_t = 0, a_ph = 0, a_phs = 0, a_nz = 0;
    integer xn [0:4];
    integer xt [0:4*400-1];
    reg signed [23:0] a_prev = 24'sd0;
    integer ai;
    initial for (ai = 0; ai < 5; ai = ai + 1) xn[ai] = 0;
    always @(posedge sys_clk) begin
        a_t <= a_t + 1;
        if (mode == "audio") begin
            if (cpu_mem(16'h9000) != a_ph[7:0]) begin a_ph <= cpu_mem(16'h9000); a_phs <= a_t; end
            if (a_prev < 0 && audio_l >= 0 && a_ph >= 1 && a_ph <= 2 && a_t - a_phs > 2 * 628992 && xn[a_ph] < 400) begin
                xt[a_ph * 400 + xn[a_ph]] <= a_t;
                xn[a_ph] <= xn[a_ph] + 1;
            end
            if (a_ph == 3 && a_t - a_phs > 8 * 628992 && audio_l != 0) a_nz <= a_nz + 1;
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
    real ay_tp, own_a, nom_a, m1, e1, e1n, cv1, cv2, ctl;
    integer meta_v;

    // ---------------------------------------------------------------- S9: keys and joysticks (keys)
    reg [79:0] want_mat;
    integer kerr, kn, kfd, sc0, kvn, jp, jm, jsw;
    reg [63:0] kv;
    reg [79:0] mv;
    reg [4:0] jv;
    reg [8*200-1:0] vecfile;
    task wait_scans(input integer n);
        begin
            sc0 = cpu_mem(16'h9010);
            while (((cpu_mem(16'h9010) - sc0) & 255) < n) @(posedge sys_clk);
        end
    endtask
    task check_lines(input [8*24-1:0] what);
        integer r;
        reg [7:0] exp_line;
        begin
            wait_scans(3);
            for (r = 0; r < 10; r = r + 1) begin
                exp_line = ~want_mat[8 * r +: 8];
                if (cpu_mem(16'h9000 + r) !== exp_line) begin
                    if (kerr < 6) $display("  keys: %0s line %0d read %02x expected %02x", what, r, cpu_mem(16'h9000 + r), exp_line);
                    kerr = kerr + 1;
                end
            end
            kn = kn + 1;
        end
    endtask

    // ---------------------------------------------------------------- S10/S11: tape and disk
    reg [8*200-1:0] ddrfile;
    integer t_pos0, t_pos1, t_pos2, t_stat, t_ok, t_play1, t_play2, t_play3;
    integer d_err, d_chunk;

    // ---------------------------------------------------------------- S13: Freeze
    // per CRTC frame (counted at the Gate Array's VSYNC, so a pause does not make frames): a hash of the video stream (the colour table's output
    // at every Gate Array pixel with the syncs), the screen RAM, the Z80's registers and the program's frame counter. The reference run has no
    // pause; the other one has three, and every line it prints must be a line of the reference.
    // T159 D-all: the case LED 1 source (drv_led, the disk motor) in the disk mode: low until the program has switched the motor on, high after
    reg led_first_low = 1'b0, led_seen = 1'b0;
    always @(posedge sys_clk) if (mode == "disk") begin
        if (!led_seen && dut.drv_led === 1'b0) led_first_low <= 1'b1;
        if (dut.drv_led === 1'b1) led_seen <= 1'b1;
    end
    reg [63:0] fh = 64'hcbf29ce484222325;
    integer pf_n = 0, fa, fk;
    always @(posedge sys_clk) if (mode == "pause_ref" || mode == "pause_run") begin
        if (dut.ce16_d) fh = (fh ^ {32'd0, dut.cm_r[7:3], dut.cm_g[7:2], dut.cm_b[7:3], dut.cm_hs, dut.cm_vs}) * 64'h100000001b3;
        if (dut.vsync && !dut.vs_d) begin
            for (fa = 0; fa < 16384; fa = fa + 1) fh = (fh ^ cpu_mem(16'hC000 + fa)) * 64'h100000001b3;
            for (fk = 0; fk < 27; fk = fk + 1) fh = (fh ^ dut.board.cpu_reg[8 * fk +: 8]) * 64'h100000001b3;
            fh = (fh ^ cpu_mem(16'h8000)) * 64'h100000001b3;
            pf_n <= pf_n + 1;
            $display("F %0d %016h", pf_n + 1, fh);
            fh = 64'hcbf29ce484222325;
        end
    end
    // while the machine is held: nothing may move
    integer h_ce = 0, h_cpu = 0, h_ay = 0, h_fdc = 0, h_tape = 0, h_fb = 0, h_aud = 0, h_age = 0, h_holds = 0, h_holds_3 = 0, h_fbw = 0;
    reg halted_d = 1'b0;
    always @(posedge sys_clk) begin
        halted_d <= halted;
        if (halted) begin
            h_age <= h_age + 1;
            if (dut.ce_16) h_ce <= h_ce + 1;
            if (dut.phi_en_p || dut.phi_en_n) h_cpu <= h_cpu + 1;
            if (dut.cclk_en_p) h_ay <= h_ay + 1;
            if (dut.ce_fdc) h_fdc <= h_fdc + 1;
            if (dut.tape_tick) h_tape <= h_tape + 1;
            if (h_age > 3000 && audio_l != 0) h_aud <= h_aud + 1;
            if (!halted_d) begin h_holds <= h_holds + 1; h_fbw <= fbm.writes; end
            if (halted_d && fbm.writes != h_fbw) h_fb <= h_fb + 1;
        end else h_age <= 0;
    end

    // ---------------------------------------------------------------- S7: the picture in the frame buffer (video)
    `include "hw_rgb.vh"
    reg [15:0] snapa [0:540*720-1];
    reg [15:0] snapb [0:540*720-1];
    reg [15:0] snapc [0:540*720-1];
    reg [4:0] pen_hw [0:15];
    initial begin
        pen_hw[0] = 5'h14; pen_hw[1] = 5'h04; pen_hw[2] = 5'h15; pen_hw[3] = 5'h1C; pen_hw[4] = 5'h18; pen_hw[5] = 5'h1D; pen_hw[6] = 5'h0C; pen_hw[7] = 5'h05;
        pen_hw[8] = 5'h0D; pen_hw[9] = 5'h16; pen_hw[10] = 5'h06; pen_hw[11] = 5'h17; pen_hw[12] = 5'h1E; pen_hw[13] = 5'h00; pen_hw[14] = 5'h1F; pen_hw[15] = 5'h0E;
    end
    // the frame buffer b as the pixel layer reads it: row r, column c (16 bit units, buffer b at FB_BASE + b * FB_STRIDE)
    function [15:0] fbpix(input integer b, input integer r, input integer c);
        reg [127:0] w;
        integer idx;
        begin
            idx = b * 65536 + (r * 720 + c) / 8;                       // stride 1 MB = 65,536 words of 16 bytes
            w = fbm.fb[idx];
            fbpix = w[16 * (c % 8) +: 16];
        end
    endfunction
    task snap_to(input integer which);
        integer b, r, c;
        begin
            b = dut.fb_latest[1:0];
            for (r = 0; r < 540; r = r + 1)
                for (c = 0; c < 720; c = c + 1)
                    if (which == 0) snapa[r * 720 + c] = fbpix(b, r, c);
                    else if (which == 1) snapb[r * 720 + c] = fbpix(b, r, c);
                    else snapc[r * 720 + c] = fbpix(b, r, c);
        end
    endtask
    // the pen of paper pixel (x = Gate Array clock 0-639, y = line 0-199) of the screen in the base RAM at $C000, by the CPC's rules
    function [3:0] pen_at(input integer m, input integer x, input integer y);
        reg [7:0] bt;
        integer ch, adr, p;
        begin
            ch = x / 16;
            adr = 16'hC000 + (y / 8) * 80 + (y % 8) * 16'h0800 + 2 * ch + ((x % 16) >= 8 ? 1 : 0);
            bt = cpu_mem(adr);
            p = (x % 8);
            case (m)
                2: pen_at = {3'd0, bt[7 - p]};
                1: pen_at = {2'd0, bt[3 - (p / 2)], bt[7 - (p / 2)]};
                default: begin                                          // mode 0: 2 pixels a byte, 4 clocks each
                    p = (x % 8) / 4;
                    pen_at = (p == 0) ? {bt[1], bt[5], bt[3], bt[7]} : {bt[0], bt[4], bt[2], bt[6]};
                end
            endcase
        end
    endfunction
    function integer vid_diff(input integer sel, input integer m, input integer xp, input integer yp, input integer rows);
        integer x, y, n;
        reg [15:0] got;
        begin
            n = 0;
            for (y = 0; y < rows; y = y + 1)
                for (x = 0; x < 640; x = x + 1) begin
                    got = (sel == 0) ? snapa[(2 * (yp + y)) * 720 + xp + x] : (sel == 1) ? snapb[(2 * (yp + y)) * 720 + xp + x] : snapc[(2 * (yp + y)) * 720 + xp + x];
                    if (got !== hw_rgb565(pen_hw[pen_at(m, x, y)])) n = n + 1;
                    got = (sel == 0) ? snapa[(2 * (yp + y) + 1) * 720 + xp + x] : (sel == 1) ? snapb[(2 * (yp + y) + 1) * 720 + xp + x] : snapc[(2 * (yp + y) + 1) * 720 + xp + x];
                    if (got !== hw_rgb565(pen_hw[pen_at(m, x, y)])) n = n + 1;
                end
            vid_diff = n;
        end
    endfunction
    integer vx, vy, vbest, vbx, vby, vd, vtot, vm, vsel;
    reg [4:0] bar_hw [0:15];
    initial begin
        bar_hw[0] = 5'h14; bar_hw[1] = 5'h04; bar_hw[2] = 5'h15; bar_hw[3] = 5'h1C; bar_hw[4] = 5'h18; bar_hw[5] = 5'h1D; bar_hw[6] = 5'h0C; bar_hw[7] = 5'h05;
        bar_hw[8] = 5'h0D; bar_hw[9] = 5'h16; bar_hw[10] = 5'h06; bar_hw[11] = 5'h17; bar_hw[12] = 5'h1E; bar_hw[13] = 5'h00; bar_hw[14] = 5'h1F; bar_hw[15] = 5'h0E;
    end
    // the number of lines (of 270) whose pixel at column c does not follow the 16-ink cycle with phase p0; rows 2k and 2k+1 must agree
    function integer bars_diff(input integer c, input integer p0);
        integer k, n;
        begin
            n = 0;
            for (k = 0; k < 270; k = k + 1) begin
                if (snapa[(2 * k) * 720 + c] !== hw_rgb565(bar_hw[(k + p0) % 16])) n = n + 1;
                if (snapa[(2 * k + 1) * 720 + c] !== hw_rgb565(bar_hw[(k + p0) % 16])) n = n + 1;
            end
            bars_diff = n;
        end
    endfunction
    integer b_l, b_r, b_pl, b_pr, b_alt, p0i;

    // ---------------------------------------------------------------- S6: the memory slots (slots): every opcode fetch with the Gate Array clock
    integer c16_total = 0, since_cclk = 0;
    reg m1_d = 1'b0;
    always @(posedge sys_clk) begin
        if (dut.ce_16) c16_total <= c16_total + 1;
        m1_d <= dut.cpu_m1;
        if (dut.cclk_en_p) since_cclk <= 0; else if (dut.ce_16) since_cclk <= since_cclk + 1;
        if (mode == "slots" && dut.cpu_m1 && !m1_d) $display("M1 %04x %0d %0d", dut.cpu_addr, c16_total, since_cclk);
    end

    initial begin
        if (!$value$plusargs("MODE=%s", mode)) mode = "z80s";
        if (!$value$plusargs("FRAMES=%d", maxframes)) maxframes = 400;
        if (!$value$plusargs("MAXCLK=%d", wd_max)) wd_max = 300000000;
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "";
        if (!$value$plusargs("EXP=%s", expfile)) expfile = "";
        repeat (8) @(posedge sys_clk);
        sys_rst = 1'b0; ui_rst = 1'b0;
        case (mode)
        "z80s": begin
            boot(2'd2);
            wait_done;
            compare(mode, 18, 21);                                  // SCF, CCF: T80 deviates in their X/Y flags (its README)
        end
        "clk": begin
            boot(2'd2);
            while (frames_seen < 8) @(posedge sys_clk);
            begin : rep
                real hz, err;
                hz = 31527777.78 * 16.0 * last_c16 / 628992.0 / (last_c16 / 319488.0) / 4.0;
                err = 0.0;
                $display("clk: CPC frame %0d sys_clk, %0d Gate Array clocks (want 319,488), lock events %0d (periods ok %0d)", last_per, last_c16, mk_n, mk_ok);
                if (last_per == 628992 && last_c16 == 319488 && mk_ok >= 4 && mk_ok >= mk_n - 3)
                    $display("PASS clk: the CRTC frame = 628992 sys_clk, 319,488 Gate Array clocks (312 lines x 1,024), lock event every frame");
                else $display("FAIL clk: frame %0d sys_clk (want 628992), Gate Array clocks %0d, lock events %0d/%0d", last_per, last_c16, mk_ok, mk_n);
            end
        end
        "page", "page664", "page464": begin
            begin : pg
                reg [1:0] mdl;
                integer b;
                mdl = (mode == "page") ? 2'd2 : (mode == "page664") ? 2'd1 : 2'd0;
                cw_wr(12'h0A0, 8'h01);
                cw_wr(12'h0A1, {6'd0, mdl});
                $readmemh(romfile, dut.mem.rom_os);
                win_wr(1'b1, 16'h4000, 8'hB1);                   // a marker at the start of upper ROM 0 (BASIC) and of upper ROM 7 (AMSDOS)
                win_wr(1'b1, 16'h8000, 8'hB7);
                cw_wr(12'h0A0, 8'h00);
                wait_done;
                compare(mode, 1, 0);
                // the app's window sees the banks the program wrote: base RAM banks 0-3 in page 0, the extension (banks 4-7) in page 1
                errs = 0;
                for (b = 0; b < 4; b = b + 1) begin
                    cw_wr(12'h0D4, 8'h00);
                    win_rd(1'b0, b * 16384 + 16'h2000);
                    if (b != 1 || mdl == 2'd2) begin
                        if (cw_val !== 8'h30 + b) begin errs = errs + 1; $display("  window: bank %0d marker %02x", b, cw_val); end
                    end else if (cw_val !== 8'h37) begin errs = errs + 1; $display("  window: bank 1 marker %02x (64 KB model: the last write stays, 37)", cw_val); end
                    if (mdl == 2'd2) begin
                        cw_wr(12'h0D4, 8'h01);
                        win_rd(1'b0, b * 16384 + 16'h2000);
                        if (b == 0 || b == 1 || b == 2 || b == 3)
                            if (cw_val !== 8'h34 + b) begin errs = errs + 1; $display("  window: bank %0d marker %02x", 4 + b, cw_val); end
                    end
                end
                win_rd(1'b1, 16'h4000); if (cw_val !== 8'hB1) begin errs = errs + 1; $display("  window: BASIC marker %02x", cw_val); end
                win_rd(1'b1, 16'h8000); if (cw_val !== 8'hB7) begin errs = errs + 1; $display("  window: AMSDOS marker %02x", cw_val); end
                if (errs == 0) $display("PASS %0s window: RAM banks and ROM markers read through the app's window equal the Z80's", mode);
                else $display("FAIL %0s window: %0d differences", mode, errs);
            end
        end
        "audio": begin
            boot(2'd2);
            maxframes = 90;
            while (cpu_mem(16'h9FFF) != 8'hA5 && frames_seen < maxframes) @(posedge sys_clk);
            if (!$value$plusargs("AY_TP=%d", meta_v)) meta_v = 0;
            ay_tp = meta_v;
            own_a = 16.0 * ay_tp * 31.5;                               // a chip clock (1 MHz) is 16 Gate Array clocks = 31.5 sys_clk
            nom_a = 31527777.78 * 16.0 * ay_tp / 1000000.0;
            m1 = mean_period(1);
            e1 = (m1 / own_a - 1.0) * 100.0; e1n = (m1 / nom_a - 1.0) * 100.0;
            cv1 = cv_period(1); cv2 = cv_period(2);
            ctl = ((m1 / (own_a * 1.01)) - 1.0) * 100.0;
            $display("audio: AY tone %0d periods, mean %f sys_clk (expected %f own, %f nominal): %f %% / %f %%; cv %f", xn[1], m1, own_a, nom_a, e1, e1n, cv1);
            $display("audio: AY noise %0d crossings, interval cv %f; silence: %0d non-zero samples", xn[2], cv2, a_nz);
            $display("audio control: the tone against an expectation 1%% too long is off by %f %%", ctl);
            if (cpu_mem(16'h9FFF) == 8'hA5 && xn[1] > 50 && e1 > -0.3 && e1 < 0.3 && e1n > -0.3 && e1n < 0.3 && cv1 < 0.02 && cv2 > 0.3 && xn[2] > 20 && a_nz == 0 &&
                (ctl < -0.5 || ctl > 0.5))
                $display("PASS audio: the AY tone period is within 0.3 %% of the programmed value, noise has no tone peak, silence is exactly 0");
            else $display("FAIL audio");
        end
        "keys": begin
            if (!$value$plusargs("VEC=%s", vecfile)) vecfile = "";
            boot(2'd2);
            kerr = 0; kn = 0;
            while (cpu_mem(16'h9010) == 0) @(posedge sys_clk);
            kfd = $fopen(vecfile, "r");
            while ($fscanf(kfd, "%h %h", kv, mv) == 2) begin
                keys = kv; want_mat = mv;
                check_lines("matrix");
            end
            $fclose(kfd);
            keys = 64'd0;
            // joysticks: port 2 is joystick 0 (line 9, bits 0-4), port 1 joystick 1 (line 6, bits 0-4); the register swaps them
            for (jsw = 0; jsw <= 1; jsw = jsw + 1) begin
                cw_wr(12'h0A2, jsw);
                for (jp = 1; jp <= 2; jp = jp + 1)
                    for (kvn = 0; kvn < 32; kvn = kvn + 1) begin
                        jv = kvn;
                        joy1 = (jp == 1) ? jv : 5'd0;
                        joy2 = (jp == 2) ? jv : 5'd0;
                        want_mat = 80'd0;
                        if ((jp == 2) ^ jsw) want_mat[72 +: 5] = jv; else want_mat[48 +: 5] = jv;
                        check_lines("joystick");
                    end
            end
            joy1 = 5'd0; joy2 = 5'd0; cw_wr(12'h0A2, 8'h00);
            // the app's injection register: each of the 80 keys
            for (kvn = 0; kvn < 80; kvn = kvn + 1) begin
                for (i = 0; i < 10; i = i + 1) cw_wr(12'h160 + i, 8'd0);
                cw_wr(12'h160 + kvn / 8, 8'd1 << (kvn % 8));
                want_mat = 80'd1 << kvn;
                check_lines("inject");
            end
            for (i = 0; i < 10; i = i + 1) cw_wr(12'h160 + i, 8'd0);
            if (kerr == 0) $display("PASS keys: %0d key sets (matrix, joysticks, injection) read back exactly as the table lists them, nothing else", kn);
            else $display("FAIL keys: %0d differences in %0d key sets", kerr, kn);
        end
        "tape": begin
            if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
            $readmemh(ddrfile, ddr.mem);
            boot(2'd2);
            cw_wr(12'h148, 8'd0); cw_wr(12'h149, 8'd0); cw_wr(12'h14A, 8'd0); cw_wr(12'h14B, 8'd0); cw_wr(12'h14C, 8'd1);
            cw_wr(12'h140, 8'd1);                                   // play: the motor bit of the program starts and stops it
            t_ok = 1;
            // phase 1: motor on, the tape runs; phase 2: motor off, it stands; phase 3: motor on again
            while (cpu_mem(16'h9100) != 8'd1 && fclk < 40) @(posedge sys_clk);
            repeat (628992) @(posedge sys_clk);
            cw_rd(12'h150); t_play1 = cw_val[0];
            cw_rd(12'h148); t_pos0 = cw_val; cw_rd(12'h149); t_pos0 = t_pos0 + 256 * cw_val;
            while (cpu_mem(16'h9100) != 8'd2 && fclk < 40) @(posedge sys_clk);
            repeat (628992 / 2) @(posedge sys_clk);
            cw_rd(12'h150); t_play2 = cw_val[0];
            cw_rd(12'h148); t_pos1 = cw_val; cw_rd(12'h149); t_pos1 = t_pos1 + 256 * cw_val;
            repeat (628992) @(posedge sys_clk);
            cw_rd(12'h148); t_pos2 = cw_val; cw_rd(12'h149); t_pos2 = t_pos2 + 256 * cw_val;
            while (cpu_mem(16'h9100) != 8'd3 && fclk < 40) @(posedge sys_clk);
            repeat (628992 / 4) @(posedge sys_clk);
            cw_rd(12'h150); t_play3 = cw_val[0];
            $display("tape: motor on: playing %0d, position %0d; motor off: playing %0d, position %0d then %0d a frame later; motor on again: playing %0d",
                     t_play1, t_pos0, t_play2, t_pos1, t_pos2, t_play3);
            if (!(t_play1 == 1 && t_pos0 > 0 && t_play2 == 0 && t_pos1 == t_pos2 && t_play3 == 1)) t_ok = 0;
            // the stop marker after block A
            cw_rd(12'h150);
            while (!cw_val[2] && fclk < 80) begin repeat (20000) @(posedge sys_clk); cw_rd(12'h150); end
            t_stat = cw_val;
            cw_rd(12'h148); t_pos0 = cw_val; cw_rd(12'h149); t_pos0 = t_pos0 + cw_val * 256;
            repeat (3 * 628992) @(posedge sys_clk);
            cw_rd(12'h148); t_pos1 = cw_val; cw_rd(12'h149); t_pos1 = t_pos1 + cw_val * 256;
            $display("tape: stop marker reached at frame %0d, status %02x, position %0d then %0d three frames later, CPU status A=%02x", fclk, t_stat, t_pos0, t_pos1, cpu_mem(16'h9000));
            if (!(t_stat[2] && !t_stat[0] && t_pos0 == t_pos1 && cpu_mem(16'h9000) == 8'h01)) t_ok = 0;
            cw_wr(12'h140, 8'd0);
            repeat (2000) @(posedge sys_clk);
            cw_wr(12'h140, 8'd1);
            if ($test$plusargs("TAPEDBG")) for (i = 0; i < 40; i = i + 1) begin
                repeat (300000) @(posedge sys_clk);
                cw_rd(12'h148); t_pos0 = cw_val; cw_rd(12'h149); t_pos0 = t_pos0 + 256 * cw_val; cw_rd(12'h150);
                $display("  tapedbg %0d: pos %0d status %02x pc %04x 9001 %02x 9FFF %02x", i, t_pos0, cw_val, dut.cpu_addr, cpu_mem(16'h9001), cpu_mem(16'h9FFF));
            end
            maxframes = 120;
            wait_done;
            cw_rd(12'h150);
            while (!cw_val[1] && fclk < maxframes) begin repeat (20000) @(posedge sys_clk); cw_rd(12'h150); end
            t_stat = cw_val;
            cw_rd(12'h151);
            $display("tape: end at frame %0d, tape status %02x, underruns %0d", fclk, t_stat, cw_val);
            compare("tape", 1, 0);
            errs = 0;
            for (i = 0; i < 18; i = i + 1) begin
                win_rd(1'b0, 16'h9010 + i);
                if (cw_val !== expm[16 + i]) errs = errs + 1;
                win_rd(1'b0, 16'h9040 + i);
                if (cw_val !== expm[64 + i]) errs = errs + 1;
            end
            if (errs == 0 && t_ok && t_stat[1] && !t_stat[3]) $display("PASS tape window: the motor bit stops and restarts the stream, block A loads at tape speed and passes its XOR, block B (one bit flipped) is refused");
            else $display("FAIL tape window: %0d window differences, motor/marker checks %0d, end status %02x", errs, t_ok, t_stat);
        end
        "disk": begin
            if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
            $readmemh(ddrfile, ddr.mem, 131072);                    // the image at 0x01A00000 (the model's memory starts at 0x01800000)
            boot(2'd2);
            // the image: base, size (49,408 bytes), mount drive A
            cw_wr(12'h180, 8'h00); cw_wr(12'h181, 8'h00); cw_wr(12'h182, 8'hA0); cw_wr(12'h183, 8'h01);
            cw_wr(12'h184, 8'h00); cw_wr(12'h185, 8'hC1); cw_wr(12'h186, 8'h00); cw_wr(12'h187, 8'h00);
            cw_wr(12'h190, 8'h01);
            maxframes = 200;
            wait_done;
            d_err = 0;
            // read: sector C=3, R=0xC2 (512 bytes at $8200) equals the image's bytes at its offset
            for (i = 0; i < 512; i = i + 1) begin : rdchk
                reg [127:0] w;
                integer off;
                off = 256 + 3 * 4864 + 256 + 512 * 1 + i;           // header, track 3 (0x1300 bytes), its 256 byte header, sector R=0xC2 is the second
                w = ddr.mem[131072 + (off >> 4)];
                if (cpu_mem(16'h8200 + i) !== w[8 * (off % 16) +: 8]) begin if (d_err < 5) $display("  disk: read byte %0d is %02x, the image has %02x", i, cpu_mem(16'h8200 + i), w[8 * (off % 16) +: 8]); d_err = d_err + 1; end
            end
            // write: the image's sector R=0xC5 now holds (i & 255) ^ 0x5A, the read-back at $8400 too
            for (i = 0; i < 512; i = i + 1) begin : wrchk
                reg [127:0] w;
                integer off;
                off = 256 + 3 * 4864 + 256 + 512 * 4 + i;
                w = ddr.mem[131072 + (off >> 4)];
                if (w[8 * (off % 16) +: 8] !== ((i & 255) ^ 8'h5A)) begin if (d_err < 5) $display("  disk: image byte %0d of the written sector is %02x", i, w[8 * (off % 16) +: 8]); d_err = d_err + 1; end
                if (cpu_mem(16'h8400 + i) !== ((i & 255) ^ 8'h5A)) begin if (d_err < 5) $display("  disk: read-back byte %0d is %02x", i, cpu_mem(16'h8400 + i)); d_err = d_err + 1; end
            end
            // the three result phases: the CPC has no terminal-count line, so each command stops at its EOT sector with ST0 bits 7:6 = 01 (abnormal termination)
            // and ST1 bit 7 (end of cylinder) set - and nothing else wrong (ST1 bits 6:0 and ST2 are 0)
            for (i = 0; i < 3; i = i + 1) begin
                $display("disk: phase %0d result ST0 %02x ST1 %02x ST2 %02x C %02x H %02x R %02x N %02x", i, cpu_mem(16'h9010 + 16 * i), cpu_mem(16'h9011 + 16 * i), cpu_mem(16'h9012 + 16 * i),
                         cpu_mem(16'h9013 + 16 * i), cpu_mem(16'h9014 + 16 * i), cpu_mem(16'h9015 + 16 * i), cpu_mem(16'h9016 + 16 * i));
                if (cpu_mem(16'h9010 + 16 * i) !== 8'h40 || cpu_mem(16'h9011 + 16 * i) !== 8'h80 || cpu_mem(16'h9012 + 16 * i) !== 8'h00) d_err = d_err + 1;
            end
            // the dirty bit of the 16 KB chunk of the written sector (offset 17,152 -> chunk 1) and no other
            cw_rd(12'h1A0); d_chunk = cw_val;
            cw_rd(12'h191);
            $display("disk: dirty byte 0 of drive A is %02x, status %02x, FDC beyond-image flag %0d", d_chunk, cw_val, cw_val[1]);
            if (d_chunk != 8'h02) d_err = d_err + 1;
            $display("disk: case LED source drv_led low before the motor %0d, high with the motor %0d, now %0d", led_first_low, led_seen, dut.drv_led);
            if (!led_first_low || !led_seen || dut.drv_led !== 1'b1) begin d_err = d_err + 1; $display("  disk: drv_led does not follow the FDC motor"); end
            // the app clears it
            cw_wr(12'h1A0, 8'h02);
            repeat (200) @(posedge sys_clk);
            cw_rd(12'h1A0);
            if (cw_val != 8'h00) d_err = d_err + 1;
            if (d_err == 0) $display("PASS disk: the FDC read a sector of the DSK image from the DDR2 byte-exact, wrote one back (image and read-back agree), marked its 16 KB chunk dirty, the app cleared it");
            else $display("FAIL disk: %0d differences", d_err);
        end
        "rom_cat": begin                                           // DIAGNOSTIC: the real ROMs (+OSROM +BASROM +AMSROM as hex), a DSK in the DDR2 (+DDR), CAT typed
            begin : rc
                reg [8*200-1:0] f2;
                integer fr, k2, dsz, pass, mnt, obs, prew, rlen, npass, idle_fr, lastfr, done_c;
                if (!$value$plusargs("DDR=%s", ddrfile)) ddrfile = "";
                if (!$value$plusargs("DSZ=%d", dsz)) dsz = 194816;
                if (!$value$plusargs("MOUNTAT=%d", mnt)) mnt = 0;
                $readmemh(ddrfile, ddr.mem, 131072);
                cw_wr(12'h0A0, 8'h01);
                cw_wr(12'h0A1, 8'h02);
                $value$plusargs("OSROM=%s", f2); $readmemh(f2, dut.mem.rom_os);
                $value$plusargs("BASROM=%s", f2); $readmemh(f2, dut.mem.rom_bas);
                $value$plusargs("AMSROM=%s", f2); $readmemh(f2, dut.mem.rom_ams);
                if ($test$plusargs("RSTREQ")) begin       // the app: cpc_init holds, mode_reset(true) = reset_req, the media go in, release
                    reset_req = 1'b1; cw_wr(12'h0A0, 8'h00);
                    if (mnt == 0) rc_mount(dsz);
                    repeat (3000) @(posedge sys_clk);
                    reset_req = 1'b0;
                end else begin
                if (mnt == 0) rc_mount(dsz);
                cw_wr(12'h0A0, 8'h00);
                end
                if (!$value$plusargs("PREWAIT=%d", prew)) prew = 150;
                if (!$value$plusargs("RLEN=%d", rlen)) rlen = 2000;
                if (!$value$plusargs("NPASS=%d", npass)) npass = ($test$plusargs("RESET2")) ? 2 : 1;
                for (pass = 0; pass < npass; pass = pass + 1) begin
                    if (pass >= 1) begin
                        $display("rom_cat: warm reset at frame %0d (%0s, %0d clocks)", frames_seen, $test$plusargs("RSTREQ") ? "reset_req, the app's / ui_btn(2) path" : "ctrl bit 0", rlen);
                        if ($test$plusargs("RSTREQ")) begin reset_req = 1'b1; repeat (rlen) @(posedge sys_clk); reset_req = 1'b0; end
                        else begin cw_wr(12'h0A0, 8'h01); repeat (rlen) @(posedge sys_clk); cw_wr(12'h0A0, 8'h00); end
                    end
                    fr = frames_seen; while (frames_seen < fr + ((mnt != 0 && pass == 0) ? mnt : prew)) @(posedge sys_clk);
                    if (mnt != 0 && pass == 0) begin
                        rc_mount(dsz);
                        $display("rom_cat: mounted at frame %0d", frames_seen);
                        fr = frames_seen; while (frames_seen < fr + 60) @(posedge sys_clk);
                    end
                    if (pass == 0 && $test$plusargs("SKIPCAT0")) continue;     // the issue's flow: insert, then reset, then CAT (no CAT between)
                    $display("rom_cat: pass %0d frame %0d, pc %04x, FDC main status %02x", pass, frames_seen, dut.cpu_addr, dut.fdc.m_status);
                    rd_cmds = 0; cmds = 0;
                    // CAT RETURN: C line 7 bit 6, A line 8 bit 5, T line 6 bit 3, RETURN line 2 bit 2
                    for (k2 = 0; k2 < 4; k2 = k2 + 1) begin : typ
                        reg [79:0] m;
                        m = 80'd0;
                        case (k2) 0: m[62] = 1'b1; 1: m[69] = 1'b1; 2: m[51] = 1'b1; default: m[18] = 1'b1; endcase
                        for (i = 0; i < 10; i = i + 1) cw_wr(12'h160 + i, m[8 * i +: 8]);
                        fr = frames_seen; while (frames_seen < fr + 5) @(posedge sys_clk);
                        for (i = 0; i < 10; i = i + 1) cw_wr(12'h160 + i, 8'h00);
                        fr = frames_seen; while (frames_seen < fr + 5) @(posedge sys_clk);
                    end
                    fr = frames_seen;
                    if (!$value$plusargs("OBS=%d", obs)) obs = 250;
                    idle_fr = 0; lastfr = frames_seen; done_c = 0;
                    while (frames_seen < fr + obs && !done_c) begin
                        @(posedge sys_clk);
                        if (frames_seen != lastfr) begin           // once per frame: the CAT is over when the FDC has been idle for 40 frames after a Read Data
                            lastfr = frames_seen;
                            if ((frames_seen - fr) % 25 == 0) $display("  obs frame %0d: pc %04x, FDC status %02x, Read Data %0d", frames_seen, dut.cpu_addr, dut.fdc.m_status, rd_cmds);
                            if (rd_cmds > 0 && dut.fdc.m_status == 8'h80) idle_fr = idle_fr + 1; else idle_fr = 0;
                            if (idle_fr >= 40) done_c = 1;
                        end
                        if (dut.io_wr && !rc_wd && dut.cpu_addr == 16'hFB7F) begin
                            cmds = cmds + 1;
                            if (cmds < 12) $display("  FDC data write %02x at frame %0d (status %02x, scan %0d)", dut.cpu_dout, frames_seen, dut.fdc.m_status, dut.fdc.fdc.image_scan_state[0]);
                            if (dut.cpu_dout[4:0] == 5'd6) rd_cmds = rd_cmds + 1;
                        end
                        rc_wd = dut.io_wr;
                    end
                    $display("rom_cat: pass %0d end frame %0d, pc %04x, FDC main status %02x, scan state %0d, ready %0d, FDC data writes %0d, read-data commands %0d, DDR reads %0d", pass, frames_seen, dut.cpu_addr, dut.fdc.m_status, dut.fdc.fdc.image_scan_state[0], dut.fdc.fdc.image_ready[0], cmds, rd_cmds, ddr.reads);
                    if (rd_cmds > 0 && dut.fdc.m_status == 8'h80) $display("PASS rom_cat pass %0d: AMSDOS read the directory (Read Data issued, FDC idle again)", pass);
                    else $display("FAIL rom_cat pass %0d: the FDC never came to the Read Data command or never became idle", pass);
                end
            end
        end
        "pause_ref": begin
            boot(2'd2);
            while (frames_seen < 24) @(posedge sys_clk);
            $display("done pause_ref");
        end
        "pause_run": begin
            boot(2'd2);
            while (frames_seen < 4) @(posedge sys_clk);
            repeat (200000) @(posedge sys_clk);
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            repeat (3 * 628992 + 123456) @(posedge sys_clk);
            halt_req = 1'b0;
            while (halted && frames_seen < 80) @(posedge sys_clk);
            // a short request holds at once too (the Gate Array stops at a clock edge, not at a frame boundary), and releases exactly
            while (frames_seen < 8) @(posedge sys_clk);
            repeat (50001) @(posedge sys_clk);
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            repeat (30000) @(posedge sys_clk);
            halt_req = 1'b0;
            while (halted && frames_seen < 80) @(posedge sys_clk);
            while (frames_seen < 12) @(posedge sys_clk);
            repeat (400000) @(posedge sys_clk);
            halt_req = 1'b1;
            while (!halted && frames_seen < 60) @(posedge sys_clk);
            repeat (7 * 628992) @(posedge sys_clk);
            halt_req = 1'b0;
            while (halted && frames_seen < 80) @(posedge sys_clk);
            while (frames_seen < 24) @(posedge sys_clk);
            $display("pause_run: %0d holds; while held: Gate Array clocks %0d, CPU clocks %0d, AY clocks %0d, FDC clocks %0d, tape ticks %0d, frame-buffer writes %0d, non-zero audio samples %0d",
                     h_holds, h_ce, h_cpu, h_ay, h_fdc, h_tape, h_fb, h_aud);
            h_holds_3 = h_holds;                                          // the holds of the three requests; the one below (and the reset's) are not counted
            halt_req = 1'b1;
            while (!halted && frames_seen < 80) @(posedge sys_clk);
            reset_req = 1'b1;
            repeat (8) @(posedge sys_clk);
            i = halted;                                                   // the hold is off while the reset is requested, though the app still asks for it
            reset_req = 1'b0;
            repeat (4) @(posedge sys_clk);
            halt_req = 1'b0;
            if (h_holds_3 == 3 && h_ce == 0 && h_cpu == 0 && h_ay == 0 && h_fdc == 0 && h_tape == 0 && h_fb == 0 && h_aud == 0 && i == 0)
                $display("PASS pause_run: held at a clock edge three times, nothing moved while held, a reset took the hold away");
            else $display("FAIL pause_run: holds %0d (want 3), counters above, halted after reset %0d", h_holds_3, i);
        end
        "video": begin
            boot(2'd2);
            vtot = 0; vbx = -1;
            for (vm = 0; vm <= 2; vm = vm + 1) begin
                // the screen of this mode is up for 12 frames; take the newest complete buffer 8 frames in
                while (cpu_mem(16'h9000) != vm + 1 && frames_seen < 80) @(posedge sys_clk);
                repeat (8 * 628992) @(posedge sys_clk);
                snap_to(vm);
                // find where the paper starts in the buffer (the search looks at 24 lines, then the whole screen must match)
                vbest = 1 << 30; vbx = 0; vby = 0;
                for (vy = 0; vy < 70; vy = vy + 1)
                    for (vx = 30; vx < 80; vx = vx + 1) begin
                        vd = vid_diff(vm, vm, vx, vy, 3);
                        if (vd < vbest) begin vbest = vd; vbx = vx; vby = vy; end
                    end
                $display("video: mode %0d search best (3 lines) %0d at (%0d,%0d); at (56,34): 3 lines %0d, 200 lines %0d", vm, vbest, vbx, vby, vid_diff(vm, vm, 56, 34, 3), vid_diff(vm, vm, 56, 34, 200));
                for (vx = vbx - 1; vx <= vbx + 1; vx = vx + 1) begin
                    vd = vid_diff(vm, vm, vx, vby, 200);
                    if (vx == vbx - 1 || vd < vtot) vtot = vd;
                end
                vd = vid_diff(vm, vm, vbx, vby, 200);
                $display("video: mode %0d paper starts at window column %0d, row %0d (source rows %0d); %0d of 256,000 pixel samples differ; border sample %04x (want %04x)",
                         vm, vbx, vby, 2 * vby, vd, (vm == 0) ? snapa[3 * 720 + 3] : (vm == 1) ? snapb[3 * 720 + 3] : snapc[3 * 720 + 3], hw_rgb565(5'h0B));
                if (vd != 0 || ((vm == 0) ? snapa[3 * 720 + 3] : (vm == 1) ? snapb[3 * 720 + 3] : snapc[3 * 720 + 3]) !== hw_rgb565(5'h0B)) errs = errs + 1;
                // T92 §5 2026-10-19 (Alex: centre it): 40 border columns each side of the 640 of paper; mode 2 starts one dot earlier and stays so
                if (vbx != ((vm == 2) ? 39 : 40)) begin errs = errs + 1; $display("  video: mode %0d paper starts at column %0d, not centred (want %0d)", vm, vbx, (vm == 2) ? 39 : 40); end
            end
            // control: a wrong mode's decode of the mode-0 picture must differ
            vd = vid_diff(0, 1, vbx, vby, 200);
            $display("video control: the mode-0 picture decoded as mode 1 differs in %0d samples", vd);
            if (errs == 0 && vd > 1000) $display("PASS video: modes 0, 1 and 2 of random screen memory (16 pens, border) equal the CPC's rules in all 256,000 samples of each mode, every line twice");
            else $display("FAIL video: %0d modes differ, control %0d", errs, vd);
        end
        "slots": begin
            boot(2'd2);
            wait_done;
            $display("slots: program finished at frame %0d", frames_seen);
        end
        "model": begin
            // 6128 first (the 128 KB machine with AMSDOS), then the app holds the reset, switches the model and the machine cold-boots into a 464
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, 8'd2);
            $readmemh(romfile, dut.mem.rom_os);
            win_wr(1'b1, 16'h4000, 8'hB1);
            win_wr(1'b1, 16'h8000, 8'hB7);
            cw_wr(12'h0A0, 8'h00);
            wait_done;
            compare("model 6128", 1, 0);
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, 8'd0);
            cw_wr(12'h0D4, 8'h00);
            win_wr(1'b0, 16'h9FFF, 8'h00);
            if (!$value$plusargs("EXP2=%s", expfile)) expfile = "";
            frames_seen = 0;
            cw_wr(12'h0A0, 8'h00);
            wait_done;
            compare("model 464", 1, 0);
        end
        "bars": begin
            boot(2'd2);
            while (frames_seen < 10) @(posedge sys_clk);
            snap_to(0);
            b_l = 1 << 30; b_r = 1 << 30; b_pl = 0; b_pr = 0;
            for (p0i = 0; p0i < 16; p0i = p0i + 1) begin
                vd = bars_diff(5, p0i);   if (vd < b_l) begin b_l = vd; b_pl = p0i; end
                vd = bars_diff(715, p0i); if (vd < b_r) begin b_r = vd; b_pr = p0i; end
            end
            b_alt = bars_diff(5, (b_pl + 1) % 16);
            if ($test$plusargs("BARSDBG")) for (p0i = 0; p0i < 270; p0i = p0i + 1) begin
                vd = -1;
                for (vx = 0; vx < 16; vx = vx + 1) if (snapa[(2 * p0i) * 720 + 5] === hw_rgb565(bar_hw[vx])) vd = vx;
                $display("  bars row %0d left ink idx %0d (want %0d)", p0i, vd, (p0i + b_pl) % 16);
            end
            $display("bars: left border follows the 16-ink cycle with phase %0d (%0d of 540 rows differ), right border phase %0d (%0d differ); the next phase differs in %0d rows", b_pl, b_l, b_pr, b_r, b_alt);
            if (b_l == 0 && b_r == 0 && ((b_pr - b_pl + 16) % 16) <= 1 && b_alt > 400) $display("PASS bars: an ink change in every raster line shows on its own line (270 lines, each twice), left and right of the picture");
            else $display("FAIL bars");
        end
        default: $display("FAIL unknown mode %0s", mode);
        endcase
        $finish;
    end
endmodule
