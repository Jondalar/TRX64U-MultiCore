// vic_scaler + overlay against a synthetic VIC (border, background, colour
// bars). A CPU model in the 100 MHz clock runs the ops in ovl_ops.txt
// (written by overlay_sim.py), one per line "op addr data":
//   0 a d  write d to 0x10140000 + a
//   1 a d  read 0x10140000 + a, expect d
//   2 0 0  wait for the next frame start
//   3 0 0  dump the next whole frame (1920 x 1080, hex RGB per line) to ovl_frame<n>.txt
//   4 0 0  end
`timescale 1ns/1ps
module tb;
    reg vclk = 0, clk = 0, sclk = 0;
    always #15.859 vclk = ~vclk;      // 31.5278 MHz
    always #3.2985 clk  = ~clk;       // 151.58 MHz
    always #5.0    sclk = ~sclk;      // 100 MHz, the RISC-V

    // VIC: 504 x 312 dots, one per 4 vclk; light blue border, blue background,
    // colour bars in the text area
    reg [1:0] ph = 0; reg [9:0] x = 0; reg [8:0] y = 0;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            if (x == 503) begin x <= 0; y <= (y == 311) ? 0 : y + 1; end
            else x <= x + 1;
        end
    end
    wire        inner = x >= 136 && x < 456 && y >= 51 && y < 251;
    wire [9:0]  bx = x - 136;
    wire [3:0]  pix = !inner ? 4'd14 : (y < 151) ? 4'd6 : bx[8:5] + 4'd1;

    reg rst = 1;
    wire [23:0] s_rgb, rgb; wire s_de, s_hs, s_vs, de, hs, vs, locked;
    wire [11:0] s_cx, cx; wire [10:0] s_cy, cy;
    vic_scaler scaler (
        .vclk(vclk), .pix(pix), .px(x), .py(y),
        .clk(clk), .rst(rst), .rgb(s_rgb), .de(s_de), .hs(s_hs), .vs(s_vs), .cx(s_cx), .cy(s_cy),
        .locked(locked), .blank(1'b0), .x_text(), .v60(1'b0), .relock(1'b0));

    reg         we = 0;
    reg  [14:0] addr = 0;
    reg  [7:0]  wdata = 0;
    wire [7:0]  rdata;
    overlay dut (
        .clk_sys(sclk), .we(we), .addr(addr), .wdata(wdata), .rdata(rdata),
        .clk(clk), .rgb_in(s_rgb), .de_in(s_de), .hs_in(s_hs), .vs_in(s_vs), .cx_in(s_cx), .cy_in(s_cy),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy));

    // ---- pixel side: frame starts, dumps, timing checks ----------------------------
    integer frame_starts = 0, dump_n = 0, fd = 0, npix = 0, errors = 0;
    reg dump_req = 0, dumping = 0, dump_done = 0;
    reg [8*16:1] fname;
    always @(posedge clk) if (locked) begin
        if (s_cx == 0 && s_cy == 0) frame_starts = frame_starts + 1;
        if (de && (cx < 768 || cy < 45)) begin
            errors = errors + 1;
            if (errors < 5) $display("de outside the active area: cx %0d cy %0d", cx, cy);
        end
        if (cx == 0 && cy == 0 && dump_req && !dumping) begin
            dumping = 1; npix = 0;
            $sformat(fname, "ovl_frame%0d.txt", dump_n);
            fd = $fopen(fname, "w");
        end
        if (dumping && de) begin
            $fwrite(fd, "%06x\n", rgb);
            npix = npix + 1;
        end
        if (dumping && cx == 2687 && cy == 1124) begin
            $fclose(fd);
            if (npix != 1920 * 1080) begin
                errors = errors + 1;
                $display("frame dump %0d: %0d pixels", dump_n, npix);
            end
            dumping = 0; dump_req = 0; dump_done = 1; dump_n = dump_n + 1;
        end
    end

    // ---- CPU side ---------------------------------------------------------------------
    integer ops, op, a, d, r, n, seen;
    initial begin
        #500 rst = 0;
        wait (locked);
        ops = $fopen("ovl_ops.txt", "r");
        n = 0;
        while (1) begin
            r = $fscanf(ops, "%d %h %h\n", op, a, d);
            if (r != 3) begin $display("bad op line %0d", n); $finish; end
            n = n + 1;
            case (op)
            0: begin
                @(posedge sclk) begin we <= 1; addr <= a; wdata <= d; end
                @(posedge sclk) we <= 0;
            end
            1: begin
                @(posedge sclk) addr <= a;
                @(posedge sclk);
                #1 if (rdata !== d[7:0]) begin
                    errors = errors + 1;
                    $display("read %04x: %02x, expected %02x", a, rdata, d[7:0]);
                end
            end
            2: begin
                seen = frame_starts;
                wait (frame_starts != seen);
            end
            3: begin
                dump_done = 0; dump_req = 1;
                wait (dump_done);
                $display("frame dump %0d written (t = %0t)", dump_n - 1, $time);
            end
            4: begin
                $display("%0d ops, %0d errors", n, errors);
                if (errors == 0) $display("PASS"); else $display("FAIL");
                $finish;
            end
            endcase
        end
    end
    initial begin
        #400_000_000 $display("TIMEOUT locked=%b", locked); $finish;
    end
endmodule
