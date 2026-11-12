// T143 D3 / D5: the Neo Geo's memory in the DDR2, over the host's two ports (ui_clk, 75 MHz). Written for this project (TREX), no upstream code.
//
// The machine (neo_core.v, 48 MHz) asks for its words with toggle requests (neo_mux.v: 68k program / system ROM, fix graphics, sprites; neo_core.v:
// ADPCM-A, ADPCM-B, Z80 program). Where they live is the application's business: five base registers (byte addresses in the DDR2).
//   rom_base   the space the SDRAM of the original held below its C ROM: system ROM $000000, SFIX $020000, S1 $080000, P1 $200000, P2 $300000...
//              and the C ROM after the P ROMs (CROM_START megabytes): a virtual byte address v of neo_mux.v is the DDR2 byte rom_base + v.
//   v1_base    the V ROM space of ADPCM-A / ADPCM-B-with-PCM (addresses with bit 24 clear); v2_base the V2 space (bit 24 set, ADPCM-B separate)
//   m1_base    the Z80 program
//   img_base   the RAM images: LO ROM +$00000 (64 KB, loaded into block RAM), backup RAM +$10000 (64 KB, lives in the DDR2: neo_bk.v reads and writes it through
//              a cache), memory card +$20000 (2 KB, 8 KB reserved), XRAM +$22000 (8 KB)
// c_* (the real-time port, highest priority at the arbiter): the three channels of the picture and the program. Up to four reads open; they come back
// in order (mig_arb), so a four-entry queue of tags says whose line it is. A request is a 128 bit line read, the word / the eight bytes the machine
// wanted are cut out of it.
// e_* (the general port): the three byte channels (ADPCM-A, ADPCM-B, Z80 program; each with a one-line cache: the sample and the program run on
// consecutive addresses), the picture writer (neo_pixw.v, one 128 bit write per word of a line) and the image mover. One command at a time.
// The image mover copies the four RAM images between the DDR2 and the machine's RAMs through their second ports (img_*): "load" before the machine
// runs (LO ROM, backup RAM, memory card, XRAM), "save" on request of the application, which then reads the DDR2 (never a RAM of the machine).
`timescale 1ns / 1ps
module neo_mem (
    input  wire         ui_clk,
    input  wire         ui_rst,
    // the application's registers (cpu_clk, static while the machine runs)
    input  wire [31:0]  rom_base,
    input  wire [31:0]  v1_base,
    input  wire [31:0]  v2_base,
    input  wire [31:0]  m1_base,
    input  wire [31:0]  img_base,
    input  wire         mv_go_tgl,              // a toggle starts the mover
    input  wire [3:0]   mv_cmd,                 // b1:0 image (0 LO, 1 backup, 2 memory card, 3 XRAM), b2 1 = save (machine -> DDR2), 0 = load; b3 1 = copy cp_len bytes in the DDR2 from cp_src to cp_dst
    input  wire [31:0]  cp_src,                 // byte addresses (16 aligned) and length (multiple of 16): the application writes where its CPU reaches (below 64 MB), the core moves it above
    input  wire [31:0]  cp_dst,
    input  wire [31:0]  cp_len,
    output reg          mv_done_tgl = 1'b0,
    output wire         mv_busy,

    // the machine's channels (48 MHz domain toggles)
    input  wire         pm_req_tgl,
    input  wire [26:1]  pm_addr,
    output reg          pm_ack_tgl = 1'b0,
    output reg  [15:0]  pm_data = 16'd0,
    input  wire         s_req_tgl,
    input  wire [26:1]  s_addr,
    output reg          s_ack_tgl = 1'b0,
    output reg  [15:0]  s_data = 16'd0,
    input  wire         c_req_tgl,
    input  wire [26:3]  c_addr,
    output reg          c_ack_tgl = 1'b0,
    output reg  [63:0]  c_data = 64'd0,
    // the early addresses of the next sprite / fix fetch (neo_mux.v), and the machine's reset / freeze (cpu_clk level: the caches are dropped)
    input  wire         ch_hint_tgl,
    input  wire [26:3]  ch_hint_addr,
    input  wire         sh_hint_tgl,
    input  wire [26:1]  sh_hint_addr,
    input  wire         inval,
    input  wire         va_req_tgl,
    input  wire [24:0]  va_addr,
    output reg          va_ack_tgl = 1'b0,
    output reg  [7:0]   va_data = 8'd0,
    input  wire         vb_req_tgl,
    input  wire [24:0]  vb_addr,
    output reg          vb_ack_tgl = 1'b0,
    output reg  [7:0]   vb_data = 8'd0,
    input  wire         mz_req_tgl,
    input  wire [24:0]  mz_addr,
    output reg          mz_ack_tgl = 1'b0,
    output reg  [7:0]   mz_data = 8'd0,

    // the RAM images (neo_core.v img_*)
    output reg  [1:0]   img_sel = 2'd0,
    output reg  [15:0]  img_addr = 16'd0,
    output reg  [15:0]  img_wdata = 16'd0,
    output reg          img_we = 1'b0,
    input  wire [15:0]  img_rdata,

    // the backup RAM (neo_bk.v): a fill of one line or a write through of one word, one operation open at a time
    input  wire         bk_req_tgl,
    input  wire         bk_wr,
    input  wire [15:1]  bk_addr,
    input  wire [15:0]  bk_wdata,
    input  wire [1:0]   bk_be,
    output reg          bk_ack_tgl = 1'b0,
    output reg  [127:0] bk_line = 128'd0,

    // the picture writer (neo_pixw.v): a 128 bit write; pw_en stays until pw_rdy
    input  wire         pw_en,
    input  wire [26:0]  pw_addr,                // 16 bit word address
    input  wire [127:0] pw_data,
    output reg          pw_rdy = 1'b0,

    // the host's ports
    output reg  [26:0]  c_addr_o = 27'd0,
    output reg  [2:0]   c_cmd = 3'd0,
    output reg          c_en = 1'b0,
    input  wire         c_rdy,
    output wire [127:0] c_wdf_data,
    output wire [15:0]  c_wdf_mask,
    output wire         c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire         c_rd_valid,
    output reg  [26:0]  e_addr = 27'd0,
    output reg  [2:0]   e_cmd = 3'd0,
    output reg          e_en = 1'b0,
    input  wire         e_rdy,
    output reg  [127:0] e_wdf_data = 128'd0,
    output reg  [15:0]  e_wdf_mask = 16'h0000,
    output wire         e_wdf_wren,
    input  wire         e_rd_valid,

    // bring-up counters
    output reg  [31:0]  st_c_cmds = 32'd0,
    output reg  [31:0]  st_e_cmds = 32'd0,
    output reg  [31:0]  st_va_miss = 32'd0,
    output reg  [31:0]  st_mz_miss = 32'd0,
    output reg  [7:0]   st_c_lat_max = 8'd0,        // longest c_* read, ui_clk cycles from the command to the line, saturating
    // the demands per channel: answered from a valid line, from a line in flight, or asked of the DDR2; hints that took a line
    output reg  [31:0]  st_c_hit = 32'd0, st_c_pend = 32'd0, st_c_miss = 32'd0, st_c_hint = 32'd0,
    output reg  [31:0]  st_s_hit = 32'd0, st_s_pend = 32'd0, st_s_miss = 32'd0, st_s_hint = 32'd0,
    output reg  [31:0]  st_p_hit = 32'd0, st_p_pend = 32'd0, st_p_miss = 32'd0
);
    assign c_wdf_data = 128'd0;
    assign c_wdf_mask = 16'hFFFF;
    assign c_wdf_wren = 1'b0;
    assign e_wdf_wren = e_en && (e_cmd == 3'b000);

    // ================================================================ c_*: P, S, C behind line caches (T143 3b)
    // The machine asks with a toggle and a held address as before; the answer comes from a 128 bit line cache (neo_lc.v) per channel: sprites 8 lines, fix 8, 68k program 4.
    // Hints (neo_mux.v: the addresses the sprite / fix fetch will ask for, known about 25 machine clocks early) fill the caches ahead of the demand; a demand for a line
    // that is there is answered without the DDR2, one for a line in flight waits for it, anything else is asked at once (urgent). The 68k's demand also asks for the next line.
    // A hint is only a guess: the demand carries the real address and is looked up by it, so a wrong hint costs a read, never a wrong word.
    localparam [1:0] K_C = 2'd0, K_S = 2'd1, K_P = 2'd2;
    (* ASYNC_REG = "TRUE" *) reg [2:0] pm_s = 3'b000, s_s = 3'b000, c_s = 3'b000, ch_s = 3'b000, sh_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] inval_s = 2'b11;
    reg pm_t = 1'b0, s_t = 1'b0, c_t = 1'b0, ch_t = 1'b0, sh_t = 1'b0;     // the request toggle each channel has taken
    wire pm_p = (pm_s[2] != pm_t);
    wire s_p  = (s_s[2]  != s_t);
    wire c_p  = (c_s[2]  != c_t);
    wire ch_p = (ch_s[2] != ch_t);
    wire sh_p = (sh_s[2] != sh_t);
    reg [4:0] tagq [0:3];
    reg [2:0] tq_w = 3'd0, tq_r = 3'd0;
    wire [2:0] tq_n = tq_w - tq_r;
    wire c_full = (tq_n >= 3'd4);
    reg drain = 1'b1;                                           // reads of before a flush are still on their way: nothing is allocated until they are back
    wire flush = inval_s[1] | ui_rst;
    wire alloc_ok = !inval_s[1] && !drain && !ui_rst;
    wire [26:0] sum_p = rom_base[26:0] + {pm_addr, 1'b0};
    wire [26:0] sum_s = rom_base[26:0] + {s_addr, 1'b0};
    wire [26:0] sum_c = rom_base[26:0] + {c_addr, 3'b000};
    reg [26:3] ch_ha = 24'd0;                                   // a hint, taken from the machine's register while it stands (a new one comes 4 machine clocks later at the earliest)
    reg [26:1] sh_ha = 26'd0;
    reg ch_hv = 1'b0, sh_hv = 1'b0;
    wire [26:0] sum_ch = rom_base[26:0] + {ch_ha, 3'b000};
    wire [26:0] sum_sh = rom_base[26:0] + {sh_ha, 1'b0};
    reg c_busy = 1'b0, s_busy = 1'b0, p_busy = 1'b0;            // a demand is accepted and waits for its line
    reg [2:0] c_ix = 3'd0, s_ix = 3'd0;
    reg [1:0] p_ix = 2'd0;
    reg c_half = 1'b0;
    reg [2:0] s_w = 3'd0, p_w = 3'd0;
    reg pp_v = 1'b0;                                            // the 68k's next line
    reg [22:0] pp_la = 23'd0;
    wire c_dem = c_p && !c_busy && alloc_ok;
    wire s_dem = s_p && !s_busy && alloc_ok;
    wire p_dem = pm_p && !p_busy && alloc_ok;
    // the three caches
    wire        c_od, s_od, p_od;
    wire [2:0]  c_oi, s_oi;
    wire [1:0]  p_oi, c_oc, s_oc, p_oc;
    wire        c_iv, c_iu, s_iv, s_iu, p_iv, p_iu;
    wire [2:0]  c_ii, s_ii;
    wire [1:0]  p_ii;
    wire [22:0] c_il, s_il, p_il;
    wire [127:0] c_rd, s_rd, p_rd;
    wire        c_rv, s_rv, p_rv;
    wire c_take, s_take, p_take;
    wire [4:0] tq_head = tagq[tq_r[1:0]];
    wire [2:0] fl_ix = tq_head[2:0];
    wire fl_c = c_rd_valid && tq_head[4:3] == K_C;               // the line is filled in the clock it is on c_rd_data
    wire fl_s = c_rd_valid && tq_head[4:3] == K_S;
    wire fl_p = c_rd_valid && tq_head[4:3] == K_P;
    neo_lc #(.N(8), .IW(3)) lc_c (
        .clk(ui_clk), .flush(flush),
        .op_v(c_dem | ch_hv), .op_la(c_dem ? sum_c[26:4] : sum_ch[26:4]), .op_urg(c_dem), .op_alloc_ok(alloc_ok),
        .op_done(c_od), .op_idx(c_oi), .op_cls(c_oc), .prot_v(c_busy), .prot_idx(c_ix),
        .iss_v(c_iv), .iss_urg(c_iu), .iss_idx(c_ii), .iss_la(c_il), .iss_take(c_take),
        .fill_v(fl_c), .fill_idx(fl_ix), .fill_data(c_rd_data), .rd_idx(c_ix), .rd_data(c_rd), .rd_valid(c_rv));
    neo_lc #(.N(8), .IW(3)) lc_s (
        .clk(ui_clk), .flush(flush),
        .op_v(s_dem | sh_hv), .op_la(s_dem ? sum_s[26:4] : sum_sh[26:4]), .op_urg(s_dem), .op_alloc_ok(alloc_ok),
        .op_done(s_od), .op_idx(s_oi), .op_cls(s_oc), .prot_v(s_busy), .prot_idx(s_ix),
        .iss_v(s_iv), .iss_urg(s_iu), .iss_idx(s_ii), .iss_la(s_il), .iss_take(s_take),
        .fill_v(fl_s), .fill_idx(fl_ix), .fill_data(c_rd_data), .rd_idx(s_ix), .rd_data(s_rd), .rd_valid(s_rv));
    neo_lc #(.N(4), .IW(2)) lc_p (
        .clk(ui_clk), .flush(flush),
        .op_v(p_dem | pp_v), .op_la(p_dem ? sum_p[26:4] : pp_la), .op_urg(p_dem), .op_alloc_ok(alloc_ok),
        .op_done(p_od), .op_idx(p_oi), .op_cls(p_oc), .prot_v(p_busy), .prot_idx(p_ix),
        .iss_v(p_iv), .iss_urg(p_iu), .iss_idx(p_ii), .iss_la(p_il), .iss_take(p_take),
        .fill_v(fl_p), .fill_idx(fl_ix[1:0]), .fill_data(c_rd_data), .rd_idx(p_ix), .rd_data(p_rd), .rd_valid(p_rv));
    // which wanted line goes to the DDR2 next: urgent ones first (sprites, fix, 68k), then hints (sprites, fix), then the 68k's next line
    reg [1:0] pick;                                             // 0 none, 1 C, 2 S, 3 P
    always @* begin
        if (c_iv && c_iu) pick = 2'd1;
        else if (s_iv && s_iu) pick = 2'd2;
        else if (c_iv) pick = 2'd1;
        else if (s_iv) pick = 2'd2;
        else if (p_iv) pick = 2'd3;
        else pick = 2'd0;
    end
    wire c_go = (pick != 2'd0) && !c_full && (!c_en || c_rdy) && !inval_s[1];
    assign c_take = c_go && pick == 2'd1;                      // the wanted line becomes in flight at the edge of the command
    assign s_take = c_go && pick == 2'd2;
    assign p_take = c_go && pick == 2'd3;
    reg [7:0] lat = 8'd0;
    reg       lat_run = 1'b0;
    always @(posedge ui_clk) begin
        pm_s <= {pm_s[1:0], pm_req_tgl};
        s_s  <= {s_s[1:0],  s_req_tgl};
        c_s  <= {c_s[1:0],  c_req_tgl};
        ch_s <= {ch_s[1:0], ch_hint_tgl};
        sh_s <= {sh_s[1:0], sh_hint_tgl};
        inval_s <= {inval_s[0], inval};
        if (inval_s[1]) drain <= 1'b1;
        else if (tq_n == 3'd0) drain <= 1'b0;
        if (ui_rst) begin
            c_en <= 1'b0; tq_w <= 3'd0; tq_r <= 3'd0; lat_run <= 1'b0; c_busy <= 1'b0; s_busy <= 1'b0; p_busy <= 1'b0; ch_hv <= 1'b0; sh_hv <= 1'b0; pp_v <= 1'b0;
            drain <= 1'b1;
        end else begin
            if (c_en && c_rdy) c_en <= 1'b0;
            // ---- the caches' operations: demands first, then the hint / next line
            if (inval_s[1]) begin ch_hv <= 1'b0; sh_hv <= 1'b0; pp_v <= 1'b0; end
            if (ch_p && !ch_hv) begin ch_ha <= ch_hint_addr; ch_hv <= 1'b1; ch_t <= ch_s[2]; end
            if (sh_p && !sh_hv) begin sh_ha <= sh_hint_addr; sh_hv <= 1'b1; sh_t <= sh_s[2]; end
            if (c_od) begin
                if (c_dem) begin
                    c_busy <= 1'b1; c_ix <= c_oi; c_half <= sum_c[3]; c_t <= c_s[2];
                    if (c_oc == 2'd0) st_c_hit <= st_c_hit + 1'd1; else if (c_oc == 2'd1) st_c_pend <= st_c_pend + 1'd1; else st_c_miss <= st_c_miss + 1'd1;
                end else begin
                    ch_hv <= 1'b0;
                    if (c_oc == 2'd2) st_c_hint <= st_c_hint + 1'd1;
                end
            end
            if (s_od) begin
                if (s_dem) begin
                    s_busy <= 1'b1; s_ix <= s_oi; s_w <= sum_s[3:1]; s_t <= s_s[2];
                    if (s_oc == 2'd0) st_s_hit <= st_s_hit + 1'd1; else if (s_oc == 2'd1) st_s_pend <= st_s_pend + 1'd1; else st_s_miss <= st_s_miss + 1'd1;
                end else begin
                    sh_hv <= 1'b0;
                    if (s_oc == 2'd2) st_s_hint <= st_s_hint + 1'd1;
                end
            end
            if (p_od) begin
                if (p_dem) begin
                    p_busy <= 1'b1; p_ix <= p_oi; p_w <= sum_p[3:1]; pm_t <= pm_s[2];
                    pp_v <= 1'b1; pp_la <= sum_p[26:4] + 1'b1;
                    if (p_oc == 2'd0) st_p_hit <= st_p_hit + 1'd1; else if (p_oc == 2'd1) st_p_pend <= st_p_pend + 1'd1; else st_p_miss <= st_p_miss + 1'd1;
                end else pp_v <= 1'b0;
            end
            // ---- the answers to the demands, when the line is there
            if (c_busy && c_rv && !inval_s[1]) begin
                c_data <= c_half ? c_rd[127:64] : c_rd[63:0]; c_ack_tgl <= ~c_ack_tgl; c_busy <= 1'b0;
            end
            if (s_busy && s_rv && !inval_s[1]) begin
                s_data <= s_rd[16 * s_w +: 16]; s_ack_tgl <= ~s_ack_tgl; s_busy <= 1'b0;
            end
            if (p_busy && p_rv && !inval_s[1]) begin
                pm_data <= p_rd[16 * p_w +: 16]; pm_ack_tgl <= ~pm_ack_tgl; p_busy <= 1'b0;
            end
            // ---- to the DDR2
            if (c_go) begin
                c_en <= 1'b1;
                c_cmd <= 3'b001;
                st_c_cmds <= st_c_cmds + 1'd1;
                case (pick)
                2'd1: begin c_addr_o <= {1'b0, c_il, 3'b000}; tagq[tq_w[1:0]] <= {K_C, c_ii}; end
                2'd2: begin c_addr_o <= {1'b0, s_il, 3'b000}; tagq[tq_w[1:0]] <= {K_S, s_ii}; end
                default: begin c_addr_o <= {1'b0, p_il, 3'b000}; tagq[tq_w[1:0]] <= {K_P, 1'b0, p_ii}; end
                endcase
                tq_w <= tq_w + 1'd1;
            end
            if (c_rd_valid) begin
                tq_r <= tq_r + 1'd1;
            end
            // a flush drops the lines a demand waits for: the demand is taken again when the caches are open (the machine's toggle has not been answered)
            if (inval_s[1]) begin
                if (c_busy) begin c_busy <= 1'b0; c_t <= ~c_t; end
                if (s_busy) begin s_busy <= 1'b0; s_t <= ~s_t; end
                if (p_busy) begin p_busy <= 1'b0; pm_t <= ~pm_t; end
            end
            // the longest wait of a read, from the command to the line (one read in flight is the usual case)
            if (c_go) begin lat <= 8'd0; lat_run <= (tq_n == 3'd0); end
            else if (lat_run && lat != 8'hFF) lat <= lat + 1'd1;
            if (c_rd_valid) begin
                lat_run <= 1'b0;
                if (lat_run && lat > st_c_lat_max) st_c_lat_max <= lat;
            end
        end
    end

    // ================================================================ e_*: V ROM, M1 ROM, pictures, images
    (* ASYNC_REG = "TRUE" *) reg [2:0] va_s = 3'b000, vb_s = 3'b000, mz_s = 3'b000;
    reg va_t = 1'b0, vb_t = 1'b0, mz_t = 1'b0;
    wire va_p = (va_s[2] != va_t);
    wire vb_p = (vb_s[2] != vb_t);
    wire mz_p = (mz_s[2] != mz_t);
    // the DDR2 byte address of each channel's request
    wire [26:0] va_ab = (va_addr[24] ? v2_base[26:0] : v1_base[26:0]) + {3'd0, va_addr[23:0]};
    wire [26:0] vb_ab = (vb_addr[24] ? v2_base[26:0] : v1_base[26:0]) + {3'd0, vb_addr[23:0]};
    wire [26:0] mz_ab = m1_base[26:0] + {8'd0, mz_addr[18:0]};
    // one line per channel
    reg [22:0]  va_tag = 23'd0, vb_tag = 23'd0, mz_tag = 23'd0;
    reg         va_ok = 1'b0, vb_ok = 1'b0, mz_ok = 1'b0;
    reg [127:0] va_line = 128'd0, vb_line = 128'd0, mz_line = 128'd0;
    wire va_hit = va_ok && va_tag == va_ab[26:4];
    wire vb_hit = vb_ok && vb_tag == vb_ab[26:4];
    wire mz_hit = mz_ok && mz_tag == mz_ab[26:4];

    // the image mover
    localparam [3:0] M_IDLE = 4'd0, M_RD = 4'd1, M_RDW = 4'd2, M_PUT = 4'd3, M_GET = 4'd4, M_GETW = 4'd5, M_GOT = 4'd6, M_WR = 4'd7, M_WRW = 4'd8, M_DONE = 4'd9,
                      M_CPR = 4'd10, M_CPRW = 4'd11, M_CPW = 4'd12, M_CPWW = 4'd13;
    reg [3:0]   ms = M_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] mg_s = 3'b000;
    reg [3:0]   m_cmd = 4'd0;
    reg [15:0]  m_wmask = 16'h0000;                // byte mask of the mover's writes (1 = not written)
    reg [27:0]  cp_s = 28'd0, cp_d = 28'd0, cp_n = 28'd0;
    reg [26:0]  m_ab = 27'd0;                    // DDR2 byte address of the current line
    reg [12:0]  m_line = 13'd0;                  // line counter
    reg [12:0]  m_lines = 13'd0;                 // lines in the image
    reg [4:0]   m_i = 5'd0;                      // word / byte in the line
    reg [127:0] m_buf = 128'd0;
    reg         m_rd_req = 1'b0, m_wr_req = 1'b0, m_rd_done = 1'b0, m_wr_done = 1'b0;
    reg [26:0]  m_req_ab = 27'd0;
    reg [127:0] m_req_data = 128'd0;
    assign mv_busy = (ms != M_IDLE);

    // the e_ engine
    localparam [2:0] E_IDLE = 3'd0, E_RDI = 3'd1, E_RDW = 3'd2, E_WRI = 3'd3;
    reg [2:0]   es = E_IDLE;
    reg [2:0]   e_who = 3'd0;                    // 1 va, 2 vb, 3 mz, 4 mover, 5 picture, 6 backup RAM
    reg [26:0]  e_ab = 27'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] bk_s = 3'b000;
    reg         bk_t = 1'b0;
    wire        bk_p = (bk_s[2] != bk_t);
    wire [26:0] bk_ab = img_base[26:0] + 27'h10000 + {11'd0, bk_addr[15:1], 1'b0};
    always @(posedge ui_clk) begin
        bk_s <= {bk_s[1:0], bk_req_tgl};
        va_s <= {va_s[1:0], va_req_tgl};
        vb_s <= {vb_s[1:0], vb_req_tgl};
        mz_s <= {mz_s[1:0], mz_req_tgl};
        mg_s <= {mg_s[1:0], mv_go_tgl};
        pw_rdy <= 1'b0;
        m_rd_done <= 1'b0;
        m_wr_done <= 1'b0;
        if (ui_rst) begin
            es <= E_IDLE; e_en <= 1'b0; ms <= M_IDLE; va_ok <= 1'b0; vb_ok <= 1'b0; mz_ok <= 1'b0;
        end else begin
            case (es)
            E_IDLE: begin
                if (va_p) begin
                    if (va_hit) begin va_data <= va_line[8 * va_ab[3:0] +: 8]; va_ack_tgl <= ~va_ack_tgl; va_t <= va_s[2]; end
                    else begin e_ab <= va_ab; e_who <= 3'd1; es <= E_RDI; st_va_miss <= st_va_miss + 1'd1; end
                end else if (vb_p) begin
                    if (vb_hit) begin vb_data <= vb_line[8 * vb_ab[3:0] +: 8]; vb_ack_tgl <= ~vb_ack_tgl; vb_t <= vb_s[2]; end
                    else begin e_ab <= vb_ab; e_who <= 3'd2; es <= E_RDI; end
                end else if (mz_p) begin
                    if (mz_hit) begin mz_data <= mz_line[8 * mz_ab[3:0] +: 8]; mz_ack_tgl <= ~mz_ack_tgl; mz_t <= mz_s[2]; end
                    else begin e_ab <= mz_ab; e_who <= 3'd3; es <= E_RDI; st_mz_miss <= st_mz_miss + 1'd1; end
                end else if (bk_p) begin
                    if (bk_wr) begin
                        e_addr <= {1'b0, bk_ab[26:4], 3'b000};
                        e_wdf_data <= {8{bk_wdata}};
                        e_wdf_mask <= ~({14'd0, bk_be} << {bk_ab[3:1], 1'b0});
                        e_cmd <= 3'b000; e_en <= 1'b1; es <= E_WRI; e_who <= 3'd6;
                    end else begin e_ab <= bk_ab; e_who <= 3'd6; es <= E_RDI; end
                end else if (pw_en && !pw_rdy) begin
                    e_wdf_mask <= 16'h0000;
                    e_addr <= pw_addr; e_wdf_data <= pw_data; e_cmd <= 3'b000; e_en <= 1'b1; es <= E_WRI; e_who <= 3'd5;
                end else if (m_rd_req) begin
                    e_ab <= m_req_ab; e_who <= 3'd4; es <= E_RDI; m_rd_req <= 1'b0;
                end else if (m_wr_req) begin
                    e_wdf_mask <= m_wmask;
                    e_addr <= {1'b0, m_req_ab[26:1]} ; e_wdf_data <= m_req_data; e_cmd <= 3'b000; e_en <= 1'b1; es <= E_WRI; e_who <= 3'd4; m_wr_req <= 1'b0;
                end
            end
            E_RDI: begin
                e_en <= 1'b1; e_cmd <= 3'b001; e_addr <= {1'b0, e_ab[26:4], 3'b000};
                es <= E_RDW;
            end
            E_RDW: begin
                if (e_en && e_rdy) begin e_en <= 1'b0; st_e_cmds <= st_e_cmds + 1'd1; end
                if (e_rd_valid) begin
                    es <= E_IDLE;
                    case (e_who)
                    3'd1: begin va_line <= c_rd_data; va_tag <= e_ab[26:4]; va_ok <= 1'b1; va_data <= c_rd_data[8 * e_ab[3:0] +: 8]; va_ack_tgl <= ~va_ack_tgl; va_t <= va_s[2]; end
                    3'd2: begin vb_line <= c_rd_data; vb_tag <= e_ab[26:4]; vb_ok <= 1'b1; vb_data <= c_rd_data[8 * e_ab[3:0] +: 8]; vb_ack_tgl <= ~vb_ack_tgl; vb_t <= vb_s[2]; end
                    3'd3: begin mz_line <= c_rd_data; mz_tag <= e_ab[26:4]; mz_ok <= 1'b1; mz_data <= c_rd_data[8 * e_ab[3:0] +: 8]; mz_ack_tgl <= ~mz_ack_tgl; mz_t <= mz_s[2]; end
                    3'd6: begin bk_line <= c_rd_data; bk_ack_tgl <= ~bk_ack_tgl; bk_t <= bk_s[2]; end
                    default: begin m_buf <= c_rd_data; m_rd_done <= 1'b1; end
                    endcase
                end
            end
            E_WRI: if (e_rdy) begin
                e_en <= 1'b0; st_e_cmds <= st_e_cmds + 1'd1; es <= E_IDLE;
                if (e_who == 3'd5) pw_rdy <= 1'b1;
                else if (e_who == 3'd6) begin bk_ack_tgl <= ~bk_ack_tgl; bk_t <= bk_s[2]; end
                else m_wr_done <= 1'b1;
            end
            default: es <= E_IDLE;
            endcase

            // ---------------------------------------------------------------- the image mover
            img_we <= 1'b0;
            case (ms)
            M_IDLE: if (mg_s[2] != mg_s[1] && mv_cmd[3]) begin
                cp_s <= cp_src[27:0]; cp_d <= cp_dst[27:0]; cp_n <= cp_len[27:0];
                m_wmask <= (mv_cmd[1:0] == 2'd1) ? 16'hCCCC : (mv_cmd[1:0] == 2'd2) ? 16'h3333 : 16'h0000;   // 1: bytes 0,1 of every four, 2: bytes 2,3
                ms <= (cp_len[27:4] == 24'd0) ? M_DONE : M_CPR;
            end else if (mg_s[2] != mg_s[1]) begin
                m_cmd <= mv_cmd;
                m_wmask <= 16'h0000;
                m_line <= 13'd0;
                // lines of 16 bytes: LO 4096, backup 4096, memory card 128 (2 KB), XRAM 512
                m_lines <= (mv_cmd[1:0] == 2'd2) ? 13'd128 : (mv_cmd[1:0] == 2'd3) ? 13'd512 : 13'd4096;
                img_sel <= mv_cmd[1:0];
                // image 1 (the backup RAM) is the DDR2 itself: nothing to move; LO ROM has no save
                ms <= (mv_cmd[1:0] == 2'd1 || (mv_cmd[2] && mv_cmd[1:0] == 2'd0)) ? M_DONE : mv_cmd[2] ? M_GET : M_RD;
            end
            // load: DDR2 line -> machine RAM
            M_RD: begin
                m_req_ab <= img_base[26:0] + (m_cmd[1:0] == 2'd0 ? 27'h0 : m_cmd[1:0] == 2'd2 ? 27'h20000 : 27'h22000) + {m_line, 4'd0};
                m_rd_req <= 1'b1; ms <= M_RDW;
            end
            M_RDW: if (m_rd_done) begin m_i <= 5'd0; ms <= M_PUT; end
            M_PUT: begin
                if (m_cmd[1:0] == 2'd0) begin
                    img_addr <= {m_line[11:0], m_i[3:0]};
                    img_wdata <= {8'd0, m_buf[8 * m_i[3:0] +: 8]};
                    img_we <= 1'b1;
                    if (m_i == 5'd15) begin m_line <= m_line + 1'd1; ms <= (m_line + 1'd1 == m_lines) ? M_DONE : M_RD; end
                    m_i <= m_i + 1'd1;
                end else begin
                    img_addr <= {1'b0, m_line[11:0], m_i[2:0]};        // 16 bit word address: 8 words a line
                    img_wdata <= m_buf[16 * m_i[2:0] +: 16];
                    img_we <= 1'b1;
                    if (m_i == 5'd7) begin m_line <= m_line + 1'd1; ms <= (m_line + 1'd1 == m_lines) ? M_DONE : M_RD; end
                    m_i <= m_i + 1'd1;
                end
            end
            // save: machine RAM -> DDR2 line (LO ROM has nothing to save). The address leaves in one clock, the block RAM answers in the next,
            // neo_core's select register adds none: word n is on img_rdata two clocks after img_addr took its address.
            M_GET: begin m_i <= 5'd0; ms <= M_GETW; end
            M_GETW: begin
                if (m_i < 5'd8) img_addr <= {1'b0, m_line[11:0], m_i[2:0]};
                if (m_i >= 5'd2) m_buf[16 * (m_i[2:0] - 3'd2) +: 16] <= img_rdata;
                if (m_i == 5'd9) ms <= M_WR;
                m_i <= m_i + 1'd1;
            end
            M_WR: begin
                m_req_ab <= img_base[26:0] + (m_cmd[1:0] == 2'd2 ? 27'h20000 : 27'h22000) + {m_line, 4'd0};
                m_req_data <= m_buf;
                m_wr_req <= 1'b1; ms <= M_WRW;
            end
            M_WRW: if (m_wr_done) begin
                m_line <= m_line + 1'd1;
                ms <= (m_line + 1'd1 == m_lines) ? M_DONE : M_GET;
            end
            // copy: one line at a time, read then write
            M_CPR: begin m_req_ab <= {cp_s[26:4], 4'd0}; m_rd_req <= 1'b1; ms <= M_CPRW; end
            M_CPRW: if (m_rd_done) ms <= M_CPW;
            M_CPW: begin m_req_ab <= {cp_d[26:4], 4'd0}; m_req_data <= m_buf; m_wr_req <= 1'b1; ms <= M_CPWW; end
            M_CPWW: if (m_wr_done) begin
                cp_s <= cp_s + 28'd16; cp_d <= cp_d + 28'd16; cp_n <= cp_n - 28'd16;
                ms <= (cp_n == 28'd16) ? M_DONE : M_CPR;
            end
            M_DONE: begin mv_done_tgl <= ~mv_done_tgl; ms <= M_IDLE; end
            default: ms <= M_IDLE;
            endcase
        end
    end
endmodule
