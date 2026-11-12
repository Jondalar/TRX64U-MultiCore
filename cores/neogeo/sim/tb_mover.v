// T143: bench of neo_mem alone (the DDR2 service of the Neo Geo core): the image mover (LO ROM, memory card, XRAM load and save), the copy engine with its byte masks,
// the picture writer's path, the backup RAM client and the three byte channels, against the DDR2 model and a model of the machine's RAMs. One PASS or FAIL line per check.
`timescale 1ns / 1ps
module tb_mover;
    reg ui_clk = 0;
    always #6.6667 ui_clk = ~ui_clk;
    reg ui_rst = 1;
    reg [31:0] rom_base = 32'h01000000, v1_base = 32'h01400000, v2_base = 32'h01500000, m1_base = 32'h01600000, img_base = 32'h01700000;
    reg mv_go_tgl = 0;
    reg [3:0] mv_cmd = 0;
    reg [31:0] cp_src = 0, cp_dst = 0, cp_len = 0;
    wire mv_done_tgl, mv_busy;
    // the machine's side: toggles
    reg pm_req_tgl = 0, s_req_tgl = 0, c_req_tgl = 0, va_req_tgl = 0, vb_req_tgl = 0, mz_req_tgl = 0, bk_req_tgl = 0;
    reg [26:1] pm_addr = 0, s_addr = 0;
    reg [26:3] c_addr_m = 0;
    reg [24:0] va_addr = 0, vb_addr = 0, mz_addr = 0;
    wire pm_ack_tgl, s_ack_tgl, c_ack_tgl, va_ack_tgl, vb_ack_tgl, mz_ack_tgl, bk_ack_tgl;
    wire [15:0] pm_data, s_data;
    wire [63:0] c_data;
    wire [7:0] va_data, vb_data, mz_data;
    reg bk_wr = 0;
    reg [15:1] bk_addr = 0;
    reg [15:0] bk_wdata = 0;
    reg [1:0] bk_be = 0;
    wire [127:0] bk_line;
    wire [1:0] img_sel;
    wire [15:0] img_addr, img_wdata;
    wire img_we;
    reg [15:0] img_rdata = 0;
    reg pw_en = 0;
    reg [26:0] pw_addr = 0;
    reg [127:0] pw_data = 0;
    wire pw_rdy;
    wire [26:0] c_addr, e_addr;
    wire [2:0] c_cmd, e_cmd;
    wire c_en, e_en, c_rdy, e_rdy, c_wdf_wren, e_wdf_wren, c_rd_valid, e_rd_valid;
    wire [127:0] c_wdf_data, e_wdf_data, rd_data;
    wire [15:0] c_wdf_mask, e_wdf_mask;
    wire [31:0] st_c_cmds, st_e_cmds, st_va_miss, st_mz_miss;
    wire [7:0] st_c_lat_max;
    reg ch_hint_tgl = 0, sh_hint_tgl = 0, inval = 0;
    reg [26:3] ch_hint_addr = 0;
    reg [26:1] sh_hint_addr = 0;
    wire [31:0] c_hit, c_pend, c_miss, c_hint, s_hit, s_pend, s_miss, s_hint, p_hit, p_pend, p_miss;
    neo_mem dut (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .rom_base(rom_base), .v1_base(v1_base), .v2_base(v2_base), .m1_base(m1_base), .img_base(img_base),
        .mv_go_tgl(mv_go_tgl), .mv_cmd(mv_cmd), .cp_src(cp_src), .cp_dst(cp_dst), .cp_len(cp_len), .mv_done_tgl(mv_done_tgl), .mv_busy(mv_busy),
        .pm_req_tgl(pm_req_tgl), .pm_addr(pm_addr), .pm_ack_tgl(pm_ack_tgl), .pm_data(pm_data),
        .s_req_tgl(s_req_tgl), .s_addr(s_addr), .s_ack_tgl(s_ack_tgl), .s_data(s_data),
        .c_req_tgl(c_req_tgl), .c_addr(c_addr_m), .c_ack_tgl(c_ack_tgl), .c_data(c_data),
        .va_req_tgl(va_req_tgl), .va_addr(va_addr), .va_ack_tgl(va_ack_tgl), .va_data(va_data),
        .vb_req_tgl(vb_req_tgl), .vb_addr(vb_addr), .vb_ack_tgl(vb_ack_tgl), .vb_data(vb_data),
        .mz_req_tgl(mz_req_tgl), .mz_addr(mz_addr), .mz_ack_tgl(mz_ack_tgl), .mz_data(mz_data),
        .img_sel(img_sel), .img_addr(img_addr), .img_wdata(img_wdata), .img_we(img_we), .img_rdata(img_rdata),
        .bk_req_tgl(bk_req_tgl), .bk_wr(bk_wr), .bk_addr(bk_addr), .bk_wdata(bk_wdata), .bk_be(bk_be), .bk_ack_tgl(bk_ack_tgl), .bk_line(bk_line),
        .pw_en(pw_en), .pw_addr(pw_addr), .pw_data(pw_data), .pw_rdy(pw_rdy),
        .c_addr_o(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren),
        .c_rd_data(rd_data), .c_rd_valid(c_rd_valid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_rd_valid),
        .st_c_cmds(st_c_cmds), .st_e_cmds(st_e_cmds), .st_va_miss(st_va_miss), .st_mz_miss(st_mz_miss), .st_c_lat_max(st_c_lat_max),
        .ch_hint_tgl(ch_hint_tgl), .ch_hint_addr(ch_hint_addr), .sh_hint_tgl(sh_hint_tgl), .sh_hint_addr(sh_hint_addr), .inval(inval),
        .st_c_hit(c_hit), .st_c_pend(c_pend), .st_c_miss(c_miss), .st_c_hint(c_hint), .st_s_hit(s_hit), .st_s_pend(s_pend), .st_s_miss(s_miss), .st_s_hint(s_hint),
        .st_p_hit(p_hit), .st_p_pend(p_pend), .st_p_miss(p_miss)
    );
    neo_ddr2 dm (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_rd_valid(c_rd_valid),
                 .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren), .e_rd_valid(e_rd_valid),
                 .rd_data(rd_data));

    // ---- the machine's RAMs behind the image port: LO 64 KB (byte), memory card 1024 words, XRAM 4096 words
    reg [7:0] lo [0:65535];
    reg [15:0] mc [0:1023];
    reg [15:0] xr [0:4095];
    reg [1:0] sel_q;
    integer k;
    always @(posedge ui_clk) begin
        sel_q <= img_sel;
        if (img_we) begin
            case (img_sel)
            2'd0: lo[img_addr] <= img_wdata[7:0];
            2'd2: mc[img_addr[9:0]] <= img_wdata;
            2'd3: xr[img_addr[11:0]] <= img_wdata;
            default: ;
            endcase
        end
        case (img_sel)
        2'd0: img_rdata <= {8'd0, lo[img_addr]};
        2'd2: img_rdata <= mc[img_addr[9:0]];
        2'd3: img_rdata <= xr[img_addr[11:0]];
        default: img_rdata <= 16'd0;
        endcase
    end

    // ---- helpers
    localparam [31:0] DDR_BASE = 32'h01000000;
    task put_ddr(input [31:0] a, input [7:0] b);
        reg [127:0] w;
        begin w = dm.mem[(a - DDR_BASE) >> 4]; w[8 * a[3:0] +: 8] = b; dm.mem[(a - DDR_BASE) >> 4] = w; end
    endtask
    function [7:0] get_ddr(input [31:0] a);
        reg [127:0] w;
        begin w = dm.mem[(a - DDR_BASE) >> 4]; get_ddr = w[8 * a[3:0] +: 8]; end
    endfunction
    function [7:0] pat(input [31:0] i);
        pat = (i * 8'd37) ^ (i >> 7) ^ 8'h5A;
    endfunction
    integer fails = 0;
    task check(input integer ok, input string what);
        begin
            if (ok) $display("  ok   %0s", what); else begin $display("  FAIL %0s", what); fails = fails + 1; end
        end
    endtask
    integer done_seen = 0;
    task go(input [3:0] cmd);
        integer t;
        begin
            mv_cmd = cmd;
            @(posedge ui_clk); mv_go_tgl = ~mv_go_tgl;
            t = 0;
            while (mv_done_tgl == done_tgl_q && t < 3000000) begin @(posedge ui_clk); t = t + 1; end
            done_tgl_q = mv_done_tgl;
            if (t >= 3000000) begin $display("  FAIL the mover did not finish (cmd %0d)", cmd); fails = fails + 1; end
        end
    endtask
    reg done_tgl_q = 0;
    integer i, n_ok, j;
    reg [127:0] w;
    reg [63:0] w64;
    initial begin
        for (k = 0; k < 65536; k = k + 1) lo[k] = 8'hEE;
        for (k = 0; k < 1024; k = k + 1) mc[k] = 16'hEEEE;
        for (k = 0; k < 4096; k = k + 1) xr[k] = 16'hEEEE;
        if ($test$plusargs("FAST")) dm.FAST = 1;
        #100 ui_rst = 0;
        repeat (10) @(posedge ui_clk);

        // ---- LO ROM load: 64 KB from img_base + 0
        for (i = 0; i < 65536; i = i + 1) put_ddr(img_base + i, pat(i));
        go(4'd0);
        n_ok = 0; for (i = 0; i < 65536; i = i + 1) if (lo[i] == pat(i)) n_ok = n_ok + 1;
        check(n_ok == 65536, "LO ROM load: 65536 bytes arrive in the block RAM");
        // ---- memory card load (img_base + 0x20000, 2 KB = 1024 words) and XRAM (img_base + 0x22000, 8 KB)
        for (i = 0; i < 2048; i = i + 1) put_ddr(img_base + 32'h20000 + i, pat(i + 1000));
        for (i = 0; i < 8192; i = i + 1) put_ddr(img_base + 32'h22000 + i, pat(i + 3000));
        go(4'd2); go(4'd3);
        n_ok = 0; for (i = 0; i < 1024; i = i + 1) if (mc[i] == {pat(2 * i + 1 + 1000), pat(2 * i + 1000)}) n_ok = n_ok + 1;
        check(n_ok == 1024, "memory card load: 1024 words, byte 0 of a word is the low byte");
        n_ok = 0; for (i = 0; i < 4096; i = i + 1) if (xr[i] == {pat(2 * i + 1 + 3000), pat(2 * i + 3000)}) n_ok = n_ok + 1;
        check(n_ok == 4096, "XRAM load: 4096 words");
        // ---- save: change the RAMs, save, read the DDR2
        for (i = 0; i < 1024; i = i + 1) mc[i] = {8'hA5, i[7:0]};
        for (i = 0; i < 4096; i = i + 1) xr[i] = {i[7:0], 8'h3C};
        go(4'd6); go(4'd7);
        n_ok = 0; for (i = 0; i < 1024; i = i + 1) if (get_ddr(img_base + 32'h20000 + 2 * i) == i[7:0] && get_ddr(img_base + 32'h20000 + 2 * i + 1) == 8'hA5) n_ok = n_ok + 1;
        check(n_ok == 1024, "memory card save: the words are in the DDR2");
        n_ok = 0; for (i = 0; i < 4096; i = i + 1) if (get_ddr(img_base + 32'h22000 + 2 * i) == 8'h3C && get_ddr(img_base + 32'h22000 + 2 * i + 1) == i[7:0]) n_ok = n_ok + 1;
        check(n_ok == 4096, "XRAM save: the words are in the DDR2");
        // ---- copy with masks
        for (i = 0; i < 4096; i = i + 1) put_ddr(32'h01100000 + i, pat(i + 77));
        for (i = 0; i < 8192; i = i + 1) put_ddr(32'h01200000 + i, 8'h11);
        cp_src = 32'h01100000; cp_dst = 32'h01200000; cp_len = 32'd4096;
        go(4'd8);                                               // full copy
        n_ok = 0; for (i = 0; i < 4096; i = i + 1) if (get_ddr(32'h01200000 + i) == pat(i + 77)) n_ok = n_ok + 1;
        check(n_ok == 4096, "copy: 4096 bytes");
        cp_dst = 32'h01204000;
        for (i = 0; i < 4096; i = i + 1) put_ddr(32'h01204000 + i, 8'h22);
        go(4'd9);                                               // parity 0: bytes 0 and 1 of every four
        n_ok = 0; for (i = 0; i < 4096; i = i + 1) if (get_ddr(32'h01204000 + i) == ((i % 4 < 2) ? pat(i + 77) : 8'h22)) n_ok = n_ok + 1;
        check(n_ok == 4096, "copy with mask 1: bytes 0,1 of every four are written, 2,3 stay");
        for (i = 0; i < 4096; i = i + 1) put_ddr(32'h01204000 + i, 8'h22);
        go(4'd10);                                              // parity 1: bytes 2 and 3
        n_ok = 0; for (i = 0; i < 4096; i = i + 1) if (get_ddr(32'h01204000 + i) == ((i % 4 >= 2) ? pat(i + 77) : 8'h22)) n_ok = n_ok + 1;
        check(n_ok == 4096, "copy with mask 2: bytes 2,3 of every four are written, 0,1 stay");
        // ---- the copy reaches above 64 MB (a DDR2 address the application's CPU cannot write)
        cp_src = 32'h01100000; cp_dst = 32'h02F00000; cp_len = 32'd256;
        go(4'd8);
        n_ok = 0; for (i = 0; i < 256; i = i + 1) if (get_ddr(32'h02F00000 + i) == pat(i + 77)) n_ok = n_ok + 1;
        check(n_ok == 256, "copy to a high address");

        // ---- the channels
        for (i = 0; i < 128; i = i + 1) put_ddr(rom_base + 32'h200000 + i, pat(i + 9));
        pm_addr = (26'h200000 + 26'd20) >> 1;                   // virtual byte 0x200014 = word 0x100000 + 10
        pm_req_tgl = ~pm_req_tgl;
        i = 0; while (pm_ack_tgl != pm_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(pm_ack_tgl == pm_req_tgl && pm_data == {pat(21 + 9), pat(20 + 9)}, "68k word channel: the little endian word at the byte address");
        for (i = 0; i < 128; i = i + 1) put_ddr(rom_base + 32'h300000 + i, pat(i + 3));
        c_addr_m = (24'h300000 + 24'd24) >> 3;
        c_req_tgl = ~c_req_tgl;
        i = 0; while (c_ack_tgl != c_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        w64 = 0; for (k = 0; k < 8; k = k + 1) w64[8 * k +: 8] = pat(24 + k + 3);
        check(c_ack_tgl == c_req_tgl && c_data == w64, "sprite channel: the eight bytes at the address, little endian");
        s_addr = (26'h80000 + 26'd6) >> 1;
        for (i = 0; i < 64; i = i + 1) put_ddr(rom_base + 32'h80000 + i, pat(i + 5));
        s_req_tgl = ~s_req_tgl;
        i = 0; while (s_ack_tgl != s_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(s_ack_tgl == s_req_tgl && s_data == {pat(7 + 5), pat(6 + 5)}, "fix channel: the word at the address");
        for (i = 0; i < 64; i = i + 1) put_ddr(v1_base + 32'h100 + i, pat(i + 11));
        for (i = 0; i < 64; i = i + 1) put_ddr(v2_base + 32'h100 + i, pat(i + 13));
        va_addr = 25'h100 + 25'd5;
        va_req_tgl = ~va_req_tgl;
        i = 0; while (va_ack_tgl != va_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(va_ack_tgl == va_req_tgl && va_data == pat(5 + 11), "ADPCM-A channel: a byte of the V1 space");
        vb_addr = 25'h1000000 + 25'h100 + 25'd7;                // bit 24: the V2 space
        vb_req_tgl = ~vb_req_tgl;
        i = 0; while (vb_ack_tgl != vb_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(vb_ack_tgl == vb_req_tgl && vb_data == pat(7 + 13), "ADPCM-B channel: bit 24 selects the V2 space");
        for (i = 0; i < 64; i = i + 1) put_ddr(m1_base + 32'h2000 + i, pat(i + 17));
        mz_addr = 25'h2000 + 25'd9;
        mz_req_tgl = ~mz_req_tgl;
        i = 0; while (mz_ack_tgl != mz_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(mz_ack_tgl == mz_req_tgl && mz_data == pat(9 + 17), "Z80 program channel");
        // the one line cache: the next byte of the same line is answered without a new DDR2 read
        i = st_e_cmds;
        mz_addr = 25'h2000 + 25'd10;
        mz_req_tgl = ~mz_req_tgl;
        k = 0; while (mz_ack_tgl != mz_req_tgl && k < 5000) begin @(posedge ui_clk); k = k + 1; end
        check(mz_ack_tgl == mz_req_tgl && mz_data == pat(10 + 17) && st_e_cmds == i && k < 10, "Z80 channel: a byte of the cached line needs no DDR2 command");


        // ---- the line caches (T143 3b; scenarios S7-S9 at the unit level): a hint fills a line ahead, the demand carries the real address and is looked up by it
        for (i = 0; i < 512; i = i + 1) put_ddr(rom_base + 32'h400000 + i, pat(i + 101));
        // S7: sprite hint, then (later) the demand for the same line: answered from the cache, no DDR2 command, within a few clocks
        ch_hint_addr = (24'h400000 + 24'd64) >> 3; ch_hint_tgl = ~ch_hint_tgl;
        repeat (80) @(posedge ui_clk);
        n_ok = st_c_cmds; k = c_hit;
        c_addr_m = (24'h400000 + 24'd72) >> 3;                  // the other half of the hinted 16 byte line? 64 + 8 = 72: same line (64..79)
        c_req_tgl = ~c_req_tgl;
        i = 0; while (c_ack_tgl != c_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        w64 = 0; for (j = 0; j < 8; j = j + 1) w64[8 * j +: 8] = pat(72 + j + 101);
        check(c_data == w64 && st_c_cmds == n_ok && c_hit == k + 1 && i < 12, "S7 sprite: a hinted line is answered from the cache (no DDR2 command, a few clocks)");
        // S8: the hint was wrong (another line): the demand gets ITS bytes, with a read of its own
        ch_hint_addr = (24'h400000 + 24'd128) >> 3; ch_hint_tgl = ~ch_hint_tgl;
        repeat (80) @(posedge ui_clk);
        n_ok = st_c_cmds;
        c_addr_m = (24'h400000 + 24'd200) >> 3;
        c_req_tgl = ~c_req_tgl;
        i = 0; while (c_ack_tgl != c_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        w64 = 0; for (j = 0; j < 8; j = j + 1) w64[8 * j +: 8] = pat(200 + j + 101);
        $display("  S8 debug: data %h want %h, cmds %0d -> %0d", c_data, w64, n_ok, st_c_cmds);
        check(c_data == w64 && st_c_cmds == n_ok + 1, "S8 sprite: a wrong hint costs a read, the demand gets the bytes of its own address");
        // a demand that comes while the hinted line is still on its way waits for it and asks nothing more
        ch_hint_addr = (24'h400000 + 24'd256) >> 3; ch_hint_tgl = ~ch_hint_tgl;
        repeat (12) @(posedge ui_clk);
        n_ok = st_c_cmds;
        c_addr_m = (24'h400000 + 24'd264) >> 3;
        c_req_tgl = ~c_req_tgl;
        i = 0; while (c_ack_tgl != c_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        w64 = 0; for (j = 0; j < 8; j = j + 1) w64[8 * j +: 8] = pat(264 + j + 101);
        check(c_data == w64 && st_c_cmds == n_ok && c_pend > 0, "sprite: a demand for a line in flight waits for it (one read)");
        // fix hints: both words of a pair
        sh_hint_addr = (26'h400000 + 26'd32) >> 1; sh_hint_tgl = ~sh_hint_tgl;
        repeat (80) @(posedge ui_clk);
        n_ok = st_c_cmds; k = s_hit;
        s_addr = (26'h400000 + 26'd38) >> 1;
        s_req_tgl = ~s_req_tgl;
        i = 0; while (s_ack_tgl != s_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(s_data == {pat(39 + 101), pat(38 + 101)} && st_c_cmds == n_ok && s_hit == k + 1, "S7 fix: a hinted line is answered from the cache");
        // S9: the 68k program cache. The first word of a line asks for the line and the next one; the following words need no command
        for (i = 0; i < 256; i = i + 1) put_ddr(rom_base + 32'h500000 + i, pat(i + 33));
        pm_addr = (26'h500000 + 26'd0) >> 1;
        pm_req_tgl = ~pm_req_tgl;
        i = 0; while (pm_ack_tgl != pm_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(pm_data == {pat(1 + 33), pat(0 + 33)}, "S9 68k: the first word of a line");
        repeat (120) @(posedge ui_clk);
        n_ok = st_c_cmds; k = p_hit;
        for (j = 1; j < 16; j = j + 1) begin                    // words 1..15: the rest of the line and the whole next line (prefetched)
            pm_addr = (26'h500000 + 2 * j) >> 1;
            pm_req_tgl = ~pm_req_tgl;
            i = 0; while (pm_ack_tgl != pm_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
            if (pm_data != {pat(2 * j + 1 + 33), pat(2 * j + 33)}) begin $display("  FAIL S9 word %0d is %04x", j, pm_data); fails = fails + 1; end
        end
        check(st_c_cmds <= n_ok + 2 && p_hit == k + 15, "S9 68k: 15 more words of two lines are all hits (the next line was fetched ahead), the third line is asked for");
        // the cache is dropped while the machine is in reset / held: a changed ROM byte is seen after that, not before
        put_ddr(rom_base + 32'h500000, 8'hC3); put_ddr(rom_base + 32'h500001, 8'h3C);
        pm_addr = (26'h500000) >> 1; pm_req_tgl = ~pm_req_tgl;
        i = 0; while (pm_ack_tgl != pm_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(pm_data == {pat(1 + 33), pat(0 + 33)}, "cache: the old word is still served while the machine runs (control: the test can see staleness)");
        inval = 1; repeat (200) @(posedge ui_clk); inval = 0; repeat (20) @(posedge ui_clk);
        pm_req_tgl = ~pm_req_tgl;
        i = 0; while (pm_ack_tgl != pm_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(pm_data == 16'h3CC3, "cache: after a reset / hold the changed word is read from the DDR2");
        // a demand that is waiting for its line while the machine is reset / held is not lost: it is answered when the caches are open again
        for (i = 0; i < 64; i = i + 1) put_ddr(rom_base + 32'h600000 + i, pat(i + 61));
        c_addr_m = (24'h600000 + 24'd16) >> 3; c_req_tgl = ~c_req_tgl;
        repeat (6) @(posedge ui_clk);
        inval = 1; repeat (300) @(posedge ui_clk);
        check(c_ack_tgl != c_req_tgl, "flush: the demand is not answered while the caches are closed");
        inval = 0;
        i = 0; while (c_ack_tgl != c_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        w64 = 0; for (j = 0; j < 8; j = j + 1) w64[8 * j +: 8] = pat(16 + j + 61);
        check(c_ack_tgl == c_req_tgl && c_data == w64, "flush: the demand that waited is answered with its own bytes after the caches open");

        // ---- the backup RAM client: write through with a byte mask, then fill a line
        for (i = 0; i < 64; i = i + 1) put_ddr(img_base + 32'h10000 + 32'h40 + i, 8'h00);
        bk_wr = 1; bk_addr = 15'h0020 + 15'd3; bk_wdata = 16'hBEEF; bk_be = 2'b10;       // word address 0x23 = byte 0x46: only the high byte (byte 0x47) is written
        bk_req_tgl = ~bk_req_tgl;
        i = 0; while (bk_ack_tgl != bk_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(bk_ack_tgl == bk_req_tgl && get_ddr(img_base + 32'h10000 + 32'h46) == 8'h00 && get_ddr(img_base + 32'h10000 + 32'h47) == 8'hBE, "backup RAM write through: only the byte lane that was written");
        bk_be = 2'b11; bk_addr = 15'h0020 + 15'd4; bk_wdata = 16'h1234;
        bk_req_tgl = ~bk_req_tgl;
        i = 0; while (bk_ack_tgl != bk_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        bk_wr = 0; bk_addr = 15'h0020;                                            // the line of byte 0x40..0x4F
        bk_req_tgl = ~bk_req_tgl;
        i = 0; while (bk_ack_tgl != bk_req_tgl && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        check(bk_ack_tgl == bk_req_tgl && bk_line[8 * 7 +: 8] == 8'hBE && bk_line[8 * 8 +: 16] == 16'h1234 && bk_line[8 * 6 +: 8] == 8'h00, "backup RAM fill: the line holds both writes");

        // ---- a picture word
        pw_addr = 27'h0C00000 >> 1; pw_data = {8{16'hA55A}} ^ 128'h0000_0000_0000_0000_0000_0000_0000_0001;
        pw_en = 1;
        i = 0; while (!pw_rdy && i < 5000) begin @(posedge ui_clk); i = i + 1; end
        @(posedge ui_clk); pw_en = 0;
        repeat (5) @(posedge ui_clk);
        w = dm.mem[(32'h0C00000 - DDR_BASE) >> 4];
        check(w == ({8{16'hA55A}} ^ 128'h1), "picture writer word lands in the DDR2");

        $display("%s", fails == 0 ? "PASS mover" : "FAIL mover");
        $finish;
    end
endmodule
