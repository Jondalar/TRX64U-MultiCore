// T90: the frame writer (c264_pixw) against a generator that has the TED's raster (456 dots, 312 lines, hblank 354..423, vblank lines
// 251..268, the colour on the dot clock) and a model of the host's e_ port. Checks (S5): every pixel of the 384 x 288 window of the
// frames lands in the right place of the right buffer, the LAST word of every row is written (the Minimig core's right-edge bug),
// the buffers go round, frame-done reports come once per frame, nothing is written while the machine is paused, and the dot
// strobe sits where the colour is settled.
`timescale 1ns / 1ps
module tb_c264_pixw;
    parameter integer X0 = 1, Y0 = 3;
    reg sys_clk = 0, ui_clk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #6.667 ui_clk = ~ui_clk;
    reg hold = 0;
    wire ted_en;
    c264_nco nco (.clk(sys_clk), .hold(hold), .en(ted_en));
    reg [1:0] ph = 0;
    always @(posedge sys_clk) if (ted_en) ph <= ph + 1;
    wire dotst = ted_en & (ph == 2'd2);

    // the TED's raster
    reg [8:0] h = 0, l = 0;
    reg hb = 1, vb = 1;
    reg [15:0] col = 0;
    function [15:0] f(input [19:0] d);
        f = d[15:0] ^ {d[19:16], 12'h5A5} ^ (d[7:0] * 8'd3);
    endfunction
    wire [8:0] hn = (h == 455) ? 9'd0 : h + 1;
    wire [8:0] ln = (h == 455) ? ((l == 311) ? 9'd0 : l + 1) : l;
    always @(posedge sys_clk) if (ted_en && ph == 2'd3) begin
        h <= hn; l <= ln;
        col <= f(ln * 456 + hn);
        hb <= (hn >= 354 && hn < 424);
        if (hn == 392) vb <= (ln >= 251 && ln < 269);
    end

    reg sys_rst = 1;
    wire quiet, frame_tgl;
    wire [7:0] latest, seq;
    wire [15:0] overruns;
    wire [26:0] e_addr;
    wire [2:0] e_cmd;
    wire e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;
    reg e_rdy = 1;
    c264_pixw #(.X0(X0), .Y0(Y0)) dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(dotst), .rgb565(col), .hblank(hb), .vblank(vb),
        .quiet(quiet), .frame_tgl(frame_tgl), .latest(latest), .seq(seq), .overruns(overruns),
        .fb_base(32'h0000_0000), .fb_stride(32'h0004_0000), .ui_clk(ui_clk), .ui_rst(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // the host's e_ port: a command is taken when e_en and e_rdy are both high
    reg [127:0] ddr [0:65535];
    reg [65535:0] seen;
    reg in_hold = 0;
    integer writes = 0, bad_addr = 0, wr_in_hold = 0;
    integer k;
    initial for (k = 0; k < 65536; k = k + 1) ddr[k] = 128'hDEAD;
    always @(posedge ui_clk) begin
        e_rdy <= ($random & 7) != 0;                // the port is busy one clock in eight
        if (e_en && e_rdy) begin
            if (e_cmd != 3'b000 || !e_wdf_wren || e_wdf_mask != 0) bad_addr = bad_addr + 1;
            if (e_addr[2:0] != 0 || e_addr[26:3] >= 65536 * 1) bad_addr = bad_addr + 1;
            else begin ddr[e_addr[26:3] >> 0] = e_wdf_data; seen[e_addr[18:3]] = 1; end
            writes = writes + 1;
            if (in_hold) wr_in_hold = wr_in_hold + 1;
        end
    end

    integer fails = 0;
    task check(input ok, input [8*100-1:0] what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
        end
    endtask

    integer frames_done = 0, tg_last = 0;
    reg [7:0] lat [0:7];
    always @(frame_tgl) begin
        frames_done = frames_done + 1;
    end

    integer r, c, b, bad, nwords, seen_all, w;
    reg [19:0] d;
    reg [15:0] got, exp;
    reg [127:0] word;
    integer fr_checked, first_buf;
    // after a frame is done, check the buffer named in `latest`
    task check_frame(input integer buf_i, input [8*100-1:0] tag);
        begin
            bad = 0;
            for (r = 0; r < 288; r = r + 1)
                for (c = 0; c < 384; c = c + 1) begin
                    d = ((269 + r + Y0) * 456 + 424 + c + X0) % (312 * 456);
                    exp = f(d);
                    word = ddr[(buf_i * 16'h4000) + (r * 48) + (c / 8)];
                    got = word >> ((c % 8) * 16);
                    if (got !== exp) begin
                        if (bad < 5) $display("     r %0d c %0d: %h, expected %h", r, c, got, exp);
                        bad = bad + 1;
                    end
                end
            check(bad == 0, tag);
        end
    endtask

    initial begin
        #200; sys_rst = 0;
        // frame 1 starts at the first vblank fall; wait for two reports so that the first complete frame is certain
        wait (frames_done >= 1);
        #1000;
        check(latest[7] == 1, "S5 the first complete frame is reported (valid)");
        $display("     first frame in buffer %0d, seq %0d", latest[1:0], seq);
        fr_checked = latest[1:0];
        first_buf = latest[1:0];
        check_frame(fr_checked, "S5 every pixel of frame 1 is in its place (incl. the last column)");
        check(overruns == 0, "S5 no line was late");
        // the right edge: the last word of every row of the buffer
        seen_all = 1;
        for (r = 0; r < 288; r = r + 1) for (w = 0; w < 48; w = w + 1) if (!seen[fr_checked * 16'h4000 + r * 48 + w]) seen_all = 0;
        check(seen_all == 1, "S5 all 48 words of all 288 rows were written (the last word of a row too)");
        // frame 2: pause in the middle, no writes while held, the frame is still right afterwards
        wait (frames_done >= 2);
        #1000;
        fr_checked = latest[1:0];
        check(seq == 2, "S5 two frames reported");
        check_frame(fr_checked, "S5 every pixel of frame 2 is in its place");
    end
    initial begin
        // the pause test runs in its own thread: 130 us into frame 3 (frame 2 was reported at about 20.05 ms intervals)
        wait (frames_done >= 2);
        #7000000;
        hold = 1;
        #50000;                                 // let the ui side drain whatever was handed over
        in_hold = 1;
        #400000;
        in_hold = 0;
        hold = 0;
        wait (frames_done >= 3);
        #1000;
        check(wr_in_hold == 0, "S6 no DDR2 write while the machine is held");
        check(seq == 3, "S5 three frames reported (the pause cost no frame)");
        check_frame(latest[1:0], "S6 the frame that was paused in the middle is right");
        check(latest[1:0] == ((first_buf + 2) % 4), "S5 the buffers go round (frame 3 two buffers after frame 1)");
        check(bad_addr == 0, "S5 every command is a plain write: cmd 0, data with the command, mask 0, 16 byte aligned");
        check(overruns == 0, "S5 still no late line");
        if (fails == 0) $display("PASS tb_c264_pixw"); else $display("FAILED tb_c264_pixw: %0d", fails);
        $finish;
    end
    initial begin #200000000; $display("FAILED tb_c264_pixw: timeout"); $finish; end
endmodule
