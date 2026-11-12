// ddr2_tester: memory test of the whole DDR2 through the app_* port, rounds without end.
//
// One round, over all 2^AW bursts (AW = 23: 128 MB), in sequential or LFSR order (every address
// exactly once; the LFSR visits all non-zero states and address 0 comes last):
//   pass 0  write  P1(a, round), no mask
//   pass 1  write  P2(a, round) with a write mask M(a, round) (random bytes; every 16th address
//                  all bytes masked, the next one none)
//   hold    `hold` clocks of nothing (retention under refresh), then
//   pass 2  read   and compare with the merge: bytes of P1 where M, of P2 where not
//   pass 3  (pingpong on) per address: write P3 (no mask), read it back at once, compare -
//                  turn-around write->read->write on the same and the next bank
// Patterns are hashes of the address and the round, so nothing is stored; reads go to a FIFO of
// addresses and are checked in order when their data returns. Counters: rounds, error words (a
// 128-bit word that differs), commands; the first error keeps address, round, pass, expected and
// actual data.
module ddr2_tester #(
    parameter AW = 23
) (
    input  wire         clk,
    input  wire         rst,
    input  wire         run,                // run while high (pass boundaries are the stops)
    input  wire [3:0]   ctl,                // [0] LFSR order, [1] toggle the order every round, [2] pingpong, [3] masks
    input  wire         clear,              // one clk: clear the counters and the first error
    input  wire [31:0]  hold,

    output reg  [26:0]  app_addr = 27'd0,
    output reg  [2:0]   app_cmd = 3'd0,
    output reg          app_en = 1'b0,
    input  wire         app_rdy,
    output reg  [127:0] app_wdf_data = 128'd0,
    output reg  [15:0]  app_wdf_mask = 16'd0,
    output reg          app_wdf_wren = 1'b0,
    input  wire         app_wdf_rdy,
    input  wire [127:0] app_rd_data,
    input  wire         app_rd_valid,

    output reg  [31:0]  rounds = 32'd0,
    output reg  [31:0]  errors = 32'd0,
    output reg  [31:0]  cmds = 32'd0,
    output reg          first_v = 1'b0,
    output reg  [AW-1:0] first_addr = {AW{1'b0}},
    output reg  [127:0] first_exp = 128'd0,
    output reg  [127:0] first_act = 128'd0,
    output reg  [31:0]  first_round = 32'd0,
    output reg  [3:0]   first_pass = 4'd0,
    output wire [3:0]   pass_o,
    output wire         order_o
);
    localparam [AW:0] TOTAL = {1'b1, {AW{1'b0}}};

    // ---- patterns -------------------------------------------------------------------
    function [31:0] mix(input [31:0] x);
        reg [31:0] y;
        begin
            y = x ^ (x << 13); y = y ^ (y >> 17); y = y ^ (y << 5);
            y = y + 32'h6D2B79F5;
            y = y ^ (y << 13); y = y ^ (y >> 17); y = y ^ (y << 5);
            mix = y;
        end
    endfunction
    // rk1 = round * 32'h85EBCA6B, rk2 = round * 32'hC2B2AE3D: registers below, one multiplier each
    function [127:0] pat(input [AW-1:0] a, input [31:0] rk1, input [1:0] sel);
        reg [31:0] s;
        begin
            s = {{(32-AW){1'b0}}, a} ^ rk1 ^ {sel, 30'd0} ^ ((sel == 2'd0) ? 32'd0 : (sel == 2'd1) ? 32'h27D4EB2F : (sel == 2'd2) ? 32'h4FA9D65E : 32'h777EC18D);
            pat = {mix(s + 32'hC2B2AE35), mix(s + 32'h165667B1), mix(s + 32'h9E3779B9), mix(s)};
        end
    endfunction
    function [15:0] msk(input [AW-1:0] a, input [31:0] rk2, input on);
        reg [31:0] h;
        begin
            h = mix({{(32-AW){1'b0}}, a} ^ rk2 ^ 32'h5BD1E995);
            msk = !on ? 16'h0000 : (a[3:0] == 4'd0) ? 16'hFFFF : (a[3:0] == 4'd1) ? 16'h0000 : (h[15:0] & h[31:16]);
        end
    endfunction
    function [127:0] merge(input [127:0] p1, input [127:0] p2, input [15:0] m);
        integer k;
        begin
            for (k = 0; k < 16; k = k + 1) merge[8 * k +: 8] = m[k] ? p1[8 * k +: 8] : p2[8 * k +: 8];
        end
    endfunction
    function [AW-1:0] lfsr_next(input [AW-1:0] x);
        reg fb;
        begin
            case (AW)
            10: fb = x[9] ^ x[6];
            12: fb = x[11] ^ x[5] ^ x[3] ^ x[0];
            16: fb = x[15] ^ x[14] ^ x[12] ^ x[3];
            20: fb = x[19] ^ x[16];
            23: fb = x[22] ^ x[17];
            default: fb = x[AW-1] ^ x[AW-2];             // not maximal: only the listed widths are tested
            endcase
            lfsr_next = {x[AW-2:0], fb};
        end
    endfunction

    // ---- sequencer --------------------------------------------------------------------
    localparam T_IDLE = 4'd0, T_INIT = 4'd1, T_GEN = 4'd2, T_PRES = 4'd3, T_NEXT = 4'd4, T_WAITRD = 4'd5,
               T_HOLD = 4'd6, T_PPR = 4'd7, T_ENDPASS = 4'd8;
    reg [3:0]  st = T_IDLE;
    reg [2:0]  pass = 3'd0;                       // 0 W1, 1 W2, 2 R, 3 pingpong
    reg [31:0] round = 32'd0;
    reg        ord = 1'b0;                        // this round's order: 1 = LFSR
    reg [AW-1:0] a = {AW{1'b0}};
    reg [AW:0] n = {(AW+1){1'b0}};                // commands of this pass issued
    reg        pp_rd = 1'b0;                      // pingpong: the read half
    reg [31:0] hold_cnt = 32'd0;
    reg [AW:0] ret_cnt = {(AW+1){1'b0}};
    wire ord_new = (pass == 3'd0) ? (ctl[1] ? (round[0] ^ ctl[0]) : ctl[0]) : ord;
    wire [31:0] seed_h = mix({round[28:0], pass} ^ 32'hA5F00F5A);
    wire [AW-1:0] seed_w = {seed_h[AW-1:1], 1'b1};
    assign pass_o  = {1'b0, pass};
    assign order_o = ord;

    // reads in flight: addresses in order
    reg [AW-1:0] fifo [0:31];
    reg [4:0]  f_w = 5'd0, f_r = 5'd0;
    reg [5:0]  inflight = 6'd0;
    wire       rd_go = app_en && app_rdy && app_cmd[0];
    wire       rd_pop = app_rd_valid;

    reg [31:0] rk1 = 32'd0, rk2 = 32'd0;           // the round's constants, from T_INIT on (a pass has them for good)

    // comparison pipeline: data and its address, then the expected word, then the verdict
    reg        v1 = 1'b0, v2 = 1'b0;
    reg [127:0] act1, act2, exp2;
    reg [AW-1:0] ra1, ra2;
    reg [2:0]  pass1;
    reg [3:0]  masks1;
    wire [127:0] e_p1 = pat(ra1, rk1, (pass1 == 3'd3) ? 2'd3 : 2'd0);   // P1, or P3 in the pingpong pass
    wire [127:0] e_p2 = pat(ra1, rk1, 2'd1);
    wire [15:0]  e_m  = msk(ra1, rk2, masks1[3]);

    reg  [3:0] ctl_r = 4'd0;
    wire c_done = !app_en || app_rdy;
    wire d_done = !app_wdf_wren || app_wdf_rdy;
    wire acc_c  = app_en && app_rdy;

    always @(posedge clk) begin
        // ---- check pipeline ----
        v1 <= rd_pop;
        if (rd_pop) begin act1 <= app_rd_data; ra1 <= fifo[f_r]; f_r <= f_r + 5'd1; pass1 <= pass; masks1 <= ctl_r; end
        v2 <= v1;
        if (v1) begin
            act2 <= act1; ra2 <= ra1;
            exp2 <= (pass1 == 3'd3) ? e_p1 : merge(e_p1, e_p2, e_m);
        end
        if (v2 && act2 != exp2) begin
            errors <= errors + 32'd1;
            if (!first_v) begin
                first_v <= 1'b1; first_addr <= ra2; first_exp <= exp2; first_act <= act2;
                first_round <= round; first_pass <= {1'b0, pass};
            end
        end
        if (rd_pop) ret_cnt <= ret_cnt + {{AW{1'b0}}, 1'b1};
        if (rd_go) begin fifo[f_w] <= app_addr[AW+2:3]; f_w <= f_w + 5'd1; end
        inflight <= inflight + {5'd0, rd_go} - {5'd0, rd_pop};
        if (acc_c) cmds <= cmds + 32'd1;

        if (clear) begin
            errors <= 32'd0; cmds <= 32'd0; rounds <= 32'd0; first_v <= 1'b0;
        end

        if (rst) begin
            st <= T_IDLE; app_en <= 1'b0; app_wdf_wren <= 1'b0; inflight <= 6'd0; f_w <= 5'd0; f_r <= 5'd0;
            v1 <= 1'b0; v2 <= 1'b0; ret_cnt <= {(AW+1){1'b0}};
            pass <= 3'd0; round <= 32'd0; rounds <= 32'd0; ord <= 1'b0; errors <= 32'd0; cmds <= 32'd0; first_v <= 1'b0;
        end else case (st)
        T_IDLE: if (run) st <= T_INIT;
        T_INIT: begin                                         // start of a pass
            ctl_r <= ctl;
            rk1 <= round * 32'h85EBCA6B; rk2 <= round * 32'hC2B2AE3D;
            ord <= ord_new;
            n <= {(AW+1){1'b0}}; ret_cnt <= {(AW+1){1'b0}}; pp_rd <= 1'b0;
            a <= ord_new ? seed_w : {AW{1'b0}};
            st <= T_GEN;
        end
        T_GEN: begin                                          // data for this address
            if (pass == 3'd2 || (pass == 3'd3 && pp_rd)) begin
                app_addr <= {{(24-AW){1'b0}}, a, 3'b000}; app_cmd <= 3'b001;
                if (inflight < 6'd24) begin app_en <= 1'b1; st <= T_PRES; end
            end else begin
                app_addr <= {{(24-AW){1'b0}}, a, 3'b000}; app_cmd <= 3'b000;
                app_wdf_data <= pat(a, rk1, (pass == 3'd0) ? 2'd0 : (pass == 3'd1) ? 2'd1 : 2'd3);
                app_wdf_mask <= (pass == 3'd1) ? msk(a, rk2, ctl_r[3]) : 16'h0000;
                app_en <= 1'b1; app_wdf_wren <= 1'b1; st <= T_PRES;
            end
        end
        T_PRES: begin
            if (acc_c) app_en <= 1'b0;
            if (app_wdf_wren && app_wdf_rdy) app_wdf_wren <= 1'b0;
            if (c_done && d_done) st <= T_NEXT;               // taken, now or earlier
        end
        T_NEXT: begin
            if (pass == 3'd3 && !pp_rd) begin
                pp_rd <= 1'b1; st <= T_GEN;                    // read the same address back
            end else begin
                pp_rd <= 1'b0;
                n <= n + {{AW{1'b0}}, 1'b1};
                if (n == TOTAL - {{AW{1'b0}}, 1'b1}) st <= T_ENDPASS;
                else begin
                    if (ord) a <= (n == TOTAL - {{(AW-1){1'b0}}, 2'd2}) ? {AW{1'b0}} : lfsr_next(a);
                    else a <= a + {{(AW-1){1'b0}}, 1'b1};
                    st <= T_GEN;
                end
            end
        end
        T_ENDPASS: begin
            if (pass == 3'd1) begin hold_cnt <= hold; st <= T_HOLD; end
            else if (pass == 3'd0) begin pass <= 3'd1; st <= T_INIT; end
            else begin st <= T_WAITRD; end
        end
        T_HOLD: begin
            if (hold_cnt == 32'd0) begin pass <= 3'd2; st <= T_INIT; end else hold_cnt <= hold_cnt - 32'd1;
        end
        T_WAITRD: begin                                        // all reads of the pass back and checked
            if (ret_cnt == TOTAL && !v1 && !v2) begin
                if (pass == 3'd2 && ctl_r[2]) begin pass <= 3'd3; st <= T_INIT; end
                else begin
                    pass <= 3'd0; round <= round + 32'd1; rounds <= rounds + 32'd1;
                    st <= run ? T_INIT : T_IDLE;
                end
            end
        end
        default: st <= T_IDLE;
        endcase
    end

endmodule
