// T257 fault 2: the VDC's interlaced bitmap (R8 = 3) as Busted Boulders shows its title, through the whole picture path: the C128's VDC
// (c128_x -DVDC_WHOLE -DVDC64) -> c128_rgbi -> c128_pixw -> a model of the host's e_ port (DDR2 as 128-bit words). Written for this project (TREX).
// The VDC RAM is the picture of the ruler program vdc_il_ruler.asm (vdcil_model.py writes it as lines_<m>.hex): picture line p at $4000 + 80 (p / 2)
// for p even (block A) and at $8B00 + 80 (p / 2) for p odd (block B), the game's layout; every line carries its own number in binary (bytes 30..38),
// so every row of a reported picture is read back as "picture line p" and then compared with that line whole, all 640 pixels.
// The four modes of the ruler, each with the registers the program writes over the C128 KERNAL's set-up (program_kernal):
//   A  the game's title registers (R4 $33, R5 7, R6 $30, R7 $32, R8 3, R9 9, R12 $40, R25 $80, R26 $F0, R28 $30), R12/R13 static
//   B  the same, and R12 = $40 or $8B written in every vertical blank as the game does it ($3A47-$3B89): two field periods from vblank start to
//      vblank start are counted on the status register (bit 5), par = (first < second), then in every blank R12 = (frame & 1) ^ par ? $8B : $40
//   C  R8 = 1 (interlace sync only), R9 4, R5 3: 240 lines a field, not woven by the frame writer (each field a frame, lines doubled)
//   D  R8 = 0, the same timing: the reference, 240 lines
// Expected (T257 fault 2, the rule of the real-hardware programs in §5): A and B show the whole ruler woven, row r = picture line r (block A in rows
// 2y, block B in rows 2y+1: the field after the reload field carries on at the memory line after the last one it showed); C and D show block A,
// rows 2y and 2y+1 = picture line 2y. +dump=<prefix> writes the first checked picture of every mode as <prefix>_<m>.ppm.
`timescale 1ns / 1ps
module tb_c128_vdcil;
    reg clk = 1'b0, ui_clk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    always #6.667 ui_clk = ~ui_clk;
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    wire [1:0] vdc_il;
    c128_x dut (.clk(clk), .rst(rst), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb), .vdc_il(vdc_il),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata(), .vk_req(18'd0), .vk_ans());
`include "vdc_bus.svh"

    // ---- the frame writer, as layer2_c128.v instantiates it
    reg wr_en = 1'b0;
    wire [15:0] px;
    c128_rgbi rgbi_i (.rgbi(rgbi), .rgb565(px));
    wire quiet, frame_tgl;
    wire [7:0] latest, seq;
    wire [15:0] overruns;
    wire [26:0] e_addr;
    wire [2:0] e_cmd;
    wire e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;
    reg e_rdy = 1'b1;
    c128_pixw #(.X0(0), .Y0(0), .COLS(640), .LINES(240), .LINES_IL(240), .LINES_BASE(200)) pw (
        .sys_clk(clk), .sys_rst(rst), .dotst(ce & wr_en), .rgb565(px), .hblank(hb), .vblank(vb), .il(vdc_il[1]), .field(vdc_il[0]),
        .quiet(quiet), .frame_tgl(frame_tgl), .latest(latest), .seq(seq), .overruns(overruns),
        .fb_base(32'h0000_0000), .fb_stride(32'h0010_0000), .ui_clk(ui_clk), .ui_rst(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // ---- the host's e_ port: three buffers of 640 x 480 RGB565, 1 MiB apart
    localparam integer BW = 65536;                    // words between the buffers
    reg [127:0] ddr [0:3*BW-1];
    always @(posedge ui_clk) begin
        e_rdy <= ($random & 7) != 0;
        if (e_en && e_rdy && e_addr[26:3] < 3 * BW) ddr[e_addr[26:3]] = e_wdf_data;
    end

    // ---- the ruler's picture lines (vdcil_model.py): ln[m * 480 + p], pixel x = bit 639 - x
    reg [639:0] ln [0:4*480-1];
    string dir;
    initial begin
        if (!$value$plusargs("lines=%s", dir)) dir = ".";
        $readmemh({dir, "/lines_0.hex"}, ln, 0, 479);
        $readmemh({dir, "/lines_1.hex"}, ln, 480, 959);
        $readmemh({dir, "/lines_2.hex"}, ln, 960, 1439);
        $readmemh({dir, "/lines_3.hex"}, ln, 1440, 1919);
    end

    integer fails = 0;
    task check(input ok, input string what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
            $fflush;
        end
    endtask

    // row r of buffer b as 640 bits (white = 1), then the picture line it shows: 0..479, -1 for a row that is no line of the picture
    // (its bits are not the line its number names), -2 for a black row; a white row is line 0 or 479 (both are all set)
    reg [639:0] rv;
    function integer row_line(input integer b, input integer r, input integer m);
        integer c, k, p;
        reg [127:0] w;
        reg [15:0] pv;
        begin
            for (c = 0; c < 640; c = c + 1) begin
                w = ddr[b * BW + r * 80 + c / 8];
                pv = w >> ((c % 8) * 16);
                rv[639 - c] = (pv == 16'hFFFF);
            end
            if (rv == {640{1'b0}}) row_line = -2;
            else if (rv == {640{1'b1}}) row_line = (r < 240) ? 0 : 479;
            else begin
                p = 0;
                for (k = 0; k < 9; k = k + 1) if (rv[639 - ((38 - k) * 8 + 3)]) p = p | (1 << k);
                row_line = (p < 480 && rv == ln[m * 480 + p]) ? p : -1;
            end
        end
    endfunction

    // ---- the checks, at every picture the writer reports
    integer mode = 0, armed = 0, settle = 0, npic = 0, nbad = 0, nb2bad = 0, pic_bad = 0;
    integer want, got, rr;
    string first_rows, dumpf;
    task dump_ppm(input integer b, input string name);
        integer fd, y, c;
        reg [127:0] w;
        reg [15:0] p;
        begin
            fd = $fopen(name, "wb");
            $fwrite(fd, "P6\n640 480\n255\n");
            for (y = 0; y < 480; y = y + 1)
                for (c = 0; c < 640; c = c + 1) begin
                    w = ddr[b * BW + y * 80 + c / 8];
                    p = w >> ((c % 8) * 16);
                    $fwrite(fd, "%c%c%c", {p[15:11], p[15:13]}, {p[10:5], p[10:9]}, {p[4:0], p[4:2]});
                end
            $fclose(fd);
        end
    endtask
    always @(frame_tgl) if (armed) begin
        if (settle > 0) settle = settle - 1;
        else if (npic < 4) begin
            npic = npic + 1;
            nbad = 0;
            first_rows = "";
            for (rr = 0; rr < 480; rr = rr + 1) begin
                got = row_line(latest[1:0], rr, mode);
                want = (mode < 2) ? rr : 2 * (rr / 2);
                if (got != want) nbad = nbad + 1;
                if (rr < 12) first_rows = (rr == 0) ? $sformatf("%0d", got) : $sformatf("%0s %0d", first_rows, got);
            end
            if (latest[2] !== 1'b1) nb2bad = nb2bad + 1;
            $display("     mode %c picture %0d (buffer %0d, b2 %0d): %0d of 480 rows are not the expected line; rows 0..11 show lines %0s",
                     "A" + mode, npic, latest[1:0], latest[2], nbad, first_rows);
            if (nbad != 0) begin
                first_rows = "";
                for (rr = 1; rr < 480 && rr < 24; rr = rr + 2) begin
                    got = row_line(latest[1:0], rr, mode);
                    first_rows = (rr == 1) ? $sformatf("%0d", got) : $sformatf("%0s %0d", first_rows, got);
                end
                $display("     mode %c picture %0d: rows 1, 3 .. 23 show lines %0s", "A" + mode, npic, first_rows);
                for (rr = 470; rr < 480; rr = rr + 1) begin
                    got = row_line(latest[1:0], rr, mode);
                    first_rows = (rr == 470) ? $sformatf("%0d", got) : $sformatf("%0s %0d", first_rows, got);
                end
                $display("     mode %c picture %0d: rows 470 .. 479 show lines %0s", "A" + mode, npic, first_rows);
            end
            if (npic == 1 && dumpf != "") dump_ppm(latest[1:0], $sformatf("%0s_%0d.ppm", dumpf, mode));
            $fflush;
            if (nbad != 0) pic_bad = pic_bad + 1;
        end
    end

    // ---- the VDC: the registers of each mode (vdc_il_ruler.asm m_il / m_c / m_d), the RAM written byte by byte through R31 as the program does
    task automatic set_ua(input [15:0] a);
        begin wr_reg(8'd18, a[15:8]); wr_reg(8'd19, a[7:0]); end
    endtask
    task automatic put_line(input [15:0] a, input [639:0] v, input integer from, input integer to);
        integer k;
        begin
            set_ua(a + from);
            wr_sel(8'd31);
            for (k = from; k < to; k = k + 1) begin wr_dat(v[639 - 8 * k -: 8]); wait_ready; end
        end
    endtask
    function [15:0] line_addr(input integer p);
        begin line_addr = ((p % 2) ? 16'h8B00 : 16'h4000) + 16'(80 * (p / 2)); end
    endfunction
    // the C128 KERNAL's VDC set-up at power-on (its pair table at $E2F8, then R25 = $47 for an 8563 from R8 on, then the PAL rows R4 = $26,
    // R7 = $20): what the ruler program finds; it writes only the registers of its modes over it
    task automatic program_kernal;
        integer i;
        reg [7:0] kt [0:59];
        begin
            {kt[0], kt[1], kt[2], kt[3], kt[4], kt[5], kt[6], kt[7], kt[8], kt[9]} = {8'd0, 8'h7E, 8'd1, 8'h50, 8'd2, 8'h66, 8'd3, 8'h49, 8'd4, 8'h20};
            {kt[10], kt[11], kt[12], kt[13], kt[14], kt[15], kt[16], kt[17], kt[18], kt[19]} = {8'd5, 8'h00, 8'd6, 8'h19, 8'd7, 8'h1D, 8'd8, 8'h00, 8'd9, 8'h07};
            {kt[20], kt[21], kt[22], kt[23], kt[24], kt[25], kt[26], kt[27], kt[28], kt[29]} = {8'd10, 8'h20, 8'd11, 8'h07, 8'd12, 8'h00, 8'd13, 8'h00, 8'd14, 8'h00};
            {kt[30], kt[31], kt[32], kt[33], kt[34], kt[35], kt[36], kt[37], kt[38], kt[39]} = {8'd15, 8'h00, 8'd20, 8'h08, 8'd21, 8'h00, 8'd23, 8'h08, 8'd24, 8'h20};
            {kt[40], kt[41], kt[42], kt[43], kt[44], kt[45], kt[46], kt[47], kt[48], kt[49]} = {8'd25, 8'h40, 8'd26, 8'hF0, 8'd27, 8'h00, 8'd28, 8'h20, 8'd29, 8'h07};
            {kt[50], kt[51], kt[52], kt[53], kt[54], kt[55], kt[56], kt[57], kt[58], kt[59]} = {8'd34, 8'h7D, 8'd35, 8'h64, 8'd36, 8'h05, 8'd22, 8'h78, 8'd25, 8'h47};
            for (i = 0; i < 60; i = i + 2) wr_reg(kt[i], kt[i + 1]);
            wr_reg(8'd4, 8'h26); wr_reg(8'd7, 8'h20);
        end
    endtask
    task automatic program_mode(input integer m);
        integer p;
        begin
            for (p = 16; p < 48; p = p + 1) put_line(line_addr(p), ln[m * 480 + p], 72, 76);   // the letter (setmode)
            wr_reg(8'd0, 8'h7E); wr_reg(8'd1, 8'h50); wr_reg(8'd2, 8'h66); wr_reg(8'd3, 8'h49); wr_reg(8'd4, 8'h33);
            wr_reg(8'd5, (m < 2) ? 8'h07 : 8'h03); wr_reg(8'd6, 8'h30); wr_reg(8'd7, 8'h32);
            wr_reg(8'd8, (m < 2) ? 8'h03 : (m == 2) ? 8'h01 : 8'h00); wr_reg(8'd9, (m < 2) ? 8'h09 : 8'h04);
            wr_reg(8'd12, 8'h40); wr_reg(8'd13, 8'h00); wr_reg(8'd20, 8'h10); wr_reg(8'd21, 8'h00);
            wr_reg(8'd23, (m < 2) ? 8'h09 : 8'h04); wr_reg(8'd24, 8'h00); wr_reg(8'd25, 8'h80); wr_reg(8'd26, 8'hF0);
            wr_reg(8'd27, 8'h00); wr_reg(8'd28, 8'h30);
        end
    endtask

    // the status register's bit 5 (vertical blank) as the program reads it
    reg [7:0] st;
    task automatic wait_vb(input v);
        integer n;
        begin
            n = 0; st = 8'h00;
            bus(1'b0, 1'b0, 8'd0, st);
            while (st[5] !== v && n < 400000) begin bus(1'b0, 1'b0, 8'd0, st); n = n + 1; end
        end
    endtask
    // mode B: the game's field parity ($3A47-$3AF4) and its R12 in every vertical blank ($3B00-$3BAC), until four pictures were checked
    integer c1, c2, par, frame, nlog;
    task automatic game_loop;
        begin
            wait_vb(1'b0); wait_vb(1'b1);
            c1 = 0; do begin bus(1'b0, 1'b0, 8'd0, st); c1 = c1 + 1; end while (st[5]);
            do begin bus(1'b0, 1'b0, 8'd0, st); c1 = c1 + 1; end while (!st[5]);
            c2 = 0; do begin bus(1'b0, 1'b0, 8'd0, st); c2 = c2 + 1; end while (st[5]);
            do begin bus(1'b0, 1'b0, 8'd0, st); c2 = c2 + 1; end while (!st[5]);
            par = (c1 < c2) ? 1 : 0;
            $display("     mode B: field periods %0d and %0d status reads, par = %0d", c1, c2, par);
            wait_vb(1'b0);
            frame = 0; nlog = 0;
            while (npic < 4 && frame < 400) begin
                wait_vb(1'b1);
                wr_reg(8'd12, (((frame & 1) ^ par) != 0) ? 8'h8B : 8'h40);
                if (nlog < 4) begin
                    $display("     mode B: R12 = $%0s after a field the frame writer takes as field %0d", (((frame & 1) ^ par) != 0) ? "8B" : "40", vdc_il[0]);
                    nlog = nlog + 1;
                end
                wait_vb(1'b0);
                frame = frame + 1;
            end
        end
    endtask

    integer m2, p2, nn;
    initial begin
        if (!$value$plusargs("dump=%s", dumpf)) dumpf = "";
        repeat (16) @(posedge clk);
        rst <= 1'b0;
        repeat (70000) @(posedge clk);
        program_kernal;
        wr_reg(8'd28, 8'h30);                            // 64 KiB addressing before the first byte (the program does the same)
        wr_reg(8'd24, 8'h00);
        for (p2 = 0; p2 < 480; p2 = p2 + 1) put_line(line_addr(p2), ln[p2], 0, 80);
        $display("     the ruler is in the VDC RAM (480 lines, cycle %0d)", cyc);
        wr_en = 1'b1;
        for (m2 = 0; m2 < 4; m2 = m2 + 1) begin
            program_mode(m2);
            mode = m2; npic = 0; pic_bad = 0; nb2bad = 0;
            settle = (m2 < 2) ? 2 : 3;                    // pictures to let go by while the VDC takes the new registers
            armed = 1;
            if (m2 == 1) game_loop;
            else begin nn = 0; while (npic < 4 && nn < 30000000) begin @(posedge clk); nn = nn + 1; end end
            armed = 0;
            check(npic >= 4, $sformatf("mode %c: four pictures were reported and checked", "A" + m2));
            check(pic_bad == 0, (m2 < 2) ? $sformatf("mode %c: every row r of the woven picture is picture line r (block A in rows 2y, block B in rows 2y+1)", "A" + m2)
                                         : $sformatf("mode %c: rows 2y and 2y+1 are picture line 2y (block A, 240 lines doubled)", "A" + m2));
            check(nb2bad == 0, $sformatf("mode %c: latest b2 = 1 (480 rows are this picture)", "A" + m2));
        end
        check(overruns == 16'd0, "no late line (overruns == 0)");
        if (fails == 0) $display("PASS tb_c128_vdcil"); else $display("FAILED tb_c128_vdcil: %0d", fails);
        $finish;
    end
    always @(posedge clk) if (cyc > 120000000) begin $display("FAILED tb_c128_vdcil: timeout"); $finish; end
endmodule
