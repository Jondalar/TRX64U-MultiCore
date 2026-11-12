// T168: a simulation model of the Xilinx primitive CARRY4 (UG953): four stages, O[i] = S[i] ^ carry-in of the stage, CO[i] = S[i] ? carry-in : DI[i]; the first carry-in
// is CI | CYINIT. Written for this project (TREX); the real primitive is Vivado's. Not read by the synthesis recipe.
`timescale 1ns / 1ps
module CARRY4 (
    output wire [3:0] CO,
    output wire [3:0] O,
    input  wire       CI,
    input  wire       CYINIT,
    input  wire [3:0] DI,
    input  wire [3:0] S
);
    wire c0 = CI | CYINIT;
    assign O[0] = S[0] ^ c0;
    assign CO[0] = S[0] ? c0 : DI[0];
    assign O[1] = S[1] ^ CO[0];
    assign CO[1] = S[1] ? CO[0] : DI[1];
    assign O[2] = S[2] ^ CO[1];
    assign CO[2] = S[2] ? CO[1] : DI[2];
    assign O[3] = S[3] ^ CO[2];
    assign CO[3] = S[3] ? CO[2] : DI[3];
endmodule
