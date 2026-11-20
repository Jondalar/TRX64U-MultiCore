// T241 S2 (run_irqmir.sh): the probe of the "mirror everything at 1 MHz" bench. Included by tb_c64scpu.v (inside `ifndef C64SCPU_CPU6510), switched on by +IRQMIR.
// It only looks; nothing here drives the machine. The program (tools/gen_c64scpu_irqmir.py) tells it the phase by writing to $00:0B11 (id, 0 = end), the end of a main-loop pass by $0B10, the
// handler's entry / exit by $0B12 / $0B13, its results by $0B20-$0B2F, the end by $0B30.
// Time is counted in PHI2 cycles as lev (one per CPU enable window, sm.l_ev = the start of a PHI2 cycle). The CPU's step of a cycle is taken at the start of the next one (core_ce at l_ev), with the BA
// level of the PHI2 fall in between (ba_f = cpu_rdy at sm.n_ev, the level the CPU's RDY sample took); a cycle that is not a write waits while it was low.
//   bal|first|last     runs of lev whose preceding PHI2 fall had BA low (the model's input, check_irqmir.py)
//   phs|id|lev|cyc|fr  / phe|id|lev|cyc|fr   the phase start / end        ps|id|n|lev   a pass ended (masked phases)
//   sum|...            per phase: counts (see the $display)                hold|...      a PHI2 cycle of a phase without a CPU step, with the terms of `ok`
//   irq|id|edge|vec|latency   a CIA 1 IRQ edge and the vector pull that took it        irqlost|id|edge   an edge while the previous one was not yet taken
//   anom|...           a step that does not follow the rule (a read step with BA low kept, a stall with BA high), with the signals    tr|...  the step trace (first 3000 cycles of a phase, IRQ windows)
//   val|id|addr|data   the program's result bytes
    reg im_on;
    initial im_on = $test$plusargs("IRQMIR");
    integer im_ph = 0, lev_n = 0;
    reg ba_f = 1'b1, aec_f = 1'b1;
    integer im_run0 = 0, im_run1 = 0;
    integer im_s_lev [0:4], im_s_cyc [0:4], im_s_fr [0:4];
    integer im_ncyc [0:4], im_ps [0:4], im_nce [0:4], im_adv [0:4], im_stall [0:4], im_hold [0:4], im_kept [0:4], im_stallhi [0:4], im_balev [0:4];
    integer im_edge [0:4], im_vec [0:4], im_he [0:4], im_hd [0:4], im_lost [0:4], im_wra [0:4], im_rmm [0:4], im_anom [0:4], im_trn [0:4];
    integer im_h_set [0:4], im_h_mem [0:4], im_h_wbuf [0:4], im_h_bus [0:4], im_h_fl [0:4], im_h_rv [0:4];
    integer im_ii;
    initial for (im_ii = 0; im_ii < 5; im_ii = im_ii + 1) begin
        im_s_lev[im_ii] = 0; im_s_cyc[im_ii] = 0; im_s_fr[im_ii] = 0; im_ncyc[im_ii] = 0; im_ps[im_ii] = 0; im_nce[im_ii] = 0; im_adv[im_ii] = 0; im_stall[im_ii] = 0; im_hold[im_ii] = 0;
        im_kept[im_ii] = 0; im_stallhi[im_ii] = 0; im_balev[im_ii] = 0; im_edge[im_ii] = 0; im_vec[im_ii] = 0; im_he[im_ii] = 0; im_hd[im_ii] = 0; im_lost[im_ii] = 0; im_wra[im_ii] = 0;
        im_rmm[im_ii] = 0; im_anom[im_ii] = 0; im_trn[im_ii] = 0; im_h_set[im_ii] = 0; im_h_mem[im_ii] = 0; im_h_wbuf[im_ii] = 0; im_h_bus[im_ii] = 0; im_h_fl[im_ii] = 0; im_h_rv[im_ii] = 0;
    end
    reg im_irq_q = 1'b0, im_pend = 1'b0;
    integer im_edge_lev = 0, im_tr_after = 0, im_ph_lev0 = 0;
    wire im_ce   = dut.c64.cpu_0.core_ce;
    wire im_wr   = dut.c64.cpu_0.wr_cyc && dut.c64.cpu_0.bus_cyc;
    wire [23:0] im_a = dut.c64.cpu_0.a_out;
    wire im_lev  = dut.c64.cpu_0.smem.l_ev;
    wire im_nev  = dut.c64.cpu_0.smem.n_ev;
    wire [7:0] im_dq = dut.c64.cpu_0.wr_cyc ? dut.c64.cpu_0.d_out : dut.c64.cpu_0.d_in;
    task im_trace(input [127:0] tag);
        begin
            $display("%0s|%0d|%0d|%06x|%s|vda%0d vpa%0d vpb%0d|d %02x|ce%0d rdyin%0d|ba_f%0d aec_f%0d|cpuirq%0d lat%0d irq_r%0d cia%0d|sync%0d armed%0d bok%0d ok%0d|ph%0d fwd%0d|wk%0d",
                tag, im_ph, lev_n, im_a, dut.c64.cpu_0.wr_cyc ? "W" : "R", dut.c64.cpu_0.vda, dut.c64.cpu_0.vpa, dut.c64.cpu_0.vpb, im_dq, im_ce, dut.c64.cpu_0.rdy_in, ba_f, aec_f,
                dut.c64.cpu_irq, dut.c64.cpu_0.irq_latched, dut.c64.cpu_0.irq_r, dut.c64.cia1_irq,
                dut.c64.cpu_0.smem.sync_cyc, dut.c64.cpu_0.smem.armed, dut.c64.cpu_0.smem.bus_ok, dut.c64.cpu_0.smem.ok, dut.c64.cpu_0.smem.ph, dut.c64.cpu_0.fwd, dut.c64.cpu_0.smem.d_wk);
        end
    endtask
    always @(posedge clk2x_r) if (im_on && !rst) begin
        if (im_nev) begin ba_f = dut.c64.cpu_rdy; aec_f = dut.c64.vic_aec; end
        if (im_lev) begin
            lev_n = lev_n + 1;
            if (!ba_f) begin if (im_run0 == 0) im_run0 = lev_n; im_run1 = lev_n; end
            else if (im_run0 != 0) begin $display("bal|%0d|%0d", im_run0, im_run1); im_run0 = 0; end
            if (im_ph != 0) begin
                im_ncyc[im_ph] = im_ncyc[im_ph] + 1;
                if (!ba_f) im_balev[im_ph] = im_balev[im_ph] + 1;
                if (!im_ce) begin       // a PHI2 cycle without a CPU step
                    im_hold[im_ph] = im_hold[im_ph] + 1;
                    if (!dut.c64.cpu_0.smem.settled) im_h_set[im_ph] = im_h_set[im_ph] + 1;
                    if (!dut.c64.cpu_0.smem.mem_ok) im_h_mem[im_ph] = im_h_mem[im_ph] + 1;
                    if (!dut.c64.cpu_0.smem.wbuf_ok) im_h_wbuf[im_ph] = im_h_wbuf[im_ph] + 1;
                    if (!dut.c64.cpu_0.smem.bus_go) im_h_bus[im_ph] = im_h_bus[im_ph] + 1;
                    if (dut.c64.cpu_0.smem.flushing) im_h_fl[im_ph] = im_h_fl[im_ph] + 1;
                    if (!dut.c64.cpu_0.smem.rom_valid) im_h_rv[im_ph] = im_h_rv[im_ph] + 1;
                    if (im_hold[im_ph] <= 12) im_trace("hold");
                end
            end
        end
        if (im_ph != 0 && im_lev && lev_n - im_ph_lev0 <= 3000) im_trace("tr");
        else if ((im_ph == 2 || im_ph == 4) && im_lev && (im_pend || im_tr_after > 0)) begin im_trace("tr"); if (!im_pend) im_tr_after = im_tr_after - 1; end
        if (im_ce && im_ph != 0) begin
            im_nce[im_ph] = im_nce[im_ph] + 1;
            if (dut.c64.cpu_0.rdy_in) im_adv[im_ph] = im_adv[im_ph] + 1; else im_stall[im_ph] = im_stall[im_ph] + 1;
            if (dut.c64.cpu_0.rdy_in && !dut.c64.cpu_0.wr_cyc && !ba_f) begin               // a step that is not a write went on although BA was low at the fall
                im_kept[im_ph] = im_kept[im_ph] + 1;
                im_anom[im_ph] = im_anom[im_ph] + 1; if (im_anom[im_ph] <= 40) im_trace("anom");
            end
            if (!dut.c64.cpu_0.rdy_in && ba_f) begin                                          // a stall although BA was high at the fall
                im_stallhi[im_ph] = im_stallhi[im_ph] + 1;
                im_anom[im_ph] = im_anom[im_ph] + 1; if (im_anom[im_ph] <= 40) im_trace("anom");
            end
            if (dut.c64.cpu_0.rdy_latched != ba_f) im_rmm[im_ph] = im_rmm[im_ph] + 1;
            if (im_wr && dut.c64.cpu_0.fwd && !aec_f) im_wra[im_ph] = im_wra[im_ph] + 1;       // a write on the C64 bus although AEC was low at the fall
        end
        // the CIA 1 IRQ line: edges and the vector pull that takes each
        im_irq_q <= dut.c64.cia1_irq;
        if (dut.c64.cia1_irq && !im_irq_q && im_ph != 0) begin
            im_edge[im_ph] = im_edge[im_ph] + 1;
            if (im_pend) begin im_lost[im_ph] = im_lost[im_ph] + 1; $display("irqlost|%0d|%0d|previous edge %0d", im_ph, lev_n, im_edge_lev); end
            im_pend = 1'b1; im_edge_lev = lev_n;
        end
        if (im_ce && dut.c64.cpu_0.bus_cyc && !dut.c64.cpu_0.wr_cyc && !dut.c64.cpu_0.vpb && im_a[15:0] == 16'hFFFE && im_ph != 0) begin
            im_vec[im_ph] = im_vec[im_ph] + 1;
            $display("irq|%0d|%0d|%0d|%0d", im_ph, im_edge_lev, lev_n, lev_n - im_edge_lev);
            im_pend = 1'b0; im_tr_after = 18;
        end
        // the program's markers
        if (im_ce && im_wr && im_a[23:8] == 16'h000B) begin
            case (im_a[7:0])
                8'h10: if (im_ph != 0) begin
                    im_ps[im_ph] = im_ps[im_ph] + 1;
                    if (im_ph == 1 || im_ph == 3) $display("ps|%0d|%0d|%0d", im_ph, im_ps[im_ph], lev_n);
                end
                8'h11: begin
                    if (dut.c64.cpu_0.d_out != 8'd0) begin
                        im_ph = dut.c64.cpu_0.d_out; im_s_lev[im_ph] = lev_n; im_s_cyc[im_ph] = cyc; im_s_fr[im_ph] = fr_n; im_ph_lev0 = lev_n;
                        $display("phs|%0d|%0d|%0d|%0d", im_ph, lev_n, cyc, fr_n);
                    end else if (im_ph != 0) begin
                        $display("phe|%0d|%0d|%0d|%0d", im_ph, lev_n, cyc, fr_n);
                        $display("sum|%0d|cyc %0d|frames %0d|passes %0d|ce %0d|adv %0d|stall %0d|hold %0d|kept %0d|stallhi %0d|ba_low_lev %0d|edges %0d|vec %0d|hentry %0d|hdone %0d|lost %0d|wr_aeclow %0d|rdy_mm %0d|anom %0d",
                            im_ph, lev_n - im_s_lev[im_ph], fr_n - im_s_fr[im_ph], im_ps[im_ph], im_nce[im_ph], im_adv[im_ph], im_stall[im_ph], im_hold[im_ph], im_kept[im_ph], im_stallhi[im_ph], im_balev[im_ph],
                            im_edge[im_ph], im_vec[im_ph], im_he[im_ph], im_hd[im_ph], im_lost[im_ph], im_wra[im_ph], im_rmm[im_ph], im_anom[im_ph]);
                        $display("holdwhy|%0d|not settled %0d|mem %0d|wbuf %0d|bus_go %0d|flushing %0d|rom_valid %0d", im_ph, im_h_set[im_ph], im_h_mem[im_ph], im_h_wbuf[im_ph], im_h_bus[im_ph], im_h_fl[im_ph], im_h_rv[im_ph]);
                        im_ph = 0;
                    end
                end
                8'h12: if (im_ph != 0) im_he[im_ph] = im_he[im_ph] + 1;
                8'h13: if (im_ph != 0) im_hd[im_ph] = im_hd[im_ph] + 1;
                8'h30: $display("fin|%0d|%0d", lev_n, cyc);
                default: if (im_a[7:4] == 4'h2 && im_ph != 0) $display("val|%0d|%02x|%02x", im_ph, im_a[7:0], dut.c64.cpu_0.d_out);
            endcase
        end
    end
