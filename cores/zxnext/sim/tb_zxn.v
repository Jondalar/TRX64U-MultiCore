// T116: benches of the ZX Spectrum Next core (zxn_core with cores/zxnext/gen, the DDR2 models, an application model that serves the SD
// mailbox from an image file the way coreapp/src/sectors.cc does, through the register block like the real application).
// The Next boots its own boot ROM from the image: TBBLUE.FW is the bench program (assembled by tools/zxnext_asm.py from cores/zxnext/sim/prog/*.s,
// the image made by tools/zxnext_progs.py), loaded at 0x6000 and started by the boot ROM exactly as the real firmware is. A program reports
// through the machine's RAM (physical 0x48000 = CPU 0x8000, the second 16 KB bank the boot ROM leaves mapped), which the bench reads out of the
// DDR2 model, and through port writes, which the bench logs. One mode per run (+MODE=...); a mode ends with one PASS or FAIL line.
//   plusargs: +MODE=<mode> +IMG=<image file> +MS=<machine milliseconds at most> [+NOIMG] [+PLOG] ...
`timescale 1ns / 1ps
module tb_zxn;
    reg cpu_clk = 0, ui_clk = 0, sys_clk = 0, mclk = 0;
    always #5 cpu_clk = ~cpu_clk;
    always #6.6667 ui_clk = ~ui_clk;
    always #15.86 sys_clk = ~sys_clk;
    always #40.69 mclk = ~mclk;
    reg sys_rst = 1, ui_rst = 1;

    // ---------------------------------------------------------------- the device under test
    reg  [4:0]  joy1 = 5'd0, joy2 = 5'd0;
    reg  [63:0] keys = 64'd0;
    reg         restore = 1'b0;
    reg         halt_req = 1'b0, reset_req = 1'b0;
    reg         cw_we = 0, cw_re = 0;
    reg  [16:0] cw_addr = 0;
    reg  [7:0]  cw_wdata = 0;
    wire [7:0]  cw_rdata;
    wire        cw_ack, cw_irq, halted, running;
    wire [26:0] c_addr, e_addr;
    wire [2:0]  c_cmd, e_cmd;
    wire        c_en, e_en, c_rdy, e_rdy, c_wdf_wren, e_wdf_wren, c_rd_valid;
    wire [127:0] c_wdf_data, e_wdf_data, c_rd_data;
    wire [15:0] c_wdf_mask, e_wdf_mask;
    wire signed [23:0] audio_l, audio_r;
    wire [3:0]  pix_color;
    wire [9:0]  pix_x;
    wire [8:0]  pix_y;
    zxn_core #(.ID_TRX2(0)) dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(reset_req), .halt_req(halt_req), .halted(halted), .running(running),
        .dma_req_tgl(1'b0), .dma_addr(16'd0), .dma_we(1'b0), .dma_wdata(8'd0), .dma_rom(1'b0), .dma_done_tgl(), .dma_rdata(), .dbg_busy(1'b0),
        .win_port_use(), .win_port_addr(), .win_port_rom(), .win_port_we(), .win_port_wdata(),
        .c64_joy1(joy1), .c64_joy2(joy2), .c64_keys(keys), .c64_restore(restore),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'h12345678), .ddr_cal(64'd0),
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid)
    );
    zxn_ddr2c cm (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
                  .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));
    zxn_ddr2e em (.ui_clk(ui_clk), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_wren(e_wdf_wren));

    // ---------------------------------------------------------------- the application's bus
    task cw_wr(input [16:0] a, input [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr <= a; cw_wdata <= d; cw_we <= 1'b1;
            @(posedge cpu_clk); cw_we <= 1'b0;
        end
    endtask
    task cw_rd(input [16:0] a, output [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr <= a; cw_re <= 1'b1;
            @(posedge cpu_clk); cw_re <= 1'b0;
            @(posedge cpu_clk); #1 d = cw_rdata;
        end
    endtask
    task cw_wr32(input [16:0] a, input [31:0] v);
        begin cw_wr(a, v[7:0]); cw_wr(a + 1, v[15:8]); cw_wr(a + 2, v[23:16]); cw_wr(a + 3, v[31:24]); end
    endtask

    // ---------------------------------------------------------------- the image and the application's sector service
    parameter integer IMGSZ = 8 * 1024 * 1024;       // run.sh's `fw` mode builds its own binary with -GIMGSZ for the card image of tools/zxnext_fwimg.py
    reg [7:0] img [0:IMGSZ - 1] /*verilator public_flat_rw*/;
    reg [7:0] img0 [0:IMGSZ - 1];                    // the image as loaded, to see what the machine changed
    // A card image that is not in memory as a whole: the sectors the firmware reads (tools/zxnext_fwmap.py) with their LBAs, found by binary search; every
    // other sector reads as zeros and is printed once as MISS (the bench's way of saying which sector the map lacks)
    parameter integer MAPN = 16384;
    reg [31:0] mkey [0:MAPN - 1];
    reg [7:0] mdat [0:MAPN * 512 - 1];
    integer mcount = 0, mlo, mhi, mmid;
    reg use_map = 1'b0;
    function integer mfind(input [31:0] lba);
        integer lo, hi, mid, r;
        begin
            lo = 0; hi = mcount - 1; r = -1;
            while (lo <= hi && r < 0) begin
                mid = (lo + hi) / 2;
                if (mkey[mid] == lba) r = mid;
                else if (mkey[mid] < lba) lo = mid + 1;
                else hi = mid - 1;
            end
            mfind = r;
        end
    endfunction
    integer mi;
    integer served_r = 0, served_w = 0;
    reg [7:0] st_b;
    reg [31:0] lba_b;
    integer ii, jj;
    reg [127:0] wtmp;
    reg [31:0] last_lba_r = 32'hFFFFFFFF, last_lba_w = 32'hFFFFFFFF;
    reg dropwr = 1'b0;                              // control: the application does not write a block back into the file
    // The application's service as sectors.cc does it: read the status (its request tag) and the LBA, do the file work (+LAT=<us> of it), then write the
    // staging sector and answer with the tag it read. One request at a time; the bench calls this every 50 us.
    integer lat_ns = 0, far_ns = 0, far_lba = 0;
    time far_after = 0;
    integer step_ns = 50000;                         // +STEP=<ns>: how often the bench runs the application's service
    reg app_busy = 1'b0, app_w;
    reg [7:0] app_tag;
    reg [31:0] app_lba;
    time app_due;
    task serve_one;
        begin
            if (!app_busy) begin
                cw_rd(17'h1A0, st_b);
                if (st_b[0]) begin
                    cw_rd(17'h1A4, lba_b[7:0]); cw_rd(17'h1A5, lba_b[15:8]); cw_rd(17'h1A6, lba_b[23:16]); cw_rd(17'h1A7, lba_b[31:24]);
                    app_lba = lba_b; app_tag = st_b & 8'hF0; app_w = st_b[1];
                    app_busy = 1'b1; app_due = $time + ((far_lba != 0 && lba_b >= far_lba && $time >= far_after) ? far_ns : lat_ns);     // +FARLBA / +FARLAT / +FARAFTER (ms): a request far into the image takes longer, once the firmware runs
                end
            end
            if (app_busy && $time >= app_due) begin
                if (app_w) begin                                    // write: staging sector -> image
                    for (ii = 0; ii < 32 && !dropwr && !use_map; ii = ii + 1) begin
                        wtmp = cm.mem[19'h20000 + ii];
                        for (jj = 0; jj < 16; jj = jj + 1) img[app_lba * 512 + ii * 16 + jj] = wtmp[8 * jj +: 8];
                    end
                    served_w = served_w + 1; last_lba_w = app_lba;
                end else begin                                      // read: image -> staging sector
                    mi = use_map ? mfind(app_lba) : -1;
                    if (use_map && mi < 0) $display("MISS %0d", app_lba);
                    for (ii = 0; ii < 32; ii = ii + 1) begin
                        for (jj = 0; jj < 16; jj = jj + 1)
                            wtmp[8 * jj +: 8] = use_map ? ((mi >= 0) ? mdat[mi * 512 + ii * 16 + jj] : 8'd0)
                                                        : ((app_lba * 512 + ii * 16 + jj < IMGSZ) ? img[app_lba * 512 + ii * 16 + jj] : 8'd0);
                        cm.mem[19'h20000 + ii] = wtmp;
                    end
                    served_r = served_r + 1; last_lba_r = app_lba;
                end
                cw_wr(17'h1A8, app_tag);
                app_busy = 1'b0;
            end
        end
    endtask

    // ---------------------------------------------------------------- helpers for the benches
    function [7:0] ramb(input [20:0] a);             // a byte of the machine's address space as the DDR2 model holds it
        reg [127:0] w;
        begin w = cm.mem[a >> 4]; ramb = w[8 * a[3:0] +: 8]; end
    endfunction
    task wait_us(input integer us);                  // machine time with the application service running
        integer k;
        begin
            for (k = 0; k < us / (step_ns / 1000); k = k + 1) begin #(step_ns); serve_one; end
        end
    endtask
    reg marker_ok;
    task wait_marker(input [20:0] a, input [7:0] v, input integer ms);   // until RAM[a] == v (the program's "done"), at most ms
        integer k;
        begin
            marker_ok = 1'b0;
            for (k = 0; k < ms * (1000 / (step_ns / 1000)) && !marker_ok; k = k + 1) begin
                #(step_ns); serve_one;
                if (ramb(a) == v) marker_ok = 1'b1;
            end
        end
    endtask
    integer r28n = 0, cpun = 0;                       // machine time: master cycles and CPU clock edges since the start
    always @(posedge cpu_clk) begin
        if (dut.r28) r28n <= r28n + 1;
        if (dut.cpu_r) cpun <= cpun + 1;
    end
    integer ccnt = 0;
    always @(posedge cpu_clk) ccnt <= ccnt + 1;
    reg [31:0] hash_h = 32'h811C9DC5;
    task hash_range(input [20:0] start, input [20:0] len);
        integer q;
        begin
            for (q = 0; q < len; q = q + 1) hash_h = (hash_h ^ {24'd0, ramb(start + q)}) * 32'h01000193;
        end
    endtask
    // the frame buffer as the host's pixel layer reads it: 640 x 512 RGB565, four buffers of 1 MB
    function [15:0] fbpix(input [1:0] bb, input [9:0] col, input [9:0] row);
        reg [127:0] w;
        integer ix;
        begin ix = row * 640 + col; w = em.fb[bb * 65536 + ix / 8]; fbpix = w[16 * (ix % 8) +: 16]; end
    endfunction
    function [15:0] c565(input [7:0] idx);            // the Layer 2 identity palette: RGB332 index -> RGB333 (blue's low bit = b1 or b0) -> the core's RGB565
        reg [8:0] rgb;
        begin rgb = {idx, idx[1] | idx[0]}; c565 = {rgb[8:6], rgb[8:7], rgb[5:3], rgb[5:3], rgb[2:0], rgb[2:1]}; end
    endfunction
    // the keys bench: the program passes over its samples again and again; a sample taken after a change of the input is one that counts when two passes
    // have finished since
    integer kchecks = 0, kbad = 0;
    task settle;
        integer k;
        reg [7:0] c0, c1;
        begin
            c0 = ramb(21'h48120);
            c1 = c0;
            for (k = 0; k < 400 && c1 != c0 + 8'd3; k = k + 1) begin #20000; serve_one; c1 = ramb(21'h48120); end
        end
    endtask
    task chk8(input [20:0] a, input [7:0] want, input [8*40-1:0] what);
        begin
            kchecks = kchecks + 1;
            if (ramb(a) !== want) begin kbad = kbad + 1; $display("  %0s: %02X, want %02X", what, ramb(a), want); end
        end
    endtask
    task verdict(input ok, input [8*160-1:0] what);
        begin
            if (ok) $display("PASS %0s", what); else $display("FAIL %0s", what);
        end
    endtask

    // ---------------------------------------------------------------- the machine's port writes, logged for the equivalence bench
    wire io_wr = ~dut.zxn.cpu_iorq_n & ~dut.zxn.cpu_wr_n & dut.zxn.cpu_rd_n;
    reg  io_wr_q = 1'b0;
    reg  plog = 1'b0;
    integer plog_n = 0, t0 = 0, stall0 = 0, miss0 = 0, pc0 = 0;
    always @(posedge cpu_clk) begin
        io_wr_q <= io_wr;
        if (plog && io_wr && !io_wr_q && dut.zxn.cpu_a[7:0] != 8'hFE && dut.zxn.cpu_a != 16'h243B && dut.zxn.cpu_a != 16'h253B
            && dut.zxn.cpu_a[7:0] != 8'hEB && dut.zxn.cpu_a[7:0] != 8'hE7) begin
            if (plog_n == 0) begin t0 = r28n; stall0 = dut.mem.st_stall; miss0 = dut.mem.st_miss_a; pc0 = ccnt; end
            plog_n = plog_n + 1;
            $display("P %0d %04X %02X", r28n - t0, dut.zxn.cpu_a, dut.zxn.cpu_do);
        end
    end

    // the audio bench: the host's samples (one every 2112 cpu_clk, the rate of zxn_audio) and the time of every phase mark the program writes to port 0x77
    reg signed [23:0] smp_l [0:131071];
    reg signed [23:0] smp_r [0:131071];
    integer nsmp = 0, acnt = 0;
    integer ph_t [0:15], ph_r [0:15], ph_c [0:15];
    integer nfb = 0, cfb0 = 0, cfb1 = 0, nfe = 0, cfe0 = 0, cfe1 = 0;
    initial begin : phinit integer q; for (q = 0; q < 16; q = q + 1) ph_t[q] = -1; end
    always @(posedge cpu_clk) begin
        acnt <= (acnt == 2111) ? 0 : acnt + 1;
        if (acnt == 2111 && nsmp < 131072) begin smp_l[nsmp] <= audio_l; smp_r[nsmp] <= audio_r; nsmp <= nsmp + 1; end
        if (io_wr && !io_wr_q && dut.zxn.cpu_a[7:0] == 8'h77) begin ph_t[dut.zxn.cpu_do[3:0]] <= nsmp; ph_r[dut.zxn.cpu_do[3:0]] <= r28n; ph_c[dut.zxn.cpu_do[3:0]] <= ccnt; end
        if (io_wr && !io_wr_q && dut.zxn.cpu_a[7:0] == 8'hFB) begin if (nfb == 0) cfb0 <= ccnt; cfb1 <= ccnt; nfb <= nfb + 1; end
        if (io_wr && !io_wr_q && dut.zxn.cpu_a[7:0] == 8'hFE && ph_t[5] >= 0 && ph_t[6] < 0) begin if (nfe == 0) cfe0 <= ccnt; cfe1 <= ccnt; nfe <= nfe + 1; end
    end
    function [15:0] crc16b(input [15:0] c, input [7:0] d);
        integer i;
        reg [15:0] x;
        begin
            x = c ^ {d, 8'h00};
            for (i = 0; i < 8; i = i + 1) x = x[15] ? ((x << 1) ^ 16'h1021) : (x << 1);
            crc16b = x;
        end
    endfunction
    reg spilog = 1'b0;
    always @(posedge cpu_clk) if (spilog && dut.sd.byte_ev && dut.sd.st >= 5'd9 && dut.sd.st <= 5'd14)
        $display("%0t: SPI st %0d in %02X -> next out %02X", $time, dut.sd.st, dut.sd.din, dut.sd.txn);
    always @(posedge dut.sd.mb_pend or negedge dut.sd.mb_pend) if (spilog) $display("%0t: mb_pend %b wr %b lba %0d", $time, dut.sd.mb_pend, dut.sd.mb_wr, dut.sd.mb_lba);
    always @(posedge dut.sd.sd_start) if (spilog) $display("%0t: sd_start dir %b", $time, dut.sd.sd_to_ddr);
    always @(posedge dut.sd.sd_done) if (spilog) $display("%0t: sd_done", $time);
    reg rdlog = 1'b0, fe_rd_q = 1'b0;
    always @(posedge cpu_clk) begin
        fe_rd_q <= dut.zxn.port_fe_rd;
        if (rdlog && dut.zxn.port_fe_rd && !fe_rd_q) $display("%0t: IN FE a=%04X data=%02X kbd_col=%02X kbd_row=%02X keys=%h", $time, dut.zxn.cpu_a, dut.zxn.port_fe_dat, dut.kbd_col, dut.kbd_row, keys);
    end
    // ---------------------------------------------------------------- the card's commands and the addresses the `fw` mode watches
    integer pc_ok_n = 0, pc_err_n = 0;
    reg [15:0] pc_ok = 16'hFFFF, pc_err = 16'hFFFF;
    reg cmdlog = 1'b0;
    reg [7:0] lc_q = 8'd0;
    reg m1_q = 1'b0;
    always @(posedge cpu_clk) begin
        m1_q <= ~dut.zxn.cpu_m1_n & ~dut.zxn.cpu_mreq_n;
        if (~dut.zxn.cpu_m1_n & ~dut.zxn.cpu_mreq_n & ~m1_q) begin
            if (dut.zxn.cpu_a == pc_ok) pc_ok_n <= pc_ok_n + 1;
            if (dut.zxn.cpu_a == pc_err) pc_err_n <= pc_err_n + 1;
        end
        if (cmdlog &&(dut.sd.st_last_cmd !== lc_q)) begin
            lc_q <= dut.sd.st_last_cmd;
            $display("%0t: CMD%0d arg lba %0d", $time, dut.sd.st_last_cmd[6:0], dut.sd.mb_lba);
        end
    end
    // ---------------------------------------------------------------- run control
    reg [1023:0] mode_s, img_s;
    integer fd, n, ms_limit;
    reg have_img = 1'b0;
    reg [7:0] t8;
    initial begin
        if (!$value$plusargs("MODE=%s", mode_s)) mode_s = "boot";
        if (!$value$plusargs("MS=%d", ms_limit)) ms_limit = 1500;
        if ($value$plusargs("IMG=%s", img_s)) begin
            fd = $fopen(img_s, "rb");
            n = $fread(img, fd);
            $fclose(fd);
            for (n = 0; n < IMGSZ; n = n + 1) img0[n] = img[n];
            have_img = 1'b1;
        end
        if ($value$plusargs("MAPK=%s", img_s)) begin
            fd = $fopen(img_s, "rb");
            mcount = $fread(mkey, fd) / 4;                      // bytes read; four to a key
            $fclose(fd);
            if ($value$plusargs("MAPD=%s", img_s)) begin
                fd = $fopen(img_s, "rb");
                n = $fread(mdat, fd);
                $fclose(fd);
            end
            use_map = 1'b1;
            have_img = 1'b1;
        end
        #200; sys_rst = 0; ui_rst = 0;
        #1000;
        if (have_img && !$test$plusargs("NOIMG")) begin
            if ($value$plusargs("SECTORS=%d", n)) cw_wr32(17'h184, n); else cw_wr32(17'h184, 32'd16384);
            cw_wr(17'h180, 8'd1);
        end
        plog = $test$plusargs("PLOG");
        spilog = $test$plusargs("SPILOG");
        dropwr = $test$plusargs("DROPWR");
        if ($value$plusargs("LAT=%d", n)) lat_ns = n * 1000;
        if ($value$plusargs("FARLBA=%d", n)) far_lba = n;
        if ($value$plusargs("FARAFTER=%d", n)) far_after = n * 1000000;
        if ($value$plusargs("FARLAT=%d", n)) far_ns = n * 1000;
        if ($value$plusargs("STEP=%d", n)) step_ns = n;
        rdlog = $test$plusargs("RDLOG");
        cmdlog = $test$plusargs("CMDLOG");
        if ($value$plusargs("PCOK=%h", n)) pc_ok = n;
        if ($value$plusargs("PCERR=%h", n)) pc_err = n;
        if ($value$plusargs("BOARDID=%d", n)) begin                // T116 D10: the app's board issue, written while the core is held, before the boot ROM runs
            cw_wr(17'h0A3, n[7:0]);
            cw_rd(17'h0A3, t8);
            if (t8 != n[7:0]) $display("  BOARD_ID reads back %02X, wrote %02X", t8, n[7:0]);
        end
        cw_wr(17'h0A0, 8'd0);                                      // release the reset hold
        if (mode_s == "boot") begin
            // S10: the boot ROM runs, finds the card, reads TBBLUE.FW and starts it (the program writes A5 5A at CPU 0x8000)
            wait_marker(21'h48001, 8'h5A, ms_limit);
            if (marker_ok && ramb(21'h48000) == 8'hA5) verdict(1, "boot: the stub started and left its marker");
            else verdict(0, "boot: the stub did not start");
            $display("  sd reads %0d (last lba %0d), writes %0d, served r%0d w%0d", dut.sd.st_reads, last_lba_r, dut.sd.st_writes, served_r, served_w);
        end else if (mode_s == "noimg") begin
            // S10, no image: the boot ROM cannot find a card and stops with its error screen (text in the display RAM), the stub never starts
            wait_us(ms_limit * 1000);
            begin : cnt
                integer z, nz;
                nz = 0;
                for (z = 0; z < 6144; z = z + 1) if (dut.zxn.bank5_ram.ram_q[z] != 8'd0) nz = nz + 1;
                $display("  display RAM pixel bytes set: %0d, sd reads %0d, bootrom_en %b, marker %02X", nz, dut.sd.st_reads, dut.zxn.bootrom_en, ramb(21'h48000));
                if (nz > 100 && dut.sd.st_reads == 0 && dut.zxn.bootrom_en && ramb(21'h48000) != 8'hA5) verdict(1, "noimg: the boot ROM stopped with its error screen");
                else verdict(0, "noimg: the boot ROM did not stop with an error screen");
            end
        end else if (mode_s == "mem") begin
            // S3: the program of prog/mem.s runs; the bench prints the machine's final memory as a hash and the machine's port writes (P lines, in master
            // cycles after the first); run.sh compares them with the run on the one-cycle SRAM model
            wait_marker(21'h48001, 8'h5A, ms_limit);
            wait_us(3000);
            // the memory the program used: its results page, the pages 20/21, 30, 40/41, 50, the first 256 bytes of the banks it marked through 0x7FFD (not the
            // boot ROM's stack in bank 0 and what it leaves in the other banks: they depend on how long the card took to answer, which the bench's clock decides)
            begin : memh
                integer w, bb;
                reg [31:0] h;
                h = 32'h811C9DC5;
                hash_range(21'h48000, 21'h1000);
                hash_range(21'h68000, 21'h4000);
                hash_range(21'h7C000, 21'h0100);
                hash_range(21'h90000, 21'h4000);
                hash_range(21'hA4000, 21'h0100);
                for (bb = 1; bb < 8; bb = bb + 1) hash_range(21'h40000 + bb * 21'h4000, 21'h0100);
                $display("RAMHASH %08X", hash_h);
            end
            $display("  results: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", ramb(21'h48002), ramb(21'h48003), ramb(21'h48004), ramb(21'h48005),
                     ramb(21'h48006), ramb(21'h48007), ramb(21'h48008), ramb(21'h48009), ramb(21'h4800A), ramb(21'h4800B), ramb(21'h48000));
            $display("  the program: %0d cpu_clk (%0d running + %0d held for the DDR2 in %0d line fetches); %0d port writes",
                     ccnt - pc0, ccnt - pc0 - (dut.mem.st_stall - stall0), dut.mem.st_stall - stall0, dut.mem.st_miss_a - miss0, plog_n);
            $display("  held for memory: %0d of %0d clocks, line fetches %0d (Layer 2 %0d), ddr2 writes %0d", dut.mem.st_stall, $time / 10, dut.mem.st_miss_a, dut.mem.st_miss_b,
                     cm.writes);
            verdict(marker_ok, "mem: the program ran to its end");
        end else if (mode_s == "video") begin
            // S4/S6: Layer 2 in the mode +VMODE (prog/video.s) against the frame buffer: every dot of the window must be the colour of the pattern
            begin : vid
                integer bad, sr, c, px, x0d, y0d, w, h, dots, f0, tot, vmode, m0;
                reg [1:0] bb;
                reg [7:0] idx;
                reg [15:0] want;
                bad = 0; tot = 0;
                if (!$value$plusargs("VMODE=%d", vmode)) vmode = 0;
                if (vmode == 0) begin x0d = 32; y0d = 32; w = 256; h = 192; dots = 2; end
                else if (vmode == 1) begin x0d = 0; y0d = 0; w = 320; h = 256; dots = 2; end
                else begin x0d = 0; y0d = 0; w = 640; h = 256; dots = 1; end
                wait_marker(21'h48001, 8'h5A, ms_limit);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                wait_us(25000);                                                  // a whole frame with the picture on: the cache is cold at the start
                f0 = dut.fb_seq; m0 = dut.mem.st_miss_b;
                wait_us(45000);                                                  // two more frames
                $display("  frames since the marker: %0d, overruns %0d; Layer 2 demand misses in %0d frames: %0d (the prefetch has the line there in time if it is few)", dut.fb_seq - f0 + 1, dut.fb_over,
                         dut.fb_seq - f0, dut.mem.st_miss_b - m0);
                // a frame reads 5120 lines (256 x 20 lines of a column band in the wide modes, 192 x 16 of a row in 256 x 192): fewer than 5 % may be a demand miss, which holds the machine
                if (dut.mem.st_miss_b - m0 > (dut.fb_seq - f0) * 256) begin bad = bad + 1; $display("  the prefetch is not in time: %0d demand misses in %0d frames", dut.mem.st_miss_b - m0, dut.fb_seq - f0); end
                bb = dut.fb_latest[1:0];
                begin : vdbg
                    integer rr, cc, nz, r0, r1, c0, c1;
                    reg [15:0] fv;
                    nz = 0; r0 = 9999; r1 = -1; c0 = 9999; c1 = -1;
                    for (rr = 0; rr < 512; rr = rr + 1) for (cc = 0; cc < 640; cc = cc + 1) begin
                        fv = fbpix(bb, cc, rr);
                        if (fv !== 16'd0) begin
                            nz = nz + 1;
                            if (rr < r0) r0 = rr; if (rr > r1) r1 = rr; if (cc < c0) c0 = cc; if (cc > c1) c1 = cc;
                        end
                    end
                    $display("  Layer 2 enable %b, active bank %02X, clip %0d..%0d x %0d..%0d, mode %0d", dut.zxn.port_123b_layer2_en, dut.zxn.nr_12_layer2_active_bank,
                             dut.zxn.nr_18_layer2_clip_x1, dut.zxn.nr_18_layer2_clip_x2, dut.zxn.nr_18_layer2_clip_y1, dut.zxn.nr_18_layer2_clip_y2, dut.zxn.nr_70_layer2_resolution);
                    $display("  buffer %0d: %0d non-black dots, rows %0d..%0d, columns %0d..%0d; Layer 2 line fetches %0d, first dots %04X %04X %04X %04X",
                             bb, nz, r0, r1, c0, c1, dut.mem.st_miss_b, fbpix(bb, 64, 64), fbpix(bb, 65, 64), fbpix(bb, 66, 64), fbpix(bb, 67, 64));
                end
                for (sr = 0; sr < h; sr = sr + 1) begin
                    for (px = 0; px < w; px = px + 1) begin
                        if (vmode == 0) idx = (px + 3 * sr + 1) & 255;
                        else if (vmode == 1) idx = (sr + 3 * px + 1) & 255;
                        else idx = (sr + 3 * (px / 2) + 1) & 255;
                        if (idx == 8'hE3) idx = 8'hE4;
                        if (vmode == 2) idx = (px % 2 == 0) ? {4'd0, idx[7:4]} : {4'd0, idx[3:0]};
                        want = c565(idx);
                        for (c = 0; c < dots; c = c + 1) begin
                            tot = tot + 2;
                            if (fbpix(bb, (x0d + px) * dots + c, (y0d + sr) * 2) !== want) begin
                                bad = bad + 1;
                                if (bad < 6) $display("  row %0d dot %0d: %04X, want %04X", y0d + sr, (x0d + px) * dots + c, fbpix(bb, (x0d + px) * dots + c, (y0d + sr) * 2), want);
                            end
                            if (fbpix(bb, (x0d + px) * dots + c, (y0d + sr) * 2 + 1) !== want) bad = bad + 1;
                        end
                    end
                end
                $display("  %0d dots checked, %0d differ; ddr2 writes by the picture %0d", tot, bad, em.writes);
                if (bad == 0 && tot > 0) verdict(1, "video: every dot of the Layer 2 window is the colour of the pattern");
                else verdict(0, "video: the picture differs from the pattern");
            end
        end else if (mode_s == "audio") begin
            // S7: each source alone for 100 ms (prog/audio.s): its frequency from the rising zero crossings of the host's samples against what the machine did - the AY
            // chips by their period registers (f = machine clock / 16 / 16 / TP, the machine clock measured here as master cycles per cpu_clk), the DAC and the beeper by the
            // rhythm of the CPU's own writes to their ports - and, after the last phase, a silence that is exactly zero
            begin : aud
                integer bad, k, i, a0, a1, ncross, peak, thr, v, state;
                integer tp [1:3];
                real f, want, dur, r28hz;
                bad = 0;
                tp[1] = 218; tp[2] = 109; tp[3] = 73;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                wait_us(2000);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                for (k = 1; k <= 5; k = k + 1) begin
                    a0 = ph_t[k] + 500; a1 = ph_t[k + 1] - 100;                                       // inside the phase: skip the first 10 ms (the DC tracker settles)
                    peak = 0;
                    for (i = a0; i < a1; i = i + 1) begin
                        v = smp_l[i]; if (v < 0) v = -v; if (v > peak) peak = v;
                        v = smp_r[i]; if (v < 0) v = -v; if (v > peak) peak = v;
                    end
                    thr = peak / 2; ncross = 0; state = 0;
                    for (i = a0; i < a1; i = i + 1) begin
                        v = smp_l[i] + smp_r[i];
                        if (state == 0 && v > thr) begin state = 1; ncross = ncross + 1; end
                        else if (state == 1 && v < -thr) state = 0;
                    end
                    dur = (a1 - a0) / 47348.9;
                    f = ncross / dur;
                    r28hz = 1.0e8 * (ph_r[k + 1] - ph_r[k]) / (ph_c[k + 1] - ph_c[k]);                  // the machine's master clock in this phase, Hz
                    if (k <= 3) want = r28hz / 256.0 / tp[k];
                    else if (k == 4) want = 1.0e8 * (nfb - 1) / 2.0 / (cfb1 - cfb0);                           // two writes a period
                    else want = 1.0e8 * (nfe - 1) / 2.0 / (cfe1 - cfe0);
                    $display("  phase %0d: peak %0d, %0d periods in %0d samples = %0f Hz (want %0f Hz, machine clock %0f MHz)", k, peak, ncross, a1 - a0, f, want, r28hz / 1.0e6);
                    if (peak < 20000) begin bad = bad + 1; $display("  phase %0d: no signal", k); end
                    // the count of rising crossings in a window of about 5000 samples is exact to one period at each end
                    if (ncross < want * dur - 2.0 || ncross > want * dur + 2.0) begin bad = bad + 1; $display("  phase %0d: wrong frequency", k); end
                end
                // the silence after the last phase: from 52 ms in, every sample is exactly zero
                a0 = ph_t[6] + 2500; a1 = ph_t[7];
                v = 0;
                for (i = a0; i < a1; i = i + 1) if (smp_l[i] != 0 || smp_r[i] != 0) v = v + 1;
                $display("  silence: %0d of %0d samples not zero", v, a1 - a0);
                if (v != 0 || a1 - a0 < 1000) bad = bad + 1;
                if (bad == 0) verdict(1, "audio: three AY, the DAC and the beeper each play their frequency, the silence is exactly zero");
                else verdict(0, "audio: a source is missing, wrong or not quiet");
            end
        end else if (mode_s == "clk") begin
            // S9: the length of the machine's frame in cpu_clk in the timing +TIMING (1 = 48K, 2 = 128K, 3 = +3, 4 = Pentagon). Locked (the default) it is the C64's PAL
            // frame (19656 cycles at 985248 Hz = 19.9498 ms = 1994980 cpu_clk) within 0.01 %; unlocked it is the machine's own: T-states of its frame
            // (69888, 70908, 70908, 71680) x 8 master cycles at 28 MHz
            begin : clkt
                integer k, ph, t_prev, d, sum, lo, hi, want, tol, tim, bad;
                bad = 0;
                if (!$value$plusargs("TIMING=%d", tim)) tim = 3;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not start"); end
                for (ph = 0; ph < 2; ph = ph + 1) begin
                    if (ph == 0) begin want = 1994980; tol = 200; end
                    else begin
                        cw_wr(17'h0A2, 8'd1);                                         // the lock off
                        want = (tim == 1) ? 1996800 : (tim == 4) ? 2048000 : 2025943; tol = 20;
                    end
                    sum = 0; lo = 99999999; hi = 0;
                    @(negedge dut.rgb_vs_n); @(negedge dut.rgb_vs_n); @(negedge dut.rgb_vs_n); t_prev = ccnt;      // the first frames after the switch are dropped
                    for (k = 0; k < 5; k = k + 1) begin
                        @(negedge dut.rgb_vs_n);
                        d = ccnt - t_prev; t_prev = ccnt;
                        sum = sum + d; if (d < lo) lo = d; if (d > hi) hi = d;
                    end
                    $display("  timing %0d, %0s: frame %0d .. %0d cpu_clk, mean %0d (want %0d +- %0d)", tim, ph == 0 ? "locked  " : "unlocked", lo, hi, sum / 5, want, tol);
                    if (lo < want - tol || hi > want + tol) bad = bad + 1;
                end
                if (bad == 0) verdict(1, "clk: the frame has the length the lock asks for, and the machine's own with the lock off");
                else verdict(0, "clk: the frame has another length");
            end
        end else if (mode_s == "boardid") begin
            // T116 D10: NextReg 0x0F reads the id the app wrote to BOARD_ID (+BOARDID=n; none: the reset value, 3 = issue 5); +WANT=n is what the run must read
            begin : bid
                integer want;
                if (!$value$plusargs("WANT=%d", want)) want = 3;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                $display("  NextReg 0x0F reads %02X, want %02X", ramb(21'h48002), want[7:0]);
                if (marker_ok && ramb(21'h48000) == 8'hA5 && ramb(21'h48002) == want[7:0]) verdict(1, "boardid: NextReg 0x0F reports the id the app wrote");
                else verdict(0, "boardid: NextReg 0x0F does not report the id the app wrote");
            end
        end else if (mode_s == "freeze") begin
            // S9: the machine under Freeze: nothing moves while it is held (no master cycle, no CPU port write, no DDR2 write by the machine or the picture) and
            // it goes on exactly where it was (the P lines equal those of the run without a Freeze, master cycle for master cycle)
            begin : fz
                integer bad, k, n_at, r28_h, p_h, w_h, e_h, c_h;
                bad = 0;
                for (k = 0; k < 3; k = k + 1) begin
                    n_at = (k == 0) ? 20 : (k == 1) ? 60 : 110;
                    while (plog_n < n_at) begin #20000; serve_one; end
                    if (!$test$plusargs("NOFREEZE")) begin
                        halt_req = 1'b1;
                        begin : hw integer q; q = 0; while (!halted && q < 400) begin #20000; serve_one; q = q + 1; end end
                        if (!halted) begin bad = bad + 1; $display("  Freeze %0d: the machine never reported held", k); end
                        r28_h = r28n; p_h = plog_n; w_h = cm.writes; e_h = em.writes; c_h = cpun;
                        #3000000; serve_one;
                        if (r28n != r28_h || cpun != c_h) begin bad = bad + 1; $display("  Freeze %0d: %0d master cycles while held", k, r28n - r28_h); end
                        if (plog_n != p_h) begin bad = bad + 1; $display("  Freeze %0d: %0d port writes while held", k, plog_n - p_h); end
                        if (cm.writes != w_h || em.writes != e_h) begin bad = bad + 1; $display("  Freeze %0d: DDR2 writes while held (machine %0d, picture %0d)", k, cm.writes - w_h, em.writes - e_h); end
                        halt_req = 1'b0;
                    end
                end
                wait_marker(21'h48001, 8'h5A, ms_limit);
                $display("  port writes %0d, ddr2 writes by the machine %0d", plog_n, cm.writes);
                if (bad == 0 && marker_ok && plog_n == 150) verdict(1, "freeze: three Freezes, nothing moved while held, the program ran to its end");
                else verdict(0, "freeze: the machine moved while it should stand, or did not finish");
            end
        end else if (mode_s == "keys") begin
            // S8: the host's key matrix, joysticks and mouse as the machine reads them (prog/keys.s samples them again and again)
            begin : kt
                integer bad, pc;
                reg [7:0] r0, r1, r2, r3, r4, r5, r6, r7;
                bad = 0;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                if (!marker_ok) begin kbad = kbad + 1; $display("  the program did not start"); end
                if ($test$plusargs("KDBG")) begin
                    settle;
                    $display("  RAM 0x8100: %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X | pass counter %02X", ramb(21'h48100), ramb(21'h48101), ramb(21'h48102), ramb(21'h48103),
                             ramb(21'h48104), ramb(21'h48105), ramb(21'h48106), ramb(21'h48107), ramb(21'h48110), ramb(21'h48111), ramb(21'h48112), ramb(21'h48113), ramb(21'h48120));
                end
                // a quiet keyboard: every half-row reads 1F, the extended keys and the sticks 00, the mouse at rest
                keys = 64'd0; joy1 = 5'd0; joy2 = 5'd0; settle;
                chk8(21'h48100, 8'h1F, "row 0"); chk8(21'h48101, 8'h1F, "row 1"); chk8(21'h48102, 8'h1F, "row 2"); chk8(21'h48103, 8'h1F, "row 3");
                chk8(21'h48104, 8'h1F, "row 4"); chk8(21'h48105, 8'h1F, "row 5"); chk8(21'h48106, 8'h1F, "row 6"); chk8(21'h48107, 8'h1F, "row 7");
                chk8(21'h48110, 8'h00, "NR B0"); chk8(21'h48111, 8'h00, "NR B1"); chk8(21'h48112, 8'h00, "port 1F"); chk8(21'h48113, 8'h00, "port 37");
                // A (C64 matrix 10) is the Spectrum's A: half-row 1 (A9 low = 0xFD), bit 0
                keys = 64'd1 << 10; settle;
                chk8(21'h48101, 8'h1E, "A on half-row 1"); chk8(21'h48100, 8'h1F, "row 0 with A"); chk8(21'h48102, 8'h1F, "row 2 with A");
                // Q (62) on half-row 2 and P (41) on half-row 5, both bit 0
                keys = (64'd1 << 62) | (64'd1 << 41); settle;
                chk8(21'h48102, 8'h1E, "Q on half-row 2"); chk8(21'h48105, 8'h1E, "P on half-row 5"); chk8(21'h48101, 8'h1F, "row 1 with Q and P");
                // SPACE (60): half-row 7 bit 0; RETURN (1): half-row 6 bit 0; SHIFT alone (15): CAPS SHIFT, half-row 0 bit 0
                keys = (64'd1 << 60) | (64'd1 << 1); settle;
                chk8(21'h48107, 8'h1E, "SPACE on half-row 7"); chk8(21'h48106, 8'h1E, "ENTER on half-row 6");
                keys = 64'd1 << 15; settle;
                chk8(21'h48100, 8'h1E, "CAPS SHIFT on half-row 0");
                // the extended keys: RUN/STOP (63) = BREAK, nextreg 0xB1 bit 5; HOME (51) = EDIT, bit 6; CRSR right (2) = RIGHT, nextreg 0xB0 bit 0; "," (47): B0 bit 5; "." (44): B0 bit 4
                keys = 64'd1 << 63; settle; chk8(21'h48111, 8'h20, "BREAK");
                keys = 64'd1 << 51; settle; chk8(21'h48111, 8'h40, "EDIT");
                keys = 64'd1 << 2; settle; chk8(21'h48110, 8'h01, "RIGHT");
                keys = 64'd1 << 47; settle; chk8(21'h48110, 8'h20, "comma");
                keys = 64'd1 << 44; settle; chk8(21'h48110, 8'h10, "full stop");
                keys = 64'd0;
                // the sticks: the C64's port 2 is the Next's left stick (Kempston 1, port 1F), port 1 the right (Kempston 2, port 37); bits 0 up, 1 down, 2 left, 3 right, 4 fire
                joy2 = 5'b10100; joy1 = 5'b00101; settle;                       // left stick: left + fire, right stick: up + left
                chk8(21'h48112, 8'h12, "port 1F: left stick left + fire"); chk8(21'h48113, 8'h0A, "port 37: right stick up + left");
                cw_wr(17'h0A1, 8'd1); settle;                                    // the two ports swapped
                chk8(21'h48112, 8'h0A, "port 1F swapped"); chk8(21'h48113, 8'h12, "port 37 swapped");
                cw_wr(17'h0A1, 8'd0); joy1 = 5'd0; joy2 = 5'd0;
                // the Kempston mouse: +5 right, +3 down (the Y counter counts the other way), left button
                cw_wr(17'h1C2, 8'd1); cw_wr(17'h1C0, 8'd5); cw_wr(17'h1C1, 8'd3); settle;
                chk8(21'h48114, 8'd5, "mouse X"); chk8(21'h48115, 8'hFD, "mouse Y"); chk8(21'h48116, 8'h0D, "mouse buttons: left");
                cw_wr(17'h1C2, 8'd2); settle; chk8(21'h48116, 8'h0E, "mouse buttons: right");
                $display("  %0d checks, %0d wrong", kchecks, kbad);
                if (kbad == 0 && kchecks > 30) verdict(1, "keys: the matrix, the extended keys, the sticks and the mouse reach the machine as they must");
                else verdict(0, "keys: the machine sees something else than the host gave");
            end
        end else if (mode_s == "sd") begin
            // S5: the card model against the image: init commands, a block read with token and CRC16, a block write that changes exactly that block
            // of the file, the block read back, a block beyond the end
            begin : sdt
                integer bad, k, lbar, lbaw, blk;
                reg [15:0] crc;
                reg [7:0] d, e;
                bad = 0;
                lbar = 8192; lbaw = 12000;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                wait_us(2000);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                if (ramb(21'h48010) !== 8'h01) begin bad = bad + 1; $display("  CMD0 R1 %02X, want 01", ramb(21'h48010)); end
                if (ramb(21'h48011) !== 8'h01 || ramb(21'h48012) !== 8'h00 || ramb(21'h48013) !== 8'h00 || ramb(21'h48014) !== 8'h01 || ramb(21'h48015) !== 8'hAA) begin
                    bad = bad + 1; $display("  CMD8 %02X %02X %02X %02X %02X, want 01 00 00 01 AA", ramb(21'h48011), ramb(21'h48012), ramb(21'h48013), ramb(21'h48014), ramb(21'h48015)); end
                if (ramb(21'h48016) !== 8'h01) begin bad = bad + 1; $display("  CMD55 R1 %02X, want 01", ramb(21'h48016)); end
                if (ramb(21'h48017) !== 8'h00) begin bad = bad + 1; $display("  ACMD41 R1 %02X, want 00", ramb(21'h48017)); end
                if (ramb(21'h48018) !== 8'h00 || ramb(21'h48019) !== 8'hC0 || ramb(21'h4801A) !== 8'hFF || ramb(21'h4801B) !== 8'h80 || ramb(21'h4801C) !== 8'h00) begin
                    bad = bad + 1; $display("  CMD58 %02X %02X %02X %02X %02X, want 00 C0 FF 80 00", ramb(21'h48018), ramb(21'h48019), ramb(21'h4801A), ramb(21'h4801B), ramb(21'h4801C)); end
                // the block read
                if (ramb(21'h48020) !== 8'h00 || ramb(21'h48021) !== 8'hFE) begin bad = bad + 1; $display("  CMD17 R1 %02X token %02X, want 00 FE", ramb(21'h48020), ramb(21'h48021)); end
                crc = 16'd0;
                for (k = 0; k < 512; k = k + 1) begin
                    d = ramb(21'h48100 + k); e = img0[lbar * 512 + k];
                    crc = crc16b(crc, e);
                    if (d !== e) begin bad = bad + 1; if (bad < 8) $display("  block byte %0d: %02X, want %02X", k, d, e); end
                end
                if ({ramb(21'h48022), ramb(21'h48023)} !== crc) begin bad = bad + 1; $display("  CRC16 %02X%02X, want %04X", ramb(21'h48022), ramb(21'h48023), crc); end
                // the block write
                if (ramb(21'h48024) !== 8'h00) begin bad = bad + 1; $display("  CMD24 R1 %02X, want 00", ramb(21'h48024)); end
                if (ramb(21'h48025) !== 8'h05) begin bad = bad + 1; $display("  data response %02X, want 05", ramb(21'h48025)); end
                if ({ramb(21'h48027), ramb(21'h48026)} == 16'd0) begin bad = bad + 1; $display("  the card was never busy during the write"); end
                if (served_w != 1) begin bad = bad + 1; $display("  the application wrote %0d blocks, want 1", served_w); end
                for (k = 0; k < 512; k = k + 1) begin
                    d = img[lbaw * 512 + k]; e = (3 * k + 5) & 255;
                    if (d !== e) begin bad = bad + 1; if (bad < 8) $display("  file byte %0d: %02X, want %02X", k, d, e); end
                end
                begin : others
                    integer q, diff;
                    diff = 0;
                    for (q = 0; q < IMGSZ; q = q + 1) if (img[q] !== img0[q] && (q < lbaw * 512 || q >= lbaw * 512 + 512)) diff = diff + 1;
                    if (diff != 0) begin bad = bad + 1; $display("  %0d bytes of the file outside the written block changed", diff); end
                end
                // the read back
                if (ramb(21'h48028) !== 8'h00 || ramb(21'h48029) !== 8'hFE) begin bad = bad + 1; $display("  read back R1 %02X token %02X", ramb(21'h48028), ramb(21'h48029)); end
                crc = 16'd0;
                for (k = 0; k < 512; k = k + 1) begin
                    d = ramb(21'h48500 + k); e = (3 * k + 5) & 255;
                    crc = crc16b(crc, e);
                    if (d !== e) begin bad = bad + 1; if (bad < 8) $display("  read-back byte %0d: %02X, want %02X", k, d, e); end
                end
                if ({ramb(21'h4802A), ramb(21'h4802B)} !== crc) begin bad = bad + 1; $display("  read-back CRC16 %02X%02X, want %04X", ramb(21'h4802A), ramb(21'h4802B), crc); end
                // beyond the end of the file: token and zeros
                if (ramb(21'h4802C) !== 8'h00 || ramb(21'h4802D) !== 8'hFE || ramb(21'h4802E) !== 8'h00 || ramb(21'h4802F) !== 8'h00) begin
                    bad = bad + 1; $display("  beyond the image: R1 %02X token %02X first %02X last %02X", ramb(21'h4802C), ramb(21'h4802D), ramb(21'h4802E), ramb(21'h4802F)); end
                $display("  card: cmd reads %0d writes %0d, app served r%0d w%0d", dut.sd.st_reads, dut.sd.st_writes, served_r, served_w);
                if (bad == 0) verdict(1, "sd: init, a block read with token and CRC, a block write that changes exactly that block, read back, beyond the end");
                else verdict(0, "sd: the card model differs from the image");
            end
        end else if (mode_s == "sdfast") begin
            // S5 (board B): NBLK consecutive CMD17 reads at 28 MHz with no gap between the SPI accesses (prog/sdfast.s): every block must be the image's
            begin : sdf
                integer bad, k, j, lba0, nblk;
                reg [15:0] crc;
                reg [7:0] d, e;
                bad = 0; lba0 = 8192; nblk = 6;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                wait_us(1000);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                for (j = 0; j < nblk; j = j + 1) begin
                    if (ramb(21'h48010 + 4 * j) !== 8'h00 || ramb(21'h48011 + 4 * j) !== 8'hFE) begin
                        bad = bad + 1; $display("  block %0d: R1 %02X token %02X, want 00 FE", j, ramb(21'h48010 + 4 * j), ramb(21'h48011 + 4 * j)); end
                    crc = 16'd0;
                    for (k = 0; k < 512; k = k + 1) begin
                        d = ramb(21'h48100 + 512 * j + k); e = img0[(lba0 + j) * 512 + k];
                        crc = crc16b(crc, e);
                        if (d !== e) begin bad = bad + 1; if (bad < 8) $display("  block %0d byte %0d: %02X, want %02X", j, k, d, e); end
                    end
                    if ({ramb(21'h48012 + 4 * j), ramb(21'h48013 + 4 * j)} !== crc) begin
                        bad = bad + 1; $display("  block %0d CRC16 %02X%02X, want %04X", j, ramb(21'h48012 + 4 * j), ramb(21'h48013 + 4 * j), crc); end
                end
                $display("  card: cmd reads %0d, app served r%0d", dut.sd.st_reads, served_r);
                if (bad == 0) verdict(1, "sdfast: six consecutive blocks at 28 MHz with no gap between the SPI accesses, token and CRC right");
                else verdict(0, "sdfast: the card model differs from the image at 28 MHz");
            end
        end else if (mode_s == "sdmulti") begin
            // S5 (board B): CMD18 of three blocks, CMD12, CMD17, CMD17 with a slow application (+LAT us per request): the application is still on the
            // fourth block of the CMD18 when the host asks for LBA_B; its late answer must not be taken for the answer to LBA_B
            begin : sdm
                integer bad, k, j, lbas [0:4], wh [0:4], tok [0:4];
                reg [15:0] crc;
                reg [7:0] d, e;
                bad = 0;
                lbas[0] = 8192; lbas[1] = 8193; lbas[2] = 8194; lbas[3] = 8200; lbas[4] = 8201;
                wh[0] = 21'h48100; wh[1] = 21'h48300; wh[2] = 21'h48500; wh[3] = 21'h48700; wh[4] = 21'h48900;
                tok[0] = 21'h48011; tok[1] = 21'h48015; tok[2] = 21'h48019; tok[3] = 21'h48035; tok[4] = 21'h48039;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                wait_us(2000);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                if (ramb(21'h48010) !== 8'h00) begin bad = bad + 1; $display("  CMD18 R1 %02X", ramb(21'h48010)); end
                if (ramb(21'h48030) !== 8'h00) begin bad = bad + 1; $display("  CMD12 R1 %02X", ramb(21'h48030)); end
                if (ramb(21'h48034) !== 8'h00 || ramb(21'h48038) !== 8'h00) begin bad = bad + 1; $display("  CMD17 R1 %02X %02X", ramb(21'h48034), ramb(21'h48038)); end
                for (j = 0; j < 5; j = j + 1) begin
                    if (ramb(tok[j]) !== 8'hFE) begin bad = bad + 1; $display("  block %0d (lba %0d): token %02X", j, lbas[j], ramb(tok[j])); end
                    crc = 16'd0;
                    for (k = 0; k < 512; k = k + 1) begin
                        d = ramb(wh[j] + k); e = img0[lbas[j] * 512 + k];
                        crc = crc16b(crc, e);
                        if (d !== e) begin bad = bad + 1; if (bad < 8) $display("  block %0d (lba %0d) byte %0d: %02X, want %02X", j, lbas[j], k, d, e); end
                    end
                    if ({ramb(tok[j] + 1), ramb(tok[j] + 2)} !== crc) begin
                        bad = bad + 1; $display("  block %0d (lba %0d) CRC16 %02X%02X, want %04X", j, lbas[j], ramb(tok[j] + 1), ramb(tok[j] + 2), crc); end
                end
                $display("  card: cmd reads %0d, app served r%0d", dut.sd.st_reads, served_r);
                if (bad == 0) verdict(1, "sdmulti: CMD18, CMD12 and the next reads at 28 MHz with a slow application: every block is the one asked for");
                else verdict(0, "sdmulti: a block is not the one asked for");
            end
        end else if (mode_s == "fwdrv") begin
            // S5 (board C): the firmware's SD driver as a program of ours (prog/fwdrv.s): CMD18 of 8 blocks + CMD12, again, CMD17; 17 blocks, each one must come in
            // (token within the driver's 5000 polls) and be the block asked for; +LAT us is the application's time for a request
            begin : fwd
                integer bad, k, j, maxp, p;
                bad = 0; maxp = 0;
                wait_marker(21'h48001, 8'h5A, ms_limit);
                wait_us(2000);
                if (!marker_ok) begin bad = bad + 1; $display("  the program did not finish"); end
                for (j = 0; j < 17; j = j + 1) begin
                    p = ramb(21'h48200 + 2 * j) + 256 * ramb(21'h48201 + 2 * j);
                    if (p > maxp) maxp = p;
                    $display("  block %0d: status %02X polls %0d first bytes %02X %02X %02X %02X %02X %02X %02X %02X want %02X %02X", j, ramb(21'h48100 + j), p,
                             ramb(21'h48400 + 512 * j), ramb(21'h48401 + 512 * j), ramb(21'h48402 + 512 * j), ramb(21'h48403 + 512 * j), ramb(21'h48404 + 512 * j), ramb(21'h48405 + 512 * j),
                             ramb(21'h48406 + 512 * j), ramb(21'h48407 + 512 * j), img0[(8192 + j) * 512], img0[(8192 + j) * 512 + 1]);
                    if (ramb(21'h48100 + j) !== 8'h01) begin bad = bad + 1; $display("  block %0d (lba %0d): no token after %0d polls", j, 8192 + j, p); end
                    else for (k = 0; k < 512; k = k + 1) begin
                        if (ramb(21'h48400 + 512 * j + k) !== img0[(8192 + j) * 512 + k]) begin
                            bad = bad + 1; if (bad < 8) $display("  block %0d (lba %0d) byte %0d: %02X, want %02X", j, 8192 + j, k, ramb(21'h48400 + 512 * j + k), img0[(8192 + j) * 512 + k]);
                        end
                    end
                end
                $display("  card: cmd reads %0d, app served r%0d, the most polls a token took: %0d", dut.sd.st_reads, served_r, maxp);
                if (bad == 0) verdict(1, "fwdrv: the firmware's driver reads CMD18 / CMD12 / CMD17 blocks, every token within its polls");
                else verdict(0, "fwdrv: the firmware's driver lost a block");
            end
        end else if (mode_s == "fw") begin
            // S5 (board C): the Next's own firmware (TBBLUE.FW, not in the repository) on the card image of tools/zxnext_fwimg.py. The boot ROM loads its first
            // module and the module reads TBBLUE.FW with a SD driver of its own (FatFS: CMD17, CMD18 stopped by CMD12). +PCOK / +PCERR are the addresses (hex) the
            // bench watches for: the first is where a read of the file's data returns well, the second where the firmware reports "Error reading TBBLUE.FW data!"
            // (instruction fetches, read from the firmware's code: 0xB68E and 0xB686); the run ends at the first of them or at +MS.
            begin : fwb
                integer k, st;
                st = 0;
                for (k = 0; k < ms_limit * (1000 / (step_ns / 1000)) && st == 0; k = k + 1) begin
                    #(step_ns); serve_one;
                    if (pc_ok_n > 0) st = 1;
                    if (pc_err_n > 0) st = 2;
                end
                $display("  card: cmd reads %0d (last lba %0d), app served r%0d, last card command %0d, machine time %0d ms", dut.sd.st_reads, last_lba_r, served_r,
                         dut.sd.st_last_cmd[6:0], $time / 1000000);
                if (st == 1) verdict(1, "fw: the firmware read TBBLUE.FW's data");
                else if (st == 2) verdict(0, "fw: the firmware says Error reading TBBLUE.FW data");
                else verdict(0, "fw: the firmware neither read TBBLUE.FW's data nor reported an error in the time");
            end
        end else begin
            $display("FAIL unknown mode");
        end
        $finish;
    end
endmodule
