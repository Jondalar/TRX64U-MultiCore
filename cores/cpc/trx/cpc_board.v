// T92 D3: the CPC's motherboard - Z80, CRTC, Gate Array, MMU, PPI and the AY - wired the way MiSTer-devel/Amstrad_MiSTer's
// rtl/Amstrad_motherboard.v wires them ((C) 2018 Sorgelig, CoreAmstrad r004; no licence text in that file, so this derived wiring is
// licence as the file it follows, cores/cpc/rtl/NOTICE). What it leaves out of the original: the PS/2 keyboard and joystick block
// (hid.sv: the matrix comes in as `mat` from the shared key translator), crt_filter (the sync filter is off: the pixel writer works on the Gate
// Array's own HSYNC/VSYNC), the CPC snapshot ports (T92 Out), the expansion port (Multiface 2, Dandanator, PlayCity: Out). The Z80 (T80pa) and
// the AY (YM2149) are the Spectrum core's, shared (cores/zx). The memory is outside: this module hands out the MMU's address and the strobes
// and takes the byte back (cpc_core.v), exactly as the original hands them to the SDRAM controller.
`timescale 1ns / 1ps
module cpc_board (
    input  wire         reset,
    input  wire         clk,                    // sys_clk
    input  wire         ce_16,                  // the Gate Array's 16 MHz as an enable

    input  wire [79:0]  mat,                    // the keyboard matrix, 10 lines x 8 bits, 1 = key down (joystick lines 9 and 6 included)
    input  wire         crtc_type,
    input  wire [3:0]   ppi_jumpers,

    input  wire         tape_in,
    output wire         tape_out,
    output wire         tape_motor,

    output wire [7:0]   ay_a,
    output wire [7:0]   ay_b,
    output wire [7:0]   ay_c,

    output wire [1:0]   mode,
    output wire [1:0]   red,
    output wire [1:0]   green,
    output wire [1:0]   blue,
    output wire         hsync,
    output wire         vsync,
    output wire         crtc_de,

    input  wire [15:0]  vram_din,
    output reg  [14:0]  vram_addr,

    input  wire         has_amsdos,             // the upper ROM 7 exists (664, 6128)
    input  wire         ram64k,                 // 464 / 664: no RAM paging
    output wire [22:0]  mem_addr,
    output wire         mem_rd,
    output wire         mem_wr,
    output wire         romen,
    output wire         phi_en_n,
    output wire         phi_en_p,
    output wire [15:0]  cpu_addr,
    output wire [7:0]   cpu_dout,
    input  wire [7:0]   cpu_din,
    output wire         iorq,
    output wire         io_rd,
    output wire         io_wr,
    output wire         cclk_en_n,
    output wire         cclk_en_p,
    output wire         ga_ready,
    output wire [15:0]  cpu_pc_dbg,
    output wire         cpu_m1
);
    wire [15:0] A;
    wire  [7:0] D;
    wire RD_n, WR_n, MREQ_n, IORQ_n, RFSH_n, INT_n, M1_n;
    wire [211:0] cpu_reg;

    // Measured (bench, +PCT3): the T80 holds IORQ_n low through its very first opcode fetch after a reset, with MREQ_n and RD_n low as well; the CRTC then put its
    // status byte (0x20) on the data bus and the first instruction ran wrong. An I/O read or write is never an M1 cycle (the interrupt acknowledge is, and has no RD_n).
    assign io_rd = ~(RD_n | IORQ_n) & M1_n;
    assign io_wr = ~(WR_n | IORQ_n) & M1_n;
    assign mem_rd = ~(RD_n | MREQ_n);
    assign mem_wr = ~(WR_n | MREQ_n);
    assign cpu_dout = D;
    assign cpu_addr = A;
    assign iorq = ~IORQ_n & M1_n;
    assign cpu_m1 = ~M1_n;
    assign ga_ready = ready;
    assign romen = ~romen_n;
    assign cpu_pc_dbg = cpu_reg[79:64];

    wire phi_n;
    wire ready, romen_n;
    wire e244_n, cpu_n, ras_n, cas_n;
    wire crtc_hs, crtc_vs;
    wire [13:0] MA;
    wire  [4:0] RA;
    wire  [7:0] crtc_dout, ppi_dout, portC, portAout, portAin;
    wire hsync_ga, vsync_ga, vblank_ga;

    T80pa CPU (
        .RESET_n(~reset), .CLK(clk), .CEN_p(phi_en_p), .CEN_n(phi_en_n),
        .WAIT_n(ready | (IORQ_n & MREQ_n)),            // the original's workaround: wait in memory and I/O cycles only
        .INT_n(INT_n), .NMI_n(1'b1), .BUSRQ_n(1'b1), .OUT0(1'b0),
        .DI(crtc_dout & ppi_dout & cpu_din), .R800_mode(1'b0), .DIRSet(1'b0), .DIR(212'd0),
        .M1_n(M1_n), .MREQ_n(MREQ_n), .IORQ_n(IORQ_n), .RD_n(RD_n), .WR_n(WR_n), .RFSH_n(RFSH_n), .HALT_n(), .BUSAK_n(),
        .A(A), .DO(D), .REG(cpu_reg)
    );

    UM6845R CRTC (
        .CLOCK(clk), .CLKEN(cclk_en_n), .nCLKEN(cclk_en_p), .nRESET(~reset), .CRTC_TYPE(crtc_type),
        .ENABLE(io_rd | io_wr), .nCS(A[14]), .R_nW(A[9]), .RS(A[8]), .DI(~RD_n ? 8'hFF : D), .DO(crtc_dout),
        .SNA_LOAD(1'b0), .SNA_ADDR(5'd0), .SNA_REGS(144'd0),
        .VSYNC(crtc_vs), .HSYNC(crtc_hs), .DE(crtc_de), .FIELD(), .CURSOR(), .MA(MA), .RA(RA)
    );

    wire [14:0] crtc_vram_addr = {MA[13:12], RA[2:0], MA[9:0]};
    reg vram_bs;
    reg [7:0] vram_d;
    reg cas_n_old;
    always @(posedge clk) begin
        // two 8-bit fetches in the vram cycle
        cas_n_old <= cas_n;
        if (!cpu_n) vram_bs <= 0;
        else begin
            vram_addr <= crtc_vram_addr;
            if (!ras_n & !cas_n_old & cas_n) vram_bs <= 1;
            if (!ras_n & !cas_n)
                vram_d <= vram_bs ? vram_din[15:8] : vram_din[7:0];
        end
    end

    wire [7:0] ga_din = e244_n ? vram_d : D;

    ga40010 GateArray (
        .clk(clk), .cen_16(ce_16), .fast(1'b0),
`ifdef VERILATOR
        .clk_16(1'b0),
`endif
        .RESET_N(~reset), .A(A[15:14]), .D(ga_din), .MREQ_N(MREQ_n), .M1_N(M1_n), .RD_N(RD_n), .IORQ_N(IORQ_n),
        .HSYNC_I(crtc_hs), .VSYNC_I(crtc_vs), .DISPEN(crtc_de),
        .CCLK(), .CCLK_EN_P(cclk_en_p), .CCLK_EN_N(cclk_en_n), .PHI_N(phi_n), .PHI_EN_N(phi_en_n), .PHI_EN_P(phi_en_p),
        .RAS_N(ras_n), .CAS_N(cas_n), .READY(ready), .CASAD_N(), .CPU_N(cpu_n), .MWE_N(), .E244_N(e244_n), .ROMEN_N(romen_n), .RAMRD_N(),
        .ROM(),
        .HSYNC_O(hsync_ga), .VSYNC_O(vsync_ga), .VBLANK(vblank_ga), .MODE(mode), .SYNC_N(), .INT_N(INT_n),
        .BLUE_OE_N(blue[0]), .BLUE(blue[1]), .GREEN_OE_N(green[0]), .GREEN(green[1]), .RED_OE_N(red[0]), .RED(red[1]),
        .SNA_LOAD(1'b0), .SNA_INKSEL(5'd0), .SNA_PALETTE(136'd0), .SNA_CONFIG(8'd0)
    );
    assign hsync = hsync_ga;
    assign vsync = vsync_ga;

    // the ROM map: lower ROM and upper ROM 0 (BASIC) always, upper ROM 7 (AMSDOS) when the model has it; every other number selects ROM 0
    wire [255:0] rom_map = {248'd0, has_amsdos, 6'd0, 1'b1};
    Amstrad_MMU MMU (
        .CLK(clk), .reset(reset), .ram64k(ram64k), .romen_n(romen_n), .rom_map(rom_map), .A(A), .D(D), .io_WR(io_wr),
        .sna_load(1'b0), .sna_ram_config(8'd0), .sna_rom_select(8'd0), .ram_A(mem_addr)
    );

    i8255 PPI (
        .reset(reset), .clk_sys(clk), .addr(A[9:8]), .idata(D), .odata(ppi_dout), .cs(~A[11]), .we(io_wr), .oe(io_rd),
        .ipa(portAin), .opa(portAout), .ipb({tape_in, 2'b11, ppi_jumpers, crtc_vs}), .opb(), .ipc(8'hFF), .opc(portC),
        .sna_load(1'b0), .sna_opa(8'd0), .sna_opb(8'd0), .sna_opc(8'd0), .sna_control(8'd0)
    );
    assign tape_motor = portC[4];
    assign tape_out = portC[5];

    // the keyboard line the PPI's port C selects, active low, on the AY's port A (the original's hid.sv does this and the joysticks)
    wire [3:0] line = portC[3:0];
    reg [7:0] kbd_out;
    integer li;
    always @* begin
        kbd_out = 8'hFF;
        for (li = 0; li < 10; li = li + 1) if (line == li) kbd_out = ~mat[8 * li +: 8];
    end

    YM2149 PSG (
        .CLK(clk), .CE(cclk_en_p), .RESET(reset), .BC(portC[6]), .BDIR(portC[7]), .DI(portAout), .DO(portAin),
        .CHANNEL_A(ay_a), .CHANNEL_B(ay_b), .CHANNEL_C(ay_c), .SEL(1'b0), .MODE(1'b0), .ACTIVE(),
        .IOA_in(kbd_out), .IOA_out(), .IOB_in(8'hFF), .IOB_out()
    );
endmodule
