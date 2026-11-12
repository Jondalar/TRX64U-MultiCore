// T143 (from cores/c128/mono/stubs, T113 5.4): the menu overlay (host/overlay.v) removed, the video passes through, the registers read 0. Same ports. The generic core application draws on the pixel layer and never writes the overlay registers.
module overlay (
    input  wire        clk_sys,
    input  wire        we,
    input  wire [14:0] addr,
    input  wire [7:0]  wdata,
    output reg  [7:0]  rdata,
    input  wire        clk,
    input  wire        ce,              // T164 WP6 ports of host/overlay.v; the stub passes through one pixel late
    input  wire [11:0] hact,
    input  wire [10:0] vact,
    input  wire [11:0] xact0,
    input  wire [10:0] yact0,
    input  wire [23:0] rgb_in,
    input  wire        de_in, hs_in, vs_in,
    input  wire [11:0] cx_in,
    input  wire [10:0] cy_in,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,
    output reg  [10:0] cy,
    output wire        own_keyboard
);
    assign own_keyboard = 1'b0;
    always @(posedge clk_sys) rdata <= 8'd0;
    always @(posedge clk) if (ce) begin
        rgb <= rgb_in; de <= de_in; hs <= hs_in; vs <= vs_in; cx <= cx_in; cy <= cy_in;
    end
endmodule
