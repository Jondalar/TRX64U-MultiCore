// T95: the whole NES core (nes_core: NES_MiSTer's machine, cartridge memory, window, frame writer, sound, registers) with a model of
// the host's DDR2 write port, a cartridge of our own loaded through the app's DMA window (mkcart.py: no game, no third-party
// image; its iNES header goes into the INES registers) and the register window read like the app reads it. Verilator, `--timing`.
//   +CART=<hex>   the image: 16 byte iNES header, 32 KB PRG, 8 KB CHR
//   +MODE=frame   the picture of the frame buffer against what the program draws (EXPECT) and the NMI count in zero page $00
//   +MODE=tone    the sound: the samples swing (and, with +MODE=frame, they do not)
//   +MODE=win     the window: write, read back, out of range accesses, the registers
//   +MODE=check   a program that stores results in work RAM and its end marker in $03FF; the pairs {address, value} of +EXPECT=<hex>
//                 (+NEXP=<lines>) are compared with them: the instruction bench (cpu), the bank benches (banks2, banks3)
//                 and the pad bench (pad, with +PAD: the bench presses the joystick and key lines of the phase the program names in $0300, T95 5.18)
//   +MODE=inv     the window-write bench: a byte of the image is changed through the DDR2 window while the machine runs
//   +BD           load the image into the DDR2 model by hierarchy instead of through the window (big images; the window is tested by win)
//   +MODE=pause   two machines, one held for a while: bus for bus the same afterwards, no clock while held
//   +PPM=<file>   write the newest frame as a PPM
`timescale 1ns / 1ps
module tb_nes;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #40.69 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;                        // 75 MHz, the host's DDR2 user clock
    reg sys_rst = 1, reset_req = 1, hold_b = 0;

    // ---- the host's DDR2 write port: a frame buffer ring at the default base, 1 MiB apart
    localparam BASE = 32'h01D00000, STRIDE = 32'h00100000;
    reg [127:0] ddr [0:262143];                         // 4 buffers x 64 K words of 16 bytes (a frame is 224 x 512 bytes)
    wire [26:0] ea0; wire [2:0] ec0; wire een0; wire [127:0] ed0; wire [15:0] em0; wire ew0;
    wire [26:0] ea1; wire [2:0] ec1; wire een1; wire [127:0] ed1; wire [15:0] em1; wire ew1;
    reg  e_rdy_r = 1'b1;
    integer writes = 0, bad_writes = 0;
    always @(posedge ui_clk) if (een0 && e_rdy_r && ec0 == 3'b000) begin
        writes = writes + 1;
        if (({ea0, 1'b0} < BASE) || ({ea0, 1'b0} >= BASE + 4 * STRIDE)) bad_writes = bad_writes + 1;
        else ddr[(({ea0, 1'b0} - BASE) >> 4)] <= ed0;
    end
    integer writes_b = 0;
    always @(posedge ui_clk) if (een1 && e_rdy_r && ec1 == 3'b000) writes_b = writes_b + 1;

    // ---- the app: DMA window and register window
    reg  dma_req_tgl = 0; reg [15:0] dma_addr = 0; reg dma_we = 0; reg [7:0] dma_wdata = 0; reg dma_rom = 0;
    wire dma_done_tgl; wire [7:0] dma_rdata;
    wire wpu; wire [15:0] wpa; wire wpr, wpw; wire [7:0] wpd;
    reg  cw_we = 0, cw_re = 0; reg [16:0] cw_addr = 0; reg [7:0] cw_wdata = 0;
    wire [7:0] cw_rdata; wire cw_ack;
    wire signed [23:0] aud_l, aud_r;
    wire [7:0] ram_rdata;
    wire halted_a, running_a, halted_b, running_b;
    wire [31:0] j_rdata;

    wire [26:0] ca_0, ca_1; wire [2:0] cc_0, cc_1; wire cen_0, cen_1, crdy_0, crdy_1; wire [127:0] cd_0, cd_1, crd_0, crd_1;
    wire [15:0] cm_0, cm_1; wire cw_0, cw_1, crv_0, crv_1;
    // ---- T95 5.18: the controller lines of the host. In mode check with +PAD the program names a phase in $0300 and the bench
    // presses what that phase is about (the names are in mkcart.py build_pad); every other mode leaves the lines released.
    reg [4:0]  pjoy1 = 5'd0, pjoy2 = 5'd0;
    reg [63:0] pkeys = 64'd0;
    localparam [5:0] K_F7 = 6'd3, K_F1 = 6'd4, K_F3 = 6'd5, K_F5 = 6'd6, K_RSHIFT = 6'd52, K_SPACE = 6'd60;
    always @(posedge cpu_clk) if ($test$plusargs("PAD")) begin
        case (A.mem.wram[11'h300])
            8'd1: begin                                                    // joystick 1 up and fire, F1
                pjoy1 <= 5'b10001; pjoy2 <= 5'd0;
                pkeys <= (64'd1 << K_F1);
            end
            8'd5: begin                                                    // T156: joystick 1 up, together with the application's PAD1 (written below)
                pjoy1 <= 5'b00001; pjoy2 <= 5'd0; pkeys <= 64'd0;
            end
            8'd2: begin                                                    // joystick 1 down and left, joystick 2 right and fire, all four keys
                pjoy1 <= 5'b00110; pjoy2 <= 5'b11000;
                pkeys <= (64'd1 << K_SPACE) | (64'd1 << K_F3) | (64'd1 << K_F5) | (64'd1 << K_RSHIFT);
            end
            default: begin                                                 // phase 3 and before the first phase: nothing pressed
                pjoy1 <= 5'd0; pjoy2 <= 5'd0; pkeys <= 64'd0;
            end
        endcase
    end

    // T156: phases 4 to 6 of the pad program belong to the application's USB pad registers; the bench writes them over the register bus, the way the
    // application does (low byte of the 16 bit pair; the NES takes player 1 at 0F8 and player 2 at 0FA).
    initial if ($test$plusargs("PAD")) begin
        wait (A.mem.wram[11'h300] == 8'd4);
        creg_w(12'h0F8, 8'hA5); creg_w(12'h0FA, 8'h5A);                    // PAD1: A Select Down Right, PAD2: B Start Up Left
        wait (A.mem.wram[11'h300] == 8'd5);
        creg_w(12'h0F8, 8'h08); creg_w(12'h0FA, 8'h00);                    // PAD1: Start; PAD2 cleared
        wait (A.mem.wram[11'h300] == 8'd6);
        creg_w(12'h0F8, 8'h00);                                           // PAD1 cleared: the unplug
    end

    nes_core A (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(1'b0),
        .reset_req(reset_req), .halt_req(1'b0), .halted(halted_a), .running(running_a),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(1'b0),
        .win_port_use(wpu), .win_port_addr(wpa), .win_port_rom(wpr), .win_port_we(wpw), .win_port_wdata(wpd),
        .port_req(wpu), .port_addr(wpa), .port_rom(wpr), .port_we(wpw), .port_wdata(wpd), .ram_rdata(ram_rdata),
        .c64_joy1(pjoy1), .c64_joy2(pjoy2), .c64_keys(pkeys),
        .pix_color(), .pix_x(), .pix_y(), .audio_l(aud_l), .audio_r(aud_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'h12345678), .ddr_cal(64'd0),
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(j_rdata), .j_rvalid(),
        .e_addr(ea0), .e_cmd(ec0), .e_en(een0), .e_rdy(e_rdy_r), .e_wdf_data(ed0), .e_wdf_mask(em0), .e_wdf_wren(ew0),
        .c_addr(ca_0), .c_cmd(cc_0), .c_en(cen_0), .c_rdy(crdy_0), .c_wdf_data(cd_0), .c_wdf_mask(cm_0), .c_wdf_wren(cw_0),
        .c_rd_data(crd_0), .c_rd_valid(crv_0));
    ddr2c_model #(.SEED(11)) mA (.ui_clk(ui_clk), .c_addr(ca_0), .c_cmd(cc_0), .c_en(cen_0), .c_rdy(crdy_0), .c_wdf_data(cd_0), .c_wdf_mask(cm_0),
        .c_wdf_wren(cw_0), .c_rd_data(crd_0), .c_rd_valid(crv_0));

    // machine B (pause bench): the same core, loaded by hierarchy, held from outside
    nes_core B (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(1'b0),
        .reset_req(reset_req), .halt_req(hold_b), .halted(halted_b), .running(running_b),
        .dma_req_tgl(1'b0), .dma_addr(16'd0), .dma_we(1'b0), .dma_wdata(8'd0), .dma_rom(1'b0), .dma_done_tgl(), .dma_rdata(), .dbg_busy(1'b0),
        .win_port_use(), .win_port_addr(), .win_port_rom(), .win_port_we(), .win_port_wdata(),
        .port_req(1'b0), .port_addr(16'd0), .port_rom(1'b0), .port_we(1'b0), .port_wdata(8'd0), .ram_rdata(),
        .c64_joy1(5'd0), .c64_joy2(5'd0), .c64_keys(64'd0),
        .pix_color(), .pix_x(), .pix_y(), .audio_l(), .audio_r(),
        .cw_we(1'b0), .cw_re(1'b0), .cw_addr(17'd0), .cw_wdata(8'd0), .cw_rdata(), .cw_ack(), .cw_irq(),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'd0), .ddr_cal(64'd0),
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(),
        .e_addr(ea1), .e_cmd(ec1), .e_en(een1), .e_rdy(e_rdy_r), .e_wdf_data(ed1), .e_wdf_mask(em1), .e_wdf_wren(ew1),
        .c_addr(ca_1), .c_cmd(cc_1), .c_en(cen_1), .c_rdy(crdy_1), .c_wdf_data(cd_1), .c_wdf_mask(cm_1), .c_wdf_wren(cw_1),
        .c_rd_data(crd_1), .c_rd_valid(crv_1));
    ddr2c_model #(.SEED(23)) mB (.ui_clk(ui_clk), .c_addr(ca_1), .c_cmd(cc_1), .c_en(cen_1), .c_rdy(crdy_1), .c_wdf_data(cd_1), .c_wdf_mask(cm_1),
        .c_wdf_wren(cw_1), .c_rd_data(crd_1), .c_rd_valid(crv_1));

    integer fails = 0;
    task check(input ok, input [8*80-1:0] what);
        begin
            if (ok) $display("ok   %0s", what); else begin $display("FAIL %0s", what); fails = fails + 1; end
        end
    endtask

    // ---- the app's window, byte by byte (the toggle protocol of ultimate_sys)
    task dma(input rom, input we, input [15:0] addr, input [7:0] wd, output [7:0] rd);
        reg t0;
        begin
            t0 = dma_done_tgl;
            dma_rom = rom; dma_we = we; dma_addr = addr; dma_wdata = wd;
            @(posedge cpu_clk); dma_req_tgl = ~dma_req_tgl;
            wait (dma_done_tgl != t0);
            rd = dma_rdata;
            @(posedge cpu_clk);
        end
    endtask
    reg [7:0] rdb;
    task creg_w(input [11:0] a, input [7:0] d);
        begin @(posedge cpu_clk); cw_addr = {5'd0, a}; cw_wdata = d; cw_we = 1; @(posedge cpu_clk); cw_we = 0; end
    endtask
    task creg_r(input [11:0] a, output [7:0] d);
        begin @(posedge cpu_clk); cw_addr = {5'd0, a}; cw_re = 1; @(posedge cpu_clk); cw_re = 0; @(posedge cpu_clk); @(posedge cpu_clk); d = cw_rdata; end
    endtask

    integer n_exp, k_exp, cpu_bad; reg [8*200-1:0] f_exp;
    reg [7:0] img [0:1048575];     // header, PRG, CHR
    reg [23:0] exp_w [0:511];
    integer prg_n, chr_n; reg bd = 0;
    integer i;
    task load_cart;
        begin
            for (i = 0; i < 16; i = i + 1) creg_w(12'h0E0 + i[11:0], img[i]);     // the iNES header
            creg_w(12'h0D4, 8'h00);
            if (bd) begin
                for (i = 0; i < prg_n; i = i + 1) mA.mem[i / 16][8 * (i % 16) +: 8] = img[16 + i];
                for (i = 0; i < chr_n; i = i + 1) mA.mem[(32'h200000 + i) / 16][8 * (i % 16) +: 8] = img[16 + prg_n + i];
            end else begin
                for (i = 0; i < prg_n; i = i + 1) begin
                    creg_w(12'h0D4, i[20:16]); dma(1'b0, 1'b1, i[15:0], img[16 + i], rdb);
                end
                for (i = 0; i < chr_n; i = i + 1) begin
                    creg_w(12'h0D4, i[20:16]); dma(1'b1, 1'b1, i[15:0], img[16 + prg_n + i], rdb);
                end
                creg_w(12'h0D4, 8'h00);
            end
        end
    endtask

    // ---- the frame buffer
    function [15:0] px(input [1:0] buf_i, input integer x, input integer row);
        reg [127:0] w; integer wi;
        begin
            wi = buf_i * 65536 + row * 32 + (x >> 3);
            w = ddr[wi];
            px = w >> ((x & 7) * 16);
        end
    endfunction
    // RGB565 of palette entries used by the program (nes_pal.v)
    localparam [15:0] C_BACK = 16'h3DFF /* 0x21: 3CBCFC */, C_BAND = 16'hF9C0 /* 0x16: F83800 */,
                      C_WHITE = 16'hFFFF /* 0x30: FCFCFC */, C_SPR = 16'h5ECA /* 0x2A: 58D854 */;
    function [15:0] rgb565(input [23:0] c);
        rgb565 = {c[23:19], c[15:10], c[7:3]};
    endfunction

    integer seqn = 0, f, nmis;
    integer nfr, t_req, dots0, cyc0, fr0, cb_hold;
    reg [24:0] ad0; reg [21:0] pa0;
    reg [7:0] seq_last;
    integer fr_clk [0:15];
    reg [1:0] lb;
    reg [8*200-1:0] f_cart, f_mode, f_ppm;
    integer fd, rr, cc;
    integer sw_min, sw_max, ns;
    reg signed [23:0] sm;

    // sound: min/max of what the host would read at mclk
    reg sound_on = 0;
    always @(A.audio.tgl) if (sound_on) ns = ns + 1;         // one sample per toggle (2112 clocks)
    always @(posedge mclk) if (sound_on) begin
        if (aud_l < sw_min) sw_min = aud_l;
        if (aud_l > sw_max) sw_max = aud_l;
    end

    task wait_frames(input integer n);
        integer k;
        begin
            for (k = 0; k < n; k = k + 1) begin
                seq_last = A.fb_seq;
                wait (A.fb_seq != seq_last);
                fr_clk[k] = $time / 10;
            end
        end
    endtask

    // the bus hash of the pause bench: the machine's own outputs (CPU-side and PPU-side memory request, PPU position) at every tick it
    // is not paused, one hash per NES frame (a frame starts when the frame counter moves). The machine resumes from a pause one master
    // tick later than a free one would be (nes.v skips the first enable after the pause to realign with its shadow clock; measured:
    // frame 2 starts at live tick +1 and every frame after), so the two are compared frame by frame and not at the same tick count.
    reg [63:0] ha = 0, hb = 0, fh_a [0:63], fh_b [0:63];
    integer ca = 0, cb = 0, fa [0:63], fbb [0:63];
    reg [15:0] fa_prev = 0, fb_prev = 0;
    wire [63:0] bus_a = {A.cpumem_addr, A.cpumem_dout, A.cpumem_read, A.cpumem_write, A.ppumem_addr[15:0], A.cycle, A.scanline[8:0], 5'd0};
    wire [63:0] bus_b = {B.cpumem_addr, B.cpumem_dout, B.cpumem_read, B.cpumem_write, B.ppumem_addr[15:0], B.cycle, B.scanline[8:0], 5'd0};
    always @(posedge cpu_clk) begin
        if (A.ce && !A.rst_r && !A.corepaused) begin
            ca <= ca + 1;
            if (A.frames != fa_prev) begin fh_a[A.frames] <= ha; fa[A.frames] <= ca; fa_prev <= A.frames; ha <= bus_a; end
            else ha <= (ha * 64'd1099511628211) ^ bus_a;
        end
        if (B.ce && !B.rst_r && !B.corepaused) begin
            cb <= cb + 1;
            if (B.frames != fb_prev) begin fh_b[B.frames] <= hb; fbb[B.frames] <= cb; fb_prev <= B.frames; hb <= bus_b; end
            else hb <= (hb * 64'd1099511628211) ^ bus_b;
        end
    end

    initial begin
        if (!$value$plusargs("CART=%s", f_cart)) begin $display("need +CART="); $finish; end
        if (!$value$plusargs("MODE=%s", f_mode)) f_mode = "frame";
        $readmemh(f_cart, img);
        bd = $test$plusargs("BD");
        prg_n = img[4] * 16384; chr_n = img[5] * 8192;
        sw_min = 32'sh7FFFFFFF; sw_max = -32'sh7FFFFFFF; ns = 0;
        #200; sys_rst = 0; #1000;
        if (f_mode == "pause") begin
            // both machines get the image by hierarchy (B has no window wired) and leave reset together
            for (i = 0; i < 16; i = i + 1) begin B.regs.hdr_r[8 * i +: 8] = img[i]; A.regs.hdr_r[8 * i +: 8] = img[i]; end
            for (i = 0; i < prg_n; i = i + 1) begin mB.mem[i / 16][8 * (i % 16) +: 8] = img[16 + i]; mA.mem[i / 16][8 * (i % 16) +: 8] = img[16 + i]; end
            for (i = 0; i < chr_n; i = i + 1) begin mB.mem[(32'h200000 + i) / 16][8 * (i % 16) +: 8] = img[16 + prg_n + i]; mA.mem[(32'h200000 + i) / 16][8 * (i % 16) +: 8] = img[16 + prg_n + i]; end
            reset_req = 0;
        end else begin
            // the cartridge goes in through the window while the machine is in reset (the app's order)
            creg_w(12'h0A0, 8'h01);
            if ($test$plusargs("PAL")) creg_w(12'h0A1, 8'h01);
            reset_req = 0;
            load_cart;
            creg_w(12'h0A0, 8'h00);
        end

        if (f_mode == "frame" || f_mode == "tone") begin
            sound_on = (f_mode == "tone") ? 1 : 0;
            wait_frames(6);
            sound_on = 0;
            lb = A.fb_latest[1:0];
            check(A.fb_latest[7] === 1'b1, "a frame is complete (FB_LATEST valid)");
            check(bad_writes == 0, "every DDR2 write is inside the four frame buffers");
            check(A.fb_over === 16'd0, "no line was handed over too late (FB_OVERRUNS = 0)");
            // frame period: 89,342 dots (89,341 on an odd frame) of 4 master clocks, 21.477 MHz on 100 MHz: 1,664,000 clocks (60.1 Hz)
            if ($test$plusargs("PAL")) check((fr_clk[3] - fr_clk[2]) > 1990000 && (fr_clk[3] - fr_clk[2]) < 2010000, "PAL frame period is 2.000 M clocks (50.0 Hz)");
            else check((fr_clk[3] - fr_clk[2]) > 1655000 && (fr_clk[3] - fr_clk[2]) < 1675000, "frame period is 1.664 M clocks (60.1 Hz)");
            $display("     frame period %0d clocks, %0d DDR2 writes; cartridge cache: %0d PRG and %0d CHR line fetches, machine held %0d clocks (%0d.%0d %%)", fr_clk[3] - fr_clk[2], writes,
                     A.mem.st_miss_prg, A.mem.st_miss_chr, A.mem.st_stall, A.mem.st_stall * 100 / ($time / 10), (A.mem.st_stall * 1000 / ($time / 10)) % 10);
            if ($value$plusargs("PPM=%s", f_ppm)) begin
                fd = $fopen(f_ppm, "w");
                $fdisplay(fd, "P3\n256 224\n255");
                for (rr = 0; rr < 224; rr = rr + 1) for (cc = 0; cc < 256; cc = cc + 1) begin
                    sm = px(lb, cc, rr);
                    $fdisplay(fd, "%0d %0d %0d", (px(lb, cc, rr) >> 11) * 255 / 31, ((px(lb, cc, rr) >> 5) & 63) * 255 / 63, (px(lb, cc, rr) & 31) * 255 / 31);
                end
                $fclose(fd);
            end
            nmis = A.mem.wram[0];
            check(nmis >= 3 && nmis <= 5, "NMI handler ran once a frame: $00 counted 3..5 in six frames (two are warm-up)");
            $display("     $00 = %0d", nmis);
            if (f_mode == "frame") begin
                if ($test$plusargs("PAL")) begin
                    // the PAL PPU blacks the two leftmost and the rightmost pixels of every line (ppu.sv mask_pal); the rest is the same
                    check(px(lb, 2, 0) === C_BAND && px(lb, 253, 7) === C_BAND && px(lb, 128, 4) === C_BAND, "band: colour 0x16 over rows 0..7");
                    check(px(lb, 10, 8) === C_BACK && px(lb, 2, 100) === C_BACK && px(lb, 253, 223) === C_BACK, "backdrop 0x21 elsewhere");
                    check(px(lb, 0, 100) === 16'h0000 && px(lb, 1, 100) === 16'h0000 && px(lb, 255, 100) === 16'h0000, "PAL: the edge pixels of a line are black");
                end else begin
                    check(px(lb, 0, 0) === C_BAND && px(lb, 255, 7) === C_BAND && px(lb, 128, 4) === C_BAND, "band: colour 0x16 over rows 0..7");
                    check(px(lb, 10, 8) === C_BACK && px(lb, 0, 100) === C_BACK && px(lb, 255, 223) === C_BACK, "backdrop 0x21 elsewhere");
                end
                check(px(lb, 32, 8) === C_BAND && px(lb, 33, 8) === C_WHITE && px(lb, 32, 9) === C_WHITE && px(lb, 63, 15) === C_BAND, "checker at x 32..63, rows 8..15: colours 1 and 2 alternate");
                check(px(lb, 31, 8) === C_BACK && px(lb, 64, 8) === C_BACK && px(lb, 40, 16) === C_BACK, "checker has the right edges");
                check(px(lb, 100, 53) === C_SPR && px(lb, 107, 60) === C_SPR && px(lb, 103, 56) === C_SPR, "sprite: colour 0x2A at x 100..107, rows 53..60");
                check(px(lb, 99, 56) === C_BACK && px(lb, 108, 56) === C_BACK && px(lb, 103, 52) === C_BACK && px(lb, 103, 61) === C_BACK, "sprite has the right edges");
            end
            if (f_mode == "tone") begin
                $display("     %0d audio samples (2112-clock windows), min %0d max %0d", ns, sw_min, sw_max);
                check(ns > 20 && (sw_max - sw_min) > 1000000, "pulse channel: the samples swing by more than 1,000,000 of the 24 bit range");
            end else begin
                // silence is silence: the DC tracker leaves exactly 0
                sound_on = 1; sw_min = 32'sh7FFFFFFF; sw_max = -32'sh7FFFFFFF; ns = 0;
                #5000000; sound_on = 0;
                check(ns > 20 && sw_min == 0 && sw_max == 0, "no sound programmed: the samples are exactly 0");
            end
        end

        if (f_mode == "check") begin
            if (!$value$plusargs("EXPECT=%s", f_exp)) begin $display("need +EXPECT="); $finish; end
            $readmemh(f_exp, exp_w);
            if (!$value$plusargs("NEXP=%d", n_exp)) begin $display("need +NEXP="); $finish; end
            begin : wait_done
                repeat (1000) begin #100000; if (A.mem.wram[11'h3FF] == 8'h42) disable wait_done; end
            end
            check(A.mem.wram[11'h3FF] == 8'h42, "the program ran to its end marker");
            cpu_bad = 0;
            for (k_exp = 0; k_exp < n_exp; k_exp = k_exp + 1)
                if (A.mem.wram[exp_w[k_exp][18:8]] !== exp_w[k_exp][7:0]) begin
                    if (cpu_bad < 8) $display("FAIL result at $%04h: got %h expected %h", exp_w[k_exp][23:8], A.mem.wram[exp_w[k_exp][18:8]], exp_w[k_exp][7:0]);
                    cpu_bad = cpu_bad + 1;
                end
            check(cpu_bad == 0, "every result matches what the program must compute");
            $display("     %0d results compared, %0d differ; cache: %0d PRG and %0d CHR line fetches, held %0d clocks of %0d", n_exp, cpu_bad,
                     A.mem.st_miss_prg, A.mem.st_miss_chr, A.mem.st_stall, $time / 10);
        end

        if (f_mode == "inv") begin
            #3000000;
            check(A.mem.wram[11'h400] == 8'h11, "the loop reads the byte the image holds ($11)");
            dma(1'b0, 1'b1, 16'h0100, 8'h22, rdb);               // the image changes under the running machine
            #3000000;
            check(A.mem.wram[11'h400] == 8'h22, "window write seen by the running loop ($22): line dropped");
            dma(1'b0, 1'b1, 16'h0100, 8'h33, rdb);
            #3000000;
            check(A.mem.wram[11'h400] == 8'h33, "and again ($33)");
        end

        if (f_mode == "win") begin
            creg_w(12'h0A0, 8'h01);                   // reset hold again; the window works either way
            dma(1'b0, 1'b0, 16'h7FFC, 8'd0, rdb);  check(rdb === img[16 + 32764], "PRG read back: reset vector low byte (image offset 0x7FFC)");
            dma(1'b0, 1'b0, 16'h7FFD, 8'd0, rdb);  check(rdb === img[16 + 32765], "PRG read back: reset vector high byte");
            dma(1'b1, 1'b0, 16'h0010, 8'd0, rdb);  check(rdb === 8'hFF, "CHR read back: tile 1 plane 0");
            dma(1'b0, 1'b1, 16'h7FFF, 8'h5A, rdb);
            dma(1'b0, 1'b0, 16'h7FFF, 8'd0, rdb);  check(rdb === 8'h5A, "PRG write at the last byte of the image reads back");
            dma(1'b0, 1'b0, 16'h7FFE, 8'd0, rdb);  check(rdb === img[16 + 32766], "the byte before it is untouched (one lane of the 16 byte word written)");
            dma(1'b1, 1'b1, 16'h1FFF, 8'hA5, rdb);
            dma(1'b1, 1'b0, 16'h1FFF, 8'd0, rdb);  check(rdb === 8'hA5, "CHR write at the last byte reads back");
            dma(1'b0, 1'b0, 16'h0000, 8'd0, rdb);  check(rdb === img[16], "PRG first byte untouched by those writes");
            dma(1'b0, 1'b1, 16'h8005, 8'hEE, rdb);
            dma(1'b0, 1'b0, 16'h8005, 8'd0, rdb);  check(rdb === 8'hEE, "a write past the 32 KB the image uses lands in the DDR2 (16 bit window address)");
            dma(1'b0, 1'b0, 16'h0005, 8'd0, rdb);  check(rdb === img[16 + 5], "and does not alias into offset 5");
            creg_w(12'h0D4, 8'h01); dma(1'b0, 1'b0, 16'h0005, 8'd0, rdb); check(rdb === 8'd0, "window page 1 is another 64 KB: offset 0x10005 reads 0");
            dma(1'b0, 1'b1, 16'h0005, 8'h33, rdb); dma(1'b0, 1'b0, 16'h0005, 8'd0, rdb); check(rdb === 8'h33, "a write in page 1 reads back there");
            creg_w(12'h0D4, 8'h00);
            dma(1'b0, 1'b0, 16'h0005, 8'd0, rdb);  check(rdb === img[16 + 5], "and page 0 is untouched");
            creg_w(12'h0D4, 8'h1F); dma(1'b1, 1'b1, 16'hFFFF, 8'h77, rdb); dma(1'b1, 1'b0, 16'hFFFF, 8'd0, rdb);
            check(rdb === 8'h77, "the top of the window (page 31, CHR) reads back"); creg_w(12'h0D4, 8'h00);
            creg_w(12'h0D0, 8'h00); creg_w(12'h0D1, 8'h00); creg_w(12'h0D2, 8'h20); creg_w(12'h0D3, 8'h01);     // CART_BASE = 0x01200000 (the old CHR area: PRG offset 0x7FFD is there 0)
            dma(1'b0, 1'b0, 16'h7FFD, 8'd0, rdb);  check(rdb === 8'd0, "another CART_BASE is another place: the old image is not there");
            creg_w(12'h0D2, 8'h00); creg_w(12'h0D3, 8'h01);
            dma(1'b0, 1'b0, 16'h7FFD, 8'd0, rdb);  check(rdb === img[16 + 32765], "back at the default CART_BASE the image reads again");
            creg_r(12'h000, rdb); check(rdb === "N", "register ID byte 0 = N");
            creg_r(12'h001, rdb); check(rdb === "E", "register ID byte 1 = E");
            creg_r(12'h002, rdb); check(rdb === "S", "register ID byte 2 = S");
            creg_r(12'h003, rdb); check(rdb === "_", "register ID byte 3 = _");
            creg_r(12'h008, rdb); check(rdb === 8'h78, "register BUILD = USR_ACCESS");
            creg_r(12'h0E4, rdb); check(rdb === 8'd2, "INES byte 4 (PRG pages) reads back what load_cart wrote");
            creg_w(12'h0E6, 8'h10); creg_r(12'h0E6, rdb); check(rdb === 8'h10, "INES byte 6 reads back what was written");
            check(A.mapper_flags[7:0] === 8'h01, "nes_flags: mapper number 1 from header byte 6 = 0x10");
            check(A.mapper_flags[10:8] === 3'd1 && A.mapper_flags[13:11] === 3'd0 && A.mapper_flags[15] === 1'b0, "nes_flags: 32 KB PRG, 8 KB CHR ROM");
            creg_w(12'h0E6, 8'h00);
            creg_r(12'h0A4, rdb); check(rdb[0] === 1'b1, "STATUS b0: in reset while the reset hold is set");
            check(j_rdata !== 32'hx, "debug word 0 answers");
        end

        if (f_mode == "pause") begin
            // both run from the same reset; A free, B asked to pause after 3 ms. The machine pauses at the next vblank on an
            // instruction boundary (nes.v: pausecore), so wait for it, then hold it for 5 ms and let it go.
            #3000000;
            if (!$test$plusargs("NOHOLD")) hold_b = 1;
            t_req = $time;
            begin : wait_halt
                repeat (400) begin #100000; if (halted_b === 1'b1) disable wait_halt; end
            end
            check(halted_b === 1'b1, "B halted within 40 ms of the request (pause point reached)");
            $display("     B halted %0d us after the request", ($time - t_req) / 1000);
            dots0 = B.dot_cnt; cyc0 = B.cyc_cnt; fr0 = B.frames; ad0 = B.cpumem_addr; pa0 = B.ppumem_addr; cb_hold = cb; writes_b = 0;
            #(5 * 1000000.0);
            check(B.dot_cnt == dots0 && B.cyc_cnt == cyc0 && B.frames == fr0, "B's dot, CPU cycle and frame counters stood still for 5 ms held");
            check(B.cpumem_addr == ad0 && B.ppumem_addr == pa0, "B's CPU and PPU request addresses did not move while held");
            check(cb == cb_hold, "B ticked no live clock while held");
            check(writes_b == 0, "B wrote nothing to the DDR2 while held");
            check(halted_b === 1'b1, "B still reports halted");
            hold_b = 0;
            #(160 * 1000000.0);
            check(halted_b === 1'b0, "B runs again after the request is released");
            $display("     A %0d live ticks, B %0d", ca, cb);
            nfr = (A.frames < B.frames ? A.frames : B.frames) - 1;
            for (i = 3; i < nfr; i = i + 1)
                if (fh_a[i] !== fh_b[i]) begin $display("FAIL bus hash of frame %0d differs", i); fails = fails + 1; i = 1000; end
            if (i < 1000) begin
                $display("ok   paused and free machine agree, frame for frame (frames 3..%0d, %0d requests hashed per frame)", nfr - 1, fa[3] - fa[2]);
                $display("     frame starts, live ticks: A %0d %0d %0d, B %0d %0d %0d (B-A %0d, %0d, %0d)", fa[2], fa[3], fa[4], fbb[2], fbb[3], fbb[4], fbb[2] - fa[2], fbb[3] - fa[3], fbb[4] - fa[4]);
            end
            check(nfr >= 8, "at least four frames (1.4 M live ticks) were compared");
        end
        if (f_mode != "frame" && f_mode != "tone" && f_mode != "win" && f_mode != "pause" &&
            f_mode != "check" && f_mode != "inv") begin $display("FAIL unknown +MODE=%0s: nothing was checked", f_mode); fails = fails + 1; end
        if (fails == 0) $display("PASS tb_nes %0s", f_mode); else $display("FAILED tb_nes %0s (%0d)", f_mode, fails);
        $finish;
    end
endmodule
