// T175: the open-drain joystick pads (diag_pads.v). Released (low = 0), every pin is the plain input it always was: with each pin held low from outside, pulled up, or toggling,
// the level the host sees (j*_i) is the pad's level, and nothing drives the pad (the external side keeps control). low[k] pulls exactly that pin low and nothing else; the
// pad is never driven high; core_decouple releases everything. The pads carry the pull-up of pins.xdc here as tri1 nets.
`timescale 1ns / 1ps
module tb_pads;
    tri1 [4:0] p1, p2;                          // the board's pull-ups
    reg  [4:0] x1 = 5'b11111, x2 = 5'b11111;    // the outside: bit set = not driven (Z), clear = held low
    reg  [9:0] low = 10'd0;
    reg        dec = 0;
    wire [4:0] j1, j2;
    genvar g;
    generate for (g = 0; g < 5; g = g + 1) begin : ext
        assign p1[g] = x1[g] ? 1'bz : 1'b0;
        assign p2[g] = x2[g] ? 1'bz : 1'b0;
    end endgenerate
    diag_pads dut (.KBJOY_1_JOY(p1), .KBJOY_2_JOY(p2), .low(low), .decouple(dec), .j1_i(j1), .j2_i(j2));
    integer errors = 0, checks = 0, a, b, k;
    task check(input c, input [8*80-1:0] msg); begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL %0s", msg); end end endtask
    initial begin
        // released: all 1024 combinations of the outside; the host sees exactly what a pure input would see
        for (a = 0; a < 32; a = a + 1) for (b = 0; b < 32; b = b + 1) begin
            x1 = a[4:0]; x2 = b[4:0]; #1;
            check(j1 === a[4:0] && j2 === b[4:0], "released pins read as the plain input (1024 combinations)");
            check(p1 === a[4:0] && p2 === b[4:0], "released: the pad level is the outside's, nothing is driven");
        end
        // driven: exactly one pin at a time (outside not driving)
        x1 = 5'b11111; x2 = 5'b11111;
        for (k = 0; k < 10; k = k + 1) begin
            low = 10'd1 << k; #1;
            check({j2, j1} === ~(10'd1 << k), "low[k] pulls pin k low and no other pin");
            check({p2, p1} === ~(10'd1 << k), "the pad itself is low, the others float high");
        end
        low = 10'h3FF; #1; check({j2, j1} === 10'h000, "all ten pulled low");
        // decouple releases
        dec = 1; #1; check({j2, j1} === 10'h3FF, "core_decouple releases every pin");
        dec = 0; low = 10'd0; #1;
        // held low from outside and drive requested: still low, no contention
        x1 = 5'b11110; low = 10'd1; #1; check(j1 === 5'b11110, "outside low and low[0]: low");
        low = 10'd0; #1; check(j1 === 5'b11110, "drive released: the outside's low stays");
        if (errors == 0) $display("PASS tb_pads errors=0 checks=%0d", checks); else $display("FAIL tb_pads errors=%0d", errors);
        $finish;
    end
endmodule

// T260: the cartridge port's address / data / R/W pads (diag_slot_pads.v). Idle = what the host's top had (A0-A11 driven 1, the rest released); a DRIVE (en, o) takes one pad low or high and
// no other; z releases A0-A11 so the port side behind the transceiver can be read; port_off (the host's expansion-port lock and the decouple) keeps the host's idle whatever the engine says.
// The outside is a tri-state driver per pad (x* bit set = not driven).
module tb_slotpads;
    wire [15:0] pa; wire [7:0] pd; wire prw;
    reg [15:0] xa = 16'hFFFF; reg [7:0] xd = 8'hFF; reg xrw = 1'b1;
    reg [15:0] xv = 16'h0000; reg [7:0] xdv = 8'h00;           // the outside's level when it drives
    genvar g;
    generate
        for (g = 0; g < 16; g = g + 1) begin : ea assign pa[g] = xa[g] ? 1'bz : xv[g]; end
        for (g = 0; g < 8; g = g + 1) begin : ed assign pd[g] = xd[g] ? 1'bz : xdv[g]; end
    endgenerate
    assign prw = xrw ? 1'bz : 1'b0;
    reg po = 0;
    reg [15:0] l2a = 16'hFFFF; reg l2ahi = 1; reg [7:0] l2d = 8'hFF; reg l2dt = 1; reg l2rw = 1;
    reg [15:0] aen = 0, ao = 0, az = 0; reg [7:0] den = 0, dov = 0; reg rwen = 0, rwo = 0;
    wire [15:0] ai; wire [7:0] di; wire rwi;
    diag_slot_pads dut (.SLOT_ADDR(pa), .SLOT_DATA(pd), .SLOT_RWn(prw), .port_off(po), .l2_a(l2a), .l2_ahi_t(l2ahi), .l2_d_o(l2d), .l2_d_t(l2dt), .l2_rw(l2rw),
        .a_en(aen), .a_o(ao), .a_z(az), .d_en(den), .d_o(dov), .rw_en(rwen), .rw_o(rwo), .a_i(ai), .d_i(di), .rw_i(rwi));
    integer errors = 0, checks = 0, k, j;
    task check(input c, input [8*80-1:0] msg); begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL %0s", msg); end end endtask
    initial begin
        #1;
        // idle: A0-A11 are driven 1 (the outside is not driving), A12-A15, data and R/W are released
        check(pa[11:0] === 12'hFFF, "idle: A0-A11 driven 1");
        check(pa[15:12] === 4'bzzzz && pd === 8'bzzzzzzzz && prw === 1'bz, "idle: A12-A15, data and R/W released");
        // the outside drives the released pads: the read-back follows it
        xa[15:12] = 4'b0000; xv = 16'h0000; xd = 8'h00; xdv = 8'h5A; xrw = 0; #1;
        check(ai[15:12] === 4'b0000 && di === 8'h5A && rwi === 1'b0, "released pads read the outside");
        xa = 16'hFFFF; xd = 8'hFF; xrw = 1;
        // the engine drives one pad at a time, low and high
        for (k = 0; k < 16; k = k + 1) begin
            aen = 16'd1 << k; ao = 16'd0; #1;
            check(pa[k] === 1'b0, "A drive low: the pad is low");
            check(k < 12 ? pa[11:0] === ~(12'd1 << k) : pa[11:0] === 12'hFFF, "A drive low: the other A0-A11 pads stay driven 1");
            for (j = 12; j < 16; j = j + 1) if (j != k) check(pa[j] === 1'bz, "A drive low: the other A12-A15 pads stay released");
            ao = 16'd1 << k; #1; check(pa[k] === 1'b1, "A drive high: the pad is high");
            aen = 0; ao = 0; #1;
        end
        for (k = 0; k < 8; k = k + 1) begin
            den = 8'd1 << k; dov = 8'd0; #1;
            check(pd[k] === 1'b0, "D drive low: the pad is low");
            for (j = 0; j < 8; j = j + 1) if (j != k) check(pd[j] === 1'bz, "D drive low: the other data pads stay released");
            dov = 8'd1 << k; #1; check(pd[k] === 1'b1, "D drive high");
            den = 0; dov = 0; #1;
        end
        rwen = 1; rwo = 0; #1; check(prw === 1'b0, "R/W drive low"); rwo = 1; #1; check(prw === 1'b1, "R/W drive high"); rwen = 0; #1; check(prw === 1'bz, "R/W released again");
        // hiz of A0-A11: the pad follows the outside
        az = 16'h0FFF; xa = 16'hF000; xv = 16'h0000; #1; check(ai[11:0] === 12'h000, "A0-A11 high-Z: the outside's low is read");
        xv = 16'h0555; #1; check(ai[11:0] === 12'h555, "A0-A11 high-Z: the outside's levels are read");
        az = 0; xa = 16'hFFFF; #1; check(pa[11:0] === 12'hFFF, "A0-A11 back to driven 1 when z is released");
        // port_off: the host's idle whatever the engine says
        po = 1; aen = 16'hFFFF; ao = 16'h0000; den = 8'hFF; dov = 8'h00; rwen = 1; rwo = 0; az = 16'h0FFF; #1;
        check(pa[11:0] === 12'hFFF, "port_off: A0-A11 driven 1 (even with z and a drive low)"); check(pa[15:12] === 4'bzzzz && pd === 8'bzzzzzzzz && prw === 1'bz, "port_off: the rest released");
        po = 0; aen = 0; den = 0; rwen = 0; az = 0; #1;
        // layer 2's own values (the host's) pass through when the engine is not driving
        l2ahi = 0; l2a = 16'hA5C3; l2dt = 0; l2d = 8'h3C; l2rw = 0; #1;
        check(pa === 16'hA5C3 && pd === 8'h3C && prw === 1'b0, "the host's values pass when the engine does not drive");
        if (errors == 0) $display("PASS tb_slotpads errors=0 checks=%0d", checks); else $display("FAIL tb_slotpads errors=%0d", errors);
        $finish;
    end
endmodule
