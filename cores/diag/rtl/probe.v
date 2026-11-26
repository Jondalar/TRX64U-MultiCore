// T175 / T260: the generic timed probe engine (v2) of the diag core. It runs a step list the application loads (what the steps mean is a TOML test, never this file) and drives /
// observes any pin of the bank (pins.toml, 121 pins) on the host's cpu_clk. Written for this project (TREX). Nothing is evaluated here: the application reads raw words and judges them.
// The register map and the step encoding are in docs/specs/T260-diag-all-pins-tests-loaded.md section 5 ("Register map v2"); this file is what that table says.
//
// Step word: [31:28] op. One clock per fetch, one per execution: an op costs 2 clocks plus its own time, a second word 1 clock more.
//   0 END          stop, release every output, done
//   1 DRIVE        [27:26] mode (0 release, 1 low, 2 high, 3 hiz), [6:0] pin index. Refused (skipped, flagged) for a pin past the bank, not drivable, a mode its class does not allow, a guarded pin while
//                  SLOT_GAMEn or SLOT_EXROMn is low. The output changes 2 clocks after the execute clock and shows in the snapshot 2 clocks after that.
//   2 WAIT         [23:0] clocks, exactly: the op takes 2 + N clocks from fetch to the next fetch
//   3 SAMPLE       push t (32 bit) and the 128 bit snapshot as four words (pins 31..0 first)
//   4 EDGES        [27:26] edge (0 any, 1 rising, 2 falling), [6:0] pin; word 1 = N clocks. Pushes the number of those edges of the pin in the N clocks after word 1 executes
//                  (a pin past the bank: refused at word 1, pushes 0xFFFFFFFF)
//   5 TIME_TO_EDGE as EDGES; word 1 = timeout (31 bit). From the clock after word 1 on, until the pin shows the edge or the timeout passed; pushes {timeout, clocks[30:0]}
//   7 LOOP         [7:0] K following words, [23:8] C repeats; one level, K and C non-zero, the body ends before word 256
//   8 WAIT_USER    halt (status waiting) until GO; the time base restarts at GO
//   9 WRITE_REG    [27:16] register offset (0x050-0x06F), [7:0] data: a write on the register block's own bus
//   10 CAPTURE     [23:0] K clocks per sample (>= 1); word 1 = M samples. Starts the capture in the background (diag_cap.v); refused when it cannot start
//   11 CAPWAIT     wait until no capture is sampling
//   other          step error (status err), stop
// t counts clocks from RUN (and from each GO), 32 bit, only while the engine runs and does not wait for the user.
// The pins are synchronised with two flops; every observation is of the synchronised value. Safety (spec D4): an output is driven only by a DRIVE the table allows, a guarded one only
// while the cartridge port shows no cartridge, and END, ABORT, a step error, the decouple and reset release every output.
`timescale 1ns / 1ps
module probe (
    input  wire         clk,
    input  wire         rst,
    input  wire         run,                // pulses
    input  wire         abort,
    input  wire         go,
    input  wire         fifo_clr,
    input  wire         dc,                 // core_decouple in cpu_clk: release everything and stop
    input  wire         st_we,              // step RAM write port
    input  wire [7:0]   st_addr,
    input  wire [31:0]  st_wdata,
    input  wire [127:0] pins_async,         // the bank, index = pin index (the ball for an input, the drive for an output)
    output wire [127:0] dg_en,              // the engine drives / overrides this pin
    output wire [127:0] dg_o,               // its level (for a `pull` pin 1 = pulling the line low)
    output wire [127:0] dg_z,               // a `pzh` pin is released to high-Z
    input  wire         fifo_rd,            // pops the head word (fifo_q is read before the pulse)
    output wire [31:0]  fifo_q,
    output wire [9:0]   fifo_level,
    output wire [127:0] live,
    output reg          running, waiting, done, ovf, err,
    output reg          refused, guard_trip,
    output wire         guard_now,
    output reg  [7:0]   ref_pc, ref_cnt,
    output wire [7:0]   pc_o,
    output reg          ev,                 // pulses when the engine stops or starts waiting for the user
    // WRITE_REG: held until wack
    output reg          reg_we,
    output reg  [11:0]  reg_addr,
    output reg  [7:0]   reg_wdata,
    input  wire         reg_wack,
    // capture control (diag_cap.v)
    output reg          cap_go,
    output reg  [23:0]  cap_k,
    output reg  [31:0]  cap_m,
    input  wire         cap_refused,        // valid in the clock after the one in which cap_go is high
    input  wire         cap_sampling,
    output wire         cap_stop            // abort, decouple, reset
);
    `include "diag_pintab.vh"
    localparam [3:0] F_FETCH = 4'd0, F_EXEC = 4'd1, F_WAIT = 4'd2, F_EDGE = 4'd3, F_TTE = 4'd4, F_USER = 4'd5, F_IDLE = 4'd6, F_SMP = 4'd7, F_REGW = 4'd8, F_CAPC = 4'd9, F_CAPW = 4'd10;

    // ---- pin synchroniser
    (* ASYNC_REG = "TRUE" *) reg [127:0] s1 = {128{1'b1}}, s2 = {128{1'b1}};
    always @(posedge clk) begin s1 <= pins_async; s2 <= s1; end
    assign live = s2;

    // the cartridge guard (D4): a cartridge pulls GAME or EXROM low
    assign guard_now = ~s2[IX_SLOT_GAMEn] | ~s2[IX_SLOT_EXROMn];

    // ---- step RAM (one block RAM), synchronous read at pc
    reg [31:0] ram [0:255];
    reg [31:0] ir = 32'd0;
    reg [7:0]  pc = 8'd0;
    assign pc_o = pc;
    always @(posedge clk) begin
        if (st_we) ram[st_addr] <= st_wdata;
        ir <= ram[pc];
    end

    // ---- result FIFO (one block RAM)
    reg [31:0] fmem [0:511];
    reg [9:0]  wp = 10'd0, rp = 10'd0;
    reg [31:0] fq = 32'd0;
    assign fifo_level = wp - rp;
    assign fifo_q = fq;
    wire full = (fifo_level == 10'd512);
    reg        push = 1'b0;
    reg [31:0] pdata = 32'd0;
    always @(posedge clk) begin
        if (push && !full) begin fmem[wp[8:0]] <= pdata; end
        fq <= fmem[rp[8:0]];
    end

    // ---- the output state of every drivable pin (generated per bit: a pin that is not drivable has no flops)
    reg        dv_upd = 1'b0;               // a DRIVE to apply (the clock after its execute clock)
    reg [6:0]  dv_idx = 7'd0;
    reg [1:0]  dv_mode = 2'd0;
    reg        rel = 1'b0;                  // release everything (END, ABORT, error)
    wire       rel_now = rst | dc | rel;
    wire [127:0] en_raw, gmask;
    genvar g;
    generate for (g = 0; g < 128; g = g + 1) begin : pinbit
        localparam [3:0] C = dgpin_cls(g);
        assign gmask[g] = C[3];
        if (C[2:0] != 3'd0) begin : drv
            reg e = 1'b0, v = 1'b0, z = 1'b0;
            wire hit = dv_upd && dv_idx == g;
            always @(posedge clk) begin
                if (rel_now || (C[3] && guard_now)) begin e <= 1'b0; v <= 1'b0; z <= 1'b0; end   // a guarded pin is let go for good, not parked
                else if (hit) begin
                    e <= (dv_mode == 2'd1) || (dv_mode == 2'd2);
                    v <= (dv_mode == 2'd2) || (dv_mode == 2'd1 && C[2:0] == 3'd2);
                    z <= (dv_mode == 2'd3);
                end
            end
            assign en_raw[g] = e;
            assign dg_en[g]  = e & ~(C[3] & guard_now);
            assign dg_o[g]   = v;
            assign dg_z[g]   = z & ~(C[3] & guard_now);
        end else begin : fixed
            assign en_raw[g] = 1'b0;
            assign dg_en[g]  = 1'b0;
            assign dg_o[g]   = 1'b0;
            assign dg_z[g]   = 1'b0;
        end
    end endgenerate

    // ---- engine
    reg [3:0]  st = F_IDLE;
    reg [31:0] t = 32'd0;
    reg [31:0] cnt = 32'd0, elapsed = 32'd0, tmo = 32'd0, edges = 32'd0;
    reg        lp_act = 1'b0;
    reg [7:0]  lp_start = 8'd0, lp_end = 8'd0;
    reg [15:0] lp_cnt = 16'd0;
    reg [3:0]  pend = 4'd0;                 // the op whose second word is next
    reg [6:0]  sel_pin = 7'd0;
    reg [1:0]  sel_edge = 2'd0;
    reg        selb = 1'b0, sb_prev = 1'b0;
    reg [127:0] snap = 128'd0;
    reg [31:0] ts = 32'd0;
    reg [2:0]  smp_i = 3'd0;
    reg [1:0]  cap_w = 2'd0;
    reg        bad = 1'b0;
    wire [3:0] op = ir[31:28];
    wire [23:0] arg = ir[23:0];
    wire [8:0] lp_end_new = {1'b0, pc} + {1'b0, arg[7:0]};
    wire       cur_edge = (sel_edge == 2'd0) ? (selb != sb_prev) : (sel_edge == 2'd1) ? (selb && !sb_prev) : (sel_edge == 2'd2) ? (!selb && sb_prev) : 1'b0;

    // DRIVE: is it allowed? (class and guard from the table)
    wire [6:0] d_idx = ir[6:0];
    wire [1:0] d_mode = ir[27:26];
    wire [3:0] d_c = dgpin_cls(d_idx);
    wire       d_valid = d_idx < DG_N;
    wire       d_mode_ok = (d_mode == 2'd0) ||
                           (d_mode == 2'd1 && d_c[2:0] != 3'd0) ||
                           (d_mode == 2'd2 && (d_c[2:0] == 3'd3 || d_c[2:0] == 3'd4 || d_c[2:0] == 3'd5)) ||
                           (d_mode == 2'd3 && d_c[2:0] == 3'd5);
    wire       d_ok = d_valid && (d_mode == 2'd0 || (d_mode_ok && !(d_c[3] && guard_now)));

    assign cap_stop = abort | dc | rst;

    task finish(input is_err);
        begin
            rel <= 1'b1;
            running <= 1'b0; waiting <= 1'b0; done <= 1'b1; st <= F_IDLE; lp_act <= 1'b0; ev <= 1'b1; pend <= 4'd0;
            if (is_err) err <= 1'b1;
        end
    endtask
    task advance;                           // the end of an op: the next step, or the loop's turn
        begin
            st <= F_FETCH;
            if (lp_act && pc == lp_end) begin
                if (lp_cnt > 16'd1) begin lp_cnt <= lp_cnt - 16'd1; pc <= lp_start; end
                else begin lp_act <= 1'b0; pc <= pc + 8'd1; end
            end else pc <= pc + 8'd1;
        end
    endtask
    task put(input [31:0] w);
        begin push <= 1'b1; pdata <= w; end
    endtask
    task refuse;
        begin
            refused <= 1'b1;
            if (ref_cnt == 8'd0) ref_pc <= pc;
            if (ref_cnt != 8'hFF) ref_cnt <= ref_cnt + 8'd1;
        end
    endtask

    initial begin
        running = 1'b0; waiting = 1'b0; done = 1'b0; ovf = 1'b0; err = 1'b0; ev = 1'b0; refused = 1'b0; guard_trip = 1'b0; ref_pc = 8'd0; ref_cnt = 8'd0;
        reg_we = 1'b0; reg_addr = 12'd0; reg_wdata = 8'd0; cap_go = 1'b0; cap_k = 24'd0; cap_m = 32'd0;
    end

    always @(posedge clk) begin
        push <= 1'b0; ev <= 1'b0; rel <= 1'b0; dv_upd <= 1'b0; cap_go <= 1'b0;
        selb <= live[sel_pin];
        // result FIFO bookkeeping (the write itself is in the block RAM process above)
        if (fifo_clr) begin wp <= 10'd0; rp <= 10'd0; ovf <= 1'b0; end
        else begin
            if (push) begin if (full) ovf <= 1'b1; else wp <= wp + 10'd1; end
            if (fifo_rd && fifo_level != 10'd0) rp <= rp + 10'd1;
        end
        if (running && !waiting) t <= t + 32'd1;
        if (guard_now && |(en_raw & gmask)) guard_trip <= 1'b1;

        if (rst) begin
            running <= 1'b0; waiting <= 1'b0; st <= F_IDLE; lp_act <= 1'b0; push <= 1'b0; reg_we <= 1'b0; pend <= 4'd0;
        end else if ((abort || dc) && running) begin
            finish(1'b0);
            reg_we <= 1'b0;
        end else begin
            case (st)
            F_IDLE: if (run) begin
                pc <= 8'd0; t <= 32'd0; done <= 1'b0; err <= 1'b0; refused <= 1'b0; guard_trip <= 1'b0; ref_pc <= 8'd0; ref_cnt <= 8'd0;
                running <= 1'b1; waiting <= 1'b0; lp_act <= 1'b0; pend <= 4'd0; st <= F_FETCH;
            end
            F_FETCH: st <= F_EXEC;                  // ir <= ram[pc] happens in this clock
            F_EXEC: if (pend != 4'd0) begin         // the second word of EDGES / TIME_TO_EDGE / CAPTURE
                pend <= 4'd0;
                case (pend)
                4'd4: if (bad) begin refuse; put(32'hFFFFFFFF); advance; end
                      else if (ir == 32'd0) begin put(32'd0); advance; end
                      else begin cnt <= ir; edges <= 32'd0; sb_prev <= selb; st <= F_EDGE; end
                4'd5: if (bad) begin refuse; put(32'hFFFFFFFF); advance; end
                      else begin tmo <= {1'b0, ir[30:0]}; elapsed <= 32'd0; sb_prev <= selb; st <= F_TTE; end
                default: begin                      // CAPTURE
                    cap_m <= ir; cap_go <= 1'b1; cap_w <= 2'd0; st <= F_CAPC;
                end
                endcase
            end else case (op)
                4'd0: finish(1'b0);
                4'd1: begin
                    if (d_ok) begin dv_upd <= 1'b1; dv_idx <= d_idx; dv_mode <= d_mode; end
                    else refuse;
                    advance;
                end
                4'd2: if (arg == 24'd0) advance; else begin cnt <= {8'd0, arg}; st <= F_WAIT; end
                4'd3: begin snap <= live; ts <= t; smp_i <= 3'd0; st <= F_SMP; end
                4'd4, 4'd5, 4'd10: begin
                    sel_pin <= ir[6:0]; sel_edge <= ir[27:26]; cap_k <= ir[23:0]; bad <= (op != 4'd10) && (ir[6:0] >= DG_N);
                    pend <= op; pc <= pc + 8'd1; st <= F_FETCH;
                end
                4'd7: begin
                    if (lp_act || arg[7:0] == 8'd0 || arg[23:8] == 16'd0 || lp_end_new[8]) finish(1'b1);
                    else begin
                        lp_act <= 1'b1; lp_start <= pc + 8'd1; lp_end <= lp_end_new[7:0]; lp_cnt <= arg[23:8];
                        pc <= pc + 8'd1; st <= F_FETCH;
                    end
                end
                4'd8: begin waiting <= 1'b1; st <= F_USER; ev <= 1'b1; end
                4'd9: begin
                    if (ir[27:16] < 12'h050 || ir[27:16] > 12'h06F) finish(1'b1);
                    else begin reg_we <= 1'b1; reg_addr <= ir[27:16]; reg_wdata <= ir[7:0]; st <= F_REGW; end
                end
                4'd11: begin st <= F_CAPW; end
                default: finish(1'b1);
            endcase
            F_WAIT: begin cnt <= cnt - 32'd1; if (cnt == 32'd1) advance; end
            F_EDGE: begin
                sb_prev <= selb;
                cnt <= cnt - 32'd1;
                if (cur_edge) edges <= edges + 32'd1;
                if (cnt == 32'd1) begin put(edges + (cur_edge ? 32'd1 : 32'd0)); advance; end
            end
            F_TTE: begin
                sb_prev <= selb;
                if (cur_edge) begin put({1'b0, elapsed[30:0]}); advance; end
                else if (elapsed == tmo) begin put({1'b1, tmo[30:0]}); advance; end
                else elapsed <= elapsed + 32'd1;
            end
            F_SMP: begin                            // t, then the snapshot words 0..3, one a clock
                case (smp_i)
                3'd0: put(ts);
                3'd1: put(snap[31:0]);
                3'd2: put(snap[63:32]);
                3'd3: put(snap[95:64]);
                default: put(snap[127:96]);
                endcase
                smp_i <= smp_i + 3'd1;
                if (smp_i == 3'd4) advance;
            end
            F_REGW: if (reg_wack) begin reg_we <= 1'b0; advance; end
            F_CAPC: begin                           // cap_refused is valid in the second clock after cap_go
                cap_w <= cap_w + 2'd1;
                if (cap_w == 2'd2) begin if (cap_refused) refuse; advance; end
            end
            F_CAPW: if (!cap_sampling) advance;
            F_USER: if (go) begin waiting <= 1'b0; t <= 32'd0; advance; end
            default: st <= F_IDLE;
            endcase
        end
    end
endmodule
