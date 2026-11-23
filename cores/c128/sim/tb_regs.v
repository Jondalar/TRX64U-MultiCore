// T113 5.6 bench: the COLD register of the C128's register page (c128_regs.v, offset 080). A write of 1 toggles cold_tgl (the core takes the edge) and
// puts the page's machine settings back to their reset values; the read shows b0 from the write until the core has shown the cold start running and
// ended it. Negative controls (each must print FAIL): -DREGS_MUT_NORESET (the settings keep their values), -DREGS_MUT_NOTGL (no toggle).
//   iverilog -g2012 -o tb_regs cores/c128/sim/tb_regs.v cores/c128/c128_regs.v && vvp tb_regs
`timescale 1ns / 1ps
module tb_regs;
    reg clk = 0;
    always #5 clk = ~clk;
    reg         we = 0, re = 0;
    reg  [16:0] addr = 0;
    reg   [7:0] wdata = 0;
    wire  [7:0] rdata;
    wire        ack, irq;
    wire [23:0] x_usb;
    wire        mon_sel, d4080, capslk, boot64, vdc_en, cold_tgl;
    wire        rom_flush_tgl;
    wire [31:0] fb_base, fb_stride;
    reg         cold_busy = 0;
    wire        chr_we, dbg_clr_tgl;
    wire [12:0] chr_a;
    wire  [7:0] chr_d;
    reg  [31:0] rom_waits = 32'h12345678;
    reg  [15:0] rom_fill_max = 16'hBEEF;
    reg  [19:0] fr_min = 20'h9AC00, fr_max = 20'h9AC00;
    reg  [79:0] ram_cnt = {16'hA5E1, 16'h9D08, 16'h7733, 16'h5566, 16'h1234};     // {blk1, wlost, fetch, cpu_wait, vic_miss}
    // the character store as the core has it: the byte at the write address, one clock later (c128_chr.v's write port)
    reg   [7:0] cmem [0:8191];
    reg   [7:0] chr_q = 8'd0;
    always @(posedge clk) begin
        if (chr_we) cmem[chr_a] <= chr_d;
        chr_q <= cmem[chr_a];
    end

    wire        joy_swap;
    wire  [4:0] jo1, jo2;
    reg   [4:0] hj1 = 5'b00001, hj2 = 5'b10000;          // the host's joysticks: 1 = up, 2 = fire
    wire [17:0] vk_req;
    reg         vk_dead = 1'b0, vk_tgl = 1'b0;
    reg   [7:0] vk_d = 8'd0;
    c128_regs dut (.clk(clk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata), .ack(ack), .irq(irq),
        .sp_caps(1'b0), .sp_4080(1'b0), .sp_mon(1'b0), .x_usb(x_usb), .mon_sel(mon_sel), .d4080(d4080), .capslk(capslk), .boot64(boot64), .joy_swap(joy_swap),
        .vdc_en(vdc_en), .rom_flush_tgl(rom_flush_tgl), .cold_tgl(cold_tgl), .cold_busy(cold_busy), .fb_base(fb_base), .fb_stride(fb_stride),
        .chr_we(chr_we), .chr_a(chr_a), .chr_d(chr_d), .chr_q(chr_q), .dbg_clr_tgl(dbg_clr_tgl), .rom_waits(rom_waits), .rom_fill_max(rom_fill_max), .fr_min(fr_min), .fr_max(fr_max), .ram_cnt(ram_cnt), .act(40'h5A_01_33_22_11),
        .status(8'd0), .rom_busy(1'b0), .rom_hits(32'd0), .rom_misses(32'd0), .fb_latest(8'd0), .fb_seq(8'd0), .fb_over(16'd0), .ev_frame(1'b0),
        .vk_req(vk_req), .vk_ans({vk_tgl, vk_d}));

    c128_joyswap joysw (.clk(clk), .swap(joy_swap), .joy1(hj1), .joy2(hj2), .joy1_o(jo1), .joy2_o(jo2));
    integer nbad = 0;
    task wr(input [11:0] off, input [7:0] v);
        begin
            @(posedge clk); addr <= {1'b0, 4'hE, off}; wdata <= v; we <= 1'b1;
            @(posedge clk); we <= 1'b0;
            @(posedge clk);
        end
    endtask
    task rd(input [11:0] off, output [7:0] v);
        begin
            @(posedge clk); addr <= {1'b0, 4'hE, off}; re <= 1'b1;
            @(posedge clk); re <= 1'b0;
            @(posedge clk); @(posedge clk);
            v = rdata;
        end
    endtask
    task expect8(input [255:0] name, input [7:0] got, input [7:0] want);
        begin
            if (got !== want) begin $display("FAIL: %0s = %02x, expected %02x", name, got, want); nbad = nbad + 1; end
            else $display("ok   %0s = %02x", name, got);
        end
    endtask
    reg [7:0] v;
    reg       tg0;
    integer i;
    // T257: a model of the VDC read-back engine (c128_x.v): the answer comes four clocks after the toggle is seen, the byte is 0xA0 + the register number, or the VRAM
    // model's byte; vk_dead = 1 never answers
    reg   [7:0] vram [0:65535];
    reg   [2:0] vk_s = 3'd0;
    integer     vk_n = 0;
    initial for (i = 0; i < 65536; i = i + 1) vram[i] = i[7:0] ^ i[15:8] ^ 8'h5C;
    always @(posedge clk) begin
        vk_s <= {vk_s[1:0], vk_req[17]};
        if (vk_s[2] != vk_s[1] && !vk_dead) vk_n <= 4;
        if (vk_n > 0) begin
            vk_n <= vk_n - 1;
            if (vk_n == 1) begin vk_d <= vk_req[16] ? vram[vk_req[15:0]] : 8'hA0 + vk_req[5:0]; vk_tgl <= ~vk_tgl; end
        end
    end
    // the application's read of a slow register: the address and a one-clock re, then wait for the ack; v = the byte at the ack
    task rdw(input [11:0] off, output [7:0] v);
        integer k;
        begin
            @(posedge clk); addr <= {1'b0, 4'hE, off}; re <= 1'b1;
            @(posedge clk); re <= 1'b0;
            k = 0; while (!ack && k < 1200) begin @(posedge clk); k = k + 1; end
            if (k >= 1200) begin $display("FAIL: no ack for %03x", off); nbad = nbad + 1; end
            v = rdata;
            // the byte stays while the address stays
            @(posedge clk); @(posedge clk);
            if (rdata !== v) begin $display("FAIL: %03x changed after the ack: %02x -> %02x", off, v, rdata); nbad = nbad + 1; end
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        // settings that are not at their reset values
        wr(12'h02B, 8'h07);                 // XSPECIAL first: its rising edges toggle MON_SEL, D4080 and CAPSLK once
        repeat (8) @(posedge clk);
        wr(12'h010, 8'h07); wr(12'h020, 8'h01); wr(12'h021, 8'h01); wr(12'h022, 8'h01); wr(12'h023, 8'h01);
        wr(12'h028, 8'hA5); wr(12'h029, 8'h5A); wr(12'h02A, 8'hFF);
        wr(12'h064, 8'h11); wr(12'h065, 8'h22); wr(12'h066, 8'h33); wr(12'h067, 8'h44); wr(12'h070, 8'h00);
        rd(12'h020, v); expect8("MON_SEL before", v, 8'h01);
        // T157 S1/S2: JOY_SWAP exchanges the host's joystick ports before the core (port 1 = host 1 while it is 0)
        rd(12'h024, v); expect8("JOY_SWAP reset", v, 8'h00);
        repeat (4) @(posedge clk);
        if (jo1 !== hj1 || jo2 !== hj2) begin $display("FAIL: swap off: ports %b %b", jo1, jo2); nbad = nbad + 1; end else $display("ok   swap off: ports as the host has them");
        wr(12'h024, 8'h01);
        rd(12'h024, v); expect8("JOY_SWAP set", v, 8'h01);
        repeat (4) @(posedge clk);
        if (jo1 !== hj2 || jo2 !== hj1) begin $display("FAIL: swap on: ports %b %b", jo1, jo2); nbad = nbad + 1; end else $display("ok   swap on: host port 2 is the core's port 1 and back");
        // T140 D27: the RAM counters (0C0-0C9), low byte first
        rd(12'h0C0, v); expect8("RAM_VIC_MISS lo", v, 8'h34); rd(12'h0C1, v); expect8("RAM_VIC_MISS hi", v, 8'h12);
        rd(12'h0C2, v); expect8("RAM_CPU_WAIT lo", v, 8'h66); rd(12'h0C3, v); expect8("RAM_CPU_WAIT hi", v, 8'h55);
        rd(12'h0C4, v); expect8("RAM_FETCH lo", v, 8'h33);    rd(12'h0C5, v); expect8("RAM_FETCH hi", v, 8'h77);
        rd(12'h0C6, v); expect8("RAM_WLOST lo", v, 8'h08);    rd(12'h0C7, v); expect8("RAM_WLOST hi", v, 8'h9D);
        rd(12'h0C8, v); expect8("RAM_VIC_BLK1 lo", v, 8'hE1); rd(12'h0C9, v); expect8("RAM_VIC_BLK1 hi", v, 8'hA5);
        rd(12'h028, v); expect8("XKEYS0 before", v, 8'hA5);
        rd(12'h064, v); expect8("FB_BASE0 before", v, 8'h11);
        rd(12'h02B, v); expect8("XSPECIAL before", v, 8'h07);
        rd(12'h080, v); expect8("COLD before", v, 8'h00);
        tg0 = cold_tgl;
        wr(12'h080, 8'h01);
        if (cold_tgl === tg0) begin $display("FAIL: cold_tgl did not toggle"); nbad = nbad + 1; end else $display("ok   cold_tgl toggled");
        // from the write on the register says "running", though the core has not shown it yet
        rd(12'h080, v); expect8("COLD right after the write", v, 8'h01);
        for (i = 0; i < 6; i = i + 1) @(posedge clk);
        cold_busy = 1'b1;
        for (i = 0; i < 6; i = i + 1) @(posedge clk);
        rd(12'h080, v); expect8("COLD while the core runs it", v, 8'h01);
        cold_busy = 1'b0;
        for (i = 0; i < 6; i = i + 1) @(posedge clk);
        rd(12'h080, v); expect8("COLD after it ended", v, 8'h00);
        // the settings are at their power-on values
        rd(12'h010, v); expect8("IRQ_EN", v, 8'h00);
        rd(12'h020, v); expect8("MON_SEL", v, 8'h00);
        rd(12'h021, v); expect8("D4080", v, 8'h00);
        rd(12'h022, v); expect8("CAPSLK", v, 8'h00);
        rd(12'h023, v); expect8("BOOT64", v, 8'h00);
        rd(12'h024, v); expect8("JOY_SWAP", v, 8'h00);
        repeat (4) @(posedge clk);
        if (jo1 !== hj1 || jo2 !== hj2) begin $display("FAIL: swap off after COLD: ports %b %b", jo1, jo2); nbad = nbad + 1; end else $display("ok   swap off after COLD");
        rd(12'h028, v); expect8("XKEYS0", v, 8'h00);
        rd(12'h029, v); expect8("XKEYS1", v, 8'h00);
        rd(12'h02A, v); expect8("XKEYS2", v, 8'h00);
        rd(12'h02B, v); expect8("XSPECIAL", v, 8'h00);
        rd(12'h064, v); expect8("FB_BASE0", v, 8'h00);
        rd(12'h065, v); expect8("FB_BASE1", v, 8'h00);
        rd(12'h066, v); expect8("FB_BASE2", v, 8'hD0);
        rd(12'h067, v); expect8("FB_BASE3", v, 8'h01);
        rd(12'h068, v); expect8("FB_STRIDE0", v, 8'h00);
        rd(12'h06A, v); expect8("FB_STRIDE2", v, 8'h10);
        rd(12'h070, v); expect8("VDC_CTRL", v, 8'h01);
        // T113 5.7: the character store through CHR_ADDR / CHR_DATA (write, add 1, read back) and the debug words
        wr(12'h090, 8'hF0); wr(12'h091, 8'h0F);                      // address 0x0FF0: the last 16 bytes of set 0
        for (i = 0; i < 40; i = i + 1) wr(12'h092, 8'h10 + i[7:0]);   // runs over into set 1 (0x1000)
        rd(12'h090, v); expect8("CHR_ADDR_LO after 40 writes", v, 8'h18);
        rd(12'h091, v); expect8("CHR_ADDR_HI after 40 writes", v, 8'h10);
        wr(12'h090, 8'hF0); wr(12'h091, 8'h0F);
        for (i = 0; i < 40; i = i + 1) begin
            rd(12'h092, v);
            if (v !== 8'h10 + i[7:0]) begin $display("FAIL: CHR_DATA byte %0d = %02x, expected %02x", i, v, 8'h10 + i[7:0]); nbad = nbad + 1; end
        end
        $display("ok   40 bytes written over the set boundary and read back");
        rd(12'h0A0, v); expect8("ROM_WAITS0", v, 8'h78);
        rd(12'h0A3, v); expect8("ROM_WAITS3", v, 8'h12);
        rd(12'h0A4, v); expect8("ROM_FILL_MAX0", v, 8'hEF);
        rd(12'h0A5, v); expect8("ROM_FILL_MAX1", v, 8'hBE);
        rd(12'h0B0, v); expect8("FR_MIN0", v, 8'h00);
        rd(12'h0B1, v); expect8("FR_MIN1", v, 8'hAC);
        rd(12'h0B2, v); expect8("FR_MIN2", v, 8'h09);
        rd(12'h0B6, v); expect8("FR_MAX2", v, 8'h09);
        // T194: the activity words (c128_act.v): ACT_VIC, ACT_CRAM, ACT_VDC, ACT_FLAGS, ACT_SEQ
        rd(12'h0D0, v); expect8("ACT_VIC", v, 8'h11);
        rd(12'h0D1, v); expect8("ACT_CRAM", v, 8'h22);
        rd(12'h0D2, v); expect8("ACT_VDC", v, 8'h33);
        rd(12'h0D3, v); expect8("ACT_FLAGS", v, 8'h01);
        rd(12'h0D4, v); expect8("ACT_SEQ", v, 8'h5A);
        // T257: the VDC read-back (100-13F, 140-143)
        rdw(12'h100, v); expect8("VDC_REG R0", v, 8'hA0);
        rdw(12'h119, v); expect8("VDC_REG R25", v, 8'hB9);
        rdw(12'h127, v); expect8("VDC_REG 39", v, 8'hC7);
        rd(12'h020, v); expect8("MON_SEL still readable after a slow read", v, 8'h00);
        wr(12'h140, 8'hFE); wr(12'h141, 8'h00);
        rdw(12'h142, v); expect8("VDC_PK_DATA $00FE", v, 8'hFE ^ 8'h5C);
        rdw(12'h142, v); expect8("VDC_PK_DATA $00FF", v, 8'hFF ^ 8'h5C);
        rdw(12'h142, v); expect8("VDC_PK_DATA $0100", v, 8'h00 ^ 8'h01 ^ 8'h5C);
        rd(12'h140, v); expect8("VDC_PK_ADDR_LO after three reads", v, 8'h01);
        rd(12'h141, v); expect8("VDC_PK_ADDR_HI after three reads", v, 8'h01);
        rd(12'h143, v); expect8("VDC_PK_STAT after answered reads", v, 8'h00);
        vk_dead = 1'b1;
        rdw(12'h104, v); expect8("VDC_REG R4 with no answer", v, 8'hFF);
        rd(12'h143, v); expect8("VDC_PK_STAT after a timeout", v, 8'h01);
        vk_dead = 1'b0;
        rdw(12'h104, v); expect8("VDC_REG R4 answered again", v, 8'hA4);
        rd(12'h143, v); expect8("VDC_PK_STAT cleared", v, 8'h00);
        tg0 = dbg_clr_tgl;
        wr(12'h0B8, 8'h01);
        if (dbg_clr_tgl === tg0) begin $display("FAIL: DBG_CLR did not toggle"); nbad = nbad + 1; end else $display("ok   DBG_CLR toggled");
        if (nbad == 0) $display("PASS tb_regs"); else $display("FAIL tb_regs: %0d wrong", nbad);
        $finish;
    end
endmodule
