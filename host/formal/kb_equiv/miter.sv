// Formal miter: old (sim/kb_equiv) vs new (ROM table) c64u_keyboard, same inputs,
// every output equal every clock. Coupling invariant: every state register of the old
// design equals its namesake in the new one (`coupled`). It is inductive and implies the
// outputs, so the proof is a short k-induction (unbounded in time). Base case: power-up
// state equal (the registers rst does not clear, or clears one clock after a registered
// copy was taken: rx, sda_s, pc, keys_a/b, keys), rst high in the first cycle.
// ROM == the old prog() is part of the proof (ins is coupled).
module miter (
    input  wire        clk,
    input  wire        rst,
    input  wire        sda_in,
    input  wire        pause
);
    wire        o_scl, o_sda, o_par, n_scl, n_sda, n_par;
    wire [63:0] o_keys, n_keys;
    wire [15:0] o_st, n_st;

    c64u_keyboard_old o (.clk(clk), .rst(rst), .sda_in(sda_in), .scl_low(o_scl),
        .sda_low(o_sda), .keys(o_keys), .status(o_st), .pause(pause), .parked(o_par));
    c64u_keyboard n (.clk(clk), .rst(rst), .sda_in(sda_in), .scl_low(n_scl),
        .sda_low(n_sda), .keys(n_keys), .status(n_st), .pause(pause), .parked(n_par));

    reg init = 1'b1;
    always @(posedge clk) init <= 1'b0;

    wire coupled =
        o.div == n.div && o.pc == n.pc && o.bitn == n.bitn && o.ph == n.ph &&
        o.rx == n.rx && o.settle == n.settle && o.wait_cnt == n.wait_cnt &&
        o.ins == n.ins && o.dir == n.dir && o.col == n.col &&
        o.keys_a == n.keys_a && o.keys_b == n.keys_b &&
        o.sda_s == n.sda_s && o.pause_s == n.pause_s;

    always @(*) begin
        if (init) begin
            assume (rst);
            // common power-up state: the registers rst does not clear, or clears one
            // clock after a registered copy of them was taken, start equal in both
            assume (coupled && o.keys == n.keys);
        end else begin
            assert (coupled);
            assert (o_scl  == n_scl);
            assert (o_sda  == n_sda);
            assert (o_par  == n_par);
            assert (o_keys == n_keys);
            assert (o_st   == n_st);
        end
    end
endmodule
