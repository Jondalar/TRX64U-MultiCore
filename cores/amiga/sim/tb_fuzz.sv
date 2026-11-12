// T85: amiga_mem on its own, driven like the Minimig's chip bus, by a random mix of streams, writes next to them and
// aliasing lines, against a plain shadow array and a DDR2 model with random timing. Every read is checked where the chipset
// and the CPU take their data (the clocks where amiga_ce lets the tick go). The point is to find a read that returns old
// data: races between the line fill, the prefetch, the write-through and the tag install.
//   parameters: MIGLAT MIGMISS MIGSTALL (the DDR2 model), SEED, NCYC (bus cycles), WRP (percent writes), POOL (words of the working set)
`timescale 1ns/1ps
module tb_fuzz;
    parameter MIGLAT = 8, MIGMISS = 0, MIGSTALL = 0;
    parameter SEED = 1;
    parameter NCYC = 2000000;
    parameter WRP = 25;
    parameter POOL = 16384;            // words in the working set (the cache holds 2048)
    parameter IDLEP = 30;              // percent of bus cycles without a chip request
    parameter P2P = 3;                 // percent of the clocks (when idle) in which the CPU's fast RAM port starts a request
    reg clk = 0, ui_clk = 0;
    always #15.86 clk = ~clk;
    always #6.667 ui_clk = ~ui_clk;
    reg flush = 1, por = 1;
    integer cyc = 0;
    always @(posedge clk) begin
        cyc <= cyc + 1;
        if (cyc == 20) por <= 0;
        if (cyc == 900) flush <= 0;
    end

    // chip bus signals
    reg cs = 0, we = 0; reg [21:0] addr = 0; reg [1:0] be = 0; reg [15:0] din = 0;
    wire [15:0] m_dout; wire m_pend;
    wire [15:0] p2_dout; wire p2_ready;
    reg p2_pending = 0, p2_wr = 0, p2_rdy0 = 0; reg [21:0] p2_addr = 0; reg [1:0] p2_be = 0; reg [15:0] p2_din = 0;
    wire p2_sel = p2_pending && (p2_ready === p2_rdy0);        // drops in the clock the ready toggle shows, like the CPU's
    wire ce28; reg [1:0] clk7_cnt = 2'b10; reg clk7_en = 1;
    wire [1:0] phase = clk7_cnt;
    always @(posedge clk) if (ce28) begin clk7_cnt <= clk7_cnt + 2'b01; clk7_en <= (clk7_cnt == 2'b00); end
    amiga_ce ce (.clk(clk), .phase(phase), .pend(m_pend), .pause(1'b0), .ce28(ce28));

    wire ui_rst, calib;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en; wire [127:0] c_wdf_data; wire [15:0] c_wdf_mask; wire c_wdf_wren;
    wire [127:0] app_rd_data; wire app_rd_valid, app_rdy, app_wdf_rdy;
    mig_ui_model #(.MEM_WORDS(1<<20), .RD_LAT(MIGLAT), .MISS(MIGMISS), .STALL_PCT(MIGSTALL), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(c_addr), .app_cmd(c_cmd), .app_en(c_en & (c_cmd == 3'b001 | app_wdf_rdy)), .app_rdy(app_rdy),
        .app_wdf_data(c_wdf_data), .app_wdf_mask(c_wdf_mask), .app_wdf_wren(c_wdf_wren & app_rdy),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_valid));
    wire c_rdy = app_rdy & ((c_cmd == 3'b001) | app_wdf_rdy);

    amiga_mem #(.BASE(27'd0)) mem (
        .sys_clk(clk), .sys_rst(por), .flush(flush), .ce28(ce28), .clk7_en(clk7_en),
        .cs(cs), .we(we), .addr(addr), .be(be), .din(din), .dout(m_dout), .pend(m_pend),
        .p2_sel(p2_sel), .p2_wr(p2_wr), .p2_addr(p2_addr), .p2_be(p2_be), .p2_din(p2_din), .p2_dout(p2_dout), .p2_ready(p2_ready),
        .ui_clk(ui_clk), .ui_rst(ui_rst | ~calib), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(app_wdf_rdy),
        .c_rd_data(app_rd_data), .c_rd_valid(app_rd_valid));


    // ---- event ring for the report of a mismatch
    localparam RN = 1<<14;
    reg [63:0] ev_t [0:RN-1]; reg [7:0] ev_k [0:RN-1]; reg [21:0] ev_a [0:RN-1]; reg [31:0] ev_d [0:RN-1]; reg [7:0] ev_st [0:RN-1];
    integer evp = 0;
    task evlog(input [7:0] k, input [21:0] a, input [31:0] d);
        begin ev_t[evp % RN] = cyc; ev_k[evp % RN] = k; ev_a[evp % RN] = a; ev_d[evp % RN] = d; ev_st[evp % RN] = {mem.fill_inflight, mem.tw_en, mem.st[2:0], 3'd0}; evp = evp + 1; end
    endtask
    function string kn(input [7:0] k);
        case (k) 1: kn = "WR      "; 2: kn = "RD-hit  "; 3: kn = "RD-crit "; 4: kn = "FILL-dem"; 5: kn = "FILL-pf "; 6: kn = "INST    "; 7: kn = "INST-inv"; 8: kn = "WR-miss "; 9: kn = "RD-miss "; default: kn = "?"; endcase
    endfunction
    task evdump(input [21:0] a);
        integer n, e, cnt, st_n;
        begin
            cnt = 0;
            for (n = 0; n < RN && n < evp && cnt < 30; n = n + 1) begin e = (evp - 1 - n) % RN; if (ev_a[e][21:3] == a[21:3]) cnt = cnt + 1; end
            st_n = n;
            for (n = st_n - 1; n >= 0; n = n - 1) begin
                e = (evp - 1 - n) % RN;
                if (ev_a[e][21:3] == a[21:3]) $display("      clk %0d  %s  word %06x  data %08x  [inflight %b tw_en %b st %0d]", ev_t[e], kn(ev_k[e]), ev_a[e], ev_d[e], ev_st[e][7], ev_st[e][6], ev_st[e][5:3]);
            end
        end
    endtask
    always @(posedge clk) begin
        if (mem.st == 3'd1 && mem.r_we && !mem.cf_full && !flush) evlog(mem.hit ? 8'd1 : 8'd8, mem.r_a, {mem.r_be, mem.r_din});
        if (mem.st == 3'd1 && !mem.r_we && mem.hit && !flush) evlog(8'd2, mem.r_a, mem.hit_word);
        if (mem.st == 3'd1 && !mem.r_we && !mem.hit && !flush) evlog(8'd9, mem.r_a, 0);
        if (mem.st == 3'd4 && mem.crit_valid && !flush) evlog(8'd3, mem.r_a, mem.crit_q);
        if (mem.st == 3'd1 && !mem.r_we && !mem.hit && !mem.fill_inflight && !mem.cf_full && !flush) evlog(8'd4, {mem.r_a[21:3], 3'd0}, 0);
        if (mem.st == 3'd1 && !mem.r_we && mem.hit && mem.r_w[2] && !mem.fill_inflight && !mem.nxt_cached && mem.nxt_valid_addr && !mem.cf_full) evlog(8'd5, {mem.nxt_line[18:0], 3'd0}, 0);
        if (mem.fill_done && mem.fill_inflight) evlog(mem.dirty_now ? 8'd7 : 8'd6, {mem.fill_line, 3'd0}, mem.fill_pf);
    end

    // ---- the generator
    reg [15:0] sh [0:(1<<22)-1];
    integer seed = SEED;
    function integer rnd(input integer n);     // 0..n-1
        integer v;
        begin v = $random(seed); if (v < 0) v = -v; rnd = v % n; end
    endfunction
    integer base_a [0:3];              // stream pointers
    integer k, ncyc = 0, nrd = 0, nwr = 0, bad = 0, nchk = 0, nrdy = 0;
    integer last_a = 0;
    reg have = 0; reg [15:0] want = 0; reg [21:0] want_a = 0; reg wasread = 0;
    reg [21:0] hist [0:15]; integer hp = 0;
    integer region;
    initial begin
        for (k = 0; k < 4; k = k + 1) base_a[k] = rnd(POOL);
    end
    function [21:0] pick_addr(input integer dummy);
        integer s, m, a, L;
        begin
            m = rnd(100);
            if (m < 50) begin                         // a stream, one word at a time (or two words and a gap: blitter-like)
                s = rnd(4);
                base_a[s] = base_a[s] + 1 + ((rnd(8) == 0) ? rnd(3) : 0);
                if (base_a[s] >= POOL || rnd(400) == 0) base_a[s] = rnd(POOL);
                a = base_a[s];
            end else if (m < 70) begin                // next to something done lately: same word, same line, next line
                a = hist[rnd(16)];
                case (rnd(4)) 0: a = a; 1: a = a ^ rnd(8); 2: a = a + 8; default: a = a - 8 + rnd(16); endcase
                if (a < 0) a = 0;
                if (a >= POOL) a = a % POOL;
            end else if (m < 85) begin                // aliasing line: same cache index, other tag
                a = hist[rnd(16)];
                L = a >> 3;
                a = ((((L ^ ((1 + rnd(7)) << 8)) ^ ((1 + rnd(7)))) << 3) | rnd(8)) % POOL;      // may or may not alias
            end else a = rnd(POOL);
            pick_addr = a[21:0];
        end
    endfunction
    // slow-RAM-like second region: shift some addresses to 0x400000 words
    function [21:0] region_of(input [21:0] a);
        begin region_of = (a[0] & a[3] & a[7]) ? (a | 22'h200000) : a; end
    endfunction

    // new request at every tick with clk7_en (visible from the next clock, like the chipset's registers)
    always @(posedge clk) if (!flush) begin
        // the chipset and the CPU take the data at the tick that ends phase 0 and at the tick with clk7_en
        if (ce28 && have && (phase == 2'd0 || clk7_en)) begin
            nchk <= nchk + 1;
            if (m_dout !== want) begin
                bad <= bad + 1;
                if (bad < 5) $display("MISMATCH cycle %0d at %0t: word %06x got %04x want %04x (pend %b st %0d fill_inflight %b fill_line %05x pf %b dirty %b crit_valid %b)", ncyc, $time, want_a, m_dout, want, m_pend, mem.st, mem.fill_inflight, mem.fill_line, mem.fill_pf, mem.fill_dirty, mem.crit_valid);
                if (bad < 2) evdump(want_a);
            end
            if (clk7_en) have <= 0;
        end
        if (ce28 && clk7_en) begin
            ncyc <= ncyc + 1;
            if (rnd(100) < IDLEP) begin
                cs <= 0; we <= 0; have <= 0;
            end else begin : issue
                reg [21:0] a; reg [1:0] b; reg [15:0] d; reg w;
                a = region_of(pick_addr(0));
                w = (rnd(100) < WRP);
                b = (rnd(10) == 0) ? (rnd(2) ? 2'b01 : 2'b10) : 2'b11;
                d = $random(seed);
                cs <= 1; we <= w; addr <= a; be <= b; din <= d;
                hist[hp % 16] <= a; hp <= hp + 1;
                if (w) begin
                    if (b[1]) sh[a][15:8] = d[15:8];
                    if (b[0]) sh[a][7:0] = d[7:0];
                    have <= 0; nwr <= nwr + 1;
                end else begin
                    have <= 1; want <= sh[a]; want_a <= a; nrd <= nrd + 1;
                end
            end
        end
        if (ncyc >= NCYC) begin
            $display("fuzz: lat %0d miss %0d stall %0d seed %0d: %0d cycles, %0d reads (%0d checked), %0d writes, %0d mismatches; cache reads %0d miss %0d prefetch %0d",
                MIGLAT, MIGMISS, MIGSTALL, SEED, ncyc, nrd, nchk, nwr, bad, mem.st_reads, mem.st_miss, mem.st_pf);
            if (bad == 0) $display("PASS tb_fuzz"); else $display("FAIL tb_fuzz");
            $finish;
        end
    end

    // ---- the CPU's fast RAM port: own region, toggle handshake
    reg [15:0] p2_want = 0; reg p2_isrd = 0; integer p2_bad = 0, p2_n = 0;
    always @(posedge clk) if (!flush) begin
        if (p2_pending && p2_ready !== p2_rdy0) begin
            p2_pending <= 0;
            if (p2_isrd) begin
                p2_n <= p2_n + 1;
                if (p2_dout !== p2_want) begin
                    p2_bad <= p2_bad + 1; bad <= bad + 1;
                    if (p2_bad < 4) $display("MISMATCH fast RAM at %0t: word %06x got %04x want %04x", $time, p2_addr, p2_dout, p2_want);
                end
            end
        end else if (!p2_pending && rnd(100) < P2P) begin : p2issue
            reg [21:0] a; reg [1:0] b; reg [15:0] d; reg w;
            a = 22'h100000 + (rnd(4) == 0 ? rnd(POOL) : (p2_addr[13:0] + 1 + rnd(2)) % POOL);
            w = rnd(100) < 40; b = (rnd(8) == 0) ? 2'b01 : 2'b11; d = $random(seed);
            p2_pending <= 1; p2_rdy0 <= p2_ready; p2_wr <= w; p2_addr <= a; p2_be <= b; p2_din <= d; p2_isrd <= !w;
            if (w) begin
                if (b[1]) sh[a][15:8] = d[15:8];
                if (b[0]) sh[a][7:0] = d[7:0];
            end else p2_want <= sh[a];
        end
    end
    integer ii;
    initial begin for (ii = 0; ii < (1<<22); ii = ii + 1) sh[ii] = 16'd0; for (ii = 0; ii < 16; ii = ii + 1) hist[ii] = 0; end
    initial begin #20000000000; $display("FAIL timeout"); $finish; end
endmodule
