// T118: what the HDMI encoder is given (hs / vs / de / cx / cy, the output of host/vic_scaler.v, the only source of the
// output timing: overlay, pix_layer and c64_text pass cx/cy/de/hs/vs through unchanged) for each core's pix_x/pix_y stream.
//   +SRC=a1200   the Amiga 1200's dummy raster, the statements of cores/amiga1200/rtl/amiga_core.v:283-296 verbatim
//   +SRC=vc20    the VC-20 core's own stream (tb_vc20_pix.vhd dumps vc20_pix.txt from cores/vc20/rtl, GHDL)
//   +SRC=slow    controls the hypothesis "a frame of another length changes the line structure": a1200 raster, 2022 clocks per
//                line instead of 2016 (frame 630,864 instead of 628,992)
//   +SRC=nomark  negative control: no clock with px == 0 on line 20, so the scaler never locks
// Per frame (vs rising edge to vs rising edge) it prints one line "G ..." with the measured structure; the verdict compares it to
// the 1080p50 numbers of vic_scaler.v's header. The host's pixel clock and the core clock are the board's (148.869 MHz and
// 31.5278 MHz, 6875 : 1456).
`timescale 1ps/1fs
module tb;
    reg sys_clk = 0, pix_clk = 0;
    always #15859   sys_clk = ~sys_clk;
    always #3358.75 pix_clk = ~pix_clk;

    // ---- the core's stream -------------------------------------------------------
    integer src;                       // 0 a1200, 1 vc20, 2 slow, 3 nomark
    reg [9:0] px = 0;
    reg [8:0] py = 0;
    reg [3:0] pix = 0;
    // a1200 (amiga_core.v:283-296): dph counts 0-3, dx 0-503 per line, dy 0-311 per frame
    reg [1:0] dph = 0; reg [8:0] dx = 0, dy = 0;
    integer lineclk = 0;               // slow: clocks per line
    integer lc = 0;
    // vc20: replay
    integer fd, rc, cyc = 0, nt = -1, nx = 0, ny = 0, nrec = 0;
    always @(posedge sys_clk) begin
        cyc <= cyc + 1;
        case (src)
        0, 3: begin
            dph <= dph + 2'd1;
            if (dph == 2'd3) begin
                if (dx == 9'd503) begin dx <= (src == 3) ? 9'd1 : 9'd0; dy <= (dy == 9'd311) ? 9'd0 : dy + 9'd1; end
                else dx <= dx + 9'd1;
            end
            px <= {1'b0, dx}; py <= dy;
        end
        2: begin
            // the same raster, the line takes lineclk clocks: 504 dots of 4 clocks, then waits
            lc <= (lc == lineclk - 1) ? 0 : lc + 1;
            if (lc == lineclk - 1) begin dx <= 0; dy <= (dy == 9'd311) ? 9'd0 : dy + 9'd1; end
            else if (lc < 2016 && lc[1:0] == 2'd3) dx <= dx + 9'd1;
            px <= {1'b0, dx}; py <= dy;
        end
        1: begin
            if (nt < 0 && !$feof(fd)) begin rc = $fscanf(fd, "%d %d %d\n", nt, nx, ny); end
            while (nt >= 0 && nt <= cyc + 1) begin
                px <= nx[9:0]; py <= ny[8:0]; nrec = nrec + 1;
                nt = -1;
                if (!$feof(fd)) rc = $fscanf(fd, "%d %d %d\n", nt, nx, ny);
            end
        end
        endcase
    end

    // ---- the host's scaler, as top.v:1336 wires it ----------------------------------
    reg rst = 1;
    wire [23:0] rgb; wire de, hs, vs, locked; wire [11:0] cx; wire [10:0] cy; wire [9:0] x_text;
    vic_scaler scaler (
        .vclk(sys_clk), .pix(pix), .px(px), .py(py),
        .clk(pix_clk), .rst(rst),
        .pal_clk(sys_clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy), .locked(locked),
        .blank(1'b0), .x_text(x_text), .v60(1'b0), .scan(1'b0), .relock(1'b0)
    );

    // ---- measurement of the encoder's input ------------------------------------------
    integer t = 0;                    // pix_clk since reset release
    reg hs_q = 0, vs_q = 0, de_q = 0;
    integer t_hs = -1, t_vs = -1, t_vsfall = -1, t_hsfall = -1;
    integer frames = 0, lines = 0, de_lines = 0, de_w = 0, dew_min = 0, dew_max = 0, ln_min = 0, ln_max = 0;
    integer hsw = 0, vsw = 0, flen = 0, ll = 0;
    integer p00_hs = -1, p00_vs = -1, pe_hs = -1, pe_vs = -1, first_cx = 0, first_cy = 0, last_cx = 0, last_cy = 0;
    reg seen_de_line = 0;
    integer nfr = 0, bad = 0, good = 0;
    integer ref_flen = 0;
    reg locked_q = 0;
    integer t_lock = -1;
    always @(posedge pix_clk) if (!rst) begin
        t <= t + 1;
        hs_q <= hs; vs_q <= vs; de_q <= de;
        if (locked && !locked_q) t_lock = t;
        locked_q <= locked;
        if (de) begin
            de_w = de_w + 1;
            seen_de_line = 1;
            if (frames > 0 && cx == 12'd768 && cy == 11'd45)    begin p00_hs = t - t_hs; p00_vs = t - t_vs; first_cx = cx; first_cy = cy; end
            if (frames > 0 && cx == 12'd2687 && cy == 11'd1124) begin pe_hs = t - t_hs;  pe_vs = t - t_vs;  last_cx = cx; last_cy = cy; end
        end
        if (de_q && !de) begin                       // end of a DE run (one per active line)
            if (dew_min == 0 || de_w < dew_min) dew_min = de_w;
            if (de_w > dew_max) dew_max = de_w;
            de_lines = de_lines + 1;
            de_w = 0;
        end
        if (hs && !hs_q) begin
            if (t_hs >= 0 && frames > 0) begin
                ll = t - t_hs;
                lines = lines + 1;
                if (ln_min == 0 || ll < ln_min) ln_min = ll;
                if (ll > ln_max) ln_max = ll;
            end
            t_hs = t;
        end
        if (!hs && hs_q) hsw = t - t_hs;
        if (!vs && vs_q) vsw = t - t_vs;
        if (vs && !vs_q) begin
            if (frames > 0 && locked && nfr > 0) begin
                flen = t - t_vs;
                // the structure of the frame that just ended
                $display("G frame=%0d flen=%0d lines=%0d ln_min=%0d ln_max=%0d hsw=%0d vsw=%0d de_lines=%0d dew_min=%0d dew_max=%0d p00_hs=%0d p00_vs=%0d pend_hs=%0d pend_vs=%0d first=(%0d,%0d) last=(%0d,%0d)",
                    nfr, flen, lines, ln_min, ln_max, hsw, vsw, de_lines, dew_min, dew_max, p00_hs, p00_vs, pe_hs, pe_vs,
                    first_cx, first_cy, last_cx, last_cy);
                // the 1080p50 numbers of vic_scaler.v: 2640 x 1125; hs 528-571 (44); vs lines 4-8 (5); 1080 active lines of 1920;
                // layer pixel (0,0) = cx 768 / cy 45: 192 clocks after hs rises, 41 lines + 720 clocks after vs rises
                if (flen == 2970000 && ln_min == 2640 && ln_max == 2640 && hsw == 44 && vsw == 5 * 2640 && de_lines == 1080 &&
                    dew_min == 1920 && dew_max == 1920 && p00_hs == 192 && p00_vs == 41 * 2640 + 720 &&
                    pe_hs == 192 + 1919 && pe_vs == 1120 * 2640 + 2639 && first_cx == 768 && first_cy == 45 &&
                    last_cx == 2687 && last_cy == 1124)
                    good = good + 1;
                else bad = bad + 1;
            end
            frames = frames + 1;
            if (locked) nfr = nfr + 1;
            t_vs = t;
            lines = 0; de_lines = 0; ln_min = 0; ln_max = 0; dew_min = 0; dew_max = 0;
            p00_hs = -1; p00_vs = -1; pe_hs = -1; pe_vs = -1; de_w = 0;
        end
    end

    // ---- run ---------------------------------------------------------------------------
    reg [1023:0] name;
    integer nsys;
    initial begin
        src = 0; lineclk = 2016;
        if ($value$plusargs("SRC=%s", name)) begin
            if (name == "vc20") src = 1;
            else if (name == "slow") begin src = 2; lineclk = 2022; end
            else if (name == "nomark") src = 3;
        end
        if (src == 1) begin
            fd = $fopen("vc20_pix.txt", "r");
            if (fd == 0) begin $display("FAIL hdmigeom: vc20_pix.txt missing (run tb_vc20_pix first)"); $finish; end
        end
        nsys = 4 * 628992 + 400000;
        #1000000 rst = 0;                       // 1 us: the pixel clock's reset sync is long done
        wait (cyc >= nsys);
        $display("S src=%0s locked=%0d t_lock=%0d vsyncs=%0d locked_frames=%0d good=%0d bad=%0d records=%0d",
                 name, locked, t_lock, frames, nfr, good, bad, nrec);
        if (locked && good >= 2 && bad == 0)
            $display("PASS hdmigeom %0s: locked, %0d complete frames, every one 2640 x 1125 with hs 44, vs 5 lines, 1080 x 1920 DE, layer origin at cx 768 / cy 45", name, good);
        else if (!locked)
            $display("FAIL hdmigeom %0s: the scaler did not lock (locked 0)", name);
        else
            $display("FAIL hdmigeom %0s: good %0d bad %0d", name, good, bad);
        $finish;
    end
endmodule
