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
    assign fifo_rd = re && r0 && off == 12'h033;       // pops in the clock of the read, so the next read sees the next word

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
        rv_q <= rd_valid_i; q_rom <= re && r0 && off[11:5] == 7'h06;
        if (rd_valid_i && !rv_q) raddr <= raddr + 19'd1;

        if (st_we) saddr <= saddr + 8'd1;      // the clock after the write of 023: st_addr is still the old value in this clock
        if (w_en) begin
            if (w_off[11:5] == 7'h08) csel[w_off[4:0]] <= w_d[6:0];
            case (w_off)
                12'h010: irq_en <= w_d;
                12'h014: cause <= (cause & ~w_d) | ev;
                12'h012: begin run <= w_d[0]; abort <= w_d[1]; go <= w_d[2]; fifo_clr <= w_d[3]; cap_rst <= w_d[4]; end
                12'h0A0: pin_idx <= w_d[6:0];
                12'h130: raddr[3:0] <= w_d[7:4];
                12'h131: raddr[11:4] <= w_d;
                12'h132: raddr[18:12] <= w_d[6:0];
                12'h134: rd_start <= w_d[0];
                12'h020: sdata[7:0] <= w_d;
                12'h021: sdata[15:8] <= w_d;
                12'h022: sdata[23:16] <= w_d;
                12'h023: begin sdata[31:24] <= w_d; st_we <= 1'b1; end    // the RAM write is in the next clock; saddr moves on then
                12'h024: saddr <= w_d;
                12'h050: av_std <= w_d[1:0];
                12'h051: av_perm <= (w_d[2:0] > 3'd5) ? 3'd5 : w_d[2:0];
                12'h052: av_rev <= w_d[2:0];
                12'h053: begin av_vinv <= w_d[0]; av_frz <= w_d[1]; end
                12'h054: av_trim <= w_d;
                12'h055: av_bs <= w_d[6:0];
                12'h056: av_be[7:0] <= w_d;
                12'h057: av_be[9:8] <= w_d[1:0];
                12'h058: av_bamp <= w_d[5:0];
                12'h059: av_gain <= w_d[4:0];
                12'h05A: av_sync <= w_d;
                12'h05B: av_blank <= w_d;
                12'h05C: av_white <= w_d[4:0];
                12'h05D: begin av_eq <= w_d[0]; av_l262 <= w_d[1]; end
                12'h05E: av_vlen <= w_d[4:0];
                12'h05F: av_vpos <= w_d;
                12'h060: av_map <= w_d[0];
                12'h061: av_mode <= (w_d[1:0] == 2'd3) ? 2'd2 : w_d[1:0];
                12'h062: av_pch <= (w_d[1:0] == 2'd3) ? 2'd2 : w_d[1:0];
                12'h063: av_rch <= w_d[1:0];
                12'h064: av_synm <= w_d[1:0];
                12'h065: av_fbm <= w_d[1:0];
                default: ;
            endcase
        end

        q <= 8'h00;
        if (r0) begin
            case (off)
                12'h000: q <= 8'h44;                // "D"
                12'h001: q <= 8'h49;                // "I"
                12'h002: q <= 8'h41;                // "A"
                12'h003: q <= 8'h47;                // "G"
                12'h004: q <= 8'hC0;
                12'h005: q <= 8'h02;
                12'h006: q <= 8'h1F;
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h00C: q <= TICKS_US_X256[7:0];
                12'h00D: q <= TICKS_US_X256[15:8];
                12'h00E: q <= TICKS_US_X256[23:16];
                12'h00F: q <= TICKS_US_X256[31:24];
                12'h010: q <= irq_en;
                12'h011: q <= status;
                12'h014: q <= cause;
                12'h016: q <= pc;
                12'h017: q <= ref_pc;
                12'h018: q <= ref_cnt;
                12'h080: q <= 8'd121;
                12'h081: q <= {7'd0, guard_now};
                12'h0A0: q <= {1'b0, pin_idx};
                12'h120: q <= cap_st;
                12'h124: q <= cap_count[7:0];  12'h125: q <= cap_count[15:8];  12'h126: q <= cap_count[23:16];  12'h127: q <= cap_count[31:24];
                12'h128: q <= cap_lost[7:0];   12'h129: q <= cap_lost[15:8];   12'h12A: q <= cap_lost[23:16];   12'h12B: q <= cap_lost[31:24];
                12'h12C: q <= cap_lines[7:0];  12'h12D: q <= cap_lines[15:8];  12'h12E: q <= cap_lines[23:16];  12'h12F: q <= cap_lines[31:24];
                12'h130: q <= {raddr[3:0], 4'd0};
                12'h131: q <= raddr[11:4];
                12'h132: q <= {1'b0, raddr[18:12]};
                12'h024: q <= saddr;
                12'h030: q <= fifo_q[7:0];
                12'h031: q <= fifo_q[15:8];
                12'h032: q <= fifo_q[23:16];
                12'h033: q <= fifo_q[31:24];
                12'h034: q <= fifo_level[7:0];
                12'h035: q <= {6'd0, fifo_level[9:8]};
                12'h050: q <= {6'd0, av_std};
                12'h051: q <= {5'd0, av_perm};
                12'h052: q <= {5'd0, av_rev};
                12'h053: q <= {6'd0, av_frz, av_vinv};
                12'h054: q <= av_trim;
                12'h055: q <= {1'b0, av_bs};
                12'h056: q <= av_be[7:0];
                12'h057: q <= {6'd0, av_be[9:8]};
                12'h058: q <= {2'd0, av_bamp};
                12'h059: q <= {3'd0, av_gain};
                12'h05A: q <= av_sync;
                12'h05B: q <= av_blank;
                12'h05C: q <= {3'd0, av_white};
                12'h05D: q <= {6'd0, av_l262, av_eq};
                12'h05E: q <= {3'd0, av_vlen};
                12'h05F: q <= av_vpos;
                12'h060: q <= {7'd0, av_map};
                12'h061: q <= {6'd0, av_mode};
                12'h062: q <= {6'd0, av_pch};
                12'h063: q <= {6'd0, av_rch};
                12'h064: q <= {6'd0, av_synm};
                12'h065: q <= {6'd0, av_fbm};
                default: ;
            endcase
            if (off[11:4] == 8'h04) q <= live[8*off[3:0] +: 8];
            if (off[11:4] == 8'h07) q <= drv_en[8*off[3:0] +: 8];
            if (off[11:5] == 7'h08) q <= {1'b0, csel[off[4:0]]};
            if (off[11:4] == 8'h14) q <= rd_data[8*off[3:0] +: 8];
        end
    end
endmodule
