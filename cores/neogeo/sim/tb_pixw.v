// T143/T164: neo_pixw alone. A frame of known pixels goes in on dotst, the 128 bit words that leave on pw_en/pw_rdy are put back together and every one of the 320 pixels of
// every row is compared with what went in (pixel value = row * 1000 + column + 1). Prints PASS pixw or FAIL pixw with the first bad pixels and the count per column mod 8.
`timescale 1ns / 1ps
module tb_pixw;
    localparam LINES = 6;
    reg sys_clk = 0, ui_clk = 0, sys_rst = 1, ui_rst = 1;
    always #10 sys_clk = ~sys_clk;
    always #6  ui_clk  = ~ui_clk;
    reg dotst = 0;  reg [2:0] dc = 0;
    always @(posedge sys_clk) begin dc <= dc + 1; dotst <= (dc == 0); end
    reg hblank = 1, vblank = 1;
    reg [15:0] rgb = 0;
    wire quiet, frame_tgl, pw_en; wire [7:0] latest, seq; wire [15:0] overruns; wire [9:0] seen_cols, seen_lines;
    wire [26:0] pw_addr; wire [127:0] pw_data;
    reg pw_rdy = 0;
    neo_pixw #(.X0(0), .Y0(0), .COLS(320), .LINES(LINES)) dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .dotst(dotst), .rgb565(rgb), .hblank(hblank), .vblank(vblank), .quiet(quiet), .frame_tgl(frame_tgl),
        .latest(latest), .seq(seq), .overruns(overruns), .seen_cols(seen_cols), .seen_lines(seen_lines), .fb_base(32'h0), .fb_stride(32'h10000),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .pw_en(pw_en), .pw_addr(pw_addr), .pw_data(pw_data), .pw_rdy(pw_rdy));
    // the receiving side: pw_rdy a few clocks after pw_en rises
    reg [15:0] mem [0:(LINES*320)+7];
    integer w;
    integer k = 0;
    always @(posedge ui_clk) begin
        pw_rdy <= 0;
        if (pw_en && !pw_rdy) begin
            k <= k + 1;
            if (k == 3) begin
                k <= 0; pw_rdy <= 1;
                for (w = 0; w < 8; w = w + 1) mem[pw_addr + w] <= pw_data[16*w +: 16];
            end
        end else k <= 0;
    end
    integer row, col, bad, badmod [0:7], i, shown;
    initial begin
        for (i = 0; i < (LINES*320)+8; i = i + 1) mem[i] = 16'hDEAD;
        for (i = 0; i < 8; i = i + 1) badmod[i] = 0;
        repeat (16) @(posedge sys_clk); sys_rst = 0; ui_rst = 0;
        repeat (16) @(posedge dotst);
        vblank = 0; repeat (4) @(posedge dotst);          // vblank ends: a frame starts
        for (row = 0; row < LINES + 1; row = row + 1) begin
            hblank = 0;
            for (col = 0; col < 400; col = col + 1) begin
                rgb = row * 1000 + col + 1;
                @(posedge dotst);
            end
            hblank = 1;
            repeat (600) @(posedge dotst);                // the ui side has time to move the row
        end
        vblank = 1; repeat (200) @(posedge dotst);
        repeat (2000) @(posedge ui_clk);
        bad = 0; shown = 0;
        for (row = 0; row < LINES; row = row + 1)
            for (col = 0; col < 320; col = col + 1)
                if (mem[row*320 + col] !== row * 1000 + col + 1) begin
                    bad = bad + 1; badmod[col % 8] = badmod[col % 8] + 1;
                    if (shown < 6) begin $display("  row %0d col %0d: %0d, expected %0d", row, col, mem[row*320 + col], row * 1000 + col + 1); shown = shown + 1; end
                end
        $display("  bad pixels per column mod 8: %0d %0d %0d %0d %0d %0d %0d %0d  (frames %0d, overruns %0d)", badmod[0], badmod[1], badmod[2], badmod[3], badmod[4], badmod[5], badmod[6], badmod[7], seq, overruns);
        if (bad == 0 && seq != 0) $display("PASS pixw: %0d rows x 320 pixels, every pixel in its place", LINES);
        else $display("FAIL pixw: %0d of %0d pixels wrong", bad, LINES * 320);
        $finish;
    end
endmodule
