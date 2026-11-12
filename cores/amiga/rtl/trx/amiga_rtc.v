// T191: the Amiga's battery-backed clock, an MSM6242B-compatible register file at $DC0000 (spec docs/specs/T191-amiga-rtc.md D4-D6).
// Register n is at $DC0000 + 4n + 3 (the low byte of the word at 4n + 2), the nibble in D3:D0:
//   0 S1  1 S10  2 MI1  3 MI10  4 H1  5 H10  6 D1  7 D10  8 MO1  9 MO10  A Y1  B Y10  C W (weekday 0-6)
//   D  b3 30s ADJ (write 1: seconds to 0, minute + 1 from 30 s on; reads 0)  b2 IRQ flag (0)  b1 BUSY (0)  b0 HOLD
//   E  stored nibble (no interrupt is generated)       F  stored nibble, 4 after power-up (24 h; 12 h is not implemented)
// The counters tick once per DIV + 1 ce28 clocks with carries into day (month length, leap years 2000-2099), month, year and weekday.
// HOLD freezes the readable counters; one second that falls into a HOLD is applied on release.
// The application loads the time through `ld` = {toggle, index[3:0], value[3:0]}: a change of the toggle writes `value` into register
// `index` (0-12); index 14 sets the loader hold (value b0: no tick while it is set), index 15 zeroes the sub-second counter and sets
// `loaded`. `loaded` is a flip-flop of this module: a reloaded bitstream clears it.
module amiga_rtc #(
    parameter [24:0] DIV = 25'd28375159            // ce28 clocks per second, minus one (PAL 28.37516 MHz)
) (
    input  wire        clk,
    input  wire        ce28,
    input  wire        en,                         // the setting: 0 = no answer on the bus, no write taken
    input  wire [8:0]  ld,                         // from the application, already in the clk domain
    output reg         loaded = 1'b0,
    input  wire        sel,                        // $DCxxxx
    input  wire        rd,
    input  wire        lwr,
    input  wire [3:0]  ra,                         // cpu_address[5:2]
    input  wire [3:0]  din,                        // cpu_data[3:0]
    output wire [15:0] dout
);
    reg [3:0] s1 = 0, s10 = 0, mi1 = 0, mi10 = 0, h1 = 0, h10 = 0, d1 = 4'd1, d10 = 0, mo1 = 4'd1, mo10 = 0, y1 = 0, y10 = 0, wd = 0;
    reg hold = 1'b0, lhold = 1'b0, pend = 1'b0;
    reg [3:0] rE = 4'h0, rF = 4'h4;
    reg [24:0] cnt = 25'd0;
    reg tq1 = 1'b0, tq2 = 1'b0;                      // the toggle one and two clocks old: the data bits are a clock newer than the edge

    // ---- read side
    reg [3:0] rq;
    always @(*) begin
        case (ra)
            4'h0: rq = s1;  4'h1: rq = s10; 4'h2: rq = mi1; 4'h3: rq = mi10; 4'h4: rq = h1;  4'h5: rq = h10; 4'h6: rq = d1;
            4'h7: rq = d10; 4'h8: rq = mo1; 4'h9: rq = mo10; 4'hA: rq = y1;  4'hB: rq = y10; 4'hC: rq = wd;
            4'hD: rq = {3'b000, hold}; 4'hE: rq = rE; default: rq = rF;
        endcase
    end
    wire act = en;
    assign dout = (sel && rd && act) ? {12'h000, rq} : 16'h0000;

    // ---- the next second
    wire [6:0] mo_b = mo10[0] ? (7'd10 + mo1) : {3'd0, mo1};
    wire [6:0] d_b  = {d10[1:0], 3'd0} + {d10[1:0], 1'b0} + d1;      // d10 * 10 + d1
    wire leap = (({y10[0], 1'b0} + y1[1:0]) & 3'd3) == 3'd0;
    reg [5:0] dim;
    always @(*) begin
        case (mo_b)
            7'd2: dim = leap ? 6'd29 : 6'd28;
            7'd4, 7'd6, 7'd9, 7'd11: dim = 6'd30;
            default: dim = 6'd31;
        endcase
    end
    wire c_s1 = (s1 == 9) || (s1 > 9);
    wire c_s  = c_s1 && (s10 >= 5);
    wire c_m1 = c_s && (mi1 >= 9);
    wire c_m  = c_m1 && (mi10 >= 5);
    wire c_h  = c_m && (h10 >= 2 ? (h1 >= 3) : (h1 >= 9));
    wire c_d  = c_h && (d_b >= {1'b0, dim});
    wire c_mo = c_d && (mo_b >= 7'd12);

    // ---- one clock for everything: the application's load, the bus writes, the tick
    wire wr = sel && lwr && act;
    reg  wr_q = 1'b0; reg [3:0] wr_ra = 0, wr_d = 0;
    wire adj_go = !wr && wr_q && wr_ra == 4'hD && wr_d[3];    // the 30s adjust acts when the strobe ends
    wire ld_ch = (tq1 != tq2);
    wire [3:0] li = ld[7:4], lv = ld[3:0];

    always @(posedge clk) begin
        wr_q <= wr;
        if (wr) begin wr_ra <= ra; wr_d <= din; end
        tq1 <= ld[8]; tq2 <= tq1;

        // a write from the bus
        if (wr) begin
            case (ra)
                4'h0: s1 <= din;  4'h1: s10 <= din; 4'h2: mi1 <= din; 4'h3: mi10 <= din; 4'h4: h1 <= din; 4'h5: h10 <= din;
                4'h6: d1 <= din;  4'h7: d10 <= din; 4'h8: mo1 <= din; 4'h9: mo10 <= din; 4'hA: y1 <= din; 4'hB: y10 <= din;
                4'hC: wd <= din;
                4'hD: hold <= din[0];
                4'hE: rE <= din;
                4'hF: rF <= din;
                default: ;
            endcase
        end

        // the application's load
        if (ld_ch) begin
            case (li)
                4'h0: s1 <= lv;  4'h1: s10 <= lv; 4'h2: mi1 <= lv; 4'h3: mi10 <= lv; 4'h4: h1 <= lv; 4'h5: h10 <= lv;
                4'h6: d1 <= lv;  4'h7: d10 <= lv; 4'h8: mo1 <= lv; 4'h9: mo10 <= lv; 4'hA: y1 <= lv; 4'hB: y10 <= lv;
                4'hC: wd <= lv;
                4'hE: begin lhold <= lv[0]; pend <= 1'b0; end
                4'hF: begin cnt <= 25'd0; loaded <= 1'b1; pend <= 1'b0; end
                default: ;
            endcase
        end

        if (adj_go) begin                           // 30 s adjust: seconds to 0, the minute rounds up from 30 s
            s1 <= 4'd0; s10 <= 4'd0; cnt <= 25'd0;
            if (s10 >= 3) begin
                if (mi1 >= 9) begin mi1 <= 4'd0; mi10 <= (mi10 >= 5) ? 4'd0 : mi10 + 4'd1; end
                else mi1 <= mi1 + 4'd1;
            end
        end

        // the second
        if (ce28 && !lhold) begin
            if (cnt == DIV) begin
                cnt <= 25'd0;
                if (hold) pend <= 1'b1;
                else tick();
            end else cnt <= cnt + 25'd1;
        end
        if (!hold && pend && !lhold && !(ce28 && cnt == DIV)) begin
            pend <= 1'b0; tick();
        end
    end

    task tick;
        begin
            s1 <= c_s1 ? 4'd0 : s1 + 4'd1;
            if (c_s1) s10 <= (s10 >= 5) ? 4'd0 : s10 + 4'd1;
            if (c_s) begin mi1 <= (mi1 >= 9) ? 4'd0 : mi1 + 4'd1; end
            if (c_m1) mi10 <= (mi10 >= 5) ? 4'd0 : mi10 + 4'd1;
            if (c_m) begin
                if (h10 >= 2 ? (h1 >= 3) : (h1 >= 9)) begin
                    h1 <= 4'd0; h10 <= (h10 >= 2) ? 4'd0 : h10 + 4'd1;
                end else h1 <= h1 + 4'd1;
            end
            if (c_h) wd <= (wd >= 6) ? 4'd0 : wd + 4'd1;
            if (c_h) begin
                if (c_d) begin d1 <= 4'd1; d10 <= 4'd0; end
                else if (d1 >= 9) begin d1 <= 4'd0; d10 <= d10 + 4'd1; end
                else d1 <= d1 + 4'd1;
            end
            if (c_d) begin
                if (c_mo) begin mo1 <= 4'd1; mo10 <= 4'd0; end
                else if (mo1 >= 9) begin mo1 <= 4'd0; mo10 <= mo10 + 4'd1; end
                else mo1 <= mo1 + 4'd1;
            end
            if (c_mo) begin
                if (y1 >= 9) begin y1 <= 4'd0; y10 <= (y10 >= 9) ? 4'd0 : y10 + 4'd1; end
                else y1 <= y1 + 4'd1;
            end
        end
    endtask
endmodule
