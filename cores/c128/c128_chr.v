// T113 §5.7: the C128's character ROMs as a store of their own - 8 KiB, two sets of 4 KiB (set 0: the C64's, bank 16 of the system ROM image; set 1: the
// C128's, bank 17) in two block RAMs (RAMB36 each, 4K x 8). The VIC reads its glyph bytes here in the clock after the address (like RAM), never from the ROM
// line cache: a cache miss may hold the CPU, it must not hold the VIC (spec 5.7). The CPU reads them here too (the character ROM visible at $D000 for
// the CPU).
//
// Write side (wclk = cpu_clk): the application writes the 8 KiB through the register page (c128_regs.v CHR_ADDR / CHR_DATA) while it loads the system
// ROM image; the byte at index i is image byte 0x10000 + i (i[12] = the set). The write port reads too (wq: the byte at the write address, one clock
// later), so the application can read the store back. Read side (rclk = the core's clock): ra[12] = set, rq = the byte at ra, valid in the next clock.
// The block RAMs start empty in the bitstream (no ROM byte of any machine is in it); a simulation (TRX_SIM_ROMS) preloads them from mem/c128_rom.mem.
`timescale 1ns / 1ps
module c128_chr (
    input  wire        wclk,
    input  wire        we,
    input  wire [12:0] wa,
    input  wire  [7:0] wd,
    output wire  [7:0] wq,
    input  wire        rclk,
    input  wire [12:0] ra,
    output wire  [7:0] rq
);
    (* ram_style = "block" *) reg [7:0] m0 [0:4095];
    (* ram_style = "block" *) reg [7:0] m1 [0:4095];
`ifdef TRX_SIM_ROMS
    reg [15:0] cimg [0:36863];
    integer ci;
    initial begin
        $readmemh("mem/c128_rom.mem", cimg);
        for (ci = 0; ci < 2048; ci = ci + 1) begin
            m0[2 * ci]     = cimg[32768 + ci][7:0];
            m0[2 * ci + 1] = cimg[32768 + ci][15:8];
            m1[2 * ci]     = cimg[34816 + ci][7:0];
            m1[2 * ci + 1] = cimg[34816 + ci][15:8];
        end
    end
`endif
    reg  [7:0] wq0, wq1, rq0, rq1;
    reg        wsel = 1'b0, rsel = 1'b0;
    always @(posedge wclk) begin
        if (we && !wa[12]) m0[wa[11:0]] <= wd;
        if (we &&  wa[12]) m1[wa[11:0]] <= wd;
        wq0  <= m0[wa[11:0]];
        wq1  <= m1[wa[11:0]];
        wsel <= wa[12];
    end
    always @(posedge rclk) begin
        rq0  <= m0[ra[11:0]];
        rq1  <= m1[ra[11:0]];
        rsel <= ra[12];
    end
    assign wq = wsel ? wq1 : wq0;
    assign rq = rsel ? rq1 : rq0;
endmodule
