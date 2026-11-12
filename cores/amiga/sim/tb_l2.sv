// T85: the Amiga core as the host sees it (amiga_core on the real mig_arb and a behavioural DDR2), driven like the
// application: registers over the cw window, Kickstart (our test ROM) and an ADF in the DDR2 window, keys,
// mouse. Checks: the machine boots the test ROM, a frame buffer is written and announced, the flip protocol
// (FB_LATEST, PX_BACK, flip event) hands out three buffers, the picture is where it should be, keyboard and
// mouse events reach the Minimig, the audio path is alive.
`timescale 1ns/1ps
module tb_l2;
    parameter FRAMES = 9;
    parameter FAST = 0;
    parameter FASTPAT = 0;             // MODE=fastpat test ROM (T97 5.9): pattern results the CPU left in chip RAM $300..$310
    parameter MEMNB = 0;               // non-BOOT: MEM_CFG byte when not 0 (0F = chip 2 MB + slow 1.5 MB, as the A1200 application writes)
    // T138: Z3 = MODE=z3ram test ROM: the Zorro III board of 32 MB (FAST_CFG 4); DDRLOG2 = log2 of the DDR2 model's 128-bit words (22: 64 MB, 23: 128 MB, the
    // board lives at DDR2 byte 0x6000000..0x7FFFFFF)
    parameter Z3 = 0;
    parameter DDRLOG2 = 22;
    parameter NOPIC = 0;               // FASTPAT with the AGA DMA beside it: the stripes picture of the tail is not checked (only the patterns are the subject)
    parameter FASTCFG = 2;             // FAST_CFG byte (2 = 4 MB, 3 = 5.5 MB)
    parameter DISK = 0;
    parameter DSKCHG = 0;              // MODE=dskchg test ROM (T85): trackdisk's poll of DF0 while the bench inserts, ejects and swaps the disk like the application's MOUNT register
    parameter MOTORCHK = 0;            // amiga_core's drv_led (the case LED 1 source, T159) is low after reset and goes high when the ROM's motor-on + select of DF0 (DISK) runs
    parameter IDE = 0;
    parameter KEY = 0;
    parameter AUDIO = 0;
    parameter SLOW = 0;
    parameter BOOT = 0;                // boot a real Kickstart + ADF (files kick.hex, adf.hex), no checks
    parameter ECS = 0;
    // T102: CFGCHK = the application's configuration commit, nothing else. The core is built with the power-up values
    // CHS_INIT / MEM_INIT and HDF_SECT (cores/amiga1200/core_sources.tcl: 16 / 3 / 1); the register bytes come from the plusargs
    // +CFG_MEM= +CFG_FAST= +CFG_CPU= +CFG_CHIPSET= +CFG_FLOPPY= +CFG_IDE= (hex) - tests/cfgdump of the application prints them.
    // The bench expects the machine of the Amiga 1200: AGA on, 2 MB chip RAM, the HDF size in sectors.
    parameter CFGCHK = 0;
    // T97 5.10: WINCHK = the application moves amiga_fb's crop window with the A1200's registers 0D0.. (1: skip 56/8, 60 words x 100 lines,
    // at the top left of the buffer; 2: skip 120, the source line ends inside the window) and reads the display window at 0C0..;
    // the three buffers are cleared first, as the application does, so what lies outside the window must stay 0.
    parameter WINCHK = 0;
    // T136: FB32 = the A1200's own amiga_fb (32-bit pixels, buffers at DDR2 0x4000000, 2 MB apart); 0: RGB565 at 0x1D00000, 1 MB apart
    parameter FB32 = 0;
    parameter C020 = 0;                // MODE=cpu020 test ROM: 68020-only instructions ran (T97 step 2)
    parameter CHS_INIT = 0, MEM_INIT = 0, HDF_SECT = 0;
    parameter MEMCFG = 4;              // BOOT: MEM_CFG byte (b1:0 chip 512K/1M/1.5M/2M, b3:2 slow none/512K/1M/1.5M); 4 = chip 512K + slow 512K
    parameter MIGLAT = 8, MIGMISS = 0, MIGSTALL = 0;   // the DDR2 model's timing (a slow one makes the chip bus stall)
    parameter PAUSE = 0;               // Freeze bench (tb_pause_mon.svh): pauses at random moments, logs for the comparison with an unpaused run
    parameter TICKS = 6500000;         // PAUSE: the run ends after this many ce28 ticks
    parameter DUMPUS = 5000000;        // BOOT: a picture of the latest frame every DUMPUS simulated microseconds (img_NNN.ppm)
    parameter STOPUS = 20000000;       // simulated microseconds to run in BOOT mode
    reg sys_clk = 0, ui_clk = 0, cpu_clk = 0, mclk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #6.667 ui_clk = ~ui_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #20.35 mclk = ~mclk;
    reg sys_rst = 1;
    integer cyc = 0;
    always @(posedge sys_clk) begin cyc <= cyc + 1; if (cyc == 30) sys_rst <= 0; end

    // DDR2 model behind the real arbiter
    wire ui_rst_m, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_valid;
    // 64 MB: the window (0x1000000) and the A1200's 32-bit buffers (0x4000000, T136) do not alias
    localparam [31:0] WIN = 32'h01000000;
    mig_ui_model #(.MEM_WORDS(1<<DDRLOG2), .RD_LAT(MIGLAT), .MISS(MIGMISS), .STALL_PCT(MIGSTALL), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst_m), .calib_done(calib), .app_addr(app_addr), .app_cmd(app_cmd),
        .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data),
        .app_rd_data_valid(app_rd_valid));
    wire [26:0] c_addr, e_addr; wire [2:0] c_cmd, e_cmd; wire c_en, e_en, c_rdy, e_rdy, c_wdf_rdy, c_valid, e_valid;
    wire [127:0] c_wdf_data, e_wdf_data, c_rd; wire [15:0] c_wdf_mask, e_wdf_mask; wire c_wdf_wren, e_wdf_wren;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst_m), .calib_done(calib),
        .req_tgl(3'b000), .addr(78'd0), .we(3'b000), .wdata(96'd0), .be(12'd0), .done_tgl(), .rdata(), .rdata_line(),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
        .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy), .c_rd_data(c_rd), .c_rd_data_valid(c_valid),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0), .v_wdf_wren(1'b0),
        .v_wdf_rdy(), .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren), .e_rd_data_valid(e_valid),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data),
        .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data),
        .app_rd_data_valid(app_rd_valid));

    // the core
    reg cw_we = 0, cw_re = 0; reg [16:0] cw_addr = 0; reg [7:0] cw_wdata = 0;
    wire [7:0] cw_rdata; wire cw_ack, cw_irq, px_we, px_re;
    reg px_flip = 0, px_vsync = 0;
    reg [63:0] keys = 64'd0; reg [4:0] joy1 = 0, joy2 = 0; reg restore = 0;
    wire [3:0] pix_color; wire [9:0] pix_x; wire [8:0] pix_y; wire signed [23:0] al, ar;
    reg [8:0] adiv = 0; always @(posedge mclk) adiv <= adiv + 9'd1; wire aen = (adiv == 9'd0);     // the host's 48 kHz frame
    reg pause_req = 0; wire paused;
    reg dma_tgl = 0; wire dma_done; wire [7:0] dma_rd; wire running;
    amiga_core #(.CHS_INIT(CHS_INIT), .MEM_INIT(MEM_INIT), .HDF_SECT(HDF_SECT)) core (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst_m | ~calib),
        .reset_req(1'b0), .running(running), .pause_req(pause_req), .paused(paused),
        .keys(keys), .joy1(joy1), .joy2(joy2), .restore(restore),
        .dma_req_tgl(dma_tgl), .dma_done_tgl(dma_done), .dma_rdata(dma_rd),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
        .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy), .c_rd_data(c_rd), .c_rd_valid(c_valid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_valid),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_sample_en(aen), .audio_l(al), .audio_r(ar),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack),
        .cw_irq(cw_irq), .px_we(px_we), .px_re(px_re), .px_rdata(8'h00), .px_ev_vsync(px_vsync), .px_ev_flip(px_flip),
        .build(32'h12345678), .ddr_cal(64'd0));
    reg mot_seen = 1'b0, mot_low_first = 1'b0;
    // core.drv_led: run_l2.sh defines MOTORCHK_ON for MOTORCHK=1 (A500 and A1200 amiga_core both have it since T159 D-all)
`ifdef MOTORCHK_ON
    generate if (MOTORCHK) begin : motor_mon
        always @(posedge sys_clk) begin
            if (!sys_rst && !mot_seen && core.drv_led === 1'b0) mot_low_first <= 1'b1;
            if (core.drv_led === 1'b1) mot_seen <= 1'b1;
        end
    end endgenerate

`endif

    // ---- the application's side
    reg cw_busy = 0;
    task cw_write(input [16:0] a, input [7:0] d);
        begin
            while (cw_busy) @(posedge cpu_clk);
            cw_busy = 1;
            @(posedge cpu_clk); cw_we <= 1; cw_addr <= a; cw_wdata <= d;
            @(posedge cpu_clk); cw_we <= 0;
            @(posedge cpu_clk); while (!cw_ack) @(posedge cpu_clk);
            cw_busy = 0;
        end
    endtask
    reg [7:0] rd8;
    task cw_read(input [16:0] a);
        begin
            while (cw_busy) @(posedge cpu_clk);
            cw_busy = 1;
            @(posedge cpu_clk); cw_re <= 1; cw_addr <= a;
            @(posedge cpu_clk); cw_re <= 0;
            @(posedge cpu_clk); while (!cw_ack) @(posedge cpu_clk);
            rd8 = cw_rdata;
            cw_busy = 0;
        end
    endtask
    // DDR2 window helpers (byte offsets in the window; the model index is the burst number)
    task win_poke16(input [23:0] byteoff, input [15:0] w);        // big endian word as the file has it
        reg [21:0] bi; integer lane;
        begin bi = (WIN + byteoff) >> 4; lane = (byteoff >> 1) & 7; ddr.mem[bi][16*lane +: 16] = {w[7:0], w[15:8]}; end
    endtask
    // the model's 128-bit words by absolute DDR2 byte address (the Zorro III board, T138)
    function [15:0] ddr_peek16(input [31:0] babs);
        reg [31:0] bi; integer lane;
        begin bi = babs >> 4; lane = (babs >> 1) & 7; ddr_peek16 = {ddr.mem[bi][16*lane +: 8], ddr.mem[bi][16*lane + 8 +: 8]}; end
    endfunction
    function [15:0] win_peek16(input [23:0] byteoff);
        reg [21:0] bi; integer lane;
        begin bi = (WIN + byteoff) >> 4; lane = (byteoff >> 1) & 7; win_peek16 = {ddr.mem[bi][16*lane +: 8], ddr.mem[bi][16*lane + 8 +: 8]}; end
    endfunction
    // the pixel as {R, G, B} 8 bits each: RGB565 widened by repeating the top bits, the 32-bit word's low 24 bits as they are
    function [23:0] fb_peek24(input integer buf_i, input integer row, input integer col);
        reg [31:0] off; reg [21:0] bi; integer lane; reg [15:0] p;
        begin
            if (FB32) begin
                off = 32'h04000000 + buf_i * 32'h200000 + row * 2880 + col * 4;
                bi = off >> 4; lane = (off >> 2) & 3;
                fb_peek24 = ddr.mem[bi][32*lane +: 24];
            end else begin
                off = WIN + 32'hD00000 + buf_i * 32'h100000 + row * 1440 + col * 2;
                bi = off >> 4; lane = (off >> 1) & 7;
                p = ddr.mem[bi][16*lane +: 16];
                fb_peek24 = {p[15:11], p[15:13], p[10:5], p[10:9], p[4:0], p[4:2]};
            end
        end
    endfunction
    function [15:0] fb_peek(input integer buf_i, input integer row, input integer col);   // as RGB565 (the checks' colours)
        reg [23:0] c;
        begin c = fb_peek24(buf_i, row, col); fb_peek = {c[23:19], c[15:10], c[7:3]}; end
    endfunction

    function [15:0] adf_word(input integer sec, input integer off);
        adf_word = (sec * 256 + off / 2) * 7 + 16'h1234 + (sec << 11);
    endfunction
    function [31:0] demfm(input [31:0] odd, input [31:0] even);
        demfm = ((odd & 32'h55555555) << 1) | (even & 32'h55555555);
    endfunction
    function [7:0] hdf_byte(input integer lba, input integer i);
        hdf_byte = (lba * 29 + i * 3 + 7) & 8'hFF;
    endfunction
    // the application's mailbox service: the RDB at LBA 0 (64 cylinders, 4 heads, 32 sectors), a pattern elsewhere
    reg mb_seen = 0;
    // A1200 stripes: +HDF=<file> serves the mailbox from a real image (read only; writes are dropped)
    integer hdf_fd = 0; reg [255*8-1:0] hdf_name; reg [7:0] hdf_buf [0:511];
    initial if ($value$plusargs("HDF=%s", hdf_name)) hdf_fd = $fopen(hdf_name, "rb");
    task hdf_read(input integer lba);
        integer q, r, k2;
        begin
            r = $fseek(hdf_fd, 0, 0);
            for (k2 = 0; k2 < (lba >> 21); k2 = k2 + 1) r = $fseek(hdf_fd, 1073741824, 1);
            r = $fseek(hdf_fd, (lba & 32'h1FFFFF) * 512, 1);
            r = $fread(hdf_buf, hdf_fd, 0, 512);
            for (q = 0; q < 512; q = q + 1) win_poke8(24'hC80000 + q, hdf_buf[q]);
        end
    endtask
    always @(core.mb_tgl) if (IDE && core.mb_tgl !== mb_seen) begin
        mb_seen = core.mb_tgl;
        #2000;
        begin : serve
            integer q, lba;
            lba = core.mb_lba;
            if (!core.mb_we && hdf_fd != 0) begin
                hdf_read(lba); hdf_reads = hdf_reads + 1;
            end else if (!core.mb_we) begin
                for (q = 0; q < 512; q = q + 1) win_poke8(24'hC80000 + q, hdf_byte(lba, q));
                if (lba == 0) begin
                    win_poke8(24'hC80000, "R"); win_poke8(24'hC80001, "D"); win_poke8(24'hC80002, "S"); win_poke8(24'hC80003, "K");
                    for (q = 64; q < 76; q = q + 1) win_poke8(24'hC80000 + q, 0);
                    win_poke8(24'hC80000 + 67, 8'd64); win_poke8(24'hC80000 + 71, 8'd32); win_poke8(24'hC80000 + 75, 8'd4);
                end
                hdf_reads = hdf_reads + 1;
            end
            cw_write(17'h1A8, 8'h01);
        end
    end
    integer hdf_reads = 0;
    task win_poke8(input [23:0] off, input [7:0] v);
        begin ddr.mem[(WIN + off) >> 4][8*(off & 15) +: 8] = v; end
    endtask
    reg [15:0] rom [0:262143];
    integer i, j;
    reg [15:0] adf [0:450559];
    initial begin
        $readmemh("rom.hex", rom);
        for (i = 0; i < 262144; i = i + 1) win_poke16(24'h780000 + 2 * i, rom[i]);
        if (BOOT) begin
            $readmemh("adf.hex", adf);
            for (i = 0; i < 450560; i = i + 1) win_poke16(24'h900000 + 2 * i, adf[i]);
        end
        if (!BOOT) for (i = 0; i < 4*28672; i = i + 1) ddr.mem[((WIN + 32'h900000) >> 4) + i][127:0] = 128'd0;   // (BOOT: it would erase the ADF just loaded)
    end

    // ---- events
    integer nfr = 0;
    reg [1:0] last_idx = 0, prev_idx = 0; reg have_latest = 0;
    reg [1:0] shown_i = 0;
    integer flips = 0;
    // the app: on a complete frame, show it (PX_BACK), the flip comes at the next "output frame start"
    reg [7:0] cause;
    task show_latest;
        reg [31:0] a;
        begin
            cw_read(17'h0AC);
            if (rd8[7]) begin
                last_idx = rd8[1:0];
                a = FB32 ? 32'h04000000 + last_idx * 32'h200000 : 32'h01D00000 + last_idx * 32'h100000;
                cw_write(17'h6088, a[7:0]); cw_write(17'h6089, a[15:8]); cw_write(17'h608A, a[23:16]); cw_write(17'h608B, a[31:24]);
                flips = flips + 1;
                pm_shown(last_idx);
            end
        end
    endtask
    always @(posedge cw_irq) begin
        // handled in the main thread
    end

    // output frame start every 20 ms; a flip applies at the next one
    reg flip_pending = 0;
    initial begin
        forever begin
            #19952000;                       // 50.12 Hz
            px_vsync = 1; @(posedge cpu_clk); px_vsync = 0;
            if (flip_pending) begin px_flip = 1; @(posedge cpu_clk); px_flip = 0; flip_pending = 0; end
        end
    end

    task dump_ppm(input integer bi, input integer n);
        integer fdp, yy, xx; reg [23:0] p24;
        begin
            fdp = $fopen($sformatf("img_%03d.ppm", n), "wb");
            $fwrite(fdp, "P6\n720 540\n255\n");
            for (yy = 0; yy < 540; yy = yy + 1) for (xx = 0; xx < 720; xx = xx + 1) begin
                p24 = fb_peek24(bi, yy, xx);
                $fwrite(fdp, "%c%c%c", p24[23:16], p24[15:8], p24[7:0]);
            end
            $fclose(fdp);
        end
    endtask

    // A1200 stripes: +PROBE_FROM=<fb frame> registers at line PLINE of the next two frames, and that line's pixel path in run-length form
    integer probe_from = -1;
    initial if (!$value$plusargs("PROBE_FROM=%d", probe_from)) probe_from = -1;
    reg [36:0] pr_prev; integer pr_run, pr_frames, pr_line, pr_nl, pr_active; reg pr_vb, pr_hb;
    initial begin pr_prev = 37'h1fffffffff; pr_run = 0; pr_frames = 0; pr_line = 0; pr_vb = 1; pr_hb = 1; pr_active = 0; end
    always @(posedge sys_clk) if (BOOT && probe_from >= 0 && core.ce28) begin : probe
        reg [36:0] cur;
        cur = {core.nm.minimig.DENISE1.bpldata, core.nm.minimig.DENISE1.plfdata, core.nm.minimig.DENISE1.clut_data, core.nm.minimig.DENISE1.sprdata, 3'd0, core.nm.minimig.DENISE1.sprsel, core.nm.minimig.DENISE1.window_ena};
        if (pr_vb && !core.vbl_o) begin
            if (pr_active) $display("PROBE frame end: %0d lines", pr_line);
            pr_active = (core.fb.frames > probe_from && pr_frames < 2); pr_line = 0;
            if (pr_active) begin pr_frames = pr_frames + 1; $display("PROBE frame %0d t=%0t", pr_frames, $time); end
        end
        if (pr_hb && !core.hbl_o) begin
            pr_line = pr_line + 1;
            if (pr_active && (pr_line == 100 || pr_line == 200)) begin
                $display("PROBE line %0d: bplcon0 %04x bplcon1 %04x bplcon2 %04x bplcon3 %04x bplcon4 %04x fmode(denise) %04x l_bpu %0d ham %b", pr_line, core.nm.minimig.DENISE1.bplcon0, core.nm.minimig.DENISE1.bplm0.bplcon1,
                    core.nm.minimig.DENISE1.bplcon2, core.nm.minimig.DENISE1.bplcon3, core.nm.minimig.DENISE1.bplcon4, core.nm.minimig.DENISE1.bplm0.fmode, core.nm.minimig.DENISE1.l_bpu, core.nm.minimig.DENISE1.ham);
                $display("PROBE   agnus ddfstrt %02x ddfstop %02x bplcon0 %02x fmode(spr) %04x; sprites armed %b%b%b%b%b%b%b%b", core.nm.minimig.AGNUS1.bpd1.ddfstrt, core.nm.minimig.AGNUS1.bpd1.ddfstop, core.nm.minimig.AGNUS1.bpd1.bplcon0, core.nm.minimig.AGNUS1.spr1.fmode,
                    core.nm.minimig.DENISE1.sprm0.sps7.armed, core.nm.minimig.DENISE1.sprm0.sps6.armed, core.nm.minimig.DENISE1.sprm0.sps5.armed, core.nm.minimig.DENISE1.sprm0.sps4.armed,
                    core.nm.minimig.DENISE1.sprm0.sps3.armed, core.nm.minimig.DENISE1.sprm0.sps2.armed, core.nm.minimig.DENISE1.sprm0.sps1.armed, core.nm.minimig.DENISE1.sprm0.sps0.armed);
            end
        end
        if (pr_active && pr_line == 100 && !core.hbl_o && !core.vbl_o) begin
            if (cur !== pr_prev) begin
                if (pr_run > 0) $display("PROBE      x%0d", pr_run);
                $display("PROBE  bpl %02x plf %02x clut %02x spr %02x sprsel %b win %b", cur[36:29], cur[28:21], cur[20:13], cur[12:5], cur[1], cur[0]);
                pr_prev = cur; pr_run = 1;
            end else pr_run = pr_run + 1;
        end
        pr_vb <= core.vbl_o; pr_hb <= core.hbl_o;
    end
    // every bitplane register write of the display lines: the 64 bits the plane register holds one 7 MHz cycle later against the 64 bits the chip RAM holds
    integer bp_ok = 0, bp_bad = 0, bp_pr = 0; reg [63:0] bp_exp, bp_got; reg [23:0] bp_aw; reg bp_pend = 0; reg [3:0] bp_pl; reg [63:0] bp_reg;
    always @(posedge sys_clk) if (BOOT && probe_from >= 0 && core.ce28 && core.clk7_en) begin
        if (bp_pend) begin
            bp_pend = 0;
            case (bp_pl) 0: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl1dat; 1: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl2dat; 2: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl3dat;
                3: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl4dat; 4: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl5dat; 5: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl6dat;
                6: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl7dat; default: bp_reg = core.nm.minimig.DENISE1.bplm0.bpl8dat; endcase
            if (bp_reg === bp_exp) bp_ok = bp_ok + 1; else bp_bad = bp_bad + 1;
            if (bp_reg !== bp_exp && bp_pr < 24) begin
                bp_pr = bp_pr + 1;
                $display("PROBE BAD line %0d plane %0d aw %06x reg %016x exp %016x | m_dout48 %012x mem.st %0d r_w %0d", pr_line, bp_pl + 1, bp_aw, bp_reg, bp_exp, core.m_dout48, core.mem.st, core.mem.r_w);
            end
        end
        if (pr_active && pr_line >= 60 && pr_line <= 200 && core.nm.minimig.DENISE1.reg_address_in[8:4] == 5'b10001) begin
            bp_aw = {core.ram_address[23:3], 3'b000}; bp_pl = core.nm.minimig.DENISE1.reg_address_in[3:1]; bp_pend = 1;
            bp_exp = {win_peek16(bp_aw), win_peek16(bp_aw + 24'd2), win_peek16(bp_aw + 24'd4), win_peek16(bp_aw + 24'd6)};
        end
    end
    always @(posedge sys_clk) if (pr_active && pr_vb && !core.vbl_o) $display("PROBE bitplane writes ok %0d bad %0d", bp_ok, bp_bad);
    // ---- main
    integer errors = 0;
    task check(input cond, input [8*64-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end else $display("ok  : %0s", what); end
    endtask
    reg [31:0] c_mem, c_fast, c_cpu, c_chs, c_flp, c_ide;
    integer bk; integer k; integer first_x, last_x, first_y, last_y, ycount, ytot, bad;
    reg [15:0] px;
    initial begin
        #1000;
        // the application's start-up: configuration while held, then run
        check($bits(core.m_dout48) == 48 && $bits(core.p2_dout48) == 48, "the wide chip/fast read buses are 48 bits (an undeclared net is 1 bit)");
        cw_read(17'h000); check(rd8 == "A", "ID byte 0 is A");
        cw_read(17'h003); check(rd8 == "G", "ID byte 3 is G");
        cw_read(17'h008); check(rd8 == 8'h78, "BUILD low byte");
        if (CFGCHK) begin
            // power-up, before any application has spoken: the machine is what the core generics say
            check(core.chs_b == CHS_INIT[7:0] && core.mem_b == MEM_INIT[7:0], "power-up: chs_b and mem_b hold CHS_INIT and MEM_INIT");
            check(core.nm.minimig.DENISE1.aga === CHS_INIT[4] && core.nm.minimig.AGNUS1.aga === CHS_INIT[4], "power-up: Denise and Agnus aga input follow CHS_INIT");
            cw_read(17'h00C); check(rd8[0] === (HDF_SECT != 0), "FEATURES b0 reports HDF_SECT");
            // the application's apply(): MEM, FAST, CPU, CHIPSET, FLOPPY, IDE - each write is a commit (cfg_tgl)
            if (!$value$plusargs("CFG_MEM=%h", c_mem) || !$value$plusargs("CFG_FAST=%h", c_fast) || !$value$plusargs("CFG_CPU=%h", c_cpu) ||
                !$value$plusargs("CFG_CHIPSET=%h", c_chs) || !$value$plusargs("CFG_FLOPPY=%h", c_flp) || !$value$plusargs("CFG_IDE=%h", c_ide)) begin
                $display("FAIL: CFGCHK needs +CFG_MEM +CFG_FAST +CFG_CPU +CFG_CHIPSET +CFG_FLOPPY +CFG_IDE"); $finish;
            end
            $display("CFGCHK: the application writes MEM=%02x FAST=%02x CPU=%02x CHIPSET=%02x FLOPPY=%02x IDE=%02x", c_mem[7:0], c_fast[7:0], c_cpu[7:0], c_chs[7:0], c_flp[7:0], c_ide[7:0]);
            cw_write(17'h0A1, c_mem[7:0]); cw_write(17'h0A2, c_fast[7:0]); cw_write(17'h0A3, c_cpu[7:0]);
            cw_write(17'h0A4, c_chs[7:0]); cw_write(17'h0A5, c_flp[7:0]); cw_write(17'h0A6, c_ide[7:0]);
            // an HDF of MegaAGS's size, announced the way the application does: size register, then mount
            if (HDF_SECT != 0) begin cw_write(17'h184, 8'hC0); cw_write(17'h185, 8'h77); cw_write(17'h186, 8'h0C); cw_write(17'h187, 8'h01); end   // 17,594,304 sectors
            else begin cw_write(17'h184, 8'h00); cw_write(17'h185, 8'h00); cw_write(17'h186, 8'h40); cw_write(17'h187, 8'h00); end                    // 4 MiB
            cw_write(17'h180, 8'h01);
            #3000000;                                                   // the commit crosses into sys_clk, the mount reaches the sector service
            $display("CFGCHK: after the commit chipset_config %02x memory_config %02x, Denise aga %0d Agnus aga %0d, bank mapper chip config %0d, HDF size %0d bytes",
                core.chs_b, core.mem_b, core.nm.minimig.DENISE1.aga, core.nm.minimig.AGNUS1.aga, core.nm.minimig.BMAP1.memory_config, core.sdc.sdc_img_size);
            check(core.nm.minimig.DENISE1.aga === 1'b1 && core.nm.minimig.AGNUS1.aga === 1'b1, "after commit: Denise and Agnus aga input still set (AGA)");
            check(core.chs_b[4] === 1'b1, "after commit: chipset_config bit 4 (AGA) still set");
            check(core.nm.minimig.BMAP1.memory_config === 2'd3 && core.mem_b[1:0] === 2'd3, "after commit: chip RAM is 2 MB (memory_config[1:0] = 3)");
            check(core.sdc.sdc_img_size === (HDF_SECT != 0 ? 64'd9008283648 : 64'd4194304), "HDF size reaches the machine in bytes (MegaAGS: 9008283648)");
            if (errors == 0) $display("PASS tb_l2 cfgchk errors=0"); else $display("FAIL tb_l2 cfgchk errors=%0d", errors);
            $finish;
        end
        cw_write(17'h0A4, CHS_INIT != 0 ? CHS_INIT[7:0] : ECS ? 8'h08 : 8'h00);
        cw_write(17'h0A1, BOOT ? MEMCFG[7:0] : (MEMNB != 0 ? MEMNB[7:0] : (SLOW ? 8'h04 : 8'h01)));   // 1 MB chip (slow test: 512K chip + 512K slow)
        cw_write(17'h0A5, 8'h00);
        if (IDE) begin
            if (hdf_fd != 0) begin cw_write(17'h184, 8'hE0); cw_write(17'h185, 8'h6A); cw_write(17'h186, 8'h32); cw_write(17'h187, 8'h01); end   // 20073312 sectors (AmigaVision.hdf)
            else begin cw_write(17'h184, 8'h00); cw_write(17'h185, 8'h00); cw_write(17'h186, 8'h40); cw_write(17'h187, 8'h00); end   // 4 MB
            cw_write(17'h180, 8'h01);
        end
        if (BOOT) begin
            cw_write(17'h0A5, 8'h04); if (hdf_fd == 0) cw_write(17'h100, 8'h01);
            if (FAST) cw_write(17'h0A2, 8'h01);
            cw_write(17'h0A0, 8'h00);
            $fflush;
            $display("BOOT: released at %0t, ECS=%0d", $time, ECS);
            for (bk = 0; bk < STOPUS / DUMPUS; bk = bk + 1) begin
                #(DUMPUS * 1000.0);
                cw_read(17'h0AC);
                if (rd8[7]) dump_ppm(rd8[1:0], bk);
                $display("BOOT: %0d s, frames %0d, hdf reads %0d", (bk + 1) * DUMPUS / 1000000, core.fb.frames, hdf_reads); $fflush;
            end
            $display("BOOT: stop, frames %0d", core.fb.frames);
            $finish;
        end
        if (DISK) begin
            for (i = 0; i < 22; i = i + 1) for (j = 0; j < 512; j = j + 2) win_poke16(24'h900000 + i * 512 + j, adf_word(i, j));
            cw_write(17'h0A5, 8'h04);            // two drives
            cw_write(17'h100, 8'h01);            // DF0 present
        end
        if (DSKCHG) cw_write(17'h0A5, 8'h04);   // two drives, no disk in
        if (FAST || FASTPAT || Z3) cw_write(17'h0A2, FASTCFG[7:0]);       // fast RAM (2 = 4 MB, 3 = 5.5 MB)
        if (WINCHK != 0) begin : winset
            integer cl;
            if (FB32) for (cl = 0; cl < 3 * 131072; cl = cl + 1) ddr.mem[(32'h04000000 >> 4) + cl] = 128'd0;
            else for (cl = 0; cl < 3 * 65536; cl = cl + 1) ddr.mem[((WIN + 32'hD00000) >> 4) + cl] = 128'd0;
            if (WINCHK == 1) begin
                cw_write(17'h0D0, 8'd56); cw_write(17'h0D1, 8'd0); cw_write(17'h0D2, 8'd8); cw_write(17'h0D4, 8'd60);
                cw_write(17'h0D6, 8'd100); cw_write(17'h0D7, 8'd0);
            end else begin
                cw_write(17'h0D0, 8'd120); cw_write(17'h0D1, 8'd0);
            end
        end
        cw_write(17'h010, 8'h01);   // irq enable: frame complete
        cw_write(17'h0A0, 8'h00);   // release
        $display("machine released at %0t", $time);
        // wait for frames; show each completed one
        while (nfr < FRAMES) begin
            @(posedge cw_irq);
            cw_read(17'h014); cause = rd8;
            if (cause[0]) begin
                nfr = nfr + 1;
                cw_write(17'h014, 8'h01);
                if (DSKCHG) begin
                    // like the application's menu: the machine is frozen (pause_req), win_eject writes MOUNT=0 (even for an empty slot), the file is
                    // filled, win_mount writes MOUNT=1; then the machine runs again
                    if (nfr == 7 || nfr == 10 || nfr == 12 || nfr == 15) begin
                        pause_req = 1; while (!paused) @(posedge sys_clk);
                        cw_write(17'h100, 8'h00);
                        if (nfr != 10) begin #20000; cw_write(17'h100, 8'h01); end     // frame 10: an eject only
                        #20000; pause_req = 0;
                    end
                end
                prev_idx = last_idx;
                show_latest;
                if (nfr > 1 && last_idx == prev_idx) begin errors = errors + 1; $display("FAIL: frame %0d reused the buffer of the frame before", nfr); end
                flip_pending = 1;
                cw_read(17'h0AF);
                $display("frame %0d complete: latest %0d seq %0d at %0t", nfr, last_idx, rd8, $time);
            end
        end
        cw_read(17'h0AD); $display("shown %0d", rd8[1:0]);
        // the picture: find the stripes in the middle row of the latest buffer
        first_x = -1; last_x = -1;
        for (j = 0; j < 720; j = j + 1) begin
            px = fb_peek(last_idx, 270, j);
            if (px == 16'hFFE0) begin if (first_x < 0) first_x = j; last_x = j; end
        end
        $display("row 270: first yellow column %0d, last %0d", first_x, last_x);
        first_y = -1; last_y = -1;
        for (j = 0; j < 540; j = j + 1) begin
            px = fb_peek(last_idx, j, first_x + 1);
            if (px == 16'hFFE0 || px == 16'h0008) begin end
        end
        ycount = 0;
        for (j = 0; j < 540; j = j + 1) if (fb_peek(last_idx, j, first_x) == 16'hFFE0) begin if (first_y < 0) first_y = j; last_y = j; ycount = ycount + 1; end
        $display("column %0d: yellow rows %0d..%0d (%0d rows)", first_x, first_y, last_y, ycount);
        if (!NOPIC) begin
        check(first_x == 40, "display is centred horizontally (first stripe at column 40 of 720)");
        check(first_y == 14 && last_y == 525, "display is centred vertically (rows 14..525 of 540)");
        check(fb_peek(last_idx, 270, 1) == 16'h0008, "border colour is COLOR00 as RGB565");
        bad = 0;
        for (k = 20; k < 520; k = k + 1) for (j = 690; j < 715; j = j + 1) if (fb_peek(last_idx, k, j) != 16'h0008) bad = bad + 1;
        check(bad == 0, "right edge (columns 690..714, rows 20..519) is the border colour");
        end
        if (WINCHK != 0) begin : winchk
            integer c1, r1, fx, lx, nz, ry0, ry1;
            reg [31:0] dh, dv;
            cw_read(17'h0C0); dh[7:0] = rd8; cw_read(17'h0C1); dh[15:8] = rd8; cw_read(17'h0C2); dh[23:16] = rd8; cw_read(17'h0C3); dh[31:24] = rd8;
            cw_read(17'h0C4); dv[7:0] = rd8; cw_read(17'h0C5); dv[15:8] = rd8; cw_read(17'h0C6); dv[23:16] = rd8; cw_read(17'h0C7); dv[31:24] = rd8;
            $display("winchk: DIW_H %08x DIW_V %08x", dh, dv);
            check(dh == 32'h01C10081, "DIW_H = HDIWSTRT 0x081, HDIWSTOP 0x1C1");
            check(dv == 32'h012C002C, "DIW_V = VDIWSTRT 0x02C, VDIWSTOP 0x12C");
            if (WINCHK == 1) begin
                fx = -1; lx = -1;
                for (c1 = 0; c1 < 720; c1 = c1 + 1) if (fb_peek(last_idx, 100, c1) == 16'hFFE0) begin if (fx < 0) fx = c1; lx = c1; end
                $display("winchk 1: row 100 first yellow column %0d, last %0d", fx, lx);
                check(fx == 20, "first stripe lands at column 20 (source x 76, skip 56)");
                check(lx >= 20 && lx <= 479, "no yellow right of the window (columns 20..479)");
                nz = 0; for (c1 = 480; c1 < 720; c1 = c1 + 1) for (r1 = 0; r1 < 540; r1 = r1 + 1) if (fb_peek(last_idx, r1, c1) != 16'h0000) nz = nz + 1;
                check(nz == 0, "columns 480..719 not written (still the cleared 0)");
                ry0 = -1; ry1 = -1; nz = 0;
                for (r1 = 0; r1 < 540; r1 = r1 + 1) begin
                    if (fb_peek(last_idx, r1, 150) == 16'hFFE0) begin if (ry0 < 0) ry0 = r1; ry1 = r1; end
                    if (r1 >= 200 && fb_peek(last_idx, r1, 150) != 16'h0000) nz = nz + 1;
                end
                $display("winchk 1: column 150 yellow rows %0d..%0d", ry0, ry1);
                check(ry0 == 18 && ry1 == 199, "yellow rows 18..199 (100 lines from line 8)");
                check(nz == 0, "rows 200..539 not written");
            end else begin
                check(fb_peek(last_idx, 270, 610) == 16'h0008, "skip 120: column 610 is still the source's border colour");
                // T136 S1: COLOR00 $004 arrives with all 8 bits (0x44); the chipset's top 4 bits alone would give 0x40
                if (FB32) check(fb_peek24(last_idx, 270, 610) == 24'h000044, "T136: the border colour has 8 bits per channel (0x000044)");
                check(fb_peek(last_idx, 270, 700) == 16'h0000, "skip 120: past the source line (753 px) the pad is black");
            end
        end
        if (FAST) begin
            bad = 0; for (j = 0; j < 256; j = j + 1) if (win_peek16(24'h200000 + 2 * j) !== j[15:0]) bad = bad + 1;
            check(bad == 0, "fast RAM words written by the CPU are in the DDR2 window at +0x200000");
            check(win_peek16(24'h000104) === 16'h7F80, "the CPU read the fast RAM back (sum 0x7F80 in chip RAM)");
        end
        if (FASTPAT) begin
            $display("fastpat: RAW %0d, verify %0d, mixed %0d, code %0d, alias %0d, flag %04x (FAST_CFG %0d)", {win_peek16(24'h300), win_peek16(24'h302)},
                     {win_peek16(24'h304), win_peek16(24'h306)}, {win_peek16(24'h308), win_peek16(24'h30A)}, {win_peek16(24'h30C), win_peek16(24'h30E)},
                     {win_peek16(24'h314), win_peek16(24'h316)}, win_peek16(24'h310), FASTCFG);
            check(win_peek16(24'h310) === 16'hF457, "the pattern ROM ran to the end (flag F457)");
            $display("fastpat: CPU counter at $100 = %04x", win_peek16(24'h100));
            check({win_peek16(24'h300), win_peek16(24'h302)} === 32'd0, "fast RAM: long write read back at once, 1400 places over 5.5 MB");
            check({win_peek16(24'h304), win_peek16(24'h306)} === 32'd0, "fast RAM: the same places read after all were written (no aliasing)");
            check({win_peek16(24'h308), win_peek16(24'h30A)} === 32'd0, "fast RAM: byte / word / long / odd-address mixes");
            check({win_peek16(24'h30C), win_peek16(24'h30E)} === 32'd0, "fast RAM: code executed from each card, patched, executed again");
            check({win_peek16(24'h314), win_peek16(24'h316)} === 32'd0, "slow RAM window does not alias the fast RAM stolen from it");
        end
        if (Z3) begin : z3chk
            localparam [31:0] ZB = 32'h06000000;               // DDR2 byte address of the board
            localparam [31:0] KK = 32'hA5C3F00F;
            $display("z3: descriptor %04x %04x %04x %04x, RAW %0d, verify %0d, mixed %0d, code %0d, flag %04x", win_peek16(24'h320), win_peek16(24'h322),
                     win_peek16(24'h324), win_peek16(24'h326), {win_peek16(24'h300), win_peek16(24'h302)}, {win_peek16(24'h304), win_peek16(24'h306)},
                     {win_peek16(24'h308), win_peek16(24'h30A)}, {win_peek16(24'h30C), win_peek16(24'h30E)}, win_peek16(24'h310));
            check(win_peek16(24'h320) >> 12 === 4'hA, "AutoConfig: type nibble A = Zorro III memory board");
            check(win_peek16(24'h322) >> 12 === 4'h1, "AutoConfig: size nibble 1 = 32 MB (extended)");
            check(win_peek16(24'h324) >> 12 === 4'h0, "AutoConfig: flags nibble 0 (extended size)");
            check(win_peek16(24'h326) >> 12 === 4'hE, "AutoConfig: product number nibble E");
            check(win_peek16(24'h310) === 16'hF457, "the pattern ROM ran to the end (flag F457)");
            check({win_peek16(24'h300), win_peek16(24'h302)} === 32'd0, "Z3: 28 longs read back at once (+0, 2^k to +16 MB, last)");
            check({win_peek16(24'h304), win_peek16(24'h306)} === 32'd0, "Z3: same places after all were written (no bit lost)");
            check({win_peek16(24'h308), win_peek16(24'h30A)} === 32'd0, "Z3: byte/word/long/odd mixes, board end, 8 and 16 MB edges");
            check({win_peek16(24'h30C), win_peek16(24'h30E)} === 32'd0, "Z3: code run from the board, patched, run again");
            check({ddr_peek16(ZB), ddr_peek16(ZB + 2)} === (32'h40000000 ^ KK), "DDR2 holds the first longword at 0x6000000");
            check({ddr_peek16(ZB + 32'h1000000), ddr_peek16(ZB + 32'h1000002)} === (32'h41000000 ^ KK), "DDR2 holds the middle longword at 0x7000000");
            check({ddr_peek16(ZB + 32'h1FFFFFC), ddr_peek16(ZB + 32'h1FFFFFE)} === (32'h41FFFFFC ^ KK), "DDR2 holds the last longword at 0x7FFFFFC");
        end
        if (DSKCHG) begin : dskchg_check
            integer n, k, ins, bad, first_ins, none_before, run, v, pv;
            n = win_peek16(24'h000102);
            ins = 0; bad = 0; first_ins = -1; none_before = 0; run = 0; pv = -1;
            for (k = 0; k < n && k < 512; k = k + 1) begin
                v = win_peek16(24'h400 + 2 * k);
                if (v == 16'h0001) begin ins = ins + 1; if (first_ins < 0) first_ins = k; end
                if (v != 16'h0000 && v != 16'h0001 && v != 16'h0101) bad = bad + 1;
                if (first_ins < 0 && v == 16'h0000) none_before = none_before + 1;
                if (v == pv) run = run + 1;
                else begin if (pv >= 0) $display("dskchg: %04x x%0d", pv, run); pv = v; run = 1; end
            end
            if (pv >= 0) $display("dskchg: %04x x%0d", pv, run);
            $display("dskchg: %0d polls, disk noticed %0d times, first at poll %0d, %0d empty polls before it", n, ins, first_ins, none_before);
            check(n >= 60, "the ROM polled the drive (>= 60 polls)");
            check(bad == 0, "polls read 0000, 0001 (a step released it) or 0101 only");
            check(none_before >= 5 && first_ins == none_before, "no disk: /DSKCHG stays low through the steps, until the mount");
            check(ins == 3, "disk noticed 3 times: insert, re-insert, replace (frozen)");
        end
        if (DISK) begin : disk_check
            integer base, sync_at, nsync, sec, trk, bad2, jj;
            reg [31:0] info, od, ev, dl;
            check(win_peek16(24'h000106) === 16'hD15C, "disk DMA finished (DSKBLK seen by the ROM)");
            nsync = 0; sync_at = -1;
            for (jj = 0; jj < 6400; jj = jj + 1) if (win_peek16(24'h20000 + 2 * jj) === 16'h4489) begin
                nsync = nsync + 1;
                if (sync_at < 0 && win_peek16(24'h20000 + 2 * (jj + 1)) === 16'h4489) sync_at = jj;
            end
            $display("track buffer: %0d sync words, first pair at word %0d", nsync, sync_at);
            check(nsync >= 20, "the track buffer holds the 11 sectors (22 sync words)");
            base = sync_at + 2;
            od = {win_peek16(24'h20000 + 2 * base), win_peek16(24'h20000 + 2 * (base + 1))};
            ev = {win_peek16(24'h20000 + 2 * (base + 2)), win_peek16(24'h20000 + 2 * (base + 3))};
            info = demfm(od, ev);
            sec = (info >> 8) & 255; trk = (info >> 16) & 255;
            $display("first sector in the buffer: track %0d sector %0d (info %08x)", trk, sec, info);
            check(trk == 0 && sec < 11, "sector header decodes to track 0");
            bad2 = 0;
            for (jj = 0; jj < 128; jj = jj + 1) begin
                od = {win_peek16(24'h20000 + 2 * (base + 28 + 2 * jj)), win_peek16(24'h20000 + 2 * (base + 29 + 2 * jj))};
                ev = {win_peek16(24'h20000 + 2 * (base + 28 + 256 + 2 * jj)), win_peek16(24'h20000 + 2 * (base + 29 + 256 + 2 * jj))};
                dl = demfm(od, ev);
                if (dl !== {adf_word(sec, 4 * jj), adf_word(sec, 4 * jj + 2)}) bad2 = bad2 + 1;
            end
            check(bad2 == 0, "the sector's 512 data bytes decode to the ADF's bytes");
        end
        if (MOTORCHK) begin : motor_check
            check(mot_low_first === 1'b1, "drv_led low after reset, before the ROM runs");
            check(mot_seen === 1'b1, "drv_led high after the ROM: motor on + select DF0");
        end
        if (IDE) begin : ide_check
            integer jj, bad3;
            check(win_peek16(24'h000108) === 16'hD15D, "the ROM read a sector from the hard disk (PIO, DRQ seen)");
            bad3 = 0;
            for (jj = 0; jj < 4; jj = jj + 1)
                if (win_peek16(24'h000200 + 2 * jj) !== {hdf_byte(5, 2 * jj), hdf_byte(5, 2 * jj + 1)}) bad3 = bad3 + 1;
            $display("IDE: %0d mailbox reads, first words %04x %04x", hdf_reads, win_peek16(24'h200), win_peek16(24'h202));
            check(bad3 == 0, "the four words read from the IDE data port are LBA 5's first bytes");
        end
        // keyboard: A key (col 1 row 2 = index 10) down and up
        keys[10] = 1'b1;
        #3000000;
        keys[10] = 1'b0;
        #3000000;
        if (SLOW) begin : slow_check
            integer jj, bad4;
            bad4 = 0; for (jj = 0; jj < 4096; jj = jj + 1) if (win_peek16(24'h600000 + 2 * jj) !== jj[15:0]) bad4 = bad4 + 1;
            check(bad4 == 0, "slow RAM $C00000: 4096 words written by the CPU are in the window at +0x600000");
            check(win_peek16(24'h000104) === 16'hF800, "the CPU read the 4096 words back (sum F800)");
            bad4 = 0; for (jj = 0; jj < 128; jj = jj + 1) if (win_peek16(24'h610000 + 2 * jj) !== {8'(2 * jj), 8'(2 * jj + 1)}) bad4 = bad4 + 1;
            check(bad4 == 0, "slow RAM byte writes: even byte = high byte of the word");
            check(win_peek16(24'h000106) === 16'hBEEF, "code written into slow RAM and run from there (jsr $C20000)");
            bad4 = 0; for (jj = 0; jj < 16; jj = jj + 1) if (win_peek16(24'h008000 + 2 * jj) !== jj[15:0]) bad4 = bad4 + 1;
            check(bad4 == 0, "long moves from slow RAM to chip RAM");
        end
        if (AUDIO) begin
            check(al > 24'sd100000 && ar == 24'sd0, "audio channel 0 plays on the left side only");
            $display("audio: left %0d right %0d", al, ar);
            // the audio KPIs through the register window: STAT_SEL 8 {over, under}, 9 {fmax, fmin}, 10 fill (FEATURES b1 says they exist)
            cw_read(17'h00C); check(rd8[1] === 1'b1, "FEATURES b1: the audio KPIs are there");
            begin : kpi
                reg [31:0] v8, v9, v10; reg [10:0] mn0, mx0, mn1, mx1; integer swap;
                swap = $test$plusargs("kpi_swap");                 // negative control: expect the wrong word, must FAIL
                mn0 = core.aud.st_fmin; mx0 = core.aud.st_fmax;
                cw_write(17'h0B8, 8'h08); #20000;
                cw_read(17'h0BC); v8[7:0] = rd8; cw_read(17'h0BD); v8[15:8] = rd8; cw_read(17'h0BE); v8[23:16] = rd8; cw_read(17'h0BF); v8[31:24] = rd8;
                cw_write(17'h0B8, 8'h09); #20000;
                cw_read(17'h0BC); v9[7:0] = rd8; cw_read(17'h0BD); v9[15:8] = rd8; cw_read(17'h0BE); v9[23:16] = rd8; cw_read(17'h0BF); v9[31:24] = rd8;
                cw_write(17'h0B8, 8'h0A); #20000;
                cw_read(17'h0BC); v10[7:0] = rd8; cw_read(17'h0BD); v10[15:8] = rd8; cw_read(17'h0BE); v10[23:16] = rd8; cw_read(17'h0BF); v10[31:24] = rd8;
                mn1 = core.aud.st_fmin; mx1 = core.aud.st_fmax;
                $display("audio KPI via registers: under %0d over %0d fmin %0d fmax %0d fill %0d (hierarchy: under %0d over %0d fmin %0d fmax %0d fill %0d)",
                         v8[15:0], v8[31:16], v9[10:0], v9[26:16], v10[10:0], core.aud.st_under, core.aud.st_over, mn1, mx1, core.aud.st_fill);
                cw_read(17'h0B8); check(rd8[3:0] === 4'd10, "STAT_SEL reads back 4 bits");
                check(swap ? (v8[15:0] === {5'd0, mx1}) : (v8[15:0] === core.aud.st_under && v8[31:16] === core.aud.st_over), "sel 8: under / over equal the FIFO's counters");
                check(v9[10:0] <= mn0 && v9[10:0] >= mn1 && v9[26:16] >= mx0 && v9[26:16] <= mx1, "sel 9: min / max fill lie between the FIFO's values before and after");
                check(v9[26:16] >= v9[10:0] && v9[26:16] > 0, "sel 9: max >= min and the FIFO ran");
                check(v10[31:11] == 0 && v10[10:0] <= 11'd1023, "sel 10: fill is an 11 bit value");
            end
        end
        if (KEY) begin
            check(win_peek16(24'h00010C)[15:8] === 8'hBF, "the CPU read the key A from the CIA serial register (BF = not rotated 20)");
        end
        if (C020) begin
            $display("cpu020: EXTB.L %04x%04x, MULU.L %04x%04x, BFEXTU %04x%04x, flag %04x", win_peek16(24'h200), win_peek16(24'h202),
                     win_peek16(24'h204), win_peek16(24'h206), win_peek16(24'h20C), win_peek16(24'h20E), win_peek16(24'h208));
            check(win_peek16(24'h208) === 16'h020F, "the 68020-only instructions ran to the end, no illegal-instruction exception (flag 020F)");
            check({win_peek16(24'h200), win_peek16(24'h202)} === 32'hFFFFFF80, "EXTB.L D0 sign-extends a byte to a long");
            check({win_peek16(24'h204), win_peek16(24'h206)} === 32'h24101100, "MULU.L D1,D2 gives the low 32 bit of 70000 * 70000");
            check({win_peek16(24'h20C), win_peek16(24'h20E)} === 32'h00000080, "BFEXTU D0{24:8},D3 extracts the low byte");
        end
        $display("PASS tb_l2 errors=%0d", errors);
        $finish;
    end

    // keyboard / mouse event monitor
    always @(core.kml) if (core.kmt == 2'd2 || core.kmt == 2'd0 || core.kmt == 2'd1) $display("kbd/mouse event type %0d data %02x at %0t", core.kmt, core.kmd, $time);
    always @(posedge sys_clk) if (BOOT && cyc % 200000 == 0) begin $fflush; end
    // BOOT: every simulated 500 ms the exec memory list (MaxLocMem and the MemHeaders), to compare OCS and ECS
    function [31:0] ld(input [23:0] a);
        ld = {win_peek16(a), win_peek16(a + 2)};
    endfunction
    integer dn;
    reg [31:0] eb, node;
    always @(posedge sys_clk) if (BOOT && cyc % 15000000 == 0 && cyc > 0) begin
        eb = ld(24'h000004);
        $display("t=%0t ms: ExecBase %08x MaxLocMem %08x vblank frames %0d", $time / 1000000, eb, (eb > 0 && eb < 24'h80000) ? ld(eb + 24'h3E) : 32'hx, core.fb.frames);
        if (eb > 0 && eb < 24'h80000) begin
            node = ld(eb + 24'h142);
            for (dn = 0; dn < 5 && node != 0 && node < 24'h800000; dn = dn + 1) begin
                if (ld(node) != 0)
                    $display("   MemHeader %08x attr %04x lower %08x upper %08x", node, win_peek16(node + 14), ld(node + 20), ld(node + 24));
                node = ld(node);
            end
        end
        $fflush;
    end
    // BOOT: every simulated second the memory path's counters (T85 rate work): reads/misses/writes/prefetches and held clocks of the
    // second that just ended, the machine's frames, and (new amiga_ce) the deepest catch-up debt and the ticks lost to it
    integer b_r = 0, b_m = 0, b_w = 0, b_p = 0, b_h = 0, b_f = 0;
    always @(posedge sys_clk) if (BOOT && cyc % 31527800 == 0 && cyc > 0) begin
`ifdef HAVE_DEBT
        $display("bootstat t=%0d s: reads %0d miss %0d writes %0d prefetch %0d held clocks %0d frames %0d; debt max %0d lost %0d", cyc / 31527800,
            core.s_reads - b_r, core.s_miss - b_m, core.s_writes - b_w, core.s_pf - b_p, core.s_hold - b_h, core.fb.frames - b_f, core.s_debt[11:0], core.s_debt[31:16]);
`else
        $display("bootstat t=%0d s: reads %0d miss %0d writes %0d prefetch %0d held clocks %0d frames %0d", cyc / 31527800,
            core.s_reads - b_r, core.s_miss - b_m, core.s_writes - b_w, core.s_pf - b_p, core.s_hold - b_h, core.fb.frames - b_f);
`endif
        b_r = core.s_reads; b_m = core.s_miss; b_w = core.s_writes; b_p = core.s_pf; b_h = core.s_hold; b_f = core.fb.frames;
        $fflush;
    end
    // BOOT: a ring of the CPU's chip-bus read addresses; report the exception vectors 2/3 (bus / address error)
    reg [23:1] ring [0:63]; integer rp = 0;
    always @(posedge sys_clk) if (BOOT && core.ce28 && core.clk7_en && !core.n_oe) begin
        ring[rp % 64] <= core.ram_address; rp <= rp + 1;
        if (core.ram_address == 23'h000006 || core.ram_address == 23'h000007 || core.ram_address == 23'h000004 || core.ram_address == 23'h000005) begin
            $display("VECTOR 2/3 read at %0t (word %06x); last reads:", $time, core.ram_address);
            for (k = 1; k <= 24; k = k + 1) $write(" %06x", ring[(rp + 64 - k) % 64]);
            $display("");
        end
    end
`include "tb_pause_mon.svh"
`include "tb_shadow_mon.svh"
    initial begin #(BOOT ? 9000000000000.0 : (PAUSE ? 20000000000.0 : 2000000000.0)); $display("FAIL timeout"); $finish; end
endmodule
