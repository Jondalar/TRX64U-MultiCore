// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// trx_sid_engine_c64u: T25.1 time-multiplexed SID engine, 8 SIDs / 24 voices / 8
// filters, all state in RAM (TRX64-Ultimate/docs/specs/T25.1-sid-engine.md).
//
// Bit-level behaviour = trx64/trx_sid_c64u/model/sidref.cpp (the M1 reference
// model); the benches in host/sim/sid compare the two cycle by
// cycle. Provenance as in the model: the digital voice follows libresidfp
// (GPL-2.0-or-later), the filter reSID 1.0's linear 8580 path (GPL-2.0-or-
// later); combined-waveform tables generated from libresidfp
// (trx64/trx_sid_c64u/mem). Written without UltiSID, reDIP or other HDL sources.
//
// One pass per C64 cycle, started by `start` (one clock), on a clock of at
// least 48 cycles per C64 cycle (the design target: 2 x dot4x, 64):
//   t = 0       the op of the previous C64 cycle (op_*) is committed: a write
//               goes into the register file, its old value is kept
//   t = 1..24   voice slot i = 3*sid + v enters (stage A at t = 1 + i)
//   t = i+9     voice i written back; done for all at t = 32
//   t = 12+4s   filter, mixer and bus value of SID s (4 clocks), done at t = 44
// Read-back (OSC3, ENV3, bus value per SID) is valid from t = 44 to the next
// start, so a CPU read in the C64 cycle that a pass runs in sees that pass.
//
// Voice pipeline records (R1..R8 = clocks after stage A):
//   A  (t_i)    state read, write effects, oscillator clock, envelope clock
//   IX (t_i+3)  waveform index, table addresses (ring candidates a/b)
//   T  (t_i+4)  directory BRAM out -> data BRAM address
//   F  (t_i+5)  table out, waveform output, MSB pulldown, noise write-back
//   D  (t_i+6)  DCA multiply
//   W  (t_i+8)  oscillator sync, write back
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module trx_sid_engine_c64u #(
    parameter MEMDIR = ""                   // directory of sid_wf_*.mem, sid_curve_init.mem
) (
    input  wire        clk,
    input  wire        rst,                 // SID /RES: power-on state for all 8 (32 clocks)
    input  wire        start,               // one clock: a C64 cycle happened
    // the CPU op of the previous C64 cycle, stable from start to t = 44
    input  wire        op_valid_i,
    input  wire        op_we_i,
    input  wire  [2:0] op_sid_i,
    input  wire  [4:0] op_reg_i,
    input  wire  [7:0] op_wdata_i,
    input  wire  [7:0] op_rdata_i,          // what the read returned (OSC3/ENV3/POT)
    // per UltiSID u (bit u): WAVES (1 = 8580), RES (1 = high), DIGI 0..3
    input  wire  [1:0] cfg_8580,
    input  wire  [1:0] cfg_res_high,
    input  wire  [3:0] cfg_digi,
    // filter curve RAM, app side: byte writes, 0x000-0x7FF UltiSID 1, 0x800-0xFFF UltiSID 2
    input  wire        cv_clk,
    input  wire        cv_we,
    input  wire [11:0] cv_addr,
    input  wire  [7:0] cv_wdata,
    // read-back for the bus (asynchronous)
    input  wire  [2:0] rb_sid,
    output wire  [7:0] rb_osc3,
    output wire  [7:0] rb_env3,
    output wire  [7:0] rb_bus,
    output wire [191:0] rb_all,             // {bus, ENV3, OSC3} of SID s at 24*s (registers, for a copy in the C64 clock)
    // envelope levels for the app (+0x80..+0x9F): {sid, voice}, asynchronous
    input  wire  [4:0] lv_addr,
    output wire  [7:0] lv_data,
    // samples: one per SID per pass, and the two UltiSID sums
    output reg         smp_valid,
    output reg   [2:0] smp_sid,
    output reg  signed [19:0] smp_data,
    output reg         sum_valid,           // one clock at t = 44
    output reg  signed [19:0] sum_u0,       // SIDs 0-3, saturated
    output reg  signed [19:0] sum_u1,       // SIDs 4-7
    output wire        busy,
    // state access (ARCH / T20 copy), clk, only while !busy (between passes):
    //   0x000 + 8*e + w   voice e = {sid, v}: state record word w (0..6, 218 bits)
    //   0x100 + 2*e       {AD, CTRL, FREQ HI, FREQ LO}
    //   0x101 + 2*e       {SR, 4'b0, PW HI, PW LO}
    //   0x180 + 4*s       {MODE/VOL, RES/FILT, FC HI, 5'b0, FC LO}
    //   0x181/2/3 + 4*s   filter state Vbp / Vlp / Vhp (25 bit, sign-extended)
    //   0x1A0 + s         {4'b0, bus TTL (20), bus value (8)}
    input  wire  [8:0] st_addr,
    input  wire        st_we,
    input  wire [31:0] st_wdata,
    output reg  [31:0] st_rdata
);

// ---------------------------------------------------------------------------
// constants (sidref.cpp)
localparam [23:0] SHIFT_MASK = ~24'h522914 & 24'h7FFFFF;  // bits 22,20,17,13,11,8,4,2 clear
function [15:0] adsr_rate(input [3:0] i);
    case (i)
    4'd0: adsr_rate = 16'h007f; 4'd1: adsr_rate = 16'h3000; 4'd2: adsr_rate = 16'h1e00; 4'd3: adsr_rate = 16'h0660;
    4'd4: adsr_rate = 16'h0182; 4'd5: adsr_rate = 16'h5573; 4'd6: adsr_rate = 16'h000e; 4'd7: adsr_rate = 16'h3805;
    4'd8: adsr_rate = 16'h2424; 4'd9: adsr_rate = 16'h2220; 4'd10: adsr_rate = 16'h090c; 4'd11: adsr_rate = 16'h0ecd;
    4'd12: adsr_rate = 16'h010e; 4'd13: adsr_rate = 16'h23f7; 4'd14: adsr_rate = 16'h5237; default: adsr_rate = 16'h64a8;
    endcase
endfunction
function [10:0] divq(input hi, input [3:0] r);
    if (hi)
        case (r)
        4'd0: divq = 1448; 4'd1: divq = 1328; 4'd2: divq = 1218; 4'd3: divq = 1117; 4'd4: divq = 1024; 4'd5: divq = 939;
        4'd6: divq = 861; 4'd7: divq = 790; 4'd8: divq = 724; 4'd9: divq = 664; 4'd10: divq = 609; 4'd11: divq = 558;
        4'd12: divq = 512; 4'd13: divq = 470; 4'd14: divq = 431; default: divq = 395;
        endcase
    else
        case (r)
        4'd0: divq = 1448; 4'd1: divq = 1324; 4'd2: divq = 1219; 4'd3: divq = 1129; 4'd4: divq = 1052; 4'd5: divq = 984;
        4'd6: divq = 925; 4'd7: divq = 872; 4'd8: divq = 826; 4'd9: divq = 783; 4'd10: divq = 745; 4'd11: divq = 711;
        4'd12: divq = 679; 4'd13: divq = 651; 4'd14: divq = 624; default: divq = 600;
        endcase
endfunction
// exponential counter period codes: 1 -> 1, 2 -> 2, 3 -> 4, 4 -> 8, 5 -> 16, 6 -> 30
function [4:0] eper_val(input [2:0] c);
    case (c)
    3'd2: eper_val = 5'd2; 3'd3: eper_val = 5'd4; 3'd4: eper_val = 5'd8; 3'd5: eper_val = 5'd16;
    3'd6: eper_val = 5'd30; default: eper_val = 5'd1;
    endcase
endfunction
function [11:0] noise_out_of(input [22:0] s);
    noise_out_of = {s[2], s[4], s[8], s[11], s[13], s[17], s[20], s[22], 4'b0};
endfunction
function [22:0] noise_wb(input [11:0] o);
    begin
        noise_wb = 23'd0;
        noise_wb[2] = o[11]; noise_wb[4] = o[10]; noise_wb[8] = o[9]; noise_wb[11] = o[8];
        noise_wb[13] = o[7]; noise_wb[17] = o[6]; noise_wb[20] = o[5]; noise_wb[22] = o[4];
    end
endfunction
function do_writeback(input [3:0] o, input [3:0] n, input is6581);
    do_writeback = !(o <= 4'd8 || n < 4'd8 || (n == 4'd8 && o != 4'hf) ||
                     (is6581 && ((o[1:0] == 2'd1 && n[1:0] == 2'd2) || (o[1:0] == 2'd2 && n[1:0] == 2'd1))) ||
                     o == 4'hc || n == 4'hc);
endfunction
// combined table: -1 none, else 0 ST, 1 PT, 2 PS, 3 PST, 4 NP
function [2:0] pd_table(input [3:0] w);
    case (w[2:0])
    3'd3: pd_table = 3'd0;
    3'd4: pd_table = w[3] ? 3'd4 : 3'd7;
    3'd5: pd_table = 3'd1;
    3'd6: pd_table = 3'd2;
    3'd7: pd_table = 3'd3;
    default: pd_table = 3'd7;
    endcase
endfunction
function [14:0] pd_index(input is8580, input [2:0] t, input [11:0] wv);
    case (t)
    3'd0: pd_index = {is8580, 14'h0000 + {2'd0, wv}};
    3'd1: pd_index = {is8580, 14'h1000 + {3'd0, wv[11:1]}};
    3'd2: pd_index = {is8580, 14'h1800 + {2'd0, wv}};
    3'd3: pd_index = {is8580, 14'h2800 + {2'd0, wv}};
    default: pd_index = {is8580, 14'h3800 + {6'd0, wv[11:4]}};
    endcase
endfunction

// ---------------------------------------------------------------------------
// voice state record: W part (bits 0-92, RAM vsw, written back at W) and
// A part (bits 93-217, RAM vsa, written at the end of stage A)
localparam O_ACC = 0;               // 24
localparam O_SR = 24;               // 23
localparam O_NOUT = 47;             // 12
localparam O_POUT = 59;             // 1
localparam O_WAV = 60;              // 12
localparam O_TSP = 72;              // 12
localparam O_OSC3 = 84;             // 8 (osc3 >> 4)
localparam O_MSBR = 92;             // 1
localparam VW_W = 93;
localparam O_SRL = 93;              // 23
localparam O_SRP = 116;             // 2
localparam O_SRR = 118;             // 20
localparam O_FTTL = 138;            // 20
localparam O_TOR = 158;             // 1
localparam O_LFSR = 159;            // 15
localparam O_RIDX = 174;            // 4
localparam O_ECNT = 178;            // 5
localparam O_EPER = 183;            // 3
localparam O_NPER = 186;            // 3
localparam O_SPIPE = 189;           // 2
localparam O_EPIPE = 191;           // 3
localparam O_XPIPE = 194;           // 2
localparam O_ST = 196;              // 2
localparam O_NST = 198;             // 2
localparam O_CEN = 200;             // 1
localparam O_RLFSR = 201;           // 1
localparam O_CNT = 202;             // 8
localparam O_ENV3 = 210;            // 8
localparam VS_W = 218;
localparam VA_W = VS_W - VW_W;
// pipeline record: the W part, then SR pipe, envelope counter, ENV3
localparam P_SRP = 93, P_CNT = 95, P_ENV3 = 103, PR_W = 111;

// power-on / reset record (sidref.cpp Sid::reset after construction)
localparam [VS_W-1:0] VS_RESET = {
    8'h00,          // env3
    8'haa,          // counter
    1'b1,           // reset_lfsr
    1'b1,           // counter_enabled
    2'd2,           // next_state
    2'd2,           // state
    2'd0, 3'd0, 2'd0, // xpipe, epipe, spipe
    3'd0, 3'd1,     // nper, eper
    5'd0, 4'd0,     // ecnt, ridx
    15'h7fff,       // lfsr
    1'b1,           // tor
    20'd0, 20'd0,   // fttl, srr
    2'd0,           // srp
    23'h7fffff,     // srl
    1'b0,           // msbr
    8'h00,          // osc3
    12'h555,        // tsp
    12'h000,        // wav
    1'b1,           // pout
    12'hfe0,        // nout
    23'h3fffff,     // sr
    24'h555555      // acc
};

// ---------------------------------------------------------------------------
// register file: voice registers by {sid, v}, filter registers by sid
reg  [7:0] r_flo [0:31], r_fhi [0:31], r_plo [0:31], r_ctl [0:31], r_ad [0:31], r_sr [0:31];
reg  [3:0] r_phi [0:31];
reg  [2:0] r_fcl [0:7];
reg  [7:0] r_fch [0:7], r_rf [0:7], r_mv [0:7];
reg  [VW_W-1:0] vsw [0:31];
reg  [VA_W-1:0] vsa [0:31];
reg  [49:0] fs_lp [0:7];                    // {vbp, vlp}
reg  [24:0] fs_hp [0:7];
reg  [27:0] bs [0:7];                       // {ttl, value}
reg  [7:0]  rbo [0:7], rbe [0:7], rbb [0:7];
reg  [7:0]  lvl [0:31];

integer k;
initial begin
    for (k = 0; k < 32; k = k + 1) begin
        r_flo[k] = 0; r_fhi[k] = 0; r_plo[k] = 0; r_phi[k] = 0; r_ctl[k] = 0; r_ad[k] = 0; r_sr[k] = 0;
        vsw[k] = VS_RESET[VW_W-1:0]; vsa[k] = VS_RESET[VS_W-1:VW_W]; lvl[k] = 0;
    end
    for (k = 0; k < 8; k = k + 1) begin
        r_fcl[k] = 0; r_fch[k] = 0; r_rf[k] = 0; r_mv[k] = 0;
        fs_lp[k] = 0; fs_hp[k] = 0; bs[k] = 0; rbo[k] = 0; rbe[k] = 0; rbb[k] = 0;
    end
end

assign rb_osc3 = rbo[rb_sid];
assign rb_env3 = rbe[rb_sid];
assign rb_bus  = rbb[rb_sid];
genvar gs;
generate for (gs = 0; gs < 8; gs = gs + 1) begin : g_rb
    assign rb_all[24*gs +: 24] = {rbb[gs], rbe[gs], rbo[gs]};
end endgenerate
assign lv_data = lvl[lv_addr];

// combined waveform tables (2 x RAMB36, two read ports each)
reg  [8:0] wf_dir [0:4095];
reg  [7:0] wf_data [0:4095];
initial begin
    $readmemh({MEMDIR, "/sid_wf_dir.mem"}, wf_dir);
    $readmemh({MEMDIR, "/sid_wf_data.mem"}, wf_data);
end
// filter curve: 2048 x 16 with byte writes (one RAMB36)
reg [15:0] cv [0:2047];
initial $readmemh({MEMDIR, "/sid_curve_init.mem"}, cv);
always @(posedge cv_clk)
    if (cv_we) begin
        if (cv_addr[0]) cv[cv_addr[11:1]][15:8] <= cv_wdata;
        else            cv[cv_addr[11:1]][7:0]  <= cv_wdata;
    end

// ---------------------------------------------------------------------------
// pass sequencing
reg  [5:0] t = 6'd63;
reg        running = 1'b0;
reg  [4:0] rst_cnt = 5'd0;
reg        rst_run = 1'b0;
assign busy = running || rst_run;

always @(posedge clk) begin
    if (start && !rst_run) begin
        t <= 6'd0;
        running <= 1'b1;
    end else if (running) begin
        t <= t + 6'd1;
        if (t == 6'd62) running <= 1'b0;
    end
    if (rst) begin
        rst_run <= 1'b1;
        rst_cnt <= 5'd0;
        running <= 1'b0;
    end else if (rst_run) begin
        rst_cnt <= rst_cnt + 5'd1;
        if (rst_cnt == 5'd31) rst_run <= 1'b0;
    end
end
wire t0 = running && t == 6'd0;
// the op, taken into this clock at start (the inputs come from the C64's clock
// and are stable from a C64 clock edge before start to the next phi2_n)
reg        op_valid = 1'b0, op_we = 1'b0;
reg  [2:0] op_sid = 3'd0;
reg  [4:0] op_reg = 5'd0;
reg  [7:0] op_wdata = 8'd0, op_rdata = 8'd0;
always @(posedge clk)
    if (start) begin
        op_valid <= op_valid_i; op_we <= op_we_i; op_sid <= op_sid_i; op_reg <= op_reg_i;
        op_wdata <= op_wdata_i; op_rdata <= op_rdata_i;
    end

// commit (t = 0)
wire       op_w   = op_valid && op_we;
wire [1:0] op_v   = op_reg >= 5'd14 ? 2'd2 : op_reg >= 5'd7 ? 2'd1 : 2'd0;
wire [2:0] op_r7  = op_reg - {op_v, 3'b0} + {3'b0, op_v};   // op_reg - 7*v
wire [4:0] op_vi  = {op_sid, op_v};
reg  [7:0] wr_old;
always @(posedge clk) if (t0 && op_w) wr_old <= r_ctl[op_vi];
// one write port per register array: reset, state port, or the committed op
wire       st_w   = st_we && !busy;
wire       rg_stv = st_w && st_addr[8:6] == 3'b100;           // 0x100-0x13F
wire       rg_stf = st_w && st_addr[8:5] == 4'b1100 && st_addr[1:0] == 2'd0;
wire [4:0] rg_wa  = rst_run ? rst_cnt : rg_stv ? st_addr[5:1] : op_vi;
wire [2:0] rf_wa  = rst_run ? rst_cnt[2:0] : rg_stf ? st_addr[4:2] : op_sid;
wire       cw     = t0 && op_w && op_reg <= 5'd20;
wire       cf     = t0 && op_w;
wire       wv0    = rg_stv && !st_addr[0], wv1 = rg_stv && st_addr[0];
always @(posedge clk) if (rst_run || wv0 || (cw && op_r7 == 3'd0)) r_flo[rg_wa] <= rst_run ? 8'd0 : wv0 ? st_wdata[7:0] : op_wdata;
always @(posedge clk) if (rst_run || wv0 || (cw && op_r7 == 3'd1)) r_fhi[rg_wa] <= rst_run ? 8'd0 : wv0 ? st_wdata[15:8] : op_wdata;
always @(posedge clk) if (rst_run || wv1 || (cw && op_r7 == 3'd2)) r_plo[rg_wa] <= rst_run ? 8'd0 : wv1 ? st_wdata[7:0] : op_wdata;
always @(posedge clk) if (rst_run || wv1 || (cw && op_r7 == 3'd3)) r_phi[rg_wa] <= rst_run ? 4'd0 : wv1 ? st_wdata[11:8] : op_wdata[3:0];
always @(posedge clk) if (rst_run || wv0 || (cw && op_r7 == 3'd4)) r_ctl[rg_wa] <= rst_run ? 8'd0 : wv0 ? st_wdata[23:16] : op_wdata;
always @(posedge clk) if (rst_run || wv0 || (cw && op_r7 == 3'd5)) r_ad[rg_wa]  <= rst_run ? 8'd0 : wv0 ? st_wdata[31:24] : op_wdata;
always @(posedge clk) if (rst_run || wv1 || (cw && op_r7 == 3'd6)) r_sr[rg_wa]  <= rst_run ? 8'd0 : wv1 ? st_wdata[31:24] : op_wdata;
always @(posedge clk) if (rst_run || rg_stf || (cf && op_reg == 5'd21)) r_fcl[rf_wa] <= rst_run ? 3'd0 : rg_stf ? st_wdata[2:0] : op_wdata[2:0];
always @(posedge clk) if (rst_run || rg_stf || (cf && op_reg == 5'd22)) r_fch[rf_wa] <= rst_run ? 8'd0 : rg_stf ? st_wdata[15:8] : op_wdata;
always @(posedge clk) if (rst_run || rg_stf || (cf && op_reg == 5'd23)) r_rf[rf_wa]  <= rst_run ? 8'd0 : rg_stf ? st_wdata[23:16] : op_wdata;
always @(posedge clk) if (rst_run || rg_stf || (cf && op_reg == 5'd24)) r_mv[rf_wa]  <= rst_run ? 8'd0 : rg_stf ? st_wdata[31:24] : op_wdata;

// slot counter: slot i at t = 1 + i
reg  [2:0] a_sid = 0;
reg  [1:0] a_v = 0;
reg        a_val = 0;
always @(posedge clk) begin
    if (t0) begin
        a_sid <= 3'd0; a_v <= 2'd0; a_val <= 1'b1;
    end else if (a_val) begin
        if (a_v == 2'd2) begin
            a_v <= 2'd0;
            a_sid <= a_sid + 3'd1;
            if (a_sid == 3'd7) a_val <= 1'b0;
        end else
            a_v <= a_v + 2'd1;
    end
    if (rst_run) a_val <= 1'b0;
end

// ---------------------------------------------------------------------------
// stage A: write effects, oscillator clock, envelope clock
wire [4:0] a_vi  = {a_sid, a_v};
wire [4:0] vs_ra = busy ? a_vi : st_addr[7:3];
wire [VS_W-1:0] a_s = {vsa[vs_ra], vsw[vs_ra]};
wire       a_8580 = cfg_8580[a_sid[2]];
wire [4:0] rg_ra = busy ? a_vi : st_addr[5:1];
wire [7:0] a_ctl = r_ctl[rg_ra];
wire [7:0] a_ad  = r_ad[rg_ra];
wire [7:0] a_srr = r_sr[rg_ra];
wire [15:0] a_freq = {r_fhi[rg_ra], r_flo[rg_ra]};
wire [4:0] a_r   = 5'd7 * a_v;
wire a_wctl = op_w && op_sid == a_sid && op_reg == a_r + 5'd4;
wire a_wad  = op_w && op_sid == a_sid && op_reg == a_r + 5'd5;
wire a_wsr  = op_w && op_sid == a_sid && op_reg == a_r + 5'd6;
wire [3:0] a_phi = r_phi[rg_ra];
wire [7:0] a_plo = r_plo[rg_ra];
// R0: everything stage A needs, registered out of the RAMs
reg  [VS_W-1:0] r0_s;
reg  [7:0] r0_ctl, r0_ad, r0_srr, r0_plo;
reg  [15:0] r0_freq;
reg  [3:0] r0_phi;
reg  [2:0] r0_sid;
reg  [1:0] r0_v;
reg        r0_val = 1'b0, r0_85, r0_wctl, r0_wad, r0_wsr;
always @(posedge clk) begin
    r0_s <= a_s; r0_ctl <= a_ctl; r0_ad <= a_ad; r0_srr <= a_srr; r0_freq <= a_freq;
    r0_phi <= a_phi; r0_plo <= a_plo; r0_sid <= a_sid; r0_v <= a_v; r0_85 <= a_8580;
    r0_val <= a_val && running && !rst_run;
    r0_wctl <= a_wctl; r0_wad <= a_wad; r0_wsr <= a_wsr;
end

reg [VS_W-1:0] a_n;                         // next state after stage A
reg [23:0] acc;
reg [22:0] sr, srl;
reg [1:0]  srp;
reg [11:0] nout;
reg        pout, tor, msbr;
reg [19:0] srr, fttl;
reg [3:0]  wf_prev;
reg        test_prev, gate_prev;
reg [23:0] acc_old, bits_set;
reg [14:0] lfsr;
reg [3:0]  ridx;
reg [4:0]  ecnt;
reg [2:0]  eper, nper;
reg [1:0]  spipe, xpipe, st, nst;
reg [2:0]  epipe;
reg        cen, rlfsr;
reg [7:0]  cnt, env3;
reg [22:0] lt;
reg        b22, fired;
reg [11:0] wav;
reg [7:0]  osc3;
reg [VS_W-1:0] a1_n, r1_s;
reg [23:0] acc_1, acc_old_1, bits_set_1;
reg [22:0] sr_1, srl_1;
reg [1:0]  srp_1, spipe_1, xpipe_1, st_1, nst_1;
reg [11:0] nout_1;
reg        pout_1, tor_1, msbr_1, cen_1, rlfsr_1, b22_1, fired_1;
reg [19:0] srr_1, fttl_1;
reg [3:0]  wf_prev_1, ridx_1;
reg        test_prev_1, gate_prev_1;
reg [14:0] lfsr_1;
reg [4:0]  ecnt_1;
reg [2:0]  eper_1, nper_1, epipe_1;
reg [7:0]  cnt_1, env3_1;
always @(*) begin : stage_a1
    acc_1 = r0_s[O_ACC +: 24]; sr_1 = r0_s[O_SR +: 23]; srl_1 = r0_s[O_SRL +: 23]; srp_1 = r0_s[O_SRP +: 2];
    nout_1 = r0_s[O_NOUT +: 12]; pout_1 = r0_s[O_POUT]; tor_1 = r0_s[O_TOR]; msbr_1 = r0_s[O_MSBR];
    srr_1 = r0_s[O_SRR +: 20]; fttl_1 = r0_s[O_FTTL +: 20];
    lfsr_1 = r0_s[O_LFSR +: 15]; ridx_1 = r0_s[O_RIDX +: 4]; ecnt_1 = r0_s[O_ECNT +: 5]; eper_1 = r0_s[O_EPER +: 3];
    nper_1 = r0_s[O_NPER +: 3]; spipe_1 = r0_s[O_SPIPE +: 2]; epipe_1 = r0_s[O_EPIPE +: 3]; xpipe_1 = r0_s[O_XPIPE +: 2];
    st_1 = r0_s[O_ST +: 2]; nst_1 = r0_s[O_NST +: 2]; cen_1 = r0_s[O_CEN]; rlfsr_1 = r0_s[O_RLFSR];
    cnt_1 = r0_s[O_CNT +: 8]; env3_1 = r0_s[O_ENV3 +: 8];
    acc_old_1 = 24'd0; bits_set_1 = 24'd0; b22_1 = 1'b0; fired_1 = 1'b0;
    wf_prev_1 = r0_wctl ? wr_old[7:4] : r0_ctl[7:4];
    test_prev_1 = r0_wctl ? wr_old[3] : r0_ctl[3];
    gate_prev_1 = r0_wctl ? wr_old[0] : r0_ctl[0];

    // ---- control register write: waveform generator (wave_ctrl)
    if (r0_wctl && r0_ctl[7:4] != wf_prev_1 && r0_ctl[7:4] == 4'd0)
        fttl_1 = r0_85 ? 20'd800000 : 20'd54000;
    if (r0_wctl && r0_ctl[3] != test_prev_1) begin
        if (r0_ctl[3]) begin
            acc_1 = 24'd0;
            srp_1 = 2'd0;
            srl_1 = sr_1;
            srr_1 = r0_85 ? 20'd986000 : 20'd50000;
        end else begin
            // shift_phase2(wf_prev_1, wf)
            if (do_writeback(wf_prev_1, r0_ctl[7:4], !r0_85))
                srl_1 = (sr_1 & SHIFT_MASK[22:0]) | noise_wb(r0_s[O_WAV +: 12]);
            b22_1 = tor_1 | srl_1[0];
            sr_1 = {b22_1 ^ srl_1[5], srl_1[22:1]};
            nout_1 = noise_out_of(sr_1);
        end
    end
    // ---- control register write: envelope (env_ctrl)
    if (r0_wctl && r0_ctl[0] != gate_prev_1) begin
        if (r0_ctl[0]) begin
            nst_1 = 2'd0;
            spipe_1 = 2'd2;
            if (rlfsr_1 || xpipe_1 == 2'd2)
                epipe_1 = (eper_1 == 3'd1 || xpipe_1 == 2'd2) ? 3'd2 : 3'd4;
            else if (xpipe_1 == 2'd1)
                spipe_1 = 2'd3;
        end else begin
            nst_1 = 2'd2;
            spipe_1 = epipe_1 != 3'd0 ? 2'd3 : 2'd2;
        end
    end
    if (r0_wad) begin
        if (st_1 == 2'd0) ridx_1 = r0_ad[7:4];
        else if (st_1 == 2'd1) ridx_1 = r0_ad[3:0];
    end
    if (r0_wsr && st_1 == 2'd2) ridx_1 = r0_srr[3:0];

    a1_n = r0_s;
    a1_n[O_ACC +: 24] = acc_1; a1_n[O_SR +: 23] = sr_1; a1_n[O_SRL +: 23] = srl_1; a1_n[O_SRP +: 2] = srp_1;
    a1_n[O_NOUT +: 12] = nout_1; a1_n[O_SRR +: 20] = srr_1; a1_n[O_FTTL +: 20] = fttl_1;
    a1_n[O_RIDX +: 4] = ridx_1; a1_n[O_SPIPE +: 2] = spipe_1; a1_n[O_EPIPE +: 3] = epipe_1; a1_n[O_NST +: 2] = nst_1;
end
// A1 -> A2
reg  [7:0] r1_ctl, r1_ad, r1_srr, r1_plo;
reg  [15:0] r1_freq;
reg  [3:0] r1_phi;
reg  [2:0] r1_sid;
reg  [1:0] r1_v;
reg        r1_val = 1'b0, r1_85;
always @(posedge clk) begin
    r1_s <= a1_n; r1_ctl <= r0_ctl; r1_ad <= r0_ad; r1_srr <= r0_srr; r1_freq <= r0_freq;
    r1_phi <= r0_phi; r1_plo <= r0_plo; r1_sid <= r0_sid; r1_v <= r0_v; r1_85 <= r0_85;
    r1_val <= r0_val && !rst_run;
end
always @(*) begin
    acc = r1_s[O_ACC +: 24]; sr = r1_s[O_SR +: 23]; srl = r1_s[O_SRL +: 23]; srp = r1_s[O_SRP +: 2];
    nout = r1_s[O_NOUT +: 12]; pout = r1_s[O_POUT]; tor = r1_s[O_TOR]; msbr = r1_s[O_MSBR];
    srr = r1_s[O_SRR +: 20]; fttl = r1_s[O_FTTL +: 20];
    lfsr = r1_s[O_LFSR +: 15]; ridx = r1_s[O_RIDX +: 4]; ecnt = r1_s[O_ECNT +: 5]; eper = r1_s[O_EPER +: 3];
    nper = r1_s[O_NPER +: 3]; spipe = r1_s[O_SPIPE +: 2]; epipe = r1_s[O_EPIPE +: 3]; xpipe = r1_s[O_XPIPE +: 2];
    st = r1_s[O_ST +: 2]; nst = r1_s[O_NST +: 2]; cen = r1_s[O_CEN]; rlfsr = r1_s[O_RLFSR];
    cnt = r1_s[O_CNT +: 8]; env3 = r1_s[O_ENV3 +: 8];
    acc_old = 24'd0; bits_set = 24'd0; b22 = 1'b0; fired = 1'b0;
    wf_prev = 4'd0; test_prev = 1'b0; gate_prev = 1'b0;

    // ---- oscillator clock (wave_clock)
    if (r1_ctl[3]) begin
        if (srr != 20'd0) begin
            srr = srr - 20'd1;
            if (srr == 20'd0) begin
                sr = sr | {1'b0, sr[22:1]} | 23'h400000;
                if (sr != 23'h7fffff) srr = r1_85 ? 20'd314300 : 20'd15000;
                srl = sr;
                nout = noise_out_of(sr);
            end
        end
        tor = 1'b1;
        pout = 1'b1;
    end else begin
        acc_old = acc;
        acc = acc + {8'd0, r1_freq};
        bits_set = ~acc_old & acc;
        msbr = bits_set[23];
        if (bits_set[19])
            srp = 2'd2;
        else if (srp != 2'd0) begin
            srp = srp - 2'd1;
            if (srp == 2'd0) begin
                if (do_writeback(r1_ctl[7:4], r1_ctl[7:4], !r1_85))
                    srl = (sr & SHIFT_MASK[22:0]) | noise_wb(r1_s[O_WAV +: 12]);
                b22 = tor | srl[0];
                sr = {b22 ^ srl[5], srl[22:1]};
                nout = noise_out_of(sr);
            end else begin
                tor = 1'b0;
                srl = sr;
            end
        end
    end

    // ---- envelope clock (env_clock)
    env3 = cnt;
    if (nper != 3'd0) begin
        eper = nper;
        nper = 3'd0;
    end
    if (spipe != 2'd0) begin
        spipe = spipe - 2'd1;
        case (nst)
        2'd0: begin
            if (spipe == 2'd1)
                ridx = r1_ad[3:0];
            else if (spipe == 2'd0) begin
                st = 2'd0;
                ridx = r1_ad[7:4];
                cen = 1'b1;
            end
        end
        2'd1: begin
            if (spipe == 2'd0) begin
                st = 2'd1;
                ridx = r1_ad[3:0];
            end
        end
        default: begin
            if ((st == 2'd0 && spipe == 2'd0) || (st == 2'd1 && spipe == 2'd1)) begin
                st = 2'd2;
                ridx = r1_srr[3:0];
            end
        end
        endcase
    end
    // env_pipe, then exp_pipe, then the rate counter reset (each "if (x != 0 &&
    // --x == 0)": a pipe that counts down without reaching 0 lets the next run)
    fired = 1'b0;
    if (epipe != 3'd0) begin
        epipe = epipe - 3'd1;
        if (epipe == 3'd0) begin
            fired = 1'b1;
            if (cen) begin
                if (st == 2'd0) begin
                    cnt = cnt + 8'd1;
                    if (cnt == 8'hff) begin
                        nst = 2'd1;
                        spipe = 2'd3;
                    end
                end else begin
                    cnt = cnt - 8'd1;
                    if (cnt == 8'h00) cen = 1'b0;
                end
                case (cnt)
                8'hff, 8'h00: nper = 3'd1;
                8'h5d: nper = 3'd2;
                8'h36: nper = 3'd3;
                8'h1a: nper = 3'd4;
                8'h0e: nper = 3'd5;
                8'h06: nper = 3'd6;
                default: ;
                endcase
            end
        end
    end
    if (!fired && xpipe != 2'd0) begin
        xpipe = xpipe - 2'd1;
        if (xpipe == 2'd0) begin
            fired = 1'b1;
            ecnt = 5'd0;
            if ((st == 2'd1 && cnt != {r1_srr[7:4], r1_srr[7:4]}) || st == 2'd2)
                epipe = 3'd1;
        end
    end
    if (!fired && rlfsr) begin
        lfsr = 15'h7fff;
        rlfsr = 1'b0;
        if (st == 2'd0) begin
            ecnt = 5'd0;
            epipe = 3'd2;
        end else if (cen) begin
            ecnt = ecnt + 5'd1;
            if (ecnt == eper_val(eper))
                xpipe = eper != 3'd1 ? 2'd2 : 2'd1;
        end
    end
    if ({1'b0, lfsr} != adsr_rate(ridx))
        lfsr = {lfsr[1] ^ lfsr[0], lfsr[14:1]};
    else
        rlfsr = 1'b1;

    // floating DAC (WaveformGenerator::output with no waveform): independent
    // of the clock above, so done here; F leaves wav/osc3 alone then
    wav = r1_s[O_WAV +: 12];
    osc3 = r1_s[O_OSC3 +: 8];
    if (r1_ctl[7:4] == 4'd0 && fttl != 20'd0) begin
        fttl = fttl - 20'd1;
        if (fttl == 20'd0) begin
            wav = wav & {1'b0, wav[11:1]};
            osc3 = wav[11:4];
            if (wav != 12'd0) fttl = r1_85 ? 20'd50000 : 20'd1400;
        end
    end

    a_n = r1_s;
    a_n[O_WAV +: 12] = wav; a_n[O_OSC3 +: 8] = osc3;
    a_n[O_ACC +: 24] = acc; a_n[O_SR +: 23] = sr; a_n[O_SRL +: 23] = srl; a_n[O_SRP +: 2] = srp;
    a_n[O_NOUT +: 12] = nout; a_n[O_POUT] = pout; a_n[O_TOR] = tor; a_n[O_MSBR] = msbr;
    a_n[O_SRR +: 20] = srr; a_n[O_FTTL +: 20] = fttl;
    a_n[O_LFSR +: 15] = lfsr; a_n[O_RIDX +: 4] = ridx; a_n[O_ECNT +: 5] = ecnt; a_n[O_EPER +: 3] = eper;
    a_n[O_NPER +: 3] = nper; a_n[O_SPIPE +: 2] = spipe; a_n[O_EPIPE +: 3] = epipe; a_n[O_XPIPE +: 2] = xpipe;
    a_n[O_ST +: 2] = st; a_n[O_NST +: 2] = nst; a_n[O_CEN] = cen; a_n[O_RLFSR] = rlfsr;
    a_n[O_CNT +: 8] = cnt; a_n[O_ENV3 +: 8] = env3;
end

// record pipeline: state + side info
// side info: valid, sid, v, ctl, pw, is8580
localparam SI_W = 1 + 3 + 2 + 8 + 12 + 1;
reg [PR_W-1:0] p1, p2, p3, p4, p45, p5, p6, p7, p8;
wire [PR_W-1:0] a_p = {a_n[O_ENV3 +: 8], a_n[O_CNT +: 8], a_n[O_SRP +: 2], a_n[VW_W-1:0]};
reg [SI_W-1:0] q1, q2, q3, q4, q45, q5, q6, q7, q8;
wire [SI_W-1:0] a_q = {r1_val, r1_sid, r1_v, r1_ctl, r1_phi, r1_plo, r1_85};
`define QV(q)   q[SI_W-1]
`define QSID(q) q[SI_W-2 -: 3]
`define QVO(q)  q[SI_W-5 -: 2]
`define QCTL(q) q[SI_W-7 -: 8]
`define QCTLB(q,b) q[SI_W-14+(b)]   // one bit of QCTL (Vivado: no index on a part-select)
`define QPW(q)  q[12:1]
`define Q85(q)  q[0]

// ---- IX (record in p3): waveform index candidates, table addresses
wire [3:0]  x_wf   = `QCTL(q3) >> 4;
wire        x_ring = !`QCTLB(q3,5) && `QCTLB(q3,2);
wire        x_8580 = `Q85(q3);
// previous voice: v0 -> v2 of the same SID (in p1), v1/v2 -> v-1 (in p4)
wire        x_pmsb = `QVO(q3) == 2'd0 ? p1[O_ACC + 23] : p4[O_ACC + 23];
wire [23:0] x_acc  = p3[O_ACC +: 24];
wire [11:0] x_ix_a = {x_acc[23] ^ (x_ring & ~x_pmsb), x_acc[22:12]};
wire [11:0] x_ix_b = {x_acc[23] ^ x_ring, x_acc[22:12]};
function [11:0] base_of(input [1:0] w, input [11:0] ix);
    case (w)
    2'd0: base_of = 12'hfff;
    2'd1: base_of = {(ix[11] ? ~ix[10:0] : ix[10:0]), 1'b0};
    2'd2: base_of = ix;
    default: base_of = ix & {ix[10:0], 1'b0};
    endcase
endfunction
wire [11:0] x_pm   = x_wf[2] ? {12{p3[O_POUT]}} : 12'hfff;
wire [11:0] x_nm   = x_wf[3] ? p3[O_NOUT +: 12] : 12'hfff;
wire [11:0] x_base_a = base_of(x_wf[1:0], x_ix_a);
wire [11:0] x_base_b = base_of(x_wf[1:0], x_ix_b);
wire [11:0] x_wv_a = x_base_a & x_pm & x_nm;
wire [11:0] x_wv_b = x_base_b & x_pm & x_nm;
wire [11:0] x_wv_o = p3[O_TSP +: 12] & x_pm & x_nm;
wire [2:0]  x_t    = pd_table(x_wf);
wire [14:0] x_e_a  = pd_index(x_8580, x_t, x_wv_a);
wire [14:0] x_e_b  = pd_index(x_8580, x_t, x_8580 ? x_wv_o : x_wv_b);

// IX -> p4 extras
reg [11:0] p4_wv_a, p4_wv_b, p4_wv_o, p4_base_a, p4_base_b;
reg [2:0]  p4_e_a, p4_e_b;
reg [11:0] dir_a_q, dir_b_q;                // dir addresses (registered = BRAM address)
reg [8:0]  dir_a, dir_b;
always @(posedge clk) begin
    p4_wv_a <= x_wv_a; p4_wv_b <= x_wv_b; p4_wv_o <= x_wv_o;
    p4_base_a <= x_base_a; p4_base_b <= x_base_b;
    p4_e_a <= x_e_a[2:0]; p4_e_b <= x_e_b[2:0];
    dir_a <= wf_dir[x_e_a[14:3]];
    dir_b <= wf_dir[x_e_b[14:3]];
end
// T (p4): data BRAM read
reg [7:0]  dat_a, dat_b, dat_a2, dat_b2;   // data BRAM out, and its output register
reg [11:0] q_wv_a, q_wv_b, q_wv_o, q_base_a, q_base_b;
reg [11:0] p5_wv_a, p5_wv_b, p5_wv_o, p5_base_a, p5_base_b;
always @(posedge clk) begin
    dat_a <= wf_data[{dir_a, p4_e_a}];
    dat_b <= wf_data[{dir_b, p4_e_b}];
    q_wv_a <= p4_wv_a; q_wv_b <= p4_wv_b; q_wv_o <= p4_wv_o;
    q_base_a <= p4_base_a; q_base_b <= p4_base_b;
    dat_a2 <= dat_a; dat_b2 <= dat_b;
    p5_wv_a <= q_wv_a; p5_wv_b <= q_wv_b; p5_wv_o <= q_wv_o;
    p5_base_a <= q_base_a; p5_base_b <= q_base_b;
end

// ---- F (record in p5): waveform output
reg [PR_W-1:0] f_n;
reg [11:0] f_wav, f_osc3;
reg [23:0] f_acc;
reg [22:0] f_sr;
reg [11:0] f_nout, f_wv;
reg        f_msbr, f_useb;
reg        f_msb_final;                     // previous voice's final MSB (registered)
wire [3:0] f_wf   = `QCTL(q5) >> 4;
wire       f_ring = !`QCTLB(q5,5) && `QCTLB(q5,2);
wire       f_8580 = `Q85(q5);
wire       f_pd   = pd_table(f_wf) != 3'd7;
// previous voice's MSB before its pulldown: v1/v2 -> p6 holds v-1 after F
reg        p6_msb_pre;
always @(*) begin
    f_n = p5;
    f_acc = p5[O_ACC +: 24]; f_sr = p5[O_SR +: 23]; f_nout = p5[O_NOUT +: 12];
    f_wav = p5[O_WAV +: 12]; f_osc3 = {p5[O_OSC3 +: 8], 4'd0};
    f_msbr = p5[O_MSBR];
    // ring source pulled low by its own MSB pulldown: take candidate b
    f_useb = `QVO(q5) != 2'd0 && f_ring && p6_msb_pre && !f_msb_final;
    f_wv = f_useb ? p5_wv_b : p5_wv_a;
    if (f_wf != 4'd0) begin
        f_wav = f_pd ? {(f_useb ? dat_b2 : dat_a2), 4'd0} : f_wv;
        if (f_8580 && f_wf[1:0] != 2'd0) begin
            f_osc3 = f_pd ? {dat_b2, 4'd0} : p5_wv_o;
            f_n[O_TSP +: 12] = f_useb ? p5_base_b : p5_base_a;
        end else
            f_osc3 = f_wav;
        if (!f_8580 && f_wf[1] && !f_wav[11]) begin
            f_msbr = 1'b0;
            f_acc[23] = 1'b0;
        end
        if (f_wf > 4'd8) begin
            if (p5[P_SRP +: 2] != 2'd1 && !`QCTLB(q5,3)) begin
                f_sr = f_sr & (SHIFT_MASK[22:0] | noise_wb(f_wav));
                f_nout = f_nout & f_wav;
            end else
                f_nout = f_wav;
        end
    end
    f_n[O_ACC +: 24] = f_acc; f_n[O_SR +: 23] = f_sr; f_n[O_NOUT +: 12] = f_nout;
    f_n[O_WAV +: 12] = f_wav; f_n[O_OSC3 +: 8] = f_osc3[11:4];
    f_n[O_MSBR] = f_msbr;
    f_n[O_POUT] = f_acc[23:12] >= `QPW(q5);
end
always @(posedge clk) begin
    f_msb_final <= f_acc[23];
    p6_msb_pre  <= p5[O_ACC + 23];
end

// ---- D (record in p6): DCA
reg signed [21:0] p7_vo;
wire signed [12:0] d_wav = {1'b0, p6[O_WAV +: 12]} - (`Q85(q6) ? 13'sh800 : 13'sh380);
wire signed [21:0] d_vo  = d_wav * $signed({1'b0, p6[P_CNT +: 8]});
always @(posedge clk) p7_vo <= d_vo;

// ---- W (record in p8): sync, write back
// sync bits of a SID, taken when its voice 2 is in p6 (v1 in p7, v0 in p8)
wire w_v2in6 = `QV(q6) && `QVO(q6) == 2'd2;
wire [2:0] s_msbr = {p6[O_MSBR], p7[O_MSBR], p8[O_MSBR]};
wire [2:0] s_test = {`QCTLB(q6,3), `QCTLB(q7,3), `QCTLB(q8,3)};
wire [2:0] s_sync = {`QCTLB(q6,1), `QCTLB(q7,1), `QCTLB(q8,1)};
wire s_event = (s_msbr[0] && !s_test[0] && s_sync[1]) || (s_msbr[1] && !s_test[1] && s_sync[2]) ||
               (s_msbr[2] && !s_test[2] && s_sync[0]);
// voice u resets when voice u-1 syncs it
wire [2:0] s_rst = {s_event && s_msbr[1] && s_sync[2] && !(s_sync[1] && s_msbr[0]),
                    s_event && s_msbr[0] && s_sync[1] && !(s_sync[0] && s_msbr[2]),
                    s_event && s_msbr[2] && s_sync[0] && !(s_sync[2] && s_msbr[1])};
reg  [2:0] s_rst_q;
always @(posedge clk) if (w_v2in6) s_rst_q <= s_rst;
wire w_rst = `QVO(q8) == 2'd0 ? (w_v2in6 ? s_rst[0] : 1'b0) : `QVO(q8) == 2'd1 ? s_rst_q[1] : s_rst_q[2];
wire [VW_W-1:0] w_rec = {p8[VW_W-1:24], w_rst ? 24'd0 : p8[23:0]};
wire [4:0] w_vi = {`QSID(q8), `QVO(q8)};

always @(posedge clk) begin
    p1 <= a_p;  q1 <= a_q;
    p2 <= p1;   q2 <= q1;
    p3 <= p2;   q3 <= q2;
    p4 <= p3;   q4 <= q3;
    p45 <= p4;  q45 <= q4;
    p5 <= p45;  q5 <= q45;
    p6 <= f_n;  q6 <= q5;
    p7 <= p6;   q7 <= q6;
    p8 <= p7;   q8 <= q7;
    if (rst_run || !running) q1[SI_W-1] <= 1'b0;
end
// ---- state access
wire [223:0] st_vs = {6'd0, a_s};
always @(*) begin
    st_rdata = 32'd0;
    casez (st_addr)
    9'b0????????: st_rdata = st_vs[st_addr[2:0] * 32 +: 32];
    9'b10?????0?: st_rdata = st_addr[0] ? {a_srr, 4'd0, a_phi, a_plo} : {a_ad, a_ctl, a_freq};
    default:
        if (st_addr[8:5] == 4'b1100)
            case (st_addr[1:0])
            2'd0: st_rdata = {h_mv, h_rf, r_fch[c_sid], 5'd0, r_fcl[c_sid]};
            2'd1: st_rdata = {{7{h_vbp[24]}}, h_vbp};
            2'd2: st_rdata = {{7{h_vlp[24]}}, h_vlp};
            default: st_rdata = {{7{h_vhp[24]}}, h_vhp};
            endcase
        else if (st_addr[8:3] == 6'b110100)
            st_rdata = {4'd0, h_bs};
    endcase
end
reg [223:0] st_m;
always @(*) begin
    st_m = st_vs;
    st_m[st_addr[2:0] * 32 +: 32] = st_wdata;
end

// state RAMs, one write port each
wire vs_stw = st_w && !st_addr[8];
// stage A's part of the record, written one clock after A2 (registered)
reg  [VA_W-1:0] va_q;
reg  [4:0] va_i;
reg        va_v = 1'b0;
always @(posedge clk) begin
    va_q <= a_n[VS_W-1:VW_W];
    va_i <= {r1_sid, r1_v};
    va_v <= r1_val && !rst_run;
end
wire [4:0] vsa_wa = rst_run ? rst_cnt : vs_stw ? st_addr[7:3] : va_i;
wire [4:0] vsw_wa = rst_run ? rst_cnt : vs_stw ? st_addr[7:3] : w_vi;
always @(posedge clk)
    if (rst_run || vs_stw || va_v)
        vsa[vsa_wa] <= rst_run ? VS_RESET[VS_W-1:VW_W] : vs_stw ? st_m[VS_W-1:VW_W] : va_q;
always @(posedge clk)
    if (rst_run || vs_stw || `QV(q8))
        vsw[vsw_wa] <= rst_run ? VS_RESET[VW_W-1:0] : vs_stw ? st_m[VW_W-1:0] : w_rec;
always @(posedge clk)
    if (rst_run || `QV(q8))
        lvl[rst_run ? rst_cnt : w_vi] <= rst_run ? 8'd0 : p8[P_CNT +: 8];
wire [2:0] rb_wa = rst_run ? rst_cnt[2:0] : `QSID(q8);
wire       rb_we = rst_run ? rst_cnt < 5'd8 : `QV(q8) && `QVO(q8) == 2'd2;
always @(posedge clk) if (rb_we) rbo[rb_wa] <= rst_run ? 8'd0 : p8[O_OSC3 +: 8];
always @(posedge clk) if (rb_we) rbe[rb_wa] <= rst_run ? 8'd0 : p8[P_ENV3 +: 8];

// ---- filter inputs: sum the voices of a SID as they leave D (p7)
wire [2:0]  g_sid = `QSID(q7);
wire [1:0]  g_v   = `QVO(q7);
wire [7:0]  g_rf  = r_rf[g_sid];
wire [7:0]  g_mv  = r_mv[g_sid];
wire [1:0]  g_dg  = g_sid[2] ? cfg_digi[3:2] : cfg_digi[1:0];
wire signed [22:0] g_dc = g_dg == 2'd0 ? 23'sd0 : $signed({1'b0, 22'd522240} >> (2'd3 - g_dg));
wire signed [22:0] g_x  = p7_vo + g_dc;
wire        g_filt = g_rf[g_v];
wire        g_off  = g_v == 2'd2 && g_mv[7];
reg  signed [22:0] g_vi, g_vnf;
wire signed [22:0] g_vi_n  = (g_v == 2'd0 ? 23'sd0 : g_vi) + (g_filt ? g_x : 23'sd0);
wire signed [22:0] g_vnf_n = (g_v == 2'd0 ? 23'sd0 : g_vnf) + (!g_filt && !g_off ? g_x : 23'sd0);
always @(posedge clk)
    if (`QV(q7)) begin
        g_vi <= g_vi_n;
        g_vnf <= g_vnf_n;
    end
wire        g_push = `QV(q7) && g_v == 2'd2;   // a SID's sums are complete

// ---- filter / mixer / bus value: a pipeline, one SID per push, F0..F8
// (every RAM read and every multiply has a register after it)
function signed [24:0] sat25(input signed [33:0] x);
    sat25 = x > 34'sh0ffffff ? 25'sh0ffffff : x < -34'sh1000000 ? -25'sh1000000 : x[24:0];
endfunction
// curve: address with the push, w0 with F0
wire [2:0]  c_sid = busy ? g_sid : st_addr[4:2];
wire [10:0] c_fc  = {r_fch[c_sid], r_fcl[c_sid]};
reg  [15:0] w0;
always @(posedge clk) w0 <= cv[{c_sid[2], c_fc[10:1]}];
// F0: SID and sums registered; its state read here
reg         f0_v = 1'b0;
reg  [2:0]  f0_sid;
reg  signed [22:0] f0_vi, f0_vnf;
always @(posedge clk) begin
    f0_v <= g_push && running && !rst_run;
    f0_sid <= g_sid;
    f0_vi <= g_vi_n;
    f0_vnf <= g_vnf_n;
end
wire [2:0]  h_ra   = busy ? f0_sid : st_addr[4:2];
wire [2:0]  b_ra   = busy ? f0_sid : st_addr[2:0];
wire [49:0] h_lp   = fs_lp[h_ra];
wire signed [24:0] h_vbp = h_lp[49:25];
wire signed [24:0] h_vlp = h_lp[24:0];
wire signed [24:0] h_vhp = fs_hp[h_ra];
wire [7:0]  h_rf   = r_rf[h_ra];
wire [7:0]  h_mv   = r_mv[h_ra];
wire [27:0] h_bs   = bs[b_ra];
wire        h_85   = cfg_8580[f0_sid[2]];
// bus value: the op, then one cycle of ageing
reg  [7:0]  b_val;
reg  [19:0] b_ttl;
always @(*) begin
    b_val = h_bs[7:0];
    b_ttl = h_bs[27:8];
    if (op_valid && op_sid == f0_sid) begin
        if (op_we) begin
            b_val = op_wdata;
            b_ttl = h_85 ? 20'ha2000 : 20'h01d00;
        end else if (op_reg >= 5'h19 && op_reg <= 5'h1c) begin
            b_val = op_rdata;
            b_ttl = h_85 ? 20'ha2000 : 20'h01d00;
        end else
            b_ttl = {1'b0, b_ttl[19:1]};
    end
    if (b_ttl != 20'd0) begin
        b_ttl = b_ttl - 20'd1;
        if (b_ttl == 20'd0) b_val = 8'd0;
    end
end
// stage registers: a name ending in k holds the SID that is in stage Fk
reg  [8:1]  fv = 8'd0;
reg  [2:0]  fs1, fs2, fs3, fs4, fs5, fs6, fs7, fs8;
reg  signed [22:0] fvi1, fvi2, fvi3, fvi4, fvi5;
reg  signed [22:0] fvnf1, fvnf2, fvnf3, fvnf4, fvnf5, fvnf6;
reg  [15:0] fw1;
reg  signed [24:0] fbp1, flp1, fhp1, fbp2, flp2;
reg  [7:0]  fmv1, fmv2, fmv3, fmv4, fmv5, fmv6, fmv7;
reg  [10:0] fq1, fq2, fq3;
reg  [27:0] fbs1, fbs2, fbs3, fbs4, fbs5, fbs6, fbs7, fbs8;
reg  signed [42:0] m1, m2, m3;
reg  signed [24:0] nhp5, nbp3, nlp3, nbp4, nlp4, nbp5, nlp5, nbp6, nlp6, nhp6, nbp7, nlp7, nhp7, nbp8, nlp8, nhp8;
reg  signed [27:0] mix6;
reg  signed [31:0] mv7;
reg  signed [19:0] out8;
wire signed [33:0] u_vhp = (m3 >>> 10) - nlp4 - fvi4;
wire signed [26:0] h_vf  = (fmv5[4] ? nlp5 : 25'sd0) + (fmv5[5] ? nbp5 : 25'sd0) + (fmv5[6] ? nhp5 : 25'sd0);
wire signed [24:0] out7  = mv7 >>> 7;
always @(posedge clk) begin
    fv <= {fv[7:1], f0_v};
    // F1: state, w0, 1/Q, bus value
    fs1 <= f0_sid; fvi1 <= f0_vi; fvnf1 <= f0_vnf; fw1 <= w0;
    fbp1 <= h_vbp; flp1 <= h_vlp; fhp1 <= h_vhp; fmv1 <= h_mv;
    fq1 <= divq(cfg_res_high[f0_sid[2]], h_rf[7:4]);
    fbs1 <= {b_ttl, b_val};
    // F2: w0 x Vhp, w0 x Vbp
    m1 <= fhp1 * $signed({2'd0, fw1});
    m2 <= fbp1 * $signed({2'd0, fw1});
    fs2 <= fs1; fvi2 <= fvi1; fvnf2 <= fvnf1; fbp2 <= fbp1; flp2 <= flp1; fmv2 <= fmv1; fq2 <= fq1; fbs2 <= fbs1;
    // F3: integrators
    nbp3 <= sat25(fbp2 - (m1 >>> 16));
    nlp3 <= sat25(flp2 - (m2 >>> 16));
    fs3 <= fs2; fvi3 <= fvi2; fvnf3 <= fvnf2; fmv3 <= fmv2; fq3 <= fq2; fbs3 <= fbs2;
    // F4: Vbp / Q
    m3 <= nbp3 * $signed({7'd0, fq3});
    nbp4 <= nbp3; nlp4 <= nlp3;
    fs4 <= fs3; fvi4 <= fvi3; fvnf4 <= fvnf3; fmv4 <= fmv3; fbs4 <= fbs3;
    // F5: Vhp
    nhp5 <= sat25(u_vhp);
    nbp5 <= nbp4; nlp5 <= nlp4;
    fs5 <= fs4; fvnf5 <= fvnf4; fmv5 <= fmv4; fbs5 <= fbs4;
    // F6: mix
    mix6 <= fvnf5 + h_vf;
    nbp6 <= nbp5; nlp6 <= nlp5; nhp6 <= nhp5;
    fs6 <= fs5; fmv6 <= fmv5; fbs6 <= fbs5;
    // F7: volume
    mv7 <= mix6 * $signed({1'b0, fmv6[3:0]});
    nbp7 <= nbp6; nlp7 <= nlp6; nhp7 <= nhp6;
    fs7 <= fs6; fbs7 <= fbs6;
    // F8: saturate; state written back from here
    out8 <= out7 > 25'sh7ffff ? 20'sh7ffff : out7 < -25'sh80000 ? -20'sh80000 : out7[19:0];
    nbp8 <= nbp7; nlp8 <= nlp7; nhp8 <= nhp7;
    fs8 <= fs7; fbs8 <= fbs7;
    if (rst_run) fv <= 8'd0;
end
// filter state, bus value: one write port each (reset, state port, F8)
wire       h_wb   = fv[8];
wire       fs_stw = st_w && st_addr[8:5] == 4'b1100 && st_addr[1:0] != 2'd0;
wire       bs_stw = st_w && st_addr[8:3] == 6'b110100;
wire       fs_rst = rst_run && rst_cnt < 5'd8;
wire [2:0] fs_wa  = rst_run ? rst_cnt[2:0] : fs_stw ? st_addr[4:2] : fs8;
wire [2:0] bs_wa  = rst_run ? rst_cnt[2:0] : bs_stw ? st_addr[2:0] : fs8;
always @(posedge clk)
    if (fs_rst || (fs_stw && st_addr[1:0] != 2'd3) || h_wb)
        fs_lp[fs_wa] <= fs_rst ? 50'd0 : !fs_stw ? {nbp8, nlp8} :
                        st_addr[1:0] == 2'd1 ? {st_wdata[24:0], h_lp[24:0]} : {h_lp[49:25], st_wdata[24:0]};
always @(posedge clk)
    if (fs_rst || (fs_stw && st_addr[1:0] == 2'd3) || h_wb)
        fs_hp[fs_wa] <= fs_rst ? 25'd0 : fs_stw ? st_wdata[24:0] : nhp8;
always @(posedge clk)
    if (fs_rst || bs_stw || h_wb)
        bs[bs_wa] <= fs_rst ? 28'd0 : bs_stw ? st_wdata[27:0] : fbs8;
always @(posedge clk)
    if (fs_rst || h_wb)
        rbb[fs_wa] <= fs_rst ? 8'd0 : fbs8[7:0];

// samples and the two UltiSID sums
reg  signed [20:0] acc_u;
wire signed [20:0] acc_n = (fs8[1:0] == 2'd0 ? 21'sd0 : acc_u) + out8;
wire signed [19:0] acc_s = acc_n > 21'sh7ffff ? 20'sh7ffff : acc_n < -21'sh80000 ? -20'sh80000 : acc_n[19:0];
always @(posedge clk) begin
    smp_valid <= h_wb;
    smp_sid <= fs8;
    smp_data <= out8;
    sum_valid <= 1'b0;
    if (h_wb) begin
        acc_u <= acc_n;
        if (fs8[1:0] == 2'd3) begin
            if (fs8[2]) begin
                sum_u1 <= acc_s;
                sum_valid <= 1'b1;
            end else
                sum_u0 <= acc_s;
        end
    end
end

`undef QV
`undef QSID
`undef QVO
`undef QCTL
`undef QPW
`undef Q85
endmodule
