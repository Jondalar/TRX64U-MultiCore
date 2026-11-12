// T89 S12 (the mouse half), the card on its own: the AppleMouse card of NanoApple2 (applemouse.vhd + pia6821.vhd as GHDL turns them into a netlist,
// the 6805 as jt6805 Verilog) on the enables of the host's sys_clk. A stand-in MCU ROM of ours (mk6805.py) replaces Apple's; the card ROM is a pattern.
//   T1 the card ROM at $C5nn is selected by the PIA's port B bits 3:1
//   T2 the PIA <-> MCU link and the MCU running on its 2 MHz enable: the nibble the 6502 side writes to port B 7:4 comes back on port A
//   T3 movement: a strobe with X = +5 starts five encoder steps 2048 enables apart; the pair of lines the MCU watches changes twice in them (the counts were
//      worked out by hand from the encoder's code); the MCU counts the changes, the 6502 side reads the count and the button; then X = -3 adds one
// Not simulated here (needs Apple's ROMs): the mouse firmware itself.
`timescale 1ns / 1ps
module tb_mouse;
    reg clk = 0;
    always #15.278 clk = ~clk;
    reg rst = 1;
    reg [3:0] acc = 0;
    reg ce = 0, ce_f = 0;
    reg [2:0] q3 = 0;
    wire ce2 = ce && q3 == 3'd6;
    always @(posedge clk) begin
        {ce, acc} <= {1'b0, acc} + 5'd7;
        ce_f <= ce;
        if (ce) q3 <= (q3 == 3'd6) ? 3'd0 : q3 + 3'd1;
    end

    reg        io_sel = 0, dev_sel = 0, rnw = 1, strobe = 0, button = 0;
    reg [15:0] a = 0;
    reg [7:0]  d_in = 0;
    reg [8:0]  x = 0, y = 0;
    wire [7:0] d_out;
    wire       oe, irq_n;
    wire [10:0] card_a, mcu_a;
    reg  [7:0] card_q = 0, mcu_q = 0;
    reg  [7:0] card_rom [0:2047];
    reg  [7:0] mcu_rom  [0:2047];
    integer i;
    initial begin
        for (i = 0; i < 2048; i = i + 1) card_rom[i] = (i * 7 + (i >> 8) * 29 + 3) & 255;
        $readmemh(`MCU_HEX, mcu_rom);
    end
    always @(posedge clk) if (ce) begin card_q <= card_rom[card_a]; mcu_q <= mcu_rom[mcu_a]; end

    applemouse dut (.CLK_14M(clk), .CE_14M(ce), .CE_F(ce_f), .CE_2M(ce2), .PHASE_ZERO(1'b0), .IO_SELECT(io_sel), .IO_STROBE(1'b0),
                    .DEVICE_SELECT(dev_sel), .RESET(rst), .A(a), .D_IN(d_in), .D_OUT(d_out), .RNW(rnw), .OE(oe), .IRQ_N(irq_n),
                    .STROBE(strobe), .X(x), .Y(y), .BUTTON(button), .CARD_ROM_A(card_a), .CARD_ROM_D(card_q), .MCU_ROM_A(mcu_a), .MCU_ROM_D(mcu_q));

    integer bad = 0;
    // a 6502 cycle is 14 enables long; the address and select are there for all of it
    task cycles(input integer n); integer k; begin k = 0; while (k < n) begin @(posedge clk); if (ce) k = k + 1; end end endtask
    task wr(input [1:0] reg_i, input [7:0] v); begin
        a = 16'hC0D0 + reg_i; dev_sel = 1; rnw = 0; d_in = v; cycles(14); dev_sel = 0; rnw = 1; cycles(2); end endtask
    reg [7:0] rd_v;
    task rd(input [1:0] reg_i); begin
        a = 16'hC0D0 + reg_i; dev_sel = 1; rnw = 1; cycles(13); rd_v = d_out; cycles(1); dev_sel = 0; cycles(2); end endtask
    task romrd(input [7:0] off); begin
        a = {8'hC5, off}; io_sel = 1; rnw = 1; cycles(13); rd_v = d_out; cycles(1); io_sel = 0; cycles(2); end endtask
    task check(input [511:0] what, input [7:0] got, input [7:0] want); begin
        if (got !== want) begin $display("FAIL %0s: got %02x, wanted %02x", what, got, want); bad = bad + 1; end
        else $display("ok   %0s: %02x", what, got);
    end endtask
    task pia_ports; begin wr(2'd1, 8'h00); wr(2'd3, 8'h00); end endtask      // control registers: the next access to 0 / 2 is the DDR

    initial begin
        #200 rst = 0;
        cycles(400);
        pia_ports;
        // T1 the card ROM: port B 3:1 outputs (the bank), data = bank 5 and bank 2
        wr(2'd2, 8'h0E);            // DDRB: bits 3:1 outputs
        wr(2'd3, 8'h04);            // port B data register selected
        wr(2'd2, 8'h0A);            // bank = bits 3:1 = 101
        romrd(8'h21); check("T1 bank 5, offset 21", rd_v, card_rom[{3'd5, 8'h21}]);
        wr(2'd2, 8'h04);            // bank 2
        romrd(8'h21); check("T1 bank 2, offset 21", rd_v, card_rom[{3'd2, 8'h21}]);
        romrd(8'h7F); check("T1 bank 2, offset 7F", rd_v, card_rom[{3'd2, 8'h7F}]);
        if (`ECHO) begin
            // T2 the nibble out on port B 7:4, back on port A
            wr(2'd3, 8'h00); wr(2'd2, 8'hFE); wr(2'd3, 8'h04);      // DDRB: 7:1 outputs
            wr(2'd1, 8'h00); wr(2'd0, 8'h00); wr(2'd1, 8'h04);      // port A: all inputs, data register selected
            wr(2'd2, 8'hA0); cycles(2000);
            `ifdef DEBUG
            $display("pia_pb_out=%h mcu_pc_in=%h mcu_pc_out=%h mcu_pa_out=%h pia_pa_in=%h mcu.addr=%h", dut.pia_pb_out, dut.mcu_pc_in, dut.mcu_pc_out, dut.mcu_pa_out, dut.pia_pa_in, dut.mcu.addr);
            `endif
            rd(2'd0); check("T2 nibble A", rd_v, 8'hFA);
            wr(2'd2, 8'h50); cycles(2000); rd(2'd0); check("T2 nibble 5", rd_v, 8'hF5);
        end else begin
            // T3 movement
            wr(2'd1, 8'h00); wr(2'd0, 8'h00); wr(2'd1, 8'h04);      // port A: inputs, data
            cycles(3000); rd(2'd0); check("T3 idle: count 0, button up", rd_v, 8'h80);
            // five steps, 2048 enables apart: the encoder (applemouse.vhd) changes the pair PB1:PB0 at the second and fourth step (2 changes in all);
            // after 3000 enables only the first step has happened, nothing has changed yet
            x = 9'd5; y = 9'd2; button = 1; strobe = 1; cycles(1); strobe = 0;
            cycles(3000); rd(2'd0); check("T3 X = +5, 3000 enables on: no change yet, button down", rd_v, 8'h00);
            cycles(9000); rd(2'd0); check("T3 X = +5 done: 2 changes, button down", rd_v, 8'h02);
            // three steps back from where the encoder stands: one more change
            x = -9'sd3; y = 9'd0; button = 0; strobe = 1; cycles(1); strobe = 0;
            cycles(8000); rd(2'd0); check("T3 X = -3 done: 3 changes, button up", rd_v, 8'h83);
        end
        if (bad == 0) $display("PASS tb_mouse"); else $display("FAIL tb_mouse %0d differences", bad);
        $finish;
    end
endmodule
