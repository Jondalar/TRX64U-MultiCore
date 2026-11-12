// One Mandelbrot engine.  CHANGED by T168 (see rtl/pixel_gen_mandelbrot.sv.diff): pipelined, two pixels interleaved; add_u50_carry4 below is unchanged.
// This will use 6 DSP48E1 slices.
// 20 instances use (6*20=120) DSP48E1 slices in the XC7A50T (i.e. all of them).
//
// Signed fixed-point format Q3.22 (as per ARM "Q notation"):
//   - cx_q, cy_q, zx, zy are signed Q(FRAC) values (default FRAC=22).
//   - The escape test compares |z|^2 against 4.0 using full precision Q(2*FRAC).
//
// Handshake:
//   - 'start' asserted for one cycle while busy=0 launches a new pixel.
//   - 'busy' stays high while iterating; done pulses for 1 cycle on completion.
//   - 'iter8' is the framebuffer palette index:
//             0 for points inside the set, otherwise the low-byte iteration count plus one.
//             0xff saturates instead of wrapping to zero.
//
// Artix-7 implementation note:
//   Each signed 25x25 product uses two DSP48E1 slices.
//   The escape-test sum is deliberately built from CARRY4 primitives so it remains in fabric and has a fast, predictable carry path.
//
(* use_dsp = "no", keep_hierarchy = "yes" *)
module add_u50_carry4 (
    input  wire [49:0] a,
    input  wire [49:0] b,
    output wire [50:0] sum
);
    // Zero extension is correct: both inputs are squares and therefore non-negative, even though their source multipliers are signed.
    wire [51:0] a_ext = {2'b00, a};
    wire [51:0] b_ext = {2'b00, b};
    wire [51:0] propagate = a_ext ^ b_ext;
    wire [51:0] sum_ext;
    wire [51:0] carry_bits;
    wire [13:0] carry_link;

    assign carry_link[0] = 1'b0;

    genvar carry_i;
    generate
        for (carry_i = 0; carry_i < 13; carry_i = carry_i + 1) begin : G_CARRY
            CARRY4 u_carry4 (
                .CI     (carry_link[carry_i]),
                .CYINIT (1'b0),
                .DI     (a_ext[carry_i*4 +: 4]),
                .S      (propagate[carry_i*4 +: 4]),
                .O      (sum_ext[carry_i*4 +: 4]),
                .CO     (carry_bits[carry_i*4 +: 4])
            );
            assign carry_link[carry_i+1] = carry_bits[carry_i*4+3];
        end
    endgenerate

    // The sum of two 50-bit values needs 51 bits.
    // The 52nd carry-chain bit is only padding for the final four-bit CARRY4 primitive.
    assign sum = sum_ext[50:0];
endmodule

module pixel_gen_mandelbrot #(
    parameter int FRAC = 22
)(
    input  logic                  clk,
    input  logic                  rst,

    // Two independent pixel contexts (0 and 1) share the multipliers; each has the handshake of the single-pixel engine.
    input  logic                  start0,
    input  logic signed [24:0]    cx_q0,
    input  logic signed [24:0]    cy_q0,
    input  logic                  start1,
    input  logic signed [24:0]    cx_q1,
    input  logic signed [24:0]    cy_q1,
    input  logic [11:0]           max_iters,  // runtime max iters (16..4095), latched per pixel

    output logic                  busy0,
    output logic                  done0,
    output logic [7:0]            iter8_0,
    output logic                  busy1,
    output logic                  done1,
    output logic [7:0]            iter8_1
);

    // Mandelbrot formula: z(0) = 0, z(n+1) = z(n)^2 + c, until |z|^2 > 4 or iter reaches max_it (as in the single-pixel engine).
    //
    // T168: the original did multiply, 50 bit add, compare and the update of z in one clock (WNS -2.3 ns at 100 MHz). Here the loop z -> z' has two register
    // stages and two pixels (contexts) chase each other through it, so the engine still finishes one iteration per clock:
    //   stage 1 (clock t):   zx, zy (DSP input registers) -> three 25x25 products -> p_* (DSP output registers)
    //   stage 2 (clock t+1): p_* -> z(n+1) = z^2 + c of the context s2 -> zx, zy;   p_xx + p_yy -> mag2_r (51 bit register); the context's tag -> t_*
    //   stage 3 (clock t+2): mag2_r > 4.0 and the last-iteration flag decide done / iter8 of the tagged context. Not part of the loop.
    // The decision of iteration n is made one clock after the context has started iteration n+1 with a junk z; that z is thrown away with the context
    // (act and busy go low in stage 3, the next tag of that context is then invalid). The result per pixel (iter8) is the same as with the single-clock engine.

    logic signed [24:0] zx, zy;                       // z of the context that is in stage 1 (the other one of s2)
    (* use_dsp = "yes", keep = "true" *) logic signed [49:0] p_xx;
    (* use_dsp = "yes", keep = "true" *) logic signed [49:0] p_yy;
    (* use_dsp = "yes", keep = "true" *) logic signed [49:0] p_xy;

    logic s2;                                         // context in stage 2 in this clock; toggles every clock

    logic        busy [2];
    logic        act  [2];                            // the context's z runs in the loop (set when z0 is loaded, cleared by the decision)
    logic        pend [2];                            // started, z0 not loaded yet
    logic signed [24:0] cx [2];
    logic signed [24:0] cy [2];
    logic [11:0] maxm1 [2];                           // max_it - 1
    logic [11:0] iter  [2];                           // iteration whose squares are in p_* while the context is in stage 2
    logic        done_r [2];
    logic [7:0]  iter8_r [2];

    assign busy0 = busy[0];   assign busy1 = busy[1];
    assign done0 = done_r[0]; assign done1 = done_r[1];
    assign iter8_0 = iter8_r[0]; assign iter8_1 = iter8_r[1];

    // Stage 2 operands.
    logic signed [24:0] zx2, zy2, two_zxzy, zx_next, zy_next;
    logic signed [24:0] cx_sel, cy_sel;
    always_comb begin
        cx_sel = cx[s2];
        cy_sel = cy[s2];
        zx2      = $signed(p_xx >>> FRAC);
        zy2      = $signed(p_yy >>> FRAC);
        two_zxzy = $signed(p_xy >>> (FRAC-1));   // 2*zx*zy
        zx_next  = (zx2 - zy2) + cx_sel;
        zy_next  = $signed(two_zxzy + cy_sel);
    end

    // Full-precision Q(2*FRAC) escape threshold: |z|^2 > 4.0.
    localparam logic [50:0] ESCAPE_Q2 = (51'd4 << (2*FRAC));

    wire [50:0] mag2_full;
    add_u50_carry4 u_mag2_add (
        .a   (p_xx),
        .b   (p_yy),
        .sum (mag2_full)
    );

    // Stage 1: the multipliers (registers on both sides are absorbed by the DSP48E1).
    always_ff @(posedge clk) begin
        p_xx <= $signed(zx) * $signed(zx);
        p_yy <= $signed(zy) * $signed(zy);
        p_xy <= $signed(zx) * $signed(zy);
    end

    // Tag and sum of stage 2, read by stage 3.
    logic        t_v;
    logic        t_c;
    logic [11:0] t_n;
    logic        t_last;
    logic [50:0] mag2_r;

    wire esc = (mag2_r > ESCAPE_Q2);
    wire ld  = pend[s2] && !act[s2];

    always_ff @(posedge clk) begin
        // z loop: a context that is not running gets a junk z (nobody reads it).
        zx <= ld ? 25'sd0 : zx_next;
        zy <= ld ? 25'sd0 : zy_next;
        mag2_r <= mag2_full;
        t_v    <= act[s2];
        t_c    <= s2;
        t_n    <= iter[s2];
        t_last <= (iter[s2] == maxm1[s2]);

        if (rst) begin
            s2 <= 1'b0;
            t_v <= 1'b0;
            for (int c = 0; c < 2; c++) begin
                busy[c] <= 1'b0;  act[c] <= 1'b0;  pend[c] <= 1'b0;
                cx[c] <= 25'sd0;  cy[c] <= 25'sd0; maxm1[c] <= 12'd15; iter[c] <= 12'd0;
                done_r[c] <= 1'b0; iter8_r[c] <= 8'd0;
            end
        end else begin
            s2 <= ~s2;
            done_r[0] <= 1'b0;
            done_r[1] <= 1'b0;

            // Start of a pixel (the same rule as the single-pixel engine: only while the context is not busy).
            if (start0 && !busy[0]) begin
                busy[0] <= 1'b1;  pend[0] <= 1'b1;
                cx[0] <= cx_q0;   cy[0] <= cy_q0;
                // Latch the iteration limit so a control change cannot alter a pixel already in progress; defensive minimum of 16.
                maxm1[0] <= ((max_iters < 12'd16) ? 12'd16 : max_iters) - 12'd1;
            end
            if (start1 && !busy[1]) begin
                busy[1] <= 1'b1;  pend[1] <= 1'b1;
                cx[1] <= cx_q1;   cy[1] <= cy_q1;
                maxm1[1] <= ((max_iters < 12'd16) ? 12'd16 : max_iters) - 12'd1;
            end

            // Stage 2 bookkeeping of the context s2: load z0 = 0 of a started pixel, or count the iteration whose squares are in p_*.
            if (ld) begin
                act[s2]  <= 1'b1;
                pend[s2] <= 1'b0;
                iter[s2] <= 12'd0;
            end else if (act[s2]) begin
                iter[s2] <= iter[s2] + 12'd1;
            end

            // Stage 3: the decision for the tagged context (always the context that is NOT in stage 2 now).
            if (t_v && (esc || t_last)) begin
                busy[t_c]   <= 1'b0;
                act[t_c]    <= 1'b0;
                done_r[t_c] <= 1'b1;
                // Escaped: nonzero values select palette colours (0xff saturates). Did not escape in the last iteration: index zero is Mandelbrot black.
                iter8_r[t_c] <= esc ? ((&t_n[7:0]) ? 8'hff : (t_n[7:0] + 8'd1)) : 8'd0;
            end
        end
    end
endmodule
