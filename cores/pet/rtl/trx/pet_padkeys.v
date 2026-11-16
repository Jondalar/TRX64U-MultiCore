// T216 P4 (GitHub #80): USB pads and the joystick ports as keys. The five buttons (up, down, left, right, fire) of both USB pads (the app's pad service writes
// their words, pet_regs 160-163) and of both joystick ports (the host's c64_joy1 / c64_joy2) are one set of five; each button presses the C64 key its code names
// (pet_regs 164-168: b5:0 the key's index in the host's key matrix col * 8 + row, b6 also the C64 SHIFT, 0xFF none), ORed into the host's key matrix before
// pet_keys turns that into the PET's: so the PET key that results follows the keyboard mapping and the model's keyboard like a typed key.
// The block is the PET's own copy of a generic service (coreapp/src/padkeys.h declares the app's side; any core that takes c64_keys can carry the same module).
`timescale 1ns / 1ps
module pet_padkeys (
    input  wire        clk,
    input  wire [63:0] keys,
    input  wire [15:0] pad1,                // cpu_clk side, quasi static
    input  wire [15:0] pad2,
    input  wire [4:0]  joy1,                // the host's joystick lines, 1 = pressed (b0 up, b1 down, b2 left, b3 right, b4 fire)
    input  wire [4:0]  joy2,
    input  wire [39:0] code,
    output wire [63:0] keys_o
);
    (* ASYNC_REG = "TRUE" *) reg [4:0] p1a = 5'd0, p1b = 5'd0, p2a = 5'd0, p2b = 5'd0, j1a = 5'd0, j1b = 5'd0, j2a = 5'd0, j2b = 5'd0;
    (* ASYNC_REG = "TRUE" *) reg [39:0] ca = {5{8'hFF}}, cb = {5{8'hFF}};
    always @(posedge clk) begin
        p1a <= pad1[4:0]; p1b <= p1a;
        p2a <= pad2[4:0]; p2b <= p2a;
        j1a <= joy1;      j1b <= j1a;
        j2a <= joy2;      j2b <= j2a;
        ca <= code;       cb <= ca;
    end
    wire [4:0] act = p1b | p2b | j1b | j2b;
    reg [63:0] ovl;
    reg        shf;
    integer i;
    always @* begin
        ovl = 64'd0;
        shf = 1'b0;
        for (i = 0; i < 5; i = i + 1) begin
            if (act[i] && cb[8 * i +: 8] != 8'hFF) begin
                ovl[cb[8 * i +: 6]] = 1'b1;
                if (cb[8 * i + 6]) shf = 1'b1;
            end
        end
    end
    assign keys_o = keys | ovl | {48'd0, shf, 15'd0};
endmodule
