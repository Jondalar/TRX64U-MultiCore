// T91 D5: the Spectrum's memories, all inferred block RAM, empty at synthesis and filled by the application (no ROM image is in the
// bitstream): 128 KB RAM (8 banks of 16 KB, true dual port) and one 32 KB ROM area (true dual port).
//   RAM port A  the CPU: address {bank, a[13:0]}, write, read data one clock after the address
//   RAM port B  the ULA's screen fetch (bank 5 or 7, replaces the MiSTer `vram` mirror) AND the app's window, time-shared: the ULA's
//               address is valid in the first two clocks after a pixel enable (its address changes only on one and it is sampled on
//               the next, at least 4 clocks later), so the window may take port B from clock 3 on (`ph` >= 2). The ULA's byte is kept
//               in `ula_q`; the window never sees the ULA's data and the ULA never sees the window's.
//   ROM port A  the app's window (read and write); port B the CPU's read
// The CPU cannot write the ROM area (the write enable is the caller's: a write above $3FFF only), as on the machine.
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zx_mem (
    input  wire        clk,
    input  wire [2:0]  ph,                      // clocks since the last pixel enable minus 1, saturating at 7
    // CPU
    input  wire [16:0] cpu_ra,
    input  wire        cpu_we,
    input  wire [7:0]  cpu_d,
    output reg  [7:0]  ram_q = 8'd0,
    input  wire [14:0] rom_a,
    output reg  [7:0]  rom_q = 8'd0,
    // ULA
    input  wire [16:0] ula_a,
    output reg  [7:0]  ula_q = 8'd0,
    // window
    input  wire        w_start,                 // one clock
    input  wire        w_we,
    input  wire        w_rom,
    input  wire [16:0] w_a,
    input  wire [7:0]  w_di,
    output wire        busy,                    // a window access is waiting for its slot
    output reg         w_done = 1'b0,           // one clock; w_do is valid with it
    output reg  [7:0]  w_do = 8'd0
);
    (* ram_style = "block" *) reg [7:0] ram [0:131071];
    (* ram_style = "block" *) reg [7:0] rom [0:32767];

    // ---- window request
    reg        pend = 1'b0, p_we = 1'b0, p_rom = 1'b0;
    reg [16:0] p_a = 17'd0;
    reg [7:0]  p_d = 8'd0;
    assign busy = pend;
    wire       go = pend & (p_rom | (ph >= 3'd2));
    reg        go_q = 1'b0, go_rom_q = 1'b0;
    always @(posedge clk) begin
        if (w_start) begin pend <= 1'b1; p_we <= w_we; p_rom <= w_rom; p_a <= w_a; p_d <= w_di; end
        go_q <= go;
        go_rom_q <= go & p_rom;
        if (go) pend <= 1'b0;
    end

    // ---- RAM
    wire [16:0] b_a  = (go & ~p_rom) ? p_a : ula_a;
    wire        b_we = go & ~p_rom & p_we;
    reg  [7:0]  b_q = 8'd0;
    always @(posedge clk) begin
        if (cpu_we) ram[cpu_ra] <= cpu_d;
        ram_q <= ram[cpu_ra];
    end
    always @(posedge clk) begin
        if (b_we) ram[b_a] <= p_d;
        b_q <= ram[b_a];
    end

    // ---- ROM
    reg [7:0] rom_wq = 8'd0;
    always @(posedge clk) begin
        if (go & p_rom & p_we) rom[p_a[14:0]] <= p_d;
        rom_wq <= rom[p_a[14:0]];
    end
    always @(posedge clk) rom_q <= rom[rom_a];

    // ---- results
    always @(posedge clk) begin
        if (ph == 3'd1 || ph == 3'd2) ula_q <= b_q;       // the ULA's address was applied in the clock before
        w_done <= go_q;
        if (go_q) w_do <= p_we ? 8'h00 : (go_rom_q ? rom_wq : b_q);
    end
endmodule
