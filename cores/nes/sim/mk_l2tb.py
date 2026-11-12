#!/usr/bin/env python3
"""T95: tb_nes_l2.v for the partition's real edge: every port of layer2_nes.v declared from the module's own header (inputs as
regs at 0, outputs as wires), instantiated with .*, plus the checks: the ID through the register window, the machine leaves
reset (c64_leds[0]), Freeze through c64_halt_req gives c64_halted and stops the DDR2 writes and the idle values of the ports a
NES does not use (the decoupler's values, top.v)."""
import re, sys

src = open(sys.argv[1]).read()
m = re.search(r"module\s+layer2_nes\b.*?\)\s*;", src, re.S)
ports = re.findall(r"(input|output)\s+wire\s+(signed\s+)?(\[[^\]]+\]\s+)?(\w+)", m.group(0))
decl, outs = [], []
for d, sg, w, n in ports:
    w = w or ""
    sg = sg or ""
    if d == "input":
        decl.append("    reg %s%s %s = 0;" % (sg, w.strip(), n))
    else:
        decl.append("    wire %s%s %s;" % (sg, w.strip(), n))
        outs.append((n, w.strip()))
body = "\n".join(decl)
open(sys.argv[2], "w").write('''`timescale 1ns / 1ps
module tb_nes_l2;
%s
    integer fails = 0, w0, w1;
    always #5 cpu_clk = ~cpu_clk;
    always #15.859 sys_clk = ~sys_clk;
    always #40.69 mclk = ~mclk;
    always #3.333 ui_clk = ~ui_clk;
    always #15.859 c64_gclk = ~c64_gclk;
    layer2_nes dut (.*);
    integer writes = 0;
    always @(posedge ui_clk) if (e_en && e_rdy && e_cmd == 0) writes = writes + 1;
    initial e_rdy_force = 1;
    reg e_rdy_force;
    always @* e_rdy = e_rdy_force;
    // the real-time DDR2 port: an empty memory (the cartridge reads 0), 30 clocks to answer
    reg [5:0] c_cnt = 0; reg c_busy = 0;
    always @(posedge ui_clk) begin
        c_rdy <= 1; c_rd_valid <= 0; c_rd_data <= 128'd0;
        if (c_en && c_rdy && c_cmd == 3'b001) begin c_busy <= 1; c_cnt <= 0; end
        if (c_busy) begin c_cnt <= c_cnt + 1; if (c_cnt == 30) begin c_busy <= 0; c_rd_valid <= 1; end end
    end
    reg [7:0] id [0:3];
    integer k;
    task cw_read(input [11:0] a, output [7:0] d);
        begin @(posedge cpu_clk); cw_addr = {5'd0, a}; cw_re = 1; @(posedge cpu_clk); cw_re = 0; @(posedge cpu_clk); @(posedge cpu_clk); d = cw_rdata; end
    endtask
    initial begin
        sys_rst = 1; cpu_rst = 1; mclk_rst = 1; ui_rst = 1; core_reset_req = 1;
        #500; sys_rst = 0; cpu_rst = 0; mclk_rst = 0; ui_rst = 0;
        #200000; core_reset_req = 0;
        for (k = 0; k < 4; k = k + 1) cw_read(k, id[k]);
        if ({id[3], id[2], id[1], id[0]} === 32'h5F53454E) $display("ok   ID through the register window is NES_"); else begin $display("FAIL ID %%h", {id[3], id[2], id[1], id[0]}); fails = fails + 1; end
        #20000000;
        if (c64_leds[0] === 1'b1) $display("ok   the machine left reset (c64_leds[0])"); else begin $display("FAIL still in reset"); fails = fails + 1; end
        if (writes > 0) $display("ok   the frame writer has written %%0d words to the DDR2 (empty cartridge: a backdrop frame)", writes); else begin $display("FAIL no DDR2 write"); fails = fails + 1; end
        if (drv_tgl === 1'b0 && smp_f_tgl === 1'b0 && reu_cmd_valid === 1'b0) $display("ok   the drive port, the sampler port and the REU are idle"); else begin $display("FAIL an unused port is busy"); fails = fails + 1; end
        if (SLOT_ROMLn === 1'b1 && SLOT_ROMHn === 1'b1 && IEC_ATN_O === 1'b0 && gate_frz === 1'b1) $display("ok   the cartridge port and the IEC bus sit at the decoupler's idle values"); else begin $display("FAIL idle values"); fails = fails + 1; end
        // Freeze
        // the machine pauses at the next vblank on an instruction boundary (nes.v pausecore): up to a frame, 17 ms
        c64_halt_req = 1;
        begin : wait_halt
            repeat (400) begin #100000; if (c64_halted === 1'b1) disable wait_halt; end
        end
        #1000000;
        w0 = writes; #4000000; w1 = writes;
        if (c64_halted === 1'b1) $display("ok   Freeze: c64_halted answers"); else begin $display("FAIL c64_halted never came"); fails = fails + 1; end
        if (w0 == w1) $display("ok   Freeze: no DDR2 write while halted"); else begin $display("FAIL %%0d DDR2 writes while halted", w1 - w0); fails = fails + 1; end
        c64_halt_req = 0;
        #40000000;
        if (c64_halted === 1'b0 && writes > w1) $display("ok   released: the machine runs on and the frame writer too"); else begin $display("FAIL not running after release"); fails = fails + 1; end
        if (fails == 0) $display("PASS tb_nes_l2"); else $display("FAILED tb_nes_l2 (%%0d)", fails);
        $finish;
    end
endmodule
''' % body)
