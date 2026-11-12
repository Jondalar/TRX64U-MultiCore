// T195: the board's three buttons UI_BUTTON[2:0] (header pins P17 / R18 / N18, pull-ups in pins.xdc: a press pulls the pin low).
// Per pin: a 2-FF synchroniser into clk, active low turned into active high, and a debounce: the level must stay the same for DEB_MS
// ticks of tick_1ms (one clk-wide pulse per millisecond, ultimate_sys tick_1kHz) before btn follows - for the press and for the release.
// btn[0] = left, btn[1] = middle, btn[2] = right (Gideon's order, ITU bits 5/6/7).
module ui_buttons #(parameter DEB_MS = 20) (
    input  wire       clk,
    input  wire       tick_1ms,
    input  wire [2:0] pin_n,        // the pins, low = pressed
    output wire [2:0] btn           // debounced, high = pressed
);
    ui_deb #(.DEB_MS(DEB_MS)) d0 (.clk(clk), .tick_1ms(tick_1ms), .pin_n(pin_n[0]), .btn(btn[0]));
    ui_deb #(.DEB_MS(DEB_MS)) d1 (.clk(clk), .tick_1ms(tick_1ms), .pin_n(pin_n[1]), .btn(btn[1]));
    ui_deb #(.DEB_MS(DEB_MS)) d2 (.clk(clk), .tick_1ms(tick_1ms), .pin_n(pin_n[2]), .btn(btn[2]));
endmodule

// One button: counts the ms the synchronised level differs from btn; a ms in which it agrees again starts the count over.
module ui_deb #(parameter DEB_MS = 20) (
    input  wire clk,
    input  wire tick_1ms,
    input  wire pin_n,
    output reg  btn = 1'b0
);
    reg s0 = 1'b0, s1 = 1'b0;
    reg [4:0] cnt = 5'd0;
    always @(posedge clk) begin
        s0 <= ~pin_n;
        s1 <= s0;
        if (s1 == btn) cnt <= 5'd0;
        else if (tick_1ms) begin
            if (cnt == DEB_MS - 1) begin cnt <= 5'd0; btn <= s1; end
            else cnt <= cnt + 5'd1;
        end
    end
endmodule
