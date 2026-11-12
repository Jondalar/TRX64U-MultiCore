// T195 S1: ui_buttons - a press on each pin is seen after the 20 ms debounce and not before, a bounce shorter than the window is not seen,
// a release is debounced the same way, the buttons are independent. 1 ms = 100 clocks here. ci/sim_iv.sh tb_ui_buttons.v
`timescale 1ns/1ps
module tb_ui_buttons;
    reg clk = 0;
    always #5 clk = ~clk;
    reg [6:0] t = 0;
    reg tick = 0;
    always @(posedge clk) begin t <= (t == 99) ? 7'd0 : t + 7'd1; tick <= (t == 99); end
    reg [2:0] pin_n = 3'b111;
    wire [2:0] btn;
    ui_buttons dut (.clk(clk), .tick_1ms(tick), .pin_n(pin_n), .btn(btn));
    integer err = 0, i, k;
    task ms(input integer n); begin repeat (n * 100) @(posedge clk); end endtask
    task expect_btn(input [2:0] e, input [255:0] what);
        begin if (btn !== e) begin err = err + 1; $display("FAIL %0s: btn=%b want %b", what, btn, e); end end
    endtask
    initial begin
        ms(3);
        expect_btn(3'b000, "idle");
        for (i = 0; i < 3; i = i + 1) begin
            // a bounce of 15 ms never gets through
            pin_n[i] = 0; ms(15); pin_n[i] = 1; ms(5);
            expect_btn(3'b000, "15 ms bounce");
            // many short bounces (3 ms low, 3 ms high) for 60 ms
            for (k = 0; k < 10; k = k + 1) begin pin_n[i] = 0; ms(3); pin_n[i] = 1; ms(3); end
            expect_btn(3'b000, "chatter");
            // a real press: not seen at 17 ms, seen at 25 ms, and only this one
            pin_n[i] = 0; ms(17);
            expect_btn(3'b000, "press before the window");
            ms(8);
            expect_btn(3'b001 << i, "press after the window");
            // a release glitch of 10 ms keeps the button down
            pin_n[i] = 1; ms(10); pin_n[i] = 0; ms(30);
            expect_btn(3'b001 << i, "release glitch");
            // a real release
            pin_n[i] = 1; ms(17);
            expect_btn(3'b001 << i, "release before the window");
            ms(8);
            expect_btn(3'b000, "release after the window");
        end
        // all three at once, then one by one up
        pin_n = 3'b000; ms(25); expect_btn(3'b111, "all down");
        pin_n[1] = 1; ms(25); expect_btn(3'b101, "middle up");
        pin_n = 3'b111; ms(25); expect_btn(3'b000, "all up");
        if (err == 0) $display("PASS tb_ui_buttons errors=0");
        else $display("FAIL tb_ui_buttons errors=%0d", err);
        $finish;
    end
endmodule
