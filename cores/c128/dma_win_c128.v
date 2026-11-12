// The app's DMA window into the C64 (T12 B; SLIM-state-path.md). c64u copy of trx64/dma_win.v (T103 Q5)
// WITHOUT the lens unit: no LK_REQ/LK_RES, no cartridge peek, no forced-select bus cycle, no DDR2 peek.
// clk = the C64's clk (top.v sys_clk).
//
// The window (ultimate_sys.vhd's handshake: a toggle per byte, the rest stable until done): a byte goes to the C64
// RAM's second port (dma_bus = 0, whenever JTAG does not use the port in that clock) or, when ultimate_sys says the
// machine is stopped (dma_bus = 1: C64_STOP), becomes one bus cycle in c64_core_c64u through the PLA with the
// app's C64_MODE Ultimax. With dma_rom it goes to the ROM block through the same port instead (the app's
// BASIC/KERNAL/CHAR apertures; dma_addr = byte offset in the block: char 0x0000, KERNAL 0x2000, BASIC 0x4000),
// read or write, at any time. With dma_rom and dma_addr[15:13] = 011 (0x6000 + offset) it goes to the colour RAM's
// second port instead (the app's colour RAM aperture, ultimate_sys 0x1018F000): no bus cycle, 4 bits, a read gives
// $Fx; the text layer (c64_text.v) reads the port in the clocks this one leaves free (cram_use). With
// C64_DMA_MEMONLY the app reaches all of RAM and the colour RAM this way, so its freeze makes no bus cycle at all
// and the machine stays exactly where the pause left it. A bus cycle changes the chip (a read of $DC0D clears the
// ICR, a write to $D019 acknowledges): that is the app's doing on a stopped machine, as with a cartridge freezer on
// a real C64. The first bus cycle of a pause makes c64_core_c64u hold the CPU (RDY, as under the REU); c64_top_c64u's
// cpu_paused follows at the next phi2 fall, so the CPU runs one or two cycles with the mapping it has before that
// cycle comes. An app that sets Ultimax makes the first cycle of a freeze with Ultimax off (C64::freeze reads
// $0002), later cycles find the CPU held.
// T140 D25: c128 copy of dma_win_c64u.v for the RAM in the DDR2 (c128_ramd.v): the RAM port job is not answered one clock after the grant but when the RAM
// module says so (`port_ack`, a pulse; `port_rdata` is valid with it), so a read of a byte that is not in a slot or the cache waits for its line.
module dma_win_c128 (
    input  wire        port_ack,
    input  wire        clk,
    // ultimate_sys's handshake: a toggle per byte, the rest stable until done
    input  wire        dma_req_tgl,
    input  wire [15:0] dma_addr,
    input  wire        dma_we,
    input  wire  [7:0] dma_wdata,
    input  wire        dma_bus,
    input  wire        dma_rom,
    output reg         dma_done_tgl = 1'b0,
    output reg   [7:0] dma_rdata = 8'h00,
    // the C64 RAM's second port, shared with JTAG (JTAG first): read data one clock after the address
    input  wire        dbg_busy,
    output wire        port_use,
    output wire [15:0] port_addr,
    output wire        port_rom,
    output wire        port_we,
    output wire  [7:0] port_wdata,
    input  wire  [7:0] port_rdata,
    // the colour RAM's second port (c64_mem_c64u.v), shared with c64_text.v: this module's clock is cram_use, read
    // data one clock after it
    output wire  [9:0] cram_addr,
    output wire        cram_we,
    output wire  [3:0] cram_wdata,
    input  wire  [3:0] cram_rdata,
    output wire        cram_use,
    // bus cycles in c64_core_c64u
    output reg         cyc_req = 1'b0,
    output wire [15:0] cyc_addr,
    output wire        cyc_we,
    output wire  [7:0] cyc_wdata,
    input  wire        cyc_done,
    input  wire  [7:0] cyc_rdata
);
    // ---- requests ----------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] dreq_s = 3'b000;
    wire ev = dreq_s[2] != dreq_s[1];
    reg        cyc_out = 1'b0;

    // colour RAM port job: c_st 1 = the port is ours (address, and the write), 2 = the read data is there
    reg  [1:0] c_st = 2'd0;
    reg        c_pend = 1'b0;
    assign cram_addr  = dma_addr[9:0];
    assign cram_use   = c_st == 2'd1;
    assign cram_we    = cram_use && dma_we;
    assign cram_wdata = dma_wdata[3:0];

    // RAM port job
    reg        p_ram = 1'b0, p_bus = 1'b0, ram_rd = 1'b0, p_wait = 1'b0;
    reg [15:0] pr_addr;
    reg        pr_we, pr_rom;
    reg  [7:0] pr_wdata;
    assign port_use   = p_ram && !dbg_busy;
    assign port_addr  = pr_addr;
    assign port_rom   = pr_rom;
    assign port_we    = pr_we;
    assign port_wdata = pr_wdata;

    assign cyc_addr  = dma_addr;
    assign cyc_we    = dma_we;
    assign cyc_wdata = dma_wdata;

    always @(posedge clk) begin
        dreq_s    <= {dreq_s[1:0], dma_req_tgl};
        cyc_req   <= 1'b0;
        ram_rd    <= port_ack && p_wait;
        if (port_use) p_wait <= 1'b1;
        if (port_ack && p_wait) p_wait <= 1'b0;

        // the window's requests
        if (ev) begin
            if (dma_rom && dma_addr[15:13] == 3'b011)
                c_pend <= 1'b1;
            else if (dma_rom || !dma_bus) begin
                p_ram <= 1'b1;
                pr_addr <= dma_addr; pr_rom <= dma_rom; pr_we <= dma_we; pr_wdata <= dma_wdata;
            end else
                p_bus <= 1'b1;
        end
        if (port_use) p_ram <= 1'b0;
        case (c_st)
        2'd0: if (c_pend) begin c_pend <= 1'b0; c_st <= 2'd1; end
        2'd1: if (dma_we) begin
                  dma_rdata <= 8'h00; dma_done_tgl <= ~dma_done_tgl; c_st <= 2'd0;
              end else
                  c_st <= 2'd2;
        default: begin
                  dma_rdata <= {4'hF, cram_rdata}; dma_done_tgl <= ~dma_done_tgl; c_st <= 2'd0;
              end
        endcase
        if (ram_rd || cyc_done) begin
            dma_rdata    <= ram_rd ? port_rdata : cyc_rdata;
            dma_done_tgl <= ~dma_done_tgl;
        end
        if (p_bus && !cyc_out) begin
            p_bus    <= 1'b0;
            cyc_req  <= 1'b1;
            cyc_out  <= 1'b1;
        end
        if (cyc_done)
            cyc_out <= 1'b0;
    end
endmodule
