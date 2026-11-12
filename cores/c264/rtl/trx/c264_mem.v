// T90: the machine's memories, all inferred block RAM, empty at synthesis and filled by the application (no ROM image is
// in the bitstream): 64 KB RAM, and six 16 KB arrays: KERNAL, BASIC, function low/high, cartridge low/high.
//   machine port  the TED machine's address (sys_clk): read every clock, the output holds between accesses (the Nano's
//                 Gowin blocks did the same); the write is one clock (m_we) at the address and data of the CPU
//   host window   write, and read with the data one clock after the address (the DMA window and the JTAG port)
// RAM_SIZE: size64 = 0 is the C16's 16 KB, mirrored through the 64 KB space (A14/A15 ignored), for both ports.
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps

module c264_rom16 (
    input  wire        clk,
    input  wire [13:0] a_addr,
    output reg  [7:0]  a_q = 8'd0,
    input  wire        b_wr,
    input  wire [13:0] b_addr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:16383];
    always @(posedge clk) a_q <= mem[a_addr];
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

module c264_ram64 (
    input  wire        clk,
    input  wire [15:0] a_addr,
    input  wire        a_wr,
    input  wire [7:0]  a_d,
    output reg  [7:0]  a_q = 8'd0,
    input  wire        b_wr,
    input  wire [15:0] b_addr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:65535];
    always @(posedge clk) begin
        if (a_wr) mem[a_addr] <= a_d;
        a_q <= mem[a_addr];
    end
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

module c264_mem (
    input  wire        clk,
    input  wire [15:0] m_addr,
    input  wire [7:0]  m_wdata,
    input  wire        m_we,
    input  wire        size64,
    output wire [7:0]  ram_q,
    output wire [7:0]  kern_q,
    output wire [7:0]  basic_q,
    output wire [7:0]  flo_q,
    output wire [7:0]  fhi_q,
    output wire [7:0]  clo_q,
    output wire [7:0]  chi_q,
    input  wire        w_wr,
    input  wire        w_hit,           // the access is in an existing array (an absent aperture reads 0 and writes nothing)
    input  wire        w_rom,           // 0: the RAM, 1: the array w_sel
    input  wire [2:0]  w_sel,
    input  wire [15:0] w_ai,
    input  wire [7:0]  w_di,
    output wire [7:0]  w_do
);
    wire [15:0] ma = size64 ? m_addr : {2'b00, m_addr[13:0]};
    wire [15:0] wa = size64 ? w_ai   : {2'b00, w_ai[13:0]};
    wire [7:0]  ram_b, kb, bb, fl_b, fh_b, cl_b, ch_b;

    c264_ram64 ram (.clk(clk), .a_addr(ma), .a_wr(m_we), .a_d(m_wdata), .a_q(ram_q),
                    .b_wr(w_wr & w_hit & ~w_rom), .b_addr(wa), .b_d(w_di), .b_q(ram_b));

    wire [5:0] sel = w_hit & w_rom ? (6'd1 << w_sel) : 6'd0;
    c264_rom16 kern  (.clk(clk), .a_addr(m_addr[13:0]), .a_q(kern_q),  .b_wr(w_wr & sel[0]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(kb));
    c264_rom16 basic (.clk(clk), .a_addr(m_addr[13:0]), .a_q(basic_q), .b_wr(w_wr & sel[1]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(bb));
    c264_rom16 funcl (.clk(clk), .a_addr(m_addr[13:0]), .a_q(flo_q),   .b_wr(w_wr & sel[2]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(fl_b));
    c264_rom16 funch (.clk(clk), .a_addr(m_addr[13:0]), .a_q(fhi_q),   .b_wr(w_wr & sel[3]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(fh_b));
    c264_rom16 cartl (.clk(clk), .a_addr(m_addr[13:0]), .a_q(clo_q),   .b_wr(w_wr & sel[4]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(cl_b));
    c264_rom16 carth (.clk(clk), .a_addr(m_addr[13:0]), .a_q(chi_q),   .b_wr(w_wr & sel[5]), .b_addr(w_ai[13:0]), .b_d(w_di), .b_q(ch_b));

    // the window's read data, one clock after the address
    reg       hit_q = 1'b0, rom_q = 1'b0;
    reg [2:0] sel_q = 3'd0;
    always @(posedge clk) begin hit_q <= w_hit; rom_q <= w_rom; sel_q <= w_sel; end
    reg [7:0] d;
    always @* begin
        d = 8'd0;
        if (hit_q) begin
            if (!rom_q) d = ram_b;
            else case (sel_q)
                3'd0: d = kb;
                3'd1: d = bb;
                3'd2: d = fl_b;
                3'd3: d = fh_b;
                3'd4: d = cl_b;
                3'd5: d = ch_b;
                default: d = 8'd0;
            endcase
        end
    end
    assign w_do = d;
endmodule
