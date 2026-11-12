// The app's DMA window into the C64 and the lens unit (T12 B, T21 §3.4.1; SLIM-state-path.md).
// clk = the C64's clk (top.v sys_clk). Before SLIM this module was freeze_resp.v, which also
// held a freeze session responder (shadow registers, commit list, statistics); that is gone:
// the machine's stop is the whole freeze, and what the app does to a stopped machine is a
// real bus cycle.
//
// The window (ultimate_sys.vhd's handshake: a toggle per byte, the rest stable until done): a
// byte goes to the C64 RAM's second port (dma_bus = 0, whenever JTAG does not use the port in
// that clock) or, when ultimate_sys says the machine is stopped (dma_bus = 1: C64_STOP or the
// debugger's stop for the D page), becomes one bus cycle in c64_core through the PLA with the
// app's C64_MODE Ultimax. With dma_rom it goes to the ROM block through the same port instead
// (the app's BASIC/KERNAL/CHAR apertures; dma_addr = byte offset in the block: char 0x0000,
// KERNAL 0x2000, BASIC 0x4000), read or write, at any time. With dma_rom and dma_addr[15:13] = 011
// (0x6000 + offset) it goes to the colour RAM's second port instead (the app's colour RAM aperture,
// ultimate_sys 0x1018F000): no bus cycle, 4 bits, a read gives $Fx; the text layer (c64_text.v) reads
// the port in the clocks this one leaves free (cram_use). With C64_DMA_MEMONLY the app reaches all of
// RAM and the colour RAM this way, so its freeze makes no bus cycle at all and the machine stays exactly
// where the pause left it. A bus cycle changes the chip
// (a read of $DC0D clears the ICR, a write to $D019 acknowledges): that is the app's doing
// on a stopped machine, as with a cartridge freezer on a real C64. The side-effect-free look
// at a chip is the state bus (trx_sys.v page 1) and, for the cartridge, the lens unit below.
// The first bus cycle of a pause makes c64_core hold the CPU (RDY, as under the REU); c64_top's
// cpu_paused follows at the next phi2 fall, so the CPU runs one or two cycles with the mapping
// it has before that cycle comes. An app that sets Ultimax makes the first cycle of a freeze
// with Ultimax off (C64::freeze reads $0002), later cycles find the CPU held.
//
// Lens unit (LK_REQ/LK_RES, T21 §3.4.1), in a stop or not: a look at the cartridge (c64_core
// pk_*: the model's mapping with its banking registers as they are, IO registers as a read
// would show them without the read's side effect, the REU's registers without clearing its
// status; ROM image and cartridge RAM from DDR2 through cart_fill, pkd_*) for a class (ROML,
// ROMH, IO1, IO2) and address the app gives, or with REAL one bus cycle with the select forced
// (cyc_force: ROML, ROMH or I/O, whatever the PLA maps) and the app's Ultimax off, which is how
// a cartridge on the physical port (cart_ext) is read; REAL needs the machine paused. It runs in
// the idle state behind the window's requests.
module dma_win (
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
    // the C64 RAM's second port, shared with JTAG (JTAG first): read data one
    // clock after the address
    input  wire        dbg_busy,
    output wire        port_use,
    output wire [15:0] port_addr,
    output wire        port_rom,
    output wire        port_we,
    output wire  [7:0] port_wdata,
    input  wire  [7:0] port_rdata,
    // the colour RAM's second port (c64_mem.v), shared with c64_text.v: this module's clock is cram_use,
    // read data one clock after it
    output wire  [9:0] cram_addr,
    output wire        cram_we,
    output wire  [3:0] cram_wdata,
    input  wire  [3:0] cram_rdata,
    output wire        cram_use,
    // bus cycles in c64_core
    output reg         cyc_req = 1'b0,
    output wire [15:0] cyc_addr,
    output wire        cyc_we,
    output wire  [7:0] cyc_wdata,
    input  wire        cyc_done,
    input  wire  [7:0] cyc_rdata,
    output wire  [1:0] cyc_force,           // the forced select of a REAL lens cycle
    // machine control
    input  wire        halted,              // c64_core: the gate is shut
    input  wire        app_ultimax,         // C64_MODE b1
    output wire        core_ultimax,
    // cartridge peek (c64_core): the address and its class (0 ROML, 1 ROMH,
    // 2 IO1, 3 IO2); the answer, combinational from the model: hit = the
    // cartridge or the REU drives this read, mem = from DDR2 at maddr (b22
    // cartridge RAM, else the ROM image), else data
    output wire [15:0] pk_addr,
    output reg   [1:0] pk_cls = 2'd0,
    input  wire        pk_hit,
    input  wire        pk_mem,
    input  wire [23:0] pk_maddr,
    input  wire  [7:0] pk_data,
    input  wire        cart_ext,            // the cartridge is on the physical port (T16)
    // the lens unit (trx_sys.v page 5 LK_REQ / LK_RES): a pulse with the request
    // {REAL, class, address}; the result word
    input  wire        lk_go,
    input  wire [18:0] lk_req,
    output wire [31:0] lk_res,
    // ... and its DDR2 read (cart_fill, ui_clk): a toggle with pkd_addr held,
    // answered by a toggle with the byte
    output reg         pkd_tgl = 1'b0,
    output reg  [23:0] pkd_addr = 24'd0,
    input  wire        pkd_ack_tgl,
    input  wire  [7:0] pkd_data
);
    // ---- requests ----------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] dreq_s = 3'b000;
    wire ev = dreq_s[2] != dreq_s[1];
    (* ASYNC_REG = "TRUE" *) reg [2:0] pkd_s = 3'b000;
    wire pkd_ev = pkd_s[2] != pkd_s[1];

    // ---- lens unit -------------------------------------------------------------------
    reg        lk_pend = 1'b0, lk_busy = 1'b0, lk_real = 1'b0;
    reg        lk_done = 1'b0, lk_hit = 1'b0, lk_ext = 1'b0, lk_rdone = 1'b0, lk_refused = 1'b0;
    reg [15:0] lk_a = 16'd0;
    reg  [1:0] lk_c = 2'd0;
    reg  [7:0] lk_data = 8'd0;
    reg [17:0] lk_to = 18'd0;                   // a REAL cycle that never comes (the machine resumed)
    assign lk_res = {lk_busy, 18'd0, lk_refused, lk_rdone, lk_ext, lk_hit, lk_done, lk_data};

    // the lens's REAL cycle decodes with the app's Ultimax off; every other cycle with the app's
    reg        cyc_out = 1'b0, cyc_lk = 1'b0;
    assign core_ultimax = (cyc_out && cyc_lk) ? 1'b0 : app_ultimax;

    // colour RAM port job: c_st 1 = the port is ours (address, and the write), 2 = the read data is there
    reg  [1:0] c_st = 2'd0;
    reg        c_pend = 1'b0;
    assign cram_addr  = dma_addr[9:0];
    assign cram_use   = c_st == 2'd1;
    assign cram_we    = cram_use && dma_we;
    assign cram_wdata = dma_wdata[3:0];

    // RAM port job
    reg        p_ram = 1'b0, p_bus = 1'b0, ram_rd = 1'b0;
    reg [15:0] pr_addr;
    reg        pr_we, pr_rom;
    reg  [7:0] pr_wdata;
    assign port_use   = p_ram && !dbg_busy;
    assign port_addr  = pr_addr;
    assign port_rom   = pr_rom;
    assign port_we    = pr_we;
    assign port_wdata = pr_wdata;

    // bus cycles: the window's, or the lens's
    assign cyc_addr  = cyc_lk ? lk_a : dma_addr;
    assign cyc_we    = cyc_lk ? 1'b0 : dma_we;
    assign cyc_wdata = dma_wdata;
    assign cyc_force = !cyc_lk ? 2'd0 : lk_c == 2'd0 ? 2'd1 : lk_c == 2'd1 ? 2'd2 : 2'd3;
    assign pk_addr   = lk_a;

    localparam [2:0] S_IDLE = 3'd0, S_PKW = 3'd1, S_PK0 = 3'd2, S_PK1 = 3'd3, S_LKC = 3'd4;
    reg  [2:0] st = S_IDLE;

    always @(posedge clk) begin
        dreq_s    <= {dreq_s[1:0], dma_req_tgl};
        pkd_s     <= {pkd_s[1:0], pkd_ack_tgl};
        cyc_req   <= 1'b0;
        ram_rd    <= port_use;

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
        if (ram_rd || (cyc_done && !cyc_lk)) begin
            dma_rdata    <= ram_rd ? port_rdata : cyc_rdata;
            dma_done_tgl <= ~dma_done_tgl;
        end
        if (p_bus && !cyc_out) begin
            p_bus    <= 1'b0;
            cyc_req  <= 1'b1;
            cyc_out  <= 1'b1;
        end
        if (cyc_done) begin
            cyc_out <= 1'b0;
            cyc_lk  <= 1'b0;
        end
        // the lens unit's request
        if (lk_go && !lk_busy) begin
            lk_busy <= 1'b1; lk_pend <= 1'b1;
            lk_a <= lk_req[15:0]; lk_c <= lk_req[17:16]; lk_real <= lk_req[18];
            lk_done <= 1'b0; lk_hit <= 1'b0; lk_ext <= 1'b0; lk_rdone <= 1'b0; lk_refused <= 1'b0;
            lk_data <= 8'hFF;
        end

        case (st)
        S_IDLE:
            if (lk_pend && !p_ram && !ram_rd && !p_bus && !cyc_out && !ev) begin
                lk_pend <= 1'b0;
                if (lk_real) begin
                    if (halted) begin
                        cyc_lk   <= 1'b1;
                        cyc_req  <= 1'b1;
                        cyc_out  <= 1'b1;
                        lk_to    <= 18'd0;
                        st       <= S_LKC;
                    end else begin
                        lk_refused <= 1'b1; lk_done <= 1'b1; lk_busy <= 1'b0;
                    end
                end else begin
                    pk_cls <= lk_c;
                    st <= S_PKW;
                end
            end
        S_PKW: st <= S_PK0;                 // pk_cls goes out (a clock for the model's decode)
        S_PK0:                              // the model's answer
            if (!pk_hit || cart_ext) begin
                lk_data <= 8'hFF; lk_hit <= pk_hit; lk_ext <= cart_ext;
                lk_done <= 1'b1; lk_busy <= 1'b0;
                st <= S_IDLE;
            end else if (!pk_mem) begin
                lk_data <= pk_data; lk_hit <= 1'b1;
                lk_done <= 1'b1; lk_busy <= 1'b0;
                st <= S_IDLE;
            end else begin
                pkd_addr <= pk_maddr;
                pkd_tgl  <= ~pkd_tgl;
                st <= S_PK1;
            end
        S_PK1:
            if (pkd_ev) begin
                lk_data <= pkd_data; lk_hit <= 1'b1;
                lk_done <= 1'b1; lk_busy <= 1'b0;
                st <= S_IDLE;
            end
        S_LKC: begin                        // the lens unit's bus cycle
            lk_to <= lk_to + 1'b1;
            if (cyc_done) begin
                lk_data <= cyc_rdata; lk_hit <= 1'b1; lk_rdone <= 1'b1;
                lk_done <= 1'b1; lk_busy <= 1'b0;
                st <= S_IDLE;
            end else if (&lk_to || !halted) begin
                lk_refused <= 1'b1;
                lk_done <= 1'b1; lk_busy <= 1'b0;
                st <= S_IDLE;
            end
        end
        default: st <= S_IDLE;
        endcase
    end
endmodule
