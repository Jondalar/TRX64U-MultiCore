// T175: the generic timed probe engine of the Port Diagnostics core. It runs a step list the application loads (what the steps mean is a TOML test, never this file) and drives /
// observes the joystick and paddle pins on the host's cpu_clk. Written for this project (TREX). Nothing is evaluated here: the application reads raw words and judges them.
//
// Step word: [31:28] op, [27:24] cmp select (TTF only), [23:0] arg.   One clock per fetch, one per execution, so an op costs 2 clocks, plus its own time:
//   0 END        stop, release every output, done
//   1 DRIVE      arg[9:0] pins named, arg[19:10] level of each named pin: 0 = drive low (open drain), 1 = release. Pins not named keep their state. Never drives high.
//   2 WAIT       arg clocks, exactly: the op takes 2 + arg clocks from fetch to the next fetch
//   3 SAMPLE     push {t[15:0], pins[9:0], cmp[3:0], flags[1:0]}  (flags b0 = result FIFO lost a word before, b1 = t wrapped since the timebase started)
//   4 EDGES      arg clocks: count the transitions of the pins in MASK (one per pin and clock), push the count
//   5 TTF        time to flip: from the next clock on, until a pin of {cmp select if non-zero, else MASK} differs from its value at the op, or arg clocks have passed;
//                push {timeout, 7'b0, clocks[23:0]}; clocks = 0 means the flip was seen on the first clock after the op
//   6 PADCTL     arg b0 PADDLE_DIS level, b1 its enable, b2 PADDLE_ADJ level, b3 its enable
//   7 LOOP       arg[7:0] K steps (the K ops after this one), arg[23:8] C repeats; one level, K and C non-zero, the body ends before step 256
//   8 WAIT_USER  halt (status waiting) until GO; the timebase restarts at GO
//   9 MASK       arg[13:0] observation mask over {cmp[3:0], joy2[4:0], joy1[4:0]} for EDGES and TTF (reset value 14'h3FFF at RUN)
//   other        step error (status err), stop
// t counts clocks from RUN (and from each GO), 32 bit, only while the engine runs and does not wait for the user.
// The pins are synchronised with two flops; every observation is of the synchronised value. Safety (spec D7): only the pins a DRIVE names are driven, and
// END, ABORT, error and reset release every output.
`timescale 1ns / 1ps
module probe (
    input  wire        clk,
    input  wire        rst,
    input  wire        run,                 // pulses
    input  wire        abort,
    input  wire        go,
    input  wire        fifo_clr,
    input  wire        st_we,               // step RAM write port
    input  wire [7:0]  st_addr,
    input  wire [31:0] st_wdata,
    input  wire [13:0] pins_async,          // {cmp[3:0], joy2[4:0], joy1[4:0]}, high = pin high
    output reg  [9:0]  drv_low,             // 1 = pin driven low
    output reg         pad_dis_o, pad_dis_en, pad_adj_o, pad_adj_en,
    input  wire        fifo_rd,             // pops the head word (fifo_q is read before the pulse)
    output wire [31:0] fifo_q,
    output wire [9:0]  fifo_level,
    output wire [13:0] live,
    output reg         running, waiting, done, ovf, err,
    output wire [7:0]  pc_o,
    output reg         ev                   // pulses when the engine stops or starts waiting for the user
);
    localparam [3:0] F_FETCH = 4'd0, F_EXEC = 4'd1, F_WAIT = 4'd2, F_EDGE = 4'd3, F_TTF = 4'd4, F_USER = 4'd5, F_IDLE = 4'd6;

    // ---- pin synchroniser
    (* ASYNC_REG = "TRUE" *) reg [13:0] s1 = 14'h3FFF, s2 = 14'h3FFF;
    reg [13:0] prev = 14'h3FFF;
    always @(posedge clk) begin
        s1 <= pins_async; s2 <= s1; prev <= s2;
    end
    assign live = s2;

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
    reg        lost = 1'b0;                 // a word was dropped since the last CLEAR
    always @(posedge clk) begin
        if (push && !full) begin fmem[wp[8:0]] <= pdata; end
        fq <= fmem[rp[8:0]];
    end

    // ---- engine
    reg [3:0]  st = F_IDLE;
    reg [31:0] t = 32'd0;
    reg        twrap = 1'b0;
    reg [23:0] cnt = 24'd0, elapsed = 24'd0, tmo = 24'd0;
    reg [31:0] edges = 32'd0;
    reg [13:0] mask = 14'h3FFF, rv = 14'd0, selm = 14'd0;
    reg        lp_act = 1'b0;
    reg [7:0]  lp_start = 8'd0, lp_end = 8'd0;
    reg [15:0] lp_cnt = 16'd0;
    wire [3:0] op = ir[31:28];
    wire [23:0] arg = ir[23:0];
    wire [13:0] chg = live ^ prev;
    wire [13:0] chgm = chg & mask;
    wire [3:0] nchg = chgm[0] + chgm[1] + chgm[2] + chgm[3] + chgm[4] + chgm[5] + chgm[6] + chgm[7] + chgm[8] + chgm[9] + chgm[10] + chgm[11] + chgm[12] + chgm[13];
    wire [8:0] lp_end_new = {1'b0, pc} + {1'b0, arg[7:0]};

    task finish(input is_err);
        begin
            drv_low <= 10'd0; pad_dis_en <= 1'b0; pad_adj_en <= 1'b0; pad_dis_o <= 1'b0; pad_adj_o <= 1'b0;
            running <= 1'b0; waiting <= 1'b0; done <= 1'b1; st <= F_IDLE; lp_act <= 1'b0; ev <= 1'b1;
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

    initial begin
        drv_low = 10'd0; pad_dis_o = 1'b0; pad_dis_en = 1'b0; pad_adj_o = 1'b0; pad_adj_en = 1'b0;
        running = 1'b0; waiting = 1'b0; done = 1'b0; ovf = 1'b0; err = 1'b0; ev = 1'b0;
    end

    always @(posedge clk) begin
        push <= 1'b0; ev <= 1'b0;
        // result FIFO bookkeeping (the write itself is in the block RAM process above)
        if (fifo_clr) begin wp <= 10'd0; rp <= 10'd0; ovf <= 1'b0; lost <= 1'b0; end
        else begin
            if (push) begin if (full) begin ovf <= 1'b1; lost <= 1'b1; end else wp <= wp + 10'd1; end
            if (fifo_rd && fifo_level != 10'd0) rp <= rp + 10'd1;
        end
        if (running && !waiting) begin
            t <= t + 32'd1;
            if (&t[15:0]) twrap <= 1'b1;
        end

        if (rst) begin
            drv_low <= 10'd0; pad_dis_en <= 1'b0; pad_adj_en <= 1'b0; pad_dis_o <= 1'b0; pad_adj_o <= 1'b0;
            running <= 1'b0; waiting <= 1'b0; st <= F_IDLE; lp_act <= 1'b0; push <= 1'b0;
        end else if (abort && running) begin
            finish(1'b0);
        end else begin
            case (st)
            F_IDLE: if (run) begin
                pc <= 8'd0; t <= 32'd0; twrap <= 1'b0; done <= 1'b0; err <= 1'b0; running <= 1'b1; waiting <= 1'b0;
                mask <= 14'h3FFF; lp_act <= 1'b0; drv_low <= 10'd0; st <= F_FETCH;
            end
            F_FETCH: st <= F_EXEC;                  // ir <= ram[pc] happens in this clock
            F_EXEC: case (op)
                4'd0: finish(1'b0);
                4'd1: begin drv_low <= (drv_low & ~arg[9:0]) | (arg[9:0] & ~arg[19:10]); advance; end
                4'd2: if (arg == 24'd0) advance; else begin cnt <= arg; st <= F_WAIT; end
                4'd3: begin put({t[15:0], live[9:0], live[13:10], twrap, lost}); advance; end
                4'd4: if (arg == 24'd0) begin put(32'd0); advance; end else begin cnt <= arg; edges <= 32'd0; st <= F_EDGE; end
                4'd5: begin
                    rv <= live; tmo <= arg; elapsed <= 24'd0;
                    selm <= (ir[27:24] != 4'd0) ? {ir[27:24], 10'd0} : mask;
                    st <= F_TTF;
                end
                4'd6: begin
                    pad_dis_o <= arg[0]; pad_dis_en <= arg[1]; pad_adj_o <= arg[2]; pad_adj_en <= arg[3]; advance;
                end
                4'd7: begin
                    if (lp_act || arg[7:0] == 8'd0 || arg[23:8] == 16'd0 || lp_end_new[8]) finish(1'b1);
                    else begin
                        lp_act <= 1'b1; lp_start <= pc + 8'd1; lp_end <= lp_end_new[7:0]; lp_cnt <= arg[23:8];
                        pc <= pc + 8'd1; st <= F_FETCH;
                    end
                end
                4'd8: begin waiting <= 1'b1; st <= F_USER; ev <= 1'b1; end
                4'd9: begin mask <= arg[13:0]; advance; end
                default: finish(1'b1);
            endcase
            F_WAIT: begin cnt <= cnt - 24'd1; if (cnt == 24'd1) advance; end
            F_EDGE: begin
                edges <= edges + nchg;
                cnt <= cnt - 24'd1;
                if (cnt == 24'd1) begin put(edges + nchg); advance; end
            end
            F_TTF: begin
                if ((live ^ rv) & selm) begin put({1'b0, 7'd0, elapsed}); advance; end
                else if (elapsed == tmo) begin put({1'b1, 7'd0, tmo}); advance; end
                else elapsed <= elapsed + 24'd1;
            end
            F_USER: if (go) begin waiting <= 1'b0; t <= 32'd0; twrap <= 1'b0; advance; end
            default: st <= F_IDLE;
            endcase
        end
    end
endmodule
