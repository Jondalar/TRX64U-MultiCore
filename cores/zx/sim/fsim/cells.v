// T119 5.15: plain models of the 7-series primitives a Vivado funcsim netlist of zx_core uses besides the block RAMs (those are the
// vendor's RAMB36E1 from unisims, which Verilator reads). Xilinx's own models of these cells use tri-state inputs (tri1) that Verilator
// refuses at a top-level instance; the functions are the documented ones (UG953): LUTn = INIT[{I(n-1)..I0}], MUXF7/8 = S ? I1 : I0, the
// flip-flops with synchronous R/S or asynchronous CLR/PRE, CARRY4 = four stages of S ? carry : DI with O = S ^ carry-in.
`timescale 1ns / 1ps
module GND (output G); assign G = 1'b0; endmodule
module VCC (output P); assign P = 1'b1; endmodule
module LUT1 #(parameter [1:0] INIT = 2'h0) (output O, input I0); assign O = INIT[I0]; endmodule
module LUT2 #(parameter [3:0] INIT = 4'h0) (output O, input I0, I1); assign O = INIT[{I1, I0}]; endmodule
module LUT3 #(parameter [7:0] INIT = 8'h0) (output O, input I0, I1, I2); assign O = INIT[{I2, I1, I0}]; endmodule
module LUT4 #(parameter [15:0] INIT = 16'h0) (output O, input I0, I1, I2, I3); assign O = INIT[{I3, I2, I1, I0}]; endmodule
module LUT5 #(parameter [31:0] INIT = 32'h0) (output O, input I0, I1, I2, I3, I4); assign O = INIT[{I4, I3, I2, I1, I0}]; endmodule
module LUT6 #(parameter [63:0] INIT = 64'h0) (output O, input I0, I1, I2, I3, I4, I5); assign O = INIT[{I5, I4, I3, I2, I1, I0}]; endmodule
module MUXF7 (output O, input I0, I1, S); assign O = S ? I1 : I0; endmodule
module MUXF8 (output O, input I0, I1, S); assign O = S ? I1 : I0; endmodule
module FDRE #(parameter [0:0] INIT = 1'b0, parameter [0:0] IS_C_INVERTED = 1'b0, parameter [0:0] IS_D_INVERTED = 1'b0, parameter [0:0] IS_R_INVERTED = 1'b0)
    (output reg Q = INIT, input C, CE, D, R);
    always @(posedge C) if (R ^ IS_R_INVERTED) Q <= 1'b0; else if (CE) Q <= D ^ IS_D_INVERTED;
endmodule
module FDSE #(parameter [0:0] INIT = 1'b1, parameter [0:0] IS_C_INVERTED = 1'b0, parameter [0:0] IS_D_INVERTED = 1'b0, parameter [0:0] IS_S_INVERTED = 1'b0)
    (output reg Q = INIT, input C, CE, D, S);
    always @(posedge C) if (S ^ IS_S_INVERTED) Q <= 1'b1; else if (CE) Q <= D ^ IS_D_INVERTED;
endmodule
module FDCE #(parameter [0:0] INIT = 1'b0, parameter [0:0] IS_C_INVERTED = 1'b0, parameter [0:0] IS_D_INVERTED = 1'b0, parameter [0:0] IS_CLR_INVERTED = 1'b0)
    (output reg Q = INIT, input C, CE, D, CLR);
    always @(posedge C or posedge CLR) if (CLR ^ IS_CLR_INVERTED) Q <= 1'b0; else if (CE) Q <= D ^ IS_D_INVERTED;
endmodule
module FDPE #(parameter [0:0] INIT = 1'b1, parameter [0:0] IS_C_INVERTED = 1'b0, parameter [0:0] IS_D_INVERTED = 1'b0, parameter [0:0] IS_PRE_INVERTED = 1'b0)
    (output reg Q = INIT, input C, CE, D, PRE);
    always @(posedge C or posedge PRE) if (PRE ^ IS_PRE_INVERTED) Q <= 1'b1; else if (CE) Q <= D ^ IS_D_INVERTED;
endmodule
module CARRY4 (output [3:0] CO, O, input CI, CYINIT, input [3:0] DI, S);
    wire c0 = CI | CYINIT;
    assign CO[0] = S[0] ? c0 : DI[0];
    assign CO[1] = S[1] ? CO[0] : DI[1];
    assign CO[2] = S[2] ? CO[1] : DI[2];
    assign CO[3] = S[3] ? CO[2] : DI[3];
    assign O = S ^ {CO[2:0], c0};
endmodule
