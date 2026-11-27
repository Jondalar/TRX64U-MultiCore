// T175: the register block of the Port Diagnostics core at the application's core window (cw_*, cpu_clk, the bytes at 0x101A0000), laid out like null_regs.v / nes_regs.v.
// Written for this project (TREX). Byte offsets in page 0; the window reads 0 and acknowledges everything not listed.
//   000-003 ID        "DIAG"
//   004 CAPS 0xC0     b6 page 6 (the host's pixel layer), b7 the pixel layer is RGB565 (the host's meaning; the diag bits are DCAPS)
//   005 CAPS2 0x02    b1 BUILD is there
//   006 DCAPS         b0 the joystick pins are open-drain outputs in the top, b1 PADDLE_DIS / PADDLE_ADJ are pins of the bank, b2 the A/V test encoder (T201, 050-05F), b3 pin bank v2 (0C0-0DF), b4 capture (100-14F): 0x1F
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   00C-00F TICKS_US  R  engine clocks per microsecond x 256 (cpu_clk 100 MHz: 0x00006400)
//   010 IRQ_EN RW, 014 IRQ_CAUSE R/W1C   b7 page flip done, b6 video frame start, b5 the engine stopped or waits for the user
//   011 STATUS        R  b0 running, b1 waiting for GO, b2 done, b3 result FIFO overflow (sticky until CLEAR), b4 step error, b5 a step was refused (sticky until RUN), b6 capture sampling, b7 guard trip (sticky until RUN)
//   012 CTRL          W  b0 RUN, b1 ABORT, b2 GO, b3 CLEAR FIFO, b4 CAP RESET   (each bit is a one-clock pulse; RUN is taken when the engine is idle)
//   016 PC            R  the engine's step index;  017 REF_PC the first refused step since RUN;  018 REF_CNT refused steps since RUN (saturates)
//   020-023 STEP_DATA W  a step word, little endian; the write of 023 stores it at STEP_ADDR and increments STEP_ADDR
//   024 STEP_ADDR     RW 8 bit index of the next step to store
//   030-033 FIFO_DATA R  head word of the result FIFO, little endian; the read of 033 pops it
//   034-035 FIFO_LEVEL R words in the FIFO (0..512)
//   040-04F LIVE      R  the 128 bit snapshot, byte k = pins 8k+7..8k (pin index = bit), synchronised
//   070-07F DRVEN     R  bit per pin: the engine drives or overrides it now
//   080 PIN_COUNT R, 081 GUARD R (b0 GAME or EXROM is low now), 0A0 PIN_IDX RW, 0C0-0DF PIN_ROM R (32 bytes of the entry PIN_IDX, diag_pinrom.v)
//   100-11F CAP_SEL RW (7 bit pin index per capture lane, 0x7F = unused), 120 CAP_STATUS R, 124 CAP_COUNT, 128 CAP_LOST, 12C CAP_LINES (R, 32 bit), 130 CAP_RADDR RW, 134 CAP_RCTRL W, 140-14F CAP_RDATA R
//   050-05F A/V test encoder (T201, diag_avout.v; all RW, live, a read gives what was written; reset = the C128's PAL values)
//     050 STD b1:0 (0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50)   051 PERM b2:0 (0..5, 5)   052 REV b2:0 (bit n: group n bit order reversed, 0)   053 VINV b0 (1), FRZ b1 (0: the marker stands still)
//     054 TRIM s8 (0)   055 BSTART b6:0 (28)   056-057 BEND b9:0 (99)   058 BAMP b5:0 (32)   059 GAIN b4:0 (16)   05A SYNC (0)   05B BLANK (77)   05C WHITE b4:0 (11)
//     05D b0 EQ (0: broad vsync pulses only; 1: equalizing + serrated vsync), b1 L262 (0: 263 lines per frame at the 60 Hz standards; 1: 262)   05E VLEN b4:0 (6: the vsync / each block's length in half lines; 5 = 2.5 lines, 12 = 6)   05F VPOS s8 (0: the vsync's start in lines against the default)
//   060-065 T259 (diag_avout.v, RW, live, reset 0 = the T201 behaviour): 060 MAP b0 (0 scan groups, 1 names)   061 MODE b1:0 (0 test picture, 1 channel probe, 2 RGB; 3 reads back 2)   062 PCH b1:0 (probe channel 0..2)
//     063 RCH b1:0 (RGB: 0 palette picture, 1..3 one word as a ramp)   064 SYNCN b1:0 (0 high, 1 low, 2 csync, 3 inverted csync)   065 FB b1:0 (0 low, 1 high, 2 picture window, 3 inverted window)
//   page 6 (0x6000-0x6FFF)  forwarded to the host's pixel layer (px_we, px_re, px_rdata)
`timescale 1ns / 1ps
module diag_regs #(
    parameter [31:0] TICKS_US_X256 = 32'h00006400
) (
    input  wire        clk,                         // cpu_clk
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire [7:0]  wdata,
    output wire [7:0]  rdata,
    output wire        ack,
    output wire        irq,
    input  wire [31:0] build,
    output wire        px_we,
    output wire        px_re,
    input  wire [7:0]  px_rdata,
    input  wire        ev_vsync,
    input  wire        ev_flip,
    input  wire        ev_eng,
    // to / from the probe engine
    output reg         run, abort, go, fifo_clr,
    output reg         st_we,
    output wire [7:0]  st_addr,
    output wire [31:0] st_wdata,
    input  wire [7:0]  status,                      // {guard trip, capture sampling, refused, err, ovf, done, waiting, running}
    input  wire [7:0]  pc, ref_pc, ref_cnt,
    input  wire [31:0] fifo_q,
    input  wire [9:0]  fifo_level,
    output wire        fifo_rd,
    input  wire [127:0] live,
    input  wire [127:0] drv_en,
    input  wire        guard_now,
    output reg         cap_rst,
    // WRITE_REG of the engine (the A/V and generator registers 050-06F)
    input  wire        e_we,
    input  wire [11:0] e_off,
    input  wire [7:0]  e_wd,
    output wire        e_wack,
    // capture (diag_cap.v)
    output wire [223:0] cap_sel,
    input  wire [7:0]  cap_st,                      // {3'b0, rd_valid, rd_busy, overrun, idle_all, sampling}
    input  wire [31:0] cap_count, cap_lost, cap_lines,
    output reg         rd_start,
    output wire [18:0] rd_line_ix,
    input  wire        rd_valid_i,
    input  wire [127:0] rd_data,
    output wire [92:0] av_cfg                       // T201: the A/V encoder's knobs (diag_avout.v lists the fields)
);
    // >>> generated from cores/diag/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_DCAPS = 12'h006;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_IDENT_TICKS_US_X256 = 12'h00C;
    localparam [11:0] R_ENGINE_IRQ_EN = 12'h010;
    localparam [11:0] R_ENGINE_STATUS = 12'h011;
    localparam [11:0] R_ENGINE_CTRL = 12'h012;
    localparam [11:0] R_ENGINE_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_ENGINE_PC = 12'h016;
    localparam [11:0] R_ENGINE_REF_PC = 12'h017;
    localparam [11:0] R_ENGINE_REF_CNT = 12'h018;
    localparam [11:0] R_ENGINE_STEP_DATA = 12'h020;
    localparam [11:0] R_ENGINE_STEP_ADDR = 12'h024;
    localparam [11:0] R_ENGINE_FIFO_DATA = 12'h030;
    localparam [11:0] R_ENGINE_FIFO_LEVEL = 12'h034;
    localparam [11:0] R_LIVE_SNAP = 12'h040;
    localparam [11:0] R_AV_STD = 12'h050;
    localparam [11:0] R_AV_PERM = 12'h051;
    localparam [11:0] R_AV_REV = 12'h052;
    localparam [11:0] R_AV_VINV = 12'h053;
    localparam [11:0] R_AV_TRIM = 12'h054;
    localparam [11:0] R_AV_BSTART = 12'h055;
    localparam [11:0] R_AV_BEND = 12'h056;
    localparam [11:0] R_AV_BAMP = 12'h058;
    localparam [11:0] R_AV_GAIN = 12'h059;
    localparam [11:0] R_AV_SYNC = 12'h05A;
    localparam [11:0] R_AV_BLANK = 12'h05B;
    localparam [11:0] R_AV_WHITE = 12'h05C;
    localparam [11:0] R_AV_SYNCB = 12'h05D;
    localparam [11:0] R_AV_VLEN = 12'h05E;
    localparam [11:0] R_AV_VPOS = 12'h05F;
    localparam [11:0] R_AV_MAP = 12'h060;
    localparam [11:0] R_AV_MODE = 12'h061;
    localparam [11:0] R_AV_PCH = 12'h062;
    localparam [11:0] R_AV_RCH = 12'h063;
    localparam [11:0] R_AV_SYNM = 12'h064;
    localparam [11:0] R_AV_FBM = 12'h065;
    localparam [11:0] R_DRVEN_DRVEN = 12'h070;
    localparam [11:0] R_PINBANK_COUNT = 12'h080;
    localparam [11:0] R_PINBANK_GUARD = 12'h081;
    localparam [11:0] R_PINSEL_IDX = 12'h0A0;
    localparam [11:0] R_PINROM_ROM = 12'h0C0;
    localparam [11:0] R_CAPSEL_SEL = 12'h100;
    localparam [11:0] R_CAPST_STATUS = 12'h120;
    localparam [11:0] R_CAPCNT_COUNT = 12'h124;
    localparam [11:0] R_CAPCNT_LOST = 12'h128;
    localparam [11:0] R_CAPCNT_LINES = 12'h12C;
    localparam [11:0] R_CAPRADDR_RADDR = 12'h130;
    localparam [11:0] R_CAPCTL_RCTRL = 12'h134;
    localparam [11:0] R_CAPRD_RDATA = 12'h140;
    // <<< generated
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    reg [1:0]  av_std = 2'd0;
    reg [2:0]  av_perm = 3'd5, av_rev = 3'd0;
    reg        av_vinv = 1'b1, av_frz = 1'b0, av_eq = 1'b0, av_l262 = 1'b0;
    reg [7:0]  av_vpos = 8'd0, av_trim = 8'd0, av_sync = 8'd0, av_blank = 8'd77;
    reg [6:0]  av_bs = 7'd28;
    reg [9:0]  av_be = 10'd99;
    reg [5:0]  av_bamp = 6'd32;
    reg [4:0]  av_gain = 5'd16, av_white = 5'd11, av_vlen = 5'd6;
    reg        av_map = 1'b0;
    reg [1:0]  av_mode = 2'd0, av_pch = 2'd0, av_rch = 2'd0, av_synm = 2'd0, av_fbm = 2'd0;
    assign av_cfg = {av_fbm, av_synm, av_rch, av_pch, av_mode, av_map, av_vpos, av_vlen, av_l262, av_eq, av_frz, av_white, av_blank, av_sync, av_gain, av_bamp, av_be, av_bs, av_trim, av_vinv, av_rev, av_perm, av_std};
    // pin table ROM and capture lane selects
    reg [6:0]  pin_idx = 7'd0;
    reg [6:0]  csel [0:31];
    reg [18:0] raddr = 19'd0;
    reg        rv_q = 1'b0, q_rom = 1'b0;
    wire [7:0] rom_q;
    diag_pinrom prom (.clk(clk), .a({pin_idx, addr[4:0]}), .q(rom_q));
    genvar cl;
    generate for (cl = 0; cl < 32; cl = cl + 1) begin : cs
        initial csel[cl] = 7'h7F;
        assign cap_sel[7*cl +: 7] = csel[cl];
    end endgenerate
    assign rd_line_ix = raddr;
    reg [7:0]  saddr = 8'd0;
    reg [31:0] sdata = 32'd0;
    wire        win  = addr[16];
    wire  [3:0] page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    wire  [7:0] ev   = {ev_flip, ev_vsync, ev_eng, 5'b00000};
    wire        r0   = !win && page == 4'h0;
    assign fifo_rd = re && r0 && off == R_ENGINE_FIFO_DATA + 12'd3;       // pops in the clock of the read, so the next read sees the next word

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    // the engine's WRITE_REG shares the write path of the register file; the bus goes first, the engine waits a clock
    wire        bus_w = we && r0;
    wire        eng_w = e_we && !we;
    assign e_wack = eng_w;
    wire        w_en  = bus_w | eng_w;
    wire [11:0] w_off = bus_w ? off : e_off;
    wire  [7:0] w_d   = bus_w ? wdata : e_wd;
    assign ack   = ack_r;
    assign irq   = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q_rom ? rom_q : q;
    assign st_addr  = saddr;
    assign st_wdata = sdata;

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= re && !win && page == 4'h6;
        run <= 1'b0; abort <= 1'b0; go <= 1'b0; fifo_clr <= 1'b0; st_we <= 1'b0; cap_rst <= 1'b0; rd_start <= 1'b0;
        rv_q <= rd_valid_i; q_rom <= re && r0 && off[11:5] == (R_PINROM_ROM + 0) >> 5;
        if (rd_valid_i && !rv_q) raddr <= raddr + 19'd1;

        if (st_we) saddr <= saddr + 8'd1;      // the clock after the write of 023: st_addr is still the old value in this clock
        if (w_en) begin
            if (w_off[11:5] == (R_CAPSEL_SEL + 0) >> 5) csel[w_off[4:0]] <= w_d[6:0];
            case (w_off)
                R_ENGINE_IRQ_EN: irq_en <= w_d;
                R_ENGINE_IRQ_CAUSE: cause <= (cause & ~w_d) | ev;
                R_ENGINE_CTRL: begin run <= w_d[0]; abort <= w_d[1]; go <= w_d[2]; fifo_clr <= w_d[3]; cap_rst <= w_d[4]; end
                R_PINSEL_IDX: pin_idx <= w_d[6:0];
                R_CAPRADDR_RADDR: raddr[3:0] <= w_d[7:4];
                R_CAPRADDR_RADDR + 12'd1: raddr[11:4] <= w_d;
                R_CAPRADDR_RADDR + 12'd2: raddr[18:12] <= w_d[6:0];
                R_CAPCTL_RCTRL: rd_start <= w_d[0];
                R_ENGINE_STEP_DATA: sdata[7:0] <= w_d;
                R_ENGINE_STEP_DATA + 12'd1: sdata[15:8] <= w_d;
                R_ENGINE_STEP_DATA + 12'd2: sdata[23:16] <= w_d;
                R_ENGINE_STEP_DATA + 12'd3: begin sdata[31:24] <= w_d; st_we <= 1'b1; end    // the RAM write is in the next clock; saddr moves on then
                R_ENGINE_STEP_ADDR: saddr <= w_d;
                R_AV_STD: av_std <= w_d[1:0];
                R_AV_PERM: av_perm <= (w_d[2:0] > 3'd5) ? 3'd5 : w_d[2:0];
                R_AV_REV: av_rev <= w_d[2:0];
                R_AV_VINV: begin av_vinv <= w_d[0]; av_frz <= w_d[1]; end
                R_AV_TRIM: av_trim <= w_d;
                R_AV_BSTART: av_bs <= w_d[6:0];
                R_AV_BEND: av_be[7:0] <= w_d;
                R_AV_BEND + 12'd1: av_be[9:8] <= w_d[1:0];
                R_AV_BAMP: av_bamp <= w_d[5:0];
                R_AV_GAIN: av_gain <= w_d[4:0];
                R_AV_SYNC: av_sync <= w_d;
                R_AV_BLANK: av_blank <= w_d;
                R_AV_WHITE: av_white <= w_d[4:0];
                R_AV_SYNCB: begin av_eq <= w_d[0]; av_l262 <= w_d[1]; end
                R_AV_VLEN: av_vlen <= w_d[4:0];
                R_AV_VPOS: av_vpos <= w_d;
                R_AV_MAP: av_map <= w_d[0];
                R_AV_MODE: av_mode <= (w_d[1:0] == 2'd3) ? 2'd2 : w_d[1:0];
                R_AV_PCH: av_pch <= (w_d[1:0] == 2'd3) ? 2'd2 : w_d[1:0];
                R_AV_RCH: av_rch <= w_d[1:0];
                R_AV_SYNM: av_synm <= w_d[1:0];
                R_AV_FBM: av_fbm <= w_d[1:0];
                default: ;
            endcase
        end

        q <= 8'h00;
        if (r0) begin
            case (off)
                R_IDENT_ID: q <= 8'h44;                // "D"
                R_IDENT_ID + 12'd1: q <= 8'h49;                // "I"
                R_IDENT_ID + 12'd2: q <= 8'h41;                // "A"
                R_IDENT_ID + 12'd3: q <= 8'h47;                // "G"
                R_IDENT_CAPS: q <= 8'hC0;
                R_IDENT_CAPS2: q <= 8'h02;
                R_IDENT_DCAPS: q <= 8'h1F;
                R_IDENT_BUILD: q <= build[7:0];
                R_IDENT_BUILD + 12'd1: q <= build[15:8];
                R_IDENT_BUILD + 12'd2: q <= build[23:16];
                R_IDENT_BUILD + 12'd3: q <= build[31:24];
                R_IDENT_TICKS_US_X256: q <= TICKS_US_X256[7:0];
                R_IDENT_TICKS_US_X256 + 12'd1: q <= TICKS_US_X256[15:8];
                R_IDENT_TICKS_US_X256 + 12'd2: q <= TICKS_US_X256[23:16];
                R_IDENT_TICKS_US_X256 + 12'd3: q <= TICKS_US_X256[31:24];
                R_ENGINE_IRQ_EN: q <= irq_en;
                R_ENGINE_STATUS: q <= status;
                R_ENGINE_IRQ_CAUSE: q <= cause;
                R_ENGINE_PC: q <= pc;
                R_ENGINE_REF_PC: q <= ref_pc;
                R_ENGINE_REF_CNT: q <= ref_cnt;
                R_PINBANK_COUNT: q <= 8'd121;
                R_PINBANK_GUARD: q <= {7'd0, guard_now};
                R_PINSEL_IDX: q <= {1'b0, pin_idx};
                R_CAPST_STATUS: q <= cap_st;
                R_CAPCNT_COUNT: q <= cap_count[7:0];  R_CAPCNT_COUNT + 12'd1: q <= cap_count[15:8];  R_CAPCNT_COUNT + 12'd2: q <= cap_count[23:16];  R_CAPCNT_COUNT + 12'd3: q <= cap_count[31:24];
                R_CAPCNT_LOST: q <= cap_lost[7:0];   R_CAPCNT_LOST + 12'd1: q <= cap_lost[15:8];   R_CAPCNT_LOST + 12'd2: q <= cap_lost[23:16];   R_CAPCNT_LOST + 12'd3: q <= cap_lost[31:24];
                R_CAPCNT_LINES: q <= cap_lines[7:0];  R_CAPCNT_LINES + 12'd1: q <= cap_lines[15:8];  R_CAPCNT_LINES + 12'd2: q <= cap_lines[23:16];  R_CAPCNT_LINES + 12'd3: q <= cap_lines[31:24];
                R_CAPRADDR_RADDR: q <= {raddr[3:0], 4'd0};
                R_CAPRADDR_RADDR + 12'd1: q <= raddr[11:4];
                R_CAPRADDR_RADDR + 12'd2: q <= {1'b0, raddr[18:12]};
                R_ENGINE_STEP_ADDR: q <= saddr;
                R_ENGINE_FIFO_DATA: q <= fifo_q[7:0];
                R_ENGINE_FIFO_DATA + 12'd1: q <= fifo_q[15:8];
                R_ENGINE_FIFO_DATA + 12'd2: q <= fifo_q[23:16];
                R_ENGINE_FIFO_DATA + 12'd3: q <= fifo_q[31:24];
                R_ENGINE_FIFO_LEVEL: q <= fifo_level[7:0];
                R_ENGINE_FIFO_LEVEL + 12'd1: q <= {6'd0, fifo_level[9:8]};
                R_AV_STD: q <= {6'd0, av_std};
                R_AV_PERM: q <= {5'd0, av_perm};
                R_AV_REV: q <= {5'd0, av_rev};
                R_AV_VINV: q <= {6'd0, av_frz, av_vinv};
                R_AV_TRIM: q <= av_trim;
                R_AV_BSTART: q <= {1'b0, av_bs};
                R_AV_BEND: q <= av_be[7:0];
                R_AV_BEND + 12'd1: q <= {6'd0, av_be[9:8]};
                R_AV_BAMP: q <= {2'd0, av_bamp};
                R_AV_GAIN: q <= {3'd0, av_gain};
                R_AV_SYNC: q <= av_sync;
                R_AV_BLANK: q <= av_blank;
                R_AV_WHITE: q <= {3'd0, av_white};
                R_AV_SYNCB: q <= {6'd0, av_l262, av_eq};
                R_AV_VLEN: q <= {3'd0, av_vlen};
                R_AV_VPOS: q <= av_vpos;
                R_AV_MAP: q <= {7'd0, av_map};
                R_AV_MODE: q <= {6'd0, av_mode};
                R_AV_PCH: q <= {6'd0, av_pch};
                R_AV_RCH: q <= {6'd0, av_rch};
                R_AV_SYNM: q <= {6'd0, av_synm};
                R_AV_FBM: q <= {6'd0, av_fbm};
                default: ;
            endcase
            if (off[11:4] == (R_LIVE_SNAP + 0) >> 4) q <= live[8*off[3:0] +: 8];
            if (off[11:4] == (R_DRVEN_DRVEN + 0) >> 4) q <= drv_en[8*off[3:0] +: 8];
            if (off[11:5] == (R_CAPSEL_SEL + 0) >> 5) q <= {1'b0, csel[off[4:0]]};
            if (off[11:4] == (R_CAPRD_RDATA + 0) >> 4) q <= rd_data[8*off[3:0] +: 8];
        end
    end
endmodule
