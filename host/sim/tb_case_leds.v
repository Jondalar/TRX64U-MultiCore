// case_leds: every selector value against its source, defaults before the
// app writes, activity stretch. iverilog -o tb tb_case_leds.v ../case_leds.v
`timescale 1ns/1ps
module tb_case_leds;
    reg clk = 0;
    always #5 clk = ~clk;
    reg [7:0] sel = 8'h00; reg sel_set = 0, def1 = 1, def2 = 0;
    reg a_pwr = 0, b_pwr = 0, a_act = 0, b_act = 0, usb_act = 0, any_act = 0, irq_line = 0;
    wire l1n, l2n;
    case_leds #(.CLK_HZ(2000)) dut (.clk(clk), .sel(sel), .sel_set(sel_set), .def1(def1), .def2(def2),
        .a_pwr(a_pwr), .b_pwr(b_pwr), .a_act(a_act), .b_act(b_act), .usb_act(usb_act),
        .any_act(any_act), .irq_line(irq_line), .led1n(l1n), .led2n(l2n));
    integer err = 0, s, v;
    reg exp;
    initial begin
        repeat (4) @(posedge clk); #1;
        if (l1n !== 1'b0 || l2n !== 1'b1) begin err = err + 1; $display("defaults %b %b", l1n, l2n); end
        sel_set = 1;
        for (s = 0; s < 16; s = s + 1)
            for (v = 0; v < 128; v = v + 1) begin
                {a_pwr, b_pwr, a_act, b_act, usb_act, any_act, irq_line} = v;
                sel = {s[3:0], 4'hF - s[3:0]};
                repeat (300) @(posedge clk); #1;   // longer than the 100-clock stretch
                case (s)
                0: exp = 1; 1: exp = 0; 2: exp = a_pwr; 3: exp = a_pwr | b_pwr; 4: exp = a_act;
                5: exp = a_act | b_act; 6: exp = a_pwr ^ a_act; 7: exp = usb_act; 8: exp = any_act;
                9: exp = !a_act; 10: exp = !(a_act | b_act); 11: exp = !usb_act; 12: exp = !any_act;
                13: exp = irq_line; 14: exp = !irq_line; default: exp = b_act;
                endcase
                if (l2n !== !exp) begin err = err + 1; $display("sel %0d v %h: led2n %b", s, v, l2n); end
            end
        // stretch: a 1-clock USB pulse lights the LED for CLK_HZ/20 = 100 clocks
        sel = 8'h07; usb_act = 0; repeat (300) @(posedge clk);
        #1 usb_act = 1; @(posedge clk); #1 usb_act = 0;
        repeat (50) @(posedge clk); #1 if (l1n !== 1'b0) begin err = err + 1; $display("stretch on"); end
        repeat (60) @(posedge clk); #1 if (l1n !== 1'b1) begin err = err + 1; $display("stretch off"); end
        if (err) $display("FAIL (%0d)", err); else $display("PASS");
        $finish;
    end
endmodule
