// Case LEDs as the app selects them: U64_CASELED_SELECT (0x1010040E), low
// nibble for output 1 (LED_CASE1n), high nibble for output 2 (LED_CASE2n),
// sources in the order of u64_config.cc ledselects[]:
//   0 on, 1 off, 2 drive A power, 3 A | B power, 4 drive A activity,
//   5 A | B activity, 6 A power ^ A activity, 7 USB activity, 8 any
//   activity, 9 !A act, 10 !(A | B act), 11 !USB, 12 !any, 13 IRQ line,
//   14 !IRQ, 15 drive B activity
// Until the app writes the selector, def1/def2 show (our bring-up meanings).
// Activity is stretched to 50 ms so that short accesses blink visibly.
module case_leds #(
    parameter integer CLK_HZ = 100000000
) (
    input  wire       clk,
    input  wire [7:0] sel,
    input  wire       sel_set,              // the app has written sel
    input  wire       def1, def2,
    input  wire       a_pwr, b_pwr,         // levels, this clock
    input  wire       a_act, b_act, usb_act, any_act, irq_line,
    output reg        led1n = 1'b1,
    output reg        led2n = 1'b1
);
    localparam integer HOLD = CLK_HZ / 20;
    reg [22:0] hu = 0, ha = 0;
    wire usb = usb_act || hu != 0;
    wire any = any_act || ha != 0;
    always @(posedge clk) begin
        hu <= usb_act ? HOLD : (hu != 0 ? hu - 1'b1 : hu);
        ha <= any_act ? HOLD : (ha != 0 ? ha - 1'b1 : ha);
    end

    function src(input [3:0] s);
        case (s)
        4'd0:  src = 1'b1;
        4'd1:  src = 1'b0;
        4'd2:  src = a_pwr;
        4'd3:  src = a_pwr | b_pwr;
        4'd4:  src = a_act;
        4'd5:  src = a_act | b_act;
        4'd6:  src = a_pwr ^ a_act;
        4'd7:  src = usb;
        4'd8:  src = any;
        4'd9:  src = !a_act;
        4'd10: src = !(a_act | b_act);
        4'd11: src = !usb;
        4'd12: src = !any;
        4'd13: src = irq_line;
        4'd14: src = !irq_line;
        default: src = b_act;
        endcase
    endfunction

    always @(posedge clk) begin
        led1n <= !(sel_set ? src(sel[3:0]) : def1);
        led2n <= !(sel_set ? src(sel[7:4]) : def2);
    end
endmodule
