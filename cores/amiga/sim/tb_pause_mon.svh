// T85 Freeze bench, included at the end of tb_l2 (module scope). With PAUSE=1 it pauses the machine at random moments
// (pause_req, the contract's machine pause), holds it for a while and lets it go, and checks, in the same run:
//  - the pause takes effect and `paused` rises (all DDR2 traffic of the machine done) within a bounded time
//  - while held: no ce28 tick, no command on the c_ or e_ port, every output of the Minimig frozen, no frame completes,
//    the picture that is shown stays the last complete frame, the audio is silent (AUDIO runs)
//  - every write the chip bus or the CPU's fast RAM port took reached the DDR2 exactly once, in order (scoreboard), and
//    nothing is outstanding when `paused` rises
//  - the machine goes on identically: the log (+log=FILE) holds hashes of the chip bus cycles per window of 100,000 ce28
//    ticks, of the fast RAM writes, of the picture per frame, and of the frame buffers the application is shown; a run with
//    +npause=0 is the reference; cmp_pause.py compares the two logs.
// Plusargs: +onsdc=1 (every pause is requested while the sector service is transferring)  +onpend=1 (every second pause is requested while a chip bus request is unanswered)  +verify=1 (end of run: the memories the 'pause' ROM copies through equal the ROM)  +log=FILE  +npause=N  +pfirst=TICK  +pstep=TICKS  +hold_us=N (all holds; -2 = random 0-4000 clocks; default a mix from 16 clocks to 62 ms)
integer pm_tick = 0;                            // ce28 ticks so far
always @(posedge sys_clk) if (core.ce28) pm_tick <= pm_tick + 1;
integer pm_lg = 0;
string  pm_name = "pause.log";
integer pm_errors = 0;
integer pm_audio_seen = 0, pm_verify = 0, pm_onpend = 0, pm_onsdc = 0;
integer pm_npause = 6, pm_pfirst = 500000, pm_pstep = 900000, pm_hold_us = -1;
localparam [26:0] PM_BASE = 27'h0800000;
localparam integer PM_WIN = 100000;

function [31:0] pm_mix(input [31:0] h, input [31:0] x);
    reg [31:0] t;
    begin t = (h ^ x) * 32'h01000193; pm_mix = {t[26:0], t[31:27]} ^ (t >> 3); end
endfunction

// ---- the scoreboard: writes the machine took, writes the DDR2 got
logic [170:0] pm_qc[$];                         // chip bus writes, in order
logic [170:0] pm_qp[$];                         // fast RAM (CPU port) writes, in order
function [170:0] pm_exp(input [21:0] a, input [1:0] be, input [15:0] d);   // {c_addr, mask, data} as the DDR2 sees it
    reg [26:0] ad; reg [15:0] mk; reg [127:0] dt;
    begin
        ad = PM_BASE + {5'd0, a[21:3], 3'd0};
        mk = 16'hFFFF & ~({14'd0, be[0], be[1]} << {a[2:0], 1'b0});
        dt = {8{d[7:0], d[15:8]}};
        pm_exp = {ad, mk, dt};
    end
endfunction
integer pm_nw_chip = 0, pm_nw_p2 = 0, pm_nw_ddr = 0;
always @(posedge sys_clk) if (PAUSE) begin
    if (core.mem.take_chip && core.mem.we) begin pm_qc.push_back(pm_exp(core.mem.addr, core.mem.be, core.mem.din)); pm_nw_chip <= pm_nw_chip + 1; end
    if (core.mem.go_p2 && core.mem.p2_wr) begin pm_qp.push_back(pm_exp(core.mem.p2_addr, core.mem.p2_be, core.mem.p2_din)); pm_nw_p2 <= pm_nw_p2 + 1; end
end
always @(posedge ui_clk) if (PAUSE) begin
    if (core.c_en && core.c_rdy && core.c_cmd == 3'b000) begin
        pm_nw_ddr <= pm_nw_ddr + 1;
        if (pm_qc.size() > 0 && pm_qc[0] == {core.c_addr, core.c_wdf_mask, core.c_wdf_data}) void'(pm_qc.pop_front());
        else if (pm_qp.size() > 0 && pm_qp[0] == {core.c_addr, core.c_wdf_mask, core.c_wdf_data}) void'(pm_qp.pop_front());
        else begin
            pm_errors <= pm_errors + 1;
            if (pm_errors < 4) $display("FAIL: DDR2 write %07x mask %04x data %032x matches no outstanding write (lost, duplicated or reordered) at %0t; heads: chip %0d %s p2 %0d", core.c_addr, core.c_wdf_mask, core.c_wdf_data, $time, pm_qc.size(), (pm_qc.size() > 0) ? $sformatf("%07x %04x %032x", pm_qc[0][170:144], pm_qc[0][143:128], pm_qc[0][127:0]) : "-", pm_qp.size());
        end
    end
end

// ---- the log: chip bus cycles per window of PM_WIN ticks, fast RAM writes, picture per frame
reg [31:0] pm_hh = 32'h811C9DC5; integer pm_hwin = 0, pm_hcnt = 0;
reg [31:0] pm_ph = 32'h811C9DC5; integer pm_pcnt = 0;
always @(posedge sys_clk) if (PAUSE && pm_lg != 0) begin
    if (core.mem.take_chip) begin
        if (pm_tick / PM_WIN != pm_hwin) begin
            $fdisplay(pm_lg, "H %0d %08x %0d", pm_hwin, pm_hh, pm_hcnt);
            pm_hwin = pm_tick / PM_WIN; pm_hh = 32'h811C9DC5; pm_hcnt = 0;
        end
        pm_hh = pm_mix(pm_mix(pm_hh, {core.mem.we, core.mem.be, 5'd0, core.mem.addr}), {16'd0, core.mem.din});
        pm_hcnt = pm_hcnt + 1;
    end
    if (core.mem.go_p2) begin
        pm_ph = pm_mix(pm_mix(pm_ph, {core.mem.p2_wr, core.mem.p2_be, 5'd0, core.mem.p2_addr}), {16'd0, core.mem.p2_din});
        pm_pcnt = pm_pcnt + 1;
        if (pm_pcnt % 256 == 0) $fdisplay(pm_lg, "P %0d %08x", pm_pcnt, pm_ph);
    end
end
// the picture, hashed per ce28 tick like tb_ideal (a frame is the stream between two vblank ends)
reg [31:0] pm_fh = 0; integer pm_fn = 0, pm_npix = 0; reg pm_vb_d = 1;
always @(posedge sys_clk) if (PAUSE && core.ce28) begin
    pm_vb_d <= core.vbl_o;
    if (!core.hbl_o && !core.vbl_o) begin pm_fh <= {pm_fh[30:0], pm_fh[31]} ^ {core.vr, core.vg, core.vb, 8'd0, 4'd0} ^ pm_npix; pm_npix <= pm_npix + 1; end
    if (pm_vb_d && !core.vbl_o) begin
        if (pm_fn > 0 && pm_lg != 0) $fdisplay(pm_lg, "F %0d %08x %0d", pm_fn, pm_fh, pm_npix);
        pm_fn <= pm_fn + 1; pm_fh <= 0; pm_npix <= 0;
    end
end

// ---- what the application shows: a hash of the frame buffer it was told is the latest
function [31:0] pm_fbh(input integer b);
    integer r, c; reg [31:0] h;
    begin
        h = 32'h811C9DC5;
        for (r = 0; r < 540; r = r + 9) for (c = 0; c < 720; c = c + 1) h = pm_mix(h, {16'd0, fb_peek(b, r, c)});
        pm_fbh = h;
    end
endfunction
reg [31:0] pm_last_hash = 0; integer pm_nshown = 0;
task pm_shown(input integer b);
    begin
        if (PAUSE) begin
            pm_last_hash = pm_fbh(b); pm_nshown = pm_nshown + 1;
            if (pm_lg != 0) $fdisplay(pm_lg, "B %0d %08x", pm_nshown, pm_last_hash);
        end
    end
endtask

// ---- everything the Minimig shows on its pins; it must not move while the machine is held
wire [255:0] pm_sig = {core.vr, core.vg, core.vb, core.hs, core.vs, core.hbl_o, core.vbl_o, core.lace_o, core.field1_o, core.hires_o,
    core.au_l, core.au_r, core.ram_address, core.n_bhe, core.n_ble, core.n_we, core.n_oe, core.refresh, core.fastram_sel,
    core.fastram_addr, core.fastram_wr, core.fastram_din, core.fastram_lds, core.fastram_uds, core.phase, core.cpu_nrst,
    core.sdc_rd, core.sdc_wr, core.sdc_sector, core.pwr_led, core.fdd_led, core.hdd_led, core.ram_data, core.kml};
reg        pm_hold = 0;                         // the machine is held and acknowledged: everything below must stay still
reg [255:0] pm_sig0 = 0;
integer    pm_tick0 = 0;
always @(posedge sys_clk) if (PAUSE && pm_hold) begin
    if (core.ce28) begin pm_errors <= pm_errors + 1; $display("FAIL: ce28 tick while held at %0t", $time); end
    if (pm_sig !== pm_sig0) begin pm_errors <= pm_errors + 1; $display("FAIL: a Minimig output moved while held at %0t", $time); pm_sig0 <= pm_sig; end
    if (pm_tick != pm_tick0) begin pm_errors <= pm_errors + 1; $display("FAIL: tick counter moved while held"); pm_tick0 <= pm_tick; end
end

always @(posedge ui_clk) if (PAUSE && pm_hold) begin
    if (core.c_en || core.e_en || core.c_wdf_wren || core.e_wdf_wren) begin pm_errors <= pm_errors + 1; $display("FAIL: DDR2 command while held at %0t", $time); end
end

// when the sector service works (to aim the pauses of the disk run at it)
integer pm_sdc_first = -1, pm_sdc_last = -1, pm_sdc_clk = 0;
always @(posedge sys_clk) if (PAUSE && core.sdc.s != 4'd0) begin
    if (pm_sdc_first < 0) pm_sdc_first = pm_tick;
    pm_sdc_last = pm_tick; pm_sdc_clk = pm_sdc_clk + 1;
end

// ---- the pauses
integer pm_k, pm_n, pm_i, pm_hold_clk, pm_seed = 32'h2545F491;
integer pm_busy_mem = 0, pm_busy_fb = 0, pm_busy_sdc = 0, pm_busy_pend = 0, pm_mid_frame = 0, pm_ack_max = 0;
integer pm_fr0; reg [31:0] pm_hash0;
function integer pm_rand(input integer lim);
    begin pm_seed = pm_seed * 1103515245 + 12345; pm_rand = ((pm_seed >> 8) & 32'hFFFFFF) % lim; end
endfunction
task pm_do_pause(input integer k, input integer hold_clk);
    integer ack, t_req, hold_ticks;
    reg b_mem, b_fb, b_sdc, b_pend;
    begin
        b_mem = !core.mem.quiet; b_fb = !core.fb.quiet; b_sdc = !core.sdc.quiet; b_pend = core.m_pend;
        pm_busy_mem = pm_busy_mem + b_mem; pm_busy_fb = pm_busy_fb + b_fb; pm_busy_sdc = pm_busy_sdc + b_sdc; pm_busy_pend = pm_busy_pend + b_pend;
        if (core.fb.in_frame) pm_mid_frame = pm_mid_frame + 1;
        t_req = pm_tick;
        if (AUDIO && al >= 24'sd100000) pm_audio_seen = 1;
        if (AUDIO && pm_audio_seen && al < 24'sd100000) begin pm_errors = pm_errors + 1; $display("FAIL: audio not playing before the pause (%0d)", al); end
        pause_req = 1;
        ack = 0;
        while (!paused && ack < 2000000) begin @(posedge sys_clk); ack = ack + 1; end
        if (!paused) begin pm_errors = pm_errors + 1; $display("FAIL: pause %0d never acknowledged", k); end
        else begin
            if (ack > pm_ack_max) pm_ack_max = ack;
            // the status register shows it too
            cw_read(17'h0A8);
            if (!rd8[6]) begin pm_errors = pm_errors + 1; $display("FAIL: STATUS bit 6 (paused) not set"); end
            if (pm_qc.size() != 0 || pm_qp.size() != 0) begin
                pm_errors = pm_errors + 1; $display("FAIL: paused rose with %0d chip + %0d fast RAM writes not at the DDR2", pm_qc.size(), pm_qp.size());
            end
            if (!core.mem.quiet || !core.fb.quiet || !core.sdc.quiet) begin pm_errors = pm_errors + 1; $display("FAIL: paused rose but a unit is busy"); end
            pm_sig0 = pm_sig; pm_tick0 = pm_tick; pm_fr0 = core.fb.frames; pm_hash0 = pm_fbh(last_idx);
            if (pm_nshown > 0 && pm_hash0 !== pm_last_hash) begin pm_errors = pm_errors + 1; $display("FAIL: the shown buffer is not the last complete frame"); end
            if (core.fb.in_frame && core.fb.fwb == last_idx[1:0]) begin pm_errors = pm_errors + 1; $display("FAIL: the frame in progress writes into the shown buffer"); end
            $display("pause %0d at tick %0d: acknowledged after %0d clocks, %0d ticks after the request (busy mem %0d fb %0d sdc %0d pend %0d, mid frame %0d), hold %0d clocks",
                     k, t_req, ack, pm_tick - t_req, b_mem, b_fb, b_sdc, b_pend, core.fb.in_frame, hold_clk);
            pm_hold = 1;
            hold_ticks = 0;
            repeat (hold_clk) @(posedge sys_clk);
            if (AUDIO && pm_audio_seen && hold_clk > 400000) begin
                if (al !== 24'sd0 || ar !== 24'sd0) begin pm_errors = pm_errors + 1; $display("FAIL: audio not silent while held (%0d %0d)", al, ar); end
                else $display("  audio silent after %0d clocks of hold", hold_clk);
            end
            if (core.fb.frames !== pm_fr0[15:0]) begin pm_errors = pm_errors + 1; $display("FAIL: a frame completed while held"); end
            if (pm_fbh(last_idx) !== pm_hash0) begin pm_errors = pm_errors + 1; $display("FAIL: the shown frame buffer changed while held"); end
            pm_hold = 0;
        end
        pause_req = 0;
        ack = 0;
        while (paused && ack < 100) begin @(posedge sys_clk); ack = ack + 1; end
        if (paused) begin pm_errors = pm_errors + 1; $display("FAIL: paused stays after the request fell"); end
        repeat (200) @(posedge sys_clk);
        if (AUDIO && pm_audio_seen) begin
            repeat (400000) @(posedge sys_clk);
            if (al < 24'sd100000) begin pm_errors = pm_errors + 1; $display("FAIL: audio did not come back after the pause (%0d)", al); end
            else $display("  audio back: left %0d", al);
        end
        if (pm_tick == t_req + 0 && 0) ;
    end
endtask
initial begin
    if (PAUSE) begin
        if ($value$plusargs("log=%s", pm_name)) ;
        if ($value$plusargs("npause=%d", pm_npause)) ;
        if ($value$plusargs("pfirst=%d", pm_pfirst)) ;
        if ($value$plusargs("pstep=%d", pm_pstep)) ;
        if ($value$plusargs("hold_us=%d", pm_hold_us)) ;
        if ($value$plusargs("verify=%d", pm_verify)) ;
        if ($value$plusargs("onpend=%d", pm_onpend)) ;
        if ($value$plusargs("onsdc=%d", pm_onsdc)) ;
        pm_lg = $fopen(pm_name, "w");
        wait (core.hold === 1'b0);
        for (pm_k = 0; pm_k < pm_npause; pm_k = pm_k + 1) begin
            wait (pm_tick >= pm_pfirst + pm_k * pm_pstep);
            if (pm_onsdc) begin wait (core.sdc.s != 4'd0); repeat (pm_rand(400)) @(posedge sys_clk); end   // directed: while the sector service transfers
            else if (pm_onpend && (pm_k % 2)) begin wait (core.m_pend); repeat (pm_rand(40)) @(posedge sys_clk); end   // directed: while a chip request is unanswered
            else repeat (pm_rand(100)) @(posedge sys_clk);
            if (pm_hold_us == -2) pm_hold_clk = pm_rand(4000);       // storm: 0 to 4000 clocks
            else if (pm_hold_us >= 0) pm_hold_clk = pm_hold_us * 31;
            else case (pm_k % 6)
                0: pm_hold_clk = 1900000;       // 60 ms: three output frames
                1: pm_hold_clk = 1300;          // 40 us
                2: pm_hold_clk = 950000;        // 30 ms
                3: pm_hold_clk = 16;
                4: pm_hold_clk = 800000;        // 25 ms
                default: pm_hold_clk = 1950000; // 62 ms
            endcase
            pm_do_pause(pm_k, pm_hold_clk);
        end
    end
end
// the run ends at TICKS ticks; the reference (+npause=0) ends the same way
initial begin
    if (PAUSE) begin
        wait (pm_tick >= TICKS);
        repeat (64) @(posedge sys_clk);
        $display("pause bench: %0d ticks, %0d frames, %0d pauses; pauses found busy: request-pending %0d mem %0d fb %0d sdc %0d, mid-frame %0d, longest ack %0d clocks",
                 pm_tick, core.fb.frames, pm_npause, pm_busy_pend, pm_busy_mem, pm_busy_fb, pm_busy_sdc, pm_mid_frame, pm_ack_max);
        $display("sector service busy: first at tick %0d, last at tick %0d, %0d clocks", pm_sdc_first, pm_sdc_last, pm_sdc_clk);
        $display("writes: chip bus %0d, fast RAM %0d, DDR2 write commands %0d (st_writes %0d); reads %0d, misses %0d, stalled clocks %0d", pm_nw_chip, pm_nw_p2, pm_nw_ddr, core.mem.st_writes, core.mem.st_reads, core.mem.st_miss, core.s_hold);
        if (pm_verify) begin : verify
            integer q, bad_c, bad_f, bad_d;
            bad_c = 0; bad_f = 0; bad_d = 0;
            for (q = 0; q < 4096; q = q + 1) begin
                if (win_peek16(24'h010000 + 2 * q) !== rom[q]) bad_c = bad_c + 1;
                if (win_peek16(24'h200000 + 2 * q) !== rom[q]) bad_f = bad_f + 1;
                if (win_peek16(24'h030000 + 2 * q) !== rom[q]) bad_d = bad_d + 1;
            end
            $display("copy check: chip $10000 bad %0d, fast RAM bad %0d, chip $30000 bad %0d; rounds counted %0d", bad_c, bad_f, bad_d, win_peek16(24'h000100));
            if (bad_c || bad_f || bad_d) begin pm_errors = pm_errors + 1; $display("FAIL: the copy loop's memory differs from the ROM"); end
        end
        if (pm_lg != 0) $fclose(pm_lg);
        if (pm_errors == 0 && errors == 0) $display("PASS tb_pause errors=0"); else $display("FAIL tb_pause errors=%0d", pm_errors + errors);
        $finish;
    end
end
