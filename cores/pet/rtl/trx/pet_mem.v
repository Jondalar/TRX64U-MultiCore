// T134: the PET's memories. Written for this project (TREX), no upstream code. Every array is a block RAM with a registered read (the data is
// there one clock after the address, which is how the machine's bus expects it: the CPU holds an address for at least three clocks).
//   ram   32 KB   $0000-$7FFF   CPU on port A, the app's window on port B
//   vram   2 KB   $8000-$8FFF   CPU on port A (mirrored by the caller), the picture on port B
//   rom   32 KB   $8000-$FFFF   (only $9000-$E7FF and $F000-$FFFF are read by the machine)  CPU port A, the app's window on port B (rom_sel 0/1)
//   chr    4 KB   character ROM   the picture on port A, the app's window on port B (rom_sel 2): four 1 KiB sets, graphics and lower case twice; the upper pair is the SuperPET's
//                 Waterloo ASCII / APL set (CRTC R12 bit 5 = MA13 selects it, T216 P6), a 2 KiB file leaves the upper pair zero
//   rom09 24 KB   the Waterloo ROMs for the 6809, CPU port A, the app's window on port B (rom_sel 3: $A000-$DFFF, rom_sel 8: $E000-$FFFF)
`timescale 1ns / 1ps
module pet_ram32 (
    input  wire        clk,
    input  wire [14:0] a_addr,
    input  wire        a_wr,
    input  wire [7:0]  a_d,
    output reg  [7:0]  a_q = 8'd0,
    input  wire        b_wr,
    input  wire [14:0] b_addr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:32767];
    always @(posedge clk) begin
        if (a_wr) mem[a_addr] <= a_d;
        a_q <= mem[a_addr];
    end
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

module pet_ram64 (
    input  wire        clk,
    input  wire [15:0] addr,
    input  wire        we,
    input  wire [7:0]  d,
    output reg  [7:0]  q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:65535];
    always @(posedge clk) begin
        if (we) mem[addr] <= d;
        q <= mem[addr];
    end
endmodule

module pet_rom (
    input  wire        clk,
    input  wire [14:0] a_addr,
    output reg  [7:0]  a_q = 8'd0,
    input  wire        b_wr,
    input  wire [14:0] b_addr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:32767];
    always @(posedge clk) a_q <= mem[a_addr];
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

// the SuperPET's Waterloo ROMs: $A000-$FFFF as the 6809 sees them, 24 KB (the I/O range inside is never read), byte address = machine address - $A000
module pet_rom09 (
    input  wire        clk,
    input  wire [14:0] a_addr,
    output reg  [7:0]  a_q = 8'd0,
    input  wire        b_wr,
    input  wire [14:0] b_addr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:24575];
    always @(posedge clk) a_q <= mem[a_addr];
    always @(posedge clk) begin
        if (b_wr && b_addr < 15'd24576) mem[b_addr] <= b_d;
        b_q <= (b_addr < 15'd24576) ? mem[b_addr] : 8'h00;
    end
endmodule

module pet_ram4k (
    input  wire        clk,
    input  wire [11:0] a_addr,
    input  wire        a_wr,
    input  wire [7:0]  a_d,
    output reg  [7:0]  a_q = 8'd0,
    input  wire [11:0] b_addr,
    input  wire        b_wr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:4095];
    always @(posedge clk) begin
        if (a_wr) mem[a_addr] <= a_d;
        a_q <= mem[a_addr];
    end
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

module pet_ram2k (
    input  wire        clk,
    input  wire [10:0] a_addr,
    input  wire        a_wr,
    input  wire [7:0]  a_d,
    output reg  [7:0]  a_q = 8'd0,
    input  wire [10:0] b_addr,
    input  wire        b_wr,
    input  wire [7:0]  b_d,
    output reg  [7:0]  b_q = 8'd0
);
    (* ram_style = "block" *) reg [7:0] mem [0:2047];
    always @(posedge clk) begin
        if (a_wr) mem[a_addr] <= a_d;
        a_q <= mem[a_addr];
    end
    always @(posedge clk) begin
        if (b_wr) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

module pet_mem (
    input  wire        clk,
    // the machine
    input  wire [14:0] m_addr,              // RAM address
    input  wire [7:0]  m_wdata,
    input  wire        m_we,
    output wire [7:0]  ram_q,
    input  wire [14:0] r_addr,              // ROM space address (CPU address - $8000)
    output wire [7:0]  rom_q,
    input  wire [10:0] vc_addr,             // video RAM, the CPU's
    input  wire        vc_we,
    output wire [7:0]  vc_q,
    input  wire [10:0] vv_addr,             // video RAM, the picture's
    output wire [7:0]  vv_q,
    input  wire [11:0] ch_addr,             // character ROM, the picture's
    output wire [7:0]  ch_q,
    // the app's window (pet_win)
    input  wire        w_wr,
    input  wire        w_hit,
    input  wire        w_rom,
    input  wire [3:0]  w_sel,
    input  wire [14:0] r9_addr,             // the 6809's ROM address (CPU address - $A000)
    output wire [7:0]  rom9_q,
    input  wire [15:0] w_ai,
    input  wire [7:0]  w_di,
    output wire [7:0]  w_do
);
    wire [7:0] ram_b, rom_b, chr_b;
    wire       hit_ram = w_hit & ~w_rom;
    wire       hit_rom = w_hit & w_rom & (w_sel[3:1] == 3'b000);
    wire       hit_chr = w_hit & w_rom & (w_sel == 4'd2);
    wire       hit_w09 = w_hit & w_rom & (w_sel == 4'd3 || w_sel == 4'd8);          // the Waterloo ROM: 3 = $A000-$DFFF, 8 = $E000-$FFFF (8 KB)
    wire [7:0] w09_b;
    pet_rom09 rom09 (.clk(clk), .a_addr(r9_addr), .a_q(rom9_q),
                     .b_wr(w_wr & hit_w09), .b_addr(w_sel[3] ? {2'b10, w_ai[12:0]} : {1'b0, w_ai[13:0]}), .b_d(w_di), .b_q(w09_b));
    pet_ram32 ram (.clk(clk), .a_addr(m_addr), .a_wr(m_we), .a_d(m_wdata), .a_q(ram_q),
                   .b_wr(w_wr & hit_ram), .b_addr(w_ai[14:0]), .b_d(w_di), .b_q(ram_b));
    pet_rom rom (.clk(clk), .a_addr(r_addr), .a_q(rom_q),
                 .b_wr(w_wr & hit_rom), .b_addr({w_sel[0], w_ai[13:0]}), .b_d(w_di), .b_q(rom_b));
    pet_ram2k vram (.clk(clk), .a_addr(vc_addr), .a_wr(vc_we), .a_d(m_wdata), .a_q(vc_q),
                    .b_addr(vv_addr), .b_wr(1'b0), .b_d(8'd0), .b_q(vv_q));
    pet_ram4k chr (.clk(clk), .a_addr(ch_addr), .a_wr(1'b0), .a_d(8'd0), .a_q(ch_q),
                   .b_addr(w_ai[11:0]), .b_wr(w_wr & hit_chr), .b_d(w_di), .b_q(chr_b));
    reg [3:0] sel_q = 4'd0;
    reg       hit_q = 1'b0, rom_sel_q = 1'b0;
    always @(posedge clk) begin
        hit_q <= w_hit;
        rom_sel_q <= w_rom;
        sel_q <= w_sel;
    end
    assign w_do = !hit_q ? 8'h00 : !rom_sel_q ? ram_b : (sel_q == 4'd3 || sel_q == 4'd8) ? w09_b : (sel_q[3] | sel_q[2]) ? 8'h00 : (sel_q == 4'd2) ? chr_b : rom_b;   // sel 4-7 is the drives' ROM space: write only
endmodule
