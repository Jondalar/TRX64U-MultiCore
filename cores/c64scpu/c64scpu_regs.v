// The C64SCPU core's register page for the application (T182), at cw_addr[16:12] = 4'hE of the core window (0x101AE000 + off), in cpu_clk. Written for
// this project (TREX), no upstream code; the offsets of the kept registers are those of the C128 core's page (cores/c128/c128_regs.v) it was cut from.
// Absent offsets read 0 and are acknowledged.
//   000-003 ID        "SCPU"
//   004 CAPS 0x01     this page, layout version 1
//   024 JOY_SWAP RW   b0 = 1: the host's joystick ports 1 and 2 are swapped before the core (T157); live; reset 0
//   030 STATUS R      b0 SuperCPU ROM valid, b1 20 MHz in effect (step 4; 0), b2 emulation mode (E), b3 held (Freeze), b4 PAL; the other bits read 0
//   040 ROM_CTRL W/R  W b0: flush the ROM line cache; R b0: the flush is running
//   044-047 ROM_HITS, 048-04B ROM_MISSES R   32 bit
//   050-053 ROM_BASE R   0x01C00000
//   T182 step 3 (spec T10): 054-057 SROM_BASE R 0x03080000;  058 SROM_LOG2 RW b4:0 (16/17/18; reset 17; not touched by COLD);  05C ROM_VALID RW b0 (reset 0; not touched by COLD:
//                     the image stays in the DDR2; 0 holds the 65816 in reset);  060 SCPU_SW RW b1:0 (b0 speed switch 1 = 20 MHz, b1 JiffyDOS; reset and COLD 0x01)
//                     064-067 DEV_CYC R 32 bit: CPU enables withheld because the CPU's own memory (DDR2 behind the cache) was not ready - the deviation from the original (D8)
//                     068-06B CACHE_HITS R, 06C-06F CACHE_MISSES R  32 bit (the 32 KiB cache of the CPU's SRAM / SuperRAM / ROM, D9, T241 D4)
//   090 CHR_ADDR_LO, 091 CHR_ADDR_HI (b3:0)  RW   the character ROM store's byte address (0..4095)
//   092 CHR_DATA   W: store the byte at CHR_ADDR and add 1;  R: the byte at CHR_ADDR (allow 2 clocks after a CHR_ADDR write), then add 1
//   0A0-0A3 ROM_WAITS R   32 bit: CPU cycles held by a missed ROM line;  0A4-0A5 ROM_FILL_MAX R   16 bit: the longest line fetch in core clocks
//   0B0-0B2 FR_MIN, 0B4-0B6 FR_MAX  R   20 bit: the shortest / longest VIC frame in core clocks since the last clear (628,992 PAL when nothing stops the VIC)
//   0B8 DBG_CLR    W: any value clears FR_MIN / FR_MAX
//   0C0-0C9 RAM_COUNTERS R   16 bit each (the RAM in the DDR2): 0C0 VIC reads answered $FF (no valid slot page), 0C2 CPU reads held by a
//                     cache/slot miss, 0C4 DDR2 line reads, 0C6 writes that found the write FIFO full (must stay 0), 0C8 VIC reads of RAM block 1 (not mirrored: $FF; 0 here)
//   080 COLD    W/R   W b0 = 1: cold start - every register of the machine goes to its power-on value (the core's reset line is pulsed and held
//                     until the frame phase of the power-on start comes round, so the host scaler's lock still fits) and JOY_SWAP takes its reset value
//                     too; R b0 = the cold start is running (up to one frame plus the hold of 256 clocks)
`timescale 1ns / 1ps
module c64scpu_regs (
    input  wire        clk,
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire  [7:0] wdata,
    output wire  [7:0] rdata,
    output wire        ack,
    output wire        irq,
    // state the core uses
    output wire        joy_swap,
    output reg         rom_flush_tgl = 1'b0,
    output wire        rom_valid_o,                // T10 ROM_VALID, SROM_LOG2, SCPU_SW (cpu_clk, quasi static)
    output wire  [4:0] srom_log2_o,
    output wire  [1:0] scpu_sw_o,
    input  wire [31:0] dev_cyc, cache_hits, cache_misses,
    output wire        chr_we,                  // the character store's write: in cpu_clk, with chr_a / chr_d
    output wire [11:0] chr_a,
    output wire  [7:0] chr_d,
    input  wire  [7:0] chr_q,
    output reg         dbg_clr_tgl = 1'b0,
    input  wire [31:0] rom_waits,
    input  wire [15:0] rom_fill_max,
    input  wire [19:0] fr_min,
    input  wire [19:0] fr_max,
    input  wire [79:0] ram_cnt,             // {blk1, wlost, fetch, cpu_wait, vic_miss}, 16 bit each (sys_clk, taken when two samples agree)
    output reg         cold_tgl = 1'b0,         // toggles per COLD write (the core takes the edge in sys_clk)
    input  wire        cold_busy,               // the cold start is running (sys_clk, taken when two samples agree)
    // from the core (sys_clk, taken when two samples agree)
    input  wire  [7:0] status,
    input  wire        rom_busy,
    input  wire [31:0] rom_hits,
    input  wire [31:0] rom_misses
);
    // >>> generated from cores/c64scpu/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_PAGE_ID_ID = 12'h000;
    localparam [11:0] R_PAGE_ID_CAPS = 12'h004;
    localparam [11:0] R_MACHINE_JOY_SWAP = 12'h024;
    localparam [11:0] R_STATUS_STATUS = 12'h030;
    localparam [11:0] R_ROM_CTRL = 12'h040;
    localparam [11:0] R_ROM_HITS = 12'h044;
    localparam [11:0] R_ROM_MISSES = 12'h048;
    localparam [11:0] R_ROM_BASE = 12'h050;
    localparam [11:0] R_ROM_SROM_BASE = 12'h054;
    localparam [11:0] R_ROM_SROM_LOG2 = 12'h058;
    localparam [11:0] R_ROM_ROM_VALID = 12'h05C;
    localparam [11:0] R_ROM_SW = 12'h060;
    localparam [11:0] R_CACHE_DEV_CYC = 12'h064;
    localparam [11:0] R_CACHE_CACHE_HITS = 12'h068;
    localparam [11:0] R_CACHE_CACHE_MISSES = 12'h06C;
    localparam [11:0] R_COLD_COLD = 12'h080;
    localparam [11:0] R_CHR_ADDR = 12'h090;
    localparam [11:0] R_CHR_DATA = 12'h092;
    localparam [11:0] R_PERF_ROM_WAITS = 12'h0A0;
    localparam [11:0] R_PERF_ROM_FILL_MAX = 12'h0A4;
    localparam [11:0] R_PERF_FR_MIN = 12'h0B0;
    localparam [11:0] R_PERF_FR_MIN_HI = 12'h0B2;
    localparam [11:0] R_PERF_FR_MAX = 12'h0B4;
    localparam [11:0] R_PERF_FR_MAX_HI = 12'h0B6;
    localparam [11:0] R_PERF_DBG_CLR = 12'h0B8;
    localparam [11:0] R_RAM_CNT_COUNT = 12'h0C0;
    // <<< generated
    wire        sel  = !addr[16] && addr[15:12] == 4'hE;
    wire [11:0] off  = addr[11:0];
    reg  [7:0]  q = 8'd0;
    reg         ack_r = 1'b0, joy_r = 1'b0;
    reg         rv_r = 1'b0;
    reg   [4:0] lg_r = 5'd17;
    reg   [1:0] sw_r = 2'b01;
    assign rom_valid_o = rv_r; assign srom_log2_o = lg_r; assign scpu_sw_o = sw_r;
    reg  [31:0] dv_a = 0, dv_b = 0, dv_q = 0, ch_a = 0, ch_b = 0, ch_q = 0, cm_a = 0, cm_b = 0, cm_q = 0;
    // values of the sys_clk side, accepted when two samples in a row agree
    reg  [7:0]  st_a = 0, st_b = 0, st_q = 0;
    reg  [31:0] h_a = 0, h_b = 0, h_q = 0, m_a = 0, m_b = 0, m_q = 0;
    reg         bz_a = 0, bz_b = 0, bz_q = 0;
    reg  [11:0] ca = 12'd0;
    reg         cw_we = 1'b0;
    reg   [7:0] cw_d = 8'd0;
    assign chr_we = cw_we;
    assign chr_a  = ca;
    assign chr_d  = cw_d;
    reg  [31:0] w_a = 0, w_b = 0, w_q = 0;
    reg  [15:0] f_a = 0, f_b = 0, f_q = 0;
    reg  [79:0] rc_a = 0, rc_b = 0, rc_q = 0;
    reg  [19:0] n_a1 = 0, n_b1 = 0, n_q1 = 0, x_a1 = 0, x_b1 = 0, x_q1 = 0;
    reg         cb_a = 0, cb_b = 0, cb_q = 0, cp = 1'b0, cseen = 1'b0;   // cp: a COLD write whose running state the core has not shown and ended yet

    assign joy_swap = joy_r;
    assign ack = ack_r;
    assign irq = 1'b0;
    assign rdata = q;

    always @(posedge clk) begin
        st_a <= status;      st_b <= st_a; if (st_a == st_b) st_q <= st_b;
        dv_a <= dev_cyc;     dv_b <= dv_a; if (dv_a == dv_b) dv_q <= dv_b;
        ch_a <= cache_hits;  ch_b <= ch_a; if (ch_a == ch_b) ch_q <= ch_b;
        cm_a <= cache_misses; cm_b <= cm_a; if (cm_a == cm_b) cm_q <= cm_b;
        h_a <= rom_hits;     h_b <= h_a;   if (h_a == h_b)   h_q <= h_b;
        m_a <= rom_misses;   m_b <= m_a;   if (m_a == m_b)   m_q <= m_b;
        bz_a <= rom_busy;    bz_b <= bz_a; if (bz_a == bz_b) bz_q <= bz_b;
        w_a <= rom_waits;     w_b <= w_a;   if (w_a == w_b)   w_q <= w_b;
        f_a <= rom_fill_max;  f_b <= f_a;   if (f_a == f_b)   f_q <= f_b;
        n_a1 <= fr_min;       n_b1 <= n_a1; if (n_a1 == n_b1) n_q1 <= n_b1;
        x_a1 <= fr_max;       x_b1 <= x_a1; if (x_a1 == x_b1) x_q1 <= x_b1;
        rc_a <= ram_cnt;      rc_b <= rc_a; if (rc_a == rc_b) rc_q <= rc_b;
        cw_we <= 1'b0;
        if (cw_we) ca <= ca + 12'd1;
        if (re && sel && off == R_CHR_DATA) ca <= ca + 12'd1;
        cb_a <= cold_busy;   cb_b <= cb_a; if (cb_a == cb_b) cb_q <= cb_b;
        if (cb_q) cseen <= 1'b1;
        if (cseen && !cb_q) begin cp <= 1'b0; cseen <= 1'b0; end
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;

        if (we && sel) begin
            case (off)
                R_MACHINE_JOY_SWAP: joy_r <= wdata[0];
                R_ROM_CTRL: if (wdata[0]) rom_flush_tgl <= ~rom_flush_tgl;
                R_ROM_SROM_LOG2: lg_r <= wdata[4:0];
                R_ROM_ROM_VALID: rv_r <= wdata[0];
                R_ROM_SW: sw_r <= wdata[1:0];
                R_CHR_ADDR: ca[7:0] <= wdata;
                R_CHR_ADDR + 12'd1: ca[11:8] <= wdata[3:0];
                R_CHR_DATA: begin cw_we <= 1'b1; cw_d <= wdata; end
                R_PERF_DBG_CLR: dbg_clr_tgl <= ~dbg_clr_tgl;
                R_COLD_COLD: if (wdata[0]) begin
                    cold_tgl <= ~cold_tgl;
                    cp <= 1'b1; cseen <= 1'b0;
                    // the machine settings of this page take their power-on values with the machine
                    joy_r <= 1'b0;
                    sw_r <= 2'b01;
                end
                default: ;
            endcase
        end

        q <= 8'h00;
        if (sel) begin
            case (off)
                R_PAGE_ID_ID: q <= "S";
                R_PAGE_ID_ID + 12'd1: q <= "C";
                R_PAGE_ID_ID + 12'd2: q <= "P";
                R_PAGE_ID_ID + 12'd3: q <= "U";
                R_PAGE_ID_CAPS: q <= 8'h01;
                R_MACHINE_JOY_SWAP: q <= {7'd0, joy_r};
                R_STATUS_STATUS: q <= st_q;
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
                R_ROM_SROM_BASE: q <= 8'h00;
                R_ROM_SROM_BASE + 12'd1: q <= 8'h00;
                R_ROM_SROM_BASE + 12'd2: q <= 8'h08;
                R_ROM_SROM_BASE + 12'd3: q <= 8'h03;
                R_ROM_SROM_LOG2: q <= {3'd0, lg_r};
                R_ROM_ROM_VALID: q <= {7'd0, rv_r};
                R_ROM_SW: q <= {6'd0, sw_r};
                R_CACHE_DEV_CYC: q <= dv_q[7:0];   R_CACHE_DEV_CYC + 12'd1: q <= dv_q[15:8];  R_CACHE_DEV_CYC + 12'd2: q <= dv_q[23:16];  R_CACHE_DEV_CYC + 12'd3: q <= dv_q[31:24];
                R_CACHE_CACHE_HITS: q <= ch_q[7:0];   R_CACHE_CACHE_HITS + 12'd1: q <= ch_q[15:8];  R_CACHE_CACHE_HITS + 12'd2: q <= ch_q[23:16];  R_CACHE_CACHE_HITS + 12'd3: q <= ch_q[31:24];
                R_CACHE_CACHE_MISSES: q <= cm_q[7:0];   R_CACHE_CACHE_MISSES + 12'd1: q <= cm_q[15:8];  R_CACHE_CACHE_MISSES + 12'd2: q <= cm_q[23:16];  R_CACHE_CACHE_MISSES + 12'd3: q <= cm_q[31:24];
                R_COLD_COLD: q <= {7'd0, cp | cb_q};
                R_CHR_ADDR: q <= ca[7:0];
                R_CHR_ADDR + 12'd1: q <= {4'd0, ca[11:8]};
                R_CHR_DATA: q <= chr_q;
                R_PERF_ROM_WAITS: q <= w_q[7:0];
                R_PERF_ROM_WAITS + 12'd1: q <= w_q[15:8];
                R_PERF_ROM_WAITS + 12'd2: q <= w_q[23:16];
                R_PERF_ROM_WAITS + 12'd3: q <= w_q[31:24];
                R_PERF_ROM_FILL_MAX: q <= f_q[7:0];
                R_PERF_ROM_FILL_MAX + 12'd1: q <= f_q[15:8];
                R_PERF_FR_MIN: q <= n_q1[7:0];
                R_PERF_FR_MIN + 12'd1: q <= n_q1[15:8];
                R_PERF_FR_MIN_HI: q <= {4'd0, n_q1[19:16]};
                R_PERF_FR_MAX: q <= x_q1[7:0];
                R_PERF_FR_MAX + 12'd1: q <= x_q1[15:8];
                R_PERF_FR_MAX_HI: q <= {4'd0, x_q1[19:16]};
                R_RAM_CNT_COUNT: q <= rc_q[7:0];   R_RAM_CNT_COUNT + 12'd1: q <= rc_q[15:8];
                R_RAM_CNT_COUNT + 12'd2: q <= rc_q[23:16]; R_RAM_CNT_COUNT + 12'd3: q <= rc_q[31:24];
                R_RAM_CNT_COUNT + 12'd4: q <= rc_q[39:32]; R_RAM_CNT_COUNT + 12'd5: q <= rc_q[47:40];
                R_RAM_CNT_COUNT + 12'd6: q <= rc_q[55:48]; R_RAM_CNT_COUNT + 12'd7: q <= rc_q[63:56];
                R_RAM_CNT_COUNT + 12'd8: q <= rc_q[71:64]; R_RAM_CNT_COUNT + 12'd9: q <= rc_q[79:72];
                default: ;
            endcase
        end
    end
endmodule
