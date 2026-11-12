// T95 D12 / M3: the memory behind NES_MiSTer's `cpumem_*` / `ppumem_*` ports. The machine addresses a 25 bit (CPU) and a 22 bit (PPU)
// linear space the way the MiSTer top's SDRAM does (cart.sv's last block, GameLoader): a region is told by the top bits,
//   CPU  0x000000-0x1FFFFF PRG ROM (up to 2 MB)   0x380000 work RAM 2 KB (the CPU's $0000-$1FFF)   0x3C0000 PRG RAM $6000-$7FFF (8 KB)
//   PPU  0x200000-0x2FFFFF CHR (ROM up to 1 MB, or RAM when the header says CHR RAM: 8 KB here)      0x3A0000 nametable RAM 2 KB
// What lives where here:
//   block RAM    work RAM, PRG RAM, nametable RAM, CHR RAM, and the two caches below
//   DDR2         PRG ROM and CHR ROM, behind the host's real-time port c_* (ui_clk) at CART_BASE: PRG at +0, CHR at +0x200000, the image
//                as the iNES file holds it (no header); the app writes it through the window below (or the host DMAs it there)
// The caches are direct mapped, 16 byte lines (one MIG word), write through, keyed by the LINEAR address the mapper produced. A bank
// switch therefore needs no invalidation - another bank is another line (a cache keyed by the CPU's address would serve the old bank). What does need one is a change of the image: a
// window write invalidates the line it hits.
//   PRG  256 lines = 4 KB (one RAMB36), index = line[7:0] ^ page bits so that a fixed bank and a switched one do not share lines
//   CHR  512 lines = 8 KB (two RAMB36)
// A miss HOLDS THE WHOLE MACHINE: `stall` takes the clock enable away (nes_core.v) until the line is in, so the machine's bus cycles
// are those of a memory that always answers in time (the Atari core does the same, a8_mem.vhd) - everything stops together and nothing
// is out of step, the machine only runs a little slower while it waits (the stall is counted). The engine fetches one line (128 bit)
// at a time; the window's byte read and write go through it as well (a write is a 128 bit write with one byte lane enabled).
//
// Timing rule (the machine has one clock enable on cpu_clk; it ticks no more often than every 4th clock). The machine's request signals
// are combinational functions of its registers (cart.sv: mapper banks, masks) and settle over several clocks, so they are only looked
// at in the clock before a tick, `cap`: the captured address, strobes and data are plain registers, the block RAMs run on them, and
// nothing here can write on a transient. The data the machine gets back is therefore two ticks old at most, and a miss is seen by the
// second tick after the request appears; the machine asks for a byte at least four ticks before it uses it (cart.sv: `cart_pre`
// starts five master cycles ahead of the CPU's `cpu_ce` and the PPU's read dot lasts four ticks). Written for this project (TREX), no
// upstream code; the engine and its clock crossing are the circuit of a8_mem.vhd (the Atari core's cache) in Verilog.
`timescale 1ns / 1ps
module nes_mem (
    input  wire        clk,                     // cpu_clk
    input  wire        rst,                     // the machine's reset: flush the caches
    input  wire        cap,
    input  wire        chr_ram,                 // the header says CHR RAM (mapper_flags[15]): the PPU may write the CHR area
    input  wire [31:0] cart_base,               // DDR2 byte address of the image (16 byte aligned; bits 3:0 ignored)
    // the machine
    input  wire [24:0] cpu_addr,
    input  wire        cpu_rd,
    input  wire        cpu_wr,
    input  wire [7:0]  cpu_dout,
    output wire [7:0]  cpu_din,
    input  wire [21:0] ppu_addr,
    input  wire        ppu_rd,
    input  wire        ppu_wr,
    input  wire [7:0]  ppu_dout,
    output wire [7:0]  ppu_din,
    output wire        stall,                   // a register: the clock enable must not tick
    // the app's window (nes_win, already registered): one byte of the image
    input  wire        w_start,                 // one clock
    input  wire        w_we,
    input  wire        w_chr,
    input  wire [20:0] w_a,                     // offset in the PRG or CHR image
    input  wire [7:0]  w_di,
    output reg         w_done = 1'b0,           // one clock; w_do is valid with it
    output reg  [7:0]  w_do = 8'd0,
    // the host's real-time DDR2 port (ui_clk)
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg  [26:0] c_addr = 27'd0,
    output reg  [2:0]  c_cmd = 3'd0,
    output wire        c_en,
    input  wire        c_rdy,
    output reg  [127:0] c_wdf_data = 128'd0,
    output reg  [15:0] c_wdf_mask = 16'hFFFF,
    output wire        c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire        c_rd_valid,
    // counters for the bring-up (clk)
    output reg  [31:0] st_miss_prg = 32'd0,
    output reg  [31:0] st_miss_chr = 32'd0,
    output reg  [31:0] st_stall = 32'd0
);
    // ------------------------------------------------------------------ captured request
    reg [24:0] ca = 25'd0;
    reg        cr = 1'b0, cw = 1'b0;
    reg [7:0]  cd = 8'd0;
    reg [21:0] pa = 22'd0;
    reg        pr = 1'b0, pw = 1'b0;
    reg [7:0]  pd = 8'd0;
    // The address of an access is taken when the access BEGINS and held while it lasts (MiSTer's SDRAM controller does the same: it
    // latches the address on the rising edge of read or write). A mapper that changes its banks in answer to the read itself - MMC2
    // and MMC4 flip a CHR latch when the PPU reads tile $FD or $FE - must not change the byte that read returns.
    wire c_busy = (cr & cpu_rd) | (cw & cpu_wr);
    wire p_busy = (pr & ppu_rd) | (pw & ppu_wr);
    always @(posedge clk) if (cap) begin
        cr <= cpu_rd; cw <= cpu_wr; cd <= cpu_dout;
        pr <= ppu_rd; pw <= ppu_wr; pd <= ppu_dout;
        if (!c_busy) ca <= cpu_addr;
        if (!p_busy) pa <= ppu_addr;
    end
    // regions
    wire c_ram  = (ca[24] == 1'b0) && (ca[21:19] == 3'b111);
    wire c_wram = c_ram && !ca[18];
    wire c_prgr = c_ram && ca[18];
    wire c_rom  = (ca[24] == 1'b0) && !ca[21];
    wire p_ci   = (pa[21:17] == 5'b11101);
    wire p_chr  = (pa[21:20] == 2'b10);
    wire p_rom  = p_chr && !chr_ram;                // CHR ROM: from the DDR2
    wire p_crm  = p_chr && chr_ram;                 // CHR RAM: block RAM

    // ------------------------------------------------------------------ block RAMs the machine writes
    (* ram_style = "block" *) reg [7:0] wram [0:2047];
    (* ram_style = "block" *) reg [7:0] prgram [0:8191];
    (* ram_style = "block" *) reg [7:0] ciram [0:2047];
    (* ram_style = "block" *) reg [7:0] chrram [0:8191];
    reg [7:0] wram_q = 8'd0, prgram_q = 8'd0, ci_q = 8'd0, crm_q = 8'd0;
    always @(posedge clk) begin
        if (cw && c_wram) wram[ca[10:0]] <= cd;
        wram_q <= wram[ca[10:0]];
    end
    always @(posedge clk) begin
        if (cw && c_prgr) prgram[ca[12:0]] <= cd;
        prgram_q <= prgram[ca[12:0]];
    end
    always @(posedge clk) begin
        if (pw && p_ci) ciram[pa[10:0]] <= pd;
        ci_q <= ciram[pa[10:0]];
    end
    always @(posedge clk) begin
        if (pw && p_crm) chrram[pa[12:0]] <= pd;
        crm_q <= chrram[pa[12:0]];
    end

    // ------------------------------------------------------------------ the caches
    // PRG: 256 lines of 16 bytes, data in 32 bit words (1024), tag = line address bits 20:12 and a valid bit
    // CHR: 512 lines, tag = line address bits 19:13 and a valid bit
    wire [8:0] c_want = ca[20:12];
    wire [6:0] p_want = pa[19:13];
    wire [7:0] c_idx = ca[11:4] ^ ca[19:12];
    wire [8:0] p_idx = pa[12:4] ^ {2'b00, pa[19:13]};
    (* ram_style = "block" *) reg [31:0] c_data [0:1023];
    (* ram_style = "block" *) reg [31:0] p_data [0:2047];
    (* ram_style = "block" *) reg [9:0]  c_tag  [0:255];        // {valid, tag}
    (* ram_style = "block" *) reg [7:0]  p_tag  [0:511];
    reg [31:0] c_dq = 32'd0, p_dq = 32'd0;
    reg [9:0]  c_tq = 10'd0;
    reg [7:0]  p_tq = 8'd0;
    reg [8:0]  c_want_q = 9'd0;
    reg [6:0]  p_want_q = 7'd0;
    reg [1:0]  c_boff_q = 2'd0, p_boff_q = 2'd0;
    reg        c_rom_q = 1'b0, c_wram_q = 1'b0, c_prgr_q = 1'b0, p_ci_q = 1'b0, p_rom_q = 1'b0, p_crm_q = 1'b0;
    // fill / invalidate ports
    reg        c_dwe = 1'b0, p_dwe = 1'b0, c_twe = 1'b0, p_twe = 1'b0;
    reg [9:0]  c_daddr = 10'd0;
    reg [10:0] p_daddr = 11'd0;
    reg [31:0] f_word = 32'd0;
    reg [7:0]  c_taddr = 8'd0;
    reg [8:0]  p_taddr = 9'd0;
    reg [9:0]  c_tdata = 10'd0;
    reg [7:0]  p_tdata = 8'd0;
    always @(posedge clk) begin
        c_dq <= c_data[{c_idx, ca[3:2]}];
        if (c_dwe) c_data[c_daddr] <= f_word;
        p_dq <= p_data[{p_idx, pa[3:2]}];
        if (p_dwe) p_data[p_daddr] <= f_word;
    end
    always @(posedge clk) begin
        c_tq <= c_tag[c_idx];
        if (c_twe) c_tag[c_taddr] <= c_tdata;
        p_tq <= p_tag[p_idx];
        if (p_twe) p_tag[p_taddr] <= p_tdata;
        c_want_q <= c_want; p_want_q <= p_want; c_boff_q <= ca[1:0]; p_boff_q <= pa[1:0];
        c_rom_q <= c_rom; c_wram_q <= c_wram; c_prgr_q <= c_prgr; p_ci_q <= p_ci; p_rom_q <= p_rom; p_crm_q <= p_chr && chr_ram;
    end
    // a registered view of the lookup: valid two clocks after the capture
    wire c_hit = c_tq[9] && (c_tq[8:0] == c_want_q);
    wire p_hit = p_tq[7] && (p_tq[6:0] == p_want_q);
    reg  c_miss = 1'b0, p_miss = 1'b0;
    reg [24:0] ca_q = 25'd0;
    reg [21:0] pa_q = 22'd0;
    reg [7:0]  c_idx_q = 8'd0;
    reg [8:0]  p_idx_q = 9'd0;
    always @(posedge clk) begin
        ca_q <= ca; pa_q <= pa; c_idx_q <= c_idx; p_idx_q <= p_idx;
        // the lookup (tag read and compare) belongs to the request captured two clocks ago; a new capture comes at the earliest four
        // clocks after the last
        c_miss <= cr && c_rom && !c_hit;
        p_miss <= pr && p_rom && !p_hit;
    end
    assign stall = c_miss | p_miss;
    always @(posedge clk) if (stall) st_stall <= st_stall + 32'd1;

    // ------------------------------------------------------------------ what the machine reads
    wire [7:0] c_byte = c_boff_q == 2'd0 ? c_dq[7:0] : c_boff_q == 2'd1 ? c_dq[15:8] : c_boff_q == 2'd2 ? c_dq[23:16] : c_dq[31:24];
    wire [7:0] p_byte = p_boff_q == 2'd0 ? p_dq[7:0] : p_boff_q == 2'd1 ? p_dq[15:8] : p_boff_q == 2'd2 ? p_dq[23:16] : p_dq[31:24];
    assign cpu_din = c_rom_q ? c_byte : c_wram_q ? wram_q : c_prgr_q ? prgram_q : 8'd0;
    assign ppu_din = p_ci_q ? ci_q : p_rom_q ? p_byte : p_crm_q ? crm_q : 8'd0;

    // ------------------------------------------------------------------ the engine (clk side)
    // jobs: 0 PRG line fill, 1 CHR line fill, 2 window read, 3 window write
    localparam [2:0] M_IDLE = 3'd0, M_RD = 3'd1, M_FILL = 3'd2, M_WR = 3'd3, M_WRD = 3'd4;
    reg [2:0]  ms = M_IDLE;
    reg [1:0]  jt = 2'd0;
    reg [26:0] j_line = 27'd0;                      // DDR2 word address (16 bit words) of the line
    reg [3:0]  j_lane = 4'd0;
    reg [7:0]  j_byte = 8'd0;
    reg [7:0]  j_cidx = 8'd0;                       // PRG line index / tag of the fill
    reg [9:0]  j_ctag = 10'd0;
    reg [8:0]  j_pidx = 9'd0;
    reg [7:0]  j_ptag = 8'd0;
    reg        rq_tgl = 1'b0;
    reg        rq_wr = 1'b0;
    reg [26:0] rq_addr = 27'd0;
    reg [3:0]  rq_lane = 4'd0;
    reg [7:0]  rq_byte = 8'd0;
    reg        dn_tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg [127:0] ui_line = 128'd0;
    reg [2:0]  fill_i = 3'd0;
    reg [9:0]  fl_cnt = 10'd0;                      // the flush sweep of the tags (512 = done)
    reg        wj_pend = 1'b0;                      // a window job waits for the engine
    reg        wj_we = 1'b0, wj_chr = 1'b0;
    reg [20:0] wj_a = 21'd0;
    reg [7:0]  wj_d = 8'd0;
    reg        rst_q = 1'b0;
    // word address (16 bit words) of the byte `off` of the image: (cart_base + off) / 2
    wire [31:0] base_b = {cart_base[31:4], 4'd0};

    // a miss to serve: the request captured two ticks ago, as the lookup saw it
    wire [31:0] c_lineb = base_b + {11'd0, ca_q[20:4], 4'd0};                    // PRG line, byte address
    wire [31:0] p_lineb = base_b + 32'h00200000 + {12'd0, pa_q[19:4], 4'd0};     // CHR line
    wire [31:0] w_lineb = base_b + (wj_chr ? 32'h00200000 : 32'd0) + {11'd0, wj_a[20:4], 4'd0};
    reg  [2:0] cool = 3'd0;                         // a fill just ended: the lookup needs a few clocks to see it
    always @(posedge clk) begin
        c_dwe <= 1'b0; p_dwe <= 1'b0; c_twe <= 1'b0; p_twe <= 1'b0; w_done <= 1'b0;
        rst_q <= rst;
        dn_s <= {dn_s[1:0], dn_tgl};
        if (w_start) begin
            wj_pend <= 1'b1; wj_we <= w_we; wj_chr <= w_chr; wj_a <= w_a; wj_d <= w_di;
        end
        case (ms)
        M_IDLE: begin
            if (fl_cnt != 10'd512) begin
                // sweep the tags: PRG index fl_cnt[7:0], CHR index fl_cnt[8:0]
                c_twe <= ~fl_cnt[8]; c_taddr <= fl_cnt[7:0]; c_tdata <= 10'd0;
                p_twe <= 1'b1; p_taddr <= fl_cnt[8:0]; p_tdata <= 8'd0;
                fl_cnt <= fl_cnt + 10'd1;
            end else if (cool != 3'd0) begin
                cool <= cool - 3'd1;
            end else if (wj_pend) begin
                wj_pend <= 1'b0;
                rq_addr <= w_lineb[27:1]; rq_lane <= wj_a[3:0]; rq_byte <= wj_d; rq_wr <= wj_we;
                rq_tgl <= ~rq_tgl;
                if (wj_we) begin
                    jt <= 2'd3; ms <= M_WR;
                    // the line is about to change: drop it from the caches if it is there
                    if (wj_chr) begin
                        p_twe <= 1'b1; p_taddr <= wj_a[12:4] ^ {2'b00, wj_a[19:13]}; p_tdata <= 8'd0;
                    end else begin
                        c_twe <= 1'b1; c_taddr <= wj_a[11:4] ^ wj_a[19:12]; c_tdata <= 10'd0;
                    end
                end else begin
                    jt <= 2'd2; ms <= M_RD;
                end
            end else if (c_miss) begin
                jt <= 2'd0; j_cidx <= c_idx_q; j_ctag <= {1'b1, c_want_q};
                rq_addr <= c_lineb[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; ms <= M_RD;
                st_miss_prg <= st_miss_prg + 32'd1;
            end else if (p_miss) begin
                jt <= 2'd1; j_pidx <= p_idx_q; j_ptag <= {1'b1, p_want_q};
                rq_addr <= p_lineb[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; ms <= M_RD;
                st_miss_chr <= st_miss_chr + 32'd1;
            end
        end
        M_RD: if (dn_s[2] != dn_s[1]) begin
            if (jt == 2'd2) begin
                w_do <= rq_lane == 4'd0 ? ui_line[7:0] : rq_lane == 4'd1 ? ui_line[15:8] : rq_lane == 4'd2 ? ui_line[23:16] : rq_lane == 4'd3 ? ui_line[31:24]
                      : rq_lane == 4'd4 ? ui_line[39:32] : rq_lane == 4'd5 ? ui_line[47:40] : rq_lane == 4'd6 ? ui_line[55:48] : rq_lane == 4'd7 ? ui_line[63:56]
                      : rq_lane == 4'd8 ? ui_line[71:64] : rq_lane == 4'd9 ? ui_line[79:72] : rq_lane == 4'd10 ? ui_line[87:80] : rq_lane == 4'd11 ? ui_line[95:88]
                      : rq_lane == 4'd12 ? ui_line[103:96] : rq_lane == 4'd13 ? ui_line[111:104] : rq_lane == 4'd14 ? ui_line[119:112] : ui_line[127:120];
                w_done <= 1'b1; ms <= M_IDLE;
            end else begin
                ms <= M_FILL; fill_i <= 3'd0;
            end
        end
        M_FILL: begin
            fill_i <= fill_i + 3'd1;
            f_word <= fill_i[1:0] == 2'd0 ? ui_line[31:0] : fill_i[1:0] == 2'd1 ? ui_line[63:32] : fill_i[1:0] == 2'd2 ? ui_line[95:64] : ui_line[127:96];
            if (fill_i < 3'd4) begin
                if (jt == 2'd0) begin c_dwe <= 1'b1; c_daddr <= {j_cidx, fill_i[1:0]}; end
                else begin p_dwe <= 1'b1; p_daddr <= {j_pidx, fill_i[1:0]}; end
            end
            if (fill_i == 3'd4) begin
                if (jt == 2'd0) begin c_twe <= 1'b1; c_taddr <= j_cidx; c_tdata <= j_ctag; end
                else begin p_twe <= 1'b1; p_taddr <= j_pidx; p_tdata <= j_ptag; end
                ms <= M_IDLE; cool <= 3'd5;
            end
        end
        M_WR: if (dn_s[2] != dn_s[1]) begin w_done <= 1'b1; ms <= M_IDLE; end
        default: ms <= M_IDLE;
        endcase
        if (rst && !rst_q) fl_cnt <= 10'd0;                                  // the machine was reset: flush
    end
    // ------------------------------------------------------------------ the ui side
    localparam [1:0] U_IDLE = 2'd0, U_ISSUE = 2'd1, U_WAITR = 2'd2;
    reg [1:0]  us = U_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg [26:0] u_addr = 27'd0;
    reg        u_wr = 1'b0;
    reg [3:0]  u_lane = 4'd0;
    reg [7:0]  u_byte = 8'd0;
    reg        c_en_i = 1'b0, c_wr_i = 1'b0;
    integer    li;
    always @(posedge ui_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        if (ui_rst) begin
            us <= U_IDLE; c_en_i <= 1'b0; c_wr_i <= 1'b0;
        end else begin
            case (us)
            U_IDLE: if (rq_s[2] != rq_s[1]) begin
                u_addr <= rq_addr; u_wr <= rq_wr; u_lane <= rq_lane; u_byte <= rq_byte; us <= U_ISSUE;
            end
            U_ISSUE: begin
                c_en_i <= 1'b1;
                c_addr <= u_addr;
                if (u_wr) begin
                    c_cmd <= 3'b000; c_wr_i <= 1'b1;
                    for (li = 0; li < 16; li = li + 1) c_wdf_data[8 * li +: 8] <= u_byte;
                    c_wdf_mask <= 16'hFFFF;
                    c_wdf_mask[u_lane] <= 1'b0;
                end else begin
                    c_cmd <= 3'b001; c_wr_i <= 1'b0;
                end
                if (c_en_i && c_rdy) begin
                    c_en_i <= 1'b0; c_wr_i <= 1'b0;
                    if (u_wr) begin dn_tgl <= ~dn_tgl; us <= U_IDLE; end else us <= U_WAITR;
                end
            end
            U_WAITR: if (c_rd_valid) begin ui_line <= c_rd_data; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
            default: us <= U_IDLE;
            endcase
        end
    end
    assign c_en = c_en_i;
    assign c_wdf_wren = c_wr_i;
endmodule
