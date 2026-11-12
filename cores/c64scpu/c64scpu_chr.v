// The character ROM as a store of its own - 4 KiB in one block RAM (RAMB36, 4K x 8; character ROM image of the C64, 901225-01). The VIC reads its glyph
// bytes here in the clock after the address (like RAM), never from the ROM line cache: a cache miss may hold the CPU, it must not hold the VIC.
// The CPU reads them here too (the character ROM visible at $D000 for the CPU).
//
// Write side (wclk = cpu_clk): the application writes the 4 KiB through the register page (c64scpu_regs.v CHR_ADDR / CHR_DATA) while it loads the
// ROMs; the byte at index i is character ROM byte i. The write port reads too (wq: the byte at the write address, one clock later), so the application
// can read the store back. Read side (rclk = the core's clock): rq = the byte at ra, valid in the next clock.
// The block RAM starts empty in the bitstream (no ROM byte of any machine is in it); a simulation (TRX_SIM_ROMS) preloads it from mem/c64scpu_rom.mem
// (bytes 0x4000-0x4FFF of the bench image, see tools/gen_c64scpu_test.py).
`timescale 1ns / 1ps
module c64scpu_chr (
    input  wire        wclk,
    input  wire        we,
    input  wire [11:0] wa,
    input  wire  [7:0] wd,
    output reg   [7:0] wq,
    input  wire        rclk,
    input  wire [11:0] ra,
    output reg   [7:0] rq
);
    (* ram_style = "block" *) reg [7:0] m [0:4095];
`ifdef TRX_SIM_ROMS
    reg [15:0] cimg [0:10239];
    integer ci;
    initial begin
        $readmemh("mem/c64scpu_rom.mem", cimg);
        for (ci = 0; ci < 2048; ci = ci + 1) begin
            m[2 * ci]     = cimg[8192 + ci][7:0];
            m[2 * ci + 1] = cimg[8192 + ci][15:8];
        end
    end
`endif
    always @(posedge wclk) begin
        if (we) m[wa] <= wd;
        wq <= m[wa];
    end
    always @(posedge rclk) rq <= m[ra];
endmodule
