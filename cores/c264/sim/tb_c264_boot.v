// T90, LOCAL ONLY (the ROMs are third-party and never in the repository): the whole C264 core with real 264 ROMs.
// Loads KERNAL/BASIC (and function ROM) through the app's DMA window, resets, waits, dumps the 40x25 text screen read back
// through the window, types a key, dumps again, writes the newest frame buffer (the pixel writer's DDR2 traffic) as a PPM.
// Plusargs: +KERNAL=hex +BASIC=hex [+FUNC_LO=hex +FUNC_HI=hex] +MS=machine ms +OUT=prefix +MODEL=0|1 +RAM=0|1 +KEYMS=ms
`timescale 1ns / 1ps
module tb_c264_boot;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0, ui_clk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #40.69 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;
    reg sys_rst = 1, reset_req = 1;
    reg halt_req = 0;
    reg [63:0] keys = 0;
    wire halted, running;
    reg dma_req_tgl = 0, dma_we = 0, dma_rom = 0;
    reg [15:0] dma_addr = 0;
    reg [7:0] dma_wdata = 0;
    wire dma_done_tgl;
    wire [7:0] dma_rdata;
    wire win_use, win_rom, win_we;
    wire [15:0] win_addr;
    wire [7:0] win_wd, ram_rdata;
    wire [3:0] pix_color;
    wire [9:0] pix_x;
    wire [8:0] pix_y;
    wire signed [23:0] audio_l, audio_r;
    reg cw_we = 0, cw_re = 0;
    reg [16:0] cw_addr = 0;
    reg [7:0] cw_wdata = 0;
    wire [7:0] cw_rdata;
    wire iec_atn_o, iec_clk_o, iec_data_o;
    wire [26:0] e_addr;
    wire [2:0] e_cmd;
    wire e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;
    reg e_rdy = 1;
    c264_core dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(1'b0),
        .reset_req(reset_req), .halt_req(halt_req), .halted(halted), .running(running),
        .keys(keys), .joy1(5'd0), .joy2(5'd0), .restore(1'b0),
        .tp_read_pin(1'b1), .tp_sense_pin(1'b1), .iec_atn_i(1'b1), .iec_clk_i(1'b1), .iec_data_i(1'b1), .iec_atn_o(iec_atn_o), .iec_clk_o(iec_clk_o), .iec_data_o(iec_data_o),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(1'b0),
        .win_port_use(win_use), .win_port_addr(win_addr), .win_port_rom(win_rom), .win_port_we(win_we), .win_port_wdata(win_wd),
        .port_req(win_use), .port_addr(win_addr), .port_rom(win_rom), .port_we(win_we), .port_wdata(win_wd), .ram_rdata(ram_rdata),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(), .cw_irq(),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0),
        .build(32'd0), .ddr_cal(64'd0), .dbg_re(1'b0), .dbg_addr(12'd0), .j_rdata(), .j_rvalid(),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // the host's e_ port and the frame buffers (default base 0x1D00000 bytes, stride 0x100000)
    reg [127:0] ddr [0:262143];
    always @(posedge ui_clk) begin
        e_rdy <= 1'b1;
        if (e_en && e_rdy) ddr[((e_addr >> 3) - 27'h1D0000) & 18'h3FFFF] <= e_wdf_data;
    end

    reg [7:0] rom_k [0:16383], rom_b [0:16383], rom_fl [0:16383], rom_fh [0:16383];
    reg [8*200-1:0] f_k, f_b, f_fl, f_fh, outp;
    integer ms, keyms, model, ram, have_func, i;
    reg [7:0] r;
    reg [7:0] scr [0:999];

    task cwr(input [16:0] a, input [7:0] d);
        begin @(posedge cpu_clk); cw_addr <= a; cw_wdata <= d; cw_we <= 1; @(posedge cpu_clk); cw_we <= 0; end
    endtask
    task dma(input rom, input we, input [15:0] a, input [7:0] d, output [7:0] q);
        reg t;
        begin
            t = dma_done_tgl;
            @(posedge sys_clk);
            dma_rom <= rom; dma_we <= we; dma_addr <= a; dma_wdata <= d;
            dma_req_tgl <= ~dma_req_tgl;
            wait (dma_done_tgl != t);
            @(posedge sys_clk);
            q = dma_rdata;
        end
    endtask
    task load_rom(input integer sel, input integer which);
        integer j;
        begin
            cwr(17'h0B0, sel);
            repeat (8) @(posedge sys_clk);
            for (j = 0; j < 16384; j = j + 1) begin
                case (which) 0: r = rom_k[j]; 1: r = rom_b[j]; 2: r = rom_fl[j]; default: r = rom_fh[j]; endcase
                dma(1, 1, (j < 8192 ? 16'h4000 : 16'h2000 - 16'h2000 + 16'h2000) + (j < 8192 ? j : j - 8192), r, r);
            end
        end
    endtask
    function [7:0] asc(input [7:0] c);
        reg [6:0] v;
        begin
            v = c[6:0];
            if (v == 0) asc = "@";
            else if (v <= 26) asc = 8'd64 + v;
            else if (v >= 32 && v <= 63) asc = v;
            else asc = ".";
        end
    endfunction
    task grab_screen;
        integer j;
        begin
            for (j = 0; j < 1000; j = j + 1) begin dma(0, 0, 16'h0C00 + j, 8'd0, r); scr[j] = r; end
        end
    endtask
    task show_screen;
        integer row, col;
        reg [8*40-1:0] line;
        begin
            for (row = 0; row < 25; row = row + 1) begin
                for (col = 0; col < 40; col = col + 1) line[(39 - col) * 8 +: 8] = asc(scr[row * 40 + col]);
                $display("|%0s|", line);
            end
        end
    endtask
    function has(input [8*32-1:0] s, input integer n);   // n characters of s (left aligned in the low bytes) anywhere on screen
        integer p, k;
        reg ok;
        begin
            has = 0;
            for (p = 0; p + n <= 1000; p = p + 1) begin
                ok = 1;
                for (k = 0; k < n; k = k + 1) if (asc(scr[p + k]) != s[(n - 1 - k) * 8 +: 8]) ok = 0;
                if (ok) has = 1;
            end
        end
    endfunction
    task dump_ppm(input integer buf_i);
        integer fd, y, x, wi;
        reg [127:0] w;
        reg [15:0] px;
        begin
            fd = $fopen({outp, ".ppm"}, "wb");
            $fwrite(fd, "P6\n384 288\n255\n");
            for (y = 0; y < 288; y = y + 1)
                for (x = 0; x < 384; x = x + 1) begin
                    w = ddr[buf_i * 65536 + y * 48 + x / 8];
                    px = w >> ((x % 8) * 16);
                    $fwrite(fd, "%c%c%c", {px[15:11], px[15:13]}, {px[10:5], px[10:9]}, {px[4:0], px[4:2]});
                end
            $fclose(fd);
        end
    endtask

    integer ph_bad = 0;
    always @(posedge sys_clk) if (dut.ph !== dut.mos8360.phicounter[1:0]) ph_bad <= ph_bad + 1;
    integer fails = 0, zc, maxa, t0, atn_edges = 0;
    reg atn_q = 1;
    always @(posedge sys_clk) begin atn_q <= iec_atn_o; if (atn_q && !iec_atn_o) atn_edges <= atn_edges + 1; end
    reg signed [23:0] prev;
    task wr(input [15:0] a, input [7:0] d); reg [7:0] q; begin dma(0, 1, a, d, q); end endtask
    task typekeys;
        begin wr(16'h0527, 8'h0D); wr(16'h0528, 8'h52); wr(16'h0529, 8'h55); wr(16'h052A, 8'h4E); wr(16'h052B, 8'h0D); wr(16'h00EF, 8'd5); end
    endtask
    initial begin
        if (!$value$plusargs("KERNAL=%s", f_k) || !$value$plusargs("BASIC=%s", f_b)) begin $display("need +KERNAL= +BASIC= (hex files)"); $finish; end
        if (!$value$plusargs("MS=%d", ms)) ms = 1600;
        if (!$value$plusargs("KEYMS=%d", keyms)) keyms = 200;
        if (!$value$plusargs("MODEL=%d", model)) model = 0;
        if (!$value$plusargs("RAM=%d", ram)) ram = 0;
        if (!$value$plusargs("OUT=%s", outp)) outp = "c264";
        $readmemh(f_k, rom_k); $readmemh(f_b, rom_b);
        have_func = $value$plusargs("FUNC_LO=%s", f_fl) && $value$plusargs("FUNC_HI=%s", f_fh);
        if (have_func) begin $readmemh(f_fl, rom_fl); $readmemh(f_fh, rom_fh); end
        #200; sys_rst = 0;
        load_rom(0, 0); load_rom(1, 1);
        if (have_func) begin load_rom(2, 2); load_rom(3, 3); end
        $display("ROMs written through the window");
        cwr(17'h0A1, model); cwr(17'h0A2, ram);
        repeat (20) @(posedge sys_clk);
        reset_req = 0;
        #(ms * 1000000.0);
        $display("machine ms %0d, frames %0d", ms, dut.frames);
        grab_screen; show_screen;
        if (has("READY.", 6)) $display("ok   READY."); else begin $display("FAIL no READY."); fails = fails + 1; end
        if (has("BYTES FREE", 10)) $display("ok   BYTES FREE"); else begin $display("FAIL no BYTES FREE"); fails = fails + 1; end
        if (has("BASIC V3.5", 10)) $display("ok   BASIC V3.5"); else begin $display("FAIL no BASIC V3.5"); fails = fails + 1; end
        $display("ph == phicounter[1:0] now: %0d", dut.ph == dut.mos8360.phicounter[1:0]);
        // the newest complete frame
        cwr(17'h0C0, 0);
        @(posedge cpu_clk); cw_addr <= 17'h0C0; cw_re <= 1; @(posedge cpu_clk); cw_re <= 0; #1;
        $display("FB_LATEST %02x", cw_rdata);
        dump_ppm(cw_rdata[1:0]);
        // type A (C64 matrix col 1 row 2 = key 10)
        keys[10] = 1; #(keyms * 1000000.0); keys[10] = 0; #(300 * 1000000.0);
        grab_screen; show_screen;
        if (!has("A", 1)) begin $display("FAIL the A key typed nothing"); fails = fails + 1; end else $display("ok   the A key typed an A");
        // ---- Run PRG as the app does it: BASIC program at $1001, links, pointers $2D-$32, RUN into the keyboard buffer ($0527, count $EF)
        // 10 PRINT "OK"
        wr(16'h1001, 8'h0C); wr(16'h1002, 8'h10); wr(16'h1003, 8'h0A); wr(16'h1004, 8'h00); wr(16'h1005, 8'h99); wr(16'h1006, 8'h20);
        wr(16'h1007, 8'h22); wr(16'h1008, 8'h4F); wr(16'h1009, 8'h4B); wr(16'h100A, 8'h22); wr(16'h100B, 8'h00); wr(16'h100C, 8'h00); wr(16'h100D, 8'h00);
        for (i = 0; i < 6; i = i + 2) begin wr(16'h2D + i, 8'h0E); wr(16'h2E + i, 8'h10); end
        typekeys;
        #(600 * 1000000.0);
        grab_screen; show_screen;
        if (has("OK", 2)) $display("ok   the PRG ran and printed OK"); else begin $display("FAIL the PRG did not print OK"); fails = fails + 1; end
        // ---- the serial bus: DIRECTORY with nothing on the bus asserts ATN and ends in ?DEVICE NOT PRESENT
        atn_edges = 0;
        wr(16'h0527, 8'h44); wr(16'h0528, 8'h49); wr(16'h0529, 8'h52); wr(16'h052A, 8'h45); wr(16'h052B, 8'h43);
        wr(16'h052C, 8'h54); wr(16'h052D, 8'h4F); wr(16'h052E, 8'h52); wr(16'h052F, 8'h59); wr(16'h0530, 8'h0D); wr(16'h00EF, 8'd10);
        #(1500 * 1000000.0);
        grab_screen; show_screen;
        if (atn_edges > 0) $display("ok   ATN was asserted %0d times", atn_edges); else begin $display("FAIL ATN never asserted"); fails = fails + 1; end
        if (has("DEVICE NOT PRESENT", 18)) $display("ok   ?DEVICE NOT PRESENT with nothing on the bus"); else begin $display("FAIL no DEVICE NOT PRESENT"); fails = fails + 1; end
        // ---- sound: 10 SYS4109 and a machine code loop that sets voice 1 (volume 15, voice 1 on, N = 0x300: 110840/256 = 433 Hz PAL)
        wr(16'h1001, 8'h0B); wr(16'h1002, 8'h10); wr(16'h1003, 8'h0A); wr(16'h1004, 8'h00); wr(16'h1005, 8'h9E);
        wr(16'h1006, 8'h34); wr(16'h1007, 8'h31); wr(16'h1008, 8'h30); wr(16'h1009, 8'h39); wr(16'h100A, 8'h00); wr(16'h100B, 8'h00); wr(16'h100C, 8'h00);
        wr(16'h100D, 8'hA9); wr(16'h100E, 8'h1F); wr(16'h100F, 8'h8D); wr(16'h1010, 8'h11); wr(16'h1011, 8'hFF);
        wr(16'h1012, 8'hA9); wr(16'h1013, 8'h00); wr(16'h1014, 8'h8D); wr(16'h1015, 8'h0E); wr(16'h1016, 8'hFF);
        wr(16'h1017, 8'hA9); wr(16'h1018, 8'h07); wr(16'h1019, 8'h8D); wr(16'h101A, 8'h12); wr(16'h101B, 8'hFF);
        wr(16'h101C, 8'h4C); wr(16'h101D, 8'h1C); wr(16'h101E, 8'h10);
        for (i = 0; i < 6; i = i + 2) begin wr(16'h2D + i, 8'h0D); wr(16'h2E + i, 8'h10); end
        typekeys;
        #(300 * 1000000.0);
        zc = 0; maxa = 0; prev = audio_l; t0 = $time;
        while ($time - t0 < 400000000) begin
            @(posedge mclk);
            if ((audio_l > 0) != (prev > 0) && audio_l != prev) zc = zc + 1;
            if (audio_l > maxa) maxa = audio_l;
            prev = audio_l;
        end
        $display("sound: %0d zero crossings in 400 ms (433 Hz = 346), peak %0d", zc, maxa);
        if (zc >= 330 && zc <= 362 && maxa > 100000) $display("ok   TED voice 1 at the right pitch"); else begin $display("FAIL TED sound"); fails = fails + 1; end
        halt_req = 1;
        #(5 * 1000000.0);
        if (audio_l == 0 && halted) $display("ok   silent and halted while frozen"); else begin $display("FAIL not silent/halted (audio %0d halted %0d)", audio_l, halted); fails = fails + 1; end
        halt_req = 0;
        if (ph_bad == 0) $display("ok   the core's dot phase equals the TED's phicounter[1:0] on every clock"); else begin $display("FAIL dot phase differs from phicounter on %0d clocks", ph_bad); fails = fails + 1; end
        if (fails == 0) $display("PASS tb_c264_boot"); else $display("FAILED tb_c264_boot");
        $finish;
    end
endmodule
