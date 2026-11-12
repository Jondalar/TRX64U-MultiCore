// T85: shadow memory checker, included at the end of tb_l2 (module scope), active with +shadow=1 (any bench run).
// Every write the memory unit accepts (chip bus and the CPU's fast RAM port) is recorded in a plain array; every read it
// answers is compared with it, and so is what the chip bus gets at the clocks where the chipset and the CPU take their data.
// The first mismatches are printed with the state of the cache (line, tag, fill / prefetch in flight, dirty flag) and the
// events of the same line before it (writes, answers, fill starts and installs).
// Memory the Amiga never wrote starts as what the DDR2 window held when the run started (Kickstart, zero).
integer sh_on = 0;
integer sh_bad = 0, sh_bad_e2e = 0, sh_checked = 0, sh_checked_e2e = 0, sh_nwr = 0;
reg [15:0] sh [0:(1<<22)-1];
integer sh_i;
initial begin
    if (!$value$plusargs("shadow=%d", sh_on)) sh_on = 0;
    #100;
    if (sh_on) for (sh_i = 0; sh_i < (1<<22); sh_i = sh_i + 1) sh[sh_i] = win_peek16(2 * sh_i);
end

// event ring: {time ns, kind, addr word, data, extra}
localparam SH_RN = 1<<14;
reg [63:0] sh_et [0:SH_RN-1];
reg [7:0]  sh_ek [0:SH_RN-1];
reg [21:0] sh_ea [0:SH_RN-1];
reg [31:0] sh_ed [0:SH_RN-1];
integer sh_ep = 0;
task sh_ev(input [7:0] k, input [21:0] a, input [31:0] d);
    begin sh_et[sh_ep % SH_RN] = $time; sh_ek[sh_ep % SH_RN] = k; sh_ea[sh_ep % SH_RN] = a; sh_ed[sh_ep % SH_RN] = d; sh_ep = sh_ep + 1; end
endtask
task sh_dump(input [21:0] a);
    integer n, e;
    begin
        // oldest first: the last 24 events of this line
        begin : lastn
            integer cnt, st_n;
            cnt = 0;
            for (n = 0; n < SH_RN && n < sh_ep && cnt < 24; n = n + 1) begin
                e = (sh_ep - 1 - n) % SH_RN;
                if (sh_ea[e][21:3] == a[21:3]) cnt = cnt + 1;
            end
            st_n = n;
            for (n = st_n - 1; n >= 0; n = n - 1) begin
                e = (sh_ep - 1 - n) % SH_RN;
                if (sh_ea[e][21:3] == a[21:3])
                    $display("    %0d ns  %s  word %06x  data %08x", sh_et[e], sh_kname(sh_ek[e]), sh_ea[e], sh_ed[e]);
            end
        end
    end
endtask
function string sh_kname(input [7:0] k);
    case (k)
    8'd1: sh_kname = "WR      ";
    8'd2: sh_kname = "RD-hit  ";
    8'd3: sh_kname = "RD-crit ";
    8'd4: sh_kname = "FILL-dem";
    8'd5: sh_kname = "FILL-pf ";
    8'd6: sh_kname = "INST    ";
    8'd7: sh_kname = "INST-inv";
    default: sh_kname = "?       ";
    endcase
endfunction

// ---- writes accepted (same clock the cache command is queued) and reads answered
wire sh_wr_acc = core.mem.st == 3'd1 && core.mem.r_we && !core.mem.cf_full && !core.mem.flush && !core.mem.sys_rst;
wire sh_rd_hit = core.mem.st == 3'd1 && !core.mem.r_we && core.mem.hit && !core.mem.flush && !core.mem.sys_rst;
wire sh_rd_crit = core.mem.st == 3'd4 && core.mem.crit_valid && !core.mem.flush && !core.mem.sys_rst;
reg [15:0] sh_exp, sh_got;
reg [21:0] sh_a;
task sh_fail(input [255:0] what);
    begin
        sh_bad = sh_bad + 1;
        if (sh_bad <= 4) begin
            $display("SHADOW MISMATCH %0s at %0t ns: word %06x (byte %06x) got %04x expected %04x src %s", what, $time, sh_a, {sh_a, 1'b0}, sh_got, sh_exp, core.mem.r_src ? "fast" : "chip");
            $display("    state: st %0d line %05x idx %02x tag_q %03x (valid %b tag %03x want %03x) fill_inflight %b fill_line %05x pf %b dirty %b crit_valid %b tw_en %b cf_full %b",
                core.mem.st, sh_a[21:3], core.mem.idx, core.mem.tag_q, core.mem.tag_q[11], core.mem.tag_q[10:0], core.mem.want_tag,
                core.mem.fill_inflight, core.mem.fill_line, core.mem.fill_pf, core.mem.fill_dirty, core.mem.crit_valid, core.mem.tw_en, core.mem.cf_full);
            sh_dump(sh_a);
        end
    end
endtask
always @(posedge sys_clk) if (sh_on) begin
    if (sh_wr_acc) begin
        sh_a = core.mem.r_a;
        if (core.mem.r_be[1]) sh[sh_a][15:8] = core.mem.r_din[15:8];
        if (core.mem.r_be[0]) sh[sh_a][7:0]  = core.mem.r_din[7:0];
        sh_nwr = sh_nwr + 1;
        sh_ev(8'd1, sh_a, {core.mem.r_src, core.mem.hit, core.mem.r_be, core.mem.r_din});
    end
    if (sh_rd_hit || sh_rd_crit) begin
        sh_a = core.mem.r_a;
        sh_exp = sh[sh_a];
        sh_got = sh_rd_hit ? core.mem.hit_word : core.mem.crit_q;
        sh_checked = sh_checked + 1;
        sh_ev(sh_rd_hit ? 8'd2 : 8'd3, sh_a, {16'd0, sh_got});
        if (sh_got !== sh_exp) sh_fail(sh_rd_hit ? "(hit)" : "(critical word)");
    end
    // fill start / install
    if (core.mem.st == 3'd1 && !core.mem.r_we && !core.mem.hit && !core.mem.fill_inflight && !core.mem.cf_full && !core.mem.flush && !core.mem.sys_rst)
        sh_ev(8'd4, {core.mem.r_a[21:3], 3'd0}, 32'd0);
    if (core.mem.st == 3'd1 && !core.mem.r_we && core.mem.hit && core.mem.r_w[2] && !core.mem.fill_inflight && !core.mem.nxt_cached && core.mem.nxt_valid_addr && !core.mem.cf_full)
        sh_ev(8'd5, {core.mem.nxt_line[18:0], 3'd0}, 32'd0);
    if (core.mem.fill_done && core.mem.fill_inflight)
        sh_ev(core.mem.dirty_now ? 8'd7 : 8'd6, {core.mem.fill_line, 3'd0}, {core.mem.fill_pf, 31'd0});
end

// ---- end to end: what the chip bus carries at the clocks where the CPU and the chipset take it
reg        sh_have = 0;
reg [15:0] sh_want = 0;
reg [21:0] sh_wa = 0;
always @(posedge sys_clk) if (sh_on) begin
    if (core.mem.take_chip && !core.mem.we) begin sh_have <= 1; sh_want <= sh[core.mem.addr]; sh_wa <= core.mem.addr; end
    if (core.mem.take_chip && core.mem.we) sh_have <= 0;
    if (sh_have && core.ce28 && (core.phase == 2'd0 || core.clk7_en) && !core.m_pend) begin
        sh_checked_e2e = sh_checked_e2e + 1;
        if (core.m_dout !== sh_want) begin
            sh_bad_e2e = sh_bad_e2e + 1;
            if (sh_bad_e2e <= 4) begin
                $display("SHADOW E2E MISMATCH at %0t ns: word %06x bus %04x expected %04x phase %0d clk7_en %b pend %b st %0d", $time, sh_wa, core.m_dout, sh_want, core.phase, core.clk7_en, core.m_pend, core.mem.st);
                sh_a = sh_wa; sh_dump(sh_wa);
            end
        end
        if (core.clk7_en) sh_have <= 0;
    end
end
// the first mismatch ends nothing; the totals are printed every 500 ms of simulated time and at the end
always @(posedge sys_clk) if (sh_on && cyc % 15000000 == 0 && cyc > 0)
    $display("SHADOW t=%0d ms: writes %0d, reads checked %0d, end to end %0d, mismatches %0d / %0d", cyc / 31526, sh_nwr, sh_checked, sh_checked_e2e, sh_bad, sh_bad_e2e);

// ---- BOOT: sound present? Swing (max - min) of the Paula output per 100 ms of simulated time (cyc counts sys_clk)
integer au_mn = 0, au_mx = 0, au_dma_n = 0, dk_n = 0;
always @(posedge sys_clk) if (BOOT && core.ce28 && |core.nm.minimig.audio_dmal) au_dma_n = au_dma_n + 1;
always @(posedge sys_clk) if (BOOT && core.sdc_rd[0]) dk_n = dk_n + 1;
always @(posedge sys_clk) if (BOOT) begin
    if ($signed(core.au_l) > au_mx) au_mx = $signed(core.au_l);
    if ($signed(core.au_l) < au_mn) au_mn = $signed(core.au_l);
    if ($signed(core.au_r) > au_mx) au_mx = $signed(core.au_r);
    if ($signed(core.au_r) < au_mn) au_mn = $signed(core.au_r);
    if (cyc % 3152600 == 0 && cyc > 0) begin
        $display("AUDIO t=%0d ms: swing %0d  frames %0d  auden %b dmaen %b audio_dma_req %0d disk_dma %0d", cyc / 31526, au_mx - au_mn, core.fb.frames, core.nm.minimig.PAULA1.auden, core.nm.minimig.PAULA1.dmaen, au_dma_n, dk_n);
        au_dma_n = 0; dk_n = 0;
        au_mn = 0; au_mx = 0;
    end
end

// ---- BOOT: the picture as a PPM every second of simulated time (frame buffer of the frame being written, 720 x 540)
integer pp_n = 0, pp_r, pp_c, pp_f; reg [23:0] pp_px;
task pp_dump(input integer b);
    begin
        pp_f = $fopen($sformatf("shot_%0d.ppm", pp_n), "w");
        $fdisplay(pp_f, "P3\n720 540\n255");
        for (pp_r = 0; pp_r < 540; pp_r = pp_r + 1) begin
            for (pp_c = 0; pp_c < 720; pp_c = pp_c + 1) begin
                pp_px = fb_peek24(b, pp_r, pp_c);
                $fwrite(pp_f, "%0d %0d %0d ", pp_px[23:16], pp_px[15:8], pp_px[7:0]);
            end
            $fwrite(pp_f, "\n");
        end
        $fclose(pp_f); pp_n = pp_n + 1;
    end
endtask
always @(posedge sys_clk) if (BOOT && cyc % 31526000 == 0 && cyc > 0) pp_dump(core.fb.fwb);

// ---- BOOT: the fire buttons of both ports, the left mouse button, Return and Space pressed for +firehold ms every +firestep ms
// from +fire0 ms, to get past the menus of a game; none without +fire0
integer fi_t0 = -1, fi_step = 4000, fi_hold = 150;
initial begin
    if ($value$plusargs("fire0=%d", fi_t0)) begin
        void'($value$plusargs("firestep=%d", fi_step)); void'($value$plusargs("firehold=%d", fi_hold));
        #((fi_t0 * 1000000.0));
        forever begin
            joy1[4] = 1; joy2[4] = 1; keys[1] = 1; keys[60] = 1; cw_write(17'h1C2, 8'h01); $display("FIRE down at %0d ms", cyc / 31526);
            #2000000; $display("FIRE state 2 ms later: _fire0 %b _fire1 %b mouse_buttons %b joystick0 %02x joystick1 %02x", core.nm.minimig._fire0, core.nm.minimig._fire1, core.mbtn, core.joy0, core.joyb);
            #((fi_hold * 1000000.0 - 2000000.0));
            joy1[4] = 0; joy2[4] = 0; keys[1] = 0; keys[60] = 0; cw_write(17'h1C2, 8'h00);
            #(((fi_step - fi_hold) * 1000000.0));
        end
    end
end

// ---- BOOT: +dump_at=MS: at that moment, the histogram of the CPU's read addresses (256-byte blocks, since +dump_from=MS) and a dump of the
// Amiga's memory (window 0 .. 8 MB, file ram.bin, byte order of the Amiga) for static analysis of what the program is doing
integer dm_at = -1, dm_from = 0, dm_f, dm_i, dm_k;
integer dm_hist [0:65535];
initial begin
    for (dm_i = 0; dm_i < 65536; dm_i = dm_i + 1) dm_hist[dm_i] = 0;
    if ($value$plusargs("dump_at=%d", dm_at)) begin
        void'($value$plusargs("dump_from=%d", dm_from));
        #((dm_at * 1000000.0));
        dm_f = $fopen("ram.bin", "wb");
        for (dm_i = 0; dm_i < (1<<22); dm_i = dm_i + 1) $fwrite(dm_f, "%c%c", win_peek16(2 * dm_i) >> 8, win_peek16(2 * dm_i) & 8'hFF);
        $fclose(dm_f);
        for (dm_k = 0; dm_k < 12; dm_k = dm_k + 1) begin : top
            integer best, bi;
            best = 0; bi = -1;
            for (dm_i = 0; dm_i < 65536; dm_i = dm_i + 1) if (dm_hist[dm_i] > best) begin best = dm_hist[dm_i]; bi = dm_i; end
            if (bi >= 0) begin $display("CPU READS block %06x: %0d", bi * 256, best); dm_hist[bi] = 0; end
        end
        $display("DUMP written at %0d ms", cyc / 31526);
    end
end
always @(posedge sys_clk) if (dm_at >= 0 && core.ce28 && core.clk7_en && core.nm.cpu_wrapper.cpu_req && !core.nm.cpu_wrapper.wr && cyc / 31526 >= dm_from)
    dm_hist[core.nm.cpu_wrapper.cpu_addr[23:8]] = dm_hist[core.nm.cpu_wrapper.cpu_addr[23:8]] + 1;
