// T134 Q13: the SuperPET's board registers at $EF00-$EFFF (written for this project from the behaviour of VICE's petmem.c, no code copied):
//   $EFF0-$EFF3  ACIA 6551 (minimal: the registers are there and read back, nothing is sent or received: status = TDRE + DCD, no data ever arrives)
//   $EFE0-$EFE3  the 6702 dongle (a read gives the output register, a write steps the eight circular shift registers; petmem.c reset6702 / write6702)
//   $EFF8-$EFFB  control (write only, while the control write protect is off): b0 CPU (0 = 6809, honoured in PROG mode only), b1 RAM write protect off, b3 diagnostic sense
//   $EFFC-$EFFD  bank select (write only): b3:0 the 4 KiB bank behind $9000-$9FFF, b5 FIRQ off, b6 flat mode, b7 control write protect off
//   $EFFE-$EFFF  b0 = 1 puts the ROM back behind $9000-$9FFF (the bank RAM is behind it at reset)
//   everything else in the page reads $FF (the dongle's decode answers $FF where it is not selected; the rest of the page is open bus on the real board)
// Flat mode (b6, the TPUG Super OS/9 MMU) and FIRQ off are stored and not acted upon: the 6809's FIRQ is not wired in this core.
`timescale 1ns / 1ps
module pet_spet (
    input  wire        clk,
    input  wire        rst,                 // the machine's reset
    input  wire        en,                  // the model is the SuperPET
    input  wire [1:0]  cpu_set,             // the app's switch, taken at reset: 0 6502, 1 6809, 2 PROG (the program picks through $EFF8 b0)
    input  wire        st,                  // one bus cycle's strobe
    input  wire [15:0] addr,
    input  wire        we,
    input  wire [7:0]  wdata,
    output wire        hit,                 // the access is in $EF00-$EFFF
    output wire [7:0]  q,
    output reg         ramen = 1'b1,        // the bank RAM is behind $9000-$9FFF
    output reg  [3:0]  bank = 4'd0,
    output reg         ramwp = 1'b0,        // the RAM is write protected
    output reg         diag = 1'b0,
    output reg         act809 = 1'b0        // the 6809 is the CPU
);
    assign hit = en & (addr[15:8] == 8'hEF);
    wire w = st & we & hit;
    reg        ctrlwp = 1'b1;
    reg        firq_off = 1'b0, flat = 1'b0;
    // ACIA
    reg [7:0]  a_cmd = 8'd0, a_ctl = 8'd0;
    // the dongle
    reg [7:0]  dv = 8'hD6;
    reg [7:0]  dprev = 8'h01;               // previous odd value
    reg        wantodd = 1'b0;
    reg [7:0]  sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7;                      // loaded in reset
    // the shift registers' lengths 6 3 7 8 1 3 5 2 = leftmost bit 1 << (len - 1); reset has bits set in 1, 2, 4, 6, 7 and 0 (DONGLE_MAGIC | 1)
    function [7:0] lm; input [2:0] i; begin
        case (i) 3'd0: lm = 8'h20; 3'd1: lm = 8'h04; 3'd2: lm = 8'h40; 3'd3: lm = 8'h80;
                 3'd4: lm = 8'h01; 3'd5: lm = 8'h04; 3'd6: lm = 8'h10; default: lm = 8'h02; endcase
    end endfunction
    // reset: shift[i] = leftmost[i] for bits in (0xD6 | 1) = {0,1,2,4,6,7}, else 0
    wire [7:0] r_sh0 = lm(0), r_sh1 = lm(1), r_sh2 = lm(2), r_sh3 = 8'h00, r_sh4 = lm(4), r_sh5 = 8'h00, r_sh6 = lm(6), r_sh7 = lm(7);
    // one step of one register: toggle the far bit when the input bit changed, rotate; the output toggles when a 1 leaves
    function [8:0] step; input [7:0] s; input [7:0] l; input chg; input v; reg [7:0] t; begin
        t = chg ? (s ^ l) : s;
        step = {v ^ t[0], t[0] ? ((t >> 1) | l) : (t >> 1)};
    end endfunction
    wire        d_sel = w & (addr[15:4] == 12'hEFE) & (addr[3:2] == 2'b00);
    wire        d_go = d_sel & (wdata[0] == wantodd);                                       // only the write with the expected parity counts
    wire [7:0]  chg = dprev ^ wdata;
    wire [8:0]  s0 = step(sh0, lm(0), chg[0], dv[0]), s1 = step(sh1, lm(1), chg[1], dv[1]), s2 = step(sh2, lm(2), chg[2], dv[2]),
                s3 = step(sh3, lm(3), chg[3], dv[3]), s4 = step(sh4, lm(4), chg[4], dv[4]), s5 = step(sh5, lm(5), chg[5], dv[5]),
                s6 = step(sh6, lm(6), chg[6], dv[6]), s7 = step(sh7, lm(7), chg[7], dv[7]);
    always @(posedge clk) begin
        if (rst) begin
            ramen <= 1'b1;
            bank <= 4'd0;
            ctrlwp <= 1'b1;
            firq_off <= 1'b0;
            flat <= 1'b0;
            a_cmd <= 8'd0;
            a_ctl <= 8'd0;
            dv <= 8'hD6;
            dprev <= 8'h01;
            wantodd <= 1'b0;
            {sh0, sh1, sh2, sh3, sh4, sh5, sh6, sh7} <= {r_sh0, r_sh1, r_sh2, r_sh3, r_sh4, r_sh5, r_sh6, r_sh7};
            act809 <= en & (cpu_set == 2'd1);
        end else if (w) begin
            if (addr[7:1] == 7'h7F) ramen <= ~wdata[0];                                   // $EFFE-$EFFF
            else if (addr[7:2] == 6'h3F) begin                                             // $EFFC-$EFFD (VICE: addr >= $EFFC; the page's top four are $EFFC-$EFFF)
                bank <= wdata[3:0];
                firq_off <= wdata[5];
                flat <= wdata[6];
                ctrlwp <= ~wdata[7];
            end else if (addr[7:3] == 5'h1F) begin                                         // $EFF8-$EFFB
                if (!ctrlwp) begin
                    if (cpu_set == 2'd2) act809 <= ~wdata[0];
                    ramwp <= ~wdata[1];
                    diag <= wdata[3];
                end
            end else if (addr[7:2] == 6'h3C) begin                                         // $EFF0-$EFF3 ACIA
                if (addr[1:0] == 2'd1) begin a_cmd <= {a_cmd[7:5], 5'd0}; end            // programmed reset: command b4:0 cleared
                if (addr[1:0] == 2'd2) a_cmd <= wdata;
                if (addr[1:0] == 2'd3) a_ctl <= wdata;
            end
            if (d_go & wantodd) begin                                                      // $EFE0-$EFE3: an odd value after an even one steps the registers
                dprev <= wdata;
                dv <= {s7[8], s6[8], s5[8], s4[8], s3[8], s2[8], s1[8], s0[8]};
                {sh7, sh6, sh5, sh4, sh3, sh2, sh1, sh0} <= {s7[7:0], s6[7:0], s5[7:0], s4[7:0], s3[7:0], s2[7:0], s1[7:0], s0[7:0]};
            end
            if (d_go) wantodd <= ~wantodd;
        end
    end
    reg [7:0] rq;
    always @* begin
        rq = 8'hFF;
        if (addr[7:2] == 6'h3C) begin                                                      // ACIA
            case (addr[1:0])
                2'd0: rq = 8'd0;
                2'd1: rq = 8'h30;                                                           // TDRE + DCD
                2'd2: rq = a_cmd;
                default: rq = a_ctl;
            endcase
        end else if (addr[7:4] == 4'hE && addr[3:2] == 2'b00) rq = dv;                     // dongle
    end
    assign q = rq;
endmodule
