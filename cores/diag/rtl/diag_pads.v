// T175 / T260: the pads of the diag core. diag_pads: the two joystick ports (pins 1-4 and 6 of each, KBJOY_1_JOY / KBJOY_2_JOY): open drain. low[i] = 1 drives the pin low, anything else releases it, which is
// the input the host always had (the PULLUP stays in pins.xdc); `i` is the pad's level in both cases. A released pin is the old pure input; the pad is never driven high. While the
// host decouples the core (core_decouple) every pin is released, as the other pads of top.v are.
`timescale 1ns / 1ps
module diag_pads (
    inout  wire [4:0] KBJOY_1_JOY,
    inout  wire [4:0] KBJOY_2_JOY,
    input  wire [9:0] low,
    input  wire       decouple,
    output wire [4:0] j1_i,
    output wire [4:0] j2_i
);
    genvar k;
    generate for (k = 0; k < 5; k = k + 1) begin : g
        IOBUF b_j1 (.IO(KBJOY_1_JOY[k]), .I(1'b0), .T(decouple | ~low[k]),     .O(j1_i[k]));
        IOBUF b_j2 (.IO(KBJOY_2_JOY[k]), .I(1'b0), .T(decouple | ~low[k + 5]), .O(j2_i[k]));
    end endgenerate
endmodule

// T260: the pads of the cartridge port's address, data and R/W lines (the bank's pz and pzh pins). Written for this project (TREX). Everything the host's top had at these pads is kept:
// `l2_*` are layer 2's own values (idle: A0-A11 driven 1, the rest released), already forced idle by the host's expansion-port lock (port_off); while the engine drives a pin (en, o) it
// replaces them, except under port_off, where the pads are exactly the host's idle (A0-A11 driven 1, the others high-Z). z releases A0-A11 to high-Z (the engine's HIZ, to read the port side
// behind the address transceiver). The levels read back (a_i, d_i, rw_i) are the pads' in every case.
module diag_slot_pads (
    inout  wire [15:0] SLOT_ADDR,
    inout  wire [7:0]  SLOT_DATA,
    inout  wire        SLOT_RWn,
    input  wire        port_off,
    input  wire [15:0] l2_a,
    input  wire        l2_ahi_t,           // A12-A15 and R/W: 1 = released
    input  wire [7:0]  l2_d_o,
    input  wire        l2_d_t,
    input  wire        l2_rw,
    input  wire [15:0] a_en, a_o, a_z,
    input  wire [7:0]  d_en, d_o,
    input  wire        rw_en, rw_o,
    output wire [15:0] a_i,
    output wire [7:0]  d_i,
    output wire        rw_i
);
    genvar k;
    generate
        for (k = 0; k < 12; k = k + 1) begin : lo
            IOBUF b_a (.IO(SLOT_ADDR[k]), .I(port_off ? 1'b1 : a_en[k] ? a_o[k] : l2_a[k]), .T(~port_off & a_z[k]), .O(a_i[k]));
        end
        for (k = 12; k < 16; k = k + 1) begin : hi
            wire e = a_en[k] & ~port_off;
            IOBUF b_a (.IO(SLOT_ADDR[k]), .I(e ? a_o[k] : l2_a[k]), .T(e ? 1'b0 : l2_ahi_t), .O(a_i[k]));
        end
        for (k = 0; k < 8; k = k + 1) begin : dat
            wire e = d_en[k] & ~port_off;
            IOBUF b_d (.IO(SLOT_DATA[k]), .I(e ? d_o[k] : l2_d_o[k]), .T(e ? 1'b0 : l2_d_t), .O(d_i[k]));
        end
    endgenerate
    wire erw = rw_en & ~port_off;
    IOBUF b_rw (.IO(SLOT_RWn), .I(erw ? rw_o : l2_rw), .T(erw ? 1'b0 : l2_ahi_t), .O(rw_i));
endmodule
