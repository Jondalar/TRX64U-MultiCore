// T113: the C128 core's register page for the application (spec 5.5, the text of record), at cw_addr[16:12] = 4'hE of the core window
// (0x101AE000 + off), in cpu_clk. Written for this project (TREX), no upstream code. Absent offsets read 0 and are acknowledged.
//   000-003 ID        "C128"
//   004 CAPS 0x01     this page, layout version 1
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b0 a VDC frame is complete, b1 monitor-switch key pressed, b2 40/80 key pressed
//   020 MON_SEL RW    b0 = 1: the 80-column picture (VDC) is shown; toggled by the monitor-switch key (reset 0)
//   021 D4080 RW      b0 = 1: the 40/80 DISPLAY switch is down (80 columns at reset); toggled by the 40/80 key (reset 0)
//   022 CAPSLK RW     b0: the CAPS LOCK latch (reset 0); toggled by the CAPS LOCK key
//   023 BOOT64 RW     b0: start like a C64 (taken at the core's reset); reset 0
//   024 JOY_SWAP RW   b0 = 1: the host's joystick ports 1 and 2 are swapped before the core (T157); live; reset 0
//   028-02A XKEYS RW  the USB keyboard's extra keys, K0-K2 x 8 rows (1 = pressed)
//   02B XSPECIAL RW   levels of the USB keyboard's special keys, acted on at the rising edge: b0 CAPS LOCK, b1 40/80, b2 monitor switch
//   030 STATUS R      b0 C64 mode, b1 the Z80 is the CPU, b2 2 MHz, b3 held (Freeze), b4 PAL
//   040 ROM_CTRL W/R  W b0: flush the ROM line cache; R b0: the flush is running
//   044-047 ROM_HITS, 048-04B ROM_MISSES R   32 bit
//   050-053 ROM_BASE R   0x01C00000
//   060 FB_LATEST R, 061 FRAME_SEQ R, 062-063 FB_OVERRUNS R, 064-067 FB_BASE RW (0x01D00000), 068-06B FB_STRIDE RW (0x00100000)
//   070 VDC_CTRL RW   b0 frame writer enabled (reset 1)
//   090 CHR_ADDR_LO, 091 CHR_ADDR_HI (b4:0)  RW   the character ROM store's byte address (0..8191; byte i = byte 0x10000 + i of the system ROM image)
//   092 CHR_DATA   W: store the byte at CHR_ADDR and add 1;  R: the byte at CHR_ADDR (allow 2 clocks after a CHR_ADDR write), then add 1  (T113 5.7)
//   0A0-0A3 ROM_WAITS R   32 bit: CPU cycles held by a missed ROM line;  0A4-0A5 ROM_FILL_MAX R   16 bit: the longest line fetch in core clocks
//   0B0-0B2 FR_MIN, 0B4-0B6 FR_MAX  R   20 bit: the shortest / longest VIC frame in core clocks since the last clear (628,992 PAL when nothing stops the VIC)
//   0C0-0C9 RAM_COUNTERS R   16 bit each (T140 D25/D27, the RAM in the DDR2; 0 in a build with block RAM): 0C0 VIC reads answered $FF (no valid slot page), 0C2 CPU reads held by a
//                     cache/slot miss, 0C4 DDR2 line reads, 0C6 writes that found the write FIFO full (must stay 0), 0C8 VIC reads of RAM block 1 (not mirrored: $FF)
//   0B8 DBG_CLR    W: any value clears FR_MIN / FR_MAX
//   0D0-0D4 ACT_VIC, ACT_CRAM, ACT_VDC, ACT_FLAGS, ACT_SEQ  R   (T194, T140 section 18: which chip is being used; c128_act.v) the last complete VIC frame:
//                     0D0 CPU writes to the VIC's registers $D000-$D02E, 0D1 to the colour RAM, 0D2 to the VDC's data register $D601 (8 bit each, saturating at 255),
//                     0D3 b0 the VIC's DEN ($D011 b4) at the end of that frame, 0D4 the frame number (wraps): read it before and after the others, they belong together when it is the same
//   080 COLD    W/R   W b0 = 1: cold start - every register of the machine goes to its power-on value (the core's reset line is pulsed and held
//                     until the frame phase of the power-on start comes round, so the frame phase of the power-on start is kept, T113 5.6; T194: the host scaler's lock is gone from this build) and this page's own
//                     machine settings (IRQ_EN, MON_SEL, D4080, CAPSLK, BOOT64, JOY_SWAP, XKEYS, XSPECIAL, FB_BASE/FB_STRIDE, VDC_CTRL) take their reset values
//                     too; R b0 = the cold start is running (up to one frame plus the hold of 256 clocks)
`timescale 1ns / 1ps
module c128_regs (
    input  wire        clk,
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire  [7:0] wdata,
    output wire  [7:0] rdata,
    output wire        ack,
    output wire        irq,
    // the C64U keyboard layer's special keys (levels) and the USB keyboard's extra keys
    input  wire        sp_caps, sp_4080, sp_mon,
    output wire [23:0] x_usb,
    // state the core uses
    output wire        mon_sel,
    output wire        d4080,
    output wire        capslk,
    output wire        boot64,
    output wire        joy_swap,
    output wire        vdc_en,
    output reg         rom_flush_tgl = 1'b0,
    output wire        chr_we,                  // the character store's write (T113 5.7): in cpu_clk, with chr_a / chr_d
    output wire [12:0] chr_a,
    output wire  [7:0] chr_d,
    input  wire  [7:0] chr_q,
    output reg         dbg_clr_tgl = 1'b0,
    input  wire [31:0] rom_waits,
    input  wire [15:0] rom_fill_max,
    input  wire [19:0] fr_min,
    input  wire [19:0] fr_max,
    input  wire [79:0] ram_cnt,             // {blk1, wlost, fetch, cpu_wait, vic_miss}, 16 bit each (sys_clk, taken when two samples agree)
    input  wire [39:0] act,                 // c128_act.v {seq, flags, vdc, cram, vic} (sys_clk, taken when two samples agree)
    output reg         cold_tgl = 1'b0,         // toggles per COLD write (the core takes the edge in sys_clk)
    input  wire        cold_busy,               // the cold start is running (sys_clk, taken when two samples agree)
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    // from the core (sys_clk, taken when two samples agree)
    input  wire  [7:0] status,
    input  wire        rom_busy,
    input  wire [31:0] rom_hits,
    input  wire [31:0] rom_misses,
    input  wire  [7:0] fb_latest,
    input  wire  [7:0] fb_seq,
    input  wire [15:0] fb_over,
    input  wire        ev_frame                 // a VDC frame is complete (a pulse seen from cpu_clk, synchronised by the caller)
);
    // >>> generated from cores/c128/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_PAGE_ID_ID = 12'h000;
    localparam [11:0] R_PAGE_ID_CAPS = 12'h004;
    localparam [11:0] R_PAGE_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_PAGE_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_MACHINE_MON_SEL = 12'h020;
    localparam [11:0] R_MACHINE_D4080 = 12'h021;
    localparam [11:0] R_MACHINE_CAPSLK = 12'h022;
    localparam [11:0] R_MACHINE_BOOT64 = 12'h023;
    localparam [11:0] R_MACHINE_JOY_SWAP = 12'h024;
    localparam [11:0] R_MACHINE_XKEYS = 12'h028;
    localparam [11:0] R_MACHINE_XSPECIAL = 12'h02B;
    localparam [11:0] R_MACHINE_STATUS = 12'h030;
    localparam [11:0] R_ROM_CTRL = 12'h040;
    localparam [11:0] R_ROM_HITS = 12'h044;
    localparam [11:0] R_ROM_MISSES = 12'h048;
    localparam [11:0] R_ROM_BASE = 12'h050;
    localparam [11:0] R_VIDEO_FB_LATEST = 12'h060;
    localparam [11:0] R_VIDEO_FRAME_SEQ = 12'h061;
    localparam [11:0] R_VIDEO_FB_OVERRUNS = 12'h062;
    localparam [11:0] R_VIDEO_FB_BASE = 12'h064;
    localparam [11:0] R_VIDEO_FB_STRIDE = 12'h068;
    localparam [11:0] R_VIDEO_VDC_CTRL = 12'h070;
    localparam [11:0] R_COLD_COLD = 12'h080;
    localparam [11:0] R_CHR_ADDR = 12'h090;
    localparam [11:0] R_CHR_DATA = 12'h092;
    localparam [11:0] R_PERF_ROM_WAITS = 12'h0A0;
    localparam [11:0] R_PERF_ROM_FILLMAX = 12'h0A4;
    localparam [11:0] R_PERF_FR_MIN = 12'h0B0;
    localparam [11:0] R_PERF_FR_MIN_HI = 12'h0B2;
    localparam [11:0] R_PERF_FR_MAX = 12'h0B4;
    localparam [11:0] R_PERF_FR_MAX_HI = 12'h0B6;
    localparam [11:0] R_PERF_DBG_CLR = 12'h0B8;
    localparam [11:0] R_PERF_RAM_VIC_FF = 12'h0C0;
    localparam [11:0] R_PERF_RAM_CPU_WAIT = 12'h0C2;
    localparam [11:0] R_PERF_RAM_FETCH = 12'h0C4;
    localparam [11:0] R_PERF_RAM_WLOST = 12'h0C6;
    localparam [11:0] R_PERF_RAM_BLK1 = 12'h0C8;
    localparam [11:0] R_ACT_VIC = 12'h0D0;
    localparam [11:0] R_ACT_CRAM = 12'h0D1;
    localparam [11:0] R_ACT_VDC = 12'h0D2;
    localparam [11:0] R_ACT_FLAGS = 12'h0D3;
    localparam [11:0] R_ACT_SEQ = 12'h0D4;
    // <<< generated
    wire        sel  = !addr[16] && addr[15:12] == 4'hE;
    wire [11:0] off  = addr[11:0];
    reg  [7:0]  irq_en = 8'd0, cause = 8'd0, q = 8'd0;
    reg         mon_r = 1'b0, d4080_r = 1'b0, caps_r = 1'b0, boot_r = 1'b0, vdc_r = 1'b1, ack_r = 1'b0, joy_r = 1'b0;
    reg  [23:0] xk_r = 24'd0;
    reg  [2:0]  xs_r = 3'd0;
    reg  [31:0] base_r = 32'h01D00000, stride_r = 32'h00100000;
    // special keys: the layer's levels (two flip-flops) or the application's, acted on at the rising edge
    reg  [2:0]  sp_a = 3'd0, sp_b = 3'd0, sp_d = 3'd0;
    wire [2:0]  sp_now = sp_b | xs_r;
    wire [2:0]  sp_rise = sp_now & ~sp_d;
    // values of the sys_clk side, accepted when two samples in a row agree
    reg  [7:0]  st_a = 0, st_b = 0, st_q = 0, l_a = 0, l_b = 0, l_q = 0, n_a = 0, n_b = 0, n_q = 0;
    reg  [15:0] o_a = 0, o_b = 0, o_q = 0;
    reg  [31:0] h_a = 0, h_b = 0, h_q = 0, m_a = 0, m_b = 0, m_q = 0;
    reg         bz_a = 0, bz_b = 0, bz_q = 0;
    reg  [12:0] ca = 13'd0;
    reg         cw_we = 1'b0;
    reg   [7:0] cw_d = 8'd0;
    assign chr_we = cw_we;
    assign chr_a  = ca;
    assign chr_d  = cw_d;
    reg  [31:0] w_a = 0, w_b = 0, w_q = 0;
    reg  [15:0] f_a = 0, f_b = 0, f_q = 0;
    reg  [79:0] rc_a = 0, rc_b = 0, rc_q = 0;
    reg  [39:0] ac_a = 0, ac_b = 0, ac_q = 0;
    reg  [19:0] n_a1 = 0, n_b1 = 0, n_q1 = 0, x_a1 = 0, x_b1 = 0, x_q1 = 0;
    reg         cb_a = 0, cb_b = 0, cb_q = 0, cp = 1'b0, cseen = 1'b0;   // cp: a COLD write whose running state the core has not shown and ended yet

    assign mon_sel = mon_r;
    assign d4080 = d4080_r;
    assign capslk = caps_r;
    assign boot64 = boot_r;
    assign joy_swap = joy_r;
    assign vdc_en = vdc_r;
    assign x_usb = xk_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q;

    always @(posedge clk) begin
        sp_a <= {sp_mon, sp_4080, sp_caps}; sp_b <= sp_a; sp_d <= sp_now;
        st_a <= status;      st_b <= st_a; if (st_a == st_b) st_q <= st_b;
        l_a <= fb_latest;    l_b <= l_a;   if (l_a == l_b)   l_q <= l_b;
        n_a <= fb_seq;       n_b <= n_a;   if (n_a == n_b)   n_q <= n_b;
        o_a <= fb_over;      o_b <= o_a;   if (o_a == o_b)   o_q <= o_b;
        h_a <= rom_hits;     h_b <= h_a;   if (h_a == h_b)   h_q <= h_b;
        m_a <= rom_misses;   m_b <= m_a;   if (m_a == m_b)   m_q <= m_b;
        bz_a <= rom_busy;    bz_b <= bz_a; if (bz_a == bz_b) bz_q <= bz_b;
        w_a <= rom_waits;     w_b <= w_a;   if (w_a == w_b)   w_q <= w_b;
        f_a <= rom_fill_max;  f_b <= f_a;   if (f_a == f_b)   f_q <= f_b;
        n_a1 <= fr_min;       n_b1 <= n_a1; if (n_a1 == n_b1) n_q1 <= n_b1;
        x_a1 <= fr_max;       x_b1 <= x_a1; if (x_a1 == x_b1) x_q1 <= x_b1;
        rc_a <= ram_cnt;      rc_b <= rc_a; if (rc_a == rc_b) rc_q <= rc_b;
        ac_a <= act;          ac_b <= ac_a; if (ac_a == ac_b) ac_q <= ac_b;
        cw_we <= 1'b0;
        if (cw_we) ca <= ca + 13'd1;
        if (re && sel && off == R_CHR_DATA) ca <= ca + 13'd1;
        cb_a <= cold_busy;   cb_b <= cb_a; if (cb_a == cb_b) cb_q <= cb_b;
        if (cb_q) cseen <= 1'b1;
        if (cseen && !cb_q) begin cp <= 1'b0; cseen <= 1'b0; end
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        cause <= ((we && sel && off == R_PAGE_IRQ_IRQ_CAUSE) ? (cause & ~wdata) : cause) | {5'd0, sp_rise[1], sp_rise[2], ev_frame};
        if (sp_rise[0]) caps_r <= ~caps_r;
        if (sp_rise[1]) d4080_r <= ~d4080_r;
        if (sp_rise[2]) mon_r <= ~mon_r;

        if (we && sel) begin
            case (off)
                R_PAGE_IRQ_IRQ_EN: irq_en <= wdata;
                R_MACHINE_MON_SEL: mon_r <= wdata[0];
                R_MACHINE_D4080: d4080_r <= wdata[0];
                R_MACHINE_CAPSLK: caps_r <= wdata[0];
                R_MACHINE_BOOT64: boot_r <= wdata[0];
                R_MACHINE_JOY_SWAP: joy_r <= wdata[0];
                R_MACHINE_XKEYS: xk_r[7:0] <= wdata;
                R_MACHINE_XKEYS + 12'd1: xk_r[15:8] <= wdata;
                R_MACHINE_XKEYS + 12'd2: xk_r[23:16] <= wdata;
                R_MACHINE_XSPECIAL: xs_r <= wdata[2:0];
                R_ROM_CTRL: if (wdata[0]) rom_flush_tgl <= ~rom_flush_tgl;
                R_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                R_VIDEO_VDC_CTRL: vdc_r <= wdata[0];
                R_CHR_ADDR: ca[7:0] <= wdata;
                R_CHR_ADDR + 12'd1: ca[12:8] <= wdata[4:0];
                R_CHR_DATA: begin cw_we <= 1'b1; cw_d <= wdata; end
                R_PERF_DBG_CLR: dbg_clr_tgl <= ~dbg_clr_tgl;
                R_COLD_COLD: if (wdata[0]) begin
                    cold_tgl <= ~cold_tgl;
                    cp <= 1'b1; cseen <= 1'b0;
                    // the machine settings of this page take their power-on values with the machine
                    irq_en <= 8'd0; cause <= 8'd0; mon_r <= 1'b0; d4080_r <= 1'b0; caps_r <= 1'b0; boot_r <= 1'b0; joy_r <= 1'b0;
                    xk_r <= 24'd0; xs_r <= 3'd0; base_r <= 32'h01D00000; stride_r <= 32'h00100000; vdc_r <= 1'b1;
                end
                default: ;
            endcase
        end

        q <= 8'h00;
        if (sel) begin
            case (off)
                R_PAGE_ID_ID: q <= "C";
                R_PAGE_ID_ID + 12'd1: q <= "1";
                R_PAGE_ID_ID + 12'd2: q <= "2";
                R_PAGE_ID_ID + 12'd3: q <= "8";
                R_PAGE_ID_CAPS: q <= 8'h01;
                R_PAGE_IRQ_IRQ_EN: q <= irq_en;
                R_PAGE_IRQ_IRQ_CAUSE: q <= cause;
                R_MACHINE_MON_SEL: q <= {7'd0, mon_r};
                R_MACHINE_D4080: q <= {7'd0, d4080_r};
                R_MACHINE_CAPSLK: q <= {7'd0, caps_r};
                R_MACHINE_BOOT64: q <= {7'd0, boot_r};
                R_MACHINE_JOY_SWAP: q <= {7'd0, joy_r};
                R_MACHINE_XKEYS: q <= xk_r[7:0];
                R_MACHINE_XKEYS + 12'd1: q <= xk_r[15:8];
                R_MACHINE_XKEYS + 12'd2: q <= xk_r[23:16];
                R_MACHINE_XSPECIAL: q <= {5'd0, xs_r};
                R_MACHINE_STATUS: q <= st_q;
                R_ROM_CTRL: q <= {7'd0, bz_q};
                R_ROM_HITS: q <= h_q[7:0];
                R_ROM_HITS + 12'd1: q <= h_q[15:8];
                R_ROM_HITS + 12'd2: q <= h_q[23:16];
                R_ROM_HITS + 12'd3: q <= h_q[31:24];
                R_ROM_MISSES: q <= m_q[7:0];
                R_ROM_MISSES + 12'd1: q <= m_q[15:8];
                R_ROM_MISSES + 12'd2: q <= m_q[23:16];
                R_ROM_MISSES + 12'd3: q <= m_q[31:24];
                R_ROM_BASE: q <= 8'h00;
                R_ROM_BASE + 12'd1: q <= 8'h00;
                R_ROM_BASE + 12'd2: q <= 8'hC0;
                R_ROM_BASE + 12'd3: q <= 8'h01;
                R_ACT_VIC: q <= ac_q[7:0];
                R_ACT_CRAM: q <= ac_q[15:8];
                R_ACT_VDC: q <= ac_q[23:16];
                R_ACT_FLAGS: q <= ac_q[31:24];
                R_ACT_SEQ: q <= ac_q[39:32];
                R_VIDEO_FB_LATEST: q <= l_q;
                R_VIDEO_FRAME_SEQ: q <= n_q;
                R_VIDEO_FB_OVERRUNS: q <= o_q[7:0];
                R_VIDEO_FB_OVERRUNS + 12'd1: q <= o_q[15:8];
                R_VIDEO_FB_BASE: q <= base_r[7:0];
                R_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];
                R_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];
                R_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_VIDEO_FB_STRIDE: q <= stride_r[7:0];
                R_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8];
                R_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16];
                R_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                R_VIDEO_VDC_CTRL: q <= {7'd0, vdc_r};
                R_COLD_COLD: q <= {7'd0, cp | cb_q};
                R_CHR_ADDR: q <= ca[7:0];
                R_CHR_ADDR + 12'd1: q <= {3'd0, ca[12:8]};
                R_CHR_DATA: q <= chr_q;
                R_PERF_ROM_WAITS: q <= w_q[7:0];
                R_PERF_ROM_WAITS + 12'd1: q <= w_q[15:8];
                R_PERF_ROM_WAITS + 12'd2: q <= w_q[23:16];
                R_PERF_ROM_WAITS + 12'd3: q <= w_q[31:24];
                R_PERF_ROM_FILLMAX: q <= f_q[7:0];
                R_PERF_ROM_FILLMAX + 12'd1: q <= f_q[15:8];
                R_PERF_FR_MIN: q <= n_q1[7:0];
                R_PERF_FR_MIN + 12'd1: q <= n_q1[15:8];
                R_PERF_FR_MIN_HI: q <= {4'd0, n_q1[19:16]};
                R_PERF_FR_MAX: q <= x_q1[7:0];
                R_PERF_FR_MAX + 12'd1: q <= x_q1[15:8];
                R_PERF_FR_MAX_HI: q <= {4'd0, x_q1[19:16]};
                R_PERF_RAM_VIC_FF: q <= rc_q[7:0];   R_PERF_RAM_VIC_FF + 12'd1: q <= rc_q[15:8];
                R_PERF_RAM_CPU_WAIT: q <= rc_q[23:16]; R_PERF_RAM_CPU_WAIT + 12'd1: q <= rc_q[31:24];
                R_PERF_RAM_FETCH: q <= rc_q[39:32]; R_PERF_RAM_FETCH + 12'd1: q <= rc_q[47:40];
                R_PERF_RAM_WLOST: q <= rc_q[55:48]; R_PERF_RAM_WLOST + 12'd1: q <= rc_q[63:56];
                R_PERF_RAM_BLK1: q <= rc_q[71:64]; R_PERF_RAM_BLK1 + 12'd1: q <= rc_q[79:72];
                default: ;
            endcase
        end
    end
endmodule
