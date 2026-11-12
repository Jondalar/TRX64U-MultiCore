// T142: bench of the SNES core (snes_core with cores/snes/gen, the DDR2 models, an application model that sets the core up through the register block like
// coreapp/cores/snes.cc does: hold, the mapper byte, the masks, the S-SMP's boot ROM (our own 64 byte stub, tools/snes_asm.py spcstub), the image straight
// into the DDR2 model, release). The program is ours (cores/snes/sim/prog/*.s, assembled by tools/snes_asm.py). It reports by writing its final word,
// DONE = 0xD0 at WRAM $7E:FFFF; when that is there the bench lets the machine run on for +SETTLE frames, then prints hashes of the machine's memories,
// the picture and the counters on lines that start with SNES (sim/run.sh compares them between the build behind snes_mem.v and the reference build with
// the memory that is never late, -DSNES_REF).
//   plusargs: +ROM=<image> +IPL=<64 byte file> +MS=<machine milliseconds at most> +SETTLE=<frames after DONE> [+TYPE=<n> +MASK=<hex> +RAMMASK=<hex> +PAL]
`timescale 1ns / 1ps
module tb_snes;
    reg cpu_clk = 0, ui_clk = 0, sys_clk = 0, mclk = 0;
    always #5 cpu_clk = ~cpu_clk;
    always #6.6667 ui_clk = ~ui_clk;
    always #15.86 sys_clk = ~sys_clk;
    always #40.69 mclk = ~mclk;
    reg sys_rst = 1, ui_rst = 1;

    reg  [4:0]  joy1 = 5'd0, joy2 = 5'd0;
    reg  [63:0] keys = 64'd0;
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
    reg [7:0] ac = 8'd0; reg asen = 1'b0;                  // the host's frame: 256 mclk, the request half a frame ahead
    always @(posedge mclk) begin ac <= ac + 8'd1; asen <= (ac == 8'd127); end
    wire [3:0]  pix_color;
    wire [9:0]  pix_x;
    wire [8:0]  pix_y;
    snes_core #(.ID_TRX2(0)) dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(reset_req), .halt_req(halt_req), .halted(halted), .running(running),
        .c64_joy1(joy1), .c64_joy2(joy2), .c64_keys(keys),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r), .audio_sample_en(asen),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0), .build(32'h12345678), .ddr_cal(64'd0),
        .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid)
    );
    snes_ddr2c cm (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
                   .c_wdf_wren(c_wdf_wren), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));
    snes_ddr2e em (.ui_clk(ui_clk), .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_wren(e_wdf_wren));

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
    task cw_rd32(input [16:0] a, output [31:0] v);
        reg [7:0] b0, b1, b2, b3;
        begin cw_rd(a, b0); cw_rd(a + 1, b1); cw_rd(a + 2, b2); cw_rd(a + 3, b3); v = {b3, b2, b1, b0}; end
    endtask

    // ---------------------------------------------------------------- the memory behind the machine, as the bench sees it
    function [7:0] ddr_byte(input [31:0] off);        // off: byte offset from the cartridge base
        begin
`ifdef SNES_REF
            ddr_byte = off < 32'h800000 ? dut.mem.rom[off] : off < 32'h820000 ? dut.mem.wram[off - 32'h800000] : dut.mem.bsr[off - 32'h820000];
`else
            ddr_byte = cm.mem[off >> 4][8 * off[3:0] +: 8];
`endif
        end
    endfunction
    task put_byte(input [31:0] off, input [7:0] b);
        begin
`ifdef SNES_REF
            dut.mem.rom[off] = b;
`else
            cm.mem[off >> 4][8 * off[3:0] +: 8] = b;
`endif
        end
    endtask

    reg [7:0] img [0:8388607];
    reg [7:0] ipl [0:63];
    integer fd, n, i;
    reg [1023:0] fname;
    reg [63:0] h;
    function [63:0] fnv(input [63:0] hh, input [7:0] b);
        begin fnv = (hh ^ {56'd0, b}) * 64'h00000100000001B3; end
    endfunction
    integer anz2; reg [63:0] ah;
    integer max_ms, settle, rom_type, rom_mask, ram_mask, dump_ms, press_idx, key_ms, hold_ms, key_every, dumpn;
    reg [8*9-1:0] gname;
    integer clocks = 0, done_at = 0;
    reg [23:0] lo, hi;
    reg [7:0] t8;
    reg [31:0] t32;
    reg [15:0] fr0, fr1;
    reg [7:0] latest;
    always @(posedge cpu_clk) clocks <= clocks + 1;
    reg ak_q = 1'b0;
    reg [10:0] age_q = 11'd0;
    always @(posedge mclk) begin
        ak_q <= (dut.audio.ak_s[2] != dut.audio.ak_s[1]);
        if ($test$plusargs("ALOG") && ak_q) $display("SNES as %0d %0d %0d", audio_l, dut.a_l, $time);
    end
    always @(posedge cpu_clk) begin
        if ($test$plusargs("ALOG") && dut.audio.cap && (dut.audio.l_in != dut.audio.lp || dut.audio.r_in != dut.audio.rp)) $display("SNES ev %0d %0d %0d", dut.audio.age, dut.audio.l_in, $time);
    end
    // +DSPLOG: one line per S-DSP sample (snd_rdy rises): the envelope of voice 0 (both ends of the flattened array until the order is known), ENVX of the voices, the pair
    reg rdy_q = 1'b0;
    integer dsp_n = 0;
    always @(posedge cpu_clk) begin
        rdy_q <= dut.m.snes_i.dsp.snd_rdy;
        if ($test$plusargs("DSPLOG") && dut.m.snes_i.dsp.snd_rdy && !rdy_q) begin
            dsp_n <= dsp_n + 1;
            $display("SNES dsp %0d %0d %0d %0d %0d", dsp_n, dut.m.snes_i.dsp.env[11:0], dut.m.snes_i.dsp.env[95:84], dut.m.snes_i.dsp.audio_l, dut.m.snes_i.dsp.audio_r);
        end
    end
    // +BRR=<file>: the four BRR blocks of tools/snes_brr.py into the audio RAM, and every word the BRR decoder writes into its ring buffer (voice/slot address, value)
    always @(posedge cpu_clk) if ($test$plusargs("BRR") && dut.ce && dut.m.snes_i.dsp.brr_buf_we)
        $display("SNES brr %0d %0d", dut.m.snes_i.dsp.brr_buf_addr_a, $signed(dut.m.snes_i.dsp.brr_buf_di));
    integer strb_n = 0, clk1_n = 0, clk2_n = 0, smp_wr_n = 0;                      // controller port activity of the machine, counted for the game dumps
    reg strb_q = 1'b0, c1_q = 1'b0, c2_q = 1'b0;
    always @(posedge cpu_clk) begin
        strb_q <= dut.joy_strb; c1_q <= dut.joy1_clk; c2_q <= dut.joy2_clk;
        if (dut.joy_strb && !strb_q) strb_n <= strb_n + 1;
        if (dut.joy1_clk && !c1_q) clk1_n <= clk1_n + 1;
        if (dut.joy2_clk && !c2_q) clk2_n <= clk2_n + 1;
    end
    integer awr_hi = 0, awr_lo = 0;
    always @(posedge cpu_clk) if (dut.ce && !dut.aram_ce_n && !dut.aram_we_n) begin
        if (dut.aram_addr >= 16'h100) begin awr_hi <= awr_hi + 1; if (awr_hi < 12) $display("SNES aram write %04h <= %02h", dut.aram_addr, dut.aram_d); end
        else awr_lo <= awr_lo + 1;
    end
    // the sound the core hands to the host (24 bit): how many mclk clocks it was not zero, and its peak
    integer anz = 0, amax = 0;
    always @(posedge mclk) begin
        if (audio_l != 24'sd0 || audio_r != 24'sd0) anz <= anz + 1;
        if ((audio_l < 0 ? -audio_l : audio_l) > amax) amax <= (audio_l < 0 ? -audio_l : audio_l);
    end
    // a pixel of the newest picture (RGB565, little endian in the DDR2 word) for the run script to compare
    reg [127:0] pxw;
    task pxs(input integer x, input integer y);
        begin
            pxw = em.fb[latest[1:0] * 32768 + ((y * 1024 + x * 2) >> 4)];
            $display("SNES px %0d %0d %02h%02h", x, y, pxw[8 * ((y * 1024 + x * 2) % 16 + 1) +: 8], pxw[8 * ((y * 1024 + x * 2) % 16) +: 8]);
        end
    endtask

    initial begin
        if (!$value$plusargs("MS=%d", max_ms)) max_ms = 200;
        if (!$value$plusargs("SETTLE=%d", settle)) settle = 2;
        if (!$value$plusargs("TYPE=%d", rom_type)) rom_type = 0;
        if (!$value$plusargs("MASK=%h", rom_mask)) rom_mask = 24'h07FFFF;
        if (!$value$plusargs("RAMMASK=%h", ram_mask)) ram_mask = 0;
        if (!$value$plusargs("ROM=%s", fname)) begin $display("FAIL no +ROM"); $finish; end
        for (i = 0; i < 8388608; i = i + 1) img[i] = 8'h00;
        fd = $fopen(fname, "rb");
        if (fd == 0) begin $display("FAIL cannot open the image"); $finish; end
        n = $fread(img, fd);
        $fclose(fd);
        if (!$value$plusargs("IPL=%s", fname)) begin $display("FAIL no +IPL"); $finish; end
        fd = $fopen(fname, "rb");
        n = $fread(ipl, fd);
        $fclose(fd);
        for (i = 0; i < 8388608; i = i + 1) if (img[i] != 8'h00) put_byte(i, img[i]);
        if ($test$plusargs("AUDIO")) begin                            // the sample directory at $0200 and one looping BRR block at $0300 (the boot stub plays it)
            dut.aram[16'h0200] = 8'h00; dut.aram[16'h0201] = 8'h03; dut.aram[16'h0202] = 8'h00; dut.aram[16'h0203] = 8'h03;
            dut.aram[16'h0300] = 8'hC3;
            for (i = 1; i < 9; i = i + 1) dut.aram[16'h0300 + i] = 8'h78;
            if ($value$plusargs("BRR=%s", fname)) begin               // four BRR blocks, filter 0..3 (tools/snes_brr.py gen), the loop back to the first
                fd = $fopen(fname, "rb");
                n = $fread(img, fd);
                $fclose(fd);
                for (i = 0; i < 36; i = i + 1) dut.aram[16'h0300 + i] = img[i];
            end
            if ($test$plusargs("DC")) for (i = 1; i < 9; i = i + 1) dut.aram[16'h0300 + i] = 8'h77;   // a constant 0x3800: the envelope shows in the output
            if ($test$plusargs("SINE")) begin                         // one period of a sine in 16 samples (range 12, filter 0, nibbles -7..7), looping
                dut.aram[16'h0300] = 8'hC3;
                dut.aram[16'h0301] = 8'h03; dut.aram[16'h0302] = 8'h56; dut.aram[16'h0303] = 8'h76; dut.aram[16'h0304] = 8'h53;
                dut.aram[16'h0305] = 8'h0D; dut.aram[16'h0306] = 8'hBA; dut.aram[16'h0307] = 8'h9A; dut.aram[16'h0308] = 8'hBD;
            end
        end
        repeat (50) @(posedge cpu_clk);
        sys_rst = 0; ui_rst = 0;
        repeat (20) @(posedge cpu_clk);
        if ($test$plusargs("PAD")) begin                              // Up and fire on joystick 1, SPACE; Left on joystick 2, RIGHT SHIFT
            joy1 = 5'b10001; joy2 = 5'b00100; keys[60] = 1'b1; keys[52] = 1'b1;
        end
        cw_wr(17'h0A0, 8'h01);                                        // CTRL: hold
        cw_wr(17'h0A1, $test$plusargs("PAL") ? 8'h01 : 8'h00);
        cw_wr(17'h0A2, rom_type[7:0]);
        cw_wr(17'h0D8, rom_mask[7:0]); cw_wr(17'h0D9, rom_mask[15:8]); cw_wr(17'h0DA, rom_mask[23:16]);
        cw_wr(17'h0DC, ram_mask[7:0]); cw_wr(17'h0DD, ram_mask[15:8]); cw_wr(17'h0DE, ram_mask[23:16]);
        for (i = 0; i < 64; i = i + 1) begin cw_wr(17'h100 + i, ipl[i]); repeat (10) @(posedge cpu_clk); end
        repeat (20) @(posedge cpu_clk);
        if ($test$plusargs("APPPAD")) begin                           // T156 (named so: $test$plusargs matches by prefix and "PAD" would be true for +PADAPP): the application's USB pads, written the way it writes them (low byte, then the high byte that commits)
            cw_wr(17'h0F4, 8'h85); cw_wr(17'h0F5, 8'h0A);             // PAD1 = $0A85: B Select Right X R
            cw_wr(17'h0F6, 8'h18); cw_wr(17'h0F7, 8'h05);             // PAD2 = $0518: Start Up A L
        end
        cw_wr(17'h0A0, 8'h00);                                        // release
        if ($test$plusargs("GAME")) begin
            // a real cartridge: run for +MS machine milliseconds, dump the newest picture every +DUMPMS (file fb_<n>.hex, RGB565 per pixel), press the key +PRESS
            // (matrix index) from +KEYMS for +HOLDMS and again 1 s later (a menu may need two presses), joystick +JOYMS the same
            if (!$value$plusargs("DUMPMS=%d", dump_ms)) dump_ms = 1000;
            if (!$value$plusargs("PRESS=%d", press_idx)) press_idx = -1;
            if (!$value$plusargs("KEYMS=%d", key_ms)) key_ms = 3000;
            if (!$value$plusargs("HOLDMS=%d", hold_ms)) hold_ms = 150;
            if (!$value$plusargs("EVERYMS=%d", key_every)) key_every = 1000;
            dumpn = 0;
            while (clocks < max_ms * 100000) begin
                repeat (20000) @(posedge cpu_clk);
                if (press_idx >= 0) begin
                    keys[press_idx] = (clocks > key_ms * 100000 && ((clocks / 100000 - key_ms) % key_every) < hold_ms) ? 1'b1 : 1'b0;
                end
                if (clocks > (dumpn + 1) * dump_ms * 100000) begin
                    dumpn = dumpn + 1;
                    cw_rd(17'h0C0, latest);
                    cw_rd(17'h0A8, fr1[7:0]); cw_rd(17'h0A9, fr1[15:8]);
                    anz2 = 0; ah = 64'hCBF29CE484222325;
                    for (i = 0; i < 65536; i = i + 1) begin if (dut.aram[i] != 8'h00) anz2 = anz2 + 1; ah = fnv(ah, dut.aram[i]); end
                    $display("SNES game t=%0d ms frames=%0d latest=%02h strobes=%0d clk1=%0d clk2=%0d aram_nz=%0d aram_hash=%016h wram0500=%02h%02h %02h%02h %02h%02h %02h%02h wram14F9=%02h", clocks / 100000, fr1, latest, strb_n, clk1_n, clk2_n, anz2, ah,
                             ddr_byte(32'h800501), ddr_byte(32'h800500), ddr_byte(32'h800503), ddr_byte(32'h800502), ddr_byte(32'h800505), ddr_byte(32'h800504), ddr_byte(32'h800507), ddr_byte(32'h800506), ddr_byte(32'h8014F9));
                    strb_n = 0; clk1_n = 0; clk2_n = 0;
                    if (latest[7]) begin
                        gname = {"fb_", 8'h30 + dumpn / 10, 8'h30 + dumpn % 10, ".hex"};
                        fd = $fopen(gname, "w");
                        for (i = 0; i < 28672; i = i + 1) begin
                            pxw = em.fb[latest[1:0] * 32768 + i];
                            for (n = 0; n < 8; n = n + 1) $fwrite(fd, "%04h\n", pxw[16 * n +: 16]);
                        end
                        $fclose(fd);
                    end
                end
            end
            $display("SNES PASS game");
            $finish;
        end
        // wait for the program's DONE
        t8 = 8'h00;
        while (t8 != 8'hD0 && clocks < max_ms * 100000) begin
            repeat (20000) @(posedge cpu_clk);
            t8 = ddr_byte(32'h800000 + 32'hFFFF);
        end
        if (t8 != 8'hD0) begin
            $display("SNES FAIL timeout: no DONE after %0d ms (clocks %0d)", max_ms, clocks);
            // where the program is: the cartridge address of ~100000 clocks (the loop it is stuck in)
            lo = 24'hFFFFFF; hi = 24'h000000;
            for (i = 0; i < 100000; i = i + 1) begin @(posedge cpu_clk); if (dut.rom_addr < lo) lo = dut.rom_addr; if (dut.rom_addr > hi) hi = dut.rom_addr; end
            $display("SNES pc range %06h .. %06h  ctrl2140..43 stall=%0d", lo, hi, dut.stall);
            for (i = 0; i < 1; i = i + 1) $display("SNES wram[%04h] %02h %02h %02h %02h %02h %02h %02h %02h  %02h %02h %02h %02h %02h %02h %02h %02h", 32'hF000,
                ddr_byte(32'h80F000), ddr_byte(32'h80F001), ddr_byte(32'h80F002), ddr_byte(32'h80F003), ddr_byte(32'h80F004), ddr_byte(32'h80F005), ddr_byte(32'h80F006), ddr_byte(32'h80F007),
                ddr_byte(32'h80F008), ddr_byte(32'h80F009), ddr_byte(32'h80F00A), ddr_byte(32'h80F00B), ddr_byte(32'h80F00C), ddr_byte(32'h80F00D), ddr_byte(32'h80F00E), ddr_byte(32'h80F00F));
            $display("SNES wram0 %02h %02h %02h %02h %02h %02h %02h %02h  chk %02h%02h %02h%02h %02h%02h %02h%02h %02h%02h %02h%02h", ddr_byte(32'h800000), ddr_byte(32'h800001),
                     ddr_byte(32'h800002), ddr_byte(32'h800003), ddr_byte(32'h800004), ddr_byte(32'h800005), ddr_byte(32'h800006), ddr_byte(32'h800007),
                     ddr_byte(32'h800101), ddr_byte(32'h800100), ddr_byte(32'h800103), ddr_byte(32'h800102), ddr_byte(32'h800105), ddr_byte(32'h800104),
                     ddr_byte(32'h800107), ddr_byte(32'h800106), ddr_byte(32'h800109), ddr_byte(32'h800108), ddr_byte(32'h80010B), ddr_byte(32'h80010A));
            $finish;
        end
        done_at = clocks;
        $display("SNES aramdone 200: %02h %02h %02h %02h  300: %02h %02h  400: %02h %02h %02h %02h  0: %02h %02h %02h %02h %02h %02h", dut.aram[16'h200], dut.aram[16'h201], dut.aram[16'h202], dut.aram[16'h203], dut.aram[16'h300], dut.aram[16'h301],
                 dut.aram[16'h400], dut.aram[16'h401], dut.aram[16'h402], dut.aram[16'h403], dut.aram[0], dut.aram[1], dut.aram[2], dut.aram[3], dut.aram[4], dut.aram[5]);
        repeat (400000) @(posedge cpu_clk);
        $display("SNES aram+4ms 200: %02h %02h %02h %02h  300: %02h %02h  400: %02h %02h %02h %02h", dut.aram[16'h200], dut.aram[16'h201], dut.aram[16'h202], dut.aram[16'h203], dut.aram[16'h300], dut.aram[16'h301],
                 dut.aram[16'h400], dut.aram[16'h401], dut.aram[16'h402], dut.aram[16'h403]);
        // let it run on: +SETTLE frames of the machine
        cw_rd(17'h0A8, fr0[7:0]); cw_rd(17'h0A9, fr0[15:8]);
        fr1 = fr0;
        while (fr1 - fr0 < settle[15:0] && clocks < (max_ms + 100) * 100000) begin
            repeat (20000) @(posedge cpu_clk);
            cw_rd(17'h0A8, fr1[7:0]); cw_rd(17'h0A9, fr1[15:8]);
        end
        halt_req = 1'b1;                                              // Freeze: the machine stops at its next tick
        repeat (2000) @(posedge cpu_clk);
        // ------------------------------------------------ the hashes
        h = 64'hCBF29CE484222325;
        for (i = 0; i < 131072; i = i + 1) h = fnv(h, ddr_byte(32'h800000 + i));
        $display("SNES wram %016h", h);
        h = 64'hCBF29CE484222325;
        for (i = 0; i < 32768; i = i + 1) h = fnv(h, dut.vram1[i]);
        for (i = 0; i < 32768; i = i + 1) h = fnv(h, dut.vram2[i]);
        $display("SNES vram %016h", h);
        h = 64'hCBF29CE484222325;
        for (i = 0; i < 65536; i = i + 1) h = fnv(h, dut.aram[i]);
        $display("SNES aram %016h", h);
        cw_rd(17'h0C0, latest);
        if (latest[7]) begin
            h = 64'hCBF29CE484222325;
            for (i = 0; i < 28672; i = i + 1)
                for (n = 0; n < 16; n = n + 1) h = fnv(h, em.fb[latest[1:0] * 32768 + i][8 * n +: 8]);
            $display("SNES pic %016h", h);
        end else $display("SNES pic none");
        $display("SNES chk %02h%02h %02h%02h %02h%02h %02h%02h %02h%02h %02h%02h", ddr_byte(32'h800101), ddr_byte(32'h800100), ddr_byte(32'h800103), ddr_byte(32'h800102),
                 ddr_byte(32'h800105), ddr_byte(32'h800104), ddr_byte(32'h800107), ddr_byte(32'h800106), ddr_byte(32'h800109), ddr_byte(32'h800108),
                 ddr_byte(32'h80010B), ddr_byte(32'h80010A));
        $display("SNES pad %02h %02h %02h %02h", ddr_byte(32'h800200), ddr_byte(32'h800201), ddr_byte(32'h800202), ddr_byte(32'h800203));
        $display("SNES padm %02h%02h %02h%02h", ddr_byte(32'h800204), ddr_byte(32'h800205), ddr_byte(32'h800206), ddr_byte(32'h800207));
        if (latest[7]) begin
            pxs(10, 10); pxs(400, 10); pxs(10, 400); pxs(500, 440); pxs(255, 100); pxs(256, 100);
        end
        $display("SNES stats clocks=%0d done_at=%0d", clocks, done_at);
        cw_rd32(17'h0B0, t32); $display("SNES miss_rom %0d", t32);
        cw_rd32(17'h0B4, t32); $display("SNES miss_wram %0d", t32);
        cw_rd32(17'h0B8, t32); $display("SNES miss_bs %0d", t32);
        cw_rd32(17'h0BC, t32); $display("SNES stall_clocks %0d", t32);
        cw_rd32(17'h0F0, t32); $display("SNES writes %0d", t32);
        cw_rd(17'h0A8, fr1[7:0]); cw_rd(17'h0A9, fr1[15:8]);
        $display("SNES frames %0d", fr1);
        $display("SNES aram200 %02h %02h %02h %02h  aram300 %02h %02h %02h  aram400 %02h %02h %02h %02h %02h %02h %02h %02h  aram0 %02h %02h %02h %02h", dut.aram[16'h200], dut.aram[16'h201], dut.aram[16'h202], dut.aram[16'h203],
                 dut.aram[16'h300], dut.aram[16'h301], dut.aram[16'h302], dut.aram[16'h400], dut.aram[16'h401], dut.aram[16'h402], dut.aram[16'h403], dut.aram[16'h404], dut.aram[16'h405], dut.aram[16'h406], dut.aram[16'h407],
                 dut.aram[0], dut.aram[1], dut.aram[2], dut.aram[3]);
        $display("SNES aramwrites hi=%0d lo=%0d", awr_hi, awr_lo);
        $display("SNES audio nonzero=%0d peak=%0d", anz, amax);
        begin : peakregs                                              // the register block's peaks (0F8..0FD), read as the application does
            reg [7:0] p0, p1, p2, p3, p4, p5;
            cw_rd(17'h0F8, p0); cw_rd(17'h0F9, p1); cw_rd(17'h0FA, p2); cw_rd(17'h0FB, p3); cw_rd(17'h0FC, p4); cw_rd(17'h0FD, p5);
            $display("SNES peakregs dsp=%0d out=%0d hands=%0d", {p1, p0}, {p3, p2}, {p5, p4});
        end
        $display("SNES PASS end");
        $finish;
    end
endmodule
