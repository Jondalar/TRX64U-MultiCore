// T157: the joystick port swap of the C128 core. swap comes from the register page (c128_regs.v JOY_SWAP, cpu_clk, quasi static) and is taken
// into the core's clock with two flip-flops; with it set the core's port 1 is the host's joystick 2 and its port 2 the host's joystick 1.
`timescale 1ns / 1ps
module c128_joyswap (
    input  wire       clk,                   // the clock the joystick lines are in (sys_clk)
    input  wire       swap,
    input  wire [4:0] joy1,
    input  wire [4:0] joy2,
    output wire [4:0] joy1_o,
    output wire [4:0] joy2_o
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] sw_s = 2'b00;
    always @(posedge clk) sw_s <= {sw_s[0], swap};
    assign joy1_o = sw_s[1] ? joy2 : joy1;
    assign joy2_o = sw_s[1] ? joy1 : joy2;
endmodule
