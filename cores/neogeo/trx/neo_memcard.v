// T143: the memory card of the cartridge machine (2 KB, the card of one game) as 1024 words in two dual-port RAMs; the interface of rtl/mem/memcard.v of the upstream
// (Sean 'Furrtek' Gonsalves, GPL v2 or later) without the Neo Geo CD's 8 KB mode: the upstream instance has 4096 words in each RAM, four times what the 2 KB card needs,
// and takes two block RAM tiles where this takes one. The second port is the image mover's (neo_mem.v), 16 bit words, memory_addr[9:0].
`timescale 1ns / 1ps
module neo_memcard (
    input  wire        CLK,
    input  wire [9:0]  CDA,                // CDA[10:1] of the machine's card address
    input  wire        CDA0,               // CDA[0]: the byte lane
    input  wire        CARD_WE,
    input  wire [7:0]  M68K_DATA,
    output wire [7:0]  CDD,
    input  wire        clk_sys,
    input  wire [9:0]  memcard_addr,
    input  wire        memcard_wr,
    input  wire [15:0] memcard_din,
    output wire [15:0] memcard_dout
);
    wire [7:0] CDD_L, CDD_U;
    dpram #(.ADDRWIDTH(10)) MEMCARDL(
        .clock_a(CLK), .address_a(CDA), .wren_a(CARD_WE & CDA0), .data_a(M68K_DATA), .q_a(CDD_L),
        .clock_b(clk_sys), .address_b(memcard_addr), .wren_b(memcard_wr), .data_b(memcard_din[7:0]), .q_b(memcard_dout[7:0]));
    dpram #(.ADDRWIDTH(10)) MEMCARDU(
        .clock_a(CLK), .address_a(CDA), .wren_a(CARD_WE & ~CDA0), .data_a(M68K_DATA), .q_a(CDD_U),
        .clock_b(clk_sys), .address_b(memcard_addr), .wren_b(memcard_wr), .data_b(memcard_din[15:8]), .q_b(memcard_dout[15:8]));
    assign CDD = CDA0 ? CDD_L : CDD_U;
endmodule
